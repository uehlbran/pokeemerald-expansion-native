/*
 * flash.c: the cartridge's 1 Mbit flash chip, as a driver entry in
 * expansion's flash driver table. Interface and how the game reaches it:
 * flash.h.
 *
 * Stands in for the MX29L010 / LE26FV10N1TS chip and for the chip-facing
 * half of expansion's driver (src/agb_flash.c, src/agb_flash_mx.c), written
 * from GBATEK "GBA Cart Backup Flash ROM"
 * (https://problemkaputt.de/gbatek.htm#gbacartbackupflashrom) and those
 * sources; no reference port's code is used (docs/C-RULES.md, licensing).
 *
 * WHAT THE REAL CHIP DOES, AND WHAT IS LEFT HERE.
 *  - Commands. The chip is driven by writes of AAh to E005555h and 55h to
 *    E002AAAh followed by a command byte (ID mode 90h, erase 80h then 10h for
 *    the chip or 30h at the sector, byte program A0h, bank select B0h); each
 *    program or erase is then polled until the location reads back the
 *    target value, under a timer. Here each driver function does its whole
 *    operation on the mapped region and returns, so there are no command
 *    writes, no polling and no timer.
 *  - Banks. The GBA sees the 128 KiB chip through a 64 KiB window, so the
 *    driver selects bank 0 or 1 before touching sectors 0-15 or 16-31. The
 *    native region is 128 KiB (gba_memmap.h, GBA_SRAM_SIZE) and holds both
 *    banks back to back, so bank switching is a no-op: sector n is at
 *    0x0E000000 + n * 4096 for every n, which is also its offset in a .sav.
 *  - Reads from WRAM. GBATEK: flash can be read "only by opcodes executed in
 *    WRAM", which is why agb_flash.c copies its read and verify loops onto
 *    the stack and calls the copy. Native code cannot run from a data buffer
 *    (and is not Thumb code), so ReadFlash and VerifyFlashSector are defined
 *    here instead, reading the region directly; agb_flash.c compiles its own
 *    out under PLATFORM_NATIVE (docs/UPSTREAM_MODIFICATIONS.md). Its other
 *    chip functions (SwitchFlashBank, SetReadFlash1) stay, reachable only
 *    from the MX driver functions that IdentifyFlash never installs natively.
 *  - Bits. Erased bytes are FFh; a program can only clear bits (GBATEK: "The
 *    target memory location must have been previously erased"). The byte
 *    program enforces this and rejects a value that would set a cleared bit,
 *    returning what the real driver returns when the chip fails to reach the
 *    value: the game always erases before programming, so the rule never
 *    rejects a correct save and catches a broken one the way hardware would.
 *    ProgramFlashSector erases first, as ProgramFlashSector_MX does, so the
 *    rule always holds for it.
 *  - Bounds. Every sector index, offset and length is checked against
 *    FLASH_SECTOR_COUNT and FLASH_SECTOR_SIZE before the region is touched,
 *    and rejected with the driver's status codes (flash.h); the GBA driver
 *    trusts its caller.
 *
 * Dirty flags. Each successful program or erase marks its sector in a 32-bit
 * mask; flash_take_dirty() reports and clears it for the frame loop, which
 * then calls save_file_store(). Endianness: FlashType.ids is a union of the
 * maker and device bytes with a u16; like the ARM7TDMI, x86 is
 * little-endian, so the maker is the low byte of joined, as on the GBA.
 *
 * This module includes expansion's include/gba/flash_internal.h (and
 * gba/types.h, which it needs), as system headers, because NativeFlash is
 * an entry of expansion's struct FlashSetupInfo; its other dependency is
 * gba_memmap.h for the region.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <gba/types.h>
#include <gba/flash_internal.h>

#include "flash.h"
#include "gba_memmap.h"

/* log2(FLASH_SECTOR_SIZE): FlashSector.shift, the "bit shift to multiply by
 * sector size" of expansion's entries. */
