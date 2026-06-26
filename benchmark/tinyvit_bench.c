/* tinyvit_bench.c
 *
 * CPU baseline microbenchmarks for the representative math ops in a
 * TinyViT-style vision transformer, intended to run on the HPS ARM core
 * under Linux (Cyclone V / Arria 10 / Stratix 10 SoC), so you can compare
 * against an FPGA accelerator end-to-end.
 *
 * Ops covered (the ones that dominate transformer inference):
 *   - GEMM / matmul        (float32 and int8)   -> QKV proj, attn scores,
 *                                                  attn*V, out proj, MLP fc1/fc2
 *   - Softmax (row-wise)   (float32)            -> attention
 *   - LayerNorm            (float32)            -> pre/post norm
 *   - GELU                 (float32)            -> MLP activation
 *   - Residual add         (float32)            -> skip connections
 *
 * Why GEMM is float32 + int8 but the nonlinear ops are float32 only:
 * in real quantized inference the matmuls run in int8 (int32 accumulate)
 * because that is where the speed/area win is, while softmax / layernorm /
 * gelu are almost always kept in float or int32 for numerical stability.
 * Benchmarking a fake int8 softmax would not reflect any real deployment,
 * so it is deliberately left out.
 *
 * Timing: clock_gettime(CLOCK_MONOTONIC). Each op is warmed up, then timed
 * over many iterations; we report min / median / mean / stddev and a
 * throughput figure (GFLOP/s for GEMM, Melem/s for elementwise ops).
 *
 * Build: see Makefile. IMPORTANT: build with -O2/-O3, otherwise the CPU
 * baseline is artificially slow and your "speedup" is meaningless.
 *
 * Usage:
 *   ./tinyvit_bench            # human-readable table
 *   ./tinyvit_bench --csv      # CSV to stdout (pipe into a spreadsheet)
 */

#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Timing                                                              */
/* ------------------------------------------------------------------ */
static inline double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ------------------------------------------------------------------ */
/* Deterministic RNG (xorshift32) so runs are reproducible            */
/* ------------------------------------------------------------------ */
static uint32_t g_rng = 0x12345678u;
static inline uint32_t xs32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}
static inline float frand(void) {        /* roughly [-0.5, 0.5) */
    return (float)(xs32() >> 8) * (1.0f / 16777216.0f) - 0.5f;
}
static inline int8_t i8rand(void) {      /* roughly [-64, 63] */
    return (int8_t)((int)(xs32() & 0x7F) - 64);
}

/* ------------------------------------------------------------------ */
/* Stats                                                              */
/* ------------------------------------------------------------------ */
typedef struct { double mn, med, mean, sd; } stats_t;

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static stats_t compute_stats(double *t, int n) {
    stats_t s = {0, 0, 0, 0};
    if (n <= 0) return s;
    qsort(t, n, sizeof(double), cmp_double); /* in place; order not reused */
    s.mn  = t[0];
    s.med = (n & 1) ? t[n / 2] : 0.5 * (t[n / 2 - 1] + t[n / 2]);
    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += t[i];
    s.mean = sum / n;
    double v = 0.0;
    for (int i = 0; i < n; i++) { double d = t[i] - s.mean; v += d * d; }
    s.sd = sqrt(v / n);
    return s;
}

/* volatile sink to stop the compiler from deleting "unused" results */
volatile double g_sink = 0.0;

/* ------------------------------------------------------------------ */
/* Ops                                                                */
/* ------------------------------------------------------------------ */

/* ---- float32 GEMM, naive ijk ordering (cache-UNfriendly on B) ---- */
static void gemm_f32_ijk(const float *A, const float *B, float *C,
                         int M, int N, int K) {
    for (int i = 0; i < M; i++)
        for (int j = 0; j < N; j++) {
            float s = 0.0f;
            for (int k = 0; k < K; k++)
                s += A[i * K + k] * B[k * N + j];
            C[i * N + j] = s;
        }
}

