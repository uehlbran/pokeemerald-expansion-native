/*
 * iobench.c: the cost of reaching a GBA I/O register three ways, as expansion's
 * io_reg.h macros expand (REG_x is *(vu16 *)(REG_BASE + offset)):
 *   fixed:    REG_BASE 0x04000000, the Linux and x86_64 builds (needs the region there);
 *   symbol:   REG_BASE the address of a section of a slid image (PIE; RIP-relative on
 *             x86-64, adrp+add on AArch64);
 *   variable: REG_BASE = a run-time base read from memory plus 0x04000000.
 * Each loop does what an interrupt-time effect does: four writes and a read per
 * iteration, volatile as the hardware registers are. Build: see build_bench.sh.
 */
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <time.h>

uint32_t run_fixed(uint32_t n);
uint32_t run_symbol(uint32_t n);
uint32_t run_variable(uint32_t n);
extern char pen_gba_io[];
extern uintptr_t pen_gba_base;

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

int main(void)
{
    /* The fixed form needs the I/O page at 0x04000000, as gba_regions.ld gives it. */
    void *io = mmap((void *)0x04000000, 4096, PROT_READ | PROT_WRITE,
                    MAP_FIXED_NOREPLACE | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (io != (void *)0x04000000) { perror("mmap 0x04000000"); return 1; }
    pen_gba_base = (uintptr_t)pen_gba_io - 0x04000000;
    const uint32_t n = 200000000u;
    uint32_t (*fn[3])(uint32_t) = {run_fixed, run_symbol, run_variable};
    const char *name[3] = {"fixed", "symbol", "variable"};
    for (int rep = 0; rep < 3; rep++)
        for (int k = 0; k < 3; k++) {
            double t0 = now();
            uint32_t acc = fn[k](n);
            double t1 = now();
            printf("%-8s rep %d: %.3f ns per iteration (acc %u)\n", name[k], rep, (t1 - t0) * 1e9 / n, acc);
        }
    return 0;
}
