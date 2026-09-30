#include "cal_model.h"

#include <string.h>

#include "cJSON.h"

// Copied from boards/papercolor/main/spectra6.c (itself M5GFX's epd_palette in
// Panel_ED2208.cpp): same order, same unweighted squared-RGB rule, first match wins on a tie.
static const struct { uint8_t r, g, b, idx; } PAL[6] = {
    {   0,   0,   0, CAL_PAL_BLACK  },
    { 255, 255, 255, CAL_PAL_WHITE  },
    { 255, 243,  56, CAL_PAL_YELLOW },
    { 191,   0,   0, CAL_PAL_RED    },
    { 100,  64, 255, CAL_PAL_BLUE   },
    {  67, 138,  28, CAL_PAL_GREEN  },
};

uint8_t cal_pal_nearest(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t best_d = 0xFFFFFFFFu;
    uint8_t  best   = CAL_PAL_WHITE;
    for (int i = 0; i < 6; i++) {
        int dr = r - PAL[i].r, dg = g - PAL[i].g, db = b - PAL[i].b;
        uint32_t d = (uint32_t)(dr * dr + dg * dg + db * db);
        if (d < best_d) { best_d = d; best = PAL[i].idx; }
    }
    return best;
}

// Copy with truncation that never splits a UTF-8 sequence.
static void copy_utf8(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static const char *str_or(const cJSON *obj, const char *key, const char *dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : dflt;
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

// "HH:MM", 00:00..23:59; an end may also be 24:00.
static bool parse_hm(const char *s, bool is_end, uint8_t *h, uint8_t *m)
{
    if (!s || strlen(s) != 5 || s[2] != ':' || !is_digit(s[0]) || !is_digit(s[1]) ||
        !is_digit(s[3]) || !is_digit(s[4])) return false;
    int hh = (s[0] - '0') * 10 + (s[1] - '0');
    int mm = (s[3] - '0') * 10 + (s[4] - '0');
    if (mm > 59 || hh > 24 || (hh == 24 && (!is_end || mm != 0))) return false;
    *h = (uint8_t)hh; *m = (uint8_t)mm;
    return true;
}

static int hex_nib(char c)
{
    if (is_digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_rgb(const char *s, uint8_t rgb[3])
{
    if (!s || strlen(s) != 7 || s[0] != '#') return false;
    for (int i = 0; i < 3; i++) {
        int hi = hex_nib(s[1 + 2 * i]), lo = hex_nib(s[2 + 2 * i]);
        if (hi < 0 || lo < 0) return false;
        rgb[i] = (uint8_t)(hi * 16 + lo);
    }
    return true;
}

static bool valid_date(const char *s)
{
    if (!s || strlen(s) != 10 || s[4] != '-' || s[7] != '-') return false;
    for (int i = 0; i < 10; i++)
        if (i != 4 && i != 7 && !is_digit(s[i])) return false;
    return true;
}

static void parse_event(const cJSON *j, cal_event_t *e)
{
    copy_utf8(e->title, sizeof e->title, str_or(j, "title", ""));
    copy_utf8(e->cal, sizeof e->cal, str_or(j, "cal", ""));
    copy_utf8(e->desc, sizeof e->desc, str_or(j, "desc", ""));
    copy_utf8(e->loc, sizeof e->loc, str_or(j, "loc", ""));
    e->hidden = strcmp(str_or(j, "vis", ""), "private") == 0;
    if (!parse_rgb(str_or(j, "color", NULL), e->rgb)) e->rgb[0] = e->rgb[1] = e->rgb[2] = 0;
    e->pal = cal_pal_nearest(e->rgb[0], e->rgb[1], e->rgb[2]);

    e->allday = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "allday"));
    if (e->allday) return;
    const char *end = str_or(j, "end", NULL);
    if (!parse_hm(str_or(j, "start", NULL), false, &e->sh, &e->sm)) {
        e->allday = true;                       // bad or missing start: show it, untimed
        e->sh = e->sm = 0;
    } else if (!end) {
        e->eh = e->sh; e->em = e->sm;           // missing end: zero-length at start
    } else if (!parse_hm(end, true, &e->eh, &e->em)) {
        e->allday = true;
        e->sh = e->sm = e->eh = e->em = 0;
    }
}

bool cal_model_parse(const char *json, size_t len, cal_window_t *out)
{
    memset(out, 0, sizeof *out);
    if (!json) return false;
    cJSON *root = cJSON_ParseWithLength(json, len);
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "days_list");
    if (!cJSON_IsObject(root) || !cJSON_IsArray(list)) { cJSON_Delete(root); return false; }

    copy_utf8(out->generated, sizeof out->generated, str_or(root, "generated", ""));
    copy_utf8(out->tz, sizeof out->tz, str_or(root, "tz", "Asia/Bangkok"));

    const cJSON *dj;
    cJSON_ArrayForEach(dj, list) {
        if (out->n_days == CAL_MAX_DAYS) break;
        if (!cJSON_IsObject(dj) || !valid_date(str_or(dj, "date", NULL))) continue;
        cal_day_t *d = &out->day[out->n_days++];
        memcpy(d->date, str_or(dj, "date", NULL), 11);
        const cJSON *ej;
        const cJSON *evs = cJSON_GetObjectItemCaseSensitive(dj, "events");
        if (!cJSON_IsArray(evs)) continue;      // missing events: an empty day
        cJSON_ArrayForEach(ej, evs) {
            if (!cJSON_IsObject(ej)) continue;
            if (d->n == CAL_MAX_EVENTS) { if (d->more < 255) d->more++; continue; }
            parse_event(ej, &d->ev[d->n++]);
        }
    }
    cJSON_Delete(root);
    return true;
}

int cal_window_index_of(const cal_window_t *w, const char *date)
{
    if (!w || !date) return -1;
    for (int i = 0; i < w->n_days; i++)
        if (strcmp(w->day[i].date, date) == 0) return i;
    return -1;
}

const cal_day_t *cal_window_find(const cal_window_t *w, const char *yyyy_mm_dd)
{
    int i = cal_window_index_of(w, yyyy_mm_dd);
    return i < 0 ? NULL : &w->day[i];
}

void cal_format_time(uint8_t h, uint8_t m, char out[6])
{
    out[0] = (char)('0' + h / 10 % 10); out[1] = (char)('0' + h % 10); out[2] = ':';
    out[3] = (char)('0' + m / 10 % 10); out[4] = (char)('0' + m % 10); out[5] = '\0';
}

int cal_day_next_at(const cal_day_t *d, int now_min)
{
    if (!d) return -1;
    int best = -1, best_min = 24 * 60 + 1;
    for (int i = 0; i < d->n; i++) {
        const cal_event_t *e = &d->ev[i];
        if (e->allday) continue;
        int s = e->sh * 60 + e->sm;
        if (s >= now_min && s < best_min) { best_min = s; best = i; }
    }
    return best;
}

int cal_day_current_at(const cal_day_t *d, int now_min)
{
    if (!d) return -1;
    for (int i = 0; i < d->n; i++) {
        const cal_event_t *e = &d->ev[i];
        if (e->allday) continue;
        int s = e->sh * 60 + e->sm, en = e->eh * 60 + e->em;
        if (s <= now_min && now_min < en) return i;
    }
    return -1;
}

void cal_day_sort(cal_day_t *d)
{
    if (!d || d->n < 2) return;
    for (int i = 1; i < d->n; i++) {                 // insertion sort: stable, n <= 24
        cal_event_t tmp = d->ev[i];
        int ki = tmp.allday ? -1 : tmp.sh * 60 + tmp.sm;
        int j = i - 1;
        while (j >= 0) {
            int kj = d->ev[j].allday ? -1 : d->ev[j].sh * 60 + d->ev[j].sm;
            if (kj <= ki) break;
            d->ev[j + 1] = d->ev[j];
            j--;
        }
        d->ev[j + 1] = tmp;
    }
}

static uint32_t chime_key(const cal_event_t *e)
{
    uint32_t h = 2166136261u;
    for (const char *p = e->title; *p; p++) { h ^= (unsigned char)*p; h *= 16777619u; }
    return ((uint32_t)(e->sh * 60 + e->sm) << 16) ^ (h & 0xFFFFu);
}

static bool ring_has(const cal_chime_ring_t *r, uint32_t k)
{
    for (int i = 0; i < r->n; i++) if (r->key[i] == k) return true;
    return false;
}

int cal_day_chime_due(const cal_day_t *d, int now_min, cal_chime_ring_t *ring)
{
    if (!d || !ring) return 0;
    int due = 0;
    for (int i = 0; i < d->n; i++) {
        const cal_event_t *e = &d->ev[i];
        if (e->allday) continue;
        int s = e->sh * 60 + e->sm;
        if (s < now_min + 1 || s > now_min + 5) continue;
        uint32_t k = chime_key(e);
        if (ring_has(ring, k)) continue;
        ring->key[ring->head] = k;
        ring->head = (uint8_t)((ring->head + 1) % 32);
        if (ring->n < 32) ring->n++;
        due++;
    }
    return due;
}
