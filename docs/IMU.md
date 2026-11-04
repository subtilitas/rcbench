# The Bluetooth IMU

<sub>**English** · [Deutsch](IMU-de.md)</sub>

A WitMotion BWT901CL angle sensor sits on a control surface and sends its
angle over Bluetooth. An HC-05 Bluetooth module on the panel's UART1
(universal asynchronous receiver-transmitter) receives it as SPP (serial
port profile) master. The panel sets the HC-05 up itself over AT commands:
it finds the module's baud, scans, binds the sensor and restarts the
module in data mode. The reading is a measurement, not a protection: it is
stale 300ms after the last angle frame, and a servo run rides through a gap
of up to 300ms once per 10s, as through the AS5600's
([Servo procedures](Servo.md)). A switch of ANGLE SOURCE hands SERVO the
new source's state in the next frame: the IMU's reading, valid or not, or
no AS5600 reading until the part's next one. A stale stretch that a frame
ends before the screen's next frame still reaches SERVO: a stale reading
timed 300ms after the last frame goes ahead of the new one.

WitMotion, BWT901CL, HC-05 and HC-06 are names of their makers' products,
and Bluetooth is a trademark of its owner. rcbench is independent: it is
not affiliated with, endorsed by or sponsored by them. The names say which
devices the panel talks to.
[Legal notice and disclaimer](../README.md#legal-notice-and-disclaimer) holds
the project's notice.

| Part | Where |
| --- | --- |
| the HC-05's AT set-up | `protocols/hc05`, no pin driver |
| the sensor's frames and commands | `protocols/witmotion`, no pin driver |
| the reading: angles, zero, still time, rate, battery, age | `shared/bench/imu.c` |
| the controller: the HC-05's operations, the sensor's commands, the link state | `shared/ui/imu_ctl.c` |
| the screen and SETUP's IMU entry | `shared/ui/imu_screen.c`, `shared/ui/sensors_pane.c` |
| the UART, the KEY line and the task | `firmware/panel/main/panel_imu.c` |

## Wiring

The pins are `PANEL_IMU_*` in
`firmware/panel/components/board/include/board_pins.h`. The signals go to
header P3, KEY on IO29 beside the heartbeat's IO28. The 5V and GND come from
H9, the RS485 header, which the bench does not use otherwise: its pin 1 is
Core_5V, the same net as pin 1 of the CAN header H11, so the CAN cable stays
as it is. IO4 and IO5 are the ESP32-P4's pad-JTAG pins (MTMS, MTDO); with
debugging over USB-JTAG they are plain GPIOs.

| HC-05 (ZS-040 breakout) | Panel | Define |
| --- | --- | --- |
| VCC (3.6 to 6V) | Core_5V of header H9 (RS485), pin 1 | |
| GND | GND of header H9 (RS485), pin 2 | |
| TXD (3.3V) | IO5 of header P3, UART1 RX | `PANEL_IMU_PIN_RX` |
| RXD | IO4 of header P3, UART1 TX, 3.3V | `PANEL_IMU_PIN_TX` |
| EN (KEY) | IO29 of header P3, high for AT commands | `PANEL_IMU_PIN_KEY` |
| STATE | not wired; the reading's age stands in for it | `PANEL_IMU_PIN_STATE` (`GPIO_NUM_NC`) |

`PANEL_IMU_UART_NUM` is 1: UART0 is the console's. `PANEL_IMU_BAUD` is
115200, the BWT901CL's fixed baud. The HC-05's breakout regulates 3.3V on
board, its TXD is 3.3V and its RXD is not 5V tolerant, so the panel's 3.3V
pins fit without level shifting. Its current is up to 40mA (datasheet). The
5V is not switched: the panel cannot power-cycle the module. The panel
raises KEY after the module's power-up, which puts it in AT mode at its data
baud; a module that refuses AT that way is not handled. With STATE wired to
a GPIO and named in `PANEL_IMU_PIN_STATE`, the console logs its level when
it changes.

## The HC-05's set-up

The panel holds KEY high from the first byte of an operation to its end,
100ms before the first byte. Each command waits 1000ms for its OK, AT+INQ
13.52s, AT+RNAME? 3000ms, AT+PAIR 22000ms, AT+RESET 1500ms. The module's
README in `protocols/hc05` has every answer and error.

| Operation | When | Commands |
| --- | --- | --- |
| PROBE | IMU ON, and every 10s while no module answers, also after a scan or bind that found no AT | `AT` at 115,200, 38,400, then 9,600 baud; `AT+VERSION?` |
| SCAN | SCAN FOR SENSORS, not while a calibration runs or a sensor command waits | `AT+INIT` (ERROR:(17) ignored), `AT+INQM=1,9,9` (11.52s), `AT+INQ`, `AT+RNAME?<addr>` per device, up to 8 |
| BIND | a tap on a found device | `AT+ROLE=1`, `AT+PSWD=1234` (`AT+PSWD="1234"` on an ERROR), `AT+PAIR=<addr>,20`, `AT+BIND=<addr>`, `AT+CMODE=0`, `AT+UART=115200,0,0`, `AT+RESET` |
| ANY | CONNECT TO ANY SENSOR, after a scan refused with ERROR:(1F) | `AT+ROLE=1`, `AT+PSWD=1234`, `AT+CMODE=1`, `AT+UART=115200,0,0`, `AT+RESET` |

After AT+RESET the module restarts in data mode with KEY low. The bound
address and the name from the scan go into SETUP (`imu_addr`, `imu_name`,
23 characters kept) and a save is asked; ANY keeps `*`. While no operation
runs, the UART's bytes are the sensor's frames. IMU OFF during an
operation keeps the HC-05 busy until the fence has caught its late
answers: up to 14,520ms after AT+INQ, 23,000ms after AT+PAIR. A probe of a
silent module ends after 5,100ms: 100ms KEY settle, 1000ms at each of the
3 bauds, and the 2000ms fence.

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

The commands go 50ms apart. A RATE or ALGORITHM change made while the sensor
is not connected goes when its frames come. One made during a calibration
goes after its CALSW 0 and SAVE. Unlock, the writes and SAVE queue only
once all of them fit the 8 command slots; until then the change waits and
the write carries SETUP's last value. A new RATE takes effect after
the sensor is switched off and on. ZERO takes the current angles as zero on
the panel; nothing goes to the sensor, and a restart forgets it.

## Limits

| Item | Value |
| --- | --- |
| battery | 250mAh, 4h on a charge (datasheet) |
| output rate | 0.2 to 200Hz, 10Hz as shipped; the panel offers 10 to 200Hz, 50Hz by default |
| accuracy X, Y | 0.05° (datasheet), 0.2° (maker's web page) |
| accuracy Z | 1° after the magnetometer's calibration, which the servo's motor disturbs; Z drift without it is not stated |
| a run's angle | carried in AS5600 counts: 360 / 4096 = 0.088° a count |
| the HC-05's buffer on firmware 3.0 | 230 bytes, about 105ms at 200Hz (not measured) |
| a link drop | whether the sensor buffers or discards its frames is not stated; the HC-05 reconnects for 30min after a range loss |
| the reading's age | stale after 300ms |

No HC-05 and no BWT901CL has been run with this code. The 2.0 and 3.0
firmware answers come from the datasheet and from reports.
