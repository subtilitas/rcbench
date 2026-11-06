---
screen: SETUP
title: Setup
order: 60
---

# Setup

SETUP contains bench-wide panel settings under PANEL, SENSORS and SD CARD. ESC (electronic speed controller) settings are under [MOTOR & ESC, SETTINGS](motor-settings.md). Open SETUP from the [menu](overview.md) to see its 3-by-3 tile grid. Each category opens a full-width settings list.

## The tiles

![The tiles, everything saved](view:setup)

| Tile | Second line | What it opens |
|---|---|---|
| PANEL | `THEME, LANGUAGE, KNOB` | the list of PANEL |
| SENSORS | `CURRENT, ANGLE, PHASE TAP` | [Sensors](setup-sensors.md): the sensor bus, the INA228 and INA3221 current monitors, the AS5600, the phase tap and the Bluetooth IMU, a left list of 9 beside the selected one's rows |
| OUTPUTS | `PROTOCOLS, PINS, BOARD` | [Outputs](outputs.md): the role of each IOmcu pin, on a drawing of the board |
| SD CARD | `LOGS, BROWSER WRITES` | the list of SD CARD: which logs the panel writes, and where, and what a browser needs to write |
| WI-FI | `NETWORK, FILES, UPLOAD` | [Wi-Fi](wifi.md): the panel's radio, for the card's files and a firmware image in a browser |
| UPDATES | `RELEASES ON GITHUB` | [Updates](updates.md): the check for a newer panel firmware release and the reminder switch |
| IMU | `BLUETOOTH ANGLE SENSOR` | [IMU](imu.md): the WitMotion BWT901CL over Bluetooth LE, the scan and the bind, the calibration and the live angles |
| LAYOUT | `CONNECTORS AND PINS` | [Layout](layout.md), on the rbmod carrier only, whose pins are soldered |

2 slots are free, and the carrier's LAYOUT takes one. The save button sits at the bottom right, as on every list. A change left unsaved in any category shows on it:

![The tiles with a value unsaved](view:setup-hub-dirty)

The home tag at the top left returns one level at a time:

