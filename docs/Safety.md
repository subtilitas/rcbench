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

Monostable window: about 150 ms. The heartbeat comes from the panel's control
task, which runs every 5 ms on the core that does not draw, so its period does
not depend on what a frame costs. The window stays at 150 ms, inside the
coprocessor's 200 ms link failsafe, rather than tightening to the new period:
the margin is what survives a task that is late, and nothing is gained by
removing it.

The monostable is on no board. On the bring-up bench the edges reach the
coprocessor's GP3 by a direct wire from J8, which is what lets that bench arm.

The monostable is retriggered by the panel's edges and removes the outputs
when they stop. What it adds over the direct wire is that it does this without
the coprocessor's firmware doing anything. Three cases, and the middle one is
the gap:

| | Direct wire and firmware | With the monostable |
|---|---|---|
| The panel stops beating, the coprocessor is healthy | disarms after HEARTBEAT_MAX_GAP_MS | the same, and sooner or later by its own timing |
| The panel stops beating and the coprocessor cannot act -- wedged, or not servicing its monitor | nothing disarms; the bank goes on driving | the outputs are removed anyway |
| The panel is healthy and the coprocessor misbehaves | the panel's STOP and the link watchdog are what is left | **no help**: the panel is still beating, so the monostable stays energised |

The third row is not a case the monostable is for, and no hardware in this
design covers it. Until the part is fitted, the second row is uncovered too,
and firmware at both ends is the whole of the interlock.

## Heartbeat monitor

