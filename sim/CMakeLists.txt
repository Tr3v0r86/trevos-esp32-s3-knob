cmake_minimum_required(VERSION 3.16)
project(pucksim C ASM)
set(CMAKE_C_STANDARD 11)
include(FetchContent)
set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(MAIN ${REPO}/boards/esp32-s3-knob/main)
find_package(SDL2 REQUIRED CONFIG)
set(LVGL_DIR "" CACHE PATH "Existing LVGL 9.5.0 source; empty downloads pinned release")
set(IDF_CJSON "" CACHE PATH "Existing cJSON source; empty downloads pinned release")
if(NOT LVGL_DIR)
 FetchContent_Declare(lvgl_src URL https://github.com/lvgl/lvgl/archive/refs/tags/v9.5.0.tar.gz)
 FetchContent_GetProperties(lvgl_src)
 if(NOT lvgl_src_POPULATED)
  FetchContent_Populate(lvgl_src)
 endif()
 set(LVGL_DIR ${lvgl_src_SOURCE_DIR})
endif()
if(NOT IDF_CJSON)
 FetchContent_Declare(cjson_src URL https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.tar.gz)
 FetchContent_GetProperties(cjson_src)
 if(NOT cjson_src_POPULATED)
  FetchContent_Populate(cjson_src)
 endif()
 set(IDF_CJSON ${cjson_src_SOURCE_DIR})
endif()
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE PATH "" FORCE)
get_target_property(SDL2_HEADERS SDL2::SDL2 INTERFACE_INCLUDE_DIRECTORIES)
foreach(HEADER IN LISTS SDL2_HEADERS)
 get_filename_component(SDL2_PARENT "${HEADER}" DIRECTORY)
 include_directories("${SDL2_PARENT}")
endforeach()
add_subdirectory(${LVGL_DIR} ${CMAKE_BINARY_DIR}/lvgl)
set(SIM_TEST_FLAGS "" CACHE STRING "Synthetic fixture compile flags")
separate_arguments(SIM_OPTIONS NATIVE_COMMAND "${SIM_TEST_FLAGS}")
file(GLOB FONTS ${REPO}/os/trevos/ui/fonts/*.c)
add_executable(pucksim
 ${MAIN}/main.c
 ${REPO}/apps/pomodoist/ui/pomodoist_ui.c
 ${REPO}/apps/pomodoist/core/pomodoist_core.c
 ${REPO}/apps/pomodoist/sync/pomodoist_outbox.c
 ${REPO}/os/trevos/trevos.c ${REPO}/os/trevos/trev_prefs.c
 ${REPO}/os/trevos/ui/trevos_ui.c ${REPO}/os/trevos/ui/trevos_home_ring.c
 ${REPO}/os/trevos/ui/trevos_home_round.c ${REPO}/os/trevos/ui/trev_ring_geom.c
 ${REPO}/os/trevos/ui/trevos_settings.c ${FONTS}
 ${REPO}/os/trev_net/trev_net_policy.c
 ${REPO}/apps/cal/cal_core/cal_model.c ${REPO}/apps/cal/cal_core/cal_glance.c
 ${REPO}/apps/cal/cal_ui/cal_faces.c ${IDF_CJSON}/cJSON.c
 sim_main.c sim_touchtest.c motion_test.c
 stubs/sim_clock.c stubs/sim_lvgl_port.c stubs/sim_nvs.c stubs/sim_bsp_display.c
 stubs/sim_trevos_shot.c stubs/sim_sync.c stubs/sim_trev_net.c stubs/sim_buttons.c
 stubs/sim_touch.c stubs/sim_cal_sync.c stubs/sim_bsp_wheel.c stubs/sim_bsp_haptic.c stubs/sim_system.c)
target_include_directories(pucksim BEFORE PRIVATE shims . ${MAIN}
 ${REPO}/os/trevos/include ${REPO}/os/trev_net/include
 ${REPO}/apps/pomodoist/ui/include ${REPO}/apps/pomodoist/core/include ${REPO}/apps/pomodoist/sync/include
 ${REPO}/apps/cal/cal_core ${REPO}/apps/cal/cal_sync/include ${REPO}/apps/cal/cal_ui/include ${IDF_CJSON})
target_compile_options(pucksim PRIVATE ${SIM_OPTIONS})
target_compile_definitions(pucksim PRIVATE app_main=board_app_main LV_LVGL_H_INCLUDE_SIMPLE
 TT_TODOIST_WRITE=0 BSP_LCD_H_RES=360 BSP_LCD_V_RES=360 TT_TOUCH_UI=1
 TT_POMODOIST_ONLY=1 TT_CAL=1 CYDSIM=1 TT_ROUND_DISPLAY=1 TT_HOME_RING=1
 CAL_FACE_GLANCE=1 TT_NO_LAYER_FX=1 POMO_DESC_LEN=480 TT_HAS_OUTBOX=1
 TT_BOOT_APP=1 BSP_HAS_BACKLIGHT_DIM=1 BSP_CLOCK_NVS=1 BSP_HAS_WHEEL=1 BSP_HAS_HAPTIC=1 SIM_WHEEL=1)
target_link_libraries(pucksim lvgl SDL2::SDL2 pthread)
