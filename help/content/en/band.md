---
screen: none
title: Status band and STOP
order: 2
---

# Status band and STOP

The status band occupies the top 60px of each screen except the splash, bus-fault screen, PANEL UPDATE and IOmcu update progress. It shows the screen name and bench state and provides the STOP control.

## What the band shows

From left to right, the band contains the home tag and screen name, the status chips, a "?" where a guide is available, and STOP at the right edge.

| Chip | Shows |
|---|---|
| LINK, NO LINK | whether the IOmcu answers on the CAN (Controller Area Network) bus |
| BENCH, SIM | BENCH while the IOmcu answers and the screens drive its outputs; SIM while the panel runs its own model |
| INJECT | in place of BENCH and SIM while a touch injector answers at I2C (Inter-Integrated Circuit) address 0x14; a panel looks for one once, at start-up |
| FAULT 01 to FAULT 7F | a fault code the IOmcu reports; absent while there is none |
| HWSTOP | in FAULT's place, red, while [SETUP > PANEL > Hardware stop](setup.md#panel) is ON and the heartbeat bit (10) is set: the bench's stop button is pressed or its cable is broken |
| SAFE, ARMED MOTOR, ARMED SERVO, ARMED, STOP | what the switch to ARMED drives; STOP while a stop is latched |
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

STOP works on every screen with a status band. Pressing it immediately switches the bench to SAFE and turns off every output, the supply output and the programming ports' 5V. It latches like an emergency stop button. Changing screens, clearing an alert, restoring the link or requesting ARMED does not clear the latch.

![A latched STOP and its box](view:band-stop)

- While it is latched, STOP stays red inside a yellow frame and reads `STOPPED` over `HOLD 2s TO RESET`, and the arm chip reads STOP. ARM, OUTPUT ON, PROG 5V ON, CONNECT and the start of a test or a run are refused, with `STOP pressed: hold STOP 2s to reset.` That alert leaves the band in the frame after the reset completes.
- Hold the latched STOP for 2s to reset it. The fill turns from red to yellow across the 2s. The panel then waits until the IOmcu reports SAFE and every output off, at least 500ms after the last STOP, and sends the heartbeat again. The bench stays SAFE: each output needs its own hold to switch on again.
- Without the IOmcu's answer within 2s, the stop stays latched and the alert band says `STOP not reset: the IOmcu did not report every output off within 2s.` Hold STOP again.
- A press on a latched STOP is a STOP too. The hold counts only while one finger stays on STOP for the whole 2s. A release before 2s shows `Hold 2s to reset the STOP.`; a finger that slides off STOP, a second finger on the band, a screen change, lost touch events or a touch controller that stops answering for 100ms resets nothing.
- A restart of the panel clears the latch: the panel starts SAFE with every output off.
- STOP acts on a press that lands on it, at the press. A finger that slides onto STOP from the screen stops nothing. A finger that lands on STOP and slides off stops all the same.
- A second finger on STOP is answered while the first finger drags, and the drag ends at that press.
- With a chip's box open, a press on STOP closes the box and stops at once.

## Chip help

![The box under the LINK chip](view:band-link)
![The box under the SAFE chip](view:band-safe)

Tap a chip to open its help box. The box explains the current state and what to do. It is 640px wide and appears 4px below the band. Tapping another chip switches to that chip's box. Other taps close the box without acting, except DISARM, OUTPUT OFF, a stick run's ABORT, STOP TEST and STOP RUN: these act on the same press. Leaving the screen also closes the box. While open, its text follows changes in the state.

![The box under ARMED MOTOR](view:band-armed)

![LINK with 37 refused link frames](view:band-link-refused)

If the IOmcu has refused any link frames since power-up, the LINK box ends with their count, for example `Rejected link frames: 37`.

## FAULT codes

![The FAULT box with three bits set](view:band-fault)

The FAULT box spans the panel, 1008px, with one paragraph per set bit:

| Code | Name | Set in this release when |
|---|---|---|
| 01 | LINK_SILENT | the IOmcu heard no request for 200ms and switched to SAFE; stays until the next arm |
| 02 | OVERCURRENT | never |
| 04 | OVERTEMP | never |
| 08 | STALL | never |
| 10 | HEARTBEAT | the safety line from the panel stopped while the panel sends it: a pressed emergency stop or an open heartbeat wire; a latched STOP shows STOP instead |
| 20 | VERSION | never |
| 40 | STORE_OFF | the IOmcu cannot write its flash this boot; set-ups sent to it last until it restarts |

For a bit this release never sets, the box says to flash both boards from one release.

## HWSTOP

![HWSTOP and its box](view:band-hwstop)

A stop button on the bench sits in the heartbeat line between the panel and the IOmcu. A pressed button and a broken or unplugged cable both open the line, and the IOmcu sets bit 10, HEARTBEAT. With SETUP > PANEL > Hardware stop OFF, the default, the band shows `FAULT 10`. With it ON, the band shows a red `HWSTOP` chip in FAULT's place.

- The bench reacts the same either way: the IOmcu is SAFE, the motor and servo outputs and the PD mini output are off, and ARM is refused.
- HWSTOP does not tell a pressed button from a broken cable. If releasing the button does not clear it, check the cable and its plugs.
- To clear it, release the button or repair the cable. HWSTOP goes 80 to 180ms after the line closes, without an ARM: the IOmcu trusts the line after 4 good heartbeat intervals of 20ms, and while HWSTOP shows the panel reads the IOmcu's status every 50ms, not every 500ms.
- The bench stays SAFE. Hold ARM for 2s to switch the outputs on again.
- Other bits set at the same time are listed in HWSTOP's box, which spans the panel as FAULT's does. They show as a FAULT code again once HWSTOP goes.
- While the panel's own STOP is latched, while its reset runs and for 1000ms after it, the panel withholds the heartbeat itself, and the band shows neither HWSTOP nor FAULT 10.

