# YGE ESC programming

<sub>**English** · [Deutsch](YgeProgramming-de.md)</sub>

Under MOTOR & ESC > PROGRAM, the row `YGE ESCs` has `OPENYGE` on its right and sits in the group `TELEMETRY WIRE`. It opens a session that reads and writes the parameter table of a YGE electronic speed controller (ESC) over its telemetry wire, on the pin of a programming port. The row stays dimmed until the IOmcu reports `LINK_CAP_YGE` (bit 9). The link pages are described in [Programming for YGE ESCs](LinkYge.md), the wire and the table in [OpenYGE](OpenYGE.md).

YGE is a name or trademark of its owner. It is used here only to say which ESCs this works with.

## Current limitations

- POWER is kept in RAM only. After a panel restart it starts on SWITCHED.
- A stop, an error or a link loss between a write's first frame and the end of its read after leaves the ESC's table unknown until the next read.
- The meaning of indices 16, 19 and 28 to 31 is not known. They are shown on RAW as numbers and are never written.
- POLE PAIRS (index 20) is read-only: whether the ESC counts pairs or poles is not known.

## What it takes

| Item | Value |
|---|---|
| Wire | the ESC's telemetry signal and ground on a programming port's pin; the throttle lead unplugged |
| Firmware written | V1.03547 and later, a table of 32 parameters, protocol header version 3 or later |
| Firmware read | any that answers; a table that cannot be written shows `READ ONLY` with the reason |
| Power | SWITCHED: the PD mini at VOLTAGE and CURRENT LIMIT. BY HAND: the ESC's own battery |
| Card | FAT32 or exFAT for the backup and FILES; a write without a card keeps the old values in RAM only |

Run the mode setup (MOTOR & ESC > PROGRAM > `ESCs via throttle stick`) before the first session. It sets every value; this session changes single values afterwards.

## Connect

The strip on every page names the target, for example `TARGET PORT 1 GPIO 13`, followed by the supply and SAFE or ARMED. With `BY HAND` the supply shows `BY HAND`. A port whose pin is not known shows `PORT 1` in the warning colour.

| Row | Range | Default |
|---|---|---|
| PORT | programming ports 1 to 4 | the first |
| POWER | SWITCHED, BY HAND | SWITCHED |
| VOLTAGE | 3.00V, or SUPPLY's lower limit if higher, to SUPPLY's upper limit for this board, at most 20.00V | SUPPLY's set point |
| CURRENT LIMIT | 0.05A, or SUPPLY's lower limit if higher, to SUPPLY's upper limit, at most 3.00A | SUPPLY's set point |
| BOOT WAIT | 1.0s to 20.0s | 5.0s |

VOLTAGE and CURRENT LIMIT follow SUPPLY until a value is typed. With `BY HAND` neither applies and both are dimmed. A value that moves while CONNECT is held ends the hold.

Take the propeller off. Connect the telemetry signal and ground only. The throttle lead's center wire (BEC, battery eliminator circuit) stays off the bench. YGE allows batteries only as the ESC's power; `BY HAND` uses the ESC's own battery.

`HOLD 2s TO CONNECT` is a red 2s hold. A release at 1999ms sends nothing. At 2000ms the panel sends OPEN, then ENTER with the key, then READ. `BY HAND` stops at `WAITING FOR POWER`: connect the battery and tap `POWER IS ON` within 60s.

![Connect page](img/motor-yge-connect.png)

![Port list](img/motor-yge-port.png)

![Connect page, BY HAND](img/motor-yge-hand.png)

![Boot wait keypad](img/motor-yge-boot.png)

![Entering](img/motor-yge-entering.png)

![Waiting for power](img/motor-yge-handwait.png)

![Hold on CONNECT at 1s of 2s](img/motor-yge-fill-connect.png)

## The tabs

| Tab | Rows |
|---|---|
| IDENTITY | ESC type with its name, firmware, serial, parameter count, `WRITABLE` or `READ ONLY` with the reason |
| MODE & BEC | ESC MODE, BEC VOLTAGE, BEC UP TO 12V, DIRECTION, F3C AUTOROTATION, KEEP MAH |
| PROTECTION | CUT-OFF HANDLING, CUT-OFF VOLTAGE, CURRENT LIMIT |
| START | START POWER MIN, START POWER MAX, MOTOR TIMING, THROTTLE RESPONSE, ACTIVE FREEWHEEL, STARTUP RESPONSE |
| GOVERNOR & RPM | GOV P GAIN, GOV I GAIN, PINION TEETH, MAIN GEAR TEETH, POLE PAIRS |
| STICK | STICK ZERO, STICK RANGE |
| RAW | indices 16 and 19, TELEMETRY TYPE, FLAGS as `0x0000`, indices 28 to 31 |

