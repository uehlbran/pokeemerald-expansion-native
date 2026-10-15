/* lsan.c: probe M2: a leak, run under Apple clang's AddressSanitizer with
 * detect_leaks=1, as platform/Makefile runs every unit test on Linux. */
#include <stdio.h>
#include <stdlib.h>

void *volatile g_kept;

int main(void)
{
    g_kept = malloc(64);
    g_kept = NULL;
    puts("leaked 64 bytes");
    return 0;
}
