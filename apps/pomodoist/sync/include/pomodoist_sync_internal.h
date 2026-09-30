// pomodoist_sync_internal.h — shared between the transport implementations.
// Both transport A (serial) and transport B (wifi) publish a parsed tasklist through
// the same slot, which the app drains via pomodoist_sync_take().
#pragma once
#include "pomodoist_core.h"

void pomo_sync_publish(const pomo_tasklist_t *l);
