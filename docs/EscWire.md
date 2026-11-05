# ESC signal wire

<sub>**English** · [Deutsch](EscWire-de.md)</sub>

Under MOTOR & ESC > PROGRAM, the row `AM32, Bluejay, BLHeli_S, ESCape32` has `SIGNAL WIRE` on its right. It opens a session that reads and writes an electronic speed controller's (ESC's) settings over its signal wire, using a throttle slot or a DShot port. The row stays dimmed until the IOmcu reports `LINK_CAP_PROGRAM`. The data layouts are described in [The ESC settings pages](LinkEsc.md).

This code was written from a behavioural specification, without access to the firmware sources.

## Current limitations

- All outputs go off after `TORN` or `LOST`. On BLHeli_S and Bluejay the settings page then loses its 48 tag bytes and becomes read-only. An AM32 stays erased until RESTORE FROM BACKUP.
- 10 failed reads of the page, 50ms at the poll rate, stop a session. This also applies during a write: it ends as `NO_ANSWER`, with a fault and a STOP.
- The record `ESCW.TXT` is saved before the write starts. It stays on the card even if the write is then abandoned. It does not record the write's outcome.
- SimonK is not supported: the bench has no signal-wire implementation for it, and no screen offers it. An ESC with SimonK gives `No answer`.

## What it takes

| Firmware | Written | Read-only with field names | Editable fields |
|---|---|---|---:|
| BLHeli_S | layout 33 | layout 32 | 12 |
| Bluejay | layout 208 | layouts 33, 200 to 207 | 15 |
| AM32 | revisions 3 and 4 | | 39 on revision 3, 40 on revision 4 |
| ESCape32 | revision 17 | | 46 |

A page with no field names shows its bytes, 16 to a line. A firmware or layout that cannot be written has no WRITE button.

## Press and hold

A button shows the pressed colour from press to release. A press on a button repaints that button only. A hold fills from its dark colour to red in 2s, in both framebuffers; ABORT acts on the press.

![Hold on CONNECT at 1s of 2s](img/motor-wire-fill-connect.png)

![Hold on ENTER AGAIN at 1s of 2s](img/motor-wire-fill-again.png)

![Hold on WRITE at 1s of 2s](img/motor-wire-fill-write.png)

![Hold on write without a backup at 1s of 2s](img/motor-wire-fill-no-backup.png)

![Hold on LEAVE at 1s of 2s](img/motor-wire-fill-leave.png)

![Hold on RELEASE at 1s of 2s](img/motor-wire-fill-release.png)

![Hold on RESTORE at 1s of 2s](img/motor-wire-fill-restore.png)

![READ AGAIN pressed](img/motor-wire-press-read.png)

![POWER IS ON pressed](img/motor-wire-press-hand.png)

![BACK pressed](img/motor-wire-press-back.png)

![a row pressed](img/motor-wire-press-row.png)

![CANCEL pressed](img/motor-wire-press-cancel.png)

![NEXT pressed](img/motor-wire-press-next.png)

![PREV pressed](img/motor-wire-press-prev.png)

![a list entry pressed](img/motor-wire-press-choice.png)