/* ---- float32 GEMM, ikj ordering (cache-friendly, vectorizable) ---- */
static void gemm_f32_ikj(const float *A, const float *B, float *C,
                         int M, int N, int K) {
    memset(C, 0, (size_t)M * N * sizeof(float));
    for (int i = 0; i < M; i++)
        for (int k = 0; k < K; k++) {
            float a = A[i * K + k];
            const float *Brow = &B[k * N];
            float *Crow = &C[i * N];
            for (int j = 0; j < N; j++)
                Crow[j] += a * Brow[j];
        }
}

/* ---- int8 GEMM, int32 accumulate, ikj ordering ---- */
static void gemm_i8_ikj(const int8_t *A, const int8_t *B, int32_t *C,
                        int M, int N, int K) {
    memset(C, 0, (size_t)M * N * sizeof(int32_t));
    for (int i = 0; i < M; i++)
        for (int k = 0; k < K; k++) {
            int32_t a = A[i * K + k];
            const int8_t *Brow = &B[k * N];
            int32_t *Crow = &C[i * N];
            for (int j = 0; j < N; j++)
                Crow[j] += a * (int32_t)Brow[j];
        }
}

/* ---- softmax over the last dim, float32, row-wise (numerically stable) ---- */
static void softmax_f32(float *X, int rows, int cols) {
    for (int r = 0; r < rows; r++) {
        float *row = &X[r * cols];
        float m = row[0];
        for (int c = 1; c < cols; c++) if (row[c] > m) m = row[c];
        float sum = 0.0f;
        for (int c = 0; c < cols; c++) { row[c] = expf(row[c] - m); sum += row[c]; }
        float inv = 1.0f / sum;
        for (int c = 0; c < cols; c++) row[c] *= inv;
    }
}

/* ---- layernorm over the feature dim, float32 (gamma=1, beta=0) ---- */
static void layernorm_f32(float *X, int rows, int cols, float eps) {
    for (int r = 0; r < rows; r++) {
        float *row = &X[r * cols];
        float mean = 0.0f;
        for (int c = 0; c < cols; c++) mean += row[c];
        mean /= cols;
        float var = 0.0f;
        for (int c = 0; c < cols; c++) { float d = row[c] - mean; var += d * d; }
        var /= cols;
        float inv = 1.0f / sqrtf(var + eps);
        for (int c = 0; c < cols; c++) row[c] = (row[c] - mean) * inv;
    }
}

/* ---- GELU, float32, tanh approximation (the common ViT variant) ---- */
static void gelu_f32(float *X, int n) {
    const float c = 0.7978845608028654f;   /* sqrt(2/pi) */
    const float a = 0.044715f;
    for (int i = 0; i < n; i++) {
        float x = X[i];
        float inner = c * (x + a * x * x * x);
        X[i] = 0.5f * x * (1.0f + tanhf(inner));
    }
}

/* ---- residual add, float32: Y += X ---- */
static void add_f32(float *Y, const float *X, int n) {
    for (int i = 0; i < n; i++) Y[i] += X[i];
}

/* ------------------------------------------------------------------ */
/* Reporting                                                          */
/* ------------------------------------------------------------------ */
static int g_csv = 0;

static void print_header(void) {
    if (g_csv) {
        printf("op,dtype,dims,iters,median_ms,min_ms,mean_ms,stddev_ms,throughput,unit\n");
    } else {
        /* Time-first: the median ms per call is the headline number.     */
        printf("\n%-14s %-6s %-22s %6s  %11s %10s %10s %9s   %s\n",
               "op", "dtype", "dimensions", "iters",
               "median(ms)", "min(ms)", "mean(ms)", "sd(ms)", "[throughput]");
        printf("%.*s\n", 108,
               "----------------------------------------------------------"
               "------------------------------------------------------");
    }
}

