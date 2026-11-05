---
screen: none
title: Sensors
order: 62
---

# Sensors

SENSORS on [SETUP](setup.md) configures the IOmcu's sensor bus, INA228, up to four TI INA3221 three-channel current monitors at 0x40 to 0x43, AS5600 output encoder and phase tap. It also configures the panel's Bluetooth IMU (inertial measurement unit). Settings are retained and saved like other SETUP values.

## The left list

![Two parts fitted, 0x40 online](view:setup-sensors)

The 220px left list has 9 entries: SENSOR BUS, INA228, INA3221 0x40 to 0x43, AS5600, PHASE TAP and IMU. Each shows a sensor name and status. Tap an entry to display its settings in the 754px right pane.

- Disabled entries show only their enable row and the status `OFF`. Switching ON reveals the remaining settings.
- Selection occurs on release within the entry and within 8px of the press. A longer slide neither selects nor scrolls.
- Each entry retains its scroll position.
- Switching entries preserves unsaved changes. SAVE and the exit warning work as in other lists.
- RESET names the selected entry, such as `RESET INA228`, and restores all its defaults, including hidden rows. A swipe ending on it has no effect.
- The rotary knob has no effect on SETUP.

| Entry | State while on |
|---|---|
| SENSOR BUS | `GPIO 16 / 17`; `NO PINS SET`; `NOT ACCEPTED BY THE IOMCU`; on the carrier `GPIO 16 / 17 SOLDERED` |
| INA228, INA3221 0x40 to 0x43 | [What a part is doing](#what-a-part-is-doing) |
| AS5600, PHASE TAP | `ON`; `NOT ACCEPTED BY THE IOMCU` |
| IMU | [The IMU](#the-imu) |

## SENSOR BUS and INA228

![The INA228 rows](view:setup-sensors-ina228)

The IOmcu reads a TI INA228 in the ESC's power path and up to four TI INA3221 on the same I2C (Inter-Integrated Circuit) bus.

| Setting | Range | Default | What it does |
|---|---|---|---|
| INA228 | ON, OFF | OFF | the monitor in the ESC's power path |
| INA228 address | 0x40 to 0x4F | 0x45 | the MATEK I2C-INA-BM as shipped |
| INA228 shunt | 50 to 20000uΩ | 200 | the shunt it measures across |
| INA228 max current | 1.0 to 300.0A, 0.1A steps | 204.8 | the current the range is set for |

Shunt resistance in uΩ multiplied by maximum current in A must not exceed 163840, equivalent to 163.84mV across the shunt. Each row shows the compatible limit, such as `Up to 300.0A on 200uΩ (163.84mV)`. Every allowed shunt has a valid current range: up to 546uΩ allows 300.0A, 1000uΩ allows 163.8A, and 20000uΩ allows 8.1A. The sensor resolves 78.125nV steps up to 40.96mV and 312.5nV steps above that.

| Setting | Range | Default | What it does |
|---|---|---|---|
| Sensor SDA | -1, 0, 4, 6, 14, 16, 18, 20, 26 | 16 | the IOmcu GPIO (general-purpose input/output) for SDA; -1 sets no bus |
| Sensor SCL | SDA + 1, or -1 | 17 | follows Sensor SDA; the row has no keys |

- The bus runs at 400kHz; that is not a setting.
- The keypad refuses a Sensor SDA pin the bus cannot have and lists the pins it takes in place of the range: GPIO 5 typed shows `Allowed bus pins: 0 4 6 14 16 18 20 26`.
- The address stops at the ends of its range: `-` at 0x40 and `+` at 0x4F change nothing.
- Pins with nothing wired to them read as the monitors not answering, not as a blocked bus.
- The IOmcu takes a change only while the bench is SAFE.
- With the INA228 enabled and answering, the MOTOR & ESC tile loses its SIMULATED mark and [MOTOR & ESC](motor.md#table) reads its voltage and current.
- With an INA3221 channel watching the servo tester's target, [SERVO](servo.md#the-servo-rail-s-meter) reads the servo rail's current from it.
- A reading at the top of the range is shown as clipped, never as a value: 163.8mV across the INA3221's shunt.
- A monitor that stops answering does not switch the bench to SAFE.

![GPIO 5 refused for Sensor SDA](view:setup-sda-why)

A pin is written with the word first, `GPIO 1`, in the keypad's entry, on the row and in the question on leaving SETUP unsaved. The keypad's header gives the range the same way, `GPIO -1 to 47`.

![The keypad of Sensor SDA, GPIO 1 typed](view:setup-sda-keypad)

The band reports one problem at a time, at most once every 5s. Examples include `Sensor SDA or SCL not set. Check SENSORS.`, `INA228 and INA3221 both use 0x40. Check SENSORS.`, `INA228 is not responding at 0x45.` and `INA3221 0x40 CH1 clipped at 1.64A. Reading shows only the limit.` Configuration messages, such as missing pins, an address conflict or a rejected value, clear when the corrected setting is active on the IOmcu.

The INA228 uses the [part status messages](#what-a-part-is-doing). `NOT ACCEPTED BY THE IOMCU` also covers rejected pins or an IOmcu without a SENSE page. `SAME ADDRESS AS INA3221` means a fitted INA3221 shares its address; neither sensor is read.

## The INA3221 parts

| Setting | Range | Default | What it does |
|---|---|---|---|
| 0x40 fitted to 0x43 fitted | ON, OFF | OFF | an INA3221 at this address; the DAOKAI module takes 0x40 with A0 bridged to GND |

Enabling a fitted part reveals three settings for each of its three channels. A part marked as not fitted hides these rows.

| Setting | Range | Default | What it does |
|---|---|---|---|
| 0x40 CH1 watches | NONE, OUT 1 to OUT 8, PORT 1 to 4, SUPPLY | OUT 1 for 0x40 CH1, NONE otherwise | what the channel's shunt is in: an output channel, a programming port or the supply rail |
| 0x40 CH1 shunt | OFF, 5.0 to 1000.0mΩ, 0.1mΩ steps | 100.0 | OFF: the channel is not read; 100mΩ reads to 1.64A |
| 0x40 CH1 limit | OFF, 10 to 32767mA, 10mA steps on the arrows | OFF | the band says when a 50ms window's highest current passes it, and the bench switches to SAFE with the supply output off |

- `+` changes a shunt from OFF to 5.0mΩ; `-` at 5.0mΩ returns to OFF.
- Tap the shunt value to open its keypad. To change the 100.0mΩ default to 44.4mΩ, enter `4`, `4`, `.`, `4`, then OK; using `-` would require 556 steps. 0 means OFF. Values from 0.1 to 4.9 are refused, leaving the keypad open with `0 or 5.0 to 1000.0mΩ` until the next key.
- The [SERVO](servo.md#the-servo-rail-s-meter) screen and test use the channel sampled every millisecond. This is the lowest channel with an enabled shunt on a fitted part that watches the tester's target: an output for OUTPUTS, or PORT n for programming port n.
- The panel sends configuration changes 500ms after the last edit, only while SAFE. Limits are sent 500ms after their last edit in any state, to IOmcu 0.22.0 or later.

## On the rbmod carrier

![SENSORS on the carrier, 0x40 soldered](view:setup-sensors-rbmod)

On the [rbmod carrier](https://github.com/subtilitas/rcbench/wiki/Carrier), Sensor SDA and SCL are soldered to GPIO 16 and GPIO 17, Tap pin to GPIO 22, and an INA3221 is fitted at 0x40. These rows have no keys or keypad and show `Soldered on this board. Not editable here.` Stored pin settings are preserved and apply again on a manually wired module:

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

For each enabled limit, the IOmcu evaluates the channel's 50ms windows. The panel reads exceedances every 200ms. The band reports at most one every 5s, repeating a persistent exceedance on the same channel every 30s. Simultaneous exceedances are reported in turn; with all 12 channels over their limits, each is named within 60s.

| Band | Meaning |
|---|---|
| `INA3221 0x41 CH2 (PORT 1) over its 0.80A limit.` | a window passed the channel's limit; in brackets what the channel watches; the limit the window was judged by |
| `An INA3221 channel exceeded its limit. Its channel number was lost on the link.` | the reply that named the channel was lost; said after the next link-up, not after an IOmcu restart |
| `INA3221 limits rejected. Check SENSORS.` | the IOmcu refused the limits and keeps its own, or refused the page |

A limit row uses the warning colour until the IOmcu has applied its value: while a write is pending, after rejection, or with an IOmcu older than 0.22.0 (protocol 4.27). Those older versions check no limits and show `IOmcu before 0.22.0: no limit is checked.`.

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
| `INA228 AT THIS ADDRESS` | the INA228 is on at this address; neither is read |
| `NOT READ, IOMCU < 0.22.0` | an older IOmcu reads one part only |
| `NO BUS PINS` | SENSOR BUS has no pin for SDA or SCL; nothing is read |

![An IOmcu older than 0.22.0](view:setup-sensors-old)

Rows for a part the IOmcu does not read use the warning colour, with the reason replacing the help text. An IOmcu older than 0.22.0 (protocol 4.25) reads only one INA3221: the part watching the servo tester's target, or otherwise the lowest fitted part.

A panel updated from 0.21 takes its first set-up from the one INA3221 it had: the part at the old address fitted, its CH1 on the old shunt and watching OUT 1, or PORT 1 when the servo tester's target was the programming port; every limit OFF. The next SAVE keeps it.

## A row the IOmcu does not hold

![Rows the IOmcu has not taken](view:setup-unheld)

A setting that differs from the value active on the IOmcu has a warning-coloured edge and value. Its help line reads `Not accepted by the IOmcu.` This applies:

- after an edit, until the write is taken: 500ms after the last edit, for the current monitors only once the bench is SAFE;
- on a value the IOmcu refused, until it is changed to one it takes;
- on a part switched ON that the IOmcu holds off: Sensor SDA at -1, the INA228 on the address of a fitted INA3221, or an IOmcu too old for the part.

No row is marked while no IOmcu answers. AS5600 centre is the panel's own value and is never marked.

The mark on Sensor SDA, on AS5600 and on the phase tap's rows also turns the entry's state to `NOT ACCEPTED BY THE IOMCU`.

## The output encoder

| Setting | Range | Default | What it does |
|---|---|---|---|
| AS5600 | ON, OFF | OFF | an AS5600 magnetic angle sensor on the servo's output shaft, at its fixed address 0x36 on the sensor bus |
| AS5600 centre | 0 to 4095 | 0 | the count with the servo at neutral; 4096 counts a turn, 0.0879° a count |

The encoder requires IOmcu 0.15.0 or later. ENC CENTRE in [servo settings](servo-settings.md#dut) takes the live reading as centre. The band reports a missing magnet with `AS5600 detects no magnet. Check the magnet on the servo horn shaft.`, and also reports weak or strong magnets. Each message appears once while the magnet status is unchanged. Editing another setting does not repeat it; switching the sensor off and on does.

## The phase tap

![The phase tap rows](view:setup-tap)

The phase tap detects ESC beeps on one motor phase. An IOmcu GPIO reads the phase through a series resistor and zener clamp. It is an input and drives no output. The IOmcu accepts changes both while SAFE and ARMED.

| Setting | Range | Default | What it does |
|---|---|---|---|
| Phase tap | ON, OFF | OFF | the capture runs on the IOmcu |
| Tap pin | 0, 1, 2, 4 to 7, 13 to 22 | 22 | the IOmcu GPIO; GPIO 22 is pad 29. The pins the RP2350-CAN brings out and does not reserve, less the ADC pins GPIO 26 to GPIO 29 |
| Tap lowest tone | 50 to 2000Hz, 10Hz steps | 400 | a lower tone is not a beep |
| Tap highest tone | 100 to 6900Hz, 50Hz steps | 6500 | above the lowest tone |
| Tap pitch split | 0 to 50% | 8 | a pitch change this large starts a new beep; 0 splits on silence only |
| Tap gap | 1 to 100ms | 3 | the silence that ends a beep |
| Tap min periods | 1 to 64 | 3 | the tone periods that make a beep |

The IOmcu rejects ADC (analog-to-digital converter) pins, output-bound pins, and pins used by the sensor bus or PD mini. Tap gap must cover at least one period of Tap lowest tone: at the default 400Hz, a 2.5ms period requires 3ms. A rejected-value message clears once a valid value is applied. The [ESC STICK](motor-stick.md) run page shows the detected beeps.

Vibration and Optical tacho have stored keys and no entry: no screen or IOmcu reads them in this release.

## The IMU

![SENSORS on the IMU entry, connected](view:setup-sensors-imu)

The WitMotion BWT901CL connects through an HC-05 Bluetooth module on the panel's UART1. Use the [IMU](imu.md) tile on SETUP to scan, bind and calibrate it. This entry contains its settings.

| Setting | Range | Default | What it does |
|---|---|---|---|
| IMU | ON, OFF | OFF | the HC-05's set-up and the sensor's reading |
| IMU axis | X, Y, Z | X | the angle the servo runs and RUDDER read; `Z: 1° WITH THE MAGNETOMETER` |
| IMU sign | +, - | + | `-` turns that angle round |
| IMU rate | 10Hz, 20Hz, 50Hz, 100Hz, 200Hz | 50Hz | the sensor's frames a second; it takes effect after the sensor is switched off and on |
| IMU algorithm | 6-AXIS, 9-AXIS | 6-AXIS | 9-AXIS takes the magnetometer for Z |
| Angle source | AS5600, IMU | AS5600 | where the servo runs take their angle from ([Servo settings](servo-settings.md)) |

RATE and ALGORITHM changes are sent with the sensor's SAVE command (`FF AA 00 00 00`). For a connected sensor, the rate message is `RATE SAVED: SWITCH THE SENSOR OFF AND ON`. If disconnected, the panel shows `RATE IS SENT WHEN THE SENSOR CONNECTS; THEN SWITCH IT OFF AND ON` and waits for frames. During IMU calibration it shows `RATE IS SENT AFTER CALIBRATION; THEN SWITCH THE SENSOR OFF AND ON` and defers the change until after CALSW 0 and SAVE.

| State | When |
|---|---|
| `OFF` | IMU OFF |
| `NO HC-05` | the HC-05 answers `AT` at none of 115200, 38400 and 9600 baud; the panel asks again every 10s |
| `NO SENSOR BOUND` | the HC-05 answers; no sensor is bound |
| `SCANNING`, `BINDING` | a scan or a bind runs |
| `BOUND` | bound; no angle frame since |
| `CONNECTED 50Hz` | angle frames within the last 300ms, at that rate |
| `NO FRAMES` | bound, frames came, none in the last 300ms |

## Related pages

- [Setup](setup.md)
- [Supply](supply.md): the PD mini's wiring, on the PD MINI tab.
- [Servo](servo.md)
- [IMU](imu.md)
