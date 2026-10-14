/*
 * alloca.h for MinGW-w64, which has none (it declares alloca in <malloc.h>).
 * src/assertf.c and src/random.c include <alloca.h>, as GCC on Linux and the
 * GBA toolchain have it. Defined as glibc's header defines it, and not by
 * including <malloc.h>: random.c includes this after global.h, whose abs() and
 * min() macros break MinGW's <stdlib.h>, which <malloc.h> pulls in. Build
 * glue for the Windows build, found through -idirafter (platform/target.mk);
 * expansion's files stay as they are.
 */
#ifndef PLATFORM_MINGW_ALLOCA_H
#define PLATFORM_MINGW_ALLOCA_H

#define alloca(size) __builtin_alloca(size)

#endif