DIRECTION, F3C AUTOROTATION, KEEP MAH and BEC UP TO 12V are bits of index 26 (FLAGS) and have rows of their own. A row whose index is at or above the table's count is left out.

A CHECK field (ESC MODE, BEC VOLTAGE, CUT-OFF HANDLING, DIRECTION, CURRENT LIMIT) has its name in the warning colour. A read-only row is dimmed and opens nothing. Microseconds show as `us`: the panel's font has no µ.

A tap on a choice opens a list of the values the field may be written with. A tap on a number opens the keypad; a value outside the writable set is refused and the keypad stays open. BEC VOLTAGE takes 5.5V to 8.4V while BEC UP TO 12V is off. An edit changes a draft on the panel and marks the row with `*`. Nothing goes to the ESC until WRITE. A value set back to the one read is no change.

A changed CHECK field is named above the footer with its new value, for example `CHECK BEC VOLTAGE 8.0V`; more than one adds `+1 MORE`.

![Identity](img/motor-yge-id.png)

![Mode and BEC](img/motor-yge-mode.png)

![Protection](img/motor-yge-protect.png)

![Start](img/motor-yge-start.png)

![Governor and rpm](img/motor-yge-gov.png)

![Stick](img/motor-yge-stick.png)

![Raw](img/motor-yge-raw.png)

![Choice list](img/motor-yge-choice.png)

![Number keypad](img/motor-yge-number.png)

![Read-only table](img/motor-yge-readonly.png)

## Write

The footer holds `READ AGAIN`, `FILES`, `WRITE n VALUES` and `LEAVE`. `WRITE n VALUES` counts the changed indices; the four bits of index 26 count as 1. With no change, or with a request running, it is greyed and a press on it does nothing. A table that cannot be written has no WRITE.

`WRITE n VALUES` is a red 2s hold, with no confirm page. A release at 1999ms sends nothing. At 2000ms the first write of the session saves the backup `BENCH/YGE/CONF/YGE001-<type>.TXT` first, for example `YGE001-5712` for type code 5712, and sends the WRITE when the card has answered. With no card, a card that is not FAT32 or exFAT, or a save that fails, the WRITE goes and the old values are kept in RAM only; the message says so. A card silent for 10s sends no WRITE. Later writes of the same session save no backup.

The IOmcu reads the table again before the write and refuses it with nothing sent when it differs from the read the draft was made from. After the write it reads the whole table back and compares it with the draft: `Written and read back.`

`READ AGAIN` with no edits reads at once. With edits not written it opens a page that lists them; `HOLD 2s TO READ` reads, `BACK` keeps the draft.

![Edits with the CHECK line](img/motor-yge-check.png)

![Hold on WRITE at 1s of 2s](img/motor-yge-fill-write.png)

![Waiting for the card](img/motor-yge-card.png)

![Writing](img/motor-yge-writing.png)

## Files

`FILES` lists `YGE001` to `YGE999` from `BENCH/YGE/CONF`, the newest first. A tap on a file loads its values into the draft and says how many: `2 values loaded from the file into the draft. WRITE sends them.` A file of another ESC type, or one with a value outside the writable set, is refused with the reason and the draft stays. A folder with no file says `No YGE files in BENCH/YGE/CONF.`

![File list](img/motor-yge-files.png)

## Leave

`LEAVE` with no edits is a tap: LEAVE when the ESC answers, then CLOSE. The pin goes back to its port and the PD mini goes off. With edits not written, `LEAVE` and `BACK` open a page that lists each edit as `BEC VOLTAGE: 7.4V -> 8.0V`. `HOLD 2s: LEAVE WITHOUT WRITING` leaves, `BACK` keeps the draft. With `BY HAND`, disconnect the ESC's battery yourself.

![Leave page](img/motor-yge-leave.png)

![Hold on LEAVE at 1s of 2s](img/motor-yge-fill-leave.png)

## Stops and other writers

`ABORT` acts on the press, also with another finger down. STOP, a stop edge, an error, the heartbeat lost or the link silent for 200ms park the line and switch the PD mini and PROG_SUPPLY off at once, also in a write; every output goes off. A stop the page latched offers `CLEAR STOP` and `PIN BACK`; the panel's own stop offers `PIN BACK` only. After `CLEAR STOP` the pin stays held with no session: `PIN BACK` returns it. A link that goes closes every list, keypad and page.

A pin held by another writer, such as the bench console's `yge`, shows its port; `RELEASE PIN` opens a page and `HOLD 2s TO RELEASE` closes that session. A port that did not take its pin back shows `PIN NOT BACK`: the bank does not arm until a CLOSE returns it. `TRY PIN AGAIN` sends that CLOSE.

![Stopped](img/motor-yge-stopped.png)

![Pin held](img/motor-yge-held.png)

![Another writer's pin](img/motor-yge-other.png)

![Pin not back](img/motor-yge-not-back.png)
