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

Each fitted part has a heading, `INA3221 0x40` to `INA3221 0x43`, and three rows for each of its three channels. A part not fitted shows no rows.

| Setting | Range | Default | What it does |
|---|---|---|---|
| 0x40 CH1 watches | NONE, OUT 1 to OUT 8, PORT 1 to 4, SUPPLY | OUT 1 for 0x40 CH1, NONE otherwise | what the channel's shunt is in: an output channel, a programming port or the supply rail |
| 0x40 CH1 shunt | OFF, 5.0 to 1000.0mΩ, 0.1mΩ steps | 100.0 | OFF: the channel is not read; 100mΩ reads to 1.64A |
| 0x40 CH1 limit | OFF, 10 to 32767mA, 10mA steps on the arrows | OFF | the band says when a 50ms window's highest current passes it, and the bench switches to SAFE with the supply output off |

- `+` on a shunt at OFF gives 5.0mΩ, `-` at 5.0mΩ gives OFF.
- A tap on a shunt's value opens the keypad: 44.4mΩ from the 100.0 default is `4`, `4`, `.`, `4` and OK, where `-` takes 556 steps. 0 is OFF; 0.1 to 4.9 is refused: the keypad stays open and shows `0 or 5.0 to 1000.0mΩ` in place of the range until the next key.
- The channel read every millisecond, whose current the [SERVO](servo.md#the-servo-rail-s-meter) screen and the servo test use, is the lowest channel with a shunt on a fitted part that watches the servo tester's target: an output while the target is OUTPUTS, PORT n while it is programming port n.
- The panel writes the set-up 500ms after the last edit, only while the bench is SAFE. The limits go 500ms after their last edit in any state, to an IOmcu of 0.22.0 or later.

## Limits

![A limit on an IOmcu older than 0.22.0](view:setup-sensors-limit)

While a limit is set, the IOmcu judges each channel window of 50ms, and the panel reads which channels passed every 200ms. The band says, at most once every 5s, and once every 30s for a channel that keeps passing. Channels over at the same time take turns: with all 12 over, each is said within 60s.

| Band | Meaning |
|---|---|
| `INA3221 0x41 CH2 (PORT 1) over its 0.80A limit.` | a window passed the channel's limit; in brackets what the channel watches; the limit the window was judged by |
| `An INA3221 channel passed its limit; which one was lost on the link.` | the reply that named the channel was lost; said after the next link-up, not after an IOmcu restart |
| `INA3221 limits rejected. Check SETUP > SENSORS.` | the IOmcu refused the limits and keeps its own, or refused the page |

A limit row the IOmcu does not hold is drawn in the warning colour: while its write waits, after a refusal, and on an IOmcu older than 0.22.0 (protocol 4.27), which judges no limit (`IOmcu older than 0.22.0: no limit is judged.`).

![The keypad of 0x40 CH1 shunt, 44.4 typed](view:setup-sensors-keypad)

![3mΩ refused, the reason in place of the range](view:setup-sensors-why)

## What a part is doing

The right of a part's heading says what the part is doing:

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
| `INA228 ON THIS ADDRESS` | the INA228 is on at this address; neither is read |
| `IOMCU OLDER THAN 0.22.0: NOT READ` | an older IOmcu reads one part only |
| `SENSOR SDA OR SCL UNSET` | INTERFACES sets no sensor bus; nothing is read |

![An IOmcu older than 0.22.0](view:setup-sensors-old)

The rows of a part the IOmcu does not read are drawn in the warning colour, with the reason in place of the help line. An IOmcu older than 0.22.0 (protocol 4.25) reads one INA3221: the part of the channel watching the servo tester's target, else the lowest fitted part.

A panel updated from 0.21 takes its first set-up from the one INA3221 it had: the part at the old address fitted, its CH1 on the old shunt and watching OUT 1, or PORT 1 when the servo tester's target was the programming port; every limit OFF. The next SAVE keeps it.

## Related pages

- [Setup](setup.md)
- [Interfaces](setup-interfaces.md)
- [Servo](servo.md)
