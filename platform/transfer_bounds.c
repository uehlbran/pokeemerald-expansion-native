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
 *    GNU ld symbols. Write: the writable globals, from __data_start
 *    (provided by glibc's crt1.o on -m32 and -m64) to _end, and the game's
 *    own initialised data, which gba_game.ld places just before .data
 *    (game_data_start, so that a soft reset can restore it); code and
 *    rodata are not.
 *  - the main thread's stack: read and write, the whole mapping glibc
 *    reports through pthread_getattr_np (not just the current depth), for
 *    the local buffers the game copies from and into. Chosen over parsing
 *    /proc/self/maps because it needs no text parsing. It must be called on
 *    the main thread: glibc then derives the bounds from the stack limit.
 *
 *  - the stack the battle test runner's functions run on (game_hooks.h,
 *    PlatformCallWithStack; gba_regions.ld .platform_call_stack): read and
 *    write, like the main thread's.
 *
 *  - under AddressSanitizer, the fake stack: its use-after-return detection
 *    moves locals to a heap-allocated "fake stack" frame, and the game copies
 *    from local buffers, so a range inside one fake-stack frame counts as
 *    stack. The detection stays on for the sanitized gates; this policy is
 *    not weakened to suit it.
 *
 * Everything else is refused, including the native heap (malloc): the game
 * has no native heap, so a transfer naming one is a bug or an attack.
 * Address 0 and the first page are never allowed. A range must lie inside one
 * region, one fake-stack frame or one of the image/stack ranges; wraparound
 * of address + length is refused by each check (gba_memmap_contains and
 * range_holds compare length with the room left).
 */
#define _GNU_SOURCE /* pthread_getattr_np */
#include "transfer_bounds.h"

#include "gba_memmap.h"

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#define TRANSFER_HAVE_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#define TRANSFER_HAVE_ASAN 1
#endif
#endif

#include <pthread.h>
#include <stdio.h>
#include <string.h>

/* Policy, not hardware: no transfer may touch the first page. A native NULL
 * (or a small offset from one) is never a GBA address, whatever the OS maps
 * there. Linux's vm.mmap_min_addr default is 65536 so nothing is mapped there
 * anyway; the 4 KiB page is the smallest unit all targets share. */
#define TRANSFER_FIRST_PAGE_SIZE ((uintptr_t)0x1000)
#define TRANSFER_ERR_STACK (-1)

extern char __executable_start[], __data_start[], _end[];           /* GNU ld / glibc crt1 */
extern char game_data_start[], game_data_end[];                     /* gba_game.ld */
extern char platform_call_stack_start[], platform_call_stack_end[]; /* gba_regions.ld */

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
    struct transfer_range call_stack;
} state;

static uintptr_t address_of(const void *pointer)
{
    uintptr_t address = 0;
    memcpy(&address, &pointer, sizeof address);
    return address;
}

/* Overflow-safe: length is compared with the room left, never added to address. */
static bool range_holds(const struct transfer_range *range, uintptr_t address, size_t length)
{
    return address >= range->start && address < range->end && length <= range->end - address;
}

/* True if the range lies inside one live ASan fake-stack frame. */
static bool in_fake_stack(uintptr_t address, size_t length)
{
#ifdef TRANSFER_HAVE_ASAN
    void *fake = __asan_get_current_fake_stack();
    void *begin = NULL;
    void *end = NULL;
    if (fake != NULL && __asan_addr_is_in_fake_stack(fake, (void *)address, &begin, &end) != NULL) {
        struct transfer_range frame = {address_of(begin), address_of(end)};
        return range_holds(&frame, address, length);
    }
#else
    (void)address;
    (void)length;
#endif
    return false;
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
    if (length == 0 || address < TRANSFER_FIRST_PAGE_SIZE) {
        return false;
    }
    /* One region: a range running from one region into the next is refused
     * even where two regions touch (mGBA debug page / palette RAM). */
    if (gba_memmap_region_of(address, length) != GBA_REGION_NONE) {
        return true;
    }
    if (range_holds(&state.stack, address, length) || range_holds(&state.call_stack, address, length)
        || in_fake_stack(address, length)) {
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
    if (error != 0 || destroy_error != 0) {
        return TRANSFER_ERR_STACK;
    }
    if (size == 0 || size > UINTPTR_MAX - address_of(low)) {
        fprintf(stderr, "transfer_bounds: pthread_attr_getstack returned an unusable stack\n");
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
    state.call_stack.start = address_of(platform_call_stack_start);
    state.call_stack.end = address_of(platform_call_stack_end);
    state.image.start = address_of(__executable_start);
    state.image.end = address_of(_end);
    /* game_data, when the link has one (not in the unit tests), lies just
     * before .data; its symbols mean nothing when it is empty. */
    uintptr_t data_start = address_of(__data_start);
    uintptr_t game_data = address_of(game_data_start);
    bool has_game_data = address_of(game_data_end) > game_data;
    state.writable_image.start = has_game_data && game_data < data_start ? game_data : data_start;
    state.writable_image.end = address_of(_end);
    state.ready = true;
    return 0;
}

void transfer_bounds_shutdown(void)
{
    memset(&state, 0, sizeof state);
}
