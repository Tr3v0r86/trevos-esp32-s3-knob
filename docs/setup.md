# Build and run

## Toolchain and first build

Use ESP-IDF **5.4**, Python 3, CMake and a matching ESP32-S3 board. Follow Espressif's [ESP-IDF installation guide](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/get-started/). Run commands from the repository root. Activate your own IDF installation before building; do not substitute a different IDF version and assume equivalent behavior.

```sh
. "$IDF_PATH/export.sh"
cp boards/esp32-s3-knob/main/secrets.h.example boards/esp32-s3-knob/main/secrets.h
PUCK_RELEASE=1 idf.py -C boards/esp32-s3-knob -B build-release build
python3 tools/check.py
python3 tools/test_tools.py
```

The copied header is empty. That build is useful for reproducibility and inspection, but it cannot fetch your tasks or events until configured. `PUCK_RELEASE=1` removes the development screenshot stream. Keep release and development builds in different build directories. The component manifest pins LVGL 9.5.0; do not change the renderer version casually when comparing screenshots.

## Connect your accounts

Edit only your ignored local `secrets.h`. Set `WIFI_SSID`, `WIFI_PASS` and `TODOIST_TOKEN` using your own Wi-Fi and Todoist API token. The token is private and allows account actions, including the focus-session comments this app sends. Never place the configured header, a copy of it, or resulting firmware in an issue or release.

For Google Calendar, create your own Google Cloud project, enable the Calendar API, configure the OAuth consent screen and create a **Desktop app** OAuth client. For a testing app, add the account you will use as a test user. Store the downloaded client JSON outside the repository. Google or a Workspace administrator may restrict authorization; this project cannot override those controls.

```sh
python3 tools/authorize-calendar.py \
  --client /path/outside/repository/google-desktop-client.json \
  --secrets boards/esp32-s3-knob/main/secrets.h \
  --account you@example.com
```

Sign in on that same computer. The helper uses loopback redirect, PKCE and state validation, checks the selected verified account and granted scopes, tests refresh-token renewal and writes the header atomically. It requests read-only calendar access plus identity scopes needed for the account check. `--calendar` defaults to `primary`; use an accessible calendar ID if needed. `--no-browser` prints the sign-in link for you to open manually on the same computer. See Google's [desktop OAuth documentation](https://developers.google.com/identity/protocols/oauth2/native-app).

Testing-mode or revoked credentials may require authorization again. After changing the header, rebuild. First-time token setup needs network access; cached display operation does not imply successful current authentication. Calendar times in this MVP assume **UTC+7**, including timestamp conversion. Do not use the device as a reliable appointment display in another timezone without implementing and testing timezone support.

## Preview without a device

Install SDL2 development libraries, a C compiler, CMake and Git. The simulator fetches pinned LVGL and cJSON sources; it requires no account credentials.

```sh
cmake -S sim -B sim/build
cmake --build sim/build -j
SIM_DISP=null SIM_TICKS=500 SIM_SHOT=/tmp/trevos-focus.ppm sim/build/pucksim
```

The null display writes a PPM capture suitable for inspecting the actual LVGL renderer. [Simulator instructions](../sim/README.md) describe interactive input and synthetic face presets. Public [reference screens](../design/screens/) are generated from invented tasks/events, not a connected account. Pure-logic [examples](../examples/README.md) do not require display hardware.

## Flash and recover deliberately

Read [hardware](hardware.md) first. Identify the **ESP32-S3 display processor**, not the audio processor. Stop if the port or hardware revision is uncertain. USB orientation can expose a different processor on this hardware family. Never erase or flash solely because a port name resembles an example.

Before replacing stock or another firmware, save a complete private flash backup with Espressif's read-flash tools and retain the manufacturer's recovery procedure. A backup may contain passwords and tokens: store it outside this repository. Compare the existing partition layout with `boards/esp32-s3-knob/partitions.csv`. A layout migration may require erasing the S3's flash, which removes its existing firmware, caches and settings. Perform that step yourself only after checking that your backup and restore procedure are usable. There is no automatic migration or guaranteed stock recovery in this project.

Once prepared, from an activated IDF environment:

```sh
python3 tools/flash.py --port YOUR_EXPLICIT_S3_PORT --build build-release
```

The helper checks the selected build targets ESP32-S3, asks esptool to identify the S3 and requires the literal confirmation `FLASH` before writing. It never selects a device automatically. Validate display, touch orientation, wheel, haptics, wake, restart, Wi-Fi and your app's behavior after flashing. Keep the original backup until recovery and normal use are verified. No downloadable credential-bearing firmware is provided.

## Reuse and release

Run `python3 tools/bundle.py` from a reviewed Git checkout to produce `dist/trevos-full.tar.gz`, `dist/pomodoist.tar.gz` and `dist/cal.tar.gz`. Bundles contain tracked source and licenses, not SDKs or personal build output. Review the archive contents before distributing them. See [architecture](architecture.md), [contributing](../CONTRIBUTING.md) and [security](../SECURITY.md).
