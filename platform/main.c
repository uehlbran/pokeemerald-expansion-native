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
 * Before any module starts, main finds the player's files (player.h: the
 * data directory, the save, the clock's offset and the settings file, read
 * then) and takes the save's lock (save_lock.h), so that a
 * second instance on the same save stops there, and it releases the lock
 * once every module has stopped, after the last store and before a restart,
 * whose new run finds the same files from the same arguments and takes the
 * lock again as any start does. --keys prints what was found and ends there,
 * and --version before anything is looked for.
 *
 * On Windows the executable is a GUI program, which may have no console to
 * print to; console.h says where its messages go then (the parent's streams,
 * its console, or pokeemerald.log in the data directory), main decides that
 * first of all (in the log mode --version too looks for the data directory
 * first, to open the log), and a failure before the game is running is also
 * shown in a message box when there is no one to read the log.
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
#include "audio_dump.h"
#include "bios.h"
#include "console.h"
#include "crash_reboot.h"
#include "crt0.h"
#include "data_dir.h"
#include "debug_channel.h"
#include "dma.h"
#include "fastforward.h"
#include "fixed_clock.h"
#include "flash.h"
#include "frame.h"
#include "frame_pacer.h"
#include "game_hooks.h"
#include "gba_memmap.h"
#include "host_console.h"
#include "input.h"
#include "interrupt.h"
#include "options.h"
#include "overlay.h"
#include "player.h"
#include "ppu.h"
#include "psg.h"
#include "replay_session.h"
#include "restart.h"
#include "rtc.h"
#include "save_convert.h"
#include "save_file.h"
#include "save_lock.h"
#include "timer.h"
#include "transfer_bounds.h"
#include "version.h"
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

/* Test-runner mode only: stdout is the hydra's, one line at a time. The size
 * is BUFSIZ, not 0, because msvcrt refuses a size below 2; it also buffers a
 * line-buffered stream fully, so on Windows the lines go out whole because
 * debug_channel flushes each one itself. */
