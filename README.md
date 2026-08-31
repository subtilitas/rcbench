# rcbench

A **motor, ESC and servo test bench** in two halves: an ESP32-S3 touch panel
that decides, draws and stores, and an RP2350 coprocessor that measures, drives
and talks to everything with a deadline.

> The rule that settles arguments: **the coprocessor owns anything with a
> deadline, the panel owns anything with an opinion.**

<!-- coverage:badge:start -->
![coverage](https://img.shields.io/badge/host--test%20coverage-94.5%25-brightgreen)
<!-- coverage:badge:end -->

**Status — under construction.** The panel boots, every screen is built, and
the CAN link between the boards runs on silicon at 1 Mbit/s with zero errors at
either end. **It cannot drive anything yet:** no output turns a command into an
edge on a pin, and most measurements wait on parts that are not fitted. Every
screen showing invented numbers says so, across the screen, in words.

[**Where things stand**](STATUS.md) is the running record — what is built, what
is not, what is deliberately not going to be, and why.

## Safety

This bench exists to spin motors and drive servos. A spinning propeller can
cause severe injury, and a failed blade can be thrown at high speed. Keep clear
of the plane of rotation whenever the bench is armed.

**If you build this, you are responsible for what it drives.** Three
independent stops stand in the way — a STOP command, a heartbeat the outputs
die without, and the coprocessor's own watchdog — and
[Safety](https://github.com/subtilitas/rcbench/wiki/Safety) explains what each
catches and **what you must build for them to work**: the retriggerable
monostable that gates the outputs is not optional, and it is on no board yet.

Nothing here has been through any safety certification. It is a hobby
instrument, published in the state it is in. The MIT licence's "without
warranty of any kind" is not a formality on a tool like this — read it as
written.

## What it does

One board, one screen, one card, for the questions a drive system otherwise
answers by guessing:

- **Motor and ESC** — voltage, current, consumption, RPM and temperatures
  plotted live, from an ESC's own telemetry where it has any.
- **Servo** — drag the horn; the arm follows the *measured* position, so a slow
  servo shows as two arms rather than a sluggish picture.
- **Receiver** — sixteen channels with their movement, so "I moved that, which
  channel was it?" has an answer. A receiver in failsafe is called a liar in
  red.
- **Programmer** — BLHeli_S, AM32, ESCape32, VESC and Hitec servos, with the
  parameters drawn from a table rather than a screen per firmware.
- **Balance, battery, logs** — and honest marks on everything whose hardware is
  not fitted.

## Building

Three build systems read one source tree. The host suite needs only a C
compiler:

```bash
cmake -S test/host -B test/host/build -DCMAKE_BUILD_TYPE=Debug
cmake --build test/host/build
ctest --test-dir test/host/build --output-on-failure
```

The two firmwares need ESP-IDF v5.4+ and pico-sdk 2.0+ respectively.
[Building](https://github.com/subtilitas/rcbench/wiki/Building) has the
toolchains, the commands and what CI checks.

## Documentation

The [wiki](https://github.com/subtilitas/rcbench/wiki) is the manual, in
**English and German**, and is generated from [`docs/`](docs) — edit the files,
not the wiki.

| | |
| --- | --- |
| [What this is for](https://github.com/subtilitas/rcbench/wiki/Manifest) | the pitch, and where each line of it stands |
| [Bringing up the link](https://github.com/subtilitas/rcbench/wiki/Bringup) | wiring the two boards and proving the wire |
| [Screens](https://github.com/subtilitas/rcbench/wiki/Screens) | operating the bench |
| [Balancing](https://github.com/subtilitas/rcbench/wiki/Balance) · [Servo procedures](https://github.com/subtilitas/rcbench/wiki/Servo) · [Receiver buses](https://github.com/subtilitas/rcbench/wiki/Receivers) | the measurements |
| [Safety](https://github.com/subtilitas/rcbench/wiki/Safety) | how it stops, and what you must build |
| [The link](https://github.com/subtilitas/rcbench/wiki/Link) · [OpenYGE](https://github.com/subtilitas/rcbench/wiki/OpenYGE) · [Performance](https://github.com/subtilitas/rcbench/wiki/Performance) | reference, for working on the firmware |

## Contributing

[CONTRIBUTING.md](CONTRIBUTING.md) has the conventions, and two of them will
surprise you if you meet them by accident: **protocols are written from
specifications rather than from other implementations** (a licensing rule that
cannot be fixed after the fact), and **coverage has a per-file floor as well as
a total**. Both are enforced by CI.

Security and safety reports: [SECURITY.md](SECURITY.md). A bug that could make
the bench drive an output when it should not is the most serious kind this
project can have, and is handled the same way.

## Attributions and licences

**This project is MIT licensed** — see [LICENSE](LICENSE). What follows is
everything in it that is not.

**DejaVu Sans Mono.** The three font tables under `shared/gfx/` are glyph
bitmaps rendered from DejaVu Sans Mono by `tools/gen_font.py`. The font is
distributed under the Bitstream Vera Fonts Copyright, whose notice travels with
those bitmaps — see [NOTICE](NOTICE). No font file is redistributed here; the
generator reads one from the machine it runs on.

**Protocols are implemented from specifications, not from other people's
code.** Nearly every open implementation of these protocols is GPL or AGPL
against this repository's MIT, so each decoder here is written from the
published description of the wire. Where a permissive reference existed it was
still only read for confirmation: PX4's receiver decoders (BSD), and MIT
reference code for SRXL2, JETI EX Bus, DShot and DroneCAN.

**Debts of design, not of code:**

- **ArduPilot's IOMCU** — the two-processor split, and the ratio between the
  two watchdogs, are copied from an arrangement that has flown this exact
  problem for a decade.
- **YGE** — the [OpenYGE specification](https://github.com/subtilitas/rcbench/wiki/OpenYGE)
  here is written from their material, and is going to them to be checked.
- **BLHeli, AM32, ESCape32, VESC and Hitec** — their published protocols and
  their configurators, read before the programmer screen was drawn.
  [BLHeli_32's parameters are deliberately absent](https://github.com/subtilitas/rcbench/wiki/BLHeli32),
  and the reason is a key we asked for and were not given.

**Third-party code in the tree: none.** `test/host/greatest.h` is a 108-line
harness written for this project — it is not the `greatest` library it is named
after.
