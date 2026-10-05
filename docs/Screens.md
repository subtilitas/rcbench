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
SUPPLY is marked MODELLED whatever the coprocessor reports: no driver for its
hardware exists.

The menu in the light theme:

![The menu in the light theme](img/overview-light.png)

## Splash

![The splash](img/splash.png)

Each subsystem reports its result as it comes up: board, display, touch, SD
card, settings, link, coprocessor. The board line carries the panel's own
firmware version, and the coprocessor line carries the protocol version and
the firmware version that the coprocessor reported -- two boards can speak
the same protocol and be different builds, and this is where that shows.

A missing card or an unusable NVS (non-volatile storage) is a warning. A
touch controller that does not answer, or a coprocessor with a different
protocol major version, is a failure and the bench will not arm.

When every step has answered, the splash hands over to the menu after a hold
of 1.6 s; a tap skips the hold.

## Motor & ESC

![Motor and ESC](img/motor.png)

Two columns. The plot and the throttle take the left, the four readouts and
the controls take a rail on the right, so reading the numbers and working the
throttle do not compete for the same part of the screen.
### The plot covers one run

![Motor and ESC, telemetry held](img/motor-held.png)

The trace is the record of a run. It advances only while the bench is armed:
arming clears it, disarming holds it as it stood. The panel tag reads
`LIVE TELEMETRY` while it advances, `TELEMETRY HELD` while it holds a run, and
`TELEMETRY IDLE` before the first arm, when the plot reads `no run recorded`.
The right-hand axis label reads `NOW` while it advances and `END` where a held
one stopped. The readouts, the TABLE pane, the totals and the temperature
strip are live at all times, armed or not.

The window is 534 columns at 20 samples per second, 26.7 s. A longer run holds
its last 26.7 s; what came before is in the CSV file and not on the screen.

The trace ends at the disarm, not when the motor stops. Disarming commands
zero throttle and the propeller coasts down after it. That coast-down is on
the readouts and not on the plot, and not in the CSV file either, which closes
on the same edge.

Arming clears the plot, and a latched stop is cleared by arming, so the trace
of a run a STOP ended does not survive making the bench usable again. One
arming writes one CSV file, and the log viewer plots it.


The strip above both columns carries the poll rate, the link's error count
(cyclic redundancy check failures and resyncs added together) and three
temperatures. ESC and MOT come from the bench page. MCU is the panel's own
die, read from the ESP32-S3's sensor: it is the display board's temperature,
not the coprocessor's.

The throttle moves by how far a finger travels, not to where it lands. A press
on the track commands nothing, so a touch at the far end asks for nothing; a
drag across the whole track asks for the whole span, and the pin follows it
unramped. `-1` and `+1` at the ends of the track step one percentage point.

ARM is a hold. The fill fades from green to the danger red across two
seconds, and the bench arms when the fade completes; letting go before then
arms nothing, and the release itself arms nothing either.

The finger has to stay on the button. One that leaves it abandons the hold,
because the gesture is contact with the control and not with the panel.
Coming back is not resuming: the contact is finished as far as ARM is
concerned, so arming after that takes lifting the finger and pressing again.
A bench that disarms under a finger still held on the button ends the gesture
too — otherwise the contact made to stop it would arm it again two seconds
later. Arming flashes the
whole button twice: white, black, red, and again, one drawn frame each. An
armed bench carries the danger red, and DISARM is a press rather than a hold:
stopping never needs a hold. DISARM and STOP stop the output immediately, with
no ramp. RESET PEAKS clears the peak markers and leaves the live readings.

### EFF is a guessimetric

The telemetry panel's header shows the rated kV, the revolutions per minute
per volt the motor is actually turning, and EFF, the second divided by the
first.

The arithmetic behind it is sound. Terminal voltage divides into the resistive
drop and the back electromotive force (back-EMF), so rpm/V under load is the
rated kV scaled by the fraction of the voltage that reaches the back-EMF, and
that fraction is the fraction of the input power that becomes mechanical. In
an ideal motor the ratio is the conversion efficiency exactly.

The number on the screen is not that, for three reasons, and it is labelled
EFF rather than efficiency because of them:

- It accounts for copper loss (I squared R) only. Iron loss, friction and
  windage fall on the mechanical side of the split, so the figure is an upper
  bound on shaft efficiency rather than a value of it.
- The bench measures pack voltage, not the motor's terminals, so the ESC's
  conduction and switching losses are inside the figure. It describes the
  drivetrain, and comparing it against a motor datasheet compares two
  different things.
- It is only as good as the rated kV. An error there maps straight into the
  percentage, and a figure above 100 % means the rated value is wrong; the
  screen shows that rather than hiding it, capped at 199 %.

