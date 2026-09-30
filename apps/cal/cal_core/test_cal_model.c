// Host test for cal_model. Run: make test
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cal_model.h"

static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n);
    size_t got = buf ? fread(buf, 1, (size_t)n, f) : 0;
    assert(got == (size_t)n);
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static cal_window_t w;   // ~163 KB, off the stack like on the device (PSRAM there)

int main(int argc, char **argv)
{
    size_t len;
    char *json = slurp(argc > 1 ? argv[1] : "sample.json", &len);
    assert(cal_model_parse(json, len, &w));
    free(json);

    assert(strcmp(w.tz, "Asia/Bangkok") == 0);
    assert(strcmp(w.generated, "2026-09-25T04:00:12+07:00") == 0);
    assert(w.n_days == 14);                                  // 15th entry has date "bad", skipped
    assert(strcmp(w.day[0].date, "2026-09-22") == 0 && w.day[0].n == 0);   // empty events
    assert(w.day[1].n == CAL_MAX_EVENTS);                    // 30 in, capped at 24
    assert(strcmp(w.day[1].ev[23].title, "Slot 24") == 0);
    assert(w.day[2].n == 0);                                 // "events" key missing

    const cal_day_t *t = cal_window_find(&w, "2026-09-25");
    assert(t && t->n == 11);
    const cal_event_t *e = t->ev;
    assert(e[0].allday && strcmp(e[0].title, "Holiday") == 0 && e[0].cal[0] == '\0');
    assert(e[0].rgb[0] == 0 && e[0].pal == CAL_PAL_BLACK);   // no colour: black
    assert(e[1].allday && e[1].pal == CAL_PAL_GREEN);
    assert(!e[2].allday && e[2].sh == 9 && e[2].sm == 0 && e[2].eh == 10 && e[2].em == 30);
    assert(e[2].rgb[0] == 0x79 && e[2].rgb[1] == 0x86 && e[2].rgb[2] == 0xcb);
    assert(strlen(e[3].title) == 60 && strlen(e[3].cal) == 24);   // 70 -> 60, 39 -> 24
    assert(e[4].allday && strcmp(e[4].title, "Bad start") == 0 && e[4].pal == CAL_PAL_RED);
    assert(!e[5].allday && e[5].eh == 13 && e[5].em == 0 && e[5].pal == CAL_PAL_BLACK);
    assert(!e[6].allday && e[6].eh == 24 && e[6].em == 0);   // runs to midnight
    assert(e[7].allday);                                     // end "25:00" malformed
    assert(strlen(e[8].title) == 59);                        // 59 + 2-byte e-acute: cut whole
    assert(strcmp(e[9].desc, "Bring the printouts. Room is on the third floor.") == 0);
    assert(strcmp(e[9].loc, "Workshop room") == 0);
    assert(e[8].desc[0] == '\0' && e[8].loc[0] == '\0' && !e[8].hidden);
    assert(e[10].hidden && strcmp(e[10].title, "Busy") == 0);

    assert(cal_day_next_at(t, 0) == 2);
    assert(cal_day_next_at(t, 9 * 60) == 2);
    assert(cal_day_next_at(t, 9 * 60 + 1) == 3);      // 11:00
    assert(cal_day_next_at(t, 12 * 60 + 30) == 5);    // 13:00 zero-length
    assert(cal_day_next_at(t, 13 * 60 + 1) == 8);     // 14:00
    assert(cal_day_next_at(t, 14 * 60 + 15) == 9);    // 14:30
    assert(cal_day_next_at(t, 22 * 60 + 1) == -1);    // e[7] is all-day (bad end)
    assert(cal_day_next_at(&w.day[0], 600) == -1);

    assert(cal_day_current_at(t, 9 * 60 + 15) == 2);  // dentist 09:00-10:30
    assert(cal_day_current_at(t, 13 * 60) == -1);     // zero-length is never in progress
    assert(cal_day_current_at(t, 14 * 60 + 45) == 8); // 14:00-15:00 (first match wins over 14:30)
    assert(cal_day_current_at(t, 20 * 60) == -1);

    cal_chime_ring_t ring = { 0 };
    assert(cal_day_chime_due(t, 8 * 60 + 54, &ring) == 0);
    assert(cal_day_chime_due(t, 8 * 60 + 55, &ring) == 1);     // 09:00 in [08:56, 09:00]
    assert(cal_day_chime_due(t, 8 * 60 + 56, &ring) == 0);     // already rung
    assert(cal_day_chime_due(t, 13 * 60 + 56, &ring) == 1);    // 14:00, once
    assert(cal_day_chime_due(t, 14 * 60 + 26, &ring) == 1);    // 14:30
    // reorder: the same day with events reversed must not re-fire
    static cal_day_t rev; rev = *t;
    for (int i = 0; i < rev.n / 2; i++) { cal_event_t s = rev.ev[i]; rev.ev[i] = rev.ev[rev.n - 1 - i]; rev.ev[rev.n - 1 - i] = s; }
    assert(cal_day_chime_due(&rev, 14 * 60 + 27, &ring) == 0);
    // two events one minute: one call, count 2, the caller chimes once
    static cal_day_t two; memset(&two, 0, sizeof two);
    two.n = 2; two.ev[0].sh = 10; two.ev[1].sh = 10; strcpy(two.ev[0].title, "a"); strcpy(two.ev[1].title, "b");
    cal_chime_ring_t r2 = { 0 };
    assert(cal_day_chime_due(&two, 9 * 60 + 56, &r2) == 2);
    assert(cal_day_chime_due(&two, 9 * 60 + 57, &r2) == 0);

    static cal_day_t sorted; sorted = *t; cal_day_sort(&sorted);
    assert(sorted.ev[0].allday && sorted.ev[1].allday && sorted.ev[2].allday && sorted.ev[3].allday);
    assert(sorted.ev[4].sh == 9 && sorted.ev[5].sh == 11 && sorted.ev[6].sh == 13 && sorted.ev[7].sh == 14 && sorted.ev[7].sm == 0);
    assert(sorted.ev[8].sm == 30 && sorted.ev[9].sh == 16 && sorted.ev[10].sh == 22);

    // find / index_of
    assert(cal_window_index_of(&w, "2026-09-22") == 0);
    assert(cal_window_index_of(&w, "2026-10-05") == 13);
    assert(cal_window_index_of(&w, "2026-10-06") == -1);
    assert(cal_window_index_of(&w, "bad") == -1);
    assert(cal_window_find(&w, "2026-09-21") == NULL);

    // not the contract
    static cal_window_t bad;
    assert(!cal_model_parse("{\"ok\":true}", 11, &bad));
    assert(!cal_model_parse("[]", 2, &bad));
    assert(!cal_model_parse("<html>", 6, &bad));
    assert(!cal_model_parse("{\"days_list\":{}}", 16, &bad));
    assert(cal_model_parse("{\"days_list\":[]}", 16, &bad) && bad.n_days == 0);

    // time formatting
    char s[6];
    cal_format_time(9, 5, s);   assert(strcmp(s, "09:05") == 0);
    cal_format_time(0, 0, s);   assert(strcmp(s, "00:00") == 0);
    cal_format_time(23, 59, s); assert(strcmp(s, "23:59") == 0);
    cal_format_time(24, 0, s);  assert(strcmp(s, "24:00") == 0);

    // palette: the six primaries map to themselves
    assert(cal_pal_nearest(0, 0, 0) == CAL_PAL_BLACK);
    assert(cal_pal_nearest(255, 255, 255) == CAL_PAL_WHITE);
    assert(cal_pal_nearest(255, 243, 56) == CAL_PAL_YELLOW);
    assert(cal_pal_nearest(191, 0, 0) == CAL_PAL_RED);
    assert(cal_pal_nearest(100, 64, 255) == CAL_PAL_BLUE);
    assert(cal_pal_nearest(67, 138, 28) == CAL_PAL_GREEN);
    // Google event colours
    assert(cal_pal_nearest(0x79, 0x86, 0xcb) == CAL_PAL_BLUE);    // lavender, the default blue
    assert(cal_pal_nearest(0xd5, 0x00, 0x00) == CAL_PAL_RED);     // tomato
    assert(cal_pal_nearest(0x33, 0xb6, 0x79) == CAL_PAL_GREEN);   // sage

    printf("sizeof cal_event_t=%zu cal_day_t=%zu cal_window_t=%zu\n",
           sizeof(cal_event_t), sizeof(cal_day_t), sizeof(cal_window_t));
    assert(sizeof(cal_day_t) < 10000);
    printf("#7986cb->%u #d50000->%u #33b679->%u\n", cal_pal_nearest(0x79, 0x86, 0xcb),
           cal_pal_nearest(0xd5, 0, 0), cal_pal_nearest(0x33, 0xb6, 0x79));
    puts("test_cal_model: all passed");
    return 0;
}
