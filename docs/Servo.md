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
| IDLE | at PULSE CENTRE: the idle current is the mean of the readings, the idle noise their standard deviation | after 1000 ms |
| MOVE | a step command to one end, with no slew: SPEED does not apply | at the arrival, or after 3000 ms plus the meter's lag (3300 ms on the PD mini): late with movement seen, unseen without |
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

- **Threshold:** per step, the larger of 0.020 A (`SERVO_TEST_MOVE_MIN_A`)
  and 3 (`SERVO_TEST_NOISE_K`) times the idle noise, the standard deviation
  of the IDLE readings. The report gives it in the `Thresh` column. A
  servo whose readings at rest spread by 0.010 A has a threshold of
  0.030 A.
- **Movement:** a reading more than the threshold away from the level
  before the command: above it, or below it when the servo leaves an end
  it was pushing on.
- **Arrival:** after movement, a reading more than the threshold above the
  destination end's holding level, then the first reading back within
  0.05 A (`SERVO_TEST_BAND_A`) of it, on either side. A reading above the
  level by the threshold is never an arrival, so the 0.05 A band times no
  move early where the threshold is smaller. A reading that falls more
  than 0.05 A below the level instead, as after a burst of current while
  the servo accelerates, hands the move to the rule below. The two ends'
  holding levels can differ by more than 0.05 A, so a reading still at the
  start end's level, or a rising current passing the destination's level,
  is not an arrival.
- **Arrival at an end held harder than the servo moves**, an end pushing on
  a stop: that level is never passed, so the move has arrived at the first
  of two readings in a row, after movement, within 0.05 A of the level and
  of each other. A current that climbs through the destination's level by
  less than 0.05 A a reading can be taken for an arrival there.
- **Not told apart:** a servo whose moving current lies within 0.05 A of the
  destination's holding current cannot be told from one already there; a
  move to that end is timed at its first two readings after movement.
- **Window:** a move has 3000 ms (`SERVO_TEST_TRAVEL_TIMEOUT_MS`) plus
  the meter's lag to arrive: 3300 ms on the PD mini, whose readings show an
  arrival about 300 ms after it happens. The lag is added rather than the
  window's last 300 ms left unjudged: a servo arriving at 2900 ms shows it
  at about 3200 ms and is timed, and a move that never arrives is still
  late, 300 ms later. A reading at or past the window's end, 3300 ms on
  the PD mini, is never an arrival: the move is late.
- **Unseen:** a counted move with no reading past the threshold within
  the window. The current cannot tell it from a servo standing still: it is
  neither timed nor late, and the report counts it in the `Unseen` column.
  A step none of whose moves showed movement reads `NOT MEASURABLE`.
- **Travel time:** from the command to the arrival's reading.
- **Moving current:** the mean of the readings between the command and the
  arrival; the peak is the highest of them.

Why the band stays 0.05 A while the threshold scales: replays of the two
bench runs below with the band at 0.05 A and with the band equal to the
threshold time the same moves to the same millisecond, because a reading
above the level by the threshold is never an arrival. The threshold is what
had to scale. 0.13.0 took movement to be 0.10 A, and the MG90S moves at 0.04
to 0.077 A over a 0.001 A hold: no move was seen, each ran out at 3000 ms and
was counted late, and the run read FAIL.

**Limitation: the move to the centre can reach IDLE.** With SETTLE at
500 ms the IDLE readings can still carry the end of the step's move to the
centre: 0.025 A falling to 0.003 A on the 1102HB at 5.00 V, from the PD
mini's 0.3 s lag and the move itself. That raises the idle mean and the
noise, and the threshold with it: 0.026 A there instead of 0.020 A. A
longer SETTLE avoids it.

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
movement** when no reading of either move lies more than that voltage's
threshold, from its own IDLE readings, from the level before its command.
The walk stops at the first voltage with no movement; the report gives that
voltage, the lowest one the servo moved at, and the threshold at the last
voltage run. No movement at the first voltage, 5.00 V, reads `not
measurable`: a servo that never showed movement may move under the
threshold.

| Constant | Value | What it is |
| --- | ---: | --- |
| `SERVO_TEST_MOVE_MIN_A` | 0.020 A | the smallest threshold: movement; above the destination's level, not there yet |
| `SERVO_TEST_NOISE_K` | 3 | the threshold in idle noises, where that is larger |
| `SERVO_TEST_BAND_A` | 0.05 A | arrived at the holding level |
| `SERVO_TEST_PDMINI_LAG_MS` | 300 ms | the PD mini's lag, as the report states it |
| `SERVO_TEST_IDLE_MS` | 1000 ms | the idle measurement |
| `SERVO_TEST_HOLD_MIN_MS` | 600 ms | the shortest hold measured |
| `SERVO_TEST_TRAVEL_TIMEOUT_MS` | 3000 ms | a move that has not arrived is late, after this plus the meter's lag |
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