static int start_debug_channel(const struct options *o)
{
    if (!o->test_runner)
        return 0;
    if (setvbuf(stdout, NULL, _IOLBF, BUFSIZ) != 0) {
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
/* On the output grid, which frame owns (frame_start_psg). */
static int start_psg(const struct options *o)
{
    (void)o;
    return frame_start_psg();
}
/* main is this program's own, not SDL's SDL_main (SDL_MAIN_HANDLED on
 * Windows, where SDL would otherwise take main), so SDL is told before any of
 * its subsystems starts, as SDL_main.h asks of a program that bypasses
 * SDL_main; on Linux the call only records it. */
static int start_sdl(const struct options *o)
{
    (void)o;
    SDL_SetMainReady();
    return 0;
}
static int start_audio(const struct options *o)
{
    return audio_init(FRAME_AUDIO_SAMPLE_RATE_HZ, o->silent ? AUDIO_SILENT : AUDIO_DEVICE);
}
/* --audio-dump: what the frame loop queues, at the rate the device plays. */
static int start_audio_dump(const struct options *o)
{
    if (o->audio_dump_path == NULL)
        return 0;
    return audio_dump_start(o->audio_dump_path, FRAME_AUDIO_SAMPLE_RATE_HZ);
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
    const char *const offset_path = player_rtc_offset_path();
    fixed_clock_base = o->rtc_fixed_epoch;
    if (o->rtc_fixed) /* no host time zone in a deterministic run */
        return rtc_init_utc(read_fixed_clock, offset_path);
    return rtc_init(NULL, offset_path);
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

/* The help overlay, for a window played from the keyboard only
 * (player_at_keyboard); before video, which draws it into its texture. */
static int start_overlay(const struct options *o)
{
    return player_at_keyboard(o) ? overlay_init(player_write_help) : 0;
}

/* Fast-forward, for the same runs as the overlay, so that no replay, gate
 * or test run is ever paced faster; before video, which draws its
 * indicator. */
static int start_fastforward(const struct options *o)
{
    return player_at_keyboard(o) ? fastforward_init(player_fast_forward()) : 0;
}

static int start_video(const struct options *o)
{
    struct video_config config = {
        .headless = o->headless,
        .scale = player_window_scale(o),
        .title = MAIN_WINDOW_TITLE,
    };
    return video_init(&config);
}

static int start_input(const struct options *o)
{
    /* Headless there is no window to take keys from, and a replay supplies
     * them; the script source reads "no key pressed" until a replay sets it. */
    return input_init(o->headless || o->replay_path != NULL ? INPUT_SOURCE_SCRIPT : INPUT_SOURCE_HOST, player_keymap());
}

static int start_flash(const struct options *o)
{
    (void)o;
    flash_init();
    return 0;
}

/* The cartridge carried over from a build with pointer_bits-bit pointers:
 * the save in the chip is put into this build's struct layout before the
 * game reads it (save_convert.h). Not marked dirty, so the file keeps the
 * other build's layout until the game saves. */
static int adopt_save_layout(const char *path, uint32_t pointer_bits)
{
    const uint32_t native_bits = save_file_native_layout().pointer_bits;
    if (pointer_bits == native_bits)
        return 0;
    const int converted = save_convert_image(flash_image(), pointer_bits, native_bits);
    if (converted < 0) {
        (void)fprintf(stderr,
                      "main: %s has a %u-bit build's save layout, which this build cannot convert; not "
                      "starting, so it is not overwritten\n",
                      path, (unsigned)pointer_bits);
        return -1;
    }
    if (converted == 0) {
        (void)fprintf(stderr,
                      "main: %s has no save slot this build recognises as a %u-bit layout; loading it unconverted "
                      "(the game will validate it)\n",
                      path, (unsigned)pointer_bits);
        return 0;
    }
    (void)fprintf(stderr,
                  "main: %s has a %u-bit build's save layout; converted %d save slot(s) to this build's (the file "
                  "keeps the old layout until the game saves)\n",
                  path, (unsigned)pointer_bits, converted);
    return 0;
}

/* Loads the save. A file this build cannot use is never overwritten: main
 * stops, and the player decides what to do with it. */
static int start_save_file(const struct options *o)
{
    (void)o;
    const char *const path = player_save_path();
    if (save_file_init(path) != SAVE_FILE_OK)
        return -1;
    int status = save_file_load();
    struct save_file_layout layout;
    switch (status) {
    case SAVE_FILE_OK:
    case SAVE_FILE_NEW:
        return 0;
    case SAVE_FILE_FOREIGN_LAYOUT:
        if (save_file_loaded_layout(&layout) && layout.layout_id == SAVE_FILE_LAYOUT_ID)
            return adopt_save_layout(path, layout.pointer_bits);
        (void)fprintf(stderr,
                      "main: %s was written by a build with another save layout, which this build cannot "
                      "read; not starting, so it is not overwritten\n",
                      path);
        return -1;
    default:
        (void)fprintf(stderr,
                      "main: %s cannot be used (above); not starting, so it is not overwritten. Move it "
                      "away to start without a save\n",
                      path);
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
 * "sdl" starts no subsystem (video, audio and input start their own) but
 * tells SDL that main is ready, and ends SDL after all three are shut down. */
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
    {"audio_dump", start_audio_dump, audio_dump_shutdown},
    {"overlay", start_overlay, overlay_shutdown},
    {"fastforward", start_fastforward, fastforward_shutdown},
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

/* The modules a soft reset starts again (restart_after_soft_reset says why),
 * in the same order as above. */
static const struct module_step soft_reset_steps[] = {
    {"interrupt", start_interrupt, interrupt_shutdown},
    {"timer", start_timer, timer_shutdown},
    {"dma", start_dma, dma_shutdown},
    {"ppu", start_ppu, ppu_shutdown},
    {"psg", start_psg, psg_shutdown},
};
#define SOFT_RESET_STEP_COUNT (sizeof soft_reset_steps / sizeof soft_reset_steps[0])

/* Stops the first `started` of steps, in reverse. */
static void stop_steps(const struct module_step *steps, size_t started)
{
    while (started > 0)
        steps[--started].stop();
}

/* Starts the count steps in order; on a failure stops the ones already
 * started and the one that failed (shutdown is safe on a partially
 * initialised module, and releases what its init acquired before failing).
 * Returns true if all started. */
static bool start_steps(const struct module_step *steps, size_t count, const struct options *options)
{
    for (size_t i = 0; i < count; i++) {
        if (steps[i].start(options) != 0) {
            (void)fprintf(stderr, "main: starting %s failed\n", steps[i].name);
            stop_steps(steps, i + 1);
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
 * erase it), rtc (the clock runs on), and video, audio, the audio dump and
 * input (the host side). */
static int restart_after_soft_reset(const struct options *options)
{
    RegisterRamReset(MAIN_CRT0_RESET_FLAGS);
    crt0_restart();
    stop_steps(soft_reset_steps, SOFT_RESET_STEP_COUNT);
    if (!start_steps(soft_reset_steps, SOFT_RESET_STEP_COUNT, options)) {
        (void)fprintf(stderr, "main: restarting the hardware modules after SoftReset failed\n");
        return -1;
    }
    return 0;
}

/* Runs the game until it ends; returns the exit status. setjmp's targets
 * are this function's frame, which stays live while AgbMain runs. */
static int run_game(const struct options *options)
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
        if (restart_after_soft_reset(options) != 0)
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

/* The message box of a start-up failure in LOG mode (console.h); SDL
 * documents it as usable before SDL_Init. */
static void show_message_box(const char *title, const char *text)
{
    if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, text, NULL) != 0)
        (void)fprintf(stderr, "main: SDL_ShowSimpleMessageBox failed: %s\n", SDL_GetError());
}

/* LOG mode: the log is opened as soon as its place is known, which takes the
 * options; a command line that does not parse gets the defaults' place, and
 * is parsed again with the log open, so that the report of what was wrong,
 * lost to the streams of a program with none, is in the log. */
static void begin_log(int argc, char **argv, struct options *options, bool parsed, char *log_path, size_t size)
{
    struct options defaults;
    char *program_only[] = {argv[0], NULL};
    if (!parsed && options_parse(1, program_only, &defaults) != OPTIONS_OK)
        return; /* cannot happen: no argument to refuse */
    const struct console_streams streams = console_standard_streams();
    (void)console_open_log(parsed ? options : &defaults, data_dir_sdl_host(), &streams, log_path, size);
    if (!parsed)
        (void)options_parse(argc, argv, options);
}

/* The program from the command line to its end; *started is set once every
 * module is running, which is when a failure stops being a start-up one. */
static int play(int argc, char **argv, enum console_mode mode, char *log_path, size_t log_path_size, bool *started)
{
    struct options options;
    int status = MAIN_EXIT_OK;
    const bool parsed = options_parse(argc, argv, &options) == OPTIONS_OK;
    if (mode == CONSOLE_LOG)
        begin_log(argc, argv, &options, parsed, log_path, log_path_size);
    if (!parsed)
        return MAIN_EXIT_USAGE;
    if (options.help) {
        options_print_usage(stdout);
        return MAIN_EXIT_OK;
    }
    if (options.version) {
        (void)printf("%s\n", version_string());
        return MAIN_EXIT_OK;
    }
    /* The data directory and the files in it, and the settings file's
     * contents, before the save's lock is taken. */
    if (player_find(&options, data_dir_sdl_host()) != 0)
        return MAIN_EXIT_FAILURE;
    if (options.keys)
        return player_print_keys(stdout) == 0 ? MAIN_EXIT_OK : MAIN_EXIT_FAILURE;

    /* Test-runner mode has no save path: it writes no save and takes no lock. */
    const char *const save_path = player_save_path();
    if (save_path != NULL && save_lock_take(save_path) != SAVE_LOCK_OK)
        return MAIN_EXIT_FAILURE; /* reported */
    if (!start_steps(module_steps, MODULE_STEP_COUNT, &options)) {
        save_lock_release();
        return MAIN_EXIT_FAILURE;
    }
    *started = true;

    status = run_game(&options);
    if (frame_flush_save() != 0 && status == MAIN_EXIT_OK)
        status = MAIN_EXIT_FAILURE;
    if (audio_dump_finish() != 0 && status == MAIN_EXIT_OK)
        status = MAIN_EXIT_FAILURE;
    if (!options.test_runner) /* stdout is the hydra's */
        (void)printf("main: %u frames\n", frame_count());
    bool restart = false;
    if (options.replay_path != NULL)
        status = settle_replay(status, &restart);
    const int64_t restart_epoch = fixed_clock_at(options.rtc_fixed_epoch, frame_count());
    const uint32_t restart_line = replay_session_restart_line();
    stop_steps(module_steps, MODULE_STEP_COUNT);
    /* After every module has stopped, so after the last store
     * (frame_flush_save); no test can see that order, since this is the last
     * thing before the process ends and only an instance started during that
     * final store could tell. Released before the restart, not handed on: on
     * Windows the new run is a child this process waits for, which could never
     * take a lock its parent holds (docs/DECISIONS.md, the amendment to ruling
     * (3)). */
    save_lock_release();
    if (restart && status == MAIN_EXIT_OK)
        return restart_process(argc, argv, restart_epoch, restart_line);
    return status;
}

int main(int argc, char **argv)
{
    const enum console_mode mode = console_choose(host_console_stream_kind(HOST_STREAM_OUT),
                                                  host_console_stream_kind(HOST_STREAM_ERR),
                                                  host_console_attach_parent);
    char log_path[DATA_DIR_PATH_SIZE] = "";
    bool started = false;
    const int status = play(argc, argv, mode, log_path, sizeof log_path, &started);
    if (!started && status != MAIN_EXIT_OK)
        console_report_start_failure(mode, log_path, show_message_box);
    return status;
}