## Screenshot

Hold the unmarked gap between the home tag and the chips for 1s. At 1s, the band reports `Screenshot taken.`; releasing your finger does nothing further. Pressing a chip, the home tag, "?" or STOP does not take a screenshot. Sliding out of the gap, using a second finger or losing touch cancels the gesture.

The screenshot captures the current display, including the band and any open box, list or keypad. Capture is immediate even while ARMED and affects no output; STOP and DISARM remain active. Only one picture is held in memory. Another hold replaces it, and a restart discards it. The splash, bus-fault screen, PANEL UPDATE and IOmcu update progress have no band, so they cannot use this gesture.

![The screenshot's popup](view:shot-popup)

The save popup waits until the bench is idle: SAFE, supply output and PROG 5V off, no held port, programming session, sweep, test, run, update, FAULT or open log. The link must be up unless the bench is simulated. If any of these conditions changes, the popup closes and the picture waits until they are met again.

- Use the keyboard to edit the name, up to 55 bytes. The default is `SHOT` followed by the next number. The `.PNG` extension stays fixed.
- SAVE writes `screenshots/<name>.PNG` to the card, creating the folder if needed. Existing names are refused; no file is overwritten.
- CANCEL discards the picture. If saving fails, for example because the card is missing or full, the popup stays open and retains the picture.
- The file is a 1024 x 600px PNG (Portable Network Graphics), viewable in a browser or on a PC. It appears in [FILES](files.md) and on the Wi-Fi page.

## The screen guide

![The menu's guide at page 1](view:help-overview)

"?", 8px left of STOP, opens the guide for the current screen. The menu, OUTPUTS, SETUP, LOGS and FILES have a guide; other screens have no "?" control. Each guide page marks up to 5 controls with numbered badges and corner brackets. A 480 x 488px dock on one half of the screen lists their names and functions, using at most 3 rows per control. The other half continues running. The dock's last row gives the address of this help.

- PREV and NEXT turn the guide pages. They replace the home tag in the band and stay in the same position on every page. Repeated taps at the same position keep turning pages. CLOSE at the bottom of the dock, or "?", closes the guide.
- Tapping elsewhere closes the guide without acting on the screen below.
- STOP closes the guide and stops the bench immediately. DISARM, OUTPUT OFF, a stick run's ABORT, STOP TEST and STOP RUN also act on the same press.
- The knob has no effect while the guide is open.
- While ARMED, the guide shows only pages that leave output-stop controls and the TARGET row uncovered.

OUTPUTS, SETUP, LOGS and FILES have a set of pages for each state: the tiles, a list and SENSORS on SETUP, the board view, a PROG PORT entry and the grid view on OUTPUTS, the file list, the import view and the plot on LOGS, the panes on FILES. Their dock takes the right half on page 1. A control the dock covers in part is marked where it shows. These screens drive no output, so all their pages show while ARMED.

![SETUP's guide at page 1](view:help-setup)
![OUTPUTS' guide at page 1](view:help-outputs)
![LOGS' guide at page 1](view:help-logs)
![FILES' guide at page 1](view:help-files)

## The alert band

![An alert across the bottom of the menu](view:overview-alert)

A fault the open screen does not show appears in a red band, 44px high, across the bottom of the screen. It covers what is beneath it; ARM and the controls that stop the bench end above it.

- It clears 30s after it arrived. A new alert starts the 30s again. A notice, the hint of a [hold button](#hold-buttons), shows in amber for 4s and gives way to an alert.
- A tap on the band, marked `x` at its right end, clears it earlier. The screen beneath receives neither that tap nor a second finger on the band.
- DISARM, OUTPUT OFF, a stick run's ABORT, STOP TEST and STOP RUN end above the band: a press on one is its own, and the alert stays. ARM and OUTPUT ON under the band are not pressed.
- "Touchscreen not responding. ARMED is blocked.", shown at start, stays until restart and has no `x`.

## Hold buttons

A hold button acts after 2s of contact. ARM, OUTPUT ON, START TEST, START LIMIT RUN, APPLY and every button labelled `HOLD 2s TO ...` use this gesture. Releasing before 2s sends no command. Instead, the alert band shows an amber hint for 4s, for example `Hold 2s to switch to ARMED.`

- The hint shows only for a release the finger made on the button. It does not show when the finger slid off the button first, when the touch was lost, for a second finger's contact, or on a screen without the status band.
- A release at 2s or later has acted and shows no hint.
- A short tap is not a fault, so the hint is a notice and not an alert: a tap on the band clears it earlier, an alert showing keeps the band and no hint shows over it, and an alert that arrives while the hint shows replaces it.
- STOP clears the hint at once, and so does a new FAULT code or a switch from ARMED to SAFE by an error.
- The hints, one per action: `Hold 2s to switch to ARMED.`, `switch the output on`, `apply`, `start the test`, `start the limit run`, `enable`, `write`, `restore`, `continue`, `leave`, `load`, `release the port`, `repair`, `run`, `install`, `download and install`, `update the IOmcu`, `clear all pin assignments` and `reset the STOP`, each after `Hold 2s to`.

## Touch near the band

- A touch that comes down on the band stays the band's. Sliding it into the screen moves nothing there.
- A touch that comes down on the screen and slides up into the band is released at its last point on the screen. A slider or the SERVO dial keeps the value it had there. A hold on ARM or OUTPUT ON is abandoned.
- OUTPUT OFF and START TEST while a servo test runs still act on such a release, because they stop something. DISARM has acted on its press already.
- A press on the band, on STOP or on the alert band ends a drag another finger makes on a slider or the SERVO dial.
