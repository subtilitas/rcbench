# Code size plan: files over 1,000 lines

Base: main edd813d6 (2026-10-16). It replaces the plan of 2026-10-12 on base
840360f8. It rests on four measured group reports: firmware, tools,
configurator and docs; screens and text tables; KST, PD mini, link modules
and out_bind; servo and ESC engines, gfx, logs and settings. Every figure
below is measured on edd813d6 unless it says "about", "not measured" or
"on main". "On main", the Status section and the lists of what stays over
1,000 lines are measured on main 14337c92 (2026-10-17).

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

On main 14337c92 (2026-10-17). The owner took every fork recommendation
(F1 to F25) and chose row 22's design. On 2026-10-17 the owner accepted the
deviations reported by then, row 12's cut among them. 34 of the 35 rows are
merged, one at a time, each with green CI on a head that contained current
main. Row 18 waits for the next release (F12); no release follows v0.20.0
yet. Dates are UTC.

| Row | PR | Merge commit | Merged |
| --- | --- | --- | --- |
| 0a | #416 | d869c5b0 | 2026-10-16 |
| 0b | #417 | dcb6f9a3 | 2026-10-16 |
| 0b (`PORT_CALLS`) | #418 | d13c7fa2 | 2026-10-16 |
| 1 | #420 | eb8a9323 | 2026-10-16 |
| 2 | #419 | b7816fe6 | 2026-10-16 |
| 3 | #421 | 453cf334 | 2026-10-16 |
| 4 | #435 | 7ac34202 | 2026-10-17 |
| 5 | #426 | 496f0aa0 | 2026-10-17 |
| 6 | #424 | be1d843e | 2026-10-17 |
| 7 | #434 | 19ce055a | 2026-10-17 |
| 8 | #423 | 8e6f657e | 2026-10-17 |
| 9 | #436 | be8c1771 | 2026-10-17 |
| 10 | #433 | 751eb9ab | 2026-10-17 |
| 11 | #437 | b7bbe94e | 2026-10-17 |
| 12 | #442 | 86e16116 | 2026-10-17 |
| 13 | #430 | 7b18ee14 | 2026-10-17 |
| 14 | #425 | 6df6dc79 | 2026-10-17 |
| 15 | #428 | 8597670a | 2026-10-17 |
| 16 | #432 | af30b6ed | 2026-10-17 |
| 17 | #429 | 6f15c208 | 2026-10-17 |
| 18 | none | waits for the next release | - |
| 19 | #443 | 58f6ac05 | 2026-10-17 |
| 20 | #444 | 84824773 | 2026-10-17 |
| 21 | #446 | 1083e430 | 2026-10-17 |
| 22 | #449 | 1edf01f3 | 2026-10-17 |
| 23 | #441 | 6d5c9cbe | 2026-10-17 |
| 24 | #448 | b17075f2 | 2026-10-17 |
| 25 | #450 | e9e20986 | 2026-10-17 |
| 26 | #453 | 48053d16 | 2026-10-17 |
| 27 | #452 | 2a579c82 | 2026-10-17 |
| 28 | #439 | 7c56be9f | 2026-10-17 |
| 29 | #427 | b57fcf09 | 2026-10-17 |
| 30 | #431 | ea828cb6 | 2026-10-17 |
| 31 | #445 | c14dc3c3 | 2026-10-17 |
| 32 | #438 | d41f777b | 2026-10-17 |
| 33 | #440 | f3f0a8c3 | 2026-10-17 |

Merges beside the rows that touch their files: #422 (ea7fdf9a, text
rework; it shifts the line ranges of screens and tests, `kst_panel.c`
3,134 to 3,588), #447 (4364ad24) and #451 (dfe5561d) for the `IOBoard.md`
citations, #454 (b8dc8b1d: `stack_check.py` measures a cycle exactly, the
`runlog` stack goes from 4,352 to 4,608 bytes), #455 (09cc2011: a HOLD
keeps the release it owes, in `servo_link.c`).

Measured against the rows above:

