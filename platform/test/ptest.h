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
 * (60), so that a hanging test fails instead of blocking the suite. The
 * environment variable PTEST_TIMEOUT_SECONDS (a positive whole number of
 * seconds; anything else is ignored) overrides it for a run, for a slow
 * machine or a debugger session. When the limit passes, SIGALRM abandons the
 * test, which is reported as failed ("timed out after N seconds"), and the
 * next test runs; the binary is not killed. The abandoned test's cleanup does
 * not run, so a test that timed out may leave its resources behind and
 * LeakSanitizer may report them: the first failure is the one to fix.
 * ptest_set_timeout(seconds) changes the limit of the running test (a test
 * that is legitimately long, or the test of this mechanism). The runner owns
 * SIGALRM and the process's alarm(2) timer while it runs; a test that needs
 * its own alarm (test_crash_reboot's children) must not run it in the runner's
 * process.
 *
 * The registry is static: a test binary is one translation unit including
 * this header. Tests run in definition order with GCC, but must not rely on it.
 */
#ifndef PTEST_H
#define PTEST_H

#include <inttypes.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PTEST_MAX_TESTS 256    /* registry capacity of one test binary */
#define PTEST_MESSAGE_SIZE 512 /* one failure line, with its terminator; longer ones are cut */
#define PTEST_DEFAULT_TIMEOUT_SECONDS 60u /* wall-clock limit of one test */
#define PTEST_TIMEOUT_ENV "PTEST_TIMEOUT_SECONDS" /* overrides the limit, see the header comment */

struct ptest_case { const char *name; void (*run)(void); };
struct ptest_outcome { bool failed; char message[PTEST_MESSAGE_SIZE]; };
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

static inline void ptest_register_in(struct ptest_registry *registry, const char *name,
                                     void (*run)(void))
{
    if (registry->count == PTEST_MAX_TESTS)
        registry->overflowed = true;
    else
        registry->cases[registry->count++] = (struct ptest_case){ name, run };
}

__attribute__((format(printf, 3, 4)))
static inline void ptest_fail_at(const char *file, int line, const char *format, ...)
{
    char *const text = ptest_current.outcome.message;
    if (ptest_current.outcome.failed)
        return; /* keep the test's first failure */
    ptest_current.outcome.failed = true;
    const int prefix = snprintf(text, PTEST_MESSAGE_SIZE, "%s:%d: %s: ", file, line,
                                ptest_current.name);
    /* snprintf returns the length before truncation; the rest goes after what it stored. */
    const size_t used = prefix < 0 ? 0
                        : (size_t)prefix < PTEST_MESSAGE_SIZE ? (size_t)prefix
                        : PTEST_MESSAGE_SIZE - 1;
    va_list args;
    va_start(args, format);
    if (vsnprintf(text + used, PTEST_MESSAGE_SIZE - used, format, args) < 0)
        (void)snprintf(text + used, PTEST_MESSAGE_SIZE - used,
                       "(unformattable message)"); /* best-effort */
    va_end(args);
}

static inline bool ptest_check_u32(const char *file, int line, const char *a_text,
                                   const char *e_text, uint32_t a, uint32_t e)
{
    if (a != e)
        ptest_fail_at(file, line, "%s == %s: actual 0x%08" PRIX32 ", expected 0x%08" PRIX32,
                      a_text, e_text, a, e);
    return a == e;
}

static inline bool ptest_check_int(const char *file, int line, const char *a_text,
                                   const char *e_text, intmax_t a, intmax_t e)
{
    if (a != e)
        ptest_fail_at(file, line, "%s == %s: actual %" PRIdMAX ", expected %" PRIdMAX,
                      a_text, e_text, a, e);
    return a == e;
}

static inline bool ptest_check_ptr(const char *file, int line, const char *a_text,
                                   const char *e_text, const void *a, const void *e)
{
    if (a != e)
        ptest_fail_at(file, line, "%s == %s: actual %p, expected %p", a_text, e_text, a, e);
    return a == e;
}

/* Strings are equal when both are NULL or both have the same contents. */
static inline bool ptest_check_str(const char *file, int line, const char *a_text,
                                   const char *e_text, const char *a, const char *e)
{
    const bool equal = a == NULL || e == NULL ? a == e : strcmp(a, e) == 0;
    if (!equal)
        ptest_fail_at(file, line, "%s == %s: actual \"%s\", expected \"%s\"", a_text, e_text,
                      a ? a : "(null)", e ? e : "(null)");
    return equal;
}

static inline bool ptest_check_mem(const char *file, int line, const char *a_text,
                                   const char *e_text, const void *a, const void *e, size_t n)
{
    if (n > 0 && (a == NULL || e == NULL)) {
        ptest_fail_at(file, line, "%s == %s over %zu bytes: null pointer", a_text, e_text, n);
        return false;
    }
    const unsigned char *const pa = a, *const pe = e;
    size_t at = 0;
    while (at < n && pa[at] == pe[at])
        at++;
    if (at < n)
        ptest_fail_at(file, line, "%s == %s over %zu bytes: first difference at offset %zu: "
                      "actual 0x%02X, expected 0x%02X", a_text, e_text, n, at, pa[at], pe[at]);
    return at == n;
}

static inline struct ptest_outcome ptest_capture(void (*body)(void *), void *context)
{
    const struct ptest_outcome outer = ptest_current.outcome;
    ptest_current.outcome = (struct ptest_outcome){ .failed = false };
    body(context);
    const struct ptest_outcome captured = ptest_current.outcome;
    ptest_current.outcome = outer;
    return captured;
}

/* The per-test limit in force: the default, the environment's, or the running test's own. */
static unsigned ptest_timeout_seconds;

/* Where the timeout's SIGALRM returns to: the running ptest_run_cases' frame. */
static sigjmp_buf *ptest_timeout_target;

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
    ptest_timeout_seconds = seconds;
    (void)alarm(seconds); /* returns the previous remainder, of no use here */
}

