# rcbench

<sub>**English** · [Deutsch](README-de.md)</sub>

[![CI (continuous
integration)](https://github.com/subtilitas/rcbench/actions/workflows/ci.yml/badge.svg)](https://github.com/subtilitas/rcbench/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/subtilitas/rcbench/branch/main/graph/badge.svg)](https://codecov.io/gh/subtilitas/rcbench)

A motor, ESC (electronic speed controller) and servo test bench on two
processors: an ESP32-S3 touch panel (user interface, settings, SD card) and an
RP2350 coprocessor (measurement, outputs, and every protocol with timing
requirements), connected over CAN (Controller Area Network) at 1 Mbit/s.

**Status: under construction.** The panel boots, every screen exists, and the
CAN link runs on hardware. No output produces a signal, and most measurements
wait on parts that are not fitted. Screens showing simulated values are marked
SIMULATION.

[STATUS.md](STATUS.md) records what is built, what is open and what is not
planned.

## Safety

This bench drives motors and servos. Keep clear of the propeller plane whenever
the bench is armed.

Three stop mechanisms are designed in: a heartbeat whose absence removes the
outputs, the coprocessor's own link watchdog, and a STOP command over the link.
The heartbeat requires a retriggerable monostable that is on no board; the
STOP command over the link is written and not run on hardware.
[Safety](https://github.com/subtilitas/rcbench/wiki/Safety) specifies all
three.

Nothing here has been through a safety certification. The MIT (Massachusetts
Institute of Technology) licence's "without warranty of any kind" applies.

## Features

| Screen | Function | State |
| --- | --- | --- |
| Motor & ESC | voltage, current, consumption, RPM (revolutions per minute) and temperatures plotted live | screen built; values simulated |
| Servo | commanded and measured position; installed-limit search; two-servo synchronisation | screen built and commanding over the link; no output driver |
| Analyser | sixteen receiver channels with history, the digital channels, LIVE / FRAME LOST / FAILSAFE / SILENT | S.BUS decoder built; PIO (programmable input/output) receiver not written |
| Programmer | BLHeli_S, AM32, ESCape32, VESC and Hitec parameter tables | screen built; no protocol on a wire |
| Balance | blade count, correction mass and angle, sensor placement guides | screen built; sensors not fitted |
| Battery | per-cell spread and verdict | screen built; cell monitor not fitted |
| Logs | browse, import and plot CSV (comma-separated values) from the card; runs are recorded while armed | built |
| Setup | settings in both themes, stored in NVS | built; persistence not run on hardware |

## Building

The host suite needs a C compiler and CMake:

```bash
cmake -S test/host -B test/host/build -DCMAKE_BUILD_TYPE=Debug
cmake --build test/host/build
ctest --test-dir test/host/build --output-on-failure
```

The panel firmware needs ESP-IDF (Espressif Internet-of-Things Development
Framework) v5.4 or newer; the coprocessor firmware needs pico-sdk 2.0 or newer.
[Building](https://github.com/subtilitas/rcbench/wiki/Building) has the
toolchains, the commands and the CI gates.

## Documentation

The [wiki](https://github.com/subtilitas/rcbench/wiki) is generated from
[`docs/`](docs) in English and German. Edit the files, not the wiki.

| Page | Content |
| --- | --- |
| [What this is for](https://github.com/subtilitas/rcbench/wiki/Manifest) | requirements and their state |
| [Building](https://github.com/subtilitas/rcbench/wiki/Building) | toolchains, commands, CI |
| [Bringing up the link](https://github.com/subtilitas/rcbench/wiki/Bringup) | wiring the two boards and verifying the bus |
| [Screens](https://github.com/subtilitas/rcbench/wiki/Screens) | operating the bench |
| [Balancing](https://github.com/subtilitas/rcbench/wiki/Balance) · [Servo procedures](https://github.com/subtilitas/rcbench/wiki/Servo) · [Receiver buses](https://github.com/subtilitas/rcbench/wiki/Receivers) | the measurements |
| [Safety](https://github.com/subtilitas/rcbench/wiki/Safety) | stop mechanisms and the required external circuit |
| [The link](https://github.com/subtilitas/rcbench/wiki/Link) · [OpenYGE](https://github.com/subtilitas/rcbench/wiki/OpenYGE) · [Performance](https://github.com/subtilitas/rcbench/wiki/Performance) | firmware reference |

## Contributing

[CONTRIBUTING.md](CONTRIBUTING.md) has the rules and the checks CI runs. Two of
them cannot be corrected after the fact: protocols are implemented from
specifications, not from other implementations (a licensing rule), and coverage
has a per-file floor as well as a total.

Security and safety reports: [SECURITY.md](SECURITY.md).

## Licences and attributions

rcbench is MIT licensed: [LICENSE](LICENSE).

- **DejaVu Sans Mono.** The three font tables under `shared/gfx/` are glyph
  bitmaps rendered from DejaVu Sans Mono by `tools/gen_font.py`, under the
  Bitstream Vera Fonts Copyright ([NOTICE](NOTICE)). No font file is
  redistributed.
- **Protocols** are implemented from published specifications. Permissive
  reference code (PX4's receiver decoders under BSD (Berkeley Software
  Distribution); MIT reference code for SRXL2, JETI EX Bus, DShot and DroneCAN)
  was read for confirmation only.
- **Design references:** ArduPilot's IOMCU (the two-processor split and the
  watchdog ratio); YGE's OpenYGE material, from which the
  [specification](https://github.com/subtilitas/rcbench/wiki/OpenYGE) is
  written; the published protocols and configurators of BLHeli, AM32, ESCape32,
  VESC and Hitec.
- **Third-party code in the tree: none.** `test/host/greatest.h` is a test
  harness written for this project; it is not the `greatest` library.
