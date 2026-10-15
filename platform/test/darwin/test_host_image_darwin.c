/*
 * test_host_image_darwin.c: the image and the main stack (host_image.h)
 * against what macOS says of them: the image starts at its Mach-O header,
 * which the link puts at 0x40000000 (platform/target.mk), and a constant that
 * holds an address, which Mach-O keeps in the writable __DATA segment
 * (__DATA,__const), is read-only data as on Linux, where the non-PIE link puts
 * it in a read-only segment; the main stack is the one pthread reports
 * (pthread_get_stackaddr_np, its top, and pthread_get_stacksize_np) and holds
 * this frame. The rest is in ../test_host_image.c.
 */
#include <mach-o/ldsyms.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "gba_memmap.h"
#include "host_image.h"
#include "ptest.h"
#include "test_support.h"

#define IMAGE_BASE 0x40000000u

static int target;
/* An address in constant data: clang puts it in __DATA,__const. */
static const int *const constant_pointer = &target;

static bool holds(struct host_span span, uintptr_t address, size_t length)
{
    return address >= span.start && address < span.end && length <= span.end - address;
}

PTEST(the_image_starts_at_its_header_at_0x40000000)
{
    const struct host_span image = host_image_whole();
    PT_ASSERT(image.start == tsup_address_of(&_mh_execute_header));
    PT_ASSERT(image.start == IMAGE_BASE);
}

PTEST(a_constant_that_holds_an_address_is_read_only_and_not_writable_data)
{
    PT_ASSERT(host_image_is_read_only(tsup_address_of(&constant_pointer)));
    PT_ASSERT(*constant_pointer == 0);
}

PTEST(the_gba_regions_below_the_image_are_not_read_only)
{
    PT_ASSERT(!host_image_is_read_only(GBA_EWRAM_BASE));
    PT_ASSERT(!host_image_is_read_only(GBA_SRAM_BASE));
}

PTEST(the_main_stack_is_pthreads_to_its_top)
{
    struct host_span stack = {0, 0};
    PT_ASSERT_EQ_INT(host_image_main_stack(&stack), 0);
    const uintptr_t top = tsup_address_of(pthread_get_stackaddr_np(pthread_self()));
    const size_t size = pthread_get_stacksize_np(pthread_self());
    PT_ASSERT(stack.end == top);
    PT_ASSERT(stack.end - stack.start == size);
    PT_ASSERT(holds(stack, tsup_address_of(__builtin_frame_address(0)), 1));
}

PTEST_MAIN()
