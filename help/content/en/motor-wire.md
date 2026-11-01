---
screen: none
title: ESC signal wire
order: 13
---

# ESC signal wire

The session reads and writes the settings of an ESC (electronic speed controller) with AM32, Bluejay, BLHeli_S or ESCape32 over its signal wire. Open it on MOTOR & ESC > PROGRAM. The row is dimmed until the IOmcu reports the capability.

No frame has reached a real ESC. SimonK is not supported.

## Before CONNECT

1. Take the propeller off.
2. Connect signal and ground only, the ESC's centre wire disconnected. A BEC (battery eliminator circuit) would feed the bench's rail.
3. Unplug servos.
4. Check `TARGET`, `POWER`, `VOLTAGE` and `LIMIT`. `VOLTAGE` and `LIMIT` follow SUPPLY until you type others.
5. Hold `HOLD 2s TO CONNECT` for 2s.

With `BY HAND`, connect the ESC's power when asked, and tap `POWER IS ON` within 60s.

## Settings

Tap a number for the keypad, or a choice for its list. Each editor shows one line of help. An edit stays on the panel until `WRITE n VALUES`.

`READ ONLY` names its reason. Its rows are dimmed and it has no write.

## Write

The page lists the changes, each hazardous field first, with `CHECK` and its new value. Hold 2s. The first write of a session saves the backup first. With no card, the page asks.

A write on BLHeli_S or Bluejay takes about 3848ms. Keep the power on.

## Files

`FILES` lists `ESCW001` to `ESCW999` on the card. A file goes into the draft, for the same firmware and layout. An AM32 that reads erased offers `RESTORE FROM BACKUP` and nothing else.

## Leave

`BACK` opens the leave page: hold 2s. ESCape32 has no LEAVE: a power cycle ends its session, and with `BY HAND` you disconnect its power.
