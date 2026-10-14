/*
 * mingw_printf.h: force-included (-include, platform/Makefile) into
 * save_file.c in the Windows build only. MinGW's GCC reads
 * format(printf, ...) against the C runtime's formats, which lack %zu and %jd,
 * while __USE_MINGW_ANSI_STDIO=1 (target.mk) makes the printf family take
 * them. Naming the archetype gnu_printf makes the check agree. save_file.c
 * names printf only in that attribute (no call), so after <stdio.h> has
 * declared the real printf the token can be redefined.
 *
 * Temporary: task W5 gives the printf-like functions a portable format
 * attribute (or save_file.c's messages move) and deletes this file.
 */
#ifndef PLATFORM_MINGW_PRINTF_H
#define PLATFORM_MINGW_PRINTF_H

#include <stdio.h>

#define printf gnu_printf

#endif /* PLATFORM_MINGW_PRINTF_H */
