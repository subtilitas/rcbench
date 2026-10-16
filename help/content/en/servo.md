---
screen: SERVO
title: Servo
order: 20
---

# Servo

SERVO drives a servo, measures its current on the servo rail, runs an automatic test across supply voltages and searches the travel limits of the installed servo. It opens from the SERVO tile on the [menu](overview.md). The dial takes the left card; the readings, the supply line, SPEED, the target and the buttons take the right card.

## At rest

![SERVO while SAFE](view:servo)

While the bench is SAFE the horn is drawn dimmed and the left card reads `ARM FIRST`. The dial takes no position. The coprocessor writes a pulse of length zero to every servo PWM (pulse-width modulation) pin.

The right card, from the top:

| Row | Shows |
|---|---|
| COMMANDED | the pulse width the screen commands, in us |
| MEASURED | the horn's angle from the AS5600 output encoder while it is on and sees its magnet, -180.0 to +179.9 deg; `---` otherwise |
| CURRENT | the servo rail's current from its meter, in hundredths of an amp |
| type row | the servo type, frame rate and pulse range in force: `STANDARD PWM  50 Hz  1000-2000` |
| meter line and plot | voltage, current and power of the rail, plotted over the last 13 s |
| SET | the supply's voltage and current limit, and its output switch |
| SPEED | the rate the bench may move the output, 100 % passes the command straight through |
| TARGET | the pins the tester drives: `GP2`, `PROG PORT GP13`, or a warning |

## ARMED

![SERVO while ARMED SERVO](view:servo-armed)

1. Hold ARM for 2 s. The band then shows ARMED SERVO, and the button reads DISARM.
2. Drag anywhere on the sweep to command a position. The solid arm is the measured position, the faint arm the commanded one.
3. Lifting the finger does not let go: the screen repeats the last position, so the servo stays where it was put.
4. Tap DISARM, press STOP or leave the screen to switch to SAFE.

ARMED SERVO drives the servo outputs and a pulsing PROG PORT. Throttle outputs get no pulse.

| Control | SAFE | ARMED |
|---|---|---|
| The dial | refused | commands the position under the finger |
| Rotary knob | refused; the turn is not kept | moves the horn from the commanded value |
| CENTRE | refused; drawn dimmed | commands PULSE CENTRE plus TRIM |
| SWEEP, PAUSE, PAUSED | refused; drawn dimmed | starts, pauses and resumes the sweep |
| RELEASE | sends the release | returns the surfaces to their rest: 1500 us for STANDARD PWM, 760 us for NARROW 760 |
| SPEED | set | set; a held position is sent again at the new rate |
| START TEST | refused: `ARM FIRST` | starts the automatic test after its hold |

The switch to ARMED sets the commanded value to the rest, the midpoint of the channel's end points, without TRIM. The first drag or knob turn starts from there. SWEEP is grey without a bound surface, without the link, or with a coprocessor older than protocol 4.2.

## The servo rail's meter

The line over the plot names the meter of the servo rail:

| Name | Meter |
|---|---|
| `INA3221 CH1` | CH1 of the INA3221: the mean current and voltage of its last 50 ms window, 20 points a second |
| `PD MINI` | the PD mini supply's last sample, or the panel's supply model |

