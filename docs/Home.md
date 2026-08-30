# rcbench

<sub>**English** · [Deutsch](Home-de.md)</sub>

<sub>These pages are generated from `docs/` in the repository and are
overwritten on every push to `main` — edit the files, not the wiki.</sub>

A **motor, ESC and servo test bench** in two halves: an ESP32-S3 touch panel
that decides, draws and stores, and an RP2350 coprocessor that measures,
drives and talks to everything with a deadline.

## Start here

| If you want to | Read |
| --- | --- |
| Know what the bench can do today, and what each thing still needs | [What this is for](Manifest.md) |
| Build and flash both boards | [Building](Building.md) |
| Wire the two boards together, and prove the wire | [Bringing up the link](Bringup.md) |
| Operate the screens | [Screens](Screens.md) |
| Balance a propeller or a ducted fan | [Balancing](Balance.md) |
| Find a servo's real endpoint, or match two servos on one surface | [Servo procedures](Servo.md) |
| Connect a receiver and see what it really sends | [Receiver buses](Receivers.md) |
| Understand how the bench stops, and what you must wire for it | [Safety](Safety.md) |
| Program a BLHeli_32 ESC | [BLHeli_32 parameters](BLHeli32.md) — identify and drive yes, parameters no |

## Reference

For working on the firmware rather than using the bench:

| | |
| --- | --- |
| [The link](Link.md) | The CAN link between the boards: wiring first, then the protocol |
| [The OpenYGE protocol](OpenYGE.md) | ESC telemetry and parameters, specified from the wire up |
| [Performance](Performance.md) | Why drawing code is budgeted in cache-line fills |

These pages tell you how to use and connect what exists. The repository's
[README](https://github.com/subtilitas/rcbench) is the running record — design
decisions, their reasons, and what is still unsettled live there.

Every page is also in German — the switch is at the top of each one.
