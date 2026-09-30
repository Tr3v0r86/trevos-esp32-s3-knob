// cal_sync.c - fetch the proxy window 60 s after GOT_IP and every 5 minutes, publish on change,
// cache to NVS new-then-erase.
#include "cal_sync.h"
#include "cal_google.h"
#include "cJSON.h"
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "cal_sync";

#ifndef CAL_CACHE_PARTITION
#define CAL_CACHE_PARTITION "nvs"
#endif
#define NS            "cal"
#define FETCH_BIT     BIT0
#define REFRESH_US    (5 * 60 * 1000000LL)
#define ARM_US        (60 * 1000000LL)
#ifndef BODY_CAP
#define BODY_CAP      (48 * 1024)   // a 2-day, 48-event window with every field at max measured 42,015 bytes
#endif
// Where the window, the body and the OAuth workspace live. PSRAM on the disk; a board without
// it (the CYD, ADR-0017) builds with MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT and smaller caps.
#ifndef CAL_ALLOC_CAPS
#define CAL_ALLOC_CAPS MALLOC_CAP_SPIRAM
#endif
#define FETCH_STACK   10240

static char   s_url[512];
static char   s_cal[96];
static const char *s_client, *s_secret, *s_refresh_token;
static EventGroupHandle_t  s_eg;
static SemaphoreHandle_t   s_lock;        // guards s_pub, s_gen and s_last
static cal_window_t       *s_pub;         // published window, PSRAM; NULL = no data, ever
static cal_window_t       *s_tmp;         // fetch target, PSRAM
static char               *s_body;        // response body, PSRAM
static volatile uint32_t   s_gen;
static time_t              s_last;
static bool                s_dirty;       // s_pub differs from what NVS holds
static esp_timer_handle_t  s_arm, s_refresh;
static TaskHandle_t        s_task;
static bool              (*s_trusted)(void);            // NULL = trust the clock as today
static void              (*s_http_date)(const char *);  // NULL = ignore the Date header
static volatile cal_link_t s_link = CAL_LINK_NONE;
static bool                s_retried;                   // one early refetch per failing streak

// ---------------------------------------------------------------- NVS store (ported from the page)

static void day_key(const char *date, char key[10])
{
    key[0] = 'd';
    memcpy(key + 1, date, 4); memcpy(key + 5, date + 5, 2); memcpy(key + 7, date + 8, 2);
    key[9] = '\0';
}
static size_t day_len(uint8_t n) { return offsetof(cal_day_t, ev) + n * sizeof(cal_event_t); }

typedef struct { char generated[26]; uint8_t n_days; char first[11]; } meta_t;

static bool today_str(char out[11])
{
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    if (tm.tm_year + 1900 < 2020) return false;
    strftime(out, 11, "%Y-%m-%d", &tm);
    return true;
}

