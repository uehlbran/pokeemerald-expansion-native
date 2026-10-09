/*
 * data_layout_probe.c: prints the C layout of the structs the assembled
 * data holds, for check_data_layout.py.
 *
 * Stands in for nothing on the GBA: it is build glue. The assembler macros
 * and asm_filter.py write these structs byte by byte; at 64 bits their
 * pointers are 8 bytes and the compiler adds padding. Makefile.native
 * compiles this file with the game's flags at 32 and at 64 bits and runs
 * it; the checker compares the objects' bytes with what it prints, so the
 * layouts come from the compiler and are written down nowhere else.
 *
 * Output, one line each:
 *   struct NAME SIZE
 *   member NAME FIELD OFFSET SIZE KIND
 * KIND is "pointer" (a pointer, or a union holding one), "value", or
 * "pointers" (a trailing pointer array; its length is in the data). The
 * words are the ones check_data_layout.py reads (LAYOUT_STRUCT,
 * LAYOUT_MEMBER, KIND_*); change them together.
 */
#include <stddef.h>
#include <stdio.h>

#include "global.h"
#include "global.fieldmap.h"
#include "gba/m4a_internal.h"

#define LAYOUT_STRUCT "struct"
#define LAYOUT_MEMBER "member"
#define KIND_VALUE "value"
#define KIND_POINTER "pointer"
#define KIND_POINTERS "pointers"

#define STRUCT(type) (void)printf(LAYOUT_STRUCT " %s %zu\n", #type, sizeof(struct type))
#define FIELD(type, field, kind)                                                                   \
    (void)printf(LAYOUT_MEMBER " %s %s %zu %zu %s\n", #type, #field, offsetof(struct type, field), \
                 sizeof(((struct type *)NULL)->field), kind)

/* struct MapHeader's flag bits share the byte after nightMusic; offsetof
 * cannot name a bitfield. */
#define MAP_HEADER_FLAGS_OFFSET (offsetof(struct MapHeader, nightMusic) + sizeof(u16))
_Static_assert(offsetof(struct MapHeader, battleType) == MAP_HEADER_FLAGS_OFFSET + 1,
               "struct MapHeader: one byte of flags between nightMusic and battleType");

static void sound_structs(void)
{
    STRUCT(ToneData);
    FIELD(ToneData, type, KIND_VALUE);
    FIELD(ToneData, key, KIND_VALUE);
    FIELD(ToneData, length, KIND_VALUE);
    FIELD(ToneData, pan_sweep, KIND_VALUE);
    FIELD(ToneData, wav, KIND_POINTER);
    FIELD(ToneData, attack, KIND_VALUE);
    FIELD(ToneData, decay, KIND_VALUE);
    FIELD(ToneData, sustain, KIND_VALUE);
    FIELD(ToneData, release, KIND_VALUE);
    STRUCT(Song);
    FIELD(Song, header, KIND_POINTER);
    FIELD(Song, ms, KIND_VALUE);
    FIELD(Song, me, KIND_VALUE);
    STRUCT(MusicPlayer);
    FIELD(MusicPlayer, info, KIND_POINTER);
    FIELD(MusicPlayer, track, KIND_POINTER);
    FIELD(MusicPlayer, numTracks, KIND_VALUE);
    FIELD(MusicPlayer, unk_A, KIND_VALUE);
    STRUCT(SongHeader);
    FIELD(SongHeader, trackCount, KIND_VALUE);
    FIELD(SongHeader, blockCount, KIND_VALUE);
    FIELD(SongHeader, priority, KIND_VALUE);
    FIELD(SongHeader, reverb, KIND_VALUE);
    FIELD(SongHeader, tone, KIND_POINTER);
    (void)printf(LAYOUT_MEMBER " SongHeader part %zu %zu " KIND_POINTERS "\n", offsetof(struct SongHeader, part),
                 sizeof(u8 *));
}

