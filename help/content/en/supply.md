---
screen: SUPPLY
title: Supply
order: 30
---

# Supply

SUPPLY controls and records a programmable supply, the PD mini. This USB-PD (USB Power Delivery) trigger communicates with the IOmcu over a UART (universal asynchronous receiver-transmitter) on two pins. Open SUPPLY from the [menu](overview.md); it always starts on LIVE.

## The strip

The top strip switches between LIVE and SETTINGS. LIVE retains its selected PLOT or TABLE view when you return.

| Task | The left column |
|---|---|
| LIVE | the plot or the table, and the set points |
| SETTINGS | the caps, the start values, the trips and the questions ([SETTINGS](#settings)) |

Both tasks share the right column: readings and the CONTROL card, with RESET PEAKS and OUTPUT ON at the bottom, where SERVO and MOTOR & ESC place ARM. The header to the right of the strip names the supply, its state and the allowed set-point range. It remains visible on both tasks, including under the keypad and confirmation dialog.

The home tag returns one level. In a confirmation dialog, it discards the change like CANCEL. In a keypad, it closes the editor without changing the value. From LIVE or SETTINGS with no dialog open, it returns to the menu; switching tasks does not add a navigation level.

## The supply model

![SUPPLY with the PD mini disabled: SIMULATED](view:supply)

With the PD mini disabled on the [PD MINI tab](#pd-mini-tab), the panel runs a model of a supply. The header reads `SIMULATED: SWITCH ON IN PD MINI`, a button that opens the tab, and the menu tile is marked SIMULATED. The model is a 6Ω load with a 1.4A burst for 0.6s every 3s, behind a 0.05Ω source resistance. Its readings are not measured, and nothing on the bench is powered.

## The PD mini

![The PD mini enabled, no IOmcu reading it: NOT ANSWERING](view:supply-offline)

With the PD mini enabled, the header shows the module and the pins from PD mini TX and PD mini RX, for example `PD MINI TX GPIO 12 RX GPIO 13`. If either pin is unset or both use the same GPIO, it shows `PD MINI NO PINS`. The panel reads the IOmcu's SUPPLY page every 100ms.

| Header | Meaning |
|---|---|
| `ONLINE 38400` | the module answers; the number is the baud rate in use |
| `NOT ANSWERING` | no IOmcu of 0.10.0 or later answers, or the readings are older than 1500ms; an output that is on is switched off |

Before the first switch-on the plot reads `Output has not been switched on yet.`, and the cards show dashes until a reading arrives.

## The readings

| Card | Shows |
|---|---|
| VOLT | the output voltage; the set point in brackets; the run's lowest voltage under it |
| CURR | the output current; the current limit in brackets; the run's peak under it |
| PWR | the output power and the run's peak |
| MODE | CV (constant voltage) at the set voltage, CC (constant current) at the current limit, TRIPPED after a trip; CC is drawn in the warning colour |

The plot shows voltage, current and power over the last 30s, with each set point as a dashed line. TABLE lists the same values. The bracketed value is the set point the supply reports; while it does not answer, the one the screen holds.

## Set points

| Set point | Model | PD mini | Step buttons |
|---|---|---|---|
| VOLTAGE | 3.3 to 21V, 20mV steps | 1 to 20V, 10mV steps | 0.1V |
| CURRENT LIMIT | 0.5 to 5A, 50mA steps | 0.05 to 3A, 10mA steps | 0.1A |

The PD mini ranges come from the vendor's page and have not been measured. Output voltage is capped at 0.7V below the reported input voltage: a 5V input allows at most 4.3V. SETTINGS can narrow both ranges further. The active range appears at the right end of the header.

- Drag a slider, or tap its track to set the value under the finger.
- `-0.1` and `+0.1` step the value.
- A tap on the VOLT or CURR card, or on a set point's value at the right end of its row, opens the keypad.

![The keypad over the left column](view:supply-keypad)

The keypad shows the range in its title row. OK takes a value inside the range, rounded to the supply's step. A value outside the range is refused and the range turns to the warning colour. CANCEL, or OK with nothing typed, keeps the set point.

## A change to a live output

![The question before a change to a live output](view:supply-confirm)

Changing a set point while the output is on opens a confirmation showing the change. APPLY sends it to the supply; CANCEL discards it and restores the slider. A drag asks once, on release. If the drag is interrupted before release, for example by a second finger tapping home, the slider returns without a question. No confirmation is needed with the output off. In SETTINGS, slider and keypad confirmations can be disabled separately.

## OUTPUT ON and OFF

OUTPUT ON is a 2s hold, with the fade ARM uses. A release before 2s switches nothing and shows `Hold 2s to switch the output on.` in the alert band. OUTPUT OFF is a tap. The output switches off, and stays off until the next hold, on:

- STOP, on any screen;
- leaving SUPPLY, for an output SUPPLY switched on;
- a supply that stops answering, or that sends no reading for 3000ms;
- a trip;
- a PD mini input voltage under the set point plus 0.5V on 2 reads in a row;
- the module switching its output off itself;
- a change to the PD mini's pins, baud rate or enable setting.

The band reports why the output switched off.

The IOmcu refuses an ON while its failsafe is latched: the band shows FAULT 01 (the link was silent for 200ms, which an IOmcu restart can cause) or FAULT 10 (the heartbeat stopped). The hold then ends with `Supply refused to switch on: IOmcu failsafe latched. Hold ARM, then DISARM.` Only the CLEAR an ARM writes lifts the failsafe: hold ARM on SERVO or MOTOR & ESC, DISARM, then hold OUTPUT ON again.

An output another screen switched on stays as it is when SUPPLY is left. The output is off after every restart, whatever the start values.

## On the rbmod carrier

![SUPPLY on the carrier](view:supply-rbmod)

On the [rbmod carrier](https://github.com/subtilitas/rcbench/wiki/Carrier), the PD mini feeds the servo rails. The programming ports have a separate 5V supply switched by GPIO 21. Below OUTPUT, its status reads `PROG PORTS 5V, GPIO 21: ON`, `OFF`, or `NOT READ` if the IOmcu does not answer. KST and Hitec programming sessions control this rail. The PD mini remains under operator control: OUTPUT ON and its set points remain available.

On LIVE, the PROG 5V box beside PLOT and TABLE controls the programming rail manually, while SAFE or ARMED. Hold PROG 5V ON for 2s to switch it on; tap PROG 5V OFF to switch it off. Early release shows `Hold 2s to switch the programming ports' 5V on.` The box shows the pin status, such as `PROG PORTS 5V, GPIO 21: OFF`, and the last INA3221 window for 0x40 CH3, such as `5.02V  0.120A`.

While a programmer holds a port, the box reads `HELD BY PROG PORT n` and accepts no press. Leaving SUPPLY switches off a 5V rail enabled by this box, unless a port holds it. Errors switch it off too, after an active servo-memory write finishes, with a maximum wait of 10000ms.

With the box set to ON, two measurement windows below 3000mV count as a 5V failure: `PROG 5V lost: INA3221 0x40 CH3 read 0.12V while ON. All outputs off.`

## SETTINGS

![The supply's settings](view:supply-settings)

SETTINGS shows its controls in the left column. Tap a value for the keypad or a switch to toggle it. Selecting LIVE while the keypad is open cancels the edit. While a live-output confirmation is open, the strip waits for APPLY or CANCEL.

| Setting | Range | Default | What it does |
|---|---|---|---|
| VOLTAGE MAX | 3.3 to 21V | 21.00V | the highest voltage a set point takes |
| CURRENT MAX | 0.5 to 5A | 5.00A | the highest current limit a set point takes |
| START VOLTAGE | up to VOLTAGE MAX | 6.00V | the voltage set point after a restart |
| START CURRENT | up to CURRENT MAX | 2.00A | the current limit after a restart |
| CURRENT TRIP | 0 to 5A | OFF | output off once the current is over it for TRIP TIME |
| VOLTAGE TRIP | 0 to 21V | OFF | output off once the voltage is over it for TRIP TIME |
| TRIP TIME | 0 to 5000ms | 100ms | how long a reading is over a trip before it fires |
| SLIDER AND STEPS | ON, OFF | ON | ask before the slider changes a live output |
| KEYPAD | ON, OFF | ON | ask before the keypad changes a live output |

- A trip of 0 is off.
- A current trip at or above CURRENT LIMIT does not fire: the supply holds the limit in CC.
- A cap lowered under a set point brings the set point down to it.

The settings are kept in NVS (non-volatile storage). They are written on the next frame with the bench SAFE and the output off. The bottom line reads `SAVED`, `SAVE PENDING`, `NOT SAVED` or `SETUP CHANGES NOT SAVED`.

With the PD mini as the supply, SETTINGS also offers RESET PD MINI. It switches the output off and restarts the module, for a module that shows ERR. Whether a restart clears every ERR is not measured.

## PD MINI tab

![The PD MINI tab](view:supply-pdmini)

SETTINGS has two tabs: LIMITS and PD MINI. Use PD MINI to configure the module's wiring.

| Setting | Range | Default | What it does |
|---|---|---|---|
| PD mini | ON, OFF | OFF | the programmable supply on SUPPLY and SERVO; OFF runs the panel's supply model |
| PD mini TX | -1 to 47 | -1 | the IOmcu GPIO to the module's DM |
| PD mini RX | -1 to 47 | -1 | the IOmcu GPIO from the module's DP |
| PD mini baud | 9600, 19200, 38400, 57600, 115200, 230400, 460800, AUTO | AUTO | the module's UART (universal asynchronous receiver-transmitter) rate; AUTO tries each of the 7 rates, one a second |

Changes take effect immediately and are saved as on LIMITS; there is no separate SAVE button. Resetting LIMITS does not reset these settings.

Wiring changes switch an active output off. These settings are therefore locked while the output is on, an ON command is pending, a programming port holds the supply, or the bench is ARMED. The help line and band identify the condition. If one begins with a pin keypad open, the keypad closes and discards the entry.

The `-` and `+` keys for PD mini TX and RX skip the other pin's GPIO. With RX on GPIO 18, decrementing TX jumps from GPIO 19 to GPIO 17. At a range boundary the key stops: with RX on GPIO 47, TX ends at GPIO 46. The keypad rejects the other pin's GPIO with `GPIO already used by the other PD mini pin`.

A stored configuration with both pins on the same GPIO loads unchanged. The PD mini stays off, the header shows `PD MINI NO PINS`, and both rows show `TX and RX on one pin. The PD mini stays off.` until you change one.

The IOmcu also rejects reserved pins and pins used by an output, sensor bus, phase tap or the other PD mini signal. For example, RX on GPIO 17 conflicts with Sensor SCL on GPIO 17. The message clears when a valid pin is applied.

![RX's GPIO 9 refused for PD mini TX](view:supply-pdmini-why)

On the [rbmod carrier](https://github.com/subtilitas/rcbench/wiki/Carrier) PD mini TX reads GPIO 7 and PD mini RX GPIO 6. These rows have no keys and open no keypad, and their help line reads `Soldered on this board. Not editable here.`. The values stored for them stay as they are and are in force again on a module wired by hand.

![PD mini TX and RX soldered](view:supply-pdmini-rbmod)

## The log of a run

Each switch-on begins a run. The plot clears at switch-on and freezes at switch-off. The CONTROL title line accumulates charge in mAh and energy in Wh for that run. RESET PEAKS restarts the minima and maxima from the current reading.

Each run is written to its own `SUPPLnnn.CSV` in `BENCH/LOGS` on the SD (Secure Digital) card, while Auto logs on [SETUP](setup.md#sd-card) > SD CARD is ON, one row every 50ms, with the set point, voltage, limit, current, power, mode, charge, energy and source, `measured` or `model`. [Logs](logs.md) opens it.

## Related pages

- [Servo](servo.md): the same set points and output switch on the SERVO screen.
