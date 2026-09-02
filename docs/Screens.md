# Screens

<sub>**English** · [Deutsch](Screens-de.md)</sub>

What is on every screen, what the menu marks mean, and how each screen is
operated.

## The status band

The top band is shared by every screen. From the right: STOP, the run clock
(while armed or after a run), ARMED or SAFE, a FAULT code when one is reported,
the output mode (LINK or SIM), and LINK or NO LINK.

STOP works on every screen. It disarms and latches: the bench stays disarmed
until it is armed again. Navigating away, an alert expiring or the link
recovering does not clear a stop.

ARM is at the bottom of a bench screen; STOP is at the top of the band.

## Menu marks

![The feature menu](img/overview.png)

| Mark | Meaning |
| --- | --- |
| SOON | the screen does not exist; the tile lists what it will do and what it waits on |
| MODELLED | the screen exists and works, but its hardware is not fitted; every value is simulated and the screen says so |
| none | the hardware is fitted and the readings are measured |

The mark is derived from the capability bits the coprocessor reports at
bring-up. A screen whose hardware is missing opens and runs from the model.

The menu in the light theme:

![The menu in the light theme](img/overview-light.png)

## Splash

![The splash](img/splash.png)

Each subsystem reports its result as it comes up: board, display, touch, SD
card, settings, link, coprocessor. A missing card or an unusable NVS
(non-volatile storage) is a warning. A touch
controller that does not answer, or a coprocessor with a different protocol
major version, is a failure and the bench will not arm. When every step has
answered, the splash hands over to the menu after a hold of 1.6 s; a tap skips
the hold.

## Motor & ESC

![Motor and ESC](img/motor.png)

Four traces on independent scales, the throttle slider, and peak values under
the seven-segment readouts. ARM enables the slider; DISARM and STOP stop the
output immediately, with no ramp. RESET PEAKS clears the peak markers and
leaves the live readings.

While the values are simulated, SIMULATION is drawn across the screen. The
watermark is removed when measured values arrive: from an ESC (electronic speed
controller) with telemetry (KISS, BLHeli_32, OpenYGE), from bidirectional
DShot, or from the coprocessor's own sensors.

## Servo

![Servo](img/servo.png)

Drag anywhere on the sweep to command a position. The solid arm is the measured
position; the faint arm is the commanded position. The gap between them is the
servo's own lag. The rings around the tip pulse while the servo is being
driven. Releasing the sweep clears the output slot.

## Analyser

![Analyser](img/analyser.png)

Sixteen channels, each with 1.5 s of history and a bar for its current value.
CH17 and CH18 are the two digital channels. A glitch is a spike in one lane; a
dropout is a notch across all sixteen at the same instant.

The state block shows one of SILENT, FAILSAFE, FRAME LOST and LIVE, with one
line of explanation:

![A receiver in failsafe](img/analyser-failsafe.png)

In FAILSAFE the receiver is sending well-formed values it has generated itself;
every trace is drawn red. Treat FAILSAFE as a stop, not as sixteen valid
channels. [Receiver buses](Receivers.md) describes the states.

## Programmer

The sequence is: device class, protocol, connect.

![Device class](img/programmer.png)

Every protocol row names its transport. There is no autodetection:

![The protocols of a class](img/programmer-protocols.png)

BLHeli_32 is not in the ESC list. The bench identifies and drives these ESCs
and sends the DShot special commands, but cannot read their parameters:
[BLHeli_32 parameters](BLHeli32.md).

Nothing is editable until a device has answered:

![Nothing has answered](img/programmer-idle.png)

After a device answers, the parameters are shown in groups with the selected
row's help under the list:

![Connected](img/programmer-params.png)

Each firmware shows its settings in its own units. BLHeli_S shows timing as
named steps; the others show degrees of advance:

![Degrees rather than named steps](img/programmer-am32.png)

A changed value is not written until WRITE is pressed. Staged changes carry a
mark and their own colour, and the WRITE button shows how many are staged:

![Two staged edits](img/programmer-dirty.png)

Steppers stop at the ends of a list; they do not wrap.

Going back one level drops the connection. Back climbs one level at a time; the
band's home tag leaves the screen.

## Battery

![Cell divergence](img/battery.png)

Cells are drawn as their departure from the pack's mean. The verdict follows
the spread, the widest gap between any two cells: HEALTHY below 30 mV, WATCH
from 30 mV, REPLACE from 60 mV. The scale follows the pack down to a floor of
12 mV and is printed beside the plot.

Measure under load. At rest a weak cell reads like the others.

## Logs

![The file browser](img/logs.png)

Browse the card, open a file, check what the import detected, then plot:

![The import view](img/logs-import.png)
![The plot](img/logs-plot.png)

The CSV (comma-separated values) reader accepts decimal comma and decimal
point, a units row and ragged rows; the import view shows what it decided
before the file is plotted. Runs recorded by the bench are written as
`BENCHnnn.CSV` in the card's root directory.

## Setup

![Setup](img/setup.png)

Settings are behind the SETUP tile, in both themes:

![Setup in the light theme](img/setup-light.png)

## Balancing

Described on its own page: [Balancing](Balance.md).

## Screens that are not ready

A tile marked SOON names what the screen will do and the part or decision it
waits on.
