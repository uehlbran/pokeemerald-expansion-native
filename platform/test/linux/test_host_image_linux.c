/*
 * test_host_image_linux.c: the main stack's bounds (host_image.h) against
 * what Linux says of them: pthread's mapping of the main thread
 * (pthread_getattr_np, a GNU extension) to its last byte, and the stack
 * pointer the process started with (glibc's __libc_stack_end) in its last
 * page. The rest is in ../test_host_image.c.
 */
#define _GNU_SOURCE /* pthread_getattr_np */
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "host_image.h"
#include "ptest.h"
#include "test_support.h"

extern void *__libc_stack_end; /* glibc: the stack pointer at the process's start */

#define PAGE_SIZE_BYTES 4096u

static bool holds(struct host_span span, uintptr_t address, size_t length)
{
    return address >= span.start && address < span.end && length <= span.end - address;
}

PTEST(the_main_stack_is_pthreads_mapping_to_its_last_byte)
{
    struct host_span stack = {0, 0};
    PT_ASSERT_EQ_INT(host_image_main_stack(&stack), 0);
    pthread_attr_t attributes;
    void *low = NULL;
    size_t size = 0;
    PT_ASSERT_EQ_INT(pthread_getattr_np(pthread_self(), &attributes), 0);
    const int got = pthread_attr_getstack(&attributes, &low, &size);
    PT_ASSERT_EQ_INT(pthread_attr_destroy(&attributes), 0);
    PT_ASSERT_EQ_INT(got, 0);
    PT_ASSERT(stack.start == tsup_address_of(low));
    PT_ASSERT(stack.end == tsup_address_of(low) + size);
    PT_ASSERT(holds(stack, tsup_address_of(__libc_stack_end), 1));
    PT_ASSERT(stack.end - tsup_address_of(__libc_stack_end) <= PAGE_SIZE_BYTES); /* the top, to its last byte */
}

PTEST_MAIN()