The coprocessor also checks the heartbeat in firmware, because a monostable
cannot distinguish a heartbeat from noise. Constants from
[`heartbeat.h`](https://github.com/subtilitas/rcbench/blob/main/shared/safety/include/heartbeat.h):

| | Value | |
| --- | ---: | --- |
| Panel edge interval | 20 ms | `HEARTBEAT_PERIOD_MS`, one edge per control-task period, not per frame |
| Accepted interval | 4–150 ms | shorter is noise; longer means the loop that owns STOP has stopped |
| Good intervals before the line is trusted | 4 | 80 ms at the panel's rate |

The check is asymmetric: four good intervals before the line is trusted, one
bad interval or one silent window to distrust it. The coprocessor refuses to
arm while the line is not trusted, and disarms its outputs when it stops.

The heartbeat is generated in the loop that reads touch and owns STOP, not by
a timer or a peripheral, and not in the loop that draws: a panel that has
stopped drawing can still be stopped, and one that cannot read touch cannot. The coprocessor's input is pulled down, so an
unpowered or unplugged panel reads as a line that is not edging.

## Deliberate behaviours

- STOP latches. The bench stays disarmed until it is armed again.
- Arming is a two-second hold on ARM, and the command goes when the hold
  completes rather than when the finger lifts. Disarming is a press.
- A hold is credited at most 250 ms per frame, so it spans at least eight
  frames with the press standing. A frame's duration is measured at its top
  and applied at its end, and without the cap one late frame credits a hold
  that began while that same frame was dispatching its touch events. Both
  the arming hold and the bus-fault acknowledgement take the cap.
- A frame that lost touch events cancels the gesture in progress. A full
  touch queue drops its oldest entry to take the newest, and no choice there
  is safe on its own: a release that never arrives leaves a screen holding a
  press, a press that never arrives orphans the release after it, and the
  movement where a finger leaves a button is what abandons the hold. The
  render task drains the queue from the other core, so inspecting an entry
  does not decide which one is removed. The loss is counted instead, and the
  frame that observes it tells the screen on top that its record of the glass
  is stale; the screen drops any gesture in progress. That asks for nothing,
  exactly as letting go early does, with the one exception below. Both queues
  are counted -- the driver's
  own event queue evicts its oldest for the same reason -- and the count is
  read after the drain and before the frame's tick, so an event lost while
  the loop is running is answered in that frame rather than the next. The
  frame log carries the two counts as `TOUCHLOST <panel>/<driver>`.
- Every control that holds state between a press and its release cancels.
  MOTOR & ESC, SERVO and CAN BUS FAULT have a gesture that completes on a
  timer, so a lost release there arms or acknowledges on its own; the
  overview's tiles, the outputs and picker screens' cells, the settings
  screen's keys and the tab rows of MOTOR & ESC, ANALYSER and BALANCE act on
  the release instead, and a press left latched owns a
  track id the controller reuses, so a later contact that began elsewhere is
  taken for the missing release. HOME and STOP are the router's own gesture
  and it cancels those itself.
- A touch stream that breaks while STOP is held stops the bench. The control
  task owns that press independently of the screens, and the release that
  would have stopped the bench may be the event that went missing -- or it may
  arrive and satisfy neither owner, because the render side cancels the band's
  press for the same loss. Nothing else would stop it, and the operator has
  already pressed STOP. What this gives up: a press that began on STOP and
  would have been carried off it before lifting, which asks for nothing today,
  stops the bench instead.
- Cancelling an armed bench's disarm still disarms. Abandoning a gesture asks
  for nothing, and on an armed bench that is the wrong direction for one of
  them: disarming is a press, so its release is the whole command, and a
  release lost to a full queue is a disarm the operator made and the bench
  never saw. Arming has already sent its command by the time the finger
  lifts, so an arm cancelled part way asks for nothing, which is correct.
- The throttle moves by how far a finger travels, not to where it lands. A
  press on the track commands nothing, so a touch at the far end asks for
  nothing; a drag across the whole track asks for the whole span, and one
  completed inside a 50 ms poll is a 0 to 100 % step at the pin. Sliders that
  command nothing dangerous, such as the servo screen's sweep speed, keep
  tap-to-set.
- A disarm returns the throttle to zero, so an arm starts from nothing rather
  than from where the last run left it.
- Leaving a bench screen disarms.
- If the touch controller stops answering for 500 ms, the bench disarms and
  refuses to arm. The panel is the only place a STOP button exists.
- After a link failsafe the coprocessor does not re-arm when traffic returns.
  Leaving the failsafe takes a write of a defined value (0x5AFE) to the control
  page.
- The coprocessor acts on overcurrent, over-temperature, stall timeout and a
  lost link on its own authority and reports the fault at the next poll.
- An armed bench drives every bound pin, whether or not anything is commanding
  it. A channel that has had no command for 500 ms is rendered at its role's
  rest: stopped for a throttle, centred for a surface. Centred is the midpoint
  of that channel's own endpoints — 1500 us across the default 1000 to 2000 us,
  and 760 us across the 660 to 860 us of a narrow servo. The timeout moves the
  channel to that rest and leaves the pin driving; disarming is what stops the
  edges.
- A receiver output of 1500 us is about half throttle. What an ESC that has
  seen no pulses does when it is then handed 1500 us is not measured on this
  bench: it may run at about half throttle, and it may refuse to arm until it
  has seen a stop. A pin bound as a servo output can therefore carry a running
  motor.
- The throttle does not reach a channel bound as a surface, because it
  commands the pins the binding calls motors. A write to the CHANNELS page
  does, because that page addresses channels by index rather than by role.
- For the first 500 ms after an arm, a channel is not at its rest. Arming
  stamps every channel's clock, so a command given while the bench was
  disarmed is not overdue and is rendered until it goes overdue. What is
  rendered depends on the channel's slew: with no slew the first step is the
  whole distance, so the channel is at that command; with a slew set, the
  disarm has already put it at rest and it ramps from there, covering at most
  `slew_per_s * 500 / 1000` before the timeout takes it back. Either way it is
  driving, and a disarm is what stops it.
- The `Ramp limit` setting, 5 to 300 %/s, governs the panel's own throttle
  bank. That bank is the modelled bench: its slewed value is read by the
  telemetry simulator and by nothing else, and only while the link is down. A
  coprocessor that is answering is sent the raw command instead, and
  `outbind_to_chan_cfg()` writes no slew for any channel, so a pin bound as a
  throttle steps to it on the next 1 ms pass. The physical throttle is not
  ramped, by decision: the bank ramps a throttle upward only, so a ramp on
  the wire would slow the rise and nothing else. [STATUS.md, Not
  planned](https://github.com/subtilitas/rcbench/blob/main/STATUS.md#not-planned).

## Heartbeat rather than enable level

A static enable level fails when firmware wedges with the pin high. Edges
expire on their own. The period check in firmware rejects a shorted or ringing
line, which a monostable alone would accept.

The panel has no wire to any output.
