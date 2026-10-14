/*
 * Tests of the native flash chip, flash.c. The expected sizes, the erased
 * value and the bit rule are written out here from GBATEK "GBA Cart Backup
 * Flash ROM"; the status codes and the entry's shape from expansion's driver
 * (include/gba/flash_internal.h, src/agb_flash_mx.c, src/agb_flash_1m.c). The
 * driver functions are called through NativeFlash's pointers, which is how
 * IdentifyFlash installs them for the game.
 *
 * Every test that looks at the chip starts with flash_init(), which erases it
 * and clears the dirty flags, so no test depends on what another left.
 */
// deps: gba_memmap.c
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <gba/types.h>
#include <gba/flash_internal.h>

#include "flash.h"
#include "gba_memmap.h"
#include "ptest.h"
#include "test_support.h"

#define KIB 1024u
#define SECTOR_BYTES (4 * KIB) /* GBATEK: "Erase 4Kbyte Sector" */
#define SECTORS 32u            /* 128 KiB in 4 KiB sectors */
#define CHIP_BYTES (128 * KIB) /* GBATEK: "128 KBytes - 1Mbit Flash ROM" */
#define CHIP_ADDRESS 0x0E000000u
#define ERASED 0xFFu /* GBATEK: "erased memory is FFh-filled" */
#define LAST_SECTOR (SECTORS - 1)

/* The driver's codes (src/agb_flash_mx.c, src/agb_flash_1m.c). */
#define BAD_SECTOR 0x80FFu
#define BAD_OFFSET 0x8000u
#define TIME_LIMIT_PROGRAM_BYTE 0xA001u /* 0xA000 | phase 1 */
#define TIMEOUT_PROGRAM_BYTE 0xC001u    /* 0xC000 | phase 1 */

_Static_assert(FLASH_SECTOR_SIZE == SECTOR_BYTES, "sector size");
_Static_assert(FLASH_SECTOR_COUNT == SECTORS, "sector count");
_Static_assert(FLASH_CHIP_SIZE == CHIP_BYTES, "chip size");
_Static_assert(FLASH_ERASED_BYTE == ERASED, "erased value");
_Static_assert(FLASH_STATUS_BAD_SECTOR == BAD_SECTOR, "bad sector code");
_Static_assert(FLASH_STATUS_BAD_ARGUMENT == BAD_OFFSET, "bad offset code");
_Static_assert((FLASH_STATUS_TIME_LIMIT | FLASH_PHASE_PROGRAM_BYTE) == TIME_LIMIT_PROGRAM_BYTE, "time limit code");
/* Agreement with expansion and with the memory map. */
_Static_assert(FLASH_CHIP_SIZE == FLASH_ROM_SIZE_1M, "not expansion's 1 Mbit size");
_Static_assert(FLASH_SECTOR_COUNT == 2 * SECTORS_PER_BANK, "not two banks of expansion's size");
_Static_assert(FLASH_CHIP_SIZE == GBA_SRAM_SIZE, "the mapped region is not the whole chip");
_Static_assert(GBA_SRAM_BASE == CHIP_ADDRESS, "the chip is not at 0x0E000000");

/* The chip as mapped: the region is at its GBA address, so the
 * integer-to-pointer conversion is the identity. */
static uint8_t *chip(void)
{
    return (uint8_t *)(uintptr_t)CHIP_ADDRESS;
}

static uint8_t *sector_at(uint32_t sector)
{
    return chip() + sector * SECTOR_BYTES;
}

/* Offset of the first of count bytes that is not value; count if none. */
static size_t first_byte_not(const uint8_t *bytes, size_t count, uint8_t value)
{
    size_t at = 0;
    while (at < count && bytes[at] == value)
        at++;
    return at;
}

static size_t count_bytes_not(const uint8_t *bytes, size_t count, uint8_t value)
{
    size_t differing = 0;
    for (size_t at = 0; at < count; at++)
        if (bytes[at] != value)
            differing++;
    return differing;
}

/* Every byte value, 0x00 and 0xFF included, and different per sector. */
static void fill_pattern(uint8_t *bytes, size_t count, uint8_t seed)
{
    for (size_t at = 0; at < count; at++)
        bytes[at] = (uint8_t)(at * 7u + seed);
}

