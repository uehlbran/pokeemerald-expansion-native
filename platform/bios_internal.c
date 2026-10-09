/*
 * bios_internal.c: helpers shared by the BIOS functions (bios_internal.h).
 *
 * The GBA BIOS computes in 32-bit ARM registers: MUL keeps the low 32 bits
 * of a product, ASR shifts in copies of the sign bit (GBATEK "ARM Opcodes:
 * Multiply and Multiply-Accumulate (MUL, MLA)", "ARM Opcodes: Data
 * Processing (ALU)"). C leaves signed overflow undefined and the right shift of a
 * negative value implementation-defined, so both are done here on uint32_t,
 * where every operation is defined, and converted back with memcpy
 * (int32_t is two's complement by definition). Range checks go through
 * transfer_bounds and name the BIOS function that was refused.
 */
#include "bios_internal.h"

#include <inttypes.h>
#include <stdio.h>

uintptr_t bios_address_of(const void *pointer)
{
    uintptr_t address = 0;
    memcpy(&address, &pointer, sizeof address);
    return address;
}

bool bios_range_ok(const char *function, const char *what, const void *start, size_t length,
                   enum transfer_access access)
{
    uintptr_t address = bios_address_of(start);
    if (transfer_bounds_ok(address, length, access)) {
        return true;
    }
    (void)fprintf(stderr, "bios: %s: %s %#" PRIxPTR " + %#zx refused (transfer_bounds)\n", function, what, address,
                  length);
    return false;
}

int32_t bios_s32_from_bits(uint32_t bits)
{
    int32_t value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

int32_t bios_asr(int32_t value, unsigned shift)
{
    uint32_t bits = (uint32_t)value >> shift;
    if (value < 0) {
        bits |= ~(UINT32_MAX >> shift);
    }
    return bios_s32_from_bits(bits);
}

int32_t bios_mul(int32_t a, int32_t b)
{
    return bios_s32_from_bits((uint32_t)a * (uint32_t)b);
}

bool bios_span(size_t count, size_t stride, size_t last_size, size_t *bytes)
{
    if (count == 0) {
        return false;
    }
    if (stride != 0 && count - 1 > (SIZE_MAX - last_size) / stride) {
        return false;
    }
    *bytes = (count - 1) * stride + last_size;
    return true;
}
