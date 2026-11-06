---
screen: SERVO
title: Servo
order: 20
---

# Servo

SERVO drives a servo, measures its current on the servo rail, runs an automatic test across supply voltages and searches the travel limits of the installed servo. It opens from the SERVO tile on the [menu](overview.md), on LIVE.

## The workbench

The strip below the band contains LIVE, TEST, PROGRAM, RUDDER and SETTINGS. Each task retains its page and selection until restart. Switching tabs adds no navigation level: home returns to the menu from any task. Within run settings it returns to TEST; within a programmer it returns to the PROGRAM list. Leaving SERVO switches the bench to SAFE.

| Task | Left card |
|---|---|
| LIVE | the horn's angle, the dial, the readings COMMANDED, MEASURED and CURRENT along its foot, and a run's box left of the dial |
| TEST | the AUTOMATIC TEST and LIMIT RUN cards, each with its SETTINGS, a summary line, its start hold, the last result and its file; SUPPLY's caps, read-only |
| PROGRAM | the list of programmers; a row opens its programmer over the left column ([Servo programmer](servo-programmer.md)) |
| RUDDER | the control surface's deflection in ° and mm, LENGTH, the buttons 1000us, CENTRE and 2000us, ZERO, RESET MAX and AUTO, which programs KST, Chaservo or Hitec end points to the deflection entered ([Rudder deflection](servo-rudder.md)) |
| SETTINGS | the pages OUTPUT, SWEEP and DUT ([Servo settings](servo-settings.md)) |

The right card is the output card, the same on every task: the type row, the meter line and plot, the supply line, SPEED, the TARGET row, CENTRE, SWEEP, RELEASE and ARM. Every row tapped there is 48px (7.2mm) tall or taller, and ARM is 60px. The rotary knob moves the horn on LIVE and RUDDER only.

## At rest

![SERVO while SAFE](view:servo)

While the bench is SAFE the horn is drawn dimmed and LIVE's left card reads `ARM FIRST`. The dial takes no position. The IOmcu writes a pulse of length zero to every servo PWM (pulse-width modulation) pin.

LIVE's readings, along the foot of the left card:

| Reading | Shows |
|---|---|
| COMMANDED | pulse width commanded by the screen, in us |
| MEASURED | angle from the selected AS5600 or IMU source, -180.0 to +179.9°; the AS5600 must be enabled and see its magnet; `---` without a reading |
| CURRENT | servo-rail current from the active meter, in hundredths of an amp |

The right card, from the top:

