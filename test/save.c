#include "global.h"
#include "pokemon_storage_system.h"
#include "test/test.h"

// If you would like to ensure save compatibility, update the values below with those for your hack. You can find these through the debug menu.
// Please note that this simple check is not 100% foolproof, but should be able to catch most unintended shifts.
#if PLATFORM_NATIVE
// The native build lays structs out by the host's ABI, not the GBA's
// -mabi=apcs-gnu, which rounds every struct to 4 bytes (docs/DECISIONS.md,
// "Native structs follow the host ABI"); these are the native sizes, so the
// test guards the native save layout as the values above it guard a hack's.
// At 64 bits SaveBlock1 is larger (two of its members hold pointers) and
// the other three are the same. The test's purpose, that a save written
// before stays loadable, is met there by the converter, which reads a
// 32-bit build's save (platform/save_convert.h, gate h).
#if __SIZEOF_POINTER__ != 8
#line 13
#define T_SAVEBLOCK1_SIZE 15496
#else
#define T_SAVEBLOCK1_SIZE 15768
#endif
#line 14
#define T_SAVEBLOCK2_SIZE 3848
#define T_SAVEBLOCK3_SIZE 1
#define T_POKEMONSTORAGE_SIZE 34144
#else
#line 7
#define T_SAVEBLOCK1_SIZE 15568
#define T_SAVEBLOCK2_SIZE 3884
#define T_SAVEBLOCK3_SIZE 4
#define T_POKEMONSTORAGE_SIZE 34144
#endif
#line 11

TEST("SaveBlock1 is backwards compatible")
{
    EXPECT_EQ(sizeof(struct SaveBlock1), T_SAVEBLOCK1_SIZE);
}

TEST("SaveBlock2 is backwards compatible")
{
    EXPECT_EQ(sizeof(struct SaveBlock2), T_SAVEBLOCK2_SIZE);
}

TEST("SaveBlock3 is backwards compatible")
{
    EXPECT_EQ(sizeof(struct SaveBlock3), T_SAVEBLOCK3_SIZE);
}

TEST("PokemonStorage is backwards compatible")
{
    EXPECT_EQ(sizeof(struct PokemonStorage), T_POKEMONSTORAGE_SIZE);
}

#undef T_SAVEBLOCK1_SIZE
#undef T_SAVEBLOCK2_SIZE
#undef T_SAVEBLOCK3_SIZE
#undef T_POKEMONSTORAGE_SIZE
