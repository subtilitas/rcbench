---
screen: none
title: ESC signal wire
order: 13
---

# ESC signal wire

This session reads and writes ESC (electronic speed controller) settings over the signal wire. It supports AM32, Bluejay, BLHeli_S and ESCape32. Open it under MOTOR & ESC > PROGRAM. The row remains dimmed until the IOmcu reports the required capability.

SimonK is not supported.

## Before CONNECT

1. Remove the propeller.
2. Connect only signal and ground. Leave the ESC's centre wire disconnected: a BEC (battery eliminator circuit) would feed the bench's rail.
3. Unplug servos.
4. Check `TARGET`, `POWER`, `VOLTAGE` and `CURRENT LIMIT`. `VOLTAGE` and `CURRENT LIMIT` follow SUPPLY until you enter different values.
5. Hold `HOLD 2s TO CONNECT` for 2s.

With `BY HAND`, connect the ESC's power when asked, and tap `POWER IS ON` within 60s.

## Settings

Tap a number to open the keypad or a choice to open its list. Each editor shows a help line. Edits remain on the panel until you use `WRITE n VALUES`.

`READ ONLY` explains why writing is unavailable. Its rows are dimmed.

## Write

The write page lists the changes, with hazardous fields first, marked `CHECK` beside their new values. Use `NEXT` and `PREV` for a long list. The hold control becomes available after the last page has been shown. Hold it for 2s. Until writing finishes, only `ABORT` and `STOP` are active. The first write in a session saves a backup before writing; an erased page cannot serve as a backup. Without a card, the page asks for confirmation.

A write on BLHeli_S or Bluejay takes about 3848ms. Keep the power on.

## Files

`FILES` lists `ESCW001` to `ESCW999` on the card. Loading a file copies its values into the draft and requires matching firmware and layout. If an AM32 reads as erased, only `RESTORE FROM BACKUP` is offered.

## Leave

After a stop or fault, the held session offers only `PIN BACK`. Use it before `CONNECT` becomes available. `BACK` opens the leave page, where a 2s hold ends the session. An erased AM32 remains erased until `RESTORE FROM BACKUP`; leaving returns the pin and switches off the supply. ESCape32 has no LEAVE command: a power cycle ends its session. With `BY HAND`, disconnect the ESC power yourself.
