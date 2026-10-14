/*
 * test_ptest_time_limit.c: ptest.h's per-test time limit. A watchdog thread
 * (host_thread.h) sets a flag when the limit passes and the test's own thread
 * finds it in its next assertion or when the test returns, abandons the test
 * and runs the next. The runs here are nested runs on registries of their
 * own (ptest_main_for), so the failure they provoke is observed instead of
 * failing this one. The tests wait seconds, so they are kept apart from
 * test_ptest.c.
 */
#include "ptest.h"
#include "test_support.h"

#define MILLISECONDS_PER_SECOND 1000u
#define POLL_MILLISECONDS 10u
#define SHORT_LIMIT_SECONDS 1u
#define STUCK_SECONDS 60u /* how long a stuck case would run: far past any limit used here */
#define LATE_SECONDS 2u   /* a case that finishes after a limit of SHORT_LIMIT_SECONDS */
#define ELAPSED_LIMIT_SECONDS 10u
#define OUTPUT_SIZE 2048

/* Waits up to seconds, asserting as a loop in a test must so that the limit can reach it. */
static void wait_asserting(unsigned seconds)
{
    const uint64_t until = tsup_monotonic_milliseconds() + (uint64_t)seconds * MILLISECONDS_PER_SECOND;
    while (tsup_monotonic_milliseconds() < until) {
        PT_ASSERT(true);
        tsup_sleep_milliseconds(POLL_MILLISECONDS);
    }
}

static void runner_hanging_case(void)
{
    ptest_set_timeout(SHORT_LIMIT_SECONDS); /* a short limit for this test only */
    wait_asserting(STUCK_SECONDS);
}

static void environment_limited_case(void)
{
    wait_asserting(STUCK_SECONDS);
}

static void lengthened_case(void)
{
    ptest_set_timeout(SHORT_LIMIT_SECONDS + LATE_SECONDS + 1u);
    wait_asserting(LATE_SECONDS);
}

static void late_case_without_a_later_assertion(void)
{
    ptest_set_timeout(SHORT_LIMIT_SECONDS);
    tsup_sleep_milliseconds(LATE_SECONDS * MILLISECONDS_PER_SECOND);
}

static void runner_passing_case(void)
{
    PT_ASSERT(true);
}

static void failing_case(void)
{
    PT_FAIL("expected failure");
}

struct run {
    int status;
    bool text_complete;
    uint64_t milliseconds;
    char text[OUTPUT_SIZE];
};

static struct run run_registry(const struct ptest_registry *registry)
{
    struct run run = {.status = -1, .text_complete = false, .milliseconds = 0, .text = ""};
    FILE *const out = tmpfile();
    if (out == NULL)
        return run;
    const uint64_t started = tsup_monotonic_milliseconds();
    run.status = ptest_main_for(registry, out);
    run.milliseconds = tsup_monotonic_milliseconds() - started;
    if (fseek(out, 0, SEEK_SET) == 0) {
        const size_t length = fread(run.text, 1, sizeof run.text - 1, out);
        run.text[length] = '\0';
        run.text_complete = !ferror(out) && length < sizeof run.text - 1;
    }
    if (fclose(out) != 0)
        run.text_complete = false;
    return run;
}

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

PTEST(runner_fails_a_hanging_test_and_runs_the_next)
{
    struct ptest_registry registry = {.count = 0};
    ptest_register_in(&registry, "runner_hanging_case", runner_hanging_case);
    ptest_register_in(&registry, "runner_passing_case", runner_passing_case);
    const struct run run = run_registry(&registry);
    PT_ASSERT(run.text_complete);
    PT_ASSERT(run.milliseconds < ELAPSED_LIMIT_SECONDS * MILLISECONDS_PER_SECOND); /* the 1 s limit fired, not 60 s */
    PT_ASSERT_EQ_INT(run.status, 1);
    PT_ASSERT(has_text(run.text, ": runner_hanging_case: timed out after 1 seconds\n"));
    PT_ASSERT(count_text(run.text, "FAIL ") == 1);
    PT_ASSERT(has_text(run.text, "1 passed, 1 failed\n"));
    PT_ASSERT_EQ_STR(ptest_current.name, "runner_fails_a_hanging_test_and_runs_the_next");
    PT_ASSERT(!ptest_current.outcome.failed);
}

