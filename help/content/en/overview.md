---
screen: OVERVIEW
title: Menu
order: 1
---

# Menu

The menu is the panel's home screen. It opens after the splash and has one tile for each bench screen. Tap a tile to open its screen. The home tag at the top left returns one level: from a main screen to this menu, or from a SETUP page or list to the SETUP tiles.

## The tiles

![The menu with the link up](view:overview)

Eight tiles, 243 x 255px each, in 4 columns and 2 rows: the workbenches in the first row, the instruments in the second, LOGS over SETUP in the last column.

| Tile | Line under the name | Page |
|---|---|---|
| MOTOR & ESC | drive, V/A, rpm | [Motor and ESC](motor.md) |
| SERVO | travel, current, programming | [Servo](servo.md) |
| SUPPLY | set and log V/A | [Supply](supply.md) |
| LOGS | runs, files | [Logs](logs.md) |
| RECEIVER | S.BUS, PPM, frames | [Receiver](analyser.md) |
| BATTERY | cells, iR, mAh | [Battery](battery.md) |
| BALANCE | vibration, phase | [Balance](balance.md) |
| SETUP | panel, pins, card, Wi-Fi | [Setup](setup.md) |

Programming has no separate tile. Use [PROGRAM](motor.md#program) on MOTOR & ESC to program electronic speed controllers (ESCs), or PROGRAM on SERVO to program servos.

A tile opens when you lift your finger on the same tile you pressed. Sliding off the tile cancels the tap.

## The SIMULATED mark

The SIMULATED mark means that the IOmcu reports no capability for that screen, or that its sensor is disabled. The mark does not describe every value on the screen. RECEIVER and BATTERY carry it but show no values. SERVO can have no mark yet use the panel's supply model as its rail meter, when neither a PD mini nor an INA3221 is enabled. Each screen identifies the source of its readings.

The mark follows the capability bits the IOmcu reports at link-up:

| Tile | Without the mark when |
|---|---|
| MOTOR & ESC | the IOmcu drives ESCs and holds an INA228 set-up enabled on SETUP, SENSORS |
| SERVO | the IOmcu drives servo PWM (pulse-width modulation) outputs |
| SUPPLY | the PD mini is enabled on SUPPLY, tab PD MINI |
| RECEIVER, BATTERY, BALANCE | never in this release: no IOmcu reports these capabilities |
| LOGS, SETUP | never marked: they hold no measurement |

When no IOmcu responds, MOTOR & ESC and SERVO also carry the mark. The SUPPLY mark depends only on the PD mini setting, regardless of the link.

## The light theme

![The menu in the light theme](view:overview-light)

SETUP, PANEL, Theme switches between DARK and LIGHT. The change applies on the next frame.

## An alert at the bottom

![An alert across the bottom of the menu](view:overview-alert)

A fault that the open screen does not show appears in a red band along the bottom of every screen with a status band. The alert band is 44px high. An alert clears after 30s or when you tap the band. The [status band](band.md#the-alert-band) page describes the details.

## At start: the splash

![The splash while the subsystems report](view:splash)

At power-on, the splash lists each subsystem as it starts: board, display, touch, storage, settings, link and IOmcu. The board line shows the panel's firmware version. The IOmcu line shows the protocol and firmware versions reported by the IOmcu.

- A missing SD (Secure Digital) card or an unusable NVS (non-volatile storage) is a warning.
- A touch controller that does not answer, or an IOmcu with another protocol major version, is a failure. The bench then does not switch to ARMED.
- When every step has answered, the menu follows after 1.6s. A tap skips the wait.
- At every start the panel runs a CAN (Controller Area Network) self-test for 1200ms. A bus that fails it opens the [bus-fault screen](busfault.md) instead of the menu.

## Related pages

- [Status band and STOP](band.md)
- [IOmcu update](coproc_update.md): the question that opens on the menu when the IOmcu runs another build.