The rated kV comes from the connected ESC when it reports one. No ESC reports
it yet, so in practice it is the `Rated kV` setting, which defaults to zero: a
guessed kV produces a plausible-looking percentage that is wrong, so with no
value the field is drawn empty and no percentage is shown. The measured rpm/V
is shown either way, being a measurement rather than an inference.

While the values are simulated, SIMULATION is drawn across the screen. The
panel simulates only while no coprocessor answers, so the watermark goes when
one does. What replaces it is what that coprocessor can actually measure: rpm
from bidirectional DShot today, and its own sensors when a measurement front
end is fitted. A quantity nothing measures is drawn as an empty field rather
than as a modelled number.

## Servo

![Servo](img/servo.png)

Drag anywhere on the sweep to command a position. The solid arm is the measured
position; the faint arm is the commanded position. The gap between them is the
servo's own lag. The rings around the tip pulse while the servo is being
driven. Lifting the finger does not let go: the screen keeps saying the last
position every SERVO_HOLD_MS, so a servo stays where it was put. **RELEASE**,
the button, is what returns the surfaces to centre -- and even that leaves the
pins bound and driving, at the middle of their travel. A disarm, or leaving
the screen, which disarms, is what stops the edges.

The screen drives the channels the binding marks as surfaces, not a fixed pin
and not a fixed protocol. PPM's eight channels are surfaces too, so a bound
PPM output moves with this screen exactly as a bound SERVO PWM one does. With
no surface channel bound at all it commands nothing and no pin moves.

SPEED is the rate the bench may move the output, in degrees a second of the
horn's travel, not a speed for the drawing alone. At 100% the command goes
straight through and the servo moves at its own rate; below that the bench
ramps the command in front of it, so 30% takes three times as long to cross
as 90%. It applies to a held output as soon as it is changed.

**ARM before anything moves.** While the bench is not armed the coprocessor
writes a pulse of length zero to every PWM pin, so the horn on screen follows
the finger and the servo does not. The button is a two-second hold, the same
gesture and the same fade as the one on MOTOR & ESC, and a press on it while
armed disarms. Leaving the screen disarms and lets go of the pin: a screen
that is not visible must not be holding a servo somewhere, or leaving the
bench armed behind it.

The right card shows what is commanded and measured, the type and frame rate
in force -- in the danger colour while they are ones that can destroy a servo
not made for them -- and the supply that feeds the servo: its voltage, current
and power, read and plotted over the last 13 s. Without a supply sample the
readings are `--`.

### Settings

SETTINGS, at the top of the right card, opens the servo's settings over the
left card. ARM, CENTRE, RELEASE and STOP stay where they are and work. A value
opens the keypad, a list opens a list, a switch flips on the tap, and the
name opens a keyboard.

