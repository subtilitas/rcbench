# Changelog

Notable changes to rcbench. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Commit-level
history is in git.

## Unreleased

## 0.4.0 - 2026-09-05

A board the panel has never met is now usable without reflashing the panel:
it says which pins it has, where they are, which pads are grounds and rails,
and what it looks like.

### Added

- **A board describes itself over the link.** Four read-only pages, and each
  one degrades on its own rather than taking the others with it. `CATALOGUE`
  (0x24) carries which GPIOs the board brings out, the pad number printed
  beside each and what holds the ones an output may not have. `SHAPE` (0x25)
  carries the outline, the pitch and the corner pad 1 sits at, which is what
  turns a pad number into a position. `PADS` (0x28) carries the grounds and
  the rails, with each rail's voltage in tenths of a volt. `ARTWORK` (0x26)
  and `ART_DATA` (0x27) carry a photograph of the board, 62 bytes at a time.
  A coprocessor that serves none of them behaves as one built before them
  did: the panel offers nothing for that board, which is what it did before.
- **The pin picker**, behind PICK A PIN on the Setup screen. The board drawn
  with a button on every pin an output may have, each on a straight trace to
  its own pad, because at any size that fits a 480-pixel panel a pad is under
  40 px across and smaller than a fingertip. A pin the coprocessor reserves
  gets no button and a cross on the pad. A board that reports no shape is not
  drawn at all: a picture from a guessed form factor points at the wrong pad
  as confidently as the right one.
- **Photographs are fetched once and kept.** A 2 MB `boardart` partition in
  the panel's flash, eight slots of 256 kB. A transfer costs about ten
  seconds of link and happens once per board; it runs in 15 ms slices of each
  50 ms poll and the flash write runs on its own task, because the control
  task beats the safety line and its ceiling is 150 ms.
- `tools/gen_board_art.py` turns the PNG beside a board into a checked-in C
  array, the `gen_font.py` convention: no Python in any firmware build, and
  `--check` in CI so the two cannot drift.
- [First run on hardware](docs/FirstRun.md), a nine-step bench guide for the
  first time both boards are powered with the heartbeat wire fitted.
- The coprocessor build fails when the image would not fit the module fitted.
  The linker measures against the board file's 16 MB while the bring-up
  module has 4 MB, so it would accept an image that does not boot.

### Changed

- **Every protocol has its own pin set.** `outbind_t` held one protocol and
  one set of pins, so binding a second protocol meant unbinding the first.
  Choosing a protocol now says which set is being edited and trims nothing;
  a pin another protocol holds is drawn grey with the holder's name, which is
  a different fact from a reserved pin's red. Slots fill in pin order across
  every protocol, so a bench wired to one protocol writes the page it always
  wrote. Slots and channels are one budget of eight each, shared.
- Protocol minor 1 to 5. Every change is an added page; an older panel and a
  newer coprocessor, or the reverse, still bring the link up.
- `log_csv_analyse` 241 lines to 36 and `log_csv_build` 221 to 22, split into
  named pieces with behaviour unchanged.
- The artwork fetch sequence moved from the panel into
  `shared/artwork/art_fetch.c` with the link passed in, so the block order and
  every failure path are exercised by the host suite instead of by nothing.

### Fixed


- Both images reported firmware 0.0.0. The identity page has carried
  firmware major, minor and patch since the page map was written and nothing
  ever set them, so a board asked what it was running answered wrongly and
  answered confidently. `shared/link/include/rcbench_version.h` holds the
  three numbers once; the coprocessor publishes them and the panel prints
  what came back on its bring-up line, beside the protocol version. The panel
  is the host and publishes no identity of its own, so it puts its own build
  on the first splash line.
- `tools/check_docs.py` holds that header to the newest heading in this file,
  so a release cut without moving it fails the build rather than shipping a
  board that misreports itself.
- `outbind_learn_board()` wrote pins into its slot as it parsed, so a
  catalogue refused part way left half of itself over the board already
  learned while reporting failure.
- A board numbered from its right-hand corner had its pad row mirrored across
  the outline rather than reversed along itself, which moves the whole row by
  a hundredth of a millimetre when the centring remainder is odd.
- The picker repainted every frame: it cleared the other framebuffers on each
  paint, which with two buffers alternating never settles. Its chrome is a
  photograph, so that held the whole panel at 13 frames a second for as long
  as the screen was open.
- `link_artxfer_begin()` compared width times height times two against the
  byte count in 32-bit arithmetic. 65535 x 32769 x 2 wraps to 65534, a length
  that agrees with a plausible block count, so such a page would transfer,
  pass its checksum, and hand back 65534 bytes calling themselves a
  65535 x 32769 image.
- `art_store_put()` persisted an entry whose width and height did not match
  its byte count, which is what later code sizes a buffer from.
- `art_fetch_meta()` returned a byte count it had not validated, so a
  coprocessor disagreeing with itself got as far as an allocation before
  being refused.
- The `.clang-tidy` note said every header uses `#pragma once`. Forty do and
  eighteen use `#ifndef` guards.

