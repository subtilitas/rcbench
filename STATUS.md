# Where rcbench stands

The running record: what exists, what is open, what is settled. The suite list,
the module tree, the coverage table and every link are held to the tree by
`tools/check_docs.py` and `tools/coverage.py`.

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
2.0. [Reference](docs/Link.md).

**Safety.** The panel's render loop toggles GPIO6 (J8). The coprocessor's
outputs are to be gated by a retriggerable monostable with a window of about
150 ms, and the line is checked in firmware (4 to 150 ms between edges, four
good intervals before it is trusted). The coprocessor fails safe after 200 ms
of link silence; the panel escalates after 1 s. [Reference](docs/Safety.md).

**Outputs.** Every output is a channel (0 to 1000 of its own travel) with a
role (throttle or surface) rendered by a driver from a table (PWM (pulse-width
modulation), PPM, DShot). Arming, clamping, slew and the silence timeout are
implemented once, in `shared/outputs/`, and used by both processors. On the
wire: the `CHANNELS`, `CHAN_CFG` and `OUTPUTS` pages.

## State

| Subsystem | State |
| --- | --- |
| Rasteriser, fonts, touch mapping, theme, widgets, icons, router | built and tested |
| Settings model and screen | built and tested on the host. The panel loads and saves the values in NVS (non-volatile storage) through `firmware/panel/components/settings_nvs/`; not run on hardware |
| CSV (comma-separated values) and number parsing, log viewer | built and tested against the fixture corpus |
| Logger | built: a run is written while armed, in the format the viewer reads |
| Board, display, GT911, SD card | built; the panel boots and reports each step on the splash |
| Shell: band, router, splash, menu, simulation watermark | built |
| Motor & ESC (electronic speed controller) screen | built; reads `bench_state` from the link or the simulator. ARM, DISARM, STOP and the throttle are written to the coprocessor's control page at every 50 ms poll while the link is up; an arm writes CLEAR first and a NACK leaves the panel disarmed. Not run on hardware |
| Servo screen | built; writes `CHAN_CFG`, `OUTPUTS` and `CHANNELS` over the link |
| Analyser, programmer, balance, battery screens | built, rendered from models |
| Link codec, page map, dispatcher, both watchdogs, CAN framing | built and tested on the host |
| CAN drivers (TWAI on the panel, XL2515 on the coprocessor) and the echo self-test | built; run on hardware at 1 Mbit/s with zero errors at either end |
| Bring-up diagnosis, both ends' counters compared | built |
| Heartbeat generator and monitor | built and driven at both ends; the monostable is not fitted |
| Coprocessor firmware | answers the identity, status, control, bench and three output pages; fails safe at 200 ms; the numbers are simulated |
| Output drivers (PWM, PPM, DShot) | not started; no driver produces an edge on a pin |
| S.BUS decoder | built and tested; the PIO (programmable input/output) receiver is not written |
| Other receiver buses | not started |
| Servo limit search, servo synchronisation | built and tested against a modelled servo |
| OpenYGE codec | built and tested; not wired in. The implementation is pursued in a separate repository |
| Measurement front end | parts selected, nothing fitted: [hardware](hardware/STATUS.md) |
| Servo programmer | Hitec table in the programmer screen; KST (a servo manufacturer) held at the owner's request |

## The tree

```
rcbench/
  README.md  README-de.md  STATUS.md  CONTRIBUTING.md  SECURITY.md
  LICENSE  NOTICE  .gitattributes  .gitignore
  .github/workflows/      ci · docs · release
  .clang-tidy  .cppcheck-suppress  ruff.toml  codecov.yml
  docs/                   the wiki source, English and German
  tools/                  render_ui · coverage · check_docs · frame_cost
                          gen_font · wiki_links
  hardware/               board design record: README, STATUS, docs/

  shared/                 pure C: no ESP-IDF, no pico-sdk, no FreeRTOS types
    gfx/                  rasteriser and three fonts
    touch/                coordinate and event mapping
    ui/                   theme · widgets · icons · router · screens
    settings/             typed schema and values
    logfile/              number and CSV parsing
    link/                 page protocol · CAN framing · watchdogs · diagnosis
    bench/                bench_state · telemetry simulator · log writer
    outputs/              channels · driver table · arming, slew and staleness
    safety/               heartbeat generator and monitor
    servo/                limit and synchronisation searches · servo model
    can/                  bit timing · MCP2515 registers · echo self-test
    sbus/                 S.BUS decoder
    openyge/              OpenYGE framing, status and parameter cache

  firmware/panel/         ESP-IDF
    main/                 main.c · selftest.c
    components/           board · display · gt911 · storage · can_twai
    sdkconfig.defaults  partitions.csv

  firmware/iomcu/         pico-sdk
    src/                  main.c · xl2515.c
    include/iomcu_pins.h

  test/host/              one suite over shared/
    fixtures/             the CSV corpus the parser is held to
```

