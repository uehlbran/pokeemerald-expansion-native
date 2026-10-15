/*
 * arm_division.h: integer division as the GBA's code does it, where it
 * differs from x86: by zero, and the most negative value by -1.
 *
 * Stands in for libgcc's division routines in the GBA build. The ARM7TDMI
 * has no divide instruction (GBATEK "ARM Instruction Summary"), so GCC
 * compiles every / and % on the GBA to a call; expansion builds with
 * -mabi=apcs-gnu, so the calls are to __udivsi3, __umodsi3, __divsi3 and
 * __modsi3, and for 64-bit operands __udivdi3, __umoddi3, __divdi3 and
 * __moddi3 (not the EABI's __aeabi_* routines, whose results differ).
 * Measured with expansion's libgcc (gcc-arm-none-eabi 13.2.1) under
 * qemu-arm, platform/arm_division_libgcc/libgcc_division.txt, by zero:
 *   32-bit unsigned n / 0 and n % 0:  0xFFFFFFFF if n != 0, else 0;
 *   32-bit signed n / 0 and n % 0:    INT32_MAX if n > 0, INT32_MIN if n < 0, else 0;
 *   64-bit unsigned n / 0:  2^(k+1) - 1, all ones from k = 63, k being the
 *                           bit length of n (so 0 / 0 is 1);
 *   64-bit unsigned n % 0:  n mod 2^(64-k) (n itself while k <= 32);
 *   64-bit signed:          the unsigned results for |n|, negated if n < 0;
 * and INT_MIN / -1 is INT_MIN, INT_MIN % -1 is 0, at both widths (computed
 * on the magnitudes, no trap). Expansion divides by zero where the GBA
 * survives (src/battle_script_commands.c ComputeBallShakeOdds,
 * src/battle_ai_util.c GetHealthPercentage, src/battle_anim_electric.c
 * AnimSparkElectricityFlashing_Step).
 *
 * x86's div and idiv raise a divide error in both cases, which the host
 * delivers as a fault (Linux and macOS: SIGFPE; host_fault.h).
 * arm_division_init installs a divide-error handler. It decodes the faulting
 * DIV or IDIV (F7 /6 or /7, any operand form, flat segment prefixes, REX on
 * x86-64), writes libgcc's quotient and remainder into the accumulator and
 * data registers at the operand's width (32-bit results are zero-extended on
 * x86-64), and resumes after the instruction.
 *
 * The 8- and 16-bit forms (F6 /6 and /7; F7 with the operand-size prefix)
 * are what clang makes of a C division whose operands it knows fit that
 * width (a u8 by a u8, a u8 by an s16; GCC divides at the int's 32 bits).
 * They get libgcc's rule at the instruction's width: all ones or zero, or
 * the most positive or negative value by the dividend's sign, or zero (AL and
 * AH at 8 bits, AX and DX at 16; the registers' other bits as they were). The
 * GBA's 32-bit result does not fit there, so the narrow one keeps what a
 * program can ask of it, its sign and whether it is zero, and an unsigned
 * one's low bits; the signed value itself differs (0x7FFF where the GBA has
 * INT32_MAX). The one such division by zero a gate reaches tests its
 * remainder for zero (src/battle_anim_electric.c,
 * AnimSparkElectricityFlashing; docs/DECISIONS.md, "Clang's narrow
 * divisions"). Anything it cannot decode (see
 * decode() in arm_division.c) and any other SIGFPE is reported with its
 * program counter on one line of stderr and passed to the handler installed
 * before (the crash reboot in test-runner mode), or kills the process as
 * without this module. The handler is async-signal-safe (it only reads the
 * instruction and its operand and edits the saved registers, through
 * host_fault). i386 and x86-64 only (anything else does not compile).
 * Single-threaded.
 */
#ifndef ARM_DIVISION_H
#define ARM_DIVISION_H

enum arm_division_error {
    ARM_DIVISION_OK = 0,
    ARM_DIVISION_ERR_SIGNAL = -1 /* host_fault could not install the handler */
};

/* Installs the divide-error handler (host_fault), keeping the one it
 * replaces to pass other SIGFPEs on to. Returns ARM_DIVISION_OK, or an error
 * after a report on stderr with nothing installed. Calling it again first
 * restores the previous handler. */
int arm_division_init(void);

/* Restores the divide-error handler that was installed before init. Safe at
 * any time, and twice. */
void arm_division_shutdown(void);

#endif /* ARM_DIVISION_H */
