/* bios_internal.c: stub (tests first). */
#include "bios_internal.h"

uintptr_t bios_address_of(const void *pointer)
{
    uintptr_t address = 0;
    memcpy(&address, &pointer, sizeof address);
    return address;
}

bool bios_range_ok(const char *function, const char *what, const void *start, size_t length,
                   enum transfer_access access)
{
    (void)function;
    (void)what;
    return transfer_bounds_ok(bios_address_of(start), length, access);
}

int32_t bios_s32_from_bits(uint32_t bits)
{
    int32_t value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

int32_t bios_asr(int32_t value, unsigned shift)
{
    (void)shift;
    return value;
}

int32_t bios_mul(int32_t a, int32_t b)
{
    (void)b;
    return a;
}