static void print_row(const char *op, const char *dt, const char *dims,
                      int iters, stats_t s, double thr, const char *unit) {
    if (g_csv) {
        printf("%s,%s,%s,%d,%.6f,%.6f,%.6f,%.6f,%.4f,%s\n",
               op, dt, dims, iters,
               s.med * 1e3, s.mn * 1e3, s.mean * 1e3, s.sd * 1e3, thr, unit);
    } else {
        /* median time leads; throughput trails in brackets as a hint     */
        printf("%-14s %-6s %-22s %6d  %11.3f %10.3f %10.3f %9.3f   [%.2f %s]\n",
               op, dt, dims, iters,
               s.med * 1e3, s.mn * 1e3, s.mean * 1e3, s.sd * 1e3, thr, unit);
    }
}

/* run a timed loop into a caller-provided times[] buffer */
#define TIME_LOOP(times, iters, warmup, CODE)                 \
    do {                                                      \
        for (int _w = 0; _w < (warmup); _w++) { CODE; }       \
        for (int _i = 0; _i < (iters); _i++) {                \
            double _t0 = now_sec();                           \
            CODE;                                             \
            double _t1 = now_sec();                           \
            (times)[_i] = _t1 - _t0;                          \
        }                                                     \
    } while (0)

/* ------------------------------------------------------------------ */
/* Benchmark drivers                                                  */
/* ------------------------------------------------------------------ */
#define MAX_ITERS 1000

static void bench_gemm_f32(const char *tag, int M, int N, int K,
                           int iters, int warmup, int use_ikj) {
    float *A = malloc((size_t)M * K * sizeof(float));
    float *B = malloc((size_t)K * N * sizeof(float));
    float *C = malloc((size_t)M * N * sizeof(float));
    if (!A || !B || !C) { fprintf(stderr, "OOM gemm_f32\n"); exit(1); }
    for (long i = 0; i < (long)M * K; i++) A[i] = frand();
    for (long i = 0; i < (long)K * N; i++) B[i] = frand();

    static double times[MAX_ITERS];
    if (use_ikj) TIME_LOOP(times, iters, warmup, gemm_f32_ikj(A, B, C, M, N, K));
    else         TIME_LOOP(times, iters, warmup, gemm_f32_ijk(A, B, C, M, N, K));

    double cs = 0.0; for (long i = 0; i < (long)M * N; i++) cs += C[i];
    g_sink += cs;

    stats_t s = compute_stats(times, iters);
    char dims[64]; snprintf(dims, sizeof dims, "[%dx%d]*[%dx%d]", M, K, K, N);
    double gflops = (2.0 * M * N * K) / s.med / 1e9;
    print_row(tag, "f32", dims, iters, s, gflops, "GFLOP/s");

    free(A); free(B); free(C);
}

static void bench_gemm_i8(const char *tag, int M, int N, int K,
                          int iters, int warmup) {
    int8_t *A = malloc((size_t)M * K);
    int8_t *B = malloc((size_t)K * N);
    int32_t *C = malloc((size_t)M * N * sizeof(int32_t));
    if (!A || !B || !C) { fprintf(stderr, "OOM gemm_i8\n"); exit(1); }
    for (long i = 0; i < (long)M * K; i++) A[i] = i8rand();
    for (long i = 0; i < (long)K * N; i++) B[i] = i8rand();

    static double times[MAX_ITERS];
    TIME_LOOP(times, iters, warmup, gemm_i8_ikj(A, B, C, M, N, K));

    long cs = 0; for (long i = 0; i < (long)M * N; i++) cs += C[i];
    g_sink += (double)cs;

    stats_t s = compute_stats(times, iters);
    char dims[64]; snprintf(dims, sizeof dims, "[%dx%d]*[%dx%d]", M, K, K, N);
    double gops = (2.0 * M * N * K) / s.med / 1e9;
    print_row(tag, "i8", dims, iters, s, gops, "GOP/s");

    free(A); free(B); free(C);
}