PTEST(the_limit_is_found_after_it_and_within_one_second_of_it)
{
    struct ptest_registry registry = {.count = 0};
    ptest_register_in(&registry, "runner_hanging_case", runner_hanging_case);
    const struct run run = run_registry(&registry);
    PT_ASSERT_EQ_INT(run.status, 1);
    PT_ASSERT(run.milliseconds >= SHORT_LIMIT_SECONDS * MILLISECONDS_PER_SECOND);
    PT_ASSERT(run.milliseconds < (SHORT_LIMIT_SECONDS + 1u) * MILLISECONDS_PER_SECOND);
}

PTEST(a_test_that_returns_late_has_timed_out_too)
{
    struct ptest_registry registry = {.count = 0};
    ptest_register_in(&registry, "late_case_without_a_later_assertion", late_case_without_a_later_assertion);
    const struct run run = run_registry(&registry);
    PT_ASSERT(run.text_complete);
    PT_ASSERT_EQ_INT(run.status, 1);
    PT_ASSERT(has_text(run.text, ": late_case_without_a_later_assertion: timed out after 1 seconds\n"));
}

PTEST(the_environment_sets_the_limit_of_a_test_that_does_not_set_its_own)
{
    struct ptest_registry registry = {.count = 0};
    ptest_register_in(&registry, "environment_limited_case", environment_limited_case);
    PT_ASSERT(tsup_set_environment(PTEST_TIMEOUT_ENV, "1"));
    const struct run run = run_registry(&registry);
    PT_ASSERT(tsup_unset_environment(PTEST_TIMEOUT_ENV));
    PT_ASSERT(run.text_complete);
    PT_ASSERT_EQ_INT(run.status, 1);
    PT_ASSERT(has_text(run.text, ": environment_limited_case: timed out after 1 seconds\n"));
}

PTEST(a_test_that_sets_a_longer_limit_than_the_environments_is_not_cut_short)
{
    struct ptest_registry registry = {.count = 0};
    ptest_register_in(&registry, "lengthened_case", lengthened_case);
    PT_ASSERT(tsup_set_environment(PTEST_TIMEOUT_ENV, "1"));
    const struct run run = run_registry(&registry);
    PT_ASSERT(tsup_unset_environment(PTEST_TIMEOUT_ENV));
    PT_ASSERT(run.text_complete);
    PT_ASSERT_EQ_INT(run.status, 0);
    PT_ASSERT(has_text(run.text, "1 passed, 0 failed\n"));
    PT_ASSERT(run.milliseconds >= LATE_SECONDS * MILLISECONDS_PER_SECOND);
}

PTEST(tests_that_finish_in_time_leave_the_report_and_the_exit_as_without_a_limit)
{
    struct ptest_registry registry = {.count = 0};
    ptest_register_in(&registry, "runner_passing_case", runner_passing_case);
    ptest_register_in(&registry, "failing_case", failing_case);
    ptest_register_in(&registry, "runner_passing_again", runner_passing_case);
    const struct run run = run_registry(&registry);
    PT_ASSERT(run.text_complete);
    PT_ASSERT_EQ_INT(run.status, 1); /* the failing case, not a limit */
    PT_ASSERT(!has_text(run.text, "timed out"));
    PT_ASSERT(count_text(run.text, "FAIL ") == 1);
    PT_ASSERT(has_text(run.text, "2 passed, 1 failed\n"));
    PT_ASSERT(run.milliseconds < SHORT_LIMIT_SECONDS * MILLISECONDS_PER_SECOND); /* no waiting for a deadline */
}

PTEST_MAIN()