/* The bytes outside [start, start + length) are all value. */
static bool outside_is(size_t start, size_t length, uint8_t value)
{
    const size_t end = start + length;
    return first_byte_not(chip(), start, value) == start
           && first_byte_not(chip() + end, CHIP_BYTES - end, value) == CHIP_BYTES - end;
}

/* ---- shutdown first, so it runs before any init in this binary ---- */

PTEST(shutdown_is_safe_without_init_and_changes_nothing)
{
    flash_shutdown();
    flash_shutdown();
    flash_init();
    const uint16_t status = NativeFlash.programFlashByte(0, 0, 0x12);
    flash_shutdown();
    flash_shutdown();
    PT_ASSERT_EQ_U32(status, FLASH_STATUS_OK);
    PT_ASSERT_EQ_U32(chip()[0], 0x12);
    PT_ASSERT(flash_take_dirty()); /* shutdown does not discard unpersisted writes */
    flash_init();
    PT_ASSERT_EQ_U32(chip()[0], ERASED);
}

/* ---- the driver table entry ---- */

PTEST(entry_has_every_driver_function)
{
    PT_ASSERT(NativeFlash.programFlashByte != NULL);
    PT_ASSERT(NativeFlash.programFlashSector != NULL);
    PT_ASSERT(NativeFlash.eraseFlashChip != NULL);
    PT_ASSERT(NativeFlash.eraseFlashSector != NULL);
    PT_ASSERT(NativeFlash.WaitForFlashWrite != NULL);
}

/* IdentifyFlash (src/agb_flash_1m.c) walks sSetupInfos until an entry's
 * makerId is 0 or its joined ID equals ReadFlashId(): NativeFlash is
 * selected only if both hold. */
PTEST(entry_has_the_native_id_and_read_flash_id_returns_it)
{
    PT_ASSERT_EQ_U32(NativeFlash.type.ids.joined, FLASH_NATIVE_ID);
    PT_ASSERT_EQ_U32(ReadFlashId(), NativeFlash.type.ids.joined);
    PT_ASSERT(NativeFlash.type.ids.separate.makerId != 0);
    PT_ASSERT_EQ_U32(NativeFlash.type.ids.separate.makerId, FLASH_NATIVE_MAKER_ID);
    PT_ASSERT_EQ_U32(NativeFlash.type.ids.separate.deviceId, FLASH_NATIVE_DEVICE_ID);
}

PTEST(native_id_is_no_real_chip)
{
    /* GBATEK "GBA Cart Backup Flash ROM", Device Types; the last two are the
     * 1 Mbit chips expansion's MX29L010 and LE26FV10N1TS entries match. */
    static const uint16_t real_chips[] = {0xD4BF, 0x1CC2, 0x1B32, 0x3D1F, 0x1362, 0x09C2};
    for (size_t i = 0; i < sizeof real_chips / sizeof real_chips[0]; i++)
        PT_ASSERT(ReadFlashId() != real_chips[i]);
    PT_ASSERT(ReadFlashId() != 0); /* DefaultFlash's ID */
}

PTEST(entry_describes_a_1_mbit_chip_of_32_4k_sectors)
{
    PT_ASSERT_EQ_U32(NativeFlash.type.romSize, CHIP_BYTES);
    PT_ASSERT_EQ_U32(NativeFlash.type.sector.size, SECTOR_BYTES);
    PT_ASSERT_EQ_U32(NativeFlash.type.sector.shift, 12);
    PT_ASSERT_EQ_U32(NativeFlash.type.sector.count, SECTORS);
}

/* ---- init and the image ---- */

PTEST(init_erases_the_whole_chip_and_clears_dirty)
{
    flash_init();
    PT_ASSERT_EQ_U32(NativeFlash.eraseFlashSector(3), FLASH_STATUS_OK);
    memset(chip(), 0x00, CHIP_BYTES);
    flash_init();
    PT_ASSERT(first_byte_not(chip(), CHIP_BYTES, ERASED) == CHIP_BYTES);
    PT_ASSERT(!flash_take_dirty());
}

PTEST(image_is_the_mapped_region)
{
    PT_ASSERT_EQ_PTR(flash_image(), chip());
    const struct gba_region_info *const sram = gba_memmap_info(GBA_REGION_SRAM);
    PT_ASSERT(sram != NULL);
    PT_ASSERT(sram->size == CHIP_BYTES);
}

