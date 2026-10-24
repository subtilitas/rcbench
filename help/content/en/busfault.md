---
screen: BUSFAULT
title: Bus fault
order: 110
---

# Bus fault

The bus-fault screen says why the panel and the IOmcu do not talk over the CAN (Controller Area Network) bus. It opens on its own, in two cases: the CAN self-test at start fails, or a link that was up has been gone for 4s. It takes the whole screen: no band and no STOP. The bench cannot switch to ARMED behind it.

## The self-test at start

![Frames cross and arrive changed](view:busfault)

The panel runs a CAN echo self-test at every start, for 1200ms inside the splash. A verdict other than every probe coming back intact opens this screen instead of the menu.

- The heading is the verdict.
- WHAT THIS MEANS says it in one line.
- CHECK IN THIS ORDER lists what to check, the cheapest check first.
- The right column shows what both ends counted: sent, returned, altered, lost.

![Nothing came back](view:busfault-silent)

| Heading | WHAT THIS MEANS | The checks point to |
|---|---|---|
| `No test frames returned.` | `No response on the bus. The two boards cannot communicate.` | power, CANH and CANL crossed, the release, the terminators |
| `Test frames returned with changed data.` | `Frames arrive with changed data. Check the wiring and bit timing.` | the terminators, the branch length, CANH and CANL twisted |
| `Some test frames were lost.` | `Some frames are lost, and the bus reports errors.` | the terminators, the bus length, CANH and CANL twisted, a connector |
| `Test frames lost without a bus error.` | `Frames arrived intact, then were discarded unread. Not a wiring fault.` | not the wire; report it with the numbers on the right |

The terminators read about 60Ω across CANH and CANL with the bench switched off. 120Ω means one of the two is missing. The total bus stays under 5m, each branch under 30cm.

## A link that stops

![The panel is off the bus](view:busfault-lost)

The same screen opens when a link that was up has been gone for 4s. The heading then says what the panel's own CAN controller is doing:

| Heading | Meaning |
|---|---|
| `Panel disconnected from the bus.` | too many frames went unacknowledged; it stopped transmitting |
| `Panel is reconnecting.` | it counts the quiet time a rejoin needs; give it 3s |
| `The panel's CAN controller has stopped.` | idle and not restarted: a fault in the panel's firmware |
| `The link stopped responding.` | the controller is on the bus and nothing answers |
| `Cannot read the controller's state.` | the driver is not running; nothing can be sent |

The screen never opens while the bench is ARMED: it has no STOP. While ARMED the alert band says the link is gone, and the screen waits for the switch to SAFE.

The same numbers go to `BENCH/RCBENCH.LOG` on the SD (Secure Digital) card, one line per report while the link is down. The file holds at most 1MiB (1,048,576 bytes): a line that would take it past that renames it to `BENCH/RCBENCH.OLD`, replacing the one there, and starts a new `BENCH/RCBENCH.LOG`. The lines are written whatever Auto logs on [SETUP](setup.md#sd-card) > SD CARD says.

## Leaving the screen

HOLD 2s TO ACKNOWLEDGE closes the screen, with the gesture and fade of ARM. Acknowledging repairs nothing. The bench then runs in simulation, nothing drives an output, and the test runs again at the next start.

## Related pages

- [Menu](overview.md): the splash and the self-test.
- [Status band and STOP](band.md): LINK and NO LINK.
- [Bringing up the link](https://github.com/subtilitas/rcbench/blob/main/docs/Link.md): the verdicts in detail.
