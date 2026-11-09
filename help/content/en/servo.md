---
screen: SERVO
title: Servo
order: 20
---

# Servo

SERVO drives a servo, measures its current on the servo rail, runs an automatic test across supply voltages and searches the travel limits of the installed servo. It opens from the SERVO tile on the [menu](overview.md), on its hub.

## The hub

![The SERVO hub](view:servo-hub)

The hub holds eight tiles, each opening a screen of its own:

| Tile | Screen |
|---|---|
| LIVE | the horn's angle, the dial, and the readings COMMANDED, MEASURED and CURRENT along its foot |
| SWEEP | the dial with SWEEP; its list sets CURVE, SPEED, RANGE and Dwell time ([Servo settings](servo-settings.md)) |
| TEST | the AUTOMATIC TEST card with a summary line, the last result and its file; SUPPLY's caps, read-only; the supply's plot and the run's box under them; the READY card and HOLD 2s: ARM + START |
| LIMIT RUN | the ends the last run found, its state lines, the READY card and HOLD 2s: ARM + START |
| RUDDER | the control surface's deflection in ° and mm, LENGTH, the buttons 1000us, CENTER and 2000us, ZERO and RESET MAX ([Rudder deflection](servo-rudder.md)) |
| RUDDER AUTO | AUTO's box, which programs KST, Chaservo or Hitec end points to the deflection entered |
| KST, HITEC | the programmer for that make, over the left card ([Servo programmer](servo-programmer.md)) |

KST and HITEC are grey while the IOmcu lacks their capability or another session holds the port; a tap names the reason. The hub's footer shows SERVO with the port line and SETTINGS, which opens the servo profile every function uses ([Servo settings](servo-settings.md)). The hub drives nothing: the bench is SAFE there.

The home tag goes from a function screen to the hub, and from the hub to the menu. Leaving a function screen switches the bench to SAFE. The PROG 5V on the carrier stays on from one SERVO screen to another and goes off when SERVO is left; on the hub the footer reads `PROG 5V ON 5.02V 0.120A` in the warning colour while it is on, `--V --A` without a reading.

