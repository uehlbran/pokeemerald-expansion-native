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
 * Natively a crash is a signal: SIGSEGV (a NULL call, a bad pointer), SIGBUS,
 * SIGILL (src/libisagbprn.c's 0xEFFF breakpoint), SIGFPE (a divide error
 * arm_division cannot complete as the GBA would), SIGABRT (the C library's
 * stack-protector and fortify checks: memory corruption the GBA does not
 * notice). With
 * crash_reboot_init, each of them runs a handler (host_fault.h's crash
 * handler, on an alternate stack) that copies the 100h bytes at 03007F00h
 * into the command line and starts the executable again
 * (host_process_restart; Linux: execve of /proc/self/exe) with the process's
 * own arguments plus
 *     --persistent-state <512 hex digits>
 * (any earlier --persistent-state dropped), so the same process (same pid,
 * same stdout, same patched executable) starts again from main;
 * crash_reboot_restore then puts the bytes back at 03007F00h before
 * AgbMain. The handler uses only async-signal-safe calls (execve, sigaction,
 * raise, write) and memory it set up beforehand.
 *
 * On Windows a crash is an exception that no handler took (an access
 * violation, an illegal instruction, a stack overflow, a divide error
 * arm_division passed on, ...), and the handler runs in the process's
 * unhandled-exception filter. There is no exec, and the filter starts no
 * process: creating one allocates, from a heap the crash may have corrupted
 * (docs/DECISIONS.md, "A crash on Windows ends the process for its parent to
 * start again"). The filter encodes the persistent bytes, writes the report,
 * which ends with them, to stderr, and ends the process with exit code
 * CRASH_REBOOT_EXIT_CODE:
 *     crash_reboot: exception 0xC0000005 at 0x..., fault address 0x...: ending
 *     with exit code 75 for the parent to start the program again with
 *     --persistent-state <512 hex digits>
 * on one line. The reboot is the parent's, the program that started this
 * one and waits for it (in the suite, test_runner/native-rom-test): on exit
 * code 75 it starts the program again with the same arguments, any earlier
 * --persistent-state dropped, plus the option and the digits from the last
 * line of stderr that begins "crash_reboot: ". A run on Windows with no such
 * parent ends at a crash with exit code 75 and no reboot. Two crashes end the
 * run without a report or a reboot on Windows: abort(), which is not an
 * exception there (the C runtime raises SIGABRT in its own signal table and
 * exits with status 3), and an overflow of the battle runner's stack
 * (game_hooks), which runs into the image's read-only headers, where the
 * system cannot write the exception's records, so no handler runs. The
 * report names a Windows fault by its exception code in hexadecimal
 * ("crash_reboot: exception 0xC0000005 at ..."), a Linux one by its signal
 * ("crash_reboot: signal 11 at ...").
 *
 * A crash with the persistent bytes unchanged since the process started
 * (zero, or what crash_reboot_restore put there) would reboot into the same
 * crash forever, as on the GBA; the handler does not reboot then: it reports
 * and lets the fault end the process (Windows: with the exception's code,
 * not CRASH_REBOOT_EXIT_CODE). The same happens if the restart fails.
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
 * signal blocked). Returns
 * CRASH_REBOOT_OK, or an error after a report on stderr with nothing
 * installed. */
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