static void bench_softmax(int rows, int cols, int iters, int warmup) {
    float *X = malloc((size_t)rows * cols * sizeof(float));
    float *X0 = malloc((size_t)rows * cols * sizeof(float));
    if (!X || !X0) { fprintf(stderr, "OOM softmax\n"); exit(1); }
    for (long i = 0; i < (long)rows * cols; i++) X0[i] = frand() * 6.0f;

    static double times[MAX_ITERS];
    /* refresh input each iter so we always softmax raw logits, not a result */
    TIME_LOOP(times, iters, warmup,
              { memcpy(X, X0, (size_t)rows * cols * sizeof(float));
                softmax_f32(X, rows, cols); });

    double cs = 0.0; for (long i = 0; i < (long)rows * cols; i++) cs += X[i];
    g_sink += cs;

    stats_t s = compute_stats(times, iters);
    char dims[64]; snprintf(dims, sizeof dims, "%dx%d", rows, cols);
    double melems = ((double)rows * cols) / s.med / 1e6;
    print_row("softmax", "f32", dims, iters, s, melems, "Melem/s");

    free(X); free(X0);
}

static void bench_layernorm(int rows, int cols, int iters, int warmup) {
    float *X = malloc((size_t)rows * cols * sizeof(float));
    float *X0 = malloc((size_t)rows * cols * sizeof(float));
    if (!X || !X0) { fprintf(stderr, "OOM layernorm\n"); exit(1); }
    for (long i = 0; i < (long)rows * cols; i++) X0[i] = frand() * 3.0f;

    static double times[MAX_ITERS];
    TIME_LOOP(times, iters, warmup,
              { memcpy(X, X0, (size_t)rows * cols * sizeof(float));
                layernorm_f32(X, rows, cols, 1e-5f); });

    double cs = 0.0; for (long i = 0; i < (long)rows * cols; i++) cs += X[i];
    g_sink += cs;

    stats_t s = compute_stats(times, iters);
    char dims[64]; snprintf(dims, sizeof dims, "%dx%d", rows, cols);
    double melems = ((double)rows * cols) / s.med / 1e6;
    print_row("layernorm", "f32", dims, iters, s, melems, "Melem/s");

    free(X); free(X0);
}

static void bench_gelu(int n, int iters, int warmup) {
    float *X = malloc((size_t)n * sizeof(float));
    float *X0 = malloc((size_t)n * sizeof(float));
    if (!X || !X0) { fprintf(stderr, "OOM gelu\n"); exit(1); }
    for (long i = 0; i < n; i++) X0[i] = frand() * 6.0f;

    static double times[MAX_ITERS];
    TIME_LOOP(times, iters, warmup,
              { memcpy(X, X0, (size_t)n * sizeof(float)); gelu_f32(X, n); });

    double cs = 0.0; for (long i = 0; i < n; i++) cs += X[i];
    g_sink += cs;

    stats_t s = compute_stats(times, iters);
    char dims[64]; snprintf(dims, sizeof dims, "%d elems", n);
    double melems = (double)n / s.med / 1e6;
    print_row("gelu", "f32", dims, iters, s, melems, "Melem/s");

    free(X); free(X0);
}

static void bench_add(int n, int iters, int warmup) {
    float *Y = malloc((size_t)n * sizeof(float));
    float *X = malloc((size_t)n * sizeof(float));
    if (!Y || !X) { fprintf(stderr, "OOM add\n"); exit(1); }
    for (long i = 0; i < n; i++) { Y[i] = frand(); X[i] = frand(); }

    static double times[MAX_ITERS];
    TIME_LOOP(times, iters, warmup, add_f32(Y, X, n));

    double cs = 0.0; for (long i = 0; i < n; i++) cs += Y[i];
    g_sink += cs;

    stats_t s = compute_stats(times, iters);
    char dims[64]; snprintf(dims, sizeof dims, "%d elems", n);
    double melems = (double)n / s.med / 1e6;
    print_row("residual_add", "f32", dims, iters, s, melems, "Melem/s");

    free(Y); free(X);
}

