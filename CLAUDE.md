# TrevOS contributor instructions

Read README.md, docs/architecture.md and DESIGN.md before changing behavior. This repository is the source of truth for TrevOS, Pomodoist and Cal on the ESP32-S3 knob target. Keep one implementation per capability.

- Board support and composition live in boards/esp32-s3-knob; app logic and UI live in their apps/ directories; shared services and theme live in os/. Do not reintroduce paths to a separate development checkout.
- All fixtures, screenshots and logs submitted for review must be synthetic and free of account/device identifiers. Never read, print, copy, stage or package live credentials or configured firmware. Use the empty secrets template for build verification.
- Respect the existing UTC+7 limitation until a reviewed timezone change adds model and adapter tests. Pomodoist does not read Cal to gate a timer.
- Keep UI faithful to DESIGN.md and the runtime theme. An app directory includes only its documented layers; examples and bundles must carry required dependencies and licenses.
- Run python3 tools/check.py, python3 tools/test_tools.py and python3 tools/audit.py, then the affected firmware/simulator build. Use the documented toolchain. Review diffs independently before release. Say which checks were actually run and which require hardware.
- Do not flash or erase a physical device as part of an automated code task. The device owner performs that step after checking the target, backup and recovery path.
- Stage exact changed paths. Completed bounded changes can be committed locally; pushing, changing visibility and publishing releases require the user's authorization. Never force-push shared history or silently overwrite concurrent work.
- No private source history, personal build products, manufacturer photographs or unlicensed assets in a release. Keep concept/simulator labels with images.

Use this file as the repository's agent-instruction home. Do not generate an AGENTS.md copy.
