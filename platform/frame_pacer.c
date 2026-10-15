/*
 * frame_pacer.c: holds the main loop to the GBA frame rate.
 *
 * Real hardware: the LCD controller runs a frame every 280896 CPU cycles,
 * 59.7275 Hz (GBATEK "LCD Dimensions and Timings"). Here: sleep with
 * SDL_Delay for the bulk of the wait and spin on the performance counter for
 * the last SPIN_MARGIN_MS, because SDL_Delay may oversleep by a millisecond
 * or more. Deadlines are exact ticks via a carried remainder (see header).
 * Fast-forward divides the period by its multiplier and nothing else: the
 * frames the game runs are the same, closer together.
 *
 * The reference ports fix the rate at 60 Hz and let their accumulator grow
 * without bound after a stall; both are avoided here (docs/research/).
 */
#include "frame_pacer.h"

#include <stdbool.h>
#include <stdio.h>

#include <SDL.h>

#define MODULE "frame_pacer"
#define MS_PER_SECOND UINT64_C(1000)
/* Sleep until this close to the deadline, then spin. */
#define SPIN_MARGIN_MS UINT64_C(2)

static struct {
    bool initialised;
    bool unthrottled;
    uint32_t multiplier;
    uint64_t counter_hz;
    uint64_t deadline;
    uint32_t remainder;
} state = {.multiplier = FRAME_PACER_NORMAL_SPEED};

/* The remainder's unit is 1/(GBA_CPU_HZ * multiplier) tick, so it stays below
 * that product, which must fit its uint32_t. */
_Static_assert((uint64_t)GBA_CPU_HZ *FRAME_PACER_MAX_MULTIPLIER <= UINT32_MAX, "the remainder fits 32 bits");

uint64_t frame_pacer_next_deadline(uint64_t prev_deadline, uint64_t period, uint64_t now, uint64_t max_stall)
{
    uint64_t next = prev_deadline + period;
    if (now > next + max_stall)
        return now + period;
    return next;
}

uint64_t frame_pacer_period_ticks(uint64_t counter_hz, uint32_t multiplier, uint32_t *remainder)
{
    const uint64_t divisor = (uint64_t)GBA_CPU_HZ * multiplier;
    uint64_t scaled = counter_hz * (uint64_t)GBA_CYCLES_PER_FRAME + *remainder;
    *remainder = (uint32_t)(scaled % divisor);
    return scaled / divisor;
}

int frame_pacer_set_multiplier(uint32_t multiplier)
{
    if (multiplier > FRAME_PACER_MAX_MULTIPLIER) {
        (void)fprintf(stderr, MODULE ": frame_pacer_set_multiplier(%lu): above %u\n", (unsigned long)multiplier,
                      FRAME_PACER_MAX_MULTIPLIER);
        return -1;
    }
    if (multiplier == state.multiplier)
        return 0;
    state.multiplier = multiplier;
    state.remainder = 0;
    state.deadline = SDL_GetPerformanceCounter();
    return 0;
}

uint32_t frame_pacer_multiplier(void)
{
    return state.multiplier;
}

enum frame_pacer_mode frame_pacer_mode_for(bool unthrottled)
{
    return unthrottled ? FRAME_PACER_UNTHROTTLED : FRAME_PACER_REALTIME;
}

int frame_pacer_init(enum frame_pacer_mode mode)
{
    uint64_t hz = SDL_GetPerformanceFrequency();
    if (hz == 0u || hz > FRAME_PACER_MAX_COUNTER_HZ) {
        (void)fprintf(stderr, MODULE ": SDL_GetPerformanceFrequency returned %llu, outside 1..%llu\n",
                      (unsigned long long)hz, (unsigned long long)FRAME_PACER_MAX_COUNTER_HZ);
        return -1;
    }
    state.counter_hz = hz;
    state.unthrottled = mode == FRAME_PACER_UNTHROTTLED;
    state.multiplier = FRAME_PACER_NORMAL_SPEED;
    state.remainder = 0;
    state.deadline = SDL_GetPerformanceCounter();
    state.initialised = true;
    return 0;
}

void frame_pacer_wait(void)
{
    if (!state.initialised || state.unthrottled || state.multiplier == FRAME_PACER_UNLIMITED)
        return;
    uint64_t period = frame_pacer_period_ticks(state.counter_hz, state.multiplier, &state.remainder);
    uint64_t max_stall = state.counter_hz * FRAME_PACER_MAX_STALL_MS / MS_PER_SECOND;
    state.deadline = frame_pacer_next_deadline(state.deadline, period, SDL_GetPerformanceCounter(), max_stall);
    uint64_t spin_ticks = state.counter_hz * SPIN_MARGIN_MS / MS_PER_SECOND;
    for (;;) {
        uint64_t now = SDL_GetPerformanceCounter();
        if (now >= state.deadline)
            return;
        uint64_t left = state.deadline - now;
        if (left > spin_ticks)
            SDL_Delay((Uint32)((left - spin_ticks) * MS_PER_SECOND / state.counter_hz));
    }
}

void frame_pacer_shutdown(void)
{
    state.initialised = false;
    state.unthrottled = false;
    state.multiplier = FRAME_PACER_NORMAL_SPEED;
}
