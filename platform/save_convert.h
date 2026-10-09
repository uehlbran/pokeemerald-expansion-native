/*
 * save_convert.h: moves the game's save between the 32-bit and the 64-bit
 * native build, whose struct layouts differ.
 *
 * Stands in for nothing on the GBA: a cartridge's save is read by the one
 * layout the game was compiled with. Natively the save holds expansion's
 * structs in the host ABI's layout (save_file.h), and at 64 bits SaveBlock1
 * is larger: its object event templates and its Enigma Berry record hold
 * pointers (save_layout32.h, save_layout64.h). A save carried from one build
 * to the other is the same cartridge moved between hosts; this module
 * rewrites the flash image so the other build reads it as the GBA would
 * have. Design: docs/DECISIONS.md, "Gate (h) is a platform-side save
 * converter"; the sector format is src/save.c's (include/save.h).
 *
 * What changes, per save slot (sectors 0-13 and 14-27, rotated within the
 * slot as src/save.c writes them): SaveBlock1's four sectors (ids 1-4) are
 * found by their ids; if all four have the signature and a checksum that
 * matches at the source layout's chunk sizes, the block is reassembled,
 * moved member by member into the target layout, split at the target's chunk
 * sizes and written back into the same four sectors with new checksums
 * (src/save.c CalculateChecksum), keeping each sector's id, signature,
 * counter and SaveBlock3 chunk. Otherwise the slot is left as it is: the
 * game's own GetSaveValidStatus then rejects it and falls back to the other
 * slot, exactly as it would have at the source width. Every other sector
 * (SaveBlock2, PokemonStorage, Hall of Fame, Trainer Hill, recorded battle)
 * has the same layout at both widths and is not touched.
 *
 * The pointers themselves are not carried over as addresses, which would be
 * meaningless in another executable: an object event template's script is
 * zeroed (the game refills every one from the map header on Continue,
 * LoadSaveblockObjEventScripts in src/overworld.c, as on the GBA), and the
 * Enigma Berry's two descriptions, raw e-Reader data that no native build
 * can dereference, are zero-extended or truncated. Padding the target layout
 * adds is zero. The Enigma Berry's own checksum (src/berry.c
 * GetEnigmaBerryChecksum, a byte sum) is recomputed in the target layout if
 * it was valid in the source and copied as it is otherwise.
 */
#ifndef SAVE_CONVERT_H
#define SAVE_CONVERT_H

#include <stdint.h>

/* The pointer width whose layout an image without a header
 * (SAVE_FILE_LEGACY) is taken to have: its only producers are GBA emulators
 * (the GBA's layout, which the 32-bit build shares) and native builds from
 * before the header, which were all 32-bit. */
#define SAVE_CONVERT_BARE_IMAGE_POINTER_BITS 32u

/* save_convert_image's error: a NULL image, or a pair of widths that is not
 * 32 and 64 in either order. */
#define SAVE_CONVERT_ERR_ARGUMENT (-1)

/* Converts the FLASH_CHIP_SIZE-byte flash image (flash.h) in place from the
 * SaveBlock1 layout of a build with from_bits-bit pointers to that of a
 * build with to_bits-bit pointers, as described at the top of this file.
 * Returns how many save slots were converted (0 to 2; an empty or damaged
 * slot is not), or SAVE_CONVERT_ERR_ARGUMENT, changing nothing. Does not
 * mark the image dirty: flash_image() writes are not game writes. */
int save_convert_image(uint8_t *image, uint32_t from_bits, uint32_t to_bits);

#endif /* SAVE_CONVERT_H */
