/*
 * main.c: probe M2's checks (research/probes/README.md, "Probe M2"). Reads the
 * names the macOS link must give the platform (the region bounds host_linked.c
 * reads, the brackets crt0.c and transfer_bounds.c read, the test table, the
 * absolute symbols, the first main callback, the weak references) and checks
 * each against the layout layout.s asks for. Built twice by build.sh: with the
 * plain C names, which -alias link flags bind to ld64's section$start$ and
 * segment$start$ symbols, and with -DPROBE_ASM_LABELS, where the declarations
 * name those symbols themselves. Prints the image's segments and sections, a
 * PASS or FAIL line per check and a SUMMARY line; exits 0 only if none failed.
 */
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#include <mach-o/loader.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "probe_test.h"

#ifdef PROBE_ASM_LABELS
#define NAMED(symbol) __asm(symbol)
#else
#define NAMED(symbol)
#endif

/* The names build.sh binds (with -alias) or the declarations name. */
extern unsigned char gba_linked_ewram_start[] NAMED("segment$start$__GBA_EWRAM");
extern unsigned char gba_linked_ewram_end[] NAMED("section$start$__GBA_EWRAM$__ewram_end");
extern unsigned char gba_linked_ewram_init_start[] NAMED("section$start$__GBA_EWRAM$__ewram_init");
extern unsigned char gba_linked_ewram_init_end[] NAMED("section$end$__GBA_EWRAM$__ewram_init");
extern unsigned char gba_ewram_data_start[] NAMED("section$start$__GBA_EWRAM$__ewram_data");
extern unsigned char gba_ewram_data_end[] NAMED("section$end$__GBA_EWRAM$__ewram_data");
extern unsigned char gba_linked_iwram_start[] NAMED("segment$start$__GBA_IWRAM");
extern unsigned char gba_linked_iwram_end[] NAMED("segment$end$__GBA_IWRAM");
extern unsigned char gba_persistent_start[] NAMED("section$start$__GBA_IWRAM$__persistent");
extern unsigned char gba_linked_io_start[] NAMED("section$start$__GBA_IO$__io");
extern unsigned char gba_linked_io_end[] NAMED("section$end$__GBA_IO$__io");
extern unsigned char gba_linked_sram_start[] NAMED("section$start$__GBA_SRAM$__sram");
extern unsigned char gba_linked_sram_end[] NAMED("section$end$__GBA_SRAM$__sram");
extern unsigned char platform_call_stack_start[] NAMED("section$start$__GBA_CALLSTK$__stack");
extern unsigned char platform_call_stack_end[] NAMED("section$end$__GBA_CALLSTK$__stack");
extern unsigned char game_data_start[] NAMED("section$start$__DATA$__game_data");
extern unsigned char game_data_end[] NAMED("section$end$__DATA$__game_data");
extern unsigned char game_bss_start[] NAMED("section$start$__DATA$__game_bss");
extern unsigned char game_bss_objects_end[] NAMED("section$end$__DATA$__game_bss");
extern unsigned char game_bss_end[] NAMED("section$end$__DATA$__game_common");
extern const struct ProbeTest __start_tests[] NAMED("section$start$__DATA$__tests");
extern const struct ProbeTest __stop_tests[] NAMED("section$end$__DATA$__tests");
extern unsigned char probe_empty_start[] NAMED("section$start$__DATA$__empty_probe");
extern unsigned char probe_empty_end[] NAMED("section$end$__DATA$__empty_probe");

/* Absolute symbols and aliases that are not section bounds: always -alias or layout.s. */
extern char gNumMusicPlayers[];
extern const unsigned char gba_game_objects_linked[];
extern const char PlatformAllocationSiteBase[];
extern void gInitialMainCB2(void);
extern const uint32_t probe_words[];

/* Weak references nothing defines: -U in build.sh. */
extern const size_t gPlatformTestEntrySize __attribute__((weak));
extern int PlatformTestTableBadEntry(void) __attribute__((weak));

