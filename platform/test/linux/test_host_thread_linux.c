/*
 * test_host_thread_linux.c: that a watchdog leaves no thread behind
 * (host_thread.h), seen in Linux's own list of the process's threads,
 * /proc/self/task. The watchdog's thread is the one id that the list gains
 * between host_watchdog_start and its return (exactly one is itself checked),
 * and after host_watchdog_stop that id must leave the list, whether the
 * deadline passed or not, so that a join that is skipped shows (a skipped join
 * leaves the thread there for good; a done one, for at most a moment). Other
 * threads of the process, such as the previous test's own watchdog still on
 * its way out of the list, come and go unseen: the test never counts them. The
 * rest is in ../test_host_thread.c. A stop that signals the thread and then
 * detaches or returns early is not seen by the list, since the signalled
 * thread leaves it within that same moment: LeakSanitizer in the gate catches
 * it (the watchdog is never released), in all three thread test binaries.
 */
#include <dirent.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "host_thread.h"
#include "ptest.h"
#include "test_support.h"

#define TASK_DIRECTORY "/proc/self/task"
#define MAX_TASKS 256u
#define EXPIRY_LIMIT_MILLISECONDS 10000u
#define SETTLE_LIMIT_MILLISECONDS 2000u

struct task_list {
    long ids[MAX_TASKS];
    size_t count;
};

/* Fills the list from /proc/self/task; false if it cannot be read or holds more than MAX_TASKS. */
static bool read_tasks(struct task_list *list)
{
    list->count = 0;
    DIR *const directory = opendir(TASK_DIRECTORY);
    if (directory == NULL)
        return false;
    bool complete = true;
    for (;;) {
        errno = 0;
        const struct dirent *const entry = readdir(directory);
        if (entry == NULL) {
            complete = complete && errno == 0;
            break;
        }
        if (entry->d_name[0] == '.')
            continue;
        char *end;
        const long id = strtol(entry->d_name, &end, 10);
        if (*end != '\0' || id <= 0 || list->count == MAX_TASKS) { /* 0 is only_new_task's "none" */
            complete = false;
            break;
        }
        list->ids[list->count++] = id;
    }
    return closedir(directory) == 0 && complete;
}

static bool has_task(const struct task_list *list, long id)
{
    for (size_t i = 0; i < list->count; i++)
        if (list->ids[i] == id)
            return true;
    return false;
}

/*
 * The id that is in `after` and not in `before`, or 0 unless that is exactly
 * one id.
 */
static long only_new_task(const struct task_list *before, const struct task_list *after)
{
    long found = 0;
    for (size_t i = 0; i < after->count; i++) {
        if (has_task(before, after->ids[i]))
            continue;
        if (found != 0)
            return 0;
        found = after->ids[i];
    }
    return found;
}

/*
 * Whether the task is gone from the list, waiting up to the limit for it.
 * pthread_join returns when the kernel clears the thread's id at the end of
 * its exit, which is before the task leaves /proc/self/task, so the list can
 * show the stopped watchdog for a moment after host_watchdog_stop (seen once
 * on a two-core runner); the join itself is what the test is about.
 */
static bool task_gone(long id)
{
    struct task_list now;
    for (unsigned waited = 0;; waited++) {
        if (!read_tasks(&now))
            return false;
        if (!has_task(&now, id))
            return true;
        if (waited >= SETTLE_LIMIT_MILLISECONDS)
            return false;
        tsup_sleep_milliseconds(1);
    }
}

/* Starts a watchdog and returns the id of its thread, or 0 with *watchdog NULL if it did not show as exactly one. */
static long start_watchdog(unsigned seconds, atomic_bool *expired, struct host_watchdog **watchdog)
{
    struct task_list before;
    struct task_list after;
    *watchdog = NULL;
    if (!read_tasks(&before))
        return 0;
    *watchdog = host_watchdog_start(seconds, expired);
    if (*watchdog == NULL)
        return 0;
    const long id = read_tasks(&after) ? only_new_task(&before, &after) : 0;
    if (id == 0) {
        host_watchdog_stop(*watchdog);
        *watchdog = NULL;
    }
    return id;
}

PTEST(a_stopped_watchdog_leaves_no_thread)
{
    atomic_bool expired = false;
    struct host_watchdog *watchdog;
    const long id = start_watchdog(30, &expired, &watchdog);
    PT_ASSERT(watchdog != NULL);
    PT_ASSERT(id > 0);
    host_watchdog_stop(watchdog);
    PT_ASSERT(task_gone(id));
}

PTEST(a_watchdog_whose_deadline_passed_leaves_no_thread_either)
{
    atomic_bool expired = false;
    struct host_watchdog *watchdog;
    const long id = start_watchdog(1, &expired, &watchdog);
    PT_ASSERT(watchdog != NULL);
    PT_ASSERT(id > 0);
    for (unsigned waited = 0; waited < EXPIRY_LIMIT_MILLISECONDS; waited++) {
        if (atomic_load_explicit(&expired, memory_order_relaxed))
            break;
        tsup_sleep_milliseconds(1);
    }
    host_watchdog_stop(watchdog);
    PT_ASSERT(atomic_load_explicit(&expired, memory_order_relaxed));
    PT_ASSERT(task_gone(id));
}

PTEST_MAIN()
