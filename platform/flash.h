/*
 * flash.h: the cartridge's 1 Mbit flash chip, as a driver entry in
 * expansion's own flash driver table.
 *
 * Stands in for the Macronix MX29L010 / Sanyo LE26FV10N1TS chip of an Emerald
 * cartridge and for the parts of expansion's driver that talk to it
 * (src/agb_flash.c, src/agb_flash_mx.c). The real chip is 128 KiB in 32
 * sectors of 4 KiB, seen through the 64 KiB window at 0x0E000000 one bank at
 * a time; it is driven by command writes to 0x0E005555 and 0x0E002AAA and
 * polled until each operation completes; erased bytes read 0xFF and
 * programming can only clear bits (GBATEK "GBA Cart Backup Flash ROM",
 * https://problemkaputt.de/gbatek.htm#gbacartbackupflashrom). Here the
 * mapped region at 0x0E000000 (gba_memmap.h, GBA_REGION_SRAM) is 128 KiB and
 * holds the whole chip, both banks contiguous, so there is no bank to switch
 * and sector n is at 0x0E000000 + n * 4096; operations complete inside the
 * call. Design: docs/RESEARCH.md, "Save"; the ports replaced the chip the same
 * way (a fake ID selects a RAM driver) but erased to 0x00 and did not bounds-
 * check (docs/research/port-*.md).
 *
 * How the game reaches it. IdentifyFlash (src/agb_flash_1m.c) asks
 * ReadFlashId for the chip's ID and installs the function pointers of the
 * sSetupInfos entry with that ID. On PLATFORM_NATIVE, NativeFlash is the
 * first entry, and ReadFlashId, ReadFlash and VerifyFlashSector are this
 * module's (agb_flash.c compiles its own out under the guard;
 * docs/UPSTREAM_MODIFICATIONS.md), so IdentifyFlash always selects
 * NativeFlash and every save, load, erase and verify the game does runs here.
 *
 * Persistence. Every program and erase marks its sector dirty. The frame
 * loop must call flash_take_dirty() at every frame boundary and, when it
 * returns true, save_file_store() (save_file.h): that is what makes every
 * path that writes flash persist, including the ones that bypass
 * TrySavingData (the link and record-mixing saves, TryWriteSpecialSaveSector).
 *
 * The driver functions keep the types of include/gba/flash_internal.h
 * exactly (u8, u16 and u32 are uint8_t, uint16_t and uint32_t), including
 * its non-const source pointers; this module never writes through them.
 */
#ifndef FLASH_H
#define FLASH_H

#include <stdbool.h>
#include <stdint.h>

/* GBATEK "GBA Cart Backup Flash ROM": "128 KBytes - 1Mbit Flash ROM", in 4 KiB
 * sectors ("Erase 4Kbyte Sector"); 32 sectors, the count in expansion's
 * MX29L010 and LE26FV10N1TS entries. */
#define FLASH_SECTOR_SIZE 0x1000u
#define FLASH_SECTOR_COUNT 32u
#define FLASH_CHIP_SIZE (FLASH_SECTOR_SIZE * FLASH_SECTOR_COUNT)

/* GBATEK "GBA Cart Backup Flash ROM": "erased memory is FFh-filled". */
#define FLASH_ERASED_BYTE 0xFFu

/* The ID NativeFlash answers with, low byte the maker and high byte the
 * device as in GBATEK's "Identification Codes" and expansion's
 * FlashType.ids. Not an ID GBATEK lists for a real chip (D4BF, 1CC2, 1B32,
 * 3D1F, 1362, 09C2), and the maker byte is not 0, which IdentifyFlash
 * treats as the end of its table. 'N' for native, '1' for 1 Mbit. */
#define FLASH_NATIVE_MAKER_ID 0x4Eu
#define FLASH_NATIVE_DEVICE_ID 0x31u
#define FLASH_NATIVE_ID ((FLASH_NATIVE_DEVICE_ID << 8) | FLASH_NATIVE_MAKER_ID)

/* Status codes, as expansion's driver returns them (src/agb_flash_mx.c,
 * src/agb_flash_1m.c): 0 is success and every failure has bit 15 set; the
 * game treats any non-zero value as a damaged sector. */
#define FLASH_STATUS_OK 0x0000u
/* Sector index of FLASH_SECTOR_COUNT or more (EraseFlashSector_MX,
 * ProgramFlashSector_MX). */
#define FLASH_STATUS_BAD_SECTOR 0x80FFu
/* An offset past the sector (ProgramFlashByte_MX); here also a NULL buffer,
 * a zero length or a length past the sector's end. */