PTEST(erase_image_erases_without_marking_dirty)
{
    flash_init();
    memset(flash_image(), 0x00, CHIP_BYTES);
    flash_erase_image();
    PT_ASSERT(first_byte_not(chip(), CHIP_BYTES, ERASED) == CHIP_BYTES);
    PT_ASSERT(!flash_take_dirty());
}

/* ---- erase ---- */

static void check_erase_sector_touches_only_it(uint16_t sector)
{
    flash_init();
    memset(chip(), 0x00, CHIP_BYTES);
    const uint16_t status = NativeFlash.eraseFlashSector(sector);
    PT_ASSERT_EQ_U32(status, FLASH_STATUS_OK);
    PT_ASSERT(first_byte_not(sector_at(sector), SECTOR_BYTES, ERASED) == SECTOR_BYTES);
    PT_ASSERT(outside_is(sector * SECTOR_BYTES, SECTOR_BYTES, 0x00));
}

PTEST(erase_sector_fills_exactly_its_4096_bytes)
{
    check_erase_sector_touches_only_it(5);
}
PTEST(erase_first_sector_fills_exactly_its_bytes)
{
    check_erase_sector_touches_only_it(0);
}
PTEST(erase_sector_15_ends_at_the_bank_boundary)
{
    check_erase_sector_touches_only_it(15);
}
PTEST(erase_sector_16_starts_bank_1)
{
    check_erase_sector_touches_only_it(16);
}
PTEST(erase_last_sector_fills_exactly_its_bytes)
{
    check_erase_sector_touches_only_it(LAST_SECTOR);
}

PTEST(erase_chip_fills_every_byte)
{
    flash_init();
    memset(chip(), 0x00, CHIP_BYTES);
    PT_ASSERT_EQ_U32(NativeFlash.eraseFlashChip(), FLASH_STATUS_OK);
    PT_ASSERT(first_byte_not(chip(), CHIP_BYTES, ERASED) == CHIP_BYTES);
}

/* ---- program ---- */

/* ProgramFlashSector_MX erases the sector before programming it, and save.c
 * relies on that (TryWriteSector never erases), so a sector full of 0x00
 * takes the new contents, 0xFF bytes included. */
PTEST(program_sector_erases_then_writes_exactly_its_bytes)
{
    flash_init();
    uint8_t source[SECTOR_BYTES];
    fill_pattern(source, sizeof source, 0x11);
    memset(chip(), 0x00, CHIP_BYTES);
    const uint16_t status = NativeFlash.programFlashSector(9, source);
    PT_ASSERT_EQ_U32(status, FLASH_STATUS_OK);
    PT_ASSERT_MEM_EQ(sector_at(9), source, SECTOR_BYTES);
    PT_ASSERT(outside_is(9 * SECTOR_BYTES, SECTOR_BYTES, 0x00));
}

PTEST(program_sector_in_bank_1_lands_at_its_flat_offset)
{
    flash_init();
    uint8_t source[SECTOR_BYTES];
    fill_pattern(source, sizeof source, 0x5C);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashSector(20, source), FLASH_STATUS_OK);
    PT_ASSERT_MEM_EQ(chip() + 0x14000, source, SECTOR_BYTES);
    PT_ASSERT(outside_is(0x14000, SECTOR_BYTES, ERASED)); /* bank 0's sector 4 untouched */
}

PTEST(program_byte_writes_one_byte_within_the_sector)
{
    flash_init();
    PT_ASSERT_EQ_U32(NativeFlash.programFlashByte(7, 0, 0x12), FLASH_STATUS_OK);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashByte(7, SECTOR_BYTES - 1, 0x34), FLASH_STATUS_OK);
    PT_ASSERT_EQ_U32(sector_at(7)[0], 0x12);
    PT_ASSERT_EQ_U32(sector_at(7)[SECTOR_BYTES - 1], 0x34);
    PT_ASSERT(count_bytes_not(chip(), CHIP_BYTES, ERASED) == 2);
}

