---
screen: SETUP
title: Setup
order: 60
---

# Setup

SETUP holds the panel's settings in three categories: ESC / BENCH, APPLICATION and INTERFACES. It opens from the SETUP tile on the [menu](overview.md); the band names it SETTINGS. The left column picks the category and holds OUTPUTS, PICK A PIN, UPDATES, RESET CATEGORY and the save button. The list of the category takes the right.

## Changing a value

![ESC / BENCH, everything saved](view:setup)

A row shows the setting, a help line and its value with its unit. A number has `-` and `+` keys; a switch or a choice has `<` and `>`.

- **Tap**: a key steps once when the finger lifts within 8 px of the press, inside the key.
- **Hold**: a held key steps first after 0.45 s, then 8 times a second, and 30 times a second from 2.25 s.
- **Scroll**: a vertical drag of more than 8 px anywhere on the list scrolls it and changes no value.
- One finger at a time: a second finger does nothing while one presses or scrolls.

A changed value is in force at once. RESET CATEGORY sets every value of the open category back to its default.

| Button | What it does |
|---|---|
| OUTPUTS | opens [Outputs](outputs.md): the role of each coprocessor pin, as a list |
| PICK A PIN | opens the [pin picker](picker.md): the same, on a drawing of the board |
| UPDATES | opens [Updates](updates.md): the check for a newer panel firmware release and the reminder switch |
| RESET CATEGORY | the open category's defaults, at once |
| SAVED, SAVE, WHEN IDLE, NOT SAVED | the save button; see below |

## Keeping the values

![A changed value, with SAVE offered](view:setup-dirty)

A changed value is not written to flash until SAVE is pressed.

| Label | Meaning |
|---|---|
| `SAVED` | nothing is unwritten; the button does nothing |
| `SAVE` | something is unwritten; a press asks for a write |
| `WHEN IDLE` | the write waits for a quiet bench |
| `NOT SAVED` | the store refused the write; a press tries again |

The write happens on the first frame with the bench SAFE, the supply output off and no board drawing being fetched or kept. While ARMED the button reads `WHEN IDLE` until the bench is SAFE. Values not saved last until the panel is switched off. Leaving the screen writes nothing.

`NOT SAVED` cannot say how much reached the flash: a refusal part way leaves the keys written before it, so the next start can load a mix of new and old values.

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
| Rotary knob | ON, OFF | OFF | an AS5600 on the panel's I2C (Inter-Integrated Circuit) terminal turns the throttle on MOTOR & ESC or the horn on SERVO |
| Knob scale | 90 to 720 deg, steps of 10 | 270 | the knob angle that moves the slider across its whole span |

![SETUP in the light theme](view:setup-light)

### The rotary knob

- The knob moves a value by how far it turns, never to where it points. At 270 deg a quarter turn moves the throttle 33.3 points.
- It never switches to ARMED. While the bench is SAFE a turn moves nothing and is not kept.
- The first reading after power-up, or after the sensor stopped answering, sets a reference and moves nothing.
- A finger on the throttle track or the SERVO dial owns the value while it is down.
- The sensor is read every 10 ms. A step over 90 deg between two readings is dropped.

Known limitation: on the Waveshare ESP32-S3 Touch LCD 7 panel the board's CH422G I/O expander answers at I2C addresses 0x20 to 0x27 and 0x30 to 0x3F. That includes the AS5600's fixed address 0x36, so an AS5600 knob cannot be read on that bus.

## INTERFACES

INTERFACES holds the current monitors, the output encoder, the phase tap, the vibration sensor, the optical tacho and the PD mini. [Interfaces](setup-interfaces.md) describes them.

## Related pages

- [Interfaces](setup-interfaces.md)
- [Outputs](outputs.md) and [Pin picker](picker.md)
- [Updates](updates.md)
- [Supply](supply.md) and [Servo settings](servo-settings.md): the settings kept on their own screens.
