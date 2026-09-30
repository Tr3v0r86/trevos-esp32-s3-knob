# cal sync: one HTTPS GET of the proxy window 60 s after GOT_IP and every 5 minutes, NVS cache
# in CAL_CACHE_PARTITION (the board sets it build-wide; default "nvs").
idf_component_register(SRCS "cal_sync.c"
                       INCLUDE_DIRS "include"
                       REQUIRES cal_core esp_event esp_netif nvs_flash
                                esp_http_client esp-tls mbedtls esp_timer esp_driver_uart)
