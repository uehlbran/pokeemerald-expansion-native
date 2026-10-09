/*
 * probe.c: probe W1 of research/m8-inventory.md (sections 2.1, 2.2, 5.2). Does the NT
 * loader map a PE image whose sections are the GBA memory regions at their GBA
 * addresses ("layout B"), and are they writable and committed? Plain C11 and the
 * Win32 API, no SDL, no platform/ code.
 *
 * Compiled twice:
 *   -DLAYOUT_IMAGE    the regions are PE sections placed by the linker script
 *                     (variants "as_linked" and "filled_gaps");
 *   -DLAYOUT_RUNTIME  there are no such sections; main reserves and commits the
 *                     regions with VirtualAlloc at the same addresses (variant
 *                     "virtualalloc", the fallback of section 2.2).
 *
 * It prints one line per fact and PASS or FAIL per check, and ends with one
 * "SUMMARY" line. The exit status is 0 only if no check failed.
 * Region addresses and sizes: GBATEK "GBA Memory Map"; platform/gba_memmap.h and
 * platform/gba_regions.ld have the same numbers (the I/O, palette, OAM and debug
 * regions are the 4 KiB page that holds them).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#if defined(LAYOUT_IMAGE) == defined(LAYOUT_RUNTIME)
#error "define exactly one of LAYOUT_IMAGE and LAYOUT_RUNTIME"
#endif

/* Layout B, research/m8-inventory.md 2.2 probe B. */
#define EXPECTED_IMAGE_BASE UINT64_C(0x01800000)
/* The code starts here (--section-start=.text=...), and must end below
 * 0x42000000 for platform/script_tag.h (inventory 2.1 item 3). */
#define CODE_LOW UINT64_C(0x40001000)
#define CODE_LIMIT UINT64_C(0x42000000)

#define KIB UINT64_C(1024)
/* Windows reserves at this granularity and rounds a reservation's base down to it
 * (inventory 2.1 table; VirtualAlloc documentation, "dwSize"/"lpAddress"). */
#define ALLOCATION_GRANULARITY UINT64_C(0x10000)
#define MIB (KIB * KIB)

struct Region {
    const char *name;
    uint64_t base;
    uint64_t size;
    bool writable; /* the ROM is read-only; every other region is read-write */
};

/* GBATEK "GBA Memory Map"; gba_regions.ld; regions.s has the same sizes. */
static const struct Region REGIONS[] = {
    {"EWRAM", UINT64_C(0x02000000), 256 * KIB, true},
    {"IWRAM", UINT64_C(0x03000000), 32 * KIB, true},
    {"I/O", UINT64_C(0x04000000), 4 * KIB, true},
    {"mGBA debug", UINT64_C(0x04FFF000), 4 * KIB, true},
    {"palette", UINT64_C(0x05000000), 4 * KIB, true},
    {"VRAM", UINT64_C(0x06000000), 96 * KIB, true},
    {"OAM", UINT64_C(0x07000000), 4 * KIB, true},
    {"ROM", UINT64_C(0x08000000), 32 * MIB, false},
    {"SRAM", UINT64_C(0x0E000000), 128 * KIB, true},
};
#define REGION_COUNT (sizeof REGIONS / sizeof REGIONS[0])

/* Two distinct patterns, so a read-back of stale zero is a failure. */
#define PATTERN_FIRST UINT8_C(0xA5)
#define PATTERN_LAST UINT8_C(0x5A)

static unsigned g_pass;
static unsigned g_fail;

static void check(bool ok, const char *what, const char *region)
{
    if (ok) {
        g_pass++;
    } else {
        g_fail++;
    }
    printf("%s %s: %s\n", ok ? "PASS" : "FAIL", region, what);
}

static unsigned long long ull(uint64_t value) { return (unsigned long long)value; }

/* Writable image sections are mapped copy-on-write, so a queried page of the exe's own
 * writable section reports PAGE_WRITECOPY (run 37922990222), VirtualAlloc'ed ones
 * PAGE_READWRITE. */
static bool is_writable_protect(DWORD protect)
{
    return protect == PAGE_READWRITE || protect == PAGE_EXECUTE_READWRITE ||
           protect == PAGE_WRITECOPY || protect == PAGE_EXECUTE_WRITECOPY;
}

static bool is_readable_protect(DWORD protect)
{
    return protect == PAGE_READONLY || is_writable_protect(protect);
}

/* VirtualQuery at the region's first byte and the page of its last byte. */
static bool query_region(const struct Region *r, uint64_t address, const char *label)
{
    MEMORY_BASIC_INFORMATION mbi;
    SIZE_T got = VirtualQuery((LPCVOID)(uintptr_t)address, &mbi, sizeof mbi);
    if (got == 0) {
        printf("  %s: VirtualQuery(0x%llx) failed, GetLastError=%lu\n", label, ull(address),
               (unsigned long)GetLastError());
        return false;
    }
    printf("  %s %s: query 0x%llx -> base=0x%llx allocbase=0x%llx size=0x%llx state=0x%lx "
           "protect=0x%lx type=0x%lx\n",
           r->name, label, ull(address), ull((uintptr_t)mbi.BaseAddress),
           ull((uintptr_t)mbi.AllocationBase), ull(mbi.RegionSize), (unsigned long)mbi.State,
           (unsigned long)mbi.Protect, (unsigned long)mbi.Type);
    return mbi.State == MEM_COMMIT &&
           (r->writable ? is_writable_protect(mbi.Protect) : is_readable_protect(mbi.Protect));
}