PTEST(write_count_counts_accepted_programs_and_erases_only)
{
    flash_init();
    PT_ASSERT_EQ_U32(flash_write_count(), 0);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashByte(2, 100, 0x0F), FLASH_STATUS_OK);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashByte(2, 100, 0xF0), TIME_LIMIT_PROGRAM_BYTE);             /* refused */
    PT_ASSERT_EQ_U32(NativeFlash.programFlashByte(FLASH_SECTOR_COUNT, 0, 0), FLASH_STATUS_BAD_SECTOR); /* refused */
    PT_ASSERT_EQ_U32(NativeFlash.eraseFlashSector(3), FLASH_STATUS_OK);
    PT_ASSERT_EQ_U32(flash_write_count(), 2);
    (void)flash_take_dirty(); /* taking the dirty flag does not reset the count */
    PT_ASSERT_EQ_U32(flash_write_count(), 2);
    flash_init();
    PT_ASSERT_EQ_U32(flash_write_count(), 0);
}

PTEST(program_byte_cannot_turn_a_0_bit_into_a_1)
{
    flash_init();
    PT_ASSERT_EQ_U32(NativeFlash.programFlashByte(2, 100, 0x0F), FLASH_STATUS_OK);
    PT_ASSERT(flash_take_dirty());
    PT_ASSERT_EQ_U32(NativeFlash.programFlashByte(2, 100, 0xF0), TIME_LIMIT_PROGRAM_BYTE);
    PT_ASSERT_EQ_U32(sector_at(2)[100], 0x0F); /* nothing written */
    PT_ASSERT(!flash_take_dirty());
    PT_ASSERT_EQ_U32(NativeFlash.programFlashByte(2, 100, 0x1F), TIME_LIMIT_PROGRAM_BYTE);
    PT_ASSERT_EQ_U32(sector_at(2)[100], 0x0F);
    /* Clearing more bits, or none, is a valid program. */
    PT_ASSERT_EQ_U32(NativeFlash.programFlashByte(2, 100, 0x05), FLASH_STATUS_OK);
    PT_ASSERT_EQ_U32(sector_at(2)[100], 0x05);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashByte(2, 100, 0x05), FLASH_STATUS_OK);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashByte(2, 100, 0x00), FLASH_STATUS_OK);
    PT_ASSERT_EQ_U32(sector_at(2)[100], 0x00);
    /* After an erase, every value can be programmed again. */
    PT_ASSERT_EQ_U32(NativeFlash.eraseFlashSector(2), FLASH_STATUS_OK);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashByte(2, 100, 0xF0), FLASH_STATUS_OK);
    PT_ASSERT_EQ_U32(sector_at(2)[100], 0xF0);
}

PTEST(program_byte_past_the_sector_end_is_rejected)
{
    flash_init();
    PT_ASSERT_EQ_U32(NativeFlash.programFlashByte(3, SECTOR_BYTES, 0x00), BAD_OFFSET);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashByte(3, UINT32_MAX, 0x00), BAD_OFFSET);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashByte(LAST_SECTOR, SECTOR_BYTES, 0x00), BAD_OFFSET);
    PT_ASSERT(first_byte_not(chip(), CHIP_BYTES, ERASED) == CHIP_BYTES);
    PT_ASSERT(!flash_take_dirty());
}

PTEST(program_sector_rejects_a_null_source)
{
    flash_init();
    memset(sector_at(4), 0x00, SECTOR_BYTES);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashSector(4, NULL), BAD_OFFSET);
    PT_ASSERT(first_byte_not(sector_at(4), SECTOR_BYTES, 0x00) == SECTOR_BYTES); /* not erased */
    PT_ASSERT(!flash_take_dirty());
}

/* The sector is erased before the copy, so a source in the chip would be
 * destroyed first; it is rejected, and nothing is erased. */
PTEST(program_sector_rejects_a_source_inside_the_chip)
{
    flash_init();
    memset(chip(), 0x00, CHIP_BYTES);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashSector(4, sector_at(4)), BAD_OFFSET);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashSector(4, sector_at(3)), BAD_OFFSET);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashSector(4, sector_at(3) + 1), BAD_OFFSET);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashSector(4, (uint8_t *)(uintptr_t)(CHIP_ADDRESS - SECTOR_BYTES + 1)),
                     BAD_OFFSET); /* its last byte is the chip's first */
    PT_ASSERT(first_byte_not(chip(), CHIP_BYTES, 0x00) == CHIP_BYTES);
    PT_ASSERT(!flash_take_dirty());
}