#define FLASH_SECTOR_SHIFT 12u

/* Game Pak bus wait states for write and read, FlashType.wait. Unused
 * natively (no bus); the values of expansion's two 1 Mbit entries, so
 * anything that reads them sets what it would on the GBA. */
#define FLASH_WAIT_WRITE 3u
#define FLASH_WAIT_READ 1u

_Static_assert(FLASH_SECTOR_SIZE == 1u << FLASH_SECTOR_SHIFT, "sector shift");
_Static_assert(FLASH_CHIP_SIZE == GBA_SRAM_SIZE, "the mapped region must hold the whole chip");
_Static_assert(FLASH_CHIP_SIZE == FLASH_ROM_SIZE_1M, "not expansion's 1 Mbit size");
_Static_assert(FLASH_SECTOR_COUNT <= 32, "the dirty mask has one bit per sector");
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
               "FlashType.ids.joined must have the maker in its low byte, as on the GBA");

/* Sectors programmed or erased since the last flash_take_dirty(), bit n for
 * sector n. */
static uint32_t dirty_sectors;

/* The chip: the mapped region at its GBA address. Integer to pointer,
 * flagged for the 64-bit milestone: GBA addresses are native addresses
 * (gba_memmap.h). */
static uint8_t *chip(void)
{
    return (uint8_t *)(uintptr_t)GBA_SRAM_BASE;
}

static bool is_sector(uint32_t sector)
{
    return sector < FLASH_SECTOR_COUNT;
}

/* [offset, offset + length) is a non-empty range inside one sector. */
static bool is_range_in_sector(uint32_t offset, uint32_t length)
{
    return length != 0 && offset < FLASH_SECTOR_SIZE && length <= FLASH_SECTOR_SIZE - offset;
}

static uint8_t *sector_bytes(uint32_t sector)
{
    return chip() + (size_t)sector * FLASH_SECTOR_SIZE;
}

static void mark_dirty(uint32_t sector)
{
    dirty_sectors |= UINT32_C(1) << sector;
}

static void erase(uint32_t sector)
{
    memset(sector_bytes(sector), FLASH_ERASED_BYTE, FLASH_SECTOR_SIZE);
    mark_dirty(sector);
}

/* ---- the driver functions of NativeFlash (signatures: flash_internal.h) ---- */

static u16 program_flash_byte(u16 sectorNum, u32 offset, u8 data)
{
    if (!is_sector(sectorNum))
        return FLASH_STATUS_BAD_SECTOR;
    if (!is_range_in_sector(offset, 1))
        return FLASH_STATUS_BAD_ARGUMENT;
    uint8_t *const byte = sector_bytes(sectorNum) + offset;
    if ((data & ~*byte) != 0) /* a 1 where the chip holds a 0 */
        return FLASH_STATUS_TIME_LIMIT | FLASH_PHASE_PROGRAM_BYTE;
    *byte = data;
    mark_dirty(sectorNum);
    return FLASH_STATUS_OK;
}

/* src is non-const only because the table's type says so. */
static u16 program_flash_sector(u16 sectorNum, u8 *src)
{
    if (!is_sector(sectorNum))
        return FLASH_STATUS_BAD_SECTOR;
    if (src == NULL)
        return FLASH_STATUS_BAD_ARGUMENT;
    erase(sectorNum); /* every byte FFh, so programming any value only clears bits */
    memcpy(sector_bytes(sectorNum), src, FLASH_SECTOR_SIZE);
    return FLASH_STATUS_OK;
}

static u16 erase_flash_chip(void)
{
    for (uint32_t sector = 0; sector < FLASH_SECTOR_COUNT; sector++)
        erase(sector);
    return FLASH_STATUS_OK;
}

static u16 erase_flash_sector(u16 sectorNum)
{
    if (!is_sector(sectorNum))
        return FLASH_STATUS_BAD_SECTOR;
    erase(sectorNum);
    return FLASH_STATUS_OK;
}

