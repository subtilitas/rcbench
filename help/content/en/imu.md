---
screen: IMU
title: IMU
order: 66
---

# IMU

The IMU screen controls a Bluetooth inertial measurement unit: a WitMotion BWT901CL angle sensor mounted on a control surface. It reaches the panel over Bluetooth LE (Low Energy) through the board's ESP32-C6; no wire, no pairing, no PIN. Open the screen from the IMU tile on [Setup](setup.md); the home tag returns there. This screen drives no outputs. Its settings are in the IMU entry of [Sensors](setup-sensors.md#the-imu). See [The Bluetooth IMU](https://github.com/subtilitas/rcbench/wiki/IMU) for the link and the sensor's commands.

![IMU with Bluetooth LE running and no sensor bound](view:imu)

| Control | Function |
|---|---|
| SCAN FOR SENSORS | opens the sensor list and starts a 10s scan; available while Bluetooth runs and no scan or calibration is running |
| HOLD 2s: CALIBRATE ACCELEROMETER | starts accelerometer calibration while connected; keep the sensor still and level during the 3s countdown |
| ZERO | uses the current angles as zero when a reading is available; with ANGLE SOURCE IMU, this clears RUDDER's zero and MAX |
| CLEAR ZERO | restores angles relative to the sensor's own zero |
| IMU SETTINGS | opens the IMU entry of [Sensors](setup-sensors.md#the-imu); its home tag returns here |

A press shorter than 2s does not start calibration. The band shows `Hold 2s to calibrate the accelerometer.`.

## The card

![Connected at 50Hz, ZERO taken](view:imu-connected)

| Line | Shows |
|---|---|
| `SENSOR: HC-06` | the bound sensor's name from the scan, `NO NAME` without one, or `NONE BOUND`; its address and type (`P` public, `R` random) under it |
| `C6: 2.13.0` | the C6's firmware version, `---` when the C6 reports none |
| state | `IMU OFF IN IMU SETTINGS`, `STARTING BLUETOOTH`, `NO BLUETOOTH ON C6`, `C6 FAILED: RESTART`, `NO SENSOR BOUND`, `SCANNING`, `CONNECTING`, `BOUND`, `CONNECTED 50Hz` or `NO FRAMES` |
| X, Y, Z | the angles from the zero through IMU filter, 0.01°, `---` without a reading. The axis the servo runs read is in the accent colour |
| RATE, AGE | the angle frames of the last second, and the time since the newest reached the screen |
| BATTERY | the sensor's battery, from its 0x52 frame; hidden above 6.0V |

A reading becomes stale 300ms after the last angle frame. With ANGLE SOURCE IMU, a servo run tolerates one gap of up to 300ms per 10s, as it does with the AS5600 ([Servo settings](servo-settings.md)). A longer gap ends the run with `IMU not answering`; a second gap within 10s ends it with `IMU dropped out twice in 10s`.

## Scan and bind

![Two sensors listed, four other devices hidden](view:imu-scan)

A scan takes 10s. It lists the devices that advertise the WitMotion sensor's service, strongest first, with name, address and signal strength; the sensor's radio is named `HC-06`. Other devices are counted as hidden. Tap a sensor to bind it and close the list; the panel connects to it at once and again 1s after a lost link. SETUP keeps the address and name and requests a save. SCAN AGAIN repeats the scan; CLOSE or the home tag closes the list.

| Line | Meaning |
|---|---|
| `NO CONNECTION: SENSOR ON, IN RANGE, NO PHONE ON IT?` | a connect, its set-up or the link failed; the panel tries again every 1s. A phone connected to the sensor keeps the panel out |
| `NOT A WITMOTION SENSOR: NO ISSC UART SERVICE` | the bound device lacks the sensor's service; no new try until another bind |
| `THE SENSOR DID NOT TAKE THE COMMAND` | a write of a command was refused or not answered within 1s; the rest of its group was dropped |
| `NO SENSOR FOUND: IS IT ON, NO PHONE ON IT?` | the scan listed no sensor |
| `THE SCAN DID NOT START: CODE <n>` | Bluetooth refused the scan with that code |

## Related pages

- [Sensors](setup-sensors.md)
- [Servo settings](servo-settings.md)
- [Setup](setup.md)
