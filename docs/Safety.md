# Safety

A bench that spins propellers needs stopping to be the one thing that cannot
fail quietly. This page is how that is arranged, and why it is arranged that
way rather than the obvious way.

## The safety line is a heartbeat, not an enable

The obvious design is a static enable line: the panel holds a pin high while it
is willing to let the outputs run, and drops it to stop them.

That design fails the most likely failure. Firmware wedges with the pin still
high and the outputs stay live — the one case where you most want the bench to
stop is the one case a level cannot express.

So **GPIO6 toggles at 100 Hz to 1 kHz**, driven by the loop that reads touch and
draws the STOP button, and the coprocessor's output enable and its servo and ESC
power path are gated from a **retriggerable monostable with a 20–50 ms window**.

A crash, a wedged task, a reset, a brown-out and an unplugged cable then present
identically: no edges, so no output. Fail-safe by absence, in hardware,
independent of firmware at both ends.

Two refinements. Noise can *fake* a heartbeat, so the monostable is the crude
backstop and the coprocessor also checks the period in firmware. And the
heartbeat is driven from the loop whose liveness it asserts — never from a
timer or its own task, because a heartbeat driven by a timer proves the timer is
alive, which is not the question.

## Why the direction line cannot be the heartbeat

It was asked, and it is worth writing down, because the idea is not silly — a
line that toggles whenever the panel transmits *is* evidence that the panel is
transmitting.

It fails on four counts.

**It proves the wrong thing is alive.** Driven by the UART peripheral — which is
what ESP-IDF's RS485 half-duplex mode does — it toggles whenever the peripheral
has bytes to send. A DMA-fed UART chewing through a queued schedule keeps
toggling perfectly while the application is wedged. That is precisely the
failure the heartbeat exists to catch.

**A pressed STOP cannot do both jobs on one wire.** STOP travels as a command
*and* stops the heartbeat. If the heartbeat is the direction line, stopping it
means stopping transmission — so you cannot send the command you just stopped
the wire to send.

**The rates do not meet.** A 20–50 ms monostable window wants edges at 100 Hz to
1 kHz. A 20 Hz telemetry schedule gives one every 50 ms, so the poll rate would
become safety-critical — and [the link](Link.md) has no spare wire to burn at
128 kbaud.

**It collapses the redundancy.** Three independent mechanisms become two,
sharing one transceiver as a common failure point.

## Three independent ways this bench stops

| | What it catches |
| --- | --- |
| **The STOP command** travels over the link | a deliberate stop, acknowledged and reported |
| **The heartbeat stops** | the panel is wedged, reset, browned out, or unplugged |
| **The coprocessor's own silence watchdog** | the link is dead in either direction |

A pressed STOP uses the first two at once: it sends the command **and** stops
the heartbeat, rather than trusting either alone.

Behind all three sits the panel's own rule, inherited from the predecessor and
proven on hardware: **the application disarms, and refuses to re-arm, if the
touch controller stops answering for 500 ms**, because the panel is the only
place a STOP button exists. A bench you cannot stop is not armed, it is running
away.

And one more, from the same source: **leaving the tester screen disarms.**
Navigating away from an armed bench must not leave a propeller spinning behind
a screen you can no longer see it on.

## The coprocessor does not ask

Overcurrent, over-temperature, stall timeout, lost link: the coprocessor acts on
its own authority and reports what it did at the next poll. It never waits for
permission to fail safe. That is the whole reason the electrical side moved to a
processor with nothing else to do.

## What this cost, and what it bought

The panel no longer drives a servo pulse at all. GPIO6 was the throttle output;
it has one job now, and it is this one. The interlock and the slew limit survive
as policy that compiles into the coprocessor, which is where the output went.

The panel is therefore incapable of spinning a motor by itself — not by
convention, but because there is no wire from it to anything that can.