| Row | Shows |
|---|---|
| type row | the servo type, frame rate and pulse range in force: `STANDARD PWM  50Hz  1000-2000` |
| meter line and plot | voltage, current and power of the rail, plotted over the last 17.8s |
| SET | the supply's voltage and current limit, and its output switch |
| SPEED | the rate the bench may move the output, 100% passes the command straight through |
| TARGET | what the tester drives, `protocol pins`: `SERVO PWM GPIO 2`, `PROG PORT 1 GPIO 13`, or a warning; a tap opens Outputs, where it is picked ([the TARGET row](servo-settings.md#the-target-row)) |

## ARMED

![SERVO while ARMED SERVO](view:servo-armed)

1. Hold ARM for 2s. The band then shows ARMED SERVO, and the button reads DISARM.
2. On LIVE, drag anywhere on the sweep to command a position. The solid arm is the measured position, the faint arm the commanded one.
3. Lifting the finger does not let go: the screen repeats the last position, so the servo stays where it was put.
4. Tap DISARM, press STOP or leave the screen to switch to SAFE.

ARMED SERVO drives the servo outputs and a pulsing PROG PORT. Throttle outputs get no pulse.

| Control | SAFE | ARMED |
|---|---|---|
| The dial | refused | commands the position under the finger |
| Rotary knob | refused; the turn is not kept | moves the horn from the commanded value |
| CENTRE | refused; drawn dimmed | commands PULSE CENTRE plus TRIM |
| SWEEP, PAUSE, PAUSED | refused; drawn dimmed | starts, pauses and resumes the sweep |
| RELEASE | sends the release | returns the surfaces to their rest: 1500us for STANDARD PWM, 760us for NARROW 760 |
| SPEED | set | set; a held position is sent again at the new rate |
| START TEST | refused: `ARM FIRST` | starts the automatic test after its hold |

Switching to ARMED sets the commanded position to the midpoint between the channel's end points, without TRIM. The first drag or knob turn starts there. SWEEP is grey without an assigned surface, an active link or IOmcu 0.10.0 or later.

## The servo rail's meter

The line over the plot names the meter of the servo rail:

| Name | Meter |
|---|---|
| `INA 0x41 CH2` | the INA3221 channel that watches the servo tester's target in SETUP > SENSORS, here 0x41's CH2: the mean current and voltage of its last 50ms window, 20 points a second |
| `PD MINI` | the PD mini supply's last sample, or the panel's supply model |

![The INA3221 as the rail's meter](view:servo-ina)
![A clipped window, in the warning colour](view:servo-clipped)

The INA3221 becomes the meter after its requirements have held for 1000ms: a fitted channel watches the tester's target, IOmcu is 0.16.0 or later, and windows containing current samples arrive. Before IOmcu 0.22.0, that channel is CH1 of the selected part. If any sample reaches the range limit, such as 1.638A with a 0.1Ω shunt, the window uses the warning colour. Its mean is then a bound rather than a measured value.

![The panel's supply model as the meter, with the SIMULATED pill](view:servo-model)

While the PD mini is disabled on SUPPLY's PD MINI tab, the supply is the panel's model, and a SIMULATED pill sits on the plot. Its readings are simulated, not measured.

A reading that did not arrive is `---` in LIVE's CURRENT, `--` on the line and a gap in the plot.

## The supply on SERVO

The SET line holds the SUPPLY screen's two set points and its output switch. A change on either screen is the change on both.

- A tap on a set point opens the keypad over the left card.
- OUTPUT ON is a 2s hold, OUTPUT OFF a tap.
- Leaving SERVO switches off an output SERVO switched on.
- After a programmer's LEAVE that leaves the output on, SERVO asks whether the set points from before the session go back onto it ([Servo programmer](servo-programmer.md)). No other end of a port's hold asks: RELEASE PORT, a STOP, an error, a lost link or a restart leaves the session's set points until the next OFF.

On the [rbmod carrier](https://github.com/subtilitas/rcbench/wiki/Carrier), PROG 5V replaces the plot below PD MINI. It controls the programming ports' separate 5V rail at GPIO 21, while SAFE or ARMED. Hold PROG 5V ON for 2s to switch it on; tap PROG 5V OFF to switch it off. The second line shows the latest INA3221 window from 0x40 CH3, for example `5.02V  0.120A`.

During a programming session it instead reads `HELD BY PROG PORT n` and accepts no press. Leaving SERVO switches off a rail enabled here unless a port holds it. SUPPLY shows the same control.

While the box is ON, two windows below 3000mV indicate a 5V failure. One possible cause is a braking servo feeding energy back until M7 latches off on overvoltage. The bench switches to SAFE, all outputs switch off, and the band shows `PROG 5V lost: INA3221 0x40 CH3 read 0.12V while ON. All outputs off.` Use PROG 5V ON to switch it on again. This monitoring is inactive without an INA3221 channel watching PROG 5V or while a programmer holds a port.

![SERVO on the carrier](view:servo-rbmod)

![HV SERVOS ONLY](view:servo-hv)

Standard servos are rated for 4.8 to 6.0V. A voltage raised from 6.0V or below to above 6.0V opens HV SERVOS ONLY. The voltage is applied after HOLD 2s TO APPLY is held; CANCEL drops it. OUTPUT ON with a set point above 6.0V opens the same warning.

## SWEEP and PAUSE

SWEEP follows SETTINGS > SWEEP's CURVE (square, sine or triangle), at SPEED from 0.05 to 5Hz and RANGE from 10 to 100% of travel. The IOmcu times the curve independently of link timing. During motion the button reads PAUSE. Tap it to hold the current position; it then reads PAUSED. Tap again to resume from that point.

![A paused sweep](view:servo-paused)

![SPEED LIMITS THE SWEEP](view:servo-sweep)

`SPEED LIMITS THE SWEEP` replaces SPEED's label, in the warning colour, while SPEED is slower than the fastest change the curve asks for. The curve then moves as ramps. Raise SPEED, or lower SETTINGS > SWEEP's SPEED or RANGE. With the SWEEP page's defaults (sine, 0.5Hz, RANGE 80%) and TRAVEL 90° each way it clears at SPEED 63%.

A finger on the dial, CENTRE, RELEASE, STOP, a switch to SAFE and leaving the screen end a sweep.

## The automatic test

Hold START TEST on TEST's AUTOMATIC TEST card for 2s to run the selected supply-voltage steps. Each step measures current at rest, moving and holding, and times travel. The brown-out walk finds the voltage at which movement stops.

The test requires ARMED and a responding supply. The status line explains any refusal: `ARM FIRST`, `NO STEP CHOSEN`, `SUPPLY NOT ANSWERING`, `RANGE TOO SMALL`, `STEP OUTSIDE VOLTAGE LIMITS` or `REPORT STILL BEING WRITTEN`. Carrier programming ports have fixed 5V power, so supply steps cannot reach them: `PROG PORTS RUN AT 5V FIXED: NO STEPS`.

![TEST and its two run cards](view:servo-test)

The summary line shows the voltage steps and duration, such as `4.8V, 6.0V, BROWN-OUT, 60s EACH`. Below START TEST are status, last result and files, for example `LAST: PASS ATEST004.CSV + .TXT`. An aborted run shows its reason instead: `LAST: ABORTED - link lost`.

![A test run](view:servo-run)

TEST remains visible when the run begins, with the current step on its status line. LIVE shows step and phase in a box to the left of the dial, below `ARM FIRST`. Beside STOP TEST it identifies the meter: `PD mini`, an INA channel such as `INA 0x41 CH2`, or `SIMULATION`.

The run ends early with the output off and servo released on STOP TEST, STOP, SAFE, link loss, leaving the screen, touching the dial, CENTRE, SWEEP, RELEASE, tapping a set point or changing servo settings. An INA3221 run also ends if that sensor ceases to be the meter or no window containing both current and voltage arrives for 200ms (4 windows of 50ms): `no INA3221 window for 0.2s`. Those windows supply the STALL AT protection, so the run cannot continue through that gap.

When the AS5600 is used from the start, the run tolerates one encoder outage of up to 300ms within 10s. High-current motor or ESC runs, or plugging a part in during a run, can disturb the sensor bus. Angles are not evaluated during the gap. The report records it, for example `AS5600 gaps: 1, longest 120ms. ...`, and the CSV column `angle gap (ms)` gives its duration.

A longer outage ends the run with `AS5600 not answering`; a second within 10s ends it with `AS5600 dropped out twice in 10s`. Both are errors that switch the output off and the bench to SAFE. With ANGLE SOURCE IMU, the corresponding reasons are `IMU not answering` and `IMU dropped out twice in 10s`.

![A result](view:servo-result)
![A result read from the INA3221](view:servo-result-ina)

LIVE keeps the result box until CLOSE; TEST keeps its LAST line until the next run. The result includes PASS, FAIL or ABORTED with a reason, longest travel time, highest holding current and recorded files. These are `ATESTnnn.CSV` and, when written, `+ .TXT` for its report in `USER/LOGS`. With User logs OFF under [SETUP](setup.md#sd-card) > SD CARD, the line reads `USER LOGS OFF` and no file is written.

## The limit run

The limit run searches for mechanical binding at each end of an installed servo's travel. It can then reduce the servo's LEFT ANGLE and RIGHT ANGLE so it stops before binding. Open TEST's LIMIT RUN card; its SETTINGS opens the [run settings](servo-settings.md#the-limit-run-s-settings).

![The limit run, ready](view:servo-limrun-ready)

1. Tap SETTINGS on the card, then LIMIT RUN to choose SEARCH ONLY or SEARCH + PROGRAM.
2. With TARGET on PROG PORT, tap SERVO to choose KST/CHASERVO or HITEC, the programmer the run's sessions go to; it takes the programmer opened last on PROGRAM. With KST/CHASERVO, tap SERVO PULSES to choose KEEP or 1000-2000. It starts at KEEP.
3. Tap the home tag to go back to the cards.
4. Hold START LIMIT RUN for 2s while the bench is ARMED. A tap or a shorter hold starts nothing and shows `Hold 2s to start the limit run.` in the alert band.
5. TEST stays shown. The box on LIVE's left card shows the phase, the side and the pulse, and the output it drives.

The line under the button names the output and the meter, the whole time: `OUTPUT PROG PORT 1 GPIO 13, CURRENT INA3221 0x41 CH2` or `OUTPUT SERVO PWM GPIO 2, CURRENT INA3221 0x40 CH1`. The TARGET row stays in sight as well.

The run reads only the INA3221 channel that watches the tester's target. With the PD mini or the model as the servo rail's meter, START LIMIT RUN stays grey and the line says why:

| Line | Condition |
|---|---|
| `ARM FIRST` | the bench is SAFE |
| `NO SERVO PWM PIN`, `PINS NOT READ` | TARGET is the bench's outputs, and no output is bound as SERVO PWM, or the binding is not read yet |
| `INA3221 NOT SET UP TO MEASURE SERVO SUPPLY CURRENT` | no channel of a fitted INA3221 watches the tester's target in SETUP > SENSORS |
| `INA3221 OFFLINE` | no link, or the INA3221 does not answer |
| `NO INA3221 MEASUREMENT WINDOWS YET` | the INA3221 is not the meter yet |
| `SERVO SUPPLY BELOW 3.0V OR NOT READ` | the last INA3221 window has no rail voltage, or less than 3.0V: an output that is off reads 0V |
| `PORT PWM OFF. NO PULSES ON THIS PORT.` | TARGET is PROG PORT with PORT PWM off |
| `CENTRE MUST BE BETWEEN PULSE MIN AND MAX` | the pulse range does not hold PULSE CENTRE |
| `REPORT STILL BEING WRITTEN` | a test or a run is under way, or its report is still going to the card |

### The search

The search moves outward from centre, low side first, in 10us steps. Each position lasts 250ms: 120ms settling time, followed by averaging the next windows. Binding is detected when current is both above 1.8 times the free-running current and at least 0.15A higher. The reported end point is 25us before that position. The bench's PULSE MIN and PULSE MAX remain unchanged.

| TARGET | Search |
|---|---|
| a SERVO PWM pin | from PULSE CENTRE out to PULSE MIN and PULSE MAX; nothing is read or written |
| PROG PORT | a read session first, then from the servo's NEUTRAL out to its own PULSE LOWER and PULSE UPPER, never past them |

For a programming-port search, the read session holds PULSE CENTRE, switches to SAFE, reads with CONNECT, returns the port with LEAVE and switches back to ARMED. No search movement occurs before this session. For KST/Chaservo, the run fails before searching if:

- no KST or Chaservo servo is read (`no KST or Chaservo servo read`): search a servo that is no KST or Chaservo on a SERVO PWM pin;
- NEUTRAL is not between PULSE LOWER and PULSE UPPER (`NEUTRAL outside LOWER/UPPER`);
- PULSE LOWER to PULSE UPPER is not inside PULSE MIN to PULSE MAX (`servo pulses outside MIN/MAX`). The bench stays SAFE, and the box names both ranges.

A first CONNECT that fails for another reason, or a read that does not complete, ends the run as ABORTED with `programming port session failed`.

![The servo's pulses outside PULSE MIN and PULSE MAX](view:servo-limrun-range)

A KST servo as delivered holds PULSE LOWER 900.0us and PULSE UPPER 2100.0us. The STANDARD PWM type's range is 1000-2000us, so that servo is refused at KEEP. Choose SERVO PULSES 1000-2000, or a PWM type whose range holds 900 to 2100us.

SERVO PULSES at 1000-2000 writes PULSE LOWER 1000.0us and PULSE UPPER 2000.0us in the read session, before anything moves, and reads them back. NEUTRAL is not written. A servo at those values already gets no write. With SEARCH ONLY these two values are the only write.

### Programming LEFT ANGLE and RIGHT ANGLE

For KST/Chaservo, SEARCH + PROGRAM requires TARGET PROG PORT with PWM active and a way to power-cycle the servo. On a manually wired bench, this means a PD mini with output on and a set point of at least 4.8V. Written settings take effect only after power cycling.

Without a PD mini, SEARCH + PROGRAM and SERVO PULSES 1000-2000 are unavailable. The card shows `PROGRAMMING: NO PD MINI TO SWITCH THE SERVO OFF AND ON`. SEARCH ONLY can run if the INA3221's latest rail voltage is at least 4.8V. With a PD mini present, its output must be on; a non-PWM port or voltage below 4.8V also blocks SEARCH ONLY.

On the rbmod carrier, PROG 5V supplies the programming port and replaces the PD mini for these power cycles. With it on, SEARCH + PROGRAM and SERVO PULSES are available. With it off, the card shows `PROG 5V OFF: SWITCH IT ON IN THE PROG 5V BOX`. The PD mini on the servo rails is not used or monitored by this port run.

At the bench's standard outputs, LIMIT RUN is SEARCH ONLY and the settings explain why. Every KST/Chaservo run on PROG PORT, including SEARCH ONLY, first reads the servo. Failure to identify one ends with `no KST or Chaservo servo read`. Hitec requirements are described below.

![No PD mini: the run writes nothing](view:servo-limrun-nopd)

![The limit run, programming](view:servo-limrun)

The run writes in sessions of the [servo programmer](servo-programmer.md):

1. The servo goes back to the centre for 1000ms.
2. The run switches the bench to SAFE.
3. CONNECT reads the servo.
4. One write, with every limit check of the programmer.
5. LEAVE gives the port back to PWM.
6. The run switches the bench to ARMED and searches that side again.

Before the first write, the servo settings are backed up to the card. The calculation assumes linear travel: PULSE LOWER and PULSE UPPER produce the full travel set by LEFT ANGLE and RIGHT ANGLE. At pulse p, the side's travel fraction is `(p - NEUTRAL) / (PULSE LOWER or UPPER - NEUTRAL)`. The angle registers' units are unknown, and this linear relation has not been measured.

Each side that binds normally receives two writes, starting from its original register value v0:

1. The overset: v0 x the share at the first bound pulse x 1.03, rounded up. The stop stays reachable, close to the pulse limit. The run searches that side again. Free there, the run ends as FAIL, `no stop after the overset write`.
2. The final value: the overset x the share at the last free pulse of that search, rounded down.

Example: NEUTRAL 1500.2us, PULSE LOWER 900.0us, v0 150, the first search free at 1000us and bound at 990us. The overset is 150 x 0.850 x 1.03 = 132. At 132 the search is free out to 930us; the final value is 132 x 0.950 = 125.

Near the pulse limit, if the overset would shift the stop by less than 20us, the run writes the final value directly: v0 times the fraction at the first search's last free pulse. The first write on the first bound side also identifies the register controlling that side.

Finally, both ends are searched again. Both free means PASS; either binding means FAIL. A run writes at most 8 values: two pulse limits, two per side, and two to identify the register. Values below 50 cross a soft limit; the 2s start hold grants consent.

### Hitec servos

![The LIMIT RUN settings, SERVO at HITEC](view:servo-limrun-hitec)

With SERVO set to HITEC, sessions use the [Hitec programmer](servo-programmer.md) and its SERIES selection. LEFT END and RIGHT END map to 0xB2 and 0xB0 on D-series servos, in counts relative to NEUTRAL 0xC2. Classic servos use raw cells 0x14 and 0x13, swapped for CCW.

Hitec has no pulse registers, so SERVO PULSES is hidden. The run uses an unmeasured pulse model: NEUTRAL 1500.0us, full travel at 850.0us and 2150.0us for D series, or 900.0us and 2100.0us for classic. PULSE MIN and MAX must cover the range, otherwise the card shows `HITEC: PULSE MIN/MAX MUST COVER 850-2150us`. TYPE WIDE, 800 to 2200us, covers both.

- A D series write saves and restarts the servo inside its session: no PD mini is needed.
- A classic servo needs a PD mini, or PROG 5V on the rbmod carrier, for every session, the read included: `Classic servos need a PD mini: the bench cycles the supply.`
- The 2s start hold is the consent CHECK THIS CHANGE asks for; each write switches ENABLE WRITE TO SERVO on for itself and off at its end.
- No servo answering the first CONNECT ends the run with `no Hitec servo read`.
- The backup file HITEC001 to HITEC999 is saved before the first write. To restore, load it on FILES of the Hitec programmer and write it: the box says `LOAD HITEC001 IN FILES`.

### What ends a limit run

Each of these ends the run as ABORTED, with the output off and the servo released:

- STOP RUN, STOP, a switch to SAFE, a lost link, leaving the screen, a change to the servo's settings;
- the INA3221 ceasing to be the meter, or no INA3221 window for 200ms (`no INA3221 window for 0.2s`);
- with the AS5600 in use from the start, the part not answering for more than 300ms (`AS5600 not answering`), or again within 10s (`AS5600 dropped out twice in 10s`); with ANGLE SOURCE IMU the IMU's readings alike (`IMU not answering`, `IMU dropped out twice in 10s`); a shorter, rarer gap is ridden through, and the report counts it;
- during a search, a window without the rail voltage (`no rail voltage in a window`), or with its mean under 3.0V, the first window included (`supply or rail voltage changed`);
- a sample at 3.0A or more, a sample at the end of the INA3221's range, or window means above 1.0A for 400ms;
- a search longer than 40s, a bench that does not switch in 3s, or a programmer step longer than 150s;
- a change to the supply's set points or output, outside the programmer's sessions, a PD mini first heard after the start among them, its output on or off; during a search, the rail more than 1.0V from the run's first window. A carrier programming-port run monitors PROG 5V instead;
- at a later session, another servo, or REVERSION, the pulses or the range of values changed (`servo holds other values`).

A run that ends while it waits for ARMED also switches the bench to SAFE.

![A run stopped after a write](view:servo-limrun-stopped)

Written settings remain in the servo after the run. The box shows `RESTORE IN PROGRAM.` For KST/Chaservo, use CONNECT and READ ALL in the programmer, then choose `Settings before the limit run.` under RESTORE. A later session creates its own backup on its first write.

![RESTORE after a limit run](view:servo-limrun-restore)

### The result

The box on LIVE stays until CLOSE, and the card's LAST line until the next run. The box shows the verdict, the reason or the values, and the report's file name.

| Verdict | Meaning |
|---|---|
| FOUND | search only: an end binds; the endpoints are reported |
| FREE | neither end binds; nothing is written |
| PASS | programmed, and both ends free in the last search: `LEFT 150>87  RIGHT 150>95` |
| FAIL | an end still binds, or the servo cannot be programmed |
| ABORTED | a protection, the operator, the link, the meter, the supply or the programmer ended it |

![A programmed servo](view:servo-limrun-pass)
![Search only](view:servo-limrun-found)

The run writes `LIMITnnn.TXT` in `USER/LOGS` regardless of REPORT, unless User logs is OFF. It records the searched output, both searches' end points, servo pulse limits and any pulse write, and LEFT ANGLE and RIGHT ANGLE before and after. It also includes travel fractions, overset and final values, values written, write and session counts, meter, AS5600 angle at each last free position when fitted, and verdict or reason. Wi-Fi and updates wait until the report file is closed.

## Current limitations

- On the panel's supply model the test's currents are the model's load, not the servo's.
- The unit of LEFT ANGLE and RIGHT ANGLE is not known. The limit run takes the travel as linear in the pulse; that is not measured.
- Until the screen sends a position, the pins run at the rate the IOmcu holds, 50Hz after a restart.

## Related pages

- [Servo settings](servo-settings.md): type, frame rate, pulses, the sweep, each run's settings and the target.
- [Servo programmer](servo-programmer.md): PROGRAM's row for KST and Chaservo servos.
- [Supply](supply.md): the PD mini.
- [Sensors](setup-sensors.md): the INA3221 and the AS5600.
