**English** | [Deutsch](Carrier-de.md)

# The rbmod carrier

The rbmod carrier holds the Waveshare RP2350-CAN module and connects
each lead to a fixed GPIO (general-purpose input/output). The panel
cannot change these pin assignments. The carrier's IOmcu runs its own
image, `rcbench-iomcu-rbmod`, and reports board 2 in IDENTITY register 5
([Link: Boards with soldered pins](Link.md#boards-with-soldered-pins)).

## Pins

| Signal | GPIO | Connector |
| --- | --- | --- |
| PROGP01, PROGP02, PROGP03, PROGP04 | 13, 14, 15, 0 | J1 |
| PWM01, PWM02, PWM03, PWM04 | 2, 4, 5, 1 | J2 |
| SDA01 / SCL01, the sensor bus (I2C0) | 16 / 17 | J5 |
| SDA02 / SCL02 (I2C1) | 18 / 19 | J4 |
| PDMINI TX / RX | 7 / 6 | J6 |
| TAP | 22 | J9 |
| PROGP_5V_EN, the enable of the programming ports' 5V | 21 | none |
| HEARTBEAT | 3 | J3 |
| CAN (Controller Area Network) | 8 to 12 | on the module |
| unused, reserved | 20, 26, 27, 28 | none |

GPIO 26 to GPIO 28 are ADC (analog-to-digital converter) pins and not 5V
tolerant. No servo line is on them.

The second I2C (Inter-Integrated Circuit) bus on J4 is not driven in this
release.

## Connectors J1 and J2

Each position is signal, + and GND. Every signal has a 120Ω series
resistor.

| J1 position | Signal | GPIO | + rail |
| --- | --- | --- | --- |
| 1 | PROGP04 | 0 | VCC_PROGPORT |
| 2 | PROGP01 | 13 | VCC_PROGPORT |
| 3 | PROGP02 | 14 | VCC_PROGPORT |
| 4 | PROGP03 | 15 | VCC_PROGPORT |

| J2 position | Signal | GPIO | + rail |
| --- | --- | --- | --- |
| 1 | PWM03 | 5 | VCC_SERVOPORT_B |
| 2 | PWM02 | 4 | VCC_SERVOPORT_B |
| 3 | PWM01 | 2 | VCC_SERVOPORT_A |
| 4 | PWM04 | 1 | VCC_SERVOPORT_A |

The position order is set for the layout.

PWM02 (GPIO 4) and PWM03 (GPIO 5) are one PWM (pulse-width modulation)
slice and run at one frame rate. PWM04 (GPIO 1) shares slice 0 with
PROGP04 (GPIO 0), and PROGP02 (GPIO 14) shares slice 7 with PROGP03
(GPIO 15). From protocol 4.36 the servo tester drives the chosen
programming port's pulse ([The programming port](#the-programming-port)).
The binding of PWM01 to PWM04 comes first: a PWM output bound on PWM04
sets PROGP04 back to FUNC NONE, and while PWM04 carries a PWM output,
PROGP04 takes no pulse. Only the chosen port runs as SERVO PWM, so
PROGP02 and PROGP03 never pulse together.

## The programming port

The KST and Hitec programmers share a programming port with the servo
tester. The panel stores the choice across restarts in SERVO TESTER
(SET_SERVO_TARGET): 1 to 4 select PROGP01 to PROGP04; 0 selects the servo
outputs. Choose the port in either of these places:

- the programmers' PORT row, before CONNECT: a tap on the row's label
  opens PROGP01 to PROGP04 with their GPIO
  ([Servo programmer: The port on the rbmod carrier](ServoProgrammer.md#the-port-on-the-rbmod-carrier));
- the PORT button on every SERVO function screen: a tap opens OUTPUTS and
  PROG PORT 1, PROG PORT 2, PROG PORT 3 and PROG PORT 4 over the left card
  ([Screens: SERVO](Screens.md#servo));
- a PROG PORT entry on SETUP > OUTPUTS, SERVO TESTER PORT n
  ([Screens: On the rbmod carrier](Screens.md#on-the-rbmod-carrier)).

| Rule | Value |
| --- | --- |
| IOmcu protocol | 4.36 or later; an older carrier holds every FUNC at NONE, and the panel offers no choice |
| FUNC of the chosen port | SERVO PWM, asked by the panel; the other three NONE |
| FUNC taken by the IOmcu | NONE or SERVO PWM; DShot is refused with BAD_VALUE |
| FUNC at an IOmcu start | NONE on every port; the panel asks for SERVO PWM again |
| Locked | ARMED or asked to arm, a test, a sweep or a limit run, a programmer holding a port |
| PROGP04 | not offered while PWM04 carries a PWM output: `PROGP04 SHARES PWM SLICE 0 WITH PWM04 (GPIO 1)` |
| Pulse | only while PROG 5V reads ON. PORT PWM ON is taken with PROG 5V off, and OUTPUTS reads `PORT PWM ON: ARM ON SERVO SWITCHES PROG 5V AND PULSES ON`; OUTPUT in SERVO's settings reads `PROG 5V OFF: ARM SWITCHES IT ON`, and the READY card of a servo test, RUDDER AUTO and a limit run reads `PROG 5V OFF: START SWITCHES IT ON` |
| Supply | M7's fixed 5V on GPIO 21; a servo test's voltage steps reach the servo rails, not the port, so a test there is refused with `PROG PORTS RUN AT 5V FIXED: NO STEPS` |
| Current | INA3221 0x40 CH3 reads all four ports together |

A pulse on a programming port runs only while the bank is ARMED. Every
error, a STOP and a silent link disarm the bank, and the port's next frame
is 0 (`test_outputs_hw_ports`). Every error, a lost heartbeat and a silent
link also switch GPIO 21 off. A PROG 5V OFF by hand stops the pulse as
well: the panel asks for the pulse only while the box reads ON.

The panel switches the port's pulse on only while the bench is SAFE, as
on every board; it switches it off at once. A PROG 5V switched on while
ARMED leaves the pulse off until the next SAFE: the limit run then reads
`PORT PWM ON, WAITING FOR SAFE: DISARM, THEN ARM`.

PORT PWM is on OUTPUTS. The PROG 5V box is on SUPPLY; SERVO switches
the 5V through ARM and ARM + START ([The PROG 5V box](#the-prog-5v-box)).
A PROG 5V switched on from SERVO stays on
from one SERVO screen to another, the hub included. Leaving SERVO switches
it off.

On LIVE, SWEEP and RUDDER the READY card's PORT and SUPPLY rows read
`ARM SWITCHES IT ON` in amber while SAFE with PORT PWM or PROG 5V off.
ARM's 2s hold then switches PORT PWM ON and PROG 5V ON, waits up to 1000ms
for both to read back, arms and waits up to 500ms for ARMED. A step that
fails names itself in the alert band and switches every output off. DISARM
switches the bench to SAFE and PROG 5V off
([Servo: ARM switches the supply](Servo.md#arm-switches-the-supply-on-live-sweep-and-rudder)).

RUDDER AUTO and LIMIT RUN need no step by hand: their HOLD 2s: ARM +
START, from SAFE, switches PORT PWM ON, then PROG 5V ON, waits up to 1000ms
for the PROG 5V, the port's PWM bit (set while SAFE) and an INA3221 window
begun after both, arms, waits up to 500ms for ARMED and starts the run. The
pulse runs from the ARM ([Servo: AUTO end points](Servo.md#auto-end-points),
[Servo: Running it](Servo.md#running-it)). Their READY card reads `START
SWITCHES IT ON` on the PROG 5V and PORT PWM rows while SAFE. A step that
fails names itself in the alert band and ends with the bench SAFE, PROG 5V
off.

## Power

| Part | Input | Output | Switched by |
| --- | --- | --- | --- |
| M6, USB-C PD (Power Delivery) trigger | USB-C | the +15V net | its DIP switch sets 5V, 9V, 12V, 15V or 20V |
| M7, buck | the +15V net | 5V, VCC_PROGPORT_DCDC: the programming ports on J1 | EN on GPIO 21, with a pull-down on EN |
| M8, buck | the +15V net | +5V: the module's VBUS and J7 | always on |
| PD mini, on J8 | its own input | VCC_PDMINI: the servo rails A and B on J2 | the SUPPLY page |

M7's fixed 5V supplies the programming ports independently of the PD
mini. During a KST or Hitec session, the operator retains control of the
PD mini: SUPPLY accepts OUTPUT ON and set
points that it would refuse during a session on other boards. KST and
Chaservo servos are programmed at 5V. On the carrier, the programmers
cannot change the programming-port voltage. Their MORE list reads
`PROGRAMMING VOLTAGE: 5V FIXED, GPIO 21` and offers no voltage or current
setting.

The KST and Hitec programmers switch GPIO 21 on and off for a session,
through the PROG_SUPPLY page (link 4.32). The PROG 5V box on SUPPLY's
LIVE task switches it by hand: PROG 5V ON is a
2s hold, PROG 5V OFF a tap, taken with the bench SAFE or ARMED, as the PD
mini's OUTPUT ON is. The box names the pin, `PROG PORTS 5V, GPIO 21: OFF`,
and shows the last 50ms window of 0x40 CH3, `5.02V  0.120A`
([The current monitor](#the-current-monitor)). While a programmer's
session holds the 5V the box takes no press and reads
`HELD BY PROG PORT n`. SUPPLY also shows the state under OUTPUT:
`PROG PORTS 5V, GPIO 21: ON` or `OFF`.
Every error, a lost heartbeat and a silent link switch it off; a write to
the servo's memory keeps it on until the write ends, 10000ms at most
([Safety, The carrier's programming-port 5V](Safety.md#the-carriers-programming-port-5v)).

The IOmcu drives GPIO 21 low from the first statement of `main()`. While
the IOmcu does not drive GPIO 21 (in reset, in BOOTSEL, unpowered), the
pull-down on EN holds M7 off.

### The input supply

M6's DIP switch is set to 15V on the carrier. M7 makes the programming
ports' 5V from the +15V net, so an input source that delivers 5V instead
leaves M7 short of 5V: a KST or Hitec CONNECT then finds no servo.

The PD mini on J8 runs from the same input and reports its input
voltage. The IOmcu reads it every 500ms while the PD mini is enabled and
answers, with its output on or off; the panel reads the SUPPLY page every
100ms. While that reading is under 13.5V (`PROG5V_INPUT_MIN_MV`: 15V less
10%, room for the reading and the cable's drop), the panel warns and
refuses:

| Where | What it shows or does |
| --- | --- |
| The alert band, once as the reading goes under 13.5V, taken back when it is 13.5V or more | `INPUT 5.1V: NEEDS 15V. CHECK THE SUPPLY VOLTAGE` |
| SUPPLY's head line, in red | `PD MINI TX GPIO 7 RX GPIO 6   INPUT 5.1V < 15V` |
| The PROG 5V box's second line, in red | `INPUT 5.1V: NEEDS 15V` |
| PROG 5V ON | Takes no hold; the alert band names the input. |
| The PROG 5V row of the READY card on KST, HITEC, LIMIT RUN and RUDDER AUTO, in red | `INPUT 5.1V < 15V`; a tap names the input in the alert band. |
| CONNECT on KST and HITEC | Refused with the input named; the session stays in PWM, the 5V off. |
| ARM + START on LIMIT RUN and RUDDER AUTO | Refused with `PROG 5V: INPUT 5.1V: NEEDS 15V. CHECK THE SUPPLY VOLTAGE`; no output is switched. |

The volts are in tenths, rounded down: 13.49V reads `13.4V`. Without a
PD mini reading there is no warning from the input: the PD mini disabled,
not fitted, not answering, or its reading older than 1500ms. The check
after PROG 5V ON ([PROG 5V missing or lost](#prog-5v-missing-or-lost))
covers that case. Rev 1 has no other reading of the input
([Rev 1 rework](#rev-1-rework)).

![SUPPLY with the input at 5.1V](img/supply-rbmod-input.png)

![KST with the input at 5.1V: CONNECT refused](img/servo-kst-rbmod-input.png)

| An ON that does not take | Alert |
| --- | --- |
| no live heartbeat (NOT_ARMED) | `PROG ports 5V not switched on: the IOmcu has no heartbeat.` |
| a port holds the 5V (BAD_VALUE) | `PROG ports 5V not switched on: a programming port holds it.` |
| no answer; the OFF follows | `PROG ports 5V ON not confirmed. Check the link.` |
| taken across lost touch events; the bench goes SAFE | `Touch signal lost while switching on. PROG ports 5V off.` |
| a press while a port holds the 5V or the page reads NOT READ | `PROG ports 5V: held by a programming port, or not read.` |

### The PROG 5V box

The PROG 5V box on SUPPLY switches the 5V by hand: PROG 5V ON is a 2s
hold, PROG 5V OFF a tap. SERVO has no box: ARM on LIVE, SWEEP and RUDDER
and ARM + START on LIMIT RUN and RUDDER AUTO switch the 5V on, and on KST
and HITEC a tap on the READY card's supply row switches it off. The panel judges the ON as it judges the PD
mini's OUTPUT ON:

- It is taken with the bench SAFE or ARMED; no switch to ARMED is asked.
- It is dropped when a STOP, an OFF or a touch loss came after the hold
  completed, and while a port holds the 5V. An error, an OFF or a restart
  of the link after the hold began refuses it as well, also once that OFF
  is written (`prog_supply_link_cuts()`).
- It is written once as OUTPUT 1 and not repeated. The IOmcu refuses it
  without a live heartbeat (NOT_ARMED) and while a port holds the 5V
  (BAD_VALUE); each, and no answer, gets an alert
  (the table above). An ON without an answer owes an
  OFF: the IOmcu may have taken it and lost its reply.
- An ON taken across lost touch events is an error: SAFE, the 5V off.
- An error, PROG 5V OFF and leaving the screen whose box switched it on
  write OFF, the last also while the ON is still on its way. An error
  drops an ON not yet written. The OFF is a flag beside its queue entry,
  as the PD mini's OUTPUT OFF is, so a full queue does not drop it.
- While a port holds the 5V the box takes no press.
- A firmware install, an IOmcu update, a flash write and the Wi-Fi count
  the ON from the moment it is queued (`supplies_live_or_coming()`).

### PROG 5V missing or lost

INA3221 0x40 CH3 measures VCC_PROGPORT_DCDC, M7's output. The panel
watches it on every ON the PROG_SUPPLY page reads: the PROG 5V box's, the
ON of ARM and of ARM + START, and a KST or Hitec session's. The page must
report GPIO 21 on or have acknowledged the panel's ON, and the heartbeat
must be running. A 50ms window whose mean bus voltage is under 4750mV
(`PROG5V_MIN_MV`: 5V less 5%, the lower bound of USB's 5V range) is low.

- **Missing:** no window of 4750mV or more by 200ms after the ON
  (`PROG5V_REACH_MS`, the page's read period). The first new window read
  at or after 200ms decides. On an input of 5V M7 cannot make 5V; what
  it makes there is not measured.
- **Lost:** once a window has read 4750mV or more, two consecutive new
  windows under 4750mV, both read at least 500ms after the ON.

M7's controller, most likely a JoulWatt JW5068A (not confirmed on the
module), latches its output off on an over-voltage (over 6.0V for 20us) or
an under-voltage (under 3.25V for 200us). It starts again only when EN is
toggled. The known cause on the carrier is the over-voltage latch: a servo
on a programming port that brakes feeds energy back into the 5V, which M7
cannot sink. A zener on the rail absorbs it on a reworked board
([Rev 1 rework](#rev-1-rework), item 3). The PROG_SUPPLY page then still
reads GPIO 21 on.

Either finding is a bench error:

- SAFE, every output off, and the page's OFF
  ([Safety, Errors](Safety.md#errors)). The next PROG 5V ON toggles EN and
  clears M7's latch.
- The band, missing: `PROG 5V MISSING: 3.1V AFTER 200ms. CHECK THE SUPPLY
  VOLTAGE`; lost: `PROG 5V lost: INA3221 0x40 CH3 read 0.12V while ON.
  All outputs off.`
- The console: `PROG 5V missing: INA3221 0x40 CH3 read 3.10V 200ms after
  ON. All outputs off.` or `PROG 5V lost: INA3221 0x40 CH3 read 0.12V,
  0.08V in 2 windows, 640ms after ON. All outputs off.`
- One error an ON: none again until the 5V has been off and on.

The 200ms count from the moment the panel sees the ON: the acknowledged
write of the box's ON, or the first page read that shows a session's ON.
A window is 50ms and the panel reads one every 40ms, so at least two whole
windows closed after the ON are read in 200ms. Two windows for a loss
keep one window of a heavy load's sag from tripping. Each window is
counted once by its number. An OFF request, whether from the operator or
an error, restarts the 500ms interval and holds a missing finding back
until it has passed: the page can still report the 5V as on until its
next read, at most 200ms later.

A KST or Hitec session switches the 5V off and on itself: a KST rail cycle
keeps it off for 1000ms, a classic Hitec entry for 500ms. The panel sees
GPIO 21 only at its page reads, every 200ms. While a session holds the 5V
a finding waits for a page read made after it that still reads GPIO 21 on;
a read that shows it off, or a window of 4750mV or more first, drops it.
A session's error therefore comes up to 200ms later than the box's.

The rule is `shared/safety/prog5v_lost.c` under `test_prog5v_lost`;
`test_prog5v_wire` drives it with the pages as the panel does.

Current limitations:

- Without an INA3221 channel watching PROG 5V there is no watch: no map in
  force with a channel set to watch PROG 5V, or no window of that channel
  read (the part offline, the link down). A board without the PROG_SUPPLY
  switch has no watch either.
- A session that switches the 5V off and on again between two page reads
  (under 200ms) and leaves it off across the read after a finding trips
  the watch. No session of the firmware switches it off for less than
  500ms, except a Hitec D series entry, whose off time is 0ms.
- A load that holds the 5V under 4750mV for two windows, for example a
  stalled servo past M7's current limit, is a loss. M7's current limit
  is not measured.
- The detection time on hardware is not measured. By the rule it is
  200ms to about 240ms after the ON for a missing 5V, and 2 new windows
  after the collapse, about 100ms, plus the 500ms after the ON for a loss;
  under a session up to 200ms more.

![SUPPLY on the carrier](img/supply-rbmod.png)

![LIVE on the carrier: PORT PWM and PROG 5V amber, ARM switches them on](img/servo-rbmod.png)

![The KST programmer holding the 5V](img/servo-kst-rbmod.png)

## The current monitor

An INA3221 (M5) on the sensor bus at address 0x40 watches the three
supplies. Its VS is +3.3V, and the carrier ties its VPU to VCC. The module's I2C
pull-ups hang on VPU, so they are bus 1's pull-ups: the firmware runs bus 1
(GPIO 16 SDA, GPIO 17 SCL) with the RP2350's pad pulls off.

| Channel | From | To |
| --- | --- | --- |
| CH1 | VCC_PDMINI | VCC_SERVOPORT_B, J2 positions 1 and 2 |
| CH2 | VCC_PDMINI | VCC_SERVOPORT_A, J2 positions 3 and 4 |
| CH3 | VCC_PROGPORT_DCDC (M7) | VCC_PROGPORT, J1 |

The part and channel assignments are fixed. On SENSE_MAP, the IOmcu
keeps the part marked as fitted and each channel assigned to its rail.
It refuses maps that change either assignment
([Link, Boards with soldered pins](Link.md#boards-with-soldered-pins)).
SETUP > SENSORS shows these rows without editing keys:

| Row | Reads |
| --- | --- |
| 0x40 fitted | ON |
| 0x40 CH1 watches | SERVO RAIL B (PWM03, PWM02) |
| 0x40 CH2 watches | SERVO RAIL A (PWM01, PWM04) |
| 0x40 CH3 watches | PROG PORTS 5V (PROGP01 to PROGP04) |

![SENSORS on the carrier](img/setup-sensors-rbmod.png)

- The shunts and the limits are settings, as on any bench: shunt OFF or
  5.0 to 1000.0mΩ, 100.0mΩ by default; limit OFF or 10 to 32767mA
  ([Screens: SENSORS](Screens.md#sensors-the-ina3221-parts)). The shunt
  value of M5 on the carrier is not stated here; 100.0mΩ is the DAOKAI
  module's.
- 0x41 to 0x43 are free for parts on J5.
- The channel read at 1kHz, which the servo test and the SERVO screen
  read, is the rail of the lowest GPIO the servo tester drives as SERVO
  PWM: CH2 for GPIO 1 or GPIO 2, CH1 for GPIO 4 or GPIO 5. With SERVO PWM
  on both rails it reads only the rail of the lowest GPIO. With a
  programming port as the tester's target it is CH3.
- With PORT on the bench's outputs and no output bound as SERVO PWM no
  channel watches the target, and none is read at 1kHz. The limit run
  then reads `NO SERVO PWM PIN` and RUDDER AUTO
  `AUTO PROGRAMS A SERVO ON THE PROG PORT`, each before any line about the
  INA3221 ([Servo](Servo.md#auto-end-points)).
- SERVO and the limit run name the meter by its rail: `INA RAIL A` over
  the plot, `INA3221 RAIL A` on the TEST card. The band names the rail of
  a channel over its limit:
  `INA3221 0x40 CH2 (SERVO RAIL A) over its 0.80A limit.`. Each
  programming port's WATCHED BY line names CH3, `INA3221 PROG 5V`. The run
  log's columns are `rail b`, `rail a` and `prog 5v`.
- An IOmcu image older than protocol 4.33 refuses the carrier's map: 0x40
  reads `NOT ACCEPTED BY THE IOMCU` until the IOmcu runs the panel's
  image.

## The image

| Build | Files | Release asset |
| --- | --- | --- |
| `cmake -S firmware/iomcu -B firmware/iomcu/build-rbmod -DIOMCU_BOARD=rbmod` | `rcbench-iomcu-rbmod-install.uf2`, `rcbench-iomcu-rbmod.uf2`, `rcbench-iomcu-rbmod-pt.uf2`, `rcbench-iomcu-rbmod.bin`, `rcbench-iomcu-rbmod.elf` | `rcbench-iomcu-rbmod-vX.Y.Z.zip` |

For the first load, use the install UF2 (USB Flashing Format) over USB,
as described in [First run](FirstRun.md#from-the-release-assets). The
panel subsequently offers each IOmcu the image for its reported board.

## On the panel

The panel reads board 2 from the IOmcu and shows the carrier's pins as
fixed:

- SETUP > OUTPUTS shows PWM01 to PWM04 as the four cells of the grid. A
  surface and a throttle protocol on PWM02 and PWM03 together are refused
  by name. The PROG PORT entries name PROGP01 to PROGP04 and their GPIO,
  offer no pin and no function, and from protocol 4.36 offer SERVO TESTER
  and PORT PWM ([The programming port](#the-programming-port);
  [Screens: On the rbmod carrier](Screens.md#on-the-rbmod-carrier)).
- SETUP > SENSORS shows Sensor SDA and SCL and Tap pin at the carrier's pins,
  with no keys and no keypad, the SENSOR BUS entry reads
  `GPIO 16 / 17 SOLDERED`, and 0x40 reads fitted with its three channels on
  the rails, with no keys ([The current monitor](#the-current-monitor)).
- SUPPLY's PD mini settings shows PD mini TX and RX at the carrier's pins, with no
  keys and no keypad.
- A LAYOUT tile opens LAYOUT: J1 and J2 in their position order
  with each position's role, GPIO, net on its + and the protocol bound
  on it, and the other connectors
  ([Screens: Layout](Screens.md#layout)).

![SETUP on the carrier, LAYOUT the last tile](img/setup-rbmod.png)

![LAYOUT](img/layout-rbmod.png)

## Rev 1 rework

Every rev 1 board requires three manual changes. The carrier image
assumes that items 1 and 2 are complete; it does not rely on item 3.
Rev 2 implements items 1 and 2 in the board layout. The solution for
item 3 remains open for rev 2: a converter that does not latch off, or
a clamp implemented on the board.

Rev 2 also adds, as planned, a divider from the +15V net to GPIO 26
(ADC0, unused on the carrier since item 1): 100kΩ top, 10kΩ bottom and
100nF at the pin. It reads 1.36V at 15V, 1.82V at 20V (M6's highest DIP
setting) and 0.45V at 5V, all under the pin's 3.3V. With it the panel can
warn about a low input supply from power-on, also without a PD mini
fitted. Rev 1 has no such divider, and the firmware does not read GPIO 26:
on rev 1 the warning comes from the PD mini's input reading
([The input supply](#the-input-supply)).

| # | Change | Reason | The firmware relies on it |
| --- | --- | --- | --- |
| 1 | PROGP03 (J1 position 4, through R4) moves from GPIO 26 to GPIO 15: the trace is cut under the series resistor R4, and a bodge wire runs from R4 to GPIO 15. | GPIO 26 is an ADC pin and not 5V tolerant. A Hitec or KST pull-up puts the servo supply on the programming line. | PROG PORT 3 is GPIO 15 on every carrier. GPIO 26 stays unused. |
| 2 | A pull-down of 2kΩ to 5kΩ from M7's EN (the GPIO 21 net) to GND. | The buck module's EN reads open as on. Without the pull-down the programming ports' 5V is on while the IOmcu is in reset, in BOOTSEL or unpowered. | The programming ports' supply is off whenever GPIO 21 is not driven high. |
| 3 | A 5.6V zener of 2W or 5W from M7's output (VCC_PROGPORT_DCDC) to GND, cathode at the rail, for example SMBJ5339B-TP (5.6V 5W, SMB package). | A braking servo feeds energy back into PROG 5V. M7 cannot sink current and latches off when its output passes about 6.0V for 20us. The zener absorbs the feedback. | No. The PROG 5V lost error ([PROG 5V missing or lost](#prog-5v-missing-or-lost)) reports a latched M7 with or without the zener. |

Item 3's limits:

- M7's over-voltage threshold is 115% to 125% of the output, 5.75V to
  6.25V on 5.00V. A 5.6V zener of ±5% conducts at 5.32V to 5.88V.
- The clamp voltage rises with the back-fed current by the zener's
  dynamic impedance. Typical datasheet values at the test current are
  about 1Ω for a 5W zener and about 5Ω for a 1W zener. Each 100mA above
  the test current adds about 0.1V on the 5W zener and about 0.5V on the
  1W zener, so the 5W zener's clamp stays lower at higher back-feed. A
  2W zener's value depends on the part; no 2W part is named here.
- Back-feed of several hundred mA is not measured. Whether a larger servo
  still latches M7 with either zener is not measured.
