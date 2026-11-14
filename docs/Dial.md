# The M5Stack Dial

<sub>**English** · [Deutsch](Dial-de.md)</sub>

The M5Stack Dial v1.1 is a round knob with a 240 x 240 pixel display. In
rcbench it is a wireless knob for the panel over Bluetooth LE (Low Energy,
BLE). Its display shows what the knob acts on. This page covers its
firmware (`firmware/dial`) and the protocol between it and the panel
(`protocols/dialknob`).

What exists: the protocol module with its host tests, and the Dial's
firmware: the power latch, the encoder, the buttons, the touch glass, the
click per detent, the faces on the display, the BLE peripheral with its
bound panel, and the USB console. What does not exist yet: the panel's
side. No panel build connects to a Dial, so a Dial moves nothing on the
bench. The BLE update and pairing are later slices.

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
  packet is DEGRADED, a notice. A connection that has sent no EVENT yet,
  while the panel discovers the service, is no loss.
- The loss is the same in both directions. When the panel's packets stop,
  the Dial shows LINK LOST after 500ms, and after 500ms more it ends the
  link and sends nothing until it is connected again. The panel then sees
  the disconnect, and an ARMED bench goes SAFE.
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
792,400 bytes, 25.2% of a slot; the seven fonts of the faces are 243,194
bytes of it. Its static RAM: 238,950 of 341,760 bytes of internal DRAM and
IRAM, the frame of 115,200 bytes and the ring tables of 26,145 bytes among
them; the heap that NimBLE takes at run time is not measured.

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

## The faces

The display shows what the knob acts on. `shared/dial` draws each face from
the panel's packets (SCREEN, NAV, VALUE_META, VALUE, LIVE_MOTOR, LIVE_SERVO,
STATE and SCALE) and from the Dial's own state: bound or not, the link, its
RSSI and its supply. It draws with the panel's rasteriser (`shared/gfx`),
the panel's fonts and the dark palette of `shared/ui/ui_theme.c`, into one
RGB565 frame of 240 x 240 pixels. The images on this page are that code's
render on a computer, from the fixed models in `test/host/dial_models.h`;
`tools/render_dial.py --check` holds them to the current render in CI.

Every face has the same furniture:

| Element | Where | What |
| --- | --- | --- |
| title tag | middle row 42, 20px tall | the panel screen's name in the 8x16 face on the accent; dimmed while the link is lost; a title wider than radius 101 allows is cut |
| state pill | middle row 66, 19px tall | `SAFE`, or `ARMED` with the run clock (`0:42`, from 1h `1h05`) on MOTOR & ESC; in the 10x20 face, in the 8x16 face when the icons beside it would leave radius 101 |
| link bars | 6px left of the pill | 4 bars, 3px wide: 4 at -60dBm or more, 3 at -70dBm, 2 at -80dBm, 1 below; amber while the panel marks the link DEGRADED; none while not connected |
| battery | 6px right of the pill | 18 x 9px, filled from 3,300 to 4,200mV; a dash while the reading is unknown; `USB` in its place while the reading is unknown and a USB host reads the port, where the row has room |
| press hints | middle rows 181 and 195 | what a press and a hold do here, in the 8x16 face |

A line is drawn in the largest font of its chain in which its ink lies
inside radius 100 of the display's center at its height, wrapped onto the
lines its block allows. `test_dial_fit` holds every string of every face,
in English and in German and with every field at its ends, inside radius
100, inside its cell, and off every other string.

### The readings: MOTOR & ESC and SERVO

![MOTOR & ESC, ARMED](img/dial-motor-armed.png)
![MOTOR & ESC, SAFE](img/dial-motor-safe.png)
![SERVO, ARMED](img/dial-servo-live.png)

Six readings in three rows of two equal cells of 78 x 31px, the columns at
x 76 and 164, the rows at y 100, 133 and 166, every cell inside radius 100.
A cell holds the reading's name in the 8x16 face, dimmed, and below it the
value in the 10x20 face with its unit in the 8x16 face, both in the
reading's colour. A reading without its valid bit shows `--`. While the
bench is SAFE the values are drawn at half strength and the first hint line
reads `ARM ON THE PANEL` in amber.

| Face | Readings, in order | Hints while ARMED |
| --- | --- | --- |
| MOTOR & ESC | throttle, speed, voltage, current, power, ESC temperature | `PRESS: 0%`, `HOLD: DISARM` |
| SERVO | commanded angle, measured angle, pulse, the rail's voltage, current and power | `PRESS: CENTER`, `HOLD: DISARM` |

Around the cells runs one ring per reading, in the grid's order, the first
outermost: radius 118 inward at a pitch of 3px to 103, 3px outside the
cells' circle. A ring is 1px wide and antialiased: the pixel whose center
lies at distance d from the display's center takes the ring's colour with
coverage clamp(1 - |d - r|, 0, 1). The arc runs from 12 o'clock clockwise
over the reading's share of its full scale; the rest of the circle is the
track, the reading's colour mixed 22% onto the background. The full scales
come from the panel's SCALE packet:

| Reading | Colour | Full scale |
| --- | --- | --- |
| throttle | ACCENT | 100% |
| speed | RPM | SCALE `rpm`: the run's peak, else the plot's full scale |
| voltage | VOLT | SCALE `mv` |
| current | CURR | SCALE `ca`, by magnitude |
| power | POWER | SCALE `mv` x `ca` |
| ESC temperature | LILAC | 120°C |
| commanded angle | ACCENT | -`travel` to +`travel`: the center is half a ring |
| measured angle | TEAL | -`travel` to +`travel` |
| pulse | LILAC | `us_min` to `us_max` |
| rail voltage, current, power | VOLT, CURR, POWER | SCALE `rail_mv`, `rail_ca`, their product |

