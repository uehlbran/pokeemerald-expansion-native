/* dma.c: DMA channels 0 to 3. Stub: the tests in test/test_dma.c come first. */
#include <stddef.h>

#include "dma.h"

int dma_init(void (*raise)(enum gba_irq))
{
    return raise == NULL ? DMA_ERR_NO_RAISE : DMA_OK;
}

void dma_shutdown(void)
{
}

int dma_set(unsigned channel, const void *src, void *dest, uint32_t control)
{
    (void)channel;
    (void)src;
    (void)dest;
    (void)control;
    return DMA_OK;
}

void dma_stop(unsigned channel)
{
    (void)channel;
}

unsigned dma_run_timing(uint16_t timing)
{
    (void)timing;
    return 0;
}
