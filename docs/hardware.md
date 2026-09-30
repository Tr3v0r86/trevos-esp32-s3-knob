# Hardware target and limits

## Target family

This firmware targets the [Waveshare ESP32-S3-Knob-Touch-LCD-1.8](https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8) hardware family: ESP32-S3 display processor, round 360 × 360 ST77916 display, CST816-family touch, rotation-only wheel and DRV2605 haptic driver. Manufacturer documentation describes a second ESP32-U4WDH audio processor. This repository does not provide firmware for that second processor.

The development unit's observed dual-processor architecture, native-USB versus CH340 behavior, display and haptic arrangement match this family. Its precise vendor branding has not been independently established. Older JC3636K518 references are not proof that every similarly advertised board is interchangeable. JC3636W518V2 appears in the panel initialization ancestry, not as a verified identity for every complete device.

## Check before flashing

Confirm your board's processor, flash/PSRAM configuration, panel, pinout and touch orientation. The intended display processor is ESP32-S3 with 16 MB flash and 8 MB PSRAM in the tested configuration. Board clone revisions may differ. Compare the pin definitions in `boards/esp32-s3-knob/main/board_pins.h` with your hardware documentation.

On the development hardware, USB-C orientation determines which processor enumerates. A CH340 serial connection may reach the audio processor. USB port naming alone is insufficient: verify chip identity. Never write the display firmware to the audio processor. Flashing and especially erasing can destroy the stock firmware or stored configuration; follow [setup and recovery](setup.md), retaining a private backup first.

## Implemented and provisional

Display, touch, wheel, haptics, network task/calendar fetch and focus-session comment delivery were exercised on the original development firmware. The public extraction must be validated separately on hardware. Simulator screenshots and compilation do not establish battery runtime, recovery, sustained frame rate or clone compatibility.

Battery measurement is not validated, so no battery percentage is promised. Audio co-processor support is absent. There is no knob-press action: the wheel rotates and the touchscreen commits. Keep calibration changes board-local and document the revision you tested.
