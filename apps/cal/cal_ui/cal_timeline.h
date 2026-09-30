// Pixel layout for one day. Shared by the LVGL face and the host regression check.
#pragma once
#include "cal_model.h"

typedef struct { int event, y, h, lane, lanes; } cal_timebox_t;
typedef struct { int n, height; bool agenda; cal_timebox_t box[CAL_MAX_EVENTS]; } cal_timeline_t;

#define CAL_MINUTE_PX 2
#define CAL_TIME_PAD 16
static inline int cal_time_y(int minute) { return CAL_TIME_PAD + minute * CAL_MINUTE_PX; }

static inline void cal_timeline_build(const cal_day_t *d, cal_timeline_t *t)
{
    *t = (cal_timeline_t){.height=cal_time_y(1440)};
    for (int i=0; i<d->n && i<CAL_MAX_EVENTS; i++) {
        const cal_event_t *e=&d->ev[i];
        if (e->allday) continue;
        int h=((int)e->eh*60+e->em-(int)e->sh*60-e->sm)*CAL_MINUTE_PX;
        if(h<48) h=48; // minimum 44px card plus 4px separation; short events show their real times
        cal_timebox_t b={.event=i,.y=cal_time_y(e->sh*60+e->sm),.h=h,.lanes=1};
        int j=t->n++;
        while(j>0 && t->box[j-1].y>b.y) { t->box[j]=t->box[j-1]; j--; }
        t->box[j]=b;
    }
    int end[CAL_MAX_EVENTS]={0}, group=0, group_end=0, lanes=0;
    for(int i=0;i<t->n;i++) {
        cal_timebox_t *b=&t->box[i];
        if(i && b->y>=group_end) {
            for(int j=group;j<i;j++) t->box[j].lanes=lanes;
            group=i;lanes=0;
        }
        int lane=0;
        while(lane<lanes && end[lane]>b->y) lane++;
        if(lane==lanes) lanes++;
        b->lane=lane;end[lane]=b->y+b->h;
        if(end[lane]>group_end) group_end=end[lane];
        if(end[lane]>t->height) t->height=end[lane];
        if(lanes>2) t->agenda=true;
    }
    for(int j=group;j<t->n;j++) t->box[j].lanes=lanes;
    // ponytail: >2 overlapping columns cannot keep readable titles on 360px glass.
    // Fall back to today's agenda; a future zoom interaction could retain proportional geometry.
    if(t->agenda) {
        for(int i=0;i<t->n;i++) {
            t->box[i].y=8+i*80;t->box[i].h=72;
            t->box[i].lane=0;t->box[i].lanes=1;
        }
        t->height=16+t->n*80;
    }
}
