// One wall clock for all simulator code. Real time unless a regression pins it.
#include <time.h>
static time_t pinned;
void sim_clock_set(time_t t) { pinned=t; }
time_t time(time_t *out) {
    struct timespec ts; clock_gettime(CLOCK_REALTIME,&ts);
    time_t t=pinned ? pinned : ts.tv_sec;
    if(out) *out=t;
    return t;
}
