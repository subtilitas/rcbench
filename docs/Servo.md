# Servo procedures

<sub>**English** · [Deutsch](Servo-de.md)</sub>

Two measurements on a servo as installed. Both need a current sensor on the
servo output, which is not fitted; both run against a modelled servo. The
defaults below are from `servo_limit_defaults()` and `servo_sync_defaults()` in
`shared/servo/`.

## Installed mechanical limit

### Purpose

Endpoints set in a transmitter are estimates. A servo held against a mechanical
stop draws stall current for as long as it is commanded there, and the
installation, not the servo, determines where the stop is. The two ends of a
surface's travel differ; measure both.

### Method

The search steps outward from centre in 10 µs of pulse width per step (about 0.9° on a servo with 11 µs per degree of travel), up to 600 µs from centre. After each step it waits
120 ms for the servo to settle and averages the current over 80 ms. While the
surface moves freely the current is flat and low. The linkage counts as bound
when the current exceeds 1.8 times the free-running baseline and exceeds it by
at least 0.15 A. The search stops at the first such step, backs off 25 µs, and
reports that pulse width as the endpoint.

Duration: the host test holds a full search against the modelled servo to under
12 s. Not measured on hardware.

### Protections

| Protection | Value | Effect |
| --- | ---: | --- |
| Current ceiling | 3.0 A | abort immediately, checked at every sample |
| Stall timeout | above 1.0 A for 400 ms | abort |
| Step size | 10 µs | one step cannot move the horn from free to hard against the stop |

All three run on the coprocessor.

### Results

- An endpoint is reported already backed off by 25 µs.
- "No limit found" means the search reached 600 µs from centre without binding.
- A limit close to centre may be a tight spot in the linkage, such as a binding
  bellcrank or a rubbing pushrod, rather than the end of travel.

## Synchronising two servos on one surface

### Purpose

Two servos on one surface (dual ailerons, split elevator) work against each
other whenever their travel or their centre disagree, and draw extra current
continuously. The surface shows no visible sign.

### Method

The two stop working against each other at the point of minimum total current.
The search holds servo A and sweeps a correction on servo B, at centre and at
±300 µs deflection:

- a difference at centre is an offset error (trim);
- a difference at an end is a travel error for that end.

Each scan covers ±40 µs around the current best point in 7 steps and narrows
three times. Each point waits 120 ms plus the travel time at an assumed 0.8
µs/ms, then averages the current over 100 ms. A minimum is accepted when the
current varies by at least 0.08 A across the scan. The current ceiling is 4.0 A
for the pair.

Each end gets its own correction; a linkage with a horn and a pushrod is not
symmetric about centre. One current sensor across the pair is sufficient.

### Results

- A pair that is already synchronised produces a minimum at zero correction.
- "No minimum" means the current varied by less than 0.08 A across the widest
  scan.
- Two servos that agree with each other and are both wrong produce no
  difference in current. That case needs the accelerometer or inspection.

## Prerequisites

The coprocessor's PWM (pulse-width modulation) output and current sensing on
the servo outputs: one sensor per output for the limit search, one across the
pair for the synchroniser. The order of work is in
[STATUS.md](https://github.com/subtilitas/rcbench/blob/main/STATUS.md).
