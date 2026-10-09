/*
 * crt0.h: what expansion's src/crt0.s did besides calling AgbMain.
 *
 * Stands in for the ROM entry code (src/crt0.s, not built natively): before
 * AgbMain it reset RAM and the registers (RegisterRamReset(RESET_ALL)) and
 * copied the initialised EWRAM and IWRAM data from ROM, at power-on and
 * again when SoftReset restarts the ROM. Natively the ELF loader does that at
 * power-on, so crt0 here keeps copies of the initialised data, the EWRAM_INIT
 * data and the game's own (gba_game.ld's game_data), and on a soft reset
 * zeroes the game's variables (game_bss) and copies the initial values back,
 * leaving the platform's own state alone. The symbols the game takes from
 * crt0.s, IntrMain and ReInitializeEWRAM, are defined in crt0.c under their
 * names (declared for the game in include/crt0.h).
 */
#ifndef PLATFORM_CRT0_H
#define PLATFORM_CRT0_H

#include <stddef.h>
#include <stdint.h>

/* The most EWRAM_INIT data crt0 keeps a copy of. Expansion 1.17.1 has one
 * byte (sNameboxWindowId, src/field_name_box.c); a link with more makes
 * crt0_init fail, so raise it. The game's initialised data has no fixed
 * bound (four bytes, an IWRAM_INIT variable in src/load_save.c, but in the
 * sanitized build every instrumented global with its redzones), so its copy
 * is allocated at init. */
#define CRT0_EWRAM_INIT_MAX_SIZE 0x1000u

/* Bytes of the IntrMain symbol (see crt0.c). */
#define CRT0_INTR_MAIN_SIZE 0x800u

/* src/crt0.s IntrMain, whose address src/main.c stores in INTR_VECTOR; data
 * natively (crt0.c says why). */
extern const uint32_t IntrMain[CRT0_INTR_MAIN_SIZE / sizeof(uint32_t)];

enum crt0_error {
    CRT0_OK = 0,
    CRT0_ERR_EWRAM_INIT_SIZE = -1, /* the EWRAM_INIT data is larger than CRT0_EWRAM_INIT_MAX_SIZE */
    CRT0_ERR_NO_MEMORY = -2,       /* no memory for the copy of the game's initialised data */
    CRT0_ERR_NO_GAME_OBJECTS = -3, /* the game executable's game_bss has no part from the game's objects */
    CRT0_ERR_BAD_TEST_TABLE = -4   /* the test runner build's .tests table is not a whole number of entries */
};

/* The .tests table check (test runner build only; no GBA counterpart: the
 * GBA links the table the same way, but its struct Test has no padding to
 * go wrong). The table is test/test_runner.c's array from __start_tests to
 * __stop_tests (gba_game.ld); the test runner build exports sizeof(struct
 * Test) as gPlatformTestEntrySize. GCC aligns a 40-byte global to 32 bytes
 * at 64 bits, which puts padding between the entries, unless the test
 * objects are compiled with -malign-data=abi (Makefile.native). Returns
 * CRT0_OK and the number of entries in *count if table_bytes is a whole
 * number of entry_size-byte entries; CRT0_ERR_BAD_TEST_TABLE if not (or if
 * entry_size is 0 or count is NULL).
 *
 * crt0_init runs two checks on the table. This one catches a table whose
 * length is not a multiple of the entry size: a different entry size from
 * the runner's, or padding that happens to leave a remainder. It misses
 * padded tables whose length is a multiple by chance (about one count in
 * five at 64 bits). The second, the runner's PlatformTestTableBadEntry
 * (test/test_runner.c), walks the table by sizeof(struct Test) and refuses a
 * null name, filename or runner: padding reads as zeros, so it catches
 * 64-byte spacing at every count. */
int crt0_check_test_table(size_t table_bytes, size_t entry_size, size_t *count);

/* Copies the EWRAM_INIT data and the game's initialised data as the loader
 * left them. Call once, before AgbMain first runs. CRT0_OK or an error
 * (reported). In the game executable (gba_game.ld: gba_game_objects_linked)
 * it fails with CRT0_ERR_NO_GAME_OBJECTS if gba_game.ld's object-path
 * patterns selected no .bss: a soft reset would then leave the game's
 * variables stale, so it says so at start-up instead. In the test runner
 * build it also checks the .tests table (crt0_check_test_table) and fails
 * with CRT0_ERR_BAD_TEST_TABLE, so no test runs from a misread table. */
int crt0_init(void);

/* Frees the copies. Safe at any time and twice. */
void crt0_shutdown(void);

/* The sizes the link placed (for tests and reports). */
size_t crt0_ewram_init_size(void);
size_t crt0_game_data_size(void);
size_t crt0_game_bss_size(void);

/* After SoftReset, before AgbMain runs again (the BIOS's RegisterRamReset
 * having cleared the GBA regions): zeroes the game's variables, rewrites
 * its initialised ones and the EWRAM_INIT data with their initial values.
 * Does nothing before crt0_init. */
void crt0_restart(void);

/* src/crt0.s ReInitializeEWRAM: rewrites the EWRAM_INIT data with its
 * initial values, as after RegisterRamReset(RESET_EWRAM). Called by
 * src/reload_save.c, and by main on a soft reset. Does nothing before
 * crt0_init. */
void ReInitializeEWRAM(void);

#endif /* PLATFORM_CRT0_H */
