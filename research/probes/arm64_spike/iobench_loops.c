/* iobench_loops.c: the three loops of iobench.c, alone so that they also
   compile for AArch64 with no libc headers (build_bench.sh). */
#include <stdint.h>

typedef volatile uint16_t vu16;
char pen_gba_io[0x400] __attribute__((aligned(4096)));
uintptr_t pen_gba_base;

#define LOOP(BASE)                                                   \
    for (uint32_t i = 0; i < n; i++) {                               \
        *(vu16 *)((BASE) + 0x10) = (uint16_t)i;          /* BG0HOFS */ \
        *(vu16 *)((BASE) + 0x12) = (uint16_t)(i >> 1);   /* BG0VOFS */ \
        *(vu16 *)((BASE) + 0x50) = 0x3F41;               /* BLDCNT  */ \
        *(vu16 *)((BASE) + 0x52) = (uint16_t)(i & 16);   /* BLDALPHA */ \
        acc += *(vu16 *)((BASE) + 0x06);                 /* VCOUNT  */ \
    }

__attribute__((noinline)) uint32_t run_fixed(uint32_t n)
{
    uint32_t acc = 0;
    LOOP((uintptr_t)0x04000000)
    return acc;
}
__attribute__((noinline)) uint32_t run_symbol(uint32_t n)
{
    uint32_t acc = 0;
    LOOP((uintptr_t)pen_gba_io)
    return acc;
}
__attribute__((noinline)) uint32_t run_variable(uint32_t n)
{
    uint32_t acc = 0;
    LOOP(pen_gba_base + 0x04000000)
    return acc;
}

