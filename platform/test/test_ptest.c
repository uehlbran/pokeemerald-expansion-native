/*
 * Self-test of ptest.h: every assertion's pass path and fail path, the
 * failure message (file, line, test name, expression, values), single
 * evaluation of arguments, the runner's summary and exit status, and
 * registration. Failure paths run under ptest_capture, so a failing
 * assertion is observed and checked instead of failing this run.
 */
#include <stdlib.h>

#include "ptest.h"

/* What a probed function did: whether it ran past its assertion, and where the assertion is. */
struct probe {
    bool reached_end;
    int assertion_line;
};

static bool has_text(const char *haystack, const char *needle)
{
    return strstr(haystack, needle) != NULL;
}

static size_t count_text(const char *haystack, const char *needle)
{
    size_t count = 0;
    for (const char *at = strstr(haystack, needle); at != NULL; at = strstr(at + 1, needle))
        count++;
    return count;
}

/* "file:line: test: " as ptest_fail_at writes it, for a probe in the named test. */
static bool has_location(const struct ptest_outcome *outcome, const struct probe *probe,
                         const char *test_name)
{
    char expected[PTEST_MESSAGE_SIZE];
    const int length = snprintf(expected, sizeof expected, "%s:%d: %s: ", __FILE__,
                                probe->assertion_line, test_name);
    return length > 0 && strncmp(outcome->message, expected, (size_t)length) == 0;
}

/* ---- PT_ASSERT ---- */

static void assert_true(void *context)
{
    struct probe *probe = context;
    PT_ASSERT(2 + 2 == 4);
    probe->reached_end = true;
}

static void assert_false(void *context)
{
    struct probe *probe = context;
    probe->assertion_line = __LINE__ + 1;
    PT_ASSERT(2 + 2 == 5);
    probe->reached_end = true;
}

PTEST(assert_passes_and_continues)
{
    struct probe probe = { false, 0 };
    const struct ptest_outcome outcome = ptest_capture(assert_true, &probe);
    PT_ASSERT(!outcome.failed);
    PT_ASSERT(probe.reached_end);
}

PTEST(assert_failure_records_location_and_returns)
{
    struct probe probe = { false, 0 };
    const struct ptest_outcome outcome = ptest_capture(assert_false, &probe);
    PT_ASSERT(outcome.failed);
    PT_ASSERT(!probe.reached_end);
    PT_ASSERT(has_location(&outcome, &probe, "assert_failure_records_location_and_returns"));
    PT_ASSERT(has_text(outcome.message, "PT_ASSERT(2 + 2 == 5)"));
    /* The capture must leave the running test's own state untouched. */
    PT_ASSERT(!ptest_registry.current.failed);
}

/* ---- PT_ASSERT_EQ_U32 ---- */

static void eq_u32_equal(void *context)
{
    struct probe *probe = context;
    PT_ASSERT_EQ_U32(UINT32_C(0xFFFFFFFF), UINT32_MAX);
    probe->reached_end = true;
}

static void eq_u32_different(void *context)
{
    struct probe *probe = context;
    const uint32_t actual = UINT32_C(0x12345678);
    probe->assertion_line = __LINE__ + 1;
    PT_ASSERT_EQ_U32(actual, UINT32_C(0x9ABCDEF0));
    probe->reached_end = true;
}

PTEST(eq_u32_passes_on_equal)
{
    struct probe probe = { false, 0 };
    const struct ptest_outcome outcome = ptest_capture(eq_u32_equal, &probe);
    PT_ASSERT(!outcome.failed);
    PT_ASSERT(probe.reached_end);
}

PTEST(eq_u32_failure_prints_both_values_in_hex)
{
    struct probe probe = { false, 0 };
    const struct ptest_outcome outcome = ptest_capture(eq_u32_different, &probe);
    PT_ASSERT(outcome.failed);
    PT_ASSERT(!probe.reached_end);
    PT_ASSERT(has_location(&outcome, &probe, "eq_u32_failure_prints_both_values_in_hex"));
    PT_ASSERT(has_text(outcome.message, "actual == UINT32_C(0x9ABCDEF0)"));
    PT_ASSERT(has_text(outcome.message, "actual 0x12345678, expected 0x9ABCDEF0"));
}

/* ---- PT_ASSERT_EQ_INT ---- */

static void eq_int_equal(void *context)
{
    struct probe *probe = context;
    PT_ASSERT_EQ_INT(-3, -3);
    probe->reached_end = true;
}

static void eq_int_different(void *context)
{
    struct probe *probe = context;
    const int actual = -5;
    probe->assertion_line = __LINE__ + 1;
    PT_ASSERT_EQ_INT(actual, 7);
    probe->reached_end = true;
}

PTEST(eq_int_passes_on_equal)
{
    struct probe probe = { false, 0 };
    const struct ptest_outcome outcome = ptest_capture(eq_int_equal, &probe);
    PT_ASSERT(!outcome.failed);
    PT_ASSERT(probe.reached_end);
}

