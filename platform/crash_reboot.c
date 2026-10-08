/*
 * crash_reboot.c: the reboot after a crash (crash_reboot.h has the contract
 * and the GBA behaviour it stands in for).
 *
 * Followed: GBATEK "BIOS Reset Functions" (RegisterRamReset keeps the last
 * 200h bytes of IWRAM), expansion's test/test_runner.c (what the runner
 * keeps there and how it resumes), POSIX for what a signal handler may call
 * (signal-safety(7): execve, sigaction, raise, write).
 */
#include "crash_reboot.h"

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "gba_memmap.h"

_Static_assert(CRASH_REBOOT_PERSISTENT_ADDRESS >= GBA_IWRAM_BASE
                   && CRASH_REBOOT_PERSISTENT_ADDRESS + CRASH_REBOOT_PERSISTENT_SIZE <= GBA_IWRAM_BASE + GBA_IWRAM_SIZE,
               "the persistent state lies in IWRAM");

extern char **environ;

/* The signals that mean the game crashed (crash_reboot.h). */
static const int crash_signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE };
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

/* write(2) of a constant message; async-signal-safe. Best-effort: there is
 * nothing to do if stderr is gone. */
static void say(const char *message)
{
    size_t length = strlen(message);
    while (length > 0) {
        ssize_t written = write(STDERR_FILENO, message, length);
        if (written <= 0)
            return;
        message += written;
        length -= (size_t)written;
    }
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
        reboot.state_hex[2 * i] = hex_digits[bytes[i] >> 4];
        reboot.state_hex[2 * i + 1] = hex_digits[bytes[i] & 0x0Fu];
    }
    reboot.state_hex[CRASH_REBOOT_STATE_HEX_LENGTH] = '\0';
}

/* Lets the signal do what it does without the handler: kill the process. */
static void die_of(int signal_number)
{
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = SIG_DFL;
    (void)sigemptyset(&action.sa_mask);
    (void)sigaction(signal_number, &action, NULL);
    (void)raise(signal_number); /* pending until the handler returns; a fault would recur anyway */
}

static void on_crash(int signal_number)
{
    if (!state_changed_since_start()) {
        say("crash_reboot: the game crashed with the test runner's persistent state as it started; "
            "a reboot would crash again, so the process ends here\n");
        die_of(signal_number);
        return;
    }
    encode_state();
    say("crash_reboot: the game crashed; rebooting with the test runner's persistent state\n");
    (void)execve(CRASH_REBOOT_EXECUTABLE, reboot.args, environ);
    say("crash_reboot: execve of " CRASH_REBOOT_EXECUTABLE " failed; the process ends here\n");
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
    stack_t stack = { .ss_sp = signal_stack, .ss_size = sizeof signal_stack, .ss_flags = 0 };
    if (sigaltstack(&stack, NULL) != 0) {
        perror("crash_reboot: sigaltstack");
        return false;
    }
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = on_crash;
    action.sa_flags = SA_ONSTACK;
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
        fprintf(stderr, "crash_reboot: crash_reboot_init: need 1 to %u arguments, with the reboot's two\n",
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
        fprintf(stderr, "crash_reboot: %s needs %u hex digits\n", CRASH_REBOOT_OPTION,
                CRASH_REBOOT_STATE_HEX_LENGTH);
        return CRASH_REBOOT_ERR_STATE;
    }
    for (size_t i = 0; i < CRASH_REBOOT_PERSISTENT_SIZE; i++) {
        int high = hex_value(state_hex[2 * i]);
        int low = hex_value(state_hex[2 * i + 1]);
        if (high < 0 || low < 0) {
            fprintf(stderr, "crash_reboot: %s: '%.2s' at %zu is not a lower-case hex byte\n", CRASH_REBOOT_OPTION,
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
    stack_t no_stack = { .ss_sp = NULL, .ss_size = 0, .ss_flags = SS_DISABLE };
    (void)sigaltstack(&no_stack, NULL); /* fails only while running on it, which shutdown never does */
    reboot.installed = false;
}
