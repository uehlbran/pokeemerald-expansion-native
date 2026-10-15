/* Probe only (task i): the save chip address as a link-time symbol. */
#include_next "gba/flash_internal.h"
#ifndef PEN_WRAP_FLASH
#define PEN_WRAP_FLASH
extern char pen_gba_sram[];
#undef FLASH_BASE
#define FLASH_BASE ((u8 *)pen_gba_sram)
#endif
