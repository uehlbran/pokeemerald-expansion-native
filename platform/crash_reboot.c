/*
 * crash_reboot.c: the reboot after a crash (crash_reboot.h has the contract
 * and the GBA behaviour it stands in for).
 *
 * Followed: GBATEK "BIOS Reset Functions" (RegisterRamReset keeps the last
 * 200h bytes of IWRAM), expansion's test/test_runner.c (what the runner
 * keeps there and how it resumes), POSIX for what a signal handler may call
 * (signal-safety(7): execve, sigaction, raise, write).
 */
#define _GNU_SOURCE /* REG_EIP, REG_RIP: the interrupted program counter */
#include "crash_reboot.h"

#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#include "gba_memmap.h"

_Static_assert(CRASH_REBOOT_PERSISTENT_ADDRESS >= GBA_IWRAM_BASE
                   && CRASH_REBOOT_PERSISTENT_ADDRESS + CRASH_REBOOT_PERSISTENT_SIZE <= GBA_IWRAM_BASE + GBA_IWRAM_SIZE,
               "the persistent state lies in IWRAM");

/* The signals that mean the game crashed (crash_reboot.h). */
static const int crash_signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
#define CRASH_SIGNAL_COUNT (sizeof crash_signals / sizeof crash_signals[0])

static const char hex_digits[] = "0123456789abcdef";

static struct {
    bool installed;
    /* The reboot's command line: main's arguments without an earlier
     * CRASH_REBOOT_OPTION and its value, then the option and state_hex. */
    char *args[CRASH_REBOOT_MAX_ARGS + 1];
    char option[sizeof CRASH_REBOOT_OPTION];
    char state_hex[CRASH_REBOOT_STATE_HEX_LENGTH + 1];
    /* The persistent bytes when the process started (zero, or restored). */
    uint8_t started_with[CRASH_REBOOT_PERSISTENT_SIZE];
} reboot;

static _Alignas(16) unsigned char signal_stack[CRASH_REBOOT_SIGNAL_STACK_SIZE];

static const volatile uint8_t *persistent_bytes(void)
{
    return (const volatile uint8_t *)(uintptr_t)CRASH_REBOOT_PERSISTENT_ADDRESS;
}

/* The report line the handler writes, built in place and written with one
 * write(2) so that the lines of processes sharing stderr do not interleave
 * (the hydra runs up to 32). Async-signal-safe: no allocation, no stdio. */
#define REPORT_SIZE 256u
#define DECIMAL_BASE 10u /* numbers are written and read in decimal */

struct report {
    char text[REPORT_SIZE];
    size_t length;
};

static void report_add(struct report *report, const char *text)
{
    while (*text != '\0' && report->length < REPORT_SIZE)
        report->text[report->length++] = *text++;
}

#define HEX_DIGIT_BITS 4u
#define HEX_DIGIT_MASK 0x0Fu

/* "0x" and as many hex digits as a pointer has. */
static void report_add_hex(struct report *report, uintptr_t value)
{
    char text[2 + 2 * sizeof value + 1];
    size_t length = 0;
    text[length++] = '0';
    text[length++] = 'x';
    for (size_t shift = CHAR_BIT * sizeof value; shift > 0; shift -= HEX_DIGIT_BITS)
        text[length++] = hex_digits[(value >> (shift - HEX_DIGIT_BITS)) & HEX_DIGIT_MASK];
    text[length] = '\0';
    report_add(report, text);
}

static void report_add_unsigned(struct report *report, unsigned value)
{
    char text[3 * sizeof value + 1];
    size_t length = sizeof text - 1;
    text[length] = '\0';
    do {
        text[--length] = (char)('0' + value % DECIMAL_BASE);
        value /= DECIMAL_BASE;
    } while (value > 0 && length > 0);
    report_add(report, &text[length]);
}

/* Best-effort: there is nothing to do if stderr is gone. */
static void report_write(const struct report *report)
{
    const char *text = report->text;
    size_t length = report->length;
    while (length > 0) {
        ssize_t written = write(STDERR_FILENO, text, length);
        if (written <= 0)
            return;
        text += written;
        length -= (size_t)written;
    }
}

