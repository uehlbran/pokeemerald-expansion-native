/*
 * ptest.h: the platform layer's unit test framework, header only, no
 * dependency beyond the C library. Why in-house: docs/RESEARCH.md, "Test
 * framework for the platform layer". Usage: platform/README.md.
 *
 *   PTEST(name) { ... }     defines a test (a void function) and registers it.
 *   PT_ASSERT(cond), PT_ASSERT_EQ_U32/_INT/_PTR/_STR(actual, expected),
 *   PT_ASSERT_MEM_EQ(actual, expected, nbytes), PT_FAIL(message):
 *                           on failure record file, line, test name, the
 *                           expression and the values (truncated to
 *                           PTEST_MESSAGE_SIZE), mark the test failed and
 *                           return from the enclosing void function. Every
 *                           argument is evaluated exactly once. A test reports
 *                           its first failure: one in a helper returns from
 *                           the helper, and later ones do not replace it.
 *   PTEST_MAIN()            defines main: runs every test, prints one line per
 *                           failed test and "N passed, M failed", returns 0 only
 *                           if every test passed; fails if there are no tests
 *                           or more than PTEST_MAX_TESTS.
 *   ptest_capture(fn, ctx)  runs fn(ctx) and returns its failure instead of
 *                           failing the running test (for testing assertions).
 *   ptest_register_in, ptest_main_for
 *                           what PTEST and PTEST_MAIN do, on a registry the
 *                           caller supplies (for testing the runner).
 *
 * Timeout: each test has a wall-clock limit of PTEST_DEFAULT_TIMEOUT_SECONDS
 * (60), so that a stuck test fails instead of blocking the suite. The
 * environment variable PTEST_TIMEOUT_SECONDS (a positive whole number of
 * seconds; anything else is ignored) overrides it for a run, for a slow
 * machine or a debugger session. A watchdog (host_thread.h: a thread that
 * only sets an atomic flag when the limit passes; no signal, no timer of the
 * process) keeps the limit, and the test's own thread looks at the flag: in
 * every assertion, and when the test returns. When it finds the limit past, it
 * abandons the test, which is reported as failed ("timed out after N
 * seconds"), and the next test runs; the binary is not killed. The abandoned
 * test's cleanup does not run, so a test that timed out may leave module
 * state, open files and locks behind: later failures in the same binary are
 * suspect, LeakSanitizer may report what it leaked, and the first failure is
 * the one to read. The reported location is the runner's line in this header;
 * the message carries the test's name. A timeout replaces any failure the
 * test had recorded (a test caught in ptest_capture included): the report is
 * always "timed out".
 * Not covered: a test that never reaches an assertion after the limit (an
 * endless loop or a blocking call with none in it) is not abandoned, because
 * nothing but the test's own thread may touch the test's state. Such a test
 * hangs the binary: give a loop an assertion, and bound a blocking call by
 * its own timeout (test/posix/posix_support.h has a bounded child and a
 * bounded read); the CI job's own time limit is the backstop.
 * ptest_set_timeout(seconds) changes the limit of the running test, counted
 * from the call (a test that is legitimately long, or the test of this
 * mechanism). The runner owns no signal and no process timer, so a test may
 * use its own alarm (test_crash_reboot's children do).
 * ptest_pause_watchdog() and ptest_resume_watchdog() stop the running test's
 * watchdog thread (joined) and start it again with the test's limit counted
 * from the resume: a test that forks must have no thread alive at the fork, so
 * the child inherits none (posix/posix_support.h's psup_run_child does this).
 *
 * The registry is static: a test binary is one translation unit including
 * this header. Tests run in definition order with GCC, but must not rely on it.
 */
#ifndef PTEST_H
#define PTEST_H

#include <inttypes.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_thread.h"

#define PTEST_MAX_TESTS 256                       /* registry capacity of one test binary */
#define PTEST_MESSAGE_SIZE 512                    /* one failure line, with its terminator; longer ones are cut */
#define PTEST_DEFAULT_TIMEOUT_SECONDS 60u         /* wall-clock limit of one test */
#define PTEST_TIMEOUT_ENV "PTEST_TIMEOUT_SECONDS" /* overrides the limit, see the header comment */

