# trev_net: the one Wi-Fi owner on every board (T1, E7). Lifted out of pomodoist_sync_wifi.c;
# the Pomodoist fetch loop and Cal sync consume the link, they never bring it up.
# trev_net_policy.c is the pure half (clock trust, HTTP Date, walker) and also builds on the
# host (make -C os/trev_net test); keep IDF headers out of it.
# Deliberately NOT a dependency of `trevos` (E7): the OS shell does not know the network exists.
idf_component_register(SRCS "trev_net.c" "trev_net_policy.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_event esp_netif nvs_flash lwip)
