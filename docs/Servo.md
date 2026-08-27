# Servo procedures

Two things this bench can do that a transmitter cannot, both of which come
from the same measurement and are both about the servo **as installed** rather
than the servo as sold. Neither is in the catalogue's small set, because both
want a current sensor per servo output.

The first is built. The second is designed and named here so that the shape of
it is on record before it is written.

## Finding the installed mechanical limit

Endpoints set by eye in a transmitter are guesses, and the cost of guessing
high is not a warning. A servo held against a mechanical stop draws stall
current for as long as it is asked to — it cooks itself, empties the pack and
wears the gears, and nobody finds out until something strips.

The stop has a signature. While the surface is free, current against commanded
position is flat and low: it takes almost nothing to hold a balanced surface at
an angle. The moment the linkage binds, current climbs steeply, because the
servo has stopped moving anything and is simply pushing.

So the search walks out from centre in small steps, settles, measures, and
watches for the knee — then **stops at the first rise rather than pushing
through it**, backs off by a margin, and reports that as the endpoint.

It has to be measured in place. The limit belongs to the linkage, the horn
position and the surface stops, not to the servo, so it is different in every
installation and cannot be looked up. That is exactly why a bench that can
measure it is worth having, and why the two ends of one surface's travel are
not mirror images of each other.

### The knee test has two halves

A step counts as bound only when its averaged current beats the free-running
baseline by **both** a ratio and an absolute margin. Either alone misreads a
case that really happens:

| | What it gets wrong on its own |
| --- | --- |
| A ratio alone | A tight spot in a linkage — a bellcrank that binds slightly, a pushrod rubbing where it passes a former — is a large *multiple* of the baseline on a lightly loaded surface while being fifty milliamps. The search would stop at the first one and report it as the end of the travel. |
| A margin alone | A large servo's free current already exceeds any margin chosen for a small one, so the test either never fires or fires immediately, depending on which servo you picked the number for. |

Requiring both costs nothing and removes both. Each half has a test that fails
when that half is deleted.

### The baseline is several positions, not one

A surface with any preload draws most at centre and less a few degrees out. A
baseline taken from that single reading sits high enough to hide a genuine
knee, so the first three positions feed it — and they are three *positions*,
not three readings of one, which is a distinction that survived into the code
only because deleting it made a test go red.

### Three protections, none of which needs the panel

This is the one routine in the project that deliberately drives a servo toward
something solid, which makes it the clearest case for the coprocessor's rule of
protecting hardware without asking.

| | |
| --- | --- |
| **A hard current ceiling** | Aborts immediately, wherever it is crossed — checked every sample rather than at the end of a measurement window, because a servo that hits its stop while still travelling would otherwise push for the whole settle time. |
| **A stall timeout** | Current above "working hard" may not persist. It has a threshold of its own rather than the knee's, and it is measured *across* positions. An earlier version armed it from the knee test, which meant that setting the knee too high to find anything also switched off the timer meant to catch a search that pushes blindly — the one configuration where it was needed was the one where it could not fire. |
| **A slow approach** | Ten microseconds a step, about a quarter of a degree. Small enough that one step cannot carry the horn from free to hard against the stop, because the whole method depends on *meeting* the knee rather than arriving past it. |

A search that runs the whole permitted travel without binding reports that,
rather than returning the travel ceiling as though it were a mechanical limit.

### Why it is a state machine

`servo_limit_step()` is fed one current reading and returns the pulse width to
command. It never sleeps and never touches hardware, so the host suite runs it
against a modelled servo and the coprocessor will run it against a real one
with no difference between the two. The model is in the same directory and
declares itself the way [the telemetry simulator](../README.md) does: nothing
it produces is used to decide anything on real hardware.

The default search covers 600 µs of travel in 10 µs steps at 200 ms a step,
which is a little under eight seconds an end. Slow is the point — but a
procedure nobody will sit through is a procedure nobody runs, so the duration
has a test of its own.

## Synchronising two servos on one surface

Designed, not yet written.

Two servos on one surface — dual ailerons, elevator halves — fight each other
through the surface whenever their travel or their centre disagree, and both
draw extra current continuously to do it. Nothing about the model tells you;
the surface simply sits there, stiff, drawing twice what it should.

So the objective is not a position at all. **The minimum of total current is
the point where they stop fighting**, which makes it a one-dimensional search
with a physical answer rather than a judgement.

And the two errors separate cleanly, which saves searching blindly:

- fighting **at centre** is an offset error
- fighting **at the extremes** is a travel error

Measure at the centre and at both ends and each correction falls out of its own
measurement. The accelerometer adds the one thing current cannot say — whether
the two servos agree with each other and are both wrong, or disagree.

## What both are waiting for

A current sensor per servo output, and the coprocessor's PWM. Neither exists
yet; see [the record](../README.md) for where those sit in the order of work.
