/* window_main.c: the host side of window_test.c (compiled plainly, as the
   platform layer is): the I/O region at WINDOW_BASE + 0x04000000 (a section
   the link places there), the report, and a host heap pointer. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

uint16_t pen_io[0x200] __attribute__((section(".gba_io")));
int run_checks(void);

void host_report(const char *name, int ok)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}
unsigned host_io_read16(unsigned offset)
{
    return pen_io[offset / 2];
}
void *host_alloc(void)
{
    return malloc(16);
}
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("image at %p, EWRAM section, I/O section at %p\n", (void *)main, (void *)pen_io);
    int fails = run_checks();
    printf("SUMMARY fails=%d\n", fails);
    return fails != 0;
}
