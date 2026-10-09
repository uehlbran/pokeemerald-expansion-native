/*
 * baseline.c: the reference for probe W1's commit-charge figures. The same CRT and
 * flags as probe.c, no regions: its commit charge is what a process of this build costs
 * before any region exists, so the difference with probe.c's figure is the regions'.
 * The charge is the process's PagefileUsage in bytes (GetProcessMemoryInfo).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <stdint.h>
#include <stdio.h>

#define KIB UINT64_C(1024)

int main(void)
{
    PROCESS_MEMORY_COUNTERS counters;
    counters.cb = sizeof counters;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters)) {
        printf("GetProcessMemoryInfo failed, GetLastError=%lu\n", (unsigned long)GetLastError());
        return 1;
    }
    printf("baseline commit charge: %llu KiB\n", (unsigned long long)((uint64_t)counters.PagefileUsage / KIB));
    return 0;
}
