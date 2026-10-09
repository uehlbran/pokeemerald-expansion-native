/*
 * mapfixed.c: the arm64 half of probe M0 (research/m8-inventory.md 2.5, 5.3). Can this
 * process map a page at the GBA's EWRAM address 0x02000000 with MAP_FIXED, and the
 * I/O address 0x04000000? On macOS arm64 the process has a 4 GiB page zero that
 * -pagezero_size may not be allowed to shrink; this tells which. Exit status 0 if
 * both mappings were made, written and read back.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

/* GBATEK "GBA Memory Map": EWRAM at 02000000, I/O registers at 04000000. */
#define EWRAM_BASE UINT64_C(0x02000000)
#define IO_BASE UINT64_C(0x04000000)
/* 16 KiB, the arm64 page, a whole number of pages on x86_64 too. */
#define MAP_SIZE ((size_t)16384)
#define PATTERN UINT8_C(0xA5)

static bool try_map(const char *name, uint64_t address)
{
    void *got = mmap((void *)(uintptr_t)address, MAP_SIZE, PROT_READ | PROT_WRITE,
                     MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
    if (got == MAP_FAILED) {
        printf("FAIL %s: mmap(0x%llx, MAP_FIXED) failed: errno=%d (%s)\n", name,
               (unsigned long long)address, errno, strerror(errno));
        return false;
    }
    volatile uint8_t *bytes = got;
    bytes[0] = PATTERN;
    bytes[MAP_SIZE - 1] = PATTERN;
    bool ok = got == (void *)(uintptr_t)address && bytes[0] == PATTERN && bytes[MAP_SIZE - 1] == PATTERN;
    printf("%s %s: mmap(0x%llx, MAP_FIXED) -> %p\n", ok ? "PASS" : "FAIL", name,
           (unsigned long long)address, got);
    return ok;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    bool ewram = try_map("EWRAM", EWRAM_BASE);
    bool io = try_map("I/O", IO_BASE);
    printf("SUMMARY ewram=%d io=%d\n", ewram, io);
    return ewram && io ? 0 : 1;
}
