# Servo procedures

<sub>**English** · [Deutsch](Servo-de.md)</sub>

Three procedures on a servo. The first two measure a servo as installed;
both need a current sensor on the servo output, which is not fitted, and both
run against a modelled servo. Their defaults are from `servo_limit_defaults()`
and `servo_sync_defaults()` in `shared/servo/`. The third, the
[automatic test](#automatic-test), reads the current of the supply that feeds
the servo, and has run only against the servo and supply models.

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

## Automatic test

### Purpose

What a servo draws at rest, moving and holding an end, and how long it takes
from end to end, at the supply voltages it is rated for; and the voltage
below which it no longer moves (brown-out). Nothing on the bench measures the
horn, so every result is read from the current of the supply that feeds the
servo: the PD mini (WeAct PD Power Mini V1) when SETUP INTERFACES enables it,
the panel's supply model otherwise. A run on the model says so in its report,
and its numbers are simulated.

START TEST on the SERVO screen's TEST page starts it:
[Screens](Screens.md#servo) describes the controls. The engine is
`shared/servo/servo_test.c`; the constants below are in `servo_test.h`.

### Method

The voltage steps are the TEST page's STEP 4.8 V and STEP 6.0 V, and with HV
SERVO on, STEP 7.4 V and STEP 8.4 V. HV SERVO is off at every restart. A run
with a step above 6.0 V starts only through the HV SERVOS ONLY warning held for
2 s. The current limit through the run is the one on the SET line.

Each step runs these phases:

| Phase | The servo | Ends |
| --- | --- | --- |
| SET | at PULSE CENTRE; the voltage asked; the first step switches the output on | the supply reads the set point back within 0.05 V, at most 3000 ms |
| SETTLE | at PULSE CENTRE | after the TEST page's SETTLE |
| IDLE | at PULSE CENTRE: the idle current is the mean of the readings | after 1000 ms |
| MOVE | a step command to one end, with no slew: SPEED does not apply | at the arrival, or after 3000 ms (late) |
| HOLD | at that end: the holding current is the mean of the readings | after DWELL, at least 600 ms |

The two ends are SWEEP's: RANGE of the travel either side of PULSE CENTRE,
trim not applied. With the defaults (RANGE 80 %, TRAVEL +/-90 deg, PULSE MIN
1000 us, MAX 2000 us) they are 1100 and 1900 us. CURVE and SPEED shape SWEEP
only.

A run that completes also switches the output off and releases the servo to
its centre. A step's first two moves, centre to the low end and on to the
high end, are not counted: they measure each end's holding level. The counted moves then go
end to end, MOVEMENTS of them or for TEST TIME, as LENGTH BY says, at most
1000 a step.

- **Movement:** a reading more than 0.10 A (`SERVO_TEST_MOVE_A`) away from
  the level before the command: above it, or below it when the servo leaves
  an end it was pushing on.
- **Arrival:** after movement, a reading more than 0.10 A above the
  destination end's holding level, then the first reading back within
  0.05 A (`SERVO_TEST_BAND_A`) of it, on either side. A reading that falls
  more than 0.05 A below the level instead, as after a burst of current
  while the servo accelerates, hands the move to the rule below. The two
  ends' holding levels can
  differ by more than 0.05 A, so a reading still at the start end's level,
  or a rising current passing the destination's level, is not an arrival.
- **Arrival at an end held harder than the servo moves**, an end pushing on
  a stop: that level is never passed, so the move has arrived at the first
  of two readings in a row, after movement, within 0.05 A of the level and
  of each other. A current that climbs through the destination's level by
  less than 0.05 A a reading can be taken for an arrival there.
- **Not told apart:** a servo whose moving current lies within 0.05 A of the
  destination's holding current cannot be told from one already there; a
  move to that end is timed at its first two readings.
- **Travel time:** from the command to the arrival's reading.
- **Moving current:** the mean of the readings between the command and the
  arrival; the peak is the highest of them.

**Brown-out.** After the steps, with BROWN-OUT on: from 5.00 V
(`SERVO_TEST_BROWNOUT_START_V`), or the voltage cap in force if lower, down
in 0.20 V steps (`SERVO_TEST_BROWNOUT_STEP_V`) to 3.00 V
(`SERVO_TEST_BROWNOUT_FLOOR_V`), or the supply's lowest set point if higher.
The cap in force is the lowest of VOLTAGE MAX, the supply's own maximum, and
for the PD mini its input voltage less 0.5 V of headroom. A floor off the
0.20 V grid is the last step: on the model, whose lowest set point is
3.3 V, the walk goes 5.0, 4.8 ... 3.4, 3.3 V. Each voltage runs
SET, SETTLE and IDLE, then two moves (`SERVO_TEST_BROWNOUT_MOVES`), centre to
the high end and on to the low end, each held 600 ms. A voltage shows **no
movement** when no reading of either move lies more than 0.10 A from the level
before its command. The walk stops at the first voltage with no movement; the
report gives that voltage and the lowest one the servo moved at.

| Constant | Value | What it is |
| --- | ---: | --- |
| `SERVO_TEST_MOVE_A` | 0.10 A | movement; above the destination's level, not there yet |
| `SERVO_TEST_BAND_A` | 0.05 A | arrived at the holding level |
| `SERVO_TEST_IDLE_MS` | 1000 ms | the idle measurement |
| `SERVO_TEST_HOLD_MIN_MS` | 600 ms | the shortest hold measured |
| `SERVO_TEST_TRAVEL_TIMEOUT_MS` | 3000 ms | a move that has not arrived is late |
| `SERVO_TEST_SET_TOL_V` | 0.05 V | a set point read back |
| `SERVO_TEST_SET_TIMEOUT_MS` | 3000 ms | for the set point, and for the output to come on |
| `SERVO_TEST_STALE_MS` | 1500 ms | no new reading ends the run |
| `SERVO_TEST_STALL_ABORT_MS` | 1000 ms | above STALL AT this long ends the run |
| `SERVO_TEST_BROWNOUT_START_V` | 5.00 V | where the brown-out walk starts |
| `SERVO_TEST_BROWNOUT_STEP_V` | 0.20 V | each step down |
| `SERVO_TEST_BROWNOUT_FLOOR_V` | 3.00 V | the lowest voltage asked |
| `SERVO_TEST_BROWNOUT_MOVES` | 2 | moves at each brown-out voltage |

### Time resolution

A reading counts once. For the PD mini the count is the SUPPLY page's
SAMPLES register: the coprocessor's count of the module's output readings
that answered. A page read whose SAMPLES has not moved brings no new reading,
whatever voltage and current it carries -- a display read on the coprocessor
that was slow (up to 400 ms, `PDMINI_REPLY_MS`) or failed leaves the last
ones standing -- and a run with no new reading for 1.5 s ends. A reading is
stamped with the panel's time of the first page read that showed its count;
the model counts and stamps its own steps. The coprocessor reads the PD
mini's output every 100 ms (`PDMINI_DISPLAY_MS`) and the panel reads the page
every 100 ms (`SUPPLY_LINK_READ_MS`), so a new reading reaches the test every
100 ms at best; the model gives one every 50 ms. When SAMPLES steps by more
than one between two page reads, the readings in between never reach the
test: the report counts them on its `Skipped` line, and the rate taken by the
supply includes them. The report states both rates as measured during the
run, and the mean interval between two readings that reached the test.

A travel time ends at the first reading back at the holding level, so it is
late by up to one interval. It also holds the command's way from the panel
to the pin: render loop, control task, CAN (Controller Area Network) link and
the next PWM (pulse-width modulation) frame. That delay is not measured. How
the PD mini averages a reading is not known.

### What ends a run

Every ending switches the output off, releases the servo to its centre, and
still writes the report, marked ABORTED with the reason. The bench stays armed
unless the ending was a disarm, STOP or leaving the screen, which disarm.
Once a run is over, whichever screen is up, SUPPLY's set points go back to
what they were before it. That waits until the run's OFF has been sent, a
reading taken after that in which the supply itself reports the output off, no ON is on its way and
OUTPUT ON is not being held on SERVO or SUPPLY. Set points changed after
the run ended are
left as they are.

| Reason in the report | Cause |
| --- | --- |
| `STOP` | STOP on the band |
| `bench disarmed` | DISARM, or anything else that disarmed the bench |
| `link lost` | the link to the coprocessor went down during the run |
| `SERVO screen left` | another screen opened |
| `stopped by the operator` | STOP TEST, a finger on the dial, CENTRE, SWEEP, RELEASE, a tap on a set point |
| `servo settings changed` | the type, frame rate, pulses, trim, travel, reverse or SPEED changed |
| `touch events lost` | events went missing between two frames |
| `supply not answering` | a reading marked not answering |
| `supply tripped` | the output went off on a trip |
| `supply output went off` | the output went off otherwise, OUTPUT OFF among them |
| `no new supply reading for 1.5 s` | `SERVO_TEST_STALE_MS` |
| `supply output did not come on` | not on 3000 ms after the ON |
| `set point not read back in 3 s` | the supply did not take a step's voltage |
| `step above the voltage cap` | a step above the cap in force: VOLTAGE MAX, or the PD mini's input less its headroom |
| `above STALL AT for 1 s` | `SERVO_TEST_STALL_ABORT_MS` |

A step is never asked above the cap: a run whose steps lie outside the
supply's range is refused at START, and a cap that comes down during a run
ends it before the step above it.

### Verdict

PASS unless one of these holds, over the voltage steps (the brown-out walk is
reported, not judged):

- the highest idle current is above IDLE CURRENT;
- the highest holding current is above HOLD CURRENT;
- the longest travel time is above TRAVEL TIME;
- a reading after SETTLE is above STALL AT;
- a counted move was late, which a servo that did not move always is.

A LIMITS value of 0 is not checked; STALL AT always is.

### Files

A run takes the next run number on the card, as an armed bench does, and the
SD card's own task writes its files: `BENCHnnn.CSV`, one row per supply
reading, and with the DUT page's REPORT on, `BENCHnnn.TXT`. An armed bench's
own run log goes on beside it in a file of its own. A number carried by a
`BENCHnnn.TXT` alone, its CSV deleted on a computer, is taken all the same,
so no report is overwritten, and the log viewer's DELETE on a run removes its
report with it. A run the card cannot
take says so on the band and on the result, `NOT RECORDED`. Rows the queue to
the card had no room for are counted in the report.

The CSV, semicolon-separated with a decimal point, as every log the bench
writes:

| Column | Unit | What it is |
| --- | --- | --- |
| `time (s)` | s | when the reading was taken, from the run's start |
| `test` | | `STEP` or `BROWN-OUT` |
| `step` | | the step, 1 to n in the order run |
| `phase` | | `SET`, `SETTLE`, `IDLE`, `MOVE` or `HOLD` |
| `command (us)` | us | the pulse commanded |
| `position (us)` | us | the measured position; empty, as nothing measures it |
| `set (V)` | V | the step's voltage |
| `voltage (V)` | V | at the output |
| `limit (A)` | A | the current limit |
| `current (A)` | A | out of the output |
| `power (W)` | W | voltage times current |
| `mode` | | `CV`, `CC` or `OFF` |
| `travel (ms)` | ms | on an arrival's row: that move's travel time |

The report, from a run against the servo and supply models in the host
suite:

```
RCBENCH SERVO TEST REPORT
Result:         PASS
Device:         DS3218 #2
Firmware:       rcbench 0.11.0
Log:            the .CSV with this file's number, one row per supply reading
Supply:         PD mini
Readings:       10.0 /s taken by the supply, 10.0 /s reached the test
Skipped:        0 readings the supply took never reached the test
Resolution:     one reading every 100 ms: a travel time is late by up to that
Duration:       46.5 s
Log rows:       466 written, 0 lost to a full queue

SETTINGS IN FORCE
Type:           STANDARD PWM, centre 1500 us, 1000-2000 us, trim +0 us, reverse OFF
Frame rate:     50 Hz
Can destroy:    none in force
HV servo:       OFF, no step above 6.0 V
Ends:           1100 us and 1900 us (RANGE 80 % of TRAVEL +/-90 deg)
Steps:          4.80 V 6.00 V
Brown-out:      from 5.00 V down in 0.20 V steps to 3.30 V
Current limit:  3.00 A
Timing:         settle 500 ms, idle 1000 ms, dwell 200 ms (hold measured 600 ms)
Length:         4 movements a step
Limits:         idle 0.30 A, holding 0.50 A, travel 800 ms, stall 2.00 A

RESULTS PER STEP (currents in A, times in ms)
Set V  Meas V  Idle   Moving Peak   Hold lo Hold hi Travel Longest Moves Late
 4.80    4.78  0.123  0.949  0.958  0.119   0.121   700    700         4    0
 6.00    5.98  0.122  0.954  0.960  0.122   0.120   700    700         4    0
Late: moves not back at the holding level within 3000 ms.

BROWN-OUT
Moved at 4.20 V; no movement at 4.00 V.
No movement: no reading of a move 0.10 A away from the level before it.

AGAINST THE LIMITS PAGE
Idle current     highest 0.123 A, limit 0.30 A: PASS
Holding current  highest 0.122 A, limit 0.50 A: PASS
Travel time      longest 700 ms, limit 800 ms: PASS
Stall threshold  highest 0.960 A, STALL AT 2.00 A: PASS
Moves arrived    0 late: PASS

NOT MEASURED
Position: nothing measures the horn; every result is the supply's current.
Current peaks between two readings: the supply reports one value a reading.
The command's way from the panel to the pin, inside every travel time.
```

The modelled servo travels 800 us in 667 ms; the report's 700 ms is that,
late by the 100 ms between readings. An aborted run reads `Result:
ABORTED - <reason>`, and a step it cut short is marked `(cut short)`; one it
never reached reads `not run`. `Can destroy` names the red tag when a heli
type or a frame rate above 60 Hz is in force. Every word of the report is in
one table in `shared/servo/servo_report.c`, and the report is written in the
interface language showing when its run starts: German from
`shared/ui/ui_text_de.c` ([Interface language](Language.md)). The CSV is
English in every language.

### Not run on hardware

No run has driven a servo or a PD mini. What the host suite holds it to is
the engine against `servo_sim` and `supply_sim`, the SERVO screen driving it,
and the CSV read back by the log viewer's parser. Not measured: the readings'
real rate through the coprocessor, the PD mini's averaging, the command's
delay to the pin, and whether a real servo's current falls back to its
holding level within 0.05 A.

## Prerequisites

Current sensing on the servo outputs: one sensor per output for the limit
search, one across the pair for the synchroniser. Neither is fitted, and both
procedures are the reason they are wanted -- every number here is a current.
The automatic test needs neither: it reads the PD mini, wired and enabled in
SETUP INTERFACES, or runs on the panel's supply model.

The pulses themselves exist: the coprocessor's PWM (pulse-width modulation)
driver is written, and [DShot and the output drivers](DShot.md) describes it.
The order of work is in
[STATUS.md](https://github.com/subtilitas/rcbench/blob/main/STATUS.md).
