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
 * "pointers" (a trailing pointer array; its length is in the data).
 */
#include <stddef.h>
#include <stdio.h>

#include "global.h"
#include "global.fieldmap.h"
#include "gba/m4a_internal.h"

#define STRUCT(type) (void)printf("struct %s %zu\n", #type, sizeof(struct type))
#define FIELD(type, field, kind)                                                           \
    (void)printf("member %s %s %zu %zu %s\n", #type, #field, offsetof(struct type, field), \
                 sizeof(((struct type *)NULL)->field), kind)

/* struct MapHeader's flag bits share the byte after nightMusic; offsetof
 * cannot name a bitfield. */
#define MAP_HEADER_FLAGS_OFFSET (offsetof(struct MapHeader, nightMusic) + sizeof(u16))
_Static_assert(offsetof(struct MapHeader, battleType) == MAP_HEADER_FLAGS_OFFSET + 1,
               "struct MapHeader: one byte of flags between nightMusic and battleType");

static void sound_structs(void)
{
    STRUCT(ToneData);
    FIELD(ToneData, type, "value");
    FIELD(ToneData, key, "value");
    FIELD(ToneData, length, "value");
    FIELD(ToneData, pan_sweep, "value");
    FIELD(ToneData, wav, "pointer");
    FIELD(ToneData, attack, "value");
    FIELD(ToneData, decay, "value");
    FIELD(ToneData, sustain, "value");
    FIELD(ToneData, release, "value");
    STRUCT(Song);
    FIELD(Song, header, "pointer");
    FIELD(Song, ms, "value");
    FIELD(Song, me, "value");
    STRUCT(MusicPlayer);
    FIELD(MusicPlayer, info, "pointer");
    FIELD(MusicPlayer, track, "pointer");
    FIELD(MusicPlayer, numTracks, "value");
    FIELD(MusicPlayer, unk_A, "value");
    STRUCT(SongHeader);
    FIELD(SongHeader, trackCount, "value");
    FIELD(SongHeader, blockCount, "value");
    FIELD(SongHeader, priority, "value");
    FIELD(SongHeader, reverb, "value");
    FIELD(SongHeader, tone, "pointer");
    (void)printf("member SongHeader part %zu %zu pointers\n", offsetof(struct SongHeader, part), sizeof(u8 *));
}

static void map_structs(void)
{
    STRUCT(MapHeader);
    FIELD(MapHeader, mapLayout, "pointer");
    FIELD(MapHeader, events, "pointer");
    FIELD(MapHeader, mapScripts, "pointer");
    FIELD(MapHeader, connections, "pointer");
    FIELD(MapHeader, music, "value");
    FIELD(MapHeader, mapLayoutId, "value");
    FIELD(MapHeader, regionMapSectionId, "value");
    FIELD(MapHeader, weather, "value");
    FIELD(MapHeader, mapType, "value");
    FIELD(MapHeader, floorNumber, "value");
    FIELD(MapHeader, nightMusic, "value");
    (void)printf("member MapHeader flags %zu 1 value\n", MAP_HEADER_FLAGS_OFFSET);
    FIELD(MapHeader, battleType, "value");
    STRUCT(MapLayout);
    FIELD(MapLayout, width, "value");
    FIELD(MapLayout, height, "value");
    FIELD(MapLayout, border, "pointer");
    FIELD(MapLayout, map, "pointer");
    FIELD(MapLayout, primaryTileset, "pointer");
    FIELD(MapLayout, secondaryTileset, "pointer");
    FIELD(MapLayout, isFrlg, "value");
    FIELD(MapLayout, borderWidth, "value");
    FIELD(MapLayout, borderHeight, "value");
    FIELD(MapLayout, padding, "value");
    STRUCT(MapConnections);
    FIELD(MapConnections, count, "value");
    FIELD(MapConnections, connections, "pointer");
    STRUCT(MapEvents);
    FIELD(MapEvents, objectEventCount, "value");
    FIELD(MapEvents, warpCount, "value");
    FIELD(MapEvents, coordEventCount, "value");
    FIELD(MapEvents, bgEventCount, "value");
    FIELD(MapEvents, objectEvents, "pointer");
    FIELD(MapEvents, warps, "pointer");
    FIELD(MapEvents, coordEvents, "pointer");
    FIELD(MapEvents, bgEvents, "pointer");
    STRUCT(ObjectEventTemplate);
    FIELD(ObjectEventTemplate, localId, "value");
    FIELD(ObjectEventTemplate, graphicsId, "value");
    FIELD(ObjectEventTemplate, kind, "value");
    FIELD(ObjectEventTemplate, x, "value");
    FIELD(ObjectEventTemplate, y, "value");
    /* the anonymous union of the normal and the clone fields, bytes 8..15 */
    (void)printf("member ObjectEventTemplate fields %zu %zu value\n", offsetof(struct ObjectEventTemplate, elevation),
                 offsetof(struct ObjectEventTemplate, script) - offsetof(struct ObjectEventTemplate, elevation));
    FIELD(ObjectEventTemplate, script, "pointer");
    FIELD(ObjectEventTemplate, flagId, "value");
    FIELD(ObjectEventTemplate, filler, "value");
    STRUCT(CoordEvent);
    FIELD(CoordEvent, x, "value");
    FIELD(CoordEvent, y, "value");
    FIELD(CoordEvent, elevation, "value");
    FIELD(CoordEvent, trigger, "value");
    FIELD(CoordEvent, index, "value");
    FIELD(CoordEvent, script, "pointer");
    STRUCT(BgEvent);
    FIELD(BgEvent, x, "value");
    FIELD(BgEvent, y, "value");
    FIELD(BgEvent, elevation, "value");
    FIELD(BgEvent, kind, "value");
    FIELD(BgEvent, bgUnion, "pointer");
}

int main(void)
{
    sound_structs();
    map_structs();
    return 0;
}
