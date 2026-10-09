/*
 * save_layout64.h: where a 64-bit build keeps the members of SaveBlock1 that
 * a save conversion must find, in bytes from the start of the block.
 *
 * Stands in for nothing on the GBA, where the struct has one layout; the
 * counterpart of save_layout32.h (which says why there are two), with the same
 * names. At 64 bits the two pointer members are 8 bytes: each object event
 * template grows by 4 bytes, the Enigma Berry record by 12 (a 4-byte hole
 * before description1, which is 8-byte aligned, and 4 more bytes for each
 * description), and the block gains 4 bytes of tail padding (its alignment
 * is 8).
 *
 * Every value is pinned by a static assertion in save_convert.c, compiled
 * with the game's flags, in every 64-bit build: an upstream change to
 * SaveBlock1, ObjectEventTemplate or EnigmaBerry stops the 64-bit build
 * until this table is updated. Measured with offsetof and sizeof (gcc -m64,
 * the game's defines, TESTING 0 and 1 alike).
 */
#ifndef SAVE_LAYOUT64_H
#define SAVE_LAYOUT64_H

#define SAVE_LAYOUT64_POINTER_SIZE 8u

#define SAVE_LAYOUT64_SAVEBLOCK1_SIZE 15768u
#define SAVE_LAYOUT64_SAVEBLOCK1_DATA_END 15764u

#define SAVE_LAYOUT64_OBJECT_EVENT_TEMPLATES 3144u
#define SAVE_LAYOUT64_OBJECT_EVENT_TEMPLATE_COUNT 64u
#define SAVE_LAYOUT64_OBJECT_EVENT_TEMPLATE_SIZE 28u
#define SAVE_LAYOUT64_TEMPLATE_SCRIPT 16u
#define SAVE_LAYOUT64_TEMPLATE_FLAG_ID 24u

#define SAVE_LAYOUT64_FLAGS 4936u

#define SAVE_LAYOUT64_ENIGMA_BERRY 12976u
#define SAVE_LAYOUT64_ENIGMA_BERRY_SIZE 64u
#define SAVE_LAYOUT64_BERRY_MAX_YIELD 10u
#define SAVE_LAYOUT64_BERRY_DESCRIPTION1 16u
#define SAVE_LAYOUT64_BERRY_DESCRIPTION2 24u
#define SAVE_LAYOUT64_BERRY_GROWTH_DURATION 32u
#define SAVE_LAYOUT64_ENIGMA_CHECKSUM 60u

#define SAVE_LAYOUT64_MYSTERY_GIFT 13040u

#endif /* SAVE_LAYOUT64_H */
