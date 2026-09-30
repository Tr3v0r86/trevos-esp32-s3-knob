# Architecture and reuse

## Three layers

A board provides the display, touch, wheel, haptic and storage integration. TrevOS provides one LVGL screen, app lifecycle, input routing, launcher and settings. Apps supply capabilities and faces. Network/time ownership is shared through `os/trev_net`; apps do not each initialize Wi-Fi.

| Layer | Directory | Responsibility |
|---|---|---|
| Hardware and composition | `boards/esp32-s3-knob/` | ESP-IDF project, BSP, app registration and hardware hooks |
| Runtime | `os/trevos/` | App contract, shell, UI kit, settings, preferences and semantic input |
| Connectivity | `os/trev_net/` | Wi-Fi lifecycle, network readiness and clock trust |
| Pomodoist | `apps/pomodoist/{core,sync,ui}/` | Timer/task model, Todoist transport/outbox, display and interactions |
| Cal | `apps/cal/{cal_core,cal_sync,cal_ui}/` | Calendar window, fetch/cache and glance UI |

## Runtime and data flow

Board startup brings up its hardware, initializes TrevOS under the LVGL lock, registers app definitions and routes semantic input. The OS calls the active app's callbacks. Wheel presence, feedback and the status rail are runtime hooks rather than app-owned pin assumptions.

Pomodoist consumes a parsed task list. Its durable outbox records completed focus sessions before trying to send Todoist comments; unavailable networking leaves work queued. A failed response must not become an invented success. Cal fetches through an authenticated account and publishes a cached calendar window; the UI reads that window. Neither app directs the other's actions.

Configuration currently enters through a local ignored `secrets.h`. Credentials are embedded in the user's build and must never appear in public binary releases. Calendar wall time remains UTC+7; changing just the C library timezone without updating conversion/model tests does not add supported global timezones.

## Reuse the smallest layer you need

Pomodoist's core is plain C. Cal's model is C plus cJSON. Their network components additionally require ESP-IDF services; their faces require TrevOS/LVGL. Start with the [examples](../examples/) and each app README rather than copying only a UI file.

To fetch selected source with Git, use sparse checkout, including the shared dependencies, examples and licenses:

```sh
git clone --filter=blob:none --sparse https://github.com/Tr3v0r86/trevos-esp32-s3-knob.git
cd trevos-esp32-s3-knob
git sparse-checkout set apps/pomodoist os examples LICENSES docs
```

That fetch is useful for inspection and integration. The complete board build additionally needs its board directory and whichever apps it registers. Source bundles from `python3 tools/bundle.py` include the documented shared material. Check each bundle's README before integrating.

## Add an app or a board

Use `os/trevos/include/trev_app.h` as the app contract and existing app definitions as the lifecycle reference. Keep board pins and driver initialization out of the app. Register the app during board composition and exercise wheel, tap, home, dark/wake, missing-data and network-error states. Use semantic theme roles and round-safe geometry.

A different board supplies its own BSP and build configuration. This repository's tested target family does not imply every ESP32 display module is supported. Document pinout, memory and input differences and validate on the actual device before claiming compatibility.
