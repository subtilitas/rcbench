# Servo procedures

<sub>**English** · [Deutsch](Servo-de.md)</sub>

Two measurements about the servo **as installed** rather than the servo as
sold. Both need a current sensor per servo output, which is not fitted yet —
so both run against a modelled servo today, and against the real one the day
the sensor goes on, with no change in between.

## Finding the installed mechanical limit

### Why you would

Endpoints set by eye in a transmitter are guesses, and the cost of guessing
high is silent: a servo held against a mechanical stop draws stall current for
as long as it is asked to. It cooks itself, empties the pack and wears the
gears, and nobody finds out until something strips.

The limit cannot be looked up, because it does not belong to the servo. It
belongs to the linkage, the horn position and the surface stops — different in
every installation, and different at the two ends of one surface's travel.
Measure both ends.

### What it does

The search walks out from centre a quarter of a degree at a time, settles,
and watches the current. While the surface is free, current is flat and low.
The moment the linkage binds, it climbs steeply — the servo has stopped moving
anything and is simply pushing. The search **stops at the first rise rather
than pushing through it**, backs off by a margin, and reports that as the
endpoint.

Expect a little under **eight seconds per end** at the default settings. Slow
is deliberate: the method depends on *meeting* the stop gently rather than
arriving past it at speed.

### What protects the servo while it runs

| | |
| --- | --- |
| A hard current ceiling | aborts immediately, checked at every sample |
| A stall timeout | current above "working hard" may not persist, wherever it happens |
| The slow approach | one step cannot carry the horn from free to hard against the stop |

All three run on the coprocessor and none of them asks permission first.

### Reading the result

- An endpoint is reported already backed off by a margin — use it as it is.
- **"No limit found"** means the search ran the entire permitted travel
  without binding. That is a real answer, not a failure: the mechanics never
  met a stop inside the range you allowed.
- A limit that appears surprisingly close to centre may be a genuine tight
  spot — a binding bellcrank, a rubbing pushrod — rather than the end of the
  travel. The test guards against the common cases, but a linkage worth
  measuring is a linkage worth looking at.

## Synchronising two servos on one surface

### Why you would

Two servos on one surface — dual ailerons, elevator halves — fight each other
through the surface whenever their travel or their centre disagree, and both
draw extra current continuously to do it. Nothing about the model tells you.
The surface simply sits there, stiff, drawing twice what it should.

### What it does

The point where the two stop fighting is the point of **minimum total
current**, so the search sweeps one correction at a time and finds that
minimum — no judgement involved. The two errors separate cleanly:

- fighting **at centre** is an offset error
- fighting **at the ends** is a travel error

It measures at the centre and at both ends, and each correction falls out of
its own measurement. Each end gets its own number, because a linkage is not
symmetric about centre once a horn and a pushrod are involved.

One current sensor **across the pair** is enough — what is being minimised is
what the two draw together.

### Reading the result

- A pair that was already synchronised still produces a clean minimum — at
  **zero correction**. That is the healthy reading.
- **"No minimum"** means the sensor could not resolve any fight across the
  widest scan. The search says so rather than returning a correction
  assembled out of noise.
- What current cannot see: two servos that agree with each other and are
  *both* wrong. A surface that is stiff nowhere but sits five degrees off has
  nothing for the pair to fight about — that case needs the accelerometer, or
  your eye.

## What both are waiting for

The coprocessor's PWM output and current sensing on the servo outputs — per
output for the limit search, one across the pair for the synchroniser. See
[the record](https://github.com/subtilitas/rcbench#readme) for where those sit
in the order of work.
