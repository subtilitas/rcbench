# rcbench

A **motor, ESC and servo test bench** in two halves: an ESP32-S3 panel that
decides, draws and stores, and an RP2350 coprocessor that measures, drives and
talks to everything with a deadline.

> **Where this stands.** The panel boots into the bench. The foundation is `shared/link` compiles out of
> down, `shared/link` compiles out of one directory into the panel firmware,
> the coprocessor firmware and the host suite, and the protocol between the two
> processors is complete and proven end to end on a laptop — and unrun on
> silicon. The shell, the instrument widgets and the motor bench are built, and
> the panel firmware runs them against the simulator until a coprocessor
> answers. What is marked *inherited*
> below came from an earlier project of the same author's, with its tests.
>
> This file is the running record — it says what is true today, not what is
> intended, and every table in it is updated by the commit that changes the
> answer.

## What it is for

One board, one screen, one card, answering the questions about a drive system
that otherwise cost three separate boxes or a trip to the manufacturer:

- external current sensors over I²C;
- an ESC programmer for whatever is reachable — AM32 and BLHeli certain;
- balancing a whole system with an accelerometer and a position sensor;
- a servo tester with everything: SBUS and the rest;
- servo programming where it can be reverse-engineered;
- a log viewer for whatever format the log came in.

Nothing here is a consumer appliance. Every line is a workshop task — measure
it, program it, balance it, read the log back. That is why the UI is dense and
high-contrast rather than friendly, and why a screen that cannot do something
says which decision is missing instead of hiding the feature.

## The split, and why there is one

The predecessor put all of it on the panel processor and ran into the same wall
from four directions: one free fast GPIO, USB and CAN sharing a multiplexer, an
ADC not accurate enough to quote a number from, and timing-critical work
competing with a 39 Hz panel, PSRAM contention and an LCD interrupt.

**An RP2350 daughterboard takes the whole electrical side.** Twelve PIO state
machines — cycle-exact, DMA-fed, freely programmable — turn every awkward
protocol into a program rather than a driver mode: S.BUS's inverted 8E2 at
100 kbaud, one-wire half duplex at any rate, bidirectional DShot's 30 µs
turnaround. Hardware PWM has RISE, FALL and LEVEL capture in silicon, so rpm and
pulse measurement leave PIO entirely.

The rule of thumb, and the one to settle arguments with:

> **The daughterboard owns anything with a deadline. The panel owns anything
> with an opinion.**