## 0.3.0 - 2026-09-04

The coprocessor drives a pin, and remembers which one.

### Added

- Four output drivers on the coprocessor: servo PWM on the hardware PWM
  slices, PPM from a PIO program fed by a pair of DMA channels that retrigger
  each other, DShot, and bidirectional DShot with the turnaround and the
  reply capture inside the PIO block. `shared/dshot/` carries the frame, the
  group code, the checksum, the period-to-speed arithmetic and the sampler
  that resynchronises on every transition; `shared/ppm/` carries the frame
  layout. Both are host-tested. [docs/DShot.md](docs/DShot.md) says what has
  not been confirmed against an ESC.
- Bidirectional DShot is driver 4 on the OUTPUTS page, not a flag on driver
  3: it inverts the line and the checksum, so an ESC set up for one protocol
  ignores the other.
- CONTROL register 3, MOTOR_POLES. An ESC reports electrical periods and has
  no idea what it is bolted to, so the magnet count is the one number the
  wire has to carry for a mechanical speed to exist. The panel sends it from
  the `Motor poles` setting when the coprocessor answers; at zero the
  coprocessor reports no speed rather than one derived from a guess. Protocol
  version 2.1.
- The output bank refuses a pin the build has reserved. The pin in an OUTPUTS
  slot is whatever an operator typed, and the coprocessor reserves the safety
  line, the CAN controller's five pins, and every number above the last GPIO
  the part has.
- The coprocessor's capability word reports servo PWM, ESC drive and ESC
  telemetry, which the panel marks its menu from.
- An outputs screen behind Setup: a protocol list and a grid of the 26 GPIOs
  the header brings out, ticked to bind. Each ticked pin becomes one slot on
  the OUTPUTS page in pin order. Reserved pins are shown and refused, with
  what holds them under the name; `shared/outputs/out_bind.c` carries the
  catalogue, the rules and the mapping both ways, host-tested.
- The output binding lives in the coprocessor's flash, not the panel's. A
  binding describes wiring and the panel is not the board the wires are in, so
  the coprocessor restores it at boot and the panel reads it back when the
  link comes up. Restoring configures the outputs and does not drive them, and
  channel commands are not restored. The save waits for the bank to stop
  driving, because writing flash stops the core for longer than the
  heartbeat's window.

### Changed

- The coprocessor no longer models the bench. It publishes what it measures,
  with a valid bit per quantity and no SIMULATED flag; with no measurement
  front end fitted that is rpm from a bidirectional DShot ESC and nothing
  else, and the other fields are drawn empty. The panel still models the
  whole bench while nothing answers, and marks that with the watermark, so a
  modelled number and a measured one never appear on one screen.


### Known limitations

- No driver has been seen on a pin. Bit timings, the PPM DMA ring and the
  bidirectional turnaround are what a host test cannot reach.
- The coprocessor's flash store has never run: the erase window, the heartbeat
  re-acquiring across it, and the sector surviving a power cycle are all
  unmeasured.
- `Output` and `Output pin` are still in the Setup table and no longer do
  anything; the outputs screen replaced them.

## 0.2.1 - 2026-09-03

Arming is a deliberate gesture, and the first release cut from a tree whose
CI passes.

### Changed

- ARM is a two-second hold. The fill fades from the OK green to the danger
  red across the hold and the command goes when the fade completes, so
  letting go early arms nothing and the release itself arms nothing. A press
  is a gesture an elbow can make. Disarming stays a press: stopping never
  needs a hold.
- Arming flashes the whole button twice -- white, black, the danger red it
  settles on, and again, one drawn frame each, about 154 ms at 39 Hz. What
  was there decayed from white over 180 ms, which is a fade and reads as one.

### Fixed

- The release after a hold disarmed what that same press had just armed.
  `motor_screen_set_armed()` cleared the flag remembering that this press was
  the one that armed, and the application calls it between the hold
  completing and the finger lifting, so the release read as a fresh press on
  DISARM.
- `tools/frame_cost.py` failed ruff on an 81-character line, and had done
  since the per-screen modes were added.
- `tools/frame_cost.py --check-doc` failed on CI and not locally. Its
  tolerance is for machine-to-machine variation and was 1%, which was enough
  only while the row pattern skipped every hyphenated mode; the largest of
  them drifts about 275 fills of 22,700 between machines. It is 2%.
- The coverage table in `STATUS.md` did not follow `motor_screen.c`.

### Known limitations

- The coprocessor refuses to arm while it is connected: it reads the
  heartbeat on GP3 with a pull-down and the panel drives GPIO6 on J8, and
  nothing joins them. The interlock is working; the wire is not fitted.
- The control task has not run on hardware.
- RESET PEAKS clears the panel's copy and the next poll restores it.
- The link error count is the CAN controller's error counters, which decay as
  the bus recovers and stop at bus-off.
- A settings save disturbs the picture for the length of the write.
- No output driver produces a signal on a pin.

## 0.2.0 - 2026-09-03

