/*
 * Tests of gba_memmap's link-layout check, the only way gba_memmap_init()
 * fails: a section that is shifted, short or oversized is rejected with
 * GBA_MEMMAP_ERR_LAYOUT and a message naming the region; an exact one passes
 * silently. A correctly linked binary cannot produce a wrong layout, so the
 * check is called directly with made-up addresses.
 *
 * The check is static, the module's own business, so this test includes the
 * module's source rather than widening its public header; it is therefore
 * not linked with gba_memmap.o (no test is named after gba_memmap_layout.c).
 */
#include "gba_memmap.c"

#include "ptest.h"

#define PAGE 0x1000u
#define KIB 1024u
#define LOG_SIZE 512 /* room for one layout message */

/* Where gba_regions.ld must put each section, written out independently:
 * base, and the GBATEK size rounded up to whole 4 KiB pages. */
struct expected_section {
    enum gba_region id;
    uintptr_t base;
    size_t linked_size;
    const char *name;
};

static const struct expected_section expected_sections[] = {
    {GBA_REGION_EWRAM, 0x02000000u, 256 * KIB, "EWRAM"},
    {GBA_REGION_IWRAM, 0x03000000u, 32 * KIB, "IWRAM"},
    {GBA_REGION_IO, 0x04000000u, PAGE, "I/O"},
    {GBA_REGION_MGBA_DEBUG, 0x04FFF000u, PAGE, "mGBA debug"},
    {GBA_REGION_PALETTE_RAM, 0x05000000u, PAGE, "palette RAM"},
    {GBA_REGION_VRAM, 0x06000000u, 96 * KIB, "VRAM"},
    {GBA_REGION_OAM, 0x07000000u, PAGE, "OAM"},
    {GBA_REGION_SRAM, 0x0E000000u, 128 * KIB, "SRAM"},
};
#define EXPECTED_SECTION_COUNT (sizeof expected_sections / sizeof expected_sections[0])
_Static_assert(EXPECTED_SECTION_COUNT == GBA_REGION_COUNT - 1, "a region has no expected section");

/* One call of the check: its result and what it wrote to its log. */
struct layout_check {
    int status;
    bool log_complete; /* the log was read back whole */
    char log[LOG_SIZE];
};

static struct layout_check check_layout(enum gba_region id, uintptr_t start, uintptr_t end)
{
    struct layout_check check = {.status = GBA_MEMMAP_OK, .log_complete = false, .log = ""};
    FILE *const log = tmpfile();
    if (log == NULL)
        return check;
    check.status = check_linked_section(id, start, end, log);
    if (fseek(log, 0, SEEK_SET) == 0) {
        const size_t length = fread(check.log, 1, sizeof check.log - 1, log);
        check.log[length] = '\0';
        check.log_complete = !ferror(log) && length < sizeof check.log - 1;
    }
    if (fclose(log) != 0)
        check.log_complete = false;
    return check;
}

/* Fails the test, naming the region, unless the check rejected [start, end)
 * and said why. Later checks in the same test do not replace the first failure. */
static void expect_rejected(const struct expected_section *section, uintptr_t start, uintptr_t end)
{
    const struct layout_check check = check_layout(section->id, start, end);
    char expected_name[64];
    const int length = snprintf(expected_name, sizeof expected_name, "gba_memmap: %s is linked at", section->name);
    PT_ASSERT(length > 0 && (size_t)length < sizeof expected_name);
    PT_ASSERT(check.log_complete);
    if (check.status != GBA_MEMMAP_ERR_LAYOUT)
        PT_FAIL(section->name);
    PT_ASSERT(strncmp(check.log, expected_name, (size_t)length) == 0);
    PT_ASSERT(strstr(check.log, "platform/gba_regions.ld") != NULL);
}

PTEST(exact_sections_pass_silently)
{
    for (size_t i = 0; i < EXPECTED_SECTION_COUNT; i++) {
        const struct expected_section *const section = &expected_sections[i];
        const struct layout_check check = check_layout(section->id, section->base,
                                                       section->base + section->linked_size);
        PT_ASSERT(check.log_complete);
        if (check.status != GBA_MEMMAP_OK)
            PT_FAIL(section->name);
        PT_ASSERT_EQ_STR(check.log, "");
    }
}

PTEST(shifted_sections_are_rejected)
{
    for (size_t i = 0; i < EXPECTED_SECTION_COUNT; i++) {
        const struct expected_section *const s = &expected_sections[i];
        expect_rejected(s, s->base + PAGE, s->base + PAGE + s->linked_size);
        expect_rejected(s, s->base - PAGE, s->base - PAGE + s->linked_size);
    }
}

PTEST(short_sections_are_rejected)
{
    for (size_t i = 0; i < EXPECTED_SECTION_COUNT; i++) {
        const struct expected_section *const s = &expected_sections[i];
        expect_rejected(s, s->base, s->base + s->linked_size - 1);
        expect_rejected(s, s->base, s->base);
    }
}

PTEST(oversized_sections_are_rejected)
{
    for (size_t i = 0; i < EXPECTED_SECTION_COUNT; i++) {
        const struct expected_section *const s = &expected_sections[i];
        expect_rejected(s, s->base, s->base + s->linked_size + 1);
        expect_rejected(s, s->base, s->base + s->linked_size + PAGE);
    }
}

PTEST(inverted_section_is_rejected)
{
    const struct expected_section *const s = &expected_sections[0];
    expect_rejected(s, s->base, s->base - 1);
}

PTEST(message_names_both_ranges)
{
    /* OAM moved up one page: the message gives where it is and where it must be. */
    const struct layout_check check = check_layout(GBA_REGION_OAM, 0x07001000u, 0x07002000u);
    PT_ASSERT(check.log_complete);
    PT_ASSERT_EQ_INT(check.status, GBA_MEMMAP_ERR_LAYOUT);
    PT_ASSERT_EQ_STR(check.log, "gba_memmap: OAM is linked at 0x07001000-0x07002000 but must be "
                                "at 0x07000000-0x07001000; link with platform/gba_regions.ld "
                                "(see platform/Makefile)\n");
}

PTEST(this_binary_is_linked_correctly)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
}

PTEST_MAIN()
