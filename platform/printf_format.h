/*
 * printf_format.h: PLATFORM_PRINTF_FORMAT, the format attribute of our printf-like
 * functions. MinGW's GCC reads `format(printf, ...)` as the C runtime's
 * format, which has no %zu or %jd, while __USE_MINGW_ANSI_STDIO=1
 * (target.mk) makes the printf family take them; its <_mingw.h> names the
 * archetype that agrees, __MINGW_PRINTF_FORMAT. Elsewhere it is `printf`.
 */
#ifndef PLATFORM_PRINTF_FORMAT_H
#define PLATFORM_PRINTF_FORMAT_H

#include <stdio.h> /* brings in __MINGW_PRINTF_FORMAT, where there is one */

#ifdef __MINGW_PRINTF_FORMAT
#define PLATFORM_PRINTF_FORMAT(format_index, first_argument) \
    __attribute__((format(__MINGW_PRINTF_FORMAT, format_index, first_argument)))
#else
#define PLATFORM_PRINTF_FORMAT(format_index, first_argument) \
    __attribute__((format(printf, format_index, first_argument)))
#endif

#endif /* PLATFORM_PRINTF_FORMAT_H */
