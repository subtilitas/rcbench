---
screen: OUTPUTS
title: Outputs
order: 62
---

# Outputs

OUTPUTS binds the IOmcu's pins to output protocols: which pins drive servos, which drive ESCs (electronic speed controllers), and with what signal. It opens from OUTPUTS on [SETUP](setup.md) on the [overview](#the-overview-off): OFF selected and every bound pin named by its protocol. From TARGET on [MOTOR & ESC](motor.md#target) it opens on the protocol the throttle's pins are bound as (the overview while they are bound as several or none is known), and from TARGET on [SERVO](servo-settings.md#the-target-row) on what that row shows: the [PROG PORT entry](#the-prog-port-entry) while the IOmcu drives the port, SERVO PWM while it drives the outputs, the overview while the target is not known. The home tag goes back to the screen it opened from; over the open protocol list it closes the list first, choosing nothing. A board with a drawing or a shape shows [its keys](#the-board-and-its-keys): the list answers which GPIO (general-purpose input/output) is bound, the board where to put the lead. A board with neither, and the rbmod carrier, show [a grid of pins](#the-grid-view).

## The board and its keys

![Three pins bound as SERVO PWM on the board](view:outputs)

A pad is under 40px across, smaller than a fingertip. So each pin has a key, 50 x 56px, on a straight trace to its pad: two staggered rows above the board and two below.

| Key | Pin | A tap |
|---|---|---|
| ivory or ebony | free | binds it to the selected protocol |
| accent colour | bound in the selected protocol | gives it back |
| grey | held by another protocol or the programming port, the protocol is full, or the binding is not confirmed | changes nothing |
| no key, red cross on the pad | reserved: GPIO 3 heartbeat, GPIO 8 to GPIO 12 CAN (Controller Area Network) | none |

A tap on a key writes the binding at once; [LAST WRITE](#last-write) says what became of it.

Current limitation: on a board with a pin in the top-left corner of its outline, the keys of the upper rows lie under the protocol list, the count and the reason. A press on the part of such a key over the protocol list opens the list instead of taking the pin. The RP2350-CAN has no pin there.

The board also marks the other wires of a servo lead:

| Mark | Pad |
|---|---|
| white round marker with **G** | ground |
| amber round marker with **5V0** or **3V3** | a rail at that voltage |
| amber round marker with **PWR** | VSYS, a rail that is an input |
| dim cross on the pad | neither, such as RUN and 3V3_EN |

## Binding pins

1. Pick a protocol in the list. Until then a tap on a key does nothing.
2. Tap a key. The tap binds the pin to the protocol at once; there is no APPLY key.
3. Tap an accented key to give its pin back.
4. Pick OFF to go back to the overview.

A pin belongs to one protocol at most. Several protocols can be bound at once.

Each bound pin is one slot of the IOmcu's OUTPUTS page, in pin order across every protocol. The lowest bound pin is channel 0. There are 8 slots and 8 channels, shared by all protocols.

## The protocols

![The protocol list](view:outputs-protocol)

| Protocol | Signal | Pins |
|---|---|---|
| OFF | binds nothing; shows the whole binding | 0 |
| SERVO PWM | PWM (pulse-width modulation) at 50Hz, for a servo or a control surface | up to 8 |
| MOTOR PWM | the same pulse at 50Hz, for an ESC; from Idle pulse to Full pulse | up to 8 |
| PPM | PPM (pulse-position modulation): 8 channels on one pin at 40Hz | 1 |
| DSHOT300, DSHOT600 | DShot digital throttle at 300 or 600kbit/s | up to 8 |
| DSHOT300 BIDIR, DSHOT600 BIDIR | DShot with rpm telemetry from the ESC on the same wire | up to 8 |

A servo channel centres when nothing commands it; a motor channel stops. The pulse cannot say which is on the pin, so the protocol says it. The MOTOR & ESC throttle drives every channel bound as MOTOR PWM or a DShot entry, and leaves the servo channels alone. PPM needs 8 channels, so a bench with PPM bound has room for nothing else.

![The longest protocol names](view:outputs-bidir)

Picking a protocol writes nothing. It chooses which keys show accented and which protocol the next tap joins. The pins of the other protocols stay bound.

## Idle pulse and Full pulse

A MOTOR PWM channel sends 0% throttle as Idle pulse and 100% as Full pulse, 1000us and 2000us by default. Both are on [MOTOR & ESC, SETTINGS](motor-settings.md), in steps of 10us: Idle pulse 800 to 1600us, Full pulse 1400 to 2400us. A SERVO PWM channel keeps 1000 to 2000us, or the range SERVO sends. A change reaches the IOmcu 300ms after the last edit while the bench is SAFE. An Idle pulse that is not 100us or more below Full pulse is not sent, and the band says `Idle pulse must be 100us or more below Full pulse. Not sent.`.

An ESC calibrated on a transmitter takes that transmitter's shortest pulse as zero. Set Idle pulse at or below that pulse and Full pulse at or above the longest one: for a transmitter that sends 985 to 2012us, 980us and 2020us.

## The protocol list and the counts

![The protocol list over the board](view:outputs-protocol)

Top left is the protocol list, A pick selects the protocol a tap on a key joins and writes nothing. A tap outside the open list closes it.

Under the list, `4 OF 8 SLOTS IN USE` counts the pins over every protocol. Under that, in amber, stands the reason no key can be added:

| Line | When |
|---|---|
| `ALL 8 SLOTS IN USE` | 8 pins are bound over all protocols |
| `NEEDS 8 CHANNELS, 4 FREE` | PPM, with 4 servo pins bound |
| `SERVO PWM TAKES 8 PINS` | the selected protocol holds as many pins as it takes |
| `CHOOSE A PROTOCOL, THEN TAP PINS` | OFF is selected |
| `NOT READ` | the binding is not confirmed; every key is grey |
| `NO VALID ASSIGNMENT IN THE PAGES` | the pages describe no binding; every key is grey |
| `GPIO 28: SHARES PWM SLICE WITH PROG PORT GPIO 13` | a tap on GPIO 28 under SERVO PWM or MOTOR PWM while the programming port holds GPIO 13: one PWM slice; the key is grey |
| `GPIO 16: SAME PWM CHANNEL AS GPIO 0` | a tap on GPIO 16 under SERVO PWM or MOTOR PWM with GPIO 0 bound as PWM: one PWM compare register; the key is grey |

![Eight of eight slots in use](view:outputs-full)

## PINS and IN USE

PINS, down the left, lists the selected protocol's pins in channel order with the pad number printed on the board. IN USE, down the right, lists the pins other protocols hold, each group under its protocol's name. With OFF selected PINS is not shown, because OFF holds no pin, and IN USE lists every bound pin.

![GPIO 13 held by the programming port](view:outputs-prog)

The programming port's pin, GPIO 13 (pad 17) by default, is grey while no protocol holds it, and IN USE lists it first under `PROG PORT`. Each further port the IOmcu reports holds its pin the same way, listed under the same name.

![The interfaces' pins held and named under IN USE](view:outputs-iface)

A pin [SETUP > INTERFACES](setup-interfaces.md) holds is grey the same way, and IN USE lists it after the programming port under `SENSOR BUS`, `PD MINI` or `PHASE TAP`. A table with more lines than fit ends in `+N`, N the lines left out.

![OFF selected: every bound pin under IN USE](view:outputs-off)

## The PROG PORT entry

![The PROG PORT entry](view:outputs-port)

The open protocol list shows one PROG PORT entry per programming port the IOmcu reports, in a second column beside the protocols: 4 from IOmcu 0.22.0, 1 on an older build, and one for port 1 while the count is not known. An IOmcu older than 0.17.0 has no programming port and no entry. A programming port is an output of its own kind, no protocol and no slot. Every entry has the same rows, in the column left of the board:

| Row | Shows and does |
|---|---|
| PIN | `PIN GPIO 13` with its pad, `SET A PIN`, `PIN GPIO 14 REFUSED` or `NO PIN KNOWN`. The keys on the board set it: a free key asks the IOmcu for that pin, the port's own accented key asks for none |
| FUNCTION | what the pin carries while no programming session holds it; a tap on its face opens the list over the column. SERVO PWM is the servo pulse on a PWM pin; NONE leaves the pin an input with its pull-down, on any free pin; DSHOT150, DSHOT300 and DSHOT600 (IOmcu 0.22.0 or later) send DShot from MOTOR & ESC's throttle, a frame every 1ms while ARMED for the throttle, on any free pin |
| WATCHED BY | the INA3221 channel that watches the port in SETUP > SENSORS, `WATCHED BY INA3221 0x41 CH2`; `WATCHED BY NO CURRENT CHANNEL` while none does |
| SERVO TESTER | OUTPUTS or PORT n: what the [SERVO](servo.md) tester drives. SERVO's TARGET row opens OUTPUTS here while the IOmcu drives the port |
| PORT PWM | ON or OFF: the pulse of the port that is the tester's target |

The IOmcu keeps each port's pin and function in its flash, with the outputs' binding: they belong to the board. Port 1 starts as SERVO PWM on GPIO 13, ports 2 to 4 as NONE with no pin. A FUNCTION tap and a key tapped are asks: the panel writes them on a bench at rest and the amber line says how they stand.

![PROG PORT 2 with its ask waiting](view:outputs-port2)

| Line under the rows | Condition |
|---|---|
| `GPIO 5: SHARES PWM SLICE WITH OUTPUT GPIO 4` | a key tapped on the PWM slice of a bound PWM output, on a SERVO PWM port; the key is grey |
| `GPIO 28: SAME PWM CHANNEL AS GPIO 12` | a key tapped on the compare register of another SERVO PWM port; the key is grey |
| `LOCKED: THE SERVO PROGRAMMER HOLDS THE PORT` | the [servo programmer](servo-programmer.md) has a port |
| `WAITS FOR SAFE, NO TEST, SWEEP OR SERVO PROGRAMMER` | an ask for this port waits for a bench at rest |
| `THE IOMCU REFUSED IT` | the IOmcu refused the last ask, or did not answer it |
| `LINK LOST BEFORE THE WRITE` | the link went down before the ask was written |
| `PORTS NOT READ YET` | the IOmcu's ports not read since the link came up |
| `LOCKED WHILE ARMED OR A TEST OR SWEEP RUNS` | the bench is ARMED, or a sweep or a test runs; PORT PWM still goes off |
| `SERVO TESTER DRIVES PROG PORT 3` | the tester drives another port |
| `THE SERVO TESTER NEEDS FUNCTION SERVO PWM` | the port is NONE or DShot |
| `IOMCU OLDER THAN 0.22.0: SERVO PWM ONLY` | the IOmcu has no FUNCTION |
| `PORT PWM NEEDS PROG PORT AS THE TESTER'S TARGET` | PORT PWM on a port that is not the tester's target |

Picking the entry writes nothing to the IOmcu, and LAST WRITE is not shown on it. The tester's two settings are kept on the panel and reach the IOmcu on a bench at rest. With more than one port the entries read `PROG PORT 1` to `PROG PORT 4`.

Another port's pin, a protocol's and an interface's stay grey. A NONE port takes any free key. Before the IOmcu's ports are read every key is grey.

![PROG PORT 3, a port without a pin](view:outputs-port3)

## The grid view

A board that reports neither a drawing nor a shape cannot be drawn: a guessed picture would point at the wrong pad as confidently as the right one. OUTPUTS then shows its pins as a grid, 4 columns of 7 cells, and the protocol list, the count `3 OF 8 PINS` and LAST WRITE in the left column. A tap on a cell ticks it. The [rbmod carrier](#on-the-rbmod-carrier) has the same grid. No cat is drawn on it. A PROG PORT entry's PIN row takes no tap there: only the keys on a drawn board set a port's pin.

![Three pins on a board with no shape](view:outputs-grid)

| Cell | Meaning | A tap |
|---|---|---|
| ticked, `PAD 6` | bound in the selected protocol; the pad number printed on the board | gives it back |
| greyed, the other protocol's name | held by another protocol | does nothing |
| red, struck through | reserved: GPIO 3 carries the safety heartbeat, GPIO 8 to GPIO 12 the CAN (Controller Area Network) controller | does nothing |
| `PROG PORT` | the pin of the IOmcu's programming port, GPIO 13 by default | does nothing |
| red, `SENSOR BUS`, `PD MINI` or `PHASE TAP` | a pin [SETUP > INTERFACES](setup-interfaces.md) holds: Sensor SDA and SCL while the INA228 or AS5600 is ON or an INA3221 is fitted on [SETUP > SENSORS](setup-sensors.md); PD mini TX and RX while the PD mini is ON with both pins set; the tap pin while the phase tap is ON. The cell follows those settings | does nothing |
| greyed, `SLICE OF GPIO 13` | under SERVO PWM and MOTOR PWM: the pin shares a PWM slice with a programming port's pin, any port the IOmcu reports; GPIO 28 and GPIO 13 are slice 6 | does nothing; `GPIO 28: SHARES PWM SLICE WITH PROG PORT GPIO 13` under the count |
| greyed, `PWM CH OF GPIO 0` | under SERVO PWM and MOTOR PWM: the pin shares a PWM compare register with a pin bound as PWM (GPIO 0 and GPIO 16, GPIO 1 and GPIO 17, GPIO 2 and GPIO 18, GPIO 4 and GPIO 20, GPIO 5 and GPIO 21, GPIO 6 and GPIO 22) | does nothing; `GPIO 16: SAME PWM CHANNEL AS GPIO 0` under the count |
| free, `PAD 7` | not bound | binds it |

When the selected protocol can take no more pins, the reason stands under the count in amber: `NEEDS 8 CHANNELS, 4 FREE`, `ALL 8 SLOTS IN USE` or `SERVO PWM TAKES 8 PINS`.

A stored binding that holds GPIO 13 keeps it: the programming port then has no pin, and the [servo programmer](servo-programmer.md) says so on CONNECT.

## On the rbmod carrier

![PWM01 to PWM04, PWM03 refused for slice 2](view:outputs-rbmod)

The [rbmod carrier](https://github.com/subtilitas/rcbench/wiki/Carrier) solders every lead to its pin, so no pin is chosen here. The grid shows the four servo outputs, PWM01 to PWM04 on GPIO 2, 4, 5 and 1, each with its GPIO under the name. A tap gives the selected protocol to the output, as a tick does on a pin. Slot n is PWM0(n+1): PWM01 is channel 0. A protocol takes at most the 4 outputs, `SERVO PWM TAKES 4 PINS`.

PWM02 and PWM03 are one PWM slice and run at one frame rate. A surface protocol (SERVO PWM) and a throttle protocol (MOTOR PWM) are not offered on the two together: the cell reads `SLICE OF PWM02` and a tap on it `PWM03 GPIO 5: SAME PWM SLICE AS PWM02 GPIO 4`. DShot runs on its own state machine and is offered beside either.

![PROG PORT 1 on the carrier](view:outputs-rbmod-port)

The PROG PORT entries name each port's pin, `PROGP01 ON GPIO 13`, and the PIN row takes no tap. FUNCTION reads PROGRAMMING and takes no tap: a programming port carries the KST and Hitec programmers and no signal protocol. SERVO TESTER and PORT PWM are not shown. The line under the rows reads `KST AND HITEC PROGRAMMERS ONLY. SIGNALS: PWM01 TO PWM04`.

## The overview: OFF

![OFF shows the protocol of every bound pin](view:outputs-off)

Every visit from SETUP opens with OFF selected. OFF binds nothing and unbinds nothing. IN USE lists every bound pin under its protocol, so the whole binding and every pin it drives are on one screen. A tap on a key does nothing, and the line under the list reads `CHOOSE A PROTOCOL, THEN TAP PINS`. Opening the screen writes nothing to the IOmcu. OFF stays selected until a protocol is picked, also across leaving the screen. A change of board, such as an IOmcu that answers during the visit or one swapped for another, selects OFF whatever was selected, so a protocol picked before is not carried to it. Pick a protocol to edit its pins; pick OFF to come back here.

## LAST WRITE

Every tap on a key is written at once. LAST WRITE stands right of the protocol list, and at the foot of the left column in the grid view; a PROG PORT entry's asks report on its own line instead. The panel then reads the IOmcu's pages back and shows what it holds.

| LAST WRITE | Meaning |
|---|---|
| `NOT WRITTEN` | nothing written since the panel started |
| `WRITTEN` | the IOmcu acknowledged the write |
| `NO LINK` | an exchange got no answer within 1000ms, or the link is down |
| `REFUSED` | the IOmcu refused: a pin it cannot bind, a change while ARMED, or a frame rate it could not put back |

An IOmcu of 0.15.0 or later takes the binding whole: `NO LINK` and `REFUSED` leave the binding in force as it was.

## The board drawing

The IOmcu carries a drawing of itself: outline, parts and every pad, to scale in 0.01mm. The panel fetches it over the link, keeps it in flash and shows it only when it matches the board's shape and pin catalogue.

![The board drawn from the shape alone](view:outputs-shape)

Without a drawing, or while the transfer is not finished, the outline and pads are drawn from the board's shape, and the keys are in the same places. A board that does not report its shape is not drawn: OUTPUTS shows [the grid view](#the-grid-view).

The drawing of the RP2350-CAN is 1,560 bytes, fetched in 26 blocks of 62 bytes. The time this takes is not measured. Writing it to flash waits for a bench that is SAFE with the supply off.

## A binding that is not confirmed

![The last binding read, after a read that failed](view:outputs-unread)

The screen edits a binding only after reading it. While a page did not read, the screen keeps the last binding read, every key grey, with `NOT READ` (`ASSIGNMENT NOT READ. EDITING LOCKED.` in the grid view). A tap on a key does nothing. The panel reads again at every link-up and every 500ms.

![Pages no binding describes](view:outputs-odd)

When the pages read but describe no binding the screen knows, for example one pin in two slots, it reads `NO VALID ASSIGNMENT IN THE PAGES` and offers `HOLD 2s TO CLEAR ALL PIN ASSIGNMENTS` under IN USE. Held, it writes a binding with no pin, and edits are taken again. A finger that leaves the key, a lift before 2s or leaving the screen abandons the hold.

## The IOmcu keeps the binding

The IOmcu stores the binding in its own flash and restores it at boot; the panel never stores one. A restored binding claims its pins and holds them at rest until the bench is switched to ARMED. A power cut during a save leaves the binding from before it.

## Related pages

- [Setup](setup.md)
- [Motor and ESC](motor.md#target): the pins the throttle drives.
- [Servo settings](servo-settings.md#the-target-row): the pins the servo tester drives.
