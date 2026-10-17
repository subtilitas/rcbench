---
screen: MOTOR
title: Motor and ESC
order: 10
---

# Motor and ESC

MOTOR & ESC drives an ESC (electronic speed controller) and records a run: voltage, current, power and rpm over time. It opens from the MOTOR & ESC tile on the [menu](overview.md). The plot and the throttle take the left column; the four readouts, ARM and the totals take the right.

## Before the first run

![Link up, never armed, no pin bound as a motor](view:motor-idle)

- The panel tag reads `TELEMETRY IDLE` and the plot reads `No run recorded yet.` until the first switch to ARMED.
- The line under the track reads `ARM FIRST` while the bench is SAFE. The track and the `-1` and `+1` keys are dimmed and take no command.
- The readouts show `---` while nothing has reported a value.
- `TARGET: NO MOTOR PIN ASSIGNED`, in the warning colour, says the binding was read and no pin is bound as a motor. Bind one on [Outputs](outputs.md).

## The throttle panel names its pins

The throttle panel names the pins the throttle drives, left of the percentage: `TARGET: GP0 GP1`. They are the pins bound as MOTOR PWM or as a DShot protocol on SETUP, OUTPUTS. The line holds 49 characters; pins that do not fit are counted, `+7`.

| Line | Condition |
|---|---|
| `TARGET: GP0 GP1` | the pins the throttle drives, in pin order |
| `TARGET: NO MOTOR PIN ASSIGNED` | the binding is read and no pin is bound as a motor |
| `TARGET: ASSIGNMENT NOT READ` | the link is down, the pages did not read, or the board has no pin map |

ARM switches the whole bank of throttle outputs to ARMED, not only the pins named here.

## A run

![A run while ARMED, with no coprocessor: SIM and the SIMULATION watermark](view:motor)

1. Hold ARM for 2 s. The fill fades from green to red; the bench switches to ARMED when the fade completes. Letting go earlier switches nothing.
2. Drag the throttle. It moves by how far the finger travels, not to where it lands. `-1` and `+1` step one percentage point.
3. Tap DISARM, or press STOP, to end the run. Both stop the output at once, with no ramp, and return the throttle to 0 %.

While ARMED the panel tag reads `LIVE TELEMETRY` and the plot's right edge reads `NOW`. ARM is drawn disabled while the [servo programmer](servo-programmer.md) holds a port.

| Control | SAFE | ARMED |
|---|---|---|
| The track | refused; drawn dimmed | moves the throttle by the finger's travel |
| `-1`, `+1` | refused; drawn dimmed | step one percentage point |
| Rotary knob | refused; the turn is not kept | moves the throttle from its value |
| ARM | switches to ARMED after a 2 s hold | DISARM: switches to SAFE on a tap |
| RESET PEAKS | clears the peak markers | clears the peak markers |
| PLOT, TABLE | switch the pane | switch the pane |

Leaving the screen, a lost link or a touch controller that stops answering also switches to SAFE and returns the throttle to 0 %.

While no coprocessor answers, the band shows SIM and SIMULATION is drawn across the screen: every value is the panel's model.

## A held run

![The plot holds the run after the switch to SAFE](view:motor-held)

Switching to SAFE holds the plot as it stood: the tag reads `LAST TELEMETRY` and the right edge reads `END`. The next switch to ARMED clears it. The plot is 534 columns at 20 samples per second, 26.7 s. A longer run keeps its last 26.7 s on screen; the whole run is in its CSV (comma-separated values) file on the card.

The readouts, the TABLE pane, the totals and the temperature strip stay live at all times, ARMED or not.

## The strip above the plot

The strip carries the poll rate, the link's error count and three temperatures: ESC and MOT from the coprocessor's bench page, MCU from the panel's own ESP32-S3 die. While the coprocessor has refused link frames since its power-up, the strip adds the count.

![37 refused link frames on the strip](view:motor-refused)

## TABLE

![The TABLE pane with an INA228 as the source](view:motor-table)

TABLE lists voltage, current, power and rpm, each with its peak. A channel nothing answered for reads `--`.

- With the INA228 enabled on SETUP, INTERFACES and answering, voltage, current and power are the INA228's: the means of the coprocessor's last 50 ms window, read at 20 Hz. The heading names `INA228`.
- `CLIPPED` follows the heading while a current read the end of the part's range: current and power are then bounds, not values.
- `ESC SAYS` lists the ESC's own telemetry voltage and current, and `diff` is the ESC's figure less the INA228's.
- Without the INA228 the channels are the ESC's telemetry, and `ESC SAYS` is not shown.
- rpm comes from bidirectional DShot telemetry. A quantity nothing measures is drawn empty, not modelled.

## kV, rpm/V and EFF

The header of the plot shows the rated kV, the measured rpm per volt and EFF, the second divided by the first, capped at 199 %. The rated kV is the `Rated kV` setting on SETUP, ESC / BENCH, 0 to 10000 rpm/V, default 0. At 0 the kV field is empty and no EFF is shown. EFF counts copper loss only and includes the ESC's losses, so it is an upper bound, not a shaft efficiency.

## The log of a run

One switch to ARMED writes one file, `ESCnnn.CSV`, on the panel's SD (Secure Digital) card. With the link up a row is one answered poll, about 19 rows per second on one bench; other benches are not measured. The file closes at the switch to SAFE. [Logs](logs.md) opens and plots it.

## Related pages

- [Outputs](outputs.md): which pins the throttle drives, and the Idle and Full pulse.
- [Setup](setup.md): Rated kV, Motor poles, the rotary knob.
- [Interfaces](setup-interfaces.md): the INA228.