PTEST(eq_int_failure_prints_both_values_signed)
{
    struct probe probe = { false, 0 };
    const struct ptest_outcome outcome = ptest_capture(eq_int_different, &probe);
    PT_ASSERT(outcome.failed);
    PT_ASSERT(!probe.reached_end);
    PT_ASSERT(has_location(&outcome, &probe, "eq_int_failure_prints_both_values_signed"));
    PT_ASSERT(has_text(outcome.message, "actual == 7: actual -5, expected 7"));
}

/* ---- PT_ASSERT_EQ_PTR ---- */

static const int ptr_targets[2] = { 1, 2 };

static void eq_ptr_equal(void *context)
{
    struct probe *probe = context;
    PT_ASSERT_EQ_PTR(&ptr_targets[1], ptr_targets + 1);
    probe->reached_end = true;
}

static void eq_ptr_different(void *context)
{
    struct probe *probe = context;
    probe->assertion_line = __LINE__ + 1;
    PT_ASSERT_EQ_PTR(&ptr_targets[0], &ptr_targets[1]);
    probe->reached_end = true;
}

PTEST(eq_ptr_passes_on_equal)
{
    struct probe probe = { false, 0 };
    const struct ptest_outcome outcome = ptest_capture(eq_ptr_equal, &probe);
    PT_ASSERT(!outcome.failed);
    PT_ASSERT(probe.reached_end);
}

PTEST(eq_ptr_failure_prints_both_addresses)
{
    struct probe probe = { false, 0 };
    const struct ptest_outcome outcome = ptest_capture(eq_ptr_different, &probe);
    char expected[128];
    const int length = snprintf(expected, sizeof expected, "actual %p, expected %p",
                                (const void *)&ptr_targets[0], (const void *)&ptr_targets[1]);
    PT_ASSERT(length > 0 && (size_t)length < sizeof expected);
    PT_ASSERT(outcome.failed);
    PT_ASSERT(!probe.reached_end);
    PT_ASSERT(has_location(&outcome, &probe, "eq_ptr_failure_prints_both_addresses"));
    PT_ASSERT(has_text(outcome.message, expected));
}

/* ---- PT_ASSERT_EQ_STR ---- */

static void eq_str_equal(void *context)
{
    struct probe *probe = context;
    const char built[] = { 'p', 'o', 'k', 'e', '\0' };
    PT_ASSERT_EQ_STR(built, "poke");
    PT_ASSERT_EQ_STR(NULL, NULL);
    probe->reached_end = true;
}

static void eq_str_different(void *context)
{
    struct probe *probe = context;
    probe->assertion_line = __LINE__ + 1;
    PT_ASSERT_EQ_STR("abc", "abd");
    probe->reached_end = true;
}

static void eq_str_null_actual(void *context)
{
    struct probe *probe = context;
    const char *const missing = NULL;
    probe->assertion_line = __LINE__ + 1;
    PT_ASSERT_EQ_STR(missing, "x");
    probe->reached_end = true;
}

PTEST(eq_str_passes_on_equal_contents_and_on_two_nulls)
{
    struct probe probe = { false, 0 };
    const struct ptest_outcome outcome = ptest_capture(eq_str_equal, &probe);
    PT_ASSERT(!outcome.failed);
    PT_ASSERT(probe.reached_end);
}

PTEST(eq_str_failure_prints_both_strings)
{
    struct probe probe = { false, 0 };
    const struct ptest_outcome outcome = ptest_capture(eq_str_different, &probe);
    PT_ASSERT(outcome.failed);
    PT_ASSERT(!probe.reached_end);
    PT_ASSERT(has_location(&outcome, &probe, "eq_str_failure_prints_both_strings"));
    PT_ASSERT(has_text(outcome.message, "actual \"abc\", expected \"abd\""));
}

PTEST(eq_str_fails_when_one_side_is_null)
{
    struct probe probe = { false, 0 };
    const struct ptest_outcome outcome = ptest_capture(eq_str_null_actual, &probe);
    PT_ASSERT(outcome.failed);
    PT_ASSERT(!probe.reached_end);
    PT_ASSERT(has_text(outcome.message, "actual \"(null)\", expected \"x\""));
}

/* ---- PT_ASSERT_MEM_EQ ---- */

static void mem_eq_equal(void *context)
{
    struct probe *probe = context;
    const uint8_t a[4] = { 1, 2, 3, 4 };
    const uint8_t b[4] = { 1, 2, 3, 4 };
    PT_ASSERT_MEM_EQ(a, b, sizeof a);
    PT_ASSERT_MEM_EQ(NULL, NULL, 0);
    probe->reached_end = true;
}

static void mem_eq_different(void *context)
{
    struct probe *probe = context;
    const uint8_t a[6] = { 1, 2, 3, 0x04, 5, 6 };
    const uint8_t b[6] = { 1, 2, 3, 0xA9, 5, 0 };
    probe->assertion_line = __LINE__ + 1;
    PT_ASSERT_MEM_EQ(a, b, sizeof a);
    probe->reached_end = true;
}

static void mem_eq_null_with_length(void *context)
{
    struct probe *probe = context;
    const uint8_t b[1] = { 0 };
    PT_ASSERT_MEM_EQ(NULL, b, sizeof b);
    probe->reached_end = true;
}