struct ptest_case {
    const char *name;
    void (*run)(void);
};
struct ptest_outcome {
    bool failed;
    char message[PTEST_MESSAGE_SIZE];
};
struct ptest_registry {
    struct ptest_case cases[PTEST_MAX_TESTS];
    size_t count;
    bool overflowed; /* a registration was refused for lack of room */
};

/* The tests PTEST() registers in this binary. */
static struct ptest_registry ptest_tests;

/* The test running now: its name and its outcome so far. */
static struct {
    const char *name;
    struct ptest_outcome outcome;
} ptest_current;

/* The per-test limit in force: the default, the environment's, or the running test's own. */
static unsigned ptest_timeout_seconds;

/* The running test's watchdog (NULL between tests, or if it could not start),
 * the flag it sets when the limit passes, and where an abandoned test returns
 * to: the running ptest_run_cases' frame. All three are the test thread's
 * own; only the flag is written by another thread, and only to set it. */
static struct host_watchdog *ptest_watchdog;
static atomic_bool *ptest_expired;
static jmp_buf *ptest_timeout_target;

/* True once the watchdog has set the flag. Relaxed: the flag carries no data
 * (host_thread.h), so there is nothing to order. */
static inline bool ptest_limit_passed(void)
{
    return ptest_expired != NULL && atomic_load_explicit(ptest_expired, memory_order_relaxed);
}

/* Called by every assertion: abandons the running test if its limit passed. */
static inline void ptest_check_limit(void)
{
    if (ptest_limit_passed() && ptest_timeout_target != NULL)
        longjmp(*ptest_timeout_target, 1);
}

static inline void ptest_register_in(struct ptest_registry *registry, const char *name, void (*run)(void))
{
    if (registry->count == PTEST_MAX_TESTS)
        registry->overflowed = true;
    else
        registry->cases[registry->count++] = (struct ptest_case){name, run};
}

__attribute__((format(printf, 3, 4))) static inline void ptest_fail_at(const char *file, int line, const char *format,
                                                                       ...)
{
    char *const text = ptest_current.outcome.message;
    if (ptest_current.outcome.failed)
        return; /* keep the test's first failure */
    ptest_current.outcome.failed = true;
    const int prefix = snprintf(text, PTEST_MESSAGE_SIZE, "%s:%d: %s: ", file, line, ptest_current.name);
    /* snprintf returns the length before truncation; the rest goes after what it stored. */
    const size_t used = prefix < 0 ? 0 : (size_t)prefix < PTEST_MESSAGE_SIZE ? (size_t)prefix : PTEST_MESSAGE_SIZE - 1;
    va_list args;
    va_start(args, format);
    if (vsnprintf(text + used, PTEST_MESSAGE_SIZE - used, format, args) < 0)
        (void)snprintf(text + used, PTEST_MESSAGE_SIZE - used, "(unformattable message)"); /* best-effort */
    va_end(args);
}

static inline bool ptest_check_u32(const char *file, int line, const char *a_text, const char *e_text, uint32_t a,
                                   uint32_t e)
{
    ptest_check_limit();
    if (a != e)
        ptest_fail_at(file, line, "%s == %s: actual 0x%08" PRIX32 ", expected 0x%08" PRIX32, a_text, e_text, a, e);
    return a == e;
}

static inline bool ptest_check_int(const char *file, int line, const char *a_text, const char *e_text, intmax_t a,
                                   intmax_t e)
{
    ptest_check_limit();
    if (a != e)
        ptest_fail_at(file, line, "%s == %s: actual %" PRIdMAX ", expected %" PRIdMAX, a_text, e_text, a, e);
    return a == e;
}

static inline bool ptest_check_ptr(const char *file, int line, const char *a_text, const char *e_text, const void *a,
                                   const void *e)
{
    ptest_check_limit();
    if (a != e)
        ptest_fail_at(file, line, "%s == %s: actual %p, expected %p", a_text, e_text, a, e);
    return a == e;
}

