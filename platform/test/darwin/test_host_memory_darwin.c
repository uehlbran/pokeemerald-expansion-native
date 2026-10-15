/*
 * test_host_memory_darwin.c: where gba_regions.macho.s puts the battle
 * runner's stack on macOS, 0x01800000 as on Linux, 1 MiB (host_memory.h):
 * the place no portable test can name, since Windows' is 0x01801000. The
 * rest is in ../test_host_memory.c.
 */
#include <stdint.h>

#include "host_memory.h"
#include "ptest.h"

#define DARWIN_CALL_STACK_ADDRESS 0x01800000u
#define CALL_STACK_SIZE (1024u * 1024u)

PTEST(the_call_stack_is_linked_at_0x01800000)
{
    const struct host_span stack = host_memory_call_stack();
    PT_ASSERT(stack.start == DARWIN_CALL_STACK_ADDRESS);
    PT_ASSERT(stack.end - stack.start == CALL_STACK_SIZE);
}

PTEST_MAIN()
