# rcbench

<sub>**English** · [Deutsch](README-de.md)</sub>

[![CI (continuous
integration)](https://github.com/subtilitas/rcbench/actions/workflows/ci.yml/badge.svg)](https://github.com/subtilitas/rcbench/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/subtilitas/rcbench/branch/main/graph/badge.svg)](https://codecov.io/gh/subtilitas/rcbench)

A motor, ESC (electronic speed controller) and servo test bench on two
processors: an ESP32-S3 touch panel (user interface, settings, SD card) and an
RP2350 coprocessor (measurement, outputs, and every protocol with timing
requirements), connected over CAN (Controller Area Network) at 1 Mbit/s.

**Status: under construction.** The project is under development and its
functions are not verified on hardware. The panel boots and every screen
exists. The output drivers are built. Switching to ARMED names the roles it drives: the
throttle outputs from MOTOR & ESC, the surfaces from SERVO. A role in ARMED
drives every pin bound to it at its rest whether or not anything commands it. What is unmeasured is
the timing on an instrument -- no bit width, frame period or reply delay in
this tree has been seen on a scope or an analyser. Most other measurements
wait on parts that are not fitted. Screens showing simulated values are marked
SIMULATION.

[STATUS.md](STATUS.md) records what is built, what is open and what is not
planned.

## Safety

This bench drives motors and servos. Keep clear of the propeller plane whenever
the bench is ARMED.

Switching to ARMED is deliberate: a two-second hold on ARM, with the command sent when the
hold completes rather than when the finger lifts. DISARM and STOP are a
single press.

Three stop mechanisms are designed in: a heartbeat whose absence removes the
outputs, the coprocessor's own link watchdog, and a STOP command over the link.
The line between the panel's J8 and the coprocessor's GP3 is fitted on the
bring-up bench. The heartbeat's retriggerable
monostable is on no board, so firmware at both ends is the only thing gating
the outputs and there is no hardware backstop behind it.
[Safety](https://github.com/subtilitas/rcbench/wiki/Safety) specifies all
three.

Nothing here has been through a safety certification. The MIT (Massachusetts
Institute of Technology) licence's "without warranty of any kind" applies.

## Features

| Screen | Function | State |
| --- | --- | --- |
| Motor & ESC | voltage, current, consumption, RPM (revolutions per minute) and temperatures plotted live | screen built; speed, voltage, current and ESC temperature from an ESC's bidirectional DShot telemetry; simulated values while no coprocessor answers |
| Servo | commanded and measured position; installed-limit search; two-servo synchronisation; servo types with their frame rates, a warning for those that can destroy a servo; a sweep through a square, sine or triangle run by the coprocessor; the supply's live power; an automatic test stepping the supply's voltage, with a CSV and a text report; a [programmer for KST and Chaservo servos](docs/ServoProgrammer.md) with backup, limit checks and a write switch | screen built and commanding over the link; drives the pins bound as surfaces, timing unmeasured; the limit search and the synchronisation are host-tested against a modelled servo and run on no board; the programmer is host-tested, no frame sent to a servo |
| Supply | the PD mini, a USB-PD (USB Power Delivery) supply: voltage and current limit set, output switched, V, A and W plotted and logged | screen built; the coprocessor drives the PD mini on a PIO (programmable input/output) UART (universal asynchronous receiver-transmitter) when SETUP enables it, and the panel runs a model otherwise |
| Analyser | sixteen receiver channels with history, the digital channels, LIVE / FRAME LOST / FAILSAFE / SILENT | S.BUS decoder built; PIO (programmable input/output) receiver not written |
| Programmer | parameter tables for ESCs with BLHeli_S, AM32, ESCape32 and VESC firmware and for Hitec servos; [ESC programming profiles](docs/EscProfiles.md) for throttle-stick menus: 72 families, 451 models | screen built; no protocol on a wire. Profiles built in and read from `/ESC/` on the card. [Stick programming](docs/StickProgramming.md) runs 24 of them from the PROGRAMMER screen, asking for a Kontronik ESC's jumper or button at its moment, against the panel's simulated ESC |
| Balance | blade count, correction mass and angle, sensor placement guides | screen built; sensors not fitted |
| Battery | per-cell spread and verdict | screen built; cell monitor not fitted |
| Logs | browse, import and plot CSV (comma-separated values) from the card; runs are recorded while ARMED or while the supply's output is on | built |
| Setup | settings in both themes, stored in NVS | built |

## Building

The host suite needs a C compiler, CMake and Python 3:

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
| [What this is for](https://github.com/subtilitas/rcbench/wiki/Manifest) | requirements and how each is met |
| [Bench configurator](https://subtilitas.github.io/rcbench/) | web page, English and German: select functions or parts, get the parts list, a list to buy with maker pages and web search links, the wiring drawing and the pin-to-pin table of that setup |
| [Panel help](https://subtilitas.github.io/rcbench/help/) | web page, English and German: every screen of the panel with screenshots of its states, what it shows and what it lets the user do |
| [Building](https://github.com/subtilitas/rcbench/wiki/Building) | toolchains, commands, CI |
| [Bringing up the link](https://github.com/subtilitas/rcbench/wiki/Bringup) | wiring the two boards and verifying the bus |
| [Screens](https://github.com/subtilitas/rcbench/wiki/Screens) | operating the bench |
| [First run on hardware](https://github.com/subtilitas/rcbench/wiki/FirstRun) | the checks for the first power-up of both boards |
| [Interface language](https://github.com/subtilitas/rcbench/wiki/Language) | English or German on the panel |
| [Balancing](https://github.com/subtilitas/rcbench/wiki/Balance) · [Servo procedures](https://github.com/subtilitas/rcbench/wiki/Servo) · [Receiver buses](https://github.com/subtilitas/rcbench/wiki/Receivers) | the measurements |
| [Safety](https://github.com/subtilitas/rcbench/wiki/Safety) | stop mechanisms and the required external circuit |
| [The link](https://github.com/subtilitas/rcbench/wiki/Link) · [OpenYGE](https://github.com/subtilitas/rcbench/wiki/OpenYGE) · [Performance](https://github.com/subtilitas/rcbench/wiki/Performance) · [DShot and the output drivers](https://github.com/subtilitas/rcbench/wiki/DShot) · [ESCs with BLHeli_32 firmware](https://github.com/subtilitas/rcbench/wiki/BLHeli32) | firmware reference |

## Contributing

[CONTRIBUTING.md](CONTRIBUTING.md) has the rules and the checks CI runs. Two of
them cannot be corrected after the fact: protocols are implemented from
specifications, not from other implementations (a licensing rule), and coverage
has a per-file floor as well as a total.

<!-- coverage:start -->
Host-suite line coverage of `shared/` and `protocols/`: **97.9%**, 40,509 of 41,389 lines in 144 files. CI fails below 94% in total or below 85% in any file. [STATUS.md](STATUS.md#tests-and-ci) has the table per file.
<!-- coverage:end -->

Security and safety reports: [SECURITY.md](SECURITY.md).

## Licences and attributions

rcbench is MIT licensed: [LICENSE](LICENSE).

- **DejaVu Sans Mono.** The three font tables under `shared/gfx/` are glyph
  bitmaps rendered from DejaVu Sans Mono by `tools/gen_font.py`, under the
  Bitstream Vera Fonts Copyright ([NOTICE](NOTICE)). No font file is
  redistributed.
- **Protocols** are implemented from published specifications; the
  programming protocol of KST and Chaservo servos, which has none, from wire
  captures of hardware the project owns. Permissive
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

## Legal notice and disclaimer

### Trademark notice

KST, CHASERVO and Hitec and their logos are trademarks or registered
trademarks of their respective manufacturers. The names are used in this
project in a purely descriptive way, to document compatibility with the
respective servos. This project is not affiliated with the named
manufacturers and is neither supported nor sponsored by them.

Every other name of a maker, product, protocol or firmware in this
repository is a name or trademark of its owner. Each appears only to
describe compatibility, to identify a part, to credit the source of a font
or to name a design or protocol reference. This covers:

- the servo makers and brands Futaba, Feetech and Dynamixel;
- the ESC (electronic speed controller) makers YGE, Kontronik, Hobbywing,
  Hacker, Jeti, Castle Creations, Graupner, Multiplex and Spektrum;
- the protocol and firmware names OpenYGE, S.BUS, S.BUS2, JETI EX Bus, SRXL2,
  DShot, KISS, BLHeli_S, BLHeli_32, AM32, ESCape32 and VESC;
- the board, module and component makers Waveshare, Raspberry Pi, Espressif,
  WeAct, Osoyoo, Matek, DAOKAI and Texas Instruments;
- the distributors Digi-Key, LCSC and JLCPCB;
- every other maker, product, protocol and firmware name in this repository.
  The section "Trademarks" of [NOTICE](NOTICE) has the list.

This project is not connected with any of the owners and is neither supported
nor sponsored by any of them.

### Disclaimer

rcbench is open-source software and hardware for self-build. Use, replication
and the programming of servos are at one's own risk. To the extent permitted
by law, the author accepts no liability for:

- damage to servos, for example from a wrong voltage, timing errors or
  corrupted settings data in the servo,
- damage to computers, microcontrollers or other connected hardware,
- injury or damage that results from operating the hardware.

The licence terms are in [LICENSE](LICENSE).