#define FLASH_STATUS_BAD_ARGUMENT 0x8000u
/* ORed with the phase: the chip did not reach the value programmed within its
 * time limit (WaitForFlashWrite_Common). A real chip cannot turn a 0 bit into
 * a 1 by programming (GBATEK: "The target memory location must have been
 * previously erased"), so such a program never completes; this is the code
 * the native driver returns for it. */
#define FLASH_STATUS_TIME_LIMIT 0xA000u
/* ORed with the phase: the driver's timer ran out before the chip showed the
 * value (WaitForFlashWrite_Common). */
#define FLASH_STATUS_TIMEOUT 0xC000u
/* The phase a byte program passes to WaitForFlashWrite (ProgramFlashByte_MX;
 * row 1 of mxMaxTime). */
#define FLASH_PHASE_PROGRAM_BYTE 1u

/* The struct FlashSetupInfo entry for the native chip (the type is
 * include/gba/flash_internal.h's; include that header to look inside).
 * src/agb_flash_1m.c lists it first in sSetupInfos under PLATFORM_NATIVE.
 * Its functions, all bounds-checked, all returning the status codes above:
 *   programFlashByte(sector, offset, data)  programs one byte; rejected with
 *       FLASH_STATUS_TIME_LIMIT | FLASH_PHASE_PROGRAM_BYTE, writing nothing,
 *       if it would turn a 0 bit into a 1 (the game always erases first, so
 *       this only catches bugs).
 *   programFlashSector(sector, src)  erases the sector, then programs its
 *       FLASH_SECTOR_SIZE bytes from src, as ProgramFlashSector_MX does (save.c
 *       relies on it erasing).
 *   eraseFlashChip(), eraseFlashSector(sector)  fill with FLASH_ERASED_BYTE.
 *   WaitForFlashWrite(phase, addr, lastData)  operations finish inside their
 *       call, so this only checks: FLASH_STATUS_OK if the byte at addr (a
 *       chip address) holds lastData, otherwise FLASH_STATUS_TIMEOUT | phase,
 *       since nothing will change it.
 * maxTime is NULL: it is read only by WaitForFlashWrite_Common's timer. */
struct FlashSetupInfo;
extern const struct FlashSetupInfo NativeFlash;

/* Link-time substitutes for src/agb_flash.c's functions of the same names,
 * which cannot run natively (see the top of this file). */

/* FLASH_NATIVE_ID: the chip ID IdentifyFlash compares with each entry. */
uint16_t ReadFlashId(void);

/* Copies size bytes at offset in sector sectorNum to dest. Rejects, printing
 * to stderr and copying nothing, a sector index of FLASH_SECTOR_COUNT or more,
 * a NULL dest, a size of 0, or a range past the sector's end. */
void ReadFlash(uint16_t sectorNum, uint32_t offset, uint8_t *dest, uint32_t size);

/* Compares sector sectorNum with the FLASH_SECTOR_SIZE bytes at src: 0 if
 * equal, otherwise the chip address (0x0E000000 upwards) of the first byte
 * that differs, as the GBA version returns. FLASH_STATUS_BAD_SECTOR or
 * FLASH_STATUS_BAD_ARGUMENT (NULL src) for invalid arguments; both are
 * non-zero and below every chip address. */
uint32_t VerifyFlashSector(uint16_t sectorNum, uint8_t *src);

/* Power-on: erases the whole chip (a blank cartridge) and clears the dirty
 * flags. Call once, before save_file_load(), which then replaces the image
 * with the save file's; never on a soft reset, because a cartridge keeps
 * its contents across one. */
void flash_init(void);

/* Releases nothing (the region belongs to gba_memmap) and changes nothing;
 * persist first if flash_take_dirty() says so. Safe at any time, including
 * before flash_init and more than once. */
void flash_shutdown(void);

/* True if any sector was programmed or erased since the last call, and
 * clears that record. Reads, verifies and rejected calls do not count, nor
 * do flash_init and flash_erase_image, which are not game writes. */
bool flash_take_dirty(void);

/* For the save file: the FLASH_CHIP_SIZE bytes of the chip, sector n at
 * offset n * FLASH_SECTOR_SIZE. Writes through it are not game writes and do
 * not mark sectors dirty. */
uint8_t *flash_image(void);

/* For the save file: erases the whole image without marking it dirty, so a
 * missing or rejected file leaves a blank chip and is not overwritten until
 * the game writes. */
void flash_erase_image(void);

#endif /* FLASH_H */
