# What this is for

<sub>**English** · [Deutsch](Manifest-de.md)</sub>

The bench measures and drives the parts of an RC (radio control) drive system:
motor and ESC (electronic speed controller), servos, receiver buses, propeller
balance and battery condition. This page lists the original requirements and
the state of each.

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

## State of each requirement

| Requirement | Implementation | State |
| --- | --- | --- |
| External current sensors over I²C (Inter-Integrated Circuit) | On the coprocessor's I²C bus. The panel's I²C bus is reserved for the touch controller and the I/O expander. | Interface defined; no driver. Parts selected: INA238 (motor), INA745A (servo rail). |
| ESC programmer (AM32 and BLHeli_S required) | One-wire half-duplex bootloader protocol at 19,200 baud on a PIO (programmable input/output) state machine | Screen built and table-driven for BLHeli_S, AM32, ESCape32 and VESC; no protocol is transmitted. BLHeli_32 parameters are not supported: [BLHeli_32](BLHeli32.md). |
| Balancing with accelerometer and index sensor | Both sensors on the coprocessor, sampled on one timebase | Screen and placement guides built; the measurement waits on the sensors |
| Servo tester with S.BUS and other protocols | Hardware PWM (pulse-width modulation) outputs; one PIO program per serial protocol | Screen built and commanding over the link; no output driver produces pulses |
| Servo programming | The Hitec D-series protocol is published; other brands need a programmer to capture | Hitec table in the programmer screen; KST (a servo manufacturer) held at the owner's request |
| Log viewer for several formats | `shared/logfile`: a CSV (comma-separated values) reader that accepts decimal comma or point, a units row and ragged rows | Built: browse, import view, plot. Runs are recorded to the card while the bench is armed. |

## Not planned

- **Configuration over JETI EX Bus.** The EX Bus specification restricts remote
  configuration to JETI products and does not document it. EX Bus support
  covers channel values and telemetry.
- **BLHeli_32 parameters.** The information needed to read them is not
  published; a request to the rights holder was declined in August 2026.
  [Details](BLHeli32.md).
- **KST servo programming.** Held at the owner's request.

## Licensing rule

Every protocol is implemented from its published specification. Most open
implementations of these protocols are GPL (GNU General Public License) or AGPL
(GNU Affero General Public License), which is incompatible with this
repository's MIT (Massachusetts Institute of Technology) licence. A protocol
without a published specification is not implemented.
