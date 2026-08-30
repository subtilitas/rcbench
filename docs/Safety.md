# Safety

<sub>**English** · [Deutsch](Safety-de.md)</sub>

How the bench stops, what you must build for that to work, and the behaviours
you will notice and should not fight.

## Three independent ways it stops

| | What it catches |
| --- | --- |
| **The STOP command** travels over the link | a deliberate stop, acknowledged and reported |
| **The heartbeat stops** | the panel is wedged, reset, browned out, or unplugged |
| **The coprocessor's own silence watchdog** | the link is dead in either direction |

A pressed STOP uses the first two at once: it sends the command **and** stops
the heartbeat, rather than trusting either alone.

## What you must build: the monostable

The safety line is **GPIO6, carrying edges** — not a level. The coprocessor's
output enable and the servo/ESC power path belong behind a **retriggerable
monostable** that only stays energised while edges keep arriving, so a crash,
a wedged task, a reset, a brown-out and an unplugged cable all present
identically: no edges, no output. In hardware, independent of firmware at both
ends.

> **Size the monostable's window at roughly 150 ms.** The heartbeat comes from
> the panel's render loop, which delivers an edge every 26–52 ms depending on
> load — a 50 ms window would drop the outputs on every second frame of a
> bench under load. 150 ms still fires well inside the coprocessor's own
> 200 ms link failsafe. (An earlier figure of 20–50 ms circulated before the
> frame rate was measured; treat it as withdrawn.)

The firmware checks the heartbeat too, because a monostable cannot tell a
heartbeat from noise — anything that edges fast enough retriggers it. The
numbers, all from
[`heartbeat.h`](https://github.com/subtilitas/rcbench/blob/main/shared/safety/include/heartbeat.h):

| | | |
| --- | ---: | --- |
| The panel produces an edge every | 26–52 ms | one per frame |
| Firmware accepts an interval of | **4–150 ms** | faster is noise; slower means the panel stopped drawing |
| Firmware trusts the line after | **4 good intervals** | ~a tenth of a second |

That check is deliberately asymmetric — slow to trust, instant to doubt. Four
good intervals before the line is called alive; one bad interval or one silent
window takes it away immediately. An interlock that enables on a glitch and
hesitates to disable would be the inverse of the thing it is named after.

## Behaviours that are deliberate

Things the bench does that can look like faults, and are not:

- **STOP latches.** After a stop, the bench stays disarmed until you arm
  again. Navigating around, waiting, or the link recovering does not re-arm
  it.
- **Leaving a bench screen disarms.** Navigating away from an armed bench
  must not leave a propeller spinning behind a screen you can no longer see
  it on.
- **A dead touch controller disarms, and blocks arming.** If touch stops
  answering for 500 ms the bench disarms and refuses to arm, because the
  panel is the only place a STOP button exists. A bench you cannot stop is
  not armed — it is running away.
- **After a link failsafe, the bench does not re-arm itself.** Traffic
  returning proves the link is alive; it does not prove anybody meant to
  continue. Arm again from the panel once the cause is fixed.
- **The coprocessor acts without asking.** Overcurrent, over-temperature,
  stall timeout, lost link: it protects the hardware on its own authority and
  reports what it did at the next poll. It never waits for permission to fail
  safe.

## Why a heartbeat and not an enable line

A static enable — pin high while running is allowed — fails the most likely
failure: firmware wedges with the pin still high, and the outputs stay live.
The one case where you most want the bench to stop is the one a level cannot
express. Edges expire on their own.

Two rules keep the heartbeat honest. It is driven **from the loop that reads
touch and draws STOP** — never from a timer, a DMA-fed peripheral, or its own
task, because a signal like that proves the wrong thing is alive: a peripheral
can keep toggling perfectly while the application is wedged. And the firmware
checks the **period**, not just the presence of edges, because that is the
check a shorted or ringing line cannot pass.

The panel itself has no wire to any output: it cannot spin a motor by
convention or by accident, because there is nothing from it to anything that
can.
