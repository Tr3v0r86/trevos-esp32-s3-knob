#include "cal_timeline.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    cal_day_t d = {0};
    cal_timeline_t t;
    d.n = 4;
    d.ev[0] = (cal_event_t){.allday=true};
    d.ev[1] = (cal_event_t){.sh=8,.eh=10};
    d.ev[2] = (cal_event_t){.sh=8,.sm=30,.eh=9};
    d.ev[3] = (cal_event_t){.sh=9,.eh=9,.em=30};
    cal_timeline_build(&d, &t);
    assert(t.n == 3 && !t.agenda);
    assert(t.box[0].h == 240 && t.box[1].h == 60);
    assert(t.box[1].y - t.box[0].y == 60);
    assert(t.box[0].lane != t.box[1].lane);
    assert(t.box[1].lane == t.box[2].lane); // adjacent events reuse the lane
    assert(t.box[0].lanes == 2 && t.box[2].lanes == 2);
    assert(t.box[0].event == 1); // all-day entries are not timeline positions
    d.ev[3].sh=8; d.ev[3].sm=45;
    cal_timeline_build(&d,&t);
    assert(t.agenda); // three concurrent columns would not be legible at 360px
    for(int i=1;i<t.n;i++) assert(t.box[i].y >= t.box[i-1].y+t.box[i-1].h+8);
    d.n=2;
    d.ev[0]=(cal_event_t){.sh=23,.sm=59,.eh=24};
    d.ev[1]=(cal_event_t){.sh=0,.eh=0}; // missing end; meaningful tap height
    cal_timeline_build(&d,&t);
    assert(t.n==2 && t.box[0].event==1);
    assert(t.box[0].h>=44 && t.box[1].h>=44);
    assert(t.height >= t.box[1].y+t.box[1].h);
    d.n=0;cal_timeline_build(&d,&t);assert(t.n==0);
    puts("test_cal_timeline: all passed");
}