LIVE, SWEEP and RUDDER show the PORT button and READY's rows on the right, with the controls under them. TEST, LIMIT RUN, RUDDER AUTO, KST and HITEC show the PORT button and the READY card. The footer holds the function's name with the port line, ARM (HOLD 2s: ARM + START on TEST, LIMIT RUN and RUDDER AUTO, the session's WRITE n VALUES on KST and HITEC), and SETTINGS for the screen's list; LIVE's is the servo profile. Every control is 48px (7.2mm) tall or taller. The rotary knob moves the horn on LIVE, SWEEP, RUDDER and RUDDER AUTO only.

## At rest

![SERVO while SAFE](view:servo)

While the bench is SAFE the horn is drawn dimmed and LIVE's left card reads `ARM FIRST`. The dial takes no position. The IOmcu writes a pulse of length zero to every servo PWM (pulse-width modulation) pin.

LIVE's readings, along the foot of the left card:

| Reading | Shows |
|---|---|
| COMMANDED | pulse width commanded by the screen, in us |
| MEASURED | angle from the selected AS5600 or IMU source, -180.0 to +179.9°; the AS5600 must be enabled and see its magnet; `---` without a reading |
| CURRENT | servo-rail current from the active meter, in hundredths of an amp |

LIVE's right card, from the top:

| Row | Shows |
|---|---|
| PORT button | what the tester drives, `protocol pins`: `SERVO PWM GPIO 2`, `PROG PORT 1 GPIO 13`, or a warning; a tap opens the list where it is picked ([the PORT button](servo-settings.md#the-port-button)) |
| READY | one row per check: PORT, SUPPLY and ARM; RUDDER adds ANGLE and ZERO |
| SET V, SET A | the PD mini's set points; a tap opens SUPPLY's keypad. On the carrier's programming port `PROG 5V`, `5V FIXED` |
| plot | volts, amps and watts of what feeds the servo over the last 9.0s, right of SET A, under a legend such as `5.02V  0.120A  0.60W` |
| SPEED | the rate the bench may move the output, 100% passes the command straight through |
| type pill | the servo type, frame rate and pulse range in force: `STANDARD PWM  50Hz  1000-2000`; a tap opens the [servo profile](servo-settings.md) on TYPE, on a SAFE bench |
| CENTER, RELEASE | SWEEP shows SWEEP and RELEASE |
| rail line | the meter of what feeds the servo on the left, its volts and amps on the right: `6.00V  0.120A` |

A READY row's dot and state are green when met, amber when ARM does it or it needs a look, and red when it refuses ARM. A tap fixes the row where it can, or names it in the alert band.

| Row | Reads |
|---|---|
| PORT | the port line. On a programming port with PORT PWM off, amber: ARM switches PORT PWM on |
| SUPPLY | what feeds the servo: PROG 5V on the carrier's programming port, PD MINI where a PD mini answers, SUPPLY where none does. `ARM SWITCHES IT ON` while SAFE and off; `ON` or `ON 6.00V` in red while it feeds the servo; `FED FROM ELSEWHERE` when the rail reads 3.0V or more with the PD mini off; `EXTERNAL` when no PD mini answers; `TAP: HV WARNING` past 6.0V |
| ANGLE, RUDDER | the angle source; `ANGLE SOURCE: NO READING` opens its settings on a tap |
| ZERO, RUDDER | `SET`; `SET ZERO WITH THE SURFACE AT NEUTRAL` takes the zero on a tap |
| ARM | `HOLD 2s`, `ARM ASKED` while ARM's steps run, `ARMED` in red; red with the reason while a programmer or a run keeps ARM from the bench |

## ARMED

![SERVO while ARMED SERVO](view:servo-armed)

1. Hold ARM for 2s. On LIVE, SWEEP and RUDDER the hold first switches on what reads off: PORT PWM on a programming port, then PROG 5V or the PD mini's output. It waits up to 1000ms for them to read back, then up to 500ms for ARMED. The band then shows ARMED SERVO, and the button reads DISARM.
2. On LIVE, drag anywhere on the sweep to command a position. The solid arm is the measured position, the faint arm the commanded one.
3. Lifting the finger does not let go: the screen repeats the last position, so the servo stays where it was put.
4. Tap DISARM, press STOP or leave the screen to switch to SAFE. On LIVE, SWEEP and RUDDER, DISARM and STOP also switch PROG 5V and the PD mini's output off. A leave switches the PD mini off; PROG 5V stays on between SERVO screens.

While ARM's steps run the button reads the step in amber, for example `START, STEP 3 OF 5: READ-BACK`. A tap on it stops them with every output off. A step that fails switches every output off and names itself in the band, for example `PROG 5V FAILED: NO READ-BACK IN 1000ms`.

ARM never switches the PD mini on past 6.0V: the SUPPLY row then reads `TAP: HV WARNING` in red, and a tap opens HV SERVOS ONLY. ARM also leaves the PD mini off when an INA3221 reads the rail at 3.0V or more with the PD mini off. Without an INA3221 window on the rail nothing reads it, and ARM switches the PD mini on.

ARMED SERVO drives the servo outputs and a pulsing PROG PORT. Throttle outputs get no pulse.

| Control | SAFE | ARMED |
|---|---|---|
| The dial | refused | commands the position under the finger |
| Rotary knob | refused; the turn is not kept | moves the horn from the commanded value |
| CENTER | refused; drawn dimmed | commands PULSE CENTER plus TRIM |
| SWEEP, PAUSE, PAUSED | refused; drawn dimmed | starts, pauses and resumes the sweep |
| RELEASE | sends the release | returns the surfaces to their rest: 1500us for STANDARD PWM, 760us for NARROW 760 |
| SPEED | set | set; a held position is sent again at the new rate |
| HOLD 2s: ARM + START on TEST | arms the bench and starts the automatic test after its hold | starts the automatic test after its hold |

Switching to ARMED sets the commanded position to the midpoint between the channel's end points, without TRIM. The first drag or knob turn starts there. SWEEP is grey without an assigned surface, an active link or IOmcu 0.10.0 or later.

## The servo rail's meter

The rail line on LIVE, SWEEP and RUDDER, and the line over the plot on the other function screens, name the meter of the servo rail:

| Name | Meter |
|---|---|
| `INA 0x41 CH2` | the INA3221 channel that watches the servo tester's target in SETUP > SENSORS, here 0x41's CH2: the mean current and voltage of its last 50ms window, 20 points a second |
| `PD MINI` | the PD mini supply's last sample, or the panel's supply model |

![The INA3221 as the rail's meter](view:servo-ina)
![A clipped window, in the warning colour](view:servo-clipped)

The INA3221 becomes the meter after its requirements have held for 1000ms: a fitted channel watches the tester's target, IOmcu is 0.16.0 or later, and windows containing current samples arrive. Before IOmcu 0.22.0, that channel is CH1 of the selected part. If any sample reaches the range limit, such as 1.638A with a 0.1Ω shunt, the window uses the warning colour. Its mean is then a bound rather than a measured value.

![The panel's supply model as the meter, with the SIMULATED pill](view:servo-model)

While the PD mini is disabled in SUPPLY's PD mini settings the supply is the panel's model: a SIMULATED pill sits on the plot, and SIMULATED follows the name on the rail line. Its readings are simulated, not measured.

A reading that did not arrive is `---` in LIVE's CURRENT, `--` on the line and a gap in the plot.

## The supply on SERVO

LIVE, SWEEP and RUDDER have the SUPPLY row and the SET row: ARM switches the supply on, DISARM switches it off. TEST, LIMIT RUN and RUDDER AUTO switch it through their hold and their run. On KST and HITEC the session's CONNECT switches it on, and a tap on the supply row while it reads ON switches it off. No SERVO screen has OUTPUT ON: it is on SUPPLY.

- A tap on SET V or SET A opens SUPPLY's keypad over the left card, inside SUPPLY's OUTPUT LIMITS. OK puts the value on SUPPLY: one set point on both screens.
- A value typed while the output is on waits for SUPPLY's question where SETTINGS > KEYPAD under CONFIRM WHILE ON asks it, ARMED or SAFE. A voltage raised above 6.0V waits for HV SERVOS ONLY.
- While a programming port holds the PD mini, SET V and SET A are dim and a tap names the hold in the band.
- The current limit, SUPPLY's SET A, is on TEST's, LIMIT RUN's and RUDDER AUTO's READY card: a tap on PD MINI opens its keypad over the left card ([The current limit](#the-current-limit)).
- Leaving SERVO switches off an output SERVO switched on.
- After a programmer's LEAVE that leaves the output on, SERVO asks whether the set points from before the session go back onto it ([Servo programmer](servo-programmer.md)). No other end of a port's hold asks: RELEASE PORT, a STOP, an error, a lost link or a restart leaves the session's set points until the next OFF.

On the [rbmod carrier](https://github.com/subtilitas/rcbench/wiki/Carrier) the programming ports have their own 5V rail at GPIO 21. No SERVO screen shows the PROG 5V box; SUPPLY does. On LIVE, SWEEP and RUDDER the SUPPLY row reads PROG 5V and ARM switches it on, on LIMIT RUN and RUDDER AUTO the hold. The SET row then reads `PROG 5V`, `5V FIXED` and takes no tap; the plot and the rail line show the INA3221 channel that watches the 5V, such as `INA PROG 5V` on the line. Without a reading of that channel the line reads `PROG 5V NO READING` and `--V  --A`, never the PD mini's values. On KST and HITEC the supply row reads ON while the 5V is on; a tap switches it off.

While a programming port holds the 5V the tap switches nothing and the band names the lock. Leaving SERVO switches off a rail switched on by ARM or the hold unless a port holds it.

While PROG 5V is ON, two windows below 3000mV indicate a 5V failure. One possible cause is a braking servo feeding energy back until M7 latches off on overvoltage. The bench switches to SAFE, all outputs switch off, and the band shows `PROG 5V lost: INA3221 0x40 CH3 read 0.12V while ON. All outputs off.` ARM, the hold or PROG 5V ON on SUPPLY switches it on again. This monitoring is inactive without an INA3221 channel watching PROG 5V or while a programmer holds a port.

### The current limit

The automatic test, the limit run and RUDDER AUTO run at SUPPLY's SET A where a PD mini feeds the servo. The READY card's PD MINI row shows it after the voltage: `ON 2.00A` on TEST, `5.0V 2.00A` on LIMIT RUN and RUDDER AUTO, amber `ON AT START 2.00A` while the hold or the steps switch the output on. LIMIT RUN's and RUDDER AUTO's lists hold it as CURRENT, `SET A 2.00A`.

- A tap on the row, green or amber, or on CURRENT opens SUPPLY's keypad: CURRENT LIMIT in A within SUPPLY's caps and OUTPUT LIMITS. OK sets SET A; SET V stays.
- With the output on, SUPPLY's question comes first, as on SUPPLY: `OUTPUT IS ON`, APPLY or CANCEL.
- No keypad while a run or its start steps are under way, or while a programming port holds the supply.
- Without a PD mini the servo's own supply limits the current: CURRENT reads `--`, locked, and no row shows a current. The Programming current of the KST and HITEC pages applies to their sessions only.

![LIVE on the carrier: ARM switches PORT PWM and PROG 5V on](view:servo-rbmod)

![HV SERVOS ONLY](view:servo-hv)

Standard servos are rated for 4.8 to 6.0V. A voltage raised from 6.0V or below to above 6.0V opens HV SERVOS ONLY. The voltage is applied after HOLD 2s TO APPLY is held; CANCEL drops it. OUTPUT ON with a set point above 6.0V opens the same warning, and so does a tap on LIVE's red SUPPLY row.

## SWEEP and PAUSE

SWEEP follows the CURVE on the SWEEP screen's SETTINGS list (square, sine or triangle), at SPEED from 0.05 to 5Hz and RANGE from 10 to 100% of travel. The IOmcu times the curve independently of link timing. During motion the button reads PAUSE. Tap it to hold the current position; it then reads PAUSED. Tap again to resume from that point.

![A paused sweep](view:servo-paused)

![SPEED LIMITS THE SWEEP](view:servo-sweep)

`SPEED LIMITS THE SWEEP` replaces SPEED's label, in the warning colour, while SPEED is slower than the fastest change the curve asks for. The curve then moves as ramps. Raise SPEED, or lower SETTINGS > SWEEP's SPEED or RANGE. With the SWEEP page's defaults (sine, 0.5Hz, RANGE 80%) and TRAVEL 90° each way it clears at SPEED 63%.

A finger on the dial, CENTER, RELEASE, STOP, a switch to SAFE and leaving the screen end a sweep.

## The automatic test

Hold HOLD 2s: ARM + START in TEST's footer for 2s to run the selected supply-voltage steps. From SAFE the hold switches PORT PWM on where the port needs it, arms the bench and starts the run; the run's steps switch the PD mini on at each step's voltage. Every end of a run, PASS included, switches the bench to SAFE. Each step measures current at rest, moving and holding, and times travel. The brown-out walk finds the voltage at which movement stops.

The READY card in place of the output card names what the hold needs: PORT, METER, ANGLE, PD MINI, STEPS, RANGE and ARM, and LOG last. LOG reads User logs, `ON` or `OFF` in amber; a tap switches it and saves it, and is refused while a run or its start steps are under way. A tap on RANGE opens the test's list on its Range row. ANGLE names the angle source the test reads, amber `AS5600 OFF` or `IMU X+ NO READING` when it reads none; it refuses nothing, and a tap on it opens the [ANGLE SOURCE list](servo-rudder.md#the-angle-source). Green is met, amber is done by the hold, red refuses it and greys the hold. A tap on the grey hold names the first red row in the alert band, for example `STEPS: NO STEP CHOSEN`. Red reasons include `SUPPLY NOT ANSWERING`, `RANGE TOO SMALL`, `STEP OUTSIDE VOLTAGE LIMITS`, `REPORT STILL BEING WRITTEN` and `RAIL LIVE WITH THE PD MINI OFF: FED FROM ELSEWHERE`: the INA3221 reads 3.0V or more on the rail with the PD mini's output off. Carrier programming ports have fixed 5V power, so supply steps cannot reach them: `PROG PORTS RUN AT 5V FIXED: NO STEPS`. The bench has 500ms to report ARMED; past that the sequence fails as `ARM: ...` with every output off.

PD MINI shows the output and SUPPLY's SET A, the current limit of every step: `ON 2.00A`, amber `ON AT START 2.00A` while the steps switch it on. A tap on it opens SET A's keypad ([The current limit](#the-current-limit)). METER names the meter the run reads; while it reads PD MINI a tap opens [Sensors](setup-sensors.md) on the INA3221 that watches the target, and the home tag returns to TEST.

![TEST and its run card](view:servo-test)

The summary line shows the voltage steps and duration, such as `4.8V, 6.0V, BROWN-OUT, 60s EACH`. Below it are status, last result and files, for example `LAST: PASS ATEST004.CSV + .TXT`. An aborted run shows its reason instead: `LAST: ABORTED - link lost`.

![A test run](view:servo-run)

TEST remains visible when the run begins, with the current step on its status line. A box under the card shows step and phase. Beside its STOP TEST it identifies the meter: `PD mini`, an INA channel such as `INA 0x41 CH2`, or `SIMULATION`.

The run ends early with the output off and servo released on STOP TEST (the footer's slot during the run, or the run's box), STOP, SAFE, link loss, leaving the screen, touching the dial, CENTER, SWEEP, RELEASE, tapping a set point or changing servo settings. An INA3221 run also ends if that sensor ceases to be the meter or no window containing both current and voltage arrives for 200ms (4 windows of 50ms): `no INA3221 window for 0.2s`. Those windows supply the STALL AT protection, so the run cannot continue through that gap.

When the AS5600 is used from the start, the run tolerates one encoder outage of up to 300ms within 10s. High-current motor or ESC runs, or plugging a part in during a run, can disturb the sensor bus. Angles are not evaluated during the gap. The report records it, for example `AS5600 gaps: 1, longest 120ms. ...`, and the CSV column `angle gap (ms)` gives its duration.

A longer outage ends the run with `AS5600 not answering`; a second within 10s ends it with `AS5600 dropped out twice in 10s`. Both are errors that switch the output off and the bench to SAFE. With ANGLE SOURCE IMU, the corresponding reasons are `IMU not answering` and `IMU dropped out twice in 10s`.

![A result](view:servo-result)
![A result read from the INA3221](view:servo-result-ina)

The box keeps the result until CLOSE; the card keeps its LAST line until the next run. The result includes PASS, FAIL or ABORTED with a reason, longest travel time, highest holding current and recorded files. These are `ATESTnnn.CSV` and, when written, `+ .TXT` for its report in `USER/LOGS`. With User logs OFF under [SETUP](setup.md#sd-card) > SD CARD, the line reads `USER LOGS OFF` and no file is written.

## The limit run

The limit run searches for mechanical binding at each end of an installed servo's travel. It can then reduce the servo's LEFT ANGLE and RIGHT ANGLE so it stops before binding. The LIMIT RUN tile on the hub opens its screen; SETTINGS in the footer opens the [run's list](servo-settings.md#the-limit-run-s-settings).

![The limit run, ready](view:servo-limrun-ready)

1. Tap SETTINGS in the footer, then MODE to choose SEARCH ONLY or SEARCH + PROGRAM.
2. With PORT on PROG PORT, tap SERVO on the READY card and pick KST/CHASERVO, HITEC D SERIES or HITEC CLASSIC, the programmer the run's sessions go to. The pick is LIMIT RUN's own and is saved; RUDDER AUTO keeps its own. With KST/CHASERVO, set SERVO PULSES to KEEP or 1000-2000. It starts at KEEP.
3. Tap the home tag to go back to LIMIT RUN.
4. Hold HOLD 2s: ARM + START in the footer. It sets a PD mini to 5.0V. From SAFE it switches PORT PWM on, then PROG 5V or the PD mini's output, waits up to 1000ms for them to read back ON, the PD mini at 5.0V, arms, waits up to 500ms for ARMED, and starts the run. A tap or a shorter hold starts nothing and shows `Hold 2s to switch the port and its supply on, ARM and start the limit run.` in the alert band.
5. While the run runs, the hold reads STOP RUN. The state lines show the phase, the side and the pulse. After the run's last command the bench is SAFE again; PROG 5V stays on, and a PD mini stays at 5.0V.

The left card shows the mode, then the output and the meter: `OUTPUT PROG PORT 1 GPIO 13, CURRENT INA3221 0x41 CH2` or `OUTPUT SERVO PWM GPIO 2, CURRENT INA3221 0x40 CH1`. LOW END and HIGH END hold the ends the last run found, in us, or `FREE`. The state lines under them name the step under way, why the hold is refused, and the last result with its file.

The READY card on the right lists what the start needs, one row each: PORT, SERVO, METER, ANGLE, RAIL, RANGE, PROGRAMMER, the supply (PROG 5V on the [rbmod carrier](https://github.com/subtilitas/rcbench/wiki/Carrier), PD MINI where the PD mini feeds the servo), PORT PWM and ARM. PROG 5V reads the run's `5.0V`, PD MINI the run's `5.0V` and SUPPLY's SET A, `5.0V 2.00A`; a tap on a green or amber PD MINI opens SET A's keypad ([The current limit](#the-current-limit)). A green dot is met. A red dot refuses the hold, or marks an output live through the row. Amber reads `START SWITCHES IT ON` or `START ARMS`: the hold does it. Grey waits on PORT. A tap on PORT opens the port choice, on SERVO the three programmers over the left column, on RANGE the [servo profile](servo-settings.md) on PULSE MIN, on ANGLE the [ANGLE SOURCE list](servo-rudder.md#the-angle-source), which refuses nothing, on a red METER [Sensors](setup-sensors.md) on the INA3221 that watches the target, on a red PD MINI with `PD MINI: 5.0V OUTSIDE ITS LIMITS` [SUPPLY](supply.md) > SETTINGS > LIMITS. A tap on another red row, or on the grey hold, names it in the alert band.

The run reads only the INA3221 channel that watches the tester's target. While one of these holds, its row is red, the hold is grey and the first state line says why:

| Row | Line | Condition |
|---|---|---|
| PROG 5V | `PROG 5V OFF: START SWITCHES IT ON` | SAFE on the rbmod carrier with PORT on a programming port and PROG 5V off. Amber: the hold switches it on |
| ARM | `ARM FIRST` | the bench is SAFE. Amber: the hold arms |
| PORT | `NO SERVO PWM PIN`, `PINS NOT READ` | PORT is the bench's outputs, and no output is bound as SERVO PWM, or the binding is not read yet |
| METER | `INA3221 NOT SET UP TO MEASURE SERVO SUPPLY CURRENT` | no channel of a fitted INA3221 watches the tester's target in SETUP > SENSORS |
| METER | `INA3221 OFFLINE` | no link, or the INA3221 does not answer |
| METER | `NO INA3221 MEASUREMENT WINDOWS YET` | the INA3221 is not the meter yet |
| RAIL | `SERVO SUPPLY BELOW 3.0V OR NOT READ` | the last INA3221 window has no rail voltage, or less than 3.0V: an output that is off reads 0V. Amber while SAFE where the hold's supply feeds the rail |
| PROGRAMMER | `LOCKED: SERVO PROGRAMMING USES THE PORT` | PORT is PROG PORT and a programmer session holds the port; named before PORT PWM |
| PORT PWM | `PORT PWM OFF: DISARM, SWITCH IT ON IN OUTPUTS` | PORT is PROG PORT with PORT PWM off, the bench ARMED |
| PORT PWM | `PORT PWM OFF: SWITCH IT ON IN OUTPUTS` | the same while SAFE. Amber: the hold switches it on |
| PORT PWM | `PORT PWM ON, WAITING FOR SAFE: DISARM, THEN ARM` | PORT is PROG PORT with PORT PWM on and its pulse still off. Amber while SAFE: the read-back waits for it |
| RANGE | `CENTER MUST BE BETWEEN PULSE MIN AND MAX` | the pulse range does not hold PULSE CENTER |
| ARM | `REPORT STILL BEING WRITTEN` | a test or a run is under way, or its report is still going to the card |
| PD MINI | `PD MINI: 5.0V OUTSIDE ITS LIMITS` | a PD mini feeds the servo and SUPPLY's limits leave out 5.0V; a tap opens SUPPLY > SETTINGS > LIMITS |

### The supply: 5.0V

LIMIT RUN and RUDDER AUTO program at 5.0V. Every programmer session of a run asks 5.0V, KST/Chaservo and Hitec alike, with or without a PD mini. The KST and HITEC pages' Programming voltage does not apply to a run; the pages keep using it. With a PD mini feeding the servo, the hold sets it to 5.0V before anything switches on, and it stays at 5.0V after the run, however the run ended. A PD mini that does not answer with 5.0V fails the start: `PD MINI FAILED: NO READ-BACK IN 1000ms`, and every output goes off.

### The search

The search moves outward from center, low side first, in 10us steps. Each position lasts 250ms: 120ms settling time, followed by averaging the next windows. Binding is detected when current is both above 1.8 times the free-running current and at least 0.15A higher. The reported end point is 25us before that position. The bench's PULSE MIN and PULSE MAX remain unchanged.

| PORT | Search |
|---|---|
| a SERVO PWM pin | from PULSE CENTER out to PULSE MIN and PULSE MAX; nothing is read or written |
| PROG PORT | a read session first, then from the servo's NEUTRAL out to its own PULSE LOWER and PULSE UPPER, never past them |

For a programming-port search, the read session holds PULSE CENTER, switches to SAFE, reads with CONNECT, returns the port with LEAVE and switches back to ARMED. No search movement occurs before this session. For KST/Chaservo, the run fails before searching if:

- no KST or Chaservo servo is read (`no KST or Chaservo servo read`): search a servo that is no KST or Chaservo on a SERVO PWM pin;
- NEUTRAL is not between PULSE LOWER and PULSE UPPER (`NEUTRAL outside LOWER/UPPER`);
- PULSE LOWER to PULSE UPPER is not inside PULSE MIN to PULSE MAX (`servo pulses outside MIN/MAX`). The bench is SAFE again, and the left card names both ranges.

A first CONNECT that fails for another reason, or a read that does not complete, ends the run as ABORTED with `programming port session failed`.

![The servo's pulses outside PULSE MIN and PULSE MAX](view:servo-limrun-range)

A KST servo as delivered holds PULSE LOWER 900.0us and PULSE UPPER 2100.0us. The STANDARD PWM type's range is 1000-2000us, so that servo is refused at KEEP. Choose SERVO PULSES 1000-2000, or a PWM type whose range holds 900 to 2100us.

SERVO PULSES at 1000-2000 writes PULSE LOWER 1000.0us and PULSE UPPER 2000.0us in the read session, before anything moves, and reads them back. NEUTRAL is not written. A servo at those values already gets no write. With SEARCH ONLY these two values are the only write.

### Programming LEFT ANGLE and RIGHT ANGLE

For KST/Chaservo, SEARCH + PROGRAM requires PORT on PROG PORT with PWM active and a way to power-cycle the servo. On a manually wired bench, this means a PD mini with output on; the hold sets it to 5.0V. Written settings take effect only after power cycling.

Without a PD mini, SEARCH + PROGRAM and SERVO PULSES 1000-2000 are unavailable. The left card shows `PROGRAMMING: NO PD MINI TO SWITCH THE SERVO OFF AND ON`. SEARCH ONLY can run if the INA3221's latest rail voltage is at least 4.8V. With a PD mini present, its output must be on; a non-PWM port, or a rail below 4.8V without a PD mini, also blocks SEARCH ONLY.

On the rbmod carrier, PROG 5V supplies the programming port and replaces the PD mini for these power cycles. With it on, SEARCH + PROGRAM and SERVO PULSES are available. With it off, the PROG 5V row reads `START SWITCHES IT ON` while SAFE: the hold switches it on. The PD mini on the servo rails is not used or monitored by this port run.

At the bench's standard outputs, LIMIT RUN is SEARCH ONLY: MODE is locked in the list with `On a PROG PORT only: choose one with PORT`. Every KST/Chaservo run on PROG PORT, including SEARCH ONLY, first reads the servo. Failure to identify one ends with `no KST or Chaservo servo read`. Hitec requirements are described below.

![No PD mini: the run writes nothing](view:servo-limrun-nopd)

![The limit run, programming](view:servo-limrun)

The run writes in sessions of the [servo programmer](servo-programmer.md):

1. The servo goes back to the center for 1000ms.
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

![LIMIT RUN's SERVO choice](view:servo-limrun-family)

![The LIMIT RUN settings, SERVO at HITEC D SERIES](view:servo-limrun-hitec)

With SERVO at HITEC D SERIES or HITEC CLASSIC, sessions use the [Hitec programmer](servo-programmer.md) in that series, whatever the HITEC page's SERIES reads. With HITEC D SERIES the list shows Hitec D receive under SERVO, the HITEC page's setting. LEFT END and RIGHT END map to 0xB2 and 0xB0 on D-series servos, in counts relative to NEUTRAL 0xC2. Classic servos use raw cells 0x14 and 0x13, swapped for CCW.

Hitec has no pulse registers, so SERVO PULSES is locked. The run uses an unmeasured pulse model: NEUTRAL 1500.0us, full travel at 850.0us and 2150.0us for D series, or 900.0us and 2100.0us for classic. PULSE MIN and MAX must cover the range, otherwise RANGE is red with `HITEC: PULSE MIN/MAX MUST COVER 850-2150us`. TYPE WIDE, 800 to 2200us, covers both.

- A D series write saves and restarts the servo inside its session: no PD mini is needed.
- A classic servo needs a PD mini, or PROG 5V on the rbmod carrier, for every session, the read included: `Classic servos need a PD mini: the bench cycles the supply.`
- The 2s ARM + START hold is the consent CHECK THIS CHANGE asks for; each write switches ENABLE WRITE TO SERVO on for itself and off at its end.
- No servo answering the first CONNECT ends the run with `no Hitec servo read`.
- The backup file HITEC001 to HITEC999 is saved before the first write. To restore, load it on FILES of the Hitec programmer and write it: the left card says `LOAD HITEC001 IN FILES`.

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

Written settings remain in the servo after the run. The left card shows `RESTORE IN KST`. For KST/Chaservo, use CONNECT and READ ALL in the programmer, then choose `Settings before the limit run.` under RESTORE. A later session creates its own backup on its first write.

![RESTORE after a limit run](view:servo-limrun-restore)

### The result

The state lines keep the result until the next run: the verdict, the reason or the values, and the report's file name. LOW END and HIGH END show the ends the run found first.

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

- [Servo settings](servo-settings.md): type, frame rate, pulses, the sweep, each run's settings and the PORT button.
- [Servo programmer](servo-programmer.md): the KST and HITEC tiles.
- [Supply](supply.md): the PD mini.
- [Sensors](setup-sensors.md): the INA3221 and the AS5600.
