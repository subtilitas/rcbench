# Safety

<sub>**English** · [Deutsch](Safety-de.md)</sub>

How the bench stops, the external circuit the design requires, and the
behaviours that are deliberate.

## Stop mechanisms

| Mechanism | Covers | State |
| --- | --- | --- |
| Heartbeat stops | the panel is wedged, reset, browned out or unplugged; a pressed STOP | generated and monitored at both ends; the monostable it gates is not fitted |
| Coprocessor silence watchdog, 200 ms | the link is dead in either direction | built and tested |
| STOP command over the link | a deliberate stop, acknowledged and reported | written; not run on hardware |

A pressed STOP stops the heartbeat, disarms the panel's own output model
and writes ARM = 0 to the control page. While the link is up the panel
writes ARM and THROTTLE at every 50 ms poll; an explicit arm writes CLEAR
(0x5AFE) first, and a NACK from the coprocessor leaves the panel disarmed.

## Required external circuit: the monostable

The safety line is panel GPIO6 (general-purpose input/output pin 6) on
header J8 (3V3, GND, GPIO6). It carries
edges, not a level. The coprocessor's output enable and the servo and ESC
(electronic speed controller) power path must be gated by a retriggerable
monostable that stays energised only while edges keep arriving. A crash, a
wedged task, a reset, a brown-out and an unplugged cable then all produce the
same result: no edges, no output, independent of firmware at both ends.

Monostable window: about 150 ms. The heartbeat comes from the panel's render
loop, which delivers an edge every 26 to 52 ms depending on load; a 50 ms
window would drop the outputs under load. 150 ms is inside the coprocessor's
200 ms link failsafe.

The monostable is on no board. The edges reach J8 and nothing else.

## Heartbeat monitor

The coprocessor also checks the heartbeat in firmware, because a monostable
cannot distinguish a heartbeat from noise. Constants from
[`heartbeat.h`](https://github.com/subtilitas/rcbench/blob/main/shared/safety/include/heartbeat.h):

| | Value | |
| --- | ---: | --- |
| Panel edge interval | 26–52 ms | one edge per rendered frame |
| Accepted interval | 4–150 ms | shorter is noise; longer means the panel has stopped rendering |
| Good intervals before the line is trusted | 4 | about 0.1 s at the panel's rate |

The check is asymmetric: four good intervals before the line is trusted, one
bad interval or one silent window to distrust it. The coprocessor refuses to
arm while the line is not trusted, and disarms its outputs when it stops.

The heartbeat is generated in the loop that reads touch and draws STOP, not by
a timer or a peripheral. The coprocessor's input is pulled down, so an
unpowered or unplugged panel reads as a line that is not edging.

## Deliberate behaviours

- STOP latches. The bench stays disarmed until it is armed again.
- Leaving a bench screen disarms.
- If the touch controller stops answering for 500 ms, the bench disarms and
  refuses to arm. The panel is the only place a STOP button exists.
- After a link failsafe the coprocessor does not re-arm when traffic returns.
  Leaving the failsafe takes a write of a defined value (0x5AFE) to the control
  page.
- The coprocessor acts on overcurrent, over-temperature, stall timeout and a
  lost link on its own authority and reports the fault at the next poll.

## Heartbeat rather than enable level

A static enable level fails when firmware wedges with the pin high. Edges
expire on their own. The period check in firmware rejects a shorted or ringing
line, which a monostable alone would accept.

The panel has no wire to any output.
