# rcbench

A **motor, ESC and servo test bench** in two halves: an ESP32-S3 panel that
decides, draws and stores, and an RP2350 coprocessor that measures, drives and
talks to everything with a deadline.

> **Where this stands.** The foundation is being laid. The tree and its build
> wiring are down and proven: `shared/link` compiles out of one directory into
> the panel firmware, the coprocessor firmware and the host suite, and all
> three build. What is claimed as *inherited* is running in
> [esp32display7](https://github.com/subtilitas/esp32display7) and comes over
> with its tests. This file is the running record — it says what is true today,
> not what is intended, and every table in it is updated by the commit that
> changes the answer.

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
| Settings model | inherited | **ported**, with the model half of its suite; the screen cases return with the screen |
| Locale-tolerant CSV and number parsing | inherited | **ported**, with its fixtures |
| Board, display, GT911, SD storage | inherited | to port as-is |
| Golden-image renderer, coverage, doc and frame-cost checks | inherited | to port as-is |
| **The link codec** | new | framing, the page/register envelope and a resynchronising decoder, written and tested; the page map and both watchdogs next |
| **The safety heartbeat** | new | not written |
| Splash, overview, bench, settings and log-viewer screens | re-cut | against the two-processor model |
| Coprocessor firmware | new | builds in CI with the codec linked in; the failsafe and the PIO assembler proven on the host; only the UART transport is untested silicon |
| Throttle output on the panel | **removed** | GPIO6 is the heartbeat; the panel emits no servo pulse |
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
    bench/                bench_state · units · throttle policy

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
- **Two watchdogs, the tighter one on the coprocessor.** It fills failsafe
  values after 200 ms of silence on its own authority; the host escalates after
  a second.
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

The finished board runs the panel's own RS485 interface at **1.5 Mbaud with
automatic direction control**. The module build — where the protocol firmware
is actually being written — runs a breakout at **128 or 256 kbaud with an
explicit direction pin**. Twelvefold, so anything that merely fits at 1.5 Mbaud
has to be checked against the bring-up rate before it is relied on. 8N1 is ten
bits per byte:

| | 128 kbaud | 256 kbaud | 1.5 Mbaud |
| --- | ---: | ---: | ---: |
| Throughput | 12.8 kB/s | 25.6 kB/s | 187.5 kB/s |
| A whole-page poll (8 + 72 bytes) | 6.25 ms | 3.13 ms | 0.53 ms |
| Whole-page polls per second | **160** | 320 | 1875 |

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

### The two ends are not symmetric

The panel uses **its own on-board RS485 interface**, which Waveshare document as
having automatic transmit/receive control, with A and B leaving on a PH2.0
terminal. So the panel drives no direction line, needs no transceiver wired to
it, and has no 5 V logic anywhere near it. GPIO6 stays the heartbeat and 43/44
stay free.

The coprocessor end is a **MAX485 breakout with an explicit DE/RE pin**, which
the RP2350 drives. That is the easier half — the turnaround is under firmware
control rather than being a property of an RC circuit — and its one trap is
the familiar one: do not release the driver until the last stop bit has left
the shift register.

So the transport is written for both, because it has to be: automatic
direction on one end, an explicit pin on the other, one protocol between them.

**The turnaround is still not modelled**, because it is a property of a part
rather than of arithmetic. An automatic-direction circuit holds its driver
enabled for a fixed time after the last edge, which puts a floor under bus
turnaround. Now that the panel's own interface is the one carrying the bench
link, that floor is measurable today rather than at the end — and it is on the
finished board too, so it is worth measuring early.

## The safety line is a heartbeat, not an enable

A static enable-high line fails the most likely failure — firmware wedges with
the pin still high and the outputs stay live. So the loop that draws the STOP
button and reads touch toggles **GPIO6** at 100 Hz to 1 kHz, and the
coprocessor's output enable and its servo and ESC power path are gated from a
retriggerable monostable with a 20–50 ms window.

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
| `ci.yml` | push / PR / tag `v*` / manual | host suite, coverage `--check`, the font and documentation checks, the ESP-IDF matrix (v5.4, v5.5) building the panel, the pico-sdk build of the coprocessor, firmware artifacts |
| `docs.yml` | push to `main` touching `docs/` | publishes `docs/` to the GitHub wiki |
| `release.yml` | tag `v*` | builds both images, packages them, opens a release |

Six binaries, each printing one line per case: `test_gfx`, `test_touch_map`,
`test_settings`, `test_logfile`, `test_link_crc` and `test_link_frame`. The
harness is `test/host/greatest.h`, about a hundred lines, with nothing
vendored. `tools/check_docs.py` holds that list to this file — a page in the
predecessor said *seven* for two releases after it was ten, and nobody reads a
doc looking for that.

Two of the tools have nothing to do yet. `render_ui.py` and `frame_cost.py`
both need the screens, which are being re-cut; they return to CI with them, and
[Building](docs/Building.md) says so rather than leaving a gap for somebody to
find.

<!-- coverage:start -->
![coverage](https://img.shields.io/badge/host--test%20coverage-93.4%25-brightgreen)

| File | Lines | Covered | Coverage |
| --- | ---: | ---: | ---: |
| `shared/gfx/gfx.c` | 467 | 411 | 88.0% |
| `shared/touch/touch_map.c` | 100 | 100 | 100.0% |
| `shared/ui/ui_theme.c` | 42 | 41 | 97.6% |
| `shared/settings/settings.c` | 131 | 125 | 95.4% |
| `shared/logfile/log_numbers.c` | 393 | 369 | 93.9% |
| `shared/logfile/log_csv.c` | 571 | 541 | 94.8% |
| `shared/logfile/log_fields.c` | 46 | 45 | 97.8% |
| `shared/link/link_crc.c` | 7 | 7 | 100.0% |
| `shared/link/link_frame.c` | 112 | 107 | 95.5% |
| **total** | **1869** | **1746** | **93.4%** |

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

## Six things this repository has not yet settled

The research behind the split is thorough, but a record is not a measurement,
and a part on the bench is not the part on the schematic.

| Claim | Where it stands |
| --- | --- |
| RS485 on the panel's **GPIO15/16**, with automatic direction control | Waveshare's documentation says so and lists no direction pin; the expander's bit assignments corroborate it by omission. But the inherited pin map claims neither pin, so it is unconfirmed here. GPIO 6, 15, 16, 19, 20, 43 and 44 are what the panel does not already own, which is at least consistent. **This is now load-bearing rather than incidental** — the bench link runs on that interface, so it is the first thing to check on the board rather than something to confirm later. |
| **Which end sets the bench's baud ceiling** | The bench runs at 128 or 256 kbaud against a target of 1.5 Mbaud, and the reason was the cheap transceiver. With the breakout moved to the coprocessor end and driven by an explicit DE pin, the MAX485 itself is good well past 1.5 Mbaud — so if something is holding the bench to 128 k it may be the panel's *automatic-direction* circuit, which does not go away on the finished board. Worth establishing which, because the answer decides whether 1.5 Mbaud is a target or an assumption. |
| **GPIO6 reaches a connector** | The wiki describes a PH2.0 header carrying ADC, CAN, I²C and RS485 without naming the ADC pin. If it does not reach one, the fallback is to *watch* SCL rather than drive a line — continuous traffic on that bus **is** the touch controller being polled, which is precisely what the heartbeat exists to prove. |
| The auto-direction circuit's **turnaround time** | Unmeasured, and it puts a floor under bus turnaround. The first thing to check if the link misbehaves, and the reason not to raise the baud rate past what the traffic needs. |
| **The MAX485 breakout's 5 V against the RP2350's power-up order** | The breakout sits at the coprocessor end, where Bank 0 *is* 5 V tolerant — but only **while the 3.3 V rail is up**. On a module build the 5 V rail (VBUS or VSYS) comes up first and the module's own regulator follows, which is exactly the wrong order: at every power-on the transceiver can drive 5 V into an unpowered input. A series resistor of about 2.2 kΩ on RO into the RX pin bounds the clamp current and costs 11 ns of edge at 5 pF — invisible even at 1.5 Mbaud. Unresolved only in the sense that the part is not fitted yet. |
| **INA228** is the sensing part | Right on merit — 85 V, twenty bits, charge and energy accumulated in hardware — but back-ordered into January 2027, and the obvious substitute stops at 36 V, which is under 8S. Check the INA238 before a layout commits to it. |

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

## The order of work

1. **The foundation** — the tree, the ported floor with its tests green, the
   tools and CI, the link codec, the heartbeat, the coprocessor skeleton, the
   re-cut screens.
2. **The link, on silicon** — when the module lands.
3. **Make the numbers real** — a KISS frame or bidirectional DShot, and the
   current sensor. The bench stops simulating.
4. **Make them keep** — the logger, then the viewer reads what the bench wrote.
5. **Make it drive** — servo outputs, then the sum-signal protocols, then the
   servo tester as it is described on the box.
6. **Make it listen** — one bus decoder at a time; each is a day and each adds a
   product feature.
7. **Make it programme** — BLHeli_S and AM32 first, because they need nobody's
   permission; Hitec first on the servo side, for the same reason.

Nothing in steps 1 to 6 is blocked on anything but time.

## The reference

[`docs/`](docs/) is the wiki source and the reference behind this record:
[what this is for](docs/Manifest.md), [the link](docs/Link.md),
[safety](docs/Safety.md), and [building](docs/Building.md). Pages are written
by the commit that lands the code they describe, so a page that is missing is a
subsystem that is not here yet.

## License

MIT — see [LICENSE](LICENSE).
