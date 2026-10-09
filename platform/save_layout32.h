/*
 * save_layout32.h: where a 32-bit build keeps the members of SaveBlock1 that
 * a save conversion must find, in bytes from the start of the block.
 *
 * Stands in for nothing on the GBA, where the struct has one layout. Natively
 * the layout is the host ABI's (save_file.h, SAVE_FILE_LAYOUT_ID), and two
 * members of SaveBlock1 hold pointers, so a 32-bit build's save and a 64-bit
 * build's differ (docs/DECISIONS.md, "Gate (h) is a platform-side save
 * converter"); save_convert.c moves a save between the two using this table
 * and save_layout64.h. The 32-bit layout is the GBA's (i386 and ARM agree on
 * the size and alignment of every member), so an emulator's save has it too.
 *
 * Every value is pinned by a static assertion in save_convert.c, compiled
 * with the game's flags, in every 32-bit build: an upstream change to
 * SaveBlock1, ObjectEventTemplate or EnigmaBerry stops the 32-bit build
 * until this table is updated. Measured with offsetof and sizeof (gcc -m32,
 * the game's defines, TESTING 0 and 1 alike).
 */
#ifndef SAVE_LAYOUT32_H
#define SAVE_LAYOUT32_H

/* sizeof(void *): the stored width of a pointer member. */
#define SAVE_LAYOUT32_POINTER_SIZE 4u

/* sizeof(struct SaveBlock1), which src/save.c splits into chunks of
 * SECTOR_DATA_SIZE bytes, and the end of its last member (waldaPhrase):
 * the size less the tail padding, none at 32 bits. */
#define SAVE_LAYOUT32_SAVEBLOCK1_SIZE 15496u
#define SAVE_LAYOUT32_SAVEBLOCK1_DATA_END 15496u

/* objectEventTemplates[OBJECT_EVENT_TEMPLATES_COUNT] (include/global.h). */
#define SAVE_LAYOUT32_OBJECT_EVENT_TEMPLATES 3144u
#define SAVE_LAYOUT32_OBJECT_EVENT_TEMPLATE_COUNT 64u
/* struct ObjectEventTemplate (include/global.fieldmap.h, packed): its size
 * and the offsets of script (a pointer) and of flagId, which filler
 * follows to the end. */
#define SAVE_LAYOUT32_OBJECT_EVENT_TEMPLATE_SIZE 24u
#define SAVE_LAYOUT32_TEMPLATE_SCRIPT 16u
#define SAVE_LAYOUT32_TEMPLATE_FLAG_ID 20u

/* flags, the member after the templates; everything from it up to
 * enigmaBerry (vars .. roamer) has no pointer. */
#define SAVE_LAYOUT32_FLAGS 4680u

/* enigmaBerry, a struct EnigmaBerry (include/global.berry.h). */
#define SAVE_LAYOUT32_ENIGMA_BERRY 12720u
#define SAVE_LAYOUT32_ENIGMA_BERRY_SIZE 52u
/* Within it: berry.maxYield, which one byte of bit-fields (minYield,
 * waterBonus) follows; berry.description1 and berry.description2
 * (pointers); berry.growthDuration, the first byte of the run that goes on
 * to holdEffectParam; checksum, a u32 byte sum of everything before it
 * (src/berry.c, GetEnigmaBerryChecksum). */
#define SAVE_LAYOUT32_BERRY_MAX_YIELD 10u
#define SAVE_LAYOUT32_BERRY_DESCRIPTION1 12u
#define SAVE_LAYOUT32_BERRY_DESCRIPTION2 16u
#define SAVE_LAYOUT32_BERRY_GROWTH_DURATION 20u
#define SAVE_LAYOUT32_ENIGMA_CHECKSUM 48u

/* mysteryGift, the member after enigmaBerry; from it to the end of the
 * block there is no pointer. */
#define SAVE_LAYOUT32_MYSTERY_GIFT 12772u

#endif /* SAVE_LAYOUT32_H */
