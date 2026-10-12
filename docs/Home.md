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
| Feature list | [What this is for](Manifest.md) |
| Choose the parts of a bench and wire them | [Bench configurator](https://subtilitas.github.io/rcbench/) (web page: parts list, wiring drawing and pin-to-pin table for the selected setup) |
| Build and flash both boards | [Building](Building.md) |
| Wire the two boards and verify the bus | [Bringing up the link](Bringup.md) |
| Operate the screens | [Screens](Screens.md) |
| Balance a propeller or a ducted fan | [Balancing](Balance.md) |
| Measure a servo's installed limit, or synchronise two servos | [Servo procedures](Servo.md) |
| Read, back up and change the settings of a KST or Chaservo servo | [Servo programmer for KST and Chaservo servos](ServoProgrammer.md) |
| Program an ESC through its throttle-stick menu | [Stick programming](StickProgramming.md) |
| Connect a receiver and inspect its output | [Receiver buses](Receivers.md) |
| Stop mechanisms and the external circuit they require | [Safety](Safety.md) |
| ESCs with BLHeli_32 firmware: what is and is not supported | [Parameters of ESCs with BLHeli_32 firmware](BLHeli32.md) |

## Reference

| Page | Content |
| --- | --- |
| [The link](Link.md) | CAN wiring, failure behaviour, the page protocol |
| [The OpenYGE protocol](OpenYGE.md) | ESC telemetry and parameter protocol specification |
| [Performance](Performance.md) | Rendering budget in cache-line fills |

The state of the project, the open items and the settled decisions are recorded
in [STATUS.md](https://github.com/subtilitas/rcbench/blob/main/STATUS.md).

Maker, product, protocol and firmware names belong to their owners and are
used only to describe compatibility:
[Legal notice and disclaimer](https://github.com/subtilitas/rcbench/blob/main/README.md#legal-notice-and-disclaimer).

Every page has a German version; the switch is at the top of each page.
