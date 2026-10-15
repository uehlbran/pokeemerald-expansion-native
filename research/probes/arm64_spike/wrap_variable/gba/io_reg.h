/* Probe only (task i): REG_BASE as an offset from a run-time base. */
#include <stdint.h>
#include_next "gba/io_reg.h"
#ifndef PEN_WRAP_IO_REG
#define PEN_WRAP_IO_REG
extern uintptr_t pen_gba_base;
#undef REG_BASE
#define REG_BASE (pen_gba_base + 0x4000000)
#endif