/* The interrupted program counter. Flagged for 64-bit: a pointer-sized
 * integer, as the kernel stores it. */
static uintptr_t interrupted_pc(const void *context)
{
    const ucontext_t *interrupted = context;
#if defined(__x86_64__)
    return (uintptr_t)interrupted->uc_mcontext.gregs[REG_RIP];
#elif defined(__i386__)
    return (uintptr_t)interrupted->uc_mcontext.gregs[REG_EIP];
#else
    (void)interrupted;
    return 0;
#endif
}

/* "crash_reboot: signal 11 at 0x...; fault address 0x...: " */
static void report_where(struct report *report, int signal_number, const siginfo_t *info, const void *context)
{
    report_add(report, "crash_reboot: signal ");
    report_add_unsigned(report, (unsigned)signal_number);
    report_add(report, " at ");
    report_add_hex(report, interrupted_pc(context));
    report_add(report, ", fault address ");
    report_add_hex(report, (uintptr_t)info->si_addr);
    report_add(report, ": ");
}

static bool state_changed_since_start(void)
{
    const volatile uint8_t *bytes = persistent_bytes();
    for (size_t i = 0; i < CRASH_REBOOT_PERSISTENT_SIZE; i++) {
        if (bytes[i] != reboot.started_with[i])
            return true;
    }
    return false;
}

static void encode_state(void)
{
    const volatile uint8_t *bytes = persistent_bytes();
    for (size_t i = 0; i < CRASH_REBOOT_PERSISTENT_SIZE; i++) {
        reboot.state_hex[2 * i] = hex_digits[bytes[i] >> HEX_DIGIT_BITS];
        reboot.state_hex[2 * i + 1] = hex_digits[bytes[i] & HEX_DIGIT_MASK];
    }
    reboot.state_hex[CRASH_REBOOT_STATE_HEX_LENGTH] = '\0';
}

/* Lets the signal do what it does without the handler: kill the process.
 * Relies on the handler being installed without SA_NODEFER: the signal is
 * blocked while the handler runs, so the raise stays pending and is
 * delivered, with the default action, when the handler returns. */
static void die_of(int signal_number)
{
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = SIG_DFL;
    (void)sigemptyset(&action.sa_mask);
    (void)sigaction(signal_number, &action, NULL);
    (void)raise(signal_number); /* pending until the handler returns; a fault would recur anyway */
}

/* sigaction's sa_sigaction type fixes these parameters as non-const. */
// cppcheck-suppress constParameterCallback
static void on_crash(int signal_number, siginfo_t *info, void *context)
{
    struct report report = {.length = 0};
    report_where(&report, signal_number, info, context);
    if (!state_changed_since_start()) {
        report_add(&report, "the test runner's persistent state is as the process started with; a reboot "
                            "would crash again, so the process ends here\n");
        report_write(&report);
        die_of(signal_number);
        return;
    }
    encode_state();
    report_add(&report, "rebooting with the test runner's persistent state\n");
    report_write(&report);
    (void)execve(CRASH_REBOOT_EXECUTABLE, reboot.args, environ);
    report.length = 0;
    report_add(&report, "crash_reboot: execve of " CRASH_REBOOT_EXECUTABLE " failed; the process ends here\n");
    report_write(&report);
    die_of(signal_number);
}

static bool build_args(int argc, char *const argv[])
{
    size_t count = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], CRASH_REBOOT_OPTION) == 0) {
            i++; /* and its value */
            continue;
        }
        if (count + 2 >= CRASH_REBOOT_MAX_ARGS)
            return false;
        reboot.args[count++] = argv[i];
    }
    memcpy(reboot.option, CRASH_REBOOT_OPTION, sizeof reboot.option);
    reboot.args[count++] = reboot.option;
    reboot.args[count++] = reboot.state_hex;
    reboot.args[count] = NULL;
    return true;
}

