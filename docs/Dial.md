# The M5Stack Dial

<sub>**English** · [Deutsch](Dial-de.md)</sub>

The M5Stack Dial v1.1 is a round knob with a 240 x 240 pixel display. In
rcbench it is a wireless knob for the panel over Bluetooth LE (Low Energy,
BLE). Its display shows what the knob acts on. This page covers its
firmware (`firmware/dial`) and the protocol between it and the panel
(`protocols/dialknob`).

What exists: the protocol module with its host tests, and the Dial's
firmware: the power latch, the encoder, the buttons, the touch glass, the
click per detent, the display with a placeholder face, the BLE peripheral
with its bound panel, and the USB console. What does not exist yet: the
panel's side. No panel build connects to a Dial, so a Dial moves nothing on
the bench. The final faces, the BLE update and pairing are later slices.

M5Stack, Dial and Stamp-S3A are names of their owner and its products, and
Bluetooth is a trademark of its owner. rcbench is independent: it is not
affiliated with, endorsed by or sponsored by them. The names say which
device the panel talks to.
[Legal notice and disclaimer](../README.md#legal-notice-and-disclaimer) holds
the project's notice.

## The device

| Part | What it is | Pins |
| --- | --- | --- |
| Module | the Dial's Stamp-S3A: ESP32-S3FN8, 8MB flash in the package, no PSRAM (pseudo-static RAM) | |
| Display | GC9A01, 1.28 inch, 240 x 240, SPI (serial peripheral interface) at 40MHz | DC GPIO4, MOSI GPIO5, SCLK GPIO6, CS GPIO7, reset GPIO8, backlight GPIO9 |
| Touch | FT3267 at I2C address 0x38, 400kHz, polled every 20ms | SDA GPIO11, SCL GPIO12, interrupt GPIO14 (not used) |
| Encoder | 16 detents a turn, 4 quadrature counts a detent, on PCNT (the pulse counter) | A GPIO41, B GPIO40 |
| Screen button | under the glass, polled every 5ms, debounced 20ms | GPIO42 |
| Stamp button | the module's button; held at reset, ROM download mode | GPIO0 |
| Buzzer | a 3ms click at 4,000Hz per detent | GPIO3 |
| Power latch | high keeps the Dial on after WAKE is let go | GPIO46 |
| Test mark | 1ms high per injected detent, test build only | GPIO1 (PORT.B G1) |
| Battery | a single LiPo cell of 1,000mAh or less on the MX1.25 socket, charged on the board | the reading pin is unknown |

The cell is held to 1,000mAh or less until the charge current is measured:
the charger (TP4057 in the schematic text) and its current are not
confirmed.

## Safety

The Dial is not a safety device. These rules are the panel's, and the
panel's side that applies them is a later slice:

- A Dial event never enables voltage or power. ARM, SUPPLY ON, PROG 5V and
  WRITE stay the panel's 2s holds. STOP is on the panel only.
- The link is relative: each EVENT carries the Dial's detent counter, never
  a value. The first packet after a connect, a packet more than 300ms after
  the one before and a jump of more than 16 detents move nothing.
- A Dial lost while ARMED is an error: 1,000ms without a packet, or the
  disconnect, switches the bench SAFE and every output off. 500ms without a
  packet is DEGRADED, a notice.
- The screen button while ARMED: a tap sets the throttle to 0% on MOTOR and
  CENTER on SERVO; a 1s hold DISARMs.

The link rules and their timing are in the protocol module, with a host test
each ([protocols/dialknob](https://github.com/subtilitas/rcbench/blob/main/protocols/dialknob/README.md)).

## Flashing over USB

The release carries `rcbench-dial-merged-<version>.bin` for offset 0x0 and
`rcbench-dial-<version>.bin`, the application alone, with their SHA-256
sums in `SHA256SUMS`.

1. Check the image: `sha256sum -c SHA256SUMS --ignore-missing` in the
   folder with the image and `SHA256SUMS`.
2. Connect the Dial's USB-C socket to the computer. A Dial that runs this
   firmware is reset into the ROM download mode by esptool over the USB
   Serial/JTAG port. A Dial that does not: hold the Stamp button (GPIO0)
   while plugging in the cable.
3. Flash:

   ```bash
   python -m esptool --chip esp32s3 -p /dev/ttyACM0 -b 460800 \
     write-flash 0x0 rcbench-dial-merged-0.23.12.bin
   ```

   esptool 5 spells the command `write-flash`; esptool 4 `write_flash`.
4. The Dial restarts and shows `NOT BOUND` and its name, `DIAL-` and the last
   4 hex digits of its address.

The merged image writes the partition table, so the NVS (non-volatile
storage) at 0x9000 is written anew and the bound panel is forgotten.

From the source, with ESP-IDF (Espressif Internet-of-Things Development
Framework) v6.1:

```bash
cd firmware/dial
idf.py -B build -D SDKCONFIG=build/sdkconfig build
idf.py -B build -p /dev/ttyACM0 flash monitor
# the test build, with the console's hooks
idf.py -B build-hooks -D SDKCONFIG=build-hooks/sdkconfig -D RCBENCH_DIAL_HOOKS=ON build
```

The partition table (`firmware/dial/partitions.csv`) holds NVS, two
application slots of 3,072kB and their otadata. The released image is
600,960 bytes, 19.1% of a slot. Its static RAM: 211,478 of 341,760 bytes of
internal DRAM and IRAM, the frame of 115,200 bytes among them; the heap that
NimBLE takes at run time is not measured.

## Bind

The Dial advertises the dialknob service UUID (universally unique
identifier) with its name in the scan response. Unbound, it advertises
every 100ms and any central may connect. The first central that subscribes
to EVENT is its bound panel: the Dial keeps the panel's address in NVS and
from then on takes a connection from that address only, through the BLE
controller's accept list, and advertises every 500ms. A scan still lists a
bound Dial.

A 10s hold of the screen button forgets the bound panel; so does `forget` on
the console. The Dial beeps, ends a link, and advertises to anyone again.
The 1s HOLD gesture goes to the panel on the way.

There is no pairing and no encryption in protocol version 1: a device in
range that connects first, or that spoofs the bound address, is taken.

## The face

The face in this firmware is a placeholder: a plain ring, the panel
screen's title and the value the knob acts on as text (a setting's value
with its unit, the throttle in %, the servo's pulse in us, the focused list
row), and SAFE or ARMED. Without a bound panel it shows `NOT BOUND`, its
name and `BIND ON THE PANEL`. 500ms without a packet from the panel it shows
`LINK LOST` and `THE TURN MOVES NOTHING`. Its own words follow the panel's
language, English or German.

## The console

The console is the USB Serial/JTAG port of the USB-C socket, with the
prompt `dial>` and the log on the same port.

| Command | What it does |
| --- | --- |
| `status` | name, address, firmware and protocol version, the bound panel, the link and its RSSI (received signal strength indication), the EVENT counts, the click setting, the battery reading |
| `sound on` / `sound off` | the click per detent; the panel's SCREEN packet sets it as well, default ON |
| `forget` | forget the bound panel, as the 10s hold |
| `help` | the commands |

The test build (`RCBENCH_DIAL_HOOKS=ON`) adds the hooks. Each injects as if
the hardware did it. The released image holds none of their code; CI fails
when it does.

| Command | What it does |
| --- | --- |
| `turn <n>` | n detents, -1,000 to 1,000, one every 20ms, each in its own EVENT; GPIO1 (PORT.B G1) goes high for 1ms when that EVENT is handed to the BLE stack. With no link no EVENT goes and GPIO1 stays low |
| `press` | a PRESS gesture |
| `hold` | a HOLD gesture |
| `tap <x> <y>` | a TAP on the glass at x, y, 0 to 239 |
| `batt <mV>` | the battery reading STATUS carries, 2,000 to 5,000mV; 0 takes it back to unknown |
| `drop` | end the link; the Dial advertises again |
| `ping` | an EVENT at once, with the echo of the last STATE's clock |

PORT.B's two pins are named in a different order by the Dial's
documentation and by M5Unified; GPIO1 is the pin by number either way.

## Limits

- No panel build connects to a Dial yet: the panel's side is a later slice.
- The battery's voltage is unknown: no source names the pin it reaches.
  STATUS carries 0 and its "battery unknown" flag. The charger's state is
  unknown as well.
- Which way of the encoder counts up is not measured, nor where in its 4
  counts the encoder rests. Clockwise positive is the protocol's rule.
- The touch controller's start-up time after the shared reset is taken as
  300ms, the FT5x06 family's; not measured on this part.
- Whether GPIO46 matters while USB powers the Dial is not confirmed.
- The face is a placeholder; characters outside ASCII and the German
  letters draw as `?`.
- The flush of a whole frame takes about 23ms at 40MHz (arithmetic, not
  measured).
- Nothing in this firmware has run on the device: the Dial is ordered, not
  delivered. The station measures the turn-to-output latency with the test
  build once the Dial is on its USB.
- No sleep, no backlight steps and no auto-off yet: the Dial stays on until
  RST is pressed or the cell is empty. Runtime on a cell is not measured.
