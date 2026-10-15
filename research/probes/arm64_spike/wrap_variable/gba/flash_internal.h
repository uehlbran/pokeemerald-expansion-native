/* Probe only (task i): the save chip address from a run-time base. */
#include <stdint.h>
#include_next "gba/flash_internal.h"
#ifndef PEN_WRAP_FLASH
#define PEN_WRAP_FLASH
extern uintptr_t pen_gba_base;
#undef FLASH_BASE
#define FLASH_BASE ((u8 *)pen_gba_base + 0x0E000000)
#endif
