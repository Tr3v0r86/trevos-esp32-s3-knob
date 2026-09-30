idf_component_register(SRCS "trevos.c" "ui/trevos_ui.c" "ui/trevos_home_round.c" "ui/trevos_home_ring.c" "ui/trev_ring_geom.c"
                            "ui/trevos_settings.c"
                            "trev_prefs.c"
                            "ui/fonts/plex_mono_b_72.c"
                            "ui/fonts/plex_mono_b_25.c"
                            "ui/fonts/plex_sans_b_28.c"
                            "ui/fonts/plex_sans_b_22.c"
                            "ui/fonts/plex_sans_b_16.c"
                            "ui/fonts/plex_sans_b_12.c"
                            "ui/fonts/plex_sans_r_14.c"
                            "ui/fonts/plex_sans_r_10.c"
                            "ui/fonts/plex_mono_sb_13.c"
                            "ui/fonts/plex_mono_sb_11.c"
                            "ui/fonts/plex_mono_sb_10.c"
                            "ui/fonts/plex_mono_sb_9.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_lvgl_port esp_system esp_timer esp_app_format nvs_flash)

# The TrevOS UI kit (trevos_ui + trevos_theme + fonts) is the board-neutral shell
# dependency, promoted here from the t3 board. lv_font_conv output + lvgl.h-consuming
# kit code guard their include on LV_LVGL_H_INCLUDE_SIMPLE -> "lvgl.h".
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
