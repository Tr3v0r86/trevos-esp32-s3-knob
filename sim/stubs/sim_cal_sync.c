/* sim/stubs/sim_cal_sync.c: host cal_sync. No network. A fixed demo window dated today and
 * tomorrow so the face finds it whatever day the sim runs on. Generation is 1 from the first
 * call: the face sees one publish, as a booted disk with a cache does.
 *   TT_DEV_CAL_EMPTY=1   today has no events (the NOTHING SCHEDULED shot).
 *   TT_DEV_CAL_MORE=1    today's `more` is 1: events were dropped past CAL_MAX_EVENTS (Calendar incomplete).
 *   TT_DEV_CAL_NODATA=1  nothing was ever published: get_day false, generation 0.
 *   SIM_CAL_LINK=none|expired  (env) link state; anything else is OK. Pair with NODATA to see its copy.
 *   TT_DEV_CAL_REPUB=1   after 20 cal_sync_generation calls (about 2 s at the face's 10 Hz
 *                        tick, inside cal_repub's 600-tick run of ~4 s) today's third event
 *                        is retitled and the generation goes 1 -> 2. */
#include "cal_sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef TT_DEV_CAL_EMPTY
#define TT_DEV_CAL_EMPTY 0
#endif
#ifndef TT_DEV_CAL_MORE
#define TT_DEV_CAL_MORE 0
#endif
#ifndef TT_DEV_CAL_NODATA
#define TT_DEV_CAL_NODATA 0
#endif
#ifndef TT_DEV_CAL_REPUB
#define TT_DEV_CAL_REPUB 0
#endif

#ifndef TT_DEV_CAL_DENSE
#define TT_DEV_CAL_DENSE 0
#endif
#ifndef TT_DEV_CAL_PRIVATE
#define TT_DEV_CAL_PRIVATE 0
#endif
#ifndef TT_DEV_CAL_ALLDAY
#define TT_DEV_CAL_ALLDAY 0
#endif
#ifndef TT_DEV_CAL_ABSENT
#define TT_DEV_CAL_ABSENT 0
#endif

static cal_window_t s_w;
static bool s_built;
static uint32_t s_gen = 1;
static unsigned s_ticks;
static time_t s_test_sync;
static bool s_cleared;   // clear_cache ran: no last sync until Sync now
void sim_cal_publish(const cal_day_t *d,time_t synced) {
    if(!s_built || memcmp(&s_w.day[0],d,sizeof *d)) s_gen++;
    s_w.day[0]=*d;s_w.n_days=1;s_built=true;s_test_sync=synced;
}

static void ev(cal_day_t *d, bool allday, int sh, int sm, int eh, int em, const char *title,
               const char *color, const char *desc, const char *loc)
{
    if (d->n >= CAL_MAX_EVENTS) return;
    cal_event_t *e = &d->ev[d->n++];
    memset(e, 0, sizeof *e);
    e->allday = allday; e->sh = sh; e->sm = sm; e->eh = eh; e->em = em;
    snprintf(e->title, sizeof e->title, "%s", title);
    snprintf(e->cal, sizeof e->cal, "Demo");
    if (desc) snprintf(e->desc, sizeof e->desc, "%s", desc);
    if (loc)  snprintf(e->loc, sizeof e->loc, "%s", loc);
    unsigned r, g, b;
    if (color && sscanf(color, "#%02x%02x%02x", &r, &g, &b) == 3) { e->rgb[0] = r; e->rgb[1] = g; e->rgb[2] = b; }
    e->pal = cal_pal_nearest(e->rgb[0], e->rgb[1], e->rgb[2]);
}

