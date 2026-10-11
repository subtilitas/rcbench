# KST and Chaservo servo programmer

<sub>**English** · [Deutsch](ServoProgrammer-de.md)</sub>

The programmer reads the 32 registers of a KST or Chaservo servo over its
signal wire, shows them as settings, and writes changed settings back. The
two brands share one protocol and one register set. It is an overlay on the
[SERVO screen](Screens.md#servo), opened with **PROGRAM** at the top of the
right card. **PROGRAM** ends a running automatic test as `stopped by the
operator`.

**No frame of this code has been sent to a servo.** The programmer has run
against a servo and line model in the host suite only. The register layout
and the timing come from 1 programming card and 1 servo of unidentified
model. The effect of a register on the servo's motion is not measured: the
names, units and limits are those the vendor tool displays and accepts.
[What is open](#measurements-still-open) lists what a bench has to measure
before a write is relied on.

![The programmer with the tester holding the outputs](img/servo-kst.png)

## Constraints

- The coprocessor runs protocol 4.12 or later. With an older one the overlay
  says `Not available: the coprocessor firmware has no KST port.` and every
  control but **CLOSE** is off.

  ![A coprocessor without the port](img/servo-kst-nopage.png)

- The channel is bound as SERVO PWM (pulse-width modulation) on OUTPUTS. A
  throttle channel is refused.
- One servo at a time, on one channel.
- A channel is in PWM or in programming, never both. While the port holds a
  channel, the tester sends no pulse to it.
- The bring-up hardware does not switch a servo's supply. The way back to
  PWM is to unplug the servo.
- Whether an output of a given hardware carries the servo's reply back to
  the pin is not known to the coprocessor. A servo on an output that does not
  ends **CONNECT** with `No servo answers. Check plug, supply and channel.`

## The port

| State | Line on the SERVO tab | The channel carries |
| --- | --- | --- |
| PWM | `The tester has the outputs. CONNECT takes channel 1.` | the tester's pulses |
| Connecting | `Connecting on channel 1: no PWM on it.` | no pulse for 50 ms, then low for 100 ms, then the entry sequence |
| Programming | `Channel 1 is in programming mode: no PWM on it.` | frames only |
| Working | `Working: 12 frames.` | frames of the running operation |
| Leaving | `Leaving programming mode.` | nothing until the port has handed the channel back |

**CHANNEL** opens the keypad for 1 to 16 and is off while the port holds a
channel.

![The channel keypad](img/servo-kst-channel.png)

**CONNECT** takes the channel out of PWM, runs the entry sequence and reads
all 32 registers. The button reads **ABORT** while an operation runs and
**LEAVE** in programming mode.

![An operation running](img/servo-kst-working.png)

While the port holds a channel the overlay stays open and **SETTINGS** is
off. **ARM**, **CENTRE**, **RELEASE**, the supply's controls and **STOP**
on the right card work. Leaving the SERVO screen leaves the port as it is;
the overlay is open again at the next entry.

## SERVO tab

![A servo read](img/servo-kst-read.png)

| Row | Button | Does |
| --- | --- | --- |
| CHANNEL | the number | the output channel, 1 to 16 |
| PORT | **CONNECT**, **ABORT**, **LEAVE** | takes the channel, ends the running operation, [gives the channel back](#leaving) |
| SERVO, then SERVO V6 or SERVO V8 | **READ ALL** | reads the 32 registers, each until 3 of at most 5 reads agree; the row then names [the servo type](#the-servo-type) |
| FILES | **CHOOSE** | [saves the servo's values to the card and loads a file](#servo-files) |
| RESTORE | **CHOOSE** | [writes a backup back](#restore) |
| MORE | **CHOOSE** | [VERIFY, DISCARD EDITS, RELEASE PAIRING](#more) |
| ENABLE WRITE TO SERVO | **OFF**, **ON** | [the write switch](#the-write-switch) |
| CHANGES | **WRITE n VALUES** | [writes the changed values](#writing) |

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
- when a read fails the fingerprint.

With the switch off the panel sends no write, no restore and no pairing
release, and the coprocessor refuses one that arrives without the switch's
key. Reading, **VERIFY**, saving to a file and loading a file work with
the switch off.

## Writing

**WRITE n VALUES** counts the fields in which the draft differs from the
servo. It is off without a change or with the switch off.

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
   without one.
2. The panel records the image the servo holds, the image the write is to
   leave and the channel: with a card in `KST.TXT`, without one in memory.
   The page says which. With a card that takes no file within 3 s the write
   is not sent: `The card took no file. Nothing was written to the servo.`
3. The panel sends the target image. The coprocessor checks it against the
   same limits, plans the register writes and runs them.
4. Every register written is read back. A write that does not take is
   undone.
5. `3 values written and verified.` ends it and clears the record.

Only registers whose value changes are written. Register 00 is never
written; register 1D is written by **RELEASE PAIRING** only.

![A write done](img/servo-kst-written.png)

| Message | The servo |
| --- | --- |
| `A write did not take. Nothing is half written.` | holds what it held |
| `A write did not take. The value is back as it was.` | holds what it held |
| `A write did not take and could not be undone.` | is in doubt: [restore](#an-interrupted-write) |
| `A register changed that was not written.` | is in doubt |
| `The servo does not hold the expected values.` | is in doubt |

A message about a failed write names the register and the count of writes
done: `Register 03, 1 writes done.`

## Backups

| Backup | Held by | Lives until |
| --- | --- | --- |
| Of the session | the coprocessor: the first image read after **CONNECT** | **LEAVE**, a restart of the coprocessor |
| The session's file | the card: the servo's 32 registers as they stand before the session's first write. None while no card is mounted | the file is removed from the card |
| Before a write | the panel, in `KST.TXT` or in memory | the write ends verified, or the record is discarded |

`KST.TXT` is the record of a write under way: text of at most 400 bytes in
the card's root folder with the line `RCBENCH KST 1`, the lines
`journal=`, `target=` and `channel=`, and `check=` with the CRC-16 (cyclic
redundancy check) of the bytes before it. A file whose check fails is not
used: `KST.TXT on the card is not readable. It is not used.`

The panel writes the text to `KST.NEW`, and the file takes the name
`KST.TXT` once the card holds it whole. A write that the card or the supply
ends leaves `KST.TXT` as it was or a whole `KST.NEW`. At power-on a
`KST.NEW` whose check holds is used and renamed; one whose check fails is
removed and `KST.TXT` is used. A file the card does not take is written
again every 5 s until the card takes it, so that a record discarded on the
panel does not stay on the card. Not run on a card.

## Servo files

A servo file is a text file on the panel's SD card with the 32 registers of
one servo. **FILES** opens the list. It is active in programming mode with
a servo read and not in doubt; otherwise the tap answers `Read the servo
first: READ ALL.` Saving and loading work with the write switch off: a file
changes the draft on the panel, never the servo.

![The files on the card](img/servo-kst-files.png)

The first entry saves. Below it stand the files, 5 a page, each with its
servo type; a file without a type line shows none. With more than 5 files
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
next free preset name, `KST001` to `KST999`. The name is edited freely.
**OK** saves; the card's side checks the name and never replaces a file.

![The name of a new file](img/servo-kst-name.png)

| Result | Shown |
| --- | --- |
| Saved | `Servo values saved to the card: WING-R.` |
| A character, a length or a name the card's side does not allow | the keyboard stays open under `NAME NOT ALLOWED`; `Name not allowed. Use A-Z, 0-9, - and _.` |
| A file has the name | the keyboard stays open under `NAME EXISTS: CHANGE IT`; `A file has this name. No file is replaced.` |
| No card | `No card. Nothing saved, nothing written to the servo.` |
| Card full | `Card full. Nothing saved, nothing written to the servo.` |
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
this session: KST003.` A save by hand of the same image counts as that
file. A read that finds other values makes the next write save again.

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
| A card is mounted later in the session | the next write, restore or pairing release saves the backup file first. The file holds the session's backup, the image read before the first write, while the coprocessor holds it; without it the file holds the servo as last read. The line on the SERVO tab changes to the file's name |
| A save through **FILES** after a write without a card | saves the servo as it stands; it does not count as the backup file |
| The card leaves the slot while a page that promises the file is open | the hold sends nothing and the page comes back with the three lines; its own hold of 2 s sends the write |
| The card leaves the slot after the backup file is saved | later writes of the session are sent; the record of the write is kept in memory |
| **FILES**, save and load without a card | refused: `No card. Nothing saved, nothing written to the servo.` |

### Loading

A tap on a file loads it. Its editable fields become the draft, exactly as
edits typed on the tabs: changed values are marked, **WRITE n VALUES**
counts them, and nothing reaches the servo before the write switch, the
write page and its 2 s hold.

![A file loaded](img/servo-kst-file-loaded.png)

| File | Shown |
| --- | --- |
| Saved by the bench, checksum intact | `KST001 loaded: 3 values differ from the servo.` |
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
locked values differ: locked, not written.` The LOCKED tab marks each such
value with `!`. A file without a type line that holds the other type's
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
| `Backup of this session, as first read` | every register that differs, by the coprocessor from the image it holds itself; without the limit checks, and the page says so | nothing |
| `Values before the interrupted write` | the editable fields, as a write with every limit check | the locked fields, register 00 and the pairing value |

A restore from the panel's image is an ordinary write: the coprocessor
accepts an unchecked restore only of the backup it holds itself, and that
check is not weakened. The page counts the locked values that differ and
stay: `2 locked values differ and stay as they are.` A servo file is not
in this list: [a load](#loading) makes it the draft, and the ordinary
write sends it.

A backup whose values pass a soft limit that the servo's own values keep
opens CHECK THIS BACKUP first. The page lists the warnings of those limits,
the same lines an edit past them shows, at most 5; with more than 5 the
fifth line counts the rest. **HOLD TO GO ON** for 2 s opens the restore
page, which needs a press and a 2 s hold of its own. The restore of the
session's backup is checked the same way while the servo's values are known.

![The warnings of a backup](img/servo-kst-restore-check.png)

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
servo is read, with the channel of the write below it: `Do not use it.
Connect it on channel 4 and READ ALL.` A record found at power-on sets
**CHANNEL** to that channel. Every control but **CONNECT**, **READ ALL**,
**RESTORE** and **LEAVE** is off. The read compares the servo with the
record:

| The servo holds | Message | Then |
| --- | --- | --- |
| the image before the write | `The servo holds what it held before the write.` | the record is cleared |
| the image the write was to leave | `The servo holds the values of the interrupted write.` | the record is cleared |
| anything else | `The servo holds a mix of old and new values.` `Do not use this servo. Restore the backup: RESTORE.` | **RESTORE** offers the session's backup while the session lives, and `Values before the interrupted write` |

![The servo holds a mix](img/servo-kst-journal.png)

`Discard the record of the interrupted write` in the restore list removes
the record without a write.

The record belongs to one servo. A servo read on another channel, or one
that differs from the record in a value that neither the write nor an edit
changes, is not that servo: `This is not the servo of the interrupted
write.` `It was on channel 4. RESTORE: discard the record.` Nothing is
restored, edited or written while that stands, and **RESTORE** holds the
discard entry only. **LEAVE** and a **CONNECT** of the write's servo bring
the comparison above. Two servos on the same channel whose locked values
are equal are not told apart.

![Another servo than the record's](img/servo-kst-other.png)

Max. Duty lies in registers 01 and 02. A write cut between those two
leaves an image that breaks a hard limit, and the panel's restore, which is
a checked write, does not start from such an image. The restore of the
session's backup does. After a restart of the coprocessor in that state the
programmer reads the servo and writes nothing to it.

## Leaving

**LEAVE**, or **CLOSE** in programming mode, opens LEAVE PROGRAMMING. A
servo that received a frame stays in programming mode until its supply was
off, and the port does not give the channel back to PWM before it is told
so.

![Leaving](img/servo-kst-leave.png)

On hardware that does not switch the servo's supply, the bring-up hardware
included: unplug the servo for 10 s, then hold **HOLD: SERVO WAS
UNPLUGGED** for 2 s. The hold is the operator's statement; the bench does
not measure that the servo was without power. On hardware whose port
reports a switched supply the button reads **HOLD TO SWITCH THE SUPPLY**:
the port switches it off for 6 s and waits 3 s. No such hardware has run
this code.

The page warns before changed values are lost and before a session ends
that could still restore a servo in doubt. Afterwards `The tester has the
output again.` and the tester's controls drive the channel as before.

## Stops and losses

| Event | Effect |
| --- | --- |
| **STOP**, a latched stop of the bench | the running operation ends after the frame on the wire, at most 12 ms; the switch goes off; `STOP: the operation was aborted.`; the port asks for **READ ALL** before anything else |
| **ABORT** | the same, without the latch |
| The link goes | the switch goes off, open pages close; a write under way is [interrupted](#an-interrupted-write) |
| The coprocessor starts again | its session and backup are gone; `The coprocessor restarted. CONNECT and READ ALL.` |
| The panel's control task takes no command for 2 s | nothing is sent; `No result from the coprocessor. READ ALL.` |
| 3 exchanges with the coprocessor in a row stay unanswered | `No answer from the coprocessor. READ ALL.`; after a read or a verify the values on the panel are not edited, saved or written before a **READ ALL** that ends |
| No result of an operation for 30 s | the operation is aborted; `No result from the coprocessor. READ ALL.`; the values on the panel are not edited, saved or written before a **READ ALL** that ends; a write is [interrupted](#an-interrupted-write) |

Every refusal of the port and every result of the session has its own
line. [The link page](Link.md#the-kst-programming-port) lists the codes.

## Limitations

- Untested on a servo. Every statement about a servo's behaviour on this
  page is the model's.
- No save, list or load of a servo file has run on a card. The panel
  firmware of this branch answers every save with a write error and
  every list and load with a read error until the servo file module is
  wired to the card. With a card mounted a session's first write waits
  for its file, so this firmware sends a write to a servo only while no
  card is mounted.
- A write without a card leaves no backup file. The session's backup is
  then the only copy of the servo as found and ends with **LEAVE**, a
  coprocessor restart or a power loss of the bench.
- A session's first write with a mounted card needs free space on it.
- A servo file carries the servo type, V6 or V8, and no other identity.
  A file of one V8 servo loads into any V8 servo.
- A servo file restores the editable fields only:
  [what it does not restore](#what-a-file-does-not-restore).
- The list shows 5 files a page and has no search and no sort of its own;
  the order is the card side's. Files are not removed or renamed on the
  panel.
- The record of an interrupted write names its channel and holds the
  servo's values. A second servo on that channel with the same locked
  values is taken for the first.
- The replacement of `KST.TXT` through `KST.NEW` is not run on a card.
- Without a card the record of a write is lost at power off.
- The record of an interrupted write found at power-on is shown in the
  programmer only; no other screen announces it.
- A servo whose image breaks a hard limit as read can be read and saved.
  A write to it is refused.
- LEFT ANGLE and RIGHT ANGLE are the vendor tool's numbers; their unit is
  not known.
- The supply voltage a servo needs in programming mode is not known.

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
   programming mode.
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

`test_kst_prog` runs the model and `test_kst_panel` the overlay, both
against the coprocessor's port and the servo model through the link's
dispatcher: every limit at its edge, the 2 s holds on both sides of the
threshold, every refusal and result with its line in both languages, a
write cut by a link loss, a restart, **ABORT** and **STOP**, a card that
fails, and the timeouts across the 2^32 ms wrap of the clock. The servo
files run against a card model: every name result, every kind of file,
both servo types and their mismatch in both directions, every refusal,
the 5 s limit, a first write with a card that is full or failing, and the
writes without a card: the first write, a restore and a pairing release,
a hold released early, a card mounted between two writes and a card
removed under the write page.