/* Every operation has finished when its function returns, so the byte at
 * addr either holds lastData now or never will. */
static u16 wait_for_flash_write(u8 phase, u8 *addr, u8 lastData)
{
    /* Pointer to integer, flagged: the bounds check compares GBA addresses. */
    if (addr == NULL || !gba_memmap_contains((uintptr_t)addr, 1, GBA_REGION_SRAM))
        return FLASH_STATUS_BAD_ARGUMENT;
    return *addr == lastData ? FLASH_STATUS_OK : (u16)(FLASH_STATUS_TIMEOUT | phase);
}

const struct FlashSetupInfo NativeFlash = {
    .programFlashByte = program_flash_byte,
    .programFlashSector = program_flash_sector,
    .eraseFlashChip = erase_flash_chip,
    .eraseFlashSector = erase_flash_sector,
    .WaitForFlashWrite = wait_for_flash_write,
    .maxTime = NULL, /* read only by WaitForFlashWrite_Common's timer */
    .type = {
        .romSize = FLASH_CHIP_SIZE,
        .sector = {
            .size = FLASH_SECTOR_SIZE,
            .shift = FLASH_SECTOR_SHIFT,
            .count = FLASH_SECTOR_COUNT,
            .top = 0, /* unused by expansion */
        },
        .wait = { FLASH_WAIT_WRITE, FLASH_WAIT_READ },
        .ids = { .separate = { .makerId = FLASH_NATIVE_MAKER_ID,
                               .deviceId = FLASH_NATIVE_DEVICE_ID } },
    },
};

/* ---- link-time substitutes for agb_flash.c ---- */

u16 ReadFlashId(void)
{
    return FLASH_NATIVE_ID;
}

void ReadFlash(u16 sectorNum, u32 offset, u8 *dest, u32 size)
{
    if (!is_sector(sectorNum) || dest == NULL || !is_range_in_sector(offset, size)) {
        (void)fprintf(stderr,
                      "flash: ReadFlash(sector %" PRIu16 ", offset %" PRIu32 ", %s, size %" PRIu32
                      "): rejected, a read is 1 to %u bytes inside one of the chip's %u "
                      "sectors into a buffer; nothing read\n",
                      sectorNum, offset, dest == NULL ? "NULL" : "dest", size,
                      FLASH_SECTOR_SIZE, FLASH_SECTOR_COUNT); /* best-effort */
        return;
    }
    memcpy(dest, sector_bytes(sectorNum) + offset, size);
}

/* src is non-const only to match the declaration agb_flash.c's
 * ProgramFlashSectorAndVerify sees. */
u32 VerifyFlashSector(u16 sectorNum, u8 *src)
{
    if (!is_sector(sectorNum))
        return FLASH_STATUS_BAD_SECTOR;
    if (src == NULL)
        return FLASH_STATUS_BAD_ARGUMENT;
    const uint8_t *const sector = sector_bytes(sectorNum);
    for (uint32_t offset = 0; offset < FLASH_SECTOR_SIZE; offset++)
        if (sector[offset] != src[offset])
            return GBA_SRAM_BASE + sectorNum * FLASH_SECTOR_SIZE + offset;
    return 0;
}

/* ---- lifecycle and the save file's view ---- */

void flash_init(void)
{
    flash_erase_image();
    dirty_sectors = 0;
}

void flash_shutdown(void)
{
    /* Nothing to release: the region belongs to gba_memmap, and the dirty
     * flags stay for a store that has not happened yet. */
}

bool flash_take_dirty(void)
{
    const bool dirty = dirty_sectors != 0;
    dirty_sectors = 0;
    return dirty;
}

uint8_t *flash_image(void)
{
    return chip();
}

void flash_erase_image(void)
{
    memset(chip(), FLASH_ERASED_BYTE, FLASH_CHIP_SIZE);
}
