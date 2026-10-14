/*
 * crash_reboot.h: the reboot that follows a crash, keeping the test runner's
 * persistent state.
 *
 * Stands in for what happens on the GBA (as mgba-rom-test runs it) when the
 * game jumps somewhere it should not, say to NULL: the CPU ends up at the
 * BIOS's reset vector, the BIOS starts the cartridge again, and crt0's
 * RegisterRamReset(RESET_ALL) clears RAM except the last 200h bytes of IWRAM
 * (GBATEK "BIOS Reset Functions", RegisterRamReset bit 1: "Clear 32K
 * On-Chip WRAM (except last 200h bytes)"). Expansion's test runner keeps
 * gPersistentTestRunnerState in 03007F00h-03007FFFh (ld_script_test.ld,
 * IWRAM_PERSISTENT, 100h bytes) so that after such a reboot it can report
 * the test that crashed as CRASH and carry on with the next one
 * (test/test_runner.c, STATE_INIT; docs/research/test-runner-protocol.md).
 *
 * Natively a crash is a fault that reaches host_fault.h's crash handler:
 * SIGSEGV, SIGBUS, SIGILL, SIGFPE (a divide error arm_division cannot
 * complete as the GBA would), SIGABRT (the C library's own checks); on
 * Windows an exception no handler took. The handler crash_reboot_init
 * installs reports the crash on stderr, one line that names the fault
 * ("crash_reboot: signal 11 at 0x..., fault address 0x...: ..."; Windows:
 * "exception 0xC0000005"), and the program starts again with its own
 * arguments, any earlier --persistent-state dropped, plus
 *     --persistent-state <512 hex digits>
 * (the 100h bytes at 03007F00h), which crash_reboot_restore puts back before
 * AgbMain. On Linux the handler restarts it in the same process
 * (host_process_restart: execve of /proc/self/exe), using async-signal-safe
 * calls and memory set up beforehand only.
 *
 * On Windows a handler must not start a process (docs/DECISIONS.md, "A crash
 * on Windows ends the process for its parent to start again"), so the
 * reboot is the parent's. The report ends with the state, on one line:
 *     crash_reboot: exception 0xC0000005 at 0x..., fault address 0x...: ending
 *     with exit code 75 for the parent to start the program again with
 *     --persistent-state <512 hex digits>
 * and the process exits with CRASH_REBOOT_EXIT_CODE (75). The parent (the
 * Windows test driver) then starts the program again with the same
 * arguments, any earlier --persistent-state dropped, plus the option and the
 * digits from the last line of stderr that begins "crash_reboot: ". Without
 * such a parent the run ends there. abort() and an overflow of the battle
 * runner's stack never reach the handler on Windows (host_fault.h,
 * host_windows_fault.c).
 *
 * A crash with the persistent bytes unchanged since the process started
 * (zero, or what crash_reboot_restore put there) would reboot into the same
 * crash forever, as on the GBA, so the handler reports it and lets the fault
 * end the process (Windows: with the exception's code, not
 * CRASH_REBOOT_EXIT_CODE); so does a failed restart.
 *
 * Single-threaded.
 */
#ifndef CRASH_REBOOT_H
#define CRASH_REBOOT_H

#include <stddef.h>
#include <stdint.h>

/* ld_script_test.ld: IWRAM_PERSISTENT, ORIGIN 0x3007F00, LENGTH 0x100. */
#define CRASH_REBOOT_PERSISTENT_ADDRESS UINT32_C(0x03007F00)
#define CRASH_REBOOT_PERSISTENT_SIZE 0x100u

/* The option the reboot adds, and the length of its value: two hex digits
 * per persistent byte. */
#define CRASH_REBOOT_OPTION "--persistent-state"
#define CRASH_REBOOT_STATE_HEX_LENGTH ((size_t)CRASH_REBOOT_PERSISTENT_SIZE * 2u)

/* The most arguments the command line may have, the reboot's two included. */
#define CRASH_REBOOT_MAX_ARGS 64u

/* The exit code of a process that crashed on Windows and asks its parent to
 * start it again with the state its report ends with (above): sysexits.h's
 * EX_TEMPFAIL, "try again", which nothing else here exits with (main's are 0,
 * 1 and 64, game_hooks' 70, the test runner's 0 to 2). A decimal literal: the
 * report spells it out. */
#define CRASH_REBOOT_EXIT_CODE 75

enum crash_reboot_error {
    CRASH_REBOOT_OK = 0,
    CRASH_REBOOT_ERR_ARGS = -1,   /* no arguments, or more than CRASH_REBOOT_MAX_ARGS with the reboot's */
    CRASH_REBOOT_ERR_SIGNAL = -2, /* host_fault could not install the handler */
    CRASH_REBOOT_ERR_STATE = -3   /* crash_reboot_restore: not 512 hex digits */
};

/* Prepares the reboot's command line from argc/argv (main's; the strings
 * must outlive the module, as main's do) and installs the crash handler
 * (host_fault_install_crash, which also unblocks the crash signals: a
 * handler's mask survives execve, so a rebooted process starts with its
 * signal blocked). Returns CRASH_REBOOT_OK, or an error after a report on
 * stderr with nothing installed. */
int crash_reboot_init(int argc, char *const argv[]);

/* Puts the bytes a reboot passed (the value of CRASH_REBOOT_OPTION) back at
 * 03007F00h and remembers them as the state the process started with.
 * Returns CRASH_REBOOT_OK, or CRASH_REBOOT_ERR_STATE (reported) leaving the
 * region untouched. */
int crash_reboot_restore(const char *state_hex);

/* Restores the signals' default actions and removes the alternate stack
 * (host_fault_uninstall_crash). Safe at any time, and twice. */
void crash_reboot_shutdown(void);

#endif /* CRASH_REBOOT_H */
