// Today's calendar: proportional time cards, two overlap lanes, and a bounded return to now.
// Horizontal swipes never change the day. Data/chime snapshots remain independent of the UI.
#if !CAL_FACE_RECT   // the rect board builds cal_face_rect.c instead (ADR-0017)
#include "cal_ui.h"
#include "cal_sync.h"
#include "cal_model.h"
#include "cal_timeline.h"
#if CAL_FACE_GLANCE
#include "cal_glance.h"
#endif
#include "trevos.h"
#include "trevos_ui.h"
#include "trevos_theme.h"
#include "esp_attr.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MEASURE     360
#define TOAST_W     250
#define TOAST_H     220
#define SNAP_MS   10000
#define SCROLL_PX    96
#define WHEEL_PX     (15*CAL_MINUTE_PX)   // one wheel detent = 15 minutes of timeline (A2)
#define AXIS_W       16
#define CAL_WHITE lv_color_hex(0xffffff)
#define CAL_INK lv_color_hex(0x111111)

// The puck's Cal is a glance face (face brief 3.6): one event per wheel detent. A value seam,
// set to 1 by the puck's CMake and pucksim only; the disk keeps the timeline below its #else.
#ifndef CAL_FACE_GLANCE
#define CAL_FACE_GLANCE 0
#endif

#ifndef TT_DEV_CAL_NOW
#define TT_DEV_CAL_NOW -1      // sim only: pin "now" in minutes since midnight so shots are stable
#endif

typedef enum { DAY_NOSYNC = 0, DAY_ABSENT, DAY_OK } day_state_t;

static EXT_RAM_BSS_ATTR cal_day_t s_day, s_scratch, s_chime_day;
static char        s_day_date[11], s_scratch_date[11];
static day_state_t s_day_state, s_scratch_state;
static cal_link_t  s_day_link, s_scratch_link, s_link_seen;   // link is only kept when there is no day to show
static uint32_t    s_gen = UINT32_MAX, s_chime_gen = UINT32_MAX;
static cal_chime_ring_t s_ring;
static char        s_chime_date[11];
static bool        s_chime_have;

static lv_obj_t *s_root, *s_clock, *s_date, *s_toast;
static uint32_t  s_scroll_at;                // lv_tick at the last input that moved the view; 0 = at rest
static bool s_date_contact;
static bool      s_rebuild_pending;

#if !CAL_FACE_GLANCE
static lv_obj_t *s_list, *s_nowline;
static lv_obj_t *s_rows[CAL_MAX_EVENTS];
static int       s_row_ev[CAL_MAX_EVENTS];
static int       s_nrows;
static int       s_view_min;
static int       s_target;                   // event index the snap targets (current, else next), -1 none
static bool      s_target_current;           // s_target is in progress, not upcoming
static cal_timeline_t s_timeline;
static lv_obj_t *s_info[CAL_MAX_EVENTS];
#endif

static void build(bool entrance);

// ---------------------------------------------------------------- time

static bool clock_ok(void)
{
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    return tm.tm_year + 1900 >= 2020;
}

static int now_min(void)
{
    if (TT_DEV_CAL_NOW >= 0) return TT_DEV_CAL_NOW;
    if (!clock_ok()) return -1;
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    return tm.tm_hour * 60 + tm.tm_min;
}

static bool today_date(char out[11])
{
    if (!clock_ok()) { out[0] = '\0'; return false; }
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    tm.tm_hour = 12; mktime(&tm);
    strftime(out, 11, "%Y-%m-%d", &tm);
    return true;
}

// Load the day being shown into scratch when the sync published or the date moved. Returns
// true when scratch differs from what is displayed, so a publish that did not touch this day
// is not a rebuild.
static bool ensure_day(void)
{
    char want[11];
    bool have_date = today_date(want);
    uint32_t gen = cal_sync_generation();
    cal_link_t lk = cal_sync_link();
    if (gen == s_gen && strcmp(want, s_scratch_date) == 0 && lk == s_link_seen) return false;
    memset(&s_scratch, 0, sizeof s_scratch);
    day_state_t st;
    if (!have_date) st = DAY_NOSYNC;
    else if (cal_sync_get_day(want, &s_scratch)) { cal_day_sort(&s_scratch); st = DAY_OK; }
    else st = gen == 0 ? DAY_NOSYNC : DAY_ABSENT;
    memcpy(s_scratch_date, want, 11);
    s_scratch_state = st;
    s_scratch_link = st == DAY_OK ? CAL_LINK_OK : lk;   // a cached day outranks the link message
    s_link_seen = lk;
    s_gen = gen;
    return st != s_day_state || s_scratch_link != s_day_link || strcmp(want, s_day_date) != 0 ||
           memcmp(&s_scratch, &s_day, sizeof s_day) != 0;
}

// The chime reads its own copy of today, refreshed on every publish, so it is right while
// Pomodoist is showing and the face's displayed copy is stale.
bool cal_chime_due(void)
{
    int nm = now_min();
    if (nm < 0) return false;
    char td[11];
    if (!today_date(td)) return false;
    if (strcmp(td, s_chime_date) != 0) { memset(&s_ring, 0, sizeof s_ring); memcpy(s_chime_date, td, 11); s_chime_gen = UINT32_MAX; }
    uint32_t gen = cal_sync_generation();
    if (gen != s_chime_gen) { s_chime_have = cal_sync_get_day(td, &s_chime_day); s_chime_gen = gen; }
    if (!s_chime_have) return false;
    return cal_day_chime_due(&s_chime_day, nm, &s_ring) > 0;
}

// ---------------------------------------------------------------- pieces

static void date_row_text(char *buf, size_t n);

