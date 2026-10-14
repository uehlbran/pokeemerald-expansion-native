/* DIAGNOSTIC, temporary (task W8/W9): what a Windows test process holds below
 * 0x02000000, to choose the m4a tests' free page. Removed after the run. */
#include <inttypes.h>
#include <stdio.h>

#include <windows.h>

#include "ptest.h"

static void print_map(const char *when)
{
    printf("-- map below 0x02000000, %s\n", when);
    uintptr_t address = 0x10000u;
    while (address < 0x02000000u) {
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery((const void *)address, &info, sizeof info) != sizeof info)
            break;
        printf("%08" PRIxPTR "-%08" PRIxPTR " state %05lx type %07lx protect %03lx base %p\n", address,
               (uintptr_t)info.BaseAddress + info.RegionSize, (unsigned long)info.State, (unsigned long)info.Type,
               (unsigned long)info.Protect, info.AllocationBase);
        address = (uintptr_t)info.BaseAddress + info.RegionSize;
    }
    static const uintptr_t candidates[] = {0x00F00000u, 0x01000000u, 0x01700000u, 0x017E0000u, 0x017F0000u};
    for (size_t i = 0; i < sizeof candidates / sizeof candidates[0]; i++) {
        void *got = VirtualAlloc((void *)candidates[i], 0x1000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        printf("VirtualAlloc %08" PRIxPTR ": %p (error %lu)\n", candidates[i], got, got ? 0ul : GetLastError());
        if (got != NULL)
            (void)VirtualFree(got, 0, MEM_RELEASE);
    }
    fflush(stdout);
}

PTEST(print_the_low_map)
{
    print_map("in a test, the watchdog running");
    void *block = malloc(8u << 20);
    print_map("after an 8 MiB malloc");
    free(block);
    PT_ASSERT(1);
}

PTEST_MAIN()
