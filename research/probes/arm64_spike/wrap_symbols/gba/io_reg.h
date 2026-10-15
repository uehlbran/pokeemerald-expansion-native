/* Probe only (task i): REG_BASE as a link-time symbol; every REG_ADDR_ and
   REG_ macro of expansion's io_reg.h is defined from it and follows. */
#include <stdint.h>
#include_next "gba/io_reg.h"
#ifndef PEN_WRAP_IO_REG
#define PEN_WRAP_IO_REG
extern char pen_gba_io[];
#undef REG_BASE
#define REG_BASE ((uintptr_t)pen_gba_io)
#endif
