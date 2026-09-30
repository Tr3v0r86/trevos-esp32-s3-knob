/* Host test for the validated scalar settings keys (C8), against the in-memory NVS.
 *   cc -std=c99 -Wall -Wextra -Werror -Ios/trevos/include -Isim/shims os/trevos/test_trev_prefs.c \
 *      os/trevos/trev_prefs.c sim/stubs/sim_nvs.c -o /tmp/tp && /tmp/tp */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "nvs.h"
#include "trev_prefs.h"

void sim_nvs_fail_next_commit(void);

/* Run trev_pref_set and count the trev_prefs log lines it wrote to stderr (the sim's own
 * [nvs] commit chatter is not ours). */
static int set_logged_lines(const char *key, int v, bool *ok)
{
    fflush(stderr);
    int saved = dup(2);
    FILE *tf = tmpfile();
    dup2(fileno(tf), 2);
    *ok = trev_pref_set(key, v);
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    rewind(tf);
    int n = 0;
    char line[256];
    while (fgets(line, sizeof line, tf)) if (strstr(line, "(trev_prefs)")) n++;
    fclose(tf);
    return n;
}

int main(void)
{
    /* pure validator: in range keeps, out of range and both edges */
    assert(trev_pref_validate(50, 25, 10, 100) == 50);
    assert(trev_pref_validate(10, 25, 10, 100) == 10);
    assert(trev_pref_validate(100, 25, 10, 100) == 100);
    assert(trev_pref_validate(9, 25, 10, 100) == 25);
    assert(trev_pref_validate(101, 25, 10, 100) == 25);
    assert(trev_pref_validate(-5, 25, 10, 100) == 25);

    /* missing reads the default */
    assert(trev_pref_get("bright", 60, 10, 100) == 60);

    /* a valid value round-trips, and survives a re-read */
    bool ok;
    set_logged_lines("bright", 80, &ok);
    assert(ok);
    assert(trev_pref_get("bright", 60, 10, 100) == 80);
    assert(trev_pref_get("bright", 60, 10, 100) == 80);

    /* stored 999 on a 10..100 key reads the default (garbage never reaches the caller) */
    nvs_handle_t h;
    assert(nvs_open("trevset", NVS_READWRITE, &h) == ESP_OK);
    assert(nvs_set_i32(h, "bright", 999) == ESP_OK);
    assert(nvs_commit(h) == ESP_OK);
    nvs_close(h);
    assert(trev_pref_get("bright", 60, 10, 100) == 60);

    /* downgrade: a key written by newer firmware sits in NVS and is never read. The
     * known key is untouched by it, and the same 999-style junk under an unknown name
     * changes nothing the old firmware can see. */
    set_logged_lines("bright", 70, &ok);
    assert(nvs_open("trevset", NVS_READWRITE, &h) == ESP_OK);
    assert(nvs_set_i32(h, "future_key", 42) == ESP_OK);
    assert(nvs_commit(h) == ESP_OK);
    nvs_close(h);
    assert(trev_pref_get("bright", 60, 10, 100) == 70);
    assert(trev_pref_get("other", 5, 0, 9) == 5);   /* an unrelated key still defaults */

    /* a failed commit: false, and exactly one log line */
    sim_nvs_fail_next_commit();
    int lines = set_logged_lines("bright", 90, &ok);
    assert(!ok);
    assert(lines == 1);
    /* the hook is one-shot: the next set commits normally */
    lines = set_logged_lines("bright", 90, &ok);
    assert(ok && lines == 0);
    assert(trev_pref_get("bright", 60, 10, 100) == 90);

    /* erase clears the namespace, unknown keys included */
    trev_prefs_erase();
    assert(trev_pref_get("bright", 60, 10, 100) == 60);
    assert(nvs_open("trevset", NVS_READONLY, &h) == ESP_OK);
    int32_t raw;
    assert(nvs_get_i32(h, "future_key", &raw) == ESP_ERR_NVS_NOT_FOUND);
    nvs_close(h);

    puts("PASS trev_prefs");
    return 0;
}
