cmake_minimum_required(VERSION 3.16)
# The puck reuses the shared TrevOS shell (os/) and the pomodoist and cal app-cores (apps/) as
# IDF components, exactly like the 1.85B disk, and the shared boards/tdisplay-s3/main/main.c.
# The board layer is the BSP (display here; wheel and haptics in later tasks), under main/.
# Target esp32s3, set in sdkconfig.defaults.
# TT_POMODOIST_ONLY means "no deskbuddy" (X7): the dashboard is not registered. It is set in
# main/CMakeLists.txt, and deskbuddy is deliberately NOT a component here, so it is not on the chip.
set(EXTRA_COMPONENT_DIRS
    "../../os"
    "../../apps/pomodoist/ui"
    "../../apps/pomodoist/core"
    "../../apps/pomodoist/sync"
    "../../apps/cal/cal_core"
    "../../apps/cal/cal_sync"
    "../../apps/cal/cal_ui")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Build-wide, not main-only: all of these are read inside components (`trevos`, `core`, `sync`,
# cal), so a target_compile_definitions on main would never reach them.
#
# TT_ROUND_DISPLAY is the round-glass seam: the bars inset and clamp to the chord and the tap
# zones derive from the bar actually drawn. TT_NO_LAYER_FX keeps layout-only colour swaps, what
# the sim is shot with, so the device runs what was inspected (ADR-0012 point 4).
idf_build_set_property(COMPILE_DEFINITIONS "TT_ROUND_DISPLAY=1" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "TT_NO_LAYER_FX=1" APPEND)
# E12 (ADR-0020): the puck's home is the ring, not the disk's pill pair. Build-wide because
# trevos_home_ring.c / trevos_home_round.c are in the `trevos` component and main.c registers
# Settings and the ring hooks under it; the disk and the CYD never set it.
idf_build_set_property(COMPILE_DEFINITIONS "TT_HOME_RING=1" APPEND)
# The puck's Cal is a glance face, one event per detent (face brief 3.6). Build-wide because
# cal_faces.c is in the `cal_ui` component; the disk and the CYD never set it and keep the timeline.
idf_build_set_property(COMPILE_DEFINITIONS "CAL_FACE_GLANCE=1" APPEND)

# POMO_DESC_LEN is laid out in `core`, filled by `sync`, rendered by `main`: it must be one
# value everywhere or the struct layouts diverge, which links and then corrupts. 480 is what
# the scrollable detail card holds; sizeof(pomo_tasklist_t) is then 21,220 bytes, which does not
# fit the 24KB main `nvs`, hence the `cache` partition below.
idf_build_set_property(COMPILE_DEFINITIONS "POMO_DESC_LEN=480" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "POMO_CACHE_PARTITION=\"cache\"" APPEND)
# The calendar cache shares the 1 MB `cache` partition with the tasklist; namespace `cal`.
idf_build_set_property(COMPILE_DEFINITIONS "CAL_CACHE_PARTITION=\"cache\"" APPEND)

# The laptop's CALT sets the clock over serial in the reduced build. With an NVS restore the
# trust state would sit RESTORED forever (rail ~HH:MM, chime held), so it omits BSP_CLOCK_NVS.
if(DEFINED ENV{PUCK_CAL_SERIAL})
    idf_build_set_property(COMPILE_DEFINITIONS "TT_CAL_SERIAL=1" APPEND)
else()
    # No RTC chip on the puck: restore the clock from NVS at boot, save once a minute while TRUSTED.
    idf_build_set_property(COMPILE_DEFINITIONS "BSP_CLOCK_NVS=1" APPEND)
endif()

idf_build_set_property(COMPILE_DEFINITIONS "BSP_HAS_WHEEL=1;TT_TOUCH_UI=1;TT_HAS_OUTBOX=1" APPEND)
project(trevos_knob)
