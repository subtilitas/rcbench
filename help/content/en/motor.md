---
screen: MOTOR
title: Motor and ESC
order: 10
---

# Motor and ESC

MOTOR & ESC drives an ESC (electronic speed controller) and records a run: voltage, current, power and rpm over time. It opens from the MOTOR & ESC tile on the [menu](overview.md), always on LIVE.

## The strip

The top strip has three tasks: LIVE, PROGRAM and SETTINGS. Tap a task to switch to it. LIVE retains its selected view, PLOT or TABLE, when you return.

| Task | The left column |
|---|---|
| LIVE | the plot or the table, and the throttle |
| PROGRAM | the list of ESC programmers ([PROGRAM](#program)) |
| SETTINGS | [Motor settings](motor-settings.md): the pack, the motor and the throttle output |

The right column is the same on all three tasks: the temperatures, four readouts and CONTROL card. The card contains totals on its title line, TARGET, RESET PEAKS and ARM. ARM is at the foot of the card, in the same position as on SERVO, and works on every task. A session opened from PROGRAM covers the card.

The home tag at the top left moves up one level at a time:

| From | The home tag leads to |
|---|---|
| the keypad on SETTINGS | SETTINGS; the value is not changed |
| a session on PROGRAM | one level up in the session, then PROGRAM's list |
| LIVE, PROGRAM's list or SETTINGS | the menu; the bench switches to SAFE |

## PROGRAM

![PROGRAM's list](view:motor-program)

PROGRAM lists two ways to program an ESC. `ESCs via throttle stick`, under STICK MENU, opens [ESC STICK](motor-stick.md) and drives the pins bound as throttle. `AM32, Bluejay, BLHeli_S, ESCape32`, under SIGNAL WIRE, opens the [signal-wire session](motor-wire.md). The signal-wire row stays dimmed until the IOmcu reports the required capability.

ESC STICK covers both columns. The tabs are replaced by a single output strip:

`TARGET DSHOT300 GPIO 0 GPIO 1   SUPPLY 7.60V  1.00A   SAFE`

- TARGET: the protocol and pins driven by the run. It uses the warning colour when no pin is bound or the binding has not been read.
- SUPPLY: the PD mini's output readings, `SUPPLY ON` when readings are unavailable, `SUPPLY OFF`, or `SUPPLY --` when the supply does not answer.
- SAFE or ARMED: the bench state.

ARM and the tabs cannot be pressed while ESC STICK is open. Opening it switches the bench to SAFE if necessary. Leaving MOTOR & ESC ends the run, switches the bench to SAFE and closes ESC STICK.

## Before the first run

![Link up, never armed, no pin bound as a motor](view:motor-idle)

- The panel tag reads `TELEMETRY IDLE` and the plot reads `No run recorded yet.` until the first switch to ARMED.
- While the bench is SAFE, the line under the track reads `ARM FIRST`. The track and the `-1` and `+1` keys are dimmed and accept no input.
- The readouts show `---` until a value has been reported.
- TARGET reads `NO MOTOR PIN` in the warning colour when the binding has been read and no pin is bound as a motor. Bind one on [Outputs](outputs.md).

## TARGET

TARGET, at the top of the CONTROL card, identifies the throttle outputs by protocol and pin: `DSHOT300 GPIO 0 GPIO 1`. These are the pins bound as MOTOR PWM or a DShot protocol on [Outputs](outputs.md). The value box holds 25 characters. If the pin list does not fit, a count replaces the remaining pins: `DSHOT300 GPIO 0 GPIO 1 +6`.

| TARGET reads | Condition |
|---|---|
| `DSHOT300 GPIO 0 GPIO 1` | the protocol and the pins the throttle drives, in pin order |
| `MIXED GPIO 0 GPIO 4` | the throttle pins run on two or more protocols, or one of them on a rate no protocol of OUTPUTS has |
| `GPIO 0 GPIO 1` | every throttle pin runs on a pulse rate no protocol of OUTPUTS has |
| `NO MOTOR PIN` | the binding is read and no pin is bound as a motor; warning colour |
| `PINS NOT READ` | the link is down, the pages did not read, or the board has no pin map; warning colour |

Tap TARGET to open [Outputs](outputs.md) on the protocol assigned to the throttle pins. If several protocols are assigned, or none is known, it opens the overview. Leaving MOTOR & ESC this way switches the bench to SAFE. The home tag on OUTPUTS returns to the task from which you opened it.

TARGET is shown as locked and accepts no tap:

- while the bench is not SAFE: it is ARMED, or an ARM or DISARM request is awaiting a response;
- while ARM is held;
- while the [servo programmer](servo-programmer.md) holds a port.

ARM switches the whole bank of throttle outputs to ARMED, not only the pins named here.

## A run

![A run while ARMED, with no IOmcu: SIM and the SIMULATION watermark](view:motor)

1. Hold ARM for 2s. The fill fades from green to red; the bench switches to ARMED when the fade completes. Letting go earlier switches nothing and shows `Hold 2s to switch to ARMED.` in the alert band.
2. Drag the throttle. It moves by how far the finger travels, not to where it lands. `-1` and `+1` step one percentage point.
3. Tap DISARM, or press STOP, to end the run. Both stop the output at once, with no ramp, and return the throttle to 0%.

While ARMED the panel tag reads `LIVE TELEMETRY` and the plot's right edge reads `NOW`. ARM is drawn disabled while the [servo programmer](servo-programmer.md) holds a port.

| Control | SAFE | ARMED |
|---|---|---|
| The track | refused; drawn dimmed | moves the throttle by the finger's travel |
| `-1`, `+1` | refused; drawn dimmed | step one percentage point |
| Rotary knob | refused; the turn is not kept | moves the throttle from its value |
| ARM | switches to ARMED after a 2s hold | DISARM: switches to SAFE on a tap |
| RESET PEAKS | clears the peak markers | clears the peak markers |
| PLOT, TABLE | switch the view of LIVE | switch the view of LIVE |

Leaving the screen, including through TARGET, switches the bench to SAFE and returns the throttle to 0%. A lost link or an unresponsive touch controller has the same effect.

While no IOmcu answers, the band shows SIM and SIMULATION is drawn across the screen: every value is the panel's model. SETUP, PANEL, Demo mode ON hides the SIMULATION watermark; SIM stays.

## A held run

![The plot holds the run after the switch to SAFE](view:motor-held)

Switching to SAFE freezes the plot: the tag reads `LAST TELEMETRY` and the right edge reads `END`. The next switch to ARMED clears it. At 20 samples per second, the plot's 600 columns cover 30.0s. For a longer run, it shows the last 30.0s; the CSV (comma-separated values) file on the card contains the whole run.

The readouts, the TABLE pane, the totals and the temperature strip stay live at all times, ARMED or not.

## The rate line and the temperatures

On LIVE, the telemetry card shows PLOT and TABLE below its title, alongside the poll rate and link error count. If the IOmcu has rejected link frames since power-up, their count appears on the line below. Every task shows three temperatures above the right column: ESC and MOT from the IOmcu's bench page, and MCU from the panel's ESP32-P4 die.

![37 refused link frames on the line](view:motor-refused)

## TABLE

![The TABLE pane with an INA228 as the source](view:motor-table)

TABLE lists voltage, current, power and rpm, each with its peak. A channel without a reported value shows `--`.

- When the INA228 is enabled on SETUP, SENSORS and responds, it supplies voltage, current and power. These are the means from the IOmcu's last 50ms window, read at 20Hz. The heading reads `INA228`.
- `CLIPPED` follows the heading when a current reading has reached the sensor's range limit. Current and power then represent bounds, not measured values.
- `ESC SAYS` lists voltage and current from the ESC's own telemetry. `diff` is the ESC value minus the INA228 value.
- Without the INA228, the channels use ESC telemetry and `ESC SAYS` is hidden.
- rpm comes from bidirectional DShot telemetry. Unmeasured quantities remain empty; they are not modelled.

## kV, rpm/V and EFF

The plot header shows rated kV, measured rpm per volt and EFF. EFF is measured rpm/V divided by rated kV, capped at 199%. Set rated kV under MOTOR & ESC, SETTINGS, `Rated kV`: 0 to 10000rpm/V, default 0. At 0, the kV field stays empty and EFF is hidden. EFF counts only copper loss and includes ESC losses, so it is an upper bound rather than shaft efficiency.

## The log of a run

When Auto logs is ON under [SETUP](setup.md#sd-card) > SD CARD, each switch to ARMED starts a `MOTORnnn.CSV` file in `BENCH/LOGS` on the panel's SD (Secure Digital) card. With the link up, each answered poll adds a row. One bench produced about 19 rows per second; other benches have not been measured. Switching to SAFE closes the file. Open it in [Logs](logs.md) to plot the run.

## Related pages

- [Motor settings](motor-settings.md): the pack, Rated kV, Motor poles, the Idle and Full pulse.
- [ESC STICK](motor-stick.md): programming an ESC through its throttle-stick menu.
- [Outputs](outputs.md): which pins the throttle drives.
- [Setup](setup.md): the rotary knob.
- [Sensors](setup-sensors.md): the INA228.
