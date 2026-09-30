// trev_net_policy.c - see the header. C99 only: nothing here may include an IDF header.
#include "trev_net_policy.h"
#include <stdio.h>
#include <string.h>

bool trev_clock_should_restore(time_t now, time_t saved)
{
    return now < TREV_EPOCH_2020 && saved >= TREV_EPOCH_2020;
}

trev_time_state_t trev_time_on_restore(trev_time_state_t s)
{
    return s == TREV_TIME_NONE ? TREV_TIME_RESTORED : s;
}

trev_time_state_t trev_time_on_trusted(trev_time_state_t s)
{
    (void)s;
    return TREV_TIME_TRUSTED;
}

// Days since 1970-01-01 for a proleptic Gregorian date (Hinnant's days_from_civil). timegm()
// is not C99, so the host test and the chip both use this.
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

bool trev_http_date_parse(const char *s, time_t *out)
{
    static const char *const MON[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    int d, y, hh, mm, ss;
    char mon[4];
    if (!s || !out) return false;
    // Field widths bound the digits sscanf reads: an oversized %d is undefined, and this is network data.
    if (sscanf(s, "%*3s, %2d %3s %4d %2d:%2d:%2d GMT", &d, mon, &y, &hh, &mm, &ss) != 6) return false;
    int m = 0;
    for (int i = 0; i < 12; i++)
        if (strcmp(mon, MON[i]) == 0) m = i + 1;
    if (!m || d < 1 || d > 31 || y < 1970 || y > 9999 || hh < 0 || hh > 23 || mm < 0 || mm > 59 ||
        ss < 0 || ss > 60)
        return false;
    *out = (time_t)days_from_civil(y, m, d) * 86400 + hh * 3600 + mm * 60 + ss;
    return true;
}

trev_walk_act_t trev_walk_fail(trev_walk_t *w, int n)
{
    if (++w->attempt < TREV_WALK_TRIES) return TREV_WALK_TRY;
    w->attempt = 0;
    if (++w->net < n) return TREV_WALK_TRY;
    w->net = 0;
    if (++w->round < TREV_WALK_ROUNDS) return TREV_WALK_TRY;
    w->round = 0;
    // ponytail: no portal action exists at 2.0 (UC3 B), so an outage can never open one
    return TREV_WALK_SLEEP;
}
