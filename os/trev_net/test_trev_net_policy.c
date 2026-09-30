#include <assert.h>
#include <stdio.h>
#include "trev_net_policy.h"
int main(void) {
    assert(trev_clock_should_restore(0, 1790000000));
    assert(!trev_clock_should_restore(1790000100, 1790000000));  /* a set live clock (kept by esp_restart) is never read back from NVS */
    assert(!trev_clock_should_restore(0, 5));
    assert(trev_time_on_restore(TREV_TIME_NONE) == TREV_TIME_RESTORED);
    assert(trev_time_on_restore(TREV_TIME_TRUSTED) == TREV_TIME_TRUSTED);
    assert(trev_time_on_trusted(TREV_TIME_RESTORED) == TREV_TIME_TRUSTED);
    time_t t;
    assert(trev_http_date_parse("Tue, 29 Sep 2026 06:18:27 GMT", &t) && t == 1790662707);
    assert(!trev_http_date_parse("garbage", &t));
    assert(!trev_http_date_parse("Tue, 31 Foo 2026 06:18:27 GMT", &t));
    assert(!trev_http_date_parse("Tue, 29 Sep 99999 06:18:27 GMT", &t));      /* year out of range */
    trev_walk_t w = {0}; int tries = 1;                          /* first attempt already made */
    while (trev_walk_fail(&w, 2) == TREV_WALK_TRY) tries++;
    assert(tries == 12);                                         /* 3 tries x 2 nets x 2 rounds */
    assert(w.net == 0 && w.attempt == 0 && w.round == 0);        /* next wake starts at net 0 */
    puts("PASS trev_net_policy"); return 0;
}
