/*
 * main.c: power-on. Parses the command line, starts the platform modules in
 * dependency order, runs expansion's AgbMain, and shuts everything down in
 * reverse order when the game ends.
 *
 * Stands in for the GBA switching on: the BIOS boot, then src/crt0.s, which
 * reset RAM, copied the initialised data, set the IRQ vector and called
 * AgbMain (crt0.h has what remains of crt0 natively). Design:
 * docs/ARCHITECTURE.md ("The frame", "Modules"), docs/RESEARCH.md "Entry
 * point and loop".
 *
 * AgbMain never returns on hardware. Natively the game ends by a longjmp to
 * main's exit target from one of two places: the frame hook, when frame_run
 * says to stop (a frame limit, the window closed, a failure), and
 * PlatformTestRunnerExit (the test runner's result). SoftReset longjmps to
 * the reset target, after which main does what crt0 does and calls AgbMain
 * again. main is the only place that ends the process, by returning; the
 * one exception is crash_reboot's handler, which, in test-runner mode,
 * replaces a crashed process with a fresh run of the same program.
 *
 * Test-runner mode (--test-runner) makes the process what mgba-rom-test is
 * to expansion's hydra (tools/mgba-rom-test-hydra, run through
 * platform/test_runner/native-rom-test): mGBA's debug registers print to
 * stdout as "GBA Debug: " lines (debug_channel), stdout carries nothing
 * else and is line-buffered, the exit status is the test runner's
 * (PlatformTestRunnerExit: 0 all as expected, 1 a test failed, 2 the debug
 * channel is missing), a crash reboots with the runner's persistent state
 * (crash_reboot, --persistent-state), and nothing is written to disk: the
 * save and the clock's offset stay in memory, so parallel runs share no
 * file.
 */
#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "arm_division.h"
#include "audio.h"
#include "bios.h"
#include "crash_reboot.h"
#include "crt0.h"
#include "debug_channel.h"
#include "dma.h"
#include "flash.h"
#include "frame.h"
#include "frame_pacer.h"
#include "game_hooks.h"
#include "gba_memmap.h"
#include "input.h"
#include "interrupt.h"
#include "ppu.h"
#include "psg.h"
#include "rom_mirror.h"
#include "rtc.h"
#include "save_file.h"
#include "timer.h"
#include "transfer_bounds.h"
#include "video.h"

/* Expansion's entry point and IRQ handler table (src/main.c). */
void AgbMain(void);
extern void (*gIntrTable[])(void);

/* Defaults. */
#define MAIN_DEFAULT_SAVE_PATH "pokeemerald.sav"
#define MAIN_DEFAULT_RTC_OFFSET_PATH "pokeemerald.rtc"
#define MAIN_DEFAULT_SCALE VIDEO_DEFAULT_SCALE
#define MAIN_DEFAULT_FRAME_LIMIT 0u /* none */
#define MAIN_WINDOW_TITLE "pokeemerald-expansion"

/* Exit codes besides the game's own: 0 for a normal end, sysexits.h's
 * EX_USAGE for a bad command line, 1 for a failure. */
#define MAIN_EXIT_OK 0
#define MAIN_EXIT_FAILURE 1
#define MAIN_EXIT_USAGE 64

/* src/crt0.s Init: "mov r0, #255 @ RESET_ALL; svc #1 << 16". */
#define MAIN_CRT0_RESET_FLAGS UINT32_C(0xFF)

/* The value main's frame hook longjmps to the exit target with, distinct
 * from game_hooks' GAME_HOOKS_JUMP_EXIT on the same target. */
#define MAIN_JUMP_FRAME_EXIT (GAME_HOOKS_JUMP_EXIT + 1)

struct options {
    bool headless;
    bool silent;
    bool unthrottled;
    bool help;
    bool test_runner;
    const char *persistent_state; /* the hex the crash reboot passes, or NULL */
    int argc;                     /* main's, for the crash reboot */
    char **argv;
    int scale;
    uint32_t frame_limit;
    const char *save_path;
    const char *rtc_offset_path;
    const char *screenshot_path;
};

static void print_usage(FILE *out)
{
    fprintf(out,
            "usage: pokeemerald-native [options]\n"
            "  --headless         no window (input reads no keys); the frames still run\n"
            "  --silent           no audio device\n"
            "  --save PATH        the save file (default %s)\n"
            "  --rtc-offset PATH  the real-time clock's offset file (default %s)\n"
            "  --scale N          window scale, 1 to %d (default %d)\n"
            "  --unthrottled      do not hold the GBA frame rate\n"
            "  --frames N         exit after N frames (exit status 0)\n"
            "  --screenshot PATH  write the last frame to PATH as a PPM when the game ends\n"
            "  --test-runner      run expansion's test runner as mgba-rom-test does for its hydra:\n"
            "                     debug output on stdout, the runner's exit status, reboot on a\n"
            "                     crash; no save or clock file is read or written\n"
            "  --persistent-state HEX\n"
            "                     (added by the reboot) the runner's state to start from\n"
            "  --help             this text\n",
            MAIN_DEFAULT_SAVE_PATH, MAIN_DEFAULT_RTC_OFFSET_PATH, VIDEO_MAX_SCALE, MAIN_DEFAULT_SCALE);
}

