/*
 * crash_reboot.c: the reboot after a crash (crash_reboot.h has the contract
 * and the GBA behaviour it stands in for).
 *
 * Followed: GBATEK "BIOS Reset Functions" (RegisterRamReset keeps the last
 * 200h bytes of IWRAM), expansion's test/test_runner.c (what the runner
 * keeps there and how it resumes), POSIX for what a signal handler may call
 * (signal-safety(7): write in host_file, execve, _exit and raise in
 * host_fault and host_process).
 */
#include "crash_reboot.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "gba_memmap.h"
#include "host_fault.h"
#include "host_file.h"
#include "host_process.h"

_Static_assert(CRASH_REBOOT_PERSISTENT_ADDRESS >= GBA_IWRAM_BASE
                   && CRASH_REBOOT_PERSISTENT_ADDRESS + CRASH_REBOOT_PERSISTENT_SIZE <= GBA_IWRAM_BASE + GBA_IWRAM_SIZE,
               "the persistent state lies in IWRAM");

static const char hex_digits[] = "0123456789abcdef";

static struct {
    /* The reboot's command line: main's arguments without an earlier
     * CRASH_REBOOT_OPTION and its value, then the option and state_hex. */
    char *args[CRASH_REBOOT_MAX_ARGS + 1];
    char option[sizeof CRASH_REBOOT_OPTION];
    char state_hex[CRASH_REBOOT_STATE_HEX_LENGTH + 1];
    /* The persistent bytes when the process started (zero, or restored). */
    uint8_t started_with[CRASH_REBOOT_PERSISTENT_SIZE];
} reboot;

static const volatile uint8_t *persistent_bytes(void)
{
    return (const volatile uint8_t *)(uintptr_t)CRASH_REBOOT_PERSISTENT_ADDRESS;
}

/* The report line the handler writes, built in place on the handler's stack
 * and written with one call (host_file_write_from_handler: one write(2), or
 * one WriteFile on Windows) so that the lines of processes sharing stderr do
 * not interleave (the hydra runs up to 32). Async-signal-safe: no
 * allocation, no stdio. Room for the longest, the one that carries the state
 * for the parent (asserted below). */
#define REPORT_SIZE 1024u

struct report {
    char text[REPORT_SIZE];
    size_t length;
};

/* The report's texts: where the fault was, then one of the endings
 * (crash_reboot.h shows whole lines). */
static const char where_head[] = "crash_reboot: ";
static const char where_at[] = " at ";
static const char where_address[] = ", fault address ";
static const char where_end[] = ": ";
static const char no_reboot_text[] = "the test runner's persistent state is as the process started with; a reboot "
                                     "would crash again, so the process ends here\n";
static const char rebooting_text[] = "rebooting with the test runner's persistent state\n";
static const char restart_failed_text[] = "crash_reboot: starting the executable again failed; the process ends "
                                          "here\n";

/* "75", the exit code in the report. */
#define TEXT_OF(number) #number
#define DECIMAL_TEXT(number) TEXT_OF(number)

/* Where the handler may not restart (Windows), what the report says before
 * the state, which ends the line (crash_reboot.h). */
static const char ending_for_the_parent[] = "ending with exit code " DECIMAL_TEXT(
    CRASH_REBOOT_EXIT_CODE) " for the parent to start the program again with " CRASH_REBOOT_OPTION " ";

/* Every report fits, so that report_add never cuts one short (a report cut
 * short would lose the state and the newline). */
#define TEXT_LENGTH(text) (sizeof(text) - 1u)
#define HEX_TEXT_LENGTH (2u + 2u * sizeof(uintptr_t)) /* "0x" and a pointer's digits */
#define WHERE_LENGTH                                                                                 \
    (TEXT_LENGTH(where_head) + (HOST_FAULT_NAME_SIZE - 1u) + TEXT_LENGTH(where_at) + HEX_TEXT_LENGTH \
     + TEXT_LENGTH(where_address) + HEX_TEXT_LENGTH + TEXT_LENGTH(where_end))
