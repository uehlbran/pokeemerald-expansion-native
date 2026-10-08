/* bios_decompress.c: stub (tests first). */
#include "bios.h"

int bios_lz77_uncomp_bounded(const void *src, size_t src_size, void *dest, enum bios_write_width width)
{
    (void)src; (void)src_size; (void)dest; (void)width;
    return BIOS_OK;
}
void LZ77UnCompWram(const uint32_t *src, void *dest) { (void)src; (void)dest; }
void LZ77UnCompVram(const uint32_t *src, void *dest) { (void)src; (void)dest; }
int bios_rl_uncomp_bounded(const void *src, size_t src_size, void *dest, enum bios_write_width width)
{
    (void)src; (void)src_size; (void)dest; (void)width;
    return BIOS_OK;
}
void RLUnCompWram(const uint32_t *src, void *dest) { (void)src; (void)dest; }
void RLUnCompVram(const uint32_t *src, void *dest) { (void)src; (void)dest; }
int bios_huff_uncomp_bounded(const void *src, size_t src_size, void *dest)
{
    (void)src; (void)src_size; (void)dest;
    return BIOS_OK;
}
void HuffUnComp(const uint32_t *src, void *dest) { (void)src; (void)dest; }