- 0a: about 200 citations in 4 hardware docs (`hardware/docs/IOBoard.md`,
  `hardware/STATUS.md`, `hardware/docs/Control.md`,
  `hardware/docs/Supply.md`), not the counts listed. The seven
  `ui_text.def` citations name revision `c7f4ff9a^`, which holds the
  cited text (owner, 2026-10-17; #451). The `STATUS.md:N` citations in
  `IOBoard.md` are outside the row; #462 cites them by section and item
  name.
- 0b: `PORT_CALLS` follows `cu_port_exchange`; no measured depth changed.
  F4's "no lazy import" is about `stack_check.py`, `stack_elf.py` and
  `stack_iomcu.py`; the one function-level import, the standard library's
  `bisect` in `code_spans()`, is not that case.
- 2: the ARM branch holds about 15 lines of decisions; the rest is I/O.
  Only the decisions move (`arming_handshake_step()`), +39/-18 in `main.c`.
  `control_task` stays at 4,288 of 5,120 bytes. The row's cases take
  `test_arming.c` to 2,005 lines.
- 1 and 3: core 0's deepest chain goes from 1,716 to 1,700 bytes (1,612
  spare below the margin); each PR alone measured 1,708.
- 3: +340/-88 over 13 files; `shared/bench` depends on `protocols/dshot`.
  The row's cases take `test_bench.c` to 1,030 lines.
