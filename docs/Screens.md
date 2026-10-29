# Screens

<sub>**English** · [Deutsch](Screens-de.md)</sub>

What is on every screen, what the menu marks mean, and how each screen is
operated.

## The status band

The top band is shared by every screen. From the right: STOP, the run clock
(while ARMED or after a run), the arm chip, a FAULT code when one is reported,
the mode, and LINK or NO LINK. The mode is BENCH while the link is up and the
panel drives the IOmcu's outputs, and SIM while no IOmcu answers
and the panel runs on its own models. INJECT replaces BENCH and SIM while a
touch injector answers at I2C (Inter-Integrated Circuit) address 0x14; the
panel looks for one once, at start-up.

The arm chip names what the switch to ARMED drives:

| Chip | Outputs that drive |
| --- | --- |
| SAFE | none |
| ARMED MOTOR | the throttle outputs, from MOTOR & ESC's ARM or a stick run on its PROGRAM; no surface and no programming port |
| ARMED SERVO | the surfaces and a pulsing PROG PORT, from SERVO; no throttle output |
| ARMED | every bound output, on an IOmcu older than protocol 4.18 |

STOP works on every screen. It switches to SAFE and latches: the bench stays SAFE
until it is ARMED again. Navigating away, an alert expiring or the link
recovering does not clear a stop.

ARM is at the bottom of a bench screen; STOP is at the top of the band.

A touch belongs to what it came down on until it lifts. The band is the top
60px of the panel.

- A touch that comes down on the band stays the band's. Sliding it down into
  the screen moves nothing there, and lifting it there presses nothing.
- A touch that comes down on the screen and slides up into the band is
  released at the last point it had on the screen. A drag on a slider or on
  the SERVO dial ends with the value it had at that point, and the distance
  travelled on the band is not added. A hold on ARM or OUTPUT ON is abandoned.
  A button, a key, a tile, a tab or a list row under that point is not
  pressed: the release is the panel's and not the finger's. Three controls
  take it, because they stop something: DISARM, OUTPUT OFF and START TEST
  while a servo test runs. The touch is not picked up again when the finger
  comes back down onto the screen; lift and press again.
- A contact that is more than 120px (the x and the y distance added) from
  where the previous report had it is taken as a lift there and a new press
  here. The touch controller is read every 10ms; its report interval on the
  panel is not measured. That lift presses nothing either, with the same
  three exceptions and STOP. The new press is a press like any other, except
  on a key of the SETUP list, where it scrolls and steps nothing.
- STOP and the home tag answer to a press that lands on them. A finger that
  slides onto STOP from the screen stops nothing.
- A second finger on STOP, on the home tag or on the alert band is answered
  while the first finger drags. After the home tag, the finger still on the
  glass moves nothing on the screen the tag leads to.

STOP is 168 x 48px at the right end of the band, from x 850. The chips
stand from STOP to the left, 8px from it and 6px apart. Each chip is 44px
tall, with its label in the 10 x 20px face, 28px in from the chip's left
edge. The widest row, a run clock of 10 hours, ARMED MOTOR, a FAULT code,
BENCH and LINK in German, starts at x 276.

The home tag is 44px tall at x 8, y 8, its name in the 20 x 36px face. The
home tags end at x 268 (MOTOR & ESC) and at x 248 or less on the other
screens, so no chip row reaches under a home tag.

### Chip help

A tap on a chip of the band opens a box under the band that says what the
chip's state means for the bench and what to do: LINK or NO LINK, the mode,
FAULT, the arm chip and the run clock. The box opens 4px under the band at
the tapped chip's left edge, moved left as far as it needs to stay 8px
inside the panel. It is 640px wide, 60 characters of the 10 x 20px face
on 21px lines; FAULT's box spans the panel (1008px, 97 characters), with
one paragraph per set bit. No box reaches into the band, so none covers
STOP, and none reaches the alert band.

- A tap opens it: press and lift on the same chip. A press that slides off
  the chip before it lifts opens nothing.
- The text follows the state while the box is open. A fault that appears
  shows at once, one that clears goes, and a box follows its chip when the
  run clock appears and moves the chips.
- A box whose chip goes closes: FAULT when the faults clear, the run clock
  when it goes.
- A tap on another chip closes the box and opens that chip's box, in one
  tap. A press on the other chip that slides off it before it lifts closes
  the box and opens nothing.
- Any other tap closes it and does nothing else: on the box's own chip, on
  the band beside the chips, on the home tag or on the screen. A tile, a
  button or the alert band under the finger is not pressed.
- STOP is the exception: a press on STOP closes the box and stops at once,
  as with no box.
- So is a control that stops what the bench drives: DISARM, OUTPUT OFF, a
  stick run's ABORT, STOP TEST and STOP RUN, and START TEST or START LIMIT
  RUN while their run is under way. A press on one closes the box and acts,
  as with no box. ARM and OUTPUT ON are not among them: a press there closes
  the box and starts no hold.
- Leaving the screen closes it.
- A finger dragging on the screen when a second finger opens a box is
  released as at the band's edge.

