# Cal

A Google Calendar glance face for a round display. The wheel browses events and a tap opens details. A cached calendar window remains available during a network outage; trusted time gates upcoming-event signals.

## Pieces and dependencies

| Directory | Use | Dependencies |
|---|---|---|
| `cal_core/` | Window model, event conversion, glance selection | C standard library and cJSON |
| `cal_sync/` | Fetch, authorization state and cache | Model, ESP-IDF HTTP/TLS/events/storage |
| `cal_ui/` | Calendar faces and interactions | Model, sync, TrevOS/LVGL |

Use the model on its own for a host integration. The complete face requires all three components and the runtime. See [examples](../../examples/), [architecture](../../docs/architecture.md) and the board composition in `boards/esp32-s3-knob/`.

## Account and display privacy

Use your own Google Cloud OAuth client and account with the `calendar.events.readonly` scope. The device can show details that this account is permitted to read, including the owner's private events. A desk display is visible to anyone standing near it. The project does not publish or proxy your calendar through a shared service. See [setup](../../docs/setup.md) and [security](../../SECURITY.md).

## Current limits

The first public MVP uses **Asia/Bangkok / UTC+7 wall time**, including the Google timestamp adapter. Global timezone and daylight-saving support are not implemented. The model holds a bounded calendar window; missing or expired data must remain visibly distinct from an empty day. It is not a calendar editor and does not constrain Pomodoist's timer.