// The caller has already refused a window without device-today (sync_task), so this never
// erases today's blob for a window that lacks it.
static esp_err_t store_save(const cal_window_t *w)
{
    if (w->n_days == 0) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(CAL_CACHE_PARTITION, NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    // Write the new days first, then the meta, then erase stale day keys: a power loss at any
    // point leaves either the old window or the new one readable, never neither.
    char key[10];
    for (int i = 0; i < w->n_days && err == ESP_OK; i++) {
        day_key(w->day[i].date, key);
        err = nvs_set_blob(h, key, &w->day[i], day_len(w->day[i].n));
    }
    meta_t m = { 0 };
    memcpy(m.generated, w->generated, sizeof m.generated);
    m.n_days = w->n_days;
    memcpy(m.first, w->day[0].date, sizeof m.first);
    if (err == ESP_OK) err = nvs_set_blob(h, "meta", &m, sizeof m);
    if (err == ESP_OK) err = nvs_commit(h);
    if (err == ESP_OK) {
        nvs_iterator_t it = NULL;
        esp_err_t r = nvs_entry_find_in_handle(h, NVS_TYPE_BLOB, &it);
        char stale[8][NVS_KEY_NAME_MAX_SIZE]; int ns = 0;   // ponytail: 8 per save; any rest go next save
        while (r == ESP_OK && ns < 8) {
            nvs_entry_info_t info; nvs_entry_info(it, &info);
            bool keep = false;
            for (int i = 0; i < w->n_days; i++) { day_key(w->day[i].date, key); if (strcmp(key, info.key) == 0) keep = true; }
            if (info.key[0] == 'd' && !keep) strcpy(stale[ns++], info.key);
            r = nvs_entry_next(&it);
        }
        nvs_release_iterator(it);
        for (int i = 0; i < ns; i++) nvs_erase_key(h, stale[i]);
        if (ns) nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "save: %s", esp_err_to_name(err));
    return err;
}

// Freshness lives in its own key, written on every good fetch whether or not the days changed.
static void store_last(time_t t)
{
    nvs_handle_t h;
    if (nvs_open_from_partition(CAL_CACHE_PARTITION, NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_i64(h, "last", (int64_t)t) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

// Loads the cache into *w and s_last. A day blob whose length does not match this firmware's
// struct is dropped, so a layout change reads as "no data" rather than garbage. Returns days loaded.
static int store_load(cal_window_t *w)
{
    memset(w, 0, sizeof *w);
    nvs_handle_t h;
    if (nvs_open_from_partition(CAL_CACHE_PARTITION, NS, NVS_READONLY, &h) != ESP_OK) return 0;
    int64_t last = 0;
    bool have_last = nvs_get_i64(h, "last", &last) == ESP_OK && last > 0;
    meta_t m; size_t len = sizeof m;
    if (nvs_get_blob(h, "meta", &m, &len) != ESP_OK || len != sizeof m || m.n_days == 0 || m.n_days > CAL_MAX_DAYS) {
        nvs_close(h); return 0;
    }
    m.generated[sizeof m.generated - 1] = '\0';
    m.first[sizeof m.first - 1] = '\0';
    memcpy(w->generated, m.generated, sizeof w->generated);
    if (have_last) {
        s_last = (time_t)last;
    } else {
        // "2026-09-25T04:00:12+07:00": fixed format from the proxy. The zone is Bangkok, the
        // same as TZ on this device, so mktime on the local fields is the right conversion.
        struct tm g = { 0 };
        if (sscanf(m.generated, "%4d-%2d-%2dT%2d:%2d:%2d", &g.tm_year, &g.tm_mon, &g.tm_mday, &g.tm_hour, &g.tm_min, &g.tm_sec) == 6) {
            g.tm_year -= 1900; g.tm_mon -= 1; g.tm_isdst = 0;
            s_last = mktime(&g);
        }
    }
    // Days are stored under their own date keys; walk forward from `first` by string date,
    // which is enough because the disk stores consecutive days from the proxy.
    char date[11]; memcpy(date, m.first, 11);
    for (int i = 0; i < m.n_days; i++) {
        char key[10]; day_key(date, key);
        cal_day_t *d = &w->day[w->n_days];
        len = sizeof *d;
        if (nvs_get_blob(h, key, d, &len) == ESP_OK && d->n <= CAL_MAX_EVENTS && len == day_len(d->n)) w->n_days++;
        else { ESP_LOGW(TAG, "cache day %s dropped (layout or length mismatch)", date); memset(d, 0, sizeof *d); }
        // next calendar date: parse, add a day, format. mktime normalises the overflow.
        struct tm tm = { 0 };
        tm.tm_year = (date[0]-'0')*1000 + (date[1]-'0')*100 + (date[2]-'0')*10 + (date[3]-'0') - 1900;
        tm.tm_mon  = (date[5]-'0')*10 + (date[6]-'0') - 1;
        tm.tm_mday = (date[8]-'0')*10 + (date[9]-'0') + 1;
        tm.tm_hour = 12;
        mktime(&tm);
        strftime(date, sizeof date, "%Y-%m-%d", &tm);
    }
    nvs_close(h);
    return w->n_days;
}

// ---------------------------------------------------------------- publish

static bool same_days(const cal_window_t *a, const cal_window_t *b)
{
    if (a->n_days != b->n_days) return false;
    for (int i = 0; i < a->n_days; i++)
        if (a->day[i].n != b->day[i].n || memcmp(&a->day[i], &b->day[i], day_len(a->day[i].n)) != 0) return false;
    return true;
}

// Copies only the header and the days in use, and bumps the generation only on a change, so
// a face does not rebuild every 5 minutes for the same window.
static void publish(const cal_window_t *src)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool changed = !same_days(src, s_pub);
    if (changed) {
        memcpy(s_pub, src, offsetof(cal_window_t, day) + (size_t)src->n_days * sizeof(cal_day_t));
        s_gen++;
    }
    xSemaphoreGive(s_lock);
    if (changed) s_dirty = true;
}

uint32_t cal_sync_generation(void) { return s_gen; }

void cal_sync_set_time_hooks(bool (*trusted)(void), void (*http_date)(const char *date))
{
    s_trusted = trusted;
    s_http_date = http_date;
}

cal_link_t cal_sync_link(void) { return s_link; }

bool cal_sync_get_day(const char *date, cal_day_t *out)
{
    if (!out) return false;
    bool ok = false;
    if (s_pub && date) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const cal_day_t *d = cal_window_find(s_pub, date);
        if (d) { memcpy(out, d, sizeof *out); ok = true; }
        xSemaphoreGive(s_lock);
    }
    if (!ok) memset(out, 0, sizeof *out);
    return ok;
}

bool cal_sync_last_sync(time_t *out)
{
    if (!s_pub) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    time_t t = s_last;
    xSemaphoreGive(s_lock);
    if (!t) return false;
    if (out) *out = t;
    return true;
}

// ---------------------------------------------------------------- fetch (ported from the page's cal_fetch.cpp)

typedef struct { char *buf; size_t cap, len; bool overflow; bool any_status; } sink_t;

static esp_err_t on_event(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_HEADER) {
        if (s_http_date && evt->header_key && evt->header_value && !strcasecmp(evt->header_key, "Date"))
            s_http_date(evt->header_value);
        return ESP_OK;
    }
    if (evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    sink_t *s = evt->user_data;
    // The 302's HTML body also arrives here; only Google's own calls (any_status) want an error body.
    if (!s->any_status && esp_http_client_get_status_code(evt->client) != 200) return ESP_OK;
    size_t n = (size_t)evt->data_len;
    if (s->overflow || s->len + n >= s->cap) { s->overflow = true; return ESP_OK; }
    memcpy(s->buf + s->len, evt->data, n);
    s->len += n;
    return ESP_OK;
}

static esp_err_t fetch(char *buf, size_t cap, size_t *out_len)
{
    char full[768];
    int n = snprintf(full, sizeof full, "%s%sback=0&days=2&desc=1&ascii=1%s%s", s_url,
                     strchr(s_url, '?') ? "&" : "?", s_cal[0] ? "&cal=" : "", s_cal);
    if (n < 0 || (size_t)n >= sizeof full) return ESP_ERR_INVALID_ARG;

    sink_t sink = { buf, cap, 0, false, false };
    esp_http_client_config_t cfg = {
        .url = full,
        .event_handler = on_event,
        .user_data = &sink,
        .timeout_ms = 45000,                 // a cold Apps Script took 16.5 s on the page
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = false,      // /exec answers 302 to script.googleusercontent.com
        .max_redirection_count = 5,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,              // the redirect target URL is 500+ chars
    };
    ESP_LOGI(TAG, "fetch: largest free internal block %u", (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    // Never log the URL: it is the secret.
    if (err != ESP_OK) { ESP_LOGW(TAG, "perform: %s", esp_err_to_name(err)); return err; }
    if (status != 200) { ESP_LOGW(TAG, "HTTP %d", status); return ESP_FAIL; }
    if (sink.overflow) { ESP_LOGW(TAG, "body over %u bytes", (unsigned)(cap - 1)); return ESP_ERR_INVALID_SIZE; }
    buf[sink.len] = '\0';
    *out_len = sink.len;
    return ESP_OK;
}

// OAuth credentials are provisioned over USB, never sent to the LAN OTA endpoint.
static bool enc(char *dst, size_t cap, const char *src) {
    static const char hex[]="0123456789ABCDEF";size_t n=0;
    for(const unsigned char *p=(const unsigned char *)src;*p;p++) {
        bool safe=(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||strchr("-._~",*p);
        if(n+(safe?1:3)>=cap) return false;
        if(safe) dst[n++]=*p;else {dst[n++]='%';dst[n++]=hex[*p>>4];dst[n++]=hex[*p&15];}
    }
    dst[n]=0;return true;
}
static int google_http(const char *url, const char *post, const char *bearer, size_t *len) {
    sink_t sink={s_body,BODY_CAP,0,false,true};
    esp_http_client_config_t cfg={.url=url,.event_handler=on_event,.user_data=&sink,
        .timeout_ms=30000,.crt_bundle_attach=esp_crt_bundle_attach,
        .disable_auto_redirect=true,.buffer_size=2048,.buffer_size_tx=4096};
    esp_http_client_handle_t c=esp_http_client_init(&cfg);if(!c)return 0;
    esp_err_t err=ESP_OK;
    if(bearer) err=esp_http_client_set_header(c,"Authorization",bearer);
    if(post && err==ESP_OK) {
        err=esp_http_client_set_method(c,HTTP_METHOD_POST);
        if(err==ESP_OK)err=esp_http_client_set_header(c,"Content-Type","application/x-www-form-urlencoded");
        if(err==ESP_OK)err=esp_http_client_set_post_field(c,post,strlen(post));
    }
    if(err==ESP_OK)err=esp_http_client_perform(c);
    int status=esp_http_client_get_status_code(c);esp_http_client_cleanup(c);
    if(err!=ESP_OK||sink.overflow) {ESP_LOGW(TAG,"Google transport failed (%s, overflow=%d)",esp_err_to_name(err),sink.overflow);return 0;}
    s_body[sink.len]=0;*len=sink.len;return status;
}
static bool google_fetch(cal_window_t *out) {
    if(!cal_google_begin(out,time(NULL))) return false;
    // Reuse PSRAM workspace: form, encoded fields, bearer, URL. No credentials on task stack.
    char *work=heap_caps_calloc(1,10240,CAL_ALLOC_CAPS);
    char *form=heap_caps_calloc(1,6144,CAL_ALLOC_CAPS);
    if(!work||!form){heap_caps_free(work);heap_caps_free(form);return false;}
    char *a=work,*b=work+2048,*r=work+4096,*auth=work+6144;
    bool ok=false;size_t len=0;
    if(!enc(a,2048,s_client)||!enc(b,2048,s_secret)||!enc(r,2048,s_refresh_token))goto done;
    int n=snprintf(form,6144,"grant_type=refresh_token&client_id=%s&client_secret=%s&refresh_token=%s",a,b,r);
    if(n<0||n>=6144)goto done;
    int status=google_http("https://oauth2.googleapis.com/token",form,NULL,&len);
    if(status!=200) {
        ESP_LOGW(TAG,"Google token HTTP %d; reauthorize on computer if persistent",status);
        if((status==400||status==401) && len && strstr(s_body,"invalid_grant")) s_link=CAL_LINK_EXPIRED;   // body never logged
        goto done;
    }
    cJSON *token=cJSON_ParseWithLength(s_body,len);
    const cJSON *access=token?cJSON_GetObjectItemCaseSensitive(token,"access_token"):NULL;
    if(!cJSON_IsString(access)||!access->valuestring[0]||strlen(access->valuestring)>3000||strpbrk(access->valuestring,"\r\n")) {cJSON_Delete(token);goto done;}
    snprintf(auth,4096,"Bearer %s",access->valuestring);cJSON_Delete(token);
    char next[1024]="";
    // 100 events/page, at most 10 pages. Refuse an incomplete window instead of caching it.
    for(int page=0;page<10;page++) {
        if(!enc(a,2048,s_cal)||!enc(b,2048,next))goto done;
        struct tm end={0};int y,m,d;sscanf(out->day[1].date,"%d-%d-%d",&y,&m,&d);
        end.tm_year=y-1900;end.tm_mon=m-1;end.tm_mday=d+1;mktime(&end);char until[11];strftime(until,sizeof until,"%Y-%m-%d",&end);
        n=snprintf(form,6144,"https://www.googleapis.com/calendar/v3/calendars/%s/events?singleEvents=true&orderBy=startTime&maxResults=100&timeZone=Asia%%2FBangkok&timeMin=%sT00%%3A00%%3A00%%2B07%%3A00&timeMax=%sT00%%3A00%%3A00%%2B07%%3A00&fields=nextPageToken,items(status,summary,description,location,colorId,start,end,attendees(self,responseStatus))%s%s",a,out->day[0].date,until,*b?"&pageToken=":"",b);
        if(n<0||n>=6144)goto done;
        status=google_http(form,NULL,auth,&len);
        if(status!=200){ESP_LOGW(TAG,"Google events HTTP %d; keeping cache",status);goto done;}
        if(!cal_google_page(s_body,len,out,next,sizeof next))goto done;
        if(!*next){ok=true;break;}
    }
done:
    // Explicit wipe before freeing token/form buffers. No credential or URL logging.
    for(volatile char *q=work;q<work+10240;q++)*q=0;
    for(volatile char *q=form;q<form+6144;q++)*q=0;
    heap_caps_free(form);heap_caps_free(work);memset(s_body,0,BODY_CAP);return ok;
}

// One early refetch per failing streak: reuses the GOT_IP arming timer (on_arm sets FETCH_BIT).
// The streak ends at the next published window; the 5-minute refresh carries on regardless.
static void refetch_soon(void)
{
    if (s_retried || !s_arm) return;
    s_retried = true;
    esp_timer_stop(s_arm);
    esp_timer_start_once(s_arm, 5 * 1000000LL);
}

// The one path from a parsed s_tmp to the face and the cache, for every transport.
static void accept_window(bool parsed)
{
    if (!parsed || s_tmp->n_days == 0) {
        ESP_LOGW(TAG, "parse failed or empty window; keeping cache");
        return;
    }
    s_link = CAL_LINK_OK;     // the fetch itself worked, whatever we then decide about its clock
    if (s_trusted && !s_trusted()) {
        ESP_LOGW(TAG, "clock not trusted; window held");
        refetch_soon();
        return;
    }
    // A window fetched a few seconds after the proxy's midnight but before the device's has
    // no device-today in it; publishing or saving it would show an empty morning.
    char td[11];
    bool clock_ok = today_str(td);
    if (clock_ok && !cal_window_find(s_tmp, td)) { ESP_LOGW(TAG, "window lacks today %s; dropped", td); if (s_trusted) refetch_soon(); return; }
    s_retried = false;
    publish(s_tmp);
    if (clock_ok) {       // an unset clock would stamp 1970 and read as "synced 56 years ago"
        time_t now = time(NULL);
        xSemaphoreTake(s_lock, portMAX_DELAY); s_last = now; xSemaphoreGive(s_lock);
        store_last(now);
    }
    if (s_dirty && store_save(s_tmp) == ESP_OK) s_dirty = false;    // a failed save retries next fetch
    ESP_LOGI(TAG, "synced %u days, %u events today", (unsigned)s_tmp->n_days, (unsigned)s_tmp->day[0].n);
}

static void sync_task(void *arg)
{
    (void)arg;
    for (;;) {
        xEventGroupWaitBits(s_eg, FETCH_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        size_t len = 0;
        bool parsed;
        if (s_refresh_token) parsed = google_fetch(s_tmp);
        else {
            if (fetch(s_body, BODY_CAP, &len) != ESP_OK) continue;
            parsed = cal_model_parse(s_body, len, s_tmp);
        }
        accept_window(parsed);
    }
}

static void on_arm(void *arg) { (void)arg; xEventGroupSetBits(s_eg, FETCH_BIT); }

// Settings > Calendar > Sync now. Same path as the arm timer: set FETCH_BIT and let sync_task run
// the fetch in its own order. s_arm exists only when a fetch transport was started, so this is a
// no-op before cal_sync_start and on the serial transport (no fetch task).
void cal_sync_request_now(void)
{
    if (!s_arm || !s_eg) { ESP_LOGW(TAG, "sync now: no fetch transport running; ignored"); return; }
    ESP_LOGI(TAG, "sync now");
    on_arm(NULL);
}

// Settings > Reset > Clear calendar cache (C7). Erases the "cal" namespace on the cache partition,
// then publishes an empty window so the face drops to DAY_ABSENT ("No data for today"). Never waits
// on sync_task: the lock is held only across the NVS erase and the s_last reset. s_lock guards s_last
// only; it does not fence store_save/store_last in accept_window, so a fetch that lands mid-clear can
// re-cache its window after the erase (accepted: the wanted outcome of a sync after a clear, and a
// torn write heals on the next fetch). Sync now bypasses the 60 s post-GOT_IP delay (on_arm directly).
void cal_sync_clear_cache(void)
{
    if (!s_pub || !s_lock) { ESP_LOGW(TAG, "clear cache: not started; ignored"); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(CAL_CACHE_PARTITION, NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_erase_all(h);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err == ESP_OK) s_last = 0;
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) { ESP_LOGE(TAG, "clear cache: %s", esp_err_to_name(err)); return; }
    // publish reads only n_days and the header of its argument for a zero-day window, so a
    // header-sized buffer stands in for a whole cal_window_t (ponytail: avoids a 162,639 B temporary;
    // ceiling: if publish or same_days ever reads day[0] for n_days == 0, this is an out-of-bounds read).
    _Alignas(cal_window_t) char hdr[offsetof(cal_window_t, day)] = { 0 };
    publish((const cal_window_t *)hdr);
    ESP_LOGI(TAG, "cache cleared");
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;
    // Pomodoist's fetch and the OTA server start on this same event; three TLS sessions on
    // internal RAM at once is how the disk ran out of heap on 2026-09-05. Cal waits a minute.
    esp_timer_stop(s_arm);
    esp_timer_start_once(s_arm, ARM_US);
}

static void on_refresh(void *arg)
{
    (void)arg;
    xEventGroupSetBits(s_eg, FETCH_BIT);      // no IP: fetch() fails fast and the cache stands
}

// Frees whatever cal_sync_start made. s_pub stays NULL, so the face shows WAITING FOR CALENDAR.
static void unwind(cal_window_t *pub)
{
    if (s_task)    { vTaskDelete(s_task); s_task = NULL; }
    if (s_refresh) { esp_timer_stop(s_refresh); esp_timer_delete(s_refresh); s_refresh = NULL; }
    if (s_arm)     { esp_timer_stop(s_arm); esp_timer_delete(s_arm); s_arm = NULL; }
    heap_caps_free(s_body); s_body = NULL;
    heap_caps_free(s_tmp);  s_tmp = NULL;
    heap_caps_free(pub);
    if (s_eg)   { vEventGroupDelete(s_eg); s_eg = NULL; }
    if (s_lock) { vSemaphoreDelete(s_lock); s_lock = NULL; }
    s_last = 0;
}

void cal_sync_start(const char *url, const char *cal_id)
{
    if (s_lock) return;   // once
    if ((!url || !url[0]) && !s_refresh_token) ESP_LOGW(TAG, "no url; calendar runs on cache only");
    else if (url) snprintf(s_url, sizeof s_url, "%s", url);
    if (cal_id) snprintf(s_cal, sizeof s_cal, "%s", cal_id);
    s_link = (s_url[0] || s_refresh_token) ? CAL_LINK_OK : CAL_LINK_NONE;   // OK until a token is refused

    // s_pub is assigned only once everything exists, so every failure below leaves it NULL.
    cal_window_t *pub = NULL;
    s_lock = xSemaphoreCreateMutex();
    s_eg   = xEventGroupCreate();
    pub    = heap_caps_calloc(1, sizeof *pub, CAL_ALLOC_CAPS);
    s_tmp  = heap_caps_calloc(1, sizeof *s_tmp, CAL_ALLOC_CAPS);
    if (!s_lock || !s_eg || !pub || !s_tmp) { ESP_LOGE(TAG, "no memory for the lock or window"); unwind(pub); return; }

    // Idempotent: pomodoist_sync already initialises the cache partition on the disk; a
    // second init returns ESP_OK or ESP_ERR_NVS_NO_FREE_PAGES, both fine to ignore here.
    (void)nvs_flash_init_partition(CAL_CACHE_PARTITION);
    int cached = store_load(s_tmp);

    if (s_url[0] || s_refresh_token) {
        const esp_timer_create_args_t aa = { .callback = on_arm, .name = "cal_arm" };
        const esp_timer_create_args_t ra = { .callback = on_refresh, .name = "cal_refresh" };
        s_body = heap_caps_malloc(BODY_CAP, CAL_ALLOC_CAPS);
        bool ok = s_body
               && esp_timer_create(&aa, &s_arm) == ESP_OK
               && esp_timer_create(&ra, &s_refresh) == ESP_OK
               && esp_timer_start_periodic(s_refresh, REFRESH_US) == ESP_OK
               && xTaskCreate(sync_task, "cal_sync", FETCH_STACK, NULL, 4, &s_task) == pdPASS;
        // The handler goes last: a GOT_IP that lands now only arms s_arm, which exists.
        if (ok) ok = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_got_ip, NULL, NULL) == ESP_OK;
        if (!ok) { ESP_LOGE(TAG, "start failed (body, timer, task or handler); no calendar"); unwind(pub); return; }
    }

    // The task only touches s_tmp after a fetch bit, and the first bit is 60 s away at best.
    s_pub = pub;
    if (cached > 0) {
        publish(s_tmp);
        s_dirty = false;      // it came from NVS
        ESP_LOGI(TAG, "cache: %u days from %s", (unsigned)s_tmp->n_days, s_tmp->day[0].date);
    }
}

void cal_sync_start_google(const char *client, const char *secret, const char *refresh, const char *cal_id)
{
    if(s_lock) return;
    if(!client||!*client||!secret||!*secret||!refresh||!*refresh||!cal_id||!*cal_id||strlen(cal_id)>=sizeof s_cal) {
        ESP_LOGE(TAG,"Incomplete Google OAuth settings; cache only");cal_sync_start(NULL,NULL);return;
    }
    s_client=client;s_secret=secret;s_refresh_token=refresh;
    cal_sync_start(NULL,cal_id);
}

// ---------------------------------------------------------------- transport A: the laptop pushes over UART
// ADR-0017 MVP: the CYD has no Wi-Fi sync yet, so tools/push-cal.py fetches on the laptop and
// sends one line each:
//   CALT <epoch>                         set the clock (no Wi-Fi means no SNTP)
//   CALG <one events.list page as JSON>  repeated per page
//   CALE                                 end of window: publish and cache it
// Pages go through the same Google adapter as the Wi-Fi path, so privacy, declines, all-day
// and midnight have one implementation. The device never holds a Google token this way.
#include "driver/uart.h"
#include <sys/time.h>
#define SER_PORT UART_NUM_0   // the console UART: logs go out on TX, pushes come in on RX

static void serial_line(char *line, size_t n, bool *open)
{
    if (!strncmp(line, "CALT ", 5)) {
        long long t = strtoll(line + 5, NULL, 10);
        if (t > 1600000000LL) {
            struct timeval tv = { .tv_sec = (time_t)t };
            settimeofday(&tv, NULL);
            ESP_LOGI(TAG, "serial: clock set from laptop");
        }
    } else if (!strncmp(line, "CALG ", 5)) {
        if (!*open && !(*open = cal_google_begin(s_tmp, time(NULL)))) {
            ESP_LOGW(TAG, "serial: page before the clock is set; ignored");
            return;
        }
        char next[1024];
        if (!cal_google_page(line + 5, n - 5, s_tmp, next, sizeof next)) {
            ESP_LOGW(TAG, "serial: malformed page; window dropped, cache stands");
            *open = false;
        }
    } else if (!strcmp(line, "CALE")) {
        if (*open) accept_window(true);
        else ESP_LOGW(TAG, "serial: end without a window");
        *open = false;
    }
}

static void serial_task(void *arg)
{
    (void)arg;
    size_t n = 0;
    bool open = false, overflow = false;
    uint8_t chunk[256];
    ESP_LOGI(TAG, "serial: ready");   // push-cal.py waits for this line after a reset
    for (;;) {
        int got = uart_read_bytes(SER_PORT, chunk, sizeof chunk, pdMS_TO_TICKS(200));
        for (int i = 0; i < got; i++) {
            char c = (char)chunk[i];
            if (c == '\r') continue;
            if (c != '\n') {
                if (n + 1 < BODY_CAP) s_body[n++] = c; else overflow = true;
                continue;
            }
            s_body[n] = '\0';
            if (overflow) { ESP_LOGW(TAG, "serial: line over %u bytes; window dropped", (unsigned)BODY_CAP); open = false; }
            else if (n) serial_line(s_body, n, &open);
            n = 0; overflow = false;
        }
    }
}

void cal_sync_start_serial(void)
{
    cal_sync_start(NULL, NULL);          // cache and window, no network
    if (!s_pub || s_body) return;
    s_link = CAL_LINK_OK;                // the laptop is the link here; "reflash with secrets" would be wrong advice
    s_body = heap_caps_malloc(BODY_CAP, CAL_ALLOC_CAPS);
    if (!s_body || uart_driver_install(SER_PORT, 4096, 0, 0, NULL, 0) != ESP_OK
        || xTaskCreate(serial_task, "cal_serial", 6144, NULL, 4, &s_task) != pdPASS)
        ESP_LOGE(TAG, "serial: start failed; calendar shows the cache only");
}
