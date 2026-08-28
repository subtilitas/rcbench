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
12–30 kB/s together — five to twelve times less than the link carries.

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
| Coprocessor firmware | new | answers identity, status, control and bench pages over a real UART, honours the turnaround, fails safe at 200 ms; unrun on silicon |
| Panel link transport | new | UART on GPIO16/15, board-switched direction, poll loop with the one-second escalation; unrun on silicon |
| **The heartbeat** | new | **driven**: edges from the render loop, dropped the instant STOP latches or touch stops answering. The monostable it gates is not fitted, so today the edges reach a header pin and a scope |
| Throttle output on the panel | **removed** | GPIO6 is the heartbeat; the panel emits no servo pulse |
| **The OpenYGE ESC protocol** | new | **specified, and the codec is built**: framing with the same resynchronising decoder the panel link uses, the telemetry payload, the status byte with its overloaded warning nibble, and the drip-fed parameter table. The session — polling, turnaround, sequence matching — is next, and seven numbers still want a logic analyser |
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
    link/                 framing · CRC-16 · pages · watchdog
    bench/                bench_state · throttle policy · simulator

  firmware/panel/         ESP-IDF
    main/                 main · panel_boot · heartbeat · link_host
    components/           board · display · touch · storage · link_uart
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

Half-duplex RS485, 8N1, because that is what the panel brings out — and beside
100–300 A of switching current, differential signalling is what one would
choose anyway. The protocol copies ArduPilot's **IOMCU**, which has flown the
same problem for a decade:

- **A page and register model.** Every transaction is a page, an offset and a
  count over up to 32 sixteen-bit registers. A new feature adds a page, not a
  message type.
- **A delimited envelope** — sync byte, length, CRSF's shape — so a decoder
  resynchronises after noise instead of waiting for a gap. CRC-16 rather than
  IOMCU's CRC-8, since this link also carries firmware images.
- **Strictly host-polled. The coprocessor never speaks unsolicited.** This is
  what makes the rest simple: nothing arbitrates outbound priority, so a stop
  command cannot queue behind a telemetry burst *by construction*.
- **Both ends are written and neither has met silicon.** The protocol between
them is proven end-to-end on a laptop — `test_link_loopback` drives the real
host state machine against the real device dispatcher across a wire that can
corrupt, truncate and go deaf — so what bring-up has left to find is the wire
itself, not the conversation.

**Two watchdogs, the tighter on the coprocessor.** It fills failsafe
  values after 200 ms of silence on its own authority; the host escalates after
  a second. **Traffic returning does not lift the failsafe** — a link that
  recovers is not consent to spin a propeller, so leaving it takes a deliberate
  write of a known value to the control page.
- **The coprocessor protects hardware without asking** — overcurrent,
  over-temperature, stall timeout, lost link — and reports what it did at the
  next poll. It never waits for permission to fail safe.

A page is 32 registers, so a whole-page transfer is 72 bytes — six of header,
sixty-four of payload, two of CRC. The research put the frame cap at 64 bytes;
here it follows the page size instead, because at every rate below the target
one a bigger frame amortises a turnaround that a smaller one pays twice.

The decoder examines **every** sync byte in its buffer rather than committing
to the first one. Noise containing a plausible-looking sync and length, in
front of a genuine frame, is what breaks the obvious implementation: it sits
waiting for bytes that will never make sense while the real frame — already
whole, already in the buffer — goes unreported. That failure was written as a
test before the decoder was written, and the first decoder failed it.

## The wire

The protocol is rate-agnostic. The schedule that uses it is not, and the bench
and the finished board are not the same wire.

The finished board runs at **1.5 Mbaud**; the module build, where the protocol
firmware is actually being written, runs at **256 kbaud**. Sixfold, so anything
that merely fits at 1.5 Mbaud has to be checked against the bring-up rate
before it is relied on. 8N1 is ten bits per byte:

