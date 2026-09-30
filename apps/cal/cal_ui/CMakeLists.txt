# cal ui: the calendar face on the TrevOS kit. No board headers; the board plays the chime.
idf_component_register(SRCS "cal_faces.c" "cal_face_rect.c"
                       INCLUDE_DIRS "include"
                       REQUIRES trevos cal_core cal_sync)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
