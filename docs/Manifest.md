# What this is for

<sub>**English** · [Deutsch](Manifest-de.md)</sub>

The bench measures and drives the parts of an RC (radio control) drive system:
motor and ESC (electronic speed controller), servos, receiver buses, propeller
balance and battery condition. This page lists the original requirements and
how each is met.

## Requirements

The original requirement list, in German:

> - Eingang für externe Stromsensoren (I²C)
> - ESC-Programmierer für das, was geht — im Moment AM32 und BLHeli garantiert,
>   YGE mal anfragen oder über EX-Bus einfach machen
> - Wuchten von Systemen über Beschleunigungssensor und Positionssensor
> - Unterstützung Reglerprogrammierung, soweit es geht
> - Servo-Tester mit allem und scharf: SBUS und andere Protokolle
> - Servo-Programmierung, wenn mir jemand Programmiergeräte ausleiht und das
>   Reverse Engineering klappt, respektive über EX-Bus
> - Logviewer für diverse Formate

## Implementation

| Requirement | Implementation |
| --- | --- |
| External current sensors over I²C (Inter-Integrated Circuit) | On the I²C buses of the IO board's measurement IOmcu, the second of its two RP2354B (hardware/docs/IOBoard.md). The panel's I²C bus is reserved for the touch controller and the I/O expander. |
| ESC programmer (ESCs with AM32 and BLHeli_S firmware required) | One-wire half-duplex bootloader protocol at 19,200 baud on a PIO (programmable input/output) state machine |
| Balancing with accelerometer and index sensor | Both sensors on the IOmcu, sampled on one timebase |
| Servo tester with S.BUS and other protocols | Hardware PWM (pulse-width modulation) outputs; one PIO program per serial protocol |
| Servo programming | The Hitec D-series protocol is published; other brands need a programmer to capture |
| Log viewer for several formats | `shared/logfile`: a CSV (comma-separated values) reader that accepts decimal comma or point, a units row and ragged rows |

The state of each requirement is in [STATUS.md](https://github.com/subtilitas/rcbench/blob/main/STATUS.md).

## Not planned

- **Configuration over JETI EX Bus.** The EX Bus specification restricts remote
  configuration to JETI products and does not document it. EX Bus support
  covers channel values and telemetry.
- **Parameters of ESCs with BLHeli_32 firmware.** The information needed to read them is not
  published; a request to the rights holder was declined in August 2026.
  [Details](BLHeli32.md).

## Licensing rule

Every protocol is implemented from its published specification or, where
none is published, from captures of the wire on hardware the project owns:
the programming protocol of KST and Chaservo servos is the one case. Most open
implementations of these protocols are GPL (GNU General Public License) or AGPL
(GNU Affero General Public License), which is incompatible with this
repository's MIT (Massachusetts Institute of Technology) licence. A protocol
without a published specification and without a capture of its wire is not
implemented.
