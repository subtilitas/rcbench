---
screen: none
title: Rudder deflection
order: 23
---

# Rudder deflection

RUDDER, a task of [SERVO](servo.md)'s strip, drives one servo and shows how far the control surface it moves is deflected: in degrees from a zero and, with the surface's length entered, in mm at the trailing edge. The angle comes from the source ANGLE SOURCE names on [Sensors](setup-sensors.md): the AS5600 output encoder or the IMU, as LIVE's MEASURED reading takes it, signed in the direction of the commanded angle. The output card on the right stays as on every task: the TARGET row names what the task drives, and ARM decides whether anything moves.

![RUDDER while SAFE](view:servo-rudder)

## The readout

| Part | Shows |
|---|---|
| DEFLECTION | the angle from ZERO, -180.0 to +179.9°; before ZERO the angle from the AS5600's ENC CENTRE, or from the IMU's count 0; `---` without a reading |
| TRAILING EDGE | L x sin(deflection) for a LENGTH of L mm: what a ruler held upright at the trailing edge reads, signed |
| MAX | the largest deflection each side since ZERO or RESET MAX, in ° and in mm |

The mm are the trailing edge's distance from the neutral plane, not the arc it travels. At 30° a surface of 100mm reads 50.0mm.

## LENGTH

LENGTH is the surface from its hinge line to its trailing edge: 0 or 10 to 500mm in steps of 1mm, kept over a restart. A tap opens the keypad. 1 to 9mm are refused, and the keypad says `0 or 10 to 500mm`. With LENGTH at 0 the readout shows degrees alone and asks for LENGTH in the mm column.

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

1000us, CENTRE and 2000us act on the press. LENGTH, ZERO, RESET MAX and AUTO act on a tap that lifts on them. The PULSE line under the buttons names the pulse commanded. A tab switch keeps the servo where it was put; DISARM, STOP and leaving SERVO switch the bench to SAFE.

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

AUTO programs a KST, Chaservo or Hitec servo on the programming port so that 1000us and 2000us reach the deflection entered for each side. The servo is the [limit run's SERVO setting](servo-settings.md#the-limit-run-s-settings), which takes the programmer opened last on PROGRAM; the box's third line names it, `SERVO HITEC D SERIES`. The box takes the place of LENGTH, the drive buttons and the state lines; MANUAL brings them back.

![AUTO's box, ARMED](view:servo-rudder-auto)

| Row | Sets |
|---|---|
| 1000us, 2000us | the target deflection of that side, 0.5 to 90.0°, 20.0°; with LENGTH set the mm at the trailing edge beside it |
| MARGIN | programmed past the target by this share, 0 to 50%, 10%: 20.0° at 10% is a goal of 22.0° |
| TOLERANCE | how near the goal a measure has to be, 0.1 to 5.0°, 0.5° |

ZERO the surface at neutral first; while the run runs, ZERO, RESET MAX and MANUAL are refused. HOLD 2s TO START starts the run; it then reads STOP RUN, and a press ends the run at once. The run reads the servo, sets a KST servo's pulse range to 1000 to 2000us, then per side drives the end pulse, waits for the angle to be still for 500ms and writes a new end point: at most 8 writes a side, each in its own session of the programmer with the servo switched off and on. The current guard of the limit run holds throughout: a stall before the goal ends the run with `surface binds at x°`. Every end switches the bench to SAFE and the supply's output off.

The first line gives the step under way, the goals, or why a START is refused; the second the result; the third the report RUDDRnnn.TXT, or `RESTORE IN PROGRAM` when a run that wrote, or cut off a write on its way, did not pass, `LOAD HITEC001 IN FILES` on a Hitec servo.

## What the angle is

The AS5600 sits on the servo output shaft, so its angle is the shaft's. A linkage between horn and surface makes the two differ: the readout shows the shaft's deflection, and the difference to the surface's is not measured. The IMU measures whatever it is fixed to, on the surface the surface's own angle.
