# Project documentation

- [Power management](architecture/power-management.md): implemented state machines,
  suspend/resume sequence, timing, wake inputs and display-sleep limits.
- `development/display-backlight-lifecycle.md`: notes on persisted brightness,
  auto-load, and the safe display startup sequence.
- `development/component-overrides.md`: local Brookesia overrides, their
  purpose, and the procedure for comparing them with Registry packages.
- `config/`: SD-card configuration templates and import instructions.
- `ui_porting_baseline/`: versioned visual golden files used by the simulator
  release regression gate.
- `releases/`: published release notes.

README preview images live in `images/`. Generated test screenshots, diffs,
build reports, and other reproducible outputs are not tracked by Git.
