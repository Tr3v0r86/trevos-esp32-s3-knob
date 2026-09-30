# Contributing

## Choose a component

Open an issue describing a concrete problem, hardware revision and expected behavior. Use `os/` for shared runtime, `apps/pomodoist/` or `apps/cal/` for capabilities, and `boards/esp32-s3-knob/` for pin/driver integration. Keep each pull request focused. Adding another board should supply a BSP and consume the existing components.

## Work without personal data

Use synthetic fixtures. Never submit tokens, OAuth client files, Wi-Fi settings, real task/event records, device backups, serial dumps or credential-bearing firmware. A screenshot is data too. Local credential files are ignored, but inspect every staged diff before committing. Report accidental disclosure through [SECURITY.md](SECURITY.md).

## Verify behavior

Run the documented host checks, build the target firmware, and exercise affected simulator interactions. For visual changes, supply before/after captures from the simulator and confirm circular text/button bounds and wheel motion. Hardware claims must name what was physically tested; do not present simulator evidence as device verification. Keep public APIs, failure handling, retry behavior and credential boundaries covered by meaningful checks.

## Review and release

Changes land through reviewed pull requests. Keep third-party notices with copied or adapted material. Release notes distinguish source/build verification from hardware verification and list known limitations. Only distribute artifacts built from the public tree with empty credentials. Module bundles must include their dependency closure and relevant licenses. The public repository is authoritative for these products; do not maintain a private parallel implementation and silently overwrite contributions.