static void probe_region(const struct Region *r)
{
    uint64_t last = r->base + r->size - 1;
    printf("region %s: 0x%llx size 0x%llx\n", r->name, ull(r->base), ull(r->size));
    check(query_region(r, r->base, "start"), "start page is committed with the right protection",
          r->name);
    check(query_region(r, last, "end"), "end page is committed with the right protection", r->name);

    volatile uint8_t *first = (volatile uint8_t *)(uintptr_t)r->base;
    volatile uint8_t *end = (volatile uint8_t *)(uintptr_t)last;
    if (r->writable) {
        *first = PATTERN_FIRST;
        *end = PATTERN_LAST;
        check(*first == PATTERN_FIRST, "first byte reads back what was written", r->name);
        check(*end == PATTERN_LAST, "last byte reads back what was written", r->name);
    } else {
        check(*first == 0, "first byte reads as zero", r->name);
        check(*end == 0, "last byte reads as zero", r->name);
    }
}

static bool commit_charge(uint64_t *out)
{
    PROCESS_MEMORY_COUNTERS counters;
    counters.cb = sizeof counters;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters)) {
        printf("GetProcessMemoryInfo failed, GetLastError=%lu\n", (unsigned long)GetLastError());
        return false;
    }
    *out = counters.PagefileUsage; /* the process's commit charge, in bytes */
    return true;
}

static uint64_t writable_region_bytes(void)
{
    uint64_t total = 0;
    for (size_t i = 0; i < REGION_COUNT; i++) {
        if (REGIONS[i].writable) {
            total += REGIONS[i].size;
        }
    }
    return total;
}

#ifdef LAYOUT_RUNTIME
/* The fallback: reserve and commit each region at its GBA address. Windows rounds a
 * reservation's base down to its 64 KiB granularity (inventory 2.1 table), so the
 * mGBA debug page asks for 0x04FFF000 and may be given 0x04FF0000. */
static void allocate_regions(void)
{
    for (size_t i = 0; i < REGION_COUNT; i++) {
        const struct Region *r = &REGIONS[i];
        void *got = VirtualAlloc((LPVOID)(uintptr_t)r->base, (SIZE_T)r->size,
                                 MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        printf("VirtualAlloc %s at 0x%llx size 0x%llx -> 0x%llx (GetLastError=%lu)\n", r->name,
               ull(r->base), ull(r->size), ull((uintptr_t)got),
               (unsigned long)(got ? 0 : GetLastError()));
        /* The base may come back rounded down to the granule; the page at the requested
         * address is checked by probe_region with VirtualQuery. */
        check(got != NULL && (uintptr_t)got == (r->base & ~(ALLOCATION_GRANULARITY - 1)),
              "VirtualAlloc returned the requested address (rounded to the granule)", r->name);
        if (got != NULL && !r->writable) {
            DWORD old;
            check(VirtualProtect(got, (SIZE_T)r->size, PAGE_READONLY, &old) != 0,
                  "VirtualProtect made the region read-only", r->name);
        }
    }
}
#endif

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    uint64_t charge_before = 0;
    bool have_before = commit_charge(&charge_before);
    check(have_before, "commit charge readable at start", "process");

#ifdef LAYOUT_RUNTIME
    printf("variant: regions reserved with VirtualAlloc in main\n");
    allocate_regions();
#else
    printf("variant: regions are PE sections\n");
#endif
    uint64_t image_base = (uintptr_t)GetModuleHandle(NULL);
    printf("image base: 0x%llx (expected 0x%llx)\n", ull(image_base), ull(EXPECTED_IMAGE_BASE));
#ifdef LAYOUT_IMAGE
    check(image_base == EXPECTED_IMAGE_BASE, "the exe is at its linked image base", "process");
    uint64_t code = (uintptr_t)&main;
    printf("main at 0x%llx\n", ull(code));
    check(code >= CODE_LOW && code < CODE_LIMIT, "code is in [0x40001000, 0x42000000)", "process");
#endif

    for (size_t i = 0; i < REGION_COUNT; i++) {
        probe_region(&REGIONS[i]);
    }

    uint64_t charge_after = 0;
    bool have_after = commit_charge(&charge_after);
    check(have_after, "commit charge readable at end", "process");
    if (have_before && have_after) {
        printf("commit charge: start %llu KiB, end %llu KiB; the writable regions are %llu KiB\n",
               ull(charge_before / KIB), ull(charge_after / KIB), ull(writable_region_bytes() / KIB));
#ifdef LAYOUT_RUNTIME
        check(charge_after >= charge_before + writable_region_bytes(),
              "commit charge grew by at least the writable regions", "process");
#endif
    }

    printf("SUMMARY pass=%u fail=%u\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
