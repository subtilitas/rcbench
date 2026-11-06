# The Bluetooth IMU

<sub>**English** · [Deutsch](IMU-de.md)</sub>

The Bluetooth IMU (inertial measurement unit) is a WitMotion BWT901CL angle
sensor placed on a control surface. It sends its angle over Bluetooth LE
(Low Energy, BLE) to the panel. The board's ESP32-C6 runs the BLE
controller; the ESP32-P4 runs the host, NimBLE, over the SDIO (Secure
Digital input/output) link that Wi-Fi uses too. The sensor needs no wire to
the panel, no pairing and no PIN.

The reading is a measurement, not a protection. It becomes stale 300ms
after the last angle frame. A servo run tolerates a gap of up to 300ms
once per 10s, as it does with the AS5600
([Servo procedures](Servo.md)). Changing ANGLE SOURCE gives SERVO the new
source's state in the next display frame: the IMU reading, valid or not,
or no AS5600 reading until the next reading from that sensor.

If an incoming angle frame ends a stale period before the next display
frame, SERVO still receives the stale state. A stale reading timestamped
300ms after the previous angle frame is delivered before the new reading.

WitMotion, BWT901CL and HC-06 are names of their makers' products, ISSC and
Microchip are names of their owners, and Bluetooth is a trademark of its
owner. rcbench is independent: it is not affiliated with, endorsed by or
sponsored by them. The names say which devices and which service the panel
talks to.
[Legal notice and disclaimer](../README.md#legal-notice-and-disclaimer) holds
the project's notice.

| Part | Where |
| --- | --- |
| the BLE central: scan list, connect, GATT (generic attribute profile) discovery, subscribe, writes, retries | `protocols/bleuart`, no radio code |
| the sensor's frames and commands | `protocols/witmotion`, no pin driver |
| the reading: angles, the low-pass filter, zero, still time, rate, battery, age | `shared/bench/imu.c` |
| the sensor's side: the reading, its commands, the calibration, the gaps, the handout to SERVO; no radio | `shared/ui/imu_sensor.c` |
| the BLE link: the central for the IMU, its devices, errors and the C6's version in the view's types | `shared/ui/imu_ble.c` |
| the controller: joins the link and the sensor's side, the link state, the view | `shared/ui/imu_ctl.c` |
| the screen and SETUP's IMU entry | `shared/ui/imu_screen.c`, `shared/ui/sensors_pane.c` |
| the IMU task: events in, actions out, every 5ms | `firmware/panel/main/panel_imu.c` |
| NimBLE's start, its calls and its callbacks | `firmware/panel/main/panel_ble.c` |

## Start

The first IMU ON (SETUP > SENSORS > IMU, also at power-up with IMU stored
ON) starts Bluetooth LE: the C6's link, the host task `nimble_host` and the
wait of 3,000ms for its sync ([Building, Bluetooth LE through the
ESP32-C6](Building.md#bluetooth-le-through-the-esp32-c6)). It stays until
the panel restarts; IMU OFF ends the scan and the link only. The C6's link
takes up to about 25s on a C6 that does not answer. A failed link holds the
C6 in reset until the panel restarts: Wi-Fi and Bluetooth are off.

| State | Meaning |
| --- | --- |
| STARTING BLUETOOTH | the start runs |
| NO BLUETOOTH ON C6 | the host has no sync with the C6; it keeps trying, and a late sync clears the state |
| C6 FAILED: RESTART | the C6's link failed |
| NO SENSOR BOUND | Bluetooth runs, no sensor is bound |
| SCANNING | a scan runs |
| CONNECTING | a connect or its GATT steps run, also after a drop |
| BOUND | subscribed and no angle frame yet; or the bound device is not a WitMotion sensor |
| CONNECTED 50Hz | angle frames within 300ms, at the rate shown |
| NO FRAMES | subscribed, the frames stopped |

## Scan and bind

SCAN FOR SENSORS runs an active scan of 10,000ms with the controller's
duplicate filter. A device is listed when its advertising data has the
16-bit UUID 0x18F0 or the ISSC Transparent UART service's 128-bit UUID;
every other device is counted as hidden. 0x18F0 is not a Bluetooth SIG
assignment, so the connect checks the service. The list holds 8 devices,
strongest first: name, address and RSSI (received signal strength
indication), for example `HC-06  00:0C:BF:08:19:29  -50dBm`. The sensor's
radio advertises the name `HC-06`.

A tap on a row binds that sensor at once. SETUP keeps its address with the
address type, `00:0C:BF:08:19:29 P` (public) or `... R` (random), as
`imu_ble`, and its name as `imu_name`, 23 characters, and asks for a save.
The save goes to nvs at the next frame with the bench SAFE and no supply
on or starting.
IMU OFF keeps the bind; at IMU ON after a restart the panel connects to it
without a scan. Only a new bind replaces it. An HC-05 binding is not
carried over; scan and bind once. The HC-05's key `imu_addr` is erased
from nvs at the first start.
Binding another sensor replaces the one before. A scan while a sensor is
connected keeps the link; a scan while a connect is pending cancels it,
and the connects go on after the scan.

## The link

| Step | Time limit | Fails as |
| --- | ---: | --- |
| connect to the bound address, interval 30 to 50ms, latency 0, supervision timeout 4,000ms, connect scan 30ms in every 60ms | 5,000ms | NO CONNECTION, again after 7,000ms for a failure the host reports (HCI 0x3E among them), after 1,000ms for the time limit |
| find the ISSC service 49535343-FE7D-4AE5-8FA9-9FAFD205E455 | 5,000ms | NOT A WITMOTION SENSOR, no retry until a new bind or IMU OFF and ON |
| its characteristics: 1E4D with NOTIFY, 8841 with WRITE or WRITE NO RESPONSE | 5,000ms | NOT A WITMOTION SENSOR |
| 1E4D's CCCD (client characteristic configuration descriptor) | 5,000ms | NOT A WITMOTION SENSOR |
| MTU (maximum transmission unit) exchange, 247 asked | 5,000ms | logged only |
| subscribe: 0x0001 to the CCCD | 5,000ms | NO CONNECTION, again after 1,000ms |
| read 6DAA once, logged | 5,000ms | logged only |

Each notification on 1E4D carries the sensor's UART stream in chunks of up
to 20 bytes, not cut at frames. The WitMotion parser takes any chunk size;
a lost notification breaks one frame, which it counts as bad and resyncs.
A drop of the link (reason in the console line) reconnects after 1,000ms.
A connection parameter update the sensor asks for is accepted and logged.
The characteristics ACA3 and 6DAA are never written.

The connect values are chosen as a phone's central asks them, not
measured against others; they sit together in
`protocols/bleuart/include/bleuart.h` (`BLEU_ITVL_*`, `BLEU_LATENCY`,
`BLEU_SUPERVISION`, `BLEU_CONN_SCAN_*`). The connect uses the own address
type the sync inferred (`ble_hs_id_infer_auto()`). On one panel (station,
2026-11-05) the first try connected 1.09s after the tap, and the first
frames came 1.88s after it. The controller then moved the link to
interval 20ms and supervision timeout 5,120ms. The MTU came out at 64.

NO CONNECTION reads `NO CONNECTION: SENSOR ON, IN RANGE, NO PHONE ON IT?`:
a phone connected to the sensor keeps the panel out, because a connected
peripheral stops advertising. Any central in range can connect to the
sensor; it has no pairing.

The link is not authenticated: the sensor's ISSC service offers no
pairing or bonding. A BLE device in range that wins the connection to the
stored address and serves the ISSC service can send angle frames the panel
takes as the sensor's. RUDDER's AUTO run with ANGLE SOURCE IMU programs end
points from them. `SECURITY.md` lists this as a current limitation.

## The sensor

Frames: 0x55, the type, four int16 words little-endian, the low byte of the
sum. The panel reads 0x53 (the angles, 180° at 32768, and the firmware
version), 0x52 (the battery, the fourth word unsigned, V x 100) and 0x51
(the temperature, °C x 100). Only an angle frame makes the reading valid.

| Command | Bytes | Sent |
| --- | --- | --- |
| unlock | `FF AA 69 88 B5` | before every other command; harmless on the older firmware (not measured on this sensor) |
| CALSW 1, 3s, CALSW 0, SAVE | `FF AA 01 01 00`, `FF AA 01 00 00`, `FF AA 00 00 00` | `HOLD 2s: CALIBRATE ACCELEROMETER`, connected |
| RATE, SAVE | `FF AA 03 <code> 00`, `FF AA 00 00 00` | a change of IMU rate: 0x06 10Hz, 0x07 20Hz, 0x08 50Hz, 0x09 100Hz, 0x0B 200Hz |
| AXIS6, SAVE | `FF AA 24 01 00` (6-AXIS) or `FF AA 24 00 00` (9-AXIS), `FF AA 00 00 00` | a change of IMU algorithm |

Commands go on 8841, 50ms apart, one write in flight, each answered
within 1,000ms. With WRITE in 8841's properties the panel writes with a
response; with WRITE NO RESPONSE only, without. A write that is refused or
not answered drops the rest of its group and shows `THE SENSOR DID NOT
TAKE THE COMMAND`. What the group was for is due again at the next
connection: a calibration's exit (unlock, CALSW 0, SAVE), a RATE or an
ALGORITHM, with the SETUP values of then. IMU OFF fails a write in flight
the same way, so a calibration ended by IMU OFF still sends CALSW 0 and
SAVE once the sensor is connected again. A RATE or ALGORITHM change made while the sensor is not
connected waits until its frames arrive. A change made during calibration
waits until after CALSW 0 and SAVE. Unlock, the writes and SAVE are queued
together only when all fit in the 8 command slots. Until then, the change
waits; the eventual write uses the latest SETUP value. A new RATE takes
effect after the sensor is switched off and on.

RATE goes to the sensor only when it changes, not at each connect: each
RATE comes with a SAVE to the sensor's flash, and the rate takes effect
only after a power cycle. SETUP's RATE is what the panel asks for, 50Hz
by default. The sensor runs the rate it last saved, 10Hz as shipped. The
IMU screen and SETUP > SENSORS > IMU show the rate the angle frames
arrive at, as measured on the panel. Which RATE values fit the BLE link
is not measured yet: 50Hz with all frames needs 2,200B/s.

IMU OFF during a
calibration ends it FAILED; its CALSW 0 and SAVE go at the next connect.
ZERO takes the current angles as zero on the panel; nothing goes to the
sensor, and a restart forgets it.

## The filter

IMU filter (SETUP > SENSORS > IMU, key `imu_filter`) is a low-pass on the
angle, on the panel. It runs once per angle frame and per axis:

```text
deg += (new - deg) / k        k = IMU filter, 1 to 50, default 1
```

| Value | Effect |
| --- | --- |
| 1 | no filtering: each frame's angle as it came |
| k | each frame moves the angle by 1/k of the rest; it covers 63% of a step in about k frames |
| 0 or above 50, stored | taken as 1 or 50 |

The time constant is about k frames, so it depends on the rate the sensor
runs, which is its saved rate and not SETUP's RATE (see above). At 10Hz,
IMU filter 10 takes about 1s to 63% of a step; at 50Hz about 0.2s.

- Every reading uses the filtered angle: the IMU screen, ZERO, RUDDER,
  AUTO, the servo test and the LIMIT RUN.
- The first angle frame after a connect, a reconnect, a bind, IMU ON or a
  stale reading seeds the filter: the angle starts at that frame's value,
  not at 0.
- The difference is wrapped to -180 to 180° first. A crossing from 179.9°
  to -179.9° moves the angle 0.2° the short way, not through 0. This
  holds on every axis.

The still time is the sensor's: the angle within 0.5° (`IMU_STILL_DEG`)
of the stretch's first frame. While the filtered angle lags the sensor's
by more than 0.05° (`IMU_SETTLE_DEG`), the still time reads 0. The panel
works out that lag from the distance at the stretch's first frame, shrunk
by (1 - 1/k) each frame. Once it is 0.05° or less, the still time counts
from the sensor's stop. AUTO on RUDDER, which waits for 500ms of still
time, then takes a settled angle, within 0.05° of the sensor's. AUTO waits
up to 3,000ms after the command plus the filter's longest settle
(`imu_filter_settle_ms()`): the frames a distance of 180° needs to come
within 0.05°, at 1.5 times the frame period measured, 300ms a frame at
most. That is 0ms for IMU filter 1, 11,700ms for IMU filter 10 at 10Hz and
60,900ms for IMU filter 50 at 10Hz. The report names the wait:
`within 14700ms of the command`.

Current limitation: with IMU filter above 1, a move of the servo test
counts as late when the filtered angle has not settled by the next
command. A move that settles in time is timed to the sensor's stop.

## Console lines

The lines carry the tags `imu` and `ble`:

```text
I (…) imu: IMU ON: starting Bluetooth LE
I (…) ble: synced in <n>ms, own address type <t>
I (…) ble: scan for 10000ms
I (…) ble: connect to 00:0C:BF:08:19:29 type 0 for 5000ms
I (…) ble: connect: own address type 0, interval 24-40 x 1.25ms, latency 0, timeout 400 x 10ms, scan 96/48 x 0.625ms
I (…) ble: connect: last report <r>dBm, <a>ms ago
I (…) ble: connect: status 0x0, handle <h>, token <k>
I (…) ble: connect: record: handle <h>, peer 00:0C:BF:08:19:29 type 0 (over the air type 0), own type 0, role 0, interval <i> x 1.25ms, latency 0, timeout <s> x 10ms, clock accuracy <c>
I (…) ble: MTU exchange: status 0, MTU <m>
I (…) ble: 6DAA: 8 bytes 00 0C 00 0C 00 00 00 02
I (…) imu: link <a> -> <b>, error <e> code 0x<c>
I (…) imu: <n> frames in 1000ms, rate <r>Hz, roll <x> pitch <y> yaw <z> deg, bad <k>, battery <v>V, <d> events dropped
I (…) imu: command FF AA 69 88 B5
I (…) ble: disconnect: reason 0x<r>, handle <h>, token <k>
I (…) ble: disconnect: record: handle <h>, peer …
I (…) ble: connect: failed to be established, reason 0x<r>
```

Reason 0x23E is HCI 0x3E, connection failed to be established, as NimBLE
reports it (0x200 + the HCI code). The console build's `ble controller`
sends the C6's controller init and enable requests again
([BenchConsole.md](BenchConsole.md)).

`<n> frames` counts frames of every type; the rate counts angle frames.

## Limits

| Item | Value |
| --- | --- |
| battery | 250mAh, 4h on a charge (datasheet) |
| output rate | 0.2 to 200Hz, 10Hz as shipped; the panel offers 10 to 200Hz, 50Hz by default |
| BLE throughput | 20 bytes a notification; at 30 to 50ms and one notification a connection event 400 to 667B/s, below the 2,200B/s of 50Hz with all frames on; the notifications per event and the rates that fit are not measured |
| connect | on one panel (station, 2026-11-05) these values connected on the first try; with interval 15ms, supervision timeout 1,000ms and a 10ms/10ms connect scan, 35 tries at -59dBm ended with HCI 0x3E |
| accuracy X, Y | 0.05° (datasheet), 0.2° (maker's web page) |
| accuracy Z | 1° after the magnetometer's calibration, which the servo's motor disturbs; Z drift without it is not stated |
| a run's angle | the automatic test judges in AS5600 counts: 360 / 4096 = 0.088° a count; RUDDER, AUTO, the LIMIT RUN and MEASURED take the degrees as read, shown to 0.01° |
| a link drop | whether the sensor's module buffers or discards the bytes it cannot send is not known; the panel reconnects after 1,000ms while IMU is ON |
| Wi-Fi beside BLE | the C6 time-shares its radio; the effect on either is not measured |
| writes | whether a write on 8841 reaches the sensor is not measured |
| the reading's age | stale after 300ms |
