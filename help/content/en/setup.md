---
screen: SETUP
title: Setup
order: 60
---

# Setup

SETUP holds the panel's settings in three categories: ESC / BENCH, APPLICATION and INTERFACES. It opens from the SETUP tile on the [menu](overview.md) on a page of tiles, 3 by 3; the band names it SETTINGS. A category's tile opens its list across the screen's width.

## The tiles

![The tiles, everything saved](view:setup)

| Tile | Second line | What it opens |
|---|---|---|
| ESC / BENCH | `8 SETTINGS` | the list of ESC / BENCH |
| APPLICATION | `10 SETTINGS` | the list of APPLICATION |
| INTERFACES | `26 SETTINGS` | the list of INTERFACES |
| OUTPUTS | `PROTOCOLS AND PINS` | [Outputs](outputs.md): the role of each coprocessor pin, as a list |
| PICK A PIN | `ON THE BOARD DRAWING` | the [pin picker](picker.md): the same, on a drawing of the board |
| UPDATES | `RELEASES ON GITHUB` | [Updates](updates.md): the check for a newer panel firmware release and the reminder switch |

Three slots stay free. The save button sits at the bottom right, as on every list. A change left unsaved in any category shows on it:

![The tiles with a value unsaved](view:setup-hub-dirty)

The home tag at the top left climbs one level at a time:

