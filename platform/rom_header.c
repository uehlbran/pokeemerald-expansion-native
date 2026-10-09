/*
 * rom_header.c: the two fields of the cartridge header the game reads.
 *
 * Stands in for the ROM header (GBATEK "GBA Cartridge Header"), which
 * src/rom_header.s reserves and gbafix fills in from expansion's Makefile.
 * src/mystery_gift.c copies the game code and software version into the
 * Mystery Gift data it exchanges. Natively there is no header, so the
 * values are what gbafix writes for Emerald: -cBPEE (GAME_CODE) and -r0
 * (REVISION). Builds of FireRed or LeafGreen (BPRE, BPGE) will need the code
 * passed in.
 */
#include <stdint.h>

/* GBATEK "GBA Cartridge Header": 0ACh, Game Code, 4 bytes, uppercase ASCII. */
#define ROM_HEADER_GAME_CODE_LENGTH 4u
/* GBATEK: 0BCh, Software Version, usually 00h. */
#define ROM_HEADER_SOFTWARE_VERSION UINT8_C(0)

/* Not a string: four bytes, no terminator, as in the header. */
const uint8_t RomHeaderGameCode[ROM_HEADER_GAME_CODE_LENGTH] = {'B', 'P', 'E', 'E'};
const uint8_t RomHeaderSoftwareVersion = ROM_HEADER_SOFTWARE_VERSION;