- 4: 45 translation units take `servo_str.h` (plan 44 + 8). Through these
  functions the panel main task has 2,416 bytes spare, not 656 (#435).
- 6: `gfx_pixel`, `gfx_pixel_get` and `gfx_hline` are `static inline` in
  `gfx_int.h`, so `draw_glyph` keeps them inlined.
- 9: 220 cases = 144 + 48 + 21 + 7. `test_servo_arm.c` keeps its own
  fixture and does not take `servo_rig.h`: taking it rewrites that
  fixture, which is not a move (#436).
- 10: 73 cases (#422 adds 2) = 13 + 60. 11: 132 cases = 89 + 17 + 14 + 12.
- 12: 31 card cases, 122 stay; `test_kst_prog.c` 5,401 lines,
  `kst_prog_fix.h` 512. The criterion is in the row (#442).
- 13: 83 cases = 8 + 13 + 32 + 16 + 14 (plan 8/9/34/15/17): each case is
  in the suite of the part it tests.
- 14: core 1's chain is 640 bytes on CI's GCC 13.2.1, 644 on ARM GNU 14.2.
- 19: `programmer_screen.c` 992, `esc_stick_panel.c` 3,451,
  `programmer_screen_int.h` 311. The shared `programmer_shared_t` has 15
  fields; the stick reads 14 of them 105 times.
- 20: `servo_settings_panel.c` 1,341 lines (plan 1,204): its drawers move
  with it (#444). The seam `servo_screen_int.h` (969 lines) holds 42
  `extern` prototypes, 35 `static inline` functions and 2 objects.
- 21: `servo_test_panel.c` 675 lines (plan 801): the limit run's parts go
  to row 31 (#446). 31: `servo_limit_panel.c` 653; `servo_screen.c` 3,613
  (plan 4,291): 3,653 after row 31, and row 22 takes `servo_cmd.c`'s part.
- 22: see the row. `servo_link.c` 1,393 lines; the control task's deepest
  chain grows by 16 bytes (#449).
- 23: `main.c` 945 lines (plan 843): the console report stays. 31
  variables become `extern` (plan 27), each with a user in another file.
  8 helpers become `static inline` in `iomcu.h`; `reserve_held()` has an
  out-of-line copy before and after, the other 7 have none. Core 0's
  chain: 1,700 bytes on CI's GCC 13.2.1, 1,692 on ARM GNU 14.2.
- 24: `panel_card.c` 556 and `panel_log.c` 658 lines (plan 930 and 710):
  `poll_identity()` and `bring_up()` stay in `main.c` as boot code (#448).
- 25: 640 + 822 + 671 lines; the servo section's statics lose `static`
  for row 22 (#450).
- 26: 658 + 703 + 505 + 366 lines. `DISARM_INHIBIT_MS` stays file-local;
  `s_arm_take_*` are `extern` in `main.c`; `log_follow_runs()` and
  `s_log_t` lose `static` (#453).
- 27: `panel_update.c` 948 lines (plan 876). Panel `main.c` ends at 2,422
  (plan about 2,214) because of row 24's boot code.
- Final stack figures (`stack_check.py --check-doc`, after #454): control
  4,384 of 5,120 bytes, main 5,440 of 7,168, `runlog` 3,408 of 3,584.
- The host suite builds 155 binaries (plan 123 to about 155).
  `esc_parity` is not registered in a build configured by `coverage.py`,
  by design: its cases follow Python's random module.

## Follow-ups (owner, 2026-10-17)

- `servo_link.c` stays at 1,393 lines: one state machine that carries the
  release a HOLD owes (#455). No second cut.
- CI runs cppcheck 2.17, the version run locally, in place of Ubuntu's
  2.13. 2.17 reads `cppcheck-suppress-macro`, so the whole-file
  `.cppcheck-suppress` entry for `panel_update.c` can shrink to `CU_LEN`.
- `--check-doc` holds two more figures: the PSRAM `.bss` table and its
  total (`ram_budget.py`; on main the build places 35,400 bytes of
  `log_viewer_screen` and 162,904 in all, the doc says 35,328 and 162,832)
  and the coprocessor's count of calls through a register (`stack_iomcu.py`;
  CI prints 133, the doc says 137).
- `tools/check_sizes.py --check` holds the lists of files over 1,000 lines
  below.
- #462: the `IOBoard.md` citations by name, the comment at the
  `s_servo_minor` store in `servo_link_came_up()`, `limrun_stage_t` and
  `prog_stage_t`, and a host suite that holds the telemetry age in
  `outputs_hw.c` across the 2^32 ms wrap.

Next: row 18, in the PR right after the next release.

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
   suite. A variable every includer uses needs neither (`kst_prog_fix.h`).
6. No new upward include. Protocol cores stay portable.
7. One author and one adversarial reviewer per PR, and green CI on a head
   that contains current main. Merges go one at a
   time because every PR edits the same lists.

## Wave 0: preparation

| PR | Change | Size |
| --- | --- | --- |
| 0a | Hardware-doc line citations become function or symbol names: 6 panel `main.c`, 32 coprocessor `main.c`, 32 `link_pages.h`, 12 `out_bind.c`, 7 `settings.c`, `ui_text.def`/`programmer_screen.c` in `hardware/docs/IOBoard.md`, `hardware/STATUS.md`, `hardware/docs/Control.md`. Several already point at wrong lines. `Research.md` stays untouched (F21) | about 100 lines of docs |
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
| 9 | `test_servo.c` (220 cases): `servo_rig.h` (about 200 lines; `test_servo_arm.c` keeps its own fixture, since taking the header rewrites it) + `servo_bench_rig.h` (about 430) (F8); `test_servo_settings.c` 48 cases; `test_servo_test_panel.c` 21; `test_servo_limit_panel.c` 7; 144 stay (about 5,660 lines) | about 3,780 / 100 |
| 10 | `test_programmer.c` (71): `programmer_rig.h` (about 85) + `test_esc_stick_panel.c` 58 cases; 13 stay | 3,650 / 70 |
| 11 | `test_kst_panel.c` (125): rig header (about 300) + files 17, no card 14, the servo limit run 12 cases in three suites; 82 stay (3,814 lines) | about 2,630 / 40 |
| 12 | `test_kst_prog.c` (153): `kst_prog_fix.h` (512 on main) + `test_kst_prog_card.c` with the cases whose subject is a card operation: a call of `kst_prog_save()`, `kst_prog_files()` or `kst_prog_load()` as the thing under test, or an assert on the file saved before the first write. A case that only uses a card as fixture stays. On main 31 cases move, measured with `gcov` on `kst_prog_card.c`, and 122 stay (5,401 lines); the estimate was 46 and 107 (4,557 lines) | 2,680 / 80 |
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
| 22 | Panel servo link client into `shared/outputs/servo_link.c`, beside `bind_link.c`, behind `link_port_t` and a `servo_link_ctx_t`: the link host, the port, the PROG page and 6 callbacks into the panel (armed, stopped, alert, disarm, stop count, arm surfaces). Each callback and the port's clock is called from one `noinline` helper (`sl_*`), which `stack_check.py` follows through `SERVO_CALLS`. `servo_cmd_t` and the `servo_rate_*` declarations move from `servo_screen.h`/`.c` into `shared/outputs/include/servo_cmd.h` and `servo_cmd.c`. The cut takes the servo section less the pin-display statics, `servo_let_go`, `servo_service`, `apply_servo_cmd` and the servo parts of `link_came_up`, `prog_link_service`, the link-loss edge and `read_outputs_binding` (owner, 2026-10-17). Not `shared/bench`: the client needs `servo_cmd.h` and the outputs types, and `shared/outputs` and `shared/ui` depend on `shared/bench`, so a file there closes an include cycle (rule 6). `s_servo_hz_sent` gets the getter `servo_link_hz_sent()`; `test_servo_link.c` has cases for the sweep version gate, the refusal while a release is owed and the rate retry. On main: `servo_link.c` 1,393, `servo_cmd.c` 53, `servo_link.h` 211, `servo_cmd.h` 181, `test_servo_link.c` 1,003 lines | about 1,100 / real not measured |
| 23 | Coprocessor `main.c` into 7 files + `iomcu.h`: pages + PROG 436, KST 165, core 1 288, tap 302, BIND 287, heartbeat + CAN 246; `main.c` keeps 843; 27 statics become `extern` | 1,723 / about 150 |
| 24 | Panel `main.c`: `panel.h` + `panel_card.c` (930) + `panel_log.c` (710); `ram_budget.py` reads `panel.h` for the queue macros (F3, about 10 lines and one test case) | 1,640 / about 150 |
| 25 | Panel: `panel_link.c` (726) + `panel_poll.c` (807) + `panel_services.c` (604) | 2,137 / about 120 |
| 26 | Panel: `panel_supply.c` (723) + `panel_arm.c` (814) + `panel_render.c` (481) + `panel_art.c` (277 + 65 drawing statics) | 2,360 / about 150 |
| 27 | Panel: `panel_update.c` (876; coprocessor update, Wi-Fi, GitHub update, `cu_render_service`) (F2) | 876 / about 60 |

After PRs 22 to 27, panel `main.c` keeps about 2,214 lines (2,422 on
main): plumbing 965 and `app_main` 740 are one web of queues and atomics.
On main `panel.h` declares 143 `extern` statics (F25; estimated at 177):
row 22 takes the 16 servo variables into `servo_link.c`, and row 24 moves
37 to their one user. The `ci.yml` cppcheck pass takes
`git ls-files 'firmware/**/*.c'`, so a new file needs no line there. Each
PR builds the injector image.

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

Figures on main 14337c92.

- **Screens:** `kst_panel.c` 3,588 (largest section 963 lines, F9),
  `log_viewer_screen.c` 2,245, `supply_screen.c` 2,079, `motor_screen.c`
  1,504, `picker_screen.c` 1,303, `settings_screen.c` 1,117,
  `wifi_screen.c` 1,002 (988 at the base; #422's text): one screen each;
  a cut is a design change.
- **Tables:** `ui_text.def` 1,508, `ui_text_de.c` 2,107 (a split needs
  `check_docs.py` and `check_formats.py` to follow includes, and it collides
  with #400; F10); `link_pages.h` 1,960 and `test_link_pages.c` 1,674 (one
  register map).
- **Modules:** `servo_test.c` 1,304 (one state machine, 14 of 24 statics
  cross), `kst_port.c` 1,051 (path in `check_protocols.py`), `kst_file.c`
  1,072 (owner answer), `settings.c` 1,021 (21 over; 3 `check_docs.py` paths,
  and a move edits `Research.md`; F19), `wifi_access.c` 1,712 (one state
  struct used 275 times; macros read by path).
- **Tools:** `check_docs.py` 1,475, `sense_trace.py` 1,456,
  `test_sense_trace_tool.py` 2,552, `help_site.py` 1,193,
  `test_check_protocols.py` 1,999, `render_screen.c` 3,126 (F11).
- **Configurator:** `catalogue.ts` 2,432 (largest table 696 lines),
  `check.ts` 1,120.
- **Docs:** `Screens.md`/`-de` 3,062/3,337 (F20), `ServoProgrammer.md`/`-de`
  1,192/1,320, `Servo.md`/`-de` 1,182/1,195, `FirstRun-de.md` 1,042,
  `Research.md` 1,028 (F21), `Link.md`/`-de` 1,397/1,516 (F23: the firmware
  update sections stay on the page).
- **Tests with no cut that lands both halves under 1,000** (F14):
  `test_kst_port` 4,182, `test_kst_session` 2,985, `test_kst_link` 2,652,
  `test_pdmini` 3,008, `test_link_update` 2,319, `test_nav` 3,139,
  `test_sense_sched` 1,990, `test_arming` 2,005, `test_safety_gate` 1,700,
  `test_esc_profiles` 1,564, `test_logwriter` 1,411, `test_gfx` 1,029,
  `test_bench` 1,030 (row 3's cases), and the suites of the group reports:
  `test_logview` 3,510, `test_supply_screen` 2,304, `test_tone` 2,214,
  `test_bind_link` 2,137, `test_outputs` 2,090, `test_motor` 2,018,
  `test_kst_file` 2,017, `test_tone_link` 1,999, `test_picker_screen`
  1,893, `test_outputs_screen` 1,854, `test_sense_page` 1,777,
  `test_kst_plan` 1,760, `test_kst_reg` 1,394, `test_servo_arm` 1,173,
  `test_widgets` 1,164, `test_servo_page` 1,158, `test_kst_pio` 1,096,
  `test_prog_port` 1,079, `test_wa_session` 1,028, `test_wa_fetch` 1,014.

## What stays over 1,000 lines after the plan

`wc -l` over the tracked files on main 14337c92, outside
`hardware/research/`, JSON files and test fixtures, gives 91 files over
1,000 lines.

Generated files do not count: `shared/esc/esc_profiles_gen.c` 4,948
(`gen_esc_profiles.py --check`), `shared/gfx/gfx_font16x28.c` 2,987 and
`shared/gfx/gfx_font8x16.c` 1,763 (`gen_font.py --check`). CI holds each to
its generator, so a split by hand is overwritten by the next run.

Of the other 88, 67 are the files left whole above. The other 21 are the
parts the rows leave:

- **Sources a row splits:** panel `main.c` 2,422 (plumbing and `app_main`
  are one web; row 24's boot code stays); `servo_screen.c` 3,613 (the sweep
  core reads 98 fields of the state); `esc_stick_panel.c` 3,451 (its 16
  drawers, about 1,470 lines, stay, F24); `kst_prog.c` 2,407 (what row 7
  leaves); `servo_limrun.c` 1,044 (what F18 leaves).
- **Sources a row creates:** `servo_link.c` 1,393 (row 22; one state
  machine with the release a HOLD owes, no second cut: owner, 2026-10-17);
  `servo_settings_panel.c` 1,341 (row 20; one overlay with its drawers).
- **Test suites, one per module (F14):** `test_servo.c` 5,681 (144 cases
  after row 9), `test_kst_prog.c` 5,401 (122 cases after row 12),
  `test_kst_panel.c` 3,793 (89 cases after row 11), `test_esc_stick_panel.c`
  3,707 (row 10's 60 cases), `test_servo_settings.c` 1,635 and
  `test_servo_test_panel.c` 1,110 (row 9, one panel each),
  `test_kst_prog_card.c` 1,355 (row 12's 31 card cases),
  `test_servo_test_win.c` 1,073 (row 16's 15 window cases on one rig),
  `test_servo_link.c` 1,003 (row 22's module).
- **Docs and the configurator:** `CHANGELOG.md` 3,467 (the archive trigger
  is 5,000 lines, F22), `CHANGELOG-0.9.md` 1,482 (row 32's archive; it does
  not grow), `catalogue.test.ts` 1,210 (what row 33 leaves).
- **Row 18, after the next release:** `out_bind.c` 1,123 and
  `test_outbind.c` 2,154.

`programmer_screen.c` ends at 992 lines (row 19) and is under 1,000.

## Size of the job

| Item | Figure |
| --- | --- |
| PRs | 35 (wave 0: 2, wave 1: 3, wave 2: 10, wave 3: 6, wave 4: 4, wave 5: 6, release-timed: 1, wave 6: 3) |
| Code moved | about 57,000 lines |
| Real change | about 3,000 lines (estimate), of which about 900 are new tests |
| New test binaries | about 32 (123 to about 155); 155 on main |
| Authors | 4 in parallel; 1 reviewer per PR; at most 6 at a time |
| Merges | one at a time; each PR merges current main and regenerates the coverage tables first |

Order: wave 0, then waves 1 and 2 in parallel. Wave 3 PR 12 after PR 7.
Wave 4: PR 19 after PR 10; PR 20 after PR 9; PRs 21 and 31 after PR 20.
Wave 5: PR 22 after PR 2; PR 23 after PRs 1 and 3; PRs 24 to 27 after PR 22,
in that order. PR 18 right after the next release. Wave 6 at any time.
On main wave 5 merged as 24, 25, 22, 26, 27. Row 22 waited for the owner's
choice of its home (the include cycle in the row), and the owner let rows
24 to 27 start before it, one at a time (2026-10-17). Row 25 declares the
servo section's statics in `panel.h`; row 22 then re-applies its edits in
`panel_link.c`, `panel_poll.c` and `panel_services.c`.

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
| F25 | Panel `main.c` statics: `extern` in `panel.h` or a state struct | `extern` (estimated at 177 one-line edits, 143 on main); a struct is a rewrite |