A full scale of 0, or no SCALE since the connect, leaves its ring at the
track; so does a reading without its valid bit.

### A list and a value

![A list on SETUP](img/dial-navigation.png)
![A value being set](img/dial-value-set.png)

A list (NAV) shows the row before the focused one, the focused row in the
20x36 face between the accent's corner brackets, the row after it, and
`4 / 9`. Up to 24 rows show as ticks on the rim over 270°, the focused one
in the accent; a longer list shows its place as a ring at radius 112.
Hints: `PRESS: OPEN`, `HOLD: BACK`.

A value (VALUE_META, VALUE) shows the number in the 32x44 numeral face with
its unit, `0 TO 60  STEP 1`, the value on entry (`WAS 5`) and one ring at
radius 118 from the minimum to the maximum. The number and the range turn
amber when the panel refused the last turn at a limit. Hints in SET mode:
`PRESS: KEEP`, `HOLD: CANCEL`; before it: `PRESS: SET`, `HOLD: BACK`.

### The link and the bind

![LINK LOST](img/dial-link-lost.png)
![Waiting for the panel](img/dial-waiting.png)
![Not bound](img/dial-bind.png)
![A screen the knob does nothing on](img/dial-other.png)

500ms without a packet from the panel, after one came on this connection,
the face shows `LINK LOST`, `THE BENCH IS SAFE`, `ALL OUTPUTS OFF` and
`ARM ON THE PANEL` inside a dashed red rim, the title dimmed. The Dial ends
the link 500ms later, and the bench is SAFE then, either because it was
SAFE already or because the panel switches it SAFE at the loss. Between the
two, for up to 500ms, a turn still reaches a panel whose own stream
stalled. Connected with no packet from the panel yet, or bound and not
connected, it shows `WAITING FOR THE PANEL` and `THE TURN MOVES NOTHING`,
with the Dial's name on the dimmed title tag. Without a bound panel it
shows `NOT BOUND`, its name and address, where to bind it on the panel
(`ON THE PANEL`, `SETUP > WIRELESS`, `SCAN, TAP THE DIAL`) and
`ADVERTISING`. On a screen the knob does nothing on, and on HOME without
list rows, it shows the title, SAFE or ARMED and
`THE TURN MOVES NOTHING HERE`.

The faces' own words follow the panel's language, English or German, from
the SCREEN packet; before the first SCREEN the last language the panel
gave, English after a start. They are the `UI_DIAL_TEXT` entries of
`shared/text/include/ui_text.def`.

### What a change costs

The face task draws every 50ms what changed since the frame before: a
cell, the pill's row, the list or value body, the hints, and of a ring only
the pixels between its old and its new end. Every repaint clears only
inside radius 102, so it never touches a ring, a tick or the rim; the
rings and the ticks are painted onto the background colour, so a repaint
equals the first paint. `test_dial_face` holds a draw of what changed to a
full draw of the same model, pixel for pixel, over sequences of every face.
A damage record collects the rectangles written, at most 8, and only those
go over SPI. The whole frame goes at a face change and every 10,000ms.

| Change | Pixels sent | At 40MHz (arithmetic, not measured) |
| --- | ---: | ---: |
| speed by 300rpm: its cell and its ring's arc | 3,920 | 1.6ms |
| throttle by 1% | 3,968 | 1.6ms |
| the run clock by 1s: the pill's row | 5,696 | 2.3ms |
| five readings at once | 19,040 | 7.6ms |
| a list's focus by one row | 25,072 | 10.0ms |
| the whole frame | 57,600 | 23.0ms |

The ring tables hold every pixel a ring touches, sorted by angle: 8,344
pixels for the six rings, 3 bytes each, in 26,145 bytes of static RAM,
built at start. A full repaint of one ring blends about 1,490 pixels; a
move by 1% of a turn at radius 118 about 15. Neither is measured on the
device.

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
  counts the encoder rests. Clockwise positive is the protocol's rule. A
  detent moves 3 counts from its center, so an encoder that rests between
  two counts never clicks.
- The touch controller's start-up time after the shared reset is taken as
  300ms, the FT5x06 family's; not measured on this part.
- Whether GPIO46 matters while USB powers the Dial is not confirmed.
- A panic or a software reset on battery probably releases GPIO46 for the
  reset and switches the Dial off; not confirmed.
- `forget`, or the 10s hold, with the bound panel in range: the panel still
  holds the Dial's address, connects again and subscribes, and the Dial
  binds it again. To bind another panel, unbind the Dial on the old one
  first.
- Characters outside ASCII, the degree sign and the German letters draw as
  `?`.
- The flush of a whole frame takes about 23ms at 40MHz (arithmetic, not
  measured). How long the ring tables take to build at start, and a ring's
  repaint, are not measured on the device.
- SCALE is the panel's to send, a later slice. Without it every ring but
  the throttle's and the ESC temperature's shows its track only.
- The value face shows no category: no packet carries one.
- Nothing in this firmware has run on the device: the Dial is ordered, not
  delivered. The station measures the turn-to-output latency with the test
  build once the Dial is on its USB.
- No sleep, no backlight steps and no auto-off yet: the Dial stays on until
  RST is pressed or the cell is empty. Runtime on a cell is not measured.
