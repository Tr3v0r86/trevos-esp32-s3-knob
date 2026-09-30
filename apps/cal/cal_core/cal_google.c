// Direct Calendar API adapter. Bangkok has no DST; keep shared model/UI unchanged.
#include "cal_google.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char *str(const cJSON *o, const char *k) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) ? v->valuestring : "";
}
// Keep display text bounded and font-safe. No HTML tags in descriptions.
static void text(char *dst, size_t cap, const char *src, bool html) {
    size_t n=0; bool tag=false, space=false;
    while (*src && n+1<cap) {
        unsigned char c=(unsigned char)*src++;
        if (html && c=='<') { tag=true; continue; }
        if (html && tag) { if(c=='>') {tag=false; space=true;} continue; }
        if (c<=32) {space=true; continue;}
        if(space && n && n+1<cap) dst[n++]=' ';
        space=false;
        if(n+1>=cap) break;
        if(c<127) dst[n++]=(char)c;
        else {
            // Preserve middle dot; fold common curly punctuation, replace other codepoints.
            if(c==0xc2 && (unsigned char)src[0]==0xb7 && n+2<cap) {
                dst[n++]=(char)c; dst[n++]=*src++;
            } else if(c==0xe2 && (unsigned char)src[0]==0x80 && src[1]) {
                unsigned char tail=(unsigned char)src[1]; src+=2;
                dst[n++]=(tail==0x98||tail==0x99)?'\'':(tail==0x9c||tail==0x9d)?'"':(tail==0x93||tail==0x94)?'-':'?';
            } else { while(((unsigned char)*src&0xc0)==0x80) src++; dst[n++]='?'; }
        }
    }
    dst[n]=0;
}
static bool date_valid(const char *s) {
    if(strlen(s)<10 || s[4]!='-' || s[7]!='-') return false;
    for(int i=0;i<10;i++) if(i!=4 && i!=7 && !isdigit((unsigned char)s[i])) return false;
    return true;
}
static bool stamp(const char *s, time_t *out) {
    int y,m,d,h,mi,se,pos=0;
    if(!date_valid(s) || sscanf(s,"%4d-%2d-%2dT%2d:%2d:%2d%n",&y,&m,&d,&h,&mi,&se,&pos)!=6) return false;
    if(m<1||m>12||d<1||d>31||h>23||mi>59||se>59) return false;
    if(s[pos]=='.') {pos++; while(isdigit((unsigned char)s[pos])) pos++;}
    int offset=0;
    if(s[pos]=='Z' && !s[pos+1]) offset=0;
    else {
        int oh,om,used=0; char sign=s[pos];
        if((sign!='+'&&sign!='-') || sscanf(s+pos+1,"%2d:%2d%n",&oh,&om,&used)!=2 || s[pos+1+used] || oh>23||om>59) return false;
        offset=(oh*60+om)*(sign=='-'?-1:1);
    }
    struct tm tm={.tm_year=y-1900,.tm_mon=m-1,.tm_mday=d,.tm_hour=h,.tm_min=mi,.tm_sec=se,.tm_isdst=0};
    *out=mktime(&tm)+(7*60-offset)*60; // mktime interpreted fields as Bangkok.
    return true;
}
static time_t midnight(const char *date) {
    struct tm tm={0}; int y,m,d;
    if(sscanf(date,"%d-%d-%d",&y,&m,&d)!=3) return 0;
    tm.tm_year=y-1900;tm.tm_mon=m-1;tm.tm_mday=d;return mktime(&tm);
}
bool cal_google_begin(cal_window_t *out, time_t now) {
    struct tm tm; localtime_r(&now,&tm);
    if(tm.tm_year<120) return false;
    memset(out,0,sizeof *out);out->n_days=2;strcpy(out->tz,"Asia/Bangkok");
    strftime(out->generated,sizeof out->generated,"%Y-%m-%dT%H:%M:%S+07:00",&tm);
    strftime(out->day[0].date,11,"%Y-%m-%d",&tm);
    now+=86400;localtime_r(&now,&tm);strftime(out->day[1].date,11,"%Y-%m-%d",&tm);return true;
}
static void insert(cal_day_t *d, const cal_event_t *ev) {
    // Keep earliest 24 in display order, even when pages contain a late all-day event.
    if(d->n<CAL_MAX_EVENTS) d->ev[d->n++]=*ev;
    else {
        if(d->more<255) d->more++;   // one event is dropped either way: this one or the evicted last
        const cal_event_t *last=&d->ev[d->n-1];
        if(!ev->allday && (last->allday || ev->sh*60+ev->sm>=last->sh*60+last->sm)) return;
        d->ev[d->n-1]=*ev;
    }
    cal_day_sort(d);
}
bool cal_google_page(const char *json, size_t len, cal_window_t *out, char *next, size_t cap) {
    cJSON *root=cJSON_ParseWithLength(json,len);bool ok=false;
    if(!root) return false;
    cJSON *items=cJSON_GetObjectItemCaseSensitive(root,"items");
    if(!cJSON_IsObject(root)||!cJSON_IsArray(items)||cJSON_GetObjectItemCaseSensitive(root,"error")) goto done;
    const char *token=str(root,"nextPageToken");if(strlen(token)>=cap) goto done;
    strcpy(next,token);
    cJSON *e;
    cJSON_ArrayForEach(e,items) {
        if(!cJSON_IsObject(e)) goto done;
        if(!strcmp(str(e,"status"),"cancelled")) continue;
        bool declined=false;cJSON *a;
        cJSON_ArrayForEach(a,cJSON_GetObjectItemCaseSensitive(e,"attendees"))
            if(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(a,"self")) && !strcmp(str(a,"responseStatus"),"declined")) declined=true;
        if(declined) continue;
        cJSON *start=cJSON_GetObjectItemCaseSensitive(e,"start"),*end=cJSON_GetObjectItemCaseSensitive(e,"end");
        const char *sd=str(start,"date"),*ed=str(end,"date");
        bool allday=*sd!=0;time_t ts=0,te=0;
        if(allday) {if(strlen(sd)!=10||strlen(ed)!=10||!date_valid(sd)||!date_valid(ed)||strcmp(ed,sd)<=0) goto done;}
        else if(!stamp(str(start,"dateTime"),&ts)||!stamp(str(end,"dateTime"),&te)||te<ts) goto done;
        cal_event_t ev={0};ev.allday=allday;
        // The token is the owner's own: Google already withholds what he may not see (#96).
        text(ev.title,sizeof ev.title,*str(e,"summary")?str(e,"summary"):"(No title)",false);
        strcpy(ev.cal,"Calendar");
        text(ev.desc,sizeof ev.desc,str(e,"description"),true);text(ev.loc,sizeof ev.loc,str(e,"location"),false);
        static const unsigned colors[]={0x7986cb,0x33b679,0x8e24aa,0xe67c73,0xf6bf26,0xf4511e,0x039be5,0x616161,0x3f51b5,0x0b8043,0xd50000};
        int id=atoi(str(e,"colorId"));unsigned rgb=(id>=1&&id<=11)?colors[id-1]:0x7986cb;
        ev.rgb[0]=rgb>>16;ev.rgb[1]=rgb>>8;ev.rgb[2]=rgb;ev.pal=cal_pal_nearest(ev.rgb[0],ev.rgb[1],ev.rgb[2]);
        for(int i=0;i<out->n_days;i++) {
            cal_day_t *d=&out->day[i];time_t lo=midnight(d->date),hi=lo+86400;
            if(allday) {if(strcmp(d->date,sd)<0||strcmp(d->date,ed)>=0) continue;}
            else {
                if(ts>=hi || (te<=lo&&ts<lo)) continue;
                int s=ts<=lo?0:(int)((ts-lo)/60),t=te>=hi?1440:(int)((te-lo)/60);
                ev.sh=s/60;ev.sm=s%60;ev.eh=t/60;ev.em=t%60;
            }
            insert(d,&ev);
        }
    }
    ok=true;
done:cJSON_Delete(root);return ok;
}
