/*
 * Self-test of ptest.h: every assertion's pass path and fail path, the
 * failure message (file, line, test name, expression, values) and its cut at
 * PTEST_MESSAGE_SIZE, single evaluation of every argument, and the runner:
 * summary and exit status, a run with no tests, the PTEST_MAX_TESTS limit,
 * and registration. Failure paths run under ptest_capture, and runner tests
 * on their own registry through ptest_main_for, so what they check is
 * observed instead of failing this run.
 */
#include "ptest.h"

#include <time.h>

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
static bool has_location(const struct ptest_outcome *outcome, const struct probe *probe, const char *test_name)
{
    char expected[PTEST_MESSAGE_SIZE];
    const int length = snprintf(expected, sizeof expected, "%s:%d: %s: ", __FILE__, probe->assertion_line, test_name);
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
    struct probe probe = {false, 0};
    const struct ptest_outcome outcome = ptest_capture(assert_true, &probe);
    PT_ASSERT(!outcome.failed);
    PT_ASSERT(probe.reached_end);
}

PTEST(assert_failure_records_location_and_returns)
{
    struct probe probe = {false, 0};
    const struct ptest_outcome outcome = ptest_capture(assert_false, &probe);
    PT_ASSERT(outcome.failed);
    PT_ASSERT(!probe.reached_end);
    PT_ASSERT(has_location(&outcome, &probe, "assert_failure_records_location_and_returns"));
    PT_ASSERT(has_text(outcome.message, "PT_ASSERT(2 + 2 == 5)"));
    /* The capture must leave the running test's own state untouched. */
    PT_ASSERT(!ptest_current.outcome.failed);
}

static void fail_twice_through_helpers(void *context)
{
    struct probe *probe = context;
    assert_false(probe); /* first failure, returns from the helper only */
    PT_FAIL("second failure");
}

PTEST(first_failure_is_kept)
{
    struct probe probe = {false, 0};
    const struct ptest_outcome outcome = ptest_capture(fail_twice_through_helpers, &probe);
    PT_ASSERT(outcome.failed);
    PT_ASSERT(has_text(outcome.message, "PT_ASSERT(2 + 2 == 5)"));
    PT_ASSERT(!has_text(outcome.message, "second failure"));
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
    struct probe probe = {false, 0};
    const struct ptest_outcome outcome = ptest_capture(eq_u32_equal, &probe);
    PT_ASSERT(!outcome.failed);
    PT_ASSERT(probe.reached_end);
}

PTEST(eq_u32_failure_prints_both_values_in_hex)
{
    struct probe probe = {false, 0};
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
    struct probe probe = {false, 0};
    const struct ptest_outcome outcome = ptest_capture(eq_int_equal, &probe);
    PT_ASSERT(!outcome.failed);
    PT_ASSERT(probe.reached_end);
}

PTEST(eq_int_failure_prints_both_values_signed)
{
    struct probe probe = {false, 0};
    const struct ptest_outcome outcome = ptest_capture(eq_int_different, &probe);
    PT_ASSERT(outcome.failed);
    PT_ASSERT(!probe.reached_end);
    PT_ASSERT(has_location(&outcome, &probe, "eq_int_failure_prints_both_values_signed"));
    PT_ASSERT(has_text(outcome.message, "actual == 7: actual -5, expected 7"));
}

/* ---- PT_ASSERT_EQ_PTR ---- */

static const int ptr_targets[2] = {1, 2};

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
    struct probe probe = {false, 0};
    const struct ptest_outcome outcome = ptest_capture(eq_ptr_equal, &probe);
    PT_ASSERT(!outcome.failed);
    PT_ASSERT(probe.reached_end);
}

PTEST(eq_ptr_failure_prints_both_addresses)
{
    struct probe probe = {false, 0};
    const struct ptest_outcome outcome = ptest_capture(eq_ptr_different, &probe);
    char expected[128];
    const int length = snprintf(expected, sizeof expected, "actual %p, expected %p", (const void *)&ptr_targets[0],
                                (const void *)&ptr_targets[1]);
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
    const char built[] = {'p', 'o', 'k', 'e', '\0'};
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
    struct probe probe = {false, 0};
    const struct ptest_outcome outcome = ptest_capture(eq_str_equal, &probe);
    PT_ASSERT(!outcome.failed);
    PT_ASSERT(probe.reached_end);
}

