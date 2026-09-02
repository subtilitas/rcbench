# BLHeli_32 parameters

<sub>**English** · [Deutsch](BLHeli32-de.md)</sub>

BLHeli_32 is not in the programmer's ESC (electronic speed controller) list.
This page states what the bench does with a BLHeli_32 ESC and what it does not.

## Supported

- Identification: MCU (microcontroller unit) and bootloader revision.
- Direction, direction reversal, 3D mode, beacon and save-settings, as DShot
  special commands on the signal line.
- Telemetry.

## Not supported

Reading or writing the parameter list: timing, PWM (pulse-width modulation)
frequency, startup power, current limit and the rest. The settings are stored
in a form the bench cannot read, and the information needed to read them is not
published.

## Status

The rights holder was asked in August 2026 whether the project could obtain
what reading and writing the parameters requires. The request was declined. The
item is closed.

## Alternative

An ESC reflashed to AM32 is fully supported by the programmer.
