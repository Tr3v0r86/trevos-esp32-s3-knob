# pomodoist sync: todoist-sync transports.
#   A (serial)  : cJSON + USB-Serial/JTAG driver.
#   B (wifi)    : esp_http_client over TLS (cert bundle) + NVS cache + NVS outbox, on the link
#                 trev_net owns (it never brings Wi-Fi up itself; E7).
# pomodoist_parse.c is the JSON layer, split out so it also builds on the host against cJSON
# and core alone (test_pomodoist_parse.c); keep ESP headers out of it.
idf_component_register(SRCS "pomodoist_sync_serial.c" "pomodoist_sync_wifi.c"
                            "pomodoist_parse.c" "pomodoist_outbox.c"
                       INCLUDE_DIRS "include"
                       REQUIRES core json driver
                                trev_net esp_event esp_netif nvs_flash
                                esp_http_client esp-tls mbedtls esp_timer)
