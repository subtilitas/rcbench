---
screen: none
title: Sensors
order: 62
---

# Sensors

SENSORS, the fourth category on [SETUP](setup.md), sets up every device the IOmcu reads: the sensor bus, the INA228, up to four TI INA3221 three-channel current monitors at 0x40 to 0x43, the AS5600 output encoder and the phase tap. The settings are kept and saved like every other setting on SETUP.

## The left list

![Two parts fitted, 0x40 online](view:setup-sensors)

A list of 8 entries stands on the left, 220px wide: SENSOR BUS, INA228, INA3221 0x40 to 0x43, AS5600, PHASE TAP. An entry shows the sensor's name and, under it, what the sensor is doing. A tap on an entry shows its rows on the right, 754px wide.

- An entry with an enable row shows that row alone while it is off, and its state reads `OFF`. ON adds the other rows under it.
- A press selects on its release, within 8px of the press and on the entry. A slide further selects nothing and scrolls nothing.
- Each entry keeps its scroll.
- A change of entry keeps what is unsaved. SAVE and the leaving warning work as on any list.
- RESET names the entry, `RESET INA228`, and sets all that entry's rows to their defaults, the hidden rows too. A flick that lands on it does nothing.
- The knob does nothing on SETUP.

| Entry | State while on |
|---|---|
| SENSOR BUS | `GPIO 16 / 17`; `NO PINS SET`; `NOT ACCEPTED BY THE IOMCU`; on the carrier `GPIO 16 / 17 SOLDERED` |
| INA228, INA3221 0x40 to 0x43 | [What a part is doing](#what-a-part-is-doing) |
| AS5600, PHASE TAP | `ON`; `NOT ACCEPTED BY THE IOMCU` |

## SENSOR BUS and INA228

![The INA228 rows](view:setup-sensors-ina228)

The IOmcu reads a TI INA228 in the ESC's power path and up to four TI INA3221 on the same I2C (Inter-Integrated Circuit) bus.

| Setting | Range | Default | What it does |
|---|---|---|---|
| INA228 | ON, OFF | OFF | the monitor in the ESC's power path |
| INA228 address | 0x40 to 0x4F | 0x45 | the MATEK I2C-INA-BM as shipped |
| INA228 shunt | 50 to 20000uΩ | 200 | the shunt it measures across |
| INA228 max current | 1.0 to 300.0A, 0.1A steps | 204.8 | the current the range is set for |

| Setting | Range | Default | What it does |
|---|---|---|---|
| Sensor SDA | -1, 0, 4, 6, 14, 16, 18, 20, 26 | 16 | the IOmcu GPIO (general-purpose input/output) for SDA; -1 sets no bus |
| Sensor SCL | SDA + 1, or -1 | 17 | follows Sensor SDA; the row has no keys |

- The bus runs at 400kHz; that is not a setting.
- The keypad refuses a Sensor SDA pin the bus cannot have and lists the pins it takes in place of the range: GPIO 5 typed shows `Pins the bus takes: 0 4 6 14 16 18 20 26`.
- The address stops at the ends of its range: `-` at 0x40 and `+` at 0x4F change nothing.
- Pins with nothing wired to them read as the monitors not answering, not as a blocked bus.
- The IOmcu takes a change only while the bench is SAFE.
- With the INA228 enabled and answering, the MOTOR & ESC tile loses its SIMULATED mark and [MOTOR & ESC](motor.md#table) reads its voltage and current.
- With an INA3221 channel watching the servo tester's target, [SERVO](servo.md#the-servo-rail-s-meter) reads the servo rail's current from it.
- A reading at the top of the range is shown as clipped, never as a value: 163.8mV across the INA3221's shunt.
- A monitor that stops answering does not switch the bench to SAFE: nothing trips on the monitors' readings.

![GPIO 5 refused for Sensor SDA](view:setup-sda-why)

A pin is written with the word first, `GPIO 1`, in the keypad's entry, on the row and in the question on leaving SETUP unsaved. The keypad's header gives the range the same way, `GPIO -1 to 47`.

![The keypad of Sensor SDA, GPIO 1 typed](view:setup-sda-keypad)

The band names what goes wrong, one message at a time, at most one every 5s. For example: `Sensor SDA or SCL not set. Check SETUP > SENSORS.`, `INA228 and INA3221 both use 0x40. Check SETUP > SENSORS.`, `INA228 is not responding at 0x45.`, `INA3221 0x40 CH1 clipped at 1.64A. Reading shows only the limit.`. A message about the set-up (pins not set, one address, a value rejected) leaves the band as soon as the setting is right again and the IOmcu holds it.

The INA228 entry's state reads as a part's ([What a part is doing](#what-a-part-is-doing)): `NOT ACCEPTED BY THE IOMCU` also for refused pins and for an IOmcu without the SENSE page, and `SAME ADDRESS AS INA3221` when a fitted part has its address, so neither is read.

## The INA3221 parts

| Setting | Range | Default | What it does |
|---|---|---|---|
| 0x40 fitted to 0x43 fitted | ON, OFF | OFF | an INA3221 at this address; the DAOKAI module takes 0x40 with A0 bridged to GND |

A fitted part shows three rows more for each of its three channels. A part not fitted shows no others.

| Setting | Range | Default | What it does |
|---|---|---|---|
| 0x40 CH1 watches | NONE, OUT 1 to OUT 8, PORT 1 to 4, SUPPLY | OUT 1 for 0x40 CH1, NONE otherwise | what the channel's shunt is in: an output channel, a programming port or the supply rail |
| 0x40 CH1 shunt | OFF, 5.0 to 1000.0mΩ, 0.1mΩ steps | 100.0 | OFF: the channel is not read; 100mΩ reads to 1.64A |
| 0x40 CH1 limit | OFF, 10 to 32767mA, 10mA steps on the arrows | OFF | the band says when a 50ms window's highest current passes it, and the bench switches to SAFE with the supply output off |

- `+` on a shunt at OFF gives 5.0mΩ, `-` at 5.0mΩ gives OFF.
- A tap on a shunt's value opens the keypad: 44.4mΩ from the 100.0 default is `4`, `4`, `.`, `4` and OK, where `-` takes 556 steps. 0 is OFF; 0.1 to 4.9 is refused: the keypad stays open and shows `0 or 5.0 to 1000.0mΩ` in place of the range until the next key.
- The channel read every millisecond, whose current the [SERVO](servo.md#the-servo-rail-s-meter) screen and the servo test use, is the lowest channel with a shunt on a fitted part that watches the servo tester's target: an output while the target is OUTPUTS, PORT n while it is programming port n.
- The panel writes the set-up 500ms after the last edit, only while the bench is SAFE. The limits go 500ms after their last edit in any state, to an IOmcu of 0.22.0 or later.

## On the rbmod carrier

![SENSORS on the carrier, 0x40 soldered](view:setup-sensors-rbmod)

The [rbmod carrier](https://github.com/subtilitas/rcbench/wiki/Carrier) solders Sensor SDA and SCL at GPIO 16 and GPIO 17, Tap pin at GPIO 22, and an INA3221 at 0x40. These rows are drawn without keys and open no keypad, the values stored for the pins stay and are in force again on a module wired by hand, and they say `Soldered on this board. Not a setting here.`:

| Row | Reads | Shunt between |
|---|---|---|
| 0x40 fitted | ON | |
| 0x40 CH1 watches | SERVO RAIL B | the PD mini and servo rail B: PWM03, PWM02 |
| 0x40 CH2 watches | SERVO RAIL A | the PD mini and servo rail A: PWM01, PWM04 |
| 0x40 CH3 watches | PROG PORTS 5V | the 5V of GPIO 21 and the programming ports |

- The shunts and the limits stay settings, with the ranges above.
- 0x41 to 0x43 are free for parts on the sensor bus's connector J5.
- The channel read every millisecond is the rail of the lowest GPIO the servo tester drives as SERVO PWM: CH2 for PWM01 (GPIO 2) or PWM04 (GPIO 1), CH1 for PWM02 (GPIO 4) or PWM03 (GPIO 5). With a programming port as the target it is CH3.
- SERVO names the meter by its rail, `INA RAIL A`; a limit line reads `INA3221 0x40 CH2 (SERVO RAIL A) over its 0.80A limit.`.

## Limits

![A limit on an IOmcu older than 0.22.0](view:setup-sensors-limit)

While a limit is set, the IOmcu judges each channel window of 50ms, and the panel reads which channels passed every 200ms. The band says, at most once every 5s, and once every 30s for a channel that keeps passing. Channels over at the same time take turns: with all 12 over, each is said within 60s.

| Band | Meaning |
|---|---|
| `INA3221 0x41 CH2 (PORT 1) over its 0.80A limit.` | a window passed the channel's limit; in brackets what the channel watches; the limit the window was judged by |
| `An INA3221 channel passed its limit; which one was lost on the link.` | the reply that named the channel was lost; said after the next link-up, not after an IOmcu restart |
| `INA3221 limits rejected. Check SETUP > SENSORS.` | the IOmcu refused the limits and keeps its own, or refused the page |

A limit row the IOmcu does not hold is drawn in the warning colour: while its write waits, after a refusal, and on an IOmcu older than 0.22.0 (protocol 4.27), which judges no limit (`IOmcu before 0.22.0: no limit is judged.`).

![The keypad of 0x40 CH1 shunt, 44.4 typed](view:setup-sensors-keypad)

![3mΩ refused, the reason in place of the range](view:setup-sensors-why)

## What a part is doing

A part's entry says under its name what the part is doing:

| State | Meaning |
|---|---|
| `ONLINE` | the IOmcu reads the part |
| `LOOKING FOR THE PART` | the set-up is taken; the first 2.5s |
| `NOT ANSWERING` | nothing answers at the address |
| `ANOTHER ID ANSWERS` | something else answers there |
| `NOT WRITTEN YET` | an edit waits for its write |
| `NOT READ YET` | no IOmcu answers yet |
| `NOT ACCEPTED BY THE IOMCU` | the IOmcu refused the set-up |
| `EVERY CHANNEL OFF` | all three shunts are OFF; nothing is written |
| `INA228 ON THIS ADDRESS` | the INA228 is on at this address; neither is read |
| `NOT READ, IOMCU < 0.22.0` | an older IOmcu reads one part only |
| `NO BUS PINS` | SENSOR BUS has no pin for SDA or SCL; nothing is read |

![An IOmcu older than 0.22.0](view:setup-sensors-old)

The rows of a part the IOmcu does not read are drawn in the warning colour, with the reason in place of the help line. An IOmcu older than 0.22.0 (protocol 4.25) reads one INA3221: the part of the channel watching the servo tester's target, else the lowest fitted part.

A panel updated from 0.21 takes its first set-up from the one INA3221 it had: the part at the old address fitted, its CH1 on the old shunt and watching OUT 1, or PORT 1 when the servo tester's target was the programming port; every limit OFF. The next SAVE keeps it.

## A row the IOmcu does not hold

![Rows the IOmcu has not taken](view:setup-unheld)

A row whose value is not the one the IOmcu holds has its edge and value in the warning colour, and its help line reads `Not accepted by the IOmcu.`. The mark shows:

- after an edit, until the write is taken: 500ms after the last edit, for the current monitors only once the bench is SAFE;
- on a value the IOmcu refused, until it is changed to one it takes;
- on a part switched ON that the IOmcu holds off: Sensor SDA at -1, the INA228 on the address of a fitted INA3221, or an IOmcu too old for the part.

No row is marked while no IOmcu answers. AS5600 centre is the panel's own value and is never marked.

The mark on Sensor SDA, on AS5600 and on the phase tap's rows also turns the entry's state to `NOT ACCEPTED BY THE IOMCU`.

## The output encoder

| Setting | Range | Default | What it does |
|---|---|---|---|
| AS5600 | ON, OFF | OFF | an AS5600 magnetic angle sensor on the servo's output shaft, at its fixed address 0x36 on the sensor bus |
| AS5600 centre | 0 to 4095 | 0 | the count with the servo at neutral; 4096 counts a turn, 0.0879deg a count |

The encoder needs an IOmcu of 0.15.0 or later. ENC CENTRE on the [servo settings](servo-settings.md#dut) sets the centre from the live reading. The band says when the sensor sees no magnet, `AS5600 detects no magnet. Check the magnet on the servo horn shaft.`, or when the magnet is too weak or too strong. A magnet message is said once while the sensor's magnet status stays the same: an edit of another row does not repeat it. Switched off and on again, the sensor says it again.

## The phase tap

![The phase tap rows](view:setup-tap)

The phase tap hears an ESC's beeps on one motor phase. One IOmcu GPIO reads the phase through a series resistor and a zener clamp. The tap is an input: it drives nothing, and the IOmcu takes a change ARMED or not.

| Setting | Range | Default | What it does |
|---|---|---|---|
| Phase tap | ON, OFF | OFF | the capture runs on the IOmcu |
| Tap pin | 0, 1, 2, 4 to 7, 13 to 22 | 22 | the IOmcu GPIO; GPIO 22 is pad 29. The pins the RP2350-CAN brings out and does not reserve, less the ADC pins GPIO 26 to GPIO 29 |
| Tap lowest tone | 50 to 2000Hz, 10Hz steps | 400 | a lower tone is not a beep |
| Tap highest tone | 100 to 6900Hz, 50Hz steps | 6500 | above the lowest tone |
| Tap pitch split | 0 to 50% | 8 | a pitch change this large starts a new beep; 0 splits on silence only |
| Tap gap | 1 to 100ms | 3 | the silence that ends a beep |
| Tap min periods | 1 to 64 | 3 | the tone periods that make a beep |

The IOmcu refuses an ADC (analog-to-digital converter) pin, a pin bound to an output, and a pin the sensor bus or the PD mini holds. Tap gap has to be at least one period of Tap lowest tone: 3ms at the default 400Hz, whose period is 2.5ms. A rejected value's message leaves the band once the value is set back to one the IOmcu takes. The run page of [ESC STICK](motor-stick.md) shows the beeps.

Vibration and Optical tacho have stored keys and no entry: no screen or IOmcu reads them in this release.

## Related pages

- [Setup](setup.md)
- [Interfaces](setup-interfaces.md)
- [Servo](servo.md)
