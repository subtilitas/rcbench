# Parameters of ESCs with BLHeli_32 firmware

<sub>**English** · [Deutsch](BLHeli32-de.md)</sub>

The programmer's ESC (electronic speed controller) list has no entry for
BLHeli_32 firmware. This page states what the bench does with an ESC that runs
BLHeli_32 firmware and what it does not.

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

An ESC reflashed to AM32 firmware can use the [signal-wire session](EscWire.md).