static void update_rail(void)
{
    if(!s_clock) return;
    trev_rail_update(s_clock);
#ifndef ESP_PLATFORM
    if(TT_DEV_CAL_NOW>=0) {
        char rail[80],t[6];lv_strlcpy(rail,lv_label_get_text(s_clock),sizeof rail);
        cal_format_time(TT_DEV_CAL_NOW/60,TT_DEV_CAL_NOW%60,t);
        if(strlen(rail)>=5) { memcpy(rail,t,5);lv_label_set_text(s_clock,rail); }
    }
#endif
    char date[64],line[128];date_row_text(date,sizeof date);
    const char *rail=lv_label_get_text(s_clock);
    const char *battery=strrchr(rail,' ');
    const char *bat=battery && strchr(battery,'%') ? battery+1 : "";
    time_t synced, now=time(NULL);
    if(clock_ok() && cal_sync_last_sync(&synced) && now>=synced && now-synced>900) {
        unsigned age=(unsigned)((now-synced)/60);
        if(age>999) age=999;
        snprintf(line,sizeof line,"%.5s %.6s %um old %s",rail,strlen(date)>4 ? date+4:date,age,bat);
    } else snprintf(line,sizeof line,"%.5s   %.10s   %s",rail,date,bat);
#if CAL_FACE_GLANCE   // the puck has no battery part: the empty %s leaves trailing spaces that pull the centred line left
    for(size_t n=strlen(line);n && line[n-1]==' ';) line[--n]=0;
#endif
    if(s_date && strcmp(lv_label_get_text(s_date),line)) lv_label_set_text(s_date,line);

}



#if !CAL_FACE_GLANCE   // only the timeline's all-day strip joins titles; wrapped so the glance build has no unused static
// Append, cut at the buffer. strlcat without the format-truncation trap a snprintf join hits.
static void join(char *dst, size_t n, const char *src)
{
    size_t u = strlen(dst);
    if (u + 1 < n) lv_strlcpy(dst + u, src, n - u);
}
#endif

#define BUSY_TITLE "BUSY"                   // a private event, everywhere it shows (hero, toast, rows, strip)
static const char *title_of(const cal_event_t *e) { return e->hidden ? BUSY_TITLE : e->title; }

