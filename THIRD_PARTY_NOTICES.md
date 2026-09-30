# Third-party notices

The root MIT license covers original project material. Dependencies and derived
assets retain their own licenses; the root license does not replace them.

## IBM Plex fonts

The generated font arrays in `os/trevos/ui/fonts/` derive from IBM Plex Sans and
IBM Plex Mono. Copyright © 2017 IBM Corp.; Reserved Font Name “Plex”. Distributed
under the SIL Open Font License 1.1, reproduced in
[LICENSES/IBM-Plex-OFL.txt](LICENSES/IBM-Plex-OFL.txt). The generated arrays are
font data, not a new font family. Upstream: https://github.com/IBM/plex.

## Display initialization data

`boards/esp32-s3-knob/main/st77916_v2_init.h` contains ST77916 panel
initialization data corresponding to ESPHome's Python `_ESP_VOCAT_INIT` table.
Copyright (c) 2019 ESPHome; MIT license, reproduced in
[LICENSES/ESPHome-MIT.txt](LICENSES/ESPHome-MIT.txt).

The 181 register commands match that upstream table in order. This project
expresses them as ESP-IDF C initializer data and appends a sleep-out command with
a 120 ms delay. The upstream model name identifies the source of the data;
it does not assert compatibility with every board using the same controller.

Verified source revision:
https://github.com/esphome/esphome/blob/58385bebff34b4f3b2acc3c9ca54b7395499b3ca/esphome/components/mipi_spi/models/st77916.py

License classification:
https://github.com/esphome/esphome/blob/dev/LICENSE

ESPHome licenses its Python and other non-runtime material under MIT. Its C/C++
runtime is GPLv3; no ESPHome runtime code is included by this attribution.

## Build dependencies

ESP-IDF and its component manager obtain dependencies during setup; their source
is not relicensed by this project. Retain the licenses and notices shipped with
ESP-IDF, LVGL, SDL, and the Espressif display/touch/LVGL-port components when
redistributing their source or compiled firmware. Versions are specified in the
board component manifest and dependency lockfile where present.

The public source excludes the legacy embedded lo-fi animation because its
redistribution provenance was not established. Manufacturer images used as design
references are not included or granted a redistribution license by this project.
