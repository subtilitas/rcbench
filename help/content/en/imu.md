---
screen: IMU
title: IMU
order: 66
---

# IMU

The IMU screen controls a Bluetooth inertial measurement unit: a WitMotion BWT901CL angle sensor mounted on a control surface. An HC-05 Bluetooth module connects it to the panel's UART1 (universal asynchronous receiver/transmitter). Open the screen from the IMU tile on [Setup](setup.md); the home tag returns there. This screen drives no outputs. Its settings are in the IMU entry of [Sensors](setup-sensors.md#the-imu). See [The Bluetooth IMU](https://github.com/subtilitas/rcbench/wiki/IMU) for wiring and HC-05 setup.

![IMU with the HC-05 answering and no sensor bound](view:imu)

| Control | Function |
|---|---|
| SCAN FOR SENSORS | opens the device list and starts a scan; available while the HC-05 responds and no scan, binding or calibration is running |
| CONNECT TO ANY SENSOR | available after the HC-05 refuses a scan with `ERROR:(1F)`; configures it to connect to any sensor that accepts PIN 1234 (`AT+CMODE=1`) |
| HOLD 2s: CALIBRATE ACCELEROMETER | starts accelerometer calibration while connected; keep the sensor still and level during the 3s countdown |
| ZERO | uses the current angles as zero when a reading is available; with ANGLE SOURCE IMU, this clears RUDDER's zero and MAX |
| CLEAR ZERO | restores angles relative to the sensor's own zero |
| IMU SETTINGS | opens the IMU entry of [Sensors](setup-sensors.md#the-imu); its home tag returns here |

A press shorter than 2s does not start calibration. The band shows `Hold 2s to calibrate the accelerometer.`.

## The card

![Connected at 50Hz, ZERO taken](view:imu-connected)

| Line | Shows |
|---|---|
| `SENSOR: HC-06` | the bound sensor's name from the scan, `NO NAME` without one, `NONE BOUND` or `ANY IN RANGE`; its address under it |
| `HC-05: 2.0-20100601` | the HC-05's firmware, `---` until it answered |
| state | `IMU OFF IN IMU SETTINGS`, `NO HC-05`, `NO SENSOR BOUND`, `SCANNING`, `BINDING`, `BOUND`, `CONNECTED 50Hz` or `NO FRAMES` |
| X, Y, Z | the angles from the zero, 0.1°, `---` without a reading. The axis the servo runs read is in the accent colour |
| RATE, AGE | the angle frames of the last second, and the time since the newest reached the screen |
| BATTERY | the sensor's battery, from its 0x52 frame; hidden above 6.0V |

A reading becomes stale 300ms after the last angle frame. With ANGLE SOURCE IMU, a servo run tolerates one gap of up to 300ms per 10s, as it does with the AS5600 ([Servo settings](servo-settings.md)). A longer gap ends the run with `IMU not answering`; a second gap within 10s ends it with `IMU dropped out twice in 10s`.

## Scan and bind

![Three devices found](view:imu-scan)

A scan takes up to 40s: an 11.5s inquiry followed by name requests for up to 8 devices. Tap a device to bind the HC-05 to it with PIN 1234 and close the list. The HC-05 then restarts and connects to that device. SETUP keeps the address and name and requests a save. SCAN AGAIN repeats the scan; CLOSE or the home tag closes the list.

| Line | Meaning |
|---|---|
| `THE HC-05 DOES NOT ANSWER AT: CHECK ITS WIRES AND KEY` | no OK at 115200, 38400 or 9600 baud |
| `THE HC-05 REFUSED THE SCAN: ERROR:(1F)` | firmware 3.0 is reported to refuse a scan; CONNECT TO ANY SENSOR binds the one sensor in range |
| `NO DEVICE FOUND: IS THE SENSOR ON?` | the inquiry ended with no device |
| `PAIRING FAILED: IS THE SENSOR ON AND IN RANGE?` | `AT+PAIR` answered FAIL or ERROR |

## Related pages

- [Sensors](setup-sensors.md)
- [Servo settings](servo-settings.md)
- [Setup](setup.md)
