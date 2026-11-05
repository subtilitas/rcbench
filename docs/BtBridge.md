# The Bluetooth bridge

<sub>**English** · [Deutsch](BtBridge-de.md)</sub>

The Bluetooth bridge is a classic ESP32 board in the HC-05's place between
the panel and the WitMotion BWT901CL angle sensor
([The Bluetooth IMU](IMU.md)). It answers the panel's HC-05 AT commands as
an HC-05 in the SPP (serial port profile) master role does, connects to
the sensor over Bluetooth Classic and passes the bytes between the
sensor's link and the panel's UART1 (universal asynchronous
receiver-transmitter). The panel's firmware is the same for both: it
cannot tell the bridge from an HC-05.

The bridge exists because an HC-05 must be a genuine one, with the CSR
BC417 chip and its flash chip, to act as a master. HC-06 modules, JDY-xx
modules and the Jieli-based clones sold as HC-05 are slaves only and
cannot connect to the sensor.

The ESP32 must be the original ESP32, such as the ESP32-WROOM-32 module:
it is the only ESP32 with Bluetooth Classic. The later families, S2, S3,
C3, C6, H2 and P4, have none. The pins below are
those of a D1 mini ESP32 board (ESP32-WROOM-32, 4MB flash).

| Part | Where |
| --- | --- |
| the AT commands, the modes and the link's tries | `protocols/hc05emu`, no pin driver ([README](../protocols/hc05emu/README.md)) |
| the UART, KEY, STATE, the LED and the task | `firmware/btbridge/main/main.c` |
| Bluedroid: inquiry, name reads, pairing, the SPP link | `firmware/btbridge/main/bridge_bt.c` |
| the settings in NVS (non-volatile storage) | `firmware/btbridge/main/bridge_store.c` |

## Wiring

The bridge is wired like the HC-05 ([The Bluetooth IMU: Wiring](IMU.md#wiring)):
5V and GND from header H9, the signals to header P3.

| D1 mini ESP32 | Signal | Panel |
| --- | --- | --- |
| 5V (VCC) | supply, to the board's 3.3V regulator | Core_5V of header H9 (RS485), pin 1 |
| GND | ground | GND of header H9 (RS485), pin 2 |
| GPIO 17 (D3) | UART2 TX, 3.3V | IO5 of header P3, UART1 RX (`PANEL_IMU_PIN_RX`) |
| GPIO 16 (D4) | UART2 RX | IO4 of header P3, UART1 TX, 3.3V (`PANEL_IMU_PIN_TX`) |
| GPIO 18 (D5) | KEY input, internal pull-down: high for AT mode | IO29 of header P3 (`PANEL_IMU_PIN_KEY`) |
| GPIO 19 (D6) | STATE output: high with the link open | not wired (`PANEL_IMU_PIN_STATE` is `GPIO_NUM_NC`) |
| GPIO 2 | the board's LED | |

Both sides run at 3.3V, so no level shifter sits between them. The ESP32's
pins are not 5V tolerant. GPIO 16 to GPIO 19 are no strapping pins; GPIO 2
is one, and the board itself wires its LED to it. The pins, the LED and
its polarity are options of `firmware/btbridge/main/Kconfig.projbuild`
(`idf.py menuconfig`, "rcbench Bluetooth bridge"). An ESP32-WROVER module
uses GPIO 16 and GPIO 17 for its PSRAM (pseudo-static random-access
memory); there, UART2 needs two other pins.

The current the board draws from H9's Core_5V is not measured. Whether a
board feeds its USB 5V and its 5V pin from one net without a diode is not
checked here; unplug H9's 5V while the board's USB is connected.

## Flashing

The release asset `rcbench-btbridge-vX.Y.Z.bin` is the merged image:
bootloader, partition table and application, written at offset 0x0.
`esptool` comes with ESP-IDF v6.1, or `pip install esptool`.
`/dev/ttyUSB0` is `COMx` on Windows.

```bash
esptool --chip esp32 --port /dev/ttyUSB0 write-flash 0x0 rcbench-btbridge-vX.Y.Z.bin
```

From the source, with ESP-IDF v6.1 (`. $IDF_PATH/export.sh`):

```bash
idf.py -C firmware/btbridge build
idf.py -C firmware/btbridge -p /dev/ttyUSB0 flash monitor
```

`sdkconfig.defaults` sets the target to `esp32`. When the Xtensa toolchain
of ESP-IDF v6.1 is missing, CMake finds no compiler; it installs with:

```bash
$IDF_PATH/tools/idf_tools.py install --targets esp32
```

The console on the board's USB port runs at 115200 baud. It prints the
version, whether a stored setting was read, KEY's level at the start, each
change of mode and link, and each radio procedure.

## Use with the panel

Nothing changes on the panel. With IMU ON, the panel's probe finds the
bridge at 115200 baud, and the IMU page shows `HC-05: btbridge-X.Y.Z`.
`SCAN FOR SENSORS` lists the sensors in range, and a tap on one binds it:
the bridge pairs with PIN 1234, stores the address and restarts in data
mode, where it connects to the sensor. The panel offers
`CONNECT TO ANY SENSOR` only after a scan answered ERROR:(1F), which the
bridge never answers.

The bridge keeps the settings an HC-05 keeps in its flash: role, CMODE
(connection mode), the bound address, the PIN, the UART and the inquiry
mode. `AT+ORGL` restores the settings of a fresh bridge and forgets the
address; `esptool --chip esp32 erase-flash` erases them with the image.

A fresh bridge is a master, CMODE 0 without an address, PIN 1234, 115200
baud 8N1 (8 data bits, no parity, 1 stop bit). In data mode it connects to
the bound address, tries again 1000ms after a failed connect or a lost
link, and stops a connect that has not ended after 15000ms. KEY raised
over an open link keeps the link and holds its bytes back until KEY falls.
`AT+RESET` restarts the bridge 300ms after its OK, in the mode KEY selects
then. The README of `protocols/hc05emu` has every command, answer, error
and timeout.

## LED

| LED | State |
| --- | --- |
| 1000ms on, 1000ms off | AT mode: KEY high |
| 100ms on, 100ms off | data mode, no link |
| on | data mode, the link to the sensor open |
| off | the 300ms restart after `AT+RESET` |

STATE (GPIO 19) is high while the link is open, in either mode, as an
HC-05's STATE pin is.

## Limits

| Item | Value |
| --- | --- |
| roles | master only; `AT+ROLE=0` is stored and answered, and the bridge then connects to nothing |
| pairing | legacy PIN pairing, 1 to 16 bytes; no SSP (secure simple pairing) |
| links | one |
| the image | 651,232 bytes in a 1,536,000-byte application partition, 716,768 bytes merged (0.23.8) |
| the congested link | 512 bytes held from the UART, more dropped |
| commands outside the HC-05's set it uses | `ERROR:(0)` ([README](../protocols/hc05emu/README.md#not-covered)) |

## Not measured

The current from H9's Core_5V; the time from power-up to the first AT
answer; the time to connect to a BWT901CL; the range; whether 200Hz frames
pass without loss and the latency the bridge adds. No bridge has been run
with a panel or a sensor.

WitMotion, BWT901CL, HC-05, HC-06, JDY, Jieli, CSR, BC417, ESP32 and D1
mini are names of their makers' products, and Bluetooth is a trademark of
its owner. rcbench is independent: it is not affiliated with, endorsed by or sponsored by them.
The names say which devices the bridge stands in for and connects to.
[Legal notice and disclaimer](../README.md#legal-notice-and-disclaimer)
holds the project's notice.
