# DE1-SoC: DMA Bandwidth & TinyViT Compute Benchmarks

CPU (HPS Cortex-A9) baseline vs. f2h DMA data movement, for deciding whether an
FPGA accelerator is worthwhile.

---

## 1. DMA Memory Transfer (DDR → OCR via f2h bridge)

> Note: these two runs still include `printf` calls inside the timed region, so
> the reported MB/s is overhead-dominated, not true bus bandwidth. Kept here for
> the record.

### 512-byte transfer

```text
brgmodrst = 0x00000000 (bit2 f2h should be 0)
Generating 128 integers in physical RAM... 512 bytes
Configuring DMA...
status   = 0x00000000
readaddr = 0x1F800000
writeaddr= 0x00000000
length   = 0x00000200
Done DMA config
DMA Status: 17
status   = 0x00000011
readaddr = 0x1F800000
writeaddr= 0x00000000
length   = 0x00000000
DMA finished
--- DMA Transfer Complete ---
Bytes Moved   : 512
Time Taken    : 0.000069 s
Est. Bandwidth: 7.06 MB/s
```

### 4096-byte transfer

```text
brgmodrst = 0x00000000 (bit2 f2h should be 0)
Generating 1023 integers in physical RAM... 4096 bytes
Configuring DMA...
status   = 0x00000000
readaddr = 0x1F800000
writeaddr= 0x00000000
length   = 0x00000FFC
Done DMA config
DMA Status: 2
status   = 0x00000011
readaddr = 0x1F800000
writeaddr= 0x00000000
length   = 0x00000000
DMA finished
--- DMA Transfer Complete ---
Bytes Moved   : 4092
Time Taken    : 0.000081 s
Est. Bandwidth: 48.01 MB/s
```

**Status decode:** `0x11` = DONE (bit 0) + length-counter drained → transfer
completed cleanly. The rising "bandwidth" (7 → 48 MB/s) with size is fixed
overhead being amortized, not a real throughput ceiling.

---

## 2. Arithmetic Compute Benchmark (HPS Cortex-A9)

Representative TinyViT ops. GEMM dimensions shown as `[M×K]·[K×N]`.
Headline number is **median time per call**; throughput is secondary.

| op             | dtype | dimensions            | iters | median (ms) | min (ms) | mean (ms) | sd (ms) | throughput     |
|----------------|-------|-----------------------|------:|------------:|---------:|----------:|--------:|----------------|
| gemm_qkv_proj       | f32   | [196×320]·[320×960]   |     5 |     871.920 |  871.655 |   871.878 |   0.135 | 0.14 GFLOP/s   |
| gemm_attn_scores    | f32   | [196×320]·[320×196]   |     5 |     167.955 |  167.908 |   168.042 |   0.205 | 0.15 GFLOP/s   |
| gemm_attn_x_v       | f32   | [196×196]·[196×320]   |     5 |     169.545 |  169.406 |   169.582 |   0.146 | 0.15 GFLOP/s   |
| gemm_out_proj       | f32   | [196×320]·[320×320]   |     5 |     277.465 |  277.454 |   277.605 |   0.269 | 0.14 GFLOP/s   |
| gemm_mlp_fc1        | f32   | [196×320]·[320×1280]  |     5 |    1172.506 | 1171.493 |  1172.354 |   0.784 | 0.14 GFLOP/s   |
| gemm_mlp_fc2        | f32   | [196×1280]·[1280×320] |     5 |    1169.872 | 1169.760 |  1169.866 |   0.085 | 0.14 GFLOP/s   |
| gemm_mlp_fc1_i8     | i8    | [196×320]·[320×1280]  |     5 |     498.603 |  498.578 |   499.108 |   0.632 | 0.32 GOP/s     |
| softmax        | f32   | 196×196               |    30 |       6.568 |    6.548 |     6.585 |   0.046 | 5.85 Melem/s   |
| layernorm      | f32   | 196×320               |    30 |       2.321 |    2.298 |     2.327 |   0.027 | 27.02 Melem/s  |
| gelu           | f32   | 250880 elems          |    30 |      72.375 |   72.346 |    72.389 |   0.044 | 3.47 Melem/s   |
| residual_add   | f32   | 62720 elems           |    30 |       0.918 |    0.912 |     0.924 |   0.015 | 68.32 Melem/s  |

p.s. the matrix dimensions are examples and don't reflect the actual tinyvit case
---

## 3. Roofline Analysis — `mlp_fc1` (int8)

The offload decision hinges on one ratio: compute offloaded vs. bytes moved.

### Work

`[196×320]·[320×1280]` → M = 196, K = 320, N = 1280

- **MACs:** M·K·N = 80.3 M (≈ 160.6 M ops)
- **Measured:** 498 ms → 0.32 GOP/s

### Data (int8 operands)

| Tensor               | Size                | Bytes  |
|----------------------|---------------------|--------|
| A (activations)      | 196 × 320           | 61 KB  |
| B (weights)          | 320 × 1280          | 400 KB |
| C (output, int8)     | 196 × 1280          | 245 KB |
| C (output, int32)    | 196 × 1280 × 4      | 980 KB |

### Transfer time @ 230 MB/s int8 gemm

| What moves                              | Bytes  | Time @ 230 MB/s |
|-----------------------------------------|--------|-----------------|
| A + B in, C out (int8)                  | 723 KB | 3.1 ms          |
| A + B in, C out (int32)                 | 1.48 MB| 6.4 ms          |
| A in + C out, weights resident (int8)   | 314 KB | 1.4 ms          |

### Verdict

Moving the data costs **1.4–6.4 ms** against **498 ms** of compute — data
movement is **~1 %** of compute time. Arithmetic intensity ≈ 54 MACs/byte, so
this op is firmly **compute-bound**: 230 MB/s is nowhere near the bottleneck, and
offloading the matmul to the FPGA should pay off provided the FPGA MAC array is
large enough to beat the compute time.
