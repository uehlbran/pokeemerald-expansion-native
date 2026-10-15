/*
 * version.h: which build of the game this is, for --version, --keys and the
 * help overlay.
 *
 * Not hardware: a cartridge's header carried a version byte (GBATEK "GBA
 * Cartridge Header", 0BCh, "Software Version"), which this does not stand
 * in for; it names the source the native executable was built from.
 */
#ifndef PLATFORM_VERSION_H
#define PLATFORM_VERSION_H

/* The program's name, before the version in what it prints. */
#define VERSION_PROGRAM_NAME "pokeemerald-native"

/* `git describe --always --dirty --match 'v[0-9]*'` of the tree
 * Makefile.native built from (a release tag of this project's, else the
 * commit's abbreviated hash, "-dirty" if files were changed), or "unknown"
 * without git or outside Makefile.native. */
const char *version_string(void);

#endif /* PLATFORM_VERSION_H */
