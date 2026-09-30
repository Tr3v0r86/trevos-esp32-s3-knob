#include "cal_google.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
int main(void) {
 setenv("TZ","ICT-7",1);tzset();
 struct tm tm={.tm_year=126,.tm_mon=8,.tm_mday=26,.tm_hour=12};
 cal_window_t *w=calloc(1,sizeof *w);char next[128];assert(w);
 assert(cal_google_begin(w,mktime(&tm)));
 const char *j="{\"items\":["
 "{\"summary\":\"Secret\",\"visibility\":\"private\",\"description\":\"Hidden\",\"location\":\"Hidden\",\"start\":{\"dateTime\":\"2026-09-26T02:00:00Z\"},\"end\":{\"dateTime\":\"2026-09-26T03:30:00Z\"}},"
 "{\"summary\":\"Declined\",\"attendees\":[{\"self\":true,\"responseStatus\":\"declined\"}]},"
 "{\"status\":\"cancelled\"},"
 "{\"summary\":\"All day\",\"start\":{\"date\":\"2026-09-26\"},\"end\":{\"date\":\"2026-09-27\"}},"
 "{\"summary\":\"Cross midnight\",\"start\":{\"dateTime\":\"2026-09-26T23:30:00+07:00\"},\"end\":{\"dateTime\":\"2026-09-27T01:00:00+07:00\"}}],\"nextPageToken\":\"next\"}";
 assert(cal_google_page(j,strlen(j),w,next,sizeof next));assert(!strcmp(next,"next"));
 assert(w->day[0].n==3&&w->day[1].n==1);assert(w->day[0].ev[0].allday);
 cal_event_t *e=&w->day[0].ev[1];assert(!e->hidden&&!strcmp(e->title,"Secret")&&!strcmp(e->loc,"Hidden"));assert(e->sh==9&&e->eh==10&&e->em==30);
#if CAL_DESC_LEN>1
 assert(!strcmp(e->desc,"Hidden"));
#endif
 assert(w->day[0].ev[2].eh==24&&w->day[1].ev[0].sh==0&&w->day[1].ev[0].eh==1);
 assert(!cal_google_page("{\"error\":{}}",12,w,next,sizeof next));
 assert(!cal_google_page("{\"items\":[{}]}",14,w,next,sizeof next));
 assert(cal_google_page("{\"items\":[]}",12,w,next,sizeof next)&&!next[0]);
 free(w);puts("Google adapter: timezone, owner-visible private events, decline, all-day, midnight, pagination and errors pass");
}
