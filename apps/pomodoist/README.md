# Pomodoist

A focus timer tied to a selected Todoist task. Choose a task, turn the wheel to set the interval, start or pause, then take a break. Completed intervals queue Todoist comments for delivery when connected. It runs independently of Cal.

## Pieces and dependencies

| Directory | Use | Dependencies |
|---|---|---|
| `core/` | Portable task/timer state | C standard library |
| `sync/` | Task parsing, serial/Wi-Fi transports, durable outbox | Core, cJSON, ESP-IDF networking/storage, `os/trev_net` |
| `ui/` | Round focus, picker, detail, break and ledger faces | Core, sync, TrevOS/LVGL and ESP-IDF timing/storage |

Copying `core/` is sufficient for a headless timer integration. A complete face needs all three components and their shared runtime. Use the [examples](../../examples/) and [architecture guide](../../docs/architecture.md) for the component closure and sparse checkout. The full board composition is in `boards/esp32-s3-knob/` at the repository root.

## Integration

The UI exports `POMODOIST_APP`, lifecycle setup functions and status-rail hooks in `ui/include/pomodoist_ui.h`. The board owns display/input/network bring-up. `core/include/pomodoist_core.h` exposes the portable timer state; sync turns external JSON into that state. Keep transport credentials outside version control.

The repository simulator supplies synthetic tasks. A real Todoist connection requires your own token. See [setup](../../docs/setup.md). Empty tasks are a valid state and must not be confused with a successful network fetch.

## Behavior and limits

The outbox attempts to preserve completed intervals through disconnection. It does not mean a public demo has posted a comment to a real account. Do not simulate success by marking a task complete: a focus session and completion of the task are different actions.

The firmware does not assess your priorities or read the calendar to block a session. Current limits and verification scope are in the root [README](../../README.md).