static bool install_handlers(void)
{
    stack_t stack = {.ss_sp = signal_stack, .ss_size = sizeof signal_stack, .ss_flags = 0};
    if (sigaltstack(&stack, NULL) != 0) {
        perror("crash_reboot: sigaltstack");
        return false;
    }
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_sigaction = on_crash;
    action.sa_flags = SA_ONSTACK | SA_SIGINFO;
    sigset_t crashes;
    if (sigemptyset(&action.sa_mask) != 0 || sigemptyset(&crashes) != 0) {
        perror("crash_reboot: sigemptyset");
        return false;
    }
    for (size_t i = 0; i < CRASH_SIGNAL_COUNT; i++) {
        if (sigaction(crash_signals[i], &action, NULL) != 0 || sigaddset(&crashes, crash_signals[i]) != 0) {
            perror("crash_reboot: sigaction");
            return false;
        }
    }
    if (sigprocmask(SIG_UNBLOCK, &crashes, NULL) != 0) {
        perror("crash_reboot: sigprocmask");
        return false;
    }
    return true;
}

int crash_reboot_init(int argc, char *const argv[])
{
    crash_reboot_shutdown();
    if (argc < 1 || argv == NULL || !build_args(argc, argv)) {
        (void)fprintf(stderr, "crash_reboot: crash_reboot_init: need 1 to %u arguments, with the reboot's two\n",
                      CRASH_REBOOT_MAX_ARGS);
        return CRASH_REBOOT_ERR_ARGS;
    }
    const volatile uint8_t *bytes = persistent_bytes();
    for (size_t i = 0; i < CRASH_REBOOT_PERSISTENT_SIZE; i++)
        reboot.started_with[i] = bytes[i];
    reboot.installed = true; /* so that shutdown undoes a partial install */
    if (!install_handlers()) {
        crash_reboot_shutdown();
        return CRASH_REBOOT_ERR_SIGNAL;
    }
    return CRASH_REBOOT_OK;
}

static int hex_value(char digit)
{
    const char *found = digit == '\0' ? NULL : strchr(hex_digits, digit);
    return found == NULL ? -1 : (int)(found - hex_digits);
}

int crash_reboot_restore(const char *state_hex)
{
    uint8_t bytes[CRASH_REBOOT_PERSISTENT_SIZE];
    if (state_hex == NULL || strlen(state_hex) != CRASH_REBOOT_STATE_HEX_LENGTH) {
        (void)fprintf(stderr, "crash_reboot: %s needs %zu hex digits\n", CRASH_REBOOT_OPTION,
                      CRASH_REBOOT_STATE_HEX_LENGTH);
        return CRASH_REBOOT_ERR_STATE;
    }
    for (size_t i = 0; i < CRASH_REBOOT_PERSISTENT_SIZE; i++) {
        int high = hex_value(state_hex[2 * i]);
        int low = hex_value(state_hex[2 * i + 1]);
        if (high < 0 || low < 0) {
            (void)fprintf(stderr, "crash_reboot: %s: '%.2s' at %zu is not a lower-case hex byte\n", CRASH_REBOOT_OPTION,
                          &state_hex[2 * i], 2 * i);
            return CRASH_REBOOT_ERR_STATE;
        }
        bytes[i] = (uint8_t)(high << 4 | low);
    }
    volatile uint8_t *region = (volatile uint8_t *)(uintptr_t)CRASH_REBOOT_PERSISTENT_ADDRESS;
    for (size_t i = 0; i < CRASH_REBOOT_PERSISTENT_SIZE; i++)
        region[i] = bytes[i];
    memcpy(reboot.started_with, bytes, sizeof bytes);
    return CRASH_REBOOT_OK;
}

void crash_reboot_shutdown(void)
{
    if (!reboot.installed)
        return;
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = SIG_DFL;
    (void)sigemptyset(&action.sa_mask);
    for (size_t i = 0; i < CRASH_SIGNAL_COUNT; i++)
        (void)sigaction(crash_signals[i], &action, NULL); /* restoring a default cannot fail for these */
    stack_t no_stack = {.ss_sp = NULL, .ss_size = 0, .ss_flags = SS_DISABLE};
    (void)sigaltstack(&no_stack, NULL); /* fails only while running on it, which shutdown never does */
    reboot.installed = false;
}