| | 128 kbaud | 256 kbaud | 1.5 Mbaud |
| --- | ---: | ---: | ---: |
| Throughput | 12.8 kB/s | 25.6 kB/s | 187.5 kB/s |
| A whole-page poll (8 + 72 bytes) | 6.25 ms | 3.13 ms | 0.53 ms |
| Whole-page polls per second | **160** | 320 | 1875 |

**128 kbaud is not used, on the schematic's evidence — see below.**

**This is what makes "nothing raw crosses" load-bearing rather than tidy.** The
research budgeted four channels of current and voltage at 1 kHz batched into
100 Hz packets. Sent raw that is 4,000 registers a second — about 125 whole
pages, **78% of the wire at 128 kbaud**, leaving nothing for telemetry. At
1.5 Mbaud the same stream is 7% and nobody would notice. Firmware written
against the comfortable case would have had to be redesigned at exactly the
wrong moment.

So it is not sent raw at either rate. The coprocessor accumulates minimum,
maximum and mean per batch and reads charge and energy out of the INA228's
hardware accumulators, and reports those with the rest of the bench state for
almost no extra bytes. Raw samples cross only during a **bounded capture** the
panel asks for by name: one second of four channels at 1 kHz is 8 kB of
samples, 10 kB framed — 0.78 s of wire at 128 kbaud, 67 ms at 1.5 Mbaud. Every
measurement that wants raw samples is a burst of exactly that shape, so this
costs nothing that was wanted.

The arithmetic lives in `shared/link/include/link_wire.h`, where the poll
schedule derives its rates from the configured baud rather than hardcoding
them — so moving from the bench to the finished board is a constant, not a
rewrite. A `#error` there fires if a future page grows the frame past what the
bring-up link's budget allows.

### What the schematic settles

Read off the board schematic rather than inferred, and it closes three open
questions and overturns one assumption.

**The panel's RS485 is real, and it is 3.3 V.** U6 is an **SP3485EN** on
**GPIO16 (TX) and GPIO15 (RX)** — the opposite of the obvious reading, and the
schematic's own pin table does not settle it: that table calls GPIO15
`RS485_TX`, which is the naming of the *transceiver's* data directions, not the
ESP32's. The connectivity settles it twice over. GPIO15 reaches U6 pin 1, `RO`
— the receiver's *output*, which the ESP32 cannot drive. GPIO16 reaches pin 4,
`DI`, and also the input of the buffer that operates the direction line, and an
automatic-direction circuit only makes sense watching the line the ESP32
transmits on.

**GPIO6 reaches a connector.** It is on **J8**, a three-pin header carrying
3V3, GND and GPIO6 and nothing else — a rail and a ground beside it, which is
exactly what a monostable on a small daughterboard wants. The pin table lists
GPIO6 against no peripheral at all: the one genuinely uncommitted fast pin.

**The direction circuit is an RC one-shot, and it sets a floor rather than a
ceiling.** An SN74LVC1G125 follows the transmit line and charges C51 (1 nF)
through R76 (200 kΩ); the gate turns on Q1, which pulls DE and /RE low against
R79's 1 kΩ pull-up, and a Schottky across R76 dumps the gate the instant the
line falls. So the driver enables on the first start bit and releases only
after the line has been high long enough to reach the FET's threshold —
72 µs at Vth = 1.0 V, 179 µs at 2.0 V.

That has a consequence nobody was looking for. The longest run of high bits
inside an 8N1 frame is nine bit times, and **if that run outlasts the hold, the
driver switches off mid-frame and the rest of the transmission never reaches
the bus.** Nine bit times must fit inside the worst-case 72 µs, which puts a
floor at about **125,000 baud**. 1.5 Mbaud clears it twelvefold and 256 kbaud
twofold; **128 kbaud clears it by two percent, which is not a margin** — so the
bring-up rate is 256 kbaud, and `link_wire.h` has an `#error` for anything
below the floor.

