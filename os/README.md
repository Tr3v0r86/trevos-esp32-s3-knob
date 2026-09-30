# TrevOS

TrevOS is the shared LVGL shell and app runtime. `trevos/` holds lifecycle, semantic input, launcher, UI kit, preferences and Settings; `trev_net/` owns Wi-Fi and time trust.

The runtime needs an LVGL display supplied by a board. It does not replace ESP-IDF or provide a complete BSP. Reuse it through the ESP-IDF component system and the public headers in `trevos/include/`. See [architecture](../docs/architecture.md), [design](../DESIGN.md) and [examples](../examples/).

Keep runtime behavior board-neutral. A board supplies haptic/rail/wheel hooks and routes input under the LVGL lock. Applications use semantic controls and callbacks rather than touching hardware drivers directly.
