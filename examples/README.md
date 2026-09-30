# Reuse a module

## Pure logic on a computer

Pomodoist core has no ESP-IDF or LVGL dependency:

```sh
cc -std=c99 -Iapps/pomodoist/core/include examples/pomodoist-core.c apps/pomodoist/core/pomodoist_core.c -o /tmp/pomo-example
/tmp/pomo-example
```

Cal model needs cJSON 1.7.x source (`CJSON_DIR`):

```sh
cc -std=c99 -Iapps/cal/cal_core -I"$CJSON_DIR" examples/cal-model.c apps/cal/cal_core/cal_model.c "$CJSON_DIR/cJSON.c" -o /tmp/cal-example
/tmp/cal-example
```

## Full app integration

Pomodoist's `core`, `sync`, `ui` components plus `os/trevos` and `os/trev_net` form the full app. Add their paths to your IDF project's `EXTRA_COMPONENT_DIRS`; provide LVGL 9.5.0 and esp_lvgl_port 2.8.x. The initial UI is designed for 360×360 round screens with a wheel and touch input. It is not a responsive layout for arbitrary displays.

After NVS/network/display setup, call `pomodoist_ui_init()`. Under the LVGL lock, call `trev_init(lv_screen_active())`, register `POMODOIST_APP`, set/open your chosen home index, then call `pomodoist_ui_start()` once. Initialize sync before starting `trev_net`. The complete board main demonstrates the ordering. `pomodoist_countdown_critical()` allows your BSP to avoid dimming during the last minute.

Cal's `cal_core`, `cal_sync`, `cal_ui` use the TrevOS shell and ESP-IDF network/NVS services. Call `cal_sync_start_google()` with your own credentials, then register `CAL_APP`. Provide the time-trust callbacks before starting sync. It is read-only; the initial Google adapter is limited to UTC+7.

## Downloadable source bundles

`python3 tools/bundle.py` creates the full source tree, Pomodoist with its shared OS dependencies, and Cal with its shared OS dependencies. Bundles preserve relative paths and licenses. They do not vendor ESP-IDF, LVGL, SDL2 or cJSON; install those documented external dependencies separately. The pure-logic examples above are the smallest runnable entry points.
