idf_component_register(SRCS "pomodoist_ui.c" INCLUDE_DIRS "include"
 REQUIRES trevos core sync trev_net nvs_flash)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