/* main's two jump targets: the end of the game, and SoftReset. */
static jmp_buf exit_target;
static jmp_buf reset_target;

static bool parse_unsigned(const char *text, unsigned long max, unsigned long *out)
{
    char *end;
    if (text == NULL || text[0] < '0' || text[0] > '9')
        return false;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno != 0 || *end != '\0' || value > max)
        return false;
    *out = value;
    return true;
}

/* Returns 0, or MAIN_EXIT_USAGE after printing what is wrong. */
static int parse_options(int argc, char **argv, struct options *out)
{
    *out = (struct options){
        .scale = MAIN_DEFAULT_SCALE,
        .frame_limit = MAIN_DEFAULT_FRAME_LIMIT,
        .save_path = MAIN_DEFAULT_SAVE_PATH,
        .rtc_offset_path = MAIN_DEFAULT_RTC_OFFSET_PATH,
        .argc = argc,
        .argv = argv,
    };
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *value = i + 1 < argc ? argv[i + 1] : NULL;
        unsigned long number;
        if (strcmp(arg, "--headless") == 0) {
            out->headless = true;
        } else if (strcmp(arg, "--silent") == 0) {
            out->silent = true;
        } else if (strcmp(arg, "--unthrottled") == 0) {
            out->unthrottled = true;
        } else if (strcmp(arg, "--help") == 0) {
            out->help = true;
        } else if (strcmp(arg, "--test-runner") == 0) {
            out->test_runner = true;
        } else if (strcmp(arg, CRASH_REBOOT_OPTION) == 0 && value != NULL) {
            out->persistent_state = value;
            i++;
        } else if (strcmp(arg, "--save") == 0 && value != NULL) {
            out->save_path = value;
            i++;
        } else if (strcmp(arg, "--rtc-offset") == 0 && value != NULL) {
            out->rtc_offset_path = value;
            i++;
        } else if (strcmp(arg, "--screenshot") == 0 && value != NULL) {
            out->screenshot_path = value;
            i++;
        } else if (strcmp(arg, "--scale") == 0 && parse_unsigned(value, VIDEO_MAX_SCALE, &number) && number >= 1) {
            out->scale = (int)number;
            i++;
        } else if (strcmp(arg, "--frames") == 0 && parse_unsigned(value, UINT32_MAX, &number) && number >= 1) {
            out->frame_limit = (uint32_t)number;
            i++;
        } else {
            fprintf(stderr, "main: bad argument '%s'%s\n", arg, value == NULL ? "" : " (or its value)");
            print_usage(stderr);
            return MAIN_EXIT_USAGE;
        }
    }
    if (out->persistent_state != NULL && !out->test_runner) {
        fprintf(stderr, "main: %s is for --test-runner only\n", CRASH_REBOOT_OPTION);
        return MAIN_EXIT_USAGE;
    }
    if (out->test_runner) {
        out->save_path = NULL;
        out->rtc_offset_path = NULL;
    }
    return 0;
}

/* --- The modules, in dependency order ------------------------------------ */

static int start_gba_memmap(const struct options *o) { (void)o; return gba_memmap_init(); }
static int start_rom_mirror(const struct options *o) { (void)o; return rom_mirror_init(); }
static int start_transfer_bounds(const struct options *o) { (void)o; return transfer_bounds_init(); }
static int start_interrupt(const struct options *o) { (void)o; return interrupt_init(gIntrTable, INTERRUPT_TABLE_LENGTH); }
static int start_timer(const struct options *o) { (void)o; return timer_init(interrupt_raise); }
static int start_dma(const struct options *o) { (void)o; return dma_init(interrupt_raise); }
static int start_crt0(const struct options *o) { (void)o; return crt0_init(); }
static int start_game_hooks(const struct options *o) { (void)o; return game_hooks_init(&exit_target); }

/* Test-runner mode only: stdout is the hydra's, one line at a time. */
static int start_debug_channel(const struct options *o)
{
    if (!o->test_runner)
        return 0;
    if (setvbuf(stdout, NULL, _IOLBF, 0) != 0) {
        fprintf(stderr, "main: setvbuf(stdout, line-buffered) failed\n");
        return -1;
    }
    return debug_channel_init(stdout);
}