_Static_assert(WHERE_LENGTH + TEXT_LENGTH(no_reboot_text) <= REPORT_SIZE, "the report without a reboot fits");
_Static_assert(WHERE_LENGTH + TEXT_LENGTH(rebooting_text) <= REPORT_SIZE, "the report of a reboot fits");
_Static_assert(TEXT_LENGTH(restart_failed_text) <= REPORT_SIZE, "the report of a failed restart fits");
_Static_assert(WHERE_LENGTH + TEXT_LENGTH(ending_for_the_parent) + CRASH_REBOOT_STATE_HEX_LENGTH + 1u <= REPORT_SIZE,
               "the report for the parent, the longest, fits with its state and newline");

/* Appends text; the assertions above keep it within REPORT_SIZE, which the
 * bound here only guards. */
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

/* stderr as the handler writes to it, resolved by crash_reboot_init: the descriptor the
 * C runtime's stderr has, which is not 2 for a program that had no stderr and
 * opened its log on it (console.h). */
static host_handler_file report_file = HOST_HANDLER_FILE_NONE;

/* Best-effort: there is nothing to do if stderr is gone. */
static void report_write(const struct report *report)
{
    (void)host_file_write_from_handler(report_file, report->text, report->length);
}

/* "crash_reboot: signal 11 at 0x..., fault address 0x...: " (Windows:
 * "exception 0xC0000005", host_fault_name) */
static void report_where(struct report *report, const struct host_fault *fault)
{
    char name[HOST_FAULT_NAME_SIZE];
    host_fault_name(fault, name);
    report_add(report, where_head);
    report_add(report, name);
    report_add(report, where_at);
    report_add_hex(report, host_fault_register(fault, HOST_FAULT_PC));
    report_add(report, where_address);
    report_add_hex(report, host_fault_address(fault));
    report_add(report, where_end);
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

/* The crash handler. When it returns, host_fault lets the fault end the
 * process. */
static void on_crash(const struct host_fault *fault)
{
    struct report report = {.length = 0};
    report_where(&report, fault);
    if (!state_changed_since_start()) {
        report_add(&report, no_reboot_text);
        report_write(&report);
        return;
    }
    encode_state();
    if (!host_process_restart_is_handler_safe()) {
        report_add(&report, ending_for_the_parent);
        report_add(&report, reboot.state_hex);
        report_add(&report, "\n");
        report_write(&report);
        host_process_end(CRASH_REBOOT_EXIT_CODE);
    }
    report_add(&report, rebooting_text);
    report_write(&report);
    (void)host_process_restart(reboot.args);
    report.length = 0;
    report_add(&report, restart_failed_text);
    report_write(&report);
}

/* The option and its value, appended to the arguments the process started with. */
#define CRASH_REBOOT_APPENDED_ARGS 2u

static bool build_args(int argc, char *const argv[])
{
    size_t count = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], CRASH_REBOOT_OPTION) == 0) {
            i++; /* and its value */
            continue;
        }
        if (count + CRASH_REBOOT_APPENDED_ARGS >= CRASH_REBOOT_MAX_ARGS)
            return false;
        reboot.args[count++] = argv[i];
    }
    memcpy(reboot.option, CRASH_REBOOT_OPTION, sizeof reboot.option);
    reboot.args[count++] = reboot.option;
    reboot.args[count++] = reboot.state_hex;
    reboot.args[count] = NULL;
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
    report_file = host_file_for_handler(fileno(stderr));
    if (host_fault_install_crash(on_crash) != 0)
        return CRASH_REBOOT_ERR_SIGNAL;
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
        (void)fprintf(stderr, "crash_reboot: %s needs %llu hex digits\n", CRASH_REBOOT_OPTION,
                      (unsigned long long)CRASH_REBOOT_STATE_HEX_LENGTH);
        return CRASH_REBOOT_ERR_STATE;
    }
    for (size_t i = 0; i < CRASH_REBOOT_PERSISTENT_SIZE; i++) {
        int high = hex_value(state_hex[2 * i]);
        int low = hex_value(state_hex[2 * i + 1]);
        if (high < 0 || low < 0) {
            (void)fprintf(stderr, "crash_reboot: %s: '%.2s' at %llu is not a lower-case hex byte\n",
                          CRASH_REBOOT_OPTION, &state_hex[2 * i], (unsigned long long)i * 2u);
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
    host_fault_uninstall_crash();
}
