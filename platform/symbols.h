/*
 * symbols.h: the running executable's function names, by address.
 *
 * Not hardware. The game's callbacks are function pointers (gMain.callback2,
 * a task's func), most of them static, so no header names them; the replay
 * inspection (inspect.h) turns the pointers into names by reading the
 * function map the native link writes next to the executable
 * (<executable>.symbols, written with nm by platform/host.mk's
 * write_symbol_map: every function symbol with a size, as the executable's
 * symbol table lists them). The executable is linked non-PIE, so the map's
 * addresses are its run-time addresses. Single-threaded.
 *
 * The map is text, one function per line:
 *     <address> <name>\n
 * the address SYMBOLS_ADDRESS_DIGITS hexadecimal digits (this build's
 * pointer width, as nm prints it), one space, and a name of one or more
 * characters other than spaces and control characters; the lines in
 * ascending address order. Functions that share an address (aliases) keep
 * the symbol table's order, and the first one is the name given.
 */
#ifndef PLATFORM_SYMBOLS_H
#define PLATFORM_SYMBOLS_H

#include <stdint.h>

/* What the map's path adds to the executable's. */
#define SYMBOLS_MAP_SUFFIX ".symbols"

/* Hexadecimal digits of an address in the map: two per byte of a pointer. */
#define SYMBOLS_ADDRESS_DIGITS (2u * sizeof(uintptr_t))

/* Reads the map at path. Replaces anything loaded before. Returns 0, or -1
 * after printing the reason (cannot open or read it; a line, named by its
 * number, that is not as above or is out of address order). After a failure
 * nothing is loaded. */
int symbols_load(const char *path);

/* Reads the running executable's map: its path (host_process) plus
 * SYMBOLS_MAP_SUFFIX. As symbols_load. */
int symbols_load_own(void);

/* Frees everything loaded. Safe before a load and more than once. */
void symbols_unload(void);

/* The name of the function that starts at address, or NULL if none does (or
 * nothing is loaded). The pointer is valid until the next load or unload. */
const char *symbols_name_at(uintptr_t address);

/* How many functions are loaded. */
uint32_t symbols_count(void);

#endif /* PLATFORM_SYMBOLS_H */