![a list's CANCEL pressed](img/motor-wire-press-list-cancel.png)

## Connect

The strip identifies the session target, for example `TARGET SLOT 1 GPIO 0`, followed by the supply and SAFE or ARMED. If no target pin is available, the target text uses the warning colour.

| Row | Range | Default |
|---|---|---|
| TARGET | throttle slots and DShot ports | the first |
| POWER | SWITCHED, BY HAND | SWITCHED |
| VOLTAGE | 3.00V, or SUPPLY's lower limit if higher, to SUPPLY's upper limit for this board and setting | SUPPLY's set point |
| CURRENT LIMIT | 0.05A, or SUPPLY's lower limit if higher, to SUPPLY's upper limit for this board and setting | SUPPLY's set point |
| ENTRY HOLD | 0.5s to 120s | 3.0s |
| OFF TIME | 0s to 10s | 1.0s |

The session has no default voltage. VOLTAGE and CURRENT LIMIT follow SUPPLY until you tap a number and enter a different value. POWER retains the last choice across panel restarts in non-volatile storage (NVS key `escw_by_hand`). Without a stored choice, it starts on SWITCHED. SWITCHED uses the PD mini. BY HAND drives the line high and waits at most 60s for `POWER IS ON`.

Take the propeller off. Connect signal and ground only: a BEC (battery eliminator circuit) on the ESC's centre wire would feed the bench's rail. Unplug servos. CONNECT is a red 2s hold.

![Connect page](img/motor-wire-connect.png)

![Connect page, BY HAND](img/motor-wire-hand.png)

![Voltage keypad](img/motor-wire-keypad.png)

![Target list](img/motor-wire-target.png)

![No target](img/motor-wire-notarget.png)

![Entering](img/motor-wire-entering.png)

![Waiting for power](img/motor-wire-handwait.png)

## The settings

IDENTITY names the firmware, the version, the layout or revision, the signature or name, and `WRITABLE` or `READ ONLY` with the reason. The other tabs hold the fields. A tap on a number opens the keypad, a tap on a choice opens its list, and each editor shows the field's help line. An edit changes a draft on the panel and marks the field's name; nothing goes to the ESC until WRITE.

![AM32 identity](img/motor-wire-am.png)

![AM32 motor](img/motor-wire-am-motor.png)

![AM32 brake and sine](img/motor-wire-am-brake.png)

![AM32 input](img/motor-wire-am-input.png)

![AM32 protection](img/motor-wire-am-protect.png)

![A choice](img/motor-wire-am-choice.png)

![A number](img/motor-wire-am-number.png)

![BLHeli_S](img/motor-wire-s.png)

![Bluejay](img/motor-wire-bj.png)

![BLHeli_S identity](img/motor-wire-s-id.png)

![BLHeli_S input and sound](img/motor-wire-s-2.png)

![Bluejay identity](img/motor-wire-bj-id.png)

![Bluejay input and sound](img/motor-wire-bj-2.png)

![ESCape32 identity](img/motor-wire-e32.png)

![ESCape32 motor](img/motor-wire-e32-1.png)

![ESCape32 duty](img/motor-wire-e32-2.png)

![ESCape32 throttle](img/motor-wire-e32-3.png)

![ESCape32 input and sound](img/motor-wire-e32-4.png)

![ESCape32 protection](img/motor-wire-e32-5.png)

![Read-only identity](img/motor-wire-readonly.png)

![Read-only rows](img/motor-wire-readonly-rows.png)

![Bytes of a layout with no field names](img/motor-wire-hex.png)

## Write

WRITE n VALUES opens a list of changes. Hazardous fields remain editable and appear first, marked `CHECK` in the warning colour, with their new values. Each page lists 12 changes. PREV and NEXT turn the pages. The red 2s hold becomes available after you have seen the last page. ESCape32 allows up to 46 changes across 4 pages.

The confirmation page keeps its own copy of the draft, so the hold writes exactly the changes listed. From the hold until the write's outcome, the session accepts only ABORT and STOP. ABORT during card activity abandons the write and leaves the ESC unchanged. WRITE waits while a file is being read from the card. If that file arrives while a confirmation page is open, it is not loaded; the page reports this.

1. The first write of a session saves the session's backup to `BENCH/ESCW/CONF`, as `ESCW001-AM32.TXT` to `ESCW999-AM32.TXT` (see [Backup names](#backup-names)). If the card is missing or the save fails, the page asks for confirmation. A second 2s hold continues without the backup.
2. The record `BENCH/ESCW/CONF/ESCW.TXT` stores the changed fields and the settings read from the ESC. If this save fails, the page asks for the same confirmation.
3. The session re-reads, writes, reads back and reports the result in words. A BLHeli_S or Bluejay write takes about 3848ms.

An erased settings page cannot serve as a backup. The session uses its first non-erased read; after a RESTORE, this is the read that follows the restore. If a `music` line contains a byte outside 0x20 to 0x7E, no file is created. The session writes nothing and explains why.

![Write page](img/motor-wire-am-write.png)

![First page of a long list](img/motor-wire-am-long.png)

![Last page of a long list](img/motor-wire-am-last.png)

![The card takes the backup](img/motor-wire-am-card.png)

![No backup on the card](img/motor-wire-no-backup.png)

![Files on the card](img/motor-wire-files.png)

![Writing](img/motor-wire-am-running.png)

FILES lists the card's files, newest first, with 11 per page below SAVE THE SESSION'S BACKUP and DELETE A FILE. It also lets you save the session's backup manually. Loading a file changes only the draft's editable fields and requires matching firmware and layout. A file changed in the panel's editor has `edited` as its last line in place of the checksum; the programmer reads its field lines.

### Backup names

A backup's name is `ESCW`, a number from 001 to 999 and the firmware: `ESCW001-AM32.TXT`, `ESCW002-BLHELI_S.TXT`, `ESCW003-BLUEJAY.TXT`, `ESCW004-ESCAPE32.TXT`. A manually saved backup uses the same naming scheme. Its number is one above the highest in the folder, including names with and without a firmware suffix. This avoids duplicate numbers and lists the newest file first. With number 999 taken the lowest free number is used; with all 999 taken the save says the card is full. Files named `ESCW001.TXT`, and files copied in under other names, list and load the same way. `ESCW.TXT` is the record, never a backup.

### Deleting a file

DELETE A FILE switches the list to deletion mode. Tapping a file opens a confirmation that names the file and requires a red 2s hold. After deletion, the list is read from the card again. Backups saved in this session since CONNECT cannot be deleted here; the page explains this. Use LOGS > FILES to delete them. If deletion fails, the file remains unchanged and the page reports the failure. Deletion applies only to the card from which the list was read. If you have since changed the card, nothing is deleted and the page explains why.

## Erased AM32

An AM32 whose settings read 0xFF offers only RESTORE FROM BACKUP and saves no backup. A file is accepted only if its CRC-32 checksum is valid and its pin code, flash code, bootloader protocol and name match the ESC's. Restoring requires a red 2s hold.

![Erased AM32](img/motor-wire-erased.png)

![Restore from a backup](img/motor-wire-restore.png)

## Leave

BACK opens LEAVE, a 2s hold. The ESC leaves its bootloader, the line goes low, the supply goes off and the pin goes back to its output. Edits not written are lost.

On an erased AM32 BACK's page says the ESC stays erased until RESTORE FROM BACKUP, that the pin goes back and that the supply goes off. Its hold sends CLOSE; with BY HAND the page says to disconnect the ESC's power.

ESCape32 has no LEAVE wire command: a power cycle ends its session. SWITCHED parks the line and switches the supply off. BY HAND parks the line and says to disconnect the ESC's power.

![Leave](img/motor-wire-am-leave.png)

![Leave on ESCape32](img/motor-wire-e32-leave.png)

![Leave on an erased AM32](img/motor-wire-erased-leave.png)

## Stops and other writers

STOP in the band stops the session, but an ongoing write reaches its read-back first. ABORT on the page acts immediately on press, including during a write. A stop, loss of supply or loss of the link closes any open editor or hold page. The screen reports what happened.

After a stop or a fault the page offers CLEAR STOP and PIN BACK. CLEAR STOP ends the stop and keeps the pin held. With the panel's own STOP standing, only PIN BACK shows, with `The bench STOP is latched: hold STOP 2s to reset it.`: CLEAR STOP needs the heartbeat, which the latched STOP withholds. Once STOP is reset, CLEAR STOP shows again.

After a stop or a fault, the panel keeps SUPPLY OFF latched on the SUPPLY page until the pin is released. The supply therefore cannot come on again while the session holds the pin. The PIN HELD page then offers only PIN BACK, with `A stop or a fault ended this session: PIN BACK, then CONNECT.` ENTER in this state is refused with `SUPPLY_OFF` before anything moves. A SUPPLY OFF refusal under PIN HELD with no stop or fault shows only the refusal message.

A pin held in `OPEN` with no stop and nothing running has a page of its own: after ABORT, after CLEAR STOP, and after a refused ENTER, whose reason shows on it, such as no PD mini or a voltage too close to the source. It shows POWER, VOLTAGE, CURRENT LIMIT, ENTRY HOLD and OFF TIME, read-only, and offers ENTER AGAIN, a red 2s hold, and PIN BACK; after a stop or a fault, CLEAR STOP included, it offers PIN BACK only. ENTER AGAIN sends those values: the session's current ones, which follow SUPPLY until typed, not those of the last CONNECT. BY HAND shows no voltage and no limit. If a value changes during the hold, the hold is cancelled. After ABORT the line says ABORT ended it. BACK names the button that ends the stage.

A message remains visible during the stage in which it occurred and on the page that follows that stage. Pressing a visible button clears it; pressing an empty area does not. On arrival at CONNECT with the pin free and no active flow, all messages are cleared except `LEFT`, the link state and the error that ended the flow. That error clears on the next button press. Link loss closes any keypad, choice list or hold page at any stage, and its message replaces `ABORT ended it`.

A pin another writer holds, the bench console or a session before a panel restart, offers RELEASE PIN with a 2s hold. The page names the pin: its GPIO and its slot or port. ARM is refused while a pin is held, and the alert names the ESC programmer.

![Stopped](img/motor-wire-stopped.png)

![Stopped by the panel's STOP](img/motor-wire-stopped-band.png)

![Pin held](img/motor-wire-held.png)

![Pin held after a stop](img/motor-wire-held-stop.png)

![Pin held, BY HAND](img/motor-wire-held-hand.png)

![Pin did not come back](img/motor-wire-not-back.png)

![Release a pin another writer holds](img/motor-wire-release.png)

![Another writer](img/motor-wire-other.png)
