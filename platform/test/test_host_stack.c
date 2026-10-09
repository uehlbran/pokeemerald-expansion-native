/*
 * test_host_stack.c: running a function on a stack of the caller's choosing
 * (host_stack.h), on the battle runner's stack (host_memory_call_stack) as
 * game_hooks uses it. The entry runs on that stack with its context, at the
 * same depth every call, and host_stack_leave from inside it is back in the
 * caller at once, after which the next call works as the first did.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "host_memory.h"
#include "host_stack.h"
#include "ptest.h"

/* What the last entry did. */
static struct {
    int calls;
    uintptr_t frame;
    const void *context;
    bool ran_past_leave;
} ran;

static void record(void *context)
{
    ran.calls++;
    ran.frame = (uintptr_t)__builtin_frame_address(0);
    ran.context = context;
}

static void leave_early(void *context)
{
    ran.calls++;
    ran.context = context;
    host_stack_leave();
    ran.ran_past_leave = true; /* only if the switch back failed */
}

static int run_on_the_call_stack(void (*entry)(void *context), void *context)
{
    const struct host_span stack = host_memory_call_stack();
    return host_stack_run((void *)stack.start, (size_t)(stack.end - stack.start), entry, context);
}

static void forget(void)
{
    ran.calls = 0;
    ran.frame = 0;
    ran.context = NULL;
    ran.ran_past_leave = false;
}

PTEST(the_entry_runs_on_the_given_stack_with_its_context)
{
    int context = 0;
    forget();
    PT_ASSERT_EQ_INT(run_on_the_call_stack(record, &context), 0);
    const struct host_span stack = host_memory_call_stack();
    PT_ASSERT_EQ_INT(ran.calls, 1);
    PT_ASSERT(ran.context == &context);
    PT_ASSERT(ran.frame >= stack.start && ran.frame < stack.end);
}

PTEST(the_entry_starts_at_the_same_depth_every_call)
{
    forget();
    PT_ASSERT_EQ_INT(run_on_the_call_stack(record, NULL), 0);
    const uintptr_t first = ran.frame;
    PT_ASSERT_EQ_INT(run_on_the_call_stack(record, NULL), 0);
    PT_ASSERT_EQ_INT(ran.calls, 2);
    PT_ASSERT(ran.frame == first);
}

PTEST(leave_returns_to_the_caller_at_once_and_the_next_call_works)
{
    int context = 0;
    forget();
    PT_ASSERT_EQ_INT(run_on_the_call_stack(record, NULL), 0);
    const uintptr_t depth = ran.frame;
    PT_ASSERT_EQ_INT(run_on_the_call_stack(leave_early, &context), 0);
    PT_ASSERT_EQ_INT(ran.calls, 2);
    PT_ASSERT(ran.context == &context);
    PT_ASSERT(!ran.ran_past_leave);
    PT_ASSERT_EQ_INT(run_on_the_call_stack(record, NULL), 0);
    PT_ASSERT_EQ_INT(ran.calls, 3);
    PT_ASSERT(ran.frame == depth);
}

PTEST_MAIN()
