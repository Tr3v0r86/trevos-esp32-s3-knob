#pragma once
#include "cal_model.h"
#include <time.h>
// Google events.list pages; caller sets TZ=ICT-7. Reject malformed pages, retain cache.
bool cal_google_begin(cal_window_t *out, time_t now);
bool cal_google_page(const char *json, size_t len, cal_window_t *out,
                     char *next, size_t next_cap);
