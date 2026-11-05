---
screen: OUTPUTS
title: Outputs
order: 62
---

# Outputs

OUTPUTS assigns IOmcu pins to output protocols, defining which pins drive servos or ESCs (electronic speed controllers) and which signal they use. From [SETUP](setup.md), it opens the [overview](#the-overview-off), with OFF selected and every assigned pin labelled by protocol.

From TARGET on [MOTOR & ESC](motor.md#target), it opens the throttle protocol, or the overview for mixed or unknown protocols. From [SERVO](servo-settings.md#the-target-row), it follows TARGET: [PROG PORT](#the-prog-port-entry) for a programming port, SERVO PWM for outputs, or the overview for an unknown target. The home tag returns to the originating screen. If the protocol list is open, home first closes it without selecting anything.

A board with a drawing or shape shows [pin keys](#the-board-and-its-keys). The list identifies each GPIO (general-purpose input/output); the drawing shows where to connect the lead. Boards without either description, and the rbmod carrier, use a [pin grid](#the-grid-view).

## The board and its keys

![Three pins bound as SERVO PWM on the board](view:outputs)

Pads are less than 40px across, smaller than a fingertip. Each therefore has a 50 x 56px key connected to its pad by a straight line. Keys form two staggered rows above the board and two below.

| Key | Pin | A tap |
|---|---|---|
| ivory or ebony | free | binds it to the selected protocol |
| accent colour | bound in the selected protocol | gives it back |
| grey | held by another protocol or the programming port, the protocol is full, or the binding is not confirmed | changes nothing |
| no key, red cross on the pad | reserved: GPIO 3 heartbeat, GPIO 8 to GPIO 12 CAN (Controller Area Network) | none |

A tap on a key writes the binding at once; [LAST WRITE](#last-write) says what became of it.

On a board with a pad near the upper-left outline, the upper keys can overlap the protocol list, count and reason. Pressing an overlapping area opens the list instead of selecting the pin. The RP2350-CAN has no pad in this position.

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

Without a command, a servo channel centres and a motor channel stops. The selected protocol determines this behavior because the pulse alone cannot identify the connected device. MOTOR & ESC drives every MOTOR PWM and DShot throttle channel while leaving servo channels unchanged. PPM requires all 8 channels, leaving no room for other protocols.

![The longest protocol names](view:outputs-bidir)

Selecting a protocol does not write anything. It highlights that protocol's assigned pins and sets the protocol for the next pin you tap. Other assignments remain unchanged.

## Idle pulse and Full pulse

A MOTOR PWM channel sends 0% throttle as Idle pulse and 100% as Full pulse, 1000us and 2000us by default. Both are on [MOTOR & ESC, SETTINGS](motor-settings.md), in steps of 10us: Idle pulse 800 to 1600us, Full pulse 1400 to 2400us. A SERVO PWM channel keeps 1000 to 2000us, or the range SERVO sends. A change reaches the IOmcu 300ms after the last edit while the bench is SAFE. An Idle pulse that is not 100us or more below Full pulse is not sent, and the band says `Idle pulse must be at least 100us below Full pulse. Not sent.`.

An ESC calibrated on a transmitter takes that transmitter's shortest pulse as zero. Set Idle pulse at or below that pulse and Full pulse at or above the longest one: for a transmitter that sends 985 to 2012us, 980us and 2020us.

## The protocol list and the counts

![The protocol list over the board](view:outputs-protocol)

Top left is the protocol list. A pick selects the protocol a tap on a key joins and writes nothing. A tap outside the open list closes it.

Under the list, `4 OF 8 SLOTS IN USE` counts the pins over every protocol. Under that, in amber, stands the reason no key can be added:

| Line | When |
|---|---|
| `ALL 8 SLOTS IN USE` | 8 pins are bound over all protocols |
| `NEEDS 8 CHANNELS, 4 FREE` | PPM, with 4 servo pins bound |
| `SERVO PWM USES 8 PINS` | the selected protocol holds as many pins as it takes |
| `CHOOSE A PROTOCOL, THEN TAP PINS` | OFF is selected |
| `NOT READ` | the binding is not confirmed; every key is grey |
| `NO VALID ASSIGNMENT IN THE PAGES` | the pages describe no binding; every key is grey |
| `GPIO 28: SHARES PWM SLICE WITH PROG PORT GPIO 13` | a tap on GPIO 28 under SERVO PWM or MOTOR PWM while the programming port holds GPIO 13: one PWM slice; the key is grey |
| `GPIO 16: SAME PWM CHANNEL AS GPIO 0` | a tap on GPIO 16 under SERVO PWM or MOTOR PWM with GPIO 0 bound as PWM: one PWM compare register; the key is grey |

![Eight of eight slots in use](view:outputs-full)

## PINS and IN USE

PINS on the left lists the selected protocol's pins in channel order, with the pad numbers printed on the board. IN USE on the right lists other assigned pins, grouped by protocol. With OFF selected, PINS is hidden and IN USE lists every assigned pin.

![GPIO 13 held by the programming port](view:outputs-prog)

The programming port's pin, GPIO 13 (pad 17) by default, is grey while no protocol holds it, and IN USE lists it first under `PROG PORT`. Each further port the IOmcu reports holds its pin the same way, listed under the same name.

![The interfaces' pins held and named under IN USE](view:outputs-iface)

A pin [SETUP > SENSORS](setup-sensors.md) or [SUPPLY's PD MINI tab](supply.md#pd-mini-tab) holds is grey the same way, and IN USE lists it after the programming port under `SENSOR BUS`, `PD MINI` or `PHASE TAP`. A table with more lines than fit ends in `+N`, N the lines left out.

![OFF selected: every bound pin under IN USE](view:outputs-off)

## The PROG PORT entry

![The PROG PORT entry](view:outputs-port)

The open protocol list has a second column with one PROG PORT entry for each reported programming port: 4 from IOmcu 0.22.0, 1 on older builds, or a provisional port 1 entry while the count is unknown. Before IOmcu 0.17.0 there is no programming port or entry. A programming port is a separate output type; it consumes neither a protocol entry nor a slot. Each entry shows the following rows to the left of the board:

| Row | Shows and does |
|---|---|
| PIN | `PIN GPIO 13` with its pad, `SET A PIN`, `PIN GPIO 14 REFUSED` or `NO PIN KNOWN`. The keys on the board set it: a free key asks the IOmcu for that pin, the port's own accented key asks for none |
| FUNCTION | what the pin carries while no programming session holds it; a tap on its face opens the list over the column. SERVO PWM is the servo pulse on a PWM pin; NONE leaves the pin an input with its pull-down, on any free pin; DSHOT150, DSHOT300 and DSHOT600 (IOmcu 0.22.0 or later) send DShot from MOTOR & ESC's throttle, a frame every 1ms while ARMED for the throttle, on any free pin |
| WATCHED BY | the INA3221 channel that watches the port in SETUP > SENSORS, `WATCHED BY INA3221 0x41 CH2`; `WATCHED BY NO CURRENT CHANNEL` while none does |
| SERVO TESTER | OUTPUTS or PORT n: what the [SERVO](servo.md) tester drives. SERVO's TARGET row opens OUTPUTS here while the IOmcu drives the port |
| PORT PWM | ON or OFF: the pulse of the port that is the tester's target |

The IOmcu stores each port's pin and function in flash alongside the output bindings, so they stay with the board. Port 1 defaults to SERVO PWM on GPIO 13; ports 2 to 4 default to NONE with no pin. Selecting a FUNCTION or pin requests a change. The panel writes it when the bench is at rest, and the amber line reports its status.

![PROG PORT 2 with its ask waiting](view:outputs-port2)

| Line under the rows | Condition |
|---|---|
| `GPIO 5: SHARES PWM SLICE WITH OUTPUT GPIO 4` | a key tapped on the PWM slice of a bound PWM output, on a SERVO PWM port; the key is grey |
| `GPIO 28: SAME PWM CHANNEL AS GPIO 12` | a key tapped on the compare register of another SERVO PWM port; the key is grey |
| `LOCKED: SERVO PROGRAMMING USES THE PORT` | the [servo programmer](servo-programmer.md) has a port |
| `WAITING FOR SAFE, NO TEST, SWEEP OR SERVO PROGRAMMING` | an ask for this port waits for a bench at rest |
| `REFUSED BY THE IOMCU` | the IOmcu refused the last ask, or did not answer it |
| `LINK LOST BEFORE THE WRITE` | the link went down before the ask was written |
| `PORTS NOT READ YET` | the IOmcu's ports not read since the link came up |
| `LOCKED WHILE ARMED OR A TEST OR SWEEP RUNS` | the bench is ARMED, or a sweep or a test runs; PORT PWM still goes off |
| `SERVO TESTER DRIVES PROG PORT 3` | the tester drives another port |
| `THE SERVO TESTER REQUIRES FUNCTION SERVO PWM` | the port is NONE or DShot |
| `IOMCU OLDER THAN 0.22.0: SERVO PWM ONLY` | the IOmcu has no FUNCTION |
| `PORT PWM NEEDS PROG PORT AS THE TESTER'S TARGET` | PORT PWM on a port that is not the tester's target |

Opening a port entry writes nothing, and the entry does not show LAST WRITE. The two tester settings are stored on the panel and sent to the IOmcu when the bench is at rest. When multiple ports are available, entries are named `PROG PORT 1` to `PROG PORT 4`.

Another port's pin, a protocol's and an interface's stay grey. A NONE port takes any free key. Before the IOmcu's ports are read every key is grey.

![PROG PORT 3, a port without a pin](view:outputs-port3)

## The grid view

Without a board drawing or shape, OUTPUTS uses a grid of 4 columns and 7 rows instead of guessing pad locations. The left column contains the protocol list, count such as `3 OF 8 PINS`, and LAST WRITE. Tap a free cell to assign it. The [rbmod carrier](#on-the-rbmod-carrier) uses the same grid, without the cat drawing. A PROG PORT entry's PIN row cannot be tapped here: only keys on a drawn board can change a port pin.

![Three pins on a board with no shape](view:outputs-grid)

| Cell | Meaning | A tap |
|---|---|---|
| ticked, `PAD 6` | bound in the selected protocol; the pad number printed on the board | gives it back |
| greyed, the other protocol's name | held by another protocol | does nothing |
| red, struck through | reserved: GPIO 3 carries the safety heartbeat, GPIO 8 to GPIO 12 the CAN (Controller Area Network) controller | does nothing |
| `PROG PORT` | the pin of the IOmcu's programming port, GPIO 13 by default | does nothing |
| red, `SENSOR BUS`, `PD MINI` or `PHASE TAP` | a pin [SETUP > SENSORS](setup-sensors.md) or [SUPPLY's PD MINI tab](supply.md#pd-mini-tab) holds: Sensor SDA and SCL while the INA228 or AS5600 is ON or an INA3221 is fitted on [SETUP > SENSORS](setup-sensors.md); PD mini TX and RX while the PD mini is ON with both pins set; the tap pin while the phase tap is ON. The cell follows those settings | does nothing |
| greyed, `SLICE OF GPIO 13` | under SERVO PWM and MOTOR PWM: the pin shares a PWM slice with a programming port's pin, any port the IOmcu reports; GPIO 28 and GPIO 13 are slice 6 | does nothing; `GPIO 28: SHARES PWM SLICE WITH PROG PORT GPIO 13` under the count |
| greyed, `PWM CH OF GPIO 0` | under SERVO PWM and MOTOR PWM: the pin shares a PWM compare register with a pin bound as PWM (GPIO 0 and GPIO 16, GPIO 1 and GPIO 17, GPIO 2 and GPIO 18, GPIO 4 and GPIO 20, GPIO 5 and GPIO 21, GPIO 6 and GPIO 22) | does nothing; `GPIO 16: SAME PWM CHANNEL AS GPIO 0` under the count |
| free, `PAD 7` | not bound | binds it |

When the selected protocol can take no more pins, the reason stands under the count in amber: `NEEDS 8 CHANNELS, 4 FREE`, `ALL 8 SLOTS IN USE` or `SERVO PWM USES 8 PINS`.

A stored binding that holds GPIO 13 keeps it: the programming port then has no pin, and the [servo programmer](servo-programmer.md) says so on CONNECT.

## On the rbmod carrier

![PWM01 to PWM04, PWM03 refused for slice 2](view:outputs-rbmod)

The [rbmod carrier](https://github.com/subtilitas/rcbench/wiki/Carrier) has fixed wiring, so you choose protocols rather than pins. Its grid shows PWM01 to PWM04 on GPIO 2, 4, 5 and 1, with each GPIO below its connector name. Tap an output to assign the selected protocol. Slot n corresponds to PWM0(n+1), so PWM01 is channel 0. Each protocol can use at most these 4 outputs: `SERVO PWM USES 4 PINS`.

PWM02 and PWM03 are one PWM slice and run at one frame rate. A surface protocol (SERVO PWM) and a throttle protocol (MOTOR PWM) are not offered on the two together: the cell reads `SLICE OF PWM02` and a tap on it `PWM03 GPIO 5: SAME PWM SLICE AS PWM02 GPIO 4`. DShot runs on its own state machine and is offered beside either.

![PROG PORT 1 on the carrier](view:outputs-rbmod-port)

PROG PORT entries show their fixed connector and pin, for example `PROGP01 ON GPIO 13`. PIN and FUNCTION cannot be edited directly. FUNCTION shows SERVO PWM when the port serves the tester, otherwise PROGRAMMING.

With IOmcu protocol 4.36 or later, SERVO TESTER and PORT PWM are available. Selecting a port requests its servo-pulse function. A conflicting PWM output on the same slice blocks the choice, and PORT PWM ON also requires the programming ports' 5V supply to be switched on in the PROG 5V box. Older protocols hide the tester rows and show `PIN FIXED. THE TESTER NEEDS IOMCU PROTOCOL 4.36`.

## The overview: OFF

![OFF shows the protocol of every bound pin](view:outputs-off)

From SETUP, OUTPUTS always opens with OFF selected. OFF changes no assignment. IN USE groups all assigned pins by protocol; pin taps do nothing and the hint reads `CHOOSE A PROTOCOL, THEN TAP PINS`. Opening the screen writes nothing to the IOmcu.

OFF remains selected until you choose a protocol, including after leaving the screen. A board change also selects OFF, whether an IOmcu first answers during the visit or is replaced by another. This prevents a previous protocol selection from carrying over to a different board. Select a protocol to edit its pins, then OFF to return to the overview.

## LAST WRITE

Pin-key changes are written immediately. LAST WRITE appears beside the protocol list, or at the foot of the left column in grid view. PROG PORT requests use their own status line instead. After writing, the panel reads the IOmcu pages back to display the active assignment.

| LAST WRITE | Meaning |
|---|---|
| `NONE YET` | nothing written since the panel started |
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

Editing requires a confirmed read of the binding. After a failed page read, the screen retains the last known binding but greys every key and shows `NOT READ`, or `ASSIGNMENT NOT READ. EDITING LOCKED.` in grid view. Pin taps have no effect. The panel retries at every link-up and every 500ms.

![Pages no binding describes](view:outputs-odd)

If the pages are readable but contain an invalid binding, such as one pin in two slots, the screen shows `NO VALID ASSIGNMENT IN THE PAGES`. Under IN USE it offers `HOLD 2s TO CLEAR ALL PIN ASSIGNMENTS`. Completing the hold writes an empty binding and enables editing again. Sliding off, releasing before 2s or leaving the screen cancels the hold.

## The IOmcu keeps the binding

The IOmcu stores the binding in its own flash and restores it at boot; the panel never stores one. A restored binding claims its pins and holds them at rest until the bench is switched to ARMED. A power cut during a save leaves the binding from before it.

## Related pages

- [Setup](setup.md)
- [Motor and ESC](motor.md#target): the pins the throttle drives.
- [Servo settings](servo-settings.md#the-target-row): the pins the servo tester drives.
