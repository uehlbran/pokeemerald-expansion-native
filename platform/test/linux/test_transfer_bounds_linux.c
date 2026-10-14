// deps: gba_memmap.c transfer_bounds.c
/*
 * Tests of transfer_bounds against what only Linux says about the process:
 * the thread library's own idea of the main stack (pthread_getattr_np) and
 * the linker's symbols for the image (GNU ld, glibc's crt1:
 * __executable_start, __data_start, _end). The rest is in
 * ../test_transfer_bounds.c. A macOS port reads the same facts its own way.
 */
#define _GNU_SOURCE /* pthread_getattr_np */
#include <pthread.h>
#include <stdint.h>

#include "gba_memmap.h"
#include "ptest.h"
#include "test_support.h"
#include "transfer_bounds.h"

extern char __executable_start[], __data_start[], _end[]; /* GNU ld / glibc crt1 */

static void start(void)
{
    PT_ASSERT_EQ_INT(transfer_bounds_init(), 0);
}

PTEST(real_stack_mapping_edges)
{
    pthread_attr_t attributes;
    void *low = NULL;
    size_t size = 0;
    PT_ASSERT_EQ_INT(pthread_getattr_np(pthread_self(), &attributes), 0);
    int error = pthread_attr_getstack(&attributes, &low, &size);
    PT_ASSERT_EQ_INT(pthread_attr_destroy(&attributes), 0);
    PT_ASSERT_EQ_INT(error, 0);
    uintptr_t first = tsup_address_of(low);
    uintptr_t last = first + size - 1;
    start();
    for (int a = TRANSFER_READ; a <= TRANSFER_WRITE; a++) {
        enum transfer_access access = (enum transfer_access)a;
        PT_ASSERT(transfer_bounds_ok(first, 1, access));
        PT_ASSERT(transfer_bounds_ok(last, 1, access));
        PT_ASSERT(transfer_bounds_ok(first, size, access));
        PT_ASSERT(!transfer_bounds_ok(last + 1, 1, access));
        PT_ASSERT(!transfer_bounds_ok(last, 2, access));
        PT_ASSERT(!transfer_bounds_ok(first, size + 1, access));
    }
    transfer_bounds_shutdown();
}

PTEST(image_edges_by_linker_symbol)
{
    uintptr_t image_first = tsup_address_of(__executable_start);
    uintptr_t data_first = tsup_address_of(__data_start);
    uintptr_t image_end = tsup_address_of(_end);
    start();
    PT_ASSERT(transfer_bounds_ok(image_end - 1, 1, TRANSFER_READ));
    PT_ASSERT(transfer_bounds_ok(image_end - 1, 1, TRANSFER_WRITE));
    PT_ASSERT(!transfer_bounds_ok(image_end, 1, TRANSFER_READ));
    PT_ASSERT(!transfer_bounds_ok(image_end, 1, TRANSFER_WRITE));
    PT_ASSERT(!transfer_bounds_ok(image_end - 1, 2, TRANSFER_READ));
    PT_ASSERT(!transfer_bounds_ok(image_end - 1, 2, TRANSFER_WRITE));
    PT_ASSERT(transfer_bounds_ok(image_first, 1, TRANSFER_READ));
    PT_ASSERT(!transfer_bounds_ok(image_first, 1, TRANSFER_WRITE));
    PT_ASSERT(!transfer_bounds_ok(image_first - 1, 1, TRANSFER_READ));
    PT_ASSERT(!transfer_bounds_ok(image_first - 1, 2, TRANSFER_READ));
    PT_ASSERT(transfer_bounds_ok(data_first - 1, 1, TRANSFER_READ));
    PT_ASSERT(!transfer_bounds_ok(data_first - 1, 1, TRANSFER_WRITE));
    PT_ASSERT(!transfer_bounds_ok(data_first - 1, 2, TRANSFER_WRITE));
    PT_ASSERT(transfer_bounds_ok(data_first, 1, TRANSFER_WRITE));
    PT_ASSERT(transfer_bounds_ok(data_first, 1, TRANSFER_READ));
    transfer_bounds_shutdown();
}

PTEST_MAIN()
