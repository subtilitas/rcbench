---
screen: none
title: Rudder deflection
order: 23
---

# Rudder deflection

RUDDER, a task of [SERVO](servo.md)'s strip, drives one servo and shows how far the control surface it moves is deflected: in degrees from a zero and, with the surface's length entered, in mm at the trailing edge. The angle is the AS5600 output encoder's, as LIVE's MEASURED reading takes it, signed in the direction of the commanded angle. The output card on the right stays as on every task: the TARGET row names what the task drives, and ARM decides whether anything moves.

![RUDDER while SAFE](view:servo-rudder)

## The readout

| Part | Shows |
|---|---|
| DEFLECTION | the angle from ZERO, -180.0 to +179.9°; before ZERO the angle from the AS5600's ENC CENTRE; `---` without a reading |
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
| Rotary knob | refused; the turn is not kept | moves the horn from the commanded value, as on LIVE |
| ZERO | takes the current angle as the neutral and clears MAX; grey without a reading | the same |
| RESET MAX | clears MAX; the zero stays | the same |

1000us, CENTRE and 2000us act on the press. LENGTH, ZERO and RESET MAX act on a tap that lifts on them. The PULSE line under the buttons names the pulse commanded. A tab switch keeps the servo where it was put; DISARM, STOP and leaving SERVO switch the bench to SAFE.

## The state lines

| Line | Reads |
|---|---|
| `ANGLE: AS5600` | the encoder reads |
| `ANGLE: AS5600 OFF IN SETUP` | the AS5600 is off on SETUP > SENSORS |
| `ANGLE: AS5600 NOT ANSWERING` | on, and no reading arrives |
| `ANGLE: AS5600 SEES NO MAGNET` | the sensor reports no magnet |
| `ANGLE: NO LINK` | no link to the IOmcu, the RP2350 board |
| `NO ZERO: FROM THE SENSOR'S CENTRE` | ZERO not taken yet |
| `ARM FIRST` | the bench is SAFE |

## What the angle is

The AS5600 sits on the servo output shaft, so the angle is the shaft's. A linkage between horn and surface makes the two differ: the readout shows the shaft's deflection, and the difference to the surface's is not measured.
