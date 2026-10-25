# First run on hardware

<sub>**English** · [Deutsch](FirstRun-de.md)</sub>

Both boards powered with the heartbeat wire fitted, and every number below
read on an instrument. Every step says what "good" looks like and what to write down when it is not.

Work down the list. Each step assumes the one above it passed.

---

## 0. Before power

> ### The panel's console and its USB-C sockets
>
> The panel's console is UART0, with USB-Serial-JTAG (the ESP32-P4's
> built-in USB serial and debug bridge) as the secondary console. The
> 7-inch ESP32-P4 board has two USB-C sockets. Which of them reaches UART0
> and which USB-Serial-JTAG is not traced
> ([Bring-up](Bringup.md#panel)). `esptool.py` flashes the board over
> either serial port it shows.

**Have to hand:** an oscilloscope, one servo, one ESC that speaks
bidirectional DShot, a bench supply with current limit, and the USB cable for
each board.

**Do not connect a motor to the ESC yet.** Step 5 drives no pin at all -- it
is the interlock, and no output is the passing result. Step 6 is the first pin
on an instrument, and the first thing to look at there is the scope, not a
propeller.

**Current limit:** set it low enough that a shorted output trips it rather
than burning a track.

---

## 1. Flash both boards

The panel is the 7-inch ESP32-P4 display board from Waveshare
(ESP32-P4-WIFI6-Touch-LCD-7B). A new board holds no rcbench image, so the
panel's first load is over USB (Universal Serial Bus),
[step 3](#from-the-release-assets). Releases 0.22.1 and older run on the
ESP32-S3 panel and not on this board ([README](../README.md#display-change-esp32-p4-panel-from-0230)). The IOmcu
is the same board on both panels. What it loads, and how, depends on the
release it runs:

| IOmcu runs | IOmcu loads |
| --- | --- |
| nothing (a new module), or 0.18.0 and older | over USB, [step 2](#from-the-release-assets) |
| 0.19.0 or later | over CAN (Controller Area Network) from the panel, [The IOmcu over CAN](#the-iomcu-over-can) |

Once the panel runs, it installs a later release over Wi-Fi ([Screens:
Wi-Fi](Screens.md#wi-fi)) or downloads it from GitHub ([Screens:
Updates](Screens.md#updates)). Both take the release asset
`rcbench-panel-p4-vX.Y.Z.bin`; the update check looks for that name in the
release's `SHA256SUMS`.

The SD (Secure Digital) card holds a FAT32, FAT16, FAT12 or exFAT
(extended FAT) volume, so a card of up to 2 TB mounts in the format it is
sold in. The panel does not read NTFS (New Technology File System), a card
with a GPT (GUID partition table) or a volume over 2 TiB, and it formats no
card ([Screens: The SD card's format](Screens.md#the-sd-cards-format)). A
card from a bench of 0.21.0 or older holds files in folders the panel does
not read ([below](#the-sd-card-of-0210-and-older)).

### From the release assets

| Asset | Holds | For |
| --- | --- | --- |
| `rcbench-iomcu-vX.Y.Z.zip` | `rcbench-iomcu-install.uf2`, `rcbench-iomcu.uf2`, `rcbench-iomcu-pt.uf2`, `rcbench-iomcu.bin`, `rcbench-iomcu.elf` | the IOmcu's USB load: `rcbench-iomcu-install.uf2` |
| `rcbench-panel-p4-vX.Y.Z.zip` | `flash_args`, `bootloader/bootloader.bin`, `partition_table/partition-table.bin`, `ota_data_initial.bin`, `rcbench-panel.bin`, `rcbench-panel-merged.bin`, `rcbench-panel.elf`, `NOTICE`, `LICENSE` | the panel's USB flash |
| `rcbench-panel-p4-vX.Y.Z.bin` | the panel's application alone, with the IOmcu image it offers over CAN | an update over Wi-Fi; not for USB |
| `SHA256SUMS` | the SHA-256 (Secure Hash Algorithm) of the three files above | the check in step 1 |

Tools: the RP2350's `RP2350` drive or `picotool` for the IOmcu,
`esptool.py` for the panel (`pip install esptool`; ESP-IDF carries it).
`/dev/ttyACM0` below is `COMx` on Windows.

1. In the download folder, `sha256sum -c SHA256SUMS` prints `OK` for each
   of the three files.
2. **IOmcu.** Unpack `rcbench-iomcu-vX.Y.Z.zip`. Hold BOOTSEL while
   the IOmcu's USB socket is connected, so the module shows its drive
   `RP2350`. Copy `rcbench-iomcu-install.uf2` onto the drive, or:

   ```bash
   picotool load -v -x rcbench-iomcu-install.uf2
   ```

   The module restarts into slot A. Its console prints `rcbench-iomcu:
   running from slot A` every 3s ([below](#the-iomcus-partition-table)).
   An IOmcu on 0.19.0 or later skips this step.
3. **Panel.** Unpack `rcbench-panel-p4-vX.Y.Z.zip` and connect a USB-C
   socket of the panel ([§0](#0-before-power)). In the unpacked folder, on
   a new board, or to start from the default settings:

   ```bash
   esptool.py --chip esp32p4 -p /dev/ttyACM0 write_flash 0x0 rcbench-panel-merged.bin
   ```

   On a panel whose settings stay:

   ```bash
   esptool.py --chip esp32p4 -p /dev/ttyACM0 write_flash @flash_args
   ```

   A browser flasher that writes one file at an offset, such as the ESP Web
   Tool, takes `rcbench-panel-merged.bin` at 0x0, with the same effect as
   the first command.
4. Power both boards with the link wired. An IOmcu loaded in step 2 runs
   the build the panel carries, and the panel offers no update. The panel
   fetches the board drawing ([§4](#4-first-link-up-the-board-drawing)).

| What | Merged image at 0x0 | Parts (`@flash_args`) |
| --- | --- | --- |
| Panel settings and Wi-Fi settings, in NVS (non-volatile storage, 24KiB at 0xD000) | erased; the panel starts on its defaults | kept |
| Panel's stored board drawings, `boardart` at 0x7E0000 | kept: the image ends with the application in `ota_0` | kept |

The IOmcu keeps its bindings, the supply's wiring and the current
monitors' set-up, with the exception for an RP2350 of stepping A2 stated
[below](#the-iomcus-partition-table).

The panel can go first. Linked to an IOmcu without the partition table,
the panel shows `IOmcu cannot update over CAN. Install firmware once via
USB.` until step 2 is done; the link runs meanwhile.

### The IOmcu over CAN

An IOmcu on 0.19.0 or later carries the partition table from its USB load
and takes the image the panel carries over CAN. The panel's link protocol
is 4.29, and an IOmcu of 0.19.0 or later speaks 4.22 or later: the major,
4, is the same, so the two boards link before and after the update.

1. Power both boards with the link wired. At link-up the panel compares
   the IOmcu's running image with the image it carries by SHA-256, finds
   another build, and the band says `IOmcu runs a different build. Update
   it under SETUP > UPDATES.`.
2. The question opens on the menu while the bench is SAFE, the supply's
   output is off and the panel's Wi-Fi radio is off. It names the version
   on the IOmcu and the version in the panel. Hold START for 2s, or after
   CANCEL `HOLD 2s TO UPDATE THE IOMCU` on SETUP > UPDATES. The progress fills
   the screen ([Screens: IOmcu update](Screens.md#iomcu-update)), and back
   on the menu the band says `IOmcu updated to X.Y.Z.`.

The update writes the slot the IOmcu does not run from. Its bindings, the
supply's wiring and the current monitors' set-up stay: they are in the
store partition at 0x3FC000, or before its first save in the old store at
0x3FE000, and the update writes neither. The store record becomes version
8 at the first save; an IOmcu image older than 0.22.0 reads no version 8
record and starts as from an empty store. CANCEL on the question leaves
the IOmcu on its build; the panel asks again after its next start. Power
lost during the update leaves the old image running, and the next link-up
offers the update again. An IOmcu that missed the USB load of 0.19.0 has
no partition table: the band says `IOmcu cannot update over CAN. Install
firmware once via USB.`, and it takes [step 2](#from-the-release-assets).
The time of the update over CAN is not measured.

### The SD card of 0.21.0 and older

The panel keeps its files on the SD (Secure Digital) card in folders under
`BENCH` and `USER` ([Logs](Screens.md#logs)). It moves nothing a card of
0.21.0 or older holds, and it does not read these:

| 0.21.0 and older | Read from | On a computer |
| --- | --- | --- |
| `/ESC/*.json`, ESC profiles | `/BENCH/ESC/CONF/` | move the files there |
| `/KST/*.TXT`, servo files, the record `KST.TXT` and its new text `KST.NEW` | `/BENCH/KST/CONF/` | move the files there, `KST.TXT` and `KST.NEW` with them |
| `/RCBENCH.LOG`, fault log | not read; new lines go to `/BENCH/RCBENCH.LOG` | keep or delete |
| run logs in the root | listed on the ROOT tab of LOGS | nothing to do |

A record `KST.TXT` or `KST.NEW` left in `/KST` is not seen at start-up:
the restore it offers for an interrupted servo write needs the file moved
first.

### From a source tree

```bash
# IOmcu — produces rcbench-iomcu-install.uf2 and rcbench-iomcu.uf2
cmake -S firmware/iomcu -B firmware/iomcu/build
cmake --build firmware/iomcu/build

# panel
idf.py -C firmware/panel set-target esp32p4
idf.py -C firmware/panel build
idf.py -C firmware/panel -p /dev/ttyACM0 flash monitor
```

The IOmcu build prints its own size check:

    -- rcbench: image 191808 bytes, 9% of the 2084864 bytes of one slot

**Watch for:** the module used for bring-up is a Waveshare RP2350-CAN with
**4MB**, while `PICO_BOARD` defaults to a board file claiming **16MB**. The
linker measures against 16MB and will not warn. The line above is the only
thing that will, so read it.

### The IOmcu's partition table

The IOmcu's flash carries a partition table: two image slots of
2036KiB each and the output store's own 8KiB partition
([Link](Link.md#firmware-update-over-the-link) has the layout). The panel
sends later IOmcu images over CAN into the slot that is not running.
A new module and an IOmcu loaded with 0.18.0 or older (an image of
protocol 4.21 or older) have no table and take the table and their first
image over USB, once.

With the IOmcu's USB socket connected and BOOTSEL held at power-up, so
the module shows its drive `RP2350`:

```bash
# drag and drop the file onto the drive, or:
picotool load -v -x firmware/iomcu/build/rcbench-iomcu-install.uf2

# check: partitions 0 (A), 1 (B) and 2 (store, data)
picotool partition info
```

**Good:** the IOmcu's console prints `rcbench-iomcu: running from
slot A` every 3s. The bindings, the supply's wiring and the current
monitors' set-up are read from the old store at 0x3FE000 until the first
save writes the new one. `no partition table -- firmware updates over CAN
need one USB load of rcbench-iomcu-install.uf2` means the image runs without
the table: load `rcbench-iomcu-install.uf2` again.

On an RP2350 of stepping A2 the load also writes the RP2350-E10 workaround
block, which lands in the last 4KiB sector of the 4MB, 0x3FF000: the old
store's second sector. Bindings whose newest save sits there are lost, and
the newest save in 0x3FE000 comes back instead. Check OUTPUTS after the load.
The stepping of the bring-up module is not known.

With the table in place, `rcbench-iomcu.uf2` alone goes to the slot that is
not running. Which slot a power-up starts when both slots hold an image of
the same version is not stated in the RP2350 datasheet. The image that
starts erases the other slot's first sector, so the choice holds and the
other build is gone. To be certain which build runs after a development
load, erase the other slot first:

```bash
picotool erase -p 1
picotool load -v -x -p 0 firmware/iomcu/build/rcbench-iomcu.uf2
```

### The panel's application slots

The panel's flash, 32MB, holds two application slots of 8,126,464 bytes
(7,936KiB), `ota_0` and `ota_1`, so the panel updates itself over Wi-Fi
([Screens: Wi-Fi](Screens.md#wi-fi)). The table is
`firmware/panel/partitions.csv`:

| Offset | Part |
| --- | --- |
| 0x2000 | the second-stage bootloader, at most 40,960 bytes |
| 0xC000 | the partition table |
| 0xD000 | `nvs`, 24KiB: the settings |
| 0x13000 | `phy_init`, 4KiB |
| 0x14000 | `otadata`, 8KiB: which slot starts |
| 0x20000 | `ota_0`, 7,936KiB |
| 0x7E0000 | `boardart`, 128KiB: the stored board drawings |
| 0x800000 | `ota_1`, 7,936KiB |
| 0xFC0000 | not allocated, to the end of the flash |

The parts keep the settings: they write the bootloader at 0x2000, the
partition table at 0xC000, `otadata` at 0x14000 and the application at
0x20000, and leave `nvs` alone. From a source tree:

```bash
idf.py -C firmware/panel -p /dev/ttyACM0 flash
```

From the release archive, `esptool.py ... write_flash @flash_args` in its
unpacked folder, as in [step 3](#from-the-release-assets).

The merged image at offset 0 covers the flash from 0x0 to the end of the
application in `ota_0` and fills the gaps between the parts with 0xFF.
`nvs` at 0xD000 lies in such a gap, so the merged image erases the
settings. `boardart` at 0x7E0000 lies past its end, so the stored board
drawings stay.

The panel installs later releases over Wi-Fi from the release asset
`rcbench-panel-p4-vX.Y.Z.bin`.

---

## 2. The link, before anything else

The panel runs the CAN echo self-test itself, at every start-up, for 1200ms
inside the splash. It answers one question: do frames cross the bus intact? It
uses no page protocol, so if it passes and the link still does not work, the
fault is above the wire.

**Good looks like nothing:** the splash shows `LINK  OK  CAN 1Mbit/s` and
hands over to the menu.

**A failure is a screen**, before the menu, with the verdict, what to check in
order, and both ends' counters. It takes a two-second hold to leave.
[Bringing up the link](Bringup.md#the-bus-fault-screen) has the verdicts and
what each one means.

**Do not go on until it passes.** Every step below assumes frames cross.

The console carries the detail if you want it. It is UART0, with
USB-Serial-JTAG (the ESP32-P4's built-in USB serial and debug bridge) as
the secondary console; which of the board's two USB-C sockets reaches which
is not traced:

    I (…) rcbench: CAN self-test: every probe came back intact
    I (…) rcbench:   sent 2024 echoed 2024 corrupt 0 lost 0 stale 0

---

## 3. The heartbeat wire — the thing that has blocked everything

| | |
|---|---|
| Panel end | **GPIO28**, IO28 of the 12-pin header P3 of the 7-inch ESP32-P4 board (`PANEL_HEARTBEAT_PIN` in `board_pins.h`). |
| IOmcu end | **GPIO 3** |
| Through | the retriggerable monostable, once one exists. It is on no board, so the bring-up bench runs a direct wire and has no hardware backstop. The wire covers a panel that stops beating while the IOmcu is healthy: it switches to SAFE after 150ms of silence. What is uncovered is a panel that stops beating while the IOmcu cannot act -- nothing then removes the outputs, and that is what the monostable would do without any firmware |

Without this wire the IOmcu refuses every switch to ARMED, and that is the interlock
working, not a fault.

**Numbers it is judged against:**

| | |
|---|---|
| Edge period | 20ms |
| Gap accepted | 4ms to 150ms |
| Edges before it counts as alive | 4 |

So the line must be edging for roughly **80ms** before a switch to ARMED can succeed.

**On the scope, at GPIO 3:** a square wave, edge to edge 20ms. If the gap ever
exceeds 150ms the IOmcu drops it and the next switch to ARMED is refused.

**Where the edges come from:** the panel's control task, inside its poll loop.
Anything that stalls that task stops the heartbeat — which is why nothing
long is allowed to run there.

---

## 4. First link-up: the board drawing

The IOmcu carries a 1,560-byte drawing of its own board. The panel
fetches it at the first link-up and keeps it in flash.

On the panel console, in this order:

    I (…) rcbench: IOmcu answered
    I (…) rcbench: hardware 1 says where its pads are
    I (…) rcbench: hardware 1 says which pads are grounds and rails
    I (…) rcbench: fetching hardware 1's drawing: 1560 bytes, hash 8f99cd48
    I (…) rcbench: hardware 1's drawing kept

**What to expect while it runs:**

- 26 blocks of 62 bytes over CAN, in a 15ms slice of each 50ms poll.
- **Flash operations** when it writes: 4 for this drawing (2 sector erases
  of 4kB, the payload, the header). The DSI (Display Serial Interface) host
  scans the frame buffers out of PSRAM by DMA (direct memory access) with no
  bounce buffer, and its interrupt runs during a flash operation
  (`CONFIG_LCD_DSI_ISR_CACHE_SAFE`), so the picture is expected to stay
  through each. What the picture does during the write is not measured.
  Settings saves write the flash the same way.
- **The write waits for a safe bench.** It takes one step per 5ms control
  pass, and only while the bench is SAFE with the supply off. ARMED or with
  the supply on, `keeping hardware 1's drawing waits for a bench in SAFE
  with the supply off` appears once on the console and `kept` follows after
  the switch to SAFE.
- **It must not disturb the heartbeat.** The transfer is sliced and the flash
  write is held back while ARMED for that reason. If the heartbeat drops
  during the fetch, stop and write it down.

On the next link-up the panel reads the drawing's length, CRC (cyclic
redundancy check) and hash from the IOmcu and shows the kept copy
when all three match:

    I (…) rcbench: hardware 1's drawing is kept: 1560 bytes, hash 8f99cd48

An IOmcu flashed with another drawing reports another hash, and the
panel fetches the drawing again. An IOmcu on protocol 4.19 or older
serves no drawing; the panel logs `hardware 1 serves no board drawing
(protocol 4.19); PICK A PIN draws its shape` and draws the board from its
outline.

---

## 5. Arm with nothing connected

Nothing is wired to an output yet. This step tests the interlock, not a pin.

1. **Heartbeat wire removed** → switching to ARMED must be **refused**. The IOmcu
   answers `NOT_ARMED` because `!beat.alive`. The panel shows the refusal.
2. **Heartbeat wire fitted, panel ARMED** → the IOmcu accepts.
3. **Pull the heartbeat wire while ARMED** → it must fail safe within
   **150ms**.
4. **Press STOP** → latches. It must take an explicit ARM to leave, not a
   link that recovers.
5. **Pull the CAN wire while ARMED** → the IOmcu gives up after
   **200ms**; the panel escalates after **1s**.
6. **Cover the touch panel / let touch die** → after **500ms** of silence
   switching to ARMED is blocked.

Every one of these is host-tested. **None of the numbers below has been seen on an instrument:** no scope or analyser has read any pulse width, frame period or reply delay in this tree.

---

## 6. First pin on an instrument

Use a **servo**, not the ESC. A servo is the forgiving case and the one the
scope reads most easily.

**Pins free for an output:** GPIO 0, GPIO 1, GPIO 2, GPIO 4, GPIO 5, GPIO 6, GPIO 7, GPIO 13 to GPIO 22
and GPIO 26 to GPIO 28. GPIO 13 is the programming port's pin on an IOmcu that
has stored none and is refused for an output until the port's pin is moved
([Link](Link.md#the-programming-ports)).
**Reserved and refused:** GPIO 3 (heartbeat), GPIO 8–GPIO 12 (CAN), GPIO 23, GPIO 24, GPIO 25 and
GPIO 29 (used by the module, not on the header) and GPIO 30 and up (not on the part).
**Refused as a second PWM pin:** a pin whose PWM compare register is already
taken by a bound pin. On the RP2350 the slice is `(pin / 2) modulo 8` below
GPIO 32 and the channel is the pin's low bit, so GPIO 0 and GPIO 16, GPIO 1 and GPIO 17, GPIO 2
and GPIO 18, GPIO 4 and GPIO 20, GPIO 5 and GPIO 21, GPIO 6 and GPIO 22 are pairs that share one
compare register. The second of a pair is refused rather than muxed onto the
first one's pulse width. The OUTPUTS page carries no bit saying "bound", so
the screen goes on looking configured while the lead produces no pulse.

On the panel: **Setup → OUTPUTS**, choose `SERVO PWM`, tick one pin. Or
**Setup → PICK A PIN**, choose `SERVO PWM` in its list and tap one key on the
board drawing — grounds are marked `G`, rails carry their voltage.

**Scope the pin before connecting the servo.**

| | |
|---|---|
| Frame rate | 40 to 560Hz (50Hz by default) |
| Pulse | 400 to 2500µs, refused outside |
| Resolution | 1µs |

**Measure and record:** the actual frame period, the pulse at both ends of
travel, and the jitter. Bit timings are the largest unverified surface in the
tree.

**Then:** confirm the servo screen lets go when it is told to, and only
then. Lifting the finger does not stop the output: the screen holds the
position it was given and says it again every **100ms** (`SERVO_HOLD_MS`),
against the IOmcu's **500ms** (`OUT_DEFAULT_TIMEOUT_MS`), so a servo
stays where it was put. **RELEASE** returns the surfaces to centre; it does
not clear the slot and the pin keeps pulsing. What stops the edges is a
switch to SAFE, a STOP, or leaving the screen, which switches to SAFE. Check on the scope that
RELEASE moves the pulse to the middle of the channel's travel and that a
switch to SAFE is what stops it.

A channel nobody is refreshing does still go to rest after 500ms, which is
what happens to a bound pin the servo screen is not holding.

---

## 7. The numbers nobody has measured

Written from the specification, exercised only against frames the same code
builds. Record real values for each:

- **Every DShot bit timing**, against an ESC's tolerance rather than the spec.
- **The reply rate** of five quarters of the DShot rate.
- **The leading-bit convention** of the group code.
- **The turnaround delay** — whether 30µs is what an ESC actually waits.
- **The extended-telemetry frame types** and their units.
- **The PPM DMA ring** playing a frame.
- **The flash erase window** on the IOmcu, and whether the heartbeat
  re-acquires across a save.

---

## 8. Current monitors (INA228, INA3221)

The IOmcu's core 1 reads an INA228 in the ESC's power path and an
INA3221 on the servo rail over I2C (Inter-Integrated Circuit) at 400kHz,
every 1ms. Every step below says what good
looks like; anything else is a finding to write down.

**Have to hand:** the MATEK I2C-INA-BM (INA228, 200 µΩ) at the address it
ships with, the DAOKAI INA3221 (0.1Ω per channel, 1.638A full scale) with
its A0 solder bridge closed to GND for 0x40 (as shipped all four A0 bridges
are open and the address floats between 0x40 and 0x41), two
2.2kΩ resistors, a multimeter, a scope or logic analyser with I2C decode,
a servo, and the ESC and motor or a resistive load on a current-limited
supply.

**Set it up on the panel.** SETUP → INTERFACES: `INA228` ON; SETUP →
SENSORS: `0x40 fitted` ON. The other sensor settings default to the modules
above: INA228 at 0x45 with 200 µΩ, the INA3221 at 0x40 with 0.1Ω on each
channel and CH1 watching OUT 1, SDA on GPIO 16 and SCL on GPIO 17. Without a panel, the build option `IOMCU_SENSE_BRINGUP=ON` enables both
parts at the page's defaults while no set-up is stored in flash:

```bash
cmake -S firmware/iomcu -B firmware/iomcu/build-sense -DIOMCU_SENSE_BRINGUP=ON
cmake --build firmware/iomcu/build-sense
```

**Wire it** (all IOmcu pads on the RP2350-CAN module):

| Wire | From | To |
|---|---|---|
| SDA | GPIO 16 (pad 21) | INA3221 SDA, MATEK SDA |
| SCL | GPIO 17 (pad 22) | INA3221 SCL, MATEK SCL |
| Pull-ups | 2.2kΩ from SDA and from SCL | 3V3 (pad 36) |
| INA3221 power | 3V3 (pad 36), GND | INA3221 VS, GND |
| MATEK power | VBUS (pad 40, 5V from USB), GND | MATEK 5V, G |

The firmware switches the pads' own pull-ups and pull-downs off on both
pins: the bus runs on the modules' pull-ups and the two 2.2kΩ. **Before
connecting the IOmcu**, power the modules and measure SDA and SCL to
GND: both must read 3.3V or less. The MATEK's pull-up rail is not known.

**The console.** Every 3s, while a part is enabled, the IOmcu prints
three lines:

```
rcbench-iomcu: sense flags 0x0133 present 0x0021 ids 0x2281 0x3220 errors 0 | bench 1680 cV 1200 cA 33 mAh 6 dWh flags 0x63 | temp 254 dC
rcbench-iomcu: sense window 4711 CH1 120 mA 6000 mV ch_flags 0x01 | capture state 0 seq 0 move 0 arrive 0
rcbench-iomcu: sense map fit 0x1 fast 0 flags 0x0201 | mA 120 0 0 0 0 0 0 0 0 0 0 0 | over 0x000 count 0
```

`flags` is the SENSE page's FLAGS: 0x0001 INA228 online, 0x0002 its
identity read was an INA228's, 0x0004 something else answers at its
address, 0x0008 a current clipped, 0x0010, 0x0020 and 0x0040 the same for
the INA3221, 0x0100 the bus is open, 0x0200 the bus is stuck. `present`
has bit n for address 0x40 + n. `bench` is the BENCH page: 10mV and
10mA steps, charge in mAh, energy in 0.1Wh, and its flags (0x01
voltage, 0x02 current, 0x20 the INA228's, 0x40 the INA228's totals).
The third line is the INA3221 map, SENSE_MAP: the parts fitted, the FAST
channel, its flags (0x0200 derived from SENSE), each channel's mean
current, and the channels over their current limit (SENSE_LIMIT) with
their count ([The INA3221 map and windows](LinkSensors.md#on-the-console)).

### 8.1 The parts answer

Power everything, nothing ARMED.

**Good:** `flags 0x0133`, `present 0x0021`, `ids 0x228x 0x3220`, `errors`
not rising between two lines.

**Write down:** the two IDs as printed. An INA238 on the MATEK answers
0x238x and is refused (flag 0x0004). A DAOKAI unit that answers 0x1408
(one buyer's reading) is refused the same way (0x0040).

### 8.2 The bus on a scope

Probe SDA and SCL at the IOmcu.

**Good:** SCL at 400kHz (2.5µs a clock); rise time, 30% to 70%, at most
300ns; a read of the INA3221's register 0x01 every 1.0ms; the INA228's
registers 0x07 and 0x05 in turn, one each 1.0ms; no NACK in the decode.

**Write down:** the rise time, the clock, the time from one transaction's
STOP to the next START (the controller's own time, not measured), and the
period of the CH1 read.

### 8.3 Readings against a meter

Run a steady current through the MATEK's shunt and measure it and the
pack voltage with the meter. Then a servo, or a resistor drawing under
1.6A, on INA3221 CH1, with the meter in series.

**Good:** `bench` voltage and current, and `CH1` mA and mV (CH1's voltage
is at the load side of the shunt), agree with the meter within the meter's
accuracy and the shunt's tolerance, which is not known for either module.
`temp` near room temperature, in 0.1°C. `ch_flags 0x01`; 0x10 appears
when CH1 passes 1.638A, and CH1's current is then a bound, not a value.

**Write down:** each pair of numbers, the meter's and the console's.

### 8.4 A run: peaks and totals

Press ARM on MOTOR & ESC and hold a steady current for 60s, then switch to SAFE.

**Good:** `bench flags` reads 0x63 (with 0x04 and 0x08 too when a
bidirectional ESC answers). `mAh` counts current × time: 2.00A for 60s
is 33mAh. A new switch to ARMED starts charge and energy from 0.
The MOTOR & ESC screen shows the INA228's voltage and current.

**Write down:** current, time and the charge reached, and the peaks the
panel shows against the scope or meter.

### 8.5 A lead pulled mid-run

While ARMED, pull the MATEK's SDA for about 5s, then plug it back.

**Good:** within about 6ms `flags` loses 0x0001 and keeps 0x0002; `bench
flags` reads 0x20: the INA228 is still the source, its fields empty, not
the ESC's numbers; `errors` rises; the bench stays ARMED. About 1s after
the lead is back, 0x0001 returns, and 0x40 stays clear until the next switch to ARMED.
With the bench SAFE and the lead out, `bench` carries the ESC's telemetry again.

### 8.6 A stuck bus

Hold SDA to GND with a wire for 2s, then let go. The lines are open drain:
the pin is never driven high, so the short is safe.

**Good:** `flags` gains 0x0200 while it is held; on SCL the scope shows 9
clocks and a STOP every 100ms; after release, 0x0200 clears and both
parts are online within about 1s.

**Write down:** the clock rate of the 9 clocks (meant to be 100kHz), and
how long the parts took to come back.

### 8.7 A save while reading

While SAFE, with both parts reading, tick a pin on the OUTPUTS screen: the
binding is saved.

**Good:** the console prints `outputs saved, record n, program window N
us` and no `window refused`; `errors` does not rise; `window` goes on
counting; the heartbeat does not drop.

**Write down:** the program window, and the erase window when one is
printed: core 1 is parked in RAM for each.

### 8.8 Not reachable yet

The move capture on SERVO_SENSE -- the PWM frame stamped to 1µs and the
travel times -- is armed by the panel's servo test, which does not use it
yet. A travel time against a scope on the PWM pin and on the shunt waits
for that.

### 8.9 CH1's 1ms samples on the console

An IOmcu image built with `-DSENSE_TRACE=ON` prints INA3221 CH1's
1ms samples on its USB (Universal Serial Bus) console as text. The link
carries 50ms windows only, so this build is the one way to see the single
samples. It is a debug build: a released image is built without the option
and holds none of its code. `tools/sense_trace.py` reads the captured
console. The bench session has two
parts: part A needs no encoder, part B repeats the moves with an AS5600
on the servo's shaft.

**What starts a trace.**

| Trigger | Trace |
| --- | --- |
| a PWM (pulse-width modulation) output renders another pulse width than in the pass before, 50ms or more after that output's last change | 4s from the frame that carries the pulse; trigger line `$C` |
| `t` typed on the console | 10s; trigger line `$K` |
| the capture's PWM edge | 4s; trigger line `$E`. No released panel arms the capture, so this trigger does not occur with one |

Every MOVE of the automatic servo test is a changed pulse, so a test with
the released panel is traced without a capture. A trigger during a trace
adds its line and moves the end to 4s (10s for `t`) after itself when
that is later: moves less than 4s apart are one trace. A command that is
slewed changes the pulse in every pass: its `$C` line has the first pulse,
and a `$D` line the pulse it ended at, once the output has held still for
50ms. The `$D` line's `cmd=` is its command's `$C` time, and the tool
ends that command there, also when the output was let go and the next
command came within 30ms. A command that waits for the next trace takes its later changes
with it: that trace ends 4s after the last of them and has the `$D`
line. An output that is let go ends a slewed command there; the next
pulse on it is a command, however soon it comes. The automatic test's
moves are steps and have no `$D` line. `x` on the console
ends a trace; a trigger line whose time lies at or after the stop is
dropped. Each trace also holds the samples of up to 64ms before its
trigger.

**What it costs.** Core 1 reads CH1 every 1ms as in the released image
and copies the sample into a ring of 4096 records (49,152 bytes; 3.9s of
1000 samples and 50 bus voltages a second). No bus transaction is added
to the tick. A change of the part's set-up or state is a record in the
same ring, so it keeps its place among the samples. Core 0 writes whole lines into the room the console's 64-byte
transmit buffer has, and nothing when no terminal is connected, so its
loop does not wait for the host. A full ring drops the newest record: the
trace has a `$L n=` line where records are missing and their sum in its
end line. The end line counts every dropped record from before the
trace's end, with or without a record kept after it. A sample dropped
while a set-up record waits for room is counted after that record, so a
trace that ends at the set-up change does not count it. The time a pass of core 0 gains from writing the lines is not
measured.

**The lines** (`shared/sense/sense_trace.h` has every field):

```text
$T v=3 n=2 trig=cmd t=220200 ms=22020 len=4000
$H dt_us=1000 shunt_uohm=100000 cfg=0x4007 on=1 rst=0
-810,289
10,277
v5952
10,289
$C t=220200 ch=0 us=1800
10,2378
$Z n=2 s=8881 v=444 l=0 m=4 ml=0 e=t
```

The lines after the first two stand in the order of their times, except a
`$D` line: a trigger line follows the samples taken before it. A `$D` line
is written once the output's pulse has stood still for 50ms, so it follows
samples up to about 50ms later than its `t=`; its `t=` is its time. Core 0 writes a trigger line and
the end line once core 1 has finished a tick that started at or after the
line's time. The end line's `e=` is the reason the trace ended: `t` its
time, `s` a changed set-up, `k` the console. A capital letter (`T`, `S`,
`K`) says that core 1 was not seen past a trigger line's time or the end
within 100ms and the line was written all the same: a sample from before
that time can be missing or stand after the line. The tool reports such a
trace as a problem and replays none of its moves.

A sample line is the time since the sample before it in 0.1ms and the
shunt code, 40µV a step: 0.4mA on the 0.1Ω shunt. A sample line 1ms
after the sample before it is 8 bytes with a 3-digit code and 10 bytes at
most with a 4-digit code. The first sample line of a trace counts from the
trigger time, so a sample from before the trigger carries a negative time,
and a sample after dropped records carries a longer one: those lines are
longer, up to the full signed 32-bit range of the time. With the
voltage lines (a 7-byte line, one in 20 samples in the output of
`sense_trace_gen`) a trace is 8.35 bytes a sample, 8.35kB a second, at the
simulated servo's 0.12A, whose codes have 3 digits. Above about 0.4A on the
0.1Ω shunt the codes have 4 digits and the rate is 9.35kB a second. Every
line ends in CR LF. The rate on hardware is not measured.

**Have to hand:** the PD mini (WeAct PD Power Mini V1) at a limit of
2.00A, the INA3221 module with CH1's 0.1Ω shunt in the servo's supply,
the servos to measure, and a Windows PC with PuTTY. Part A needs no
encoder. Part B needs an AS5600 on the servo's shaft.

**The terminal.** The console is the IOmcu module's own USB-C
socket, the one the image is flashed through, not the panel's. In the
Windows Device Manager it is under "Ports (COM & LPT)" as "USB Serial
Device (COMn)", hardware ID `VID_2E8A`. PuTTY settings:

| Where | Setting |
| --- | --- |
| Session | Connection type Serial, Serial line `COMn`, Speed 115200 |
| Connection, Serial | Data bits 8, Stop bits 1, Parity None, Flow control None |
| Session, Logging | "All session output", a new log file name for each run |

The port is USB CDC (communications device class): the speed is not used,
any value works. The IOmcu prints only while the terminal holds DTR
(data terminal ready), which PuTTY does while the port is open. No
timestamps in the log: inside a trace a line with anything in front of it
is damage, and no move of that trace is replayed. A key is sent when it is
pressed, with no Enter.

**Part A: without the encoder.**

1. Build the image:

   ```bash
   export PICO_SDK_PATH=/path/to/pico-sdk
   cmake -S firmware/iomcu -B firmware/iomcu/build-trace -DSENSE_TRACE=ON
   cmake --build firmware/iomcu/build-trace
   ```

2. Flash `firmware/iomcu/build-trace/rcbench-iomcu.uf2`: hold BOOTSEL,
   plug the module into the PC, copy the file to its drive.
3. Open the port in PuTTY with logging on. Within 3s it prints a line
   starting `rcbench-iomcu:`. On the panel, SETUP → SENSORS: `0x40
   fitted` ON.
4. Noise, no servo: with no servo connected and the supply's output on at
   6.00V, press `t`. 10s later the console prints a line starting `$Z`.
   Close PuTTY; this log is `noise.log`.
5. Servo at rest: new log file. Connect the servo, press ARM, leave it at the
   centre, supply on at 4.80V. Press `t` and wait for the `$Z` line.
6. Moves, in the same log: on the SERVO screen's TEST task, in the
   AUTOMATIC TEST card's SETTINGS, set TEST LENGTH BY
   to MOVEMENTS, MOVEMENTS 20, DWELL TIME 1000ms, STEP 4.8V on and every
   other step and BROWN-OUT off. START TEST. The trace runs from the first
   move to 4s after the last; wait for its `$Z` line, then close PuTTY.
7. Repeat steps 5 and 6 at 6.00V with STEP 6.0V, and both for each
   servo: one log file per servo and voltage.
8. On the host, for each log:

   ```bash
   python3 tools/sense_trace.py noise.log
   python3 tools/sense_trace.py mg90s-4v8.log
   ```

**Good:** every trace reads `counts match the end line` and `0 records
missing`; the exit code is 0.

**What the tool prints.** Per trace: the samples and voltages read against
the end line; the mean, the standard deviation and the largest distance
from the mean of the samples before the first command, as read and through
a moving mean of 4 and of 8 samples; then every move replayed through
`shared/servo/servo_move.c` with the filter at 1, 4 and 8 samples and the
band at 0.02, 0.05 and 0.10A: seen or not, and the arrival in ms from the
frame. For the log as a whole: the settings that see every move and time
its arrival, each setting's median arrival and its distance from the
capture's setting (filter 4, band 0.05A), and how far apart the settings
put one move's arrival. It ends `not compared with the horn`: without an
encoder an arrival is the current back at its holding level, which can
lead or lag the horn. One `<log>-trace-<n>.csv` per trace holds four
columns, separated by `;`: time in ms, current in A, the bus voltage in V
on the rows that have one, and `clipped`. `clipped` is 1 for a sample at
or past the top of the range, -1 at or past the bottom, and 0 for a value.
A clipped row's current is the end of the range, a bound and not a value:
1.6380A or -1.6384A on a 0.1Ω shunt. A file with three columns has no
clip mark, and its clipped rows read as values.

**What the tool refuses.** Between a trace's `$T` and `$Z` line every
line is a whole line of the format or a console line of the IOmcu's
own, which starts `rcbench-iomcu:`. A line is whole with every field of
its type in its order, nothing before, between or after the fields, and
each number in the range the IOmcu holds it in: a `$C` or `$D` line
has `ch=` and `us=`, a `$D` line `cmd=` after them, a `$E` or `$K` line
has neither. The tool reads format versions 3 and 2 (`$T v=`). A version 2
`$D` line has no `cmd=`, and the tool ends with it the latest command of
its channel no more than 30ms after its `t=`: a command that follows a
let-go output within 30ms takes that `$D` line as its own. A `$D` line of
the other version's form is damage. A line ends LF or
CR LF. Anything else is damage. So are a second `$H` line, a line other
than `$L` before the `$H` line, a trace length other than 4000ms
(10000ms for `t`), a sample period other than 1000µs, a sample timed
before the one before it, a second voltage for one sample, a `$S` line
that repeats the state before it, and a line of a trace outside a trace.
A `$L` line before the `$H` line counts records missing before the
trace's first sample, and no move of that trace is replayed. The tool
prints a
`PROBLEM:` line with the count and the number of the first such line,
replays no move of that trace and exits 1.

A move's levels come from four windows of samples:

| Window | What is taken from it |
| --- | --- |
| the samples before the trace's first command | every move's threshold; the holding level of a move to a pulse width that no earlier move left |
| the 50ms before the move | its level before |
| the 200ms before the move | its threshold; the holding level of each later move back to the pulse width this move left |
| from 50ms before the move to the next move or the trace's end | the samples the replay is fed |

A window with no sample, with a `$L` line in it or at either end, or with
a `$S` line in it or at either end gives no level. Each move that needs
that level reads `not replayed`, with the window named. A clipped sample
in a window counts as 4094 steps towards that end of the range, as in the
replay, and the move's line says how many of its level samples are
clipped.

**More than one output.** `ch=` in a `$C` or `$D` line is the output
channel, not the pin: the channels count from 0 over the bound pins in
ascending pin order, 1 channel per PWM pin. With GPIO 0, GPIO 1, GPIO 2 and GPIO 4
bound, GPIO 2 is channel 2 and GPIO 4 is channel 3. The shunt carries the
current of every servo on the supply, and the SERVO screen commands every
servo output in the same frame. The tool replays no command that has
another command within 30ms of it, so with 2 servo outputs bound it
replays no move. `--loaded-channel N` names a channel with a servo
connected to its pin; the option repeats, 1 channel each:

```bash
python3 tools/sense_trace.py mg90s-4v8.log --loaded-channel 2
```

The `$C` and `$D` lines of every other channel are then no moves and
count for no 30ms. A `$E` line has no channel: the commands of the named
channels take their edge lines first, and an edge line left within 30ms
of another channel's command is left out. The report's second line names
the channels and counts what is left out:

```text
loaded channel(s) 2: 25 command line(s) of channel(s) 1, 3 left out
```

It ends `no command line of channel(s) N` for a named channel that no `$C`
line of the log has; the exit code stays 0. Two named channels commanded
within 30ms of each other are not replayed. Without the option every
channel counts. The tool does not check that a named channel's pin has a
servo on it: a servo on a channel that is not named moves in the replayed
current.

`--floor` takes 0.000001A to 1000A, `--pair-ms` more than 0ms up to
60000ms, `--csv-offset` up to 1e9 s either side of 0, `--loaded-channel`
a whole number from 0 to 65535 with no sign and no leading zero; any
other value exits 2. A servo CSV file
that cannot be read, a CSV file that cannot be written and a replay that
answers another number of moves than it was sent exit 2.

**Write down:** the tool's output for each log. Part A answers: the noise
of CH1's 1ms samples with no servo and with a servo at rest, against the
threshold's floor (`SERVO_MOVE_MIN_A`, 0.020A); which filter lengths and
bands see all 22 moves of each test (2 that place the servo and 20
counted); and how much the arrival moves with the setting. It does not
answer whether an arrival is the horn's.

**Part B: with the AS5600.** The same moves with the encoder on the shaft.

1. SETUP → INTERFACES: `AS5600` ON, with the bank SAFE. On the SERVO
   screen's SETTINGS > DUT, with the servo at its neutral, tap ENC CENTRE.
2. Repeat part A's steps 5 to 7.
3. Keep each log together with its test's `ATESTnnn.CSV` from the SD card's
   `USER/LOGS` folder.
4. On the host, for each pair:

   ```bash
   python3 tools/sense_trace.py mg90s-4v8.log --servo-csv ATEST012.CSV
   ```

The tool then also prints, per move, the arrival less the encoder's
`travel angle (ms)`, and its median per setting. Part B answers what the
capture's filter length (`SENSE_CAP_FILTER_N`, 4) and arrival band
(`SERVO_MOVE_BAND_A`, 0.05A) are to be: both are chosen, not measured.

**Not known:**

- Part B: the encoder's travel time counts from the command as the panel
  issues it, the trace's arrival from the PWM frame at the pin. The
  difference the tool prints contains the time between the two: up to one
  poll and one frame, not measured.
- A command's frame time is computed from the PWM counter read after the
  pulse is written. A frame that ends between the two puts that one
  command's time one frame (20ms at 50Hz) late. How often: not measured.
- Part B: the two files have different clocks. The tool pairs rows and commands by
  the spacing of the moves; when two offsets pair equally many rows, and
  not the same rows with the same moves, it says so, exits 1 and takes
  `--csv-offset`.
- A log with a restart of the IOmcu in it has one clock per boot.
  The tool takes a `$T` line for a restart when its millisecond tick lies
  before the last one's or its trace number is not past the last one's;
  missing numbers are traces no console took, not a restart. A restart
  after which neither goes back is not told.
  The tool pairs each boot's moves on their own. When the rows of a later
  boot do not lie after those of the boot before it, it pairs no row, says
  so and exits 1: evaluate each boot's part of the log with its part of
  the CSV file.
- Commands within 30ms of each other, as two outputs written in one
  frame, change the current together. The tool lists them and replays
  none of them, unless `--loaded-channel` leaves all but one of them
  out. A command that follows another output's command later
  than that, before the first servo arrived, is replayed with the first
  servo's current in it; the tool does not tell the two apart.
- Whether a terminal keeps up with 8.35 to 9.35kB a second without loss. A trace
  whose line counts do not match its end line is reported and exits 1.
- The line format has no check value. A line cut inside its last number
  is a whole line with another number, and the tool reads it as that: a
  sample's code, a voltage, the pulse width of a `$C` line, the command
  time of a `$D` line, the
  time of a `$E` or `$K` line, the reset count of a `$H` or `$S` line. A
  cut anywhere else in a line is reported, and so is a lost sample,
  voltage or trigger line. A lost `$S` line is reported when the next one
  repeats the state. A log that stops between two traces reads as a log
  with fewer traces.

---

## 9. When something goes wrong

| Symptom | Look here first |
|---|---|
| Switch to ARMED refused, wire fitted | Heartbeat not edging 4 times yet, or gap over 150ms. Scope GPIO 3. |
| Switch to ARMED refused, no obvious reason | Touch dead for 500ms blocks it. Touch the panel. |
| Link up, screen offers no pins | The board's catalogue did not arrive. Console says which page failed. |
| Heartbeat drops at the first link-up | **The drawing transfer or the flash keeper.** See §4. |
| Display freezes briefly | A flash write. Expected while the board drawing is kept, once per flash operation, 4 for the RP2350-CAN's drawing. |
| IOmcu does not boot after flashing | Image past 4MB. Check the size line from §1. |
| Panel boots but no `boardart` partition | Flashed app-only over an old table. Merge-bin at offset 0. |
| Output goes to mid-travel after ½ s | Working as intended — nothing wrote to that channel, so it went to its rest: mid-travel for a servo, zero for a motor. The pulses continue while the bench is ARMED |
| Pulses stop altogether | Not the timeout. Something released the pin, switched the bench to SAFE, or stopped the bench |
| `sense flags` without 0x0100, a part enabled | The I2C block did not open on those pins. They must be one block's SDA and SCL: SDA's GPIO number mod 4 is 0 or 2, SCL the next |
| `sense flags` 0x0200 that stays | SDA or SCL held low: an unpowered module clamping the bus, a missing pull-up, or a short |
| `sense present 0x0000` with the bus open | Nothing answers: pull-ups missing, modules unpowered, or SDA and SCL swapped |

**If the bench misbehaves in a way that points at the panel**, the three
newest and least proven things are all mine and all only compiler-checked:

- the **drawing keep** in the control task, one flash step a pass,
- the **drawing slice** inside the control task's poll loop, and
- the **run log task** (`runlog`), which owns every write to the SD (Secure
  Digital) card.

The first two are inert on an IOmcu that reports no drawing, which is
the quickest way to rule them out. The third is inert with no card in the
slot. With a card it appends a line to `BENCH/RCBENCH.LOG` whenever a fault is
raised, ARMED or not; the run file is what waits for a switch to ARMED, and for the
first row after it.

---

## 10. What to write down

For each step: what was measured, against what it was expected to be, and
what the scope showed. `STATUS.md` carries an "Open items" table — the rows
about drivers on hardware, the control page and the flash store are the ones
this run answers.

**Anything that is not measured stays written as not measured.** A number
guessed into that table is worse than an empty cell.