/* ---- sector bounds ---- */

PTEST(sector_31_is_the_last_valid_sector)
{
    flash_init();
    uint8_t source[SECTOR_BYTES];
    fill_pattern(source, sizeof source, 0x31);
    uint8_t read_back[SECTOR_BYTES];
    memset(read_back, 0, sizeof read_back);
    PT_ASSERT_EQ_U32(NativeFlash.eraseFlashSector(LAST_SECTOR), FLASH_STATUS_OK);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashSector(LAST_SECTOR, source), FLASH_STATUS_OK);
    PT_ASSERT_EQ_U32(VerifyFlashSector(LAST_SECTOR, source), 0);
    ReadFlash(LAST_SECTOR, 0, read_back, SECTOR_BYTES);
    PT_ASSERT_MEM_EQ(read_back, source, SECTOR_BYTES);
    PT_ASSERT_MEM_EQ(chip() + CHIP_BYTES - SECTOR_BYTES, source, SECTOR_BYTES);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashByte(LAST_SECTOR, SECTOR_BYTES - 1, 0x00), FLASH_STATUS_OK);
    PT_ASSERT_EQ_U32(chip()[CHIP_BYTES - 1], 0x00);
}

static void check_sector_rejected(uint16_t sector)
{
    flash_init();
    uint8_t source[SECTOR_BYTES];
    memset(source, 0x00, sizeof source);
    uint8_t dest[SECTOR_BYTES];
    memset(dest, 0xEE, sizeof dest);
    PT_ASSERT_EQ_U32(NativeFlash.eraseFlashSector(sector), BAD_SECTOR);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashSector(sector, source), BAD_SECTOR);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashByte(sector, 0, 0x00), BAD_SECTOR);
    PT_ASSERT_EQ_U32(VerifyFlashSector(sector, source), BAD_SECTOR);
    ReadFlash(sector, 0, dest, 1);
    PT_ASSERT_EQ_U32(dest[0], 0xEE);
    PT_ASSERT(first_byte_not(chip(), CHIP_BYTES, ERASED) == CHIP_BYTES);
    PT_ASSERT(!flash_take_dirty());
}

PTEST(sector_32_is_rejected)
{
    check_sector_rejected(SECTORS);
}
PTEST(sector_0xFFFF_is_rejected)
{
    check_sector_rejected(UINT16_MAX);
}

/* ---- read ---- */

PTEST(read_copies_the_requested_bytes)
{
    flash_init();
    uint8_t source[SECTOR_BYTES];
    fill_pattern(source, sizeof source, 0x20);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashSector(20, source), FLASH_STATUS_OK);
    uint8_t dest[SECTOR_BYTES];
    memset(dest, 0xEE, sizeof dest);
    ReadFlash(20, 0, dest, SECTOR_BYTES);
    PT_ASSERT_MEM_EQ(dest, source, SECTOR_BYTES);
    memset(dest, 0xEE, sizeof dest);
    ReadFlash(20, 100, dest, 50);
    PT_ASSERT_MEM_EQ(dest, source + 100, 50);
    PT_ASSERT_EQ_U32(dest[50], 0xEE);
    ReadFlash(20, SECTOR_BYTES - 1, dest, 1);
    PT_ASSERT_EQ_U32(dest[0], source[SECTOR_BYTES - 1]);
}

PTEST(read_rejects_zero_length_and_ranges_past_the_sector_end)
{
    flash_init();
    memset(sector_at(1), 0x00, 2 * SECTOR_BYTES); /* sectors 1 and 2 differ from dest */
    uint8_t dest[SECTOR_BYTES + 2];
    memset(dest, 0xEE, sizeof dest);
    ReadFlash(1, 0, dest, 0);
    ReadFlash(1, 0, dest, SECTOR_BYTES + 1);
    ReadFlash(1, 1, dest, SECTOR_BYTES);
    ReadFlash(1, SECTOR_BYTES, dest, 1);
    ReadFlash(1, UINT32_MAX, dest, 2); /* offset + size wraps to 1 */
    ReadFlash(1, 2, dest, UINT32_MAX);
    ReadFlash(1, 0, NULL, 1);
    PT_ASSERT(first_byte_not(dest, sizeof dest, 0xEE) == sizeof dest);
    PT_ASSERT(!flash_take_dirty());
}

