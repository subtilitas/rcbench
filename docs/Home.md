# rcbench

<sub>**English** · [Deutsch](Home-de.md)</sub>

<sub>These pages are generated from `docs/` in the repository and overwritten
on every push to `main`. Edit the files, not the wiki.</sub>

rcbench is a motor, ESC (electronic speed controller) and servo test bench
built from two processors: an ESP32-P4 touch panel, the Waveshare ESP32-P4 7B
(user interface on a 1024x600 MIPI DSI (Mobile Industry Processor Interface,
Display Serial Interface) display, settings, SD card, Wi-Fi through its
ESP32-C6), and an RP2350 IOmcu (IO microcontroller: measurement, outputs,
and every protocol with timing requirements). The two are connected over CAN (Controller Area
Network) at 1Mbit/s.

## Using the bench

| Task | Page |
| --- | --- |
| Feature list | [What this is for](Manifest.md) |
| Choose the parts of a bench and wire them | [Bench configurator](https://subtilitas.github.io/rcbench/) (web page: parts list, list to buy with maker pages and web search links, wiring drawing and pin-to-pin table for the selected setup) |
| Build and flash both boards | [Building](Building.md) |
| Wire the two boards and verify the bus | [Bringing up the link](Bringup.md) |
| Flash both boards and run the bench for the first time | [First run on hardware](FirstRun.md) |
| Operate the screens | [Screens](Screens.md) |
| Balance a propeller or a ducted fan | [Balancing](Balance.md) |
| Measure a servo's installed limit, or synchronise two servos | [Servo procedures](Servo.md) |
| Read, back up and change the settings of a KST or Chaservo servo | [Servo programmer for KST and Chaservo servos](ServoProgrammer.md) |
| Read the settings of a Hitec servo | [Servo programmer for Hitec servos](HitecProgrammer.md) |
| Program an ESC through its throttle-stick menu | [Stick programming](StickProgramming.md) |
| Connect a receiver and inspect its output | [Receiver buses](Receivers.md) |
| Stop mechanisms and the external circuit they require | [Safety](Safety.md) |
| ESCs with BLHeli_32 firmware: what is and is not supported | [Parameters of ESCs with BLHeli_32 firmware](BLHeli32.md) |

## Reference

| Page | Content |
| --- | --- |
| [The rbmod carrier](Carrier.md) | The carrier board's pins, connectors, power and rev 1 rework |
| [The link](Link.md) | CAN wiring, failure behaviour, the page protocol |
| [The programming port for KST and Chaservo servos](LinkKST.md) | The wire protocol of the programming port for KST and Chaservo servos |
| [The programming port for Hitec servos](LinkHitec.md) | The wire protocol of the programming port for Hitec servos |
| [The ESC settings pages](LinkEsc.md) | The pages that read and write an ESC's settings over its signal wire |
| [The INA3221 map and windows](LinkSensors.md) | The INA3221 current monitors' map and sample windows |
| [The bench console](BenchConsole.md) | The IOmcu's command console on its USB serial port, a bench tool |
| [DShot and the output drivers](DShot.md) | DShot frames, telemetry and the output drivers |
| [The OpenYGE protocol](OpenYGE.md) | ESC telemetry and parameter protocol specification |
| [ESC programming profiles](EscProfiles.md) | The profiles for ESC programming |
| [ESC signal wire](EscWire.md) | Reading and writing an ESC's settings over its signal wire: AM32, Bluejay, BLHeli_S, ESCape32 |
| [Performance](Performance.md) | Rendering budget in cache-line fills |
| [Interface language](Language.md) | The interface language and its texts |

The state of the project, the open items and the settled decisions are recorded
in [STATUS.md](https://github.com/subtilitas/rcbench/blob/main/STATUS.md).

Maker, product, protocol and firmware names belong to their owners and are
used only to describe compatibility:
[Legal notice and disclaimer](https://github.com/subtilitas/rcbench/blob/main/README.md#legal-notice-and-disclaimer).

Every page has a German version; the switch is at the top of each page.