static void build(void)
{
    memset(&s_w, 0, sizeof s_w);
    time_t now = time(NULL);
    struct tm tm; localtime_r(&now, &tm);
    strftime(s_w.day[0].date, 11, "%Y-%m-%d", &tm);
    tm.tm_mday += 1; tm.tm_hour = 12; mktime(&tm);
    strftime(s_w.day[1].date, 11, "%Y-%m-%d", &tm);
    s_w.n_days = 2;
    snprintf(s_w.generated, sizeof s_w.generated, "2026-09-26T07:42:00+07:00");
    snprintf(s_w.tz, sizeof s_w.tz, "Asia/Bangkok");

    cal_day_t *t = &s_w.day[0], *m = &s_w.day[1];
    if (!TT_DEV_CAL_EMPTY) {
        ev(t, true, 0, 0, 0, 0, "A day for making", "#f6bf26", NULL, NULL);
        ev(t, false, 8, 30, 9, 0, "Morning walk", "#33b679", "A screen-free start.", "The park");
        ev(t, false, 9, 0, 10, 0, "Creative work", "#7986cb", "One quiet hour for the next idea.", "Studio");
        ev(t, false, 10, 30, 11, 0, "Design review", "#7986cb", "Compare a few sketches.", "Workshop");
        ev(t, false, 12, 0, 13, 0, "Lunch break", "#616161", NULL, NULL);
        ev(t, false, 14, 0, 15, 0, "Reading hour", "#e67c73", NULL, NULL);
    }
    if(TT_DEV_CAL_DENSE) ev(t,false,9,10,9,40,"Sketch review","#33b679",NULL,"Studio");
    if(TT_DEV_CAL_MORE) t->more=1;
    if(TT_DEV_CAL_PRIVATE && t->n>2) t->ev[2].hidden=true;
    if(TT_DEV_CAL_ALLDAY) t->n=1;
    ev(m, false, 9, 0, 10, 0, "Build something small", "#7986cb", NULL, NULL);
    ev(m, false, 13, 0, 14, 0, "Afternoon walk", "#616161", NULL, "The park");
    const char *palette=getenv("SIM_CAL_COLOR");
    if(palette) for(int i=0;i<t->n;i++) t->ev[i].pal=(unsigned)atoi(palette)%7;
    s_built = true;
}

void cal_sync_start(const char *url, const char *cal_id) { (void)url; (void)cal_id; }
void cal_sync_start_serial(void) {}
void cal_sync_set_time_hooks(bool (*trusted)(void), void (*http_date)(const char *date)) { (void)trusted; (void)http_date; }
cal_link_t cal_sync_link(void)
{
    const char *l = getenv("SIM_CAL_LINK");
    if (l && !strcmp(l, "none")) return CAL_LINK_NONE;
    if (l && !strcmp(l, "expired")) return CAL_LINK_EXPIRED;
    return CAL_LINK_OK;
}

uint32_t cal_sync_generation(void)
{
    if (TT_DEV_CAL_NODATA) return 0;
    if (!s_built) build();
    if (TT_DEV_CAL_REPUB && s_gen == 1 && ++s_ticks > 20) {
        cal_day_t *t = &s_w.day[0];
        if (t->n > 2) snprintf(t->ev[2].title, sizeof t->ev[2].title, "Creative work, new studio");
        s_gen = 2;
    }
    return s_gen;
}

bool cal_sync_get_day(const char *date, cal_day_t *out)
{
    if (!out) return false;
    if (TT_DEV_CAL_ABSENT) { memset(out,0,sizeof *out);return false; }
    if (TT_DEV_CAL_NODATA) { memset(out, 0, sizeof *out); return false; }
    if (!s_built) build();
    if (strcmp(date, s_w.day[0].date) != 0) fprintf(stderr, "[cal fixture] non-today lookup\n");
    const cal_day_t *d = cal_window_find(&s_w, date);
    if (!d) { memset(out, 0, sizeof *out); return false; }
    memcpy(out, d, sizeof *out);
    return true;
}

// 07:42 local today, matching sim_sync.c's Pomodoist stub, so the rail's age line is stable.
// SIM_CAL_SYNC_AGE_MIN=<n> (env, default 0) backdates the sync by n minutes so the ring's
// "Today, synced N min ago" line can be shot; unset, every shot reads exactly as before.
bool cal_sync_last_sync(time_t *out)
{
    if (TT_DEV_CAL_NODATA || s_cleared) return false;
    const char *age = getenv("SIM_CAL_SYNC_AGE_MIN");
    time_t back = age ? (time_t)atoi(age) * 60 : 0;
    if(s_test_sync) {if(out) *out=s_test_sync - back;return true;}
    time_t now = time(NULL);
    struct tm tm; localtime_r(&now, &tm);
    tm.tm_sec = 0;
    if (out) *out = mktime(&tm) - back;
    return true;
}

// Settings rows (T11, C7). Never called by a shot. Sync now re-publishes the fixture; clear
// publishes an empty window (get_day false, generation bumped).
void cal_sync_request_now(void) { fprintf(stderr, "[cal] sync now\n"); if (!s_built || s_cleared) build(); s_cleared = false; s_gen++; }
void cal_sync_clear_cache(void)
{
    fprintf(stderr, "[cal] cache cleared\n");
    memset(&s_w, 0, sizeof s_w); s_built = true; s_test_sync = 0; s_cleared = true; s_gen++;
}