| From | The home tag leads to |
|---|---|
| a category's list | the tiles; the list keeps its position |
| the tiles | the menu; with a value unsaved, the warning first ([Leaving with values unsaved](#leaving-with-values-unsaved)) |
| OUTPUTS, PICK A PIN, UPDATES | the tiles |

## Changing a value

![ESC / BENCH, everything saved](view:setup-esc)

The list spans the screen and shows 6 rows. Under it a strip holds the category's name, RESET CATEGORY and the save button. The strip ends 4 px above the band an alert shows across the bottom, so an alert covers no key, row or button.

A row shows the setting, a help line and its value with its unit. A number has `-` and `+` keys; a switch or a choice has `<` and `>`.

- **Tap**: a key steps once when the finger lifts within 8 px of the press, inside the key.
- **Hold**: a held key steps first after 0.45 s, then 8 times a second, and 30 times a second from 2.25 s.
- **Scroll**: a vertical drag of more than 8 px anywhere on the list scrolls it and changes no value.
- One finger at a time: a second finger does nothing while one presses or scrolls.

A changed value is in force at once. RESET CATEGORY sets every value of the open category back to its default.

| Button | What it does |
|---|---|
| RESET CATEGORY | the open category's defaults, at once |
| SAVED, SAVE, WHEN IDLE, NOT SAVED | the save button; see below |

## Keeping the values

![A changed value, with SAVE offered](view:setup-dirty)

A changed value is not written to flash until SAVE is pressed. A value stepped back to the one stored is no change: the button reads `SAVED` again, and a write asked for is dropped.

| Label | Meaning |
|---|---|
| `SAVED` | nothing is unwritten; the button does nothing |
| `SAVE` | something is unwritten; a press asks for a write |
| `WHEN IDLE` | the write waits for a quiet bench |
| `NOT SAVED` | the store refused the write; a press tries again |

The write happens on the first frame with the bench SAFE, the supply output off and no board drawing being fetched or kept. While ARMED the button reads `WHEN IDLE` until the bench is SAFE. Values not saved last until the panel is switched off. Leaving a list or the tiles writes nothing.

`NOT SAVED` cannot say how much reached the flash: a refusal part way leaves the keys written before it, so the next start can load a mix of new and old values. After a refusal the button stays `NOT SAVED` or `SAVE` until a save succeeds, even with every value stepped back.

## Leaving with values unsaved

![One value unsaved, at the home tag](view:setup-leave)

The home tag on the tiles leads to the menu. While the save button reads `SAVE` or `NOT SAVED`, it opens a warning first. The warning lists each value that differs from the stored one as `name: stored -> now`, with the value and unit as the list shows them. The stored values are the ones loaded at start-up or written by the last successful save. The list includes the DUT (device under test) name and the values set on other screens, such as SERVO or ESC STICK: one save keeps them all.

| Button | What it does |
|---|---|
| STAY | closes the warning; the tiles show again, a list keeps its category and position |
| DISCARD | sets every listed value back to its stored one, as a step by hand would, then opens the menu |
| SAVE | asks for a write, as the save button does, then opens the menu |

- DISCARD sends a pin or a port back to the coprocessor at once. A PD mini wiring set back switches the supply output off, as a change by hand does.
- While the bench is ARMED, DISCARD is dimmed and does nothing; the line `DISCARD waits until the bench is SAFE.` says so. STAY and SAVE work.
- PWM target and Port PWM are not set back while the servo programmer holds the servo port; they stay unsaved.
- With more than 10 changes, 9 lines show and the last line reads `and 3 more` for 12.
- After a refused save the line `Saving failed. Tap SAVE to try again.` shows under the list. DISCARD then also asks for a write of the values put back, since the flash can hold part of the refused write.

![Twelve values unsaved](view:setup-leave-many)

No warning shows when nothing is unsaved, when a save is asked (`WHEN IDLE`), or on the way to OUTPUTS, PICK A PIN and UPDATES. STOP stops the bench and leaves the warning open. A screen the panel opens by itself, such as the bus-fault screen after a link loss, closes the warning and leaves the values in force and unsaved.

## ESC / BENCH

| Setting | Range | Default | What it does |
|---|---|---|---|
| Cells | 1 to 14 S | 6 | stored; no screen uses it in this release |
| Capacity | 0 to 30000 mAh, 100 mAh steps | 5000 | stored; no screen uses it in this release |
| Internal R | 0 to 200 mOhm | 18.0 | stored; no screen uses it in this release |
| Motor poles | 2 to 42, steps of 2 | 14 | rpm is eRPM (electrical rpm) divided by poles/2 |
| Rated kV | 0 to 10000 rpm/V, steps of 10 | 0 | the kV on [MOTOR & ESC](motor.md#kv-rpm-v-and-eff); 0 leaves it empty |
| Idle pulse | 800 to 1600 us, steps of 10 | 1000 | the PWM (pulse-width modulation) width at 0 % throttle |
| Full pulse | 1400 to 2400 us, steps of 10 | 2000 | the PWM width at 100 % throttle |
| Ramp limit | 5 to 300 %/s, steps of 5 | 55 | the throttle slew of the panel's model, used while no coprocessor answers |

Idle pulse and Full pulse go to the coprocessor at link-up and on every change. The coprocessor ramps no throttle: a bound pin steps to the command.

## APPLICATION

![APPLICATION](view:setup-application)

| Setting | Range | Default | What it does |
|---|---|---|---|
| Theme | DARK, LIGHT | DARK | the colour theme, from the next frame |
| Brightness | 20 to 100 %, steps of 5 | 100 | scales the palette |
| Contrast | 60 to 140 %, steps of 5 | 100 | pushes colours away from mid grey |
| Backlight | ON, OFF | ON | stored; no screen uses it in this release; the board wires the backlight as on or off only |
| Language | ENGLISH, DEUTSCH | ENGLISH | the interface language, from the next frame, without a restart |
| Units | METRIC, IMPERIAL | METRIC | stored; no screen uses it in this release |
| Dim after | 0 to 60 min, steps of 5 | 0 | stored; no screen uses it in this release |
| One finger pans | ON, OFF | OFF | one finger pans a zoomed plot on [Logs](logs.md#the-plot) |
| Rotary knob | ON, OFF | OFF | an MT6701 at 0x06 on the panel's I2C (Inter-Integrated Circuit) terminal turns the throttle on MOTOR & ESC or the horn on SERVO |
| Knob scale | 90 to 720 deg, steps of 10 | 270 | the knob angle that moves the slider across its whole span |

![SETUP in the light theme](view:setup-light)

### The rotary knob

- The knob moves a value by how far it turns, never to where it points. At 270 deg a quarter turn moves the throttle 33.3 points.
- It never switches to ARMED. While the bench is SAFE a turn moves nothing and is not kept.
- The first reading after power-up, or after the sensor stopped answering, sets a reference and moves nothing.
- A finger on the throttle track or the SERVO dial owns the value while it is down.
- The sensor is read every 10 ms. A step over 90 deg between two readings is dropped.

The sensor is an MT6701 in its I2C mode, MODE pin high, at 0x06. The panel never addresses 0x14, 0x20 to 0x27, 0x30 to 0x3F or 0x5D for it, where the board's CH422G I/O expander and GT911 touch controller answer, nor 0x00 to 0x03 or 0x78 to 0x7F, which the I2C specification reserves. With no sensor fitted, Rotary knob ON probes 0x06 every 100 ms. The host tests show that this changes nothing else; on the panel it is not measured. Wiring: [Building](https://github.com/subtilitas/rcbench/blob/main/docs/Building.md#rotary-knob-wiring).

## INTERFACES

INTERFACES holds the current monitors, the output encoder, the phase tap, the vibration sensor, the optical tacho and the PD mini. [Interfaces](setup-interfaces.md) describes them.

## Related pages

- [Interfaces](setup-interfaces.md)
- [Outputs](outputs.md) and [Pin picker](picker.md)
- [Updates](updates.md)
- [Supply](supply.md) and [Servo settings](servo-settings.md): the settings kept on their own screens.
