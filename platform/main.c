/*
 * main.c: power-on. Starts the platform modules in
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
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <SDL.h>

#include "arm_division.h"
#include "audio.h"
#include "bios.h"
#include "crash_reboot.h"
#include "crt0.h"
#include "debug_channel.h"
#include "dma.h"
#include "fixed_clock.h"
#include "flash.h"
#include "frame.h"
#include "frame_pacer.h"
#include "game_hooks.h"
#include "gba_memmap.h"
#include "input.h"
#include "interrupt.h"
#include "options.h"
#include "ppu.h"
#include "psg.h"
#include "replay_session.h"
#include "restart.h"
#include "rtc.h"
#include "save_file.h"
#include "timer.h"
#include "transfer_bounds.h"
#include "video.h"

/* Expansion's entry point and IRQ handler table (src/main.c). */
void AgbMain(void);
extern void (*gIntrTable[])(void);

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

/* main's two jump targets: the end of the game, and SoftReset. */
static jmp_buf exit_target;
static jmp_buf reset_target;

/* --- The modules, in dependency order ------------------------------------ */

static int start_gba_memmap(const struct options *o)
{
    (void)o;
    return gba_memmap_init();
}
static int start_transfer_bounds(const struct options *o)
{
    (void)o;
    return transfer_bounds_init();
}
static int start_interrupt(const struct options *o)
{
    (void)o;
    return interrupt_init(gIntrTable, INTERRUPT_TABLE_LENGTH);
}
static int start_timer(const struct options *o)
{
    (void)o;
    return timer_init(interrupt_raise);
}
static int start_dma(const struct options *o)
{
    (void)o;
    return dma_init(interrupt_raise);
}
static int start_crt0(const struct options *o)
{
    (void)o;
    return crt0_init();
}
static int start_game_hooks(const struct options *o)
{
    (void)o;
    return game_hooks_init(&exit_target);
}

/* Test-runner mode only: stdout is the hydra's, one line at a time. */
static int start_debug_channel(const struct options *o)
{
    if (!o->test_runner)
        return 0;
    if (setvbuf(stdout, NULL, _IOLBF, 0) != 0) {
        (void)fprintf(stderr, "main: setvbuf(stdout, line-buffered) failed\n");
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
static int start_arm_division(const struct options *o)
{
    (void)o;
    return arm_division_init();
}
static int start_ppu(const struct options *o)
{
    (void)o;
    return ppu_init();
}
static int start_psg(const struct options *o)
{
    (void)o;
    return psg_init(FRAME_AUDIO_SAMPLE_RATE_HZ);
}
static int start_sdl(const struct options *o)
{
    (void)o;
    return 0;
}
static int start_audio(const struct options *o)
{
    return audio_init(FRAME_AUDIO_SAMPLE_RATE_HZ, o->silent);
}

/* --rtc-fixed: the clock's base time (fixed_clock.h); the source reads the
 * frame count, so it is the same on every run of the same script. */
static int64_t fixed_clock_base;

static int64_t read_fixed_clock(void)
{
    return fixed_clock_at(fixed_clock_base, frame_count());
}

static int start_rtc(const struct options *o)
{
    fixed_clock_base = o->rtc_fixed_epoch;
    if (o->rtc_fixed) /* no host time zone in a deterministic run */
        return rtc_init_utc(read_fixed_clock, o->rtc_offset_path);
    return rtc_init(NULL, o->rtc_offset_path);
}
static int start_frame_pacer(const struct options *o)
{
    return frame_pacer_init(frame_pacer_mode_for(o->unthrottled));
}

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
    struct video_config config = {.headless = o->headless, .scale = o->scale, .title = MAIN_WINDOW_TITLE};
    return video_init(&config);
}

static int start_input(const struct options *o)
{
    /* Headless there is no window to take keys from, and a replay supplies
     * them; the script source reads "no key pressed" until a replay sets it. */
    return input_init(o->headless || o->replay_path != NULL ? INPUT_SOURCE_SCRIPT : INPUT_SOURCE_HOST);
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
        (void)fprintf(stderr, "main: %s has no header (an emulator's .sav or an older build's); loading it\n",
                      o->save_path);
        return 0;
    case SAVE_FILE_FOREIGN_LAYOUT:
        (void)fprintf(stderr,
                      "main: %s was written by a build with another save layout, which this build cannot "
                      "read yet; not starting, so it is not overwritten\n",
                      o->save_path);
        return -1;
    default:
        (void)fprintf(stderr,
                      "main: %s cannot be used (above); not starting, so it is not overwritten. Move it "
                      "away to start without a save\n",
                      o->save_path);
        return -1;
    }
}

static int start_frame(const struct options *o)
{
    struct frame_config config = {
        .frame_limit = o->frame_limit,
        .screenshot_path = o->screenshot_path,
        /* Only the test runner leaves frames undrawn: nobody sees them there.
         * A replay's screenshot commands and a final screenshot need every
         * frame drawn. */
        .skip_drawing = o->test_runner && o->replay_path == NULL && o->screenshot_path == NULL,
    };
    return frame_init(&config);
}

