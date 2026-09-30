// trev_wake.h - the wheel wake policy, pure (no LVGL, no IDF) so a host test can run it.
//
// The first input on a dark screen only wakes it (E6): the backlight does not come back until
// the next housekeeping tick, so a turn delivered now would navigate a face nobody can see.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { TREV_WHEEL_TURN, TREV_WHEEL_WAKE } trev_wheel_act_t;

// dark_ms 0 = never dark, so a board that never dims always turns.
static inline trev_wheel_act_t trev_wheel_action(uint32_t idle_ms, uint32_t dark_ms)
{
    return (dark_ms && idle_ms > dark_ms) ? TREV_WHEEL_WAKE : TREV_WHEEL_TURN;
}

// Sign of the detent, negated when the wheel is mounted the other way round.
static inline int trev_wheel_dir(int detent, bool invert)
{
    int d = (detent > 0) - (detent < 0);
    return invert ? -d : d;
}
