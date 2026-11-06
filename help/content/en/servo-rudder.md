---
screen: none
title: Rudder deflection
order: 23
---

# Rudder deflection

RUDDER on the [SERVO](servo.md) strip drives one servo and displays control-surface deflection. It shows degrees from a zero point and, when the surface length is entered, millimetres at the trailing edge. ANGLE SOURCE on [Sensors](setup-sensors.md) selects the AS5600 output encoder or IMU. Like MEASURED on LIVE, the reading is signed in the direction of the commanded angle. The output card remains on the right: TARGET identifies the driven output, and ARM determines whether it can move.

![RUDDER while SAFE](view:servo-rudder)

## The readout

| Part | Shows |
|---|---|
| DEFLECTION | the angle from ZERO, -180.0 to +179.9° from the AS5600, -180.00 to +180.00° from the IMU, from 100.00° on in smaller digits; before ZERO the angle from the AS5600's ENC CENTRE, or from the IMU's count 0; `---` without a reading |
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
| CENTRE | refused; drawn dimmed | commands PULSE CENTRE plus TRIM |
| Rotary knob | refused; the turn is not kept | moves the horn from the commanded value, as on LIVE; refused while AUTO runs |
| ZERO | takes the current angle as the neutral and clears MAX; grey without a reading; another ANGLE SOURCE, the IMU's other AXIS or SIGN, or a ZERO or CLEAR ZERO on the IMU screen, drops the zero and MAX | the same; refused while AUTO runs |
| RESET MAX | clears MAX; the zero stays | the same; refused while AUTO runs |
| AUTO | shows AUTO's box ([AUTO](#auto)) | the same |

1000us, CENTRE and 2000us act on press. LENGTH, ZERO, RESET MAX and AUTO act when you release a tap on them. PULSE below the buttons shows the commanded pulse. Switching tabs leaves the servo in position. DISARM, STOP or leaving SERVO switches the bench to SAFE.

## The state lines

| Line | Reads |
|---|---|
| `ANGLE: AS5600`, `ANGLE: IMU X+` | the source reads; the IMU is named with its AXIS and SIGN |
| `ANGLE: AS5600 OFF IN SETUP` | the AS5600 is off on SETUP > SENSORS |
| `ANGLE: AS5600 NOT ANSWERING`, `ANGLE: IMU X+ NOT ANSWERING` | on, and no reading arrives; the IMU also while it is off or not connected |
| `ANGLE: AS5600 SEES NO MAGNET` | the sensor reports no magnet |
| `ANGLE: NO LINK` | no link to the IOmcu, the RP2350 board |
| `NO ZERO: FROM THE SENSOR'S CENTRE` | ZERO not taken yet |
| `ARM FIRST` | the bench is SAFE |

## AUTO

AUTO programs a KST, Chaservo or Hitec servo on the programming port so that 1000us and 2000us produce the deflection specified for each side. The [limit run's SERVO setting](servo-settings.md#the-limit-run-s-settings) selects the programmer last opened on PROGRAM. The third line identifies it, for example `SERVO HITEC D SERIES`.

The AUTO box replaces LENGTH, the drive buttons and status lines; MANUAL restores them. The side view remains visible, with the two targets as amber dashed lines. It places 1000us on the negative side and 2000us on the positive side, or uses the sides measured by the last run.

![AUTO's box, ARMED](view:servo-rudder-auto)

| Row | Sets |
|---|---|
| 1000us, 2000us | the target deflection of that side, 0.5 to 90.0°, 20.0°; with LENGTH set the mm at the trailing edge beside it |
| MARGIN | programmed past the target by this share, 0 to 50%, 10%: 20.0° at 10% is a goal of 22.0° |
| TOLERANCE | how near the goal a measure has to be, 0.1 to 5.0°, 0.5° |

First, use ZERO with the surface at neutral. During the run, ZERO, RESET MAX and MANUAL are locked. HOLD 2s TO START begins the run. The button then reads STOP RUN; pressing it ends the run immediately.

The run reads the servo and, for KST, sets its pulse range to 1000 to 2000us. For each side it commands the end pulse, waits for the angle to remain still for 500ms, then writes a new end point. With the IMU and an [IMU filter](setup-sensors.md#the-imu) above 1 it waits until the filtered angle has settled too, up to 3000ms plus the filter's longest settle, which the report names. It makes at most 8 writes per side, each in a separate programmer session with the servo switched off and on. The limit run's current guard remains active throughout. A stall before the goal ends the run with `surface binds at x°`. Every end switches the bench to SAFE and the supply output off.

The first line shows the current step, goals or reason START is refused. The second shows the result. The third names the report, RUDDRnnn.TXT. If an unsuccessful run wrote settings or interrupted a write, it instead shows `RESTORE IN PROGRAM`, or `LOAD HITEC001 IN FILES` for a Hitec servo.

## What the angle is

The AS5600 measures the servo output shaft. A linkage can make the control surface's deflection differ from the shaft reading; that difference is not measured. The IMU measures the object it is attached to. Mounted on the surface, it measures the surface's own angle.
