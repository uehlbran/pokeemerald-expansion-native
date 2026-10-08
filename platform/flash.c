/* flash.c: empty module; the tests in test/test_flash.c come first. */
#include <gba/types.h>
#include <gba/flash_internal.h>

#include "flash.h"
#include "gba_memmap.h"

static uint16_t program_byte(uint16_t sectorNum, uint32_t offset, uint8_t data)
{
    (void)sectorNum;
    (void)offset;
    (void)data;
    return 0;
}

static uint16_t program_sector(uint16_t sectorNum, uint8_t *src)
{
    (void)sectorNum;
    (void)src;
    return 0;
}

static uint16_t erase_chip(void)
{
    return 0;
}

static uint16_t erase_sector(uint16_t sectorNum)
{
    (void)sectorNum;
    return 0;
}

static uint16_t wait_for_write(uint8_t phase, uint8_t *addr, uint8_t lastData)
{
    (void)phase;
    (void)addr;
    (void)lastData;
    return 0;
}

const struct FlashSetupInfo NativeFlash = {
    program_byte, program_sector, erase_chip, erase_sector, wait_for_write, NULL, { 0 },
};

uint16_t ReadFlashId(void)
{
    return 0;
}

void ReadFlash(uint16_t sectorNum, uint32_t offset, uint8_t *dest, uint32_t size)
{
    (void)sectorNum;
    (void)offset;
    (void)dest;
    (void)size;
}

uint32_t VerifyFlashSector(uint16_t sectorNum, uint8_t *src)
{
    (void)sectorNum;
    (void)src;
    return 0;
}

void flash_init(void)
{
}

void flash_shutdown(void)
{
}

bool flash_take_dirty(void)
{
    return false;
}

uint8_t *flash_image(void)
{
    return (uint8_t *)(uintptr_t)GBA_SRAM_BASE;
}

void flash_erase_image(void)
{
}
