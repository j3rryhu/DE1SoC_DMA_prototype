#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>

// -------------------------------------------------------------------------
// Cyclone V HPS Memory Map Definitions
// -------------------------------------------------------------------------
// The Heavyweight HPS-to-FPGA (H2F) AXI Bridge physical base address
#define H2F_AXI_MASTER_BASE 0xC0000000 

// The amount of memory to map (4KB is the standard Linux page size)
#define HW_REGS_SPAN        0x00001000 
#define HW_REGS_MASK        (HW_REGS_SPAN - 1)

// Your custom adder's offset exactly as defined in Platform Designer
#define CUSTOM_ADDER_OFFSET 0x00000000

int main() {
    int fd;
    void *virtual_base;
    volatile uint32_t *adder_ptr;

    // 1. Open /dev/mem to access physical memory
    if ((fd = open("/dev/mem", (O_RDWR | O_SYNC))) == -1) {
        printf("ERROR: could not open \"/dev/mem\". Run as root (sudo).\n");
        return 1;
    }

    // 2. Map the physical bridge address into the Linux virtual memory space
    virtual_base = mmap(NULL, HW_REGS_SPAN, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, H2F_AXI_MASTER_BASE);
    
    if (virtual_base == MAP_FAILED) {
        printf("ERROR: mmap() failed...\n");
        close(fd);
        return 1;
    }

    // 3. Create a pointer to the exact memory address of your custom IP
    // Notice how we add the offset to the base address
    adder_ptr = (uint32_t *)(virtual_base + (CUSTOM_ADDER_OFFSET & HW_REGS_MASK));

    printf("Hardware successfully mapped!\n");
    printf("Physical Address: 0x%08X\n\n", H2F_AXI_MASTER_BASE + CUSTOM_ADDER_OFFSET);

    // =====================================================================
    // FUNCTIONALITY TEST
    // =====================================================================
    // Note: This assumes your Avalon-MM Verilog uses Register 0 and 1 for inputs, 
    // and Register 2 for the output. Adjust the array indices if your Verilog is different.
    
    printf("--- Running Basic Math Test ---\n");
    uint32_t input_A = 150;
    uint32_t input_B = 275;

    // Write to the hardware
    *(adder_ptr + 0) = input_A; // Writes to offset 0x00
    *(adder_ptr + 1) = input_B; // Writes to offset 0x04 (32-bit word = 4 bytes)

    // Read from the hardware
    uint32_t result = *(adder_ptr + 2); // Reads from offset 0x08
    printf("Wrote %u and %u to hardware.\n", input_A, input_B);
    printf("Hardware returned: %u\n\n", result);


    // =====================================================================
    // AXI BRIDGE BANDWIDTH TEST
    // =====================================================================
    printf("--- Running Heavyweight Bridge Bandwidth Test ---\n");
    
    struct timespec start, end;
    int iterations = 10000000; // 10 million writes
    
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // Blast data across the AXI bridge as fast as the CPU loop allows
    for (int i = 0; i < iterations; i++) {
        *(adder_ptr + 0) = i; 
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);

    // Calculate throughput
    double time_taken = (end.tv_sec - start.tv_sec) + 1e-9 * (end.tv_nsec - start.tv_nsec);
    double total_bytes = (double)iterations * sizeof(uint32_t);
    double mb_per_sec = (total_bytes / 1024.0 / 1024.0) / time_taken;

    printf("Wrote %d integers in %.4f seconds.\n", iterations, time_taken);
    printf("Estimated AXI Write Bandwidth: %.2f MB/s\n", mb_per_sec);

    // =====================================================================
    // CLEANUP
    // =====================================================================
    if (munmap(virtual_base, HW_REGS_SPAN) != 0) {
        printf("ERROR: munmap() failed...\n");
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}