The panel runs the bench from a task of its own, and the screen from what is
left. Three defects that made the panel unusable on hardware are fixed, and a
multi-agent review of the result found twenty more.

### Added

- A control task pinned to the core that does not draw owns touch, STOP,
  arming, the outputs, the link and the heartbeat, at a fixed 5 ms. Drawing
  keeps `app_main`. A frame that costs 50 ms no longer rate-limits steering.
- `shared/safety/arming.c` holds the rules that decide whether the bench may
  be armed, as a state machine over timestamps with no driver, link or task,
  and `test_arming` holds nine cases over them. The policy was inside the
  control loop, which no test can compile.
- The Motor & ESC screen is laid out in two columns: the plot and the throttle
  on the left, the four readouts and the controls on a rail to the right. A
  strip above both carries the poll rate, the link's error count and the ESC,
  motor and panel-die temperatures.
- The rated kV, the revolutions per minute per volt the motor is turning, and
  EFF, the ratio of the two. The rated value comes from the connected ESC when
  one reports it, through `motor_screen_set_esc_kv()`, and from `SET_MOTOR_KV`
  otherwise. `docs/Screens.md` documents EFF as an estimate and says why.
- A percentage point at each end of the throttle track.
- `ui_slider_painted_rect()`, `ui_slider_release()` and
  `ui_slider_set_tap_to_set()`; `ui_router_stop_live()`;
  `gfx_text_rotated_points()`.
- `tools/frame_cost.py` gains a mode per screen, `-sim` and `-chrome`
  variants and `throttle`. CI holds the per-screen modes to 1,200 fills, the
  `-sim` modes to 2,800 and the `-chrome` modes to 45,000.

### Changed

- The throttle moves by the distance a finger travels rather than to where it
  lands: a press on the track commands nothing, so a touch at the far end
  cannot ask for full travel in one contact. Sliders that command nothing
  dangerous keep tap-to-set.
- A disarm returns the throttle to zero.
- An armed bench carries the danger red. ARM fades from green to red over
  350 ms while held and flashes once for 180 ms as the arm takes effect.
- The band's clock times the run rather than the panel's uptime.
- Each readout follows the bench page's flag for its own channel, so a
  coprocessor that measures nothing shows a gap rather than a zero.
- The panel loads its settings before `display_init()`.
- `CONFIG_LCD_RGB_ISR_IRAM_SAFE` and `CONFIG_LCD_RGB_RESTART_IN_VSYNC` are
  off; `SET_MOTOR_KV` defaults to 0 rather than 920.

### Fixed

- The panel looped through the splash. The bounce-buffer refill reads PSRAM
  through the data cache, which a main-flash operation closes, and formatting
  an empty NVS partition did exactly that while the panel scanned. The core
  panicked with `Cache disabled but cached memory region accessed`.
- The picture sat 17 px to the left with each line's tail wrapped one line
  down. `CONFIG_LCD_RGB_RESTART_IN_VSYNC` restarts the DMA every vertical
  blanking interval, and each restart empties the LCD FIFO and then resumes
  from a link that skips `LCD_LL_FIFO_DEPTH + 1` pixels the FIFO no longer
  holds.
- The SIMULATION watermark cost 8,412,078 instructions per frame, drawn at
  8.9 fps. Its stencil is the same set of pixels every frame; recording them
  once costs 144,721.
- The heartbeat stopped for up to 1000 ms. `beat()` shared a loop with a link
  exchange that spins to `LINK_HOST_TIMEOUT_MS`, against a 150 ms ceiling.
  `control_pump()` runs inside that wait.
- A latched stop could not be cleared while a coprocessor was attached: the
  latch suppresses the heartbeat, the coprocessor refuses to arm without one,
  and the latch cleared only after a successful arm.
- A tap on the splash latched STOP, which draws no STOP to press.
- A stop could be dropped by a full command queue, and by a test-then-clear
  that lost one arriving between the two.
- A drag whose release never arrived stayed latched, and a later contact
  applied its distance to a stale origin.
- The simulator and the log ran twenty times slower than the wall clock while
  the link was down, and a timed-out poll republished stale readings as a
  fresh plot column and a log row.
- The plot advanced at the frame rate rather than the sample rate.
- `s_bring.have_status` was never set, disabling two link diagnoses.
- `ui_router_invalidate()` reached six screens of ten.
- `tools/frame_cost.py --check-doc` skipped every hyphenated mode.
- The throttle survived a disarm and was re-applied on the next arm.
- The full-panel clears squared off the panels' chamfered corners, and the
  throttle readout's ghost and the slider thumb's shadow were derived from a
  background they no longer sit on.

### Known limitations

- The control task has not run on hardware. `firmware/panel/main/main.c` is
  not in the host suite.
- RESET PEAKS clears the panel's copy and the next poll restores it.
- The link error count is the CAN controller's error counters, which decay as
  the bus recovers and stop at bus-off.
- A settings save disturbs the picture for the length of the write.
- No output driver produces a signal on a pin.

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
