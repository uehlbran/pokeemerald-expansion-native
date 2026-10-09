/*
 * test_host_image.c: where the executable's code and data, and the main
 * thread's stack, lie (host_image.h). The bounds contain main, this file's
 * globals and the current stack frame, each where it belongs: code and
 * read-only data in the image but not its writable part, mutable globals in
 * both, and none of them, nor the heap, a GBA region or the stack, read-only
 * but the first two.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gba_memmap.h"
#include "host_image.h"
#include "ptest.h"

int main(void); /* PTEST_MAIN's, below: the code whose address is taken */

#define BYTE_COUNT 16u

static const uint8_t read_only_bytes[BYTE_COUNT] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
static uint8_t data_bytes[BYTE_COUNT] = {1};
static uint8_t bss_bytes[BYTE_COUNT];

/* An object's address as an integer (memcpy, not a cast of the pointer). */
static uintptr_t address_of(const void *pointer)
{
    uintptr_t address = 0;
    memcpy(&address, &pointer, sizeof address);
    return address;
}

/* main's address, through a volatile pointer so that the load is kept. */
static uintptr_t main_address(void)
{
    /* cppcheck-suppress constVariablePointer ; a function pointer has no const pointee to add, and volatile keeps the load */
    int (*volatile function)(void) = main;
    uintptr_t address = 0;
    memcpy(&address, (const void *)&function, sizeof address);
    return address;
}

/* The current frame, which is on the real stack even where AddressSanitizer
 * moves locals to its fake stack. */
static uintptr_t frame_address(void)
{
    return address_of(__builtin_frame_address(0));
}

static bool holds(struct host_span span, uintptr_t address, size_t length)
{
    return address >= span.start && address < span.end && length <= span.end - address;
}

PTEST(the_image_holds_main_and_every_global)
{
    const struct host_span image = host_image_whole();
    PT_ASSERT(image.start < image.end);
    PT_ASSERT(holds(image, main_address(), 1));
    PT_ASSERT(holds(image, address_of(read_only_bytes), sizeof read_only_bytes));
    PT_ASSERT(holds(image, address_of(data_bytes), sizeof data_bytes));
    PT_ASSERT(holds(image, address_of(bss_bytes), sizeof bss_bytes));
}

PTEST(the_writable_part_holds_the_mutable_globals_only)
{
    const struct host_span image = host_image_whole();
    const struct host_span writable = host_image_writable();
    PT_ASSERT(writable.start > image.start);
    PT_ASSERT(writable.end == image.end);
    PT_ASSERT(holds(writable, address_of(data_bytes), sizeof data_bytes));
    PT_ASSERT(holds(writable, address_of(bss_bytes), sizeof bss_bytes));
    PT_ASSERT(!holds(writable, address_of(read_only_bytes), 1));
    PT_ASSERT(!holds(writable, main_address(), 1));
}

PTEST(code_and_read_only_data_are_read_only)
{
    PT_ASSERT(host_image_is_read_only(main_address()));
    PT_ASSERT(host_image_is_read_only(address_of(read_only_bytes)));
    PT_ASSERT(host_image_is_read_only(address_of("a string literal")));
}

PTEST(nothing_writable_is_read_only)
{
    uint8_t *heap = malloc(BYTE_COUNT);
    const bool heap_read_only = heap != NULL && host_image_is_read_only(address_of(heap));
    free(heap);
    PT_ASSERT(heap != NULL);
    PT_ASSERT(!heap_read_only);
    PT_ASSERT(!host_image_is_read_only(address_of(data_bytes)));
    PT_ASSERT(!host_image_is_read_only(address_of(bss_bytes)));
    PT_ASSERT(!host_image_is_read_only(frame_address()));
    PT_ASSERT(!host_image_is_read_only(GBA_IWRAM_BASE));
    PT_ASSERT(!host_image_is_read_only(0));
    PT_ASSERT(!host_image_is_read_only(UINTPTR_MAX));
}

PTEST(the_main_stack_holds_the_current_frame_and_not_the_image)
{
    struct host_span stack = {0, 0};
    PT_ASSERT_EQ_INT(host_image_main_stack(&stack), 0);
    const struct host_span image = host_image_whole();
    PT_ASSERT(stack.start < stack.end);
    PT_ASSERT(holds(stack, frame_address(), 1));
    PT_ASSERT(!holds(stack, main_address(), 1));
    PT_ASSERT(stack.end <= image.start || stack.start >= image.end);
}

PTEST_MAIN()
