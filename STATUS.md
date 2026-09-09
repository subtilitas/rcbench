# Where rcbench stands

The running record: what exists, what is open, what is settled. The suite
list, the coverage table, the compile table and every link are held to the
tree by `tools/check_docs.py` and `tools/coverage.py`. The directory sketch
below is not: nothing checks it, so read it as a map and not as an inventory.

[README.md](README.md) is the front page; the
[wiki](https://github.com/subtilitas/rcbench/wiki) is the manual.

## Architecture

Two processors. The coprocessor (RP2350) owns everything with a deadline; the
panel (ESP32-S3) owns everything else. Nothing raw crosses the link: bit
timing, GCR (group-coded recording) decoding, receiver framing and pulse
capture stay on the coprocessor, and only results travel.

| | Panel | Coprocessor |
| --- | :---: | :---: |
| UI, touch, screens, settings, SD card | ✔ | |
| Stop | drives the heartbeat line; sends the STOP command | honours the line; times out the link |
| Servo outputs, PPM (pulse-position modulation), DShot, bidirectional DShot | | ✔ |
| Receiver buses: S.BUS, iBUS, SUMD, CRSF, EX Bus, SRXL2 | | ✔ |
| One-wire programming | | ✔ |
| Current, voltage, cells, rpm (revolutions per minute), temperature | | ✔ |
| Accelerometer and index pulse, one timebase | | ✔ |
| Balancing arithmetic, plots, verdicts | ✔ | |
| Power path, current limiting, protection | | ✔ |
| CAN | TWAI (Two-Wire Automotive Interface, the ESP32-S3's CAN controller) controller | XL2515 over SPI (Serial Peripheral Interface) |

**Link.** Classic CAN at 1 Mbit/s. A 29-bit identifier carries priority, op,
page, offset and count; a frame carries up to four registers; the transport
does no reassembly; the coprocessor transmits only when asked. Worst-case
payload 52 kB/s against 12 to 30 kB/s of expected traffic. Protocol version
3.0. [Reference](docs/Link.md).

**Safety.** The panel's control task drives GPIO6 (J8) from the core that does
not draw. The task runs every 5 ms; the line edges every 20 ms
(`HEARTBEAT_PERIOD_MS`). The coprocessor's
outputs are to be gated by a retriggerable monostable with a window of about
150 ms, and the line is checked in firmware (4 to 150 ms between edges, four
good intervals before it is trusted). The coprocessor fails safe after 200 ms
of link silence; the panel escalates after 1 s. [Reference](docs/Safety.md).

**Outputs.** Every output is a channel (0 to 1000 of its own travel) with a
role (throttle or surface) rendered by a driver from a table (PWM (pulse-width
modulation), PPM, DShot, bidirectional DShot). Arming, clamping, slew and the
silence timeout are implemented once, in `shared/outputs/`, and used by both
processors. The role is what a screen's command looks for: the MOTOR & ESC
throttle commands every channel bound as a throttle, so an ESC on a bound pin
follows the slider and a servo beside it does not. On the wire: the
`CHANNELS`, `CHAN_CFG` and `OUTPUTS` pages. The
pin is in the slot, not in the firmware: the coprocessor refuses the pins it
must not drive and binds the rest as they arrive. An armed bank drives every
bound pin whether or not anything commands it: a channel nobody has commanded
for 500 ms is rendered at its role's rest, which for a surface is the midpoint
of that channel's own endpoints -- 1500 us across the default 1000 to 2000 us,
and 760 us across the 660 to 860 us of a narrow servo. The timeout moves the
channel to that rest and leaves the pin driving; a disarm is what stops it.
[Reference](docs/DShot.md).

**Measured or modelled.** A coprocessor that answers reports only what it
measures, with a valid bit per quantity and no `SIMULATED` flag. The panel
models the whole bench when nothing answers, and marks that with the
watermark. The two never appear on one screen.

**More than one coprocessor.** The coprocessor reports which board it is in
the identity page's hardware register, and the panel offers that board's pins
and no other. A board this build does not know offers nothing and reserves
every pin: guessing a pin map is how an output reaches the safety line.
Changing board clears any selection, because a pin index belongs to one
board's catalogue. A board whose outputs are soldered is shown rather than
offered.

**The output binding lives on the coprocessor.** A binding describes wiring,
and the panel is not the board the wires are in. The coprocessor keeps the
`OUTPUTS` and `CHAN_CFG` pages in its own flash and restores them at boot; the
panel stores none of it and reads the page back when the link comes up.
Restoring configures the outputs and does not drive them -- every driver is
gated on the bank being armed, which the coprocessor grants only while the ARM
register is set, the link is out of failsafe and the heartbeat is trusted --
and channel commands are not restored, so a bench never comes back holding the
throttle it was last given.
The save waits for the bank to stop driving and then for a gap in the
traffic, because writing flash stops the core answering: two sectors of
sixteen record slots spend one sector erase per sixteen saves, and that erase
is taken in a gap ahead of the save that needs it.

## State

| Subsystem | State |
| --- | --- |
| Rasteriser, fonts, touch mapping, theme, widgets, icons, router | built and tested |
| Settings model and screen | built and tested on the host. The panel loads and saves the values in NVS (non-volatile storage) through `firmware/panel/components/settings_nvs/`, confirmed on hardware: a save writes and the next boot reports what it loaded |
| CSV (comma-separated values) and number parsing, log viewer | built and tested against the fixture corpus |
| Logger | built: a run is written while armed, in the format the viewer reads. The card is written by the `runlog` task, not by the control task that beats the safety line, and the file is committed every 20 rows or 1000 ms of run, so a power cut mid-run costs that much of it plus whatever the queue to that task holds -- under 1.0 s while the card keeps up, 84 rows and 4.20 s at 20 Hz with the queue full |
| Board, display, GT911, SD card | built; the panel boots and reports each step on the splash |
| Shell: band, router, splash, menu, simulation watermark | built |
| Motor & ESC (electronic speed controller) screen | built; reads `bench_state` from the link or the simulator. ARM, DISARM, STOP and the throttle are written to the coprocessor's control page at every 50 ms poll while the link is up; an arm writes CLEAR first and a NACK leaves the panel disarmed. An arm and a throttle have gone through it on the bring-up bench and run a motor; the paths a session has to provoke -- a NACK, a STOP mid-throttle, a link pulled while armed -- have not |
| Servo screen | built; writes `CHAN_CFG`, `OUTPUTS` and `CHANNELS` over the link |
| Analyser, programmer, balance, battery screens | built, rendered from models |
| Link codec, page map, dispatcher, both watchdogs, CAN framing | built and tested on the host |
| CAN drivers (TWAI on the panel, XL2515 on the coprocessor) and the echo self-test | built; run on hardware at 1 Mbit/s with zero errors at either end |
| Bring-up diagnosis, both ends' counters compared | built |
| Heartbeat generator and monitor | built and driven at both ends; the monostable is not fitted |
| Coprocessor firmware | answers the identity, status, control, bench and three output pages; fails safe at 200 ms. It publishes what the ESC reports over extended DShot telemetry -- speed, voltage, current, power and the ESC's own temperature -- and nothing the bench measures itself, because no measurement front end is fitted |
| Output drivers (PWM, PPM, DShot, bidirectional DShot) | built; the frame arithmetic, the group code, the reply sampler and the GPIO-to-PWM-slice fold are host-tested. PWM has swung a servo and plain DShot has run a motor from the panel on the bring-up bench, with no instrument on either pin. PPM and bidirectional DShot have driven nothing. [Reference](docs/DShot.md) |
| S.BUS decoder | built and tested; the PIO (programmable input/output) receiver is not written |
| Motor pole count over the link | the panel sends `Motor poles` on the CONTROL page when the coprocessor answers; with none sent the coprocessor reports no speed rather than one derived from a guess |
| Outputs screen | built and tested on the host: a protocol list and a pin grid behind the Setup screen's OUTPUTS key, writing `CHAN_CFG` and `OUTPUTS` on every change and reading the binding back from the coprocessor. Reserved pins are shown and refused. Run on hardware: the bindings behind the servo and motor runs were made here, and the nine saves that produced `FAULT 01` were an operator ticking pins on it |
| Output binding in the coprocessor's flash | built, not run on hardware: the last two sectors of the first 4 MB as 32 record slots, restored at boot, saved once the bank is idle and the bus quiet. A save is one page program; a sector erase falls to one per sixteen saves and is taken ahead of the save that needs it, or at boot. The placement rules are host-tested in `test_outstore`; `firmware/iomcu/src/out_store.c` and `firmware/iomcu/src/main.c` compile and have not run on a board. What has run on hardware is the single-sector store this one replaces: eight of its saves printed an erase-and-program window of 19,174 to 19,186 us. The offset is deliberately the 4 MB module's rather than the 16 MB board file's. A record written by an earlier build reads as unwritten, so the first boot on this build starts from the defaults -- no driver and no pin in any slot -- and the binding is gone until an operator sets it again on the OUTPUTS screen |
| Other receiver buses | not started |
| Servo limit search, servo synchronisation | built and tested against a modelled servo |
| OpenYGE codec | built and tested; not wired in. The implementation is pursued in a separate repository |
| Measurement front end | parts selected, nothing fitted: [hardware](hardware/STATUS.md) |
| Servo programmer | Hitec table in the programmer screen; KST (a servo manufacturer) held at the owner's request |

## The tree

```
rcbench/
  README.md  README-de.md  STATUS.md  CHANGELOG.md  CONTRIBUTING.md
  SECURITY.md  LICENSE  NOTICE  .gitattributes  .gitignore
  .github/workflows/      ci · docs · release
  .clang-tidy  .cppcheck-suppress  ruff.toml  codecov.yml
  docs/                   the wiki source, English and German
  tools/                  render_ui · coverage · check_docs · frame_cost
                          gen_font · gen_board_art · wiki_links
  hardware/               board design record: README, STATUS, docs/
  testbench/              the measurement bench: README, WIRING, host scripts,
                          decoders. Nothing on it has been run

  shared/                 pure C: no ESP-IDF, no pico-sdk, no FreeRTOS types
    artwork/              the board picture, stored and fetched over the link
    gfx/                  rasteriser and three fonts
    touch/                coordinate and event mapping
    ui/                   theme · widgets · icons · router · screens
    settings/             typed schema and values
    logfile/              number and CSV parsing · run log names
    link/                 page protocol · CAN framing · watchdogs · diagnosis
    bench/                bench_state · telemetry simulator · log writer
    outputs/              channels · driver table · arming, slew and staleness
                          · where a saved binding goes in flash
    safety/               heartbeat generator and monitor · arming policy
    servo/                limit and synchronisation searches · servo model
    can/                  bit timing · MCP2515 registers · echo self-test
    sbus/                 S.BUS decoder
    dshot/                frames · GCR · eRPM and extended telemetry
    ppm/                  frame layout
    openyge/              OpenYGE framing, status and parameter cache

  firmware/panel/         ESP-IDF
    main/                 main.c · selftest.c
    components/           board · display · gt911 · storage · can_twai
                          settings_nvs · art_flash
    sdkconfig.defaults  partitions.csv

  firmware/iomcu/         pico-sdk
    src/                  main.c · xl2515.c · out_store · art_rp2350_can
                          out_pwm · out_ppm · out_dshot · outputs_hw
                          ppm.pio · dshot.pio
    include/iomcu_pins.h

  test/host/              one suite over shared/
    fixtures/             the CSV corpus the parser is held to
```

### Who compiles what

| Module | panel | iomcu | host |
| --- | :-: | :-: | :-: |
| `gfx` · `touch` · `ui` · `settings` · `logfile` · `sbus` | ✔ | | ✔ |
| `link` · `bench` · `outputs` · `safety` · `can` | ✔ | ✔ | ✔ |
| `artwork` | ✔ | | ✔ |
| `servo` · `openyge` · `dshot` · `ppm` | | ✔ | ✔ |

Each module carries one `CMakeLists.txt` that registers an IDF component under
`ESP_PLATFORM` and a static library otherwise. The panel sets
`EXTRA_COMPONENT_DIRS`; the coprocessor and the host suite use
`add_subdirectory()`. Includes are flat: `#include "gfx.h"`.

## Tests and CI

CI (continuous integration) runs the workflows below on GitHub Actions.

| Workflow | Trigger | What it does |
| --- | --- | --- |
| `ci.yml` | push, pull request, tag `v*`, manual | host suite; the same suite under ASan (AddressSanitizer) and UBSan (UndefinedBehaviorSanitizer); coverage `--check` and the Codecov upload; the font, frame-cost, screenshot, docs and wiki-link checks; clang-tidy, cppcheck and ruff; the ESP-IDF (Espressif Internet-of-Things Development Framework) matrix (v5.4, v5.5) building the panel; the pico-sdk build of the coprocessor; firmware artifacts including a merged panel image for offset 0 |
| `docs.yml` | push to `main` touching `docs/` | publishes `docs/` to the GitHub wiki |
| `release.yml` | tag `v*` | builds both images, packages them with checksums, creates a release |

The host suite is 44 binaries, one line per case: `test_gfx`, `test_touch_map`,
`test_nav`, `test_widgets`, `test_bench`, `test_motor`, `test_servo`,
`test_analyser`, `test_programmer`, `test_balance`, `test_battery`,
`test_settings`, `test_logfile`, `test_link_crc`, `test_link_pages`,
`test_link_watchdog`, `test_link_loopback`, `test_link_bringup`,
`test_link_can`, `test_link_artxfer`, `test_art_store`, `test_art_fetch`, `test_outputs`, `test_outstore`,
`test_can_timing`, `test_can_selftest`,
`test_mcp2515`, `test_heartbeat`, `test_arming`, `test_servo_limit`,
`test_servo_sync`, `test_sbus`, `test_dshot_frame`, `test_dshot_telem`,
`test_ppm`, `test_outbind`, `test_outputs_screen`, `test_picker_screen`, `test_busfault_screen`, `test_openyge_frame`, `test_openyge_status`,
`test_openyge_params`, `test_logview` and `test_logwriter`. The harness is
`test/host/greatest.h`, written for this project. `tools/check_docs.py` holds
this list to `test/host/CMakeLists.txt`.

Coverage floors: 94% overall, 85% for every file except `stub_screen.c`, which
is exempt by name. `tools/coverage.py --check` fails on drift of the table
below. `render_ui.py --check` holds 34 committed screenshots to the current
render; `frame_cost.py` holds a bench frame to 15,600 cache-line fills and a
chrome-cached screen to 2,000.

<!-- coverage:start -->
| File | Lines | Covered | Coverage |
| --- | ---: | ---: | ---: |
| `shared/gfx/gfx.c` | 643 | 618 | 96.1% |
| `shared/gfx/gfx_seg.c` | 96 | 91 | 94.8% |
| `shared/touch/touch_map.c` | 100 | 100 | 100.0% |
| `shared/ui/ui_theme.c` | 42 | 41 | 97.6% |
| `shared/ui/ui_widgets.c` | 200 | 192 | 96.0% |
| `shared/ui/ui_icons.c` | 110 | 110 | 100.0% |
| `shared/ui/ui_band.c` | 34 | 32 | 94.1% |
| `shared/ui/ui_watermark.c` | 42 | 40 | 95.2% |
| `shared/ui/ui_plot.c` | 168 | 158 | 94.0% |
| `shared/ui/ui_hero.c` | 29 | 28 | 96.5% |
| `shared/ui/ui_slider.c` | 160 | 151 | 94.4% |
| `shared/ui/ui_tabs.c` | 57 | 50 | 87.7% |
| `shared/ui/ui_router.c` | 153 | 143 | 93.5% |
| `shared/ui/splash_screen.c` | 60 | 57 | 95.0% |
| `shared/ui/overview_screen.c` | 69 | 64 | 92.8% |
| `shared/ui/stub_screen.c` | 45 | 8 | 17.8% |
| `shared/ui/motor_screen.c` | 432 | 425 | 98.4% |
| `shared/ui/servo_screen.c` | 464 | 432 | 93.1% |
| `shared/ui/analyser_screen.c` | 219 | 213 | 97.3% |
| `shared/ui/balance_screen.c` | 303 | 303 | 100.0% |
| `shared/ui/battery_screen.c` | 178 | 173 | 97.2% |
| `shared/ui/programmer_screen.c` | 316 | 299 | 94.6% |
| `shared/ui/log_viewer_screen.c` | 686 | 626 | 91.2% |
| `shared/ui/log_select.c` | 26 | 26 | 100.0% |
| `shared/ui/settings_screen.c` | 280 | 271 | 96.8% |
| `shared/ui/outputs_screen.c` | 231 | 229 | 99.1% |
| `shared/ui/picker_screen.c` | 313 | 307 | 98.1% |
| `shared/ui/busfault_screen.c` | 296 | 293 | 99.0% |
| `shared/settings/settings.c` | 145 | 139 | 95.9% |
| `shared/logfile/log_numbers.c` | 393 | 371 | 94.4% |
| `shared/logfile/log_csv.c` | 610 | 580 | 95.1% |
| `shared/logfile/log_fields.c` | 46 | 45 | 97.8% |
| `shared/logfile/log_name.c` | 52 | 52 | 100.0% |
| `shared/safety/heartbeat.c` | 58 | 58 | 100.0% |
| `shared/safety/arming.c` | 90 | 84 | 93.3% |
| `shared/servo/servo_limit.c` | 120 | 116 | 96.7% |
| `shared/servo/servo_sync.c` | 172 | 167 | 97.1% |
| `shared/openyge/openyge_frame.c` | 165 | 162 | 98.2% |
| `shared/openyge/openyge_status.c` | 39 | 39 | 100.0% |
| `shared/openyge/openyge_params.c` | 66 | 66 | 100.0% |
| `shared/servo/servo_sim.c` | 122 | 122 | 100.0% |
| `shared/sbus/sbus.c` | 54 | 53 | 98.2% |
| `shared/dshot/dshot_frame.c` | 23 | 23 | 100.0% |
| `shared/dshot/dshot_telem.c` | 126 | 122 | 96.8% |
| `shared/ppm/ppm.c` | 42 | 42 | 100.0% |
| `shared/can/can_timing.c` | 105 | 103 | 98.1% |
| `shared/can/can_selftest.c` | 145 | 140 | 96.5% |
| `shared/can/mcp2515.c` | 20 | 20 | 100.0% |
| `shared/link/link_bringup.c` | 61 | 61 | 100.0% |
| `shared/link/link_can.c` | 94 | 92 | 97.9% |
| `shared/link/link_crc.c` | 7 | 7 | 100.0% |
| `shared/link/link_dev.c` | 77 | 74 | 96.1% |
| `shared/link/link_host.c` | 128 | 119 | 93.0% |
| `shared/link/link_artxfer.c` | 61 | 60 | 98.4% |
| `shared/artwork/art_store.c` | 104 | 96 | 92.3% |
| `shared/artwork/art_fetch.c` | 63 | 62 | 98.4% |
| `shared/bench/bench_state.c` | 76 | 72 | 94.7% |
| `shared/outputs/outputs.c` | 188 | 179 | 95.2% |
| `shared/outputs/outputs_pages.c` | 131 | 122 | 93.1% |
| `shared/outputs/out_bind.c` | 465 | 453 | 97.4% |
| `shared/outputs/out_pwm_map.c` | 15 | 15 | 100.0% |
| `shared/outputs/out_store_map.c` | 68 | 68 | 100.0% |
| `shared/bench/telemetry_sim.c` | 47 | 44 | 93.6% |
| `shared/bench/log_writer.c` | 66 | 62 | 93.9% |
| **total** | **9996** | **9570** | **95.7%** |

_Generated by `tools/coverage.py`; CI runs `--check` and fails on drift._
<!-- coverage:end -->

## Open items

| Item | State | Needs |
| --- | --- | --- |
| The control page under measurement | the NVS round trip has run, and the control page has since carried an accepted ARM: with the heartbeat wire fitted, a servo has been swung and a motor run from the panel. What that does not cover is the paths a session has to provoke rather than pass through -- the failsafe clear, a STOP mid-throttle, the link unplugged while armed, and an arm refused for each of its three reasons | a session with both boards that provokes them one at a time, and writes down what the band said |
| Output drivers on hardware | a servo has been swung and a motor run from the panel on the bring-up bench, so a pin drives; what nothing has seen is any timing on an instrument. What a host test cannot reach: every bit timing, the DMA ring that plays a PPM frame, the PIO turnaround from a bidirectional DShot frame to its reply, and whether an ESC answers at all | an oscilloscope, a servo, and an ESC that does bidirectional DShot. [What is unconfirmed](docs/DShot.md#what-has-not-been-confirmed-on-a-wire) |
| The flash save costs CAN frames | measured on the bring-up module: eight erase-and-program windows printed 19,174 to 19,186 us, and the XL2515's overrun count climbed from 2 to 8 while nine saves were taken. A frame at 1 Mbit/s is about 130 us and the controller holds two, so 19 ms is about 150 frame times, or seventy times over the 260 us the two buffers hold, with nobody emptying them; the bus reports no error, because the frames arrived and nobody collected them. A lost request costs the panel LINK_HOST_TIMEOUT_MS (1000 ms) of waiting, and 1000 ms of silence latches this end's 200 ms failsafe, so one lost frame is enough for `FAULT 01`. What the store does about it is above, and none of that has run on a board; what it does not do is get under the 260 us the two buffers hold. The 19 ms is an erase and a page program inside one window, which is what the store this replaces printed: neither half of it is measured on its own. The program is the window every save still pays, and 256 bytes against the erase's 4,096 puts it one to two orders of magnitude below 19 ms on serial NOR flash, or roughly 200 to 2,000 us against a 260 us budget | a board: the page program and erase windows from the console lines, the overrun count across sixteen saves, the heartbeat after a window, and a power cut mid-save |
| The capability word is read once, at boot | `s_capabilities` is taken from the identity page during bring-up and never again, so a coprocessor that arrives after boot, or is swapped for one with different parts fitted, leaves the menu marked from the wrong word. The board identity beside it comes from the identity page that opens each link, so it follows a swap; this word does not, because it is written at boot and read by the render task, and moving the write into the control task adds a cross-task race | the same snapshot treatment the bench numbers already get |
| A card swapped while running is not re-mounted | `storage_mounted()` is cleared only by `storage_deinit()`, so a card removed after boot leaves a stale mount and RESCAN keeps reading the old volume. The viewer does not unmount to recover: it lists from the render task while the `runlog` task writes the run log to the same volume, and unmounting under an open handle frees the SPI bus beneath a write from another task | one task owning the card's lifetime, so a remount can be sequenced against the writer |
| The browse list shows 48 of a card's entries | a card takes 999 runs and the list holds `LOG_VIEWER_MAX_FILES`, so it keeps the newest runs -- by the number in `BENCHnnn.CSV`, the only age on a card whose every FAT timestamp is 1980-01-01 -- and its tab says how many entries the card holds. A run is the only entry with a known age, so on a card holding 48 or more runs nothing else is listed: an imported `SWEEP.CSV` is outranked by every run and cannot be reached | a second page, or a filter on the browse list |
| Betaflight logs are listed nowhere | the log viewer opens `.csv` only. Nothing in the tree decodes a Betaflight blackbox log, so `.bfl` is neither offered on the card nor named on the screen | a BFL decoder, and the suffix back in `CARD_SUFFIXES` and in the viewer's empty-card line |
| An unbound output slot is invisible from the panel | the coprocessor leaves a slot the silicon cannot serve unbound: a reserved pin, a pin the package does not have, a PWM (pulse-width modulation) compare register another bound pin already holds, or no free PIO (programmable input/output) block. The OUTPUTS page reads back what was asked for and carries no register saying whether a slot is bound, so the outputs screen draws an unbound slot exactly as it draws a driving one and the refusal reaches the operator as a lead that does not move | a bound bit per slot on the OUTPUTS page, and the outputs screen drawing the difference |
| Coprocessor board file | the build uses `pimoroni_pico_plus2_rp2350` (RP2350B, 16 MB flash); the bring-up module is a Waveshare RP2350-CAN (RP2350A, 4 MB flash) | a board header for the module, or a `-DPICO_BOARD` in CI; the final board is an RP2350B for the 27 to 32 GPIO (general-purpose input/output) the pin budget needs |
| Panel TWAI pins | GPIO19 (RX) and GPIO20 (TX) inferred from the multiplexer; confirmed empirically by the bring-up | trace on the schematic |
| UART (universal asynchronous receiver-transmitter) socket GPIOs | UART0's default pins assumed for the bridged USB-C socket; the secondary USB-Serial-JTAG (the ESP32-S3's built-in USB (Universal Serial Bus) serial and debug bridge) console covers a mismatch | read off the schematic |
| S.BUS receiver | decoder built; the inverted 8E2 receiver at 100 kbaud is a PIO program that is not written | PIO program |
| Measurement front end | INA238 (motor), INA745A (servo rail), BQ25887 (pack) and TPS55288 (servo supply) selected; no schematic | [hardware record](hardware/STATUS.md) |
| Monostable | not on any board | hardware |
| The release publishes without waiting for CI | `release.yml` and `ci.yml` both trigger on a `v*` tag and run in parallel. `release.yml`'s publish job needs only its own two build jobs, so the host suite, the sanitizer run, the coverage floors, clang-tidy, cppcheck, ruff, `check_docs.py`, the frame-cost ceilings and the screenshot check cannot stop `gh release create`. Both files build the two images with the same steps, so the artefacts are the ones CI would have built; what is unguarded is everything CI checks that is not a build. A tag added a `version` job that refuses a tag not matching `rcbench_version.h`, which is the narrow case that was worth making structural | a `workflow_run` trigger on a successful CI run for tag refs, or the host suite folded into `release.yml` as a job the builds need. Until then: confirm CI is green on the commit before pushing the tag |
| OpenYGE wire facts | seven items want a capture: rpm scale, CRC (cyclic redundancy check) seed, frame length, legacy header, turnaround, parameter indices, `status2` | an ESC and a logic analyser; [list](docs/OpenYGE.md#8-what-to-measure-before-trusting-this-page) |
| The bench in a browser | serving the interface to a browser on another machine is open; a browser on the panel is not planned. There is no network stack in the tree: no Wi-Fi bring-up, no sockets, no HTTP (Hypertext Transfer Protocol), and Wi-Fi costs internal RAM and CPU time on a board whose frame budget is spent. The safety line is a heartbeat, and a remote client cannot hold one: a browser that stops answering is indistinguishable from one whose user is idle | a read-only client (numbers, plots and logs out; arming, throttle and STOP stay at the panel), and before any code, a written answer to how a remote session proves it is still present |
| No ESC reports its kV | the ESC screen shows the rated kV, what the motor turns per volt, and the ratio of the two as EFF, an estimate documented as such in [Screens](docs/Screens.md). The rated value is read from the connected ESC when it reports one and from `SET_MOTOR_KV` when it does not; nothing calls `motor_screen_set_esc_kv()` yet, so it is whatever the operator entered, and zero draws the field empty | an ESC parameter set on the link. The OpenYGE cache is built and unconnected; BLHeli_32's parameters are not published |
| The heartbeat has no hardware backstop | the wire from J8's GPIO6 to the coprocessor's GP3 is fitted on the bring-up bench, and arming succeeds there: a motor and a servo have each been run from the panel. What is not fitted is the retriggerable monostable the wire is supposed to pass through, so firmware at both ends is the only thing gating the outputs. The wire covers a panel that stops beating while the coprocessor is healthy: the monitor sees no edge for HEARTBEAT_MAX_GAP_MS (150 ms) and the loop disarms. Uncovered is a panel that stops beating while the coprocessor cannot act -- nothing then removes the outputs. That is what the monostable does, retriggered by the panel's edges and needing no firmware. A healthy panel beside a misbehaving coprocessor is covered by neither, and by no hardware in this design | the monostable specified in [Safety](docs/Safety.md), on a board. `testbench/WIRING.md` carries the specification and deliberately no part numbers |
| The control task has no test of its own | touch, STOP, arming, the outputs, the link and the heartbeat run in a task on the core that does not draw. It has run on hardware -- an arm, a throttle and a servo command have all gone through it -- but nothing exercises it deliberately: `main.c` is not in the host suite. The `runlog` task beside it, which owns every write to the card, is in the same position. A multi-agent review found six defects in it, including a heartbeat that stopped for up to 1000 ms on an unanswered poll and a splash tap that latched STOP; those are fixed, and the rules it drives are now in `shared/safety/arming.c` under `test_arming` | a session with both boards: arm, drag the throttle while the screen is busy, press STOP, unplug the link, and confirm the heartbeat's period on a scope at J8. ESP-IDF warns that a second core touching PSRAM shares bandwidth with the bounce-buffer refill and can starve it into the screen shift already seen on this board; the control task touches no framebuffer, which is the reason to expect it is clear, not evidence that it is |
| Settings save disturbs the picture | `settings_save()` writes NVS while the panel scans. The refill interrupt is masked for the length of the write, so the bounce buffer starves and the driver restarts the DMA at the next VBlank | nothing, unless the disturbance proves unacceptable. `CONFIG_SPI_FLASH_AUTO_SUSPEND` would remove it (the module's flash is 0x46 4018, an XMC die ESP-IDF grants `SPI_FLASH_CHIP_CAP_SUSPEND`), but ESP-IDF warns against it for a workload with an interrupt every 512 us |
| Interface language | about 430 user-visible strings are literals in ten screens, 168 of them the programmer's parameter names and help | an X-macro string table with one ID per string, a table per language, and a fallback to English; deferred until the screens stop changing |

## Constraints

- The XL2515 module's crystal is 16 MHz; `test_can_timing` pins it. An 8 MHz
  part caps the bus at 500 kbit/s.
- No 5 V transceiver is in the path: the SIT65HVD230 is a 3.3 V part. A 5 V
  transceiver on RP2350 bank 0 needs a 2.2 kΩ series resistor on RO, because
  the 3.3 V rail comes up after the 5 V rail on a module build.
- Native USB and CAN are exclusive on GPIO19/20. The console is UART0 with
  USB-Serial-JTAG as secondary.
- The motor monitor is the INA238. The INA228 has no stock at either vendor
  (checked 2026-09-01); one footprint takes either, and DEVICE_ID says which is
  fitted.
- The monostable window is 150 ms.
- A main-flash operation stops the panel scanning for its duration. The bounce
  buffer holds 10 lines (512 us at the 16 MHz pixel clock) and is refilled
  from PSRAM in an interrupt handler that reads through the data cache, which
  every main-flash read, write and erase closes. The handler is not IRAM-safe,
  so ESP-IDF masks it and the buffer starves rather than the core panicking
  with `Cache disabled but cached memory region accessed`. Settings still load
  before `display_init()`, so boot is undisturbed.
- `CONFIG_LCD_RGB_RESTART_IN_VSYNC` stays off. It restarts the panel's DMA on
  every vertical blanking interval, and each restart empties the LCD FIFO and
  then resumes from a link that skips `LCD_LL_FIFO_DEPTH + 1` = 17 px, which
  the FIFO no longer holds. The result is a picture 17 px to the left with
  each line's tail wrapped one line down.
- The link is CAN only. There is no RS485 transceiver, direction circuit or
  turnaround in the design.
- Every stop latches; nothing re-arms on its own.

## Not planned

- **Configuration over JETI EX Bus.** The specification restricts remote
  configuration to JETI products and does not document it.
- **BLHeli_32 parameters.** The information is not published; the rights holder
  declined in August 2026. [Details](docs/BLHeli32.md).
- **Futaba S.BUS2 telemetry slots.** Answering in the 325 µs slot without
  colliding with the receiver's own sensors is out of scope.
- **A USB oscilloscope.** The ESP32-S3's USB is full speed only.
- **A web browser on the panel.** The leanest headless Chromium build is
  over 100 MB against a 3 MB app partition; one blank tab needs more memory
  than the board's PSRAM, of which 1.5 MB is the two framebuffers; and the
  multi-process sandbox and the JIT (just-in-time compiler) need processes,
  an MMU (memory management unit) and a POSIX layer, none of which exist
  here. A cut-down HTML renderer would compete with the screens for the
  976 KiB per frame of PSRAM traffic in [the budget](docs/Performance.md).
  The other direction, a browser elsewhere reading this interface, is in the
  [open items](#open-items).
- **KST servo programming.** Held at the owner's request.

Every protocol is implemented from its published specification. Nearly every
open implementation of these protocols is GPL (GNU General Public License) or
AGPL (GNU Affero General Public License) against this repository's MIT
(Massachusetts Institute of Technology) licence; the permissive exceptions
(PX4's receiver decoders under BSD (Berkeley Software Distribution), MIT
reference code for SRXL2, JETI EX Bus, DShot and DroneCAN) are read for
confirmation only. OpenYGE's source material was supplied as GPL code and is
kept outside the repository; the specification in
[docs/OpenYGE.md](docs/OpenYGE.md) is what any implementation is written from.

## Order of work

| Step | State |
| --- | --- |
| 1. Foundation: tree, ported modules with tests, tools and CI, link codec, heartbeat, coprocessor skeleton, screens | done |
| 2. The link on hardware | done 2026-08-28: 1 Mbit/s, zero errors at either end |
| 3. Measured numbers: ESC telemetry or bidirectional DShot, and the current sensor | extended DShot telemetry fills `bench_state` with the ESC's own voltage, current, power, speed and temperature, and is not run against an ESC. The bench's own current sensor is selected and not fitted. OpenYGE is pursued in a separate repository |
| 4. Recording: the logger, and the viewer reads what the bench wrote | done |
| 5. Outputs: the control page from the panel, then PWM, then the sum-signal protocols | drivers built and host-tested; a servo and a motor have run from the panel, with no timing seen on an instrument. SETTINGS/OUTPUTS configures a slot |
| 6. Receiver buses, one decoder at a time | S.BUS decoded; the PIO receiver and the other buses open |
| 7. Programming: BLHeli_S and AM32, Hitec on the servo side | screen built; no protocol on a wire |

Next: an instrument on a pin. Every bit timing in this tree was written from a
specification and exercised only against frames the same code builds, and a
servo turning is not a frame period. `testbench/` is what answers that -- the
bench is being assembled and nothing on it has been run. After that, the
monostable, which is the one safety element specified and on no board.
