---
screen: BALANCE
title: Balance
order: 80
---

# Balance

BALANCE shows the amount and position of balancing mass for a propeller or fan, along with sensor placement. Without a linked balance node it uses fixed example values. Open it from the BALANCE tile on the [menu](overview.md). It has three tabs: BALANCE, TEST RIG and AIRCRAFT.

## The BALANCE tab

![A two-blade propeller](view:balance)

A balance result consists of a magnitude and an angle. Use the right card to select the rotor and view the result.

| Control or row | Function |
|---|---|
| PROPELLER, EDF | selects the rotor type; EDF means electric ducted fan |
| BLADES, `-`, `+` | sets the blade count from 2 to 6; tap the count to open the keypad |
| VIBRATION | vibration, in g |
| ANGLE | angle of the heavy spot, in ° |
| ADD | mass to add, in g |
| WHERE | position for the mass: between two blades, or at an angle on the hub |

Without a linked balance node, VIBRATION, ANGLE and ADD show fixed values of 0.42g, 265° and 0.35g. These are not measurements. A SIMULATED pill and `Not measured.` appear below them. WHERE calculates a blade reference from the angle and the selected blade count.

![A five-blade ducted fan](view:balance-edf)

For a ducted fan the correction is an angle on the hub, `hub, 265°`, because a blade tip inside a duct cannot be reached.

![The blade count's keypad, 5 typed](view:balance-keypad)

The keypad covers both cards. OK accepts counts from 2 to 6. Any other count leaves the keypad open and shows the allowed range in the warning colour. CANCEL, the home tag or a tab closes it without changing the count.

Without an index pulse, balancing requires four runs: a baseline, followed by runs with a trial mass at 0, 120 and 240°. An index pulse reduces the number of runs to two.

## The balance node

![BALANCE with a balance node linked](view:balance-node)

With a balance node linked ([Wireless](setup-wireless.md#the-balance-node)) the readings are the node's, and SIMULATED is gone:

| Row | Shows |
|---|---|
| VIBRATION | the 1x vibration, in g |
| ANGLE | its angle from the index mark, against the rotation; `--` without the index |
| SPEED | the rpm of the report |
| SIGNAL | `GOOD`, `FAIR` or `POOR` with the quality in %, or why there is none: `SENSOR LEVEL`, `CLIPPED`, `RPM RANGE`, `INDEX LOST`, `NO INDEX`, `NO REVOLUTION`, `SETTLING`, `NO ANGLE` |
| ADD, WHERE | the correction of the runs taken on [MOTOR & ESC](motor.md#balance), `--` before then |

A report older than 1000ms shows `--` in every row, never an old value. The panel sends the blade count to the node. Without an index it sends the bench's rpm, from the ESC's telemetry or the phase tap.

## The TEST RIG tab

![Where the sensors go on a rig](view:balance-rig)

- Bolt the accelerometer across the shaft, as close to the bearing as possible. Do not tape it in place.
- Draw one index mark on the motor bell with a pen, giving one pulse per revolution. Tape adds mass to the part being balanced. Marking the blades produces one pulse per blade.
- Keep the sensor lead out of the propeller wash.

## The AIRCRAFT tab

![Where the sensors go on an aircraft](view:balance-aircraft)

- Mount the accelerometer flat on the firewall near the bolts, not on the cowl.
- Put the index mark on the motor bell. A spinner can be refitted at a different angle.
- Tie the tail down. An aircraft that can rock acts as a spring and affects the measurement.

## Related pages

- [Wireless](setup-wireless.md#the-balance-node): binding the balance node.
- [Motor and ESC](motor.md#balance): taking the runs.
- [Sensors](setup-sensors.md): the Vibration and Optical tacho keys, stored and not read in this release; no row shows them.
- [Balancing](https://github.com/subtilitas/rcbench/blob/main/docs/Balance.md): sensor choice and placement in detail.