The record previously carried this as an open question about a possible baud
*ceiling*. There is no ceiling; there is a floor, and the rate that was nearly
chosen sits on it.

**And the far end must not answer too early.** The hold runs from the last
*falling* edge rather than the end of the frame — a final byte of `0xFF` starts
its hold nine bit times early — so the coprocessor waits a conservative 200 µs
after the last received byte. At 1.5 Mbaud that is 37% on top of a whole-page
transaction; at 256 kbaud, 6%.

The coprocessor's own end is a MAX485 breakout with an explicit DE/RE pin,
which is the easier half: the turnaround is under firmware control, and its one
trap is not releasing the driver until the last stop bit has left the shift
register.

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

21 binaries, each printing one line per case: `test_gfx`,
`test_touch_map`, `test_nav`, `test_widgets`, `test_bench`, `test_motor`,
`test_settings`,
`test_logfile`, `test_link_crc`, `test_link_frame`, `test_link_pages`,
`test_link_watchdog`, `test_link_loopback`, `test_heartbeat`,
`test_servo_limit`, `test_servo_sync`, `test_openyge_frame`,
`test_openyge_status`, `test_openyge_params`, `test_logview` and
`test_logwriter`. The
harness is `test/host/greatest.h`, about a hundred lines, with nothing
vendored. `tools/check_docs.py` holds that list to this file — a page in the
predecessor said *seven* for two releases after it was ten, and nobody reads a
doc looking for that.

All five tools run in CI again: `render_ui.py --check` holds eleven committed
screenshots to the current render, and `frame_cost.py` holds the steady-state
frame to 12,000 cache-line fills. It currently costs **740**.