PTEST(eq_str_failure_prints_both_strings)
{
    struct probe probe = {false, 0};
    const struct ptest_outcome outcome = ptest_capture(eq_str_different, &probe);
    PT_ASSERT(outcome.failed);
    PT_ASSERT(!probe.reached_end);
    PT_ASSERT(has_location(&outcome, &probe, "eq_str_failure_prints_both_strings"));
    PT_ASSERT(has_text(outcome.message, "actual \"abc\", expected \"abd\""));
}

PTEST(eq_str_fails_when_one_side_is_null)
{
    struct probe probe = {false, 0};
    const struct ptest_outcome outcome = ptest_capture(eq_str_null_actual, &probe);
    PT_ASSERT(outcome.failed);
    PT_ASSERT(!probe.reached_end);
    PT_ASSERT(has_text(outcome.message, "actual \"(null)\", expected \"x\""));
}

/* ---- PT_ASSERT_MEM_EQ ---- */

static void mem_eq_equal(void *context)
{
    struct probe *probe = context;
    const uint8_t a[4] = {1, 2, 3, 4};
    const uint8_t b[4] = {1, 2, 3, 4};
    PT_ASSERT_MEM_EQ(a, b, sizeof a);
    PT_ASSERT_MEM_EQ(NULL, NULL, 0);
    probe->reached_end = true;
}

static void mem_eq_different(void *context)
{
    struct probe *probe = context;
    const uint8_t a[6] = {1, 2, 3, 0x04, 5, 6};
    const uint8_t b[6] = {1, 2, 3, 0xA9, 5, 0};
    probe->assertion_line = __LINE__ + 1;
    PT_ASSERT_MEM_EQ(a, b, sizeof a);
    probe->reached_end = true;
}

static void mem_eq_null_with_length(void *context)
{
    struct probe *probe = context;
    const uint8_t b[1] = {0};
    PT_ASSERT_MEM_EQ(NULL, b, sizeof b);
    probe->reached_end = true;
}

PTEST(mem_eq_passes_on_equal_bytes_and_zero_length)
{
    struct probe probe = {false, 0};
    const struct ptest_outcome outcome = ptest_capture(mem_eq_equal, &probe);
    PT_ASSERT(!outcome.failed);
    PT_ASSERT(probe.reached_end);
}

PTEST(mem_eq_failure_prints_first_difference)
{
    struct probe probe = {false, 0};
    const struct ptest_outcome outcome = ptest_capture(mem_eq_different, &probe);
    PT_ASSERT(outcome.failed);
    PT_ASSERT(!probe.reached_end);
    PT_ASSERT(has_location(&outcome, &probe, "mem_eq_failure_prints_first_difference"));
    PT_ASSERT(has_text(outcome.message, "over 6 bytes: first difference at offset 3: actual 0x04, expected 0xA9"));
}