![The servo's settings](img/servo-settings.png)

| Page | Setting | What it does |
| --- | --- | --- |
| OUTPUT | TYPE | the servo profile: STANDARD PWM, NARROW 760, WIDE, HELI CYCLIC or HELI TAIL 760 |
| OUTPUT | FRAME RATE | how often a pulse is sent; the type's list, or CUSTOM on the keypad |
| OUTPUT | PULSE MIN, CENTRE, MAX | the pulse widths the travel maps onto, 400 to 2500 us: -90 deg is MIN, 0 is CENTRE, +90 deg is MAX, and RELEASE rests at CENTRE. An end lies no further from CENTRE than CENTRE lies from 400 us or 2500 us |
| OUTPUT | TRIM | added to the centre, 5 us a step, up to 200 us either way |
| OUTPUT | TRAVEL | how far the horn may go each way, 10 to 90 degrees |
| OUTPUT | REVERSE | the direction the horn's angle maps onto the pulse |
| TEST | CURVE, SPEED, RANGE | the automatic test's movement: square, sine or triangle, 0.05 to 5 Hz, 10 to 100 % of the travel |
| TEST | LENGTH BY, TEST TIME, MOVEMENTS | how long each supply step runs: a time, or a number of movements |
| TEST | DWELL, SETTLE | held at each end; waited after a supply step before measuring |
| TEST | STEP 4.8 / 6.0 / 7.4 / 8.4 V, BROWN-OUT | the supply steps, and the brown-out run from 5.0 V down |
| LIMITS | VOLTAGE MAX, CURRENT MAX | the SUPPLY screen's caps, the same settings |
| LIMITS | STALL AT | above this current the servo counts as stalled |
| LIMITS | IDLE CURRENT, HOLD CURRENT, TRAVEL TIME | pass/fail limits; 0 is not checked |
| DUT | NAME | the device under test, up to 23 characters, for the report |
| DUT | REPORT | a text report beside each test's log |

| Type | Centre | Travel | Frame rates |
| --- | --- | --- | --- |
| STANDARD PWM | 1500 us | 1000-2000 us | 50, 60, 100, 150, 200, 250, 300, 333 Hz |
| NARROW 760 | 760 us | 660-860 us | as STANDARD PWM |
| WIDE | 1500 us | 800-2200 us | as STANDARD PWM, up to 312 Hz |
| HELI CYCLIC | 1520 us | 820-2220 us | 50, 120, 200, 333 Hz |
| HELI TAIL 760 | 760 us | 410-1110 us | 200, 333, 560 Hz |

A pulse needs a pause before the next frame. STANDARD PWM, NARROW 760 and WIDE
keep at least 1 ms between pulses, so their fastest rate is 1 / (longest
pulse + 1 ms): 333 Hz for 2000 us. The heli profiles run at Rotorflight's
rates for digital cyclic and narrow-band tail servos, with at least 0.5 ms
between pulses. A frame rate the pulses leave no pause for is refused, and a
longer PULSE MAX is refused at a rate it does not fit. The pause is kept after
the longest pulse the coprocessor can render, the top of the range a command
carries: PULSE MAX, or past it when CENTRE is off the middle, by as much as
CENTRE lies nearer MAX than MIN. So a CENTRE that would push that top past
the rate's pause is refused too.

**A heli type, or any frame rate above 60 Hz, can destroy a servo that is not
made for it.** Choosing one opens a warning in the danger colour that names
what is chosen and what it does to a servo not made for it; it is applied only
after HOLD TO APPLY has been held for 2 s. CANCEL, a finger that slides off,
a touch loss and leaving the screen apply nothing. The type and frame rate in
force stay red on the right card, and every restart goes back to STANDARD PWM
at 50 Hz: a servo plugged in after a restart never meets a rate meant for
another one.

![The warning](img/servo-warning.png)

**The frame rate reaches the pins** through the coprocessor's SERVO page
(protocol 4.1). It applies to every PWM output whose first channel is a
surface; a PPM output keeps its own frame. It goes with every held position,
and a profile changed while the armed servo rests restates the rest with it;
one changed while an arm is on its way is the one the arm uses.
A faster rate goes after the pulse widths, once all of them have landed; any
other rate goes before them, and nothing wider goes out until it has landed,
so the pins never carry a fast rate with a slower profile's wider pulses. The
OUTPUT page says what became of it:

| Note | Meaning |
| --- | --- |
| In force | every PWM surface runs at the rate shown |
| The rate goes with the next position | not written yet |
| REFUSED | a surface shares a PWM slice with an output at another rate; the pins keep the rate they had |
| This coprocessor takes no frame rate | protocol 4.0: every PWM output runs at its binding's 50 Hz |

A coprocessor restart, and every binding written on OUTPUTS, put each slot
back at its own rate, 50 Hz for a servo; the screen sends its rate again with
its next position, against the binding then in force. A binding is not
written while the reset to each slot's own rate goes unanswered, and the bench
does not arm, from any screen, while the rate the surfaces run at is not
known: the arm is refused with `servo frame rate not known -- arm again`.

The OUTPUT settings are kept for the session; the TEST, LIMITS and DUT
settings are kept in NVS (non-volatile storage) and written as on SUPPLY.

![The automatic test's settings](img/servo-test.png)
![The limits](img/servo-limits.png)
![The name](img/servo-name.png)

Current limitations:

- Until the screen sends a position, the pins run at the rate the SERVO page
  holds, which after a restart or a new binding is 50 Hz.
- No automatic test runs in this build. The TEST, LIMITS and DUT settings are
  kept for it, and no report is written.
- The supply on the right card is SUPPLY's model: no PD mini driver exists,
  so its voltage, current and power are simulated, not measured.

## Supply

![Supply](img/supply.png)

Sets, switches and records a programmable supply: the PD mini, a USB-PD (USB
Power Delivery) trigger controlled over a UART (universal asynchronous
receiver-transmitter). No driver for the PD mini exists, because its UART
protocol is not in this repository. The panel runs a model of a supply in its
place: the header says SUPPLY MODEL and the menu tile is marked MODELLED.

The layout is the one MOTOR & ESC uses. The plot shows voltage, current and
power over the last 27 s. The rail shows the readings, the run's lowest
voltage and its highest current and power. MODE says which set point the
supply holds: CV (constant voltage) at the set voltage, or CC (constant
current) at the current limit. CC is drawn in the warning colour: a supply in
CC is not giving the load the voltage it was set to.

A set point is shown beside its reading. VOLT and CURR carry it in brackets
after the label, and the plot draws it as a dashed line in the reading's
colour, on the reading's scale. TABLE lists both. The bracketed value is the
set point the supply reports holding; while it does not answer, the one the
screen holds.

| Set point | Range | Slider step | Buttons |
| --- | --- | --- | --- |
| VOLTAGE | 3.3 to 21 V | 20 mV | 0.1 V |
| CURRENT LIMIT | 0.5 to 5 A | 50 mA | 0.1 A |

The ranges are a USB-PD PPS (Programmable Power Supply) source's widest
profile, 3.3 to 21 V at up to 5 A, narrowed by the caps in SETTINGS. A driver
reports the range its source offers, and the sliders follow it. A tap on a
track sets the value under the finger.

**A tap on the VOLT or CURR card, or on a set point's value, opens a keypad**
over the left column. It shows the range in the title row and the current
value faint until a digit is typed. OK takes a value inside the range,
rounded to the supply's step; a value outside it is refused and the range
turns to the warning colour. OK with nothing typed, and CANCEL, leave the set
point as it was.

![The keypad](img/supply-keypad.png)

**A change to a live output asks first.** While the output is on, a new set
point from the slider or its step buttons, or from the keypad, opens a
question that names the change. APPLY gives it to the supply; CANCEL drops it
and the slider goes back. A drag asks once, on the release, and the supply
holds the old set point until then. With the output off nothing is asked.
SETTINGS switches the question off for the slider and for the keypad
separately.

![The question](img/supply-confirm.png)

**OUTPUT ON is a two-second hold**, the gesture and the fade ARM uses. OUTPUT
OFF is a tap. STOP switches the output off on every screen. So does every
other stop the bench counts -- touch that stops answering, an ON whose touch
events went missing before the screen showed it, and the coprocessor refusing
to stay armed -- and a supply that stops answering, and a trip. The output
stays off until it is switched on again. Leaving the screen keeps the output
on, so a servo or an ESC fed by the supply stays powered on the screen that
tests it, and the link-lost screen, which has no STOP, does not open while
the output is on.

OUTPUT ON and OFF, RESET PEAKS and the readings stay live under the keypad,
the question and SETTINGS. One finger at a time: while one holds a control, a
second finger does nothing anywhere on the screen.

### Settings

SETTINGS, at the right of the strip above both columns, opens the supply's
settings over the left column. Each is kept in the panel's NVS (non-volatile
storage) and survives a restart. A value opens the keypad; a switch flips on
the tap. Every change is written at the next frame the bench is disarmed, the
supply's output is off and the board's photograph is not being fetched, and
with it any change on SETUP that was not saved: a flash write stalls both
cores, OUTPUT OFF and the trips included. The bottom line says SAVED, SAVE
WAITING, NOT SAVED (the write was refused), or SETUP CHANGES NOT SAVED: a
change made on SETUP and left without SAVE, which nothing writes until SAVE
there or a change here asks for it.

![The supply's settings](img/supply-settings.png)

| Setting | Range | Default | What it does |
| --- | --- | --- | --- |
| VOLTAGE MAX | 3.3 to 21 V | 21.00 V | the highest voltage a set point takes |
| CURRENT MAX | 0.5 to 5 A | 5.00 A | the highest current limit a set point takes |
| START VOLTAGE | up to VOLTAGE MAX | 6.00 V | the voltage set point after a restart |
| START CURRENT | up to CURRENT MAX | 2.00 A | the current limit after a restart |
| CURRENT TRIP | 0 to 5 A | OFF | output off once the current has been over it for TRIP TIME |
| VOLTAGE TRIP | 0 to 21 V | OFF | output off once the voltage has been over it for TRIP TIME |
| TRIP TIME | 0 to 5000 ms | 100 ms | how long a reading is over a trip before it fires |
| SLIDER AND STEPS | ON, OFF | ON | ask before the slider changes a live output |
| KEYPAD | ON, OFF | ON | ask before the keypad changes a live output |

A cap lowered under a set point brings the set point down to it at once, and a
start value with it. A typed value goes onto the setting's step the safe way: a
cap rounds down, so 12.01 V allows 12.00 V, and a trip typed above 0 is at
least one step, never OFF. A trip of 0 is off. The time over a trip counts from
the first reading over it. A reading under its trip starts the
count again; a reading that did not arrive leaves it where it was. A trip
switches the output off, MODE shows TRIP until the output is switched on
again, and the band says which trip fired. The supply holds its current limit
in CC, so a current trip at or above CURRENT LIMIT does not fire; set it below
the limit to switch off a load that draws too much for too long.

The output stays off at a restart, whatever the start values.

### The log

A run is one switch-on of the output. The plot clears when the output comes on
and holds the run after it goes off. The mAh and Wh under the switch count the
run from the readings shown, each step capped at 1 s. RESET PEAKS starts the
lowest and highest values again from the current reading.

A run is recorded to a `BENCHnnn.CSV` of its own, one row every 50 ms:

```
time (s);set (V);voltage (V);limit (A);current (A);power (W);mode;charge (mAh);energy (Wh)
```

A reading that did not arrive is an empty cell. `mode` is CV, CC or OFF, and
empty while the supply does not answer. The bench takes the log: arming during
a supply run ends that run's file, and an output still on at the disarm starts
a new file. That file's charge and energy go on from the totals counted since
the output came on, as the screen's do, so its first row does not start at 0. The supply is stepped and logged on its own 50 ms cadence while
the control task waits on the link, so an unanswered coprocessor does not thin
out the supply's plot or its log.

The model is a 6 ohm load with a 1.4 A burst for 0.6 s every 3 s, behind a
0.05 ohm source resistance. At the starting set points, 6.00 V and 2.00 A, the
burst takes it into CC. Its readings are not measured and nothing on the bench
is powered.

The PD mini's wiring -- PD mini, PD mini TX, PD mini RX and PD mini baud -- is
on SETUP under INTERFACES. No driver reads those settings.

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
`BENCH001.CSV` to `BENCH999.CSV` in the card's root directory. A run is one
arming, or one switch-on of the SUPPLY output while the bench is not armed.

The list holds 48 entries and a card holds up to 999 runs. When there are more
than fit, the list keeps the newest runs and its tab reads `48 OF 137 FILES`
instead of `FILES`, so a run that is missing from the list is a run the list
was too short for rather than a run that was never written. The number in the
name is what newest means: the panel has no clock that survives a power cycle,
so every file on the card is dated 1980-01-01. A run outranks a file the bench
did not write, so a card holding 48 or more runs lists no other file. Delete
old runs to get one back.

DELETE removes the selected file from the card. It asks first: a second panel
names the file and its size, and only its own DELETE, pressed and released on
that button, removes the file. CANCEL, or leaving the screen, closes the
question without deleting. The run the logger has open is refused. A file
that was open in the import view or the plot is dropped from both when it is
deleted. The logger numbers each run above the highest run it found on the
card. It reads the card at the first run after boot and after a run that could
not be opened, and counts on from there otherwise. A deleted number is used
again only if it was above every run left on the card at that read.

![The DELETE question](img/logs-delete.png)

A run is committed to the card every 20 rows or 1000 ms of run, whichever
comes first. Power lost mid-run costs the rows the commit has not finished
with and the rows still in the queue between the control task and the card's
own task: under 1.0 s of run while the card keeps up, and 84 rows, 4.20 s at
the panel's 20 Hz sample rate, if the card has stalled and the queue is full.
The rest of the file is readable either way. The card is written by a task of its own: an SD (Secure
Digital) card is allowed 250 ms to finish a write, and the task that beats the
safety line has a ceiling of 150 ms.

Eight messages say what a card did to a run. Each appears on the band:

| Message | What happened |
|---|---|
| `no card -- this run is not recorded` | nothing is mounted, so the run was never opened |
| `card unreadable -- run not recorded` | the card would not list, so a run number could not be chosen |
| `card full or unwritable -- run not recorded` | no run number could be created |
| `the card did not keep up -- run not recorded` | every row was dropped, so there is no file for this run at all |
| `the card fell behind -- the log has gaps` | some rows were dropped; the file's time column shows where |
| `the card stopped taking rows -- run not recorded past here` | a write failed mid-run; every later row is rejected |
| `the card stopped taking rows -- the log is short` | the same failure, said again when the run closes |
| `the card failed on the last write -- the log is short` | the close failed, so the rows since the last commit are not in the file |

The first four mean there is no file to look for. The last four mean there is
one and it stops early.

A run being written is not offered in LOGS. A file's length lives in its
directory entry and is written when the file is closed, so a run still open
would list at its last committed length and read as a finished one.

## Setup

![Setup](img/setup.png)

Settings are behind the SETUP tile, in both themes:

![Setup in the light theme](img/setup-light.png)

### Keeping the values

A changed value takes effect at once and is not written to flash until SAVE is
pressed. The button under RESET CATEGORY says which of three states the
screen is in:

| Label | Meaning |
| --- | --- |
| `SAVED` | Nothing is unwritten. The button is inert. |
| `SAVE` | Something is unwritten. Pressing it asks for a write. |
| `WHEN IDLE` | A write has been asked for and is waiting for a moment to happen in. |
| `NOT SAVED` | The store refused the write. What reached the medium is not known from the screen: a refusal on one key leaves the keys written before it committed, so the next boot can load a mix of the new values and the old. Pressing it tries again. |

![A changed value, with SAVE offered](img/setup-dirty.png)

The press asks; it does not write. Writing settings commits a page of NVS
(non-volatile storage), and a flash operation on the ESP32-S3 disables the
instruction cache, so neither core runs for its duration. The write is taken
on the first frame at which the bench is disarmed and no board photograph is
being fetched or stored. On the settings screen that is the next frame, and
the label goes to `SAVED` as fast as the eye follows the press. Armed, the
request stands as `WHEN IDLE` until the bench is disarmed.

A store that refuses leaves the label at `NOT SAVED` in the danger colour
until the next successful write or the next edit. A refusal does not undo what
was already written: the values are set key by key and a failure part way
through leaves the earlier keys committed, so the medium can hold a mix of the
new values and the old. The screen cannot say which. A panel whose NVS could
not be brought up at all refuses every write of the session, writes nothing,
and says so once more on the splash as `NVS unavailable`.

Values not saved are kept until the panel is switched off. Leaving the screen
writes nothing.

## The bus-fault screen

The panel runs the CAN (Controller Area Network) echo self-test at every
start-up, for 1200 ms inside the splash. A verdict other than every probe
coming back intact puts this on the panel instead of the menu.

![Frames cross and arrive changed](img/busfault.png)

It exists because the fault is invisible from every other screen: a bus that
does not carry frames looks exactly like a coprocessor that is not fitted, and
both look like a bench that shows no numbers. The verdict is the heading, the
list is what to check in the order that costs least to check, and the right
column is what both ends counted.

![Nothing came back](img/busfault-silent.png)

Leaving it takes a two-second hold, the ARM gesture and the same fade.
Acknowledging repairs nothing: the bench runs in simulation, nothing drives an
output, and the test runs again at the next start-up. There is no band and no
STOP, because nothing can be armed behind it.

### A link that stops

The same screen carries the other half: a link that was up and has been gone
for 4 s. The heading is what this panel's own CAN controller is doing, because
the wire carried frames a moment ago.

![The panel is off the bus](img/busfault-lost.png)

| Heading | Meaning |
| --- | --- |
| `this panel is off the bus` | too many frames went unacknowledged; it stopped transmitting |
| `this panel is rejoining the bus` | it is counting the quiet time a rejoin needs, about 3 s |
| `this panel's controller has stopped` | idle and not restarted — a fault in the firmware |
| `the link stopped answering` | the controller is on the bus and nothing answers |
| `the controller cannot be read` | the driver is not running; nothing can be sent |

Four seconds, not one: the link drops for a poll now and then, and a screen
that took over on every blip is a screen operators learn to dismiss.

**Never while armed.** The screen has no STOP, and a bench with something
spinning must not have its stop button covered by a diagnosis. Armed, the
alert band says the link is gone and the screen waits for the disarm.

The same numbers go to `RCBENCH.LOG` on the SD card, one line per report while
the link is down:

    t=182s link=down for 47s  bus=OFF tx_err=248 rx_err=0 bus_err=1976 rejoins=44/44  polls=5323 replies=5279 timeouts=44

The fields are the same in the same order every time. `rejoins` is this outage
over the total since boot, and an unreadable controller writes `?` in its
columns rather than a differently shaped line: the reading that most needs a
timestamp is the one where the controller would not answer.

The card is there because the panel's console is not reachable on every board.
The native USB socket carries GPIO19 and GPIO20, which the multiplexer hands to
CAN about a second into boot, so it is gone before a bench fault happens. The
bridged socket is on UART0 and normally carries the console right through, but
what it is connected to is switchable: a slide switch beside the BOOT and
RESET buttons is marked UART1 and UART2. In one position the bridge chip
enumerates and passes nothing in either direction, and the panel then has no
console at all.

[Bringing up the link](Link.md) has the verdicts and what each one means.

## Outputs

Behind the OUTPUTS key on the Setup screen. The protocols bound to the
coprocessor's pins, and which pins each one drives.

![Outputs](img/outputs.png)

A set of pins per protocol, not eight independent slots: a bench is wired a
protocol at a time — four servo leads, then one ESC (electronic speed
controller) — and asking for the protocol and then for its pins is the shape
of that job. More than one protocol can be bound at once, and a pin belongs to
at most one of them.

Each ticked pin becomes one slot on the [OUTPUTS page](Link.md#page-map), in
pin order across every protocol, taking channels from zero upward — so the
lowest ticked pin is channel 0 whatever order the screen was touched in and
whichever protocol holds it. Eight slots and eight channels are the budget,
shared. PPM renders eight channels on its one pin, so a bench with PPM on it
has room for nothing else. It runs at 40 Hz, not the 50 Hz the pulse drivers
use: eight channels need 23,300 us of frame and 50 Hz gives 20,000.

SERVO PWM and MOTOR PWM are the same pulse at the same 50 Hz and differ in
what the channel is for. A servo channel centres when nothing commands it; a
motor channel stops. Nothing in the pulse says which is on the pin, so the
entry says it: bind an ESC as MOTOR PWM and a control surface as SERVO PWM.
The MOTOR & ESC throttle drives every channel bound as a motor — MOTOR PWM
and the DShot entries — and leaves the servo channels alone.

A MOTOR PWM channel sends 0 % throttle as the Idle pulse and 100 % as the
Full pulse: 1000 us and 2000 us by default, set under SETUP, ESC / BENCH, in
steps of 10 us (Idle pulse 800 to 1600 us, Full pulse 1400 to 2400 us). The
two settings reach the channels bound as a motor and no others: a SERVO PWM
channel keeps 1000 to 2000 us, or the range the SERVO screen sends for the
servo chosen there. An edit reaches the coprocessor 300 ms after the last
change while the bench is disarmed; one made while it is armed waits for the
disarm. A coprocessor that links up is brought to the two settings before
the bench drives it: on a bench already armed, the coprocessor is held
disarmed until it has them. An Idle pulse that is not below the Full pulse is not sent, and the
band says `idle pulse must be below full pulse -- not sent`.

An ESC whose throttle range was calibrated on a transmitter takes that
transmitter's shortest pulse as zero. An Idle pulse above it reads to the ESC
as throttle not at the bottom, and the ESC does not arm; many ESCs beep
rapidly in that state. Set the Idle pulse at or below the transmitter's
shortest pulse and the Full pulse at or above its longest: for a transmitter
that sends 985 to 2012 us, 980 us and 2020 us.

When the protocol can take no more pins, the reason is under it in amber:
`NEEDS 8 CHANNELS, 4 FREE`, `ALL 8 SLOTS IN USE`, or `SERVO PWM TAKES 8
PINS`. A board drawn entirely in grey with nothing beside it reads as a
fault, and PPM greys the whole board whenever anything else is bound.

![PPM with servo pins already bound](img/outputs-full.png)

The protocol is a list rather than a stepper, because there are eight of them
and stepping past seven to reach the eighth is not choosing:

![The protocol list](img/outputs-protocol.png)

Reserved pins are shown and cannot be ticked. GP3 carries the safety heartbeat
and GP8 to GP12 are the CAN (Controller Area Network) controller, and each
says so under its name. Hiding them would leave an operator hunting for GP10
and finding a gap. [DShot and the output drivers](DShot.md#which-pin) has the
whole map.

The pad number under each pin is the one printed on the board, so an operator
counting pads and an operator reading GPIO (general-purpose input/output)
numbers arrive at the same pin.

Changing anything writes the pages at once. There is no APPLY key: a screen
holding a choice that has not been sent is a screen that disagrees with the
bench, with nothing to say which of the two is driving. What became of the
write is under the protocol — WRITTEN, NO LINK, or REFUSED.

Switching protocol says which set is being edited. Nothing is dropped: the
pins ticked for the protocol being left stay bound, and the pins of the one
arrived at come back as they were. A selector that retargeted the current pins
would make binding a second protocol mean unbinding the first.

A pin another protocol holds is drawn greyed, with that protocol's name under
it where a free pin shows its pad number. That is a choice, undone by going to
that protocol and unticking it there — unlike a reserved pin, which is drawn
red and struck through because it is the wiring rather than a choice.

Four servo leads and an ESC, with DShot600 the protocol being edited. GP5 is
ticked; GP0, GP1, GP2 and GP4 say SERVO PWM and cannot be ticked here; GP3 and
GP8 to GP12 are red because the coprocessor reserves them:

![Outputs with pins held by another protocol](img/outputs-held.png)

So a cell is in one of four states, and each says what to do about it: ticked
in this protocol, held by another and named, reserved and struck through, or
free and showing its pad number.

### Pick a pin

Behind the PICK A PIN key on the Setup screen, and the same binding the
Outputs screen holds. The list answers "which GPIO is bound"; this answers
"where do I put the lead".

![The pin picker](img/picker.png)

The buttons are not the pads. At any size that fits a 480-pixel panel a pad is
under 40 pixels across, which is smaller than a fingertip, so the pads are
drawn where they are and the touching happens on staggered rows of buttons
beside the board, each on a straight trace to its own pad.

A button is coloured the way its cell is on the Outputs screen: this
protocol's pins are accented, a pin another protocol holds is grey, and a pin
the coprocessor reserves has no button at all — it is crossed on the pad,
because a button under a pin that cannot be chosen says it could be.

Down the left are this protocol's pins in channel order; down the right are
the pins other protocols hold, named. The two together read as one run of
channels, because that is what the OUTPUTS page carries.

Where the pads are comes from the board, not from the panel: the [shape
page](Link.md#page-map) carries the outline, the pitch and the corner pad 1
sits at. A board that does not say is not drawn at all — a picture from a
guessed shape points at the wrong pad as confidently as the right one, and
this screen's whole job is finding a pad on the board in front of you. Its
pins are still on the Outputs screen.

The photograph is separate again. With one, the board on screen is the board
in your hands; without one, the outline and every pad are drawn from the
shape and the buttons are in the same places:

![The picker without a photograph](img/picker-drawn.png)

The photograph is fetched over the link once per board and kept in the
panel's flash, so it costs about ten seconds the first time a board is seen
and nothing after that. A board with no photograph, or one whose transfer has
not finished, is drawn rather than left blank.

A servo lead has three wires and the buttons describe one. The other two are
marked on the board itself: a ground carries a white **G**, and a rail carries
its voltage — **5V0**, **3V3**. A rail that is an input rather than a fixed
voltage carries **PWR** instead, because a number that is only sometimes true
is worse here than no number. Pads that are neither, like RUN, are dotted and
left unlabelled.

They are marked inside the outline on a short trace, at two depths so a run of
rails at one end of a row does not draw one label over the next. Inside is the
only room left: the space beside the board belongs to the buttons, and a mark
on the pad itself would be as small as the pad.

These come from the [pads page](Link.md#page-map), which is separate from the
catalogue because they do not fit in it — a page is 32 registers and a
40-pad board has more pads than that between the two. A board that does not
serve it has its grounds and rails unmarked, and a lead is placed by reading
the board rather than the screen.

### The coprocessor keeps it, not the panel

A binding describes wiring, and the panel is not the board the wires are in.
The coprocessor writes the OUTPUTS and CHAN_CFG pages to its own flash and
restores them at boot; the panel never stores a binding and never sends one
unasked. When the link comes up the panel reads the page and shows what is
configured over there. After a panel restart, what this screen shows and what
is driving pins are the same thing.

Restoring configures the outputs. It does not drive them: every driver is
gated on the bank being armed, which the coprocessor grants only while the ARM
register is set, the link is out of failsafe and the heartbeat is trusted, so
a restored binding claims its pins and holds them at idle until somebody arms.
Channel commands are not restored — a configuration survives a power cycle and
a throttle position does not.

The save waits for the bench to stop driving, and then for a gap in the
traffic. Writing flash stops the coprocessor with interrupts off, and it
answers nothing while it is stopped: on the bring-up module a save that
erased and programmed inside one window measured 19,178 us, against a CAN
frame of about 130 us and two frames of buffer in the controller. The erase
and the page program have not been timed apart from each other. A request lost
in that window costs the panel 1000 ms of waiting, which is past the
coprocessor's 200 ms silence failsafe, so one lost frame ends as `FAULT 01`
(`LINK_FAULT_LINK_SILENT`) over a cable with nothing wrong with it.

So the sector is not erased per save. Two sectors hold sixteen records each; a
save writes the next record, and a sector is erased only once every record in
it has been superseded. That erase is taken before the save that needs it: at
boot before the coprocessor starts answering, or on a pass after the save that
first writes into the other sector, once the bus has been quiet for 5 ms. It
is the sector left behind that is erased, not the one just filled, so the
erase and the save that needs it are separated by the fifteen saves in
between. Fifteen saves in
sixteen therefore cost one page program and no erase. How long a page program
takes on the module's flash is not measured; the console line printed after
each save carries it.

A power cut during a save leaves the binding from before it. Each record
carries the number of 0 bits it holds, beside that number complemented. An
erase sets bits and a program clears them, so a record caught in either holds
fewer 0 bits than it claims, and the claim cannot survive a partial write in
either direction. The record being written is therefore rejected rather than
probably rejected, the record before it is still the newest good one, and the
sector being erased is never the one holding the record still wanted.

A page the screen cannot describe — two protocols at once, a rate no entry
offers, a pin that is not on the header — reads back as nothing configured
rather than as a selection that disagrees with the page it came from.

## Balancing

Described on its own page: [Balancing](Balance.md).

## Screens that are not ready

A tile marked SOON names what the screen will do and the part or decision it
waits on.