PTEST(read_rejects_a_destination_inside_the_chip)
{
    flash_init();
    uint8_t source[SECTOR_BYTES];
    fill_pattern(source, sizeof source, 0x51);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashSector(1, source), FLASH_STATUS_OK);
    PT_ASSERT(flash_take_dirty());
    ReadFlash(1, 0, sector_at(5), 16);                             /* another sector */
    ReadFlash(1, 0, sector_at(1) + 8, 16);                         /* overlapping its source */
    ReadFlash(1, 0, chip() + CHIP_BYTES - 8, 16);                  /* across the chip's end */
    ReadFlash(1, 0, (uint8_t *)(uintptr_t)(CHIP_ADDRESS - 8), 16); /* across its start */
    PT_ASSERT(first_byte_not(sector_at(5), SECTOR_BYTES, ERASED) == SECTOR_BYTES);
    PT_ASSERT_MEM_EQ(sector_at(1), source, SECTOR_BYTES);
    PT_ASSERT(first_byte_not(chip() + CHIP_BYTES - 8, 8, ERASED) == 8);
    PT_ASSERT(!flash_take_dirty());
}

/* ---- what ReadFlash, which returns nothing, writes to stderr ---- */

#define REPORT_SIZE 1024

struct report {
    bool captured; /* stderr was redirected and read back whole */
    char text[REPORT_SIZE];
};

/* Runs action(context) with stderr sent to a temporary file, and returns
 * what was written to it (the pattern of test_interrupt.c). */
static struct report stderr_of(void (*action)(void *), void *context)
{
    struct report report = {.captured = false, .text = ""};
    report.captured = tsup_capture_stderr(action, context, report.text, sizeof report.text);
    return report;
}

struct read_call {
    uint16_t sector;
    uint32_t offset;
    uint8_t *dest;
    uint32_t size;
};

static void read_with(void *call)
{
    const struct read_call *const c = call;
    ReadFlash(c->sector, c->offset, c->dest, c->size);
}

static void check_read_report(struct read_call call, const char *expected)
{
    const struct report report = stderr_of(read_with, &call);
    PT_ASSERT(report.captured);
    if (strstr(report.text, expected) == NULL || strstr(report.text, "; nothing read\n") == NULL)
        PT_ASSERT_EQ_STR(report.text, expected);
}

PTEST(rejected_reads_are_reported_on_stderr)
{
    flash_init();
    uint8_t dest[SECTOR_BYTES];
    check_read_report((struct read_call){1, 0, dest, 0},
                      "flash: ReadFlash(sector 1, offset 0, dest, size 0): rejected");
    check_read_report((struct read_call){1, 1, dest, SECTOR_BYTES},
                      "flash: ReadFlash(sector 1, offset 1, dest, size 4096): rejected");
    check_read_report((struct read_call){SECTORS, 0, dest, 1},
                      "flash: ReadFlash(sector 32, offset 0, dest, size 1): rejected");
    check_read_report((struct read_call){1, 0, NULL, 1},
                      "flash: ReadFlash(sector 1, offset 0, NULL, size 1): rejected");
    check_read_report((struct read_call){1, 0, sector_at(2), 1},
                      "flash: ReadFlash(sector 1, offset 0, dest, size 1): rejected");
}

PTEST(a_valid_read_reports_nothing)
{
    flash_init();
    uint8_t dest[SECTOR_BYTES];
    struct read_call call = {LAST_SECTOR, 0, dest, SECTOR_BYTES};
    const struct report report = stderr_of(read_with, &call);
    PT_ASSERT(report.captured);
    PT_ASSERT_EQ_STR(report.text, "");
}

/* ---- verify ---- */