**The PD mini's readings lag and repeat.** Two runs on the bench with the PD
mini on 0.13.0, an MG90S and a 1102HB, read it every 102 to 106 ms. The
median time from a command to the first reading 0.02 A above the level
before it is 0.31 s, and a current often repeats over several readings.
Every travel time read off the PD mini is therefore an upper bound: on the
MG90S 771 to 989 ms for a servo rated about 0.1 s per 60 deg. The report
says so on its `Lag`, `Repeats` and `Travel times` lines, and TRAVEL TIME
cannot fail a run on the PD mini: its line gives the longest travel time and
reads `upper bound, not checked against the limit`. What reads the current
is described to the run (`servo_test_meter_t`: a name, the lag, whether
readings repeat, whether travel times are an upper bound), so a faster
current sensor sets its own and has TRAVEL TIME checked. A run on the
panel's model states no lag of its own and checks TRAVEL TIME as the PD mini
would: not at all.

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

Over the voltage steps (the brown-out walk is reported, not judged), FAIL
when one of these holds:

- the highest idle current is above IDLE CURRENT;
- the highest holding current is above HOLD CURRENT;
- the longest travel time is above TRAVEL TIME, where the current's meter
  times travel (not the PD mini);
- a reading after SETTLE is above STALL AT;
- a counted move was late: movement seen, no arrival within the window,
  3000 ms plus the meter's lag.

Otherwise NOT MEASURABLE when a counted move was unseen, or when the
brown-out walk saw no movement at its first voltage, and PASS when neither
holds. A run of the brown-out walk alone that sees nothing reads NOT
MEASURABLE, not PASS. Its `Result` line then reads `no movement seen at
5.00 V, the brown-out walk's first voltage`, and `Brown-out start` under
the limits gives the same voltage with NOT MEASURABLE. NOT MEASURABLE says the current could not show every move: a servo
moving under the threshold and one standing still read alike. The report's
`Result` line gives how many of the counted moves showed no movement, and
`Moves seen` how many were unseen. A servo that does not move reads NOT
MEASURABLE, not FAIL. With no move arrived there is no longest travel time,
and the `Travel time` line reads `longest --` and `not measured, no move
arrived`, on any meter.

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

The report from the host suite's replay of an MG90S micro servo's run on
the bench with the PD mini (`test/host/fixtures/servo-mg90s.csv`), with
TRAVEL TIME set to 800 ms:

```
RCBENCH SERVO TEST REPORT
Result:         PASS
Device:         MG90S
Firmware:       rcbench 0.14.0
Log:            the .CSV with this file's number, one row per supply reading
Supply:         PD mini
Readings:       9.1 /s taken by the supply, 9.1 /s reached the test
Skipped:        0 readings the supply took never reached the test
Resolution:     one reading every 109 ms: a travel time is late by up to that
Lag:            about 300 ms from a change of current to the reading that shows it
Repeats:        a reading can repeat the last value for several readings
Travel times:   an upper bound, not checked against the limit
Duration:       184.0 s
Log rows:       1684 written, 0 lost to a full queue

SETTINGS IN FORCE
Type:           STANDARD PWM, centre 1500 us, 1000-2000 us, trim +0 us, reverse OFF
Frame rate:     50 Hz
Can destroy:    none in force
HV servo:       OFF, no step above 6.0 V
Ends:           1100 us and 1900 us (RANGE 80 % of TRAVEL +/-90 deg)
Steps:          4.80 V 6.00 V
Brown-out:      from 5.00 V down in 0.20 V steps to 3.00 V
Current limit:  2.00 A
Timing:         settle 500 ms, idle 1000 ms, dwell 200 ms (hold measured 600 ms)
Length:         60 s a step
Limits:         idle OFF, holding OFF, travel 800 ms, stall 2.00 A

RESULTS PER STEP (currents in A, times in ms)
Set V  Meas V  Idle   Thresh Moving Peak   Hold lo Hold hi Travel Longest Moves Late Unseen
 4.80    4.80  0.004  0.020  0.037  0.065  0.001   0.001   861    989        41    0      0
 6.00    6.00  0.001  0.020  0.040  0.077  0.001   0.001   892    978        41    0      0
Thresh: movement is a reading max(0.020 A, 3 x idle noise) from the level before the command.
Arrival: after a reading Thresh above the end's holding level, the first back within 0.05 A of it.
Late: moves seen moving that did not arrive within 3300 ms.
Unseen: moves with no movement seen; not timed, not counted late.

BROWN-OUT
Moved at every step down to 3.00 V; lower not tested.
No movement: no reading of a move more than Thresh, 0.023 A at 3.00 V, from the level before it.

AGAINST THE LIMITS PAGE
Idle current     highest 0.004 A, limit OFF: not checked
Holding current  highest 0.001 A, limit OFF: not checked
Travel time      longest 989 ms, limit 800 ms: upper bound, not checked against the limit
Stall threshold  highest 0.077 A, STALL AT 2.00 A: PASS
Moves arrived    0 late: PASS
Moves seen       0 unseen: PASS
Brown-out start  movement seen at 5.00 V: PASS

NOT MEASURED
Position: nothing measures the horn; every result is the supply's current.
Current peaks between two readings: the supply reports one value a reading.
The command's way from the panel to the pin, inside every travel time.
```