And the constraint that follows it: **nothing raw crosses the link.** DShot bit
timing, GCR decoding, S.BUS framing and pulse capture all die on the
coprocessor; only results travel. Live telemetry, four channels of current and
voltage batched to 100 Hz, decoded ESC frames and an accelerometer burst come to
12–30 kB/s together — comfortably inside what the link carries, though the
margin is about two-fold on CAN rather than the five-to-twelve RS485 was
heading for. [The link](#the-link) has that arithmetic.

| | Panel | Coprocessor |
| --- | :---: | :---: |
| UI, touch, screens, settings, card, network | ✔ | |
| **Stop** | asserts the line **and** sends the command | honours the line unconditionally |
| Servo outputs, PPM, narrow band | | ✔ |
| S.BUS, iBUS, SUMD, CRSF, EX Bus, SRXL2 | | ✔ |
| DShot, bidirectional DShot, telemetry decode | | ✔, results only |
| One-wire programming, every baud rate | | ✔ |
| Current, voltage, cells, internal resistance, rpm, temperature | | ✔ |
| Accelerometer and index pulse | | ✔ — **one timebase**, which is what a phase measurement needs |
| The balancing arithmetic, the plot, the verdict | ✔ | |
| Power path, current limiting, protection | | ✔ |
| CAN | | ✔ |

## Where things stand

| | Source | State |
| --- | --- | --- |
| Rasteriser and fonts, touch mapping | inherited | **ported**, with their suites green |
| Theme, widgets, icons | inherited | **ported** |
| Router | inherited | waits for the screens — it includes every one of them |
| Settings model and screen | inherited | **ported**, with the whole of its suite |
| Locale-tolerant CSV and number parsing | inherited | **ported**, with its fixtures |
| Board, display, GT911, SD storage | inherited | **ported**: the panel boots, reports each step to the splash, and runs the shell |
| Golden-image renderer, coverage, doc and frame-cost checks | inherited | to port as-is |
| **The link codec** | new | framing, the page map, the dispatcher, both watchdogs and both transports — and a bench page that crosses the wire and reads back as the same `bench_state` |
| **The safety heartbeat** | new | **built and driven at both ends**: the panel edges from the loop that owns STOP, the coprocessor judges the period in firmware and refuses to arm without it |
| The shell — band, router, splash, menu | re-cut | **built**: STOP on every screen, screens handed a sub-canvas so they cannot draw over it |
| **Motor & ESC bench** | re-cut | **built**: four traces on independent scales, hero readouts, throttle with presets, arm and reset — reading a `bench_state` the link or the simulator fills |
| **Log viewer** | re-cut | **built**: browse, import with its evidence, plot with cursors |
| **The logger** | new | **built**: a run is written while armed, in the format the viewer reads — checked by writing one and parsing it back |
| **Setup** | re-cut | **built**, and the seven screen cases held back with it are restored |
| The other two benches and three scaffolds | re-cut | routed and rendered; each says what it will do and what is blocking it |
| Instrument widgets — plot, rails, hero, slider, tabs | re-cut | **built** and tested: the scale ladder, the shrink hysteresis, and the press-ownership contracts |
| The simulation watermark | new | **built**: SIMULATION across the whole screen at 15% whenever the numbers are modelled, and no screen can opt out |
| Coprocessor firmware | new | answers identity, status, control, bench and servo pages over CAN, fails safe at 200 ms. **Run on silicon**: the bus is up at 1 Mbit/s with zero errors at either end |
| Panel link transport | new | TWAI on GPIO19/20 through the board's multiplexer, poll loop with the one-second escalation and fragment reassembly. **Run on silicon** |
| **The link moves to CAN** | new | **drivers at both ends, and a bring-up self-test that runs on silicon**: an echo across the bus that answers only "do frames cross intact", separately from whether the link works. The coprocessor echoes permanently; the panel side is opt-in because it costs native USB. [The procedure](docs/Bringup.md). The mapping and the bit timing are tested on the host, the coprocessor's pins came from the vendor's own driver rather than a guess,
while the panel's TWAI pins are inferred from the multiplexer and still want
tracing on the schematic; the drivers are not. A coprocessor module with an XL2515 controller and a SIT65HVD230 transceiver, against the panel's own CAN path — which the board already has, multiplexed with USB. `link_msg_t` never knew what carried it, so the dispatcher is unchanged and this is an added transport rather than a rewrite. [What it buys and costs](docs/Link.md) |
| **Bring-up diagnosis** | new | **built**: both ends' counters compared and the most fundamental fault named rather than the loudest — a return path that never releases looks nothing like a dead coprocessor once the far end's own frame count is in the room. The coprocessor now fills the three STATUS registers it had always declared and never written. [The procedure](docs/Bringup.md) |
| **The heartbeat** | new | **driven**: edges from the render loop, dropped the instant STOP latches or touch stops answering. The monostable it gates is not fitted, so today the edges reach a header pin and a scope |
| Throttle output on the panel | **removed** | GPIO6 is the heartbeat; the panel emits no servo pulse |
| **S.BUS** | new | **decoded and tested**: sixteen channels of eleven bits, the two digital ones, and both flags. Framed on the inter-frame gap rather than on a header byte that is also an ordinary channel value — see [the receiver buses](docs/Receivers.md). The inverted 8E2 receiver that produces the bytes is a PIO program and is not written |
| **The OpenYGE ESC protocol** | **elsewhere** | the implementation is being carried in a separate repository, so nothing further is done here. [The specification](docs/OpenYGE.md) stays: it is the document of record, it is going to YGE to be checked, and the CRC seed and decoder shape it settles are the panel link's too. The codec in `shared/openyge/` is **dormant** — built, tested, wired into nothing |
| **A servo page on the link** | new | **built and tested**: one output, where to hold it, and the range outside which the coprocessor will not go. The limits live at the far end because that is the end holding the wire — a host that has been restarted, reflashed or is simply wrong must not be able to drive a servo into its stops. A pulse outside the range is *clamped* and a limit outside what a servo can take is *refused*, which are different answers to different mistakes: pulses arrive many times a second from a host that may be mid-drag, and refusing one gives a servo that stops following rather than one that stops at its stop. `LINK_PROTOCOL_MINOR` goes to 1, which an older host ignores. What is still missing is the PWM at the far end: the page is answered, and nothing yet turns it into an edge |
| **Finding a servo's installed limit** | new | **the search is built and tested** against a modelled servo — the knee in current against position, with three protections. It has nothing to drive until the coprocessor has PWM and a sensor per output |
| **Synchronising two servos on one surface** | new | **built and tested**: total current minimised at centre and at each end, which separates an offset error from a travel error. Waiting on the same sensor |
| Servo programmer | **held** | the KST work stays in the predecessor until asked for |

## The tree

```
rcbench/
  README.md  LICENSE  .gitattributes  .gitignore
  .github/workflows/      ci · docs · release
  docs/                   the wiki source
  tools/                  render_ui · coverage · check_docs · frame_cost · gen_font

  shared/                 pure C — no ESP-IDF, no pico-sdk, no FreeRTOS types
    gfx/                  rasteriser and three fonts
    touch/                coordinate and event mapping
    ui/                   theme · widgets · icons · router · screens
    settings/             typed schema and values
    logfile/              number and CSV parsing
    link/                 messages · CAN framing · pages · watchdogs · diagnosis
    bench/                bench_state · throttle policy · simulator

  firmware/panel/         ESP-IDF
    main/                 main.c
    components/           board · display · gt911 · storage · can_twai
    sdkconfig.defaults  partitions.csv

  firmware/copro/         pico-sdk
    src/                  main · link_uart · heartbeat_in · safety · pages/
    pio/                  dshot.pio
    include/copro_pins.h

  test/host/              one suite over shared/ and its fakes
    fakes/                transport · clock · filesystem
    fixtures/             the CSV corpus the parser is held to
```

Everything that decides something is pure C with no vendor SDK; everything that
touches hardware is not. That split is what makes the host suite, the coverage
numbers and the golden-image check possible — and here it runs across two
processors.

### Who compiles what

| Module | panel | copro | host |
| --- | :-: | :-: | :-: |
| `gfx` · `touch` · `ui` · `settings` · `logfile` | ✔ | | ✔ |
| `link` | ✔ | ✔ | ✔ |
| `bench` | ✔ | ✔ | ✔ |

`link` is the only module all three compile, and that is the point of it.
`bench` splits by header rather than by directory: `bench_state.h` is the model
the panel renders, `throttle.c` is the arm interlock and the slew limit, which
compile into the coprocessor — because that is where the output is now.

Screens stay in `shared/ui/` rather than under `firmware/panel/`. They are pure
C over a canvas and own no hardware, which is what lets the identical code
render to a PNG on a laptop, and what lets the navigation and hit-region tests
exist at all.

Each module carries one ten-line `CMakeLists.txt` that answers to whichever
build system is asking — `idf_component_register()` under `ESP_PLATFORM`, a
plain `add_library()` otherwise. The panel sets `EXTRA_COMPONENT_DIRS` and gets
every module as a first-class component; the coprocessor and the host suite
`add_subdirectory()` the ones they want. No wrapper components, and no source
list written down twice. It puts one `if(ESP_PLATFORM)` in a directory that
claims no vendor SDK — that claim is about the C, and the alternative is
twenty-one files of indirection to avoid one conditional.

Includes are flat: `#include "gfx.h"`, not `"rcbench/gfx.h"`.

## The link

**CAN, 1 Mbit/s**, between the panel's TWAI controller and an XL2515 on the
coprocessor. It began as half-duplex RS485 because that is what the panel
brought out; the board turned out to have a CAN path already, multiplexed
against USB, and the coprocessor module arrived with a controller on it.

The protocol above it did not change, and that is the point. It copies
ArduPilot's **IOMCU**, which has flown the same problem for a decade:

- **A page and register model.** Every transaction is a page, an offset and a
  count over up to 32 sixteen-bit registers. A new feature adds a page, not a
  message type.
- **Strictly host-polled. The coprocessor never speaks unsolicited.** That rule
  predates CAN and is kept even though arbitration would now make an
  unsolicited frame harmless.
- **Two watchdogs, the tighter on the coprocessor.** It fills failsafe values
  after 200 ms of silence on its own authority; the host escalates after a
  second. **Traffic returning does not lift the failsafe** — a link that
  recovers is not consent to spin a propeller, so leaving it takes a deliberate
  write of a known value to the control page.
- **The coprocessor protects hardware without asking** — overcurrent,
  over-temperature, stall timeout, lost link — and reports what it did at the
  next poll. It never waits for permission to fail safe.

`link_msg_t` never knew what carried it, and `link_dev_dispatch` — which
decides what to answer — has no transport in it at all. That is what let the
byte transport be **deleted** rather than adapted when the wire changed:
the dispatcher, the page map, both watchdogs and their tests were untouched.

### What CAN changed, and what it cost

Arbitration replaces taking turns, so there is no direction line. With it went
the RC one-shot, the turnaround wait, the ~125 kbaud floor, an unmeasured FET
threshold and a 5 V transceiver hazard — two open questions in this record
closed by ceasing to be askable. The controller also brings a 15-bit CRC, an
acknowledge slot, automatic retransmission and bus-off confinement, none of
which had to be written.

**Priority became a property of the address.** CAN arbitrates by identifier,
lowest wins, so a write to the control page outranks every telemetry read *on
the wire*, against traffic already in flight, with no software involved at
either end. RS485 could not promise that at any baud rate.

The cost is bandwidth. TWAI is classic CAN only — 1 Mbit/s, eight data bytes a
frame:

| | payload |
| --- | ---: |
| Classic CAN, 1 Mbit/s, 29-bit IDs, worst-case stuffing | **52 kB/s** |
| RS485 at the 1.5 Mbaud it was heading for | 133 kB/s |

Against the 12–30 kB/s that actually crosses, the margin fell from five-to-
twelve times to about two. A `bench_state` poll is thirteen registers, five
frames, **1.55% of the bus at 20 Hz**, and a 60 kB coprocessor image takes
1.2 s. Two is enough — but this record said twelve, and now says two.

The 29-bit identifier holds exactly the fields the message already had:
priority, op, page, offset, count. Three things fall out of that. A read has
**no payload**, because the whole question is its address. **Nothing is
reassembled at the transport**: each frame carries its own offset and count, so
a thirteen-register reply is four independent messages and a dropped one costs
one register range rather than a transfer. And the **frame CRC is gone**,
because carrying another two bytes would spend a quarter of an eight-byte
payload duplicating what the controller already did.

[The link](docs/Link.md) has the identifier layout, the bit timing and the
arithmetic; [bringing it up](docs/Bringup.md) has the procedure.

## The safety line is a heartbeat, not an enable

A static enable-high line fails the most likely failure — firmware wedges with
the pin still high and the outputs stay live. So the loop that draws the STOP
button and reads touch edges **GPIO6**, and the coprocessor's output enable and
its servo and ESC power path are gated from a retriggerable monostable.

The rate follows that loop, because that is the loop whose liveness is being
asserted: it asks for an edge every 20 ms and gets one every 26–52 ms — 39 Hz
with nothing changing, 19.5 Hz on the frames a telemetry sample lands. The
coprocessor accepts intervals from 4 to 150 ms and wants four consecutive good
ones before it will believe the line. **This supersedes the 100 Hz–1 kHz and
20–50 ms figures this record used to carry**, which predate the panel having a
measured frame rate; a 50 ms monostable would drop the outputs on every second
frame of a bench under load. The window belongs at roughly 150 ms, which is
still well inside the coprocessor's 200 ms link failsafe —
[Safety](docs/Safety.md) has the arithmetic.

A crash, a wedged task, a reset, a brown-out and an unplugged cable then present
identically: no edges, so no output. Fail-safe by absence, in hardware,
independent of firmware at both ends. Noise can fake a heartbeat, so the
monostable is the crude backstop and the coprocessor checks the period as well.

Three independent mechanisms result: a pressed stop travels as a command **and**
stops the heartbeat, a hung host stops the heartbeat, and a dead link trips the
coprocessor's silence watchdog. The panel's own rule sits behind all three — it
disarms, and refuses to re-arm, if the touch controller stops answering for
500 ms, since the panel is the only place a STOP button exists.

This is why the panel no longer drives a servo pulse at all. GPIO6 was the
throttle output; it has one job now.

## Building

```bash
# panel
. $IDF_PATH/export.sh
idf.py -C firmware/panel set-target esp32s3
idf.py -C firmware/panel build
idf.py -C firmware/panel -p /dev/ttyACM0 flash monitor    # COMx on Windows

# coprocessor
cmake -S firmware/copro -B firmware/copro/build
cmake --build firmware/copro/build

# the parts that are pure C, on any laptop
cmake -S test/host -B test/host/build -DCMAKE_BUILD_TYPE=Debug
cmake --build test/host/build
ctest --test-dir test/host/build --output-on-failure
```

ESP-IDF **v5.4 or newer** — the RGB panel's `on_frame_buf_complete` event, which
the framebuffer swap waits on, landed there. `sdkconfig.defaults` sets the
octal-PSRAM, 64-byte-cache-line and IRAM-safe-LCD-ISR options the panel needs;
start from it rather than a bare `menuconfig`.

## Tests and CI

| Workflow | Trigger | What it does |
| --- | --- | --- |
| `ci.yml` | push / PR / tag `v*` / manual | host suite, coverage `--check`, the font, frame-cost, golden-image and documentation checks, the ESP-IDF matrix (v5.4, v5.5) building the panel, the pico-sdk build of the coprocessor, and firmware artifacts including a merged panel image that flashes to `0x0` on its own |
| `docs.yml` | push to `main` touching `docs/` | publishes `docs/` to the GitHub wiki |
| `release.yml` | tag `v*` | builds both images, packages them, opens a release |

32 binaries, each printing one line per case: `test_gfx`,
`test_touch_map`, `test_nav`, `test_widgets`, `test_bench`, `test_motor`,
`test_servo`, `test_analyser`, `test_programmer`, `test_balance`, `test_battery`, `test_settings`,
`test_logfile`, `test_link_crc`, `test_link_pages`,
`test_link_watchdog`, `test_link_loopback`, `test_link_bringup`,
`test_link_can`, `test_link_servo`, `test_can_timing`, `test_can_selftest`,
`test_mcp2515`,
`test_heartbeat`,
`test_servo_limit`, `test_servo_sync`, `test_sbus`, `test_openyge_frame`,
`test_openyge_status`, `test_openyge_params`, `test_logview` and
`test_logwriter`. The
harness is `test/host/greatest.h`, about a hundred lines, with nothing
vendored. `tools/check_docs.py` holds that list to this file — a page in the
predecessor said *seven* for two releases after it was ten, and nobody reads a
doc looking for that.

All five tools run in CI again: `render_ui.py --check` holds fourteen committed
screenshots to the current render, and `frame_cost.py` holds a bench frame to
15,600 cache-line fills and a chrome-cached one to 2,000. A frame between
samples currently costs **740**.

<!-- coverage:start -->
![coverage](https://img.shields.io/badge/host--test%20coverage-93.2%25-brightgreen)

| File | Lines | Covered | Coverage |
| --- | ---: | ---: | ---: |
| `shared/gfx/gfx.c` | 629 | 604 | 96.0% |
| `shared/gfx/gfx_seg.c` | 96 | 91 | 94.8% |
| `shared/touch/touch_map.c` | 100 | 100 | 100.0% |
| `shared/ui/ui_theme.c` | 42 | 41 | 97.6% |
| `shared/ui/ui_widgets.c` | 129 | 122 | 94.6% |
| `shared/ui/ui_icons.c` | 109 | 109 | 100.0% |
| `shared/ui/ui_band.c` | 34 | 32 | 94.1% |
| `shared/ui/ui_watermark.c` | 21 | 19 | 90.5% |
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
| `shared/ui/analyser_screen.c` | 214 | 204 | 95.3% |
| `shared/ui/balance_screen.c` | 297 | 217 | 73.1% |
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
| `shared/link/link_bringup.c` | 61 | 50 | 82.0% |
| `shared/link/link_can.c` | 94 | 92 | 97.9% |
| `shared/link/link_crc.c` | 7 | 7 | 100.0% |
| `shared/link/link_dev.c` | 74 | 71 | 96.0% |
| `shared/link/link_servo.c` | 34 | 32 | 94.1% |
| `shared/link/link_host.c` | 113 | 102 | 90.3% |
| `shared/bench/bench_state.c` | 61 | 57 | 93.4% |
| `shared/bench/throttle.c` | 53 | 45 | 84.9% |
| `shared/bench/telemetry_sim.c` | 47 | 44 | 93.6% |
| `shared/bench/log_writer.c` | 43 | 40 | 93.0% |
| **total** | **7054** | **6573** | **93.2%** |

_Generated by `tools/coverage.py`; CI runs `--check` and fails on drift._
<!-- coverage:end -->

`docs/` starts nearly empty on purpose — Home, the manifest, the link, the
safety line and building. Every other page is written by the commit that lands
the code it describes, rather than ported ahead of it. Accurate prose about a
subsystem that is not here yet is still a page nothing references, and
`check_docs.py` holds the prose to the tree precisely so that cannot drift.

Coverage lands in this file and CI runs `--check`, which fails on drift rather
than committing a fixup — a file that rewrites itself is one nobody reads the
diff of. What the screens *look like* is a separate question, answered by golden
images the same renderer produces on a laptop.

What the link has to survive on that laptop, before it ever sees a wire: every
bit position of the extended identifier checked against the datasheet, a reply
wider than a frame reassembled from pieces that arrive back to front, a lost
piece leaving a request unanswered rather than half-answered, a reply to an
abandoned question refused, both watchdogs against a mock clock including two
timeouts in a row, and register round-trips at both ends of every range.

The CRC lives on for [OpenYGE](docs/OpenYGE.md), which needs the same
polynomial with a different seed. The link itself no longer has one: CAN
carries a CRC, an acknowledge slot and retransmission in silicon.

## What is still unsettled

Of the six this list started with, three were closed by reading the board
schematic — the link's pins, whether GPIO6 reaches a connector, and the
direction circuit's turnaround. A fourth was closed by being answered wrongly:
the question was which end set a baud *ceiling*, and there was no ceiling. The
last two were closed by the move to CAN, which is a stronger kind of closed:
the direction circuit they were about no longer exists.

Two are open. Two more were closed by the move to CAN — and closed in the
strong sense: the questions stop being asked rather than being answered, which
is what replacing a mechanism does that fixing one does not. They are kept
below because the reasoning applies again to anybody who fits RS485.

| | Where it stands |
| --- | --- |
| ~~**The XL2515's crystal**~~ | **Closed: it is 16 MHz**, so 1 Mbit/s is reachable exactly and the budget stands at 51.6 kB/s against the 12–30 that crosses. Determined rather than assumed — the vendor's driver carries CNF triples for ten standard rates, and each decodes to its advertised rate at 16 MHz and at no other crystal. `test_can_timing` pins it, so a module with a different can fails a test rather than a bus. An 8 MHz part would have capped the link at 500 kbit/s and 25.8 kB/s, short at the top of the range. |
| **INA228** is the sensing part | Right on merit — 85 V, twenty bits, charge and energy accumulated in hardware — but back-ordered into January 2027, and the obvious substitute stops at 36 V, which is under 8S. Check the INA238 before a layout commits to it. |
| ~~**A 5 V transceiver against the RP2350's power-up order**~~ | **Closed by CAN.** The coprocessor module's SIT65HVD230 is a 3.3 V part, so there is no 5 V anywhere in the path. The original concern, kept because it returns with any 5 V transceiver: Bank 0 is 5 V tolerant only while the 3.3 V rail is up, and on a module build the 5 V rail comes up first — so at every power-on the transceiver can drive 5 V into an unpowered input. A 2.2 kΩ series resistor on RO bounds the clamp current for eleven nanoseconds of edge. |
| **The interface is in English, in place** | About 430 user-visible strings sit as literals in ten screens, 168 of them the programmer's parameter names and help. Moving them out is mechanical and the verification is unusually strong -- the golden images must come out byte-identical, same text from a different source -- but it is a large single change and it is deferred rather than half-done. The shape when it happens: an X-macro list so each string's ID and its English text are declared on one line and cannot drift apart, a table per language, and a lookup that falls back to English on any missing entry so a partial translation shows text rather than blanks. The programmer's tables would carry string ids instead of literals, which suits them, since they already carry everything else about a parameter. |
| **Seven OpenYGE numbers want a logic analyser** | The protocol came from YGE's developer as code rather than as a document, and code answers "what does this do" rather than "what does the wire guarantee". The RPM scale is the one that actually contradicts itself — the field is described as 0.1 eRPM and multiplied by ten — and a wrong answer there is a tachometer that reads a hundredfold out. [The spec](docs/OpenYGE.md) lists all seven; each is one measurement, and the parameter indices want confirming before anything is *written* to an ESC. |
| ~~**Q1's threshold voltage is not on the schematic**~~ | **Closed by CAN.** The direction circuit's hold time set the baud floor, and CAN has no direction circuit. The schematic names R76, C51, D7 and R79 but not the FET, so the floor was quoted from the pessimistic end of a plausible range (1.0 V → 72 µs → 125 kbaud); one scope capture would still settle it if RS485 is ever fitted again. |
| ~~**The panel's console shares a multiplexer with CAN**~~ | **Closed.** Native USB — USB-Serial-JTAG and USB-OTG both — is on GPIO19/20, dedicated analog pins the matrix cannot move, and those are what the FSUSB42UMX switches against CAN. So selecting CAN costs the native console whichever way the mux is wired. The board has a second USB-C socket behind a USB-UART bridge, which shares nothing with CAN, so the console is now **UART0 primary with USB-Serial-JTAG as secondary** — output goes to both and whichever socket is plugged in shows it. One thing left to confirm from the schematic: which GPIOs the bridged socket lands on. UART0's defaults are assumed, and the assumption fails soft, because the secondary still works whenever USB is selected. |

## What is deliberately not built

Each of these was investigated and closed, and the reason is worth keeping so
nobody reopens it by accident.

- **Configuration over JETI's EX Bus.** The specification says remote
  configuration is "available only for the products of JETI model" and that its
  description is not part of the document. What EX Bus delivers is channel
  values and telemetry.
- **BLHeli_32**, whose firmware and update servers are gone.
- **Futaba's S.BUS2 telemetry slots** — answering in the right 325 µs window
  without colliding with the customer's real sensors is the hardest job in the
  survey.
- **A USB oscilloscope.** This chip's USB is Full-Speed only and about twenty
  times too slow for one.
- **Servo programming for KST**, held at the owner's request.

And one rule that constrains every protocol commit: **nearly every open
implementation of these protocols is GPL or AGPL against this repository's MIT.**
The permissive exceptions are PX4's receiver decoders (BSD) and MIT reference
code for SRXL2, JETI EX Bus, DShot and DroneCAN. Everything else is written from
the specification, not from somebody's line.

**OpenYGE is the first time that rule was actually exercised**, and it is worth
recording how, because the same shape will recur. YGE's own developer supplied
the protocol — there is no published document — but supplied it as code
carrying a GPL header. The code is kept outside this repository entirely: not
in the tree, not in the history. What came across is what cannot be owned, a
byte offset and a scale factor and a polynomial, written up from scratch as
[the OpenYGE specification](docs/OpenYGE.md), and that page is what any
implementation here is written from. Six defects in the supplied code are
recorded on it rather than inherited.

## The order of work

1. **The foundation** — the tree, the ported floor with its tests green, the
   tools and CI, the link codec, the heartbeat, the coprocessor skeleton, the
   re-cut screens.
2. ~~**The link, on silicon**~~ — **done.** The module landed, the bus came up
   at 1 Mbit/s, and an echo test ran 2144 probes with zero errors at either
   end. [Bringing it up](docs/Bringup.md) is the procedure and what it found.
3. **Make the numbers real** — a KISS frame or bidirectional DShot, and the
   current sensor. The bench stops simulating. ESC telemetry is the shortest
   path through this and [OpenYGE](docs/OpenYGE.md) is the worked example, but
   that one is being pursued in a separate repository; what returns here is
   whichever protocol is proven there, as a source filling `bench_state`.
4. **Make them keep** — the logger, then the viewer reads what the bench wrote.
5. **Make it drive** — servo outputs, then the sum-signal protocols, then the
   servo tester as it is described on the box.
6. **Make it listen** — one bus decoder at a time; each is a day and each adds a
   product feature. **S.BUS is decoded**; what it still needs is the PIO
   program that turns an inverted 8E2 line into bytes.
7. **Make it programme** — BLHeli_S and AM32 first, because they need nobody's
   permission; Hitec first on the servo side, for the same reason.

**Step 2 is the exception, and it is the only one.** Both ends of the link are
written and tested against each other on the host; what is left is running them
against each other on real silicon, and that waits on the RP2350 module rather
than on anybody's time. Step 5 waits on the same board for its PWM.

Steps 3, 4 and 6 are not blocked. Step 6 in particular is pure parsing — a bus
decoder is bytes in and channels out, host-testable to the last edge case
before any receiver is plugged in, which is the same shape the link codec and
the servo searches were built in.

## The reference

[`docs/`](docs/) is the wiki source and the reference behind this record:
[what this is for](docs/Manifest.md), [the link](docs/Link.md),
[safety](docs/Safety.md), [bringing up the link](docs/Bringup.md),
[the servo procedures](docs/Servo.md),
[the OpenYGE protocol](docs/OpenYGE.md), and [building](docs/Building.md). Pages are written
by the commit that lands the code they describe, so a page that is missing is a
subsystem that is not here yet.

## License

MIT — see [LICENSE](LICENSE).

---

In collaboration with Claude Code.