PTEST(verify_returns_0_or_the_address_of_the_first_difference)
{
    flash_init();
    uint8_t source[SECTOR_BYTES];
    fill_pattern(source, sizeof source, 0x17);
    PT_ASSERT_EQ_U32(NativeFlash.programFlashSector(17, source), FLASH_STATUS_OK);
    PT_ASSERT_EQ_U32(VerifyFlashSector(17, source), 0);
    source[123] ^= 0x01;
    PT_ASSERT_EQ_U32(VerifyFlashSector(17, source), CHIP_ADDRESS + 17 * SECTOR_BYTES + 123);
    source[SECTOR_BYTES - 1] ^= 0x80;
    source[0] ^= 0x80;
    PT_ASSERT_EQ_U32(VerifyFlashSector(17, source), CHIP_ADDRESS + 17 * SECTOR_BYTES);
    PT_ASSERT_EQ_U32(VerifyFlashSector(17, NULL), BAD_OFFSET);
}

/* ---- wait ---- */

PTEST(wait_for_write_reports_whether_the_byte_holds_the_value)
{
    flash_init();
    PT_ASSERT_EQ_U32(NativeFlash.programFlashByte(6, 10, 0x3C), FLASH_STATUS_OK);
    uint8_t *const programmed = sector_at(6) + 10;
    PT_ASSERT_EQ_U32(NativeFlash.WaitForFlashWrite(1, programmed, 0x3C), FLASH_STATUS_OK);
    PT_ASSERT_EQ_U32(NativeFlash.WaitForFlashWrite(1, programmed, 0x3D), TIMEOUT_PROGRAM_BYTE);
    PT_ASSERT_EQ_U32(NativeFlash.WaitForFlashWrite(2, sector_at(6), ERASED), FLASH_STATUS_OK);
    PT_ASSERT_EQ_U32(NativeFlash.WaitForFlashWrite(3, chip() + CHIP_BYTES - 1, ERASED), FLASH_STATUS_OK);
}

PTEST(wait_for_write_rejects_addresses_outside_the_chip)
{
    uint8_t *const past_end = (uint8_t *)(uintptr_t)(CHIP_ADDRESS + CHIP_BYTES);
    uint8_t *const before = (uint8_t *)(uintptr_t)(CHIP_ADDRESS - 1);
    PT_ASSERT_EQ_U32(NativeFlash.WaitForFlashWrite(1, past_end, ERASED), BAD_OFFSET);
    PT_ASSERT_EQ_U32(NativeFlash.WaitForFlashWrite(1, before, ERASED), BAD_OFFSET);
    PT_ASSERT_EQ_U32(NativeFlash.WaitForFlashWrite(1, NULL, 0), BAD_OFFSET);
}

/* ---- dirty ---- */

PTEST(every_program_and_erase_marks_dirty_and_take_clears_it)
{
    flash_init();
    uint8_t source[SECTOR_BYTES];
    fill_pattern(source, sizeof source, 0x44);
    PT_ASSERT(!flash_take_dirty());
    PT_ASSERT_EQ_U32(NativeFlash.eraseFlashSector(4), FLASH_STATUS_OK);
    PT_ASSERT(flash_take_dirty());
    PT_ASSERT(!flash_take_dirty());
    PT_ASSERT_EQ_U32(NativeFlash.programFlashByte(4, 0, 0x00), FLASH_STATUS_OK);
    PT_ASSERT(flash_take_dirty());
    PT_ASSERT(!flash_take_dirty());
    PT_ASSERT_EQ_U32(NativeFlash.programFlashSector(LAST_SECTOR, source), FLASH_STATUS_OK);
    PT_ASSERT(flash_take_dirty());
    PT_ASSERT(!flash_take_dirty());
    PT_ASSERT_EQ_U32(NativeFlash.eraseFlashChip(), FLASH_STATUS_OK);
    PT_ASSERT(flash_take_dirty());
    PT_ASSERT(!flash_take_dirty());
}

PTEST(reads_verifies_waits_and_id_do_not_mark_dirty)
{
    flash_init();
    uint8_t buffer[SECTOR_BYTES];
    memset(buffer, ERASED, sizeof buffer);
    ReadFlash(0, 0, buffer, SECTOR_BYTES);
    PT_ASSERT_EQ_U32(VerifyFlashSector(0, buffer), 0);
    PT_ASSERT_EQ_U32(NativeFlash.WaitForFlashWrite(1, chip(), ERASED), FLASH_STATUS_OK);
    PT_ASSERT(ReadFlashId() == FLASH_NATIVE_ID);
    PT_ASSERT(!flash_take_dirty());
}

PTEST_MAIN()
