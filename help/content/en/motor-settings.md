---
screen: none
title: Motor settings
order: 11
---

# Motor settings

SETTINGS is the third tab on [MOTOR & ESC](motor.md). Its left pane contains the bench's 8 ESC (electronic speed controller) settings, arranged in two columns: PACK and MOTOR on the left, THROTTLE OUTPUT on the right. The main right column remains visible and usable, with its readouts, TARGET, ARM, totals and RESET PEAKS.

![SETTINGS](view:motor-settings)

Each 48px row shows a setting, its value and unit, and a help line. Tap the value to open a keypad over the left pane and tab strip. The keypad shows the setting and its range. OK accepts an in-range value. An out-of-range value leaves the keypad open and highlights the range in the warning colour. CANCEL or the home tag closes the keypad without changes. Values are rounded to the setting's step: 4321mAh becomes 4300mAh.

![The keypad for Capacity](view:motor-settings-keypad)

## The settings

| Heading | Setting | Range | Default | What it does |
|---|---|---|---|---|
| PACK | Cells | 1 to 14S | 6 | stored; no screen uses it in this release |
| PACK | Capacity | 0 to 30000mAh, 100mAh steps | 5000 | stored; no screen uses it in this release |
| PACK | Internal R | 0 to 200mΩ | 18 | stored; no screen uses it in this release |
| MOTOR | Motor poles | 2 to 42, steps of 2 | 14 | rpm is eRPM (electrical rpm) divided by poles/2 |
| MOTOR | Rated kV | 0 to 10000rpm/V, steps of 10 | 0 | the kV on [LIVE](motor.md#kv-rpm-v-and-eff); 0 leaves it empty |
| THROTTLE OUTPUT | Idle pulse | 800 to 1600us, steps of 10 | 1000 | the PWM (pulse-width modulation) width at 0% throttle |
| THROTTLE OUTPUT | Full pulse | 1400 to 2400us, steps of 10 | 2000 | the PWM width at 100% throttle |
| THROTTLE OUTPUT | Ramp limit | 5 to 300%/s, steps of 5 | 55 | the throttle slew of the panel's model, used while no IOmcu answers |

Idle pulse must be at least 100us below Full pulse. Otherwise, the keypad stays open, leaves the value unchanged and shows `Idle pulse at most 1900us (100us below Full pulse)` or `Full pulse at least 1100us (100us above Idle pulse)` in the warning colour. This check is necessary because the allowed ranges, 800 to 1600us and 1400 to 2400us, overlap.

Idle pulse and Full pulse are sent to the IOmcu at link-up and after each change. The IOmcu applies throttle commands immediately to bound pins, without a ramp.

Idle pulse and Full pulse can be changed only on a SAFE bench. They are locked while the bench is ARMED, ARM is held, or an ARM or DISARM request awaits a response. Their help line then reads `Set on a SAFE bench only` in the warning colour. An open keypad for either setting closes when ARM is pressed or the bench leaves SAFE. The other 6 rows can be changed at any time.

A change to Idle pulse or Full pulse reaches the IOmcu on the first poll at least 300ms after the last edit. Until then, with the IOmcu connected, ARM is refused with `Pulse range not yet sent to the IOmcu. Hold ARM again.` and the bench stays SAFE. Hold ARM again: the 2s hold allows time for delivery.

If the IOmcu does not accept the pulse pair, ARM remains refused until the pair is applied. The band explains what to change:

| Band | What to do |
|---|---|
| `No pulse range from the IOmcu. Update the IOmcu firmware, then hold ARM again.` | update the IOmcu: the panel offers the image it carries when the IOmcu connects |
| `Idle pulse less than 100us below Full pulse. Change one, then hold ARM again.` | lower Idle pulse or raise Full pulse |
| `IOmcu rejected the pulse range. Change Idle pulse, Full pulse or TARGET.` | shorten the pulse, or bind the motor pin at a slower rate on OUTPUTS: a pulse longer than its PWM frame is refused |
| `The IOmcu has a different pulse range. Change Idle pulse or Full pulse.` | change either value; the panel sends the pair again |

Writing a binding on OUTPUTS or reconnecting the IOmcu also resends the pulse pair.

## Keeping the values

A changed value takes effect immediately and requests a save. The panel writes the settings on the first frame when the bench is SAFE, the supply output is off, and no board drawing is being fetched or saved. The write includes every unsaved setting, including changes made on SETUP. The footer shows the save status:

| Line | Meaning |
|---|---|
| `SAVED` | all settings are saved |
| `SAVE PENDING` | a save is requested and awaits a quiet bench |
| `NOT SAVED` | the write failed; values remain in effect but unsaved |
| `SETUP CHANGES NOT SAVED` | changes from SETUP or another screen are unsaved, with no save requested; the next change here saves them too |

After a failed write, the values remain unsaved. The next change requests another write. Leaving SETUP also lists them in its [unsaved-values warning](setup.md#leaving-with-values-unsaved).

## Related pages

- [Motor and ESC](motor.md)
- [Outputs](outputs.md): which pins the throttle drives.
- [Setup](setup.md): the save button and the warning on leaving.