// Date portion of the single time/date/battery rail.
static void date_row_text(char *buf, size_t n)
{
    if (!s_day_date[0]) { lv_strlcpy(buf, "SETTING CLOCK", n); return; }
    struct tm tm = { 0 };
    tm.tm_year = (s_day_date[0]-'0')*1000 + (s_day_date[1]-'0')*100 + (s_day_date[2]-'0')*10 + (s_day_date[3]-'0') - 1900;
    tm.tm_mon  = (s_day_date[5]-'0')*10 + (s_day_date[6]-'0') - 1;
    tm.tm_mday = (s_day_date[8]-'0')*10 + (s_day_date[9]-'0');
    tm.tm_hour = 12; mktime(&tm);
    char dstr[16]; strftime(dstr, sizeof dstr, "%a %d %b", &tm);
    snprintf(buf, n, "%s", dstr);
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t fill)
{
    lv_obj_t *o=lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,fill,0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

// Same event colour family, tuned for large readable surfaces instead of thin tabs.
static lv_color_t fill_of(const cal_event_t *e)
{
    static const uint32_t fills[]={0xe8e8e8,0xe8e8e8,0xffd77a,0xff8585,0xffd77a,0xb98cf5,0x28d990};
    return lv_color_hex(e->hidden ? 0xe8e8e8 : fills[e->pal<7 ? e->pal : 5]);
}

#if !CAL_FACE_GLANCE
// Keep a long event's title in view while its time-proportional body scrolls underneath.
static void pin_card_text(void)
{
    if(!s_list) return;
    int top=lv_obj_get_scroll_y(s_list), origin=lv_obj_get_y(s_list);
    for(int i=0;i<s_nrows;i++) {
        lv_obj_t *r=s_rows[i], *info=s_info[i];
        int ih=lv_obj_get_height(info), ry=lv_obj_get_y(r);
        int pad=lv_obj_get_height(r)<70 ? 4 : 12;
        int y=top-ry+pad;
        int limit=lv_obj_get_height(r)-ih-pad;
        if(y>limit) y=limit;
        if(y<pad) y=pad;
        int screen_y=origin+ry-top+y;
        // Card colour reaches the bezel; every line of text stays inside the circle.
        int far=abs(screen_y-180);
        if(abs(screen_y+ih-180)>far) far=abs(screen_y+ih-180);
        if(far>=164) {lv_obj_add_flag(info,LV_OBJ_FLAG_HIDDEN);continue;}
        int inset=180-(int)sqrtf(164*164-far*far);
        int rx=lv_obj_get_x(r),rw=lv_obj_get_width(r);
        int left=inset>rx+12 ? inset-rx : 12;
        int right=360-inset<rx+rw-12 ? 360-inset-rx : rw-12;
        if(right-left<52) {lv_obj_add_flag(info,LV_OBJ_FLAG_HIDDEN);continue;}
        lv_obj_remove_flag(info,LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(info,left,y);lv_obj_set_width(info,right-left);
        for(uint32_t j=0;j<lv_obj_get_child_count(info);j++) lv_obj_set_width(lv_obj_get_child(info,j),right-left);
    }
    if(s_nowline) lv_obj_invalidate(s_nowline);
}

// One foreground child, native dashed drawing; no layer buffer or object per dot.
static void draw_now(lv_event_t *event)
{
    lv_obj_t *obj=lv_event_get_target_obj(event);
    lv_area_t a;lv_obj_get_coords(obj,&a);
    int y=a.y1+1,dy=abs(y-180)+2;
    if(dy>=176) return;
    int inset=180-(int)sqrtf(176*176-dy*dy);
    bool clear[360]={0};
    for(int x=inset;x<360-inset;x++) clear[x]=true;
    for(int i=0;i<s_nrows;i++) {
        if(!lv_obj_is_visible(s_info[i])) continue;
        for(uint32_t j=0;j<lv_obj_get_child_count(s_info[i]);j++) {
            lv_obj_t *label=lv_obj_get_child(s_info[i],j);
            if(!lv_obj_is_visible(label)) continue;
            lv_area_t t;lv_obj_get_coords(label,&t);
            lv_point_t text_size;
            lv_text_get_size(&text_size,lv_label_get_text(label),lv_obj_get_style_text_font(label,0),
                             lv_obj_get_style_text_letter_space(label,0),lv_obj_get_style_text_line_space(label,0),
                             lv_obj_get_width(label),LV_TEXT_FLAG_NONE);
            if(text_size.y<lv_area_get_height(&t)) t.y2=t.y1+text_size.y-1;
            if(text_size.x<lv_area_get_width(&t)) t.x2=t.x1+text_size.x-1;
            if(y+2<t.y1-2 || y-2>t.y2+2) continue;
            int left=t.x1-2<0 ? 0:t.x1-2,right=t.x2+2>359 ? 359:t.x2+2;
            for(int x=left;x<=right;x++) clear[x]=false;
        }
    }
    lv_layer_t *layer=lv_event_get_layer(event);
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);
    d.dash_width=2;d.dash_gap=3;
    for(int x=0;x<360;) {
        while(x<360 && !clear[x]) x++;
        int start=x;while(x<360 && clear[x]) x++;
        // Anchor the dash phase in screen coordinates across text exclusions.
        start=((start+4)/5)*5;
        if(start+2>=x) continue;
        d.p1=(lv_point_precise_t){start,y};d.p2=(lv_point_precise_t){x-1,y};
        d.color=CAL_WHITE;d.width=4;lv_draw_line(layer,&d);
        d.color=CAL_INK;d.width=2;lv_draw_line(layer,&d);
    }
}

static void on_scroll(lv_event_t *e) { (void)e; pin_card_text(); }

static lv_obj_t *row(lv_obj_t *parent, const cal_event_t *e, const cal_timebox_t *b, int nm)
{
    (void)nm;
    int w=(MEASURE-2*AXIS_W-(b->lanes-1)*6)/b->lanes;
    lv_color_t color=fill_of(e);
    lv_obj_t *r=box(parent, AXIS_W+b->lane*(w+6), b->y, w, b->h-4,color);
    lv_obj_set_style_radius(r,18,0);
    lv_obj_t *info=lv_obj_create(r);lv_obj_remove_style_all(info);
    lv_obj_remove_flag(info,LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    int ih=b->h>=100 ? 70 : 34;
    lv_obj_set_size(info,w-18,ih);lv_obj_set_pos(info,12,12);
    char a[6],z[6],when[16];cal_format_time(e->sh,e->sm,a);cal_format_time(e->eh,e->em,z);
    snprintf(when,sizeof when,"%s-%s",a,z);
    lv_obj_t *time=tt_label(info,when,TT_F_TINY,CAL_INK,0);
    lv_obj_set_width(time,w-18);lv_obj_set_height(time,12);
    lv_label_set_long_mode(time,LV_LABEL_LONG_DOT);
    lv_obj_t *title=tt_label(info,title_of(e),b->lanes==1 && ih>34 ? TT_F_TITLE : TT_F_TILE,CAL_INK,0);
    lv_obj_set_pos(title,0,16);lv_obj_set_size(title,w-18,ih>34 ? 50 : 18);
    lv_label_set_long_mode(title,LV_LABEL_LONG_DOT);
    s_info[s_nrows]=info;
    return r;
}
#endif

// No bounded scroll_by in this LVGL's headers; clamp against the object's own scroll limits.
static void scroll_clamped(lv_obj_t *obj, int32_t dy)
{
    if (!obj) return;
    lv_obj_scroll_to_y(obj,lv_obj_get_scroll_y(obj),LV_ANIM_OFF);
    int32_t top = lv_obj_get_scroll_top(obj), bottom = lv_obj_get_scroll_bottom(obj);
    if (bottom < 0) bottom = 0;
    if (top < 0) top = 0;
    if (dy < 0 && -dy > bottom) dy = -bottom;
    if (dy > 0 && dy > top) dy = top;
    if (dy) lv_obj_scroll_by(obj, 0, dy, LV_ANIM_OFF);
}

#if !CAL_FACE_GLANCE
static void snap_to_now(bool anim)
{
    if(s_toast) return;
    s_scroll_at=0;
    if(!s_list) return;
    lv_obj_update_layout(s_list);
    int nm=now_min(),y=nm>=0 ? cal_time_y(nm)-96 : 0;
    s_view_min=nm;
    if(s_timeline.agenda) {
        y=0;
        for(int i=0;i<s_nrows;i++) if(s_row_ev[i]==s_target) y=s_timeline.box[i].y;
        if(s_target<0 && s_nrows) y=s_timeline.box[s_nrows-1].y;
    }
    int max=s_timeline.height-lv_obj_get_height(s_list);
    if(y>max) y=max;
    if(y<0) y=0;
    lv_obj_scroll_to_y(s_list,y,anim ? LV_ANIM_ON : LV_ANIM_OFF);
    pin_card_text();
}
#endif

static void schedule_rebuild(void)
{
#ifndef ESP_PLATFORM
    if (!s_rebuild_pending && (s_toast || s_scroll_at)) fprintf(stderr, "[cal] rebuild deferred\n");
#endif
    s_rebuild_pending = true;
}

static void toast_close(void)
{
    if (!s_toast) return;
    lv_obj_delete(s_toast);
    s_toast = NULL;
    // ponytail: the idle clocks count from the last input, so a toast read past SNAP_MS does
    // not close into an instant snap.
    uint32_t t = lv_tick_get();
    s_scroll_at = t ? t : 1;
}

static void toast_open(const cal_event_t *e)
{
    if (!s_root || s_toast || !e) return;
    const tt_skin_t *sk = tt_skin_paper();
    const int PAD = 16, TEXT_W = TOAST_W - 2 * PAD;

    s_toast = lv_obj_create(s_root);
    lv_obj_set_size(s_toast, TOAST_W, TOAST_H);
    lv_obj_center(s_toast);
    lv_obj_set_style_bg_color(s_toast, CAL_WHITE, 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_toast, TT_SLATE, 0);
    lv_obj_set_style_border_width(s_toast, 1, 0);
    lv_obj_set_style_border_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_toast, 16, 0);
    lv_obj_set_style_pad_all(s_toast, PAD, 0);
    lv_obj_set_style_pad_row(s_toast, 8, 0);
    lv_obj_set_flex_flow(s_toast, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_toast, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_toast, LV_SCROLLBAR_MODE_AUTO);

    char when[24];
    if (e->allday) lv_strlcpy(when, "ALL DAY", sizeof when);
    else {
        char a[6], b[6]; cal_format_time(e->sh, e->sm, a); cal_format_time(e->eh, e->em, b);
        snprintf(when, sizeof when, "%s - %s", a, b);      // ASCII hyphen: the mono subset has no en dash
    }
    lv_obj_t *eb = tt_eyebrow(s_toast, when, sk->muted);
    lv_obj_set_width(eb, TEXT_W);

    lv_obj_t *ti = tt_title(s_toast, title_of(e), sk->ink);
    lv_obj_set_width(ti, TEXT_W);
    lv_label_set_long_mode(ti, LV_LABEL_LONG_WRAP);

    if (!e->hidden && e->loc[0]) {
        lv_obj_t *lo = tt_label(s_toast, e->loc, TT_F_BODY, sk->muted, 0);
        lv_obj_set_width(lo, TEXT_W);
        lv_label_set_long_mode(lo, LV_LABEL_LONG_WRAP);
    }
    if (!e->hidden && e->desc[0]) {                   // no filler when absent (D10)
        lv_obj_t *de = tt_label(s_toast, e->desc, TT_F_BODY, TT_DESC, 0);
        lv_obj_set_width(de, TEXT_W);
        lv_label_set_long_mode(de, LV_LABEL_LONG_WRAP);
    }
#ifndef ESP_PLATFORM
    fprintf(stderr,"[cal] toast event=%d\n",(int)(e-s_day.ev));
#endif
    tt_anim_fade_in(s_toast, 160, 0);               // no-op under TT_NO_LAYER_FX=1; lifted in the design session
}

// ---------------------------------------------------------------- the face

static lv_obj_t *build_header(const tt_skin_t *sk)
{
    (void)sk;
    s_clock=tt_label(s_root,"",TT_F_TINY,CAL_INK,0);
    lv_obj_add_flag(s_clock,LV_OBJ_FLAG_HIDDEN);trev_rail_init(s_clock);
    s_date=tt_label(s_root,"",TT_F_TINY,CAL_INK,0);
    lv_obj_set_pos(s_date,66,38);lv_obj_set_size(s_date,228,16);
    lv_obj_set_style_text_align(s_date,LV_TEXT_ALIGN_CENTER,0);
    return s_date;
}

#if !CAL_FACE_GLANCE   // the glance has its own centred state faces (state_face, link_face)
static void build_state(const tt_skin_t *sk,const char *txt)
{
    lv_obj_t *l=tt_label(s_root,txt,TT_F_TITLE,sk->ink,0);
    lv_obj_set_width(l,220);lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(l);
}
#endif

#if !CAL_FACE_GLANCE
static void select_target(int nm)
{
    int cur=nm>=0 ? cal_day_current_at(&s_day,nm) : -1;
    int next=nm>=0 ? cal_day_next_at(&s_day,nm) : -1;
    s_target=cur>=0 ? cur : next;s_target_current=cur>=0;
}

static void build_list(int nm,int list_y)
{
    cal_timeline_build(&s_day,&s_timeline);
    s_list=lv_obj_create(s_root);lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list,MEASURE,360-list_y);
    lv_obj_align(s_list,LV_ALIGN_TOP_MID,0,list_y);
    lv_obj_add_flag(s_list,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_list,LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scroll_dir(s_list,LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list,LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_list,TT_SLATE,LV_PART_SCROLLBAR);
    lv_obj_add_event_cb(s_list,on_scroll,LV_EVENT_SCROLL,NULL);
    lv_obj_t *extent=box(s_list,0,s_timeline.height-1,1,1,TT_PAPER);
    (void)extent;
    if(!s_timeline.agenda) {
        for(int hour=0;hour<24;hour++) {
            int y=cal_time_y(hour*60);
            box(s_list,AXIS_W,y,MEASURE-AXIS_W,1,lv_color_hex(0xededed));

        }
    }
    for(int i=0;i<s_timeline.n;i++) {
        const cal_timebox_t *b=&s_timeline.box[i];
        s_rows[s_nrows]=row(s_list,&s_day.ev[b->event],b,nm);
        s_row_ev[s_nrows++]=b->event;
    }
    if(!s_timeline.agenda) {
        s_nowline=lv_obj_create(s_list);lv_obj_remove_style_all(s_nowline);
        lv_obj_set_pos(s_nowline,0,cal_time_y(nm<0 ? 0:nm)-1);
        lv_obj_set_size(s_nowline,MEASURE,4);
        lv_obj_remove_flag(s_nowline,LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(s_nowline,draw_now,LV_EVENT_DRAW_MAIN,NULL);
        if(nm<0) lv_obj_add_flag(s_nowline,LV_OBJ_FLAG_HIDDEN);
    }
}

static void build(bool entrance)
{
    if (!s_root) return;
    memcpy(&s_day, &s_scratch, sizeof s_day);       // codex 3: the displayed snapshot is taken here, once
    memcpy(s_day_date, s_scratch_date, sizeof s_day_date);
    s_day_state = s_scratch_state;
    s_day_link = s_scratch_link;
    s_rebuild_pending = false;

    lv_obj_clean(s_root);
    s_clock = s_date = s_list = s_nowline = s_toast = NULL;
    s_nrows = 0; s_target = -1; s_target_current = false;
    const tt_skin_t *sk = tt_skin_paper();
    tt_face_ground(s_root, sk);
    lv_obj_set_style_bg_color(s_root,CAL_WHITE,0);
    lv_obj_t *hd = build_header(sk);

    int nm = now_min();
    if(s_day_link==CAL_LINK_NONE) build_state(sk,"Not linked. Reflash with calendar secrets.");
    else if(s_day_link==CAL_LINK_EXPIRED) build_state(sk,"Calendar link expired. Mint a new token and reflash.");
    else if(s_day_state==DAY_NOSYNC) build_state(sk,clock_ok() ? "Waiting for calendar" : "Setting clock");
    else if(s_day_state==DAY_ABSENT) build_state(sk,"No data for today");
    else if(!s_day.n) build_state(sk,s_day.more ? "Calendar incomplete" : "Nothing scheduled");
    else {
        select_target(nm);
        int y=66;char all[128]="";
        for(int i=0;i<s_day.n;i++) if(s_day.ev[i].allday) {
            if(all[0]) join(all,sizeof all," / ");
            else join(all,sizeof all,"ALL DAY  ");
            join(all,sizeof all,title_of(&s_day.ev[i]));
        }
        if(all[0]) {
            lv_obj_t *strip=box(s_root,72,62,216,24,lv_color_hex(0xe8e8e8));
            lv_obj_set_style_radius(strip,6,0);
            lv_obj_t *l=tt_label(strip,all,TT_F_TINY,TT_SLATE,0);
            lv_obj_set_pos(l,8,5);lv_obj_set_size(l,200,12);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);
            y=92;
        }
        if(cal_day_next_at(&s_day,0)>=0) build_list(nm,y);
        else build_state(sk,"All day");
    }

    snap_to_now(false);                              // also sets the verb
    update_rail();
    if (entrance) tt_face_enter(s_root,hd,s_rows,s_nrows);
#ifndef ESP_PLATFORM
    // shoot.sh reads this once to aim TAPC at a row: the first row whose centre is in view.
    static bool s_row0_told;
    if (!s_row0_told && s_nrows && s_list) {
        lv_area_t lc; lv_obj_get_coords(s_list, &lc);
        for (int i = 0; i < s_nrows; i++) {
            lv_area_t c; lv_obj_get_coords(s_rows[i], &c);
            int32_t cy = (c.y1 + c.y2) / 2;
            int32_t top=c.y1>lc.y1 ? c.y1:lc.y1, bottom=c.y2<lc.y2 ? c.y2:lc.y2;
            cy=(top+bottom)/2;
            if (bottom-top>=8) { fprintf(stderr, "[cal] row0 x=%d y=%d\n", (int)((c.x1+c.x2)/2),(int)cy); s_row0_told = true; break; }
        }
    }
#endif
}

static void go_today(void) { toast_close();snap_to_now(true); }

static void cal_on_start(trev_app_t *app, lv_obj_t *root)
{
    (void)app;
    s_root = root; s_date_contact=false; s_scroll_at = 0; s_rebuild_pending = false;
    s_gen = UINT32_MAX;                              // codex 5: the first ensure_day always loads
    ensure_day();
    build(true);
}

static void cal_on_stop(trev_app_t *a)
{
    (void)a;
    s_root = s_clock = s_date = s_list = s_nowline = s_toast = NULL;
    s_nrows = 0;
}

static void cal_on_tick(trev_app_t *a, uint32_t now_ms)
{
    (void)a; (void)now_ms;
    if (!s_root) return;
    uint32_t idle=trev_idle_ms();
    if(s_date_contact && idle>350) s_date_contact=false;
    if(ensure_day()) {
        if(strcmp(s_scratch_date,s_day_date)) {
            s_date_contact=idle<350;s_scroll_at=0;build(false);return;
        }
        schedule_rebuild();
    } else if(s_rebuild_pending && s_scratch_state==s_day_state && s_scratch_link==s_day_link &&
              !memcmp(&s_scratch,&s_day,sizeof s_day)) s_rebuild_pending=false;
    update_rail();
    int nm=now_min();
    select_target(nm);
    if(s_nowline) {
        if(nm<0) lv_obj_add_flag(s_nowline,LV_OBJ_FLAG_HIDDEN);
        else {
            lv_obj_remove_flag(s_nowline,LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_y(s_nowline,cal_time_y(nm)-1);
        }
        pin_card_text();
    }
    if(s_toast) return; // Reading ends explicitly, or at the hard midnight boundary.
    bool browsing=s_scroll_at && (lv_tick_elaps(s_scroll_at)<=SNAP_MS || idle<=SNAP_MS);
    if(s_rebuild_pending && !browsing && idle>350) {build(false);return;}
    if(s_scroll_at && !browsing) snap_to_now(true);
    else if(!s_scroll_at && idle>SNAP_MS && nm!=s_view_min) snap_to_now(false);
}

// A2: the wheel scrolls the timeline 15 minutes per detent, NEXT = later. Any input closes the toast.
static void cal_on_turn(trev_app_t *a, trev_turn_t dir)
{
    (void)a;
    toast_close();
    if(!s_list) return;
#ifndef ESP_PLATFORM
    int before=lv_obj_get_scroll_y(s_list);
#endif
    scroll_clamped(s_list,-(int)dir*WHEEL_PX);
#ifndef ESP_PLATFORM
    fprintf(stderr,"[cal] turn dir=%d moved=%d\n",(int)dir,(int)lv_obj_get_scroll_y(s_list)-before);
#endif
    s_scroll_at=lv_tick_get() ? lv_tick_get() : 1;      // the 10 s snap-back restarts, as after a drag
}

static void cal_on_commit(trev_app_t *a)
{
    (void)a;
    if(s_date_contact) return;
    if (s_toast) { toast_close(); return; }
    go_today();
}

static void cal_on_gesture(trev_app_t *a, trev_gesture_t g)
{
    (void)a;
    if(s_date_contact) return;
    if (s_toast) {
        switch (g) {
        case TREV_GESTURE_SWIPE_UP:   scroll_clamped(s_toast, -140); return;
        case TREV_GESTURE_SWIPE_DOWN: scroll_clamped(s_toast,  140); return;
        case TREV_GESTURE_FACE_DOWN: case TREV_GESTURE_FACE_UP: return;
        default: toast_close(); return;              // tap, or a sideways swipe: closes
        }
    }
    switch (g) {
    case TREV_GESTURE_SWIPE_UP:
        if (s_list) { scroll_clamped(s_list, -SCROLL_PX); s_scroll_at = lv_tick_get(); }
        break;
    case TREV_GESTURE_SWIPE_DOWN:
        if (s_list) { scroll_clamped(s_list,  SCROLL_PX); s_scroll_at = lv_tick_get(); }
        break;
    case TREV_GESTURE_SWIPE_LEFT: case TREV_GESTURE_SWIPE_RIGHT: break; // today only
    case TREV_GESTURE_TAP_CONTENT: {
        int x, y; trev_last_tap(&x, &y);
        if (!s_list) break;
        lv_area_t lc; lv_obj_get_coords(s_list, &lc);
        if (y < lc.y1 || y > lc.y2 || x < lc.x1 || x > lc.x2) break;   // outside the visible list: rows scrolled away still have coords (F5)
        for (int i = 0; i < s_nrows; i++) {
            lv_area_t c; lv_obj_get_coords(s_rows[i], &c);
            if (x >= c.x1 && x <= c.x2 && y >= c.y1 && y <= c.y2) { toast_open(&s_day.ev[s_row_ev[i]]); return; }
        }
        break;
    }
    default: break;
    }
}

static void cal_on_drag(trev_app_t *a,int dx,int dy)
{
    (void)a;(void)dx;
    if(s_date_contact) return;
    lv_obj_t *target=s_toast ? s_toast : s_list;
#ifndef ESP_PLATFORM
    int before=target ? lv_obj_get_scroll_y(target) : 0;
#endif
    scroll_clamped(target,dy);
#ifndef ESP_PLATFORM
    fprintf(stderr,"[cal] drag dy=%d moved=%d\n",dy,target ? (int)lv_obj_get_scroll_y(target)-before : 0);
#endif
    if(!s_toast) s_scroll_at=lv_tick_get() ? lv_tick_get() : 1;
}

const trev_app_def_t CAL_APP = {
    .api_version = TREV_APP_API_VERSION,
    .id = "cal", .name = "Cal",
    .on_start = cal_on_start, .on_stop = cal_on_stop, .on_tick = cal_on_tick,
    .on_turn = cal_on_turn, .on_commit = cal_on_commit, .on_gesture = cal_on_gesture, .on_drag = cal_on_drag,
};

#ifndef ESP_PLATFORM
#include "test_cal_face.inc"
#endif

#else  // CAL_FACE_GLANCE
// The puck's face (face brief 3.6, cal-glance.dc.html): today's timed events one at a time on a
// hero card, the next one under it, and the wheel steps through them. The rules (order, where
// the wheel rests, the NOW/NEXT/IN/ENDED label) are pure and host-tested in cal_glance.c.
static int8_t    s_idx[CAL_MAX_EVENTS];      // today's timed events, in display order (indexes into s_day)
static int       s_n, s_pos, s_rest;         // list length, the position shown, where the wheel rests
static int       s_min_shown = -1;           // the minute the labels were built for
static lv_obj_t *s_hero, *s_set;          // s_set = hero + supporting row, the part that slides with a detent (5.12.2)

static int text_w(const char *t, const lv_font_t *f, int track)
{
    lv_point_t p;
    lv_text_get_size(&p, t, f, track, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return p.x;
}

// One line of CAL_INK text, x from the parent's left, its vertical centre at cy. The Plex bitmaps'
// line heights differ from the canvas's CSS, so rows are placed by centre, not by top edge.
static lv_obj_t *line(lv_obj_t *parent, const char *txt, const lv_font_t *f, int x, int cy, int w, int track)
{
    lv_obj_t *l = tt_label(parent, txt, f, CAL_INK, track);
    int lh = lv_font_get_line_height(f);
    lv_obj_set_size(l, w, lh);
    lv_obj_set_pos(l, x, cy - lh / 2);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    return l;
}

// The strip: "Calendar incomplete" when the day was cut at CAL_MAX_EVENTS (cal-glance-incomplete),
// else the first all-day title.
static void build_strip(void)
{
    const char *title = NULL;
    if (s_day.more) title = "Calendar incomplete";
    else for (int i = 0; i < s_day.n && !title; i++) if (s_day.ev[i].allday) title = title_of(&s_day.ev[i]);
    if (!title) return;
    lv_obj_t *strip = box(s_root, 72, 62, 216, 22, lv_color_hex(0xe8e8e8));
    lv_obj_set_style_radius(strip, 6, 0);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(strip, 10, 0);
    if (!s_day.more) tt_label(strip, "ALL DAY", TT_F_TINY, CAL_INK, 0);
    int w = text_w(title, TT_F_BODY, 0);
    if (w > 150) w = 150;
    lv_obj_t *t = tt_label(strip, title, TT_F_BODY, CAL_INK, 0);
    lv_obj_set_size(t, w, lv_font_get_line_height(TT_F_BODY));
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
}

// A centred CAL_INK label of explicit lines (the "\n" is the break, never LVGL's wrap), its
// vertical centre at cy, 300 wide so nothing re-wraps and every glyph stays inside r=168.
static void centred(const char *txt, const lv_font_t *f, int cy, int pitch)
{
    int lh = lv_font_get_line_height(f), n = 1;
    for (const char *c = txt; *c; c++) if (*c == '\n') n++;
    int gap = pitch ? pitch - lh : 0, h = n * lh + (n - 1) * gap;
    lv_obj_t *l = tt_label(s_root, txt, f, CAL_INK, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_line_space(l, gap, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(l, 300, h);
    lv_obj_set_pos(l, 30, cy - h / 2);
}

// nothing scheduled, waiting, setting clock, no data, all day, incomplete: one title at the centre
static void state_face(const char *txt) { centred(txt, TT_F_TITLE, 180, 0); }

// nolink / expired (cal-nolink, cal-expired): title cy 154, body cy 199 on a 22 px pitch
static void link_face(const char *title, const char *body)
{
    centred(title, TT_F_TITLE, 154, 0);
    centred(body, TT_F_TILE, 199, 22);
}

static void ty_exec(void *o, int32_t y) { lv_obj_set_style_translate_y((lv_obj_t *)o, y, 0); }

static void build_cards(int nm)
{
    int ev = s_idx[s_pos];
    const cal_event_t *e = &s_day.ev[ev];
    char lab[32], a[6], z[6], when[16];
    cal_glance_label(&s_day, ev, nm, lab);
    cal_format_time(e->sh, e->sm, a); cal_format_time(e->eh, e->em, z);
    snprintf(when, sizeof when, "%s-%s", a, z);          // ASCII hyphen: the mono subset has no en dash

    // The set is the union of the hero (48,100 264x112) and the row (56,228 248x48), so every rest pixel stays put.
    s_set = box(s_root, 48, 100, 264, 176, lv_color_hex(0));
    lv_obj_set_style_bg_opa(s_set, LV_OPA_TRANSP, 0);
    s_hero = box(s_set, 0, 0, 264, 112, fill_of(e));
    lv_obj_set_style_radius(s_hero, 28, 0);
    line(s_hero, lab, TT_F_LABEL, 24, 30, 216, 1);
    line(s_hero, title_of(e), TT_F_TITLE, 24, 56, 216, 0);
    line(s_hero, when, TT_F_LABEL, 24, 82, 216, 0);

    if (s_pos + 1 < s_n) {
        const cal_event_t *e2 = &s_day.ev[s_idx[s_pos + 1]];
        lv_obj_t *r = box(s_set, 8, 128, 248, 48, fill_of(e2));
        lv_obj_set_style_radius(r, 24, 0);
        cal_format_time(e2->sh, e2->sm, a);
        int tw = text_w(a, TT_F_LABEL, 0), x = 20 + tw + 12;
        line(r, a, TT_F_LABEL, 20, 24, tw, 0);
        line(r, title_of(e2), TT_F_TILE, x, 24, 248 - 20 - x, 0);
    }
    lv_obj_t *pos;
    tt_readout(s_root, 300, CAL_INK, &pos);
    lv_label_set_text_fmt(pos, "%d / %d", s_pos + 1, s_n);
}

static void build(bool entrance)
{
    if (!s_root) return;
    memcpy(&s_day, &s_scratch, sizeof s_day);       // the displayed snapshot is taken here, once
    memcpy(s_day_date, s_scratch_date, sizeof s_day_date);
    s_day_state = s_scratch_state;
    s_day_link = s_scratch_link;
    s_rebuild_pending = false;

    lv_obj_clean(s_root);
    s_clock = s_date = s_toast = s_hero = s_set = NULL;
    s_n = 0;
    const tt_skin_t *sk = tt_skin_paper();
    tt_face_ground(s_root, sk);
    lv_obj_set_style_bg_color(s_root, CAL_WHITE, 0);
    lv_obj_t *hd = build_header(sk);
    lv_obj_set_style_text_font(s_date, TT_F_STATUS, 0);   // 13 mono at top 30, not the timeline's 11 at 38
    lv_obj_set_y(s_date, 30);

    s_min_shown = now_min();
    int nm = s_min_shown < 0 ? 0 : s_min_shown;
    if(s_day_link==CAL_LINK_NONE) link_face("Not linked.","Reflash with\ncalendar secrets.");
    else if(s_day_link==CAL_LINK_EXPIRED) link_face("Calendar link expired.","Mint a new token\nand reflash.");
    else if(s_day_state==DAY_NOSYNC) state_face(clock_ok() ? "Waiting for calendar" : "Setting clock");
    else if(s_day_state==DAY_ABSENT) state_face("No data for today");
    else {
        s_n = cal_glance_list(&s_day, nm, s_idx, &s_rest);
        if (!s_scroll_at || s_pos >= s_n) s_pos = s_rest;   // at rest, or the list shrank under the wheel
        if (s_pos < 0) s_pos = 0;
        if (s_n) { build_strip(); build_cards(nm); }
        else if (s_day.more) state_face("Calendar incomplete");   // nothing timed, and the day was cut
        else {
            bool allday = false;
            for (int i = 0; i < s_day.n; i++) allday |= s_day.ev[i].allday;
            if (allday) build_strip();
            state_face(allday ? "All day" : "Nothing scheduled");
        }
    }
    update_rail();
    if (entrance) tt_face_enter(s_root, s_hero ? s_hero : hd, NULL, 0);
}

static void cal_on_start(trev_app_t *app, lv_obj_t *root)
{
    (void)app;
    s_root = root; s_date_contact = false; s_scroll_at = 0; s_rebuild_pending = false; s_pos = 0;
    s_gen = UINT32_MAX;                              // the first ensure_day always loads
    ensure_day();
    build(true);
}

static void cal_on_stop(trev_app_t *a)
{
    (void)a;
    s_root = s_clock = s_date = s_toast = s_hero = s_set = NULL;
    s_n = 0;
}

static void cal_on_tick(trev_app_t *a, uint32_t now_ms)
{
    (void)a; (void)now_ms;
    if (!s_root) return;
    uint32_t idle = trev_idle_ms();
    if (s_date_contact && idle > 350) s_date_contact = false;
    if (ensure_day()) {
        if (strcmp(s_scratch_date, s_day_date)) { s_date_contact = idle < 350; s_scroll_at = 0; build(false); return; }
        schedule_rebuild();
    } else if (s_rebuild_pending && s_scratch_state == s_day_state && s_scratch_link == s_day_link &&
               !memcmp(&s_scratch, &s_day, sizeof s_day)) s_rebuild_pending = false;
    update_rail();
    if (s_toast) return;                             // reading defers every rebuild
    bool browsing = s_scroll_at && (lv_tick_elaps(s_scroll_at) <= SNAP_MS || idle <= SNAP_MS);
    if (s_scroll_at && !browsing) {                  // idle: back to where the wheel rests
        s_scroll_at = 0;
        if (s_pos != s_rest) { build(false); return; }
    }
    if (s_rebuild_pending || now_min() != s_min_shown) build(false);   // the labels count minutes
}

// One detent = one event, clamped to the list. Returns whether anything visible changed: the
// board ticks only on true (trev_app.h on_wheel, "felt"), so a detent at either end or on a
// state face (nothing to step) is absorbed silently. An open toast closes, which is visible.
static bool glance_step(int dir)
{
    bool closed = s_toast != NULL;
    toast_close();
    if (!s_n) return closed;
    int p = s_pos + dir;
    if (p < 0) p = 0;
    if (p > s_n - 1) p = s_n - 1;
    bool moved = p != s_pos;
    int32_t live = tt_step_live(s_set, ty_exec, 0);    // before build() deletes the old set (and its tween)
    s_pos = p;
    s_scroll_at = lv_tick_get() ? lv_tick_get() : 1;   // the 10 s snap-back restarts
    if (moved) {
        build(false);
        // From the detent's side (down enters from below, up from above); a detent mid-slide restarts from its live offset.
        if (s_set) tt_step(s_set, ty_exec, LV_CLAMP(-16, live + 16 * dir, 16), 0, TT_STEP_MS, NULL);
#ifndef ESP_PLATFORM
        fprintf(stderr, "[cal] glance %d/%d\n", s_pos + 1, s_n);
#endif
    }
    return moved || closed;
}

// The wheel proper: "felt" is what glance_step returns.
static bool cal_on_wheel(trev_app_t *a, trev_turn_t dir) { (void)a; return glance_step((int)dir); }

// The left pill's PREV arrives here (trev_input_turn), not through on_wheel.
static void cal_on_turn(trev_app_t *a, trev_turn_t dir) { (void)a; glance_step((int)dir); }

static void cal_on_commit(trev_app_t *a)
{
    (void)a;
    if (s_date_contact) return;
    if (s_toast) { toast_close(); return; }
    s_scroll_at = 0;
    if (s_pos != s_rest) build(false);
}

static void cal_on_gesture(trev_app_t *a, trev_gesture_t g)
{
    (void)a;
    if (s_date_contact) return;
    if (s_toast) {
        switch (g) {
        case TREV_GESTURE_SWIPE_UP:   scroll_clamped(s_toast, -140); return;
        case TREV_GESTURE_SWIPE_DOWN: scroll_clamped(s_toast,  140); return;
        case TREV_GESTURE_FACE_DOWN: case TREV_GESTURE_FACE_UP: return;
        default: toast_close(); return;              // tap, or a sideways swipe: closes
        }
    }
    if (g != TREV_GESTURE_TAP_CONTENT || !s_hero || !s_n) return;
    int x, y; trev_last_tap(&x, &y);
    lv_area_t c; lv_obj_get_coords(s_hero, &c);
    if (x >= c.x1 && x <= c.x2 && y >= c.y1 && y <= c.y2) toast_open(&s_day.ev[s_idx[s_pos]]);
}

// A drag scrolls the toast; otherwise a no-op that still makes trev_has_direct_touch() true, so
// every release is a content tap (the ring's E1 comment).
static void cal_on_drag(trev_app_t *a, int dx, int dy)
{
    (void)a; (void)dx;
    if (s_date_contact || !s_toast) return;
    scroll_clamped(s_toast, dy);
}

const trev_app_def_t CAL_APP = {
    .api_version = TREV_APP_API_VERSION,
    .id = "cal", .name = "Cal",
    .on_start = cal_on_start, .on_stop = cal_on_stop, .on_tick = cal_on_tick,
    .on_turn = cal_on_turn, .on_wheel = cal_on_wheel, .on_commit = cal_on_commit,
    .on_gesture = cal_on_gesture, .on_drag = cal_on_drag,
};

#ifndef ESP_PLATFORM
// sim_main.c calls this for SIM_CAL_POLISH_TEST; the polish test drives the timeline's statics.
void cal_polish_test(void) { fprintf(stderr, "cal_polish_test: timeline build only\n"); }
#endif
#endif // CAL_FACE_GLANCE
#endif // !CAL_FACE_RECT