/* Test-runner mode only: put back the state a crash left, then be ready to
 * reboot on the next one. */
static int start_crash_reboot(const struct options *o)
{
    if (!o->test_runner)
        return 0;
    if (o->persistent_state != NULL && crash_reboot_restore(o->persistent_state) != CRASH_REBOOT_OK)
        return -1;
    return crash_reboot_init(o->argc, o->argv);
}

/* After crash_reboot, so that a SIGFPE it cannot complete goes to the
 * crash reboot in test-runner mode. */
static int start_arm_division(const struct options *o) { (void)o; return arm_division_init(); }
static int start_ppu(const struct options *o) { (void)o; return ppu_init(); }
static int start_psg(const struct options *o) { (void)o; return psg_init(FRAME_AUDIO_SAMPLE_RATE_HZ); }
static int start_sdl(const struct options *o) { (void)o; return 0; }
static int start_audio(const struct options *o) { return audio_init(FRAME_AUDIO_SAMPLE_RATE_HZ, o->silent); }
static int start_rtc(const struct options *o) { return rtc_init(NULL, o->rtc_offset_path); }
static int start_frame_pacer(const struct options *o) { return frame_pacer_init(o->unthrottled); }

static void run_frame(void);

static int start_bios(const struct options *o)
{
    (void)o;
    if (bios_init(run_frame) != BIOS_OK)
        return -1;
    bios_set_reset_target(&reset_target);
    return 0;
}

static int start_video(const struct options *o)
{
    struct video_config config = { .headless = o->headless, .scale = o->scale, .title = MAIN_WINDOW_TITLE };
    return video_init(&config);
}

static int start_input(const struct options *o)
{
    /* Headless there is no window to take keys from; the script source
     * reads "no key pressed" until a replay sets it. */
    return input_init(o->headless ? INPUT_SOURCE_SCRIPT : INPUT_SOURCE_HOST);
}

static int start_flash(const struct options *o)
{
    (void)o;
    flash_init();
    return 0;
}

/* Loads the save. A file this build cannot use is never overwritten: main
 * stops, and the player decides what to do with it. */
static int start_save_file(const struct options *o)
{
    if (save_file_init(o->save_path) != SAVE_FILE_OK)
        return -1;
    int status = save_file_load();
    switch (status) {
    case SAVE_FILE_OK:
    case SAVE_FILE_NEW:
        return 0;
    case SAVE_FILE_LEGACY:
        fprintf(stderr, "main: %s has no header (an emulator's .sav or an older build's); loading it\n",
                o->save_path);
        return 0;
    case SAVE_FILE_FOREIGN_LAYOUT:
        fprintf(stderr, "main: %s was written by a build with another save layout, which this build cannot "
                "read yet; not starting, so it is not overwritten\n", o->save_path);
        return -1;
    default:
        fprintf(stderr, "main: %s cannot be used (above); not starting, so it is not overwritten. Move it "
                "away to start without a save\n", o->save_path);
        return -1;
    }
}

static int start_frame(const struct options *o)
{
    struct frame_config config = {
        .frame_limit = o->frame_limit,
        .screenshot_path = o->screenshot_path,
        .skip_drawing = o->headless && o->screenshot_path == NULL, /* nobody sees the frames */
    };
    return frame_init(&config);
}

struct module_step {
    const char *name;
    int (*start)(const struct options *options);
    void (*stop)(void);
};

/* Dependency order (docs/ARCHITECTURE.md "Modules"); stopped in reverse.
 * "sdl" starts nothing (video, audio and input start their own SDL
 * subsystems) and ends SDL after all three are shut down. */
static const struct module_step module_steps[] = {
    { "gba_memmap", start_gba_memmap, gba_memmap_shutdown },
    { "rom_mirror", start_rom_mirror, rom_mirror_shutdown },
    { "debug_channel", start_debug_channel, debug_channel_shutdown },
    { "crash_reboot", start_crash_reboot, crash_reboot_shutdown },
    { "arm_division", start_arm_division, arm_division_shutdown },
    { "transfer_bounds", start_transfer_bounds, transfer_bounds_shutdown },
    { "interrupt", start_interrupt, interrupt_shutdown },
    { "timer", start_timer, timer_shutdown },
    { "dma", start_dma, dma_shutdown },
    { "crt0", start_crt0, crt0_shutdown },
    { "game_hooks", start_game_hooks, game_hooks_shutdown },
    { "bios", start_bios, bios_shutdown },
    { "ppu", start_ppu, ppu_shutdown },
    { "psg", start_psg, psg_shutdown },
    { "sdl", start_sdl, SDL_Quit },
    { "audio", start_audio, audio_shutdown },
    { "video", start_video, video_shutdown },
    { "input", start_input, input_shutdown },
    { "rtc", start_rtc, rtc_shutdown },
    { "flash", start_flash, flash_shutdown },
    { "save_file", start_save_file, save_file_shutdown },
    { "frame", start_frame, frame_shutdown },
    { "frame_pacer", start_frame_pacer, frame_pacer_shutdown },
};
#define MODULE_STEP_COUNT (sizeof module_steps / sizeof module_steps[0])

