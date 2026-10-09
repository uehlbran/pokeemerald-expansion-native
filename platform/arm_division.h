/*
 * arm_division.h: integer division as the GBA's code does it, where it
 * differs from x86: by zero, and INT_MIN by -1.
 *
 * Stands in for libgcc's division routines in the GBA build. The ARM7TDMI
 * has no divide instruction (GBATEK "ARM Instruction Summary"), so GCC
 * compiles every / and % on the GBA to a call: __udivsi3, __umodsi3,
 * __divsi3 or __modsi3 (expansion builds with -mabi=apcs-gnu). Read from
 * the libgcc linked into expansion's test ELF (gcc-arm-none-eabi 13.2.1):
 * with a zero divisor each branches to __aeabi_idiv0, which returns, and
 * the call returns
 *   unsigned n / 0 and n % 0:  0xFFFFFFFF if n != 0, else 0;
 *   signed n / 0 and n % 0:    INT32_MAX if n > 0, INT32_MIN if n < 0, else 0;
 * and INT32_MIN / -1 is INT32_MIN, INT32_MIN % -1 is 0 (computed on the
 * magnitudes, no trap). Expansion divides by zero in places the GBA
 * survives (src/battle_script_commands.c ComputeBallShakeOdds for a ball
 * that cannot catch, src/battle_ai_util.c GetHealthPercentage for an empty
 * battler, src/battle_anim_electric.c AnimSparkElectricityFlashing_Step).
 *
 * x86's div and idiv raise a divide error in both cases, which Linux
 * delivers as SIGFPE. arm_division_init installs a SIGFPE handler that
 * decodes the faulting instruction, and for a 32-bit div or idiv (opcode
 * F7h, ModRM reg field 6 or 7, any operand form, segment prefixes CS, DS, ES
 * or SS) by zero, or idiv of INT32_MIN by -1, writes the results above into
 * EAX (quotient) and EDX (remainder) and resumes after the instruction; the
 * compiler uses one or the other, and libgcc gives the same value for both.
 * Any other SIGFPE goes to the handler that was installed before (the crash
 * reboot in test-runner mode), or kills the process as without this module.
 * The handler is async-signal-safe (it only reads the instruction and its
 * operand and edits the saved registers).
 *
 * i386 only: the 64-bit build reports ARM_DIVISION_ERR_64_BIT (flagged for
 * the 64-bit milestone: REX prefixes, 64-bit operands and RIP-relative
 * operands would need decoding). Single-threaded.
 */
#ifndef ARM_DIVISION_H
#define ARM_DIVISION_H

enum arm_division_error {
    ARM_DIVISION_OK = 0,
    ARM_DIVISION_ERR_SIGNAL = -1, /* sigaction failed */
    ARM_DIVISION_ERR_64_BIT = -2  /* a 64-bit build (above) */
};

/* Installs the SIGFPE handler, keeping the one it replaces to pass other
 * SIGFPEs on to. Returns ARM_DIVISION_OK, or an error after a report on
 * stderr with nothing installed. Calling it again first restores the
 * previous handler. */
int arm_division_init(void);

/* Restores the SIGFPE handler that was installed before init. Safe at any
 * time, and twice. */
void arm_division_shutdown(void);

#endif /* ARM_DIVISION_H */