/* Strings are equal when both are NULL or both have the same contents. */
static inline bool ptest_check_str(const char *file, int line, const char *a_text, const char *e_text, const char *a,
                                   const char *e)
{
    ptest_check_limit();
    const bool equal = a == NULL || e == NULL ? a == e : strcmp(a, e) == 0;
    if (!equal)
        ptest_fail_at(file, line, "%s == %s: actual \"%s\", expected \"%s\"", a_text, e_text, a ? a : "(null)",
                      e ? e : "(null)");
    return equal;
}

static inline bool ptest_check_mem(const char *file, int line, const char *a_text, const char *e_text, const void *a,
                                   const void *e, size_t n)
{
    ptest_check_limit();
    if (n > 0 && (a == NULL || e == NULL)) {
        ptest_fail_at(file, line, "%s == %s over %zu bytes: null pointer", a_text, e_text, n);
        return false;
    }
    const unsigned char *const pa = a, *const pe = e;
    size_t at = 0;
    while (at < n && pa[at] == pe[at])
        at++;
    if (at < n)
        ptest_fail_at(file, line,
                      "%s == %s over %zu bytes: first difference at offset %zu: "
                      "actual 0x%02X, expected 0x%02X",
                      a_text, e_text, n, at, pa[at], pe[at]);
    return at == n;
}

static inline struct ptest_outcome ptest_capture(void (*body)(void *), void *context)
{
    const struct ptest_outcome outer = ptest_current.outcome;
    ptest_current.outcome = (struct ptest_outcome){.failed = false};
    body(context);
    const struct ptest_outcome captured = ptest_current.outcome;
    ptest_current.outcome = outer;
    return captured;
}

/* The default limit: PTEST_TIMEOUT_ENV if it holds a positive whole number
 * that fits an unsigned, else PTEST_DEFAULT_TIMEOUT_SECONDS. */
static inline unsigned ptest_default_timeout(void)
{
    const char *const text = getenv(PTEST_TIMEOUT_ENV);
    if (text == NULL || text[0] < '0' || text[0] > '9')
        return PTEST_DEFAULT_TIMEOUT_SECONDS;
    char *end = NULL;
    const unsigned long value = strtoul(text, &end, 10);
    if (*end != '\0' || value == 0 || value > UINT32_MAX / 2)
        return PTEST_DEFAULT_TIMEOUT_SECONDS;
    return (unsigned)value;
}

/* Gives the running test seconds (more than 0) from now to finish. */
static inline void ptest_set_timeout(unsigned seconds)
{
    if (host_watchdog_restart(ptest_watchdog, seconds) == 0)
        ptest_timeout_seconds = seconds;
}

/* Stops the running test's watchdog (joining its thread), leaving no thread
 * and no handle. A flag already set stays set. */
static inline void ptest_pause_watchdog(void)
{
    host_watchdog_stop(ptest_watchdog);
    ptest_watchdog = NULL;
}

/* Starts the running test's watchdog again, with the test's limit counted from
 * now. Does nothing outside a run. */
static inline void ptest_resume_watchdog(void)
{
    if (ptest_expired != NULL && ptest_watchdog == NULL)
        ptest_watchdog = host_watchdog_start(ptest_timeout_seconds, ptest_expired);
}

