---
screen: none
title: YGE ESCs
order: 14
---

# YGE ESCs

This session reads and writes the parameter table of a YGE ESC (electronic speed controller) over its telemetry wire, on the pin of a programming port. Open it under MOTOR & ESC > PROGRAM, row `YGE ESCs`. The row remains dimmed until the IOmcu reports the required capability.

Run the mode setup (`ESCs via throttle stick`) before the first session: it sets every value.

## Before CONNECT

1. Remove the propeller.
2. Connect only the telemetry signal and ground. Unplug the throttle lead: its center wire (BEC, battery eliminator circuit) stays off the bench.
3. Check `PORT`, `POWER`, `VOLTAGE`, `CURRENT LIMIT` and `BOOT WAIT` (1.0s to 20.0s, 5.0s by default). `VOLTAGE` and `CURRENT LIMIT` follow SUPPLY until you enter different values.
4. Hold `HOLD 2s TO CONNECT` for 2s.

YGE allows batteries only as the ESC's power. With `BY HAND`, connect the ESC's battery when asked and tap `POWER IS ON` within 60s.

## Settings

The tabs are IDENTITY, MODE & BEC, PROTECTION, START, GOVERNOR & RPM, STICK and RAW. A field whose name is in the warning colour is a CHECK field: its new value is named above the footer before you write. Dimmed rows are read-only. Tap a choice to open its list, or a number to open the keypad. A value outside the writable set is refused. Edits remain on the panel until you write them.

`READ AGAIN` reads the table again. With edits not written it asks first: hold 2s to read, or `BACK` to keep the edits.

## Write

`WRITE n VALUES` is a red 2s hold. It is greyed while nothing has changed. The first write of a session saves a backup `YGE001-<type>` to `BENCH/YGE/CONF` first. Without a card the write goes and the old values stay in RAM only. After the write the table is read back and compared.

## Files

`FILES` lists the backups and files in `BENCH/YGE/CONF`. A tap loads a file's values into the draft. A file of another ESC type is refused.

## Leave

`LEAVE` with no edits ends the session at once. With edits not written, a page lists them: hold 2s to leave without writing, or `BACK`. After a stop only `PIN BACK` and, for the page's own stop, `CLEAR STOP` are offered. With `BY HAND`, disconnect the ESC's battery yourself.
