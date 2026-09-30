# PUCK_NO_HAPTIC=1 (env) drops the haptic source and its define: the build proves the shared
# main.c needs none of it, the same way a clone board without the chip behaves at runtime (E10).
if(NOT DEFINED ENV{PUCK_NO_HAPTIC})
    set(PUCK_HAPTIC_SRC "bsp_haptic.c")
    set(PUCK_HAPTIC_DEF BSP_HAS_HAPTIC=1)
endif()

if(DEFINED ENV{PUCK_RELEASE})
    set(PUCK_SHOT_DEF BSP_NO_DEV_SHOT=1)
else()
    set(PUCK_SHOT_SRC "trevos_shot.c")
endif()

idf_component_register(
    SRCS "main.c" ${PUCK_SHOT_SRC}
         "bsp_display.c" "bsp_wheel.c" ${PUCK_HAPTIC_SRC} "bsp_touch.c" "bsp_i2c.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_lcd esp_driver_spi esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_driver_pcnt
             esp_lcd_touch esp_lcd_touch_cst816s esp_lvgl_port esp_timer
             trevos core sync ui trev_net nvs_flash esp_netif esp_event mbedtls
             cal_ui cal_sync)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    LV_LVGL_H_INCLUDE_SIMPLE
    BSP_LCD_H_RES=360
    BSP_LCD_V_RES=360
    # Rotation: the ST77916 is mounted 90deg CCW (USB-C tail to the side), done by MADCTL through
    # esp_lvgl_port, so LVGL still renders 360x360. Touch flags below must match (C10).
    BSP_DISP_SWAP_XY=true
    BSP_DISP_MIRROR_X=false
    BSP_DISP_MIRROR_Y=true
    BSP_TOUCH_SWAP_XY=1
    BSP_TOUCH_MIRROR_X=0
    BSP_TOUCH_MIRROR_Y=1
    # The ST77916 wants RGB565 MSB-first on the wire and the S3 is little endian; esp_lvgl_port
    # owns the flush, so the swap is its flag.
    BSP_DISP_SWAP_BYTES=true
    BSP_INPUT_TOUCH=1            # main.c: touch path instead of buttons
    # Serial sync would fight for the native USB-Serial/JTAG console. The reduced calendar build
    # feeds the calendar over that console by its own path (TT_CAL_SERIAL).
    BSP_NO_SERIAL_SYNC=1
    TT_POMODOIST_ONLY=1          # X7: means "no deskbuddy"; deskbuddy is not a component here, so main.c
                                 # cannot even #include it. The app set is Pomodoist, Cal and Settings.
    TT_CAL=1                     # the home + Pomodoist + Cal set (shared with the disk)
    TT_BOOT_APP=1                # boots Pomodoist, never the home (UC1 B, D11)
    BSP_HAS_BACKLIGHT_DIM=1      # idle dimming
    BSP_HAS_WHEEL=1              # the wheel: 1 ms PCNT sampling into a queue (C2)
    ${PUCK_SHOT_DEF}             # BSP_NO_DEV_SHOT=1 under PUCK_RELEASE, empty otherwise
    ${PUCK_HAPTIC_DEF}           # the haptic: probed DRV2605 (E10); empty under PUCK_NO_HAPTIC
    # The durable outbox is linked here (REQUIRES sync), so the faces that read today's
    # completions are real rather than counting nothing.
    TT_HAS_OUTBOX=1)