/* game.c */
void CB2_Probe(void);
uint32_t *ProbeGameStaticBss(void);
extern uint8_t gEwramInit;
extern uint8_t gEwramData[];
extern uint8_t gPersistent[];
extern uint32_t gCommon[];
extern uint32_t gGameBss[];
extern uint32_t gGameData;

static unsigned g_pass;
static unsigned g_fail;

static unsigned long long ull(uintptr_t value)
{
    return (unsigned long long)value;
}

static uintptr_t at(const void *pointer)
{
    uintptr_t address;
    memcpy(&address, &pointer, sizeof address);
    return address;
}

static uintptr_t function_at(void (*function)(void))
{
    uintptr_t address;
    memcpy(&address, &function, sizeof address);
    return address;
}

static void check(bool ok, const char *what, uintptr_t got, uintptr_t expected)
{
    if (ok)
        g_pass++;
    else
        g_fail++;
    printf("%s %s: got 0x%llx, expected 0x%llx\n", ok ? "PASS" : "FAIL", what, ull(got), ull(expected));
}

static void check_equal(const char *what, uintptr_t got, uintptr_t expected)
{
    check(got == expected, what, got, expected);
}

static void print_layout(const struct mach_header_64 *header)
{
    const uint8_t *cursor = (const uint8_t *)(header + 1);
    for (uint32_t i = 0; i < header->ncmds; i++) {
        struct load_command command;
        memcpy(&command, cursor, sizeof command);
        if (command.cmd == LC_SEGMENT_64) {
            struct segment_command_64 segment;
            memcpy(&segment, cursor, sizeof segment);
            printf("segment %-16.16s 0x%09llx-0x%09llx file 0x%llx+0x%llx prot %d/%d flags 0x%x\n", segment.segname,
                   ull((uintptr_t)segment.vmaddr), ull((uintptr_t)(segment.vmaddr + segment.vmsize)),
                   ull((uintptr_t)segment.fileoff), ull((uintptr_t)segment.filesize), segment.initprot,
                   segment.maxprot, segment.flags);
            for (uint32_t s = 0; s < segment.nsects; s++) {
                struct section_64 section;
                memcpy(&section, cursor + sizeof segment + s * sizeof section, sizeof section);
                printf("    section %-16.16s 0x%09llx-0x%09llx align 2^%u flags 0x%08x\n", section.sectname,
                       ull((uintptr_t)section.addr), ull((uintptr_t)(section.addr + section.size)), section.align,
                       section.flags);
            }
        } else {
            printf("load command 0x%x\n", command.cmd);
        }
        cursor += command.cmdsize;
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const struct mach_header_64 *header = &_mh_execute_header;
    printf("header at 0x%llx, slide %lld, flags 0x%x\n", ull(at(header)), (long long)_dyld_get_image_vmaddr_slide(0),
           header->flags);
    print_layout(header);

    check_equal("EWRAM starts at 0x02000000", at(gba_linked_ewram_start), 0x02000000u);
    check_equal("EWRAM_INIT starts EWRAM", at(gba_linked_ewram_init_start), 0x02000000u);
    check_equal("EWRAM_INIT's one byte is gEwramInit", at(&gEwramInit), at(gba_linked_ewram_init_start));
    check_equal("EWRAM_INIT holds its initial value", gEwramInit, 0x5Au);
    check(at(gba_ewram_data_start) >= at(gba_linked_ewram_init_end), "EWRAM_DATA follows EWRAM_INIT",
          at(gba_ewram_data_start), at(gba_linked_ewram_init_end));
    check(at(gEwramData) >= at(gba_ewram_data_start) && at(gEwramData) < at(gba_ewram_data_end),
          "gEwramData is in EWRAM_DATA", at(gEwramData), at(gba_ewram_data_start));
    check_equal("the marker after EWRAM_DATA is at EWRAM's end", at(gba_linked_ewram_end), 0x02040000u);
    check_equal("IWRAM starts at 0x03000000", at(gba_linked_iwram_start), 0x03000000u);
    check_equal("IWRAM's segment ends at 0x03008000", at(gba_linked_iwram_end), 0x03008000u);
    check_equal("the persistent section is at 0x03007F00", at(gba_persistent_start), 0x03007F00u);
    check_equal("gPersistent is at 0x03007F00", at(gPersistent), 0x03007F00u);
    check_equal("I/O starts at 0x04000000", at(gba_linked_io_start), 0x04000000u);
    check_equal("I/O is one page", at(gba_linked_io_end) - at(gba_linked_io_start), 0x1000u);
    check_equal("SRAM starts at 0x0E000000", at(gba_linked_sram_start), 0x0E000000u);
    check_equal("SRAM is 128 KiB", at(gba_linked_sram_end) - at(gba_linked_sram_start), 0x20000u);
    check_equal("the call stack starts at 0x01800000", at(platform_call_stack_start), 0x01800000u);
    check_equal("the call stack is 1 MiB", at(platform_call_stack_end) - at(platform_call_stack_start), 0x100000u);
    check(at(&gGameData) >= at(game_data_start) && at(&gGameData) < at(game_data_end), "gGameData is in game_data",
          at(&gGameData), at(game_data_start));
    check(at(gGameBss) >= at(game_bss_start) && at(gGameBss) < at(game_bss_objects_end), "gGameBss is in game_bss",
          at(gGameBss), at(game_bss_start));
    const uintptr_t static_bss = at(ProbeGameStaticBss());
    check(static_bss >= at(game_bss_start) && static_bss < at(game_bss_objects_end), "a static is in game_bss",
          static_bss, at(game_bss_start));
    check(at(gCommon) >= at(game_bss_objects_end) && at(gCommon) < at(game_bss_end),
          "COMMON_DATA follows the game's bss", at(gCommon), at(game_bss_objects_end));
    check(at(gCommon) - at(game_bss_objects_end) < 16u, "nothing but alignment between bss and COMMON_DATA",
          at(gCommon), at(game_bss_objects_end));
    check_equal("three tests in the table", (uintptr_t)(__stop_tests - __start_tests), 3u);
    check_equal("the table is three entries long", at(__stop_tests) - at(__start_tests),
                3u * sizeof(struct ProbeTest));
    for (const struct ProbeTest *test = __start_tests; test < __stop_tests; test++)
        printf("test %llu: %s\n", (unsigned long long)test->number, test->name);
    check_equal("an empty section starts where it ends", at(probe_empty_end), at(probe_empty_start));
    check_equal("gNumMusicPlayers is the address 4", at(gNumMusicPlayers), 4u);
    check_equal("gba_game_objects_linked is the address 1", at(gba_game_objects_linked), 1u);
    check_equal("PlatformAllocationSiteBase is the header", at(PlatformAllocationSiteBase), at(header));
    check_equal("gInitialMainCB2 is CB2_Probe", function_at(gInitialMainCB2), function_at(CB2_Probe));
    check_equal("an undefined weak object is null", at(&gPlatformTestEntrySize), 0u);
    check_equal("an undefined weak function is null", function_at((void (*)(void))PlatformTestTableBadEntry), 0u);
    check_equal("an 8-byte word holds CB2_Probe", (uintptr_t)probe_words[0] | (uintptr_t)probe_words[1] << 32,
                function_at(CB2_Probe));
    check_equal("a 4-byte word holds CB2_Probe", probe_words[2], function_at(CB2_Probe));
    check_equal("a 4-byte word holds the I/O page", probe_words[3], 0x04000000u);
    check_equal("a 4-byte word holds an absolute symbol", probe_words[4], 4u);
    check(at(&main) >= 0x40000000u && at(&main) < 0x42000000u, "the code is below 0x42000000", at(&main), 0x40000000u);
    unsigned long size = 0;
    const uint8_t *data = getsectiondata(header, "__DATA", "__game_data", &size);
    check_equal("getsectiondata finds game_data", at(data), at(game_data_start));
    printf("SUMMARY pass=%u fail=%u\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
