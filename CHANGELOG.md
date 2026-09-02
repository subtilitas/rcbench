# Changelog

Notable changes to rcbench. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Commit-level
history is in git.

## 0.1.0 - 2026-09-02

First tagged release. Pre-release firmware: no output driver produces a
signal, and the control-page and settings-store changes have not run on
hardware.

### Added

- The panel writes ARM, THROTTLE and CLEAR to the coprocessor's control page:
  ARM and THROTTLE at every 50 ms poll, CLEAR on an explicit arm. STOP,
  DISARM and a dead touch controller write ARM = 0. The band shows the
  coprocessor's fault bits.
- The panel initialises settings from the schema and loads and saves them in
  NVS (non-volatile storage) through the new `settings_nvs` component.
- `README-de.md`, a German front page, with a language switch at the top of
  both READMEs.
- `CHANGELOG.md`.
- `tools/check_docs.py` checks the README pair, the pages under `hardware/`,
  the `Who compiles what` table against the three build files, and the
  screenshot count in `STATUS.md`.

### Changed

- The wiki pages, both READMEs, `STATUS.md`, `CONTRIBUTING.md`, `SECURITY.md`
  and the `hardware/` pages describe the system in its current state, without
  development history. Stale statements corrected: the module tree, the compile
  table, the console configuration, the coprocessor board, the Performance
  table, and the bit-timing figures for both CAN (Controller Area Network)
  controllers.
- `STATUS.md` lists the open items with what each needs, and the order of work
  with the state of each step.

### Fixed

- `test/host/test_settings.c` defined its geometry macros twice.

### Known limitations

- The control page writes and the NVS (non-volatile storage) settings store
  have not run on hardware.
- No output driver produces a signal on a pin.
