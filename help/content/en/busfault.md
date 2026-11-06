---
screen: BUSFAULT
title: Bus fault
order: 110
---

# Bus fault

The bus-fault screen explains why the panel and IOmcu cannot communicate over the CAN (Controller Area Network) bus. It opens automatically if the startup CAN self-test fails or an established link has been lost for 4s. It occupies the whole display, with no status band or STOP control. The bench cannot switch to ARMED while this screen is open.

## The self-test at start

![Frames cross and arrive changed](view:busfault)

The panel runs a 1200ms CAN echo self-test during every startup splash. Unless every test frame returns intact, this screen opens instead of the menu.

- The heading gives the verdict.
- WHAT THIS MEANS explains it in one line.
- CHECK IN THIS ORDER lists the checks, starting with the least costly.
- The right column shows the counts at both ends: sent, returned, altered and lost.

![Nothing came back](view:busfault-silent)

| Heading | WHAT THIS MEANS | The checks point to |
|---|---|---|
| `No test frames returned.` | `No response on the bus. The two boards cannot communicate.` | power, CANH and CANL crossed, the release, the terminators |
| `Test frames returned with changed data.` | `Frames arrive with changed data. Check the wiring and bit timing.` | the terminators, the branch length, CANH and CANL twisted |
| `Some test frames were lost.` | `Some frames are lost, and the bus reports errors.` | the terminators, the bus length, CANH and CANL twisted, a connector |
| `Test frames lost without a bus error.` | `Frames arrived intact, then were discarded unread. Not a wiring fault.` | not the wire; report it with the numbers on the right |

With the bench switched off, measure about 60Ω across CANH and CANL. A reading of 120Ω means one of the two terminators is missing. Keep the total bus under 5m and each branch under 30cm.

## A link that stops

![The panel is off the bus](view:busfault-lost)

After an established link has been lost for 4s, the same screen opens. Its heading describes the state of the panel's CAN controller:

| Heading | Meaning |
|---|---|
| `Panel disconnected from the bus.` | too many frames went unacknowledged; it stopped transmitting |
| `Panel is reconnecting.` | it counts the quiet time a rejoin needs; give it 3s |
| `The panel's CAN controller has stopped.` | idle and not restarted: a fault in the panel's firmware |
| `The link stopped responding.` | the controller is on the bus and nothing answers |
| `Cannot read the controller's state.` | the driver is not running; nothing can be sent |

This screen does not open while the bench is ARMED, because it has no STOP control. The alert band reports the lost link instead. The bus-fault screen waits until the bench is SAFE.

The same counts are written to `BENCH/RCBENCH.LOG` on the SD (Secure Digital) card, one line per report while the link is down. The file is limited to 1MiB (1,048,576 bytes). Before a line would exceed that limit, the logger renames the file to `BENCH/RCBENCH.OLD`, replacing any existing file of that name, and starts a new `BENCH/RCBENCH.LOG`. These lines are written regardless of the Auto logs setting under [SETUP](setup.md#sd-card) > SD CARD.

## A link that answers again

![The link answers again](view:busfault-back)

The counts on the right follow the link while the screen shows. Once the IOmcu has answered every request for 1s, the bar turns green and reads LINK BACK, and `back` counts the seconds since the first answer. The start-up test's screen does the same, with the test's counts kept on the right.

- HOLD 2s TO RECONNECT replaces HOLD 2s TO ACKNOWLEDGE. It opens the menu on the live bench, SAFE.
- One request without an answer takes RECONNECT back at once. The next 1s of answers offers it again.
- RECONNECT clears nothing. A STOP that stands stays, and so does the FAULT chip. FAULT 01, the IOmcu's failsafe, clears at the next ARM.

## Leaving the screen

HOLD 2s TO ACKNOWLEDGE closes the screen, using the same hold gesture and fade as ARM. Acknowledging does not repair the fault. The bench then runs in simulation without driving outputs. The self-test runs again at the next startup. Once the link answers again, HOLD 2s TO RECONNECT takes its place.

A press released before 2s shows what the hold does in place of the red bar, amber, for 4s: `Hold 2s to acknowledge.` or `Hold 2s to reconnect.` A tap on the bar closes it.

## Related pages

- [Menu](overview.md): the splash and the self-test.
- [Status band and STOP](band.md): LINK and NO LINK.
- [Bringing up the link](https://github.com/subtilitas/rcbench/blob/main/docs/Link.md): the verdicts in detail.
