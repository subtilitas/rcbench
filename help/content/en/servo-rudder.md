---
screen: none
title: Rudder deflection
order: 23
---

# Rudder deflection

RUDDER on the [SERVO](servo.md) hub drives one servo and displays control-surface deflection. It shows degrees from a zero point and, when the surface length is entered, millimetres at the trailing edge. ANGLE SOURCE selects the AS5600 output encoder or IMU: a tap on the ANGLE row opens [its list](#the-angle-source). Like MEASURED on LIVE, the reading is signed in the direction of the commanded angle. The right card shows the PORT button and READY's rows: PORT, ANGLE, ZERO, SUPPLY and ARM. ARM in the footer switches PORT PWM and the supply on where they read off, then arms ([SERVO, ARMED](servo.md#armed)).

![RUDDER while SAFE](view:servo-rudder)

## The readout

| Part | Shows |
|---|---|
| DEFLECTION | the angle from ZERO, -180.0 to +179.9° from the AS5600, -180.00 to +180.00° from the IMU, from 100.00° on in smaller digits; before ZERO the angle from the AS5600's ENC CENTER, or from the IMU's count 0; `---` without a reading |
| TRAILING EDGE | L x sin(deflection) for a LENGTH of L mm: what a ruler held upright at the trailing edge reads, signed |
| MAX | the largest deflection each side since ZERO or RESET MAX, in ° and in mm |

The mm are the trailing edge's distance from the neutral plane, not the arc it travels. At 30° a surface of 100mm reads 50.0mm.

## The side view

The side view sits to the right of the status lines, with the leading edge on the left. It shows the fixed surface in grey, the hinge, and the flap as a cyan wedge at the angle shown by DEFLECTION. Positive deflection raises the trailing edge. Ticks mark 0, ±30, ±60 and ±90°; dashed lines mark each side's MAX. The drawing stops at ±90°, while the numeric reading continues beyond it. The drawn flap length stays fixed. Without a reading, the flap is dimmed on the neutral line.

![The surface at -85.0°](view:servo-rudder-85)

## LENGTH

LENGTH is the distance from the hinge line to the trailing edge: 0 or 10 to 500mm in 1mm steps. It survives a restart. Tap it to open the keypad. Values from 1 to 9mm are refused with `0 or 10 to 500mm`. At 0, the display shows only degrees and asks for LENGTH in the mm column.

![LENGTH not set: degrees alone](view:servo-rudder-nolength)

## Driving the servo

![RUDDER while ARMED, at 2000us](view:servo-rudder-armed)

| Control | SAFE | ARMED |
|---|---|---|
| 1000us, 2000us | refused; drawn dimmed | commands that pulse, held to TRAVEL and the pulse range of the type in force |
| CENTER | refused; drawn dimmed | commands PULSE CENTER plus TRIM |
| Rotary knob | refused; the turn is not kept | moves the horn from the commanded value, as on LIVE; refused while AUTO runs |
| ZERO | takes the current angle as the neutral and clears MAX; grey without a reading; another ANGLE SOURCE, the IMU's other AXIS or SIGN, or a ZERO or CLEAR ZERO on the IMU screen, drops the zero and MAX | the same; refused while AUTO runs |
| RESET MAX | clears MAX; the zero stays | the same; refused while AUTO runs |

1000us, CENTER and 2000us act on press. LENGTH, ZERO and RESET MAX act when you release a tap on them. PULSE below the buttons shows the commanded pulse. DISARM, STOP or leaving the screen switches the bench to SAFE. [RUDDER AUTO](#rudder-auto) programs the end points on a screen of its own.

## The angle source

A tap on READY's ANGLE row, with a reading or without, opens the ANGLE SOURCE list over the left card on a SAFE bench. ARMED or during a run the tap is refused with the reason in the band. The list holds up to 10 entries:

| Entry | A tap |
|---|---|
| `AS5600` | sets ANGLE SOURCE AS5600 and closes the list |
| `AS5600: OFF IN SETUP` | opens the AS5600 entry of [Sensors](setup-sensors.md); the home tag returns here |
| a sensor, `HC-06 00:0C:BF:08:19:29 P -50dBm`: name, its full address, its type (`P` public, `R` random), the signal; in two columns on two lines | binds it, switches the IMU on and sets ANGLE SOURCE IMU |
| `NO IMU FOUND` | the last scan found none; the tap names the usual causes in the band |
| `SEARCH FOR IMU` | switches the IMU on if it is off and scans for 10s once Bluetooth LE runs; the list stays open and shows `SCANNING: 10s`, then the sensors found |

The source in force is in the accent colour. With the IMU on and no sensor bound, opening the list starts a scan by itself.

![The ANGLE SOURCE list: the AS5600 in force, two sensors found](view:servo-rudder-angle)

![Six sensors found in two columns, two of them with the same address end](view:servo-rudder-angle-many)

![A scan under way](view:servo-rudder-angle-scan)

![The AS5600 off in SETUP, a scan that found none](view:servo-rudder-angle-none)

## The state lines

| Line | Reads |
|---|---|
| `ANGLE: AS5600`, `ANGLE: IMU X+` | the source reads; the IMU is named with its AXIS and SIGN |
| `ANGLE: AS5600 OFF IN SETUP` | the AS5600 is off on SETUP > SENSORS |
| `ANGLE: AS5600 NOT ANSWERING`, `ANGLE: IMU X+ NOT ANSWERING` | on, and no reading arrives; the IMU also while it is off or not connected |
| `ANGLE: AS5600 SEES NO MAGNET` | the sensor reports no magnet |
| `ANGLE: NO LINK` | no link to the IOmcu, the RP2350 board |
| `NO ZERO: FROM THE SENSOR'S CENTER` | ZERO not taken yet |
| `ARM FIRST` | the bench is SAFE |

## RUDDER AUTO

RUDDER AUTO, a tile of its own on the hub, programs a KST, Chaservo or Hitec servo on the programming port so that 1000us and 2000us produce the deflection specified for each side. Its SERVO, KST/CHASERVO, HITEC D SERIES or HITEC CLASSIC, is RUDDER AUTO's own saved choice, apart from LIMIT RUN's; a tap on the READY card's SERVO row picks it. The third line identifies it, for example `SERVO HITEC D SERIES`.

The left card holds the readout, the targets, MARGIN and TOLERANCE, ZERO and RESET MAX, the side view and the state lines. The side view shows the two targets as amber dashed lines: 1000us on the negative side and 2000us on the positive side, or the sides measured by the last run.

![RUDDER AUTO on the bench's outputs: PORT red, the hold grey](view:servo-rudder-auto)

| Row | Sets |
|---|---|
| 1000us, 2000us | the target deflection of that side, 0.5 to 90.0°, 20.0°; with LENGTH set the mm at the trailing edge beside it |
| MARGIN | programmed past the target by this share, 0 to 50%, 10%: 20.0° at 10% is a goal of 22.0° |
| TOLERANCE | how near the goal a measure has to be, 0.1 to 5.0°, 0.5° |

### READY

The READY card on the right lists what the start needs, one row each: PORT, SERVO, METER, ANGLE, ZERO, RANGE, PROGRAMMER, the supply (PROG 5V on the [rbmod carrier](https://github.com/subtilitas/rcbench/wiki/Carrier), PD MINI where the PD mini feeds the servo), PORT PWM and ARM. PROG 5V and PD MINI read the run's `5.0V`. A green dot is met. A red dot refuses the hold, or marks an output live through the row. Amber reads `START SWITCHES IT ON` or `START ARMS`: the hold does it. Grey waits on PORT. A tap on PORT opens the port choice, on SERVO the three families over the left column, on ANGLE the [ANGLE SOURCE list](#the-angle-source), on a red ZERO it takes the zero, on a red METER it opens [Sensors](setup-sensors.md), on a red PD MINI with `PD MINI: 5.0V OUTSIDE ITS LIMITS` [SUPPLY](supply.md) > SETTINGS > LIMITS. A tap on another red row names it in the alert band. The line under the rows is the meter the run reads.

![On the rbmod carrier, SAFE: PROG 5V, PORT PWM and ARM amber, the hold green](view:servo-rudder-auto-rbmod)

### ARM + START

First take ZERO with the surface at neutral. Then hold HOLD 2s: ARM + START in the footer. The hold sets a PD mini to 5.0V, switches PORT PWM on, then PROG 5V or the PD mini's output. It waits up to 1000ms for both to read back ON, the PD mini at 5.0V, and for a meter window taken after that, arms, waits up to 500ms for ARMED, and starts the run. The pulse runs only from the ARM. The button shows the step in amber, the first line `START, STEP 3 OF 5: READ-BACK`. A tap on it stops the sequence. During the steps the left card, SETTINGS and the READY card take no press, and the run starts on the settings the hold fired on. While the run runs it reads STOP RUN; a press ends the run at once.

![The sequence waiting for the PROG 5V's read-back](view:servo-rudder-auto-start-rbmod)

A step that fails names itself in the alert band and on the first line, for example `PROG 5V FAILED: NO READ-BACK IN 1000ms`, and switches every output off: the bench is SAFE, PROG 5V and the PD mini's output off.

![PROG 5V not read back in 1000ms](view:servo-rudder-auto-failed-rbmod)

The run reads the servo and, for KST, sets its pulse range to 1000 to 2000us. For each side it commands the end pulse, waits for the angle to remain still for 500ms, then writes a new end point. With the IMU only the IMU axis must stay still; the other axes may move. With the IMU and an [IMU filter](setup-sensors.md#the-imu) above 1 it waits until the filtered angle has settled too, up to 3000ms plus the filter's longest settle, which the report names. It makes at most 8 writes per side, each in a separate programmer session at 5.0V with the servo switched off and on ([The supply: 5.0V](servo.md#the-supply-5-0v)); a PD mini stays at 5.0V after the run. The limit run's current guard remains active throughout. A stall before the goal ends the run with `surface binds at x°`. Every end switches the bench to SAFE and the supply output off. During the run, ZERO and RESET MAX are locked.

The second line shows the result. The third names the report, RUDDRnnn.TXT. If an unsuccessful run wrote settings or interrupted a write, it instead shows `RESTORE IN KST`, or `LOAD HITEC001 IN FILES` for a Hitec servo.

![RUDDER AUTO's SETTINGS list](view:servo-rudder-auto-settings)

## What the angle is

The AS5600 measures the servo output shaft. A linkage can make the control surface's deflection differ from the shaft reading; that difference is not measured. The IMU measures the object it is attached to. Mounted on the surface, it measures the surface's own angle.
