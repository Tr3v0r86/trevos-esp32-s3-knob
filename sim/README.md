# Desktop simulator

## Build

Install a C compiler, CMake and SDL2 development files. On macOS use `brew install sdl2 cmake`; on Debian/Ubuntu use `sudo apt-get install libsdl2-dev cmake build-essential`.

```sh
cmake -S sim -B sim/build -DSIM_TEST_FLAGS='-DTT_DEV_DEMO=1 -DTT_DEV_VIEW=1 -DTT_DEV_CAL_NOW=560'
cmake --build sim/build -j
sim/build/pucksim
```

If CMake cannot find SDL2 on macOS, add `-DCMAKE_PREFIX_PATH="$(brew --prefix sdl2)"` to the configure command. A fresh configure downloads LVGL **9.5.0** and cJSON **1.7.19**. To use existing sources, pass `-DLVGL_DIR=/path/to/lvgl` and `-DIDF_CJSON=/path/to/cJSON`. Do not silently substitute different renderer versions when comparing captures.

## Inputs

The simulator runs the board's startup and the real apps against in-memory hardware/network stubs. No account credentials are read and no Todoist or Calendar requests occur. Wheel input is delivered through stdin:

```text
TURN +1
TURN -1
TAP 180 290
HOME
WAIT 400
QUIT
```

`TAP` uses screen pixels on the 360×360 canvas. `WAIT` lets animation settle for the given milliseconds. The interactive SDL window also accepts pointer input. Long press or `HOME` returns to the ring launcher.

## Synthetic captures

`TT_DEV_DEMO=1` populates invented tasks. Calendar events are always invented in the simulator. `TT_DEV_CAL_NOW=560` pins the local time to 09:20. Face presets are `TT_DEV_VIEW=1` focus, `2` picker, `3` break, `10` session ledger, `11` Cal and `12` launcher. Without a preset, normal boot opens Pomodoist; without `TT_DEV_DEMO`, the task list stays empty.

Reconfigure and rebuild when changing presets. Capture without opening a window:

```sh
SIM_DISP=null SIM_TICKS=600 SIM_SHOT=/tmp/trevos-focus.ppm sim/build/pucksim </dev/null
```

The PPM is the real LVGL render. On macOS convert with `sips -s format png /tmp/trevos-focus.ppm --out /tmp/trevos-focus.png`. Other image tools can read PPM directly. To capture Settings, build the launcher preset and pipe `WAIT 400`, two `TURN +1` lines, `WAIT 400`, then `TAP 180 180` to the simulator. Give scripted captures enough ticks to process waits and settle.

## Limits and checks

The simulated panel is square: the physical glass is round, so corners outside its circle are invisible on hardware. Simulator captures do not prove display wiring, color order, touch orientation, hardware haptics, radio reliability, memory endurance or recovery. Mark them **simulator captures**, never device photographs.

After configuring the simulator, `python3 tools/check.py` uses its downloaded cJSON source to run portable tests. `python3 tools/test_tools.py` and `python3 tools/audit.py` check the publication tooling and tracked-source guard. Build firmware separately before any manual hardware verification.
