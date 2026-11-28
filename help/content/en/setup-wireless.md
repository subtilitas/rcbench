---
screen: none
title: Wireless
order: 62
---

# Wireless

WIRELESS on [SETUP](setup.md) holds the panel's Bluetooth LE (Low Energy) devices: the WitMotion BWT901CL IMU (inertial measurement unit), the M5Stack Dial, a wireless knob, the power node, an INA228 on the ESC's XT60, the balance node, and the label printer, a Supvan T50M Pro. All reach the panel through the board's ESP32-C6. The first IMU ON, Dial ON, Power node ON or Balance node ON starts Bluetooth LE until the panel restarts. Settings are retained and saved like other SETUP values.

## The left list

The 220px left list has 5 entries: IMU, DIAL, BALANCE NODE, PRINTER and POWER NODE. Each shows its name and its state. Tap an entry to display its settings in the 754px right pane. The list works as on [Sensors](setup-sensors.md#the-left-list): an entry that is off shows its enable row alone and the state `OFF`, each entry keeps its scroll position, and RESET names the entry, `RESET DIAL`.

## The IMU

![WIRELESS on the IMU entry, connected](view:setup-wireless-imu)
![The same, at ZERO, Clear zero and Calibrate](view:setup-wireless-imu-cal)

![The IMU off: IMU and Angle source](view:setup-wireless-imu-off)

The WitMotion BWT901CL is an angle sensor for a control surface. It connects over Bluetooth LE through the board's ESP32-C6: no wire, no pairing, no PIN. This entry scans for it, binds it, shows its live angles, takes its zero and calibrates it, and holds its settings. Nothing here drives an output. See [The Bluetooth IMU](https://github.com/subtilitas/rcbench/wiki/IMU) for the link and the sensor's commands.

With the IMU off the entry shows IMU and Angle source alone. With it on, these rows stand between Angle source and IMU axis:

| Row | Value | What it does |
|---|---|---|
| Scan for sensor | `SCAN`, `STARTING BLUETOOTH`, `SCANNING`, `FOUND n`, `NONE FOUND`, `STOPPED BY ARM` | a tap opens [Search](#search) and scans 10s for the sensors in range. While Bluetooth LE starts the scan waits for it, `STARTING BLUETOOTH`. Refused while ARMED, `NO IMU SCAN WHILE ARMED: SWITCH TO SAFE FIRST`; while a scan runs, `A SCAN IS RUNNING: IT ENDS WITHIN 10s`; and without Bluetooth on the C6, `BLUETOOTH IS NOT READY: NO SENSOR SCAN` |
| Bound sensor | the sensor's name, else its address, else `NONE` | a tap opens [Search](#search) on the last scan's list |
| Last error | | shown only while the last scan, connect or write failed; its help line names the error (the table below) |
| Angle X, Angle Y, Angle Z | `+12.30°` | the angles from the zero through IMU filter, 0.01°, `---` without a reading. The axis IMU axis names reads `Angle X: read by runs` |
| Frames | `RATE 50.0Hz  AGE 20ms` | the angle frames of the last second and the time since the newest reached the panel; `RATE 0.0Hz  NO ANGLE` without a reading |
| Sensor battery | `3.95V` | the sensor's battery from its 0x52 frame, `---` without one or above 6.0V |
| C6 version | `2.13.0` | the ESP32-C6's firmware, `---` when the C6 reports none |
| Zero | | a tap takes the current angles as 0°. With Angle source IMU it clears RUDDER's zero and MAX. Locked without a reading, `NO ANGLE FROM THE SENSOR YET` |
| Clear zero | | a tap gives the angles from the sensor's own 0° again; it also clears RUDDER's zero and MAX on the IMU. Locked while no zero is set, `NO ZERO SET` |
| Calibrate | `HOLD 2s: CALIBRATE` | a 2s hold on the button starts the accelerometer's calibration; keep the sensor still and level for the 3s it runs. A release before 2s starts nothing, and the band shows `Hold 2s to calibrate the accelerometer.`. Locked while the sensor sends no frames, `THE SENSOR SENDS NO FRAMES`, and while it runs, `KEEP IT STILL AND LEVEL FOR 3s` counting down. Its end shows in the band: `ACCELEROMETER CALIBRATED AND SAVED`, or `CALIBRATION ENDED WITHOUT FRAMES: TRY AGAIN` |

A reading becomes stale 300ms after the last angle frame. With Angle source IMU, a servo run tolerates one gap of up to 300ms per 10s, as it does with the AS5600 ([Servo settings](servo-settings.md)). A longer gap ends the run with `IMU not answering`; a second gap within 10s ends it with `IMU dropped out twice in 10s`.

![No sensor bound after a scan that found none](view:setup-wireless-imu-error)

| Last error | Meaning |
|---|---|
| `NO CONNECTION: SENSOR ON, IN RANGE, NO PHONE ON IT?` | a connect, its set-up or the link failed; the panel tries again every 1s. A phone connected to the sensor keeps the panel out |
| `NOT A WITMOTION SENSOR: NO ISSC UART SERVICE` | the bound device lacks the sensor's service; no new try until another bind |
| `THE SENSOR DID NOT TAKE THE COMMAND` | a write of a command was refused or not answered within 1s; the rest of its group was dropped and goes again at the next connection |
| `NO SENSOR FOUND: IS IT ON, NO PHONE ON IT?` | the scan listed no sensor |
| `NO OTHER SENSOR FOUND: THE BOUND ONE IS LINKED` | the scan listed no sensor while the bound one is connected; a connected sensor does not advertise |
| `THE SCAN DID NOT START: CODE <n>` | Bluetooth refused the scan with that code |

The settings:

| Setting | Range | Default | What it does |
|---|---|---|---|
| IMU | ON, OFF | OFF | Bluetooth LE, the link to the bound sensor and its reading; the first ON starts Bluetooth LE until the panel restarts |
| Angle source | AS5600, IMU | AS5600 | where the servo runs take their angle from ([Servo settings](servo-settings.md)); shown with the IMU off too, so the source can go back to the AS5600 without switching the IMU on |
| IMU axis | X, Y, Z | X | the angle the servo runs and RUDDER read; `Z: 1° WITH THE MAGNETOMETER` |
| IMU sign | +, - | + | `-` turns that angle round |
| IMU rate | 10Hz, 20Hz, 50Hz, 100Hz, 200Hz | 50Hz | the sensor's frames a second; sent only when changed, it takes effect after the sensor is switched off and on; the sensor ships at 10Hz, and `CONNECTED` shows the rate it runs |
| IMU algorithm | 6-AXIS, 9-AXIS | 6-AXIS | 9-AXIS takes the magnetometer for Z |
| IMU filter | 1 to 50 | 1 | the panel's low-pass on the angle; 1 is off |
| IMU settling time | 0.5 to 10.0s, steps of 0.5s | 3.0s | RUDDER AUTO on the IMU: how long the angle must hold before a measure takes it |
| IMU precision | 0.05 to 1.00°, steps of 0.05° | 0.10° | RUDDER AUTO on the IMU: the span the angle may cover in that time |

IMU filter smooths the angle on the panel, per frame and axis: the angle moves by 1/k of the difference to the new frame, with k the setting. It covers 63% of a step in about k frames, so at 10Hz an IMU filter of 10 takes about 1s. The frames per second are the rate the sensor runs, which `CONNECTED` shows, not the IMU rate asked. The angle rows, RUDDER, AUTO and the servo runs read the filtered angle. The first frame after connection sets the initial filter value. The filter follows the shorter angle when crossing 180°. AUTO waits until the filtered angle has settled, up to 3000ms plus the time the setting can take ([The Bluetooth IMU](https://github.com/subtilitas/rcbench/wiki/IMU#the-filter)).

IMU settling time and IMU precision are the rule a RUDDER AUTO measure keeps on the IMU: it takes the angle once the readings over the last settling time span no more than the precision. The BWT901CL in 6-AXIS keeps creeping for 5s to 10s after a servo step; with the defaults the angle taken lies within 0.07° of where the reading settles. A longer settling time or a smaller precision makes each measure slower and closer; the measure's wait grows with the settling time ([The settle rule](https://github.com/subtilitas/rcbench/wiki/IMU#the-settle-rule)).

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
| Scan for Dial | `SCAN` | | a tap opens [Search](#search) and scans 10s for the Dials in range; a switch to ARMED stops the scan, `STOPPED BY ARM`; refused while ARMED, with `NO DIAL SCAN WHILE ARMED: SWITCH TO SAFE FIRST` in the band |
| Bound Dial | `NONE` or the Dial's name | `NONE` | a tap opens [Search](#search) on the last scan's list. RESET keeps the bound Dial |
| Dial step, throttle | 0.5%, 1%, 2%, 5% | 1% | the throttle one detent moves |
| Dial step, servo | 0.5°, 1°, 2°, 5° | 1° | the horn angle one detent moves |

The panel connects to the bound Dial by its address, without a scan, and again after each loss. A Dial in range that is not bound is never taken.

On the bench screens:

- A turn moves nothing while the bench is SAFE, and turns made while SAFE are not kept for the ARM. The first EVENT after a connect, an EVENT more than 300ms after the last and a jump of more than 16 detents move nothing.
- A press of the Dial's screen button while ARMED sets the throttle to 0% on MOTOR & ESC and the horn to CENTER on SERVO. A 1s hold DISARMs, also while an ARM is on its way, and ends an ESC STICK run as ABORT does. On SERVO it ends a test, a LIMIT RUN and a RUDDER AUTO run in every phase, also while the run holds the bench SAFE for its programmer session.
- A Dial lost while ARMED is an error: 1000ms without an EVENT, or the disconnect, switches the bench to SAFE with all outputs off, and the band shows `DIAL LOST WHILE ARMED: SWITCHED TO SAFE, ALL OUTPUTS OFF`. 500ms without an EVENT shows `DIAL: NO EVENT FOR 500ms`. The clock starts at the first EVENT of a connection, so a connect that has sent none is no loss.
- While SAFE the Dial navigates the menu, SETUP, the SERVO tiles with their SETTINGS, the OUTPUTS board and every choice list: a ring in the accent colour marks the focused item, a turn moves it, a press taps it and a 1s hold goes back as the home tag does. A press on a number opens its keypad; a turn then sets the value, a press is OK and a hold CANCEL. A press on an on/off or option row sets it by turning: a press keeps, a hold restores. No 2s hold on the panel completes from the Dial.
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

## The power node

![WIRELESS on the POWER NODE entry, connected](view:setup-wireless-pnode)

The power node is an ESP32-C6 with its own cell that reads an INA228 board in the ESC's (electronic speed controller's) supply line and sends its readings over Bluetooth LE. The node finds the INA228 at any address from 0x40 to 0x4F. INA228 source decides whose INA228 MOTOR & ESC shows: the IOmcu's, wired, or the node's.

| Setting | Range | Default | What it does |
|---|---|---|---|
| Power node | ON, OFF | OFF | Bluetooth LE and the link to the bound node; OFF ends the link without an error |
| INA228 source | WIRED, NODE | WIRED | WIRED: the IOmcu's INA228 ([Sensors](setup-sensors.md)); NODE: the power node's readings. Shown with the node off too, so the source can go back to WIRED |
| Scan for node | `SCAN` | | a tap opens [Search](#search) and scans 10s for the power nodes in range; a switch to ARMED stops the scan, `STOPPED BY ARM`; refused while ARMED, with `NO NODE SCAN WHILE ARMED: SWITCH TO SAFE FIRST` in the band |
| Bound node | `NONE` or the node's name | `NONE` | a tap opens [Search](#search) on the last scan's list. RESET keeps the bound node |
| Node name | the node's name, `OLD FIRMWARE`, `NOT LINKED` | | the name kept on the node, up to 16 characters A to Z, 0 to 9, space, `-`, `_`, `.`. A tap opens the keyboard; OK writes it to the node, an empty name clears it. Bound node and Search show it. Only while SAFE and linked |
| Node battery | `3.92V`, `LOW 3.41V`, `NO CELL`, `READ FAILED`, `NOT LINKED` | | the node's cell, from its status every 1s; `LOW` under 3.5V |
| Node INA228 | `0x40, 500uΩ`, `NOT FOUND`, `NOT LINKED` | | the address the node found its INA228 at, and the shunt the node holds, from its INFO after each connect and each shunt change |
| INA228 address | AUTO, 0x40 to 0x4F | AUTO | AUTO: the node searches 0x40 to 0x4F; a fixed address: the node asks that one only. Each step of `<` or `>` writes it to the node. Only while SAFE and linked |
| Node shunt | 50 to 10000µΩ | 200µΩ | the shunt of the INA228 board on the node; the MATEK I2C-INA-BM's is 200µΩ. The panel writes it to the node after each connect and each change when the node holds another; the node keeps it through restarts. The full scale is 40.96mV over the shunt: 204.8A on 200µΩ, 81.92A on 500µΩ, 4.096A on 10000µΩ. Each change clears the node's charge and energy totals. The display node's own SETUP sets the shunt too; the last change wins, and a change on the node comes into this setting at the next link. Does not change while ARMED. A node whose firmware takes no shunt: `POWER NODE TAKES NO SHUNT: UPDATE ITS FIRMWARE` in the band |

![WIRELESS on the POWER NODE entry, scrolled to Node INA228, INA228 address and Node shunt](view:setup-wireless-pnode-shunt)

![Node name tapped: the name keyboard](view:setup-wireless-pnode-name)

With INA228 source on NODE:

- MOTOR & ESC's voltage, current, power, peaks, charge and energy are the node's while the IOmcu link is up. The ESC's own telemetry stays beside them. The log's `source` column reads `node`.
- A reading older than 300ms is none.
- With Power node ON and a node bound, the MOTOR & ESC tile on the [menu](overview.md#the-simulated-mark) carries no SIMULATED mark, also with the IOmcu's INA228 OFF and with the node not connected.
- An ARM of MOTOR & ESC or ESC STICK is refused while Power node is OFF or no reading came in the last 100ms, with `Cannot switch to ARMED: no power node readings. Set INA228 source WIRED or connect the node.` in the band. SERVO and OUTPUTS arm whatever the node does.
- In a MOTOR & ESC or ESC STICK run, a gap without a reading of up to 300ms rides, once in 10s. A longer gap, or a second one within 10s, switches the bench to SAFE with all outputs off, and the band shows `POWER NODE READINGS LOST WHILE ARMED: SWITCHED TO SAFE, ALL OUTPUTS OFF`. A SERVO or OUTPUTS run does not end on the node.
- INA228 source, Node shunt, Node name and INA228 address do not change while ARMED: `NO NODE SETTING CHANGE WHILE ARMED: SWITCH TO SAFE FIRST`. A node whose firmware has no name: `THE NODE TAKES NO NAME OR ADDRESS: UPDATE ITS FIRMWARE`.
- The node's readings feed no protection: the current limits and trips read their own sensors.

| State | When |
|---|---|
| `OFF` | Power node OFF |
| `STARTING BLUETOOTH` | Bluetooth LE starts |
| `NO BLUETOOTH ON C6` | the host has no sync with the C6; it keeps trying |
| `C6 FAILED: RESTART` | the C6's link failed until the panel restarts |
| `NO NODE BOUND` | Bluetooth runs; no node is bound |
| `SCANNING` | a scan runs |
| `CONNECTING` | the panel connects to the bound node and sets up its service |
| `NOT A POWER NODE` | the bound device serves no node service, or is a balance node |
| `CONNECTED, NO READING` | the link stands; no reading yet |
| `OTHER PROTOCOL 2` | the node's firmware speaks another protocol version; nothing it sends is taken |
| `INA228 SET-UP REFUSED` | the node names a shunt and range the panel cannot scale; nothing it sends is taken |
| `NO INA228 ON THE NODE` | the node sends, but its INA228 is not online |
| `CONNECTED` | a reading within the last 300ms |
| `NO READING FOR 300ms` | linked, no reading for 300ms |
| `NODE LOST` | the link dropped after a reading |

## The balance node

![WIRELESS on the BALANCE NODE entry, reporting](view:setup-wireless-bal)

The balance node measures the vibration and the index of a rotor for [Balance](balance.md#the-balance-node). It drives nothing, so its loss is no error.

| Setting | Range | Default | What it does |
|---|---|---|---|
| Balance node | ON, OFF | OFF | Bluetooth LE and the link to the bound node; OFF ends the link |
| Scan for node | `SCAN` | | a tap opens [Search](#search) and scans 10s for the balance nodes in range, `rcbench-bal-XXXX`; a switch to ARMED stops the scan; refused while ARMED, with `NO NODE SCAN WHILE ARMED: SWITCH TO SAFE FIRST` in the band |
| Bound node | `NONE` or the node's name | `NONE` | a tap opens [Search](#search) on the last scan's list. RESET keeps the bound node |
| Node name | the node's name, `OLD FIRMWARE`, `NOT LINKED` | | the name kept on the node, up to 16 characters A to Z, 0 to 9, space, `-`, `_`, `.`. A tap opens the keyboard; OK writes it to the node, an empty name clears it. Bound node and Search show it. Only while SAFE and linked |
| Node battery | `3.91V`, `3.40V LOW`, `NO READING`, `--` | | the node's cell, from the STATUS the node sends beside its reports; LOW under 3.50V |
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

## The label printer

![WIRELESS on the PRINTER entry, the roll read](view:printer-idle)
![A label ended with COVER OPEN](view:printer-error)

The printer prints the servo labels of SERVO LABEL's PRINT ([Servo settings](servo-settings.md)). It has no switch: the panel links to it for a request and ends the link 30s after its last answer. A request is refused while a device update runs, and an update while a request or a scan for printers runs. The printer advertises no service: a scan lists the devices whose public address starts with `A4:93:40`.

| Row | What it shows or does |
|---|---|
| Scan for printer | `SCAN`, `SCANNING`, `FOUND n`, `NONE FOUND`, `STOPPED BY ARM`; a tap opens [Search](#search) and scans 10s. Refused while ARMED, while a print or a device update runs, and while a scan runs, the reason in the band |
| Bound printer | its name, else its address, else `NONE`; a tap opens [Search](#search) on the last scan's list |
| Address | the bound address, `A4:93:40:30:FA:71 P` |
| State | `NOT BOUND`, `IDLE`, `LINK UP`, the request's phase (`SENDING 3/5`) or its result (`PRINTED, 293 LEFT`, `COVER OPEN`) |
| Roll | `40x30mm GAP 3mm` as the printer reports it, `NO LABEL` |
| Labels left | the printer's own count |
| Read printer | reads the roll and the labels left |
| Test label | prints the bench's test label; SAFE only |

Read printer and Test label are locked with no printer bound, while a request runs and while an update runs; the help line names the reason. Opening the entry with a printer bound reads its roll once. A request that ends with an error puts its text in the band. RESET PRINTER keeps the bind.

## Search

![SEARCH on the power node, two nodes found](view:setup-wireless-search)
![SEARCH for the printer while a scan runs](view:printer-search)

![SEARCH on the IMU, two sensors found, four other devices hidden](view:setup-wireless-imu-search)

SEARCH finds and binds the IMU, the Dial, the power node, the balance node or the label printer. Scan for sensor, Scan for Dial, Scan for node and Scan for printer open it and scan 10s; Bound sensor, Bound Dial, Bound node and Bound printer open it on the last scan's list. The IMU's scan lists the devices that advertise the WitMotion sensor's service; the sensor's radio is named `HC-06`. The printer's bind and its UNBIND are refused while a print or a device update runs. A refused scan says why in the band, and SEARCH opens on the last list. The home tag returns to WIRELESS on the device's entry.

- The line over the list: `SCANNING: 7s LEFT`, `FOUND 2: TAP ONE TO BIND IT`, `NONE FOUND`, `STOPPED BY ARM` or `NO SCAN YET: TAP SCAN AGAIN`.
- The list: up to 8 devices, strongest first, each with its name or address and its signal in dBm. The bound one is lit. The list empties when a scan starts and fills when it ends.
- Right: the device's kind, its link state, the bound device with its address under its name (`P` public, `R` random), and SCAN AGAIN, a new scan of 10s. SCAN AGAIN is grey while a scan runs. For the IMU, the devices the last scan heard and did not list: `4 OTHER DEVICES HIDDEN`.

![The bind question](view:setup-wireless-search-bind)

Tap a device to bind it: the question shows its name, address and signal, and the device it replaces. BIND binds it; SETUP keeps it and asks for a save, and SEARCH stays and lights it once it is bound. CANCEL, or the home tag, binds nothing. BIND binds nothing while a scan runs, `A SCAN IS RUNNING: PICK THE NODE WHEN IT ENDS`, or when a new scan changed the list under the question, `NODE NOT BOUND: THE LIST CHANGED, PICK IT AGAIN`; the Dial's texts name the Dial, the IMU's the sensor. The IMU's BIND binds nothing while its calibration runs or Bluetooth LE starts, `NO BIND NOW: THE CALIBRATION RUNS OR BLUETOOTH STARTS`. After a bind the panel connects to the sensor at once and again 1s after a lost link.

### Unbind

`HOLD 2s: UNBIND`, under SCAN AGAIN, forgets the bound device. Hold it 2s: the link ends, SETUP keeps no device and asks for a save, and the card reads `NONE`. Let go before 2s and nothing is unbound. With nothing bound the button is grey, and a tap reads `NOTHING IS BOUND: NOTHING TO UNBIND`. To use the device again, scan and bind it.

ARMED does not refuse it. A run that uses the device meets it as a lost link:

- Dial: lost at once; while ARMED that is an error, and every output goes off.
- Power node: a run that reads its INA228 gets no reading and ends as an error after 300ms, every output off.
- Balance node: its readings go stale.
- IMU: a run that reads its angle meets a gap longer than 300ms and ends with `IMU not answering`.

## Related pages

- [Setup](setup.md)
- [The screen guide](band.md#the-screen-guide)
- [Sensors](setup-sensors.md)
- [Motor and ESC](motor.md)
- [Balance](balance.md)
- [Servo](servo.md)
- [Sensor nodes](https://github.com/subtilitas/rcbench/wiki/Nodes#the-panel)
