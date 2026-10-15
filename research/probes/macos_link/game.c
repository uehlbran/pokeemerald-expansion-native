/*
 * game.c: probe M2's stand-in for a game object, compiled as the macOS port
 * plans to compile expansion's C: the pragma first (the C filter's first line),
 * then the section spellings the filter gives expansion's markers.
 */
#pragma clang section bss = "__DATA,__game_bss,zerofill" data = "__DATA,__game_data"
#include <stdint.h>

#include "probe_test.h"

#define SECTION_TESTS __attribute__((section("__DATA,__tests"), no_sanitize("address"), used))
#define EWRAM_INIT_SECTION __attribute__((section("__GBA_EWRAM,__ewram_init"), no_sanitize("address")))
#define EWRAM_DATA_SECTION __attribute__((section("__GBA_EWRAM,__ewram_data,zerofill"), no_sanitize("address")))
#define PERSISTENT_SECTION __attribute__((section("__GBA_IWRAM,__persistent,zerofill"), no_sanitize("address")))
#define COMMON_SECTION __attribute__((section("__DATA,__game_common,zerofill"), no_sanitize("address")))

void CB2_Probe(void);
void CB2_Probe(void)
{
}

static void run_a(void)
{
}

SECTION_TESTS static const struct ProbeTest sTestA = {"first", "game.c", run_a, 1};
SECTION_TESTS static const struct ProbeTest sTestB = {"second", "game.c", run_a, 2};
SECTION_TESTS static const struct ProbeTest sTestC = {"third", "game.c", run_a, 3};

EWRAM_INIT_SECTION uint8_t gEwramInit = 0x5A;
EWRAM_DATA_SECTION uint8_t gEwramData[0x30000];
PERSISTENT_SECTION uint8_t gPersistent[0x100];
COMMON_SECTION uint32_t gCommon[16];
uint32_t gGameBss[16];
uint32_t gGameData = 0x1234;
static uint32_t sGameStaticBss;

uint32_t *ProbeGameStaticBss(void);
uint32_t *ProbeGameStaticBss(void)
{
    static uint32_t local;
    local++;
    sGameStaticBss += local;
    return &sGameStaticBss;
}
