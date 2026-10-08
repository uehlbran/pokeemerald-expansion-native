/* psg_noise.c: placeholder so the tests build and fail before the implementation. */
#include "psg_internal.h"

uint16_t psg_lfsr_seed(enum psg_lfsr_width width)
{
    (void)width;
    return 0;
}

uint16_t psg_lfsr_clock(uint16_t lfsr, enum psg_lfsr_width width)
{
    (void)width;
    return lfsr;
}
