/*
 * test_host_thread_linux.c: that a watchdog leaves no thread behind
 * (host_thread.h), counted from Linux's own list of the process's threads,
 * /proc/self/task: the same after a stop as before the start, whether the
 * deadline passed or not, so that a join that is skipped shows (a skipped join
 * leaves the thread there for good; a done one, for at most a moment). The
 * rest is in ../test_host_thread.c.
 */
#include <dirent.h>
#include <stdatomic.h>
#include <string.h>

#include "host_thread.h"
#include "ptest.h"
#include "test_support.h"

#define TASK_DIRECTORY "/proc/self/task"
#define PAST_ONE_SECOND_MILLISECONDS 1500u
#define SETTLE_LIMIT_MILLISECONDS 2000u

/* The number of threads in the process, or 0 if the list cannot be read. */
static size_t thread_count(void)
{
    DIR *const directory = opendir(TASK_DIRECTORY);
    if (directory == NULL)
        return 0;
    size_t count = 0;
    for (const struct dirent *entry = readdir(directory); entry != NULL; entry = readdir(directory))
        if (entry->d_name[0] != '.')
            count++;
    return closedir(directory) == 0 ? count : 0;
}

/*
 * The thread count once a joined thread is gone from the list, or the count
 * as it stands when that takes longer than the limit. pthread_join returns
 * when the kernel clears the thread's id at the end of its exit, which is
 * before the task leaves /proc/self/task, so the list can show the stopped
 * watchdog for a moment after host_watchdog_stop (seen once on a two-core
 * runner); the join itself is what the test is about.
 */
static size_t thread_count_settled(size_t expected)
{
    size_t count = thread_count();
    for (unsigned waited = 0; count != expected && waited < SETTLE_LIMIT_MILLISECONDS; waited++) {
        tsup_sleep_milliseconds(1);
        count = thread_count();
    }
    return count;
}

PTEST(a_stopped_watchdog_leaves_no_thread)
{
    atomic_bool expired = false;
    const size_t before = thread_count();
    PT_ASSERT(before > 0);
    struct host_watchdog *const watchdog = host_watchdog_start(30, &expired);
    PT_ASSERT(watchdog != NULL);
    const size_t during = thread_count();
    host_watchdog_stop(watchdog);
    PT_ASSERT_EQ_INT(during, before + 1);
    PT_ASSERT_EQ_INT(thread_count_settled(before), before);
}

PTEST(a_watchdog_whose_deadline_passed_leaves_no_thread_either)
{
    atomic_bool expired = false;
    const size_t before = thread_count();
    PT_ASSERT(before > 0);
    struct host_watchdog *const watchdog = host_watchdog_start(1, &expired);
    PT_ASSERT(watchdog != NULL);
    tsup_sleep_milliseconds(PAST_ONE_SECOND_MILLISECONDS);
    host_watchdog_stop(watchdog);
    PT_ASSERT(atomic_load_explicit(&expired, memory_order_relaxed));
    PT_ASSERT_EQ_INT(thread_count_settled(before), before);
}

PTEST_MAIN()
