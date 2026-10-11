# Servo programmer for KST and Chaservo servos

<sub>**English** · [Deutsch](ServoProgrammer-de.md)</sub>

The programmer reads the 32 registers of a KST or Chaservo servo over its
signal wire, shows them as settings, and writes changed settings back. The
servos of the two brands share one protocol and one register set. It is an
overlay on the [SERVO screen](Screens.md#servo), opened with **PROGRAM** at
the top of the right card. **PROGRAM** ends a running automatic test as
`stopped by the operator`.

KST and Chaservo are names or trademarks of their respective owners. rcbench
is independent: it is not affiliated with, endorsed by or sponsored by them.
The names say which servos the programmer works with.
[Legal notice and disclaimer](../README.md#legal-notice-and-disclaimer) holds
the project's notice.

**No frame of this code has been sent to a servo.** The programmer has run
against a servo and line model in the host suite only. The register layout
and the timing come from 1 Tool Card #2, KST's programming card, and 1 KST
servo of unidentified model. No Chaservo servo has been read. The effect of
a register on the servo's motion is not measured: the names, units and
limits are those the vendor tool displays and accepts.
[What is open](#measurements-still-open) lists what a bench has to measure
before a write is relied on.

![The programmer with the tester holding the port](img/servo-kst.png)

## Constraints

- The coprocessor runs protocol 4.13 or later. With an older one the overlay
  says `Not available: the coprocessor has no programming port.` and
  `Update the coprocessor to version 4.13 or later.`, and every control but
  **CLOSE** is off.
- **LEAVE** with its wait needs protocol 4.15 on the coprocessor. An older
  one is [named and handled without it](#a-coprocessor-older-than-link-version-415).

  ![A coprocessor without the port](img/servo-kst-nopage.png)

- The servo is on the coprocessor's programming port, port 1: GP13, pad 17
  of the bring-up module, unless another pin is stored for it. The port is
  no output channel and no slot of OUTPUTS.
- The SERVO screen's **PWM TARGET** is **PROG PORT**
  ([the target](#the-target)). With the standard outputs as target the
  programmer is locked.
- One servo at a time.
- The port is in PWM (pulse-width modulation) or in programming, never both.
  While the port is in programming, the tester sends no pulse to it.
- Programming and an armed bench are exclusive. **CONNECT** on a bench that
  is armed, asked to arm or has a finger on **ARM** is refused with
  `DISARM first: the port is not taken on an armed bench.` While the port
  is held, from **CONNECT** until it reports PWM, no screen arms the bench:
  **ARM** on SERVO and on MOTOR & ESC is drawn disabled and takes no press,
  and a run of the ESC stick programmer does not start and says
  `Servo programmer holds a port: LEAVE on SERVO first.` An arm that
  reaches the bench another way is refused with the alert
  `no arm: the servo programmer holds a port`. **DISARM** and **STOP**
  work in every state. On an armed bench the read, the verify, the write,
  the restore and the pairing release are refused with the same line as
  **CONNECT**; **LEAVE** is taken.
- With a PD mini answering, the port sets the servo's supply at **CONNECT**
  ([the supply](#the-supply)). Without one the supply is not the bench's to
  set, and the programmer takes it as it is.
- The programmer assumes that a servo needs no power cycle: neither to take
  a written value into effect nor to follow PWM after programming. Neither
  is measured ([limitations](#limitations)).
- Whether a given hardware carries the servo's reply back to the pin is not
  known to the coprocessor. A servo on a port that does not ends **CONNECT**
  with `No servo answers. Check plug, supply and port.`

## The target

The tester of the SERVO screen drives the standard servo outputs or the
programming port. **SETTINGS**, tab DUT, holds the two rows:

| Row | Values | Does |
| --- | --- | --- |
| PWM TARGET | **OUTPUTS**, **PROG PORT** | what the sweep, **CENTRE**, **RELEASE**, the hold of a position and the automatic test drive |
| PORT PWM | **ON**, **OFF** | whether the programming port renders the tester's pulse while it is the target |

Both rows are kept with the SERVO settings. **PWM TARGET** changes only
with the bench disarmed, no sweep and no test running, and the port not in
programming; the panel releases a held position before it asks the
coprocessor for the other target. **PORT PWM** goes on under the same
rule and goes off with the port not in programming. With **OUTPUTS** as
target **PORT PWM** is locked.

While the programmer has the port neither row changes: from **CONNECT** to
the port back in PWM after **LEAVE**, during every operation, and for as
long as the coprocessor reports the port out of PWM. Both rows are drawn
locked, a reset of the settings leaves the two values, the panel writes
nothing to the coprocessor's page of the port, and the tester sends the
port no command and no release. A release owed to the port goes out once
the port is back in PWM.

A port without a pin cannot be the target: a slot of OUTPUTS that holds
GP13 leaves the port none. The tester then keeps the standard outputs,
whatever the row says.

With the standard outputs as target the programmer shows `Programming needs
the programming port.` and `SERVO screen, DUT tab: set PWM TARGET to PROG
PORT.`, and every control but **CLOSE** is off.

![The standard outputs are the target](img/servo-kst-target.png)

## The port

| State | Line on the SERVO tab | The port carries |
| --- | --- | --- |
| PWM, a PD mini answers | `The tester has port 1. CONNECT sets 5.0 V, 1000 mA.` | the tester's pulses |
| PWM, no PD mini | `The tester has port 1. No PD mini: supply as it is.` | the tester's pulses |
| Connecting | `Connecting on port 1: no PWM on it.` | no pulse for 50 ms, then low for 100 ms, then the entry sequence |
| Programming | `Port 1 is in programming mode: no PWM on it.` | frames only |
| Programming, the supply read | `Port 1 in programming mode, no PWM: 5.00 V, 37 mA.` | frames only |
| Working | `Working: 12 frames.` | frames of the running operation |
| Working, the coprocessor plans | `Planning the order of the writes. No frame sent yet.` | nothing: a coprocessor of link version 4.14 plans a write or a restore before its first frame |
| Leaving | `Leaving programming mode. PWM is not on yet.` | the line low for **WAIT BEFORE PWM**; no pulse until the port reports PWM |

**CONNECT** takes the port out of PWM, sets the supply where a PD mini
answers, runs the entry sequence and reads all 32 registers. The button
reads **ABORT** while an operation runs or a leave waits, and **LEAVE** in
programming mode.

![An operation running](img/servo-kst-working.png)

While the port is in programming the overlay stays open, **SETTINGS** is
off and the tester's controls send nothing to the port. **STOP** on the
right card works. Leaving the SERVO screen leaves the port as it is; the
overlay is open again at the next entry.

| **CONNECT** ends with | Meaning |
| --- | --- |
| `The programming port holds no pin. Nothing was sent.` | a slot of OUTPUTS holds the port's pin |
| `The coprocessor has no such programming port.` | the coprocessor has fewer ports than the one asked for |
| `Programming voltage or current is over the SUPPLY limits.` and `Lower it under MORE, or raise the limit. Nothing was set.` | PROGRAMMING VOLTAGE is above the SUPPLY screen's voltage limit, or CURRENT LIMIT above its current limit; nothing is sent |
| `DISARM first: the port is not taken on an armed bench.` | the bench is armed or asked to arm; the coprocessor refuses the same on its own with `The port is busy. Try again.` |
| `The supply cannot give this voltage or current limit.` | the voltage is above 8.4 V, the current limit outside 50 to 3000 mA, or the voltage less than 0.5 V under the PD mini's input; the supply is not changed |
| `Supply under 4.8 V. Programming needs 4.8 V or more.` | the programming voltage, or the voltage of an output that is on, is under 4.8 V; the supply is not changed |
| `The supply did not switch within 3000 ms. Check it.` | the PD mini did not read as asked; the port is back in PWM, and an output this **CONNECT** switched on is off again |
| `The coprocessor does not trust the link yet. Try again.` | the coprocessor does not trust the heartbeat, or the link was silent |

![A port without a pin](img/servo-kst-nopin.png)

## The supply

With the port in the tester's hands, **MORE** opens what **CONNECT** sets.

![What CONNECT sets](img/servo-kst-options.png)

| Entry | Range | Default | Does |
| --- | --- | --- | --- |
| PROGRAMMING VOLTAGE | 4.8 to 6.0 V in 0.1 V; up to 8.4 V with HV SERVO on | 5.0 V | the supply voltage of the servo in programming mode |
| CURRENT LIMIT | 50 to 3000 mA | 1000 mA | the supply's current limit during programming |
| WAIT BEFORE PWM | 0 to 10 000 ms | 100 ms | the time the port holds its line low between **LEAVE** and the first pulse |

Each entry opens the number keypad with its range. A value outside the range
is not taken. All three are kept with the SERVO settings.

**CONNECT** switches an output that is off on at the two values, and the
session starts 1000 ms after the PD mini reads on. For an output that is
on, the session runs at the lower of the two voltages and the lower of the
two current limits: a voltage above the programming voltage is lowered to
it, a current limit above CURRENT LIMIT is lowered to it, and neither is
raised. Leaving programming does not restore a value **CONNECT** lowered.

The port programs at 4.8 V or more. 4.8 V is a chosen value, not a measured
one: the supply voltage a servo needs to store a register is not known. An
output that is on under 4.8 V is not programmed at and stays as it is; the
operator raises it first. A PD mini whose input is under 5.3 V cannot give
4.8 V, and no session starts on it.

While the port is out of PWM the supply's set points are the port's. The
coprocessor refuses every change of the voltage and of the current limit
and every ON; it takes an OFF. The SUPPLY screen and the SET row of the
right card show the same: the set points, the sliders, the steps and
**OUTPUT ON** are dimmed and take no press, **OUTPUT OFF** and
**RESET PEAKS** work, and the MODE card of the SUPPLY screen reads
`servo programmer`. The limits of the SUPPLY screen's settings stay
editable; a limit lowered under a set point reaches the set point when the
port is back in PWM.

An OFF that arrives while a frame is on the wire, from **OUTPUT OFF**,
**STOP** or a lost heartbeat, reads off at once and reaches the PD mini
when the port has cut the frame, 12 ms later at most. The PD mini's own
protection is not delayed. How long the PD mini's output takes to fall
after its off command is not measured.

![The voltage keypad](img/servo-kst-voltage.png)
![The wait keypad](img/servo-kst-wait.png)

A voltage above 6.0 V needs **HV SERVO** on the TEST tab of **SETTINGS**;
without it the keypad ends at 6.0 V. With it, a value above 6.0 V opens the
warning page of the SERVO screen's high-voltage setting, and **HOLD TO
APPLY** takes it after 2 s. Switching **HV SERVO** off sets a programming
voltage above 6.0 V back to 5.0 V.

A voltage above 6.0 V is not stored. The SERVO settings keep 6.0 V for it,
and a stored value above 6.0 V, from the settings page or a settings file,
is loaded as 6.0 V and stored as that, with **HV SERVO** on or off. The
programmer reads the settings when it opens and at every entry of the SERVO
screen while no port is held, and when the panel starts under a held port:
each of these begins at 6.0 V or below, and a voltage above it takes the
warning page and its hold of 2 s again. A session that holds its port keeps
the voltage it began with.

![A programming voltage above 6.0 V](img/servo-kst-hv.png)

With a PD mini answering, the state line names the values before
**CONNECT** and the measured output voltage and current in programming
mode. The SUPPLY screen and the supply rows of the right card follow what
the port set.

![A PD mini answers](img/servo-kst-supply.png)
![The supply in programming mode](img/servo-kst-supplied.png)

Without a PD mini the voltage and the current limit are stored and not
applied. **WAIT BEFORE PWM** is applied at [leaving](#leaving) with and
without a PD mini by a coprocessor of link version 4.15 or later.

## SERVO tab

![A servo read](img/servo-kst-read.png)

| Row | Button | Does |
| --- | --- | --- |
| PORT | **CONNECT**, **ABORT**, **LEAVE** | takes the port, ends the running operation, [gives the port back](#leaving) |
| SERVO, then SERVO V6 or SERVO V8 | **READ ALL** | reads the 32 registers, each until 3 of at most 5 reads agree; the row then names [the servo type](#the-servo-type) |
| FILES | **CHOOSE** | [saves the servo's values to the card and loads a file](#servo-files) |
| RESTORE | **CHOOSE** | [writes a backup back](#restore) |
| MORE | **CHOOSE** | in PWM [what CONNECT sets](#the-supply); in programming mode [VERIFY, DISCARD EDITS, RELEASE PAIRING](#more) |
| ENABLE WRITE TO SERVO | **OFF**, **ON** | [the write switch](#the-write-switch) |
| CHANGES | **WRITE n VALUES**, **WRITE 1 VALUE** | [writes the changed values](#writing) |

Below the rows stand the port's state, the servo type, the name of the
session's backup file and the last message. A message in the warning colour is a refusal or a result
that needs a second look; one in the danger colour says the servo's content
is in doubt.

### A servo of unknown layout

The coprocessor compares the image with the layout of the known servo, its
fingerprint. An image that fails is shown and never written: the edit rows
are off, a tap on the write switch leaves it off and repeats the message,
and **RELEASE PAIRING** is refused. **READ ALL**, a save to a file and
**VERIFY** work. A file is not loaded.

![A servo that fails the fingerprint](img/servo-kst-readonly.png)

## Editing

A value is edited in a draft on the panel. Nothing reaches the servo before
[**WRITE n VALUES**](#writing). A value that differs from the servo's is
marked twice: by `*` before it and by the accent fill.

![Edited values on TUNE](img/servo-kst-tune.png)

A numeric value opens the keypad. Its range is the range the hard limits
accept in the draft as it stands, so a value outside cannot be entered.
STRETCH opens a list of the 6 confirmed factors. An ON/OFF value flips on
the tap. DEAD BAND and NEUTRAL have minus and plus keys: one raw count a
tap, and while held 8 counts a second after 0.45 s and 30 a second after a
further 1.8 s.

![The keypad with the range the limits accept](img/servo-kst-keypad.png)
![The stretch list](img/servo-kst-stretch.png)

### TUNE

| Row | Register field | Hard limit |
| --- | --- | --- |
| DEAD BAND | Dead Band, µs | the field's range |
| MAX. DUTY | Max. Duty, % | 10 to 98 % |
| BOOST | Boost, µs | at most 3000 µs |
| STRETCH | Stretch, factor | x1 to x32; the 2 remaining codes are not offered |
| SOFT START | Soft_Start, ON or OFF | none |
| SPEED LIMIT | spd_sel, ON or OFF | ON needs SPEED at 5 % or above |
| SPEED | SPD, % | at most 100 %; at least 5 % while SPEED LIMIT is ON |

### PULSE

![The PULSE tab](img/servo-kst-pulse.png)

| Row | Register field | Hard limit |
| --- | --- | --- |
| NEUTRAL | Neutral, µs | 1360 to 1640 µs; 100 µs or more from either end point |
| PULSE LOWER | Pulse Lower, µs | 500 µs or more; 100 µs or more below NEUTRAL; 400 µs or more below PULSE UPPER |
| PULSE UPPER | Pulse Upper, µs | 2500 µs or less; 100 µs or more above NEUTRAL |
| LEFT ANGLE | Left Angle, the vendor tool's number | not 0 |
| RIGHT ANGLE | Right Angle, the vendor tool's number | not 0 |
| REVERSION | Reversion, ON or OFF | none |

The NEUTRAL conversion between register and µs is a fit to 3 values and
uncertain by about 1 µs.

### FAILSAFE

![The FAILSAFE tab](img/servo-kst-failsafe.png)

| Row | Register field | Hard limit |
| --- | --- | --- |
| MOVE ON SIGNAL LOSS | Allow Uncont, ON or OFF | none |
| POSITION | Uncontrolled Pos, µs | 500 to 2500 µs and within PULSE LOWER to PULSE UPPER |
| AFTER | Uncontrolled Time, s | 0.4 to 13.0 s |

The limits are those of `protocols/kst/kst_limits.c`; the panel holds none
of its own. The lower limit of MAX. DUTY and the 5 % of SPEED are choices,
not readings of a servo.

### A value a hard limit refuses

A tap that would break a hard limit changes nothing and says which limit.

![A refused switch](img/servo-kst-hard.png)

### A value that needs a second look

A value inside the hard limits that breaks a soft limit opens CHECK THIS
CHANGE with the old and the new value and one line per limit. **HOLD TO
APPLY** takes it after 2 s of holding; **CANCEL**, or a finger that slides
off, leaves the draft as it was.

![A soft limit](img/servo-kst-soft.png)

| Condition | Warning |
| --- | --- |
| MAX. DUTY above the servo's | `Higher duty raises stall current and heat.` |
| MAX. DUTY below 50 % | `Low duty weakens the servo; it may not hold the surface.` |
| BOOST above the servo's | `Higher boost can make the servo buzz and run hot.` |
| STRETCH above the servo's | `Higher stretch can make the servo oscillate.` |
| SOFT START switched off | `Without soft start the servo jumps at power-on.` |
| SPEED LIMIT switched on | `Speed limit active: the servo moves at the set speed.` |
| SPEED below 30 % with SPEED LIMIT on | `A slow servo can lose control of the model.` |
| DEAD BAND below the servo's | `A narrow dead band can cause hunting and heat.` |
| DEAD BAND below 1.0 µs | `A very narrow dead band: hunting and heat are likely.` |
| MOVE ON SIGNAL LOSS switched on | `On signal loss the servo moves to the failsafe position.` |
| POSITION more than 300 µs from NEUTRAL, MOVE ON SIGNAL LOSS on | `Failsafe position is far from centre.` |
| AFTER below 1.0 s | `Short dropouts will trigger the failsafe move.` |
| an end point less than 300 µs from NEUTRAL | `End point close to neutral: little travel on that side.` |
| an end point more than 100 µs from the servo's | `End point changed: travel per stick movement changes.` |
| NEUTRAL more than 20 µs from the servo's | `Centre moves; one side comes closer to the stop.` |
| REVERSION changed | `Direction is reversed. Check every surface before flight.` |
| an angle above the servo's | `More travel can drive the servo into its stop and burn it.` |
| an angle below 50 | `Small angle: little travel for the full stick.` |
| DEAD BAND below 1.0 µs with STRETCH above x1 | `Narrow dead band with raised gain: risk of oscillation.` |
| MAX. DUTY above 90 % with an angle above the servo's | `High duty with more travel: a stall can destroy the servo.` |

"The servo's" is the value last read from the servo.

### LOCKED

![The LOCKED tab](img/servo-kst-locked.png)

| Row | Field |
| --- | --- |
| TYPE | 20k_sel, shown as [the servo type](#the-servo-type), V6 or V8 |
| LOCK | Lock |
| MODE | Servo or Gyro Servo |
| PROT1 EN, PROT1 CANCLE, PROT1 TIME, PROT1 PWM, POT ERR, POT SAMPLES, PWM ERR | the protection block the vendor tool hides; `PROT1 CANCLE` is the vendor's spelling |

These values are shown and no edit changes them. Their function or their
encoding is not confirmed. The tab also shows the pairing value, register
1D, and the card number the vendor tool needs for this servo. A servo in
Gyro Servo mode is read and not written: the limits of that mode are not
known.

## The write switch

**ENABLE WRITE TO SERVO** is off by default. A tap on **OFF** opens a page
that names the risks; **HOLD TO ENABLE** switches it on after 2 s. A tap on
**ON** switches it off at once.

![Enabling the write](img/servo-kst-enable.png)

The switch goes off:

- at every entry of the SERVO screen,
- when the link to the coprocessor goes or the coprocessor starts again,
- at **STOP** and at every stop the bench latches,
- with every **CONNECT** and every **LEAVE**,
- when a read finds another image than the one held,
- when a read fails the fingerprint,
- when a write, a restore, a read or **VERIFY** ends with the servo in
  doubt: the read that follows does not switch it on again,
- when a read finds a servo that fits the record of an interrupted write.

With the switch off the panel sends no write, no restore and no pairing
release, and the coprocessor refuses one that arrives without the switch's
key. Reading, **VERIFY**, saving to a file and loading a file work with
the switch off.

The switch takes its tap with a servo read. A session the coprocessor has
locked after an interrupted write takes it with no servo read as well, once
the panel holds the session's backup: this is the state of a panel that
starts under such a session. The switch then opens one write, the restore
of the session's backup. An edit, a write of values, a pairing release and
a load of a file stay refused until a read and the end of the lock.
**READ ALL** in that state reads the servo and shows its values; the lock
stays, and no value is edited.

## Writing

**WRITE n VALUES** counts the fields in which the draft differs from the
servo; with one such field it reads **WRITE 1 VALUE**. It is off without a
change or with the switch off.

![Three changed values, the switch on](img/servo-kst-changes.png)

A tap opens the write page. **HOLD TO WRITE** starts the write after 2 s.
The first write after a **CONNECT** shows `Horn free, no linkage attached.`
as its first line. A write that the card, a stop or the link kept from
starting is not that first write: the line stays for the next one.

![The write page](img/servo-kst-write.png)

1. The first write of a session saves the servo's values to
   [a new file on the card](#the-backup-before-the-first-write). A card
   that is mounted and does not take the file ends the write. With no card
   mounted the write page says that no file is saved, and the write goes
   without one. While the record of an interrupted write is held no file is
   saved: the servo may be half written, and the record holds its values as
   found.
2. The panel records the image the servo holds, the image the write is to
   leave and the port: with a card in `KST/KST.TXT`, without one in memory.
   The page says which. With a card that takes no file within 3 s the write
   is not sent: `The card took no file. Nothing was written to the servo.`
3. The panel sends the target image. The coprocessor checks it against the
   same limits, plans the register writes and runs them.
4. Every register written is read back. A write that does not take is
   undone.
5. `3 values written and verified.`, or `1 value written and verified.`,
   ends it and clears the record.

A value written and read back is taken as in effect. The programmer asks
for no power cycle after a write, and its verify is the read-back of the
same session. Whether a KST servo acts on a written value before its next
power-up is not measured.

Only registers whose value changes are written. Register 00 is never
written; register 1D is written by **RELEASE PAIRING** only.

![A write done](img/servo-kst-written.png)

| Message | The servo |
| --- | --- |
| `No safe order of writes leads to these values.` | holds what it held; nothing was sent |
| `Too many changes to plan at once. Nothing was written.` `Split the change: write fewer values at a time.` | holds what it held; nothing was sent. The planner stops after 216,000 units of work without a verdict. The session goes on and the pending values stay |
| `A write did not take. Nothing is half written.` | holds what it held |
| `A write did not take. The value is back as it was.` | holds what it held |
| `A write did not take. The writes before it stay. READ ALL.` | holds the registers written before the one that failed, and the old value in the others. The second line names the register and the count of writes done. The write switch goes off, and **READ ALL** comes before the next write |
| `A write did not take and could not be undone.` | is in doubt: [restore](#an-interrupted-write) |
| `A register changed that was not written.` | is in doubt |
| `The servo does not hold the expected values.` | is in doubt |

A message about a failed write names the register and the count of writes
done: `Register 03, 1 write done.`

## Backups

| Backup | Held by | Lives until |
| --- | --- | --- |
| Of the session | the coprocessor: the first image read after **CONNECT** | **LEAVE**, a restart of the coprocessor |
| The session's file | the card: the servo's 32 registers as they stand before the session's first write. None while no card is mounted | the file is removed from the card |
| Before a write | the panel, in `KST/KST.TXT` or in memory | the write ends verified, or the record is discarded |

`KST.TXT` is the record of a write under way: text of at most 400 bytes in
the card's folder `KST` with the line `RCBENCH KST 1`, the lines
`journal=`, `target=` and `port=`, and `check=` with the CRC-16 (cyclic
redundancy check) of the bytes before it. A file whose check fails is not
used: `KST.TXT on the card is not readable. It is not used.` The name `KST`
is the record's: no servo file takes it, and the list of files leaves it
out.

The panel writes the text to `KST.NEW`, and the file takes the name
`KST.TXT` once the card holds it whole. A write that the card or the supply
ends leaves `KST.TXT` as it was or a whole `KST.NEW`. At power-on a
`KST.NEW` whose check holds is used and renamed; one whose check fails is
removed and `KST.TXT` is used. A file the card does not take is written
again every 5 s until the card takes it, so that a record discarded on the
panel does not stay on the card. Not run on a card.

## Servo files

A servo file is a text file in the folder `KST` of the panel's SD card with
the 32 registers of one servo, named `<name>.TXT`. The panel creates the
folder with the first save. **FILES** opens the list. It is active in programming mode with
a servo read and not in doubt; otherwise the tap answers `Read the servo
first: READ ALL.` Saving and loading work with the write switch off: a file
changes the draft on the panel, never the servo.

![The files on the card](img/servo-kst-files.png)

The first entry saves. Below it stand the files sorted by name, 5 a page,
each with its servo type; a file without a type line shows none. The list
reaches the first 64 files by name; a card with more shows these and counts
no others. With more than 5 files
the title counts them, `FILES 1 TO 5 OF 7`, and `Show the next files` turns
the page, back to the first after the last. The panel asks the card for
each page and waits at most 5 s for it.

![The second page of 7 files](img/servo-kst-files-next.png)
![A card without servo files](img/servo-kst-files-none.png)

### The servo type

A servo and a file are V6 or V8. The 20k_sel field decides: 1 is V6, a
20 kHz drive; 0 is V8, a 333 Hz drive. No other register enters the type.
Once a servo is read, the **SERVO** row reads `SERVO V6` or `SERVO V8` and
the line below the rows says `Servo type V6: 20 kHz drive.` The type is
never edited and never written.

![A V8 servo and a V6 file](img/servo-kst-file-v6.png)

### Saving

`Save this servo's values as a new file` opens the text keyboard with the
next free preset name, `KST001` to `KST999`. The keyboard takes up to 16
characters: A to Z, 0 to 9, `-`, `_` and the full stop. **OK** saves; the
card's side checks the name and never replaces a file. Names compare
without case. A file with a longer name, up to 32 characters, is listed and
loaded; a line that names it shows its first 14 characters and `..`.

![The name of a new file](img/servo-kst-name.png)

| Result | Shown |
| --- | --- |
| Saved | `Servo values saved to the card: WING-R.` |
| A full stop at the start or the end, or a reserved name: `KST`, `CON`, `PRN`, `AUX`, `NUL`, `COM1` to `COM9`, `LPT1` to `LPT9`, alone or in front of a full stop | the keyboard stays open under `NAME NOT ALLOWED`; `Name not allowed: reserved, or a full stop at an end.` |
| A file has the name | the keyboard stays open under `NAME EXISTS: CHANGE IT`; `A file has this name. No file is replaced.` |
| No card | `No card. Nothing saved, nothing written to the servo.` |
| Card full, or `KST001` to `KST999` all taken at the backup before a write | `Card full. Nothing saved, nothing written to the servo.` |
| The card does not take the file, or does not answer within 5 s | `Card write error. Nothing written to the servo.` |

![A name that exists](img/servo-kst-name-exists.png)
![A name that is not allowed](img/servo-kst-name-bad.png)
![A file saved](img/servo-kst-name-saved.png)
![No card](img/servo-kst-card-none.png)
![A full card](img/servo-kst-card-full.png)

A save writes the image as read, the locked fields included, also of a
servo that fails the fingerprint.

### The backup before the first write

The first write, restore or pairing release of a session saves the servo's
image to a new file under the next free preset name, without asking. The
write page says `The servo's values go to a new file on the card first.`
The servo receives nothing before the card holds the file. A mounted card
that is full, that fails the write or that leaves the slot between the
request and the file ends the write with the lines of the table above, and
nothing is written to the servo. Freeing space on the card or removing the
card lets the write go. The SERVO tab then names the file: `Backup file of
this session: KST003.` A save by hand before the first write of the
session counts as that file. A save by hand after a write holds the written
values and leaves the line as it is. A read that finds other values makes
the next write save again.

A servo in doubt is the exception: its image is not worth a file, and the
restore of the session's backup runs without one.

#### Without a card

With no card mounted the write, the restore and the pairing release are
not blocked. The page whose hold sends the write carries three more lines:

```
No card: no backup file of the servo is saved.
The backup of this session is held in the bench only.
It is gone when the session ends or the bench loses power.
```

The page's one hold of 2 s confirms them; no second hold is asked for. A
hold released before 2 s sends nothing.

![The write page without a card](img/servo-kst-write-nocard.png)

After such a write the SERVO tab shows `No backup file: no card. Session
restore only.` in place of the file's name. The only copy of the servo as
found is the session's backup in the coprocessor: **LEAVE**, a restart of
the coprocessor and a power loss of the bench end it, and the values the
servo held before the write are then not recoverable from the bench.

![The SERVO tab after a write without a card](img/servo-kst-nofile.png)

| Case | Result |
| --- | --- |
| A card is mounted later in the session | the next write, restore or pairing release saves the backup file first. The file holds the session's backup, the image read before the first write. The line on the SERVO tab changes to the file's name |
| The link goes and comes back, or the panel starts, under a session the coprocessor keeps | the writes of the session are not known to the panel. The next write, restore or pairing release saves the backup file first, and the file holds the session's backup, not the servo as last read. A save through **FILES** counts as the backup file only while the servo equals the session's backup |
| The servo may hold written values and the panel does not hold the session's backup | the write goes without a file, and its page carries the three lines above: the servo as last read is not saved as the backup file |
| The coprocessor starts again | its session and the session's backup are gone. **CONNECT** begins a session, and its first write saves the servo as that session finds it |
| A save through **FILES** after a write without a card | saves the servo as it stands; it does not count as the backup file |
| The card leaves the slot while a page that promises the file is open | the hold sends nothing and the page comes back with the three lines; its own hold of 2 s sends the write |
| The card leaves the slot after the backup file is saved | later writes of the session are sent; the record of the write is kept in memory |
| **FILES**, save and load without a card | refused: `No card. Nothing saved, nothing written to the servo.` |
| A card put in after the panel started | the panel tries to mount a card when the programmer opens and at each tap on **CONNECT**, **READ ALL**, **FILES**, **RESTORE**, **MORE**, **ENABLE WRITE TO SERVO** or the write row while none is mounted. The tap that finds the card still reads no card; the next one has it, and an open write page changes to the page with the file |

### Loading

A tap on a file loads it. Its editable fields become the draft, exactly as
edits typed on the tabs: changed values are marked, **WRITE n VALUES**
counts them, and nothing reaches the servo before the write switch, the
write page and its 2 s hold.

![A file loaded](img/servo-kst-file-loaded.png)

| File | Shown |
| --- | --- |
| Saved by the bench, checksum intact | `KST001 loaded: 3 values differ from the servo.`, or `KST001 loaded: 1 value differs from the servo.` |
| Edited, with a type line and no checksum line | the same, and `Edited file, values checked as manual input.` |
| Edited, without a type line | the same, and `Edited file without servo type, checked as manual input.` |
| Holds the servo's editable values | `KST001 holds the servo's values: nothing to write.` |
| Of the other servo type | `File is V8, servo is V6. Not loaded.` |
| Checksum line present and wrong | `File damaged: the checksum does not fit. Not loaded.` `Without its checksum line it loads as an edited file.` |
| Not a servo file | `Not a servo file. Not loaded.` |
| A line that does not read | `Line 12 of the file is not readable. Not loaded.` |
| Written by a later format version | `The file needs a later bench version. Not loaded.` |
| A value outside its hard limit | `Line 9: MAX. DUTY outside its limit. Not loaded.` |
| Values that break a hard limit together | `The file breaks a limit and is not loaded:` and the limit's line |
| The card does not read, or does not answer within 5 s | `The card does not read.` |
| No servo file on the card | `No servo files on the card.` |

![A file of the other type](img/servo-kst-file-type.png)
![A damaged file](img/servo-kst-file-damaged.png)
![An edited file](img/servo-kst-file-edited.png)
![An edited file without a type line](img/servo-kst-file-untyped.png)
![A file with the servo's values](img/servo-kst-file-same.png)
![A line that does not read](img/servo-kst-file-bad.png)
![A file of a later format](img/servo-kst-file-newer.png)
![A value outside its hard limit](img/servo-kst-file-hard.png)

A file whose values pass a soft limit that the servo's own values keep
opens CHECK THIS FILE before anything becomes the draft. The page lists
the same warning lines an edit past those limits shows, at most 5; with
more the last line counts the rest. **HOLD TO LOAD** for 2 s takes the
values into the draft. **CANCEL** leaves the draft as it was.

![The warnings of a file](img/servo-kst-file-soft.png)

### What a file does not restore

A load writes the editable fields only. These stay as the servo holds
them, whatever the file says:

- the servo type, 20k_sel,
- Lock,
- the mode, Servo or Gyro Servo,
- the protection block of the LOCKED tab,
- the pairing value, register 1D,
- register 00 and every bit that belongs to no named field.

A file that differs there is loaded, and the difference is counted: `2
locked values differ: locked, not written.`, or `1 locked value differs:
locked, not written.` The LOCKED tab marks each such value with `!`. A file without a type line that holds the other type's
20k_sel is loaded the same way, since nothing but the type line denies a
load.

![Locked values that differ](img/servo-kst-file-locked.png)

The restore of the session's backup is the one way to write a locked
register, and it ends with the session.

## Restore

**RESTORE** opens the list of what there is to restore. Every restore needs
the write switch and a 2 s hold.

![The restore list](img/servo-kst-restore-list.png)
![A restore of the session's backup](img/servo-kst-restore.png)

| Source | Written back | Not restored |
| --- | --- | --- |
| `Backup of this session, as first read` | every register that differs, by the coprocessor from the image it holds itself; without the limit checks, and the page says so. A register that differs and that no write of the session changed ends the restore before its first write | nothing |
| `Values before the interrupted write` | the editable fields, as a write with every limit check | the locked fields, register 00 and the pairing value |

A restore from the panel's image is an ordinary write: the coprocessor
accepts an unchecked restore only of the backup it holds itself, and that
check is not weakened. The page counts the locked values that differ and
stay: `2 locked values differ and stay as they are.`, or `1 locked value
differs and stays as it is.` A servo file is not
in this list: [a load](#loading) makes it the draft, and the ordinary
write sends it.

A backup whose values pass a soft limit that the servo's own values keep
opens CHECK THIS BACKUP first. The page lists the warnings of those limits,
the same lines an edit past them shows, at most 5; with more than 5 the
fifth line counts the rest. **HOLD TO GO ON** for 2 s opens the restore
page, which needs a press and a 2 s hold of its own. The restore of the
session's backup is checked the same way while the servo's values are known.

![The warnings of a backup](img/servo-kst-restore-check.png)

The session's backup belongs to the servo it was read from. A read that
differs from the backup in a field no write of the session set out to
change is another servo: `Another servo: the session's backup is not for
it.` `LEAVE, then CONNECT: a session with its own backup.` The write
switch goes off, the restore list does not hold the session's backup, and
no restore of it is sent. **LEAVE** and **CONNECT** begin a session whose
backup is this servo's. A read of the first servo brings its backup back.
A write that the card, a stop or the coprocessor keeps from starting, or
that the coprocessor ends with no register written, counts as no write: its
fields still tell the servos apart. So does a write that is undone and
read back with no write counted done.

The coprocessor makes the comparison again, register by register, on the
read that precedes the first write of a restore of the session's backup.
It holds the set of registers that the writes of the session changed. A
servo that differs from the backup in any other register is not the
backup's servo: the restore ends with `The servo differs from the read.
READ ALL.`, no register is written, the write switch goes off and the port
takes **READ ALL** and nothing else until the servo is read. The restore
therefore does not repair a register that something other than a write of
the session changed. After a write that ended without a full read of the
servo, the registers that differ at the next full read count as changed by
that write.

While a read is owed after a write in doubt and the session is not locked,
the restore list does not hold the session's backup: `Read the servo first:
READ ALL.`

A restore, a pairing release or a write that the card or the panel's
control task keeps from starting leaves the pending values, the write
switch and the record of an interrupted write as they were before the
hold.
Two servos equal in every field the session did not write are not told
apart. After a link loss or a start of the panel under a session the
session's writes are not known, and the comparison is not made.

![Another servo than the backup's](img/servo-kst-swapped.png)

## More

![MORE](img/servo-kst-more.png)

- **VERIFY** reads all registers and compares them with the image held:
  `Verified: the servo holds what was read.` or the count of registers that
  differ.
- **DISCARD EDITS** sets the draft back to the servo's values.
- **RELEASE PAIRING** sets register 1D to 00 after the write switch and a
  2 s hold. What the servo does with the value is not known. A restore of
  the session's backup sets it back.

![Releasing the pairing](img/servo-kst-release.png)

## An interrupted write

A write counts as interrupted when the link goes, the coprocessor or the
panel starts again, **ABORT** or a stop ends it, or no result arrives
within 30 s. The record made before the write then stands, on the card
across a power cycle of the bench.

![An interrupted write](img/servo-kst-doubt.png)

`A write was interrupted. The servo may be half written.` stays until the
servo is read, with the port of the write below it: `Do not use it.
Connect it on port 1 and READ ALL.` Every control but **CONNECT**, **READ
ALL**, **RESTORE** and **LEAVE** is off. The read compares the servo with the
record:

| The servo holds | Message | Then |
| --- | --- | --- |
| the image before the write | `Is this the servo of the interrupted write on port 1?` `It holds the old values. If yes: RESTORE, discard record.` | the record stays until it is discarded |
| the image the write was to leave | `Is this the servo of the interrupted write on port 1?` `It holds the new values. If yes: RESTORE, discard record.` | the record stays until it is discarded |
| anything else | `The servo holds a mix of old and new values.` `Do not use this servo. Restore the backup: RESTORE.` | **RESTORE** offers the session's backup while the session lives, and `Values before the interrupted write` |

![The servo holds a mix](img/servo-kst-journal.png)

`Discard the record of the interrupted write` in the restore list removes
the record without a write.

A servo that holds, register for register, the image before the write or
the image the write was to leave is either the servo of the write or a
second servo with the same settings. The panel cannot tell the two apart
and asks. The two lines stay on the SERVO tab, the write switch does not go
on, and no write, restore or pairing release is sent. If it is the servo of
the write, `Discard the record of the interrupted write` in the restore
list ends the question. If it is not, **LEAVE** keeps the record for the
servo of the write.

The record belongs to one servo. A servo read on another port is not that
servo, and neither is one with a register that holds neither the value of
the image before the write nor the value the write was to leave: `This is
not the servo of the interrupted write.` `It was on port 2. RESTORE:
discard the record.` Nothing is restored, edited or written while that
stands, and **RESTORE** holds the discard entry only. **LEAVE** and a
**CONNECT** of the write's servo bring the comparison above. One register
is exempt: the low register of a field that lies in two registers and that
the write changes, because the plan of a write may set it to a third value
on its way.

The comparison is made when the servo is read and when the record is
loaded: a record from a card mounted with a servo already read is compared
with that servo at once. A restore of the session's backup with no servo
read does not belong to a record that is held: the record stays as it is,
on the panel and on the card. With no record held such a restore makes its
own, with the backup as the image the servo is to hold. A record made
while no card is mounted is written to a card that is mounted later.

![Another servo than the record's](img/servo-kst-other.png)

Max. Duty lies in registers 01 and 02. A write cut between those two
leaves an image that breaks a hard limit, and the panel's restore, which is
a checked write, does not start from such an image. The restore of the
session's backup does. After a restart of the coprocessor in that state the
programmer reads the servo and writes nothing to it.

## Leaving

**LEAVE**, or **CLOSE** in programming mode, asks to end the session and
to return the port to servo PWM. The programmer does this without a supply
cycle: the servo stays powered, a PD mini's output stays on at its voltage
and current limit, and the tester's signal follows on the same port.
Whether a KST servo that received a frame follows PWM before its supply was
off is not measured.

LEAVE PROGRAMMING opens with `The port goes back to servo PWM. The supply
stays as is.` and `The servo gets the tester's signal and may move.` The
page warns before changed values are lost and before a session ends that
could still restore a servo in doubt. **HOLD TO LEAVE** for 2 s sends the
port's LEAVE operation of link version 4.15. No frame goes to the servo and
no supply command to the PD mini. The port holds its line low for
[**WAIT BEFORE PWM**](#the-supply) and then gives the pin to the PWM
generator.

![Leaving](img/servo-kst-leave.png)

The page is the same with a PD mini answering.

![LEAVE with a PD mini](img/servo-kst-leave-supply.png)

The state line reads `Leaving programming mode. PWM is not on yet.` and the
tester's controls stay off until the port reports PWM. The button reads
**ABORT** while the wait runs. Then `The tester has the port again.` and
the tester drives the port at once, with **PWM TARGET** on **PROG PORT**
and **PORT PWM** on. The next **CONNECT** starts a new session with its own
read.

### The wait before PWM

**WAIT BEFORE PWM** is 0 to 10 000 ms, 100 ms by default. The keypad takes
no value above 10 000 ms, and no leave is sent with one. A wait of 0 ms
ends at the coprocessor's next pass. The time left of a running wait is not
shown. The wait a KST servo needs is not measured.

| During the wait | Effect |
| --- | --- |
| **ABORT** | the port stays in programming mode with the session as it was; `Leaving was interrupted before the wait ended.`; **LEAVE** is taken again at once |
| **STOP** or a disarm of the bench, no PD mini answering | the same line; the port stays in programming mode and asks for **READ ALL** |
| The supply's output reads off or is switched off: **STOP** with a PD mini answering, the PD mini's own protection | the same line; the supply stays off for 2000 ms, then the port is in PWM |
| The link goes | as [a loss of the link](#stops-and-losses) |

### A port that is not left

The port refuses a leave while it asks for a read or holds a servo that a
write left in doubt.

| The port | A PD mini answers | Line | Way out |
| --- | --- | --- | --- |
| asks for a read after a stop or an abort | yes | `The port asks for a read first: READ ALL.` | **READ ALL**, then **LEAVE**; or **STOP**, which switches the supply off |
| holds a servo in doubt | yes | `Servo in doubt: the port keeps the session. RESTORE first.` and `STOP ends the session and switches the supply off.` | **RESTORE**, then **LEAVE**; or **STOP** |
| either | no | `The tester has the port again.` and `The session was in doubt: no wait before PWM applied.` | none needed: the port is in PWM |

Without a PD mini **STOP** switches no supply and leaves the port in
programming mode, and a servo that no longer answers takes no read. The
panel then sends the port's POWER_CYCLED operation in place of LEAVE.
Without a PD mini it changes the port's state only: no frame, no wait, and
the servo's supply is whatever feeds it. The record of an interrupted write
stays for the next session.

### A coprocessor older than link version 4.15

Link versions 4.13 and 4.14 have no LEAVE. The panel sends none to them.

| A PD mini answers | The panel | Lines |
| --- | --- | --- |
| no | sends POWER_CYCLED, which changes the port's state only; the port is in PWM without a wait | `The tester has the port again.` and `Coprocessor older than 4.15: no wait before PWM applied.` |
| yes | sends nothing: both operations these versions have for the step cycle the supply. The session stays | `Update the coprocessor to 4.15 to leave with a PD mini.` and `STOP ends the session and switches the supply off.` |

With a PD mini on such a coprocessor, **STOP** or the supply's output
switched off ends the session: the supply stays off for 2000 ms and the
port is in PWM again.

## Stops and losses

| Event | Effect |
| --- | --- |
| **STOP**, a latched stop of the bench | the running operation ends after the frame on the wire, at most 12 ms; the switch goes off; `STOP: the operation was aborted.`; the port asks for **READ ALL** before anything else. A PD mini's output is switched off, and the port is in PWM again 2000 ms after it reads off |
| **ABORT** | the same, without the latch |
| The link goes | the switch goes off, open pages close; a write under way is [interrupted](#an-interrupted-write) |
| The coprocessor starts again | its session and backup are gone; `The coprocessor restarted. CONNECT and READ ALL.` |
| The panel's control task takes no command for 2 s | nothing is sent; `No result from the coprocessor. READ ALL.` The command is withdrawn: the control task drops it when it leaves the queue, however late |
| The coprocessor refuses an exchange of an operation it has taken | the line of the refusal; the values on the panel are not edited, saved or written before a **READ ALL** that ends |
| 3 exchanges with the coprocessor in a row stay unanswered | `No answer from the coprocessor. READ ALL.`; after a read or a verify the values on the panel are not edited, saved or written before a **READ ALL** that ends |
| No result of an operation for 30 s, counted from the last time the port reported the operation running or planning | the operation is aborted; `No result from the coprocessor. READ ALL.`; the values on the panel are not edited, saved or written before a **READ ALL** that ends; a write is [interrupted](#an-interrupted-write) |

Every refusal of the port and every result of the session has its own
line. [The link page](Link.md#the-programming-port-for-kst-and-chaservo-servos) lists the codes.

## Limitations

- Untested on a servo. Every statement about a servo's behaviour on this
  page is the model's.
- No Chaservo servo has been read.
- The record of an interrupted write is read from the card when the panel
  starts, and from a card put in later when the programmer mounts it. A
  record the panel already holds stands, before and after the servo is
  read back: the record on such a card is not read then, and the next
  record written replaces it.
- A card taken out and put back while the panel runs is not mounted again.
- The time a mount attempt without a card holds the screen is not measured.
- Not run on hardware: neither firmware image has run this code with a
  servo, a card or a PD mini.
- The limits and trip settings of the SUPPLY screen are not held while the
  programmer has the port; the set points and **OUTPUT ON** are.
- With the simulated supply selected (no PD mini wired) the panel dims the
  same controls, and the servo's supply is whatever feeds it.
- The rows **PWM TARGET** and **PORT PWM** follow the port's state one
  frame of the screen after the control task does. The control task holds
  the page of the port and the tester's commands from its own reading of
  the port.
- The panel's glue in `firmware/panel/main/main.c` is not in the host
  suite: the command queue between the screen and the control task, the
  point where the control task drops a withdrawn command, the hand-over
  of the port's state to the page and the tester, and the card's file
  calls.
- Whether a KST servo follows PWM while it is in programming mode is not
  measured. The programmer leaves programming without a supply cycle and
  drives the port with PWM afterwards.
- Whether a written value acts before the servo's next power-up is not
  measured. The programmer treats a value that reads back as in effect.
- With a PD mini answering and a coprocessor older than link version 4.15,
  the port is not left from the programmer: **STOP** or the supply's output
  switched off ends the session, with the supply off.
- With a PD mini answering, a port that holds a servo in doubt is left by
  **RESTORE** and **LEAVE**, or by **STOP** with the supply off.
- **WAIT BEFORE PWM** is not applied by a coprocessor older than link
  version 4.15, and not on the leave of a port in doubt without a PD mini.
  The wait a KST servo needs is not measured. The time left of a running
  wait is not shown.
- Without a PD mini the programming voltage and the current limit are not
  applied.
- No save, list or load of a servo file has run on a card.
- The list of files reaches the first 64 files by name.
- A write without a card leaves no backup file. The session's backup is
  then the only copy of the servo as found and ends with **LEAVE**, a
  coprocessor restart or a power loss of the bench.
- A session's first write with a mounted card needs free space on it.
- A servo file carries the servo type, V6 or V8, and no other identity.
  A file of one V8 servo loads into any V8 servo.
- A servo file restores the editable fields only:
  [what it does not restore](#what-a-file-does-not-restore).
- The list shows 5 files a page, sorted by name, and has no search. Files
  are not removed or renamed on the panel.
- The record of an interrupted write names its port and holds the servo's
  values. A second servo on that port whose 32 registers equal the image
  before the write or the image the write was to leave is not told apart
  from the first: the panel asks, and the answer is the operator's.
- The replacement of `KST.TXT` through `KST.NEW` is not run on a card.
- Without a card the record of a write is lost at power off.
- The record of an interrupted write found at power-on is shown in the
  programmer only; no other screen announces it.
- A servo whose image breaks a hard limit as read can be read and saved.
  A write to it is refused.
- LEFT ANGLE and RIGHT ANGLE are the vendor tool's numbers; their unit is
  not known.
- The supply voltage a servo needs in programming mode is not known. The
  floor of 4.8 V is chosen, not measured.

## Measurements still open

None of these is done.

1. The line's electricals on the port's path: levels of bench and servo,
   which side holds the line low at idle, the pull-down, the servo's output
   impedance, and the time both sides drive.
2. The motor's state in programming mode, and whether a written value acts
   at once.
3. What the servo does with a PWM pulse in programming mode, and with a
   frame outside it.
4. Persistence: whether a written value survives a power cycle, and
   whether a commit step exists.
5. Write reliability: 1000 writes with read-back, the count that do not
   take, the count of registers that change unasked, the spread of the
   reply delay.
6. The read error rate on a 1 m lead with other servos moving on
   neighbouring outputs: 100,000 reads.
7. The timing of entry and of exit: how long without power ends
   programming mode, whether a servo follows PWM after programming without
   a power cycle, and the wait it needs before the first pulse.
8. The function of each editable setting on a servo that may be lost:
   position, travel time, stall current, standing current and oscillation
   against the value.
9. The tolerance of the transmit rate around 25.40 µs a half-cell, and the
   shortest gap between frames the servo accepts.
10. A second servo of the same type, and other generations: fingerprint,
    reply timing, half-cell.
11. Open encodings: the NEUTRAL constants, the 2 unconfirmed STRETCH
    codes, the angle fields in Gyro Servo mode, and whether the vendor tool
    enforces the ranges it labels.
12. A servo written here is read by the vendor tool with its card.
13. The model of the servo the layout was taken from.
14. Whether 20k_sel separates the servo generations as the type rule
    assumes: 1 servo is recorded, and no V8 servo has been read.
15. A save, a list and a load on a card: the time each takes against the
    5 s limit, and a card removed during a save.

## Tests

`test_prog_link` runs the panel's side of the target and of the port's
pulse switch: a port without a pin, a target that waits for a bench at
rest, a coprocessor that starts again, and a programmer that has the port
for each of the 16 pairs of page and wanted values. `test_settings` runs
a held setting against every call that changes one, and `test_servo` the
two rows of the DUT tab. `test_kst_card` runs the card's side
of the files on a card held in memory. `test_kst_prog` runs the model and
`test_kst_panel` the overlay, both against the coprocessor's port, a supply
model and the servo model through the link's dispatcher: the supply's
ranges at their ends and one step outside, the wait at 0 ms, 10 000 ms and
10 001 ms as the leave's wait, an abort, a disarm and a supply that goes
off in the wait, a leave on link versions 4.13 and 4.14 with and without a
PD mini, **STOP** during a leave, every limit at its edge, the 2 s holds on both sides of the
threshold, every refusal and result with its line in both languages, a
write cut by a link loss, a restart, **ABORT** and **STOP**, a card that
fails, and the timeouts across the 2^32 ms wrap of the clock. The servo
files run against a card model: every name result, every kind of file,
both servo types and their mismatch in both directions, every refusal,
the 5 s limit, a first write with a card that is full or failing, and the
writes without a card: the first write, a restore and a pairing release,
a hold released early, a card mounted between two writes and a card
removed under the write page. A command the control task takes after
its 2 s have passed reaches no wire, for every kind of operation; a
refusal at each later exchange of each kind of operation asks for
**READ ALL**; every result code of the session has a class, and a code
without one fails the test; a restore that the card keeps from starting
at each file step leaves the pending values; a record from a card mounted
before and after the read, of the same and of another servo.
