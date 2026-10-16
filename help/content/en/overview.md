---
screen: OVERVIEW
title: Menu
order: 1
---

# Menu

The menu is the panel's home screen. It opens after the splash and holds one tile per bench screen. A tap on a tile opens its screen. The home tag at the top left of every other screen climbs one level: from a screen the menu opens it returns here, from a list or screen that SETUP's tiles open it returns to the tiles.

## The tiles

![The menu with the link up](view:overview)

Nine tiles, 150 x 204 px each, in two rows:

| Tile | Line under the name | Page |
|---|---|---|
| MOTOR & ESC | drive, V/A, rpm | [Motor and ESC](motor.md) |
| SERVO | travel, current | [Servo](servo.md) |
| SUPPLY | set and log V/A | [Supply](supply.md) |
| ANALYSER | buses and frames | [Analyser](analyser.md) |
| LOGS | record and view data | [Logs](logs.md) |
| SETUP | pack and output | [Setup](setup.md) |
| BATTERY | cells, iR, mAh | [Battery](battery.md) |
| BALANCE | vibration, phase | [Balance](balance.md) |
| PROGRAMMER | ESC and servo | [Programmer](programmer.md) |

A tap opens a tile when the finger lifts on the same tile it pressed. A press that slides off the tile opens nothing.

## The SIMULATED mark

The SIMULATED mark on a tile says that the coprocessor reports no capability for that screen, or that its sensor is not enabled. It does not classify each value on the screen: ANALYSER and BATTERY carry the mark and show no values at all, and SERVO without the mark shows the panel's supply model as its rail meter when neither a PD mini nor an INA3221 is enabled. Each screen names the source of its readings itself.

The mark follows the capability bits the coprocessor reports at link-up:

| Tile | Without the mark when |
|---|---|
| MOTOR & ESC | the coprocessor drives ESCs and holds an INA228 set-up enabled on SETUP, INTERFACES |
| SERVO | the coprocessor drives servo PWM (pulse-width modulation) outputs |
| SUPPLY | the PD mini is enabled on SETUP, INTERFACES |
| ANALYSER, BATTERY, BALANCE, PROGRAMMER | never in this release: no coprocessor reports these capabilities |
| LOGS, SETUP | never marked: they hold no measurement |

With no coprocessor answering, MOTOR & ESC and SERVO carry the mark too. SUPPLY follows the PD mini setting alone, link or not.

## The light theme

![The menu in the light theme](view:overview-light)

SETUP, APPLICATION, Theme switches between DARK and LIGHT. The change applies on the next frame.

## An alert at the bottom

![An alert across the bottom of the menu](view:overview-alert)

A fault that the open screen does not show appears in a red band, 34 px high, across the bottom of every screen with the status band. It clears 30 s after it arrived, or on a tap on the band. The [status band](band.md#the-alert-band) page lists how it behaves.

## At start: the splash

![The splash while the subsystems report](view:splash)

At power-on the splash lists each subsystem as it comes up: board, display, touch, storage, settings, link and coprocessor. The board line carries the panel's firmware version. The coprocessor line carries the protocol version and the firmware version the coprocessor reports.

- A missing SD (Secure Digital) card or an unusable NVS (non-volatile storage) is a warning.
- A touch controller that does not answer, or a coprocessor with another protocol major version, is a failure. The bench then does not switch to ARMED.
- When every step has answered, the menu follows after 1.6 s. A tap skips the wait.
- At every start the panel runs a CAN (Controller Area Network) self-test for 1200 ms. A bus that fails it opens the [bus-fault screen](busfault.md) instead of the menu.

## Related pages

- [Status band and STOP](band.md)
- [Coprocessor update](coproc_update.md): the question that opens on the menu when the coprocessor runs another build.