static void map_structs(void)
{
    STRUCT(MapHeader);
    FIELD(MapHeader, mapLayout, KIND_POINTER);
    FIELD(MapHeader, events, KIND_POINTER);
    FIELD(MapHeader, mapScripts, KIND_POINTER);
    FIELD(MapHeader, connections, KIND_POINTER);
    FIELD(MapHeader, music, KIND_VALUE);
    FIELD(MapHeader, mapLayoutId, KIND_VALUE);
    FIELD(MapHeader, regionMapSectionId, KIND_VALUE);
    FIELD(MapHeader, weather, KIND_VALUE);
    FIELD(MapHeader, mapType, KIND_VALUE);
    FIELD(MapHeader, floorNumber, KIND_VALUE);
    FIELD(MapHeader, nightMusic, KIND_VALUE);
    (void)printf(LAYOUT_MEMBER " MapHeader flags %zu 1 " KIND_VALUE "\n", MAP_HEADER_FLAGS_OFFSET);
    FIELD(MapHeader, battleType, KIND_VALUE);
    STRUCT(MapLayout);
    FIELD(MapLayout, width, KIND_VALUE);
    FIELD(MapLayout, height, KIND_VALUE);
    FIELD(MapLayout, border, KIND_POINTER);
    FIELD(MapLayout, map, KIND_POINTER);
    FIELD(MapLayout, primaryTileset, KIND_POINTER);
    FIELD(MapLayout, secondaryTileset, KIND_POINTER);
    FIELD(MapLayout, isFrlg, KIND_VALUE);
    FIELD(MapLayout, borderWidth, KIND_VALUE);
    FIELD(MapLayout, borderHeight, KIND_VALUE);
    FIELD(MapLayout, padding, KIND_VALUE);
    STRUCT(MapConnections);
    FIELD(MapConnections, count, KIND_VALUE);
    FIELD(MapConnections, connections, KIND_POINTER);
    STRUCT(MapEvents);
    FIELD(MapEvents, objectEventCount, KIND_VALUE);
    FIELD(MapEvents, warpCount, KIND_VALUE);
    FIELD(MapEvents, coordEventCount, KIND_VALUE);
    FIELD(MapEvents, bgEventCount, KIND_VALUE);
    FIELD(MapEvents, objectEvents, KIND_POINTER);
    FIELD(MapEvents, warps, KIND_POINTER);
    FIELD(MapEvents, coordEvents, KIND_POINTER);
    FIELD(MapEvents, bgEvents, KIND_POINTER);
    STRUCT(ObjectEventTemplate);
    FIELD(ObjectEventTemplate, localId, KIND_VALUE);
    FIELD(ObjectEventTemplate, graphicsId, KIND_VALUE);
    FIELD(ObjectEventTemplate, kind, KIND_VALUE);
    FIELD(ObjectEventTemplate, x, KIND_VALUE);
    FIELD(ObjectEventTemplate, y, KIND_VALUE);
    /* the anonymous union of the normal and the clone fields, bytes 8..15 */
    (void)printf(LAYOUT_MEMBER " ObjectEventTemplate fields %zu %zu " KIND_VALUE "\n",
                 offsetof(struct ObjectEventTemplate, elevation),
                 offsetof(struct ObjectEventTemplate, script) - offsetof(struct ObjectEventTemplate, elevation));
    FIELD(ObjectEventTemplate, script, KIND_POINTER);
    FIELD(ObjectEventTemplate, flagId, KIND_VALUE);
    FIELD(ObjectEventTemplate, filler, KIND_VALUE);
    STRUCT(CoordEvent);
    FIELD(CoordEvent, x, KIND_VALUE);
    FIELD(CoordEvent, y, KIND_VALUE);
    FIELD(CoordEvent, elevation, KIND_VALUE);
    FIELD(CoordEvent, trigger, KIND_VALUE);
    FIELD(CoordEvent, index, KIND_VALUE);
    FIELD(CoordEvent, script, KIND_POINTER);
    STRUCT(BgEvent);
    FIELD(BgEvent, x, KIND_VALUE);
    FIELD(BgEvent, y, KIND_VALUE);
    FIELD(BgEvent, elevation, KIND_VALUE);
    FIELD(BgEvent, kind, KIND_VALUE);
    FIELD(BgEvent, bgUnion, KIND_POINTER);
}

int main(void)
{
    sound_structs();
    map_structs();
    return 0;
}