| From | The home tag leads to |
|---|---|
| a category's list | the tiles; the list keeps its position |
| the tiles | the menu; with a value unsaved, the warning first ([Leaving with values unsaved](#leaving-with-values-unsaved)) |
| OUTPUTS, WI-FI, UPDATES, IMU | the tiles; OUTPUTS opened from TARGET on MOTOR & ESC or SERVO goes back there, WI-FI opened from the button of LOGS or UPDATES likewise. A list or a keyboard open on one of them closes first |

## Changing a value

![The hub, everything saved](view:setup)

The list is 984px wide, or 754px on SENSORS, with 6 rows of 64px. A 48px strip below it contains the category name, RESET CATEGORY and the save button. It ends 4px above the alert band, so alerts never cover a key, row or button.

A row shows the setting, a help line and its value with its unit. A number has `-` and `+` keys; a switch or a choice has `<` and `>`.

- **Tap**: a key steps once when you release inside it, within 8px of the initial press.
- **Hold**: repetition starts after 0.45s at 8 steps per second, increasing to 30 per second after 2.25s.
- **Scroll**: drag vertically by more than 8px anywhere on the list to scroll without changing a value.
- **Keypad**: tap a numeric value between `-` and `+`. The keypad shows the setting, range and unit, with decimal places matching the step size. OK rounds to the nearest step: 73% brightness becomes 75%. An out-of-range value leaves the keypad open and highlights the range. Values within the displayed range but excluded from the allowed steps show a specific reason instead: `0 or 5.0 to 1000.0mΩ` for a shunt between 0 and 5.0mΩ, `0 or 10 to 32767mA` for a current limit between 0 and 10mA, `Allowed bus pins: 0 4 6 14 16 18 20 26` for an invalid Sensor SDA pin, or `GPIO already used by the other PD mini pin`. The next key restores the range text. CANCEL or the home tag discards the edit. Set an unassigned pin (-1) with `-`; pin keypads have no `+/-` key.
- Only one finger is handled at a time. A second finger has no effect while the first presses or scrolls.

A changed value is in force at once. RESET CATEGORY sets every value of the open category back to its default; on SENSORS `RESET INA228` does it for the selected entry alone.

| Button | What it does |
|---|---|
| RESET CATEGORY | the open category's defaults, at once |
| SAVED, SAVE, WHEN IDLE, NOT SAVED | the save button; see below |

## Keeping the values

![A changed value, with SAVE offered](view:setup-dirty)

SAVE writes changed values to flash. Returning every value to its stored value clears the changes: the button reads `SAVED` again and any pending write is cancelled.

| Label | Meaning |
|---|---|
| `SAVED` | all values are saved; the button has no effect |
| `SAVE` | unsaved values remain; press to request a write |
| `WHEN IDLE` | a write awaits a quiet bench |
| `NOT SAVED` | the write failed; press to retry |

The write occurs on the first frame when the bench is SAFE, the supply output is off, and no board drawing is being fetched or saved. While ARMED, the button reads `WHEN IDLE` until the bench is SAFE. Unsaved values remain in effect until power-off. Leaving a list or the tile page does not save them.

`NOT SAVED` does not indicate how much reached flash. A failed write can leave some keys updated, so the next start may load a mix of old and new values. After failure, the button stays `NOT SAVED` or `SAVE` until a save succeeds, even if you restore all previous values.

## Leaving with values unsaved

![One value unsaved, at the home tag](view:setup-leave)

The tile page's home tag returns to the menu. If the save button reads `SAVE` or `NOT SAVED`, it first warns about unsaved changes. Each changed value appears as `name: stored -> now`, with the same value format and unit as in the list. Stored values are those loaded at startup or written by the last successful save.

The warning also includes the DUT (device under test) name and settings from other screens, such as SERVO, ESC STICK and MOTOR & ESC. One save writes them all.

| Button | What it does |
|---|---|
| STAY | closes the warning; the tiles show again, a list keeps its category and position |
| DISCARD | sets every listed value back to its stored one, as a step by hand would, then opens the menu |
| SAVE | asks for a write, as the save button does, then opens the menu |

- DISCARD sends a pin or a port back to the IOmcu at once. A PD mini wiring set back switches the supply output off, as a change by hand does.
- While the bench is ARMED, DISCARD is dimmed and does nothing; the line `DISCARD waits until the bench is SAFE.` says so. STAY and SAVE work.
- PWM target and Port PWM are not set back while the servo programmer holds the servo port; they stay unsaved.
- With more than 10 changes, 9 lines show and the last line reads `and 3 more` for 12.
- After a refused save the line `Saving failed. Tap SAVE to try again.` shows under the list. DISCARD then also asks for a write of the values put back, since the flash can hold part of the refused write.

![Twelve values unsaved](view:setup-leave-many)

Changing a setting on [MOTOR & ESC, SETTINGS](motor-settings.md) requests a save automatically. That write includes unsaved SETUP values.

No warning shows when nothing is unsaved, when a save is asked (`WHEN IDLE`), or on the way to OUTPUTS, WI-FI and UPDATES. STOP stops the bench and leaves the warning open. A screen the panel opens by itself, such as the bus-fault screen after a link loss, closes the warning and leaves the values in force and unsaved.

## PANEL

![PANEL](view:setup-application)

| Setting | Range | Default | What it does |
|---|---|---|---|
| Theme | DARK, LIGHT | DARK | the colour theme, from the next frame |
| Brightness | 20 to 100%, steps of 5 | 100 | scales the palette |
| Contrast | 60 to 140%, steps of 5 | 100 | pushes colours away from mid grey |
| Screen rotation | NORMAL, 180° | NORMAL | turns picture and touch by 180°, from the next frame; locked while ARMED |
| Language | ENGLISH, DEUTSCH | ENGLISH | the interface language, from the next frame, without a restart |
| Units | METRIC, IMPERIAL | METRIC | stored; no screen uses it in this release |
| Time zone | UTC, LONDON, BERLIN, HELSINKI, NEW YORK, CHICAGO, DENVER, LOS ANGELES, TOKYO, SYDNEY | BERLIN | the zone file dates are written and shown in, daylight saving included |
| Dim after | 0 to 60min, steps of 5 | 0 | stored; no screen uses it in this release |
| One finger pans | ON, OFF | OFF | one finger pans a zoomed plot on [Logs](logs.md#the-plot) |
| Rotary knob | ON, OFF | OFF | an MT6701 at 0x06 on a bus of its own, IO30 and IO31 of header P3 (I2C: Inter-Integrated Circuit), turns the throttle on MOTOR & ESC or the horn on SERVO |
| Knob range | 90 to 720°, steps of 10 | 270 | the knob angle that moves the slider across its whole span |
| Keyboard transparency | OFF, 25%, 50% | OFF | how much of the text shows through the text editor's keyboard on [Files](files.md#the-text-editor); other keyboards stay opaque |
| Demo mode | ON, OFF | OFF | hides the SIMULATION watermark from the next frame; the band still says SIM and the tiles still say SIMULATED. The panel draws the watermark only while no IOmcu answers, so the setting changes nothing on a bench with an IOmcu |

The backlight is on while the panel runs. No setting switches it.

Screen rotation uses the ESP32-P4's PPA (pixel-processing accelerator) to rotate each outgoing frame and its touch coordinates together. A contact at (x, y) in NORMAL maps to (1023 - x, 599 - y) at 180°. Touch injector contacts are not rotated. The setting's row has no keys while ARMED.

If copying a frame takes more than 50ms, picture and touch both return to NORMAL, with a message in the band. The setting remains 180°, so rotation is attempted again at the next start.

![Screen rotation at 180° while ARMED](view:setup-rotation)

The panel has no RTC (real-time clock) chip. Its clock is unset after power-on and set through NTP (Network Time Protocol). Each join of the home network makes one request to `pool.ntp.org`, lasting at most 30000ms. If the join ends before the answer, the radio remains on for that request, for at most 30000ms. ARM, STOP, supply operation, a log run or an installation shuts it down immediately. An unresponsive first DNS (Domain Name System) server delays the request by about 7s after each join.

A software restart preserves the clock. FAT stores timestamps in local time at the moment of writing; changing Time zone does not alter existing file timestamps. Until NTP answers, written files are dated 1980 and FILES INFO shows `not known`. Access-point mode does not query a time server.

![Time zone set to LOS ANGELES](view:setup-time-zone)

![SETUP in the light theme](view:setup-light)

### The rotary knob

- The knob changes a value by the amount turned, not its absolute position. With a 270° range, a quarter turn changes throttle by 33.3 percentage points.
- It never switches to ARMED. Turns while SAFE have no effect and are discarded.
- The first reading after power-up or a sensor outage establishes a reference without moving the output.
- A finger held on the throttle track or SERVO dial takes precedence over the knob.
- The sensor is read every 10ms. A change greater than 90° between readings is discarded.

The sensor is an MT6701 in its I2C mode, MODE pin high, at 0x06. Nothing else of the board is on its bus. The panel never addresses 0x14 or 0x5D for it, where the GT911 touch controller answers on the touch bus, nor 0x00 to 0x03 or 0x78 to 0x7F, which the I2C specification reserves. With no sensor fitted, Rotary knob ON probes 0x06 every 100ms. The host tests show that this changes nothing else; on the panel it is not measured. Wiring: [Building](https://github.com/subtilitas/rcbench/blob/main/docs/Building.md#rotary-knob-wiring).

## SD CARD

![SD CARD](view:setup-card)

| Setting | Range | Default | What it does |
|---|---|---|---|
| Auto logs | ON, OFF | ON | a log of each ARMED and supply run in Auto log folder: `SERVOnnn.CSV`, `MOTORnnn.CSV`, `SUPPLnnn.CSV` |
| User logs | ON, OFF | ON | the AUTOMATIC TEST's `ATESTnnn.CSV` and report and the LIMIT RUN's `LIMITnnn.TXT` in User log folder |
| Browser writes | OPEN, ACCESS CODE, HOLD, OFF | OPEN | what WI-FI's browser page needs to change the card: [Wi-Fi](wifi.md#browser-writes) |
| Auto log folder | a folder | `BENCH/LOGS` | where Auto logs writes |
| User log folder | a folder | `USER/LOGS` | where User logs writes |

- OFF writes no file and takes no number. No card alert of that kind of run shows.
- Auto logs switched off during a run ends that run's file there; switched on during an ARMED or supply run, a file starts there.
- User logs counts at START TEST and START LIMIT RUN: a run keeps what it started with. With it off, the SERVO screen names `USER LOGS OFF` where the file would be.
- REPORT on SERVO > OPTIONS > DUT decides, with User logs ON, whether `ATESTnnn.TXT` is written.
- The fault log, `BENCH/RCBENCH.LOG`, is written whatever Auto logs says.
- [Logs](logs.md#what-a-run-writes) lists the folders.

Tap a folder row to open its keyboard. The `.` key enters `/`. OK applies the folder in uppercase; SAVE stores it. CANCEL or the home tag keeps the previous folder. A change during a run applies to the next file: the current run finishes in its original folder. The next run's number follows the new folder's contents. Both settings may use the same folder.

![The folder keyboard with a folder refused](view:setup-folder)

An invalid folder leaves the keyboard open and the setting unchanged. The line beside the title explains why:

| Refused | Example | Line |
|---|---|---|
| a folder name empty, or `/` at the start, at the end or twice | `/LOGS`, `LOGS/`, `A//B` | `A folder name is empty` |
| over 8 characters in a folder name | `BENCHLOGS`; `BENCHLOG` is taken | `At most 8 characters per folder` |
| over 2 folders | `A/B/C`; `A/B` is taken | `At most 2 folder levels` |
| a character other than A-Z, 0-9, `_` and `-` | `MY LOGS` | `A-Z, 0-9, _ and - only` |
| a Windows device name: CON, PRN, AUX, NUL, COM1 to COM9, LPT1 to LPT9 | `AUX/LOGS` | `Reserved name` |
| `BENCH/ESC`, `BENCH/KST`, `BENCH/HITEC` or `BENCH/ESCW`, the bench's own files | `BENCH/KST`; `BENCH/KSTX` is taken | `Reserved for the bench's files` |

## SENSORS

SENSORS lists the IOmcu's sensor bus, INA228, four INA3221 monitors, AS5600 and phase tap, plus the panel's Bluetooth IMU. Each has an entry in the left list. See [Sensors](setup-sensors.md).

## Related pages

- [Supply](supply.md): the PD mini's wiring, on the PD MINI tab.
- [Sensors](setup-sensors.md)
- [Outputs](outputs.md)
- [Updates](updates.md)
- [Supply](supply.md), [Servo settings](servo-settings.md) and [Motor settings](motor-settings.md): the settings kept on their own screens.
