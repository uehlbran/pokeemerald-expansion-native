/*
 * libgcc_division.h: the C division operators, compiled for the GBA's ARM7TDMI
 * once per ARM procedure-call ABI, for the record of what libgcc returns
 * where x86 traps (libgcc_division.c).
 *
 * libgcc_division_ops.c is compiled twice (Makefile): with expansion's own C
 * flags (-mthumb -mabi=apcs-gnu, as the GBA build compiles src/), where GCC
 * calls __udivsi3, __umodsi3, __divsi3, __modsi3, __udivdi3, __umoddi3,
 * __divdi3 and __moddi3; and with the EABI (-mabi=aapcs, arm-none-eabi-gcc's
 * default), where it calls __aeabi_uidiv, __aeabi_uidivmod, __aeabi_idiv,
 * __aeabi_idivmod, __aeabi_uldivmod and __aeabi_ldivmod (a remainder comes
 * from the divmod routine's second result).
 */
#ifndef LIBGCC_DIVISION_H
#define LIBGCC_DIVISION_H

#include <stdint.h>

/* n / d and n % d for each operand type of the game's C. */
struct division_ops {
    uint32_t (*udiv32)(uint32_t n, uint32_t d);
    uint32_t (*umod32)(uint32_t n, uint32_t d);
    int32_t (*sdiv32)(int32_t n, int32_t d);
    int32_t (*smod32)(int32_t n, int32_t d);
    uint64_t (*udiv64)(uint64_t n, uint64_t d);
    uint64_t (*umod64)(uint64_t n, uint64_t d);
    int64_t (*sdiv64)(int64_t n, int64_t d);
    int64_t (*smod64)(int64_t n, int64_t d);
};

/* libgcc_division_ops.c compiled as expansion's C is (-mabi=apcs-gnu). */
extern const struct division_ops game_division_ops;

/* libgcc_division_ops.c compiled for the EABI (-mabi=aapcs). */
extern const struct division_ops eabi_division_ops;

#endif /* LIBGCC_DIVISION_H */