/* ------------------------------------------------------------------ */
/* Shapes                                                             */
/*                                                                    */
/* >>> EDIT THESE to match YOUR TinyViT variant. <<<                  */
/* The numbers below are example/typical transformer-block shapes,    */
/* NOT authoritative TinyViT constants. Plug in your real             */
/* embed dim (D), token count (TOK = H*W of the stage feature map),   */
/* and MLP ratio so the baseline reflects the actual workload.        */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--csv") == 0) g_csv = 1;

    /* ---- representative transformer-block dimensions (EXAMPLE) ---- */
    const int TOK = 196;   /* tokens / sequence length (e.g. 14x14)   */
    const int D   = 320;   /* embedding dimension                     */
    const int MLP = 4 * D; /* MLP hidden dimension (ratio 4)          */

    if (!g_csv) {
        printf("TinyViT-style CPU op baseline  (HPS ARM / Linux)\n");
        printf("Example shapes: TOK=%d  D=%d  MLP=%d   "
               "(EDIT in source to match your variant)\n", TOK, D, MLP);
        printf("Throughput uses the MEDIAN time. Build with -O2/-O3 for a fair baseline.\n");
    }
    print_header();

    /* Iteration counts kept small for the Cortex-A9 (DE1-SoC).         */
    /* Bump GI up if you want tighter stats and can spare the time.     */
    const int GI = 5,  GW = 1;     /* GEMM:        iters, warmup        */
    const int EI = 30, EW = 3;     /* elementwise: iters, warmup        */

    /* The six GEMMs of one transformer block, f32 only to keep it fast.
       Each row prints as [M x K] * [K x N].                            */
    /* QKV projection:    [TOK x D] * [D x 3D]                          */
    bench_gemm_f32("gemm_qkv_proj",    TOK, 3 * D, D, GI, GW, 1);
    /* Attention scores:  [TOK x D] * [D x TOK]                         */
    bench_gemm_f32("gemm_attn_scores", TOK, TOK,   D, GI, GW, 1);
    /* Attention x V:     [TOK x TOK] * [TOK x D]                       */
    bench_gemm_f32("gemm_attn_x_v",    TOK, D,   TOK, GI, GW, 1);
    /* Output projection: [TOK x D] * [D x D]                           */
    bench_gemm_f32("gemm_out_proj",    TOK, D,     D, GI, GW, 1);
    /* MLP fc1:           [TOK x D] * [D x MLP]                         */
    bench_gemm_f32("gemm_mlp_fc1",     TOK, MLP,   D, GI, GW, 1);
    /* MLP fc2:           [TOK x MLP] * [MLP x D]                       */
    bench_gemm_f32("gemm_mlp_fc2",     TOK, D,   MLP, GI, GW, 1);

    /* One int8 GEMM kept as a datatype reference (mlp_fc1 is heaviest, */
    /* so it shows the f32-vs-i8 gap most clearly).                     */
    bench_gemm_i8 ("gemm_mlp_fc1_i8",  TOK, MLP,   D, GI, GW);

    /* Nonlinear / elementwise ops.                                     */
    bench_softmax  (TOK, TOK, EI, EW);
    bench_layernorm(TOK, D,   EI, EW);
    bench_gelu     (TOK * MLP, EI, EW);   /* GELU runs on MLP hidden */
    bench_add      (TOK * D,   EI, EW);

    if (!g_csv) {
        printf("\nNotes:\n");
        printf("  * GEMM throughput = 2*M*N*K / median_time (GFLOP/s for f32, GOP/s for i8).\n");
        printf("  * Compare qkv_proj (ikj) vs qkv_proj_ijk: same math, the ijk gap is pure\n");
        printf("    cache effect -- benchmark the version your real kernel actually uses.\n");
        printf("  * softmax/layernorm/gelu are f32 on purpose; they stay float even in\n");
        printf("    int8-quantized inference. Only the GEMMs have an int8 variant.\n");
        printf("  * This is COMPUTE only. For the accelerator comparison, separately time\n");
        printf("    DMA-in + DMA-out and report end-to-end, not kernel-vs-kernel.\n");
        printf("  * sink=%g (ignore; prevents dead-code elimination)\n", g_sink);
    }
    return 0;
}