While the link is up and the IOmcu has refused link frames, the LINK
box ends with the count, the same line MOTOR & ESC shows in its header
strip: `Rejected link frames: 37` ([Refused frames](Link.md#refused-frames)).
The line follows the count while the box is open and goes with the link.

The FAULT box names each bit, its code and what it means:

| Code | Bit | Set by this release |
| --- | --- | --- |
| 01 | LINK_SILENT | the IOmcu heard no request for 200ms; stays until the next arm |
| 02 | OVERCURRENT | never |
| 04 | OVERTEMP | never |
| 08 | STALL | never |
| 10 | HEARTBEAT | the safety line from the panel stopped: after any STOP until the next arm, a pressed emergency stop or an open heartbeat wire |
| 20 | VERSION | never |
| 40 | STORE_OFF | the IOmcu cannot write its flash this boot |

For a bit this release never sets, or one the panel has no name for, the box
says to flash both boards from one release.

![The FAULT box with LINK_SILENT, HEARTBEAT and STORE_OFF set](img/band-fault.png)

![The LINK box](img/band-link.png)

![The LINK box with 37 refused link frames](img/band-link-refused.png)

![The SAFE box](img/band-safe.png)

![The ARMED MOTOR box](img/band-armed.png)

## The alert band

A fault the screen itself does not show appears in a red band, 44px high,
across the bottom of every screen that has the status band. Its text is in the
10 x 20px face, at most 97 characters a line on up to 2 lines. It covers the
bottom 44px of the screen while it shows; ARM and every control that stops
the bench end above it.

- It clears 30s after the frame it arrived in. A new alert, even with the
  same text, starts the 30s again.
- A tap on the band clears it earlier: press and lift on the band, marked
  with an `x` at its right end. The screen beneath receives neither that tap
  nor a second finger's on the band; only the first finger's lift clears.
- An alert that arrives while a finger is on the band is not cleared by
  that lift.
- The controls that stop what the bench drives end above the band, panel
  y 556 to 599: DISARM, OUTPUT OFF, a stick run's ABORT (y 496 to 551),
  its ABORT on a manual step (y 444 to 491), STOP TEST and STOP RUN. A press
  on one with an alert showing is its own, and the alert stays. A stop
  control a screen draws under the band acts on the first press, and the
  press clears the band; ARM and OUTPUT ON under the band are not pressed:
  the tap clears the band and starts no hold.
- An alert that arrives while a finger is already down on a control leaves
  that press to the control: its release acts as with no alert.
- "Touchscreen not responding. ARMED is blocked.", shown at start, stays
  until restart and has no `x`: no touch is there to tap with. A later alert
  shows over it; once that one clears, it shows again.

## Menu marks

![The feature menu](img/overview.png)

The menu is a grid of 4 columns by 2 rows, each tile 243 x 255px with
10px margins. A tile carries a 40px icon, its name in the 20 x 36px face
and a line in the 10 x 20px face. The first row holds the workbenches, MOTOR & ESC, SERVO and
SUPPLY; the second the instruments, RECEIVER, BATTERY and BALANCE; the last
column LOGS over SETUP. A fourth workbench or a fourth instrument fails the
build at 4 columns (`shared/ui/overview_screen.c`). Programming has no tile:
it is the PROGRAM task of MOTOR & ESC and of SERVO.

Each tile carries a line under its name, 2 rows of at most 22 characters,
8px clear of the tile's sides:

| Tile | Line |
| --- | --- |
| MOTOR & ESC | drive, V/A, rpm |
| SERVO | travel, current, program |
| SUPPLY | set and log V/A |
| LOGS | runs and files |
| RECEIVER | S.BUS, PPM, frames |
| BATTERY | cells, iR, mAh |
| BALANCE | vibration, phase |
| SETUP | pins, sensors, panel |

A tile marked SIMULATED opens a screen whose source the bench does not
report: the IOmcu reports no capability for it, or its sensor is not
enabled on SETUP under INTERFACES. A screen without its hardware opens and runs
from the model. The mark does not classify each value on a screen. RECEIVER
and BATTERY carry the mark and show no values. SERVO without the mark shows the
panel's supply model as its rail meter when neither a PD mini nor an INA3221 is
enabled. Each screen names the source of its readings itself.

| Tile | Without the mark when |
| --- | --- |
| MOTOR & ESC | the IOmcu drives ESCs and holds an INA228 set-up enabled on SETUP, INTERFACES |
| SERVO | the IOmcu drives servo PWM outputs |
| SUPPLY | the PD mini is enabled on SETUP, INTERFACES, whatever the IOmcu reports; otherwise the panel runs its own model of a supply |
| RECEIVER, BATTERY, BALANCE | never in this release: no IOmcu reports these capabilities |
| LOGS, SETUP | always: they hold no measurement |

The capability bits come from the IOmcu at bring-up. With no IOmcu
answering, MOTOR & ESC and SERVO carry the mark too.

The menu in the light theme:

![The menu in the light theme](img/overview-light.png)

## Keypad, keyboard and the shared controls

The keypad, the keyboard, the choice lists, the tabs and the TARGET row look
the same on every screen. Each screen gives them their area or their row;
the faces and the spacing inside are theirs.

- **Keypad.** A title row of 32px, the entry of 60px, then 4 x 4 keys
  8px apart, 8px from the area's edge, the keys ending 4px above the
  alert band at the latest (below). On SERVO a key is 145 x 88px
  (21.8 x 13.2mm); on SUPPLY 145 x 76px. The title and the range are in the
  10 x 20 face, or in 8 x 16 where the two do not fit side by side. The
  entry is in the 32 x 44 numerals with the unit in 12 x 24; an entry that
  does not fit takes the 24 x 30 numerals with the unit in 10 x 20. The
  digits and the point are in the largest of 20 x 36, 16 x 28, 12 x 24
  and 10 x 20 whose cell fits a digit key with 6px each side, else in
  8 x 16; 20 x 36 needs a key of 32 x 48px and is the face of every
  keypad the panel opens. DEL, CLR, CANCEL, OK and `+/-` share one face:
  the largest of the same faces, never larger than the digits', in which
  all of them fit their keys with 6px each side. `CANCEL` needs a key of
  132px for 20 x 36. German `ABBRECHEN` needs 192px for 20 x 36, 156px
  for 16 x 28, 120px for 12 x 24 and 102px for 10 x 20, so the German
  keypads of SERVO, SUPPLY and MOTOR & ESC's SETTINGS draw their words in
  12 x 24.
- **Keyboard.** A title row of 32px, the text field of 52px, then 5 rows
  of keys 6px apart. The title and the count are in 10 x 20, or in 8 x 16
  where the two do not fit side by side. The text is in the 20 x 36 face
  when 23 characters and the caret fit the field, else in 16 x 28. The
  letters, digits and marks are in the largest of 20 x 36, 16 x 28,
  12 x 24 and 10 x 20 whose cell fits a character key with 4px each side,
  else in 8 x 16; 20 x 36 needs a key of 28 x 44px and is the face of
  every keyboard the panel opens. The word keys (the bottom row, DEL,
  `ABC`, `abc`, `#+=`) share one face: the largest of the same faces,
  never larger than the characters', in which all of them fit their keys
  with 4px each side, HIDE measured in place of SHOW too, so a press does
  not change it. On WI-FI the keyboard takes the body above the alert
  band, 1008 x 484px, with keys of 93 x 71px (14.0 x 10.7mm) and every
  word in 20 x 36; the German password's in 16 x 28, as `LEERZEICHEN`
  has two keys of 192px. SERVO's name keyboards, keys of 55 x 72px, draw
  their words in 12 x 24; the ESC STICK search keyboard, keys of
  59 x 57px, in 16 x 28. The text editor's keyboard has no title row, no
  field and no digit row: 4 rows of keys of 93 x 56px, every word in
  20 x 36, `123` one more word key
  ([The text editor](#the-text-editor)).
- **Choice list.** A face or a row 56px tall or taller draws its text in
  the 20 x 36 face, 12px in, with an 11px chevron 28px from the face's
  right edge. A shorter one draws in 16 x 28, 10px in, with a 7px chevron
  22px from the edge. A text that does not fit before the chevron in
  20 x 36 draws in 16 x 28: `DSHOT600 BIDIR` on a face of 292px. Each face
  and row picks its own, so the rows of one list can differ. OUTPUTS and
  PICK A PIN draw theirs under 56px.
- **Tabs.** A row of tabs 32px tall or taller draws its labels in the
  10 x 20 face when every label fits its tab with 6px each side in the
  language shown; another row draws them in 8 x 16. LOGS' ROOT, USER and
  BENCH and the PROG PORT entry's segments on OUTPUTS are in 10 x 20. The
  strips of SERVO and MOTOR & ESC are 40px tall and draw in 10 x 20;
  SUPPLY's is 24px tall and keeps 8 x 16. RECEIVER's and BALANCE's tabs
  are 150 x 48px and draw in 10 x 20.
- **TARGET row.** A row 40px tall or taller draws its label and its value
  in 10 x 20, with 88px for the label, and counts the pins it names in
  10px cells. A shorter row draws in 8 x 16 with 64px for the label and
  counts in 8px cells. SERVO and MOTOR & ESC draw theirs 48px tall.
- **Plot.** A plot keeps 1,024 samples a series, one for each pixel column
  of a plot as wide as the panel: 51.2s at 20 samples a second.

Key grids end 4px above the alert band, at body y 492, on every screen.
A keypad or a keyboard whose area reaches further paints its whole area
and stops its keys there, so an alert covers no key. On SERVO the keypads'
keys are then 145 x 88px and the keyboards' 55 x 72px; on SUPPLY the
keypad's are 145 x 76px; on the ESC STICK search keyboard 59 x 57px; on
MOTOR & ESC's SETTINGS the keypad's are 145 x 89px; on BALANCE the blade
count's keypad's are 242 x 72px.

## Splash

![The splash](img/splash.png)

The splash fills the panel, without the band. The mark `rcbench` is the
20 x 36px face at twice its size on an accent tag 88px tall, centred at
the top. The tagline sits under it in the 12 x 24px face. Each step has a
row of 40px from y 200: its name, its mark (`OK`, `--`, `!!`, `..` while
it runs) and its detail, all in the 12 x 24px face. A detail starts at
x 432 and is cut 24px short of the right edge, after 47 characters.

Each subsystem reports its result as it comes up: board, display, touch, SD
card, settings, link, IOmcu. The board line carries the panel's own
firmware version, and the IOmcu line carries the protocol version and
the firmware version that the IOmcu reported: two boards can speak
the same protocol and be different builds, and this is where that shows.

A missing card or an unusable NVS (non-volatile storage) is a warning. The
SD card line reads `no card`, `not FAT32/exFAT` (FAT: file allocation table) or
`unreadable`
([The SD card's format](#the-sd-cards-format)). A
touch controller that does not answer, or an IOmcu with a different
protocol major version, is a failure and the bench does not switch to ARMED.

When every step has answered, the splash hands over to the menu after a hold
of 1.6s; a tap skips the hold.

## Motor & ESC

![Motor and ESC](img/motor.png)

MOTOR & ESC is a workbench: a strip of tasks at the top, LIVE, PROGRAM and
SETTINGS (German `LIVE`, `PROGRAMMIEREN`, `OPTIONEN`), each 150 x 40px from
y 8 of the body, and a page under it from y 56. The menu's tile always opens LIVE. A tap on a tab switches
the task; a switch is no navigation level and leaves LIVE's view, PLOT or
TABLE, as it was.

| Task | The left column, 620px |
| --- | --- |
| LIVE | the plot or the table over the throttle |
| PROGRAM | the list of ESC programmers; a row opens its session over the whole body ([PROGRAM](#program)) |
| SETTINGS | the 8 ESC settings under PACK, MOTOR and THROTTLE OUTPUT ([Settings](#motor--esc-settings)) |

Two columns, as on SERVO. The page takes the left 620px, the rail the right
380px, so reading the numbers and working the throttle do not compete for
the same part of the screen. The rail holds, top down, the three
temperatures, the four readouts, 56px each in 32 x 44 numerals, and the
CONTROL card: its title line with the run's mAh and Wh, the TARGET row
(48px), RESET PEAKS (48px) and ARM (60px). The TARGET row and ARM sit
where SERVO's output card has its TARGET row and ARM: ARM from body y 424 to
484, 8px above the alert band. The rail is the same
on LIVE, PROGRAM's list and SETTINGS, and ARM works on all three; a session
opened from PROGRAM covers it. With the rotary knob on, turning it moves the
throttle on LIVE ([the knob](#panel-the-rotary-knob)); on PROGRAM and
SETTINGS a turn moves nothing.

The home tag climbs one level at a time:

| From | The home tag leads to |
| --- | --- |
| the keypad on SETTINGS | SETTINGS; the value is not changed |
| a session on PROGRAM | one level up in the session; from its top, PROGRAM's list ([PROGRAM](#program)) |
| LIVE, PROGRAM's list or SETTINGS | the menu; the bench switches to SAFE |

### TARGET names the protocol and the pins

The CONTROL card opens with the TARGET row: the label, 88px, and a value box
of 25 characters that names what the throttle drives, the protocol and then
the pins, `DSHOT300 GPIO 0 GPIO 1`. They are the pins bound as MOTOR PWM or as a
DShot protocol, read from the same SETUP > OUTPUTS pages the IOmcu's throttle
role reads. The pins are in pin order; those that do not fit are counted.
With GPIO 0 to GPIO 7 bound as DSHOT300 it reads `DSHOT300 GPIO 0 GPIO 1 +6`.

A programming port whose FUNCTION is DSHOT150, DSHOT300 or DSHOT600 and whose
pin the IOmcu holds follows the pins as `PORT n`, and counts with them. A
port alone is named in full where it fits, `DSHOT300 PROG PORT 2 GPIO 14` (28
characters), as `DSHOT300 PORT 2 GPIO 14` (23 characters) in the CONTROL
card's 25, and as `DSHOT300 PORT 2` in a shorter field.

| TARGET reads | Condition |
| --- | --- |
| `DSHOT300 GPIO 0 GPIO 1` | one protocol drives every throttle pin |
| `MIXED GPIO 0 GPIO 4` | the throttle pins run on two or more protocols, or one of them on a rate no OUTPUTS protocol has |
| `GPIO 0 GPIO 1` | every throttle pin runs on a rate no OUTPUTS protocol has |
| `NO MOTOR PIN` | the binding is read and neither a pin nor a programming port is bound as a motor; warning colour |
| `PINS NOT READ` | no binding was read: the link is down, the pages did not read, or no pin map exists for the board; warning colour |

With DShot programming ports:

| TARGET reads | Condition |
| --- | --- |
| `DSHOT300 PORT 2 GPIO 14` | a DSHOT300 programming port on GPIO 14 drives the throttle and no pin is bound as a motor |
| `DSHOT300 GPIO 0 PORT 2` | GPIO 0 bound as DSHOT300 and a DSHOT300 port |
| `MIXED GPIO 0 PORT 2` | the pins and the ports run at different rates, or the pins at a rate no catalogue protocol has |

![TARGET names PROG PORT 2 as DSHOT300](img/motor-port.png)

The throttle still moves in the two warning states. ARM switches the whole
bank to ARMED, not only the pins named here.

A tap on TARGET opens [OUTPUTS](#outputs) on the protocol the throttle's
pins are bound as, on the PROG PORT entry of a DShot port that drives the
throttle alone, on the overview while they are bound as several or none
is known. Leaving switches the bench to SAFE,
as every way off the screen does. The home tag on OUTPUTS returns to MOTOR &
ESC on the task TARGET was tapped on; any other screen opened in between
makes the next entry a tile's, on LIVE. TARGET takes no tap, and is drawn
with the locked edge, while any of these holds:

| Condition | Why |
| --- | --- |
| ARMED, an ARM asked and not answered, or a DISARM asked and not answered | a binding changes the pins the bench drives |
| ARM held | the press is ARM's |
| the [servo programmer](ServoProgrammer.md) holds a port | ARM is locked for the same reason |
### The plot covers one run

![Motor and ESC, telemetry held](img/motor-held.png)

The trace is the record of a run. It advances only while the bench is ARMED:
switching to ARMED clears it, switching to SAFE holds it as it stood. The panel tag reads
`LIVE TELEMETRY` while it advances, `LAST TELEMETRY` while it holds a run, and
`TELEMETRY IDLE` before the first switch to ARMED, when the plot reads `No run recorded yet.`.
The right-hand axis label reads `NOW` while it advances and `END` where a held
one stopped. The readouts, the TABLE pane, the totals and the temperature
strip are live at all times, ARMED or not.

The plot is 600 x 184px. The window is 600 columns at 20 samples per second,
30.0s. A longer run holds its last 30.0s; what came before is in the CSV
file and not on the screen.

The trace ends at the switch to SAFE, not when the motor stops. Switching to SAFE commands
zero throttle and the propeller coasts down after it. That coast-down is on
the readouts and not on the plot, and not in the CSV file either, which closes
on the same edge.

Switching to ARMED clears the plot, and a latched stop is cleared by switching to ARMED, so the trace
of a run a STOP ended does not survive making the bench usable again. One
switching to ARMED writes one CSV file, and LOGS plots it.

On LIVE the telemetry card carries PLOT and TABLE under its title, two tabs
in 200 x 40px, and beside them the poll rate and the link's error count
(cyclic redundancy check failures and resyncs added together). While the
link is up and the IOmcu has refused link frames since its power-up, the
line under them shows how many, in 8 x 16: `Rejected link frames: 37`. An
IOmcu older than protocol 4.21 sends no count, and the line shows none
([Refused frames](Link.md#refused-frames)). The three temperatures are on
the line over the rail on every task. ESC and MOT come from the bench page.
MCU is the panel's own die, read from the ESP32-P4's sensor: it is the
display board's temperature, not the IOmcu's.

![MOTOR & ESC with 37 refused link frames](img/motor-refused.png)

The throttle moves by how far a finger travels, not to where it lands. A press
on the track commands nothing, so a touch at the far end asks for nothing; a
drag across the whole track asks for the whole span, and the pin follows it
unramped. `-1` and `+1` at the ends of the track step one percentage point.

The throttle changes only while the bench is ARMED. What each control does:

| Control | SAFE | ARMED |
| --- | --- | --- |
| The track | refused: no command, the slider and the readout stay; drawn dimmed | moves the throttle by the finger's travel |
| `-1`, `+1` | refused; drawn dimmed | step one percentage point |
| Rotary knob | refused; the turn is not kept | moves the throttle from its value |
| ARM | switches the throttle outputs to ARMED after its 2s hold; drawn disabled and without effect while the [servo programmer](ServoProgrammer.md) holds a port | switches to SAFE on a tap |
| RESET PEAKS | resets the peaks | resets the peaks |
| PLOT, TABLE | switch LIVE's view | switch LIVE's view |

While SAFE, the line under the track reads `ARM FIRST`. The refusal runs
from the switch to SAFE until the bench reports ARMED: a control touched after
the ARM hold completes and before that report is refused too. From a DISARM the
screen asks for, by the tap or by leaving, or a STOP while the bench is ARMED,
until the bench reports SAFE, the controls are refused too.

A switch to SAFE returns the throttle to 0%, however it happens: the DISARM tap,
STOP, a lost link, a touch controller that stops answering, or leaving the
screen. The slider, the readout and the `-1` and `+1` steps then stay at
0.0% until the next switch to ARMED, so the first `+1` after a switch to ARMED asks for 1.0% and
the first 1% turn of the knob asks for 1.0%. A bench switched to ARMED without the ARM
hold, by a stick run on PROGRAM, starts from 0% as well. A throttle the
screen has posted and the panel has not sent when the bench switches to SAFE is not
sent. A finger on the track when the bench switches to ARMED or to SAFE moves nothing
until it lifts and presses again.

ARM is a hold. The fill fades from green to the danger red across two
seconds, and the bench switches to ARMED when the fade completes; letting go before then
switches nothing, and the release itself switches nothing either.

The finger has to stay on the button. One that leaves it abandons the hold,
because the gesture is contact with the control and not with the panel.
Coming back is not resuming: the contact is finished as far as ARM is
concerned, so a switch to ARMED after that takes lifting the finger and pressing again.
A bench that switches to SAFE under a finger still held on the button ends the gesture
too — otherwise the contact made to stop it would switch it to ARMED again two seconds
later. The switch to ARMED flashes the
whole button twice: white, black, red, and again, one drawn frame each. While ARMED, the button carries the danger red, and DISARM is a press rather than a hold:
stopping never needs a hold, and DISARM acts at the press, not at the lift. DISARM and STOP stop the output immediately, with
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
  percentage, and a figure above 100% means the rated value is wrong; the
  screen shows that rather than hiding it, capped at 199%.

The rated kV comes from the connected ESC when it reports one. No ESC reports
it yet, so in practice it is the `Rated kV` setting, which defaults to zero: a
guessed kV produces a plausible-looking percentage that is wrong, so with no
value the field is drawn empty and no percentage is shown. The measured rpm/V
is shown either way, being a measurement rather than an inference.

While the values are simulated, SIMULATION is drawn across the screen. The
panel simulates only while no IOmcu answers, so the watermark goes when
one does. What replaces it is what that IOmcu can actually measure: rpm
from bidirectional DShot today, and its own sensors when a measurement front
end is fitted. A quantity nothing measures is drawn as an empty field rather
than as a modelled number.

### TABLE, and what the ESC says

![The TABLE pane with an INA228 as the source](img/motor-table.png)

TABLE lists the four channels, each with its extreme. A channel nothing
answered for reads `--`.

With the INA228 enabled on SETUP under INTERFACES and answering (BENCH flag
bit 5), voltage, current and power are the INA228's: the means of the
IOmcu's last 50ms window, read at 20Hz, with the run's peaks from its
500Hz samples. The heading names the source, `INA228`, and adds `CLIPPED`
while a current read the end of the part's range in the last window or since
the switch to ARMED: current and power, or their peaks, are then bounds and not values.
Under the channels, `ESC SAYS` lists the ESC's own telemetry voltage and
current from the SENSE page, and `diff` is the ESC's figure less the
INA228's. An ESC that reports a current it does not measure shows a
difference the size of the reading. An INA228 that stops answering during a
run leaves voltage, current and power empty to the end of that run; they do
not fall back to the ESC's figures. The peaks it measured in that run stay
shown, here and on the rail. Without the INA228, the channels are the
ESC's telemetry and `ESC SAYS` is not shown.

The mAh and Wh on the CONTROL card's title line are the INA228's own while BENCH flag bit 6 is
set: its charge and energy since the switch to ARMED, accumulated in the part at every
conversion and read in 0.01mAh and 0.01Wh steps. The IOmcu takes
the last run's totals and bit 6 off the bench page on the switch to ARMED, and the panel
counts the first 100ms of a run itself as well, so a page built before the
switch cannot carry the last run's totals into this one. Without bit 6 the panel counts both from the current
and power shown. When the INA228 stops
answering mid-run, the count carries on from its last total and does not go
back.

A run's CSV file has one row for every sample the panel takes, and no row
without one:

- With the link up a sample is one answered read of the BENCH page. The
  panel polls every 50ms plus the 5ms pass of its control task that looks
  at the gate and the pass's own exchanges: 52.9ms on average on one bench,
  about 19 rows per second. Other benches are not measured.
- With the link down a sample is one step of the model, every 50ms.
- A sample is one row. With the INA3221 read through the window ring
  (IOmcu protocol 4.11, CH1 enabled) a poll that brings more than one
  50ms window writes one row for each: the rows of one poll carry the same
  `time`, the sample is in the last of them with the newest window, and the
  rows before it hold `window` and the channel columns and are empty
  otherwise. At a 53ms poll one poll in 17 writes two rows.
- Nothing else gates a row. A row the card's queue cannot take is dropped
  and counted, and the band says so at the run's end (see [Logs](#logs)).

`time` is the wall time since the switch to ARMED in seconds, written to 1ms, and is
not a row count: the first row is not at 0, and a stretch without samples or
with dropped rows is a step in `time` of that length. The value keeps 1ms
of resolution for the first 4.5h of a run and 2ms up to 9.1h.

A file written by firmware 0.15.0 or older differs in two ways. Rows are
missing in stretches of up to 30s after a servo command, and
`time` advances only with a written row, so it ends short of the run's
length and shows no gap. In such a file the `window` numbers, 50ms apart,
give the spacing of the rows while the INA3221 is enabled.


```
time (s);voltage (V);current (A);power (W);rpm (rpm);esc (C);motor (C);charge (mAh);energy (Wh);ina voltage (V);ina current (A);esc current (A);window;ch1 current (A);ch1 max (A);ch1 voltage (V);ch2 current (A);ch2 max (A);ch2 voltage (V);ch3 current (A);ch3 max (A);ch3 voltage (V);source
```

In a MOTOR file `voltage`, `current` and `power` are what MOTOR & ESC
shows. A SERVO file, from a switch to ARMED on SERVO, has two more
columns before `source`:

```
...;ch3 voltage (V);pulse (us);angle (deg);source
```

- `voltage`, `current`, `power`, `charge` and `energy` are the servo rail's
  meter's ([the meter](#servo)):
  - while an INA3221 channel is the meter, its newest 50ms window at the
    sample: the mean bus voltage, the mean current and their product;
  - otherwise the PD mini's last reading at the sample.
  - Voltage and power are written to 0.01, current to 0.001.
  - Charge and energy are counted by the panel from those readings since the
    switch to ARMED, to 1mAh and 0.01Wh.
- `pulse` is the pulse COMMANDED shows, in whole us. It is empty while a
  programming session holds the port.
- `angle` is the AS5600's angle MEASURED shows, to 0.1deg. It is empty while
  AS5600 is off in SETUP, the sensor gives no valid reading or the link is
  down.
- A cell is empty while its meter has not delivered that reading.
- A row takes a figure only from a source that is the row's `source`:
  - all five meter cells are empty on a `measured` row while the supply is
    the panel's model (PD mini disabled on SETUP under INTERFACES);
  - they are empty on a `model` row while the PD mini's last reading is
    still shown after the link went down;
  - a `model` row has no INA3221 figure and no `angle`, which read through
    the IOmcu. `pulse` is the panel's own command and is on rows of both
    sources.
- The meter's figures, `pulse` and `angle` are on the last row of a sample.
  The rows before it in one poll hold the window and the channel columns
  only.
- The other columns of a SERVO file are a MOTOR file's.

`source` is
`measured` on a row whose values a meter produced and `model` on a row the
panel's model produced (the SIMULATED mark); a supply file has the same last
column. A file without the column ends at `ch3 voltage` or `energy`, and
LOGS and the tools read both forms: `source` is a
text column and is never plotted. `ina voltage` and
`ina current` are the INA228's and are empty while it is not the source;
`esc current` is the ESC's own telemetry current, from the SENSE page while
the INA228 is the source and from the BENCH page otherwise, and empty when
the ESC reports none or the panel models the bench. A quantity nothing
measured is an empty cell. `window` and the nine channel columns are the
INA3221's 50ms window (per channel the mean and highest current and the
lowest bus voltage) on the first row after the panel read it and on no
other, so each window is in the file once, under its number; a channel the
window has no readings of is empty, and so are CH1's current cells or its
voltage cell for a window that holds no sample of that quantity. With an
IOmcu of protocol 4.11 and
CH1 enabled every CH1 window is in the file while two reads lie at most
200ms apart, and a step in `window` of more than 1 is a window that was
lost or a number the IOmcu skipped. A window taken while the bench
was SAFE is in no file. CH1's figures count a clipped sample at the end
of the range, 1.638A on the 0.1Ω shunt, and keep the sign of a negative
current. CH2 and CH3 are on the row of their window when the panel read
both pages within that window, and empty on a row the poll caught up. With
an older IOmcu, or with CH1 off, the columns hold the last window of
each read: a poll longer than 50ms skips windows, 1 in 17 at 53ms, and a
clipped sample is left out of the figures. LOGS groups `ina voltage`,
`ina current` and `esc current` under INA228 and ESC, and the window's
columns under INA3221.

With an IOmcu of protocol 4.25 whose sensor map the panel has read, the
channel columns follow that map as it stands when the file opens, one group
of three per channel that watches a target, named after the target:

```
window;port 1 current (A);port 1 max (A);port 1 voltage (V);out 3 current (A);out 3 max (A);out 3 voltage (V);supply current (A);supply max (A);supply voltage (V);source
```

- A channel takes a group while its part is fitted, its shunt is not 0 and
  it watches an output, a programming port or the supply
  ([SENSORS](#sensors-the-ina3221-parts)).
- The channel watching the servo tester's target comes first, then the
  others from 0x40 CH1 to 0x43 CH3.
- Two channels on one target add the channel to the name:
  `out 3 0x41 ch2 current (A)`.
- At most 3 groups, 23 columns in a MOTOR file and 25 in a SERVO file: the
  fourth watching channel and later are in no file. LOGS reads at most 25
  columns.
- A file with fewer groups ends `source` earlier: 13 + 3 x groups + 1
  columns, and 2 more in a SERVO file. With no channel watching anything the file has no channel
  column.

The group of the channel watching the servo tester's target comes from
the window ring, SERVO_WIN, and is on every row that has a `window`. The
other groups come from SENSE_WIN_A and SENSE_WIN_B
([Link](LinkSensors.md#sense_win_a-and-sense_win_b)), which hold each
channel's last window only. The panel reads them on every poll, after
SERVO_WIN, with a gate of 40ms, while a part on the page is fitted. Such a
group is on the row of its window when the last read of its page, or the
read before it, holds that window number, and empty otherwise. A window
that closed and was followed by the next one between two reads is in no
row: at a 53ms poll about 1 row in 17 (`test_sense_win_link`). The map
counts once the IOmcu holds it in force; with the map derived from SENSE
the file keeps `ch1` to `ch3`. The map takes no write while the bank
drives, so the columns hold the whole run. LOGS groups `out`, `port` and `supply`
columns under INA3221.

### MOTOR & ESC settings

![SETTINGS](img/motor-settings.png)

SETTINGS holds the 8 settings of the motor, the pack and the throttle
output (`SET_CAT_ESC` in the settings schema) in schema order, under three headings:

| Heading | Settings |
| --- | --- |
| PACK | Cells, Capacity, Internal R |
| MOTOR | Motor poles, Rated kV |
| THROTTLE OUTPUT | Idle pulse, Full pulse, Ramp limit |

The rows stand in two columns of 295px: PACK and MOTOR on the left,
THROTTLE OUTPUT on the right. A row is 68px: the label and, right of it,
the value with its unit in a box of 120 x 48px, and under them the help
line in 8 x 16. A tap on the box opens the keypad over the left column and
the strip, 620 x 529px, so no tab switches the task under it; the rail
stays in reach. The keypad names the setting, its range and its unit. OK
takes a value inside the range and lands it on the setting's step, 4321mAh
as 4300mAh; outside the range it keeps the keypad open and names the range
in the warning colour. CANCEL and the home tag close it with the value
unchanged.

![The keypad over SETTINGS](img/motor-settings-keypad.png)

The keypad of Idle pulse refuses a value that is not 100us or more below
Full pulse (`LINK_CC_THROTTLE_GAP_US`), and the keypad of Full pulse one that
is not 100us or more above Idle pulse. The ranges overlap, 800 to 1600us and
1400 to 2400us. The keypad stays open, shows `Idle pulse at most 1900us
(100us below Full pulse)` or `Full pulse at least 1100us (100us above Idle
pulse)` in the warning colour in place of the range, and the row keeps its
value. The panel's payment of the pair to the IOmcu holds the same gap; the
IOmcu's own write check holds each pulse to 400 to 2500us and does not read
it.

Idle pulse and Full pulse take a value on a SAFE bench only: the IOmcu takes
the endpoints while it is disarmed, 300ms after the last edit, and the
switch to ARMED sends none. While the bench is ARMED, ARM is held, or an ARM
or a DISARM waits for its answer, their help line reads `Set on a SAFE bench
only` in the warning colour and the box takes no tap; a keypad open for one
closes when ARM is pressed or the bench leaves SAFE. The 2s hold of ARM
leaves an edit made before the press the time to reach the IOmcu. While an
edit is still owed to the IOmcu with the link up, the switch to ARMED is
refused with `Pulse range not yet sent to the IOmcu. Hold ARM again.` and
the bench stays SAFE. A pair the IOmcu refused, or stored otherwise, keeps
the switch refused with a text that names what to change, until an edit
here, a binding on OUTPUTS or a link-up brings the pair into force ([A
pulse range on its way](Safety.md#a-pulse-range-on-its-way)). The other 6 rows take a value at any time.

The settings are one store. Every value OK sets is in force at once and asks
for a write; the panel writes the whole store on the first frame with the
bench SAFE, the supply output off and no board drawing being fetched or
kept, the same write SETUP's SAVE asks for ([Keeping the
values](#keeping-the-values)). A value left unsaved on SETUP is written with
it, so SETUP has nothing left to warn about. A refused write leaves every
value unsaved, and SETUP's warning on leaving lists the ESC rows with its
own. The line at the page's foot, ending 4px above the alert band, names
the store's state:

| Line | State |
| --- | --- |
| `SAVED` | nothing is unwritten |
| `SAVE PENDING` | a write is asked and waits for a quiet bench |
| `NOT SAVED` | the last write was refused |
| `SETUP CHANGES NOT SAVED` | values are unsaved and no write is asked; the next change here writes them |

### PROGRAM

![PROGRAM's list](img/motor-program.png)

PROGRAM in the strip shows the list of ESC programmers in the page area, from
the table SERVO's list comes from (`shared/ui/prog_registry.c`,
[Programmer](#programmer) on SERVO). The list has one row, `ESCs by throttle
stick` (German `ESCs per Gasknüppel`), in the group STICK MENU (German
`STICK-MENÜ`); a list draws its group headings only above 8 rows. The row's
right side reads `THROTTLE OUTPUT >`: it drives the pins bound as throttle,
not a programming port, and needs no IOmcu capability. The output card stays
on the right while the list shows.

A tap on the row opens ESC STICK, one level down. ESC STICK is a full-width
session: it covers the left column, the rail and the strip's tabs, and no
press reaches ARM or a tab while it is open. Opening it on a bench that is
not SAFE switches the bench to SAFE, a DISARM held by another finger
included. The strip row holds the output
strip in their place, one line in the 10 x 20 face from x 12, y 4:

![ESC STICK under the output strip](img/motor-stick.png)

```
TARGET DSHOT300 GPIO 0 GPIO 1   SUPPLY 7.60V  1.00A   SAFE
```

| Part | Shows |
| --- | --- |
| `TARGET` (German `ZIEL`) | the protocol and the pins bound as throttle, as the CONTROL card's TARGET row names them, in at most 26 characters; in the warning colour while no pin is bound or the binding is not read |
| `SUPPLY` (German `NETZTEIL`) | the PD mini's output as it reads: `SUPPLY 7.60V  1.00A`; `SUPPLY ON` while it is on without a voltage and a current reading; `SUPPLY OFF`; `SUPPLY --` while the supply does not answer |
| `SAFE` or `ARMED` | the bench; ARMED in the danger colour |

The strip is at most 100 characters, 1000px, in English and in German. It
is repainted when its text changes.

From ESC STICK's makers the home tag closes the session to PROGRAM's list;
from the list it leaves for the menu. Leaving MOTOR & ESC from any level ends
a stick run, switches off the supply output the run switched on, switches the
bench to SAFE and closes ESC STICK. Back on MOTOR & ESC, PROGRAM shows its
list.

The parameter tables for ESCs with BLHeli_S, AM32, ESCape32 and VESC firmware
are a model in the firmware (`shared/ui/prog_models.c`). No transport carries their protocols, and no
PROGRAM list has a row for them. The bench identifies and drives ESCs with
BLHeli_32 firmware and sends the DShot special commands, but cannot read
their parameters: [Parameters of ESCs with BLHeli_32 firmware](BLHeli32.md).

### ESC STICK

ESC STICK programs an ESC through its throttle-stick menu, from an ESC
profile; [Stick programming](StickProgramming.md) describes the runs.

Every control on ESC STICK takes a tap. It acts when the finger lifts
inside the control it pressed, no more than 8px from the press in x and in
y. A finger that comes down on a control and travels on presses nothing, and
neither does one that crosses a control. One finger at a time presses a
control. Three controls act on the press:

- HOLD 2s TO RUN: its 2s hold starts at the press.
- ABORT of a stick run, on the run's page and on a manual step's prompt. It
  stops a motor, so it does not wait for the lift, and it answers a second
  finger.
- The keys of the search keyboard, which follow their own press and lift.

ESC STICK lays out on the body under the output strip, from body y 28,
with its labels, rows and buttons in the 10 x 20 face:

| Part | Place and size |
| --- | --- |
| crumb row | y 30 to 78: BACK 120 x 48px, the trail in the 12 x 24 face, and at the right the search field (260px, its X 48px), `^` and `v` (56px each) on the list or TIMING (168px) on a profile's page |
| list | 7 rows a page, 48px tall at a 56px pitch from y 84, 984px wide; a model's name in 30 characters; the footer line ends 4px above the alert band |
| profile's page and TIMING | the parameter card on the left, 660px: 6 rows a page, 48px tall at a 56px pitch, each with its number, its name (up to 23 characters), its value in a 250px column and `-` and `+` of 48 x 48px, 8px between any two |
| side column | 340px right of the parameter card: CHANGE (TIMING's title on TIMING), the rows shown and `^` and `v` of 48 x 48px; MANUAL INTERVENTION REQUIRED, 310 x 48px; then how the profile's menu works, the supply and the entry time, and the picked row's help, wrapped to 31 characters a line |
| foot row | y 436 to 492, 4px above the alert band: the note at the left on up to 2 lines, DEFAULTS and RUN, ABORT, OK or CLOSE, 168 x 56px each |
| run and result | the device card across the body from y 84 with its two lines, the run's card under it from y 136 with the stack light at its right, two help lines from y 390, and the foot row; a result's MANUAL INTERVENTION REQUIRED, 330 x 56px, left of OK |
| warning, manual steps, prompt | a panel over the pages from y 28, its title in the 20 x 36 face, its lines at a 22px pitch, its buttons 48px tall ending 4px above the foot row, so a second tap on RUN's place lands on none of them |

BACK and the band's home tag climb one level at a time: a stick profile's
page to the list, a maker's models to the makers, the makers to PROGRAM's
list. Over the RUN warning the home tag is CANCEL, and over the manual steps
it is OK. On a stick run under way it is ABORT: the run ends, and its result
stays on the page.

The list shows the makers first, alphabetical, each with how many of its
models run:

![The makers](img/motor-stick.png)

A maker opens its models, one row a model, by current, then voltage, then
name, each with its family and what the profile is or why it does not run.
A model opens its family's profile:

![Kontronik's models](img/motor-stick-hand-list.png)

SEARCH filters the level showing. Its keyboard docks on the right, the rows
narrow beside it, and every key filters at once. A model is found when the
text appears in its maker and family read as one text, or in its maker and
its own name; a maker shows when any of its models is found. Case does not
matter, and `*` stands for any run of characters:

![Typing a search](img/motor-stick-find.png)

OK keeps the search on both levels, X clears it:

![A search applied, Hobbywing's models](img/motor-stick-found.png)

A profile's items start at KEEP. The steppers pick a value; RUN counts the
values picked:

![Two values picked](img/motor-stick-items.png)

RUN opens a warning over the whole screen, PROPELLER REMOVED? MOTOR SECURED?: a
motor on the ESC must be mounted solid and carry no propeller, or a
resistor load takes its place. The run starts after HOLD 2s TO RUN is held
for 2s:

![The warning](img/motor-stick-warning.png)

RUN opens nothing, and the line beside it names the reason, while no pin
is bound as a throttle, while the output binding is not read, and while
the supply is the panel's model; such a reason closes a warning that is
open ([Before a run](StickProgramming.md#before-a-run)).

A profile whose ESC needs a person at it (a jumper fitted and pulled, a
button pressed) carries a red MANUAL tag in the list and MANUAL
INTERVENTION REQUIRED in red on its page. The button shows the steps and
when each is due; so does the first opening of the profile. The warning
lists the steps due before the power-up, and the run stops for each later
step with DONE and ABORT, waiting at most 60s
([Manual steps](StickProgramming.md#manual-steps)):

![A profile with manual steps](img/motor-stick-hand.png)

![The run waiting for the jumper](img/motor-stick-hand-prompt.png)

A Kontronik ESC that locks itself when its supply goes off before it has
confirmed the stored mode holds the run powered after the store, the stick
where it stored, until DONE:

![The run waiting for the ESC's confirmation](img/motor-stick-hand-end.png)

While it runs, the page shows the phase, the beeps of the group under way
and the last group. ABORT, the home tag, STOP and leaving the screen end it
with the throttle at MIN, the supply off and the bench SAFE. The stack light's
green is on while a beep is detected, for at least 150ms:

![A run](img/motor-stick-run.png)

While the phase tap is enabled on SETUP the run's page adds a read-only
readout under the current line: the tap's state, the pitch of its last 8ms
window, the counts of beeps the IOmcu lost and beeps the panel did not
read, and the last four
beeps with their number, length in ms and mean pitch in Hz. The run still
counts its beeps from the supply current.

![A run with the phase tap running](img/motor-stick-tone.png)

| State | Meaning |
| --- | --- |
| `RUNNING` | the capture runs |
| `OVERRUN` | it runs, and the capture ring or FIFO overran since the tap was enabled |
| `WAITING` | no link, or the TONE page has not answered in the last 500ms, or its flags say the tap runs and its window number has not moved for 1.5s, or the tap was switched off and the page still holds it on |
| `NO TONE PAGE` | the IOmcu speaks a protocol older than 4.8 |
| `PIN NOT FREE` | the IOmcu holds the tap off: its pin is bound elsewhere, or the IOmcu runs no capture since its boot (core 1 not started) and its saved set-up has the tap enabled; the second clears only with a restart of the IOmcu |
| `NOT RUNNING` | enabled, and the page holds the tap off or does not run it: the set-up was refused or is not written yet |

PITCH and LOST are the page's values from the last status read. They read
`--`, and OVERRUN is not shown, while no read younger than 500ms gives the
state: in `WAITING`, in `NO TONE PAGE`, and in `NOT RUNNING` while the
set-up the page holds has the tap off. MISSED and the beeps are the panel's
own count and record and stay.

The result stays until OK. Its red light is on when the run ended because
something was not as expected, a stop the bench raised itself included,
and dark on DONE, a STOP pressed, ABORT, the home tag and leaving the screen ([the list](StickProgramming.md#how-a-run-ends)):

![Done](img/motor-stick-done.png)

![Stopped](img/motor-stick-aborted.png)

![Ended by the supply](img/motor-stick-failed.png)

TIMING holds the beep timings and the supply settings. None of them is
measured:

![Timing](img/motor-stick-timing.png)

A row's `-` and `+` step its value. A tap on the value, the 250px column
between the name and the keys, opens the keypad over the page, from under
the output strip to 4px above the alert band: 1008 x 464px, keys of
242 x 81px. It names the setting, its range and its unit and takes the
step's digits after the point: one for Voltage's 0.1V, two for Current
limit's 0.05A. OK sets the value as a step does, onto the nearer step (1.23A is
1.25A); CLOSE asks for the save, as after a step. A value outside the
range keeps the keypad open and names the range in the warning colour.
CANCEL, OK with nothing typed and the home tag close it and change nothing;
TIMING stays. Nothing under it takes a press. A profile's items are lists
of the ESC's own values and open no keypad.

![The keypad of Beep min](img/motor-stick-timing-keypad.png)

## Servo

![Servo](img/servo.png)

SERVO is a workbench. The strip under the band holds its tasks, **LIVE**,
**TEST**, **PROGRAM** and **SETTINGS** (German `LIVE`, `TEST`,
`PROGRAMMIEREN`, `OPTIONEN`), 150 x 40px a tab, 4px apart, from y 8
under the band. The tile opens LIVE. A tab
switch is no navigation level: the home tag goes home from every task, and
climbs one level only from a run's settings back to TEST and from a
programmer back to PROGRAM's list. Each task keeps its page and what is
selected on it until the panel restarts. Leaving SERVO switches the bench to
SAFE, from any task.

| Task | Left card, the page area |
| --- | --- |
| LIVE | the dial, and a run's box over it while a run is under way or its result is shown |
| TEST | the AUTOMATIC TEST and LIMIT RUN cards, and SUPPLY's caps, read-only |
| PROGRAM | the list of programmers, one row each; a row opens its programmer over the left column |
| SETTINGS | the pages OUTPUT, SWEEP and DUT |

The left card is 620px wide and starts under the strip, at y 56. The
right card is the output card, 380px wide, from the top margin to the
bottom one beside the strip, the same on every task: the type in force,
the servo rail's line and plot, the SET line, SPEED, the TARGET row,
CENTRE, SWEEP, RELEASE and ARM. Every row tapped there is 48px tall or
taller; ARM is 60px and ends 4px above the alert band, as OUTPUT OFF
does. A panel (a keypad, the keyboard, a list, a warning, the question
or a programmer) covers the whole left column, the strip's tabs with it,
and is answered or closed before the task changes.

LIVE's card holds the horn's angle in the bench's numerals top left, with
`ARM FIRST` under it while the bench is SAFE, the dial (radius 150px)
with the servo, and along its foot the three readings, a third of the card
each: COMMANDED in microseconds, MEASURED in degrees and CURRENT in amps,
the label and unit over numerals 44px tall. Numerals wider than their
198px column (a current held at `-1000000.00`) are drawn in a
smaller face, so they stay inside it. The readings are on LIVE only; on
the other tasks the line over the plot carries the current.

The horn is drawn in fractions of a pixel: its tip, 98px from the shaft,
moves 1.7px a degree and is drawn within 0.1px of its angle at every
step. A horn that moves repaints the boxes around it, as each frame buffer
last drew it and as it is, about 160px across, and the numerals of the
angle, COMMANDED and MEASURED; the two cards are not repainted for it.

On LIVE, while the bench is ARMED, drag anywhere on the sweep to command a position. The solid arm is the measured
position; the faint arm is the commanded position. The gap between them is the
servo's own lag. The rings around the tip pulse while the servo is being
driven. Lifting the finger does not let go: the screen keeps saying the last
position every SERVO_HOLD_MS, so a servo stays where it was put. **RELEASE**,
the button, is what returns the surfaces to centre, and even that leaves the
pins bound and driving, at the middle of their travel. A switch to SAFE, or leaving
the screen, which switches to SAFE, is what stops the edges. With the rotary knob on,
turning it moves the horn from where it is ([the knob](#panel-the-rotary-knob)),
on LIVE alone: on TEST, on PROGRAM, on SETTINGS, under a panel and under a
programmer the dial is out of sight and a turn moves nothing. A turn in the
frame in which LIVE comes back moves nothing either; the next one does.

One finger drags the dial. A press by a second finger anywhere on the screen
ends the drag, as it ends a drag on SPEED: the target goes on to the last
angle the finger gave, and the dial follows again after a new press on it.

The press on the dial commands its angle at once. The drag after it goes
through a one-pole filter with a time constant of 20ms (`SV_DRAG_TAU_MS`),
reckoned from each frame's length, so it is the same at 30, 60 or 120
frames a second. The horn and the pulse the servo is sent both follow the
filtered angle, 1.8° behind a finger moving at 90°/s. The filter evens out
the beat between the touch controller's reads, every 10.5ms, and the
frames, every 16.6ms: at 90°/s the horn steps 1.0° to 1.8° a frame, where
the reads alone give 0.5° to 2.1°. After the finger lifts, the target goes
on to the finger's last angle, 99% of the way in 100ms. Any other command
ends the filter at once: a switch to SAFE, DISARM, STOP, RELEASE, CENTRE,
SWEEP or the knob, even a knob turn that comes to the pulse the filter last
sent. So does any press that ends the drag before the finger lifts: a
second finger anywhere on the screen, on the band, on STOP or on the alert
band, and the dragging finger carried onto the band. The filter then stops
where it has got to and commands nothing more. It moves nothing while the bench is SAFE, and an ARM
starts from the rest with nothing of a drag before it.

The screen drives the channels the binding marks as surfaces, not a fixed pin
and not a fixed protocol. PPM's eight channels are surfaces too, so a bound
PPM output moves with this screen exactly as a bound SERVO PWM one does. With
no surface channel bound at all it commands nothing and no pin moves.

SPEED is the rate the bench may move the output, in degrees a second of the
horn's travel, not a speed for the drawing alone. At 100% the command goes
straight through and the servo moves at its own rate; below that the bench
ramps the command in front of it, so 30% takes three times as long to cross
as 90%. It applies to a held output as soon as it is changed. Without
feedback the horn is drawn moving at the same rate, and at 100% at the
command at once.

**ARM before anything moves.** ARM here switches the surfaces and a pulsing PROG
PORT to ARMED; a throttle output sends no pulse ([An arm for one
role](Link.md#an-arm-for-one-role)). While the bench is SAFE for the
surfaces the IOmcu writes a pulse of length zero to every PWM pin, and
the screen takes no position: the commanded value changes only while the servo follows it. The
button is a two-second hold, the same
gesture and the same fade as the one on MOTOR & ESC, and a press on it while
ARMED switches to SAFE. Leaving the screen switches to SAFE and lets go of the pin: a screen
that is not visible must not be holding a servo somewhere, or leaving the
bench ARMED behind it.

What each control does while the bench is SAFE:

| Control | SAFE | ARMED |
| --- | --- | --- |
| The dial | refused: no command, the horn and COMMANDED stay | commands the position under the finger |
| Rotary knob | refused; the turn is not kept | moves the horn from the commanded value |
| CENTRE | refused; drawn dimmed | commands PULSE CENTRE plus TRIM |
| SWEEP, PAUSE, PAUSED | refused; drawn dimmed | starts, pauses and resumes the sweep |
| RELEASE | sends the release; the value shown stays | centres the surfaces; the value shown becomes the rest |
| SPEED | set; sends nothing | set; a held position is sent again at the new rate |
| SETTINGS > OUTPUT: TYPE, FRAME RATE, PULSE MIN, CENTRE and MAX, TRIM, TRAVEL, REVERSE | set; send nothing | set; a held position or the rest is sent again under them |
| TARGET | a tap opens SETUP > OUTPUTS | locked; names the target |
| SET, OUTPUT ON, OUTPUT OFF | as on SUPPLY | as on SUPPLY |
| START TEST | refused: `ARM FIRST` | starts the run after its hold |

While SAFE, the horn is drawn dimmed and LIVE's left card reads `ARM FIRST`. The
horn and COMMANDED show the position last driven; after a restart, PULSE
CENTRE. A pulse-width setting changed while SAFE changes the
microseconds COMMANDED shows for that angle, and TRAVEL does not clamp it.
From a DISARM the screen asks for, by the tap or by leaving, or a STOP while the
bench is ARMED, until the bench reports SAFE, the SAFE column holds as
well, and a sweep, a pause, a held position and a run of the automatic test
end where it is asked.

When the bench switches to ARMED, the commanded value becomes the rest the pins drive:
the midpoint of the channel's endpoints, 1500us for STANDARD PWM and 760us
for NARROW 760, without TRIM. Without feedback the horn is drawn there at
once. The first drag or knob turn after a switch to ARMED starts from that value,
whatever the screen showed before; a 1% turn of the knob on STANDARD PWM
asks for 1510us. A type or pulse width changed while the bench is ARMED before the
first position moves the rest, and the value with it.

A finger on the dial when the bench switches to ARMED, switches to SAFE or stops moves nothing
until it lifts and presses again. A position, a sweep or a pause the screen
has posted and the panel has not sent when the bench switches to SAFE or stops is not
sent, and a knob turn in that state is withdrawn: the value returns to the
one before the turn.

RELEASE while the bench is ARMED follows the rule of the switch to ARMED again. The pins go to the rest,
and the commanded value becomes that rest: 1500us for STANDARD PWM, 760us
for NARROW 760, without TRIM. The horn is drawn moving there at SPEED. A
drag under way ends. A press on the dial drives the servo again, and the
first knob turn counts from the rest: a 1% turn on STANDARD PWM asks for
1510us. The same holds for the release a run of the automatic test ends
with, and for the one a changed type or pulse width sends on a resting
bench. One command waits for each frame: of a position and a RELEASE made in
the same frame the later is sent, and the value shown is that one's. A knob
turn in a frame in which RELEASE waits is dropped.

The output card shows the type, frame rate and pulse range in force (in
the danger colour while they are ones that can destroy a servo not made for
them) and the servo rail as its meter reads it: voltage, current and power
on the line over the plot, plotted over the last 17.8s (356px at 20
points a second). LIVE's CURRENT reading shows the current again. The type
row, 44px tall, reads `STANDARD PWM  50Hz  1000-2000`: the type, the
frame rate and PULSE MIN to PULSE MAX in microseconds, two spaces apart.
The longest, a 13-character type at 560Hz with two 4-digit ends, takes 32
of the row's 35 characters, so the range carries no unit. A pulse width
typed in the settings reaches the row at once.

| The servo rail's meter | Name on the line | LIVE's CURRENT, the line and the plot show |
| --- | --- | --- |
| an INA3221 channel | `INA`, its part's address and channel: `INA 0x41 CH2` | the channel's last 50ms window: the mean current, the mean bus voltage at the load side of the shunt, and their product. One point of the plot per window, 20 a second |
| the PD mini | `PD MINI` | the supply's last sample. One point per sample |
| the model, while no IOmcu answers | `PD MINI` | the supply's last sample: the supply model's under the SIMULATION watermark, and no reading where SETUP INTERFACES enables the PD mini, which does not answer without the link |

![The INA3221 as the meter](img/servo-ina.png)

The name stays `PD MINI` in both languages. While the PD mini is disabled on
SETUP under INTERFACES, the supply is the panel's model of it, as on SUPPLY:
then a SIMULATED pill (SIMULIERT in German), 26px high and 30px plus the word
wide but at least 110px (120px for SIMULATED and SIMULIERT), sits
on the plot's top-left corner under the name, because the line beside the
name holds the three readings. The pill comes and goes with the model,
without a tap. With an INA3221 channel as the meter the line names the
channel, `INA 0x40 CH1` to `INA 0x43 CH3`, and carries no pill, the supply's model or
not. The channel is the one watching the servo tester's target
([SENSORS](#sensors-the-ina3221-parts)); on an IOmcu older than 4.25 that
channel is CH1 of the one part SENSE reads.

![The panel's model as the supply](img/servo-model.png)

Which meter it is is decided once per poll
([the current monitors](#interfaces-the-current-monitors)): the INA3221
1000ms after its conditions hold, the PD mini from the first poll in which
one fails.

- A current is signed and shown in hundredths of an amp: `0.41A`,
  `-0.02A`, `-1.64A`. A reading that rounds to zero is `0.00A`. The
  power on the line is signed in tenths of a watt. Beside a channel's name the
  three readings have 64, 84 and 74px, 6, 8 and 7 characters of 10px,
  after the name's 134px; beside PD MINI 88, 88 and 90px after its 90px:
  a voltage of 10V or more is shown in tenths, `26.0V`, and
  a power of more than 7 characters in tenths, `-100.0W` and beyond or
  `1000.0W` and beyond, in whole watts.
- A reading that did not arrive is `---` in LIVE's CURRENT, `--` on the
  line and a gap in the plot: no sample yet, a supply that does not answer,
  no window since the link came up, a window without samples of that
  quantity.
- A window in which a sample read an end of the INA3221's range, 1.638A or
  -1.638A on a 0.1Ω shunt, shows its mean as it is, in the warning colour
  in LIVE's CURRENT and on the line. The clipped samples count in that mean
  at the end of the range.

  ![A clipped window](img/servo-clipped.png)

- A link that goes down takes the INA3221's last window with it: `---`
  and `--` until the meter changes or a window arrives over the link.
- The plot is emptied when the INA3221 becomes the meter and when it stops
  being it: the trace is of the meter the line names. Each window and
  each supply sample carries the meter of the poll that took it, so a
  reading taken before its meter became the rail's is no point of the
  trace, and none taken after is left out. The plot's scale starts at 0,
  and a negative current is drawn on its lower edge.
- LIVE's CURRENT numerals are repainted when their digits or their colour
  change, and by themselves.

The automatic test reads the servo rail's meter as the panel names it at
START TEST: the INA3221 channel while the INA3221 is that meter and the
supply is the PD mini, the PD mini's readings otherwise, and the model's
with the supply modelled ([The meter](Servo.md#the-meter)).

MEASURED is the horn's angle from the centre count while AS5600 is on in
SETUP, INTERFACES, the sensor answers and it detects its magnet: `+38.0deg`,
tenths of a degree, -180.0 to +179.9, read 20 times a second. It is `---`
while the sensor does not answer and while it reports no magnet (STATUS MD
clear): its count is no angle then. A magnet reported too weak or too strong
leaves the angle on show; the band says which. Where AS5600 is off the row
shows the feedback's angle. COMMANDED beside it is the pulse width in
microseconds.

**The supply is set and switched here too.** The SET line under the plot
holds SUPPLY's two set points, the voltage and the current limit, and its
output switch. They are SUPPLY's, not a copy: a change on either screen is
the change on both.

- A tap on a set point opens the keypad over the left card. A value outside
  the caps is brought inside them, as on SUPPLY.
- While the output is on, a typed set point waits for SUPPLY's question,
  OUTPUT IS ON, with APPLY and CANCEL, unless SUPPLY's SETTINGS, CONFIRM
  WHILE ON, KEYPAD is off. The question goes, unanswered, when the output
  goes off or on STOP. An APPLY tapped in the same frame as STOP applies
  nothing, though the supply reports the output off a sample later.
- A voltage raised from 6.0V or below to above it opens the warning HV
  SERVOS ONLY: standard servos are rated for 4.8 to 6.0V, and above that
  only a servo specified as HV (high voltage) is within its rating; a
  standard servo above it can be destroyed immediately. The
  voltage is applied after HOLD 2s TO APPLY is held, the gesture of the
  profile warning; CANCEL drops it. STOP ends the hold. With the output on it replaces SUPPLY's
  question, and it stays if the output goes off. A voltage already above
  6.0V changes without it. A voltage set on SUPPLY does not open it.

  ![The HV warning](img/servo-hv.png)

- OUTPUT ON is a two-second hold, OUTPUT OFF a tap, as on SUPPLY. STOP ends
  a hold under way. With a voltage set point above 6.0V, wherever it was
  set, OUTPUT ON opens HV SERVOS ONLY instead, naming the voltage, and the
  output comes on only after HOLD 2s TO APPLY is held; the switch's own
  hold and a tap on APPLY switch nothing on. A set point that rises above
  6.0V during the ordinary hold (on SUPPLY, or put back after a run)
  is seen as the hold completes: HV SERVOS ONLY opens and nothing comes on.
  Leaving SERVO switches off an output SERVO switched on: by OUTPUT ON,
  by a test, or by the servo programmer's CONNECT. While the servo
  programmer holds a port the output stays on, as an OFF would end the
  session and cut a write under way; it goes off when the port lets go,
  unless SERVO is shown again first. An ON SERVO asked for that is on its
  way when the port takes the supply counts as switched on. An output SERVO did not switch on
  stays as it is. The servo programmer, the keypads and the questions
  belong to SERVO and are no leave. A press on OUTPUT OFF as the screen is
  left is sent as the OFF it was.

**SWEEP drives the servo through a curve** on the IOmcu, where its
timing does not depend on the link: SETTINGS > SWEEP's CURVE (square, sine
or triangle) and SPEED (0.05 to 5 cycles a second), and the automatic test's
DWELL TIME (the hold at each end), about PULSE CENTRE. RANGE is a share of the travel the servo may make:
of TRAVEL, and of the nearer of PULSE MIN and MAX, so the sweep reaches
neither end it may not. SPEED on the right card limits it as it limits a
drag, and trim is not applied. The horn follows the same curve, computed on
the panel and timed from when the IOmcu started its own. The panel
draws only what the IOmcu is known to do: a sweep asked for, or
resumed, is drawn once its start is acknowledged, and until then the horn
stays where the output is. Without feedback it is then worked on from where
the output was when the IOmcu began, along the curve at SPEED. Two
taps on the sweep button that reach the screen in one frame, before the
first one's command has left, cancel out: nothing is sent and the sweep
goes on as it was.

**PAUSE pauses it.** While a sweep runs the button reads PAUSE in the accent
colour. A tap stops the sweep where the output has got to (which SPEED can
leave behind the curve) and holds it there; the button then reads PAUSED,
filled in the warning colour. The IOmcu does the holding (the link's
HOLD, SWEEP register 4), because only it knows exactly where that is;
without feedback the horn drawn on the panel is an estimate of it. The panel
repeats the hold every 100ms (`SERVO_HOLD_MS`), so a pause outlasts the
IOmcu's 500ms rule. A pause does not wait behind sweep writes already
on the wire. A tap on PAUSED carries the sweep on from the point of the
curve it was paused at: its place in a dwell and the ends it has reached go
on from there too, and the horn is drawn on from that phase. The phase is
the curve's when the IOmcu acknowledged the HOLD, not at the tap: the
curve runs on there for the exchange in between, and so the drawing runs
on too, at the SPEED in force at the tap, for at most 500ms, the longest
the IOmcu runs a sweep it has not heard. Without feedback the drawn
horn is then put where the output had got at the acknowledgement: the curve
to that phase, slewed as the drawing slews. A SPEED changed in between reaches the
IOmcu only with the resume. A tap on PAUSED leaves the horn where it
is until the resume is acknowledged; the panel then times the drawing from
the phase the IOmcu kept, as it timed the two acknowledgements, also
when the tap came before the HOLD's acknowledgement; that acknowledgement,
arriving after the tap, still moves the horn to where the IOmcu held
the output, and the resume goes on from there. The IOmcu
keeps the phase while it holds and resumes the curve (`LINK_SV_RESUME`,
protocol 4.6); the output slews from where it was held to the curve at
SPEED's rate, at once at 100%, and is normally already there. A curve
changed on SETTINGS > SWEEP while paused starts as a new sweep instead. An
IOmcu older than 4.6, or one that refuses the resume because its hold
has ended, starts the curve over from its beginning (the centre for a
sine or a triangle, the first end for a square) and the alert band says
so: `IOmcu firmware older than 0.13.0. Sweep starts again.` or `IOmcu could not resume. Sweep starts again.`. So does PAUSED tapped while
the HOLD has gone unanswered, since the IOmcu may then be holding or
still running: `Pause was not confirmed. Sweep starts again.`. A
PAUSED tapped before a HOLD that the panel then lets go of (below) starts
nothing: the pause ends with that HOLD.

![A paused sweep](img/servo-paused.png)

A hold the link left unrepeated for 500ms has been let go at the far end,
so the panel releases the surfaces to their centre and the horn goes there.
So does a HOLD answered only at a retry, or after longer than 500ms: the
IOmcu may have let it go and held again elsewhere, so where it holds
is not known on the panel, and the pause ends rather than keep an angle a
later change of profile would send to the servo. The link going ends a
sweep or a pause too, and the horn is drawn at rest, as the IOmcu
rests the surfaces 500ms after the last write it heard; no sweep or hold
asked for before is sent when the link comes back, and the next tap starts
a sweep.
A finger on the dial, CENTRE, RELEASE, STOP, a switch to SAFE and leaving the screen
end a sweep, running or paused, and the button reads SWEEP again. A changed
type, frame rate, pulse, trim, travel or reverse ends a pause too, and the
servo is held at the paused angle as a position. With feedback the paused
angle is the one the servo last reported, which it can still be moving to
after the tap; without feedback it is the drawn estimate at the
acknowledgement. A changed SPEED keeps the
pause; the resume runs at the new SPEED. Touch events going missing pause a
running sweep as PAUSE would. A changed setting starts a running sweep over
with the new curve, drawn from when the IOmcu acknowledges it; until
then the old curve is drawn on, as the IOmcu runs it, under the
pulses and TRAVEL it was sent with. Each acknowledgement is matched to the
command it answers: a SPEED or curve changed while a start waits does not
let the earlier command's acknowledgement draw the newer one. A changed
profile or frame rate goes with it at once. SWEEP is offered while the
bench is ARMED, with a surface bound and the link up, and an IOmcu speaking
protocol 4.2;
greyed otherwise; the IOmcu stops a sweep the panel has not
repeated for 500ms and leaves each surface where its output has got to.

**SPEED LIMITS THE SWEEP** replaces SPEED's label on the right card, in the
warning colour, while SPEED is slower than the fastest change the curve asks
for. SPEED then shapes the motion instead of CURVE: a square, a sine and a
triangle all move as ramps at SPEED's rate and look alike, and the output
can turn back before it reaches an end. The line follows the settings, so it
shows before SWEEP is pressed as well as while a sweep runs. Raise SPEED, or
lower SETTINGS > SWEEP's SPEED (the rate in Hz) or RANGE, to clear it. A square
jumps between its ends, so it shows at every SPEED below 100%. With the
SWEEP page's defaults (sine, 0.5Hz, RANGE 80%) and TRAVEL +/-90deg it
clears at SPEED 63%, and for a triangle at 40%.
[Servo procedures](Servo.md#sweep-and-speed) gives the rule.

![SPEED limiting a sweep](img/servo-sweep.png)

**START TEST runs the automatic test** from TEST's AUTOMATIC TEST card: the servo stepped
through the supply voltages chosen there, its current measured at rest,
moving and holding, its travel timed, and the voltage at which it stops
moving. [Servo procedures](Servo.md#automatic-test) gives the method and the
files. The card, 600 x 200px, holds the run's name, a summary line
(`4.8 6.0V BROWN-OUT, 60s EACH`, or `..., 20 MOVEMENTS EACH`), START
TEST (300 x 56px) with the card's SETTINGS (160 x 48px) beside it, the
state line, the last result with its files
(`LAST: PASS ATEST004.CSV + .TXT`) and the STALL AT note. START TEST needs
the bench in ARMED and a supply that answers; the state line says ARM FIRST, NO STEP CHOSEN, SUPPLY NOT ANSWERING, RANGE TOO
SMALL (the ends RANGE and TRAVEL give land on PULSE CENTRE), A STEP IS
OUTSIDE THE CAPS (a chosen step above the voltage cap in force, checked
before any warning opens) or LAST REPORT STILL WRITING, and follows the
bench and the settings as they change. The note under the last result says when no reading can pass STALL AT: `STALL AT 2.00A
cannot be reached: current limit 2.00A` while STALL AT is at or above the
SET line's current limit, else `STALL AT 2.00A cannot be reached: INA3221
range 1.638A` while the INA3221 is the rail's meter and STALL AT is at or
above its range. START TEST stays open: see
[Stall and constant current](Servo.md#stall-and-constant-current).
It is a two-second hold,
the gesture OUTPUT ON uses, because a run switches the supply on and moves
the servo. With HV SERVO on and a step above 6.0V chosen, a tap opens HV
SERVOS ONLY instead, naming the highest step, and the run starts only after
its HOLD 2s TO APPLY is held. HV SERVO is for the session: every restart
turns it off.

TEST stays shown as the run starts, and the card's state line reads the
step. LIVE shows the run's box on the left of its card, under `ARM FIRST`,
346px wide and clear of the horn at every angle: its step and
phase, with the meter the run reads beside STOP TEST: `PD mini`,
the INA3221 channel, `INA 0x41 CH2`, or `SIMULATION`. It is the servo rail's meter at START TEST, and
the run reads it to its end ([The meter](Servo.md#the-meter)). The run owns
the servo and SUPPLY's set points and switch until it ends. It ends early, the output switched off and the servo released
to its centre, on STOP TEST (in LIVE's box or on the card), STOP, a
switch to SAFE, the link going, leaving the screen, a finger on the dial, CENTRE,
SWEEP, RELEASE, a tap on a set point, a change to the servo's type, pulses,
trim, travel, reverse or SPEED, touch events going missing, and on the
supply: see [the list](Servo.md#what-ends-a-run). Once a run is over, whichever screen is up, SUPPLY's set points go back to
what they were before it. That waits until the run's OFF has been sent, a
reading taken after that in which the supply itself reports the output off
(not the panel's request, which the PD mini follows a link exchange and a
module transaction later), no ON is on its way and
OUTPUT ON is not being held on SERVO or SUPPLY. Set points changed after
the run ended are
left as they are.

![A run](img/servo-run.png)

The result stays in LIVE's box until CLOSE, and on the card's LAST line
until the next run: PASS, FAIL or ABORTED and why, the longest travel time and the highest holding current, and the files
the card took in `USER/LOGS`: `ATESTnnn.CSV`, and `+ .TXT` once the card has
taken the report whole. With User logs OFF on [Setup](#setup) > SD CARD the
line reads `USER LOGS OFF` and no file is written.

![A result](img/servo-result.png)

A run that read the INA3221's windows names it there, as the line over the
plot does:

![A result on the INA3221](img/servo-result-ina.png)

**START LIMIT RUN runs the limit search** from TEST's LIMIT RUN card: the servo
stepped outward in 10us steps until its current shows the linkage bound,
and, with LIMIT RUN on SEARCH + PROGRAM, a KST or Chaservo servo's LEFT ANGLE
and RIGHT ANGLE lowered until neither end binds. On a SERVO PWM pin the
search runs from PULSE CENTRE to PULSE MIN and PULSE MAX; on PROG PORT a
read session comes first, and the search runs from the servo's NEUTRAL to
its own PULSE LOWER and PULSE UPPER. [Servo procedures](Servo.md#installed-mechanical-limit)
gives the method, the protections and the report. It is a 2s hold while
the bench is ARMED and the INA3221 is the servo rail's meter. Over it the
card's summary names the mode, `SEARCH ONLY` or `SEARCH + PROGRAM`, and
SERVO PULSES while the run reads the port. Under it a line names the
output it drives and the meter, `OUTPUT PROG PORT 1 GPIO 13, CURRENT
INA3221 0x41 CH2` or `OUTPUT SERVO PWM GPIO 2, CURRENT INA3221 0x40 CH1`
(`CURRENT INA3221` while no channel watches the target), the next line says why
it cannot start or what it does, then the last result and its report.
These four lines are in the 10 x 20 face, 20px apart, the first 2px under
START LIMIT RUN, so the fourth ends 2px inside the 200px card.
TEST stays shown as the run starts. Its box on LIVE's left card shows the
values written of 8, the phase with the side and the pulse, and the output
it drives, with STOP RUN; after the run, the verdict, the values or the
ends, and `LIMITnnn.TXT` until CLOSE (`USER LOGS OFF` with User logs OFF). The run switches the bench to SAFE and
to ARMED itself for each write; the operator's STOP, SAFE or any of the
automatic test's reasons end it with the output off.

![The limit run, ready](img/servo-limrun-ready.png)
![The limit run, programming](img/servo-limrun.png)
![A programmed servo](img/servo-limrun-pass.png)
![The servo's pulses outside PULSE MIN and PULSE MAX](img/servo-limrun-range.png)

### Programmer

![PROGRAM's list](img/servo-program.png)
![A row the IOmcu lacks the capability for](img/servo-program-nocap.png)
![A row whose port a console Hitec session holds](img/servo-program-busy.png)

PROGRAM in the strip shows the list of programmers in the page area, one
row each: the name on the left, and on the right what the row drives with
its pin, `PROG PORT 1 GPIO 13`, then `>`. The KST and Chaservo row is on the
port SERVO TESTER names, PORT 1, PORT 2, PORT 3 or PORT 4, and reads `PROG
PORT` with its pin while SERVO TESTER is OUTPUTS. A row is dimmed with
`PORT IN USE` while another session holds its port: another row's, or a
HITEC session the bench console opened, which the panel reads from the
HITEC page's STATE every 500ms. The Hitec row is not dimmed by a HITEC
session: once its programmer has read the page, a port the page holds for
another session opens that programmer with RELEASE PORT
([A port another session holds](HitecProgrammer.md#a-port-another-session-holds)).
The list has two rows, `KST and Chaservo servos` (German `KST- und
Chaservo-Servos`) and `Hitec servos` (German `Hitec-Servos`). A tap on a live row
opens its programmer over the left column, the strip's tabs with it, one
level down, and ends a running automatic test. CLOSE in the programmer, or
the home tag, climbs back to the list; the next home tag goes home. Over a
keypad, a list, a warning or the keyboard in the programmer the home tag
closes that one, and the programmer stays.

A row is drawn dimmed, with its reason in place of its port, and a tap on
it opens nothing:

| Reason | When |
| --- | --- |
| `IOMCU HAS NO SERVO PWM` | the IOmcu, the RP2350 board, does not report the capability the row needs; with no IOmcu answering it reports none. The capability's name is the link's, the same in German |
| `PORT IN USE` | another row's programmer holds the same programming port |

The rows come from one table in the firmware, `shared/ui/prog_registry.c`.
A new servo maker is one row there and its programmer, never a tile or a
tab. A list of more than 9 rows gets a SEARCH box that opens the keyboard,
`*` for any text; a list showing more than 8 rows draws them under their
group headings; a list taller than the page area pages with `^` and `v`. A
list holds 48 rows at most. With two rows none of these is drawn. Modelled
protocols without a transport on the IOmcu have no row
([PROGRAM](#program) on MOTOR & ESC).

A row is 48px tall with 8px under it, a heading 28px with 4px under
it, both in the 10 x 20 face. The bar over a paged list is 48px tall:
SEARCH 260px wide on the left, `^` and `v` 56px each on the right.
Every entry ends 4px above the alert band: on SERVO's left card 7 rows
fit without the bar, 6 under it. MOTOR & ESC's list is the same list.

The KST and Chaservo programmer takes the programming port the tester
drives out of PWM (pulse-width modulation) and reads and writes the
servo's settings on its signal wire. PROG PORT 1 is GPIO 13 on pad 17 by
default. It connects only with SERVO TESTER on SETUP > OUTPUTS set to a
port, PORT 1, PORT 2, PORT 3 or PORT 4; with OUTPUTS it shows `Use the programming port to
program a servo.` and `Set SERVO TESTER to PORT 1 to PORT 4 on SETUP >
OUTPUTS.`, and CONNECT is locked. While it holds the port it stays open: CLOSE and the
home tag open LEAVE PROGRAMMING, and while the port moves into or out of
programming they do nothing. Leaving SERVO leaves the port as it is; back
on SERVO the programmer is open again on PROGRAM. ARM is drawn disabled; a
press on it arms nothing and shows `Cannot switch to ARMED: a port is in
use by the servo programmer.` in the alert band, on SERVO and on MOTOR &
ESC alike. CENTRE, RELEASE and STOP work, and COMMANDED reads `---`: the
tester sends no pulse to the port. In PWM with a PD mini answering, the
programmer's PORT row names the voltage and the current limit CONNECT sets,
`PORT  5.0V  1000mA`. The set points and OUTPUT ON of
the SET row and of the SUPPLY screen are dimmed and take no press for as
long; OUTPUT OFF works.
It leaves programming without a cycle of the servo's supply. Whether a KST
servo follows PWM while in programming mode is not measured.

The programmer's layout on the 620px left card, every control 48px tall
or more and ending 4px above the alert band:

| Part | Size |
| --- | --- |
| tabs and CLOSE | one row at the top, 48px tall: five tabs of 95px, CLOSE 120px |
| rows | 48px tall at a 56px pitch, up to 5 a tab, in two columns of 295px; a wide row spans both |
| values | 150px (15 cells of the 10 x 20 face); a wide row's 260px, with its `-` and `+` keys of 56px inside it |
| notes and messages | under the last row, 10 x 20 face, 60 cells a row, 20px a row, to 4px above the alert band: 7 rows under the SERVO tab |
| lists (FILES, RESTORE, MORE, a factor) | a title, then up to 7 entries 48px tall at a 56px pitch, CANCEL 150 x 48px at the foot |
| warning pages | the title in the 20 x 36 face, up to 16 rows of lines in the 10 x 20 face (fewer in a shorter area: the rows that fit 8px above the buttons), the hold button 300 x 56px and CANCEL 180 x 56px at the foot |

The tabs' labels are in the 10 x 20 face when every label fits its 95px
tab, as in English, and in the 8 x 16 face when one does not, as German
`ABSTIMMEN` does. No note, message or control lies under the alert band.
Every line of a message is drawn: the longest takes 5 of the 7 rows under
the SERVO tab. Where the two standing lines leave no row beside the
message for a page count, they yield to it: they page with the other
notes, first, and a line taller than a page waits for the message to go.
In German the tallest pair (4 rows) and the tallest message (5 rows) need
9 rows; there the pair waits.

[Servo programmer for KST and Chaservo servos](ServoProgrammer.md)
describes it.

#### Hitec programmer

The Hitec programmer opens from the row `Hitec servos` with the same
geometry: five tabs (SERVO, MOTION, TRAVEL, PROTECT, INFO) and CLOSE, rows
of 48px at a 56px pitch, values of 150px and a value tab's wide values of
260px, notes in the 10 x 20 face to 4px above the alert band, and hold
pages with a 300 x 56px hold button and CANCEL 180 x 56px. It reads the
servo and writes the edited values back: a tap on a writable value opens a
list of up to 6 entries or the keypad, ENABLE WRITE TO SERVO on SERVO is
the write switch, and WRITE n VALUES, 220px wide, opens the write page.
The SERIES row sets D SERIES or CLASSIC before CONNECT; the PORT row names
the port, its GPIO, the series, the receive mode and the supply CONNECT
sets. While it holds the port, ARM, the SET row's set points and OUTPUT ON,
and the SUPPLY screen's are locked as under the KST programmer, and a press
on a locked supply control says `Servo programming holds the supply: set
points and ON are locked.` While the port is held FILES takes the SERIES
row's place: the servo files in `BENCH/HITEC/CONF`, 5 a page under the
entry that saves, the text keyboard of a new name and CHECK THIS FILE for a
file of another model or past a soft rule. RESTORE writes the session's
backup back, and MORE holds FACTORY DEFAULT for a classic servo. A port the
HITEC page holds for another session opens it with RELEASE PORT, a hold of
2s that gives the port back to PWM with the supply as it is. [Servo
programmer for Hitec servos](HitecProgrammer.md) holds the rows, the flow
and the messages.

![The Hitec programmer, the tester holding the port](img/servo-hitec.png)
![A D485HW read](img/servo-hitec-read.png)
![A port another session holds](img/servo-hitec-other.png)
![The write page](img/servo-hitec-write.png)
![The Hitec files on the card](img/servo-hitec-files.png)
![RESTORE of the session's backup](img/servo-hitec-restore-page.png)

### Settings

SETTINGS in the strip shows the servo's settings in the page area, on three
pages under their own tabs: OUTPUT, SWEEP and DUT (German `AUSGANG`,
`SWEEP`, `PRÜFLING`). Each run's own settings open from its card's
SETTINGS on TEST, as a list in the page area one level down. CLOSE at the
top right of the list, in the line of the run's name, or the home tag goes
back to the cards. Another task hides the list, and TEST shows it again. The output card stays where it is and works. A
value opens the keypad, a list opens a list, a switch flips on the tap, and
the name opens a keyboard.

SETTINGS' page tabs are 197 x 40px across the card. Its rows are two
columns of 295px, each row 48px tall and 52px from the next, the label
left and the value right, 140px wide (TYPE, FRAME RATE and NAME take both
columns). The notes under OUTPUT's rows are lines of 60 characters, 21px
apart. A run's settings are three columns of 193px, each row its label
over its 48px value, 71px from the next: the automatic test's 15 rows
are five to a column, the steps with HV SERVO, the pass and fail limits
with BROWN-OUT, and the length and timing. TEST LENGTH BY tops the right
column, under the card's SETTINGS: a second tap on SETTINGS opens its list
and changes no setting. The notes, the save line, the STALL AT note and
the caps line under TEST's cards end 4px or more above the alert band. A
list's choices are
two columns of five, 48px tall and 56px apart; the warnings read in
12 x 24px letters, 49 characters a line, with HOLD 2s TO APPLY and CANCEL
56px tall ending 4px above the alert band.

![The servo's settings](img/servo-settings.png)
![The SWEEP page](img/servo-sweep-page.png)

| Where | Setting | What it does |
| --- | --- | --- |
| SETTINGS > OUTPUT | TYPE | the servo profile: STANDARD PWM, NARROW 760, WIDE, HELI CYCLIC or HELI TAIL 760 |
| SETTINGS > OUTPUT | FRAME RATE | how often a pulse is sent; the type's list, or CUSTOM on the keypad |
| SETTINGS > OUTPUT | PULSE MIN, CENTRE, MAX | the pulse widths the travel maps onto, 400 to 2500us: -90deg is MIN, 0 is CENTRE, +90deg is MAX, and RELEASE rests at CENTRE. An end lies no further from CENTRE than CENTRE lies from 400us or 2500us |
| SETTINGS > OUTPUT | TRIM | added to the centre, 5us a step, up to 200us either way; `-` and `+` step it, a tap on the value opens the signed keypad |
| SETTINGS > OUTPUT | TRAVEL | how far the horn may go each way, 10 to 90 degrees |
| SETTINGS > OUTPUT | REVERSE | the direction the horn's angle maps onto the pulse |
| SETTINGS > SWEEP | CURVE, SPEED, RANGE | the movement of SWEEP: square, sine or triangle, 0.05 to 5Hz, 10 to 100% of the travel. The automatic test moves in steps between the ends RANGE gives |
| AUTOMATIC TEST > SETTINGS | TEST LENGTH BY, TEST TIME, MOVEMENTS | how long each supply step runs: a time, or a number of movements |
| AUTOMATIC TEST > SETTINGS | DWELL TIME, SETTLING TIME | held at each end, by the test and by SWEEP; waited after a supply step before measuring |
| AUTOMATIC TEST > SETTINGS | STEP 4.8 / 6.0 / 7.4 / 8.4V, BROWN-OUT | the supply steps, and the brown-out run from 5.0V down; 7.4 and 8.4V run only with HV SERVO on |
| AUTOMATIC TEST > SETTINGS | HV SERVO | adds the 7.4 and 8.4V steps, off by default and at every restart; a run with them starts only through HV SERVOS ONLY |
| AUTOMATIC TEST > SETTINGS | STALL AT | above this current the servo counts as stalled |
| AUTOMATIC TEST > SETTINGS | IDLE CURRENT, HOLD CURRENT, TRAVEL TIME | pass/fail limits; 0 is not checked |
| LIMIT RUN > SETTINGS | LIMIT RUN | SEARCH ONLY or SEARCH + PROGRAM, a tap switches; SEARCH + PROGRAM is offered for a servo on the programming port only, and the row reads SEARCH ONLY with the reason under it otherwise. For the session |
| LIMIT RUN > SETTINGS | SERVO PULSES | KEEP or 1000-2000, a tap switches; shown with TARGET on PROG PORT only. 1000-2000 writes the servo's PULSE LOWER and PULSE UPPER to 1000.0 and 2000.0us in the run's read session, before the search. KEEP by default; for the session |
| SETTINGS > DUT | NAME | the device under test, up to 23 characters, for the report |
| SETTINGS > DUT | REPORT | a text report beside each test's log |
| SETTINGS > DUT | ENC CENTRE | a tap takes the live count as the centre, with the servo at its neutral, and sets nothing without a reading from the current link (none after a link loss until a new one arrives) or while the sensor reports no magnet; the row shows the stored count, 0 to 4095, and is dimmed while AS5600 is off. The same setting as SETUP, INTERFACES, AS5600 centre |

TRIM's keypad is the one signed keypad: its `+/-` key, between the point
and OK, puts a minus sign in front of the entry or takes it away, before or
after the digits. OK takes -200 to +200us and puts the value on the 5us
steps of `-` and `+`, the nearer one: 13 is 15, -12 is -10, -0 is 0. -201
keeps the keypad open and names the range in the warning colour; the sign
alone is nothing typed. The other keypads have no `+/-` key and their keys
stay where they are.

![TRIM's keypad, -25us typed](img/servo-trim-keypad.png)

AS5600, the output encoder, is switched on and off on SETUP > INTERFACES
alone ([the output encoder](#interfaces-the-output-encoder)); DUT's note
says so. It adds the horn's angle to the MEASURED row and to a run's CSV
and report ([Servo](Servo.md#the-output-encoder)).

SUPPLY's caps, the voltage and current the set points and a run's steps stay
inside, show read-only under TEST's cards and on DUT, with their home and
the caps in force: `Caps 21.00V 5.00A: SUPPLY > SETTINGS` (German
`Grenzen 21.00V 5.00A: SUPPLY > OPTIONEN`). They are set on SUPPLY >
SETTINGS.

![The DUT page with the output encoder on](img/servo-encoder.png)

The TARGET row of the output card, between SPEED and CENTRE, names what
the tester drives as `protocol pins`, centred in its value box, and is the
control that picks it, as MOTOR & ESC's TARGET row is. A tap on the box
opens SETUP > OUTPUTS on what the row shows, where the IOmcu sends the
commands, not on the TARGET setting: on the PROG PORT entry while it
drives the port, on SERVO PWM while it drives the outputs, and on the
overview while the target is not known or no IOmcu answers
([OUTPUTS](#outputs)). A TARGET write still pending, or PROG PORT asked
for with no pin on the port, opens on what is driven. There SERVO TESTER
picks OUTPUTS or PROG PORT, the TARGET setting, and PORT PWM the port's
pulse, both kept in NVS (non-volatile storage). Leaving SERVO switches the
bench to SAFE and ends a sweep or a test; the home tag on OUTPUTS comes back
to SERVO on the task the row was tapped on. The row is locked
(outlined, its value dimmed, and it takes no press) while the bench is
ARMED, during a sweep (running or paused), an automatic test or a limit
run, and while the servo programmer holds the port; it names the target all
the same. The output card stays in sight under the pages, their keypads and
the servo programmer, so the row names the pin while any of them covers the
left column.

SERVO TESTER holds OUTPUTS, PORT 1, PORT 2, PORT 3 or PORT 4. With
OUTPUTS the row names the GPIOs bound as SERVO PWM in the binding the
tester drives: `SERVO PWM GPIO 4`, `SERVO PWM GPIO 2 GPIO 4`; as many pins as fit
the 25 characters of the value, then `+N`: 8 pins from GPIO 40 read
`SERVO PWM GPIO 40 +7`. The row is in the warning colour, label and
value, with no such pin (`NO SERVO PWM PIN`) and before a binding has been
read (`PINS NOT READ`). With a programming port the row reads `PROG PORT 1
GPIO 13` in the warning colour, the port's number and the GPIO the IOmcu
reports for it. The tester's commands then do not reach the SERVO PWM
outputs; while the bank is ARMED they hold their rest pulse, the centre
between their endpoints. A port target without a usable pin (none, refused
at start, FUNCTION NONE, or a port the IOmcu does not have) reads `PROG
PORT 1`, also in the warning colour. The IOmcu then keeps the standard
outputs as its target, and the tester sends them no command but a
release, and SWEEP is greyed, until the port has a SERVO PWM pin or
SERVO TESTER is OUTPUTS. With PORT PWM
off the port renders no pulse, and the row reads `PORT 1 GPIO 13 PWM OFF`, 22
characters with a two-digit pin, or `PROG PORT 1 PWM OFF`. Two states name
no target, both in the warning colour, and the tester sends no command in
either: `NO LINK` while no IOmcu answers, and `UNKNOWN` while the
IOmcu's programming-port page has not been read since the link came
up, a write to it was not acknowledged, or a read of it went unanswered.
While the servo programmer holds the port, the row does not change: it keeps
naming the target the IOmcu holds, a port or the outputs; the
programmer drives the port and the tester sends nothing. The page is read
every 500ms. An IOmcu without that page (protocol older than 4.13) has
the standard outputs as target. The row follows the target the IOmcu
holds, not the setting: a SERVO TESTER change waits for a quiet bench and
reaches the row once the IOmcu has taken it. The IOmcu starts with the
outputs as target, and moves the target to the outputs while a
programming session leaves its port without a pin. In SAFE the panel
writes the set port back within one read of the page, 500ms, without
waiting for the next ARM.

![The TARGET row with PROG PORT](img/servo-target.png)

| Type | Centre | Travel | Frame rates |
| --- | --- | --- | --- |
| STANDARD PWM | 1500us | 1000-2000us | 50, 60, 100, 150, 200, 250, 300, 333Hz |
| NARROW 760 | 760us | 660-860us | as STANDARD PWM |
| WIDE | 1500us | 800-2200us | as STANDARD PWM, up to 312Hz |
| HELI CYCLIC | 1520us | 820-2220us | 50, 120, 200, 333Hz |
| HELI TAIL 760 | 760us | 410-1110us | 200, 333, 560Hz |

A pulse needs a pause before the next frame. STANDARD PWM, NARROW 760 and WIDE
keep at least 1ms between pulses, so their fastest rate is 1 / (longest
pulse + 1ms): 333Hz for 2000us. The heli profiles run at Rotorflight's
rates for digital cyclic and narrow-band tail servos, with at least 0.5ms
between pulses. A frame rate the pulses leave no pause for is refused, and a
longer PULSE MAX is refused at a rate it does not fit. The pause is kept after
the longest pulse the IOmcu can render, the top of the range a command
carries: PULSE MAX, or past it when CENTRE is off the middle, by as much as
CENTRE lies nearer MAX than MIN. So a CENTRE that would push that top past
the rate's pause is refused too.

**A heli type, or any frame rate above 60Hz, can destroy a servo that is not
made for it.** Choosing one opens a warning in the danger colour that names
what is chosen and what it does to a servo not made for it; it is applied only
after HOLD 2s TO APPLY has been held. CANCEL, a finger that slides off,
a touch loss and leaving the screen apply nothing. The type and frame rate in
force stay red on the right card, and every restart goes back to STANDARD PWM
at 50Hz: a servo plugged in after a restart never meets a rate meant for
another one.

![The warning](img/servo-warning.png)

**The frame rate reaches the pins** through the IOmcu's SERVO page
(protocol 4.1). It applies to the TARGET alone. With OUTPUTS, the standard
servo ports, as target that is every PWM output whose first channel is a surface; a
PPM output keeps its own frame. With PROG PORT as target it is the
programming port's pin, and every slot keeps its own rate (protocol 4.19).
Every other programming port runs at 50Hz. A port whose pin shares its PWM
slice with another programming port's pin runs at 50Hz only: the RP2350
gives both pins of a slice one divider and one wrap, so a faster rate there
is refused. A change of TARGET puts the rate back to 0, each output at
its own rate, and the screen sends its rate again with the next position. It goes with every held position,
and a profile changed while the servo rests ARMED restates the rest with it;
one changed while a switch to ARMED is on its way is the one that switch uses.
A faster rate goes after the pulse widths, once all of them have landed; any
other rate goes before them, and nothing wider goes out until it has landed,
so the pins never carry a fast rate with a slower profile's wider pulses. The
OUTPUT page says what became of it:

| Note | Meaning |
| --- | --- |
| In force | the rate runs on the target the IOmcu holds, named from its PROG page: `Active: OUTPUTS run at 330Hz.` or `Active: PROG PORT GPIO 13 runs at 330Hz.` The note is in the warning colour while that target differs from the TARGET chosen (second line `The TARGET chosen is not active.`; PROG PORT with no pin bound to the port leaves OUTPUTS the target), while the port target has no pin, while PORT PWM is OFF (`PORT PWM is OFF. The port sends no pulses.`) and while the target is not known. Against an IOmcu before protocol 4.19 the note reads "every PWM surface runs at": such an IOmcu runs every PWM surface at the rate, whatever the TARGET. With no link both read `No link. The active frame rate is unknown.` in the warning colour: an IOmcu restarted while the link is down runs every pin at its own rate again |
| The rate goes with the next position | not written yet |
| REFUSED | a pulse of the target (a surface, or the programming port while its pulse is on) is longer than the frame at that rate, or a target pin shares a PWM slice with an output at another rate; the pins keep the rate they had |
| This IOmcu takes no frame rate | protocol 4.0: every PWM output runs at its binding's 50Hz |

An IOmcu restart, and every binding written on OUTPUTS, put each slot
back at its own rate, 50Hz for a servo; the screen sends its rate again with
its next position, against the binding then in force. A binding is not
written while the reset to each slot's own rate goes unanswered (OUTPUTS says
`NO LINK`) or is refused (`REFUSED`), and the bench
does not switch to ARMED, from any screen, while the rate the TARGET runs at is not
known: the switch to ARMED is refused with `Servo frame rate unknown. Hold ARM again.`, and
a switch to ARMED made while the link was down reaches the IOmcu only once the
reset has landed.

The OUTPUT settings, HV SERVO, LIMIT RUN and SERVO PULSES are kept for the
session; SWEEP's, the automatic test's and DUT's settings and TARGET are
kept in NVS. A change asks for the write at once; the store takes it at
the first frame the bench is idle ([Keeping the values](#keeping-the-values)).
The save line says `SAVED`, `SAVE PENDING`, `NOT SAVED` or `SETUP CHANGES
NOT SAVED`: at the foot of each SETTINGS page, and under the run's name on
a run's settings.

![TEST and its two run cards](img/servo-test.png)
![The automatic test's settings](img/servo-test-settings.png)
![The limit run's settings](img/servo-limits.png)
![The name](img/servo-name.png)

Current limitations:

- Until the screen sends a position, the pins run at the rate the SERVO page
  holds, which after a restart or a new binding is 50Hz.
- On the panel's supply model the automatic test's currents are the model's load, not the servo's: a run there
  measures the model. [What is not measured](Servo.md#what-is-not-measured).
- The supply on the right card is SUPPLY's: the PD mini when SETUP enables
  it, the panel's model otherwise, whose voltage, current and power are
  simulated, not measured.

## Supply

![Supply](img/supply.png)

Sets, switches and records a programmable supply: the PD mini, a USB-PD (USB
Power Delivery) trigger controlled over a UART (universal asynchronous
receiver-transmitter). The IOmcu drives it on a PIO (programmable
input/output) UART on two of its pins, through the SUPPLY link page
(protocol 4.3). The panel writes the page and reads it every 100ms. With
the PD mini enabled on SETUP under INTERFACES, the header names the module
and the IOmcu's UART pins that carry it, `PD MINI TX GPIO 12 RX GPIO 13`,
from the PD mini TX and PD mini RX settings. With either pin not set, or
both on one GPIO, the header says `PD MINI NO PINS`. With the PD mini disabled, the panel runs a model of a supply in its place: the header
says SIMULATED SUPPLY and the menu tile is marked SIMULATED.

SUPPLY is a workbench: a strip of tasks at the top, LIVE and SETTINGS
(German `LIVE`, `OPTIONEN`), each 150 x 40px, and a page under it. The
menu's tile always opens LIVE. A tap on a tab switches the task; a switch
is no navigation level and leaves LIVE's view, PLOT or TABLE, as it was.

| Task | The left column, 620px |
| --- | --- |
| LIVE | the plot or the table over the set points |
| SETTINGS | the 9 supply settings under LIMITS, AFTER RESTART, TRIPS and CONFIRM WHILE ON ([Supply settings](#supply-settings)) |

The header is the row right of the strip: the module and its pins, ONLINE
or NOT ANSWERING, in 10 x 20, and at the right end the range the set points
may take, in 8 x 16, for example `3.3-8.4V  0.50-5.00A`. It stays in sight on both tasks and
under the keypad and the question. The rail on the right, 380px, is the
same on both tasks: four reading cards of 64px, VOLT, CURR, PWR and MODE,
and the CONTROL card with the run's mAh and Wh on its title line, RESET
PEAKS (48px) and OUTPUT ON / OFF (60px) at its foot, where SERVO and
MOTOR & ESC have ARM and DISARM.

The layout is the one MOTOR & ESC uses. On LIVE the left column holds the
output card, PLOT | TABLE under its title, and SET POINTS under it. Each
set point is a row: its label, then the `-0.1` key (72 x 48px), the track
(312 x 48px), the `+0.1` key and the value in a 120 x 48px box. The plot
shows voltage, current and power over the last 30s. The rail shows the readings, the run's lowest
voltage and its highest current and power. MODE says which set point the
supply holds: CV (constant voltage) at the set voltage, or CC (constant
current) at the current limit. CC is drawn in the warning colour: a supply in
CC is not giving the load the voltage it was set to.

A set point is shown beside its reading. VOLT and CURR carry it in brackets
after the label, and the plot draws it as a dashed line in the reading's
colour, on the reading's scale. TABLE lists both. The bracketed value is the
set point the supply reports holding; while it does not answer, the one the
screen holds.

| Set point | Model | PD mini | Buttons |
| --- | --- | --- | --- |
| VOLTAGE | 3.3 to 21V, 20mV steps | 1 to 20V, 10mV steps | 0.1V |
| CURRENT LIMIT | 0.5 to 5A, 50mA steps | 0.05 to 3A, 10mA steps | 0.1A |

The model's ranges are a USB-PD PPS (Programmable Power Supply) source's
widest profile, 3.3 to 21V at up to 5A. The PD mini's are from the
vendor's page, not measured. The PD mini is a buck converter and gives no
more than it is fed: its voltage is also capped 0.7V under the input voltage
it reports, so a 5V input voltage allows at most 4.3V. An input voltage
under 1.7V leaves the 1V minimum, which the screen shows as the cap, and
one under 1.5V holds the output off; until the input voltage is read the
cap is 20V. A set point over the input voltage
puts the module into ERR until it is power cycled; the margin it needs is
not measured. The cap lies 0.2V above the line the output is cut at (see
below), so the input voltage can drop 0.2V under load before a set point at the
cap is cut. Both are narrowed by the caps in SETTINGS, and
the sliders follow the supply in use. A tap on a track sets the value under
the finger.

**A tap on the VOLT or CURR card, or on a set point's value box, opens a
keypad** over the left column, under the strip, with keys of 145 x 76px. It shows the range in the title row and the current
value faint until a digit is typed. OK takes a value inside the range,
rounded to the supply's step; a value outside it is refused and the range
turns to the warning colour. OK with nothing typed, and CANCEL, leave the set
point as it was.

![The keypad](img/supply-keypad.png)

**A change to a live output asks first.** While the output is on, a new set
point from the slider or its step buttons, or from the keypad, opens a
question that names the change. APPLY gives it to the supply; CANCEL drops it
and the slider goes back. A drag asks once, on the release, and the supply
holds the old set point until then. A drag that ends without its finger
lifting (the home tag tapped with a second finger, or a contact the touch
controller loses) asks nothing, and the slider goes back. With the output
off nothing is asked.
STOP closes the question unanswered: an APPLY tapped in the same frame
(about 50ms) as STOP applies nothing. SETTINGS switches the question off for the slider and for the keypad
separately.

![The question](img/supply-confirm.png)

**OUTPUT ON is a two-second hold**, the gesture and the fade ARM uses. OUTPUT
OFF is a tap. STOP switches the output off on every screen. So does every
other stop the bench counts: touch that stops answering, an ON whose touch
events went missing before a whole frame saw the output on, the
IOmcu refusing to stay ARMED, a supply that stops answering, and a
trip. The output stays off until it is switched on again. Leaving the screen
switches off an output SUPPLY switched on: one reported on, or an ON on its
way. SERVO and ESC STICK do the same for the output each switched on, so an
output is on only while the screen that switched it on is shown. The
exception: an output SERVO switched on stays on after SERVO is left while the
servo programmer holds its port, until the port lets go (see SERVO). A servo
is powered from SERVO and an ESC from ESC STICK. MOTOR & ESC switches no
supply; an ESC run there is powered from its own source. An output another
screen switched on stays as it is when SUPPLY is left. The keypad, the
question, the other overlays of SUPPLY and a switch of task are no leave. The link-lost screen,
which has no STOP, does not open while the output is on.

**The home tag climbs one level at a time.** The output stays as it is
while it climbs:

| From | The home tag leads to |
| --- | --- |
| the question | what is under it; the change is dropped, as CANCEL drops it |
| a keypad | what is under it, LIVE or SETTINGS; the value is not changed |
| LIVE or SETTINGS | the menu; an output SUPPLY switched on goes off |

The tasks are siblings, not levels, as on MOTOR & ESC and SERVO: the home
tag never climbs from SETTINGS to LIVE.

**With the PD mini**, an OFF goes to the SUPPLY page before anything else
the panel owes it, and the IOmcu switches the output off itself when
the panel's heartbeat stops. An enabled PD mini reads NOT ANSWERING while no
IOmcu that speaks protocol 4.3 answers, or while the page's readings
are older than 1500ms; an output that is on is switched off then. A page
that answers while the module's reading count (SAMPLES) stands (a display
read that fails while the other reads answer) keeps ONLINE: after
1500ms without a new reading the voltage, current and power cards show
dashes and no trip, peak or total takes a reading, and after 3000ms (`SUPPLY_LINK_BLIND_MS`) an
output that is on is switched off and the band says
`No readings from the supply. Output off.`. A set point changed faster than
the module takes it (the slider dragged with SLIDER AND STEPS off, about
one new set point every 50ms) does not hold the readings back: a reading
400ms past its due (`PDMINI_POLL_LATE_MS`) goes before the next set-point
write, one at a time with another transaction between two; the state read,
and the input voltage read before a write, may also follow another reading straight
away. With a module that answers at once, the display is then read about
500ms after the last display read and the output state about 900ms after
the last state read, each with up to two transactions more. An output the
module switches on while it is off on the panel, or switches off itself, is
seen during the drag. The input voltage is read before a set-point write or an ON
only when one is waiting, and not between the reads for an ON. Set points
and an ON reach a module that answers up to 390ms late (the driver waits
400ms; host model, 40 start phases): one set point within 2.5s; an ON
within 3.6s when the set points are already written, up to 7.5s when it
is asked right after a set point change or a drag; a module switching its
output itself seen within 2.1s during a drag. An output the module
switches on while OFF is asked is switched off about 500ms later with a
module that answers at once, up to 1.8s later at rest and up to 2.1s
during a drag with one that answers up to 390ms late. The real module's
reply time is not measured.
Current limitation: a module that answers late stretches the 500ms of the
state and input voltage reads, as each transaction takes its reply time. At rest
the input voltage is read 850ms apart at 100ms late, 1.3s apart at 200ms and
2.5s apart at 390ms; the state up to 1.5s apart.
Current limitation: with a module answering 279ms or more late and the
slider dragged, the display is read only 3.7s to 5.0s after the last
display read (5.0s at 390ms), past `SUPPLY_LINK_BLIND_MS`; an output that
is on is switched off and the band says
`No readings from the supply. Output off.`. The panel reads the page every
100ms, which adds up to 100ms to the gap it sees: from 263ms late the
driver's gap is over 2.9s, so the cut is possible from 263ms. At rest the
display read stands up to 2.5s at 390ms. It stands over 1.4s from
259ms late and 1.5s from 279ms, so the cards can show dashes from about
260ms. The
output is also switched off, with a line in the band, when the IOmcu
refuses an ON (no heartbeat) or lets one go (its heartbeat stopped, or it
started again). The output is switched off too, and not on
again until a new hold, when the module switches it off itself: its
overcurrent protection or its button. The band also says when the pins are
refused, when the output would not switch, and when the set points would not
take. A pin or baud change, and enabling or disabling the PD mini, switch the
output off.

**A sagging input voltage switches the PD mini off.** While the output is on, the
IOmcu reads the input voltage of the module every 500ms with a module that
answers at once, and further apart with one that answers late (see above).
When the input voltage reads under the set point plus 0.5V on 2 reads in a
row, it switches the output off at once, and the band names the input voltage
and the set point, for example
`PD mini input 6.18V is below the required 6.00V + 0.5V. Output off.`. From the
sag to the cut takes about 1.0s with a module that answers at once.
Current limitation: with one that answers late it takes 1.7s at 100ms
late, 1.9s at 200ms and up to 4.7s up to 390ms (host model). The
output stays off until a new hold. An input voltage reading under 1.5V leaves no
room for the lowest set point, 1.0V: the ON is not written, and the band
shows the same message. One low reading switches nothing, and a
read that fails neither counts nor clears. While the output is on, its set
point is not lowered to follow a falling input voltage. The rule is chosen without
a bench measurement: whether a live module goes to ERR when its input voltage
sags is not measured, so the rule may cut a run that would have survived.

OUTPUT ON and OFF, RESET PEAKS and the readings stay live on SETTINGS and
under the keypad and the question. One finger at a time: while one holds a control, a
second finger does nothing anywhere on the screen.

### Supply settings

SETTINGS, the strip's second tab, shows the supply's settings in the left
column: the caps and the start values on the left, the trips and the
confirmations on the right, each a 48px row with its value in a 120 x
48px box, and the save line at the foot. Each is kept in the panel's NVS (non-volatile storage) and survives
a restart. A value opens the keypad over the page; a switch flips on the
tap. A tap on LIVE while the keypad is open closes it, and the setting keeps
its value, as CANCEL leaves it. The same holds for a set point's keypad and
a tap on SETTINGS. While the question about a live output is open, the
strip takes no tap until APPLY or CANCEL answers it. Every change is written at the next frame the bench is SAFE with
no switch to ARMED waiting, the supply's output is off with no ON queued and the board's
drawing is not being fetched or kept, and with it any change on SETUP that was not saved: a flash write stalls both
cores, OUTPUT OFF and the trips included. The bottom line says SAVED, SAVE PENDING, NOT SAVED (the write was refused), or SETUP CHANGES NOT SAVED: a
change made on SETUP and left without SAVE, which nothing writes until SAVE
there or a change here asks for it.

![The supply's settings](img/supply-settings.png)

| Setting | Range | Default | What it does |
| --- | --- | --- | --- |
| VOLTAGE MAX | 3.3 to 21V | 21.00V | the highest voltage a set point takes |
| CURRENT MAX | 0.5 to 5A | 5.00A | the highest current limit a set point takes |
| START VOLTAGE | up to VOLTAGE MAX | 6.00V | the voltage set point after a restart |
| START CURRENT | up to CURRENT MAX | 2.00A | the current limit after a restart |
| CURRENT TRIP | 0 to 5A | OFF | output off once the current has been over it for TRIP TIME |
| VOLTAGE TRIP | 0 to 21V | OFF | output off once the voltage has been over it for TRIP TIME |
| TRIP TIME | 0 to 5000ms | 100ms | how long a reading is over a trip before it fires |
| SLIDER AND STEPS | ON, OFF | ON | ask before the slider changes a live output |
| KEYPAD | ON, OFF | ON | ask before the keypad changes a live output |

A cap lowered under a set point brings the set point down to it at once, and a
start value with it. A typed value goes onto the setting's step the safe way: a
cap rounds down, so 12.01V allows 12.00V, and a trip typed above 0 is at
least one step, never OFF. A trip of 0 is off. The time over a trip counts from
the first reading over it. A reading under its trip starts the
count again; a reading that did not arrive leaves it where it was. A trip
switches the output off, MODE shows TRIPPED until the output is switched on
again, and the band says which trip fired. The supply holds its current limit
in CC, so a current trip at or above CURRENT LIMIT does not fire; set it below
the limit to switch off a load that draws too much for too long.

The output stays off at a restart, whatever the start values.

With the PD mini as the supply, SETTINGS also offers RESET PD MINI. It
switches the output off and restarts the module (its SYSTEM_RESET command),
for a module that shows ERR (a set point over its input voltage puts it there)
without unplugging it. The module is asked who it is again about 1s later.
Whether a restart clears every ERR is not measured. An IOmcu older than
protocol 4.4 has no reset: the output goes off and the band says so.

### The log

A run is one switch-on of the output. The plot clears when the output comes on
and holds the run after it goes off. The mAh and Wh on the CONTROL card's
title line count the run from the readings shown, each step capped at 1s. RESET PEAKS starts the
lowest and highest values again from the current reading.

A run is recorded to a `SUPPLnnn.CSV` of its own in `BENCH/LOGS`, one row
every 50ms, while Auto logs on [Setup](#setup) > SD CARD is ON:

```
time (s);set (V);voltage (V);limit (A);current (A);power (W);mode;charge (mAh);energy (Wh);source
```

A reading that did not arrive is an empty cell. `mode` is CV, CC or OFF, and
empty while the supply does not answer. `source` is `measured` for the PD mini's
readings and `model` for the panel's model. The bench takes the log: switching to ARMED during
a supply run ends that run's file, and an output still on at the switch to SAFE starts
a new file. That file's charge and energy go on from the totals counted since
the output came on, as the screen's do, so its first row does not start at 0. The supply is stepped and logged on its own 50ms cadence while
the control task waits on the link, so an unanswered IOmcu does not thin
out the supply's plot or its log.

The model is a 6Ω load with a 1.4A burst for 0.6s every 3s, behind a
0.05Ω source resistance. At the starting set points, 6.00V and 2.00A, the
burst takes it into CC. Its readings are not measured and nothing on the bench
is powered.

The PD mini's wiring (PD mini, PD mini TX, PD mini RX and PD mini baud) is
on SETUP under INTERFACES. TX and RX are IOmcu GPIO numbers: TX goes to
the module's DM, RX comes from its DP. Their `-` and `+` keys step over the
GPIO the other row holds, and at the end of the range do nothing: with RX on
GPIO 47, TX stops at GPIO 46. The keypad refuses the other row's GPIO and shows
`The other PD mini pin holds this GPIO` in place of the range. A set-up stored with both on one GPIO loads as
stored: the panel writes the page disabled, the PD mini stays off, and both
rows say `TX and RX on one pin. The PD mini stays off.` until one is
stepped. The PD mini switch turns the module off. The IOmcu refuses a pin that is
reserved, bound to an output, held by the sensor bus or the phase tap, or
the other pin, and the band says `PD mini pins rejected. Check SETUP >
INTERFACES.`: RX on GPIO 17 is refused while Sensor SDA is GPIO 16, as Sensor SCL
is then GPIO 17. The alert leaves the band once a read shows the page holding
the wiring asked: at once when the pin is stepped back to the one the page
still holds. Once the module has
answered, a change waits for a state read of the module sent after it, at
most about 1.2s, and is taken only if that read shows the output off: the
module can switch itself on between two state reads, by its button
or its AUTO OUT setting, and new pins would leave it no OFF path. A read
that shows the output on, or fails, refuses the change, and the band says
`PD mini wiring rejected. Its output may still be on.`. A refused change is not
written again until the wiring changes in SETUP; this alert too leaves the
band once a read shows the page holding the wiring asked. PD mini baud is the module's
own UART Baudrate setting: 9600, 19200 (as shipped), 38400, 57600,
115200, 230400 or 460800 baud, or AUTO. AUTO, the default, lets the
IOmcu find it: it tries each of the 7 rates, one a second. The
SUPPLY header shows the rate in use after ONLINE, e.g. `ONLINE 38400`, with
AUTO and with a fixed rate alike.

![RX's GPIO 9 refused for PD mini TX](img/setup-pdmini-why.png)

## Receiver

![Receiver](img/analyser.png)

Sixteen channels in two columns of eight lanes, 52px each. A lane holds the
channel's name in 10 x 20, its history, a bar 56px wide for its current value
and the value in us in 8 x 16. The history is 200 samples, one pixel each:
about 2.8s at 71 frames a second (one S.BUS frame every 14ms). CH17 and
CH18 are the two digital channels. A glitch is a spike in one lane; a dropout
is a notch across all sixteen at the same instant.

The state card on the right is 260px wide: the state word in 20 x 36, its
line in up to three lines of 12 x 24, BUS, RATE, FRAMES, RESYNC and BAD TAIL in
rows of 32px, and CH17 and CH18 as lamps 44px tall, whose dot lights while the channel is set; they take no tap. CHANNELS and RAW
DATA are tabs of 150 x 48px. RAW DATA shows the last frame's bytes, eight to
a row, and the decoder's counts in 8 x 16.

No firmware feeds this screen yet (see [Receiver buses](Receivers.md)); it
shows NO SIGNAL.

The state block shows one of NO SIGNAL, FAILSAFE, FRAME LOST and LIVE, with one
line of explanation:

![A receiver in failsafe](img/analyser-failsafe.png)

In FAILSAFE the receiver is sending well-formed values it has generated itself;
every trace is drawn red. Treat FAILSAFE as a stop, not as sixteen valid
channels. [Receiver buses](Receivers.md) describes the states.

## Battery

![Cell divergence](img/battery.png)

Cells are drawn as their departure from the pack's mean. The verdict follows
the spread, the widest gap between any two cells: HEALTHY below 30mV, WATCH
from 30mV, REPLACE from 60mV. The scale follows the pack down to a floor of
12mV and is printed beside the plot.

The plot card is 640px wide, the right card 360px. A bar is at most 48px
wide and reaches 164px either side of the mean. Under each bar the cell's
number and its voltage are in 12 x 24 up to 11 cells and in 10 x 20 from 12 to
14. The verdict is in 20 x 36, its line in up to two lines of 12 x 24, the six
rows 36px each, the note in 10 x 20.

Measure under load. At rest a weak cell reads like the others.

No firmware feeds this screen: no cell monitor is read, so a bench shows
NO PACK. The picture above is rendered from a model.

## Logs

![The file browser](img/logs.png)

Browse the card, open a file, check what the import detected, then plot:

![The import view](img/logs-import.png)
![The plot](img/logs-plot.png)

![A SERVO run as it opens: pulse, angle, current and voltage](img/logs-servo-plot.png)

The home tag climbs one level, as BACK does: the plot to the import view,
the import view to the file list, the file list to the menu. Over the DELETE question it is CANCEL. WI-FI, in the bottom row beside
RESCAN, opens [Wi-Fi](#wi-fi), and FILES beside it opens [Files](#files).

The file list takes the body in three parts. The header row, body y 4 to
52, holds ROOT, USER and BENCH (110 x 40px each), the folder and the
card's name, and PREV and NEXT (140 x 48px) while the folder holds more
than one page. The list under it is 1008px wide and shows 7 rows of 44px
at a pitch of 48px: each row takes a tap anywhere in its 48px. A row
shows the file's name, its date and, right-aligned, its size, in the
10 x 20 face. The name takes up to 60 characters; a longer one shows its
first 58 and `..`. The date is the modification time the card holds, as
[Files](#files) INFO shows it: in the zone of SETUP > PANEL's Time zone,
`2026-10-26 09:41`, in a column of 160px. A file stamped in 1980, an unset
clock's ([PANEL: Time zone and the clock](#panel-time-zone-and-the-clock)),
or with no stamp leaves the column empty. The size stands in a box of
100px at the row's end. The file's kind and length are not shown. The footer, body y 440 to 492, holds RESCAN
(140 x 52px), WI-FI and FILES (100 x 52px) at the left and DELETE and
OPEN (140 x 52px) at the right, and ends 4px above the alert band. A
message about a file stands between FILES and DELETE on up to two lines of
28 characters.

The import view shows DETECTED (400px wide) beside the column list (600px,
7 rows at a pitch of 48px), with four buttons of 246 x 52px under them.
The plot is 992 x 316px under four series chips of 240 x 44px; under it
run the time labels, then BACK (140 x 52px), `<` and `>` (96 x 52px) and
the cursor's line. The DELETE question is a box of 700 x 280px with CANCEL
and DELETE of 300 x 56px. Labels, rows, buttons and the import's warnings
draw in the 10 x 20 face; the sentences of the empty list, the question,
the boxes that say why nothing shows and the file messages in 12 x 24; the
plot's time labels and the ranges of the columns and the chips in 8 x 16.

The CSV (comma-separated values) reader accepts decimal comma and decimal
point, a units row and ragged rows; the import view shows what it decided
before the file is plotted. A file opens with up to 4 columns chosen: in a
SERVO file `pulse`, `angle`, `current` and `voltage`, each where it holds
numbers, then the next numeric columns; in any other file its first 4
numeric columns. Each numeric column shows its range, lowest to
highest: one decimal while both ends are below 1000, none from 1000 on
(`11.9..12.6V`, `2392..14639rpm`). A range wider than the 17 glyphs of
its box drops the decimal (`-999.9..-999.9mOhm` reads
`-1000..-1000mOhm`); a wider one is cut at the box's
edge. A row ends at its line feed (LF, 0x0A).
Text after the last line feed is a row cut off by a power loss or a full
card: it can hold every field and a number cut short, `95` of `9554`, so it
is left out of the rows, the figures and the plot. ROWS then shows in the
warning colour and the import view says `INCOMPLETE LAST ROW. SKIPPED.`. A
file from another program that ends without a line feed loses its last row
the same way. Spaces, tabs and carriage returns (CR) after the last line
feed are no row.

Runs recorded by the bench are written to two folders of the card, each run
under the name of what started it and a number from 001 to 999. The panel
makes each folder, one level at a time, the first time it writes there. The
table names the default folders; Auto log folder and User log folder on
[Setup](#setup) > SD CARD set others:

| Folder | Name | Run |
| --- | --- | --- |
| `BENCH/LOGS` | `SERVOnnn.CSV` | one switch to ARMED from SERVO |
| `BENCH/LOGS` | `MOTORnnn.CSV` | one switch to ARMED from MOTOR & ESC's ARM or a stick run on its PROGRAM |
| `BENCH/LOGS` | `SUPPLnnn.CSV` | one switch-on of the SUPPLY output while the bench is SAFE |
| `USER/LOGS` | `ATESTnnn.CSV` | one AUTOMATIC TEST; its report is `ATESTnnn.TXT` |
| `USER/LOGS` | `LIMITnnn.TXT` | one LIMIT RUN's report, with no CSV beside it |

The name follows the screen that armed the bench: an arm that asks for the
throttle outputs writes `MOTORnnn`, an arm that asks for the surfaces writes
`SERVOnnn`. An IOmcu older than protocol 4.18 arms the throttle outputs with
every arm; a switch to ARMED from SERVO still writes `SERVOnnn.CSV`.

Two switches on [Setup](#setup) > SD CARD decide what is written. Auto logs
OFF writes nothing to `BENCH/LOGS`; switched off during a run, it ends that
run's file. User logs OFF writes nothing to `USER/LOGS`; a test keeps the
state it started with. Both are ON by default. The fault log,
`BENCH/RCBENCH.LOG`, is written whatever the switches say.

A folder changed on [Setup](#setup) > SD CARD takes the next file: a run or
a test already open finishes in the folder it started in. The next one is
numbered from what the new folder holds. Both kinds can name one folder;
its numbers are then one series across all five names. The files left in a
folder used before are not listed: the USER and BENCH tabs list the folders
that are set. They stay on the card, and setting the folder back lists them
again.

Firmware 0.21.0 and older wrote every run to the card's root as
`BENCHnnn.CSV`, `ESCnnn.CSV`, `SERVOnnn.CSV` with its `SERVOnnn.TXT` (a
servo test) and `SERVOnnn.TXT` alone (a limit run). Those files stay where
they are and stay readable: nothing moves them, and the ROOT tab lists them.

The numbers are one series per folder, across every name in it: the next
run takes one above the highest number the folder holds, whichever name
carries it, so in `BENCH/LOGS` `SUPPL041.CSV` and `SERVO042.CSV` are
followed by `MOTOR043.CSV` or `SUPPL043.CSV`. A number carried by a CSV or a
TXT file is taken, and so is one carried by a folder's name. The panel walks
a folder once, the first time it writes there after a start, and remembers
the number after that; before it takes a number it checks the 10 names that
number can carry (5 names, CSV and TXT), so a file written in between is
stepped over and not overwritten. A gap left by a deleted file is not
filled. A folder holding a number 999 under any name takes no further run,
even with lower numbers free: the band reads
`Card full or not writable. Run not recorded.`, or for a test
`Card missing or full. Servo test not recorded.` A card in the slot that
does not mount reads `Card is neither FAT32 nor exFAT. Servo test not recorded.`
or `Cannot read the card. Servo test not recorded.` for a test. A folder that cannot be
made, because a file holds its name or the card is write-protected or full,
reads `Cannot make folder BENCH/LOGS. Run not recorded.` A failed run makes
the panel walk the folder again next time, so a file deleted in between
frees its number. The list shows the CSV files and the reports, and DELETE
on a run removes the report with the same name and number too. A report is
a `.TXT` file named like a run (`log_report_number()`), the rule FILES
labels a report by: `ATESTnnn.TXT` beside `ATESTnnn.CSV` or without it,
`LIMITnnn.TXT`, which a LIMIT RUN writes with no CSV, and in the root
`SERVOnnn.TXT`, of firmware 0.21.0 and older. A run and its report are two
rows, the CSV first: OPEN on the CSV reads it into the import, OPEN on the
report opens it in the [text viewer](#the-text-viewer). Other `.TXT` files are not listed. Runs are listed
newest first by number, across all names in the folder, a report by its own
number: `LIMIT044.TXT`, then `MOTOR043.CSV`, then `SERVO042.CSV`, then
`SERVO042.TXT`, then `SUPPL041.CSV`.

![A LIMIT RUN's report opened from LOGS in the text viewer](img/logs-report.png)

OPEN on a report opens it in the [text viewer](#the-text-viewer) on
FILES, read-only. The band's title reads FILES; the home tag returns to
LOGS, with the report chosen on the list again. A report over 1MiB stays
on the list, and the footer says
`Over 1MB: too large to open. Download it over Wi-Fi.`

![The ROOT tab with the runs of an older firmware](img/logs-root.png)

Three tabs at the header row's left choose the folder on the list, in this
order: ROOT (the card's root), USER (User log folder, `USER/LOGS` by
default) and BENCH (Auto log folder, `BENCH/LOGS` by default). A tab
tapped lists its folder from the first page. With no card the tabs switch
all the same: the tab tapped is drawn selected, the box reads
`No card. Insert one and tap RESCAN.` with that tab's line under it, and
the tap looks for a card again, as RESCAN does. A card in the slot that does
not mount reads `Card is neither FAT32 nor exFAT. Format it on a computer.` or
`Cannot read the card. Check it on a computer, then tap RESCAN.` instead
([The SD card's format](#the-sd-cards-format)). The header right of the tabs
shows the folder, `BENCH/LOGS`, and nothing on ROOT. The card's name,
`UNITEST`, ends the row, right-aligned: at the right edge of the list, and
left of PREV when PREV and NEXT show. An empty folder says why and what goes there:
`No .csv files in BENCH/LOGS.` and the names it takes, or with its switch OFF
`AUTO LOGS OFF in SETUP > SD CARD.`. Under the DELETE question the tabs are
hidden. A file opened from one tab stays that tab's file: its import view
and plot read it from that folder whichever tab is chosen since.

The list shows the folder a page of 48 entries at a time, and a folder holds
up to 999 runs. Pages run newest run first: runs and reports by
number, highest first, then the files the bench did not write, by name. The rows of a page run the same
way, the newest run at the top. The number in the name is what newest means:
a file's date is only as good as the panel's clock, which starts at each
power-on unset and is set by NTP (Network Time Protocol) on the home
network ([PANEL: Time zone and the clock](#panel-time-zone-and-the-clock)).
A file written before that is dated 1980.

![A full card of 999 runs and one other file on its fourth page](img/logs-pages.png)

With more than 48 files in the folder, PREV and NEXT show in the header row and
the tab names the page's part of the card, `FILES 145 TO 192 OF 1000`, instead
of `FILES`. PREV is grey on the first page and NEXT on the last. Pages are cut
from the top, 48 at a time: when fewer than 48 entries stand ahead of the page
on screen, for example after a run was recorded, PREV reads the first page
from the top. Runs recorded while a later page is open are on page 1. PREV
back from page 3 or later then passes over as many entries as were recorded;
NEXT from page 1 shows them, and the tab numbers show the shift. Each page is one
read of the tab's folder: the walk keeps the 48 entries of that page
and counts the rest, so the panel holds one page in memory whatever the card
holds. The read runs on the task that draws the screen and handles touch. For
999 runs the selection takes about 2ms on a desktop computer (x86-64,
measured); on the panel it is estimated at 20 to 40ms, and the directory read
at 63 sectors of 512 bytes, about 25ms at the card's 20MHz SPI (Serial
Peripheral Interface) clock, or 125 sectors, about 50ms, with a report beside
every run. Neither is measured on the panel. A folder with a report beside
every run lists up to 1,998 entries; the selection's time for those is not
measured. Each entry's size comes with the directory read, so a page costs
one walk and the selection.

The scroll bar along the list's right edge is 56px wide, 8.4mm at the 0.150
mm a pixel assumed for the panel (not measured). Its thumb is the rows on
screen against the rows on the page, and it scrolls within the page. A tap on
the track brings the thumb's middle to the finger; a finger on the thumb drags
it.

LOGS reads the card each time it opens, from the menu or back from WI-FI:
the first page of the tab's folder, its newest run selected, so a run
recorded while LOGS was closed is on the list without RESCAN. When the panel
has written a file since LOGS was last open, LOGS opens on that file's tab;
otherwise the tab stays as it was left, and BENCH after a restart. A file's import or plot view left open
stays open; the list behind it is the one just read.

RESCAN reads the page on screen again. On the first page it reads from the
top, so a run recorded since shows. On another page it reads from that page's
first entry, also when that file has been deleted since. When the folder holds
fewer entries than that place, RESCAN shows the last page. Every read selects
the page's newest run, the top row, so OPEN opens the run just recorded. A
page with no numbered run selects nothing. DELETE reads the page again the
same way and keeps the scroll, with nothing selected: DELETE and OPEN are grey
until a row is chosen. Deleting the last page's only file shows the page
before.

The header names the card by its volume label, read once when the card is
mounted. A card without a label shows the product name the card reports
(for example `SC16G`), and a card that reports neither shows `SD CARD`.

A card put in, taken out or swapped while the panel runs is found by the
next RESCAN, the next opening of LOGS, the next page and the next OPEN,
with no restart ([The SD card's format](#the-sd-cards-format)). The header,
the list and the empty list's words are then the card's in the slot: a new
card opens on its first page, an empty slot says `No card. Insert one and
tap RESCAN.`. OPEN and DELETE act on the card the page was read from only,
and a file left open in the import or plot view is read again from its own
card only. After a swap they are refused: a file reads `Cannot open
<name>.`, nothing is deleted, and the new card stays as it is. The viewer reads from the first page after every new mount,
also of a card with the same label as the one before: two cards with the
same label, or two unlabelled cards of one model, carry the same name in
the header and are still told apart.

In the plot, two fingers zoom: spreading them zooms in, pinching zooms out,
and moving both together pans. The view stays where the fingers leave it, and
a bar under the plot shows which part of the run is on screen. The narrowest
view is 8 samples, the widest the whole run. One finger and the `<` and `>`
buttons move the cursor as before; stepping the cursor past the edge of a
zoomed view carries the view with it. BACK returns to the import view.

The plot places the rows evenly by row number. The labels under it and the
cursor's read-out are the file's time column, counted from the file's first
row, and the header's duration is the last row's time less the first's. A
gap in the rows is a jump in those times, not a wider stretch of plot. For
a run with the bench in ARMED the time is wall time, so the duration is the run's;
for a file written by firmware 0.15.0 or older it is shorter than the run
(see Motor & ESC, the CSV file).

SETUP → PANEL → One finger pans, off by default, lets a single finger
pan a zoomed view. While it is on and the view is zoomed in, a finger that
moves more than 10px from where it touched down drags the view sideways, and
the sample under the finger stays under it. A touch that stays within 10px
selects a value as with the setting off, and a pan leaves the cursor where it
was. A view that shows the whole run is not panned: one finger moves the
cursor. A second finger on the plot starts a zoom, as before.

A button or a row on LOGS takes a tap. It acts when the finger that pressed
it lifts inside it, having been no more than 8px from the press in x and in
y. A vertical drag of more than 8px on the file list or on the column list
scrolls it and selects nothing. On those two views one finger at a time
presses; a second finger does nothing.

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

A run is committed to the card every 20 rows or 1000ms of run, whichever
comes first. Power lost mid-run costs the rows the commit has not finished
with and the rows still in the queue between the control task and the card's
own task: under 1.0s of run while the card keeps up, and 84 rows, 4.20s at
the panel's 20Hz sample rate, if the card has stalled and the queue is full.
The rest of the file is readable either way; a row the cut stopped part way
through has no line feed and is left out on reading (see [Logs](#logs)). The card is written by a task of its own: an SD (Secure
Digital) card is allowed 250ms to finish a write, and the task that beats the
safety line has a ceiling of 150ms.

Nine messages say what a card did to a run. Each appears on the band:

| Message | What happened |
|---|---|
| `No card. This run is not being recorded.` | nothing is mounted, so the run was never opened |
| `Card is neither FAT32 nor exFAT. Run not recorded.` | the card in the slot holds no FAT volume, so nothing is mounted ([The SD card's format](#the-sd-cards-format)) |
| `Cannot read the card. Run not recorded.` | the card would not list, so a run number could not be chosen, or the card in the slot does not mount |
| `Card full or not writable. Run not recorded.` | no run number could be created |
| `Card too slow. Run not recorded.` | every row was dropped, so there is no file for this run at all |
| `The card was too slow. Some data is missing from the recording.` | some rows were dropped; the file's time column shows where |
| `Data can no longer be saved to the card. Recording stopped here.` | a write failed mid-run; every later row is rejected. A card taken out during the run is found at this failure: the file is closed at once, the run goes on unrecorded, and the run's close says nothing more |
| `Data could no longer be saved to the card. Recording incomplete.` | the same failure, said again when the run closes |
| `The last write to the card failed. Recording incomplete.` | the close failed, so the rows since the last commit are not in the file |

The first five mean there is no file to look for. The last four mean there is
one and it stops early.

A run being written is not offered in LOGS. A file's length lives in its
directory entry and is written when the file is closed, so a run still open
would list at its last committed length and read as a finished one.

### The SD card's format

The panel mounts a card formatted FAT12, FAT16, FAT32 or exFAT (extended
File Allocation Table). The volume is either the whole card or the first of
the 4 primary partitions of an MBR (master boot record) that holds one of
these formats. Long file names of up to 255 characters are read and
written.

| Card | Size | Format out of the box | On the panel |
| --- | --- | --- | --- |
| SDSC (SD Standard Capacity) | up to 2GB | FAT12 or FAT16 | mounts |
| SDHC (SD High Capacity) | 4GB to 32GB | FAT32 | mounts |
| SDXC (SD eXtended Capacity) | 64GB to 2 TB | exFAT | mounts |
| SDUC (SD Ultra Capacity) | over 2 TB | exFAT | does not mount |

Not supported:

- a card partitioned GPT (GUID Partition Table; GUID: globally unique
  identifier) instead of MBR;
- a volume over 2 TiB;
- NTFS (New Technology File System), ext4, HFS+ and APFS.

A card in one of these does not mount. The panel never formats a card.

To format a card for the bench, use the SD Association's SD Memory Card
Formatter: it writes FAT32 up to 32GB and exFAT from 64GB, on an MBR. On
Windows, the format dialog's FAT32 or exFAT does the same. On macOS, Disk
Utility's Erase with Scheme `Master Boot Record` and Format `MS-DOS (FAT)`
or `ExFAT`. On Linux, for the card's first partition at `/dev/sdX1` on a
`dos` partition table: `sudo mkfs.vfat -F 32 /dev/sdX1` or
`sudo mkfs.exfat /dev/sdX1`.

Formatting erases the card. The panel looks for a card at start-up, at
RESCAN, when LOGS opens, at a page or a tab of LOGS, at OPEN, at a run's
start and a servo test's start, and on the servo programmer's taps listed
in [Servo programmer](ServoProgrammer.md). It does not look once a frame.
A look at a mounted card is one SEND_STATUS (CMD13) command. A card that
does not answer it has been taken out or swapped: the panel lets its volume
go and mounts what the slot holds. A file open on that card, such as
the run being written or a Wi-Fi download, is closed first; until the last
one is, the slot reads as no card. A read that fails is followed by the same
command: a card that answers it and fails its reads reads as unreadable,
and the panel mounts it once per look, not in a loop. What the slot holds is said in each place that needs a card:

| Slot | [Splash](#splash), SD card line | LOGS | Run on the band |
|---|---|---|---|
| empty | `no card` | `No card. Insert one and tap RESCAN.` | `No card. This run is not being recorded.` |
| a card with no FAT or exFAT volume: NTFS, GPT or not formatted | `not FAT32/exFAT` | `Card is neither FAT32 nor exFAT. Format it on a computer.` | `Card is neither FAT32 nor exFAT. Run not recorded.` |
| a card that does not mount for another reason, or answers and fails a read: a read error, a card that does not finish its start-up | `unreadable` | `Cannot read the card. Check it on a computer, then tap RESCAN.` | `Cannot read the card. Run not recorded.` |

The panel's console line says which with the code behind it:
`I storage: SK64G mounted, exFAT, 61002 MB` with the format of a card that
mounts, `I storage: no card (ESP_ERR_TIMEOUT)`,
`W storage: card holds no FAT volume (FatFs 13): format it FAT32 or exFAT`,
or `W storage: mount failed: FatFs <n>` and `W storage: mount failed:
<ESP-IDF error name>`. A card taken out or swapped is
`I storage: card gone (check: FatFs 3)`, with no E line from ESP-IDF, and
the next line says what the slot holds. A card that answers and fails a
read is `W storage: card answers and does not read (walk: FatFs 1)`. A card taken out during a run is found by the write that fails, and
ESP-IDF logs that write's E lines (`sdmmc_cmd`, `diskio_sdmmc`) before the
`I storage: card gone` line. A card that starts and does not mount is started a
second time to ask FatFs why. That look adds one card start-up to each
failed try, its time not measured, and nothing to a card that mounts.

A file's size is read 64-bit, so a file of 4GiB and more on an exFAT card
lists with its size, `8.00GB`. OPEN reads files up to 2GiB less 1 byte:
a larger one, also a FAT32 file of 2GiB to 4GiB less 1 byte, stays on the
list and OPEN says `BENCH001.CSV is 2GB or larger. Not opened.` The sizes
on the list are binary: 1kB is 1,024 bytes, 1MB 1,024kB and 1GB
1,024MB. [WI-FI](#wi-fi) lists and downloads a file of any size the card
holds.

## Files

FILES shows the SD (Secure Digital) card in two panes and copies, moves,
renames and deletes its files, makes folders and empty text files, and
shows a file as text in its [text viewer](#the-text-viewer). It
opens from FILES in the bottom row of [Logs](#logs) and from the FILES tile
of [Setup](#setup). The home tag closes a box or the keyboard
first, then leads back to the screen that opened FILES. FILES drives no
output.

![FILES](img/files.png)

Each pane is one folder of the card, 500px wide, at x 8 and x 516. The
left pane opens on the card's root, the right one on the User log folder
(`USER/LOGS` by default). The pane with the accent heading is the active
one: OPEN, RENAME, DELETE, NEW and INFO act in it, and COPY and MOVE take
its selected entry to the other pane's folder. A tap on a row selects it
and makes its pane the active one; a second tap on the selected row is
OPEN.

A pane's header row, body y 4 to 52, holds the folder's path on a button
312px wide that goes up one folder; the root shows the card's volume
label, or `SD CARD`, and goes nowhere. PREV and NEXT, 88 x 48px, stand at
the pane's right end while the folder holds more than 48 entries:

![The second page of a folder of 120 entries](img/files-paged.png)

The heading over the rows counts the entries: `6 ENTRIES`, or
`ENTRIES 49 TO 96 OF 120` on a folder of more than one page. Entries the
panel does not list are counted after it, `+ 1 NOT LISTED`: names that start
with a full stop (`.DS_Store`, `._RCBENCH.LOG`) and names over 63 bytes. A
folder that holds only such entries is not empty, and DELETE refuses it.

The list shows 6 rows at a pitch of 48px, drawn 44px tall; a drag on the
list or on the 56px scroll bar moves it. Folders come first, then files,
each group in name order. A row shows an icon, the name, the size
right-aligned (`512B`, `14kB`, `7.0MB`, `8.00GB`; binary units, 1kB is
1,024 bytes) and, under the name, the kind and a state word:

| Kind | Entry |
|---|---|
| FOLDER | a folder |
| LOG | a CSV file in the Auto or the User log folder |
| REPORT | a `.TXT` report in either log folder |
| SERVO FILE | a file in `BENCH/KST/CONF` |
| ESC PROFILE | a `.JSON` file in `BENCH/ESC/CONF` |
| FAULT LOG | `BENCH/RCBENCH.LOG` and its older and temporary copies |
| CSV, JSON, TEXT | a `.CSV`, `.JSON` or `.TXT` file elsewhere |
| FILE | any other file |

| State | Meaning |
|---|---|
| FIXED | a folder the bench keeps: the two log folders, `BENCH/ESC/CONF`, `BENCH/KST/CONF`, `BENCH/HITEC/CONF` and the folders on the way to them, `BENCH` and `USER` among them. No RENAME, MOVE or DELETE |
| LOCKED | the servo programmers' records, `BENCH/KST/CONF/KST.TXT` and `BENCH/HITEC/CONF/HITEC.TXT`, and their `KST.NEW` and `HITEC.NEW` while they are written. INFO only |
| WRITING | a file the logger has open. INFO only |

The fault log takes COPY and DELETE only while the IOmcu link is up: with
the link down the panel writes to it every 5s.

The footer, body y 440 to 492, holds RESCAN, 140 x 52px, at the left, as
on [Logs](#logs), then COPY, MOVE, RENAME, DELETE, NEW, INFO and OPEN,
118 x 52px, OPEN at the right margin; the buttons stand 6px apart, and the
footer ends 4px above the alert band. Each label is in the 10 x 20 face
with at least 4px either side; the German AKTUALISIEREN is 130px wide and
VERSCHIEBEN 110px. A button that would be refused is
drawn dimmed; a tap on it says why in the message line, two lines of the
12 x 24 face over the footer.

| Button | What it does |
|---|---|
| RESCAN | looks at the slot and reads each pane again at the page it shows; see [RESCAN](#rescan) |
| COPY | copies the selected file into the other pane's folder. Folders are not copied |
| MOVE | moves the selected file or folder into the other pane's folder |
| RENAME | opens the keyboard with the entry's name |
| DELETE | asks first. A run's CSV takes its report along; a folder is deleted only when empty |
| NEW | FOLDER or TEXT FILE, then the keyboard for its name; a text file is made empty |
| INFO | the entry's name, folder, kind, size or entries, time and rule |
| OPEN | enters a folder; a CSV file in one of LOGS' three folders (ROOT, USER, BENCH) that LOGS lists opens on [Logs](#logs) on that folder's tab, in the import; a CSV file over 2GiB less 1 byte shows LOGS' list with the reason, and a card swapped since FILES listed it opens nothing and says `The card was taken out or swapped. Tap RESCAN.` Every other file, a report among them, opens in the [text viewer](#the-text-viewer). A file over 1MiB is not opened: OPEN is grey and says `Over 1MB: too large to open. Download it over Wi-Fi.` A file the logger still writes is not opened, OPEN is grey and says `ATEST012.TXT is being written.`; the servo programmers' records take INFO only |

An existing name is never overwritten: COPY, MOVE, RENAME and NEW refuse
it with `NOTES.TXT exists there already.` A copy is written under the
name with `.NEW` added and renamed when it is complete, so a copy that
stops leaves no part file under the real name. After each operation the
logger walks its folders again before its next file, so the next run
takes a number above a log copied or renamed into a log folder. COPY and MOVE into the
folder the entry is in, or a folder into itself, are refused:
`Both panes show this folder, or the target is inside it.`

### The text viewer

![A servo test's report in the text viewer](img/files-view.png)

The text viewer shows a file of the card as text, read-only. OPEN on a
file in FILES opens it, and OPEN on a report in [Logs](#logs). The home
tag closes it: onto the panes when FILES opened it, back to LOGS when LOGS
did. Leaving FILES closes it too, unless the [editor](#the-text-editor)
holds unsaved text. It opens a file of up to 1MiB
(1,048,576 bytes) and up to 65,536 line ends; the card task reads it
whole, in chunks of 16KiB, while the screen shows `Reading the card.`

The header row, body y 4 to 52, shows the file's folder and name (the
front cut to `..` past 44 cells), its size, READ-ONLY, and at the right
`LINE 23 OF 57`: the line on the top row and the file's lines. A line end
at the file's end starts no line of its own.

The text stands in a panel of 1008 x 372px from body y 60: 18 rows of 92
cells of the 10 x 20 face, 20px apart. EDIT, the footer's second button,
opens the [editor](#the-text-editor). SMALL, at the footer's left,
switches to the 8 x 16 face: 22 rows of 115 cells, 16px apart, for a
report's step table; it then reads NORMAL. The choice holds for every file
until the panel restarts.

![The same report in SMALL, on its second screen](img/files-view-small.png)

A line wider than a row continues on the next row, marked `..` at the
row's left. A tab reaches the next multiple of 8 cells; at a row's end it
ends there. A byte that starts no UTF-8 sequence, a control character and
a NUL byte after the first 4KiB draw as `?`, one cell each; a character
the faces hold no glyph for draws as `?` too. A leading UTF-8 byte order
mark is not drawn. A line ends at LF, at CR LF and at a lone CR; a file
with more than one kind shows `MIXED LINE ENDS` in the footer.

A drag on the text moves it one row per 20px (16px with SMALL), from where
it stood at the press; a press that moves 8px or less is no drag. A drag
on the 56px scroll bar puts the thumb under the finger, the bar's travel
spread over the file's lines and the rows of each wrapped line; the
travel's ends are the first and the last screen, as PAGE UP and PAGE DOWN
reach them, also when the last screen starts inside a wrapped line. A tap
on the bar beside the thumb moves the thumb there. PAGE UP and PAGE DOWN, 140 x 52px at the footer's right, move
a screen of rows, 18 or 22; each is grey at the text's first or last
screen, and both are grey when the whole text shows.

The colours follow the bench's words, by the file's name: a `.TXT` file
by the rules of a report, a `.JSON` file as JSON, a `.CSV` file as CSV,
any other file as plain text.

| Class | In a `.TXT` file | Colour |
|---|---|---|
| heading | line 1, and a line in capitals such as `SETTINGS IN FORCE` | accent |
| label | `Result:` or `Neutral =` from the line's start, within 24 cells | dim |
| OK | the report's PASS, in every language | OK green |
| FAIL | the report's FAIL, in every language | danger red |
| WARN | ABORTED, NOT MEASURABLE, not measured, not checked, not run, cut short, `--`, `[not loaded]` | warning |
| number with a unit | `6.00V`, `0.210A`, `400ms`, `333Hz` | the unit's colour: V and mV voltage, A and mA current, W power, rpm and Hz speed, others teal |
| comment | `#` to the line's end | faint |
| registers | `00: 1A 2B ...`, `checksum 9F3A77C1`, `journal=...` | dim |

The words are the report writers' own texts, in English and in German, so
a report written in German reads in its colours on a panel set to English.

![A servo file of the KST programmer](img/files-view-kst.png)
![An ESC profile](img/files-view-json.png)

A file the viewer does not show leaves the reason in the panel:
`Not a text file: a NUL byte in its first 4kB.`,
`Too many lines: 65536 at most.`, a file over 1MiB since it was listed,
and the card's own answers, such as
`The card was taken out or swapped. Tap RESCAN.` An empty file shows
`The file is empty.`

The viewer holds the file in one buffer of 1MiB and its line index of
256KiB, both in PSRAM (pseudo-static random-access memory). The time a
read of 1MiB takes on the panel's card is not measured.

### The text editor

![A line typed into a report, the keyboard open](img/files-edit.png)

EDIT in the viewer switches it to the editor and opens the keyboard. The
editor changes the text in the panel's memory; SAVE writes it to the
card. The panel then holds 8 rows of the text (10 with SMALL) in a panel
of 1008 x 170px from body y 60, and the keyboard from body y 236 to the
alert band: 4 rows of 10 keys, 93 x 56px each, 6px apart, with no digit
row; the letters start at the top row. Four layers share the 3 character
rows: lower case, capitals (`ABC`), and two pages of marks. `123`, left of
DEL, shows the first page: the digits 0 to 9 on the top row and the 17
marks `- / : ; ( ) = # " _ . , ? ! ' [ ]`. On that page `#+=`, left of Z,
shows the second page with the other 15: `$ % & * + < > @ \ ^` and
`` ` { | } ~ ``. `abc` goes back to the letters from either page. A digit
is 2 taps: `123`, then the digit. DEL takes the character before the
cursor. The bottom row
is ENTER (2 keys wide), SPACE (4), `<-` and `->` (1 each) and DONE (2).
A key acts on the release of the contact that pressed it, over the same
key; a second contact is ignored while one is down. The keyboard types
ASCII (0x20 to 0x7E); the letters Ä Ö Ü ä ö ü ß are drawn but not typed.

The cursor is a bar 2px wide in the accent colour before the cell it
types into. A tap on the text puts it on the nearest cell of the tapped
row; past a line's end it stands at that end, under the last line at the
text's end. `<-` and `->` move it one character, a line end of CR LF
counting as one. After every key the text moves the least that keeps the
cursor's row on the screen. The header shows EDITING while the text is as
on the card, MODIFIED in the warning colour once it differs, and
`LINE 3 OF 59` for the cursor's line.

ENTER types the file's own line end: CR LF in a file whose first line
ends with CR LF, LF otherwise and in an empty file. The editor holds the
limits of the viewer: a key that would make the text larger than 1MiB
types nothing and the header says `Text full: 1MB at most.`; ENTER at
65,536 line ends types nothing and says `Text full: 65536 lines at most.`

DONE, or the home tag, closes the keyboard; the footer then holds SAVE,
REVERT and KEYBOARD at the left, PAGE UP and PAGE DOWN at the right, and a
note of 3 rows of the 8 x 16 face between them. KEYBOARD opens the
keyboard again.

SETUP → PANEL → Keyboard see-through lets the text show through the
editor's keyboard: OFF (the default), 25% or 50%. Above OFF the panel of
the text reaches to body y 432 under the keyboard, as in the viewer, and
rows 9 to 18 (11 to 22 with SMALL) show through it. Each pixel of a key
and of the gaps between keys is then 25% or 50% the text behind and the
rest the key's or the panel's colour; the labels, each key's top hairline
and the keyboard's outline stay opaque. The 8 rows over the keyboard are
still the ones the cursor is kept on, a tap places it on and PAGE UP and
PAGE DOWN move. The setting reaches only the editor's keyboard: the name,
folder, search and Wi-Fi keyboards carry their own text field and draw
opaque. Steps above 50% are not offered: the keys' edges then fade into
the text behind them.

![A KST file saved with a comment: its checksum line is gone](img/files-edit-kst.png)

SAVE writes the text as `<name>.NEW` in the file's folder, in chunks of
16KiB on the card task, deletes `<name>` and gives the new file its name;
the note then says `<name> saved.` From the tap on SAVE until the save
ends the text does not change: a key types nothing, REVERT and the home
tag wait, and the question of an open over the text shows DISCARD grey.
A save that writes the text closes that question and opens the other
file; a save that fails leaves the question with DISCARD live. SAVE is
grey while the text is as on the card, and a tap on it then says
`No changes to save.` SAVE is shut while the bench is ARMED, while a run
is being logged and while the logger writes the file: the note says
`Bench ARMED. The card is read-only.` or the reason in its own words, and
the text stays as typed. A switch to ARMED or a run that starts during a
save stops it before its next chunk; its `.NEW` is removed, the file on
the card stays as it was, and the note says
`Stopped: the bench switched to ARMED.` or
`Stopped: a run is being logged.` A card taken out during a save leaves
the text unsaved with `Not saved: the card was taken out or swapped.`;
taken out between the delete and the rename, the card holds the whole
text as `<name>.NEW` and no `<name>`, and RENAME in FILES gives it its
name back.

A file opens only without a NUL byte in its first 4kB. SAVE holds the
text to the same test: an edit that brings a NUL from further in into the
first 4kB is not saved, the note says
`Not saved: a NUL byte in the first 4kB. FILES would not open the file again.`,
and the text stays as typed. REVERT puts back such a file as loaded,
without a second test. A file over the editor's 1MiB or 65,536 line ends
leaves the text as it is, with the note of a key refused at that limit;
a file FILES opened is within both.

A KST file, one whose first line is `KST servo configuration, format`
and a number, holds a checksum line when the servo programmer wrote it.
SAVE of a changed KST file takes out every line the programmer's reader
takes for a checksum line: its first word, after spaces and tabs, is
`checksum` in any case, ended by a space, a tab, `=`, `:` or the line's
end. The note says
`Saved without the checksum line: the servo programmer reads it as
edited.` The programmer then reads it as a file a person edited: from its
value lines, with no checksum to match ([Programmer](#programmer)). A KST
file saved unchanged
keeps its checksum. A file in `BENCH/ESC/CONF` ending in `.json` is read
when the panel starts; the note after its save says
`Saved. The panel reads it at its next start.`

![The question over unsaved changes](img/files-edit-unsaved.png)

REVERT, and the home tag with the keyboard closed while the text is
unsaved, ask first: UNSAVED CHANGES over STAY and DISCARD, 300 x 56px
each. STAY, or the home tag over the question, keeps the text. DISCARD
after REVERT puts the text back to the file as loaded or last saved, the
editor still on; DISCARD after the home tag does the same and shows the
viewer, and the next home tag leaves it as the viewer's does, to the
panes or back to LOGS. With the text as on the card the home tag shows
the viewer without a question. An OPEN from LOGS or from FILES while the
text is unsaved shows the question too; DISCARD then opens the other
file, STAY keeps the text and drops the open.

STOP stops the bench and leaves the editor and its text as they are;
SAVE is then shut while the bench is ARMED. A route that leaves FILES
without the home tag keeps an editor with unsaved text, and FILES shows
it again on the way back in. There is no undo of single keys: DISCARD
puts back the whole file. The editor keeps the file as loaded in a second
buffer of 1MiB in PSRAM for DISCARD. The servo programmer's journal
`BENCH/KST/CONF/KST.TXT` does not open, and the fault log
`BENCH/RCBENCH.LOG` opens with EDIT grey; a tap on it says why. The
typing rate the panel keeps up with is not measured.

### RESCAN

RESCAN looks at the slot, then reads each pane's folder again, one pane
after the other, at the page the pane shows. The first page is read from
the folder's first entry, so an entry new on the card that ranks before
every row, such as an `ATEST001.CSV` beside `SERVO` logs, comes onto it.
A later page is read from its own first entry, as PREV and NEXT name their
pages; an entry new before it is on a page before it. The way back into
FILES reads both panes the same way. Each pane keeps its place:

- a list that shows its folder's first entry on top shows the folder's
  first entry on top again, the new one included;
- otherwise the entry on the pane's top row stays on top; when it has gone
  from the card, the entry at its place in the order is on top;
- the selected entry stays selected while it is on the page read. When it
  has gone from the card, or stands on another page, nothing is
  selected, as after a DELETE: MOVE and RENAME act on the selection without
  a question, so no other entry takes it. A tap selects again;
- the page's first entry gone from the card still names its place, and the
  page starts at the next entry. A page with nothing left from that place
  on is the folder's last page;
- a page that holds fewer rows than before shows its last 6 rows, with no
  empty row under them;
- a folder removed from the card on a computer shows
  `This folder is not on the card.` with no rows, and UP leads out of it;
- with no card both panes read `No card. Insert one and tap RESCAN.`; a
  card put in after that is a new card, and both panes start again in the
  root and the User log folder.

New entries are listed in their place in the order. RESCAN during a
listing, INFO or a job waits until that request is answered: the card task
takes one request at a time.

### The write gate

FILES writes only while the bench is SAFE and no log file is open. While
the bench is ARMED or a run is being logged, every button but INFO and
RESCAN is dimmed and the message line reads
`Bench ARMED. The card is read-only.` or
`A run is being logged. The card is read-only.` Browsing and INFO work in
every state.

![FILES while the bench is ARMED](img/files-armed.png)
![FILES while a test is logged](img/files-logging.png)

A copy that runs when the bench switches to ARMED or a run starts logging
stops before its next chunk of 16KiB and says
`Stopped: the bench switched to ARMED.` or
`Stopped: a run is being logged.` The part copy is removed.

### The boxes

![The DELETE question](img/files-delete.png)

The DELETE question is a box of 700 x 280px with CANCEL and DELETE of
300 x 56px. It names the entry and its size; for a run's CSV it names the
report of the same number that goes with it when the pane's page lists
that report. A report that would stand on the next or the previous page
of a folder of more than 48 entries is named too: the page cannot tell,
and DELETE takes the report along whenever the card holds it.

![A copy under way](img/files-copy.png)

A copy, a move, a rename, a delete or a new entry that takes longer than
0.25s shows its progress in the same box. A copy shows a bar and the bytes
done of the bytes to copy, and CANCEL stops it before its next chunk:
`Stopped by CANCEL.`

![INFO on a file](img/files-info.png)
![INFO on a folder](img/files-info-folder.png)

INFO is a box of 800 x 420px with CLOSE of 300 x 56px. A file shows its
name, folder, kind, size, time and rule; a folder shows its entries, the
bytes of the files directly in it, the entries not listed, its time and its
rule. The time is the modification time the card holds, in the zone of
SETUP > PANEL's Time zone, `2024-05-17 12:34`. A stamp in 1980 reads
`not known`: FAT (File Allocation Table) writes 1980 for a clock that was
not set, and the panel names no date it does not have
([PANEL: Time zone and the clock](#panel-time-zone-and-the-clock)).

![NEW's choice](img/files-new.png)

NEW opens a list of two rows of 56px above NEW: FOLDER and TEXT FILE. A
press beside the list closes it.

### The name keyboard

![The keyboard for a new text file](img/files-name.png)

RENAME and NEW type the name on the keyboard of [Wi-Fi](#wi-fi), across the
body. It takes up to 59 bytes; Ä Ö Ü ä ö ü ß take 2 each, and DEL removes a
whole character. OK refuses a name the card cannot hold and says why right
of the title: an empty name (`Enter a name.`), one of `" * / : < > ? \ |`,
a space or a full stop at either end, or a device name such as `CON`,
`NUL`, `COM1` alone or before a full stop. CANCEL closes the keyboard and
changes nothing.

### No card

![FILES with no card](img/files-nocard.png)

With no card, both panes read `No card. Insert one and tap RESCAN.` and
every button but RESCAN is dimmed. A card taken out or swapped is found by
the next request or by RESCAN: both panes go back to the root and the User
log folder of the card in the slot.

### The card task

FILES makes no card call itself. Every listing, INFO and operation is a
request to the task `cardjob` (priority 1, core 0, a stack of 6656B and a
copy buffer of 16KiB of internal RAM), started at FILES' first request. The
screen keeps drawing while the card works. After each copy the panel logs
its rate on the console,
`files: copy of <bytes>B in <time>ms: <rate>kB/s, done; cardjob stack <bytes>B unused`,
`not done` for a copy that stopped. The rate on a card is not measured.

## Wi-Fi

![WI-FI, no home network kept](img/wifi.png)

WI-FI runs the panel's Wi-Fi for two jobs in a browser: every file on the
card to download, and a panel firmware image to upload. It opens from the
WI-FI tile on [SETUP](#setup) and from WI-FI in the bottom row of
[LOGS](#logs). Its home tag returns to the screen that opened it, SETUP or
LOGS; so does CLOSE on PANEL UPDATE after an upload's install failed. Over
the network list or a keyboard the home tag climbs one level first, as
CANCEL does: from a keyboard to the list, keeping nothing typed, and from the
list to the buttons. Over the question about an uploaded image the home tag
is CANCEL: the image is dropped, a list or keyboard under the question
closes with it, and the screen stays. A second tap while the image is still
there leaves the screen. The
radio runs only while
this screen is open, and for the release check and the download of
[Updates](#updates). It is off at every start of the panel, and it goes off
when the screen is left, before an install writes the flash, and after
10min without a request.

Every view of WI-FI ends 4px above the alert band, at body y 492, so an
alert covers no control. Rows, buttons and keys are 48px or taller.

| View | Layout |
| --- | --- |
| Radio off | five buttons of 460 x 60px (69.0 x 9.0mm), 8px apart, on the left; on the right a card of 524px with lines of 49 characters in 10 x 20, 24px apart |
| Radio on | rows of 56px: the label in 10 x 20 in 240px, the value in 20 x 36 in 740px, where a network name of 32 characters takes 640px; the lines under the rows in 12 x 24, 28px apart; WI-FI OFF 460 x 60px from body y 424 |
| Network list | two columns of eight rows of 492 x 48px, 52px apart: the name in 12 x 24 (32 characters are 384px), the signal in 10 x 20; SCAN AGAIN, HIDDEN NETWORK and CANCEL 328 x 56px each |
| Question about an image | a card of 1008 x 484px: the title and the versions in 20 x 36, the sentences in 12 x 24, wrapped at 80 characters; CANCEL and HOLD 2s TO INSTALL 460 x 72px |
| Keyboard | 1008 x 484px, keys of 93 x 71px ([Keyboard](#keypad-keyboard-and-the-shared-controls)) |

The radio starts only with the bench SAFE, the supply output off and no log
run open. Otherwise the buttons that start it are grey and the card says what
is missing:

![The radio may not start](img/wifi-blocked.png)

It goes off by itself on a switch to ARMED, on STOP, on the supply output
coming on, on a log run opening, after 10min without a request and after 16
wrong access codes. The card then says which, in one line:
`Wi-Fi switched off: bench ARMED.`, `Wi-Fi switched off: STOP.`,
`Wi-Fi switched off: supply output on.`, `Wi-Fi switched off: a recording session started.`,
`Wi-Fi switched off after 10min without a request.`,
`Wi-Fi switched off after 16 wrong access codes.`, or `Wi-Fi could not start.` A STOP
or a switch to ARMED also drops an uploaded image that waits on the panel.
While the radio is up, the link to the IOmcu and the heartbeat run as
on every other screen.

When Wi-Fi could not be set up at start (no memory for its lock or its
buffers), every button is grey and the card says `Wi-Fi setup failed at startup. It is unavailable. Restart to try again.` The rest of the panel runs
as without Wi-Fi.

### ACCESS POINT

ACCESS POINT starts the panel's own network. The screen shows what a phone
or a computer needs:

![The access point is up](img/wifi-ap.png)

| | |
| --- | --- |
| Band and channel | 2.4GHz, channel 6 (`WIFI_ACCESS_AP_CHANNEL`) |
| Security | WPA2 (Wi-Fi Protected Access 2) personal |
| Network name | `rcbench-` and 4 hex digits of the panel's MAC (media access control) address, `rcbench-3F7A` |
| Password | 8 characters, made new at every start, from `abcdefghjkmnpqrstuvwxyz23456789` (no 0, 1, i, l or o) |
| Clients | 1 at a time |
| IP address | `http://192.168.4.1/` |
| Range | not measured |

1. SETUP → WI-FI → ACCESS POINT, or LOGS → WI-FI → ACCESS POINT.
2. On the phone or the computer, join the network the screen names, with
   the password it shows.
3. Open `http://192.168.4.1/` and enter the access code the screen shows.

### Home network

The panel can join a network that is already there instead: WPA2 or WPA3
(Wi-Fi Protected Access 3) personal. Enterprise networks and open networks
are not offered.

CHOOSE HOME NETWORK scans for 10s at most, then lists up to 16 networks,
strongest first, with their signal in dBm. A network without a name, an open
one and an enterprise one are left off the list.

![Scanning](img/wifi-scanning.png)
![The networks found](img/wifi-scan.png)

A tap on a network opens the keyboard for its password. HIDDEN NETWORK
opens it for the name of a network that does not announce itself, and then
for the password. SCAN AGAIN scans again; CANCEL goes back.

`No WPA2 or WPA3 network found.` means a scan completed and found no
network the list keeps. A scan the radio reports failed runs again, 3 scans
at most (`WA_SCAN_TRIES`) within the same 10s. A scan that fails each time,
that the radio refuses or that does not end within the 10s leaves
`Wi-Fi could not start.` in the list's place; a STOP, a switch to ARMED, the
supply output coming on or a log run opening leaves its own line there.
SCAN AGAIN tries again. CHOOSE HOME NETWORK is on the buttons only: with the
access point up or the panel joined, the screen offers WI-FI OFF first.

![The network's name](img/wifi-key-ssid.png)
![Lower case](img/wifi-key-lower.png)
![ABC: upper case](img/wifi-key-upper.png)
![#+=: marks](img/wifi-key-marks.png)

The keyboard has three layers of the same grid: lower case, `ABC` for upper
case, `#+=` for the 32 printable marks of ASCII (American Standard Code for
Information Interchange). Each of the 95 printable ASCII characters is on
one layer once; SPACE is the wide key. The keyboard covers the body above
the alert band, 1008 x 484px, with keys of 93 x 71px
([Keypad, keyboard and the shared controls](#keypad-keyboard-and-the-shared-controls)).
A name takes 1 to 32 bytes; OK on an empty name keeps the keyboard open
and says `Enter a network name.` A
password takes 8 to 63 characters, the WPA2 and WPA3 personal range: the
64th is refused, and OK under 8 says so.

![The password, hidden](img/wifi-key-pass.png)
![SHOW](img/wifi-key-shown.png)
![Fewer than 8 characters](img/wifi-key-short.png)

The password shows as `*` until SHOW; HIDE hides it again. OK keeps the
network: its name and its password go to NVS (non-volatile storage), and the
screen then offers JOIN HOME NETWORK.

![A home network is kept](img/wifi-home.png)

![Joining](img/wifi-joining.png)
![Joined](img/wifi-joined.png)

Joined, the screen shows the network, the panel's address and its name,
`http://rcbench-3F7A.local/`, with the same 4 hex digits as the access
point's name. The name answers through mDNS (multicast domain name system)
on computers that resolve `.local` names; which phones do is not measured.
The address works on every one. A name responder that fails to start (no
task, no socket, no multicast group) is tried again 3 times, 5s apart
(`WA_MDNS_RETRIES`, `WA_MDNS_RETRY_MS`); after that the name stays silent
until the next join, and the address still works.

A join that fails says why, and offers ACCESS POINT, which works without
the home network:

| Line on the card | Condition |
| --- | --- |
| `Could not connect. Password rejected.` | 3 auth-class drops in a row within 20s (`WA_AUTH_TRIES`): the key refused, a MIC (message integrity code) failure, or the key handshake out of time; or no association within 20s (`WA_JOIN_MS`) with an auth-class drop as the last drop |
| `Could not connect. Network not found.` | no association within 20s (`WA_JOIN_MS`), and the last drop of another kind, or none |
| `Connection failed. No IP address within 15s.` | associated, and no address from DHCP (dynamic host configuration protocol) within 15s (`WA_ADDRESS_MS`) |
| `Connection to the home network lost.` | joined, then the network dropped the panel |

A drop while joining connects again. One auth-class drop does not end the
join: ESP-IDF reports a wrong password and a key handshake answered late
with the same reasons, 15 and 204, and a correct password is answered late
when the panel or the router is busy. Any other drop sets the count back
to 0. Each drop goes to the serial log as one line with its reason number
(ESP-IDF's `wifi_err_reason_t`).

FORGET HOME NETWORK erases the name and the password from NVS and sets the
access code back to on.

A change of what is kept (a network, FORGET, the access code switch) takes
effect at once and reaches NVS on the next frame that may write flash. A
write that fails is tried again 3 times, 10s apart (`WA_PENDING_RETRIES`,
`WA_PENDING_RETRY_MS`), and the card says `Not saved. Writing to flash failed. Saving will be tried again with the next change.` until one succeeds. The change holds
until the panel restarts either way; only a write that succeeded outlasts
the restart.

Current limitations:

- NVS is not encrypted. The home network's password is kept in plain form in
  the panel's flash, where anyone who reads the flash over USB (Universal
  Serial Bus) reads it.
- The page is plain HTTP (hypertext transfer protocol). On a home network
  the access code and every file cross the network unencrypted. The screen
  says so while joined.
- An uploaded image is checked against its own SHA-256, not against a
  signature. Any image built as `rcbench-panel` for the ESP32-P4 installs
  after the 2s hold, and on a home network a device between the browser
  and the panel can replace the image on its way. The IOmcu image a
  panel image carries comes with it and is offered to the IOmcu at the
  next link-up.
- The panel joins only while this screen is open. A panel that stays joined
  whenever the bench is SAFE is not built.
- The page is slow to arrive. On a home network the page, 18.9kB with its
  card listing, took 4.54s, about 4.2kB/s, against a download's
  688-757kB/s. The cause is not measured.

### The access code

The page asks for an 8-character code before it lists a file or takes an
upload. The screen shows it, and it is made new at every start of the radio,
so a code from an earlier session opens nothing. The browser keeps it for the
session. 16 wrong codes switch the radio off.

ACCESS CODE switches it, for both ways in. It is on by default, after
FORGET HOME NETWORK and on a panel with nothing kept. The switch is kept in
NVS. Off, the page lists, sends files and takes an upload without a code,
and the screen says `Access code off. Any device on the network can open the page.` An upload still installs only after the 2s hold on the
panel.

Every request is checked before the access code, with the code on and
off (`shared/webaccess/wa_guard.h`):

| Check | Refused with |
| --- | --- |
| The `Host` header names the panel: `rcbench-3F7A.local`, `rcbench-3F7A` or the address the browser connected to (`192.168.4.1` as an access point), in any letter case, with no port or port 80. An IPv6 address is refused. | 421 Misdirected Request |
| A page, file, ZIP or file API request carries no body. | 400 Bad Request |
| A code entry has at most 95 bytes of body. | 413 Content Too Large |
| A code entry and an upload carry an `Origin` header of `http://` and a name above; without an `Origin`, a `Referer` that starts so. Neither header, `Origin: null` and any other site are refused. | 403 Forbidden |
| An upload carries the header `X-Rcbench-Upload: 1`, which the page's own script sends. | 403 Forbidden |

So another web page open in the same browser cannot reach the card under
a DNS (domain name system) name of its own, cannot send codes that count
toward the 16, and cannot send an image. A browser sends a header of its
own on a request to another site only after asking that site, and the
panel answers no such question with a yes. A refused request is answered
before its body is read, and the connection closes with the answer.

### The page

One HTML (hypertext markup language) page, with no script or style from
elsewhere, so it works on a phone with no other connection. It follows the
panel's language. It has two parts.

**Panel firmware.** The version installed, a file picker, Upload with a
progress bar, and the panel's answer as a line of text. The file is the
release asset `rcbench-panel-p4-vX.Y.Z.bin`. A file larger than the panel's
slot, 8,126,464 bytes, is refused before a byte is sent. The panel checks
the whole image before it asks anything: the ESP image header for the
ESP32-P4 (chip ID 0x0012), the application's project name `rcbench-panel`, a version of the
form MAJOR.MINOR.PATCH and the appended SHA-256 (secure hash algorithm, 256
bits). A second upload while one waits is refused until CANCEL on the panel.
The screen shows the bytes received while an upload runs. The upload
writes no flash: the panel keeps the file in PSRAM (pseudo-static
random-access memory) until PANEL UPDATE writes it.
The panel writes the image to flash only after the hold on PANEL UPDATE
([Installing: PANEL UPDATE](#installing-panel-update)). While it writes,
the DSI (Display Serial Interface) host goes on scanning the frame buffers
out of PSRAM by DMA (direct memory access), and its interrupt runs during
each flash erase (`CONFIG_LCD_DSI_ISR_CACHE_SAFE=y` in
`firmware/panel/sdkconfig.defaults`). What the display shows during the
erases is not measured.

**Files on the card.** Every file, folder by folder, with its name, its size
in bytes and its modification time as the card has it, in the zone of
SETUP > PANEL's Time zone. A stamp in 1980, an unset clock's, leaves the
time cell empty. Each name downloads
that file. The page reads the card only: nothing on it is deleted, renamed or
written. A name over 255 characters, a path over 384 bytes and a folder
deeper than 16 are left out, and the page says how many. With no card, or a
card that cannot be read, the page says so and the upload still works.

**Download all as one ZIP file** sends the whole card as one ZIP file. The
page gives the number of files, their bytes and the ZIP file's bytes before
the click. The files are stored as they are, not compressed, with their
folder paths from the card's root, names in UTF-8 and the card's times in
the zone of SETUP > PANEL's Time zone, as ZIP takes local times. A
file over 4GiB − 1B, an archive over 4GiB − 1B and more than 65,535
entries use ZIP64. Nothing is written to the card to make it. The panel
keeps 24 bytes per entry in PSRAM, not the archive: it is sent as it is
read. The number of entries has no limit of its own; a card whose entries
do not fit in the free PSRAM gets 503 Service Unavailable.

The download counts the entries, walks the card again to fix each one's
size, and then passes each entry twice: once for its data, and once at the
end for the ZIP file's directory.

- A file that grows by appending, as a log does, is sent as it was when the
  download started: its first bytes, up to the size it had then.
- A file rewritten in place is sent with its first bytes as they are when
  the download reads them.
- A file or folder added during the download is not in it. More than 16
  added between the count and the second walk get 503 Service Unavailable.
- A file made shorter before the download reads it, and a file or folder
  removed or renamed before the download's last pass over it, end the
  download short; the browser reports it as failed. The rotation of
  `BENCH/RCBENCH.LOG` at 1MiB is a rename.

On a home network a download of a 16MiB file ran at 688-757kB/s, the card
read and the radio together, not separated, and an upload of a 2.56MB image
to `/update` at 556-599kB/s. The ping averaged 14.6ms; no SDIO (Secure
Digital input/output) error was logged and the panel drew 60fps during the
downloads. The range is not measured.
[Performance](Performance.md#wi-fi-downloads) gives what bounds the rate and
the steps to measure it. A download of S bytes at a rate of R bytes per
second takes S / R seconds: at 700kB/s, a 50MB card takes about 71s. The page answers one request at a time, so a request made
during a download waits for its end. A card pulled during a download ends
the download short, and so does a browser that takes nothing for 30s; the
browser reports it as failed, and the panel runs on.

### The file API

The page's server also answers four reads for a script: a page of a
folder, the facts of one entry, a text file whole, and the panel's state
(`shared/webaccess/wa_api.h`, `firmware/panel/components/wifi_access/wifi_files.c`).
The page itself does not call them. They read the card only, through
the same pages, rules and file load as FILES: nothing is written, renamed
or deleted. Each takes the guard of the table above and the access code
as a download does; a request without a code gets 403 and `CODE`.

| Request | Answer |
| --- | --- |
| `GET /api/status` | JSON (JavaScript Object Notation): the panel's name, version and language, `"writes":"closed"` with the reason (`READ_ONLY`; `ARMED` or `LOGGING` while the write gate is shut), the card's mount number, name, size and free bytes, and the limits: 48 entries a page, 1,048,576 bytes and 65,536 line ends a file |
| `GET /api/list?path=P&go=G&key=N&key_dir=D` | JSON: up to 48 entries of folder `P`, folders first, then names with case folded, as FILES lists them. Each entry has its name, whether it is a folder, its bytes, its kind (`FOLDER`, `RUN`, `REPORT`, `KST`, `PROFILE`, `FAULT`, `CSV`, `JSON`, `TEXT`, `OTHER`), its place (`FREE`, `FIXED`, `LOCKED`, `FAULT`) and whether the logger writes it. Then the mount number, the entries ahead of the page, the folder's total, the entries not listed, and whether a page stands before and after. `G` is `first` (the default), `last`, `next`, `prev` or `same`; the last three name the page by the entry `N` at its edge, `D` 1 for a folder. |
| `GET /api/info?path=P&dir=D&mount=M` | JSON: the size and the time FAT stamped of `P`, -1 for none; for a folder (`D` 1) its entries, their bytes and the entries not listed |
| `GET /api/file?path=P&mount=M` | the bytes of text file `P` as the card holds them, BOM (byte order mark) included, with `ETag` (their CRC-32, cyclic redundancy check), `X-Rcbench-Eol` (`lf`, `crlf`, `cr` or `mixed`), `X-Rcbench-Lines` and `X-Rcbench-Bom` (1 or 0) |

A path is relative to the card's root and percent-encoded with `/` as the
separator: `%2F` is refused, `+` is a plus. It holds at most 120 bytes and
16 folders, as on FILES. With `mount=M`, an INFO or a file read from
another card than the one listed is refused with `CARD_GONE`.

A refusal is an HTTP status and `{"error":"WORD","text":"..."}`, the text
in the panel's language:

| Word | Status | When |
| --- | --- | --- |
| `NAME` | 400 | a path the card or FILES does not take |
| `REQUEST` | 400 | a parameter outside the ones above, or no path for INFO or a file |
| `ARG` | 400 | a folder asked as a file |
| `CODE` | 403 | no access code, or a wrong one, which counts toward the 16 |
| `LOCKED` | 403 | the servo programmers' records, `KST.TXT` and `HITEC.TXT`: download them instead |
| `NOT_FOUND` | 404 | no such entry, or one of another kind |
| `TOO_BIG` | 413 | a file over 1,048,576 bytes, refused before a byte is read |
| `NOT_TEXT` | 415 | a NUL byte in the first 4,096 bytes |
| `TOO_MANY_LINES` | 422 | more than 65,536 line ends |
| `BUSY` | 423 | a file the logger writes |
| `IO` | 500 | the card failed a read |
| `NO_CARD`, `CARD_GONE`, `MEMORY`, `CANCELLED` | 503 | no card; the card taken out or swapped; no PSRAM for the request; the radio going off |

The words of the writes FILES makes (`EXISTS`, `FULL`, `DENIED`, `FIXED`,
`NOT_EMPTY`, `SAME`, `ARMED`, `LOGGING`) have their status and text too;
no read gives them.

The API's memory is PSRAM, taken at its first request and given back when
the radio goes off: about 22kB for a request, and 1MiB for a file's bytes
and 256KiB for its line index at the first file. Each list, INFO and file
request logs its answer, its bytes, its time and the stack the server's
task left unused, as `api: <request> <answer>, <n>B in <t>ms; httpd stack
<n>B unused`.
The server's stack is 8,192 bytes; a card read on it is not measured on a
panel.

### Installing: PANEL UPDATE

An image the page received waits on the panel as a question with both
versions:

![A newer image](img/wifi-question.png)

An older version is allowed and the question says it is older. A board
drawing being fetched or kept makes the install wait:

![An older image, and a drawing being kept](img/wifi-older.png)

The same version from another build is offered as a reinstall. The build
that runs already is not offered:

![The same version, another build](img/wifi-rebuild.png)
![The build that runs](img/wifi-installed.png)

INSTALL is a 2s hold (`HOLD 2s TO INSTALL`). The browser cannot start it.
It needs the bench SAFE, the supply output off, no log run open, no board
drawing being fetched or kept and no update of the IOmcu running
(`Installation is waiting for the IOmcu update to finish.`). CANCEL drops the image.

![Writing](img/panel-update.png)

The hold switches the radio off first. Then the control task stops its polls
and withholds the heartbeat on purpose, so the IOmcu holds its outputs
in failsafe as it does at any restart of the panel. PANEL UPDATE takes the
whole screen, with no band and no STOP: nothing can be switched to ARMED
behind it. A 64px bar holds the title; the bytes stand in the 20 x 36px
face over a bar 36px tall and 976px wide, and the lines under it are in
the 12 x 24px face. It writes the slot that is not running in pieces of 64KiB and
shows the bytes and the percentage. The display's scan-out goes on during
each flash erase; what the display shows then is not measured.
Then the slot is read back and checked, it becomes the one that starts, and
the panel restarts.

![Written and checked](img/panel-update-done.png)

The splash shows the new version, and the band says
`Panel firmware 0.22.1 installed.`. A write that fails changes nothing that
starts: the running build stays, and CLOSE goes back to the screen that
opened WI-FI, SETUP or LOGS, or after a download from GitHub to where the
download started.

![The write failed](img/panel-update-failed.png)

A new build's first start is a trial. Without touch the panel restarts into
the build before it at the touch step: a bench without touch has no STOP.
With the display and touch up, the build is kept once the panel works as a
whole:

- the card was tried, and a missing or unreadable card counts;
- the settings are loaded;
- the control task and the logger task have each run one pass;
- a whole frame reached the display, and the screen loop ticked once after
  it.

The IOmcu is not waited for: a panel without one is kept the same way.
30s after the start the build is kept once the screen loop ticks, whatever
else is missing. Keeping writes otadata, so it waits for a moment that can
take a flash write, by the rule the settings save follows: not while the
bench is ARMED or an arm waits, not while the supply output is on, and not
while a board drawing is fetched or kept. The console says which way it
went:

    I (…) rcbench: 0.22.1 on trial: kept once every task runs and a frame is on the display, or at 30s after the start with the screen loop running
    I (…) rcbench: 0.22.1 kept: every task runs and a frame is on the display, 4210ms after the start
    W (…) rcbench: 0.22.1 kept at the 30s bound with the screen loop running; not seen: logger task

The figure 4210ms is an example; the time is not measured. A start that
does not get there, by a panic, a watchdog reset, a restart or a power
cut, goes back to the build before it, which says
`Panel firmware 0.22.1 failed to start. Version 0.22.0 is running again.` on the band.
A failed start is never kept: when marking it invalid fails, the panel
restarts with the trial unconfirmed, and the bootloader goes back all the
same. When keeping a good start fails (otadata not written), the band says
`Panel firmware 0.22.1 was not kept. The next restart restores the previous version.`; the build
runs until then.

Current limitations:

- A power cut during the trial start loses that line; the panel still goes
  back to the build before.
- A trial start that stops before the screen loop runs, in a card mount
  that does not return for example, is not kept and not reset either: the
  task watchdog reports and does not restart (CONFIG_ESP_TASK_WDT_PANIC is
  off). The panel stays on the splash until a restart or a power cut,
  which goes back to the build before.
- How long the erase and the write take is not measured. The dark display
  at each erase is reported on 0.20.0 and not measured on 0.21.x.

Before the first update over Wi-Fi, the panel needs one flash over USB to
reach the layout with two application slots: [First run, §1](FirstRun.md#1-flash-both-boards).

## Setup

The SETUP tile of the menu opens a page of tiles, 3 by 3, in the menu's
style. Each tile is 328 x 130px with 10px margins: a 36px icon centred
over its name in the 12 x 24px face, and a line in the 10 x 20px face.

![Setup](img/setup.png)

- Four categories: PANEL, INTERFACES, SD CARD and SENSORS, the bench-wide
  settings. Each tile names the number of settings its list shows. A tap
  opens the category's list.
  The settings of the pack, the motor and the throttle output are on
  MOTOR & ESC > SETTINGS
  ([MOTOR & ESC settings](#motor--esc-settings)).
- Five screens of their own: OUTPUTS ([Outputs](#outputs)), PICK A PIN
  ([Pick a pin](#pick-a-pin)), WI-FI ([Wi-Fi](#wi-fi)), UPDATES
  ([Updates](#updates)) and FILES ([Files](#files)). The band of each
  names it as its tile does, and the band of this page reads SETUP.
- No slot is free. A tenth entry has no slot: the page holds 9, and the
  build fails at the tenth.
- SAVE, bottom right, is the same button the lists show
  ([Keeping the values](#keeping-the-values)). A change left unsaved in any
  category shows here too:

![The tiles with a value unsaved](img/setup-hub-dirty.png)

The same page in the light theme:

![Setup in the light theme](img/setup-light.png)

A category's list is 984px wide from the panel's left margin and shows
428px of it, 6 rows whole, over a strip of three columns, 48px tall: the
category's name, RESET CATEGORY and SAVE. A row is 64px tall at a 70px
pitch: the setting's name in the 20 x 36px face, its help line under it in
the 10 x 20px face, its value in the 12 x 24px face. A value wider than
the gap between the keys would take the 10 x 20px face; no value of a
SETUP setting needs it, in English or in German. The strip ends at panel y 552, 4px above the alert band
([The alert band](#the-alert-band)), so an alert never covers a key, a row,
RESET CATEGORY or SAVE, on the tiles or on a list.

![PANEL](img/setup-application.png)

The home tag climbs one level at a time:

| From | The home tag leads to |
| --- | --- |
| a category's list | the tiles; the list keeps its position |
| the tiles | the menu; with a value unsaved, the warning first ([Leaving with values unsaved](#leaving-with-values-unsaved)) |
| OUTPUTS, PICK A PIN, WI-FI, UPDATES, FILES | the tiles; WI-FI and FILES opened from LOGS go back to LOGS, OUTPUTS opened from TARGET on MOTOR & ESC or SERVO goes back there. A list or a keyboard open on one of them closes first ([Outputs](#outputs), [Pick a pin](#pick-a-pin), [Wi-Fi](#wi-fi)) |

The menu's SETUP tile always opens the tiles. Leaving SETUP writes nothing,
from the tiles or from a list.

PANEL's Language switches the whole interface between English and
German on the next frame, with no restart. What follows it, what stays
English and why: [Interface language](Language.md). One finger pans, in the
same category, sets whether one finger pans the zoomed log plot; it is off by
default and is described under [Logs](#logs). Keyboard see-through sets how
much of the text shows through the text editor's keyboard: OFF, 25% or 50%,
OFF by default; described under [The text editor](#the-text-editor).

Eight settings are stored and shown, and no part of the firmware reads them:
Units and Dim after under PANEL; Cells, Capacity and
Internal R on MOTOR & ESC; Vibration, Optical tacho and Tacho pin. Ramp limit is read only
as the slew of the modelled bench.

### Keys and the list

A row's `-` and `+` keys (`<` and `>` on a switch or a choice) change its
value by one step. A changed value is in force at once; SAVE decides whether
it survives a restart ([Keeping the values](#keeping-the-values)). A number
stops at the ends of its range, and so does INA228 address, which has `-`
and `+` keys: 0x40 to 0x4F. A shunt on SENSORS takes 0 (OFF) or 5.0 to
1000.0mΩ: `+` from OFF gives 5.0 and `-` from 5.0 gives OFF. A switch or a
choice wraps round from its last value to its first.

- **Tap.** A key steps once when the finger lifts. The lift counts when the
  finger is the one that pressed the key, is inside the key, and is no more
  than 8px from the press in x and in y. The key is drawn pressed from the
  press on.
- **Hold.** A key held steps for the first time 0.45s after the press, then
  8 times a second, and 30 times a second from 2.25s after the press. Held
  for 1.00s it has stepped 5 times, for 3.00s 38 times. The lift after a
  hold adds no step. The hold ends when the finger is more than 8px from the
  press or outside the key.
- **Scroll.** A vertical drag of more than 8px that starts anywhere on the
  list scrolls it, the key columns included. A scroll changes no value. The
  bar right of the list, 12px wide, shows the position and takes no touch.
  A list that fits its 428px has no bar.
- A key is drawn 56 x 48px and takes its column, 56px wide, over the
  64px of its row. The 6px between two rows belong to no key.
- **Keypad.** A tap on a number's value, the 166px between its `-` and `+`
  keys, opens the keypad over the list and the strip: 1004 x 482px, keys of
  241 x 85px, its foot 4px above the alert band. It names the setting, its
  range and its unit, shows the value faint until a digit is typed, and
  takes as many digits after the point as the step has: one for a shunt's
  0.1mΩ, none for a pin. OK sets the value as a step sets it: in force at
  once, kept by SAVE. A value between two steps is set to the nearer step:
  73% brightness is 75%. A value outside the range keeps the keypad open
  and names the range in the warning colour. A value inside the range that
  the keys never reach keeps it open too and names the reason in place of
  the range, in the warning colour: `0 or 5.0 to 1000.0mΩ` for a shunt
  between 0 and 5.0mΩ, `0 or 10 to 32767mA` for a current limit
  between 0 and 10mA, `Pins the bus takes: 0 4 6 14 16 18 20 26` for a
  Sensor SDA pin the bus cannot have, `Pins the tap takes: 0 1 2 4 5 6 7 13
  14 15 16 17 18 19 20 21 22` for a Tap pin the module does not bring out,
  and `The other PD mini pin holds this GPIO` for the GPIO the other PD mini
  pin holds. The pin lists are built from `settings_sense_sda_valid()` and
  `settings_tap_pin_valid()`, the checks the rows' keys step by. The next
  key brings the range back. A pin's keypad has no `+/-`
  key: -1, a pin not set, is set with `-`. A number shows as many digits
  after the point as its step has, so the row reads what the keypad takes:
  1.25A, not 1.2A. CANCEL, OK with nothing typed and the
  home tag close it and change nothing; leaving SETUP closes it too. Nothing
  under it takes a press. A switch, a choice, INA228 address and Sensor SCL
  open no keypad, and a drag that starts on a value scrolls and opens
  nothing. The bench's state does not lock the keypad. It locks the keys
  of one row, Screen rotation, while ARMED
  ([PANEL: Screen rotation](#panel-screen-rotation)).
- One finger at a time. While one presses or scrolls, a second finger does
  nothing on this screen.

Two limits:

- A finger that rests on a key for 0.45s or longer and then scrolls has
  stepped the key: that is a hold.
- A contact the touch controller leaves out of one report arrives as a lift
  and a new press. If the new press is on a key and lifts there without
  moving, it is a tap. How often the GT911 drops a moving contact from a
  report is not measured.

### PANEL: Screen rotation

Screen rotation turns the picture and the touch by 180°, for a panel
mounted upside down.

| Setting | Values | Default |
| --- | --- | --- |
| Screen rotation | NORMAL, 180° | NORMAL |

![Screen rotation at 180° while the bench is ARMED: the row has no keys](img/setup-rotation.png)

- The flush turns the picture. In 180° every screen draws the upright
  picture into a logical buffer, one beside each of the two frame buffers
  (2 x 1,228,800 bytes of PSRAM, pseudo-static RAM, allocated at start-up
  in both settings). Each flip copies what the frame drew, as at most 8
  rectangles of 16-row bands, into the frame buffer the panel scans,
  turned by 180°, through the ESP32-P4's PPA (pixel-processing
  accelerator). NORMAL draws straight into the frame buffer and copies
  nothing. The EK79007's own scan direction (DCS (Display Command Set)
  command 0x36) cannot turn the picture on this board: the strap on its
  PINCTL pin holds the scan direction, at start-up as while the video
  runs.
- The copy runs on the render loop, inside the flip. The control task,
  which owns STOP, never waits on it. Its time per flip is the console's
  `TURN` figure. On SERVO LIVE sweeping, ARMED, a station run read TURN
  3.8 to 4.8ms and 30.2fps, against 39 to 45fps in NORMAL: with the copy,
  every frame takes two panel frames ([Performance](Performance.md#on-the-board)).
  On the host, SERVO LIVE
  sweeping copies 77,093px a frame on average, MOTOR & ESC LIVE ARMED
  192,911px, a screen at rest 0px (`test_gfx_turn`).
- The glass's contacts turn with the picture. A contact the glass reports
  at logical pixel (x, y) in NORMAL reports at (1023 - x, 599 - y) in 180°.
  Both read one flag: the one the flip sets once the turned frame is on the
  glass. Every screen, the band, STOP and the safety rules see logical
  pixels in both settings.
- Touch injector contacts are logical pixels in both settings and do not
  turn. Station scripts and the coordinates of the injector's README hold
  in both.
- A step applies on the next frame, without a restart, with one whole
  repaint and two whole copies. SAVE keeps it, and the next start draws its
  first frame, the splash, turned.
- A flip whose copies take longer than 50ms, or that the PPA refuses, sends
  its frame out unturned. Picture and touch are NORMAL from that frame on,
  the band says `Screen rotation 180° failed. NORMAL until restart.`, and
  the console says `rotation 180 off: PPA timeout` (or `PPA error`). The
  stored setting stays 180°, and the next start turns again. A copy the PPA
  ends after its timeout writes its block once more: that block shows
  turned until its screen repaints it.
- The PPA's SRM (scale, rotate, mirror) engine runs with Espressif's
  workaround for hardware bug DIG-734 on for every copy, as Espressif's
  esp_lvgl_adapter 0.6.4 patches it (`firmware/panel/ppa_dig734.cmake`).
- Locked while ARMED: the row has no keys, and RESET CATEGORY and DISCARD
  leave it. A turn moves every control away from the finger, STOP
  included. The rule is the one Idle pulse and Full pulse on MOTOR & ESC,
  SETTINGS follow: a value for a SAFE bench only.

### PANEL: Time zone and the clock

The panel board carries no RTC (real-time clock) chip. Its clock is the
ESP32-P4's system time ([Building](Building.md#the-panels-clock)).

| Setting | Values | Default |
| --- | --- | --- |
| Time zone | UTC, LONDON, BERLIN, HELSINKI, NEW YORK, CHICAGO, DENVER, LOS ANGELES, TOKYO, SYDNEY | BERLIN |

![PANEL scrolled to Time zone, set to LOS ANGELES](img/setup-time-zone.png)

- Time zone sets the zone file dates are written and shown in: FAT stamps
  on the card, FILES INFO, the Wi-Fi page's file list and the ZIP file. A
  step applies to the next stamp and the next frame, without a restart.
  SAVE keeps it; the next start sets it before the card mounts.
  Daylight saving follows each zone's rule: Berlin has CET (UTC+1) and CEST
  (UTC+2) from the last Sunday of March 01:00 UTC to the last Sunday of
  October 01:00 UTC. The rules are POSIX TZ strings in
  `shared/clock/wall_clock.c`.
- FAT stores a file's time as local time at the moment of writing, with no
  zone (ESP-IDF's `get_fattime()` reads `localtime_r()`). A later change of
  the zone does not move the times of files already on the card: a file
  written at 12:00 still reads 12:00 after a step to a zone 1h behind.
- The row is not locked while ARMED: it moves no control.
- At each power-on the clock starts unset, at 1970-01-01 00:00:00 UTC. A
  file written then is stamped 1980-01-01 plus the time since the
  power-on, `1980-01-02 06:54` a day and 6h 54min after it: FAT has no
  earlier date. FILES INFO reads such a stamp as `not known`, and the
  Wi-Fi page's file list leaves its time empty.
- A software restart keeps the clock: a firmware update, a restart after
  a fault. ESP-IDF keeps the system time in the ESP32-P4's RTC timer,
  which only a power-on resets. The timer counts the board's 32.768kHz
  crystal Y3, or the internal RC oscillator when the crystal does not
  start ([Building](Building.md#the-rtc-slow-clock)). With the crystal
  the drift is at most about 30ppm, measured over 65min. On the RC it is not
  a fixed figure: 584ppm over 2h04min in one run, under 15ppm over
  1h51min in another. Drift accumulates only across a hardware reset; a
  software restart (`esp_restart`) re-anchors the clock to NTP time on the
  next join.
- Each join of the home network asks `pool.ntp.org` once by SNTP (Simple
  Network Time Protocol): the release check at start, CHECK FOR UPDATES
  and the download on [Updates](#updates), and WI-FI's home network
  ([Wi-Fi](#wi-fi)). The request goes out when the panel has its address.
  It ends with the answer or after 30000ms without one, whichever comes
  first.
- A join that ends while the request is unanswered keeps the radio up for
  it, to the answer or the 30000ms at most: the release check's own end,
  OFF on WI-FI, leaving WI-FI and the 10min idle time-out. The console
  says `wifi: radio held for the NTP answer, 30000ms at most from the
  request`. ARM, an arm waiting, the supply coming on, a log run opening,
  STOP, an install, START of an IOmcu update, a failed radio, a lost
  network, 16 wrong access codes and a new WI-FI or GitHub session take
  the radio down at once; the request then ends unanswered and the clock
  stays as it was until the next join. How long an answer takes on a real
  network is not measured; on the bench's home network one came 7.2s after
  the address.
- A GitHub request still waiting on its name lookup does not hold the
  radio: the radio waits 1.5s at most for the request, then goes off, and
  the lookup ends when lwIP gives up on it. No new check or download
  starts before that.
- After each join the console names the DNS (Domain Name System) servers
  the home network gave, in the order lwIP asks them, for example
  `wifi: dns 192.0.2.53, 192.0.2.1`.
- Current limitation: a first DNS server that does not answer delays every
  name lookup after a join by about 7s, the NTP request's and the release
  check's alike. lwIP tries a server at 0s, 1s, 2s and 4s and asks the
  next at 7s; ESP-IDF v5.5.5 offers neither figure in its configuration.
- An answer sets the clock and writes one console line: the server, the
  time set in UTC and how far it moved the clock, for example
  `ntp: pool.ntp.org set 2026-10-24 14:03:12 UTC, offset +1792739352.345s from the old clock`.
  No answer: `ntp: no answer from pool.ntp.org in 30000ms; clock unchanged`.
  A join that ends first: `ntp: join ended before an answer from pool.ntp.org; clock unchanged`.
- At each start the console names the slow clock the RTC timer counts:
  `clock: rtc slow clock 32k crystal, 32768.012Hz calibrated`, or as a
  warning `clock: rtc slow clock internal RC, 136012.345Hz calibrated:
  the 32k crystal did not start`.
- At each start the console says what the clock holds: `clock: not set
  (...s since 1970); file dates read 1980 until an NTP answer` after a
  power-on, or the time kept across a restart in UTC and in the zone.
- The access point (WI-FI without a home network) has no route out and
  asks no time server. A panel that only ever runs the access point keeps
  1980 dates.
- The band's clock (`04:17`) is the run's time since ARM, not the time of
  day. Run logs count their time column from the run's start, and the
  reports (ATESTnnn.TXT, LIMITnnn.TXT) carry no date: the file's stamp is
  their date.

### PANEL: the rotary knob

An MT6701 magnetic angle sensor at 0x06 on a bus of its own, IO30 (SDA) and
IO31 (SCL) of header P3 (I2C: Inter-Integrated Circuit), turns the slider of the bench screen on top: the
throttle on MOTOR & ESC, the horn on SERVO. Wiring:
[Building](Building.md#rotary-knob-wiring). Other screens ignore it.

| Setting | Range | Default |
| --- | --- | --- |
| Rotary knob | OFF, ON | OFF |
| Knob scale | 90 to 720deg in steps of 10 | 270deg |

Knob scale is the knob angle that moves the slider across its whole span.
At 270deg a quarter turn moves the throttle 33.3 points, and the horn
across a third of its travel from -travel to +travel.

- The knob moves a value by how far it turns, never to where it points. A
  turn adds to the throttle or the horn's angle and stops at 0 and 100% or
  at the travel limit. Turning past an end and back moves the value from the
  end.
- It never switches to ARMED. ARM is the same hold on the same button.
- The first reading after power-up and the first one after the sensor stopped
  answering set a reference and move nothing, so a knob that comes back
  turned does not jump the slider.
- The sensor is read every 10ms, and every 100ms while it does not answer.
  A poll is three reads, 0x03, 0x04 and 0x03 again, each with a 5ms bus
  timeout; a NACK or a timeout on any counts as no answer. While the knob
  has no reference, a probe of 0x06 (the address byte alone) comes first,
  and the reads follow only when it is acknowledged. A poll whose two 0x03
  bytes differ moves nothing and keeps the reference. A high byte that
  changes and changes back between the two 0x03 reads is accepted, so one
  step of up to 64 counts (0.52% of the span at 270deg) at a slider end
  stop remains possible; how often is not measured. The sensor reports no
  magnet status over I2C, so an answer is a reading. A step of more than
  90deg between two readings counts as a glitch and is dropped.
- A finger on the throttle track or on the SERVO dial owns the value while it
  is down, and for the whole frame in which it touched the control, also when
  it lifts in that frame. On SERVO the knob does not take the horn from a running or paused
  sweep, a test run, or the open settings panel.
- A frame that lost touch events drops the knob's motion with its gestures,
  and so does a frame in which the router navigated at all, also away from
  the screen and back to it. The knob's command goes out with the next frame;
  if that frame finds lost touch events first, the command is withdrawn and
  the slider returns to its value from before the knob.
- The knob replaces only a throttle or a horn position. While another
  command waits for the frame (a switch to ARMED that has just completed, a switch to SAFE, a
  peak reset, a release), the knob's motion in that frame is dropped, not
  held.
- On SERVO a turn while the bench is SAFE moves nothing and is not kept. The switch to ARMED
  sets the horn to the rest, and the next turn counts from there. A turn
  posted and not yet sent when the bench switches to SAFE or stops is withdrawn. On
  MOTOR & ESC a turn while the bench is SAFE moves nothing and is not
  kept either; the switch to SAFE returns the slider to 0%, and the next turn after
  a switch to ARMED counts from there.
- The knob's bus is the ESP32-P4's second I2C controller, and nothing else
  of the board is on it. The panel never puts the knob, or another device of
  the builder's, at 0x14 or 0x5D, the GT911 touch controller's two addresses
  on the touch bus, or at one the I2C specification reserves, 0x00 to 0x03
  and 0x78 to 0x7F. With no sensor fitted, Rotary knob ON sends one
  unacknowledged probe to 0x06 every 100ms. In the host tests' model of the
  bus that changes nothing else. On the panel it is not measured: 0x00 to
  0x07 have not been scanned on this board.
- The knob is not measured on hardware: its direction, its noise and its
  behaviour with no magnet over it.

### INTERFACES: the current monitors

INTERFACES lists its 23 settings in five sections. A heading above the first
row of each names it: I2C BUS (the INA228, Sensor SDA and SCL, and the
AS5600), PHASE TAP, BALANCE SENSORS (Vibration and Optical tacho), SUPPLY
(the PD mini) and PROG PORT (Hitec D receive). A heading is a row of its
own, 64px high at the list's 70px pitch, in the accent colour over a rule,
with no keys and no value: a tap or a drag on it changes nothing, and a drag
scrolls the list. The list is 28 rows long.

![INTERFACES, the current monitors](img/setup-interfaces.png)

The IOmcu reads I2C (Inter-Integrated Circuit) current monitors on one bus
on two of its pins: a TI INA228 in the ESC's power path, set up here, and up
to four TI INA3221, set up on SENSORS ([SENSORS: the INA3221
parts](#sensors-the-ina3221-parts)). The INA228's rows and the bus's pins
are at the top of INTERFACES:

| Setting | Range | Default | |
| --- | --- | --- | --- |
| INA228 | ON, OFF | OFF | the monitor in the ESC's power path |
| INA228 address | 0x40 to 0x4F | 0x45 | the MATEK I2C-INA-BM as shipped; its solder bridges give 0x44 or 0x41 |
| INA228 shunt | 50 to 20000 µΩ, 1 µΩ steps | 200 | the MATEK's |
| INA228 max current | 1.0 to 300.0A, 0.1A steps | 204.8 | the current the range is set for: it chooses the ADC (analog-to-digital converter) range and nothing else. 300A is the bench's design maximum; the IOmcu refuses more. Its refusal of a range past 2000A full scale stays, and 300A on the smallest shunt, 50 µΩ, does not reach it |
| Sensor SDA | −1, 0, 4, 6, 14, 16, 18, 20, 26 | 16 | IOmcu GPIO (general-purpose input/output). `-` and `+` step from one of these values to the next and stop at −1 and at 26. −1: the pins are not set, and no part is enabled on the page |
| Sensor SCL | SDA + 1, or −1 | 17 | the GPIO after Sensor SDA, and −1 with it. The row shows the value and has no keys |

SDA and SCL are one I2C block's pair: SDA's GPIO number mod 4 is 0 or 2, and
SCL is the GPIO after it. Sensor SDA takes only the pins of such a pair that
the RP2350-CAN brings out and does not reserve: GPIO 2 is left out because GPIO 3
is the heartbeat, GPIO 8 to GPIO 12 are the CAN controller's, and GPIO 22, GPIO 24 and
GPIO 28 because the module does not bring out GPIO 23, GPIO 25 and GPIO 29. The rule is
the one the IOmcu's SENSE page judges a write by
(`link_sn_pins_pair()`, `settings_sense_sda_valid()`). A pin an output, the
PD mini or the phase tap holds is offered all the same, and the IOmcu
refuses it. With Sensor SDA at −1 the panel writes ENABLE 0, SDA 0 and SCL 0
to the page.

The keypad refuses a pin the rule does not take and shows the pins it
takes in place of the range, listed from the same rule: GPIO 5 typed shows
`Pins the bus takes: 0 4 6 14 16 18 20 26`.

![GPIO 5 refused for Sensor SDA](img/setup-sda-why.png)

A pin is written with the word first, `GPIO 1`, in the keypad's entry, on the
row and in the question on leaving SETUP unsaved. The keypad's header gives
the range the same way, `GPIO -1 to 47`.

![The keypad of Sensor SDA, GPIO 1 typed](img/setup-sda-keypad.png)

Stored settings: the `sns_scl` key is not read. Sensor SCL is derived from
the stored Sensor SDA, and the key is saved with the derived value. A stored
Sensor SDA outside the values above loads as 16, with no alert and without
marking the settings as changed.

The bus runs at 400kHz, which is not a setting:
the IOmcu takes no other clock. A reading at the top of the INA3221's
range, 163.8mV across the shunt, is shown as clipped and never as a value.

The panel writes the set-up to the IOmcu's SENSE page (protocol 4.7)
500ms after the last edit, only while the bench is SAFE, and only what
differs from what the page holds: the IOmcu refuses a change while it
drives, and keeps each change in flash. It reads the page at every link-up
first, so a set-up the IOmcu already keeps is not written again. When
a monitor's own values change while both are enabled, both are switched off
on the page first and on again after, so no step on the way puts the two
parts on one address. An
IOmcu older than 4.7 is sent nothing. After a write is taken the panel
reads the IOmcu's identity again: capability bits 3 and 4 follow the
set-up, and with the INA228 enabled the MOTOR & ESC tile loses its SIMULATED
mark.

While a monitor is enabled, the panel reads SENSE's 14 read-only registers
every 50ms; while the INA3221 is, SERVO_SENSE's channel windows every
50ms, each 50ms window once. With an IOmcu of protocol 4.11 and CH1
among the INA3221's channels it also reads SERVO_WIN every 50ms, which
holds the meter channel's last four windows, and SENSE to register 31
([Link](Link.md#the-window-ring)). With an IOmcu of protocol 4.25 it reads
SENSE_WIN_A and SENSE_WIN_B on every poll, after SERVO_WIN and at most every 40ms, while a part on the page is fitted,
for the run log's columns ([TABLE](#table-and-what-the-esc-says)).

The servo rail has one meter at a time: the INA3221 channel that watches
the servo tester's target ([SENSORS](#sensors-the-ina3221-parts); on an
IOmcu older than 4.25, CH1 of the one part SENSE reads) while a fitted
part has that channel, the IOmcu speaks protocol 4.11, the IOmcu holds the
set-up, the part answers as an INA3221 on a bus that is not stuck, a window
with current samples closed in the last 200ms, its reset count stands, and
all of that has held for 1000ms; the PD mini otherwise. The SERVO screen's
CURRENT row, line and plot show the meter's reading ([Servo](#servo)); the
servo test reads the PD mini whichever it is.

The band says what they show, each once and one at a time: the most
pressing first, and the next no sooner than 5s later, so two at once are
both said. In order:

| Message | When |
| --- | --- |
| `IOmcu has no SENSE page. Current monitors cannot be read.` | a monitor is enabled and the IOmcu speaks a protocol older than 4.7; said at the link-up, and when a monitor is enabled while it answers |
| `Sensor SDA or SCL not set. Check SETUP > INTERFACES.` | a monitor is enabled with a pin at −1; neither is enabled on the page |
| `INA228 and INA3221 both use 0x40. Check SETUP > INTERFACES.` | the INA228 on the address of a fitted INA3221; neither is read |
| `Sensor pins GPIO 16/GPIO 17 rejected. Check SETUP > INTERFACES.` | the IOmcu refused the pins: bound to an output, held by the PD mini or the phase tap, or reserved; both monitors stay off. Shown for 30s or until tapped, once per edit |
| `INA228 shunt or maximum current rejected. Check SETUP > INTERFACES.` | the shunt's voltage at the max current passes 163.84mV, or the range it gives passes 2000A; the INA228 stays off |
| `INA3221 settings rejected. Check SETUP > SENSORS.` | the IOmcu refused the INA3221 set-up: the SENSE_MAP map, or on an IOmcu older than 4.25 the one-part frame; the IOmcu keeps what it held |
| `INA3221 0x41 has every channel off. Check SETUP > SENSORS.` | a fitted part has all three shunts at OFF; no map is written until one is on again, and the line leaves the band then |
| `Sensor bus blocked: SDA low. Trying clock pulses to release it.` | the IOmcu found SDA or SCL held low and clocks the bus free. A line that reads low with nothing holding it (pins with no part and no pull-up wired to them) is not this: it rises within 50µs on the IOmcu's own pad pull-up (32 to 86kΩ), and the monitors read as not answering |
| `INA228 is not responding at 0x45.` | enabled, and not answering 2.5s after its set-up was taken, or having stopped answering after it did. `A device responds at 0x44.` is added when the IOmcu's address scan found something where the part could be |
| `0x40 reports 1408h, not an INA3221. Device not used.` | something answers at the part's address with another identity |
| `INA228 current at its range limit. Reading shows only the limit.` | the INA228 read the end of its range in the last 50ms window or since the switch to ARMED |
| `INA3221 0x40 CH1 clipped at 1.64A. Reading shows only the limit.` | a channel of the part SERVO_SENSE reads hit the top of its range; the current is its full scale |
| `IOmcu storage unavailable. Settings will be lost at restart.` | STATUS fault bit 6: the IOmcu saves nothing this boot, so the set-up, the bindings and the supply's wiring are lost at its restart; said once per link-up |
| `IOmcu firmware older than 0.16.0. Servo current is read from the PD mini.` | the INA3221 is on with CH1 and the IOmcu speaks protocol 4.7 to 4.10: it has no window ring, and the PD mini is the servo rail's meter. Said at the link-up, and when the INA3221 is switched on while it answers |
| `INA3221 has reset. Check its power supply.` | the IOmcu found the INA3221 on its power-on set-up and set it up again 1000ms later; said once per reset. The windows of those 1000ms hold no samples |

A refused set-up is not written again until it changes on SETUP. Pins that
are one I2C block's pair, refused because an output, the PD mini or the
board holds one, are offered again every 5s without another alert, so
freeing the pin on OUTPUTS or SUPPLY lets the bus open. A monitor
that stops answering does not switch the bench to SAFE: nothing trips on the
monitors' readings.

An alert about the set-up leaves the band as soon as its cause is over,
before its 30s are up: `Sensor SDA or SCL not set.` and `INA228 and
INA3221 both use 0x40.` with the edit that puts the setting right, and a
refusal once the IOmcu holds the frame as asked: at once when the value
is stepped back to the one the page still holds, otherwise when the write
of the new value is taken, 500ms after the last edit and with the bench
SAFE (`sense_link_event_stands()`). An alert that was replaced before a
frame showed it, and whose cause is over by its next turn, is not shown.
What a read said (not responding, another identity, a limit, a blocked
bus) stays for its 30s or until tapped.

#### A row the IOmcu does not hold

![INTERFACES, pins the IOmcu has not taken](img/setup-unheld.png)

A row whose value is not the one the IOmcu's page holds is marked: its
edge and its value are drawn in the warning colour, and its help line reads
`Not accepted by the IOmcu.`. The panel compares the set-up it asks for
with the one the page last acknowledged or read back (`sense_link_unheld()`,
`tone_link_unheld()`). The mark covers the current monitors' rows, Sensor
SDA and Sensor SCL (one mark, the pins are one frame), AS5600, and the phase
tap's rows in two groups: Phase tap, Tap pin and the two tones, and Tap
pitch split, Tap gap and Tap min periods. SENSORS marks its rows by part
([SENSORS: the INA3221 parts](#sensors-the-ina3221-parts)).

| The mark shows | Until |
| --- | --- |
| from an edit | the write is acknowledged: 500ms after the last edit, and for the current monitors only once the bench is SAFE |
| on a value the IOmcu refused | the value is changed to one it takes; for pins held elsewhere, until the 5s retry is taken |
| on INA228 or AS5600 at ON while the page has the part off | the cause is gone: a refused frame, Sensor SDA at −1, the INA228 on a fitted INA3221's address, an IOmcu older than the part |
| after an IOmcu restart or a link-up, on what its page reads differently | the panel has written it |

No row is marked while no IOmcu answers, or before the page has been
read after a link-up: there is nothing to compare with. AS5600 centre is the
panel's own and is never marked. The alert on the band is separate and stays
as described above.

### SENSORS: the INA3221 parts

SENSORS sets up to four TI INA3221 three-channel current monitors on the
sensor bus, at 0x40 to 0x43, and says per channel what it measures. The
INA228 and the bus's pins stay on INTERFACES.

![SENSORS, two parts fitted](img/setup-sensors.png)

The list starts with the heading INA3221 PARTS over four rows:

| Setting | Range | Default | |
| --- | --- | --- | --- |
| 0x40 fitted to 0x43 fitted | ON, OFF | OFF | an INA3221 at this address. The DAOKAI module takes 0x40 with its A0 solder bridge closed to GND. As shipped all four A0 bridges are open and the address floats between 0x40 and 0x41 |

Each fitted part follows under a heading of its own, `INA3221 0x40` to
`INA3221 0x43`, with nine rows, three a channel. A part that is not fitted
has no heading and no rows.

| Setting | Range | Default | |
| --- | --- | --- | --- |
| 0x40 CH1 watches | NONE, OUT 1 to OUT 8, PORT 1 to 4, SUPPLY | OUT 1 for 0x40 CH1, NONE for every other channel | what the channel's shunt is in: an output channel, a programming port, or the supply rail |
| 0x40 CH1 shunt | OFF, 5.0 to 1000.0mΩ, 0.1mΩ steps | 100.0 | OFF: the channel is not read. The DAOKAI's R100 reads to 1.64A |
| 0x40 CH1 limit | OFF, 10 to 32767mA, 10mA steps on the arrows | OFF | a 50ms window whose highest current passes it is said on the band, and the bench switches to SAFE with the supply output off ([Safety, Errors](Safety.md#errors)). OFF: no limit |

A tap on a shunt's value opens the keypad ([Keys and the list](#keys-and-the-list)).
44.4mΩ from the 100.0 default is five keys, `4`, `4`, `.`, `4` and OK,
where `-` takes 556 steps. 0 is OFF; 0.1 to 4.9 is refused, and the keypad
shows `0 or 5.0 to 1000.0mΩ` in place of the range until the next key.
A LIMIT row takes 0 (OFF) or 10 to 32767mA; 1 to 9 is refused with
`0 or 10 to 32767mA`.

![The keypad of 0x40 CH1 shunt](img/setup-sensors-keypad.png)

![3mΩ refused, the reason in place of the range](img/setup-sensors-why.png)

The heading of a part shows on its right what the part is doing:

| State | When |
| --- | --- |
| `ONLINE` | the IOmcu reads the part, which answers as an INA3221 |
| `LOOKING FOR THE PART` | the set-up is taken, and the IOmcu has not found the part yet: the first 2.5s |
| `NOT ANSWERING` | nothing answers at the part's address 2.5s after the set-up was taken, or the part stopped answering |
| `ANOTHER ID ANSWERS` | something answers at the address with another identity |
| `NOT WRITTEN YET` | an edit waits: 500ms after the last edit, and only while the bench is SAFE |
| `NOT READ YET` | no IOmcu answers, or its page has not been read since the link came up |
| `NOT ACCEPTED BY THE IOMCU` | the IOmcu refused the map; it keeps the one it held |
| `EVERY CHANNEL OFF` | all three shunts of the part are OFF: no map is written |
| `INA228 ON THIS ADDRESS` | the INA228 is on at the part's address: the part is left out of the map and the INA228 off |
| `IOMCU OLDER THAN 0.22.0: NOT READ` | an IOmcu older than protocol 4.25 reads one part only, and not this one |
| `SENSOR SDA OR SCL UNSET` | INTERFACES has no pin for the sensor bus's SDA or SCL: no part is enabled |

The rows of a part refused, with every channel off, on the INA228's
address, without a sensor bus or not read by an older IOmcu are marked as a row the IOmcu does
not hold: edge and value in the warning colour, and a line in place of the
help. On an IOmcu older than 4.25 that line reads `IOmcu older than 0.22.0: one INA3221, CH1 at 1kHz.`:

![SENSORS on an IOmcu older than 4.25](img/setup-sensors-old.png)

A LIMIT row is marked the same way while the IOmcu does not hold its
limit: from the edit to the write taken, after a refused write or a
refused page with `Not accepted by the IOmcu.`, and on an IOmcu older than 4.27, for every
limit that is not OFF, with `IOmcu older than 0.22.0: no limit is judged.`:

![A limit on an IOmcu older than 4.27](img/setup-sensors-limit.png)

The panel writes the limits to the IOmcu's SENSE_LIMIT page (protocol
4.27) in one write of 12 registers, LIMIT_MA in mA, 500ms after the last
edit, in any bank state, and only where the page holds something else. It
reads the page whole at every link-up. While the page holds a limit that
is not OFF, it reads OVER and OVER_COUNT every 200ms, armed or not; that
read clears OVER on the IOmcu. The IOmcu judges each channel's 50ms
window once as it closes: over when the window's highest current is above
the limit, or when a sample read the top of the range and that top is
above it ([Link](LinkSensors.md#sense_limit)). Before it writes new
limits over held ones, the panel reads OVER once, so a window judged under
the old limit is said with the old limit. The band says, at most once
every 5s and for one channel at most once every 30s while it keeps
passing. Channels over at the same time take turns, starting after the
channel said last: with all 12 over, each is said within 12 alerts, 60s.

| Band | When |
| --- | --- |
| `INA3221 0x41 CH2 (PORT 1) over its 0.80A limit.` | a window of the channel passed its limit since the last read; the target in brackets is what the channel watches, none for NONE; the limit is the one the window was judged by |
| `An INA3221 channel passed its limit; which one was lost on the link.` | OVER_COUNT moved with no OVER bit: the reply that carried the bits did not arrive. A reply that does not arrive takes the link down, so this is said after the read at the next link-up. A count of 0 there is an IOmcu that started again and is not said |
| `INA3221 limits rejected. Check SETUP > SENSORS.` | the IOmcu refused the write; it keeps the limits it held, and the panel does not write again until a limit changes. Also an IOmcu that names protocol 4.27 or later and answers the page with BAD_PAGE: the panel sends the page nothing more until the next link-up |

OVER bits the IOmcu holds from before the link came up, also those of an
IOmcu that started again, are said by channel at the first read.

The panel writes the map to the IOmcu's SENSE_MAP page (protocol 4.25) in
one write of 26 registers: the parts fitted, the channel read at 1kHz,
the twelve shunts and what each channel watches. As for SENSE it reads the
page first at every link-up, writes 500ms after the last edit and only
while the bench is SAFE, and writes nothing the page holds. A refused map is
not written again until it changes. While a part is fitted the panel reads
the parts' IDs and states every 500ms. A part that stops answering, or
that answers as something else, is said on the band once, as on INTERFACES;
the part SENSE reports on (below) is said by SENSE's own reads.

The channel read at 1kHz, whose windows the servo test and the SERVO
screen use, is the lowest channel with a shunt on a fitted part that
watches the servo tester's target: an output, OUT 1 to OUT 8, while the
target is OUTPUTS, and PORT n while it is programming port n. Every other
channel with a shunt is read in turn, one a millisecond: 90.9Hz each with
eleven ([Link](Link.md)).

On an IOmcu older than 4.25 the servo test and the SERVO screen take the
INA3221 as the servo rail's meter only while that channel is CH1 of its
part: that IOmcu reads CH1 at 1kHz. With it on CH2 or CH3 the rail's meter
is the PD mini there.

An IOmcu older than 4.25 has no SENSE_MAP and reads one INA3221 from
SENSE: the part of the channel read at 1kHz, else the lowest fitted part,
with its CH1 shunt (or its first one that is not OFF) and the channels on
that shunt. SENSE carries one shunt for every channel it reads, so a channel
on another shunt is not read.

With the map in force, SENSE's INA3221 address, shunt and channels mirror
it on the IOmcu and the panel never writes them.

Stored settings: a panel updated from 0.21 has no SENSORS keys. Each one
it lacks is taken at start-up from the four INA3221 settings 0.21 kept:
the part at the old address fitted when the INA3221 was ON, its CH1 on the
old shunt, CH2 and CH3 on it with CH1+2+3 and OFF with CH1, and its CH1
watching PORT 1 when the servo tester's target was the programming port,
OUT 1 otherwise; every other channel 100.0mΩ and NONE; every limit OFF.
The old settings
stay stored, unchanged, so a panel taken back to 0.21 finds its set-up. The
next SAVE writes every SENSORS key.

### INTERFACES: the output encoder

An ams OSRAM AS5600 magnetic angle sensor on the servo's output shaft, on the
sensor bus beside the monitors, at its fixed address 0x36 (protocol 4.9):

| Setting | Range | Default | |
| --- | --- | --- | --- |
| AS5600 | ON, OFF | OFF | the output encoder; it takes the sensor bus's SDA and SCL above, and needs both pins set |
| AS5600 centre | 0 to 4095, 1 count steps | 0 | the sensor's 12-bit count with the servo at its neutral; 4096 counts a turn, 0.0879 degrees a count. The SERVO screen's ENC CENTRE sets it from the live reading |

The panel writes the enable bit with the rest of the bus frame, 500ms after
the last edit and only while the bench is SAFE, to an IOmcu that
names protocol 4.9; an older one is sent nothing. While the encoder is
enabled it reads SENSE's registers 12 to 31 every 40ms. The band says, each
once and one at a time like the monitors':

| Message | When |
| --- | --- |
| `IOmcu firmware older than 0.15.0. AS5600 not read.` | the encoder is enabled and the IOmcu speaks a protocol older than 4.9; said at the link-up, and when the encoder is enabled while it answers |
| `AS5600 is not responding at 0x36.` | enabled, and not answering 2.5s after its set-up was taken, or having stopped answering after it did; also when something at 0x36 gives a STATUS no AS5600 gives, ML and MH both set |
| `AS5600 detects no magnet. Check the magnet on the servo horn shaft.` | it answers, an angle has been read, and STATUS MD (magnet detected) is clear. The count is no angle: MEASURED shows `---`, ENC CENTRE sets nothing, and a run logs and judges no angle |
| `AS5600: magnetic field too weak. Move the magnet closer.` | STATUS MD and ML are set. The angle is used; the sensor's datasheet specifies its noise for 30 to 90 mT only |
| `AS5600: magnetic field too strong. Move the magnet farther away.` | STATUS MD and MH are set. The angle is used, as with ML |

A magnet message is said once while the sensor's STATUS bits MD, ML and MH
stay as they were said, also across the set-ups an edit of another row of
INTERFACES writes. It is said again when one of the bits changes (a weak
magnet that goes missing, a missing one that comes back weak, a weak one
that reads strong), when the field read right in between, when the sensor
was said not to answer, after the sensor was switched off, and after a
link-up. The angle stops when MD
clears and returns when it is set again. The sensor's
DIR pin sets the direction its count rises in; the panel takes the count to
rise with the servo's pulse width.

### INTERFACES: the phase tap

![INTERFACES, the phase tap](img/setup-tap.png)

The phase tap hears an ESC's beeps on one motor phase. One IOmcu
GPIO reads the phase through a series resistor and a zener clamp; the
IOmcu times the pin's edges and reports each beep's length and pitch.
The wiring is not in the build guide. The tap is an input: it drives
nothing, and the IOmcu takes a change ARMED or not. Its rows follow
the bus's pins:

| Setting | Range | Default | |
| --- | --- | --- | --- |
| Phase tap | ON, OFF | OFF | the capture runs on the IOmcu |
| Tap pin | 0, 1, 2, 4 to 7, 13 to 22 | 22 | IOmcu GPIO; GPIO 22 is pad 29. The keypad and the keys offer the pins the RP2350-CAN brings out and does not reserve, less the ADC (analog-to-digital converter) pins GPIO 26 to GPIO 29: GPIO 3 is the heartbeat, GPIO 8 to GPIO 12 are the CAN controller's, and the module does not bring out GPIO 23 to GPIO 25 and GPIO 29. Refused by the IOmcu: a pin bound to an output; a pin the SENSE or SUPPLY page holds |
| Tap lowest tone | 50 to 2000Hz, 10Hz steps | 400 | a tone below it is not a beep |
| Tap highest tone | 100 to 6900Hz, 50Hz steps | 6500 | above the lowest tone |
| Tap pitch split | 0 to 50%, 1% steps | 8 | a pitch change this large starts a new beep with no silence between; 0 splits on silence only |
| Tap gap | 1 to 100ms | 3 | the silence that ends a beep; at least one period of the lowest tone, so 20ms at 50Hz |
| Tap min periods | 1 to 64 | 3 | the tone periods that make a beep |

The panel writes the set-up to the IOmcu's TONE page (protocol 4.8)
500ms after the last edit, and only what differs from what the page holds:
the IOmcu keeps each change in flash. It reads the page at every
link-up first. The set-up goes in two frames, the pin and the tone range,
then the split, the gap and the periods. When both change, the one that
leaves the page a valid set-up goes first: a lower tone with a gap shorter
than its period is refused, so the gap is written before the tone goes down
and after it goes up. Switching the tap off writes the first frame with
the values the page holds and the tap disabled, whatever range is asked, so
a range the page would refuse cannot keep the tap running; the range
follows in a frame of its own. Until the page holds the tap off the screen
shows `WAITING`, not off. A set-up kept in flash that met a busy pin at the
IOmcu's boot is written again every 5s, without another message,
while the page reports the pin refused. A change of ENABLE or of the pin
empties the IOmcu's ring and the panel takes its place at the newest
beep number, so none is read twice and none counted as missed; a change
of the range or of the other values leaves the ring and the numbering
alone. An IOmcu older than 4.8 is sent nothing.

While the page holds the tap on, the panel reads its 16 read-only registers
every 50ms: the flags, the last 8ms window's pitch, the number of the
newest beep and the counts of beeps lost and lows ignored. A read whose
flags say the tap runs speaks for it while its window number moves: the
IOmcu closes a window every 8ms, so a number that has stood for
1.5s (`TONE_LINK_WINDOW_STALE_MS`) is a capture that stopped while the
page still answers, and the screen shows `WAITING` as for a read older
than 500ms. It takes the
beeps one by one by number. The IOmcu keeps the last 64 beeps,
numbered 1 to 65535 and then 1 again, and a read does not remove one, so a
reply lost on the link loses no beep. A beep that left the ring before it
was read, or that the ring moved past while the panel was more than 64
behind, is counted as missed. The last 8 beeps are kept for the screen; the
run page of ESC STICK shows 4 of them ([ESC STICK](#esc-stick)).

The band says what the tap reports, each once and one at a time, as for the
current monitors:

| Message | When |
| --- | --- |
| `IOmcu has no TONE page. Phase tap not read.` | the tap is enabled and the IOmcu speaks a protocol older than 4.8; said at the link-up, and when the tap is enabled while it answers |
| `Phase tap on GPIO 22, 400 to 6500Hz rejected. Check SETUP > INTERFACES.` | the IOmcu refused the first frame: the pin is not allowed, the tone range is not one it takes (the highest tone not above the lowest, or the gap shorter than the lowest tone's period), or it runs no capture since its boot (core 1 not started). A pin that an output, the sensor bus, the PD mini or a programming port holds is refused as busy (BUSY, protocol 4.17): offered again every 5s without another message, so freeing the pin lets the tap start. Any other refusal is written again only after the next change on SETUP INTERFACES or link-up. From a 4.16 or older IOmcu, which says BAD_VALUE for both, the frame is offered again every 5s when its tone range and gap are valid ([Link](Link.md#a-busy-pin-on-the-phase-tap)) |
| `Phase tap split, gap or periods rejected. Check SETUP > INTERFACES.` | the IOmcu refused the second frame; not written again until a value changes |
| `Phase tap pin GPIO 22 is in use. Capture not running.` | the IOmcu reports the pin refused, for one of two causes. A set-up it kept in flash met a binding at its boot: the first frame is offered again every 5s without another message, so freeing the pin lets the tap start. The IOmcu runs no capture since its boot (core 1 not started, protocol 4.17): no pin is bound and freeing one changes nothing. The next offer of the first frame is refused with BAD_VALUE without another message, and the frame is not offered again until the next change on SETUP INTERFACES or link-up; only a restart of the IOmcu lets the tap run. The band says `Phase tap on GPIO 22, 400 to 6500Hz rejected. Check SETUP > INTERFACES.` only for a set-up other than the one the IOmcu kept, written at link-up before the first status read or less than 5s after the pin was first reported not free or last offered; a change made later goes out as a quiet offer |
| `Phase tap capture overflow. Beeps are incomplete.` | the IOmcu's capture ring or FIFO (first in, first out buffer) overran since the tap was enabled; the beep under way was cut |

The three alerts about the set-up (the first frame rejected, the second
frame rejected, the pin in use) leave the band as soon as the cause is
over, before their 30s are up (`tone_link_event_stands()`): a refused frame
once the IOmcu holds it as asked, at once when the value is stepped back to
the one the page still holds, otherwise when the write of the new value is
taken 500ms after the last edit; a pin in use once a status read shows the
pin taken, or once the tap is switched off and the page holds it off. Tap gap
2ms with Tap lowest tone at 400Hz is refused by design: the gap has to be
at least one period of the lowest tone, 2.5ms at 400Hz
(`tone_init()`, `protocols/phase_tap/tone.c`).

### INTERFACES: the programming port

The last row of INTERFACES, under PROG PORT, says how the IOmcu receives
the answer of a Hitec D series servo on its programming port, GPIO 13 by
default ([Receive mode](LinkHitec.md#receive-mode)):

| Setting | Values | Default | |
| --- | --- | --- | --- |
| Hitec D receive | PULL-UP, PRECHARGE | PULL-UP | PULL-UP: a 2.2kΩ resistor from the signal wire to the servo supply lifts the line, and the IOmcu reads it released. PRECHARGE: no resistor; the IOmcu drives the pad high for 100.0ns every 1086.7ns and samples it 113.3ns after it lets go |

It is kept in NVS like the other rows; a missing or unreadable stored value
is PULL-UP. The panel writes it to the HITEC page at every link-up and
after every change, armed or not; the IOmcu takes it at its next OPEN of a
D series line. Classic servos (HS-5xxx, HS-7xxx) need neither and ignore
it. The setting reaches the lines the Hitec programmer on SERVO opens
([Servo programmer for Hitec servos](HitecProgrammer.md)) and those the
bench console opens ([The bench console](BenchConsole.md)).

Unmarked, the help line follows the value: `PULL-UP needs 2.2kΩ on the
signal wire` at PULL-UP, `PRECHARGE: no pull-up on the signal wire` at
PRECHARGE.

The row is marked as the rows of
[A row the IOmcu does not hold](#a-row-the-iomcu-does-not-hold) are, with its
own help line:

| The help line reads | While |
| --- | --- |
| `Not accepted by the IOmcu.` | the value is not acknowledged yet, or the IOmcu refused it |
| `IOmcu older than 0.22.0: receives with PULL-UP.` | PRECHARGE is set and the IOmcu speaks protocol 4.23: it has no register for the mode |
| `IOmcu older than 0.21.1: no Hitec line.` | PRECHARGE is set and the IOmcu speaks protocol 4.22 or older: it has no HITEC page |

No mark shows while no IOmcu answers, and none for PULL-UP on an older
IOmcu.

![INTERFACES, Hitec D receive at PRECHARGE on an IOmcu older than 4.24](img/setup-prog.png)

### SD CARD: which logs are written, and where

![SD CARD](img/setup-card.png)

| Setting | Range | Default | Writes |
| --- | --- | --- | --- |
| Auto logs | OFF, ON | ON | `SERVOnnn.CSV`, `MOTORnnn.CSV` and `SUPPLnnn.CSV` in Auto log folder: a log of each ARMED and supply run |
| User logs | OFF, ON | ON | `ATESTnnn.CSV` with its `ATESTnnn.TXT`, and `LIMITnnn.TXT`, in User log folder: the AUTOMATIC TEST and the LIMIT RUN |
| Auto log folder | a folder, see below | `BENCH/LOGS` | where Auto logs writes |
| User log folder | a folder, see below | `USER/LOGS` | where User logs writes |

- OFF writes no file and takes no number, and no card alert of that kind of
  run shows.
- Auto logs is read on every pass of the control task: switched OFF during
  a run it ends that run's file, switched ON during an ARMED or supply run
  it starts one.
- User logs is read at START TEST and START LIMIT RUN: a test keeps the
  state it started with. With it OFF, the run card and LIVE's box read
  `USER LOGS OFF` where the files would be named.
- REPORT on SERVO > OPTIONS > DUT decides, with User logs ON, whether
  `ATESTnnn.TXT` is written.
- The fault log, `BENCH/RCBENCH.LOG`, is written whatever the two say.
- [Logs](#logs) gives the names and the numbering.

A tap on a folder's row opens the on-screen keyboard over the list, on the
folder in force. The `.` key types `/` there. OK takes the folder, in upper
case, as an edit: SAVE keeps it, as it keeps a value, and the warning on
leaving SETUP names it. CANCEL and the home tag leave the folder as it was.
OK on a folder the card cannot take keeps the keyboard open and the folder
in force unchanged, and the line right of the title says why:

![The folder keyboard with a folder of 9 characters refused](img/setup-folder.png)

| Refused | Example | Line |
| --- | --- | --- |
| an empty folder name: nothing typed, or a `/` at the start, at the end or twice | `/LOGS`, `LOGS/`, `A//B` | `A folder name is empty` |
| a folder name of more than 8 characters | `BENCHLOGS` (`BENCHLOG` is taken) | `8 characters a folder at most` |
| more than 2 folders | `A/B/C` (`A/B` is taken) | `2 folders deep at most` |
| a character other than A-Z, 0-9, `_` and `-` | `MY LOGS` | `A-Z, 0-9, _ and - only` |
| a device name of Windows: CON, PRN, AUX, NUL, COM1 to COM9, LPT1 to LPT9 | `AUX/LOGS` | `Reserved name` |
| a folder of the bench's own files: `BENCH/ESC`, `BENCH/KST`, `BENCH/HITEC` | `BENCH/KST` (`BENCH/KSTX` is taken) | `Folder of the bench's own files` |

Every folder name the panel takes is a FAT (File Allocation Table) 8.3 short
name, the same on every computer. The longest folder is 17 characters,
`ABCDEFGH/12345678`. A stored folder the panel does not take, for example
one written by another firmware, is not written to: the logs go to the
default folder of their kind, and LOGS lists that folder.

### Keeping the values

A changed value takes effect at once and is not written to flash until SAVE is
pressed. SAVE, beside RESET CATEGORY on a list and alone on the tiles,
says which of four states the values are in. The values are one set: the
state is the same on the tiles and on every list.

| Label | Meaning |
| --- | --- |
| `SAVED` | Nothing is unwritten. The button is inert. |
| `SAVE` | Something is unwritten. Pressing it asks for a write. |
| `WHEN IDLE` | A write has been asked for and is waiting for a moment to happen in. |
| `NOT SAVED` | The store refused the write. What reached the medium is not known from the screen: a refusal on one key leaves the keys written before it committed, so the next boot can load a mix of the new values and the old. Pressing it tries again. |

![A changed value, with SAVE offered](img/setup-dirty.png)

The press asks; it does not write. Writing settings commits a page of NVS
(non-volatile storage), and a flash operation on the ESP32-P4 disables the
cache, so neither core runs code from flash for its duration. The write is taken
on the first frame at which the bench is SAFE with no switch to ARMED waiting, the
supply's output is off with no ON queued, and no board drawing is being
fetched or kept. On the settings screen that is the next frame, and
the label goes to `SAVED` as fast as the eye follows the press. While ARMED, the
request stands as `WHEN IDLE` until the bench is SAFE.

A store that refuses leaves the label at `NOT SAVED` in the danger colour
until the next successful write or the next edit. A refusal does not undo what
was already written: the values are set key by key and a failure part way
through leaves the earlier keys committed, so the medium can hold a mix of the
new values and the old. The screen cannot say which. A panel whose NVS could
not be brought up at all refuses every write of the session, writes nothing,
and says so once more on the splash as `NVS unavailable`.

Values not saved are kept until the panel is switched off. Leaving the screen
writes nothing.

A value stepped back to the one stored is no change, and the label returns to
`SAVED`; a write asked for and not yet taken is dropped. A float stepped
away and back can miss its stored word by a rounding, and counts as back
while it is less than half a step from it. The stored values are a copy the panel keeps: the ones loaded at
start-up, the defaults where the store had none, or the ones the last
successful save wrote. A refused save leaves the copy as it was and not
known to be the medium's, so the label stays `NOT SAVED` or `SAVE`
until a save succeeds, even with every value stepped back.

### Leaving with values unsaved

The home tag on the tiles, the only way from SETUP to the menu, is held while
the label reads `SAVE` or `NOT SAVED`. A warning covers the body instead: a
card of 1004 x 424px with a 3px edge in the warning colour, over the strip.
Its title is in the 20 x 36px face, the two lines on what unsaved means and
the notes in the 12 x 24px face.

![One value unsaved, at the home tag](img/setup-leave.png)

It lists each value and string that differs from the stored copy, one line
of 24px each in the 10 x 20px face, as `name: stored -> now`. A value is written as the list
writes it, with its unit; a string, the DUT (device under test) name, as it
is. Values set on other screens, such as SERVO's settings, ESC STICK's or
MOTOR & ESC's, are listed too: they are in the same set and the same save
keeps them. A change on MOTOR & ESC, SETTINGS asks for that save itself. The
lines follow the schema's order, the strings after the values. With more
than 10 changes, 9 lines show and the tenth reads `and 3 more` for 12.

![Twelve values unsaved](img/setup-leave-many.png)

The strip's three columns hold the answers:

| Button | What it does |
| --- | --- |
| STAY | Closes the warning. The tiles show again; a list keeps its category and its scroll. |
| DISCARD | Sets every changed value back to its stored one through the path a step by hand takes, then opens the menu. |
| SAVE | Asks for a write as the save button does, then opens the menu. The write follows the same rule: the first idle frame. |

DISCARD sets each value with the same call a key does, in schema order, so
the IOmcu hears each pin and port that moves, and PANEL's
language, theme, brightness and contrast apply on the next frame. A PD mini
wiring set back switches the supply output off and raises the alert a change by hand
raises. PWM target and Port PWM are not set back while the servo programmer
holds the servo port, as a key does not step them then; they stay unsaved.
After a refused save DISCARD also asks for a write of the values put back:
the medium can hold part of the refused write, and a restart would load it.

While the bench is ARMED, DISCARD is drawn inert and a line
`DISCARD waits until the bench is SAFE.` says why. Setting back several values at once,
pins among them, on a running bench is a change the operator did not make
in that moment. STAY and SAVE work while ARMED; a save waits for SAFE as
`WHEN IDLE`. The line `Saving failed. Tap SAVE to try again.` shows in
the danger colour after a refused save.

The warning holds only the home tag:

- Nothing unsaved, or a save asked (`WHEN IDLE`): no warning, the menu opens.
- OUTPUTS, PICK A PIN, WI-FI, UPDATES and FILES open from a tile and
  return to SETUP: no warning on the way out or back.
- The home tag over the warning is STAY.
- STOP stops the bench and leaves the warning as it is.
- A screen the panel opens by itself, such as the bus-fault screen after a
  link loss, closes the warning at once. The values stay in force and
  unsaved.

## The bus-fault screen

The panel runs the CAN (Controller Area Network) echo self-test at every
start-up, for 1200ms inside the splash. A verdict other than every probe
coming back intact puts this on the panel instead of the menu.

![Frames cross and arrive changed](img/busfault.png)

It exists because the fault is invisible from every other screen: a bus that
does not carry frames looks exactly like an IOmcu that is not fitted, and
both look like a bench that shows no numbers. The verdict is the heading, the
list is what to check in the order that costs least to check, and the right
column is what both ends counted.

The screen fills the panel. A red bar of 64px holds the title, and the
verdict stands under it, both in the 20 x 36px face. The left column,
712px, holds the meaning in at most two lines and the list of checks in
the 12 x 24px face: a row of 32px per check, and 28px more for each line
a check wraps onto. The three notes follow from y 432. The right column,
240px from x 760, holds the counts in the 10 x 20px face.
`HOLD 2s TO ACKNOWLEDGE` runs 976 x 56px across the foot, 16px above the
panel's edge.

![Nothing came back](img/busfault-silent.png)

Leaving it takes a two-second hold, the ARM gesture and the same fade.
Acknowledging repairs nothing: the bench runs in simulation, nothing drives an
output, and the test runs again at the next start-up. There is no band and no
STOP, because the bench cannot switch to ARMED behind it.

### A link that stops

The same screen carries the other half: a link that was up and has been gone
for 4s. The heading is what this panel's own CAN controller is doing, because
the wire carried frames a moment ago.

![The panel is off the bus](img/busfault-lost.png)

| Heading | Meaning |
| --- | --- |
| `Panel disconnected from the bus.` | too many frames went unacknowledged; it stopped transmitting |
| `Panel is reconnecting.` | it is counting the quiet time a rejoin needs, about 3s |
| `The panel's CAN controller has stopped.` | idle and not restarted — a fault in the firmware |
| `The link stopped responding.` | the controller is on the bus and nothing answers |
| `Cannot read the controller's state.` | the driver is not running; nothing can be sent |

Four seconds, not one: the link drops for a poll from time to time, and a screen
that took over on every blip is a screen operators learn to dismiss.

**Never while ARMED.** The screen has no STOP, and a bench with something
spinning must not have its stop button covered by a diagnosis. While ARMED, the
alert band says the link is gone and the screen waits for the switch to SAFE.

The same numbers go to `BENCH/RCBENCH.LOG` on the SD card, one line per report
while the link is down, whatever Auto logs on [Setup](#setup) > SD CARD says.
The file holds at most 1MiB (1,048,576 bytes): a line that would take it past
that renames it to `BENCH/RCBENCH.OLD`, replacing the one there, and starts a
new `BENCH/RCBENCH.LOG`. The card holds at most 2MiB of fault log; at 208
bytes a line every 5s, one file is about 7 hours of link loss. During the
rename the old `RCBENCH.OLD` is named `BENCH/RCBENCH.TMP`, and a rename that
fails gives it its name back. A `RCBENCH.TMP` still on the card at a line, left
by a power cut or a removal the card refused, is removed first, or named
`RCBENCH.OLD` when none stands. When a rename or that removal fails, or a size
does not read, the line is not written. Firmware 0.21.0 and older wrote
`RCBENCH.LOG` in the root; that file stays and is not written again:

    t=182s link=down for 47s  bus=OFF tx_err=248 rx_err=0 bus_err=1976 rejoins=44/44  polls=5323 replies=5279 timeouts=44

The fields are the same in the same order every time. `rejoins` is this outage
over the total since boot, and an unreadable controller writes `?` in its
columns rather than a differently shaped line: the reading that most needs a
timestamp is the one where the controller would not answer.

The card is there because a bench fault can happen with no console attached.
The console is UART0 (universal asynchronous receiver-transmitter 0) with
USB-Serial-JTAG (the ESP32-P4's built-in USB serial and debug bridge) as
secondary, and which of the board's two USB-C sockets reaches which is not
traced. The link's CAN is the ESP32-P4's TWAI (Two-Wire Automotive Interface)
on GPIO21 (receive) and GPIO22 (transmit), to a TJA1051T/3 transceiver; neither
pin carries anything else.

[Bringing up the link](Link.md) has the verdicts and what each one means.

## IOmcu update

A panel built with the IOmcu's image carries it and offers it when the
IOmcu runs another build. The check runs at every link-up and compares
SHA-256 (Secure Hash Algorithm) digests, not version numbers. A different
build raises the alert `IOmcu runs a different build. Update it under SETUP > UPDATES.`, and the question opens on the menu while the bench is SAFE, the
supply's output is off, the panel's Wi-Fi radio is wholly off and no install
of a panel image is under way. A START given while either holds is spent,
and the question opens again once both have ended.

![The IOmcu update question](img/coproc-update.png)

The question is a box of 860 x 248px in the body, its lines in the
12 x 24px face. CANCEL and START are 398 x 56px each, side by side,
START on the right; their middles are at panel x 305 and 719, y 376.

The question names the version the IOmcu runs and the version this
panel carries. CANCEL is a tap: the question closes, and the panel does not
ask again for that IOmcu version until it restarts. Leaving the
question another way declines the same way. SETUP > UPDATES still starts
the update after a decline ([The IOmcu](#the-iomcu)). START
takes the 2s hold, the ARM gesture and the same fade.

An older image is offered too. The question then says `The firmware on this panel is older.` in the warning colour, and START takes the same hold.

![An older image](img/coproc-update-older.png)

Once START's hold completes, the update has the panel to itself: the link to
the IOmcu goes down, the outputs are off, the IOmcu refuses ARM
until the update ends, and the progress fills the screen. It has no band and
no STOP, as the bus-fault screen has none: the bench cannot switch to ARMED
behind it.

![The progress](img/coproc-update-progress.png)

The progress has a 64px title bar, the step in the 20 x 36px face, the
versions under it, a bar 36px tall and 976px wide from y 180, the block
count, and three notes in the 12 x 24px face, 28px apart.

| Step | Shown while |
| --- | --- |
| `Preparing firmware.` | the panel hashes the image, 8KiB a step |
| `Sending firmware.` | the blocks go out, 64 bytes each; the bar and `Block 1497 of 2994, 50%` count them |
| `IOmcu is checking the firmware.` | the IOmcu hashes the slot it wrote |
| `IOmcu is starting the new firmware.` | it restarts into the new slot, on trial |
| `Keeping the new firmware.` | the panel reads the new image's identity and writes BUY |

The screen returns to the menu when the update ends, or to UPDATES when
UPDATES' hold started it. The alert band then says how it ended:

| Alert | Meaning |
| --- | --- |
| `IOmcu updated to 0.22.0.` | the new image runs and is kept |
| `IOmcu update stopped: no response. Previous firmware is running.` | nothing answered for 4s |
| `IOmcu update stopped: refused, error 6. Previous firmware is running.` | the IOmcu refused a step; the error numbers are in [Link](Link.md#the-pages) |
| `IOmcu update stopped: firmware check failed. Previous firmware is running.` | the SHA-256 of the slot is not the image's |
| `IOmcu update stopped: flash memory stayed busy. Previous firmware is running.` | the store's gate stayed shut for 10s |
| `IOmcu update stopped: IOmcu restarted 3 times. Previous firmware is running.` | three starts from block 0 |
| `IOmcu update stopped: new firmware did not start. Previous firmware is running.` | no identity from the new image within 12s; the boot ROM goes back to the old one |
| `IOmcu cannot update over CAN. Install firmware once via USB.` | an IOmcu without the update pages or without the partition table ([First run](FirstRun.md#the-iomcus-partition-table)) |
| `New IOmcu firmware kept.` | an image found on trial at link-up was bought |

Power off during the update: the IOmcu starts its old image, and the
next link-up offers the update again. The update writes the slot the
IOmcu does not run from. [Link](Link.md#firmware-update-over-the-link)
has the transfer, the flash layout and the limits. The transfer time is not
measured.

The question opens from the menu rather than from LOGS: the offer comes with
the link, and LOGS is about the SD card.

## Updates

UPDATES asks GitHub which panel firmware release is the newest, and installs
it. It opens from the UPDATES tile on SETUP, and its home tag leads back
there. A reminder at start
asks the same question once per start of the panel. 0.22 is the last
release for the ESP32-S3 panel; the ESP32-P4 panel is offered every newer
release ([The release line](#the-release-line)).

![UPDATES](img/updates.png)

The buttons are 460 x 56px, 64px apart, on the left. The card on the
right is 532px wide and ends 8px above the alert band; its text runs in
lines of 41 characters of the 12 x 24px face, 28px apart. Under the
buttons the IOmcu section's lines are in the 10 x 20px face, and its
hold is a fifth button of the same size, from body y 428.

| Control | Action |
| --- | --- |
| `CHECK FOR UPDATES` | Asks GitHub once. `CANCEL` on the same key while the check runs. |
| `REMINDER AT START: ON` / `OFF` | Switches the start-up check and its reminder. Kept with SAVE on SETUP, as every setting. On by default. |
| `RELEASES: STABLE` / `TESTING` | Sets which releases a check reads ([The check](#the-check)). Kept with SAVE on SETUP. `STABLE` by default. A switch hides the last check's result: a release found on the other setting is not offered. |
| `HOLD 2s TO DOWNLOAD & INSTALL` | Shown once a check found a newer release. Downloads, checks and installs it. |
| `HOLD 2s TO UPDATE THE IOMCU` | Starts the IOmcu's update from the image this panel carries ([The IOmcu](#the-iomcu)). |

The card names the running version and the kept home network. Both the
check and the download need:

- a home network kept on WI-FI ([Home network](#home-network)).
  Without one, the card says `No home network saved.` and
  `Save a network under SETUP > WI-FI.`, and CHECK FOR UPDATES is grey;
- the bench SAFE, the supply output off and no log run open. Otherwise the
  key is grey and the card says `Wi-Fi requires SAFE, the supply output off,` and
  `and no open recording session.`;
- no update of the IOmcu running and no panel install under way.

The download needs, as an install does, no board drawing being fetched or
kept.

### The check

Releases are numbered by kind. An odd minor number is a testing release,
published on GitHub as a pre-release (0.21.x). An even minor number is a
stable release (0.22.x).

The panel joins the home network, sends one HTTPS (HTTP over transport layer
security) request and reads the newest release's tag from the answer.
RELEASES sets the request:

| RELEASES | Request | What is read |
| --- | --- | --- |
| `STABLE` | `GET https://github.com/subtilitas/rcbench/releases/latest` | The `Location` header of the redirect, which names the tag (`.../releases/tag/v0.20.0`). The body is not read. GitHub's latest release is never a pre-release. When that release is past the release line and newer than the running build, the `TESTING` request follows once ([The release line](#the-release-line)). |
| `TESTING` | `GET https://api.github.com/repos/subtilitas/rcbench/releases?per_page=10` with `Accept: application/vnd.github+json` and `X-GitHub-Api-Version: 2022-11-28` | `tag_name` and `draft` of each of the 10 newest releases by date in the JSON (JavaScript Object Notation) answer, testing or stable. The highest version among those that are no draft is the newest release, whatever their order; the highest of the release line among them is kept beside it. The answer is read in pieces of 4096 bytes up to the 10th release's end, and is not stored. |

Then the radio goes off. The join, the address and the request take 30s at
most, together. The join also asks an NTP server for the time; an answer
still owed when the check ends keeps the radio up for 30s at most from the
request ([PANEL: Time zone and the clock](#panel-time-zone-and-the-clock)). On either setting a release is offered only when it is of
the panel's release line and newer than the running build. The ESP32-P4
panel's line holds every release ([The release line](#the-release-line)).

Current limitation of both settings: a first DNS (Domain Name System)
server of the home network that does not answer delays each name lookup by
about 7s, out of the check's 30s
([PANEL: Time zone and the clock](#panel-time-zone-and-the-clock)).

Current limitations of `TESTING`: GitHub answers 60 API requests an hour for
each public IP (Internet Protocol) address without an account, and the
devices behind one address share them. Each check is one request, and the
panel sends no retry. A release older by date than the 10 newest is not
seen, however high its version.

![A check under way](img/updates-checking.png)

The card shows the step (`Connecting to Workshop 2.4`, `Checking GitHub.`) and the
seconds left (`26s left`). The result stays on the card until the next
check; a check stopped for WI-FI clears once WI-FI's session has ended and
the radio is off:

![A newer release](img/updates-newer.png)
![Up to date](img/updates-current.png)
![A testing release found with RELEASES on TESTING](img/updates-testing.png)

| Card | Meaning |
| --- | --- |
| `Version 0.22.1 is available.` | The newest release is newer than the running build. The hold appears. |
| `Up to date. Panel version: 0.22.0.` and `Latest release on GitHub: 0.22.0.` | The newest release is the running version or older. |
| `No newer release for the ESP32-S3 panel. Panel version: 0.22.0.` and `Latest release on GitHub: 0.23.0.` | A panel held to the 0.22 line only: the newest release is newer than the running build and past the release line. It is not offered, and the reminder at start does not open for it. |
| `Could not connect. Password rejected.`, `Could not connect. Network not found.`, `Connection failed. No IP address within 15s.`, `Connection to the home network lost.` | The join failed, as on WI-FI. |
| `No response within 30s.` | The 30s ran out. |
| `No connection to GitHub.` | The name lookup, the connection or the certificate check failed. |
| `GitHub rejected the request: HTTP 404.` | An answer other than a redirect (`STABLE`) or other than 200 (`TESTING`). |
| `GitHub's request limit is reached: HTTP 403.` and `GitHub allows 60 API requests an hour per address.` | `TESTING`: GitHub answered 403 or 429, its limit for requests without an account. |
| `The panel's own version is not MAJOR.MINOR.PATCH: no update offered.` and `Latest release on GitHub: 0.22.1.` | The running build's version does not parse. No release is offered. |
| `Unexpected answer from GitHub.` | The `Location` (`STABLE`) or the API's answer (`TESTING`) is missing or out of form; a second line says which (tables below). |
| `Wi-Fi switched off: bench ARMED.`, `...: supply output on.`, `...: a recording session started.`, `...: STOP.` | The check ended for that. |
| `Stopped: the WI-FI screen is using Wi-Fi.` | WI-FI opened. The line clears once WI-FI's session has ended and the radio is off. |
| `Stopped: the IOmcu update has started.` | The IOmcu's update began. |
| `Cancelled.` | CANCEL. |
| `Wi-Fi could not start.` | The radio did not come up. |

![A check that found no answer](img/updates-failed.png)

The version text is read strictly: `v`, then MAJOR.MINOR.PATCH, digits only,
each number from 0 to 65535 in at most 5 digits and without a leading zero
(`v0.20.0`, not `v0.020.0`), and nothing after it. A release is newer when
its MAJOR is higher, or MAJOR is equal and MINOR is higher, or both are
equal and PATCH is higher.

| Second line | Meaning |
| --- | --- |
| `The response has no Location header.` | The redirect carries no `Location`. |
| `The address is too long.` | A `Location` longer than 69 bytes: the tag address and 18 bytes. |
| `A byte is outside printable ASCII.` | A space, a control character or a byte above 126. |
| `The address is in an unexpected location.` | Not `https://github.com/subtilitas/rcbench/releases/tag/...`. |
| `The version text has an invalid format.` | The tag fails the rule above. |

The API's answer under `TESTING` is read as strictly. It needs status 200
and either a `Content-Length` from 1 to 262,144 bytes or the chunked
coding (`Transfer-Encoding: chunked`) with its releases ending within
262,144 bytes. GitHub sends the chunked form when its cache does not hold
the request (`Cache-Control: max-age=60`) and the `Content-Length` form
from its cache. The 10 newest releases on 2026-10-18, v0.12.0 to v0.20.0,
were 108,198 bytes, the largest one 13,734. Of each release, only its own `tag_name` and `draft` count: a
key in a nested object or inside a string is passed over. A release with
`draft` true is passed over. A release out of form refuses the whole
answer, a draft's included, and nothing is offered: the second line says
why. An 11th release and anything after the 10th is not read.

| Second line | Meaning |
| --- | --- |
| `The response does not specify a size.` | Neither a `Content-Length` nor the chunked coding. |
| `The response is empty.` | A `Content-Length` of 0, or a chunked answer without a byte. |
| `The download is too large for the panel.` | A `Content-Length` over 262,144 bytes. |
| `Received less data than expected.` | The answer ended before its `Content-Length`. |
| `Received more data than expected.` | A read returned more bytes than asked for. |
| `The answer names no tag_name.` | A release has no `tag_name`, there is no release but drafts, or the answer ended before its array or its 10th release did. |
| `The tag_name is too long.` | A `tag_name` over 18 bytes. |
| `The answer is not in the expected JSON form.` | Not an array of objects; a `tag_name` that is not a plain string (an escape, a control character, a number); a `draft` other than `true` or `false`; either key twice in a release; a control character in another string; a bracket closed by the other kind; nesting deeper than 32 levels. |
| `The releases do not end within 262144 bytes.` | The scan's own bound of 262,144 bytes: a chunked answer whose array or 10th release does not end within it. An answer within its `Content-Length` bound does not pass it. |
| `The version text has an invalid format.` | A tag fails the rule above. |

### The release line

0.22 is the last release for the ESP32-S3 panel. Later releases are for the
7-inch ESP32-P4 display board from Waveshare.

The ESP32-P4 panel's line holds every release: a check offers any release
newer than the running build, on `STABLE` and on `TESTING` alike
(`FW_REL_LINE_CAPPED` 0 in `shared/fwupdate/include/fw_release.h`). The
card's foot is the reminder's and RELEASES' three lines, the last
`TESTING also offers testing releases: odd minor numbers.`.

| GitHub names | ESP32-P4 panel on 0.23.0 |
| --- | --- |
| `v0.24.0`, `v1.0.0` | offered: `Version 0.24.0 is available.` |
| `v0.23.0` | `Up to date. Panel version: 0.23.0.` |
| `v0.22.1` | `Up to date. Panel version: 0.23.0.`: an older release is never offered |

The ESP32-S3 panel's line, 0.22 (release 0.22.0; in this tree
`fw_release_set_line_capped(true)`, which the host suite sets and the
ESP32-P4 build does not), offers a release of MAJOR 0 and MINOR 22 only,
newer than the running build, on `STABLE` and on `TESTING` alike
(`FW_REL_LINE_MAJOR` and `FW_REL_LINE_MINOR`). Its card's foot says so in
its first lines, `0.22 is the last release for the ESP32-S3 panel. Later
releases are for the 7-inch ESP32-P4 display board from Waveshare.`, and
its last line reads `TESTING also reads testing releases. Both offer only
0.22.x.`:

| GitHub names | Panel on 0.22.0 | Panel on 0.22.1 |
| --- | --- | --- |
| `v0.22.1` | offered: `Version 0.22.1 is available.` | `Up to date. Panel version: 0.22.1.` |
| `v0.22.0` | `Up to date. Panel version: 0.22.0.` | `Up to date. Panel version: 0.22.1.`: an older release is never offered |
| `v0.23.0`, `v0.24.0`, `v1.0.0` | `No newer release for the ESP32-S3 panel. Panel version: 0.22.0.` | `No newer release for the ESP32-S3 panel. Panel version: 0.22.1.` |
| a tag out of form, such as `v0.22.1-rc1` | `Unexpected answer from GitHub.` and `The version text has an invalid format.` | the same |

With `TESTING`, when the highest of the 10 newest releases is past the
line, the highest release of the line among them stands in for it once it
is newer than the running build: beside a 0.23.0 pre-release, 0.22.1 is
offered to a panel on 0.22.0. `STABLE` reads GitHub's latest release; a
stable release older than a published one is not marked latest
([Building: Releases](Building.md#releases)). When the latest release is
past the line and newer than the running build, `STABLE` sends one request
more, the one `TESTING` sends, and offers the newest 0.22.x among the 10
newest releases when it is newer than the running build. That request counts
against GitHub's limit of 60 API requests an hour; when it fails, the card
reads as without it: `No newer release for the ESP32-S3 panel.`. With
nothing on the line newer than the running build, both settings end with
that line and name the newest release. The IOmcu takes the image this panel
carries, from the same release ([The IOmcu](#the-iomcu)).

Current limitation of the 0.22 line: the list holds the 10 newest releases
by date (`per_page=10`). A 0.22.x patch release with 10 or more releases
published after it is not found, on either setting.

### The IOmcu

Under the buttons, UPDATES shows the IOmcu's update: the version the
IOmcu runs (`Running: 0.21.2`), the version of the image this panel
carries (`Carried: 0.22.0`), one line of state, and
`HOLD 2s TO UPDATE THE IOMCU`. The hold starts the update that the
link-up's question starts, with the same progress screen
([IOmcu update](#iomcu-update)). It also starts it after that
question was declined. When the update ends, or the START was spent
without one, the screen returns to UPDATES.

![The IOmcu section with another build](img/updates-coproc.png)

The hold is live only in the last row of the table below. Otherwise it is
grey, and the line names the first reason that holds, in this order:

| Line | Condition |
| --- | --- |
| `This panel carries no IOmcu firmware.` | The panel was built without the IOmcu's image. `Carried: not known`. |
| `Waiting for the IOmcu update to finish.` | An update runs, an image found on trial is being kept, or a START is not taken yet. |
| `No IOmcu is responding.` | No link, and no IOmcu of another protocol major answers. `Running: not known`. |
| `Waiting for the IOmcu's build check.` | An IOmcu answers; its build was not compared yet. |
| `Checking the IOmcu's build.` | The SHA-256 comparison runs. |
| `Build check failed. It runs again at the next link-up.` | The comparison ended without an answer. |
| `No update over CAN. Install firmware once via USB.` | The IOmcu has no update pages or no partition table. |
| `No IOmcu image for this board in this panel build.` | The IOmcu reports a board (IDENTITY register 5) this panel build carries no image for. No update is offered: another board's image would put its pin map on this one. |
| `IOmcu runs the build this panel carries.` | The digests are equal: there is nothing to send. |
| `The update requires SAFE and the supply output off.` | ARMED, an arm waiting, or the supply output on or switching on. |
| `Waiting for the panel's Wi-Fi to switch off.` | A check, a download or the WI-FI screen has the radio on, or it is still switching off. |
| `Waiting: panel firmware installation is running.` | A panel install is under way. |
| `Outputs stay off. ARMED is blocked until the update ends.` | The hold starts the update. For an older image the line reads `The firmware on this panel is older.` in the warning colour; for the same version number `Same version number, but a different build.` |

The conditions are the question's: another build, the bench SAFE, the
supply output off, the radio wholly off and no panel install. An IOmcu
that runs the carried build gets no hold; the link-up's question does not
open for it either. An IOmcu of another protocol major shows NO LINK in
the band and still takes the update, as the question offers it to that
IOmcu too.

### The reminder

![The reminder](img/upd-remind.png)

The reminder is a box of 860 x 480px with its lines in the 12 x 24px
face. NOT NOW and NEVER REMIND ME are 398 x 56px side by side; the hold is
812 x 64px under them.

With REMINDER AT START on and a home network kept, the panel runs the check
once after each start. It waits for a moment the bench allows: the splash
gone, the bench SAFE, the supply output off, no log run open, WI-FI closed
and no IOmcu update offered or running. A start-up check
that has found no such moment 10min after the start does not run at that
start.

When the check finds a newer release, the reminder opens on the menu
(OVERVIEW). It does not open while the bench is ARMED or an arm waits,
while the supply output is on, or while the IOmcu's update question or
its progress is up: those come first, and the reminder opens once they have
gone and the menu shows.

| Control | Action |
| --- | --- |
| `NOT NOW` | Closes the reminder until the next start of the panel. Leaving it by the HOME tag does the same. |
| `NEVER REMIND ME` | Switches REMINDER AT START off and asks for the save, which happens at the next moment the settings may be written ([Keeping the values](#keeping-the-values)). SETUP > UPDATES switches it on again. |
| `HOLD 2s TO DOWNLOAD & INSTALL` | The same hold as on UPDATES. |

The start-up check shows nothing when it finds no newer release or fails.
Its result is on UPDATES.

### The download

The 2s hold is the one confirmation: no question follows the download. The
panel joins the home network within 30s, then fetches two files of the
release's tag over HTTPS, `SHA256SUMS` and `rcbench-panel-p4-v0.23.0.bin`.
GitHub answers each with a redirect to its file host; the panel follows at
most 3 redirects, and only to `github.com`, `release-assets.githubusercontent.com`
and `objects.githubusercontent.com`. Then:

1. `SHA256SUMS` is 4096 bytes at most, and holds exactly one line for the
   image's file name.
2. The image announces its size before a byte is kept. An image without a
   size, of 0 bytes, or larger than the slot (8,126,464 bytes) is refused.
   The image is kept in PSRAM (pseudo-static random-access memory).
3. The image's SHA-256 (Secure Hash Algorithm, 256 bits) must equal its line
   in `SHA256SUMS`.
4. Every check an uploaded image passes ([Installing](#installing-panel-update))
   runs, and the image must name the release's version.

Only then does the install start, as INSTALL starts it: the radio off
wholly, the write, the restart, the trial start. From the hold to the write,
nothing touches the flash.

![The download](img/panel-download.png)

| Step | Shown while |
| --- | --- |
| `Connecting to Workshop 2.4` | the join and the address |
| `Reading SHA256SUMS.` | the first file |
| `Downloading firmware.` | the image; the bar and `720896 of 1843200 bytes (39%)` count it |
| `Checking the SHA-256 checksum and firmware.` | the checks |

CANCEL ends the download. The download also ends on STOP, on ARMED, on the
supply output coming on, on a log run opening, on a board drawing being
fetched, and after 180s from the hold. The running build stays, and CLOSE
goes back to UPDATES, or to the menu when the hold was the reminder's.

![A download that stopped](img/panel-download-failed.png)

| Line | Meaning |
| --- | --- |
| `Cannot use the SHA256SUMS file.` | `SHA256SUMS` missing (`HTTP status 404.`), over 4096 bytes, `A checksum line has an invalid format.`, `No checksum entry for this firmware.` or `Two checksum entries for this firmware.` |
| `The SHA-256 checksum does not match SHA256SUMS.` | The image is not the one the release lists. |
| `The firmware did not pass the panel's checks.` | One of the image checks refused it. |
| `GitHub rejected the request: HTTP 404.` | An answer to the image other than the file or a redirect. |
| `Unexpected answer from GitHub.` | `More than 3 redirects.`, `Redirect to a host that is not allowed.`, `The address is too long.` (a `Location` over 2048 bytes), `The response does not specify a size.`, `The response is empty.`, `The download is too large for the panel.`, `Received less data than expected.`, `The firmware file specifies a different version.` |
| `Not enough memory for the firmware.` | PSRAM had no room for the announced size. |
| `No response within 30s.`, `No response within 180s.` | The join's 30s or the download's 180s ran out. |

Current limitations:

- The check's and the download's durations on a real network are not
  measured.
- No certificate's dates are checked: mbedTLS is built without its expiry
  check (`CONFIG_MBEDTLS_HAVE_TIME_DATE` off, ESP-IDF's default).
- A release whose assets are not named `rcbench-panel-p4-vX.Y.Z.bin` and
  `SHA256SUMS` cannot be downloaded.

## Outputs

Behind the OUTPUTS tile on SETUP, and behind the TARGET rows of MOTOR & ESC
and SERVO; its home tag leads back to the screen that opened it. Over the
open protocol list the home tag closes the list, choosing nothing, and the
screen stays. The protocols bound to the IOmcu's pins, which pins each one
drives, and the IOmcu's programming ports. From SETUP it opens on the
[overview](#the-overview-off): OFF selected, every bound pin named by its
protocol, and no pin editable until a protocol is picked. From a TARGET row
it opens on what that workbench drives:

| Opened from | Opens on |
| --- | --- |
| SETUP's OUTPUTS tile | the overview, OFF |
| MOTOR & ESC's TARGET row | the protocol the throttle's pins are bound as; the PROG PORT entry of a DShot port with no pin beside it; the overview while they are bound as several (`MIXED`), two ports drive the throttle, or none is known |
| SERVO's TARGET row | what the row shows: the PROG PORT entry while the IOmcu drives the port, SERVO PWM while it drives the outputs, the overview while the target is not known |

![Outputs](img/outputs.png)

The left column is 300px wide: the PROTOCOL label, the protocol list's
face (300 x 64px), the count and the reason under it in 10 x 20, and
LAST WRITE at the column's foot with the result in 20 x 36. The pins take a
grid of 4 columns of 7 cells, 167 x 64px each (25.1 x 9.6mm), 8px apart
across and 6px apart down, from x 324. A cell holds a 24px tick box, the
pin name in 16 x 28 (`GPIO 28`, 112px) and, under them, what holds the pin or its pad in
8 x 16, 18 characters at most. The grid's last row ends at body y 492,
4px above the alert band. The open protocol list holds the 8 protocols in
a column of 48px rows (7.2mm) from the face's top and the PROG PORT
entries in a second column of the same rows beside it; with 4 entries the
list is 600 x 392px and ends at body y 436.

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
has room for nothing else. It runs at 40Hz, not the 50Hz the pulse drivers
use: eight channels need 23,300us of frame and 50Hz gives 20,000.

SERVO PWM and MOTOR PWM are the same pulse at the same 50Hz and differ in
what the channel is for. A servo channel centres when nothing commands it; a
motor channel stops. Nothing in the pulse says which is on the pin, so the
entry says it: bind an ESC as MOTOR PWM and a control surface as SERVO PWM.
The MOTOR & ESC throttle drives every channel bound as a motor — MOTOR PWM
and the DShot entries — and leaves the servo channels alone.

A MOTOR PWM channel sends 0% throttle as the Idle pulse and 100% as the
Full pulse: 1000us and 2000us by default, set under MOTOR & ESC, SETTINGS, in
steps of 10us (Idle pulse 800 to 1600us, Full pulse 1400 to 2400us). The
two settings reach the channels bound as a motor and no others: a SERVO PWM
channel keeps 1000 to 2000us, or the range the SERVO screen sends for the
servo chosen there. An edit reaches the IOmcu 300ms after the last
change while the bench is SAFE; one made while it is ARMED waits for the
switch to SAFE. An IOmcu that links up is brought to the two settings before
the bench drives it: with the bench already ARMED, the IOmcu is held
SAFE until it has them. An Idle pulse that is not 100us or more below the Full pulse is not sent, and the
band says `Idle pulse must be 100us or more below Full pulse. Not sent.`.

An ESC whose throttle range was calibrated on a transmitter takes that
transmitter's shortest pulse as zero. An Idle pulse above it reads to the ESC
as throttle not at the bottom, and the ESC does not arm; many ESCs beep
rapidly in that state. Set the Idle pulse at or below the transmitter's
shortest pulse and the Full pulse at or above its longest: for a transmitter
that sends 985 to 2012us, 980us and 2020us.

When the protocol can take no more pins, the reason is under it in amber:
`NEEDS 8 CHANNELS, 4 FREE`, `ALL 8 SLOTS IN USE`, or `SERVO PWM TAKES 8
PINS`. A board drawn entirely in grey with nothing beside it reads as a
fault, and PPM greys the whole board whenever anything else is bound.

![PPM with servo pins already bound](img/outputs-full.png)

The protocol is a list rather than a stepper, because there are eight of them
and stepping past seven to reach the eighth is not choosing:

![The protocol list](img/outputs-protocol.png)

Reserved pins are shown and cannot be ticked. GPIO 3 carries the safety heartbeat
and GPIO 8 to GPIO 12 are the CAN (Controller Area Network) controller, and each
says so under its name. Hiding them would leave an operator hunting for GPIO 10
and finding a gap. [DShot and the output drivers](DShot.md#which-pin) has the
whole map.

The pin of the IOmcu's programming port, GPIO 13 by default, is shown as
`PROG PORT` and cannot be ticked either:

![GPIO 13 held by the programming port](img/outputs-prog.png)

A stored binding that holds that pin keeps it: the port then has no pin, and
the programmer's CONNECT ends with the line for a port without a pin
([the port](ServoProgrammer.md#the-port)).

A pin SETUP > INTERFACES holds is shown the same way, red and struck
through, with what holds it under its name, and cannot be ticked:

| Under the name | Pins | While |
|---|---|---|
| `SENSOR BUS` | Sensor SDA and the Sensor SCL after it, GPIO 16 and GPIO 17 by default | INA228 or AS5600 is ON, or an INA3221 is fitted on SENSORS |
| `PD MINI` | PD mini TX and PD mini RX | PD mini is ON with both pins set, on two pins |
| `PHASE TAP` | Tap pin, GPIO 22 by default | Phase tap is ON |

These are the pins the IOmcu reserves from the outputs for those parts.
While the IOmcu answers, each part's cells show the pins its page holds
by that page's last read or acknowledged write. A move onto pins the
IOmcu refuses leaves the old pins taken and the asked pins free. A part
the IOmcu refused holds nothing: a tap on an ADC (analogue-to-digital
converter) pin from GPIO 26 to GPIO 29, a tap whose pin the IOmcu reports
refused, a sensor bus no part of which the page enables, any part on an
IOmcu without its page. With no IOmcu answering, or before the first read
of a page, the cells follow the settings: Sensor SDA at -1 holds nothing,
nor does the PD mini with either pin at -1, nor a tap on an ADC pin, nor
a part whose row SETUP marks as not taken. A row stepped on SETUP frees
the old pin and holds the new one once the page holds it, with no
restart. A binding that already holds the pin keeps it, as against the
programming port; the IOmcu then refuses the part its pin, and SETUP
marks the row.

![The sensor bus, the PD mini and the phase tap on their pins](img/outputs-iface.png)

### The PROG PORT entry

The protocol list of OUTPUTS ends in one PROG PORT entry per programming port the
IOmcu reports: 4 from protocol 4.26, 1 on an older build. While the count
is not known (no link, the page not read since the link came up) the list
holds one entry, for port 1. An IOmcu older than protocol 4.13 has no
programming port, and the list holds no entry. With more than one the
entries are named `PROG PORT 1`, `PROG PORT 2`, `PROG PORT 3` and
`PROG PORT 4`. A programming port is an
output of its own kind, no slot and no protocol
([Link](Link.md#the-programming-ports)). Every entry has the same rows:

| Row | What it shows and does |
| --- | --- |
| PIN | a button: `PIN GPIO 13`, `SET A PIN` for a port without one, `PIN GPIO 14 REFUSED` for a pin the IOmcu does not hold, `NO PIN KNOWN` before the first read. A tap opens [PICK A PIN](#pick-a-pin) on this port |
| FUNCTION | a choice face with what the pin carries while no programming session holds it. A tap opens the list over the rows under it: SERVO PWM, NONE, and on an IOmcu of protocol 4.28 or later DSHOT150, DSHOT300 and DSHOT600. A tap on an entry asks the IOmcu for it and shuts the list; a press beside the list shuts it and reaches nothing under it. The face is drawn sunk and dim, and takes no tap, while the row is locked |
| WATCHED BY | the INA3221 channel that watches the port in SETUP > SENSORS, `WATCHED BY INA3221 0x41 CH2`: the lowest channel with a shunt on a fitted part; `WATCHED BY NO CURRENT CHANNEL` while none does |
| SERVO TESTER | OUTPUTS \| PORT n: the tester's target ([the target](ServoProgrammer.md#the-target)) |
| PORT PWM | ON \| OFF: the pulse of the port that is the tester's target |

The pins on the right are drawn as the overview names them, the port's
own pin lit; no pin takes a tick there.

| Function | The pin | Pin rule |
| --- | --- | --- |
| SERVO PWM | the servo pulse from the PWM generator, while the bank is ARMED for the servos and PORT PWM is ON on the tester's port; low otherwise | a PWM pin, on no slice of a PWM output and on no compare register of another SERVO PWM port |
| NONE | an input with its pull-down; no pulse | any free header pin |
| DSHOT150, DSHOT300, DSHOT600 | plain DShot at 150, 300 or 600kbit/s, a frame every 1ms while the bank is ARMED for the throttle; no frame otherwise. MOTOR & ESC drives it ([TARGET](#target-names-the-protocol-and-the-pins)) | any free header pin a PIO block reaches; no slice rule |

Port 1 starts as SERVO PWM on GPIO 13, ports 2 to 4 as NONE with no pin. The
IOmcu keeps each port's pin and function in its flash with the outputs'
binding, so they belong to the board, not to the panel. A FUNCTION tap and
a pin tapped in PICK A PIN are asks: the panel writes them on a bench at
rest (SAFE, no test, no sweep, no servo programmer session) and says how
the ask stands in the amber line, `WAITS FOR SAFE, NO TEST, SWEEP OR SERVO
PROGRAMMER`, `THE IOMCU REFUSED IT` or `LINK LOST BEFORE THE WRITE`. A port
that is the tester's target is let go for the write (its pulse off, the
target the outputs) and taken back after it.

![The PROG PORT entry](img/outputs-port.png)

![PROG PORT 2 as DSHOT300, the FUNCTION list open](img/outputs-port-dshot.png)

The amber line under the rows names the first reason that holds:

| Line | When |
| --- | --- |
| `LOCKED: THE SERVO PROGRAMMER HOLDS THE PORT` | a programming session holds a port |
| the ask's line | the last ask for this port waits, was refused or lost |
| `PORTS NOT READ YET` | the page not read since the link came up: FUNCTION takes no tap |
| `LOCKED WHILE ARMED OR A TEST OR SWEEP RUNS` | ARMED, or a test or a sweep runs |
| `SERVO TESTER DRIVES PROG PORT n` | the tester drives another port, port n |
| `THE SERVO TESTER NEEDS FUNCTION SERVO PWM` | the port is NONE or DShot: the tester takes it as no target |
| `IOMCU OLDER THAN 0.22.0: SERVO PWM ONLY` | the IOmcu has no FUNCTION: every port is SERVO PWM and the row takes no tap |
| `SERVO TESTER DRIVES PROG PORT n` | the tester drives this port, port n: FUNCTION takes no tap |
| `PORT PWM NEEDS PROG PORT AS THE TESTER'S TARGET` | PORT PWM on a port that is not the tester's target |

![PROG PORT 2, NONE, its ask waiting](img/outputs-port2.png)

The tester's two segments do not write a page from this screen: the
settings are kept in NVS, and the panel takes them to the IOmcu on a bench
at rest. Picking the entry writes nothing, and LAST WRITE is not shown on
it.

SERVO PWM and MOTOR PWM run on the PWM slices, and the IOmcu refuses
two bindings there ([Servo PWM](DShot.md#servo-pwm)). Under those two
entries the screen holds such a pin, as it holds a pin of another protocol:
grey, no tick, and the pin it clashes with named under its name.

| Cell | Line after a tap on it | Rule |
| --- | --- | --- |
| `SLICE OF GPIO 13` | `GPIO 28: SHARES PWM SLICE WITH PROG PORT GPIO 13` | the pin shares a slice with a SERVO PWM programming port's pin while the port holds it, checked against every port the IOmcu reports; GPIO 13 and GPIO 28 are slice 6. A NONE or DShot port's pin is under no slice rule |
| `PWM CH OF GPIO 0` | `GPIO 16: SAME PWM CHANNEL AS GPIO 0` | the pin shares a compare register with a pin bound as SERVO PWM or MOTOR PWM: GPIO 0 and GPIO 16, GPIO 1 and GPIO 17, GPIO 2 and GPIO 18, GPIO 4 and GPIO 20, GPIO 5 and GPIO 21, GPIO 6 and GPIO 22 |

The line stands under the protocol, in amber, until the next pick, the next
tick that lands, the next visit to the screen or a change of board. A line
on the protocol's own budget (`ALL 8 SLOTS IN USE` and the others) comes
first. DShot and PPM run on PIO (programmable I/O), not on a slice, and are
offered both pins.

Limitation: the slice rule needs the programming port's pin, which the panel
reads from the IOmcu. Until that reading arrives, up to 500ms after
the link comes up or the port's pin changes, GPIO 28 is offered, and a tap on
it ends in `LAST WRITE: REFUSED`. Other IOmcu checks, such as the
SERVO page's frame rate on a pin shared with MOTOR PWM, also refuse with no
reason on this screen.

The pad number under each pin is the one printed on the board, so an operator
counting pads and an operator reading GPIO (general-purpose input/output)
numbers arrive at the same pin.

Ticking or unticking a pin writes the binding at once. There is no APPLY key:
a screen holding a choice that has not been sent is a screen that disagrees
with the bench, with nothing to say which of the two is driving. The panel
then reads both pages back and shows what the IOmcu holds. What became
of the write is under the protocol, as LAST WRITE:

| LAST WRITE | Meaning |
| --- | --- |
| `NOT WRITTEN` | nothing has been written since the panel started, or a write was not sent because the binding on the screen was not one a read confirmed |
| `WRITTEN` | the IOmcu acknowledged every exchange of the write |
| `NO LINK` | an exchange of the write got no answer within 1000ms, or the link was down |
| `REFUSED` | the IOmcu answered and refused: a pin it cannot bind, a change while the bench is ARMED, or a frame rate it could not put back |

To an IOmcu speaking protocol 4.10 the binding is written whole: the
two pages are prepared beside the ones in force and taken together by one
commit ([Link](Link.md#a-binding-taken-whole)). `NO LINK` and `REFUSED` then
leave the binding in force exactly as it was, except when the one
acknowledgement of the commit is lost: the binding is then in force, the
screen says `NO LINK`, and the read at the next link-up shows it. An older
IOmcu takes the pages one entry at a time, and a write that stops part
way leaves the entries before it in force.

Picking a protocol in the list writes nothing. It says which set of pins the
screen shows as ticked and which set the next tick joins. Nothing is dropped:
the pins ticked for the protocol being left stay bound, and the pins of the
one arrived at come back as they were. A selector that retargeted the current
pins would make binding a second protocol mean unbinding the first. On entry
the list shows OFF, the overview. A protocol picked stays selected across
read-backs of the same board. The list on PICK A PIN selects the same
protocol.

`DSHOT600 BIDIR` and `DSHOT300 BIDIR` are the longest entries, 14 characters:

![The longest protocol name in the list](img/outputs-bidir.png)

A pin another protocol holds is drawn greyed, with that protocol's name under
it where a free pin shows its pad number. That is a choice, undone by going to
that protocol and unticking it there — unlike a reserved pin, which is drawn
red and struck through because it is the wiring rather than a choice.

Four servo leads and an ESC, with DSHOT300 the protocol being edited. GPIO 5 is
ticked; GPIO 0, GPIO 1, GPIO 2 and GPIO 4 say SERVO PWM and cannot be ticked here; GPIO 3 and
GPIO 8 to GPIO 12 are red because the IOmcu reserves them:

![Outputs with pins held by another protocol](img/outputs-held.png)

So a cell is in one of four states, and each says what to do about it: ticked
in this protocol, held by another and named, reserved and struck through, or
free and showing its pad number.

### The overview: OFF

Every visit from SETUP opens with OFF selected; a TARGET row opens the
screen on what its workbench drives. OFF is the first entry of
the list and binds nothing: it takes 0 pins, so no pin count is shown under
it. With OFF selected, every bound pin shows the name of the
protocol that holds it in full-brightness text, so the whole binding and
every pin the IOmcu drives are on one screen. No pin can be ticked or
unticked: a tap on a pin does nothing and writes nothing, and the line under
the list says `CHOOSE A PROTOCOL, THEN TAP PINS`. Opening the screen writes
nothing. Picking a protocol in the list leaves the overview, and picking OFF
goes back to it; neither writes or unbinds anything.

OFF stays selected until another entry is picked: across read-backs,
repaints, a link that goes down and comes back, and leaving the screen. A
change of board selects OFF, whatever was selected: an IOmcu that
answers for the first time during the visit, or one swapped for another
board. A protocol picked before is not carried to the board that answers,
so the next tap writes nothing. A press on a pin draws no pressed face.
PICK A PIN opens with OFF selected too.

One write is reachable with OFF selected. With pages that describe no
binding, the line reads `NO VALID ASSIGNMENT IN THE PAGES`, and holding
`HOLD 2s TO CLEAR ALL PIN ASSIGNMENTS` writes a binding with no pin. That
hold is the way out of those pages
([A binding that is not confirmed](#a-binding-that-is-not-confirmed)).

GPIO 0 bound as DSHOT600 BIDIR, GPIO 1 as MOTOR PWM, GPIO 2 and GPIO 13 as SERVO PWM,
with OFF selected. A slot holds GPIO 13 on this bench, so the programming port
has no pin:

![OFF shows the protocol of every bound pin](img/outputs-off.png)

With SERVO PWM picked on the same bench, GPIO 2 and GPIO 13 are ticked and show
`PAD 4` and `PAD 17`: a pin ticked in the selected protocol shows its pad
number, because the protocol is the one named in the list.

### A binding that is not confirmed

The screen edits a binding only after reading it. A read gives one of four
results:

| Read | The screen |
| --- | --- |
| both pages read and they describe a binding, one with no pin bound included | shows it and takes edits |
| a page did not read: no answer within 1000ms, a refusal, or the link is down | keeps the last binding read, dimmed, with `ASSIGNMENT NOT READ. EDITING LOCKED.` |
| both pages read and no binding describes them: one pin in two slots, a rate or driver no protocol entry has, a pin off the board or reserved, channels out of slot order | keeps the last binding read, dimmed, with `NO VALID ASSIGNMENT IN THE PAGES` and the key `HOLD 2s TO CLEAR ALL PIN ASSIGNMENTS` |
| the IOmcu is a board this build has no pin map for | shows no pins |

In the second and third state a tap on a pin does nothing here and on PICK
A PIN, and no write is sent. A tick made just before the read failed is
taken back: the pins shown are the ones last read, not the ones last
tapped. The list still opens, and a pick still changes which protocol's pins
show their pad. The panel reads again at every link-up and every 500ms while
the link is up and the binding is not confirmed; the first read that gives a
binding ends the state.

![The last binding read, after a read that failed](img/outputs-unread.png)

Pages no binding describes do not become readable by reading them again.
`HOLD 2s TO CLEAR ALL PIN ASSIGNMENTS`, held, writes a binding with no pin; the
read-back then shows nothing bound and edits are taken again. It is the one
write the screen sends in this state. A finger that leaves the key, a lift
before 2s, and leaving the screen abandon the hold.

![Pages no binding describes](img/outputs-odd.png)

An IOmcu older than 4.10 can be left in this state by a write that
lost a frame part way. An IOmcu speaking 4.10 written by this panel is
not; it shows this state only for pages another host or an older panel left
in its flash.

### Pick a pin

Behind the PICK A PIN tile on SETUP, and its home tag leads back there.
Over the open protocol list the home tag closes the list, choosing
nothing, and the screen stays.
The same binding the
Outputs screen holds. The list answers "which GPIO is bound"; this answers
"where do I put the lead".

![The pin picker](img/picker.png)

The keys are not the pads. At any size that fits a 1024 x 600 panel a pad is
under 40 pixels across, which is smaller than a fingertip, so the pads are
drawn where they are and the touching happens on two staggered rows of keys
above the board and two below, 50 x 56px (7.5 x 8.4mm), each on a
straight trace to its own pad. The board is drawn 600px wide and at most
240px tall, to scale: the RP2350-CAN's 51 x 21mm outline draws at
582 x 240px. Two keys in one row stand two pads apart, 8px clear of each
other, and the lower row ends at body y 490, above the alert band. The
protocol list's face (280 x 56px), the count and the reason are top left;
the PINS table is under them, 171px wide, rows of 36px with the pin name
in 16 x 28; IN USE is on the right, 168px wide. Both tables stand 8px
clear of the outermost key a 600px board can have, a pad on its very edge
included. The protocol list opens as on Outputs: the protocols in a column
of 48px rows, the PROG PORT entries in a second column beside it.

Each visit draws one of 8 cat drawings, picked at random, bottom left or
bottom right where no row, key or list comes within 4px of it; it takes no
taps.

Current limitation: the head stays where it is whatever the board. On a
board whose shape page puts a pin in the top-left corner of its outline,
the keys of the upper rows reach as far left as x 187, under the protocol
list's face (x 8 to 288), the count and the reason (to x 358). Such a key
is drawn over them, and a press on its part over the face opens the
protocol list instead of taking the pin. The RP2350-CAN's pads in that
corner are rails and ground, so none of its keys lies there
(`the_built_in_boards_keys_clear_the_head` in `test_picker_layout`).

A key's colour says what a tap on it does:

| Key | Pin | A tap |
| --- | --- | --- |
| ivory (row nearer the board) or ebony (row beyond it) | free | binds it to the selected protocol |
| accent | bound in the selected protocol | gives it back |
| grey | another protocol holds it, the programming port has it, the selected protocol can take no more pins, or the binding is not confirmed | changes nothing |
| none, red cross on the pad | reserved by the IOmcu: GPIO 3 heartbeat, GPIO 8 to GPIO 12 CAN | — |

Neighbouring keys differ in height and in colour, like the keys of a
keyboard. A reserved pin has no key because a key under a pin that cannot be
chosen says it could be.

Top left is the protocol list, the same control as on the Outputs screen. It
names the selected protocol. A tap opens it over the board; a pick selects
the protocol a tap on a key joins, on this screen and on the Outputs screen,
and writes nothing. A tap outside the open list closes it and picks nothing.
Every visit opens with OFF selected, on this screen and on the Outputs
screen from SETUP: no key binds or unbinds a pin until a protocol is picked, and the
line under the list says `CHOOSE A PROTOCOL, THEN TAP PINS`. OFF stays
selected across read-backs until a protocol is picked, and a change of
board selects OFF, whatever was selected.

![The protocol list on the picker](img/picker-protocol.png)

Under the list is `N OF 8 SLOTS IN USE`, counted over every protocol: the
protocols share the 8 slots of the OUTPUTS page. Under that, in amber, is
the reason no key can be added, in the words of the Outputs screen:

| Line | When |
| --- | --- |
| `ALL 8 SLOTS IN USE` | 8 pins are bound over all protocols |
| `NEEDS 8 CHANNELS, 4 FREE` | PPM, which carries 8 channels on one pin, with 4 servo pins bound |
| `SERVO PWM TAKES 8 PINS` | the selected protocol holds as many pins as it takes |
| `CHOOSE A PROTOCOL, THEN TAP PINS` | OFF is selected |
| `NOT READ` | the binding is not confirmed |

Without the line a board drawn all grey reads as a fault.

![Eight of eight slots in use](img/picker-full.png)

Down the left, under PINS, are the selected protocol's pins in channel
order, each with the pad number printed on the board in the PAD column. Down
the right, under IN USE, are the pins other protocols hold, each group named
and listed where its first pin falls. The two tables together read as one
run of channels, because that is what the OUTPUTS page carries. With OFF
selected the PINS table is not drawn, because OFF holds no pin; the slot
count and IN USE stay, and IN USE then lists every bound pin.

The pin of the IOmcu's programming port, GPIO 13 (pad 17) by default, is
drawn grey while no protocol holds it, a tap on it changes nothing, and IN
USE lists it first under `PROG PORT`, without a channel. Every port the
IOmcu reports, at most 4, holds its pin so, each listed once under the
one `PROG PORT` name, and the slice rule below is checked against each.
A protocol whose stored binding holds that pin keeps it, the IOmcu gives
the port no pin then, and the key is that protocol's like any other.

The list ends in the PROG PORT entries, as on the Outputs screen, and the
two screens show one selection: OUTPUTS' PIN button opens this screen on
its port. With a port picked the keys set that port's pin:

| Key | A tap |
| --- | --- |
| the port's pin, accented | asks for no pin |
| a free key | asks for that pin |
| grey, under another port's name (`PROG PORT 2`), a protocol's or an interface's | changes nothing |
| grey, `SLICE OF GPIO 4` or `PWM CH OF GPIO 12`, on a SERVO PWM port | changes nothing; the line says `GPIO 5: SHARES PWM SLICE WITH OUTPUT GPIO 4` or `GPIO 28: SAME PWM CHANNEL AS GPIO 12` |

The count line names the port's function, `FUNCTION: SERVO PWM`, and PINS
holds the port's one pin with its pad. A NONE port takes any free key. An
ask waits for a bench at rest and says so in the amber line, as on the
Outputs screen. Before the ports are read every key is grey and the line
reads `PORTS NOT READ YET`.

![PROG PORT 3 picked](img/picker-port.png)

Under SERVO PWM and MOTOR PWM a key the IOmcu refuses on a PWM slice
is grey too, as on the Outputs screen: GPIO 28 while the port holds GPIO 13, and
the partner of a pin bound as PWM, GPIO 16 beside GPIO 0. A tap on it changes
nothing and puts the Outputs screen's line under the count:
`GPIO 28: SHARES PWM SLICE WITH PROG PORT GPIO 13`, `GPIO 16: SAME PWM CHANNEL AS GPIO 0`.

![GPIO 13 held by the programming port](img/picker-prog.png)

A pin SETUP > INTERFACES holds is grey the same way while no protocol holds
it, a tap on it changes nothing, and IN USE lists it after the programming
port, under `SENSOR BUS`, `PD MINI` or `PHASE TAP` in red, without a
channel. Which pins those are, and when, is in the table under
[Outputs](#outputs). A row stepped on SETUP, or the IOmcu taking or
refusing a part, redraws the keys and the table on the next frame.

![The interfaces' pins held and named under IN USE](img/picker-iface.png)

IN USE lists every pin it holds. A protocol's name takes a 20px line and a
pin a 28px line. When those run past the foot of the table, every pin line
is 20px with the GPIO in the smaller face, then 16px, then the names 16px
too. The tallest table of protocols is OFF selected, the programming port
listed and the 8 slots over the 6 protocols that take one channel a pin: 16
lines, 392px of the 432px under the heading. With 4 programming ports and
every interface on beside that binding, the table lists 27 lines, the ports
and the interfaces first, in the smaller lines. A table that still runs past
the foot holds the lines that fit and ends in `+N`, N the lines left out.

![The tallest IN USE table](img/picker-off.png)

A tap on a key writes the binding through the same sequence as a tick on the
Outputs screen, with the same results. While the binding is not confirmed — a
read failed, or the pages describe no binding — every key is grey, `NOT
READ` stands under the count, and a tap on a key changes nothing and writes
nothing. The protocol list still selects.

Where the pads are comes from the board, not from the panel: the [shape
page](Link.md#page-map) carries the outline, the pitch and the corner pad 1
sits at. A board that does not say is not drawn at all — a picture from a
guessed shape points at the wrong pad as confidently as the right one, and
this screen's whole job is finding a pad on the board in front of you. Its
pins are still on the Outputs screen.

The board's drawing comes from the board as well. The IOmcu carries a
drawing of itself, the one the configurator draws: the outline, the parts as
flat shapes and every pad, to scale in 0.01mm ([The board
drawing](Link.md#the-board-drawing)). The panel draws it on the shape's pad
grid, so the board on screen is laid out like the board in your hands.
Without a drawing, the outline and every pad are drawn from the shape and
the keys are in the same places:

![The picker without a drawing](img/picker-drawn.png)

The panel shows a drawing only when it describes the board the panel binds:
the outline and every pad position match the shape, every pin pad carries
the catalogue's GPIO, and every other pad the catalogue's ground or rail. A
drawing that disagrees is not shown, and the shape is drawn instead.

The drawing of the RP2350-CAN is 1,560 bytes, fetched over the link in 26
blocks of 62 bytes in a 15ms slice of each 50ms poll. The time this takes
on a bench is not measured. The panel keeps it in its flash with the
drawing's length, CRC (cyclic redundancy check) and hash, and shows the kept
copy only while the IOmcu still reports the same three; an IOmcu
flashed with another drawing is fetched again. Writing it to flash waits for
a bench that is SAFE with the supply off and pauses between two 4kB sectors
when the bench is ARMED or the supply comes on
([Safety](Safety.md#deliberate-behaviours)).
A board with no drawing, or one whose transfer has not finished, is drawn
from the shape rather than left blank.

A servo lead has three wires and the keys describe one. The other two are
marked on the board itself, on a short trace in from the pad:

| Mark | Pad |
| --- | --- |
| white round marker, 36px, with a large **G**, 40px in from the pad | ground |
| amber round marker, 32px, with the voltage: **5V0**, **3V3** | a rail at a fixed voltage |
| amber round marker with **PWR** | a rail that is an input, VSYS: a number that is only sometimes true is worse here than none |
| dim cross on the pad | neither, like RUN and 3V3_EN |

The marks go inside the outline because that is the only room left: the
space beside the board belongs to the keys, and a mark on the pad itself
would be as small as the pad. Rails sit on neighbouring pads, 29px apart,
so a rail's marker is 30px or 78px in from its pad, alternating along the
row; the deeper one clears the marker of a ground on the pad beside it.

These come from the [pads page](Link.md#page-map), which is separate from the
catalogue because they do not fit in it — a page is 32 registers and a
40-pad board has more pads than that between the two. A board that does not
serve it has its grounds and rails unmarked, and a lead is placed by reading
the board rather than the screen.

### The IOmcu keeps it, not the panel

A binding describes wiring, and the panel is not the board the wires are in.
The IOmcu writes the OUTPUTS and CHAN_CFG pages to its own flash and
restores them at boot; the panel never stores a binding and never sends one
unasked. When the link comes up the panel reads the page and shows what is
configured over there. After a panel restart, what this screen shows and what
is driving pins are the same thing.

Restoring configures the outputs. It does not drive them: every driver is
gated on the pin's role being ARMED, which the IOmcu grants only while
the ARM register names that role, the link is out of failsafe and the heartbeat is trusted, so
a restored binding claims its pins and holds them at idle until somebody switches to ARMED.
Channel commands are not restored — a configuration survives a power cycle and
a throttle position does not.

The save waits for the bench to stop driving, and then for a gap in the
traffic. Writing flash stops the IOmcu with interrupts off, and it
answers nothing while it is stopped: on the bring-up module a save that
erased and programmed inside one window measured 19,178us, against a CAN
frame of about 130us and two frames of buffer in the controller. The figure is one
measurement: 1 sample, taken on the bring-up module, with the flash part not
yet identified. The erase and the page program have not been timed apart from
each other. The window is to be argued again against the flash part's
datasheet maximum once the part is known. A request lost
in that window costs the panel 1000ms of waiting, which is past the
IOmcu's 200ms silence failsafe, so one lost frame ends as `FAULT 01`
(`LINK_FAULT_LINK_SILENT`) over a cable with nothing wrong with it.

So the sector is not erased per save. Two sectors hold sixteen records each; a
save writes the next record, and a sector is erased only once every record in
it has been superseded. That erase is taken before the save that needs it: at
boot before the IOmcu starts answering, or on a pass after the save that
first writes into the other sector, once the bus has been quiet for 5ms. It
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

Pages the screen cannot describe — one pin in two slots, a rate no entry
offers, a pin that is not on the header — are shown as such and not as
nothing configured: see [A binding that is not
confirmed](#a-binding-that-is-not-confirmed).

## Balancing

Described on its own page: [Balancing](Balance.md).

The BALANCE pane shows a fixed angle (265°) and two fixed readings (0.42g
vibration, 0.35g mass to add) until a sensor is fitted. The SIMULATED pill and
the words "Not measured." stand under the readings, 20px high and 88px wide.
The TEST RIG and AIRCRAFT panes show placement guides and carry no mark.

BALANCE, TEST RIG and AIRCRAFT are tabs of 150 x 48px. The rotor's card is
640px wide, its disc 160px in radius; the right card is 360px wide.
PROPELLER and EDF are 162 x 48px, the blade count's `-` and `+` 56 x 48px,
the count's tap area 114 x 48px, the four readings rows of 36px. The notes
and the rules' two lines are in 8 x 16; the rules' heads, the labels and the
drawings' callouts in 10 x 20.

## Unknown screen ids

A screen id that names no screen draws the overview.
