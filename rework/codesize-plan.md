# Code size plan: files over 1,000 lines

Base: main edd813d6 (2026-10-16). It replaces the plan of 2026-10-12 on base
840360f8. It rests on four measured group reports: firmware, tools,
configurator and docs; screens and text tables; KST, PD mini, link modules
and out_bind; servo and ESC engines, gfx, logs and settings. Every figure
below is measured on edd813d6 unless it says "about" or "not measured".

Abbreviations: PR = pull request; KST = the programming wire of KST and
Chaservo servos; PD = USB Power Delivery; ESC = electronic speed controller;
TU = translation unit (one `.c` file as compiled); ELF = Executable and
Linkable Format.

## What changed since 2026-10-12

- No split PR of the old plan is done. Every PR the old rows waited for
  (#344, #349, #351, #353, #355, #357, #358, #362, #364) is merged. Open:
  #400 (draft, deferred) and #167 (hardware docs, no file of the plan).
- Growth: panel `main.c` 8,657 to 10,081; `servo_screen.c` 5,894 to 6,957;
  `test_servo.c` 7,966 to 9,398; CHANGELOG 3,722 to 4,854; `link_pages.h`
  1,618 to 1,960.
- New over 1,000: `servo_limrun.c` 1,488 and its test 2,555; `wifi_access.c`
  1,712; `help_site.py` 1,193; `picker_screen.c` 1,290; `settings_screen.c`
  1,115; `settings.c` 1,021; `test_link_update.c` 2,319 and six more tests.
- The limit run (#399) is built on the automatic test's parts. The old single
  `servo_test_panel.c` (PR 21) no longer holds.
- The firmware update (#393, #394, #398) adds 876 lines to panel `main.c`.
  #400 edits the same lines.

## The rule that does not hold: same stack depth

At `-O2`, GCC inlines 114 of the 209 functions in panel `main.c`. Under any
split along its sections, 92 of them land in another file and become real
calls. On the coprocessor it is 18 of 22. The frames of `control_task`,
`poll_far_end` and `app_main` change. `docs/Performance.md` holds exact
depths, so `stack_check.py --check-doc` fails after a pure move. The control
task has 832 bytes spare (depth 4,288 of a 5,120-byte limit, stack 6,144
bytes). The depth after a split is not measured. Fork F1 decides the new rule.

The shared-module splits (waves 2 to 4) are not on any deepest task chain
today. The smallest headroom through one of their files is 656 bytes
(`servo_report.c` on the panel main task). Each of these PRs still runs
`stack_check.py --check-doc` on the panel ELF before and after.

## Status

The owner took every fork recommendation (F1 to F25). Open PRs, each on
its own branch from main edd813d6, CI green on every job including both
firmware images and both `stack_check --check-doc` steps unless noted:

| Row | PR | Branch | State |
| --- | --- | --- | --- |
| 0a | #416 | `codesize/0a-doc-citations` | open, green |
| 0b | #417 | `codesize/0b-stack-elf` | open, green |
| 0b (`PORT_CALLS`) | #418 | `codesize/0b-port-calls` (on #417) | open, green; no measured depth changed |
| 1 | #420 | `codesize/1-arm-flash-gates` | open, CI pending |
| 2 | #419 | `codesize/2-arming-step` | open, green; no measured depth changed |
| 3 | #421 | `codesize/3-bench-telem` | open, CI pending |

Measured against the rows above:

- 0a: about 200 citations in 4 hardware docs, not the counts listed. Seven
  `ui_text.def` citations in `IOBoard.md` describe text that is not in the
  tree (OneShot/MultiShot, KISS telemetry, signal-only mode, socket
  ceilings); they wait for the owner to reword or delete them.
- 2: the ARM branch holds about 15 lines of decisions; the rest is I/O.
  Only the decisions move (`arming_handshake_step()`), +39/-18 in `main.c`.
- 3: +340/-88 over 13 files; `shared/bench` depends on `protocols/dshot`.
- #421 and #416 both rewrite 4 rows of `IOBoard.md`; the second to merge
  keeps #421's rows.
- The host suite counts 127 tests in a build configured by `coverage.py`:
  `esc_parity` is not registered with coverage on.

## Rules for every PR of the plan

1. Behaviour is identical. A move changes no logic, timing or wire format.
   Logic that leaves a firmware `main.c` gets its host test in the same PR.
2. The checks pass before and after with the same result: host suite,
   `render_ui.py --check` (pixel-identical), `coverage.py --check`,
   `check_docs.py`, `check_protocols.py`, `check_formats.py`,
   `ram_budget.py --check-doc`, both firmware images and the injector image.
3. Stack and frame-cost tables: per fork F1. Before and after figures go in
   the PR body.
4. Each new `.c` costs one line in its `CMakeLists.txt`, `coverage.py`
   TRACKED with a STATUS.md row, and `frame_cost.py`/`render_ui.py` SOURCES
   where the parent file is listed. Each new ui object that owns state costs
   a `psram_bss.lf` entry and rows in the .bss tables of Performance.md and
   Performance-de.md. Each new test binary costs one name in
   `test/host/CMakeLists.txt` and the STATUS.md suite list and counts.
5. Test rig headers hold only `static inline` helpers. A file-scope variable
   that no header function uses fails `-Werror=unused-variable` in a suite
   that does not touch it; it gets `__attribute__((unused))` or stays in the
   suite.
6. No new upward include. Protocol cores stay portable.
7. One author and one adversarial reviewer per PR, and green CI on a head
   that contains current main. Merges go one at a
   time because every PR edits the same lists.

## Wave 0: preparation

| PR | Change | Size |
| --- | --- | --- |
| 0a | Hardware-doc line citations become function or symbol names: 6 panel `main.c`, 32 coprocessor `main.c`, 32 `link_pages.h`, 12 `out_bind.c`, 7 `settings.c`, `ui_text.def`/`programmer_screen.c` in `hardware/docs/IOBoard.md`, `hardware/STATUS.md`, `docs/Control.md`. Several already point at wrong lines. `Research.md` stays untouched (F21) | about 100 lines of docs |
| 0b | `stack_check.py`: the shared ELF part (`Elf`, `Graph`, `call_args`, `die`, `doc_rows`, `sext`, `ROOT`) into `stack_elf.py`; the coprocessor half (573 lines) into `stack_iomcu.py`; `stack_check.py` stays the entry point (old PR 27, F4). Also `PORT_CALLS` follows `cu_port_exchange` (a gap found on the way); this may change a measured depth, so it goes first and alone if it does | 573 moved / about 40 |

## Wave 1: logic out of firmware main.c, under host test

| PR | Change | Size |
| --- | --- | --- |
| 1 | Coprocessor: `arm_now` (3 inputs now, with `update_hw_holds_arm()`) into `safety_gate`; `flash_busy` (4 inputs; also gates the firmware update) beside `flash_gate_open()` in `shared/safety/flash_gate.c` (F5); one case per input bit | about 170 lines |
| 2 | Panel: the ARM branch of `service_arming` (4,727-4,842, 116 lines) into `shared/safety/arming.c` as a step function; cases for a stale stop count and a stop after ARM | about 350 lines |
| 3 | Coprocessor `sample()`: the stale rules (`RPM_STALE_MS` 200, `EDT_STALE_MS` 2,000) and the valid bits (1,840-1,883) into `bench_state_from_telem()`; cases at 199/201 ms and 1,999/2,001 ms. #351 did the units already | about 150 lines |

## Wave 2: shared sources with their tests

| PR | Change | Moved / real |
| --- | --- | --- |
| 4 | `servo_str.c` (`servo_report.c` 20-476) + `servo_str.h` (`servo_test.h` 848-1,098) + `servo_test_io.h` (103 lines). `ui_text.h` takes `servo_str.h`: the engine header leaves 44 includers, `servo_screen.h` takes `servo_test_io.h`: it leaves 8 more. `check_formats.py` and `check_docs.py` read the new paths | 811 / 65 |
| 5 | `esc_json_lex.c` (64-514 + 1,392-1,403) + private header; 4 externs; `esc_json.c` about 985 | 503 / 40 |
| 6 | `gfx_text.c` (709-1,034 + 1,206-1,349) + `gfx_int.h`; 0 function externs, 1 variable (`s_trace_hold`); `gfx.c` about 856 | 494 / 30 |
| 7 | `kst_prog.c` into `kst_prog.c` (2,414) + `kst_prog_card.c` (2,417-2,813) + `kst_prog_file.c` (2,814-3,078) + `kst_prog_int.h`; 16 statics become private externs | 665 / 80 |
| 8 | `pdmini.c`: `pdmini_codec.c` (11-78, 0 externs) and `pdmini_jobs.c` (594-1,062, 6 externs) (F13); `pdmini.c` about 525 | 537 / 40 |
| 13 | `esc_stick.c` into `esc_det.c` (587-785), `esc_stick_rules.c` (65-586), `esc_stick_read.c` (readouts 786-915, F17) and the run file (about 961); `test_esc_stick.c` into 5 suites over `esc_stick_rig.h` (279 lines): det 8, rules 9, run 34, hand 15, entry 17 cases | about 3,300 / 150 |
| 14 | `sense_trace_feed.c` (31-224, 0 externs, the core 1 stack stays); `test_sense_trace.c` into 5 suites over `sense_trace_rig.h` (about 389 lines): 14 + 17 + 17 + 13 + 19 cases (F15) | about 3,900 / 30 |
| 15 | `log_reader.c` (rows 58-323 + sniff 379-482); `is_blank` as `static inline` in `log_csv_int.h`; `log_csv.c` 943. `test_logfile.c` into 3 suites over a 65-line rig: 13 + 25 + 15 cases | about 1,200 / 60 |
| 17 | `sense_link_view.c` (970-1,280) + `sense_link_int.h`; `test_sense_link.c` into 2 suites over `sense_link_fix.h` (104 lines): 24 + 25 cases | about 1,230 / 60 |
| 29 | `servo_limrun.c`: outbox and report (1,066-1,488) into `servo_limrun_report.c` (F18); `servo_limrun.c` 1,065. `test_servo_limrun.c` into 3 suites over a 570-line rig: 30 + 16 + 4 cases | about 2,900 / 30 |

## Wave 3: test files whose source splits later or stays

| PR | Change | Moved / real |
| --- | --- | --- |
| 9 | `test_servo.c` (220 cases): `servo_rig.h` (about 200 lines, also taken by `test_servo_arm.c`) + `servo_bench_rig.h` (about 430) (F8); `test_servo_settings.c` 48 cases; `test_servo_test_panel.c` 21; `test_servo_limit_panel.c` 7; 144 stay (about 5,660 lines) | about 3,780 / 100 |
| 10 | `test_programmer.c` (71): `programmer_rig.h` (about 85) + `test_esc_stick_panel.c` 58 cases; 13 stay | 3,650 / 70 |
| 11 | `test_kst_panel.c` (125): rig header (about 300) + files 17, no card 14, the servo limit run 12 cases in three suites; 82 stay (3,814 lines) | about 2,630 / 40 |
| 12 | `test_kst_prog.c` (153): `kst_prog_fix.h` (563) + `test_kst_prog_card.c` 46 cases; 107 stay (4,557 lines) | 2,680 / 80 |
| 16 | `test_servo_test.c` into 4 suites over `servo_test_rig.h` (13 + 19 + 4 + 23 cases); `test_servo_test_win.c` into 3 over its own rig (15 + 6 + 9) | about 4,100 / 40 |
| 30 | `test_settings.c` (100) into 5 suites over `settings_rig.h` (364 lines): 20 + 23 + 20 + 16 + 21 cases (F16) | about 3,700 / 40 |

## Wave 4: the screens

| PR | Change | Moved / real |
| --- | --- | --- |
| 19 | `programmer_screen.c` into `programmer_screen.c` (1,125) + `esc_stick_panel.c` (307-451 + 1,213-4,431 = 3,364), an overlay on `kst_panel.c`'s shape; the stick reads 13 device fields 103 times through a rectangle table or `ui_tap.h`; own `psram_bss.lf` entry | 3,364 / about 250 |
| 20 | `servo_screen.c`: settings overlay into `servo_settings_panel.c` (1,204 lines); 49 statics and 30 foreign fields cross through `servo_screen_int.h` (F7) | 1,204 / about 120 |
| 21 | `servo_test_panel.c` (801 lines) | 801 / about 80 |
| 31 | `servo_limit_panel.c` (661 lines); the 7 test statics it calls go into the private header (F6). `servo_screen.c` ends at 4,291 | 661 / about 60 |

`frame_cost.py` figures for the servo views after PRs 20, 21 and 31 are not
measured.

## Wave 5: the firmware files

| PR | Change | Moved / real |
| --- | --- | --- |
| 22 | Panel servo link client into `shared/bench/servo_link.c` behind `link_port_t`: servo section less the pin-display statics, `servo_let_go`, `servo_service` (up to 916 lines); `s_servo_hz_sent` gets a getter; `test_servo_link.c` with cases for the sweep version gate, the refusal while a release is owed and the rate retry | about 916 / real not measured |
| 23 | Coprocessor `main.c` into 7 files + `iomcu.h`: pages + PROG 436, KST 165, core 1 288, tap 302, BIND 287, heartbeat + CAN 246; `main.c` keeps 843; 27 statics become `extern` | 1,723 / about 150 |
| 24 | Panel `main.c`: `panel.h` + `panel_card.c` (930) + `panel_log.c` (710); `ram_budget.py` reads `panel.h` for the queue macros (F3, about 10 lines and one test case) | 1,640 / about 150 |
| 25 | Panel: `panel_link.c` (726) + `panel_poll.c` (807) + `panel_services.c` (604) | 2,137 / about 120 |
| 26 | Panel: `panel_supply.c` (723) + `panel_arm.c` (814) + `panel_render.c` (481) + `panel_art.c` (277 + 65 drawing statics) | 2,360 / about 150 |
| 27 | Panel: `panel_update.c` (876; coprocessor update, Wi-Fi, GitHub update, `cu_render_service`) (F2) | 876 / about 60 |

After PRs 22 to 27, panel `main.c` keeps about 2,214 lines: plumbing 965 and
`app_main` 740 are one web of queues and atomics. 177 statics become `extern`
in `panel.h` (F25); 29 move to their one user. Each PR also adds its files to
the `ci.yml` cppcheck pass for `main.c` (lines 304-331) and builds the
injector image.

## Timed with a release

| PR | Change | Moved / real |
| --- | --- | --- |
| 18 | `out_board.c` + `out_board.h` out of `out_bind.c` (16-551; `out_bind.c` 587); `k_pico_pins` stays at line 33 so the 40 pad anchors of the configurator stay valid; `drawings/test/panel.test.ts` repointed in the same PR; `OUT_BIND` in `catalogue.ts`/`check.ts` changes in the next release PR (F12). `test_outbind.c`: board suite 21 cases out, 44 stay | about 1,280 / about 200 |

## Wave 6: docs and the configurator test

| PR | Change |
| --- | --- |
| 28 | Link.md KST port section (697-1,141; DE 763-1,253) to its own page in EN and DE; 4 inbound anchors, sidebar 2 lines |
| 32 | CHANGELOG: 0.9.1 and older (3,379-4,854, 1,476 lines) to `CHANGELOG-0.9.md`; CHANGELOG keeps 3,378 (F22) |
| 33 | `catalogue.test.ts`: Osoyoo strip data and coverage-tool cases (1,208-1,770, 22 tests) to their own file; no list edit; leaves about 1,213 |

## Left whole, with the reason

- **Screens:** `kst_panel.c` 3,134 (no section over 913 lines, F9),
  `supply_screen.c` 2,014, `log_viewer_screen.c` 1,952, `motor_screen.c`
  1,503, `picker_screen.c` 1,290, `settings_screen.c` 1,115: one screen each;
  a cut is a design change.
- **Tables:** `ui_text.def` 1,505, `ui_text_de.c` 2,103 (a split needs
  `check_docs.py` and `check_formats.py` to follow includes, and it collides
  with #400; F10); `link_pages.h` 1,960 and `test_link_pages.c` 1,674 (one
  register map).
- **Modules:** `servo_test.c` 1,304 (one state machine, 14 of 24 statics
  cross), `kst_port.c` 1,051 (path in `check_protocols.py`), `kst_file.c`
  1,072 (owner answer), `settings.c` 1,021 (21 over; 3 `check_docs.py` paths,
  and a move edits `Research.md`; F19), `wifi_access.c` 1,712 (one state
  struct used 275 times; macros read by path).
- **Tools:** `check_docs.py` 1,473, `sense_trace.py` 1,456,
  `test_sense_trace_tool.py` 2,552, `help_site.py` 1,193,
  `test_check_protocols.py` 1,999, `render_screen.c` 3,021 (F11).
- **Configurator:** `catalogue.ts` 2,432 (largest table 696 lines),
  `check.ts` 1,120.
- **Docs:** `Screens.md`/`-de` 2,975/3,237 (F20), `ServoProgrammer.md`/`-de`,
  `Servo.md`/`-de` 1,181/1,195, `FirstRun-de.md` 1,044, `Research.md` 1,028.
- **Tests with no cut that lands both halves under 1,000** (F14):
  `test_kst_port` 4,182, `test_kst_session` 2,985, `test_kst_link` 2,652,
  `test_pdmini` 3,008, `test_link_update` 2,319, `test_nav` 2,997,
  `test_sense_sched` 1,990, `test_arming` 1,727, `test_safety_gate` 1,667,
  `test_esc_profiles` 1,564, `test_logwriter` 1,400, `test_gfx` 1,029, and
  the KST, tone, outputs, sense-page, bind-link, outbind-rest, screen and
  widget suites listed in the group reports.

## What stays over 1,000 lines after the plan

Panel `main.c` about 2,214; `servo_screen.c` 4,291 (the sweep core reads 98
fields of the state); `esc_stick_panel.c` 3,364 (new; its 16 drawers, about
1,470 lines, can go to `esc_stick_draw.c` later); `kst_prog.c` 2,414;
`programmer_screen.c` 1,125; `servo_limrun.c` 1,065; and the files left
whole above.

## Size of the job

| Item | Figure |
| --- | --- |
| PRs | 35 (wave 0: 2, wave 1: 3, wave 2: 10, wave 3: 6, wave 4: 4, wave 5: 6, release-timed: 1, wave 6: 3) |
| Code moved | about 57,000 lines |
| Real change | about 3,000 lines (estimate), of which about 900 are new tests |
| New test binaries | about 32 (123 to about 155) |
| Authors | 4 in parallel; 1 reviewer per PR; at most 6 at a time |
| Merges | one at a time; each PR merges current main and regenerates the coverage tables first |

Order: wave 0, then waves 1 and 2 in parallel. Wave 3 PR 12 after PR 7.
Wave 4: PR 19 after PR 10; PR 20 after PR 9; PRs 21 and 31 after PR 20.
Wave 5: PR 22 after PR 2; PR 23 after PRs 1 and 3; PRs 24 to 27 after PR 22,
in that order. PR 18 right after the next release. Wave 6 at any time.

Each split PR adds one line to lists #400 also edits (`test/host/CMakeLists.txt`,
`coverage.py`, `render_ui.py`, STATUS.md). #400 rebases when it resumes.

## Forks for the owner

A yes takes every recommendation below. Name a fork to choose otherwise.

| Fork | Question | Recommendation |
| --- | --- | --- |
| F1 | Stack and frame-cost tables after a firmware split | Re-measure in the same PR and update Performance.md/-de. New rule: each task stays at or under its limit (control 5,120 bytes, core 0 2,044 bytes) and the PR states the change. A function whose move pushes a task over stays in `main.c` |
| F2 | Panel update code (876 lines) and #400 | Split it now (PR 27); #400 is 89 commits behind and rebases anyway |
| F3 | `ram_budget.py` reads queue macros from one file only | It also reads `panel.h` (about 10 lines and a test case) |
| F4 | `stack_check.py` import shape | A third module `stack_elf.py` for the shared part; no lazy import |
| F5 | Home of coprocessor `flash_busy` | `shared/safety/flash_gate.c`, beside the panel's `flash_gate_open()` |
| F6 | Servo automatic test and limit run: one panel or two | Two: `servo_test_panel.c` 801 and `servo_limit_panel.c` 661 lines |
| F7 | Servo screen panels: command API or shared state | Shared state in a private `servo_screen_int.h`; keeps the change a move. A command API is new code |
| F8 | Servo test rig: one header or two | Two: `servo_rig.h` (about 200) and `servo_bench_rig.h` (about 430) |
| F9 | `kst_panel.c` 3,134 | Leave whole |
| F10 | Text tables per screen | Leave whole |
| F11 | `render_screen.c` view table | Leave whole |
| F12 | `out_board` timing | Right after the next release, with `k_pico_pins` at line 33; the configurator path changes in the following release PR |
| F13 | `pdmini.c`: codec only (993 lines, 7 under) or codec and jobs | Codec and jobs; `pdmini.c` about 525 |
| F14 | Tests over 1,000 with no clean cut | Leave whole, one suite per module |
| F15 | `test_sense_trace.c`: feed suite only (about 3,300 left) or 5 suites | 5 suites |
| F16 | `test_settings.c`: now or after a `settings_screen.c` split | Now; `settings_screen.c` stays whole |
| F17 | `esc_stick.c` readouts | Own file `esc_stick_read.c`; run file about 961 |
| F18 | `servo_limrun.c` | Outbox and report out (1,066-1,488); 1,065 lines left |
| F19 | `settings.c` 1,021 | Leave whole |
| F20 | `Screens.md` against the web help | Leave whole; the web help carries the per-screen pages |
| F21 | Hardware-doc line citations | Replace with names once (PR 0a); `Research.md` stays |
| F22 | CHANGELOG archive | Now (PR 32), 146 lines before the 5,000-line trigger |
| F23 | Optional doc pages (Link.md firmware update, Servo.md automatic test, `link_pages.h` update page) | Not now |
| F24 | `esc_stick_panel.c` 3,364: second cut of the drawers | Not now |
| F25 | Panel `main.c` statics: `extern` in `panel.h` or a state struct | `extern` (177 one-line edits); a struct is a rewrite |
