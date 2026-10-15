/*
 * divprobe.c: what AArch64's SDIV and UDIV (and the MSUB a compiler uses for
 * %) return for n / 0 and INT_MIN / -1, at 32 and 64 bits. Freestanding, raw
 * Linux syscalls, no relocations: build with build_div.sh and run under
 * qemu-aarch64. One line per case: width, kind, n, d, quotient, remainder.
 */
typedef unsigned long u64;
typedef long s64;
typedef unsigned int u32;
typedef int s32;

#define ALWAYS static inline __attribute__((always_inline))

ALWAYS void sys_write(const char *buf, u64 len)
{
    register u64 x0 __asm__("x0") = 1;
    register const char *x1 __asm__("x1") = buf;
    register u64 x2 __asm__("x2") = len;
    register u64 x8 __asm__("x8") = 64; /* write */
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
}

ALWAYS __attribute__((noreturn)) void sys_exit(u64 code)
{
    register u64 x0 __asm__("x0") = code;
    register u64 x8 __asm__("x8") = 93; /* exit */
    __asm__ volatile("svc #0" : : "r"(x0), "r"(x8) : "memory");
    __builtin_unreachable();
}

ALWAYS u64 put_hex(char *p, u64 v, int digits)
{
    p[0] = '0';
    p[1] = 'x';
    for (int i = 0; i < digits; i++) {
        u32 nib = (u32)(v >> (4 * (digits - 1 - i))) & 15u;
        p[2 + i] = (char)(nib < 10 ? '0' + nib : 'a' + nib - 10);
    }
    p[2 + digits] = ' ';
    return (u64)digits + 3;
}

/* kind: 's' or 'u'; one line: "<w><kind> n d q r\n" */
ALWAYS void line(char width, char kind, u64 n, u64 d, u64 q, u64 r, int digits)
{
    char buf[128];
    u64 at = 0;
    buf[at++] = width;
    buf[at++] = kind;
    buf[at++] = ' ';
    at += put_hex(buf + at, n, digits);
    at += put_hex(buf + at, d, digits);
    at += put_hex(buf + at, q, digits);
    at += put_hex(buf + at, r, digits);
    buf[at - 1] = '\n';
    sys_write(buf, at);
}

/* The instructions themselves, as a compiler emits them for / and %. */
ALWAYS void s32case(u32 n, u32 d)
{
    u32 q, r;
    __asm__ volatile("sdiv %w0, %w2, %w3\n\tmsub %w1, %w0, %w3, %w2" : "=&r"(q), "=&r"(r) : "r"(n), "r"(d));
    line('3', 's', n, d, q, r, 8);
}
ALWAYS void u32case(u32 n, u32 d)
{
    u32 q, r;
    __asm__ volatile("udiv %w0, %w2, %w3\n\tmsub %w1, %w0, %w3, %w2" : "=&r"(q), "=&r"(r) : "r"(n), "r"(d));
    line('3', 'u', n, d, q, r, 8);
}
ALWAYS void s64case(u64 n, u64 d)
{
    u64 q, r;
    __asm__ volatile("sdiv %0, %2, %3\n\tmsub %1, %0, %3, %2" : "=&r"(q), "=&r"(r) : "r"(n), "r"(d));
    line('6', 's', n, d, q, r, 16);
}
ALWAYS void u64case(u64 n, u64 d)
{
    u64 q, r;
    __asm__ volatile("udiv %0, %2, %3\n\tmsub %1, %0, %3, %2" : "=&r"(q), "=&r"(r) : "r"(n), "r"(d));
    line('6', 'u', n, d, q, r, 16);
}

__attribute__((noreturn, section(".text.start"))) void _start(void)
{
    /* The inputs of platform/arm_division_libgcc/libgcc_division.txt, each
       call written out so that the object has no .rodata to relocate. */
    u32case(0, 0); u32case(1, 0); u32case(2, 0); u32case(5, 0);
    u32case(0x7fffffffu, 0); u32case(0x80000000u, 0); u32case(0xffffffffu, 0);
    u32case(7, 2);
    s32case(0, 0); s32case(1, 0); s32case(5, 0); s32case(0x7fffffffu, 0);
    s32case(0xffffffffu, 0); s32case(0xfffffffbu, 0); s32case(0x80000000u, 0);
    s32case(0x80000000u, 0xffffffffu); /* INT32_MIN / -1 */
    s32case(0xfffffff9u, 2);
    u64case(0, 0); u64case(1, 0); u64case(2, 0);
    u64case(0x8000000000000000ul, 0); u64case(0xfffffffffffffffful, 0);
    s64case(0, 0); s64case(1, 0); s64case(0x7ffffffffffffffful, 0);
    s64case(0xfffffffffffffffful, 0); s64case(0x8000000000000000ul, 0);
    s64case(0x8000000000000000ul, 0xfffffffffffffffful); /* INT64_MIN / -1 */
    sys_exit(0);
}