static void ptest_on_alarm(int signal_number)
{
    (void)signal_number;
    if (ptest_timeout_target != NULL)
        siglongjmp(*ptest_timeout_target, 1);
}

/* Runs the cases, reports to out, returns the exit status (0 = all passed). */
static inline int ptest_run_cases(const struct ptest_case *cases, size_t count, FILE *out)
{
    const char *const outer_name = ptest_current.name;
    const struct ptest_outcome outer = ptest_current.outcome;
    const unsigned outer_timeout = ptest_timeout_seconds;
    sigjmp_buf *const outer_target = ptest_timeout_target;
    const unsigned outer_alarm = alarm(0); /* a run inside a test suspends that test's timer */
    struct sigaction handler = { .sa_handler = ptest_on_alarm };
    struct sigaction outer_handler;
    (void)sigemptyset(&handler.sa_mask);
    const bool handled = sigaction(SIGALRM, &handler, &outer_handler) == 0;
    sigjmp_buf jump;
    ptest_timeout_target = &jump;
    volatile size_t failed = 0;
    for (volatile size_t i = 0; i < count; i++) {
        ptest_current.name = cases[i].name;
        ptest_current.outcome = (struct ptest_outcome){ .failed = false };
        ptest_timeout_seconds = ptest_default_timeout();
        if (sigsetjmp(jump, 1) == 0) {
            if (handled)
                (void)alarm(ptest_timeout_seconds);
            cases[i].run();
        } else {
            ptest_fail_at(__FILE__, __LINE__, "timed out after %u seconds", ptest_timeout_seconds);
        }
        (void)alarm(0);
        if (ptest_current.outcome.failed) {
            failed++;
            (void)fprintf(out, "FAIL %s\n", ptest_current.outcome.message); /* ferror below */
        }
    }
    (void)fprintf(out, "%zu passed, %zu failed\n", count - failed, (size_t)failed); /* ferror below */
    ptest_timeout_target = outer_target;
    ptest_timeout_seconds = outer_timeout;
    if (handled)
        (void)sigaction(SIGALRM, &outer_handler, NULL);
    if (outer_alarm > 0)
        (void)alarm(outer_alarm);
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

#define PTEST(name)                                                       \
    static void ptest_body_##name(void);                                  \
    __attribute__((constructor)) static void ptest_register_##name(void) \
    {                                                                     \
        ptest_register_in(&ptest_tests, #name, ptest_body_##name);        \
    }                                                                     \
    static void ptest_body_##name(void)

#define PTEST_MAIN() int main(void) { return ptest_main_for(&ptest_tests, stdout); }

/* Returns from the test unless the check passed. */
#define PTEST_REQUIRE_(check) do { if (!(check)) return; } while (0)

#define PT_FAIL(message) \
    do { ptest_fail_at(__FILE__, __LINE__, "PT_FAIL: %s", (message)); return; } while (0)
#define PT_ASSERT(cond) \
    do { if (!(cond)) { ptest_fail_at(__FILE__, __LINE__, "PT_ASSERT(%s)", #cond); return; } } while (0)
#define PT_ASSERT_EQ_U32(actual, expected) \
    PTEST_REQUIRE_(ptest_check_u32(__FILE__, __LINE__, #actual, #expected, (actual), (expected)))
#define PT_ASSERT_EQ_INT(actual, expected) \
    PTEST_REQUIRE_(ptest_check_int(__FILE__, __LINE__, #actual, #expected, (actual), (expected)))
#define PT_ASSERT_EQ_PTR(actual, expected) \
    PTEST_REQUIRE_(ptest_check_ptr(__FILE__, __LINE__, #actual, #expected, (actual), (expected)))
#define PT_ASSERT_EQ_STR(actual, expected) \
    PTEST_REQUIRE_(ptest_check_str(__FILE__, __LINE__, #actual, #expected, (actual), (expected)))
#define PT_ASSERT_MEM_EQ(actual, expected, nbytes)                                      \
    PTEST_REQUIRE_(ptest_check_mem(__FILE__, __LINE__, #actual, #expected, (actual), \
                                   (expected), (nbytes)))

#endif /* PTEST_H */