static int start_replay(const struct options *o)
{
    if (o->replay_path == NULL)
        return 0;
    return replay_session_start(o->replay_path, o->out_dir, o->resume_after_line);
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
    {"gba_memmap", start_gba_memmap, gba_memmap_shutdown},
    {"debug_channel", start_debug_channel, debug_channel_shutdown},
    {"crash_reboot", start_crash_reboot, crash_reboot_shutdown},
    {"arm_division", start_arm_division, arm_division_shutdown},
    {"transfer_bounds", start_transfer_bounds, transfer_bounds_shutdown},
    {"interrupt", start_interrupt, interrupt_shutdown},
    {"timer", start_timer, timer_shutdown},
    {"dma", start_dma, dma_shutdown},
    {"crt0", start_crt0, crt0_shutdown},
    {"game_hooks", start_game_hooks, game_hooks_shutdown},
    {"bios", start_bios, bios_shutdown},
    {"ppu", start_ppu, ppu_shutdown},
    {"psg", start_psg, psg_shutdown},
    {"sdl", start_sdl, SDL_Quit},
    {"audio", start_audio, audio_shutdown},
    {"video", start_video, video_shutdown},
    {"input", start_input, input_shutdown},
    {"rtc", start_rtc, rtc_shutdown},
    {"flash", start_flash, flash_shutdown},
    {"save_file", start_save_file, save_file_shutdown},
    {"frame", start_frame, frame_shutdown},
    {"frame_pacer", start_frame_pacer, frame_pacer_shutdown},
    {"replay", start_replay, replay_session_stop},
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
            (void)fprintf(stderr, "main: starting %s failed\n", module_steps[i].name);
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
        || dma_init(interrupt_raise) != DMA_OK || ppu_init() != PPU_OK
        || psg_init(FRAME_AUDIO_SAMPLE_RATE_HZ) != PSG_OK) {
        (void)fprintf(stderr, "main: restarting the hardware modules after SoftReset failed\n");
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
        (void)fprintf(stderr, "main: unknown jump to the exit target\n");
        return MAIN_EXIT_FAILURE;
    }

    if (setjmp(reset_target) == BIOS_RESET_JUMP_VALUE) {
        if (restart_after_soft_reset() != 0)
            return MAIN_EXIT_FAILURE;
    }
    frame_power_on();
    game_hooks_forget_agb_main_loop();
    AgbMain();
    (void)fprintf(stderr, "main: AgbMain returned, which it never does on the GBA\n");
    return MAIN_EXIT_FAILURE;
}

/* How the replay ended, folded into the exit status. Sets *restart when the
 * script asked for a restart. */
static int settle_replay(int status, bool *restart)
{
    switch (replay_session_status()) {
    case REPLAY_DONE:
        return status;
    case REPLAY_RESTART:
        *restart = true;
        return status;
    case REPLAY_FAILED:
        (void)fprintf(stderr, "replay: FAILED: %s\n", replay_session_message());
        return MAIN_EXIT_FAILURE;
    case REPLAY_RUNNING:
    default:
        if (status == MAIN_EXIT_OK)
            (void)fprintf(stderr, "replay: FAILED: the game ended before the script did\n");
        return MAIN_EXIT_FAILURE;
    }
}

/* The cold restart (restart.h), after everything is shut down: this does not
 * return unless the exec failed. */
static int restart_process(int argc, char **argv, int64_t epoch, uint32_t line)
{
    struct restart_args args;
    if (restart_build_args(argc, argv, line, epoch, &args) == 0)
        (void)restart_exec(&args); /* reports its failure */
    return MAIN_EXIT_FAILURE;
}

int main(int argc, char **argv)
{
    struct options options;
    int status = MAIN_EXIT_OK;
    if (options_parse(argc, argv, &options) != OPTIONS_OK)
        return MAIN_EXIT_USAGE;
    if (options.help) {
        options_print_usage(stdout);
        return MAIN_EXIT_OK;
    }

    if (!start_modules(&options))
        return MAIN_EXIT_FAILURE;

    status = run_game();
    if (frame_flush_save() != 0 && status == MAIN_EXIT_OK)
        status = MAIN_EXIT_FAILURE;
    if (!options.test_runner) /* stdout is the hydra's */
        printf("main: %u frames\n", frame_count());
    bool restart = false;
    if (options.replay_path != NULL)
        status = settle_replay(status, &restart);
    const int64_t restart_epoch = fixed_clock_at(options.rtc_fixed_epoch, frame_count());
    const uint32_t restart_line = replay_session_restart_line();
    stop_modules(MODULE_STEP_COUNT);
    if (restart && status == MAIN_EXIT_OK)
        return restart_process(argc, argv, restart_epoch, restart_line);
    return status;
}
