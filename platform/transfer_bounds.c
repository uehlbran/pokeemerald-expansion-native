/*
 * transfer_bounds.c: which memory a DMA or BIOS copy may touch.
 *
 * Stands in for the GBA bus. On the GBA a transfer may read any mapped
 * region and write the RAM regions; writes to ROM are ignored (GBATEK "GBA
 * Memory Map"). In the native process the game's memory is three things:
 *
 *  - the GBA regions (gba_memmap.h): read and write. The I/O region is
 *    writable here although the hardware ignores writes to some registers;
 *    that filtering belongs to the I/O code, not to this check.
 *  - the executable image, which holds what the GBA's ROM and IWRAM held.
 *    Read: __executable_start to _end (code, read-only data, .data, .bss),
 *    GNU ld symbols. Write: __data_start (provided by glibc's crt1.o on
 *    -m32 and -m64) to _end, the writable globals; code and rodata are not.
 *  - the main thread's stack: read and write, the whole mapping glibc
 *    reports through pthread_getattr_np (not just the current depth), for
 *    the local buffers the game copies from and into. Chosen over parsing
 *    /proc/self/maps because it needs no text parsing. It must be called on
 *    the main thread: glibc then derives the bounds from the stack limit.
 *
 * Everything else is refused, including the native heap (malloc): the game
 * has no native heap, so a transfer naming one is a bug or an attack.
 * Address 0 and the first page are never allowed.
 */
#define _GNU_SOURCE /* pthread_getattr_np */
#include "transfer_bounds.h"

#include "gba_memmap.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define TRANSFER_NULL_PAGE_SIZE ((uintptr_t)0x1000) /* never accessible, any range touching it */
#define TRANSFER_ERR_STACK (-1)

extern char __executable_start[], __data_start[], _end[]; /* GNU ld / glibc crt1 */

struct transfer_range {
    uintptr_t start;
    uintptr_t end; /* one past the last byte */
};

static struct {
    bool ready;
    bool warned;
    struct transfer_range image;
    struct transfer_range writable_image;
    struct transfer_range stack;
} state;

static uintptr_t address_of(const void *pointer)
{
    uintptr_t address = 0;
    memcpy(&address, &pointer, sizeof address);
    return address;
}

/* address is already known to be >= range.start */
static bool range_holds(const struct transfer_range *range, uintptr_t address, size_t length)
{
    return address >= range->start && address < range->end && length <= range->end - address;
}

bool transfer_bounds_ok(uintptr_t address, size_t length, enum transfer_access access)
{
    if (!state.ready) {
        if (!state.warned) {
            fprintf(stderr, "transfer_bounds: not initialised, refusing every transfer\n");
            state.warned = true;
        }
        return false;
    }
    if (length == 0 || address < TRANSFER_NULL_PAGE_SIZE || length > UINTPTR_MAX - address) {
        return false;
    }
    for (int r = GBA_REGION_EWRAM; r < GBA_REGION_COUNT; r++) {
        if (gba_memmap_contains(address, length, (enum gba_region)r)) {
            return true;
        }
    }
    if (range_holds(&state.stack, address, length)) {
        return true;
    }
    if (access == TRANSFER_WRITE) {
        return range_holds(&state.writable_image, address, length);
    }
    return range_holds(&state.image, address, length);
}

static int record_stack(struct transfer_range *stack)
{
    pthread_attr_t attributes;
    void *low = NULL;
    size_t size = 0;
    int error = pthread_getattr_np(pthread_self(), &attributes);
    if (error != 0) {
        fprintf(stderr, "transfer_bounds: pthread_getattr_np failed: %s\n", strerror(error));
        return TRANSFER_ERR_STACK;
    }
    error = pthread_attr_getstack(&attributes, &low, &size);
    if (error != 0) {
        fprintf(stderr, "transfer_bounds: pthread_attr_getstack failed: %s\n", strerror(error));
    }
    int destroy_error = pthread_attr_destroy(&attributes);
    if (destroy_error != 0) {
        fprintf(stderr, "transfer_bounds: pthread_attr_destroy failed: %s\n", strerror(destroy_error));
    }
    if (error != 0 || destroy_error != 0 || size == 0 || size > UINTPTR_MAX - address_of(low)) {
        return TRANSFER_ERR_STACK;
    }
    stack->start = address_of(low);
    stack->end = stack->start + size;
    return 0;
}

int transfer_bounds_init(void)
{
    struct transfer_range stack;
    transfer_bounds_shutdown();
    int error = record_stack(&stack);
    if (error != 0) {
        return error;
    }
    state.stack = stack;
    state.image.start = address_of(__executable_start);
    state.image.end = address_of(_end);
    state.writable_image.start = address_of(__data_start);
    state.writable_image.end = address_of(_end);
    state.ready = true;
    return 0;
}

void transfer_bounds_shutdown(void)
{
    memset(&state, 0, sizeof state);
}