### Who compiles what

| Module | panel | iomcu | host |
| --- | :-: | :-: | :-: |
| `gfx` · `touch` · `ui` · `settings` · `logfile` · `sbus` | ✔ | | ✔ |
| `link` · `bench` · `outputs` · `safety` · `can` | ✔ | ✔ | ✔ |
| `servo` · `openyge` | | ✔ | ✔ |

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

The host suite is 32 binaries, one line per case: `test_gfx`, `test_touch_map`,
`test_nav`, `test_widgets`, `test_bench`, `test_motor`, `test_servo`,
`test_analyser`, `test_programmer`, `test_balance`, `test_battery`,
`test_settings`, `test_logfile`, `test_link_crc`, `test_link_pages`,
`test_link_watchdog`, `test_link_loopback`, `test_link_bringup`,
`test_link_can`, `test_outputs`, `test_can_timing`, `test_can_selftest`,
`test_mcp2515`, `test_heartbeat`, `test_servo_limit`, `test_servo_sync`,
`test_sbus`, `test_openyge_frame`, `test_openyge_status`,
`test_openyge_params`, `test_logview` and `test_logwriter`. The harness is
`test/host/greatest.h`, written for this project. `tools/check_docs.py` holds
this list to `test/host/CMakeLists.txt`.

Coverage floors: 94% overall, 85% for every file except `stub_screen.c`, which
is exempt by name. `tools/coverage.py --check` fails on drift of the table
below. `render_ui.py --check` holds 23 committed screenshots to the current
render; `frame_cost.py` holds a bench frame to 15,600 cache-line fills and a
chrome-cached screen to 2,000.

<!-- coverage:start -->
| File | Lines | Covered | Coverage |
| --- | ---: | ---: | ---: |
| `shared/gfx/gfx.c` | 643 | 618 | 96.1% |
| `shared/gfx/gfx_seg.c` | 96 | 91 | 94.8% |
| `shared/touch/touch_map.c` | 100 | 100 | 100.0% |
| `shared/ui/ui_theme.c` | 42 | 41 | 97.6% |
| `shared/ui/ui_widgets.c` | 129 | 122 | 94.6% |
| `shared/ui/ui_icons.c` | 110 | 110 | 100.0% |
| `shared/ui/ui_band.c` | 34 | 32 | 94.1% |
| `shared/ui/ui_watermark.c` | 43 | 41 | 95.3% |
| `shared/ui/ui_plot.c` | 145 | 134 | 92.4% |
| `shared/ui/ui_hero.c` | 29 | 28 | 96.5% |
| `shared/ui/ui_slider.c` | 125 | 116 | 92.8% |
| `shared/ui/ui_tabs.c` | 57 | 50 | 87.7% |
| `shared/ui/ui_router.c` | 134 | 129 | 96.3% |
| `shared/ui/splash_screen.c` | 60 | 57 | 95.0% |
| `shared/ui/overview_screen.c` | 69 | 63 | 91.3% |
| `shared/ui/stub_screen.c` | 45 | 8 | 17.8% |
| `shared/ui/motor_screen.c` | 175 | 169 | 96.6% |
| `shared/ui/servo_screen.c` | 344 | 306 | 89.0% |
| `shared/ui/analyser_screen.c` | 214 | 207 | 96.7% |
| `shared/ui/balance_screen.c` | 298 | 297 | 99.7% |
| `shared/ui/battery_screen.c` | 173 | 167 | 96.5% |
| `shared/ui/programmer_screen.c` | 311 | 294 | 94.5% |
| `shared/ui/log_viewer_screen.c` | 681 | 621 | 91.2% |
| `shared/ui/settings_screen.c` | 241 | 228 | 94.6% |
| `shared/settings/settings.c` | 131 | 125 | 95.4% |
| `shared/logfile/log_numbers.c` | 393 | 371 | 94.4% |
| `shared/logfile/log_csv.c` | 571 | 543 | 95.1% |
| `shared/logfile/log_fields.c` | 46 | 45 | 97.8% |
| `shared/safety/heartbeat.c` | 58 | 58 | 100.0% |
| `shared/servo/servo_limit.c` | 120 | 116 | 96.7% |
| `shared/servo/servo_sync.c` | 172 | 167 | 97.1% |
| `shared/openyge/openyge_frame.c` | 165 | 162 | 98.2% |
| `shared/openyge/openyge_status.c` | 39 | 39 | 100.0% |
| `shared/openyge/openyge_params.c` | 66 | 66 | 100.0% |
| `shared/servo/servo_sim.c` | 122 | 122 | 100.0% |
| `shared/sbus/sbus.c` | 54 | 53 | 98.2% |
| `shared/can/can_timing.c` | 105 | 103 | 98.1% |
| `shared/can/can_selftest.c` | 145 | 134 | 92.4% |
| `shared/can/mcp2515.c` | 20 | 20 | 100.0% |
| `shared/link/link_bringup.c` | 61 | 61 | 100.0% |
| `shared/link/link_can.c` | 94 | 92 | 97.9% |
| `shared/link/link_crc.c` | 7 | 7 | 100.0% |
| `shared/link/link_dev.c` | 74 | 71 | 96.0% |
| `shared/link/link_host.c` | 113 | 102 | 90.3% |
| `shared/bench/bench_state.c` | 61 | 57 | 93.4% |
| `shared/outputs/outputs.c` | 153 | 143 | 93.5% |
| `shared/outputs/outputs_pages.c` | 96 | 90 | 93.8% |
| `shared/bench/telemetry_sim.c` | 47 | 44 | 93.6% |
| `shared/bench/log_writer.c` | 43 | 40 | 93.0% |
| **total** | **7254** | **6860** | **94.6%** |

