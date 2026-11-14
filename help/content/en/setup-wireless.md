---
screen: none
title: Wireless
order: 62
---

# Wireless

WIRELESS on [SETUP](setup.md) holds the panel's Bluetooth LE (Low Energy) devices: the WitMotion BWT901CL IMU (inertial measurement unit), the M5Stack Dial, a wireless knob, and the balance node. They reach the panel through the board's ESP32-C6. The first IMU ON, Dial ON or Balance node ON starts Bluetooth LE until the panel restarts. Settings are retained and saved like other SETUP values.

## The left list

The 220px left list has 3 entries: IMU, DIAL and BALANCE NODE. Each shows its name and its state. Tap an entry to display its settings in the 754px right pane. The list works as on [Sensors](setup-sensors.md#the-left-list): an entry that is off shows its enable row alone and the state `OFF`, each entry keeps its scroll position, and RESET names the entry, `RESET DIAL`.

## The IMU

![WIRELESS on the IMU entry, connected](view:setup-wireless-imu)

![The IMU off: IMU and Angle source](view:setup-wireless-imu-off)

The WitMotion BWT901CL connects over Bluetooth LE through the board's ESP32-C6. Use the [IMU](imu.md) tile on SETUP to scan, bind and calibrate it. This entry contains its settings.

| Setting | Range | Default | What it does |
|---|---|---|---|
| IMU | ON, OFF | OFF | Bluetooth LE, the link to the bound sensor and its reading; the first ON starts Bluetooth LE until the panel restarts |
| Angle source | AS5600, IMU | AS5600 | where the servo runs take their angle from ([Servo settings](servo-settings.md)); shown with the IMU off too, so the source can go back to the AS5600 without switching the IMU on |
| IMU axis | X, Y, Z | X | the angle the servo runs and RUDDER read; `Z: 1° WITH THE MAGNETOMETER` |
| IMU sign | +, - | + | `-` turns that angle round |
| IMU rate | 10Hz, 20Hz, 50Hz, 100Hz, 200Hz | 50Hz | the sensor's frames a second; sent only when changed, it takes effect after the sensor is switched off and on; the sensor ships at 10Hz, and `CONNECTED` shows the rate it runs |
| IMU algorithm | 6-AXIS, 9-AXIS | 6-AXIS | 9-AXIS takes the magnetometer for Z |
| IMU filter | 1 to 50 | 1 | the panel's low-pass on the angle; 1 is off |

IMU filter smooths the angle on the panel, per frame and axis: the angle moves by 1/k of the difference to the new frame, with k the setting. It covers 63% of a step in about k frames, so at 10Hz an IMU filter of 10 takes about 1s. The frames per second are the rate the sensor runs, which `CONNECTED` shows, not the IMU rate asked. The IMU screen, RUDDER, AUTO and the servo runs read the filtered angle. The first frame after a connect starts the filter at its own value, and a crossing of 180° takes the short way. AUTO waits until the filtered angle has settled, up to 3000ms plus the time the setting can take ([The Bluetooth IMU](https://github.com/subtilitas/rcbench/wiki/IMU#the-filter)).

RATE and ALGORITHM changes are sent with the sensor's SAVE command (`FF AA 00 00 00`). For a connected sensor, the rate message is `RATE SAVED: SWITCH THE SENSOR OFF AND ON`. If disconnected, the panel shows `RATE IS SENT WHEN THE SENSOR CONNECTS; THEN SWITCH IT OFF AND ON` and waits for frames. During IMU calibration it shows `RATE IS SENT AFTER CALIBRATION; THEN SWITCH THE SENSOR OFF AND ON` and defers the change until after CALSW 0 and SAVE.

| State | When |
|---|---|
| `OFF` | IMU OFF |
| `STARTING BLUETOOTH` | Bluetooth LE starts: the C6's link, the host, its sync |
| `NO BLUETOOTH ON C6` | the host has no sync with the C6; it keeps trying |
| `C6 FAILED: RESTART` | the C6's link failed; Wi-Fi and Bluetooth are off until the panel restarts |
| `NO SENSOR BOUND` | Bluetooth runs; no sensor is bound |
| `SCANNING` | a scan runs |
| `CONNECTING` | the panel connects to the bound sensor and sets up its service |
| `BOUND` | subscribed, no angle frame yet; or the bound device is not a WitMotion sensor |
| `CONNECTED 50Hz` | angle frames within the last 300ms, at that rate |
| `NO FRAMES` | subscribed, frames came, none in the last 300ms |

## The Dial

![WIRELESS on the DIAL entry, linked](view:setup-wireless-dial)

The M5Stack Dial turns the throttle on [MOTOR & ESC](motor.md) and the horn on [SERVO](servo.md) LIVE, as the panel's own knob does. It never switches anything on: ARM, SUPPLY ON, PROG 5V and every 2s hold stay on the panel, and so does STOP.

| Setting | Range | Default | What it does |
|---|---|---|---|
| Dial | ON, OFF | OFF | Bluetooth LE and the link to the bound Dial; OFF ends the link, and a Dial switched off is no loss |
| Scan for Dial | `SCAN` | | a tap scans 10s and lists the Dials in range; a switch to ARMED stops it, `STOPPED BY ARM`; refused while ARMED, with `NO DIAL SCAN WHILE ARMED: SWITCH TO SAFE FIRST` in the band |
| Bound Dial | `NONE` or the Dial's name | `NONE` | a tap lists the Dials the last scan found, with their signal in dBm; a tap on one binds it and asks for a save. RESET keeps the bound Dial |
| Dial step, throttle | 0.5%, 1%, 2%, 5% | 1% | the throttle one detent moves |
| Dial step, servo | 0.5°, 1°, 2°, 5° | 1° | the horn angle one detent moves |

The panel connects to the bound Dial by its address, without a scan, and again after each loss. A Dial in range that is not bound is never taken.

On the bench screens:

- A turn moves nothing while the bench is SAFE, and turns made while SAFE are not kept for the ARM. The first EVENT after a connect, an EVENT more than 300ms after the last and a jump of more than 16 detents move nothing.
- A press of the Dial's screen button while ARMED sets the throttle to 0% on MOTOR & ESC and the horn to CENTER on SERVO. A 1s hold DISARMs, also while an ARM is on its way, and ends an ESC STICK run as ABORT does.
- A Dial lost while ARMED is an error: 1000ms without an EVENT, or the disconnect, switches the bench to SAFE with all outputs off, and the band shows `DIAL LOST WHILE ARMED: SWITCHED TO SAFE, ALL OUTPUTS OFF`. 500ms without an EVENT shows `DIAL: NO EVENT FOR 500ms`. The clock starts at the first EVENT of a connection, so a connect that has sent none is no loss.
- Other screens take no turn from the Dial. The Dial shows their title and the bench's state.

| State | When |
|---|---|
| `OFF` | Dial OFF |
| `STARTING BLUETOOTH` | Bluetooth LE starts |
| `NO BLUETOOTH ON C6` | the host has no sync with the C6; it keeps trying |
| `C6 FAILED: RESTART` | the C6's link failed until the panel restarts |
| `NO DIAL BOUND` | Bluetooth runs; no Dial is bound |
| `SCANNING` | a scan runs |
| `CONNECTING` | the panel connects to the bound Dial and sets up its service |
| `BOUND: NOT A DIAL` | the bound device serves no Dial service |
| `CONNECTED, NO EVENT` | the link stands; no EVENT yet |
| `OTHER PROTOCOL 2` | the Dial's firmware speaks another protocol version; nothing it sends is taken |
| `CONNECTED` | EVENTs within the last 500ms |
| `DEGRADED` | no EVENT for 500ms |
| `DIAL LOST` | no EVENT for 1000ms, or the disconnect |

## The balance node

![WIRELESS on the BALANCE NODE entry, reporting](view:setup-wireless-bal)

The balance node measures the vibration and the index of a rotor for [Balance](balance.md#the-balance-node). It drives nothing, so its loss is no error.

| Setting | Range | Default | What it does |
|---|---|---|---|
| Balance node | ON, OFF | OFF | Bluetooth LE and the link to the bound node; OFF ends the link |
| Scan for node | `SCAN` | | a tap scans 10s and lists the balance nodes in range, `rcbench-bal-XXXX`; a switch to ARMED stops it; refused while ARMED, with `NO NODE SCAN WHILE ARMED: SWITCH TO SAFE FIRST` in the band |
| Bound node | `NONE` or the node's name | `NONE` | a tap lists the nodes the last scan found; a tap on one binds it and asks for a save. RESET keeps the bound node |
| Node battery | `3.91V`, `3.40V LOW`, `NO READING`, `--` | | the node's cell, read every 5s; LOW under 3.50V |
| Trial mass | 0.01g to 20.00g | 0.50g | the mass taped on for the trial runs on [MOTOR & ESC](motor.md#balance) |

| State | When |
|---|---|
| `NO NODE BOUND` | Bluetooth runs; no node is bound |
| `BOUND: NOT A NODE` | the bound device serves no node service |
| `CONNECTED, READING INFO` | the link stands; INFO is read |
| `ROLE 1, PROTOCOL 1` | the node is no balance node, or speaks another protocol; nothing it sends is taken |
| `CONNECTED, NO REPORT` | no report for 1000ms |
| `REPORTING` | a report within the last 1000ms |

`OFF`, `STARTING BLUETOOTH`, `NO BLUETOOTH ON C6`, `C6 FAILED: RESTART`, `SCANNING` and `CONNECTING` read as for the Dial.

## Related pages

- [Setup](setup.md)
- [IMU](imu.md)
- [Sensors](setup-sensors.md)
- [Motor and ESC](motor.md)
- [Balance](balance.md)
- [Servo](servo.md)