PTEST(mem_eq_fails_on_null_with_nonzero_length)
{
    struct probe probe = {false, 0};
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
    struct probe probe = {false, 0};
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

/* How often each argument of an assertion was evaluated. */
struct evaluations {
    uint32_t actual, expected, nbytes;
};

static const uint8_t evaluated_bytes[2] = {7, 7};

static void count_pointer_evaluations(void *context)
{
    struct evaluations *const count = context;
    PT_ASSERT_EQ_PTR((count->actual++, &evaluated_bytes[0]), (count->expected++, &evaluated_bytes[0]));
    PT_ASSERT_MEM_EQ((count->actual++, &evaluated_bytes[0]), (count->expected++, &evaluated_bytes[1]),
                     (count->nbytes++, sizeof evaluated_bytes[0]));
}

PTEST(pointer_and_memory_arguments_are_evaluated_once)
{
    struct evaluations count = {0, 0, 0};
    const struct ptest_outcome outcome = ptest_capture(count_pointer_evaluations, &count);
    PT_ASSERT(!outcome.failed);
    PT_ASSERT_EQ_U32(count.actual, 2);
    PT_ASSERT_EQ_U32(count.expected, 2);
    PT_ASSERT_EQ_U32(count.nbytes, 1);
}

/* ---- truncation ---- */

static void fail_with_long_message(void *context)
{
    (void)context;
    char message[2 * PTEST_MESSAGE_SIZE];
    memset(message, 'x', sizeof message - 1);
    message[sizeof message - 1] = '\0';
    PT_FAIL(message);
}

PTEST(long_failure_message_is_cut_to_message_size)
{
    const struct ptest_outcome outcome = ptest_capture(fail_with_long_message, NULL);
    const char *const terminator = memchr(outcome.message, '\0', sizeof outcome.message);
    PT_ASSERT(outcome.failed);
    PT_ASSERT(terminator == outcome.message + PTEST_MESSAGE_SIZE - 1);
    PT_ASSERT(has_text(outcome.message, ": long_failure_message_is_cut_to_message_size: PT_FAIL: xxx"));
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

#define RUN_TEXT_SIZE 2048 /* room for everything the runs below print */

/* A run of a registry: its exit status and what it printed. */
struct run {
    int status;
    bool text_complete; /* all output was read back and fits in text */
    char text[RUN_TEXT_SIZE];
};

static struct run run_registry(const struct ptest_registry *registry)
{
    struct run run = {.status = -1, .text_complete = false, .text = ""};
    FILE *const out = tmpfile();
    if (out == NULL)
        return run;
    run.status = ptest_main_for(registry, out);
    if (fseek(out, 0, SEEK_SET) == 0) {
        const size_t length = fread(run.text, 1, sizeof run.text - 1, out);
        run.text[length] = '\0';
        run.text_complete = !ferror(out) && length < sizeof run.text - 1;
    }
    if (fclose(out) != 0)
        run.text_complete = false;
    return run;
}

PTEST(runner_reports_failures_and_returns_nonzero)
{
    struct ptest_registry registry = {.count = 0};
    ptest_register_in(&registry, "runner_passing_case", runner_passing_case);
    ptest_register_in(&registry, "runner_failing_case", runner_failing_case);
    const struct run run = run_registry(&registry);
    PT_ASSERT(run.text_complete);
    PT_ASSERT_EQ_INT(run.status, 1);
    PT_ASSERT(has_text(run.text, ": runner_failing_case: PT_FAIL: expected failure\n"));
    PT_ASSERT(count_text(run.text, "FAIL ") == 1);
    PT_ASSERT(has_text(run.text, "1 passed, 1 failed\n"));
    /* The nested run restores the running test's name and state. */
    PT_ASSERT_EQ_STR(ptest_current.name, "runner_reports_failures_and_returns_nonzero");
    PT_ASSERT(!ptest_current.outcome.failed);
}

PTEST(runner_returns_zero_when_all_pass)
{
    struct ptest_registry registry = {.count = 0};
    ptest_register_in(&registry, "first", runner_passing_case);
    ptest_register_in(&registry, "second", runner_passing_case);
    const struct run run = run_registry(&registry);
    PT_ASSERT(run.text_complete);
    PT_ASSERT_EQ_INT(run.status, 0);
    PT_ASSERT_EQ_STR(run.text, "2 passed, 0 failed\n");
}

PTEST(runner_fails_when_no_tests_are_registered)
{
    const struct ptest_registry registry = {.count = 0};
    const struct run run = run_registry(&registry);
    PT_ASSERT(run.text_complete);
    PT_ASSERT_EQ_INT(run.status, 1);
    PT_ASSERT_EQ_STR(run.text, "ptest: no tests registered\n");
}

PTEST(registry_holds_exactly_max_tests)
{
    struct ptest_registry registry = {.count = 0};
    for (int i = 0; i < PTEST_MAX_TESTS; i++)
        ptest_register_in(&registry, "passing", runner_passing_case);
    char expected[64];
    const int length = snprintf(expected, sizeof expected, "%d passed, 0 failed\n", PTEST_MAX_TESTS);
    PT_ASSERT(length > 0 && (size_t)length < sizeof expected);
    PT_ASSERT(!registry.overflowed);
    const struct run run = run_registry(&registry);
    PT_ASSERT(run.text_complete);
    PT_ASSERT_EQ_INT(run.status, 0);
    PT_ASSERT_EQ_STR(run.text, expected);
}

PTEST(registration_past_max_tests_fails_the_run_naming_the_limit)
{
    struct ptest_registry registry = {.count = 0};
    for (int i = 0; i <= PTEST_MAX_TESTS; i++)
        ptest_register_in(&registry, "passing", runner_passing_case);
    PT_ASSERT(registry.overflowed);
    PT_ASSERT(registry.count == PTEST_MAX_TESTS);
    const struct run run = run_registry(&registry);
    PT_ASSERT(run.text_complete);
    PT_ASSERT_EQ_INT(run.status, 1);
    PT_ASSERT(has_text(run.text, "raise PTEST_MAX_TESTS"));
    PT_ASSERT(!has_text(run.text, "passed")); /* no test ran */
}

/* A test name longer than a whole message: the line is cut, the run still fails. */
PTEST(runner_cuts_a_failure_line_whose_prefix_overflows)
{
    char long_name[PTEST_MESSAGE_SIZE + 100];
    memset(long_name, 'n', sizeof long_name - 1);
    long_name[sizeof long_name - 1] = '\0';
    struct ptest_registry registry = {.count = 0};
    ptest_register_in(&registry, long_name, runner_failing_case);
    const struct run run = run_registry(&registry);
    const char *const line = strstr(run.text, "FAIL ");
    const char *const line_end = line != NULL ? strchr(line, '\n') : NULL;
    PT_ASSERT(run.text_complete);
    PT_ASSERT_EQ_INT(run.status, 1);
    PT_ASSERT(line_end != NULL);
    PT_ASSERT(line_end - line == (ptrdiff_t)strlen("FAIL ") + PTEST_MESSAGE_SIZE - 1);
    PT_ASSERT(has_text(run.text, "0 passed, 1 failed\n"));
}

PTEST(registry_contains_this_test)
{
    bool found = false;
    for (size_t i = 0; i < ptest_tests.count; i++)
        found = found
                || (ptest_tests.cases[i].run == ptest_body_registry_contains_this_test
                    && strcmp(ptest_tests.cases[i].name, "registry_contains_this_test") == 0);
    PT_ASSERT(found);
    PT_ASSERT(!ptest_tests.overflowed);
}

/* ---- timeout ---- */

#define HANG_ELAPSED_LIMIT_SECONDS 10

static void runner_hanging_case(void)
{
    ptest_set_timeout(1); /* a short limit for this test only */
    sleep(PTEST_DEFAULT_TIMEOUT_SECONDS);
}

PTEST(runner_fails_a_hanging_test_and_runs_the_next)
{
    struct ptest_registry registry = {.count = 0};
    ptest_register_in(&registry, "runner_hanging_case", runner_hanging_case);
    ptest_register_in(&registry, "runner_passing_case", runner_passing_case);
    const time_t started = time(NULL);
    const struct run run = run_registry(&registry);
    const time_t elapsed = time(NULL) - started;
    PT_ASSERT(run.text_complete);
    PT_ASSERT(elapsed < HANG_ELAPSED_LIMIT_SECONDS); /* the 1 s limit fired, not the 60 s default */
    PT_ASSERT_EQ_INT(run.status, 1);
    PT_ASSERT(has_text(run.text, ": runner_hanging_case: timed out after 1 seconds\n"));
    PT_ASSERT(count_text(run.text, "FAIL ") == 1);
    PT_ASSERT(has_text(run.text, "1 passed, 1 failed\n"));
    PT_ASSERT_EQ_STR(ptest_current.name, "runner_fails_a_hanging_test_and_runs_the_next");
    PT_ASSERT(!ptest_current.outcome.failed);
}

#define OUTER_ALARM_SECONDS 50u

PTEST(a_nested_run_restores_the_outer_alarm_target_and_limit)
{
    struct ptest_registry registry = {.count = 0};
    ptest_register_in(&registry, "runner_passing_case", runner_passing_case);
    const sigjmp_buf *const target_before = ptest_timeout_target;
    const unsigned limit_before = ptest_timeout_seconds;
    (void)alarm(OUTER_ALARM_SECONDS);
    const struct run run = run_registry(&registry);
    const unsigned remaining = alarm(0);
    PT_ASSERT(run.text_complete);
    PT_ASSERT_EQ_INT(run.status, 0);
    PT_ASSERT(remaining > 0 && remaining <= OUTER_ALARM_SECONDS); /* re-armed with what was left */
    PT_ASSERT(ptest_timeout_target == target_before);
    PT_ASSERT_EQ_INT(ptest_timeout_seconds, limit_before);
}

PTEST(timeout_comes_from_the_environment_when_it_is_a_positive_number)
{
    const char *const saved = getenv(PTEST_TIMEOUT_ENV);
    char previous[32] = "";
    if (saved != NULL)
        (void)snprintf(previous, sizeof previous, "%s", saved);
    unsigned seen[5];
    const char *const values[5] = {"7", "0", "abc", "5x", ""};
    for (size_t i = 0; i < 5; i++) {
        setenv(PTEST_TIMEOUT_ENV, values[i], 1);
        seen[i] = ptest_default_timeout();
    }
    unsetenv(PTEST_TIMEOUT_ENV);
    const unsigned unset = ptest_default_timeout();
    if (saved != NULL)
        setenv(PTEST_TIMEOUT_ENV, previous, 1);
    PT_ASSERT_EQ_INT(seen[0], 7);
    for (size_t i = 1; i < 5; i++)
        PT_ASSERT_EQ_INT(seen[i], PTEST_DEFAULT_TIMEOUT_SECONDS);
    PT_ASSERT_EQ_INT(unset, PTEST_DEFAULT_TIMEOUT_SECONDS);
}

PTEST_MAIN()
