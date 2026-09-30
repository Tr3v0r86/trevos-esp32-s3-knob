# Changelog

## 0.1.0 — public MVP extraction

- TrevOS runtime, ring launcher, settings and shared network/time services.
- Pomodoist timer core, Todoist synchronization and separate UI component.
- Cal model, Google Calendar integration and round glance face.
- Self-contained ESP32-S3 knob board target, desktop simulator and synthetic examples.
- Design system, build/reuse documentation, license notices and contribution workflow.

The original implementation ran on a development device; the public component extraction needs its own hardware verification. Calendar wall time is currently UTC+7 without daylight-saving support. Compile-time credentials, unverified battery measurement and unsupported audio co-processor remain known limitations.