Every move is seen at the 0.020 A threshold and arrives; the brown-out
walk replays the 5.00 V response at every voltage, so it says nothing about
this servo below 5.00 V. The same replay of a 1102HB digital servo, which
holds 0.015 to 0.029 A and peaks at 0.039 to 0.044 A, reads:

```
Result:         NOT MEASURABLE - 25 of 46 counted moves showed no movement in the current
...
Set V  Meas V  Idle   Thresh Moving Peak   Hold lo Hold hi Travel Longest Moves Late Unseen
 4.80    4.80  0.015  0.020  0.025  0.037  0.028   0.015   687    772        22    0     13
 6.00    5.99  0.003  0.020  0.027  0.042  0.029   0.017   636    688        24    0     12
...
No movement seen at 5.00 V, the first step: not measurable.
```

Its moves to the high end leave the low end's 0.028 A and never pass it by
0.020 A: all of them are unseen. Its moves to the low end are seen and
arrive. On 0.13.0 the same servo read FAIL with all 34 counted moves late.

An aborted run reads `Result:
ABORTED - <reason>`, and a step it cut short is marked `(cut short)`; one it
never reached reads `not run`. `Can destroy` names the red tag when a heli
type or a frame rate above 60 Hz is in force. Every word of the report is in
one table in `shared/servo/servo_report.c`, and the report is written in the
interface language showing when its run starts: German from
`shared/ui/ui_text_de.c` ([Interface language](Language.md)). The CSV is
English in every language.

### Not run on hardware

Three runs of 0.13.0 on the bench with the PD mini, from a tester, are the
only hardware runs: an MG90S micro servo, a 1102HB digital and an MS24
digital. The MS24, moving at 0.16 to 0.18 A, passed. The MG90S and the
1102HB read FAIL, every move late, because 0.13.0 took movement to be
0.10 A; both servos moved. The host suite replays the MG90S's and the
1102HB's CSVs (`test/host/fixtures/`, trimmed) against the engine, and an
MS24-like case, the MG90S's currents times 3. The threshold and the
NOT MEASURABLE verdict have not run on hardware. Beyond that the host suite
holds the engine to `servo_sim` and `supply_sim`, the SERVO screen driving
it, and the CSV read back by the log viewer's parser. Not measured: the PD
mini's averaging, and the command's delay to the pin.

## Sweep and SPEED

SWEEP on the SERVO screen drives the servo through the TEST page's curve.
SPEED on the right card limits how fast the output may move. When SPEED is
slower than the fastest change the curve asks for, SPEED shapes the motion
instead of the curve, and SPEED's row reads SPEED LIMITS THE SWEEP in the
warning colour ([Screens](Screens.md#servo)).

The fastest change, with f the TEST page's rate in Hz and A the amplitude in
degrees (RANGE of the travel either side of PULSE CENTRE):

| CURVE | Fastest change | Where |
| --- | --- | --- |
| square | a jump | at each change of end |
| sine | 2 pi f A | through the centre |
| triangle | 4 f A | throughout |

SPEED below 100 % allows 3.6 deg/s per percent: 36 deg/s at 10 %, 356.4
deg/s at 99 %. At 100 % the command is not slewed and nothing is limited. A
square is limited at every SPEED below 100 %. The dwell does not enter: it
adds time at the ends, not to the motion. The line follows the settings,
before SWEEP is pressed and while it runs. `sweep_slew_limited()` in
`shared/servo/servo_sweep.c` decides it, in the command units the
coprocessor slews in.

| TEST page | A | Fastest change | Clears at SPEED |
| --- | ---: | ---: | ---: |
| sine, 0.5 Hz, RANGE 80 %, TRAVEL +/-90 deg | 72 deg | 226 deg/s | 63 % |
| triangle, 0.5 Hz, RANGE 80 %, TRAVEL +/-90 deg | 72 deg | 144 deg/s | 40 % |
| square, any | any | a jump | 100 % |

To clear the line, raise SPEED, or lower the TEST page's SPEED (the rate) or
RANGE.

The line compares the curve with SPEED only. A servo slower than both limits
the sweep as well; nothing on the bench measures the horn, so that is not
shown.

PAUSE, the sweep button while a sweep runs, holds the output where it has
got to, and the button reads PAUSED, filled in the warning colour. A tap on
PAUSED carries the sweep on from the phase it was paused at, on a
coprocessor speaking protocol 4.6; an older one starts the curve over. A
paused sweep is the moment to raise SPEED: the pause stays, and the resume
runs at the new rate.

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
