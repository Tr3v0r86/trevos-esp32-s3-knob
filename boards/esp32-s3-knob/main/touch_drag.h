// Direct-drag capture is isolated so the exact board decision is host-testable.
#pragma once
#include <stdbool.h>
#include <stdlib.h>
#define TOUCH_DRAG_PX 12
#define TOUCH_SWIPE_PX 60
#define TOUCH_LONG_MS 700
typedef struct {int x0,y0,x,y;bool claimed;} touch_drag_t;
static inline void touch_drag_begin(touch_drag_t *d,int x,int y) {*d=(touch_drag_t){x,y,x,y,false};}
static inline bool touch_drag_ready(touch_drag_t *d,int x,int y) {
    if(!d->claimed && abs(y-d->y0)>=TOUCH_DRAG_PX && abs(y-d->y0)>=abs(x-d->x0)) d->claimed=true;
    return d->claimed;
}
static inline void touch_drag_delivered(touch_drag_t *d,int x,int y){d->claimed=true;d->x=x;d->y=y;}

typedef enum { TOUCH_WAKE, TOUCH_CONSUMED, TOUCH_SWIPE, TOUCH_IGNORE, TOUCH_CONTENT, TOUCH_ZONE } touch_release_t;
static inline touch_release_t touch_release(bool dark,bool claimed,bool travelled,int dx,int dy,
                                            unsigned held,bool content) {
    if(dark) return TOUCH_WAKE;
    if(claimed) return TOUCH_CONSUMED;
    if(abs(dx)>=TOUCH_SWIPE_PX || abs(dy)>=TOUCH_SWIPE_PX) return TOUCH_SWIPE;
    if(travelled) return TOUCH_IGNORE;
    return held<TOUCH_LONG_MS && content ? TOUCH_CONTENT : TOUCH_ZONE;
}