/* Runs the cases, reports to out, returns the exit status (0 = all passed). */
static inline int ptest_run_cases(const struct ptest_case *cases, size_t count, FILE *out)
{
    const char *const outer_name = ptest_current.name;
    const struct ptest_outcome outer = ptest_current.outcome;
    const unsigned outer_timeout = ptest_timeout_seconds;
    struct host_watchdog *const outer_watchdog = ptest_watchdog; /* a run inside a test leaves that test's limit be */
    atomic_bool *const outer_expired = ptest_expired;
    jmp_buf *const outer_target = ptest_timeout_target;
    atomic_bool expired;
    jmp_buf jump;
    ptest_timeout_target = &jump;
    volatile size_t failed = 0;
    for (volatile size_t i = 0; i < count; i++) {
        ptest_current.name = cases[i].name;
        ptest_current.outcome = (struct ptest_outcome){.failed = false};
        ptest_timeout_seconds = ptest_default_timeout();
        atomic_init(&expired, false);
        ptest_expired = &expired;
        ptest_watchdog = host_watchdog_start(ptest_timeout_seconds, &expired);
        if (setjmp(jump) == 0) {
            cases[i].run();
            ptest_check_limit(); /* a test that finished late has failed too */
        } else {
            /* The longjmp may have left ptest_capture's outcome in place and the test may
             * have failed before: the timeout is the report. */
            ptest_current.outcome = (struct ptest_outcome){.failed = false};
            ptest_fail_at(__FILE__, __LINE__, "timed out after %u seconds", ptest_timeout_seconds);
        }
        host_watchdog_stop(ptest_watchdog); /* joined: no thread of the test's is left */
        ptest_watchdog = NULL;
        ptest_expired = NULL;
        if (ptest_current.outcome.failed) {
            failed++;
            (void)fprintf(out, "FAIL %s\n", ptest_current.outcome.message); /* ferror below */
        }
    }
    (void)fprintf(out, "%zu passed, %zu failed\n", count - failed, (size_t)failed); /* ferror below */
    ptest_timeout_target = outer_target;
    ptest_expired = outer_expired;
    ptest_watchdog = outer_watchdog;
    ptest_timeout_seconds = outer_timeout;
    ptest_current.name = outer_name;
    ptest_current.outcome = outer;
    const bool output_ok = fflush(out) == 0 && !ferror(out);
    return failed == 0 && output_ok ? 0 : 1;
}

/* Runs every test in registry, reporting to out; returns the exit status. */
static inline int ptest_main_for(const struct ptest_registry *registry, FILE *out)
{
    if (registry->overflowed) {
        (void)fprintf(out, "ptest: more than %d tests registered; raise PTEST_MAX_TESTS\n",
                      PTEST_MAX_TESTS); /* best-effort */
        return 1;
    }
    if (registry->count == 0) {
        (void)fputs("ptest: no tests registered\n", out); /* best-effort */
        return 1;
    }
    return ptest_run_cases(registry->cases, registry->count, out);
}

#define PTEST(name)                                                      \
    static void ptest_body_##name(void);                                 \
    __attribute__((constructor)) static void ptest_register_##name(void) \
    {                                                                    \
        ptest_register_in(&ptest_tests, #name, ptest_body_##name);       \
    }                                                                    \
    static void ptest_body_##name(void)

#define PTEST_MAIN()                                 \
    int main(void)                                   \
    {                                                \
        return ptest_main_for(&ptest_tests, stdout); \
    }

/* Returns from the test unless the check passed. */
#define PTEST_REQUIRE_(check) \
    do {                      \
        if (!(check))         \
            return;           \
    } while (0)

#define PT_FAIL(message)                                             \
    do {                                                             \
        ptest_check_limit();                                         \
        ptest_fail_at(__FILE__, __LINE__, "PT_FAIL: %s", (message)); \
        return;                                                      \
    } while (0)
#define PT_ASSERT(cond)                                                \
    do {                                                               \
        ptest_check_limit();                                           \
        if (!(cond)) {                                                 \
            ptest_fail_at(__FILE__, __LINE__, "PT_ASSERT(%s)", #cond); \
            return;                                                    \
        }                                                              \
    } while (0)
#define PT_ASSERT_EQ_U32(actual, expected) \
    PTEST_REQUIRE_(ptest_check_u32(__FILE__, __LINE__, #actual, #expected, (uint32_t)(actual), (uint32_t)(expected)))
#define PT_ASSERT_EQ_INT(actual, expected) \
    PTEST_REQUIRE_(ptest_check_int(__FILE__, __LINE__, #actual, #expected, (intmax_t)(actual), (intmax_t)(expected)))
#define PT_ASSERT_EQ_PTR(actual, expected) \
    PTEST_REQUIRE_(ptest_check_ptr(__FILE__, __LINE__, #actual, #expected, (actual), (expected)))
#define PT_ASSERT_EQ_STR(actual, expected) \
    PTEST_REQUIRE_(ptest_check_str(__FILE__, __LINE__, #actual, #expected, (actual), (expected)))
#define PT_ASSERT_MEM_EQ(actual, expected, nbytes) \
    PTEST_REQUIRE_(ptest_check_mem(__FILE__, __LINE__, #actual, #expected, (actual), (expected), (nbytes)))

#endif /* PTEST_H */
