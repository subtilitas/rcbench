---
screen: none
title: Motor settings
order: 11
---

# Motor settings

SETTINGS, the third tab of [MOTOR & ESC](motor.md)'s strip, holds the bench's 8 ESC (electronic speed controller) settings in the left column under three headings: PACK, MOTOR and THROTTLE OUTPUT. The right column stays in place and works: the readouts, TARGET, ARM, the totals and RESET PEAKS.

![SETTINGS](view:motor-settings)

A row shows the setting, a help line and its value with its unit. A tap on the value opens the keypad over the left column and the strip. The keypad names the setting and its range. OK takes a value inside the range; a value outside it keeps the keypad open and names the range in the warning colour. CANCEL, and the home tag, close the keypad and change nothing. A typed value lands on the setting's step: 4321 mAh becomes 4300 mAh.

![The keypad for Capacity](view:motor-settings-keypad)

## The settings

| Heading | Setting | Range | Default | What it does |
|---|---|---|---|---|
| PACK | Cells | 1 to 14 S | 6 | stored; no screen uses it in this release |
| PACK | Capacity | 0 to 30000 mAh, 100 mAh steps | 5000 | stored; no screen uses it in this release |
| PACK | Internal R | 0 to 200 mOhm | 18.0 | stored; no screen uses it in this release |
| MOTOR | Motor poles | 2 to 42, steps of 2 | 14 | rpm is eRPM (electrical rpm) divided by poles/2 |
| MOTOR | Rated kV | 0 to 10000 rpm/V, steps of 10 | 0 | the kV on [LIVE](motor.md#kv-rpm-v-and-eff); 0 leaves it empty |
| THROTTLE OUTPUT | Idle pulse | 800 to 1600 us, steps of 10 | 1000 | the PWM (pulse-width modulation) width at 0 % throttle |
| THROTTLE OUTPUT | Full pulse | 1400 to 2400 us, steps of 10 | 2000 | the PWM width at 100 % throttle |
| THROTTLE OUTPUT | Ramp limit | 5 to 300 %/s, steps of 5 | 55 | the throttle slew of the panel's model, used while no IOmcu answers |

Idle pulse and Full pulse go to the IOmcu at link-up and on every change. The IOmcu ramps no throttle: a bound pin steps to the command.

Idle pulse and Full pulse take a value on a SAFE bench only. While the bench is ARMED, ARM is held, or an ARM or a DISARM waits for its answer, their help line reads `Set on a SAFE bench only` in the warning colour and the value takes no tap. A keypad open for one of them closes when ARM is pressed or the bench leaves SAFE. The other 6 rows take a value at any time.

## Keeping the values

A changed value is in force at once and asks for a write. The panel writes on the first frame with the bench SAFE, the supply output off and no board drawing being fetched or kept. The settings are one store: the write keeps every unsaved value, those changed on SETUP included. The line under the rows says where the store stands:

| Line | Meaning |
|---|---|
| `SAVED` | nothing is unwritten |
| `SAVE PENDING` | a write is asked and waits for a quiet bench |
| `NOT SAVED` | the store refused the write; the values stay in force and unsaved |
| `SETUP CHANGES NOT SAVED` | values changed on SETUP or another screen are unsaved and no write is asked; the next change here writes them too |

After a refused write the values stay unsaved. The next change asks for a write again, and SETUP's warning on leaving lists them ([Leaving with values unsaved](setup.md#leaving-with-values-unsaved)).

## Related pages

- [Motor and ESC](motor.md)
- [Outputs](outputs.md): which pins the throttle drives.
- [Setup](setup.md): the save button and the warning on leaving.
