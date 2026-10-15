---
screen: SERVO
title: Servo
order: 20
---

# Servo

SERVO drives a servo, measures its current on the servo rail and runs an automatic test across supply voltages. It opens from the SERVO tile on the [menu](overview.md). The dial takes the left card; the readings, the supply line, SPEED, the target and the buttons take the right card.

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

![The panel's supply model as the meter, with the MODELLED pill](view:servo-model)

While the PD mini is disabled on SETUP, INTERFACES, the supply is the panel's model, and a MODELLED pill sits on the plot. Its readings are simulated, not measured.

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

The settings close as the run starts. Its step and phase show at the top of the left card, with the meter beside STOP TEST: `PD mini`, `INA3221 CH1` or `MODEL`. The run ends early, the output off and the servo released, on STOP TEST, STOP, a switch to SAFE, a lost link, leaving the screen, a finger on the dial, CENTRE, SWEEP, RELEASE, a tap on a set point, or a change to the servo's settings.

![A result](view:servo-result)
![A result read from the INA3221](view:servo-result-ina)

The result stays until CLOSE: PASS, FAIL or ABORTED and why, the longest travel time, the highest holding current, and the files the card took, `BENCHnnn.CSV` and `+ .TXT` for the report.

## Current limitations

- On the panel's supply model the test's currents are the model's load, not the servo's.
- Until the screen sends a position, the pins run at the rate the coprocessor holds, 50 Hz after a restart.

## Related pages

- [Servo settings](servo-settings.md): type, frame rate, pulses, the test, the limits and the target.
- [Servo programmer](servo-programmer.md): PROGRAM, for KST and Chaservo servos.
- [Supply](supply.md) and [Interfaces](setup-interfaces.md): the PD mini, the INA3221 and the AS5600.
