/*
 * symbols.h: the running executable's function names, by address.
 *
 * Not hardware. The game's callbacks are function pointers (gMain.callback2,
 * a task's func), most of them static, so no header names them; the replay
 * inspection (inspect.h) turns the pointers into names by reading the
 * executable's own ELF symbol table (.symtab, which the native build keeps).
 * Only function symbols with a size are kept. Non-PIE executables only (ELF
 * type ET_EXEC, whose symbol values are run-time addresses, as the native build
 * links with -no-pie); a PIE is refused with a message. Single-threaded.
 */
#ifndef PLATFORM_SYMBOLS_H
#define PLATFORM_SYMBOLS_H

#include <stdint.h>

#define SYMBOLS_EXE_PATH "/proc/self/exe"

/* Reads the function symbols of the ELF executable at path (this build's
 * word size). Replaces anything loaded before. Returns 0, or -1 after
 * printing the reason (cannot open or read, not an ELF file of this width,
 * no .symtab, a table that lies about its size). After a failure nothing is
 * loaded. */
int symbols_load(const char *path);

/* Frees everything loaded. Safe before a load and more than once. */
void symbols_unload(void);

/* The name of the function that starts at address, or NULL if none does (or
 * nothing is loaded). When several symbols share the address (aliases) any
 * one of them is returned. The pointer is valid until the next load or
 * unload. */
const char *symbols_name_at(uintptr_t address);

/* How many function symbols are loaded. */
uint32_t symbols_count(void);

#endif /* PLATFORM_SYMBOLS_H */
