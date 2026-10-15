/*
 * window_test.c: expansion's own idioms for turning 32-bit values into
 * pointers and back, compiled with expansion's headers (global.h, io_reg.h,
 * defines.h) and platform/script_tag.h, run with the image and the regions
 * at WINDOW_BASE (2^32) + their Linux addresses. Built twice by run.sh:
 * through rebasefilter.py (expected: every check passes) and plain (the
 * control: the 32-bit values are offsets, so the plain casts miss).
 */
#include "global.h"
#include "platform/script_tag.h"

extern const u8 gTestBlob[];
extern const char gTestString[];
u32 gTestEwramVar __attribute__((section(".gba_ewram")));
static volatile int sNativeCalled;

void TestNative(void);
void TestNative(void)
{
    sNativeCalled = 1;
}

/* Provided by the host side (window_main.c), which the filter does not see. */
void host_report(const char *name, int ok);
unsigned host_io_read16(unsigned offset);
void *host_alloc(void);

int run_checks(void);
int run_checks(void)
{
    int fails = 0;
#define CHECK(name, cond) do { int ok_ = (cond); host_report(name, ok_); fails += !ok_; } while (0)

    /* 1. A data word read as a pointer (global.h T1_READ_PTR). */
    const char *s = (const char *)T1_READ_PTR(gTestBlob + 1);
    CHECK("T1_READ_PTR reaches the string", s == gTestString && s[0] == 'H');

    /* 2. A tagged native pointer from a script word (script_tag.h). */
    u32 w = T1_READ_32(gTestBlob + 5);
    CHECK("the script tag is set on the word", script_tag_is_set(w));
    void (*f)(void) = (void (*)(void))(uintptr_t)w;
    CHECK("the tag is seen on the pointer", script_tag_is_set((uintptr_t)f));
    f = SCRIPT_UNTAG(f);
    CHECK("the untagged pointer is the function", f == TestNative);
    f();
    CHECK("the function ran", sNativeCalled == 1);

    /* 3. An EWRAM pointer from a data word, and expansion's range test. */
    u32 *e = (u32 *)T2_READ_PTR(gTestBlob + 9);
    CHECK("T2_READ_PTR reaches the EWRAM variable", e == &gTestEwramVar);
    CHECK("EWRAM_START <= (uintptr_t)p < EWRAM_END", (uintptr_t)e >= EWRAM_START && (uintptr_t)e < EWRAM_END);

    /* 4. A pointer kept in two s16 task fields (src/task.c:177-192). */
    s16 data[2];
    u32 v = (u32)e;
    data[0] = v;
    data[1] = v >> 16;
    u32 *e2 = (u32 *)((u16)data[0] | (data[1] << 16));
    CHECK("SetWordTaskArg/GetWordTaskArg round trip", e2 == e);
    *e2 = 42;
    CHECK("the write through it lands", gTestEwramVar == 42);

    /* 5. An I/O register through io_reg.h's macro (REG_BASE + 0x10). */
    REG_BG0HOFS = 0x1234;
    CHECK("REG_BG0HOFS reaches the I/O region", host_io_read16(0x10) == 0x1234);

    /* 6. A VRAM address made from defines.h's integer and back. */
    void *vram = (void *)(BG_VRAM + 0x20);
    CHECK("(uintptr_t)(void *)(BG_VRAM + 0x20) == BG_VRAM + 0x20", (uintptr_t)vram == BG_VRAM + 0x20);

    /* 7. Pointers outside the window survive uintptr_t: the stack and the host heap. */
    char buf[32];
    char *q = (char *)(((uintptr_t)buf + 7) & ~(uintptr_t)7);
    CHECK("a stack pointer aligned through uintptr_t", q >= buf && q < buf + 8);
    void *h = host_alloc();
    CHECK("a host heap pointer through uintptr_t", (void *)(uintptr_t)h == h);

    /* 8. A null word stays null. */
    CHECK("a zero word is NULL", T1_READ_PTR(gTestBlob + 13) == NULL);
    return fails;
}