PTEST(mem_eq_passes_on_equal_bytes_and_zero_length)
{
    struct probe probe = { false, 0 };
    const struct ptest_outcome outcome = ptest_capture(mem_eq_equal, &probe);
    PT_ASSERT(!outcome.failed);
    PT_ASSERT(probe.reached_end);
}

PTEST(mem_eq_failure_prints_first_difference)
{
    struct probe probe = { false, 0 };
    const struct ptest_outcome outcome = ptest_capture(mem_eq_different, &probe);
    PT_ASSERT(outcome.failed);
    PT_ASSERT(!probe.reached_end);
    PT_ASSERT(has_location(&outcome, &probe, "mem_eq_failure_prints_first_difference"));
    PT_ASSERT(has_text(outcome.message,
                       "over 6 bytes: first difference at offset 3: actual 0x04, expected 0xA9"));
}

PTEST(mem_eq_fails_on_null_with_nonzero_length)
{
    struct probe probe = { false, 0 };
    const struct ptest_outcome outcome = ptest_capture(mem_eq_null_with_length, &probe);
    PT_ASSERT(outcome.failed);
    PT_ASSERT(!probe.reached_end);
    PT_ASSERT(has_text(outcome.message, "null pointer"));
}

/* ---- PT_FAIL ---- */

static void fail_always(void *context)
{
    struct probe *probe = context;
    probe->assertion_line = __LINE__ + 1;
    PT_FAIL("the backup chip did not answer");
    probe->reached_end = true;
}

PTEST(fail_records_message_and_returns)
{
    struct probe probe = { false, 0 };
    const struct ptest_outcome outcome = ptest_capture(fail_always, &probe);
    PT_ASSERT(outcome.failed);
    PT_ASSERT(!probe.reached_end);
    PT_ASSERT(has_location(&outcome, &probe, "fail_records_message_and_returns"));
    PT_ASSERT(has_text(outcome.message, "PT_FAIL: the backup chip did not answer"));
}

/* ---- single evaluation ---- */

static void count_evaluations(void *context)
{
    uint32_t *counter = context;
    PT_ASSERT_EQ_U32((*counter)++, 0);
    PT_ASSERT_EQ_INT((*counter)++, 1);
    PT_ASSERT_EQ_STR(((*counter)++, "s"), "s");
}

PTEST(assertion_arguments_are_evaluated_once)
{
    uint32_t counter = 0;
    const struct ptest_outcome outcome = ptest_capture(count_evaluations, &counter);
    PT_ASSERT(!outcome.failed);
    PT_ASSERT_EQ_U32(counter, 3);
}

/* ---- runner and registry ---- */

static void runner_passing_case(void)
{
    PT_ASSERT(true);
}

static void runner_failing_case(void)
{
    PT_FAIL("expected failure");
}

/* Runs cases into a memory stream; returns the exit status, stores the text (caller frees). */
static int run_into_text(const struct ptest_case *cases, size_t count, char **text)
{
    size_t length = 0;
    *text = NULL;
    FILE *out = open_memstream(text, &length);
    if (out == NULL)
        return -1;
    const int status = ptest_run_cases(cases, count, out);
    if (fclose(out) != 0)
        return -1;
    return status;
}

PTEST(runner_reports_failures_and_returns_nonzero)
{
    const struct ptest_case cases[] = {
        { "runner_passing_case", runner_passing_case },
        { "runner_failing_case", runner_failing_case },
    };
    char *text = NULL;
    const int status = run_into_text(cases, 2, &text);
    const bool fail_line =
        text != NULL && has_text(text, ": runner_failing_case: PT_FAIL: expected failure\n");
    const size_t fail_lines = text != NULL ? count_text(text, "FAIL ") : 0;
    const bool summary = text != NULL && has_text(text, "1 passed, 1 failed\n");
    free(text);
    PT_ASSERT_EQ_INT(status, 1);
    PT_ASSERT(fail_line);
    PT_ASSERT(fail_lines == 1);
    PT_ASSERT(summary);
    /* The nested run restores the running test's name and state. */
    PT_ASSERT_EQ_STR(ptest_registry.current_name, "runner_reports_failures_and_returns_nonzero");
    PT_ASSERT(!ptest_registry.current.failed);
}

PTEST(runner_returns_zero_when_all_pass)
{
    const struct ptest_case cases[] = {
        { "first", runner_passing_case },
        { "second", runner_passing_case },
    };
    char *text = NULL;
    const int status = run_into_text(cases, 2, &text);
    const bool summary = text != NULL && strcmp(text, "2 passed, 0 failed\n") == 0;
    free(text);
    PT_ASSERT_EQ_INT(status, 0);
    PT_ASSERT(summary);
}

PTEST(registry_contains_this_test)
{
    bool found = false;
    for (size_t i = 0; i < ptest_registry.count; i++)
        found = found || (ptest_registry.cases[i].run == ptest_body_registry_contains_this_test &&
                          strcmp(ptest_registry.cases[i].name, "registry_contains_this_test") == 0);
    PT_ASSERT(found);
    PT_ASSERT(!ptest_registry.overflowed);
}

PTEST_MAIN()