<!-- coverage:start -->
![coverage](https://img.shields.io/badge/host--test%20coverage-95.0%25-brightgreen)

| File | Lines | Covered | Coverage |
| --- | ---: | ---: | ---: |
| `shared/gfx/gfx.c` | 514 | 498 | 96.9% |
| `shared/touch/touch_map.c` | 100 | 100 | 100.0% |
| `shared/ui/ui_theme.c` | 42 | 41 | 97.6% |
| `shared/ui/ui_widgets.c` | 111 | 104 | 93.7% |
| `shared/ui/ui_icons.c` | 109 | 109 | 100.0% |
| `shared/ui/ui_band.c` | 34 | 32 | 94.1% |
| `shared/ui/ui_watermark.c` | 21 | 19 | 90.5% |
| `shared/ui/ui_plot.c` | 138 | 127 | 92.0% |
| `shared/ui/ui_hero.c` | 19 | 18 | 94.7% |
| `shared/ui/ui_slider.c` | 91 | 83 | 91.2% |
| `shared/ui/ui_tabs.c` | 45 | 40 | 88.9% |
| `shared/ui/ui_router.c` | 128 | 124 | 96.9% |
| `shared/ui/splash_screen.c` | 60 | 57 | 95.0% |
| `shared/ui/overview_screen.c` | 65 | 60 | 92.3% |
| `shared/ui/stub_screen.c` | 45 | 40 | 88.9% |
| `shared/ui/motor_screen.c` | 163 | 157 | 96.3% |
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
| `shared/link/link_crc.c` | 7 | 7 | 100.0% |
| `shared/link/link_frame.c` | 112 | 107 | 95.5% |
| `shared/link/link_dev.c` | 69 | 66 | 95.7% |
| `shared/link/link_host.c` | 69 | 63 | 91.3% |
| `shared/bench/bench_state.c` | 61 | 57 | 93.4% |
| `shared/bench/throttle.c` | 53 | 45 | 84.9% |
| `shared/bench/telemetry_sim.c` | 47 | 44 | 93.6% |
| `shared/bench/log_writer.c` | 43 | 40 | 93.0% |
| **total** | **4951** | **4701** | **95.0%** |

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

What the link codec has to survive on that laptop, before it ever sees a wire:
CRC against bit-flipped frames, resynchronisation after truncation and injected
noise so that a corrupt frame is **never** accepted, the watchdog transition
against a mock clock, and register round-trips at both ends of every range.

## What is still unsettled

Three of the six things this list carried were closed by the board schematic —
the RS485 pins, whether GPIO6 reaches a connector, and the direction circuit's
turnaround, all of which are now written up under
[what the schematic settles](#what-the-schematic-settles). A fourth was closed
by being answered wrongly: the question was which end set a baud *ceiling*, and
there is no ceiling.

Two remain, and two are new.

| | Where it stands |
| --- | --- |
| **INA228** is the sensing part | Right on merit — 85 V, twenty bits, charge and energy accumulated in hardware — but back-ordered into January 2027, and the obvious substitute stops at 36 V, which is under 8S. Check the INA238 before a layout commits to it. |
| **The MAX485 breakout's 5 V against the RP2350's power-up order** | The breakout sits at the coprocessor end, where Bank 0 *is* 5 V tolerant — but only while the 3.3 V rail is up. On a module build the 5 V rail comes up first, so at every power-on the transceiver can drive 5 V into an unpowered input. A 2.2 kΩ series resistor on RO bounds the clamp current and costs eleven nanoseconds of edge. Unresolved only in the sense that the part is not fitted yet. |
| **Seven OpenYGE numbers want a logic analyser** | The protocol came from YGE's developer as code rather than as a document, and code answers "what does this do" rather than "what does the wire guarantee". The RPM scale is the one that actually contradicts itself — the field is described as 0.1 eRPM and multiplied by ten — and a wrong answer there is a tachometer that reads a hundredfold out. [The spec](docs/OpenYGE.md) lists all seven; each is one measurement, and the parameter indices want confirming before anything is *written* to an ESC. |
| **Q1's threshold voltage is not on the schematic** | The direction circuit's hold time — and therefore the baud floor — depends on it, and the schematic names R76, C51, D7 and R79 but not the FET. The floor is quoted from the pessimistic end of a plausible range (1.0 V → 72 µs → 125 kbaud). Worth one measurement: scope DE against TX at 256 kbaud and read the release directly. |

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
2. **The link, on silicon** — when the module lands.
3. **Make the numbers real** — a KISS frame or bidirectional DShot, and the
   current sensor. The bench stops simulating. [OpenYGE](docs/OpenYGE.md) now
   looks like the shortest path through this: a YGE ESC reports voltage,
   current, consumption, eRPM and four temperatures itself, which fills every
   modelled field of `bench_state` **without** the INA228 that is back-ordered
   into 2027. It does not replace that sensor — the ESC reports what it
   believes about itself and a shunt reports what the bench knows — but it
   takes the watermark off the screen a year earlier.
4. **Make them keep** — the logger, then the viewer reads what the bench wrote.
5. **Make it drive** — servo outputs, then the sum-signal protocols, then the
   servo tester as it is described on the box.
6. **Make it listen** — one bus decoder at a time; each is a day and each adds a
   product feature.
7. **Make it programme** — BLHeli_S and AM32 first, because they need nobody's
   permission; Hitec first on the servo side, for the same reason. OpenYGE
   reads and writes an ESC's whole configuration over the same wire as its
   telemetry, with no reverse engineering and the vendor's own help, so it is
   arguably ahead of both.

Nothing in steps 1 to 6 is blocked on anything but time.

## The reference

[`docs/`](docs/) is the wiki source and the reference behind this record:
[what this is for](docs/Manifest.md), [the link](docs/Link.md),
[safety](docs/Safety.md), [the servo procedures](docs/Servo.md),
[the OpenYGE protocol](docs/OpenYGE.md), and [building](docs/Building.md). Pages are written
by the commit that lands the code they describe, so a page that is missing is a
subsystem that is not here yet.

## License

MIT — see [LICENSE](LICENSE).

---

In collaboration with Claude Code.
