/*
 * test_host_thread_darwin.c: that a watchdog leaves no thread behind
 * (host_thread.h), seen in macOS's own list of the task's threads
 * (task_threads, each named by its unique id from thread_info), as
 * ../linux/test_host_thread_linux.c sees it in /proc/self/task. The
 * watchdog's thread is the one id the list gains between host_watchdog_start
 * and its return, and after host_watchdog_stop that id must leave the list,
 * whether the deadline passed or not. macOS's watchdog waits with
 * pthread_cond_timedwait_relative_np (host_macos.c), a wait of its own that
 * this exercises to both ends. The rest is in ../test_host_thread.c and
 * ../posix/test_host_thread_posix.c.
 */
#include <mach/mach.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "host_thread.h"
#include "ptest.h"
#include "test_support.h"

#define MAX_THREADS 256u
#define EXPIRY_LIMIT_MILLISECONDS 10000u
#define SETTLE_LIMIT_MILLISECONDS 2000u

struct thread_list {
    uint64_t ids[MAX_THREADS];
    size_t count;
};

/* The id of the thread a port names, or 0 if thread_info cannot say. */
static uint64_t thread_id(thread_act_t thread)
{
    thread_identifier_info_data_t info;
    mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;
    if (thread_info(thread, THREAD_IDENTIFIER_INFO, (thread_info_t)&info, &count) != KERN_SUCCESS)
        return 0;
    return info.thread_id;
}

/* Fills the list with the task's threads' ids; false if it cannot be read or holds more than MAX_THREADS. The ports
 * task_threads gives, and the array they come in, are given back. */
static bool read_threads(struct thread_list *list)
{
    thread_act_array_t threads;
    mach_msg_type_number_t count = 0;
    if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS)
        return false;
    bool complete = count <= MAX_THREADS;
    list->count = 0;
    for (mach_msg_type_number_t i = 0; i < count; i++) {
        const uint64_t id = thread_id(threads[i]);
        complete = complete && id != 0;
        if (complete)
            list->ids[list->count++] = id;
        (void)mach_port_deallocate(mach_task_self(), threads[i]);
    }
    const kern_return_t freed = vm_deallocate(mach_task_self(), (vm_address_t)threads,
                                              (vm_size_t)count * sizeof threads[0]);
    return complete && freed == KERN_SUCCESS;
}

static bool has_thread(const struct thread_list *list, uint64_t id)
{
    for (size_t i = 0; i < list->count; i++)
        if (list->ids[i] == id)
            return true;
    return false;
}

/* The id that is in `after` and not in `before`, or 0 unless that is exactly one id. */
static uint64_t only_new_thread(const struct thread_list *before, const struct thread_list *after)
{
    uint64_t found = 0;
    for (size_t i = 0; i < after->count; i++) {
        if (has_thread(before, after->ids[i]))
            continue;
        if (found != 0)
            return 0;
        found = after->ids[i];
    }
    return found;
}

/* Whether the thread is gone from the list, waiting up to the limit for it: the join returns once the thread has
 * finished, which may be a moment before the kernel takes it off the task's list. */
static bool thread_gone(uint64_t id)
{
    struct thread_list now;
    for (unsigned waited = 0;; waited++) {
        if (!read_threads(&now))
            return false;
        if (!has_thread(&now, id))
            return true;
        if (waited >= SETTLE_LIMIT_MILLISECONDS)
            return false;
        tsup_sleep_milliseconds(1);
    }
}

/* Starts a watchdog and returns its thread's id, or 0 with *watchdog NULL if it did not show as exactly one. */
static uint64_t start_watchdog(unsigned seconds, atomic_bool *expired, struct host_watchdog **watchdog)
{
    static struct thread_list before;
    static struct thread_list after;
    *watchdog = NULL;
    if (!read_threads(&before))
        return 0;
    *watchdog = host_watchdog_start(seconds, expired);
    if (*watchdog == NULL)
        return 0;
    const uint64_t id = read_threads(&after) ? only_new_thread(&before, &after) : 0;
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
    const uint64_t id = start_watchdog(30, &expired, &watchdog);
    PT_ASSERT(watchdog != NULL);
    PT_ASSERT(id != 0);
    host_watchdog_stop(watchdog);
    PT_ASSERT(!atomic_load_explicit(&expired, memory_order_relaxed));
    PT_ASSERT(thread_gone(id));
}

PTEST(a_watchdog_whose_deadline_passed_leaves_no_thread_either)
{
    atomic_bool expired = false;
    struct host_watchdog *watchdog;
    const uint64_t id = start_watchdog(1, &expired, &watchdog);
    PT_ASSERT(watchdog != NULL);
    PT_ASSERT(id != 0);
    for (unsigned waited = 0; waited < EXPIRY_LIMIT_MILLISECONDS; waited++) {
        if (atomic_load_explicit(&expired, memory_order_relaxed))
            break;
        tsup_sleep_milliseconds(1);
    }
    host_watchdog_stop(watchdog);
    PT_ASSERT(atomic_load_explicit(&expired, memory_order_relaxed));
    PT_ASSERT(thread_gone(id));
}

PTEST_MAIN()
