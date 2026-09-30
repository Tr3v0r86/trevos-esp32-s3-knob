// trev_prefs.h — scalar settings keys in NVS namespace "trevset" (C8).
//
// Every key is an i32 validated on load against the caller's range: a missing, corrupt or
// out-of-range value reads as the default, so garbage in flash never reaches a face. Keys
// this firmware does not ask for (written by newer firmware) are never read. No LVGL here.
#pragma once
#include <stdbool.h>

// Pure: v when min <= v <= max, else def.
int  trev_pref_validate(int v, int def, int min, int max);

// The stored value if present and in range, else def.
int  trev_pref_get(const char *key, int def, int min, int max);

// Store and commit v (the caller passes a valid one; nothing is checked here). Returns false
// and logs one line when the write or commit fails.
bool trev_pref_set(const char *key, int v);

// Erase the whole "trevset" namespace and nothing else.
void trev_prefs_erase(void);
