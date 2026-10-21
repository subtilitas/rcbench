---
screen: none
title: Sensors
order: 62
---

# Sensors

SENSORS, the fourth category on [SETUP](setup.md), sets up to four TI INA3221 three-channel current monitors on the sensor bus, at 0x40 to 0x43, and says for each channel what it measures. The INA228 and the bus's pins stay on [Interfaces](setup-interfaces.md). The settings are kept and saved like every other setting on SETUP.

## The parts

![Two parts fitted, 0x40 online](view:setup-sensors)

| Setting | Range | Default | What it does |
|---|---|---|---|
| 0x40 fitted to 0x43 fitted | ON, OFF | OFF | an INA3221 at this address; the DAOKAI module takes 0x40 with A0 bridged to GND |

Each fitted part has a heading, `INA3221 0x40` to `INA3221 0x43`, and two rows for each of its three channels. A part not fitted shows no rows.

| Setting | Range | Default | What it does |
|---|---|---|---|
| 0x40 CH1 watches | NONE, OUT 1 to OUT 8, PORT 1 to 4, SUPPLY | OUT 1 for 0x40 CH1, NONE otherwise | what the channel's shunt is in: an output channel, a programming port or the supply rail |
| 0x40 CH1 shunt | OFF, 5.0 to 1000.0 mOhm, 0.1 mOhm steps | 100.0 | OFF: the channel is not read; 100 mOhm reads to 1.64 A |

- `+` on a shunt at OFF gives 5.0 mOhm, `-` at 5.0 mOhm gives OFF.
- The channel read every millisecond, whose current the [SERVO](servo.md#the-servo-rail-s-meter) screen and the servo test use, is the lowest channel with a shunt on a fitted part that watches the servo tester's target: an output while the target is OUTPUTS, PORT n while it is programming port n.
- The panel writes the set-up 500 ms after the last edit, only while the bench is SAFE.

## What a part is doing

The right of a part's heading says what the part is doing:

| State | Meaning |
|---|---|
| `ONLINE` | the IOmcu reads the part |
| `LOOKING FOR THE PART` | the set-up is taken; the first 2.5 s |
| `NOT ANSWERING` | nothing answers at the address |
| `ANOTHER ID ANSWERS` | something else answers there |
| `NOT WRITTEN YET` | an edit waits for its write |
| `NOT READ YET` | no IOmcu answers yet |
| `NOT ACCEPTED BY THE IOMCU` | the IOmcu refused the set-up |
| `EVERY CHANNEL OFF` | all three shunts are OFF; nothing is written |
| `INA228 ON THIS ADDRESS` | the INA228 is on at this address; neither is read |
| `IOMCU OLDER THAN 4.25: NOT READ` | an older IOmcu reads one part only |
| `SENSOR SDA OR SCL UNSET` | INTERFACES sets no sensor bus; nothing is read |

![An IOmcu older than 4.25](view:setup-sensors-old)

The rows of a part the IOmcu does not read are drawn in the warning colour, with the reason in place of the help line. An IOmcu older than 4.25 reads one INA3221: the part of the channel watching the servo tester's target, else the lowest fitted part.

A panel updated from 0.21 takes its first set-up from the one INA3221 it had: the part at the old address fitted, its CH1 on the old shunt and watching OUT 1, or PORT 1 when the servo tester's target was the programming port. The next SAVE keeps it.

## Related pages

- [Setup](setup.md)
- [Interfaces](setup-interfaces.md)
- [Servo](servo.md)
