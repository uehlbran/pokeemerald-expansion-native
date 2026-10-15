/*
 * version.c: the build's version (version.h).
 *
 * PLATFORM_VERSION is defined for this file alone (platform/Makefile, from
 * Makefile.native): in the flags of every file it would change all their
 * compile commands at each commit, and ccache would miss on every one.
 */
#include "version.h"

#ifndef PLATFORM_VERSION
#define PLATFORM_VERSION "unknown"
#endif

const char *version_string(void)
{
    return PLATFORM_VERSION;
}
