# Screens

<sub>**English** · [Deutsch](Screens-de.md)</sub>

How to operate the bench: what is always on screen, what the menu marks mean,
and how each screen is used.

## The status band

The top strip belongs to the bench on every screen. Right to left: link state,
the output mode, the armed state, the run clock, and STOP.

**STOP always works.** It disarms from any screen, and it latches: the bench
stays stopped until you arm again, deliberately. Nothing else clears a stop —
not navigating away, not an alert expiring, not the link coming back.

ARM sits at the bottom of a bench screen and STOP at the top of the band, a
full screen apart, so reaching for one cannot find the other.

## What the menu marks mean

![The feature menu](img/overview.png)

| Mark | Meaning |
| --- | --- |
| **SOON** | The screen does not exist yet |
| **MODELLED** | The screen exists and works, but its hardware is not fitted — every number in it is invented, and the screen says so |
| nothing | The part is fitted and the readings are real |

MODELLED corrects itself: the mark comes from what the coprocessor reports as
actually fitted, so it disappears the moment the part goes on. Absent is not
forbidden — a screen whose hardware is missing still opens and still works
from the model, wearing the mark that says so.

The menu is also drawn in the light theme:

![The menu in the light theme](img/overview-light.png)

## The splash is the self-test

![The splash](img/splash.png)

Each subsystem reports its own result as it comes up, so a board with no card,
a touch controller that did not answer, or a coprocessor speaking the wrong
protocol version says so here — rather than looking mysteriously broken three
screens later. A failure is still reported, not a dead end: read it and move
on. Once every step has answered, the list holds briefly and hands over to the
menu; a tap skips the hold.

## Motor & ESC

![Motor and ESC](img/motor.png)

Four traces on their own scales, the throttle on the slider, peak values under
the seven-segment readouts. ARM before the slider drives anything; DISARM and
STOP both stop it at once, with no ramp. RESET PEAKS clears the peak markers
and leaves the live readings alone.

While nothing real is connected the screen runs from the model and says so —
SIMULATION sits across the plot. It comes off the moment real numbers arrive,
and that needs no extra sensor: an ESC with telemetry (KISS, BLHeli_32,
OpenYGE) or bidirectional DShot reports voltage, current, consumption, RPM and
temperatures over the signal wire itself.

## Servo

![Servo](img/servo.png)

Drag anywhere on the sweep and the horn follows. The solid arm is the
**measured** position; the faint arm behind it is what you commanded. When the
two separate, that gap is the servo's own lag — the screen adds none of its
own, so a slow servo shows as two arms rather than as a sluggish picture. The
rings around the tip pulse while the servo is being actively driven.

## Analyser

![Analyser](img/analyser.png)

Sixteen channels, each with its last second and a half of history and a bar
for where it stands now. Move a control on the transmitter and the channel you
moved is the one with a step in its trace — that is the question this screen
is built to answer. The bars answer the other one: how far, right now, without
reading a number. CH17 and CH18 are the digital channels.

A glitch shows as a spike in one lane. A dropout shows as a notch across all
sixteen at the same instant.

The state block is the biggest thing on the screen because the receiver can
lie:

![A receiver in failsafe](img/analyser-failsafe.png)

A receiver in failsafe sends sixteen perfectly well-formed values that it was
told to invent. The bench turns every trace red and says FAILSAFE in words.
**Treat failsafe as stop, never as sixteen valid numbers** — that is exactly
what they look like. Four states, each with a line saying what it means:
SILENT, FAILSAFE, FRAME LOST and LIVE.

## Programmer

Programming goes in order: what you are programming, which protocol it
speaks, and only then connect.

![What are you programming](img/programmer.png)

Picking the class is picking which lead is in your hand; picking the protocol
is picking what to say down it. Every protocol row names its transport —
there is no autodetect:

![The protocols of a class](img/programmer-protocols.png)

**BLHeli_32 is not in the ESC list.** Its settings are encrypted and the key
is not public, so the bench identifies and drives these ESCs — direction, 3D
mode, beacon and save-settings work as DShot special commands — but cannot
name a parameter. [The full answer](BLHeli32.md).

Until a device answers, nothing is editable:

![Nothing has answered](img/programmer-idle.png)

Once one has, the parameters appear grouped, and the selected row's help sits
under the list:

![Connected](img/programmer-params.png)

Each firmware shows its settings in its own units — BLHeli_S puts timing in
named steps, everything after it in degrees of advance:

![Degrees rather than named steps](img/programmer-am32.png)

**Changed is not written.** A staged change wears a mark and its own colour,
and the WRITE button counts how many are staged. A value you edited never
looks like a value read off the hardware:

![Two staged edits](img/programmer-dirty.png)

Steppers stop at their ends rather than wrapping around — on an ESC, the
wrong end of a wrapped list is a direction.

Going back up a level drops the connection. The device that answered a
bootloader is not the one that will answer a CLI, and the screen will not show
one identity above the other's parameters. Back climbs one level at a time;
the band's tag leaves the screen.

## Battery

![Cell divergence](img/battery.png)

Cells are drawn as departures from their own mean, because the number that
matters is the **spread** — the widest gap between any two cells. The verdict
follows it: HEALTHY below 30 mV, WATCH from 30, REPLACE from 60.

Spread is between cells, never against a nominal. A pack that is flat but even
is a discharged pack; a pack that is full but uneven is a broken one, and only
the second is this screen's business.

The scale follows the pack, down to a 12 mV floor, and is printed beside the
plot — a bar that fills the plot could otherwise be four millivolts or forty.

**Measure under load.** At rest, a tired cell looks like every other.

## Logs

![The file browser](img/logs.png)

Browse the card, open a file, check what the import detected and the evidence
behind it, then plot:

![The import view](img/logs-import.png)
![The plot](img/logs-plot.png)

CSV in several dialects is read tolerantly — decimal commas included — and
what the reader decided is shown before you commit to it.

## Setup

![Setup](img/setup.png)

Settings live behind the SETUP tile, in both themes:

![Setup in the light theme](img/setup-light.png)

## Balancing

Moved to its own page: [Balancing](Balance.md). The measurement is the easy
part — where the sensors go decides whether the answer means anything.

## A screen that cannot do its job yet says why

A tile that is not ready does not say "coming soon"; it names what it will do
and the single decision or part that has to arrive first — so the menu is also
the to-do list, and it never promises a measurement the bench cannot take.
