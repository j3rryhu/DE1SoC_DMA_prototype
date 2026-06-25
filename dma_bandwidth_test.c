#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>

#define LW_BRIDGE_BASE      0xFF200000
#define LW_BRIDGE_SPAN      0x00200000
#define DMA_BASE_OFFSET     0x00000000 

#define SDRAM_PHYS_BASE     0x1F800000
#define SDRAM_SPAN          0x01000000 

#define DMA_STATUS_REG      0
#define DMA_READ_ADDR_REG   1
#define DMA_WRITE_ADDR_REG  2
#define DMA_LENGTH_REG      3
#define DMA_CONTROL_REG     6

#define CTRL_WORD           (1 << 2)
#define CTRL_QUADWORD       (1 << 11)
#define CTRL_GO             (1 << 3)
#define CTRL_WCON           (1 << 9)

int main() {
    int fd;
    void *lw_virtual_base;
    void *sdram_virtual_base;
    volatile uint32_t *dma_ctrl;
    volatile uint32_t *sdram_array;

    if ((fd = open("/dev/mem", (O_RDWR | O_SYNC))) == -1) {
        printf("ERROR: Could not open /dev/mem.\n");
        return 1;
    }

    lw_virtual_base = mmap(NULL, LW_BRIDGE_SPAN, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, LW_BRIDGE_BASE);
    dma_ctrl = (volatile uint32_t *)(lw_virtual_base + DMA_BASE_OFFSET);

    sdram_virtual_base = mmap(NULL, SDRAM_SPAN, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, SDRAM_PHYS_BASE);
    sdram_array = (volatile uint32_t *)sdram_virtual_base;

    // =========================================================
    // THE DOWNGRADE: 1024 Bytes (256 integers)
    // =========================================================
    uint32_t transfer_bytes = 4; 
    uint32_t num_integers = transfer_bytes / 4;
    
    printf("Generating %u integers (1 KB) in physical RAM...\n", num_integers);
    for (uint32_t i = 0; i < num_integers; i++) {
        sdram_array[i] = 1; 
    }

    printf("Configuring DMA...\n");
    dma_ctrl[DMA_READ_ADDR_REG]  = SDRAM_PHYS_BASE; 
    dma_ctrl[DMA_WRITE_ADDR_REG] = 0x00000000;      
    dma_ctrl[DMA_LENGTH_REG]     = transfer_bytes;  

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Firing with QUADWORD because your hardware is still 128-bit
    dma_ctrl[DMA_CONTROL_REG] = (CTRL_QUADWORD | CTRL_WCON | CTRL_GO);

    printf("Done DMA config\n");
    // FIXED: Waiting for the BUSY bit (0x2) to drop to 0
    uint32_t dma_status = dma_ctrl[DMA_STATUS_REG];
    while (((dma_status >> 1) & 0x1) != 0) {
        // CPU waits here for hardware
        // printf("DMA status: %d\n", dma_status);
        dma_status = dma_ctrl[DMA_STATUS_REG];
    }

    printf("DMA finished\n");
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double time_taken = (end.tv_sec - start.tv_sec) + 1e-9 * (end.tv_nsec - start.tv_nsec);
    double megabytes_transferred = (double)transfer_bytes / (1024.0 * 1024.0);
    double bandwidth = megabytes_transferred / time_taken;

    printf("--- DMA Transfer Complete ---\n");
    printf("Bytes Moved: %u\n", transfer_bytes);
    printf("Time Taken : %f seconds\n", time_taken);
    printf("Est. Bandwidth: %.2f MB/s\n", bandwidth);

    munmap(lw_virtual_base, LW_BRIDGE_SPAN);
    munmap(sdram_virtual_base, SDRAM_SPAN);
    close(fd);

    return 0;
}