static void stop_modules(size_t started)
{
    while (started > 0)
        module_steps[--started].stop();
}

/* Starts every module; on a failure stops the ones already started and the
 * one that failed (shutdown is safe on a partially initialised module, and
 * releases what its init acquired before failing). Returns true if all
 * started. */
static bool start_modules(const struct options *options)
{
    for (size_t i = 0; i < MODULE_STEP_COUNT; i++) {
        if (module_steps[i].start(options) != 0) {
            fprintf(stderr, "main: starting %s failed\n", module_steps[i].name);
            stop_modules(i + 1);
            return false;
        }
    }
    return true;
}

/* --- Running the game ---------------------------------------------------- */

/* The BIOS's frame hook (VBlankIntrWait). The jump goes through game_hooks,
 * which first leaves the battle runner's stack if a test body is waiting a
 * frame on it. */
static void run_frame(void)
{
    if (frame_run() == FRAME_EXIT)
        game_hooks_unwind(&exit_target, MAIN_JUMP_FRAME_EXIT);
}

/* What happens between SoftReset and AgbMain on the GBA, in order:
 *  1. crt0's RegisterRamReset(RESET_ALL) clears RAM and the registers
 *     (the BIOS module stores zeroes into the I/O page);
 *  2. crt0 copies the initialised data back; natively crt0_restart also
 *     zeroes the game's variables, which live outside the GBA regions;
 *  3. the modules that keep shadow state of the registers just zeroed start
 *     again from it: interrupt (pending requests, and IF with its marker),
 *     timer (counters and reloads), dma (armed channels), ppu (affine
 *     reference points) and psg (channel state);
 * then run_game re-enters AgbMain, as crt0 does. Never restarted: flash and
 * save_file (a cartridge keeps its save across a reset; flash_init would
 * erase it), rtc (the clock runs on), and video, audio and input (the host
 * side). */
static int restart_after_soft_reset(void)
{
    RegisterRamReset(MAIN_CRT0_RESET_FLAGS);
    crt0_restart();
    interrupt_shutdown();
    timer_shutdown();
    dma_shutdown();
    ppu_shutdown();
    psg_shutdown();
    if (interrupt_init(gIntrTable, INTERRUPT_TABLE_LENGTH) != INTERRUPT_OK || timer_init(interrupt_raise) != TIMER_OK
        || dma_init(interrupt_raise) != DMA_OK || ppu_init() != PPU_OK || psg_init(FRAME_AUDIO_SAMPLE_RATE_HZ) != PSG_OK) {
        fprintf(stderr, "main: restarting the hardware modules after SoftReset failed\n");
        return -1;
    }
    return 0;
}

/* Runs the game until it ends; returns the exit status. setjmp's targets
 * are this function's frame, which stays live while AgbMain runs. */
static int run_game(void)
{
    switch (setjmp(exit_target)) {
    case 0:
        break;
    case GAME_HOOKS_JUMP_EXIT:
        return (int)game_hooks_exit_code();
    case MAIN_JUMP_FRAME_EXIT:
        return frame_exit_status();
    default:
        fprintf(stderr, "main: unknown jump to the exit target\n");
        return MAIN_EXIT_FAILURE;
    }

    if (setjmp(reset_target) == BIOS_RESET_JUMP_VALUE) {
        if (restart_after_soft_reset() != 0)
            return MAIN_EXIT_FAILURE;
    }
    frame_power_on();
    game_hooks_forget_agb_main_loop();
    AgbMain();
    fprintf(stderr, "main: AgbMain returned, which it never does on the GBA\n");
    return MAIN_EXIT_FAILURE;
}

int main(int argc, char **argv)
{
    struct options options;
    int status = parse_options(argc, argv, &options);
    if (status != 0)
        return status;
    if (options.help) {
        print_usage(stdout);
        return MAIN_EXIT_OK;
    }

    if (!start_modules(&options))
        return MAIN_EXIT_FAILURE;

    status = run_game();
    if (frame_flush_save() != 0 && status == MAIN_EXIT_OK)
        status = MAIN_EXIT_FAILURE;
    if (!options.test_runner) /* stdout is the hydra's */
        printf("main: %u frames\n", frame_count());
    stop_modules(MODULE_STEP_COUNT);
    return status;
}
