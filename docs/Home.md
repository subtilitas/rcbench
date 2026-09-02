# rcbench

<sub>**English** · [Deutsch](Home-de.md)</sub>

<sub>These pages are generated from `docs/` in the repository and overwritten
on every push to `main`. Edit the files, not the wiki.</sub>

rcbench is a motor, ESC (electronic speed controller) and servo test bench
built from two processors: an ESP32-S3 touch panel (user interface, settings,
SD card) and an RP2350 coprocessor (measurement, outputs, and every protocol
with timing requirements). The two are connected over CAN (Controller Area
Network) at 1 Mbit/s.

## Using the bench

| Task | Page |
| --- | --- |
| Feature list and the state of each feature | [What this is for](Manifest.md) |
| Build and flash both boards | [Building](Building.md) |
| Wire the two boards and verify the bus | [Bringing up the link](Bringup.md) |
| Operate the screens | [Screens](Screens.md) |
| Balance a propeller or a ducted fan | [Balancing](Balance.md) |
| Measure a servo's installed limit, or synchronise two servos | [Servo procedures](Servo.md) |
| Connect a receiver and inspect its output | [Receiver buses](Receivers.md) |
| Stop mechanisms and the external circuit they require | [Safety](Safety.md) |
| BLHeli_32 ESCs: what is and is not supported | [BLHeli_32 parameters](BLHeli32.md) |

## Reference

| Page | Content |
| --- | --- |
| [The link](Link.md) | CAN wiring, failure behaviour, the page protocol |
| [The OpenYGE protocol](OpenYGE.md) | ESC telemetry and parameter protocol specification |
| [Performance](Performance.md) | Rendering budget in cache-line fills |

The state of the project, the open items and the settled decisions are recorded
in [STATUS.md](https://github.com/subtilitas/rcbench/blob/main/STATUS.md).

Every page has a German version; the switch is at the top of each page.
