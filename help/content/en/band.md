---
screen: none
title: Status band and STOP
order: 2
---

# Status band and STOP

The status band is the top 60px of every screen except the splash, the bus-fault screen, PANEL UPDATE and the progress of an IOmcu update. It names the screen, shows the state of the bench and holds STOP.

## What the band shows

From the left: the home tag with the screen's name, then the chips, then "?" on a screen with a guide, then STOP at the right edge.

| Chip | Shows |
|---|---|
| LINK, NO LINK | whether the IOmcu answers on the CAN (Controller Area Network) bus |
| BENCH, SIM | BENCH while the IOmcu answers and the screens drive its outputs; SIM while the panel runs its own model |
| INJECT | in place of BENCH and SIM while a touch injector answers at I2C (Inter-Integrated Circuit) address 0x14; a panel looks for one once, at start-up |
| FAULT 01 to FAULT 7F | a fault code the IOmcu reports; absent while there is none |
| SAFE, ARMED MOTOR, ARMED SERVO, ARMED | what the switch to ARMED drives |
| run clock | the length of the current run from the switch to ARMED, minutes and seconds; it holds the last run at SAFE |

The home tag goes to the [menu](overview.md).

## The arm chip

| Chip | Outputs that get pulses |
|---|---|
| SAFE | none |
| ARMED MOTOR | the throttle outputs, from MOTOR & ESC's ARM or a stick run on its PROGRAM |
| ARMED SERVO | the servo outputs and a pulsing PROG PORT, from SERVO |
| ARMED | every bound output, on an IOmcu older than 0.18.0 |

A channel with no command for 500ms sits at rest while ARMED: a throttle at 0%, a servo at the middle of its travel.

## STOP

STOP works on every screen that has the band. It switches the bench to SAFE, switches the supply output off and latches: the bench stays SAFE until it is switched to ARMED again. Leaving the screen, an alert that clears or a link that comes back does not clear a stop.

- STOP acts on a press that lands on it, at the press. A finger that slides onto STOP from the screen stops nothing. A finger that lands on STOP and slides off stops all the same.
- A second finger on STOP is answered while the first finger drags, and the drag ends at that press.
- With a chip's box open, a press on STOP closes the box and stops at once.

## Chip help

![The box under the LINK chip](view:band-link)
![The box under the SAFE chip](view:band-safe)

A tap on a chip opens a box under the band. It says what the chip's state means for the bench and what to do. The box is 640px wide and opens 4px under the band. A tap on another chip closes it and opens that chip's box. Any other tap closes it and does nothing else, except on DISARM, OUTPUT OFF, a stick run's ABORT, STOP TEST or STOP RUN: those act on the same press, as with no box. Leaving the screen closes it too. The text follows the state while the box is open.

![The box under ARMED MOTOR](view:band-armed)

![LINK with 37 refused link frames](view:band-link-refused)

While the IOmcu has refused link frames since its power-up, the LINK box ends with the count, for example `Rejected link frames: 37`.

## FAULT codes

![The FAULT box with three bits set](view:band-fault)

The FAULT box spans the panel, 1008px, with one paragraph per set bit:

| Code | Name | Set in this release when |
|---|---|---|
| 01 | LINK_SILENT | the IOmcu heard no request for 200ms and switched to SAFE; stays until the next arm |
| 02 | OVERCURRENT | never |
| 04 | OVERTEMP | never |
| 08 | STALL | never |
| 10 | HEARTBEAT | the safety line from the panel stopped: after any STOP until the next arm, a pressed emergency stop, or an open heartbeat wire |
| 20 | VERSION | never |
| 40 | STORE_OFF | the IOmcu cannot write its flash this boot; set-ups sent to it last until it restarts |

For a bit this release never sets, the box says to flash both boards from one release.

## Screenshot

Hold the empty part of the band for 1s, between the home tag and the chips. Nothing on the panel marks the place. The band's notice says `Screenshot taken.` at 1s, and the release does nothing. A press on a chip, the home tag, "?" or STOP is no screenshot, and neither is a slide out of the gap, a second finger or a lost touch.

The picture is what the panel shows, band included, also over a box, a list or a keypad. It is taken at once while ARMED too and touches no output; STOP and DISARM stay live. One picture is held in memory: another hold replaces it, and a restart loses it. The splash, the bus-fault screen, PANEL UPDATE and the progress of an IOmcu update have no band and no hold.

