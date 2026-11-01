# ESC signal wire

<sub>**English** · [Deutsch](EscWire-de.md)</sub>

This page describes MOTOR & ESC > PROGRAM's row `AM32, Bluejay, BLHeli_S, ESCape32`, `SIGNAL WIRE` at its right. ESC is electronic speed controller. The session reads and writes an ESC's settings over its signal wire, from a throttle slot or a DShot port. The row stays dimmed until the IOmcu reports `LINK_CAP_PROGRAM`. The pages behind it are [The ESC settings pages](LinkEsc.md).

This code was written from a behavioural specification, without access to the firmware sources.

## Current limitations

- No frame has reached a real ESC. Every result here comes from a simulated ESC in the host tests and from bench runs with no ESC on the line.
- Bluejay and ESCape32 are not run on hardware.
- A bidirectional DShot pin rests high on SAFE, which is a bootloader's entry condition. What the next ARM does after that is not measured.
- All outputs go off after `TORN` or `LOST`. On BLHeli_S and Bluejay the page then loses its 48 tag bytes and reads read-only. An AM32 stays erased until RESTORE FROM BACKUP.
- 10 failed reads of the page, 50ms at the poll rate, stop a session. A write gets no exception: it ends as `NO_ANSWER`, with a fault and a STOP.
- The record `ESCW.TXT` is written before a write. It does not say how the write ended.
- SimonK is not supported: this bench has no wire code for it, and no screen offers it. An ESC with SimonK gives `No answer`.

## What it takes

| Firmware | Written | Read-only with field names | Editable fields |
|---|---|---|---:|
| BLHeli_S | layout 33 | layout 32 | 12 |
| Bluejay | layout 208 | layouts 33, 200 to 207 | 15 |
| AM32 | revisions 3 and 4 | | 39 on revision 3, 40 on revision 4 |
| ESCape32 | revision 17 | | 46 |

A page with no field names shows its bytes, 16 to a line. A firmware or layout that cannot be written has no WRITE button.

## Connect

The strip names what the session drives: `TARGET SLOT 1 GPIO 0`, then the supply and SAFE or ARMED. No pin shows in the warning colour.

| Row | Range | Default |
|---|---|---|
| TARGET | throttle slots and DShot ports | the first |
| POWER | SWITCHED, BY HAND | SWITCHED |
| VOLTAGE | 3.00V to SUPPLY's limit | SUPPLY's set point |
| LIMIT | 0.05A to SUPPLY's limit | SUPPLY's set point |
| ENTRY HOLD | 0.5s to 120s | 3.0s |
| OFF TIME | 0s to 10s | 1.0s |

The session has no default voltage. VOLTAGE and LIMIT follow SUPPLY until a tap on the number types another. SWITCHED uses the PD mini. BY HAND drives the line high and waits at most 60s for `POWER IS ON`.

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

![ESCape32 identity](img/motor-wire-e32.png)

![ESCape32 motor](img/motor-wire-e32-1.png)

![ESCape32 duty](img/motor-wire-e32-2.png)

![ESCape32 throttle](img/motor-wire-e32-3.png)

![ESCape32 input](img/motor-wire-e32-4.png)

![ESCape32 protection](img/motor-wire-e32-5.png)

![ESCape32 sound](img/motor-wire-e32-6.png)

![Read-only identity](img/motor-wire-readonly.png)

![Read-only rows](img/motor-wire-readonly-rows.png)

![Bytes of a layout with no field names](img/motor-wire-hex.png)

## Write

WRITE n VALUES opens a page that lists the changes. Each hazardous field stays editable: the page names it with `CHECK`, first, in the warning colour, with its new value. A page lists 12 changes. PREV and NEXT turn the list, and the red 2s hold is offered once the last page has been shown. ESCape32 has up to 46 changes, 4 pages.

What the page lists is what the hold writes: the page keeps its own copy of the draft. From the hold to the write's outcome the session takes ABORT and STOP only. ABORT while the card works gives the write up, and nothing is written. WRITE waits while a file is on its way from the card. A file that comes while a page is open is not loaded, and the page says so.

1. The first write of a session saves the session's backup to `BENCH/ESCW/CONF/ESCW001.TXT` to `ESCW999.TXT`. With no card, or a card that refuses it, the page asks; a second 2s hold goes without one.
2. The record `BENCH/ESCW/CONF/ESCW.TXT` takes the changed fields and the ESC as read. A card that refuses it asks the same question.
3. The page re-reads, writes, reads back and says the result in words. A BLHeli_S or Bluejay write takes about 3848ms.

An erased page is no backup. The session's backup is its first read that is not erased; after a RESTORE that is the read following it. A `music` line with a byte outside 0x20 to 0x7E gives no file: the session writes nothing and says why.

![Write page](img/motor-wire-am-write.png)

![First page of a long list](img/motor-wire-am-long.png)

![Last page of a long list](img/motor-wire-am-last.png)

![The card takes the backup](img/motor-wire-am-card.png)

![Writing](img/motor-wire-am-running.png)

FILES lists the files on the card, newest first, and saves the session's backup by hand. A file goes into the draft, to the same firmware and layout, editable fields only. A file changed in the panel's editor has `edited` as its last line in place of the checksum, and the programmer reads its field lines.

## Erased AM32

An AM32 whose settings read 0xFF offers RESTORE FROM BACKUP and nothing else, and saves no backup. A file is taken only when its CRC-32 holds and its pin code, flash code, bootloader protocol and name equal the ESC's. The restore is a red 2s hold.

![Erased AM32](img/motor-wire-erased.png)

## Leave

BACK opens LEAVE, a 2s hold. The ESC leaves its bootloader, the line goes low, the supply goes off and the pin goes back to its output. Edits not written are lost.

On an erased AM32 BACK's page says the ESC stays erased until RESTORE FROM BACKUP, that the pin goes back and that the supply goes off. Its hold sends CLOSE; with BY HAND the page says to disconnect the ESC's power.

ESCape32 has no LEAVE: a power cycle ends its session. SWITCHED parks the line and switches the supply off. BY HAND parks the line and says to disconnect the ESC's power.

![Leave](img/motor-wire-am-leave.png)

![Leave on ESCape32](img/motor-wire-e32-leave.png)

![Leave on an erased AM32](img/motor-wire-erased-leave.png)

## Stops and other writers

STOP in the band stops the session. A write runs to its read-back first. ABORT on the page acts on the press and cuts at once, also in a write. A stop, a supply lost or a link lost closes an open editor or hold page, and the screen says what happened.

After a stop or a fault the page offers CLEAR STOP and PIN BACK. CLEAR STOP ends the stop and keeps the pin held.

A pin held in `OPEN` with no stop and nothing running has a page of its own: after ABORT, after CLEAR STOP, and after a refused ENTER, whose reason shows on it, such as no PD mini or a voltage too close to the source. It offers ENTER AGAIN, a red 2s hold with the values of the last CONNECT, and PIN BACK. BACK names the button that ends the stage.

A pin another writer holds, the bench console or a session before a panel restart, offers RELEASE PIN with a 2s hold. ARM is refused while a pin is held, and the alert names the ESC programmer.

![Stopped](img/motor-wire-stopped.png)

![Pin held](img/motor-wire-held.png)

![Another writer](img/motor-wire-other.png)
