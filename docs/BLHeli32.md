# BLHeli_32 parameters

<sub>**English** · [Deutsch](BLHeli32-de.md)</sub>

Why the programmer's ESC list does not include BLHeli_32, and what the bench
does with one instead.

## What works

A BLHeli_32 ESC is a recognised, drivable, measurable ESC on this bench:

- it is **identified** — the bench names its MCU and bootloader revision;
- **direction, direction reversal, 3D mode, beacon and save-settings** work,
  because those are DShot special commands on the signal wire;
- **telemetry** is unaffected.

## What does not

The parameter list — timing, PWM frequency, startup power, current limit and
the rest. Those settings are stored in a form this bench cannot read, and the
information needed to read them is not published.

## We asked

In August 2026 the rights holder was asked whether this project could have what
it needs to read and write them. **He declined**, and that settles it: the
question is closed rather than pending, and it is written down here so nobody
spends an evening reopening it.

## What to do instead

An ESC reflashed to **AM32** is open, and this bench programmes it fully.
