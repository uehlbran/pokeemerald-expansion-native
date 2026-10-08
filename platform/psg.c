/* psg.c: placeholder so the tests build and fail before the implementation. */
#include "psg.h"

int psg_init(uint32_t output_sample_rate_hz)
{
    (void)output_sample_rate_hz;
    return PSG_OK;
}

void psg_shutdown(void)
{
}

size_t psg_generate(int16_t *stereo_out, size_t frames)
{
    (void)stereo_out;
    (void)frames;
    return 0;
}