![The screenshot's popup](view:shot-popup)

The popup opens once the bench is idle and nothing else is up: SAFE, supply and PROG 5V off, no port held, no programming session, sweep, test or run, no update, no FAULT, no log open, the link up or the bench simulated. If that stops, the popup closes and the picture waits again.

- The keyboard edits the name, `SHOT` and the next number by default, up to 55 bytes. `.PNG` stays.
- SAVE writes `screenshots/<name>.PNG` on the card and makes the folder when it is missing. A taken name is refused; nothing is overwritten.
- CANCEL drops the picture. A failure, no card or a full card, stays on the popup with the picture kept.
- The file is a PNG of 1024 x 600px, for any browser or PC. It shows in [FILES](files.md) and on the Wi-Fi page.

## The screen guide

![The menu's guide at page 1](view:help-overview)

"?", 8px left of STOP, opens the guide of the screen on top. The menu has one; the other screens show no "?" yet. The guide numbers up to 5 controls of the screen with a badge and corner brackets and lists them in a dock of 480 x 488px on one half of the screen: each control's name, then what it does in 3 rows at most. The other half keeps running. The dock's last row names this help's address.

- PREV and NEXT turn the pages. They sit in the band in place of the home tag, the same spot on every page, so a second tap where the first landed turns the next page. CLOSE, at the dock's bottom, or a tap on "?" closes the guide.
- A tap anywhere else closes it and does nothing else.
- STOP works through the guide: a press on STOP closes it and stops at once. DISARM, OUTPUT OFF, a stick run's ABORT, STOP TEST and STOP RUN act on the same press, as with no guide.
- The knob moves nothing while the guide is open.
- While ARMED the guide shows only the pages that cover no control that stops an output and no TARGET row.

## The alert band

![An alert across the bottom of the menu](view:overview-alert)

A fault the open screen does not show appears in a red band, 44px high, across the bottom of the screen. It covers what is beneath it; ARM and the controls that stop the bench end above it.

- It clears 30s after it arrived. A new alert starts the 30s again. A notice, the hint of a [hold button](#hold-buttons), shows in amber for 4s and gives way to an alert.
- A tap on the band, marked `x` at its right end, clears it earlier. The screen beneath receives neither that tap nor a second finger on the band.
- DISARM, OUTPUT OFF, a stick run's ABORT, STOP TEST and STOP RUN end above the band: a press on one is its own, and the alert stays. ARM and OUTPUT ON under the band are not pressed.
- "Touchscreen not responding. ARMED is blocked.", shown at start, stays until restart and has no `x`.

## Hold buttons

A hold button acts after 2s of contact. ARM, OUTPUT ON, START TEST, START LIMIT RUN, APPLY and every button labelled `HOLD 2s TO ...` are hold buttons. A release before 2s sends nothing and shows the button's hint in the alert band for 4s, in amber instead of red, for example `Hold 2s to switch to ARMED.`

- The hint shows only for a release the finger made on the button. It does not show when the finger slid off the button first, when the touch was lost, for a second finger's contact, or on a screen without the status band.
- A release at 2s or later has acted and shows no hint.
- A short tap is not a fault, so the hint is a notice and not an alert: a tap on the band clears it earlier, an alert showing keeps the band and no hint shows over it, and an alert that arrives while the hint shows replaces it.
- STOP clears the hint at once, and so does a new FAULT code or a switch from ARMED to SAFE by an error.
- The hints, one per action: `Hold 2s to switch to ARMED.`, `switch the output on`, `apply`, `start the test`, `start the limit run`, `enable`, `write`, `restore`, `continue`, `leave`, `load`, `release the port`, `repair`, `run`, `install`, `download and install`, `update the IOmcu` and `clear all pin assignments`, each after `Hold 2s to`.

## Touch near the band

- A touch that comes down on the band stays the band's. Sliding it into the screen moves nothing there.
- A touch that comes down on the screen and slides up into the band is released at its last point on the screen. A slider or the SERVO dial keeps the value it had there. A hold on ARM or OUTPUT ON is abandoned.
- OUTPUT OFF and START TEST while a servo test runs still act on such a release, because they stop something. DISARM has acted on its press already.
- A press on the band, on STOP or on the alert band ends a drag another finger makes on a slider or the SERVO dial.
