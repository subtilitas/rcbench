---
screen: IMU
title: IMU
order: 66
---

# IMU

IMU is the screen of the Bluetooth IMU (inertial measurement unit): a WitMotion BWT901CL angle sensor on a control surface, which reaches the panel through an HC-05 Bluetooth module on the panel's UART1. It opens from the IMU tile on [Setup](setup.md); its home tag leads back there. The screen drives no output. Its settings are on the IMU entry of [Sensors](setup-sensors.md#the-imu). The wiring and the HC-05's set-up are on the wiki page [The Bluetooth IMU](https://github.com/subtilitas/rcbench/wiki/IMU).

![IMU with the HC-05 answering and no sensor bound](view:imu)

| Control | What it does |
|---|---|
| SCAN FOR SENSORS | opens the found list and starts a scan; it takes a press while the HC-05 answers and no scan, bind or calibration runs |
| CONNECT TO ANY SENSOR | only after the HC-05 refused a scan with `ERROR:(1F)`: sets it to connect to any sensor that takes PIN 1234 (`AT+CMODE=1`) |
| HOLD 2s: CALIBRATE ACCELEROMETER | while connected: the sensor's accelerometer calibration. Keep it still and level for the 3s the line counts down |
| ZERO | takes the current angles as zero; while a reading is there. With ANGLE SOURCE IMU, RUDDER drops its zero and MAX |
| CLEAR ZERO | the angles from the sensor's own zero again |
| IMU SETTINGS | opens the IMU entry of [Sensors](setup-sensors.md#the-imu); its home tag comes back here |

A press of the hold shorter than 2s starts nothing, and the band says `Hold 2s to calibrate the accelerometer.`.

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

The reading is stale 300ms after the last angle frame. A servo run with ANGLE SOURCE IMU rides through a gap of up to 300ms once per 10s, as through the AS5600's ([Servo settings](servo-settings.md)). A longer gap ends the run as `IMU not answering`, a second within 10s as `IMU dropped out twice in 10s`.

## Scan and bind

![Three devices found](view:imu-scan)

A scan takes up to 40s: 11.5s of inquiry, then the name of each device found, up to 8. A tap on a device binds the HC-05 to it with PIN 1234 and closes the list; the HC-05 then restarts and connects to it. SETUP keeps the address and the name and asks for a save. SCAN AGAIN starts another scan; CLOSE and the home tag close the list.

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
