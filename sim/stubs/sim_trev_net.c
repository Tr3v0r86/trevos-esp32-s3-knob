/* sim/stubs/sim_trev_net.c: host trev_net. No radio, no SNTP, no NVS clock.
 *
 * The host has a real clock, so the state is TRUSTED, the same branch a synced device takes.
 * SIM_TIME=restored|none pins the other two states so the rail's "~HH:MM" and the suppressed
 * chime can be shot. status() reports a fixed healthy link for the Settings face.
 * Everything else is a no-op: start() has no network to walk, restore() has no NVS clock.
 */
#include "trev_net.h"
#include <stdlib.h>
#include <string.h>

void trev_net_start(const trev_net_ap_t *aps, int n) { (void)aps; (void)n; }

bool trev_net_connected(void) { return true; }

trev_time_state_t trev_net_time_state(void)
{
    const char *t = getenv("SIM_TIME");
    if (t && !strcmp(t, "restored")) return TREV_TIME_RESTORED;
    if (t && !strcmp(t, "none"))     return TREV_TIME_NONE;
    return TREV_TIME_TRUSTED;
}

void trev_net_trust_time(time_t utc) { (void)utc; }

void trev_net_clock_restore(void) { }

void trev_net_status(char *ssid, size_t ssid_cap, int *rssi, char *ip, size_t ip_cap)
{
    if (ssid && ssid_cap) { strncpy(ssid, "SimNet", ssid_cap - 1); ssid[ssid_cap - 1] = '\0'; }
    if (rssi) *rssi = -52;
    if (ip && ip_cap) { strncpy(ip, "192.168.1.40", ip_cap - 1); ip[ip_cap - 1] = '\0'; }
}
