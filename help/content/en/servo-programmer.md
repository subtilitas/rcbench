---
screen: none
title: Servo programmer
order: 22
---

# Servo programmer

The servo programmer reads the 32 registers of a KST or Chaservo servo through its signal wire and displays them as settings. Both brands use the same protocol and register set. Changes are written back only when requested.

Open PROGRAM on [SERVO](servo.md), then `KST and Chaservo servos`. The session covers the left column and task tabs, one level below the list. CLOSE or the home tag returns to the list. Opening the programmer ends a running automatic test.

![PROGRAM's list](view:servo-program)

The row identifies the SERVO TESTER port and pin, for example `PROG PORT 1 GPIO 13`. It is dimmed and cannot open when the IOmcu, the RP2350 board, does not report servo-PWM support, including when no IOmcu responds. The row then shows `IOMCU HAS NO SERVO PWM`.

![The row dimmed](view:servo-program-nocap)

It is dimmed with `PORT IN USE` while a Hitec session opened from the bench console holds that port.

![The port in use](view:servo-program-busy)

KST and Chaservo are names or trademarks of their respective owners. rcbench is not affiliated with, endorsed by or sponsored by them. The names say which servos the programmer works with.

Register layout and timing were derived from 1 KST Tool Card #2 and 1 Chaservo servo. Register effects on motion have not been measured. Names, units and limits follow those displayed and accepted by the vendor tool.

## What it needs

![The programmer with the tester holding the port](view:servo-kst)

- IOmcu 0.17.0 or later (protocol 4.15, including LEAVE).
- A servo connected to an IOmcu programming port, PROG PORT 1 to 4, with a pin and FUNCTION SERVO PWM. On a manually wired board, port 1 defaults to GPIO 13, pad 17; set other pins in each PROG PORT entry on Outputs. Carrier pins are fixed; see [The port and the supply](#the-port-and-the-supply).
- SERVO TESTER set to that port: PORT 1, PORT 2, PORT 3 or PORT 4. The [TARGET row](servo-settings.md#the-target-row) opens its Outputs entry. On a carrier with protocol 4.36 or later, you can also choose it from the programmer's PORT row.
- A SAFE bench. While ARMED, CONNECT is refused with `Tap DISARM first. The port cannot be taken over while ARMED.` While the port is held, no screen can switch the bench to ARMED.

![An IOmcu without the programming port](view:servo-kst-nopage)
![The standard outputs are the target](view:servo-kst-target)

If a requirement is unmet, all controls except CLOSE are disabled and the tab explains why.

## The port and the supply

The port runs either PWM (pulse-width modulation) or programming. CONNECT stops PWM, sets the supply if a PD mini responds, performs the entry sequence and reads all 32 registers. The button then becomes LEAVE, or ABORT while an operation is running.

![What CONNECT sets](view:servo-kst-options)

While the tester owns the port, MORE configures the values applied by CONNECT:

| Entry | Range | Default |
|---|---|---|
| PROGRAMMING VOLTAGE | 4.8 to 6.0V in 0.1V steps; up to 8.4V with HV SERVO on | 5.0V |
| CURRENT LIMIT | 50 to 3000mA | 1000mA |
| WAIT BEFORE PWM | 0 to 10000ms | 100ms |

![A PD mini answers](view:servo-kst-supply)
![The supply in programming mode](view:servo-kst-supplied)

While the port is in programming mode, it controls the supply set points. The SET row and SUPPLY screen reject changes and ON; OUTPUT OFF and STOP remain available. Without a PD mini, voltage and current limit are stored but not applied. After returning to PWM, the pre-session set points are restored once the output is off. If LEAVE keeps it on, restoration waits for the next OFF. Any set point changed in the meantime is retained.

![The question after LEAVE](view:servo-hitec-restore-ask)

If LEAVE keeps the output on at session set points that differ from the pre-session values, SERVO asks after PWM resumes: `Restore your supply set points 5.50V 2.00A on the live output?` YES applies both values. Raising voltage from 6.0V or below to above 6.0V first opens HV SERVOS ONLY. NO keeps the session values until the next OFF.

The question appears once per LEAVE. It waits for SERVO keypads, lists, warnings, questions and the OUTPUT ON hold. It is suppressed while a port owns the supply, after OFF, error or STOP, during a limit run, or after a set point has changed.

Only LEAVE triggers this question. RELEASE PORT, STOP, an error, link loss, panel or IOmcu restart, an unsolicited port release, or a switch to the port's own supply ends ownership without asking. Session set points then remain until the next OFF. Hitec programming uses the same behavior.

On the [rbmod carrier](https://github.com/subtilitas/rcbench/wiki/Carrier), programming ports use a separate fixed 5V supply switched by GPIO 21. MORE shows `PROGRAMMING VOLTAGE: 5V FIXED, GPIO 21`. The session switches this rail wherever the table below describes switching the PD mini output. During ownership, the PROG 5V box below PD MINI reads `HELD BY PROG PORT n` and accepts no press. Outside a session, hold PROG 5V ON for 2s or tap PROG 5V OFF to control it.

With carrier protocol 4.36 or later, tap the PORT label before CONNECT to choose PROGP01 (GPIO 13), PROGP02 (GPIO 14), PROGP03 (GPIO 15) or PROGP04 (GPIO 0). This sets SERVO TESTER and requests SERVO PWM on that port, NONE on the other three. The tester waits for confirmation before sending commands. Port selection is locked while ARMED or arming, during a test, sweep or limit run, or while a programmer holds a port. A conflicting PWM output on the same slice also blocks the port choice.

![The KST programmer holding the 5V](view:servo-kst-rbmod)

## The power cycle

KST and Chaservo servos enter programming mode after power-up. Written settings take effect at the next power-up. On the tested Chaservo, CONNECT was ignored while power stayed on, and PWM commands were ignored after writing until power was cycled.

| Step | A PD mini with its output on | No PD mini |
|---|---|---|
| CONNECT | the output off for 1s, on, the line low for 1s, then the entry | the entry 250ms after the tap: switch the servo's supply off and on, tap CONNECT 1s later |
| LEAVE after a write | the output off for 1s, on, the line low for 1s, then PWM | PWM; the values act after the servo's supply is switched off and on |
| LEAVE with nothing written | PWM after WAIT BEFORE PWM, the supply as it is | the same |

If the output is off, CONNECT switches it on and enters programming. ABORT or STOP during the 1s off period leaves it off: `The supply cycle was stopped. The PD mini output stays off.`

![CONNECT with the output on](view:servo-kst-supply-on)
![The 1s off](view:servo-kst-cycling)

![An operation running](view:servo-kst-working)

## The SERVO tab

![A servo read](view:servo-kst-read)

| Row | Button | Does |
|---|---|---|
| PORT | CONNECT, ABORT, LEAVE | takes the port, ends the running operation, gives the port back |
| SERVO | READ ALL | reads the 32 registers, each until 3 of at most 5 reads agree |
| TYPE | V6?, V8?, V6, V8 | the servo type, V6 or V8, as the operator sets it |
| FILES | CHOOSE | saves the servo's values to the card, loads a file |
| MORE | CHOOSE | in programming: VERIFY, DISCARD EDITS and, with KST EXPERT MODE ON, RELEASE PAIRING |
| ENABLE WRITE TO SERVO | OFF, ON | the write switch |
| RESTORE | CHOOSE | writes a backup back |
| (no label) | WRITE n VALUES | writes the changed values |

No register identifies the series. V8 wing servos can have 20k_sel set as on V6. After reading, TYPE shows the type suggested by that bit with a question mark, such as `V6?`, and asks you to check the servo label. Tap TYPE to confirm V6 or V8.

The type belongs to the session and is included in saved files. Loading requires a matching type; if none is set, selecting a file asks for it first. The type is never written to the servo.

![The TYPE list](view:servo-kst-type)
![A type set](view:servo-kst-type-set)

![A servo of unknown layout: read only](view:servo-kst-readonly)

A servo whose registers do not fit the known layout is shown and never written: `Read only. This servo type is unknown.`

## Editing

![Edited values on TUNE](view:servo-kst-tune)

Edits remain in a draft on the panel until WRITE. Changed values have an `*` and accent fill. Tap a number for a keypad constrained by the hard limits, STRETCH for a choice list, or a switch to toggle it.

![The keypad with the range the limits accept](view:servo-kst-keypad)
![The STRETCH list](view:servo-kst-stretch)

| Tab | Rows |
|---|---|
| TUNE | DEAD BAND, MAX. DUTY (10 to 98%), BOOST (up to 3000us), STRETCH (x1 to x32), SOFT START, SPEED LIMIT, SPEED; DEAD BAND, BOOST and STRETCH with KST EXPERT MODE ON only |
| PULSE | NEUTRAL (1360 to 1640us), PULSE LOWER (from 500us), PULSE UPPER (up to 2500us), LEFT ANGLE, RIGHT ANGLE, REVERSION |
| FAILSAFE | MOVE ON SIGNAL LOSS, POSITION (500 to 2500us), AFTER (0.4 to 13.0s) |
| LOCKED | 20K SEL, LOCK, MODE and the protection block; shown, never edited; the tab shows with KST EXPERT MODE ON only |

![The PULSE tab](view:servo-kst-pulse)
![The FAILSAFE tab](view:servo-kst-failsafe)

LEFT ANGLE and RIGHT ANGLE are the servo's own numbers; their unit is unknown.

![A value a hard limit refuses](view:servo-kst-hard)

A tap that would break a hard limit changes nothing and names the limit.

![CHECK THIS CHANGE](view:servo-kst-soft)

A value within hard limits but outside a soft limit opens CHECK THIS CHANGE. It shows the servo's value, proposed value and one line per affected limit. Hold HOLD 2s TO APPLY for 2s to accept it; CANCEL preserves the draft. A [limit run](servo.md#the-limit-run) uses its 2s start hold as consent for LEFT ANGLE or RIGHT ANGLE below 50 and does not ask again here.

![The LOCKED tab](view:servo-kst-locked)

## KST EXPERT MODE

KST EXPERT MODE on [SERVO settings](servo-settings.md#dut) > DUT decides what the programmer shows. It is OFF by default and kept across restarts. Hold the row 2s to turn it ON; a shorter press shows `Hold 2s to switch KST EXPERT MODE on.` on the band. Tap it to turn it OFF.

With the mode OFF, DEAD BAND, BOOST and STRETCH leave TUNE, RELEASE PAIRING leaves MORE, and the LOCKED tab leaves the strip. TUNE closes up and the strip holds 4 tabs. DEAD BAND and BOOST can cause back-and-forth movement, buzzing and heat, STRETCH oscillation; what RELEASE PAIRING does inside the servo and what the LOCKED values mean is not known.

![TUNE with KST EXPERT MODE OFF](view:servo-kst-tune-expert-off)
![MORE with KST EXPERT MODE OFF](view:servo-kst-more-expert-off)

A file load or a RESTORE still writes the whole image. The write page, CHECK THIS FILE and the restore's page name each hidden value that changes, first and in the warning colour: `DEAD BAND (KST EXPERT MODE): 2.0 -> 1.6us`. A draft keeps an edit of a hidden value when the mode turns OFF, and the write page names it.

![The write page naming the hidden values](view:servo-kst-write-expert-off)

## Writing

![Enabling the write](view:servo-kst-enable)

ENABLE WRITE TO SERVO starts off. Tap OFF to read the risks, then hold HOLD 2s TO ENABLE for 2s. Write access is revoked whenever SERVO is entered, on STOP, CONNECT or LEAVE, on link loss, or when the servo's state is uncertain.

![Three changed values, the switch on](view:servo-kst-changes)
![The write page](view:servo-kst-write)

WRITE n VALUES opens the write page with the changes, `DEAD BAND: 2.0 -> 1.6us`. HOLD 2s TO WRITE starts the write after 2s.

1. The first write of a session saves the servo's values to a new file on the SD (Secure Digital) card.
2. The panel records the write in `BENCH/KST/CONF/KST.TXT`, or in memory without a card.
3. The IOmcu checks the values against the same limits, plans the register writes and runs them.
4. Every register written is read back. A write that does not take is undone.

![A write running: do not unplug](view:servo-kst-writing)
![A write done](view:servo-kst-written)

A value written and read back is in the servo and acts from its next power-up. With a PD mini the message says `The values take effect after LEAVE switches the supply off for 1s, then on.`; without one `The written values take effect after the servo supply is switched off and on.`

![A write with a PD mini](view:servo-kst-written-supply)

## Files

![The files on the card](view:servo-kst-files)

FILES lists servo files from `BENCH/KST/CONF`, 5 per page, with their servo type. The first entry saves the servo's values to a new name, starting at `KST001`. Names can contain up to 16 characters.

![The name of a new file](view:servo-kst-name)
![A file loaded](view:servo-kst-file-loaded)

Tap a file to load it into the draft. It reaches the servo only after writing. A type mismatch, damaged file or value outside a hard limit prevents loading; the status line explains why. Without a confirmed type, `SELECT SERVO TYPE BEFORE LOADING` opens first.

![The type asked before a load](view:servo-kst-type-first)

## Restore and MORE

![The restore list](view:servo-kst-restore-list)
![A restore of the session's backup](view:servo-kst-restore)

RESTORE offers the session backup (the first values read after CONNECT), the values before an interrupted write, or `Settings before the limit run.` Every restore requires write access and a 2s hold.

![MORE in programming mode](view:servo-kst-more)

- VERIFY reads all registers and compares them with the image held.
- DISCARD EDITS sets the draft back to the servo's values.
- RELEASE PAIRING, with KST EXPERT MODE ON, sets register 1D to 00 after a 2s hold. What the servo does with the value is not known.

## An interrupted write

![An interrupted write](view:servo-kst-doubt)

A write is considered interrupted after link loss, a board restart, ABORT or stop, or a missing result at the deadline. The tab shows `Writing was interrupted. The settings may be incomplete.` until the servo is read again. READ ALL compares the current settings with the write record and offers RESTORE.

![The servo holds a mix of old and new values](view:servo-kst-journal)

## Leaving

![LEAVE PROGRAMMING](view:servo-kst-leave)
![LEAVE after a write with a PD mini](view:servo-kst-leave-cycle)

LEAVE, CLOSE or the home tag in programming mode opens LEAVE PROGRAMMING. Returning to servo PWM may move the servo. After a write with a PD mini, the page explains that power will cycle off for 1s first. Without a PD mini, it explains that written values require a manual power cycle. If nothing was written, the supply stays unchanged.

Hold HOLD 2s TO LEAVE for 2s. The port holds its line low for WAIT BEFORE PWM, then returns the pin to the tester. Leaving the SERVO screen does not perform LEAVE: the session retains the port and reopens on PROGRAM when you return. Within the programmer, home closes an open keypad, list, warning or keyboard first.

## Hitec D receive

MORE offers Hitec D receive while the tester owns the port. The default PULL-UP requires a 2.2kΩ resistor between signal and servo supply. PRECHARGE needs no resistor: the IOmcu drives the pad high for 100ns every 1.09us and samples it 113ns later.

The setting applies at the next OPEN of a D-series line, so it cannot change during a session. Use LEAVE, change the setting, then CONNECT. Classic HS-5xxx and HS-7xxx servos require neither method and ignore the setting.

IOmcu 0.21.1 and 0.21.2 (protocol 4.23) always receive with PULL-UP. If PRECHARGE is selected, the list shows `IOmcu older than 0.22.0: receives with PULL-UP.` Before 0.21.1, there is no Hitec line; selecting PRECHARGE shows `IOmcu older than 0.21.1: no Hitec line.`

![Hitec D receive's list](view:servo-hitec-rx)

## Hitec: MOVE SERVO

MOVE SERVO on the Hitec programmer's TRAVEL tab moves a D series servo's horn to the position being edited, inside the held session, while the bench stays SAFE. Nothing is saved in the servo until WRITE n VALUES.

![MOVE SERVO's page](view:servo-hitec-move)
![MOVE SERVO on, LEFT END followed](view:servo-hitec-move-on)

- Tap OFF, read the page and hold HOLD 2s TO MOVE for 2s. A shorter press shows `Hold 2s to move the servo.` and starts nothing.
- It needs a PD mini answering with its output on: STOP switches the servo's supply off. Without one, the rbmod carrier's fixed 5V included, it says `Moving the servo needs a PD mini with its output on.`
- DIRECTION is CW in the servo and in the draft; the CCW mapping is not measured.
- NEUTRAL, LEFT END and RIGHT END get `-` and `+` keys of 8 counts, 0.11°, repeating while held. The horn follows the row last stepped, its label in the accent colour; it starts on NEUTRAL. A tap on the value opens the keypad.
- The horn moves 2.0° at most a pass and stays inside the reach of the stored end points, 2606 to 13743 on a factory D485HW.
- A horn that lags more than 2.0° on 2 passes, a current at 80% of the current limit for 0.4s, or an AS5600 angle that does not follow moves the horn back 1.0° and ends the loop: `Servo blocked at 6000. Moved back to 6167.`
- A tap on ON, a tab change, CLOSE or the home tag ends it: target 3000, a restart of the servo, and a read held against the start, about 2.6s in the host model. `MOVE SERVO is OFF. The stored values read back unchanged.`
- STOP, a switch to SAFE at the bench, a lost link, the supply going off and a failed read end it at once with every output off.

### FIND STOP

While MOVE SERVO is on, FIND STOP takes DIRECTION's row. It searches for the mechanical stop of one side and sets that end in the draft; it writes nothing.

![FIND STOP searching on the left](view:servo-hitec-move-find)
![A stop found on the left](view:servo-hitec-move-found)

- Tap LEFT END or RIGHT END. The horn goes to NEUTRAL, then 0.5° outward every 2 passes, up to the edge of the reach.
- A bind sets that end 1.0° inside the stop, marked `*`: `Stop at 4840. LEFT END set to 4915 in the edits.` The horn then follows that end.
- No bind up to the edge, 40s passed, or a second tap on the key: the edits stay as they were.
- WRITE n VALUES after MOVE OFF writes the end found.

The thresholds are chosen, not measured with a horn against a block. [MOVE SERVO on the Hitec programmer](https://github.com/subtilitas/rcbench/wiki/HitecMove) holds every message and limit.

## Related pages

- [Servo](servo.md) and [Servo settings](servo-settings.md)
- [Servo programmer reference](https://github.com/subtilitas/rcbench/blob/main/docs/ServoProgrammer.md): every message, limit and limitation.
