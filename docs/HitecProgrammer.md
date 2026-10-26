# Servo programmer for Hitec servos

<sub>**English** · [Deutsch](HitecProgrammer-de.md)</sub>

The programmer reads the settings of a Hitec servo over its signal wire,
shows them, decoded and as stored, and writes edited values back. Before
every write it records the servo's values, on the card where one is
mounted; it makes no backup file. It opens from its row
`Hitec servos` on **PROGRAM** of the [SERVO screen](Screens.md#servo), one
level down, and covers the left column, the strip's tabs with it. **CLOSE**
or the home tag returns to PROGRAM's list while the programmer holds no
port. The row is dimmed with `IOMCU HAS NO SERVO PWM` and opens nothing while
the IOmcu, the RP2350 board, reports no servo PWM (pulse-width modulation),
which includes no IOmcu answering.

Hitec is a name or trademark of its owner. rcbench is independent: it is
not affiliated with, endorsed by or sponsored by it. The name says which
servos the programmer works with.
[Legal notice and disclaimer](../README.md#legal-notice-and-disclaimer) holds
the project's notice.

Two series are read and written:

| Series | Servos | Wire | Read | Written |
| --- | --- | --- | --- | --- |
| D SERIES | D485HW and the other D series servos | 115,200bit/s, inverted | 21 registers | the registers of each changed setting 20ms apart, the save (0x70 = 0xFFFF), 100ms, the restart (0x46 = 1), 1000ms, then the 21 registers read back |
| CLASSIC | HS-5xxx and HS-7xxx | 19,200bit/s | 100 cells: 0x00 to 0x23, 0x30 to 0x53, the name 0x60 to 0x6F, the date 0x70 to 0x73, the serial 0x80 to 0x87, and the version | each changed cell, then the balance in cell 0x1C, each answered `+ +`, 2ms apart; then cells 0x00 to 0x23 read back |

The register model and its encodings come from 1 D485HW (2026-10-20) and
from 1 HS-5645MG, 1 HS-7955TG and 1 HS-5955TG (2026-10-18 to 2026-10-20)
([`protocols/hitec`](../protocols/hitec/README.md)). The page it runs on is
[The programming port for Hitec servos](LinkHitec.md).

![The programmer with the tester holding the port](img/servo-hitec.png)

## Constraints

- The IOmcu runs protocol 4.23 or later. With an older one the SERVO tab
  says `Unavailable: the IOmcu has no Hitec port.` and
  `Update the IOmcu to version 0.21.1 or later.`
- SERVO TESTER on SETUP > OUTPUTS is set to a port, PORT 1, PORT 2,
  PORT 3 or PORT 4, and
  the port names a pin. With OUTPUTS the PORT row reads `NO PROG PORT SET`
  and CONNECT says `Use the programming port to program a servo.`
- The bench is SAFE. CONNECT is refused on an ARMED bench, on one asked to
  arm and with a finger on ARM: `Tap DISARM first. The port cannot be taken
  over while ARMED.` A latched STOP refuses it too:
  `A STOP is latched. It clears with the next switch to ARMED.`
- A classic servo enters programming mode only at power-up, so the bench
  cycles its supply: CONNECT for CLASSIC needs a PD mini that answers,
  otherwise `Classic servos need a PD mini: the bench cycles the supply.`
- A D series servo answers on the line as SETUP > INTERFACES > Hitec D
  receive sets it, PULL-UP or PRECHARGE
  ([Receive mode](LinkHitec.md#receive-mode)). CONNECT waits until the
  IOmcu holds that mode: `Receive mode not set on the IOmcu yet. Try
  again.`
- The programming voltage is the setting the KST programmer uses
  (SET_SERVO_PROG_V, MORE > PROGRAMMING VOLTAGE), 4.8V to 6.0V for a Hitec
  session: a higher setting runs at 6.0V and the SERVO tab says
  `Hitec programming runs at 6.0V or less.` The current limit is 50mA to
  3000mA. Both have to lie inside the SUPPLY limits, or CONNECT says
  `Programming voltage or current exceeds the SUPPLY limits.`

## The SERVO tab

| Row | What it does |
| --- | --- |
| PORT | Two lines: the port and its GPIO with the series, `PROG PORT 1 GPIO 13  D SERIES`, then the receive mode and the supply CONNECT sets, `RX PRECHARGE  5.0V 1000mA` (`SUPPLY AS IT IS` for a D series servo with no PD mini). The button reads CONNECT, ABORT while the port moves into programming or a read runs, WAIT, LEAVE while the port is held, and RELEASE PORT while another session holds it |
| SERIES | D SERIES or CLASSIC, chosen before CONNECT; kept with the SERVO settings (SET_HITEC_SERIES). Not taken while the port is held |
| SERVO | The model read, `D485HW` or `HS-5645MG`; READ ALL reads the series' set again |
| MORE | The programming voltage and current limit, each on the keypad, while the tester holds the port. While the port is held: DISCARD EDITS with an edit standing, and REPAIR CHECKSUM with a classic checksum that is wrong |
| AFTER STOP | CLEAR after a switch to SAFE at the bench: the IOmcu clears the port's STOPPED flag and the read goes on, or the entry again where the supply went off |
| ENABLE WRITE TO SERVO | The write switch, OFF or ON ([The write switch](#the-write-switch)) |
| RESTORE | While a record of an interrupted write stands: the values before it back to the servo, or the record discarded ([The record of a write](#the-record-of-a-write)) |
| WRITE n VALUES | The draft to the servo ([Writing](#writing)); `WRITE 1 VALUE` for one, off with none |

Under the rows the state line says what the port does now, and the last
message stands at the foot of the notes. Every control is 48px tall or more
and ends 4px above the alert band; the geometry is the KST programmer's
([Screens](Screens.md#programmer)).

![A D485HW read](img/servo-hitec-read.png)
![The SERIES list](img/servo-hitec-series.png)
![A read under way](img/servo-hitec-reading.png)

## CONNECT

1. OPEN: the IOmcu takes the port from PWM. No pulse for 50ms, then the line
   is the wire driver's, released.
2. The entry:
   - D series: POWER at the programming voltage with an off time of 0ms
     where a PD mini answers, then 1000ms for the servo to start. Without a
     PD mini the servo runs on the supply as it is.
   - Classic: POWER with the supply off for 500ms, then on, and a LISTEN
     for the servo's `+`; the servo's second `+` follows 250ms to 267ms
     later; 0x67 then reads the version.
3. READ ALL: the series' set, each address with 3 tries.
4. The model is identified: D series by register 0x00 (34485 is the
   D485HW), classic by the name cells. The message names it:
   `D series servo, model 34485, ID 0.` or
   `Classic servo HS-5645MG, version 0x16.` A model the register model does
   not name is read in the layout of the closest one, and the SERVO tab
   says which: `Model not known. The fields are read as on the D485HW.`

In the host model (`test_hitec_prog`) a D series CONNECT takes 2.4s, 1.2s of
it the entry, and a classic one 4.2s at a frame of 16ms and 7.6s at one of
33ms: a classic read takes 2 frames a cell. On the bench these times are
not measured.

While the port is held:

- ARM is drawn disabled. A press on it arms nothing and shows
  `Cannot switch to ARMED: a port is in use by the servo programmer.` in
  the alert band, on SERVO and on MOTOR & ESC alike. STOP works. DISARM has
  nothing to end: the bench cannot be ARMED while the port is held.
- The set points and OUTPUT ON of the SET row and of the SUPPLY screen take
  no press; a press on them says `Servo programming holds the supply: set
  points and ON are locked.` OUTPUT OFF works. From protocol 4.29 the IOmcu
  holds its SUPPLY page from OPEN to CLOSE as well
  ([The supply while a port is held](LinkHitec.md#the-supply-while-a-port-is-held)).
- The KST and Chaservo row and MOTOR & ESC's TARGET row take no press, and
  ESC STICK does not start a run.

## The value tabs

MOTION, TRAVEL, PROTECT and INFO show the draft: what the last full read
found, with the edits not written yet. A value that fits its encoding shows
decoded with the stored value in brackets beside it, `CW (0)`; one that
fits no known encoding shows as `RAW 1234` in the warning colour. A
writable value takes a tap while the port is held and not stopped
([Editing](#editing)).

| Tab | D series | Classic |
| --- | --- | --- |
| MOTION | SPEED, DEAD BAND, SOFT START | SPEED, DEAD BAND, RESOLUTION |
| TRAVEL | NEUTRAL, LEFT END, RIGHT END in servo counts with degrees (220° over 16383 counts, 8192 the centre), DIRECTION | CENTRE in microseconds, LEFT END, RIGHT END as raw values (unit unknown), DIRECTION |
| PROTECT | FAILSAFE (OFF, LIMP, or a position in microseconds), OVERLOAD PROTECTION, SMART SENSOR, SENSITIVITY (raw) | FAILSAFE, FAILSAFE POSITION, OVERLOAD PROTECTION |
| INFO | MODEL, ID, POSITION, and registers 0x04, 0x22, 0x8A, 0x8C, 0xD4 and 0xD6 raw | MODEL, VERSION, DATE, SERIAL, CHECKSUM |

A classic image whose cells 0x00 to 0x1C do not sum to 0 modulo 256 shows
CHECKSUM `WRONG` and the note `Checksum of cells 0x00-0x1C is wrong. The servo
may not follow PWM.`

![TRAVEL of a D485HW](img/servo-hitec-travel.png)
![MOTION of a D485HW](img/servo-hitec-motion.png)
![PROTECT of a D485HW](img/servo-hitec-protect.png)
![INFO of a D485HW](img/servo-hitec-info.png)
![An HS-5645MG read](img/servo-hitec-classic.png)
![TRAVEL of an HS-5645MG](img/servo-hitec-classic-travel.png)
![PROTECT of an HS-5645MG](img/servo-hitec-classic-protect.png)
![INFO of an HS-5645MG](img/servo-hitec-classic-info.png)

## Editing

A value is edited in the draft on the panel. Nothing reaches the servo
before [WRITE n VALUES](#writing). A value that differs from the servo's is
marked twice: by `*` before it and by the accent fill.

| Parameter | Editor | Values |
| --- | --- | --- |
| D series SPEED | keypad | 10% to 100% in steps of 10% |
| D series DEAD BAND, classic DEAD BAND | list | steps 1 to 5 |
| D series SOFT START | list | 20%, 40%, 60%, 80%, 100% |
| D series NEUTRAL, LEFT END, RIGHT END | keypad | 1 to 16383 counts; LEFT END below NEUTRAL below RIGHT END |
| D series DIRECTION, classic DIRECTION | list | CW, CCW |
| D series FAILSAFE | list, then the keypad for POSITION | OFF, LIMP, or a position of 900us to 2100us |
| D series OVERLOAD PROTECTION, classic OVERLOAD PROTECTION | list | OFF, 10% to 50% in steps of 10% |
| D series SMART SENSOR, classic FAILSAFE | list | ON, OFF |
| D series ID | keypad | 0 to 255 |
| classic SPEED | keypad | 10% to 100% in steps of 10% |
| classic CENTRE | keypad | 900.00us to 2100.00us in steps of 0.25us |
| classic LEFT END, RIGHT END | keypad | 0 to 255, raw |
| classic FAILSAFE POSITION (HS-5xxx) | keypad | 900.00us to 2100.00us in steps of 0.25us |

The ranges and steps are those of `protocols/hitec/hitec_reg.c`. The
keypad shows the range; a value no step gives, or one that breaks the order
of the D series positions, is refused on the keypad with the reason:
`Not a value this setting takes.` or `LEFT END, NEUTRAL and RIGHT END must
ascend.` An edit writes the side effects the vendor programmer writes: a D
series DIRECTION mirrors NEUTRAL and both ends, a classic CENTRE sets both
end points to 0x10, a classic DIRECTION swaps them and takes the failsafe
position from the factory cells 0x49 and 0x4A, and every classic edit
balances cell 0x1C in the draft. A D series DIRECTION stored as neither 0
nor 1 is changed alone, without the mirror. A value stored raw is replaced
by the value of the table an edit sets; DISCARD EDITS under MORE brings the
raw value back to the draft.

### A value that needs a second look

A value that breaks a soft rule opens CHECK THIS CHANGE with the field's
line, `SPEED: 100% -> 20%`, the servo's value first, and one line per rule.
**HOLD 2s TO APPLY** takes it; **CANCEL** or the home tag leaves the draft as
it was.

| Rule | Warning |
| --- | --- |
| SPEED below 30% | `A slow servo can cause loss of control of the model.` |
| DEAD BAND below the servo's | `A narrow dead band can cause back-and-forth movement and heat.` |
| OVERLOAD PROTECTION switched off | `Without overload protection a stalled servo heats up.` |
| an end point other than the servo's | `End point changed. The servo travel per stick movement changes too.` |
| NEUTRAL or CENTRE other than the servo's | `The centre shifts. One side moves closer to the mechanical stop.` |
| DIRECTION reversed | `Direction reversed. Check all control surfaces before flight.` |
| a failsafe position more than 300us from 1500us | `Failsafe position is far from the centre.` |

A rule confirmed for a value holds for that value and for values nearer the
servo's on the same side. A value further from the servo's, or on its other
side, asks again: LEFT END 3351 confirmed at 3300 takes 3301 and asks at
3299 and at 3352. "The servo's" is the value of the last full read. A
write, a read and DISCARD EDITS end every confirmation. A stored value that
fits no encoding is no value to compare with: every change of it asks.

![SOFT START's list](img/servo-hitec-list.png)
![An edited value](img/servo-hitec-edit.png)
![SPEED on the keypad](img/servo-hitec-keypad.png)
![CHECK THIS CHANGE](img/servo-hitec-soft.png)

## The write switch

**ENABLE WRITE TO SERVO** is off by default. A tap on **OFF** opens a page
that names the risks; **HOLD 2s TO ENABLE** switches it on. A tap on **ON**
switches it off at once. A tap on **WRITE n VALUES**, RESTORE or REPAIR
CHECKSUM with the switch off opens the same page: its hold switches the
switch on and writes nothing.

The switch goes off:

- at every entry of the SERVO screen,
- at every CONNECT and every LEAVE,
- when the link to the IOmcu goes,
- at a STOP and at a switch to SAFE at the bench,
- when a write does not end verified,
- when a read finds other values than the ones held.

It goes on only with the port held, the servo in programming mode and read,
the port not stopped, the bench SAFE and no STOP latched.

![ENABLE WRITE TO SERVO](img/servo-hitec-enable.png)

## Writing

**WRITE n VALUES** counts the values in which the draft differs from the
servo. A tap opens the write page: the changed values, `SOFT START: 20% ->
60%`, in the order of the tabs, 6 at most and the count of the rest;
`Keep the servo horn clear. Disconnect the linkage.` and `Test the servo on
the bench before fitting it to a model.` on the first write after CONNECT
and on every write that changes DIRECTION, NEUTRAL, CENTRE or an end point;
what the series does; and where the values before the write are recorded.
**HOLD 2s TO WRITE** starts it.

1. The record: the servo's values, the values the write is to leave, the
   series, the port and its GPIO. With a card mounted it goes to
   `BENCH/HITEC/CONF/HITEC.TXT` first, and the write waits for it: a card
   that does not take it within 3000ms ends the write with
   `Could not save the file to the card. Nothing was written to the servo.`
   Without a card the record is kept in RAM.
2. The plan of `hitec_plan()`: only the settings that change. D series: the
   setting's registers as the HFP-30 writes them, 20ms apart, then the save,
   100ms, the restart and 1000ms. Classic: each cell of a changed setting,
   then the balance in cell 0x1C, each answered `+ +` within 3 tries.
3. The read-back: the D series set of 21 registers, or classic cells 0x00
   to 0x23, held against the draft.
4. `2 values written and verified.`, or `1 value written and verified.`,
   ends it and clears the record. The switch stays on.

In the host model (`test_hitec_prog_write`) a D series write of one value
takes 2.5s and a classic one 1.2s at a frame of 16ms and 2.5s at one of
33ms. On the bench these times are not measured.

The write is refused on an ARMED bench or one asked to arm, with a STOP
latched, on a stopped port, with no servo read, with the switch off, with
no change, and while a record of an earlier write stands.

![The write page](img/servo-hitec-write.png)
![A write under way](img/servo-hitec-writing.png)
![A write done](img/servo-hitec-written.png)
![The classic write page](img/servo-hitec-classic-write.png)

| Message | The servo |
| --- | --- |
| `Servo settings do not match after the write. Tap READ ALL.` | holds values the read-back found other than the draft's; the record stands |
| `Writing was interrupted at register 0x54. Tap READ ALL.` | took the D series registers before it; nothing is saved |
| `Writing was interrupted before the servo saved. Tap READ ALL.` | took the registers, unsaved |
| `Writing was interrupted after the save, before the restart. Tap READ ALL.` | saved them; the restart is owed |
| `Every value went out; their check was interrupted. Tap READ ALL.` | took all of them; the read-back did not end |
| `Writing was interrupted at cell 0x06. Tap READ ALL.` and `The checksum may be wrong: the servo may not follow PWM.` | may hold the new cell without its balance |
| `Cell 0x06 is written, its checksum in cell 0x1C is not. Tap READ ALL.` and the same second line | holds the new cell and the old balance: the checksum is wrong |
| `A write did not end verified. Tap READ ALL before the next write.` | a write once a step went out leaves what the servo holds unknown; READ ALL comes before the next write |

A write that ends any way but verified switches the switch off.

## The record of a write

The record stands from the start of a write until the write ends verified,
the write ends before a step reached the servo, a RESTORE ends verified, or
it is discarded. With a card it is read again
at the panel's next start: a panel that restarts during a write keeps it.
Then the message band says `Writing was interrupted. The settings may be
incomplete.` and `Do not use the servo. Connect it to GPIO 13 and tap READ
ALL.`, and the SERVO tab keeps the first line until a servo is read. The next full read compares the servo with the
record, address by address over the addresses a write changes:

| The servo holds | Message | RESTORE offers |
| --- | --- | --- |
| the values before the write | `Is this the servo whose write was interrupted on GPIO 13?` `It has the old values. If this is the same servo, discard the record under RESTORE.` | the restore, which then sends nothing and clears the record, and the discard |
| the values the write was to leave | the same first line and `It has the new values. If this is the same servo, discard the record under RESTORE.` | the restore and the discard |
| some of each | `The servo contains both old and new settings.` `Do not use this servo. RESTORE writes the values before the write back.` | the restore and the discard |
| another series, port or D series model, or a value that is neither | `The interrupted write affected a different servo.` `It was connected to GPIO 13. Discard the record under RESTORE.` | the discard only |

The classic balance 0x1C is not compared: a write cut between a cell and its
balance leaves a third value there. RESTORE writes the values before the
write back as a write of its own, with the switch, the hold page `RESTORE`
and its read-back; the record stays until that ends verified. `Discard the
record of the interrupted write.` removes it without a write. While a
record stands WRITE n VALUES and REPAIR CHECKSUM are refused: `A record of
an interrupted write stands. Restore or discard it under RESTORE.` The file
manager shows `HITEC.TXT` and `HITEC.NEW` with INFO only: a hand edit could
send wrong values to a servo.

![After a write that did not verify](img/servo-hitec-verify.png)
![RESTORE](img/servo-hitec-restore.png)

## REPAIR CHECKSUM

A classic servo whose cells 0x00 to 0x1C do not sum to 0 modulo 256 shows
CHECKSUM `WRONG`, and the SERVO tab says `MORE > REPAIR CHECKSUM writes cell
0x1C.` REPAIR CHECKSUM under MORE opens a page that names the cell's value
and the balance, `Writes cell 0x1C: 0x5F -> 0x5E.`; its hold writes the
balance alone, with its record and the read-back of cells 0x00 to 0x23.
It needs the switch, no edit standing (WRITE n VALUES writes the balance
with the edits) and no record.

![A classic checksum that is wrong](img/servo-hitec-classic-sum.png)
![REPAIR CHECKSUM](img/servo-hitec-classic-repair.png)

## LEAVE

CLOSE, the home tag or LEAVE on the PORT row opens LEAVE PROGRAMMING; a
hold of 2s on `HOLD 2s TO LEAVE` leaves:

- D series: a reboot of the servo (register 0x46 = 1), which then ignores
  signals for 1s; the supply stays as it is.
- Classic: the supply off for 500ms and on again.

Then CLOSE: the port is back in PWM and the message says
`The port is back in tester mode.` LEAVE is taken on a stopped port, an
ARMED bench and with STOP latched: the reboot is refused by a stopped port,
and the CLOSE is always taken.

![LEAVE PROGRAMMING, D series](img/servo-hitec-leave.png)
![LEAVE PROGRAMMING, classic](img/servo-hitec-classic-leave.png)

## STOP, SAFE and a lost link

| Event | The port | What the operator does |
| --- | --- | --- |
| STOP, pressed or the bench's own | The IOmcu cuts the transaction after the frame on the wire, keeps the port with the pin released and sets STOPPED. The panel latches the stop and withholds the heartbeat | `Stopped. The pin is released. LEAVE, then switch to ARMED once to clear the STOP.`, and no line offers CLEAR. CLEAR and READ ALL are refused with the STOP latched: the IOmcu would refuse the CLEAR as UNSAFE. LEAVE, then ARM and DISARM, then CONNECT |
| A switch to SAFE at the bench alone | STOPPED, no latch | `Stopped. The pin is released. Tap CLEAR to go on, or LEAVE.` CLEAR goes on with the read |
| ABORT | The operation ends; the port stays held | READ ALL again, or LEAVE |
| The link goes silent | The IOmcu's failsafe latches and the port stays held; the panel's module no longer counts it as its own | Once the link is back the overlay says `Port held by another Hitec session.` and offers RELEASE PORT |
| The panel starts again | As a lost link | As a lost link |

![STOP latched](img/servo-hitec-stopped.png)

## A port another session holds

A port the HITEC page holds and this panel did not open, one the bench
console opened or one held over a lost link or a panel restart, keeps the
Hitec row live: the overlay opens with the PORT row
`PROG PORT 1 GPIO 13  HELD ELSEWHERE` and `Another Hitec session holds it.`
RELEASE PORT opens a page; a hold of 2s on `HOLD 2s TO RELEASE` sends CLOSE,
which the IOmcu takes whether or not it trusts the heartbeat. The supply
stays as it is. Then a switch to ARMED clears the IOmcu's latched failsafe
as after any stop: ARM is refused only while the port is held.

![A port another session holds](img/servo-hitec-other.png)
![RELEASE PORT](img/servo-hitec-release.png)
![The port released](img/servo-hitec-released.png)

## Messages

| Line | When |
| --- | --- |
| `No D series servo answers. Check plug, receive mode and series.` | no byte came on the last of 3 tries of a D series read |
| `No classic servo answered after power-up. Check plug and series.` | no `+` came within 2s after the power-up |
| `The servo's answer to the entry was wrong. Check the series.` | bytes came that do not fit the entry |
| `Bad answer from the servo (code 14). Reading again.` | a try failed; the code is the decoder's (`hitec_rx_t`) |
| `Read failed at address 0x32. Tap READ ALL.` | every try of one address failed |
| `The IOmcu refused: its failsafe is latched.` | OPEN or CLEAR refused UNSAFE |
| `The IOmcu ended the session. The port is back in tester mode.` | the port went back to PWM with no command of this panel |
| `No answer from the IOmcu in time. The operation ended.` | a command not taken within 2000ms, or a request without an outcome within 30,000ms |
| `The supply did not follow the POWER request. Check the PD mini.` | POWER ended with SUPPLY |
| `The IOmcu cannot drive the line of this port.` | OPEN found no free line |
| `The IOmcu refused the request (code 7).` | a refusal with no message of its own |

![A classic CONNECT with no PD mini](img/servo-hitec-nopd.png)
![MORE](img/servo-hitec-more.png)

## Current limitations

- Classic writes through the IOmcu are run on one servo, an HS-7955TG
  (station, 2026-10-25): cell 0x02 and the balance 0x1C, kept over a power
  cycle. Every other cell, the cells 0x1D to 0x23, which a write sends
  without a balance, and the data memory command 0x64 are not tested;
  the programmer sends no 0x64. A write to a servo with a wrong checksum
  and REPAIR CHECKSUM are not tested on a servo.
- D series writes are measured on 1 servo, a D485HW, by the HFP-30's
  captured writes; writes from rcbench to a D series servo are tested in
  the host model only. Whether a save cut by a power loss leaves the
  servo's flash whole is not measured. Values written and not saved are
  lost at the servo's next power-off, and the D series entry of a CONNECT
  switches the supply off and on.
- The soft rules compare with the last full read, not with the first read
  of the session, and no backup file is made: the record of a write holds
  the values before it.
- The record is read from the card at the panel's start only. A card put
  in later is not read for one, and a record made with no card mounted is
  kept in RAM and is gone after a panel restart.
- NEUTRAL, LEFT END and RIGHT END are edited as numbers on the keypad; the
  servo does not move to them while they are edited, and the keypad has no
  `-`/`+` keys. FACTORY DEFAULT is not offered.
- Station steps S6 to S8 and S10 to S11 (writes to a D485HW and a classic
  servo, a link cut during a write) are not run.
- The D series POWER of the entry with an off time of 0ms switches the PD
  mini's output off and on again: the servo restarts at every CONNECT.
- A classic read takes 2 render frames a cell; at 33ms a frame 100 cells
  take 6.6s in the host model.
- Station steps S1 to S5 of the plan (a D485HW at PRECHARGE and PULL-UP, the
  classic HS-5645MG and HS-7955TG, STOP and a pulled link cable during READ
  ALL, ARM and SUPPLY while held) are not run.