![The INA3221 as the rail's meter](view:servo-ina)
![A clipped window, in the warning colour](view:servo-clipped)

The INA3221 becomes the meter 1000 ms after its conditions hold: it is enabled on SETUP, INTERFACES with CH1, the coprocessor speaks protocol 4.11, and windows with current samples arrive. A window in which a sample hit the end of the range, 1.638 A on a 0.1 ohm shunt, shows in the warning colour: its mean is then a bound.

![The panel's supply model as the meter, with the SIMULATED pill](view:servo-model)

While the PD mini is disabled on SETUP, INTERFACES, the supply is the panel's model, and a SIMULATED pill sits on the plot. Its readings are simulated, not measured.

A reading that did not arrive is `---` in the CURRENT row, `--` on the line and a gap in the plot.

## The supply on SERVO

The SET line holds the SUPPLY screen's two set points and its output switch. A change on either screen is the change on both.

- A tap on a set point opens the keypad over the left card.
- OUTPUT ON is a 2 s hold, OUTPUT OFF a tap.
- Leaving SERVO switches off an output SERVO switched on.

![HV SERVOS ONLY](view:servo-hv)

Standard servos are rated for 4.8 to 6.0 V. A voltage raised from 6.0 V or below to above 6.0 V opens HV SERVOS ONLY. The voltage is applied after HOLD TO APPLY is held for 2 s; CANCEL drops it. OUTPUT ON with a set point above 6.0 V opens the same warning.

## SWEEP and PAUSE

SWEEP moves the servo along the TEST page's CURVE (square, sine or triangle) at its SPEED, 0.05 to 5 Hz, across RANGE, 10 to 100 % of the travel. The coprocessor times the curve, so the link's timing does not shape it. While a sweep runs the button reads PAUSE. A tap holds the servo where it is and the button reads PAUSED; a tap on PAUSED carries the sweep on from that point.

![A paused sweep](view:servo-paused)

![SPEED LIMITS THE SWEEP](view:servo-sweep)

`SPEED LIMITS THE SWEEP` replaces SPEED's label, in the warning colour, while SPEED is slower than the fastest change the curve asks for. The curve then moves as ramps. Raise SPEED, or lower the TEST page's SPEED or RANGE. With the TEST page's defaults (sine, 0.5 Hz, RANGE 80 %) and TRAVEL 90 deg each way it clears at SPEED 63 %.

A finger on the dial, CENTRE, RELEASE, STOP, a switch to SAFE and leaving the screen end a sweep.

## The automatic test

START TEST on the TEST page steps the servo through the supply voltages chosen there. At each step it measures the current at rest, moving and holding, times the travel, and finds the voltage at which the servo stops moving. START TEST is a 2 s hold. It needs the bench ARMED and a supply that answers; otherwise the line under it names the reason: `ARM FIRST`, `NO STEP CHOSEN`, `SUPPLY NOT ANSWERING`, `RANGE TOO SMALL`, `A STEP IS OUTSIDE THE CAPS` or `LAST REPORT STILL WRITING`.

![A test run](view:servo-run)

The settings close as the run starts. Its step and phase show at the top of the left card, with the meter beside STOP TEST: `PD mini`, `INA3221 CH1` or `SIMULATION`. The run ends early, the output off and the servo released, on STOP TEST, STOP, a switch to SAFE, a lost link, leaving the screen, a finger on the dial, CENTRE, SWEEP, RELEASE, a tap on a set point, or a change to the servo's settings. A run on the INA3221 also ends when the INA3221 stops being the meter, or when no window with both current and voltage arrives for 200 ms (4 windows of 50 ms): `no INA3221 window for 0.2 s`.

![A result](view:servo-result)
![A result read from the INA3221](view:servo-result-ina)

The result stays until CLOSE: PASS, FAIL or ABORTED and why, the longest travel time, the highest holding current, and the files the card took, `BENCHnnn.CSV` and `+ .TXT` for the report.

## The limit run

The limit run finds where the installed servo binds at each end of its travel. It can then lower the servo's own LEFT ANGLE and RIGHT ANGLE, so the servo stops short of the binding point. It is on the [LIMITS page](servo-settings.md#limits) of the settings.

![The limit run, ready](view:servo-limrun-ready)

1. Tap LIMIT RUN to choose SEARCH ONLY or SEARCH + PROGRAM.
2. With PWM TARGET on PROG PORT, tap SERVO PULSES to choose KEEP or 1000-2000. It starts at KEEP.
3. Hold START LIMIT RUN for 2 s while the bench is ARMED. A tap or a shorter hold starts nothing.
4. The settings close. The box on the left card shows the phase, the side and the pulse, and the output it drives.

The line under the button names the output and the meter, the whole time: `OUTPUT PROG PORT GP13, CURRENT INA3221 CH1`. The TARGET row stays in sight as well.

The run reads only INA3221 CH1. With the PD mini or the model as the servo rail's meter, START LIMIT RUN stays grey and the line says why:

| Line | Condition |
|---|---|
| `ARM FIRST` | the bench is SAFE |
| `INA3221 NOT SET UP TO MEASURE SERVO SUPPLY CURRENT` | the INA3221 or its CH1 is off on SETUP, INTERFACES |
| `INA3221 OFFLINE` | no link, or the INA3221 does not answer |
| `NO INA3221 MEASUREMENT WINDOWS YET` | the INA3221 is not the meter yet |
| `SERVO SUPPLY BELOW 3.0 V OR NOT READ` | the last INA3221 window has no rail voltage, or less than 3.0 V: an output that is off reads 0 V |
| `PORT PWM OFF. NO PULSES ON THIS PORT.` | PWM TARGET is PROG PORT with PORT PWM off |
| `NO SERVO PWM PIN`, `PINS NOT READ` | PWM TARGET is the bench's outputs, and no output is bound as SERVO PWM, or the binding is not read yet |
| `CENTRE MUST BE BETWEEN PULSE MIN AND MAX` | the pulse range does not hold PULSE CENTRE |
| `LAST REPORT STILL WRITING` | a test or a run is under way, or its report is still going to the card |

### The search

The search steps outward from the centre, the low side first, 10 us a step. Each position takes 250 ms: 120 ms to settle, then the mean of the next windows. An end binds when the current exceeds 1.8 times the free current and exceeds it by at least 0.15 A. The endpoint is reported 25 us back from the binding position. The bench's own PULSE MIN and PULSE MAX are not changed.

| PWM TARGET | Search |
|---|---|
| a SERVO PWM pin | from PULSE CENTRE out to PULSE MIN and PULSE MAX; nothing is read or written |
| PROG PORT | a read session first, then from the servo's NEUTRAL out to its own PULSE LOWER and PULSE UPPER, never past them |

The read session holds the servo at PULSE CENTRE, switches the bench to SAFE, reads the servo with CONNECT, gives the port back with LEAVE and switches the bench to ARMED. Nothing is driven before it. The run ends as FAIL before any search when:

- the read fails (`no KST or Chaservo servo read`, or `programming port did not finish`): search a servo that is no KST or Chaservo on a SERVO PWM pin;
- NEUTRAL is not between PULSE LOWER and PULSE UPPER (`NEUTRAL outside LOWER/UPPER`);
- PULSE LOWER to PULSE UPPER is not inside PULSE MIN to PULSE MAX (`servo pulses outside MIN/MAX`). The bench stays SAFE, and the box names both ranges.

![The servo's pulses outside PULSE MIN and PULSE MAX](view:servo-limrun-range)

A KST servo as delivered holds PULSE LOWER 900.0 us and PULSE UPPER 2100.0 us. The STANDARD PWM type's range is 1000-2000 us, so that servo is refused at KEEP. Choose SERVO PULSES 1000-2000, or a PWM type whose range holds 900 to 2100 us.

SERVO PULSES at 1000-2000 writes PULSE LOWER 1000.0 us and PULSE UPPER 2000.0 us in the read session, before anything moves, and reads them back. NEUTRAL is not written. A servo at those values already gets no write. With SEARCH ONLY these two values are the only write.

### Programming LEFT ANGLE and RIGHT ANGLE

SEARCH + PROGRAM needs a KST or Chaservo servo on the programming port: PWM TARGET on PROG PORT, the port in PWM, and 4.8 V or more. With a PD mini, its output must be on and its set point counts; without one, the servo rail in the INA3221's last window counts. On the bench's outputs LIMIT RUN reads SEARCH ONLY and the third line says why. On PROG PORT every run starts with a read session: a port not in PWM, a PD mini output that is off, or a supply under 4.8 V keeps the button grey for SEARCH ONLY as well, and the third line names the condition.

![The limit run, programming](view:servo-limrun)

The run writes in sessions of the [servo programmer](servo-programmer.md):

1. The servo goes back to the centre for 1000 ms.
2. The run switches the bench to SAFE.
3. CONNECT reads the servo.
4. One write, with every limit check of the programmer.
5. LEAVE gives the port back to PWM.
6. The run switches the bench to ARMED and searches that side again.

The first write saves the servo's values to a backup file on the card. The run takes the servo as linear: PULSE LOWER and PULSE UPPER are the pulses at which it reaches the full travel that LEFT ANGLE and RIGHT ANGLE set. The share of a side at pulse p is `(p - NEUTRAL) / (PULSE LOWER or UPPER - NEUTRAL)`. The unit of LEFT ANGLE and RIGHT ANGLE is not known, and the linear relation is an assumption.

Each bound side gets two writes, v0 the value its register held:

1. The overset: v0 x the share at the first bound pulse x 1.03, rounded up. The stop stays reachable, close to the pulse limit. The run searches that side again. Free there, the run ends as FAIL, `no stop after the overset write`.
2. The final value: the overset x the share at the last free pulse of that search, rounded down.

Example: NEUTRAL 1500.2 us, PULSE LOWER 900.0 us, v0 150, the first search free at 1000 us and bound at 990 us. The overset is 150 x 0.850 x 1.03 = 132. At 132 the search is free out to 930 us; the final value is 132 x 0.950 = 125.

Near the pulse limit, where the overset would move the stop less than 20 us, the run writes the final value at once: v0 x the share at the first search's last free pulse. The first bound side's first write also finds which register limits that side. Last, the run searches both ends again: PASS when both are free, FAIL when one binds. A run writes 8 values at most: the two pulses, two on each side, and two to find the register. A value below 50 is a soft limit: the 2 s start hold is the consent to it.

### What ends a limit run

Each of these ends the run as ABORTED, with the output off and the servo released:

- STOP RUN, STOP, a switch to SAFE, a lost link, leaving the screen, a change to the servo's settings;
- the INA3221 no longer the meter, or no INA3221 window for 200 ms (`no INA3221 window for 0.2 s`);
- during a search, a window without the rail voltage (`no rail voltage in a window`), or with its mean under 3.0 V, the first window included (`supply or rail voltage changed`);
- a sample at 3.0 A or more, a sample at the end of the INA3221's range, or window means above 1.0 A for 400 ms;
- a search longer than 40 s, a bench that does not switch in 3 s, or a programmer step longer than 150 s;
- a change to the supply's set points or output, outside the programmer's sessions, a PD mini first heard after the start among them, its output on or off; during a search, the rail more than 1.0 V from the run's first window;
- at a later session, another servo, or REVERSION, the pulses or the range of values changed (`servo holds other values`).

A run that ends while it waits for ARMED also switches the bench to SAFE.

![A run stopped after a write](view:servo-limrun-stopped)

After a write the servo keeps the values last written. The box then reads `RESTORE IN PROGRAM.`. On the programmer, CONNECT and READ ALL, then RESTORE offers `Settings before the limit run.`. A session after the run saves its own backup file at its first write.

![RESTORE after a limit run](view:servo-limrun-restore)

### The result

The box stays until CLOSE. It shows the verdict, the reason or the values, and the report's file name.

| Verdict | Meaning |
|---|---|
| FOUND | search only: an end binds; the endpoints are reported |
| FREE | neither end binds; nothing is written |
| PASS | programmed, and both ends free in the last search: `LEFT 150>87  RIGHT 150>95` |
| FAIL | an end still binds, or the servo cannot be programmed |
| ABORTED | a protection, the operator, the link, the meter, the supply or the programmer ended it |

![A programmed servo](view:servo-limrun-pass)
![Search only](view:servo-limrun-found)

The report is a `BENCHnnn.TXT` of its own, written whatever REPORT says. It holds where the search ran, the endpoints of both searches, the servo's pulses and the pulse write, LEFT ANGLE and RIGHT ANGLE before and after with the shares, the overset and the final value, the values written, the writes sent and the sessions, the meter, the AS5600 angle at each last free position when it is fitted, and the verdict or the reason. Until the card has closed it, Wi-Fi and an update wait.

## Current limitations

- On the panel's supply model the test's currents are the model's load, not the servo's.
- The unit of LEFT ANGLE and RIGHT ANGLE is not known. The limit run takes the travel as linear in the pulse; that is not measured.
- Until the screen sends a position, the pins run at the rate the coprocessor holds, 50 Hz after a restart.

## Related pages

- [Servo settings](servo-settings.md): type, frame rate, pulses, the test, the limits and the target.
- [Servo programmer](servo-programmer.md): PROGRAM, for KST and Chaservo servos.
- [Supply](supply.md) and [Interfaces](setup-interfaces.md): the PD mini, the INA3221 and the AS5600.