_Generated by `tools/coverage.py`; CI runs `--check` and fails on drift._
<!-- coverage:end -->

## Open items

| Item | State | Needs |
| --- | --- | --- |
| Control page and settings persistence on hardware | both written after the last hardware session; the coprocessor's NOT_ARMED refusal, the failsafe clear and the NVS round trip are host-tested at the shared/ level only | a session with both boards: arm, stop, unplug the link, arm again |
| Output drivers | no PWM, PPM or DShot driver produces an edge | PWM on the RP2350's hardware PWM first; DShot as a PIO program |
| Coprocessor board file | the build uses `pimoroni_pico_plus2_rp2350` (RP2350B, 16 MB flash); the bring-up module is a Waveshare RP2350-CAN (RP2350A, 4 MB flash) | a board header for the module, or a `-DPICO_BOARD` in CI; the final board is an RP2350B for the 27 to 32 GPIO (general-purpose input/output) the pin budget needs |
| Panel TWAI pins | GPIO19 (RX) and GPIO20 (TX) inferred from the multiplexer; confirmed empirically by the bring-up | trace on the schematic |
| UART (universal asynchronous receiver-transmitter) socket GPIOs | UART0's default pins assumed for the bridged USB-C socket; the secondary USB-Serial-JTAG (the ESP32-S3's built-in USB (Universal Serial Bus) serial and debug bridge) console covers a mismatch | read off the schematic |
| S.BUS receiver | decoder built; the inverted 8E2 receiver at 100 kbaud is a PIO program that is not written | PIO program |
| Measurement front end | INA238 (motor), INA745A (servo rail), BQ25887 (pack) and TPS55288 (servo supply) selected; no schematic | [hardware record](hardware/STATUS.md) |
| Monostable | not on any board | hardware |
| OpenYGE wire facts | seven items want a capture: rpm scale, CRC (cyclic redundancy check) seed, frame length, legacy header, turnaround, parameter indices, `status2` | an ESC and a logic analyser; [list](docs/OpenYGE.md#8-what-to-measure-before-trusting-this-page) |
| The bench in a browser | serving the interface to a browser on another machine is open; a browser on the panel is not planned. There is no network stack in the tree: no Wi-Fi bring-up, no sockets, no HTTP (Hypertext Transfer Protocol), and Wi-Fi costs internal RAM and CPU time on a board whose frame budget is spent. The safety line is a heartbeat, and a remote client cannot hold one: a browser that stops answering is indistinguishable from one whose user is idle | a read-only client (numbers, plots and logs out; arming, throttle and STOP stay at the panel), and before any code, a written answer to how a remote session proves it is still present |
| Chrome invalidation reaches six screens of ten | `ui_router_invalidate()` calls the splash, overview, stub, motor, log viewer and settings screens. The analyser, balance, battery, programmer and servo screens keep their cached chrome, so a theme change or a change of the SIMULATED flag leaves them stale. `frame_cost.py` shows it: `balance-chrome` costs the same as `balance` | an invalidate hook on the five screens that lack one, and a `-chrome` ceiling in CI once they respond |
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
| 3. Measured numbers: ESC telemetry or bidirectional DShot, and the current sensor | open. Parts selected, nothing fitted. OpenYGE is pursued in a separate repository; what returns is a source that fills `bench_state` |
| 4. Recording: the logger, and the viewer reads what the bench wrote | done |
| 5. Outputs: the control page from the panel, then PWM, then the sum-signal protocols | open. The pages and the output bank exist; no driver and no control-page write |
| 6. Receiver buses, one decoder at a time | S.BUS decoded; the PIO receiver and the other buses open |
| 7. Programming: BLHeli_S and AM32, Hitec on the servo side | screen built; no protocol on a wire |

Next: the two panel items at the top of the open list (settings initialisation,
control page), then the first PWM driver. None of them needs a part that is not
on the desk.
