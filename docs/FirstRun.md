# First run on hardware

<sub>**English** · [Deutsch](FirstRun-de.md)</sub>

Both boards powered with the heartbeat wire fitted, and every number below
read on an instrument. Written for 0.8.1. Every step says what "good" looks like and what to write down when it is not.

Work down the list. Each step assumes the one above it passed.

---

## 0. Before power

> ### Set the panel's UART switch to `UART1` before anything else
>
> A slide switch beside the **BOOT** and **RESET** buttons is marked
> **`UART1`** and **`UART2`**. It selects what the bridged USB-C socket's
> serial side is connected to, and the console needs it on **`UART1`**.
>
> On the other setting the socket still enumerates: a COM port appears and
> the operating system names the CH343. Nothing crosses it in either
> direction — no console output and no flashing — which is the same symptom
> a broken cable or a broken board gives.
>
> The panel's other USB-C socket carries native USB on GPIO19 and GPIO20,
> which the multiplexer hands to CAN about a second into every boot. It
> flashes the board and it cannot watch a running one. The bridged socket is
> the panel's only console while the bench runs.

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

What a bench loads over USB (Universal Serial Bus) for 0.20.0 depends on
the release it runs:

| Bench runs | Coprocessor | Panel |
| --- | --- | --- |
| nothing (a new bench), or 0.18.0 and older | over USB, [step 2](#from-the-release-assets) | over USB, [step 3](#from-the-release-assets) |
| 0.19.0 | over CAN (Controller Area Network) from the panel ([From 0.19.0](#from-0190)) | over USB, [step 3](#from-the-release-assets) |
| 0.20.0 and later | over CAN from the panel | over Wi-Fi |

The USB load of a new bench, or of one on 0.18.0 and older, puts the
coprocessor's partition table and the panel's two application slots in
place, and neither comes over the link. A 0.19.0 panel takes no image over
Wi-Fi: ACCESS POINT stops with `Wi-Fi could not start.` as it is pressed, and
by the estimate of internal RAM (random-access memory) in
[Performance](Performance.md#internal-ram) JOIN HOME NETWORK stops the same
way. From 0.20.0 on, the panel installs a release over Wi-Fi ([Screens:
Wi-Fi](Screens.md#wi-fi)) or downloads it from GitHub
([Screens: Updates](Screens.md#updates)), and the coprocessor takes its
image over CAN from the panel.

### From the release assets

| Asset | Holds | For |
| --- | --- | --- |
| `rcbench-iomcu-vX.Y.Z.zip` | `rcbench-iomcu-install.uf2`, `rcbench-iomcu.uf2`, `rcbench-iomcu-pt.uf2`, `rcbench-iomcu.bin`, `rcbench-iomcu.elf` | the coprocessor's USB load: `rcbench-iomcu-install.uf2` |
| `rcbench-panel-vX.Y.Z.zip` | `flash_args`, `bootloader/bootloader.bin`, `partition_table/partition-table.bin`, `ota_data_initial.bin`, `rcbench-panel.bin`, `rcbench-panel-merged.bin`, `rcbench-panel.elf` | the panel's USB flash |
| `rcbench-panel-vX.Y.Z.bin` | the panel's application alone, with the coprocessor image it offers over CAN | an update over Wi-Fi of a panel that runs 0.20.0 or later; not for USB |
| `SHA256SUMS` | the SHA-256 (Secure Hash Algorithm) of the three files above | the check in step 1 |

Tools: the RP2350's `RP2350` drive or `picotool` for the coprocessor,
`esptool.py` for the panel (`pip install esptool`; ESP-IDF carries it).
`/dev/ttyACM0` below is `COMx` on Windows.

1. In the download folder, `sha256sum -c SHA256SUMS` prints `OK` for each
   of the three files.
2. **Coprocessor.** Unpack `rcbench-iomcu-vX.Y.Z.zip`. Hold BOOTSEL while
   the coprocessor's USB socket is connected, so the module shows its drive
   `RP2350`. Copy `rcbench-iomcu-install.uf2` onto the drive, or:

   ```bash
   picotool load -v -x rcbench-iomcu-install.uf2
   ```

   The module restarts into slot A. Its console prints `rcbench-iomcu:
   running from slot A` every 3 s ([below](#the-coprocessors-partition-table)).
3. **Panel.** Unpack `rcbench-panel-vX.Y.Z.zip` and connect the panel's
   USB-C socket (bridged socket: switch on `UART1`, [§0](#0-before-power)).
   In the unpacked folder, on a bench whose settings stay:

   ```bash
   esptool.py --chip esp32s3 -p /dev/ttyACM0 write_flash @flash_args
   ```

   On a new bench, or to start from the default settings:

   ```bash
   esptool.py --chip esp32s3 -p /dev/ttyACM0 write_flash 0x0 rcbench-panel-merged.bin
   ```

   A browser flasher that writes one file at an offset, such as the ESP Web
   Tool, takes `rcbench-panel-merged.bin` at 0x0, with the same effect as
   the second command.
4. Power both boards with the link wired. The panel finds the build it
   carries on the coprocessor and offers no update. It fetches the board
   drawing again ([§4](#4-first-link-up-the-board-drawing)).

| What | Parts (`@flash_args`) | Merged image at 0x0 |
| --- | --- | --- |
| Panel settings and Wi-Fi settings, in NVS (non-volatile storage, 24 KiB at 0x9000) | kept | erased; the panel starts on its defaults |
| Panel's stored board drawing | lost once; fetched again at link-up | lost once; fetched again at link-up |

The coprocessor keeps its bindings, the supply's wiring and the current
monitors' set-up, with the exception for an RP2350 of stepping A2 stated
[below](#the-coprocessors-partition-table).

The panel can go first. A panel of 0.19.0 or later linked to a coprocessor
without the partition table shows `Coprocessor cannot update over CAN. Install firmware once via USB.` until step 2 is done; the link runs meanwhile.

### From 0.19.0

A bench on 0.19.0 loads the panel over USB, and only the panel. The
coprocessor carries the partition table from the USB load of 0.19.0 and
takes 0.20.0 over CAN from the image the new panel carries. The link
protocol stays 4.22, so the two boards link before and after the update.

1. In the download folder, `sha256sum -c SHA256SUMS` prints `OK` for each
   of the three files.
2. **Panel.** Flash `rcbench-panel-v0.20.0.zip` as in
   [step 3](#from-the-release-assets) above: `write_flash @flash_args` to
   keep the settings, or `rcbench-panel-merged.bin` at 0x0, from
   `esptool.py` or the browser flasher, to start from the default settings.
3. Power both boards with the link wired. At link-up the panel compares the
   coprocessor's running image with the image it carries by SHA-256, finds
   another build, and the band says `Coprocessor runs a different build. An update is available in the menu.`.
4. **Coprocessor.** The question opens on the menu while the bench is SAFE,
   the supply's output is off and the panel's Wi-Fi radio is off. It names
   0.19.0 on the coprocessor and 0.20.0 in the panel. Hold START for 2 s.
   The progress fills the screen ([Screens: Coprocessor
   update](Screens.md#coprocessor-update)), and back on the menu the band
   says `Coprocessor updated to 0.20.0.`.

| What | Parts (`@flash_args`) | Merged image at 0x0 |
| --- | --- | --- |
| Panel settings and Wi-Fi settings, in NVS (24 KiB at 0x9000) | kept | erased; the panel starts on its defaults |
| Panel's stored board drawing, `boardart` at 0x7E0000 | kept: the last part ends with the application, about 2.1 MB from 0x0 | kept: the image ends with the application, about 2.1 MB from 0x0 |

The panel shows the stored drawing again when the coprocessor's ARTWORK
page names the same drawing, and fetches it otherwise
([§4](#4-first-link-up-the-board-drawing)). The setting REMINDER AT START
is new in 0.20.0 and starts on.

The coprocessor's update writes the slot it does not run from. Its
bindings, the supply's wiring and the current monitors' set-up stay: they
are in the store partition at 0x3FC000, or before its first save in the old
store at 0x3FE000, and the update writes neither. CANCEL on the
question leaves the coprocessor on 0.19.0; the panel asks again after its
next start. Power lost during the update leaves the old image running, and
the next link-up offers the update again. A coprocessor that missed the USB
load of 0.19.0 has no partition table: the band says `Coprocessor cannot update over CAN. Install firmware once via USB.`, and it takes
[step 2](#from-the-release-assets). The time of the update over CAN is not
measured.

### From a source tree

```bash
# coprocessor — produces rcbench-iomcu-install.uf2 and rcbench-iomcu.uf2
cmake -S firmware/iomcu -B firmware/iomcu/build
cmake --build firmware/iomcu/build

# panel
idf.py -C firmware/panel set-target esp32s3
idf.py -C firmware/panel build
idf.py -C firmware/panel -p /dev/ttyACM0 flash monitor
```

The coprocessor build prints its own size check:

    -- rcbench: image 191808 bytes, 9% of the 2084864 bytes of one slot

**Watch for:** the module used for bring-up is a Waveshare RP2350-CAN with
**4 MB**, while `PICO_BOARD` defaults to a board file claiming **16 MB**. The
linker measures against 16 MB and will not warn. The line above is the only
thing that will, so read it.

### The coprocessor's partition table

The coprocessor's flash carries a partition table: two image slots of
2036 KiB each and the output store's own 8 KiB partition
([Link](Link.md#firmware-update-over-the-link) has the layout). The panel
sends later coprocessor images over CAN into the slot that is not running.
A new module and a coprocessor loaded with 0.18.0 or older (an image of
protocol 4.21 or older) have no table and take the table and their first
image over USB, once.

With the coprocessor's USB socket connected and BOOTSEL held at power-up, so
the module shows its drive `RP2350`:

```bash
# drag and drop the file onto the drive, or:
picotool load -v -x firmware/iomcu/build/rcbench-iomcu-install.uf2

# check: partitions 0 (A), 1 (B) and 2 (store, data)
picotool partition info
```

**Good:** the coprocessor's console prints `rcbench-iomcu: running from
slot A` every 3 s. The bindings, the supply's wiring and the current
monitors' set-up are read from the old store at 0x3FE000 until the first
save writes the new one. `no partition table -- firmware updates over CAN
need one USB load of rcbench-iomcu-install.uf2` means the image runs without
the table: load `rcbench-iomcu-install.uf2` again.

On an RP2350 of stepping A2 the load also writes the RP2350-E10 workaround
block, which lands in the last 4 KiB sector of the 4 MB, 0x3FF000: the old
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

The panel's flash holds two application slots of 4,063,232 bytes, `ota_0`
and `ota_1`, so it can update itself over Wi-Fi ([Screens:
Wi-Fi](Screens.md#wi-fi)). A panel flashed with 0.18.0 or older
has one 3 MiB `factory` slot and reaches the new layout with one flash over
USB:

| Offset | 0.18.0 and older | 0.19.0 and later |
| --- | --- | --- |
| 0x9000 | `nvs`, 24 KiB | `nvs`, 24 KiB |
| 0xF000 | `phy_init`, 4 KiB | `phy_init`, 4 KiB |
| 0x10000 | `factory`, 3 MiB | `otadata`, 8 KiB |
| 0x20000 | | `ota_0`, 3,968 KiB |
| 0x310000 | `storage`, 1 MiB, unused | |
| 0x400000 | | `ota_1`, 3,968 KiB |
| 0x410000 | `boardart`, 2 MiB | |
| 0x7E0000 | | `boardart`, 128 KiB |

The parts keep the settings: they write the bootloader at 0x0, the
partition table at 0x8000, `otadata` at 0x10000 and the application at
0x20000, and leave `nvs` alone. From a source tree:

```bash
idf.py -C firmware/panel -p /dev/ttyACM0 flash
```

From the release archive, `esptool.py ... write_flash @flash_args` in its
unpacked folder, as in [step 3](#from-the-release-assets).

The merged image at offset 0 covers the flash from 0x0 to the end of the
application in `ota_0`, about 2 MB, and fills the gaps between the parts
with 0xFF. `nvs` at 0x9000 lies in such a gap, so the merged image erases
the settings.

Either way the stored board drawing is lost once: `boardart` moved from
0x410000 to 0x7E0000. The panel fetches it again at the next link-up
([§4](#4-first-link-up-the-board-drawing)). From 0.20.0 on, the panel
installs later releases over Wi-Fi from the release asset
`rcbench-panel-vX.Y.Z.bin`.

---

## 2. The link, before anything else

The panel runs the CAN echo self-test itself, at every start-up, for 1200 ms
inside the splash. It answers one question: do frames cross the bus intact? It
uses no page protocol, so if it passes and the link still does not work, the
fault is above the wire.

**Good looks like nothing:** the splash shows `LINK  OK  CAN 1 Mbit/s` and
hands over to the menu.

**A failure is a screen**, before the menu, with the verdict, what to check in
order, and both ends' counters. It takes a two-second hold to leave.
[Bringing up the link](Bringup.md#the-bus-fault-screen) has the verdicts and
what each one means.

**Do not go on until it passes.** Every step below assumes frames cross.

The console carries the detail if you want it — the **UART socket**, not
native USB, because GPIO19 and GPIO20 carry both and the multiplexer selects
one:

    I (…) rcbench: CAN self-test: every probe came back intact
    I (…) rcbench:   sent 2024 echoed 2024 corrupt 0 lost 0 stale 0

---

## 3. The heartbeat wire — the thing that has blocked everything

| | |
|---|---|
| Panel end | **GPIO6**, on **J8** (a three-pin header carrying 3V3, GND, GPIO6) |
| Coprocessor end | **GP3** |
| Through | the retriggerable monostable, once one exists. It is on no board, so the bring-up bench runs a direct wire and has no hardware backstop. The wire covers a panel that stops beating while the coprocessor is healthy: it switches to SAFE after 150 ms of silence. What is uncovered is a panel that stops beating while the coprocessor cannot act -- nothing then removes the outputs, and that is what the monostable would do without any firmware |

Without this wire the coprocessor refuses every switch to ARMED, and that is the interlock
working, not a fault.

**Numbers it is judged against:**

| | |
|---|---|
| Edge period | 20 ms |
| Gap accepted | 4 ms to 150 ms |
| Edges before it counts as alive | 4 |

So the line must be edging for roughly **80 ms** before a switch to ARMED can succeed.

**On the scope, at GP3:** a square wave, edge to edge 20 ms. If the gap ever
exceeds 150 ms the coprocessor drops it and the next switch to ARMED is refused.

**Where the edges come from:** the panel's control task, inside its poll loop.
Anything that stalls that task stops the heartbeat — which is why nothing
long is allowed to run there.

---

## 4. First link-up: the board drawing

The coprocessor carries a 1,560-byte drawing of its own board. The panel
fetches it at the first link-up and keeps it in flash.

On the panel console, in this order:

    I (…) rcbench: coprocessor answered
    I (…) rcbench: hardware 1 says where its pads are
    I (…) rcbench: hardware 1 says which pads are grounds and rails
    I (…) rcbench: fetching hardware 1's drawing: 1560 bytes, hash 8f99cd48
    I (…) rcbench: hardware 1's drawing kept

**What to expect while it runs:**

- 26 blocks of 62 bytes over CAN, in a 15 ms slice of each 50 ms poll.
- **A display stall** when it writes to flash: one per flash operation, 4
  for this drawing (2 sector erases of 4 kB, the payload, the header). A
  flash operation closes the cache the panel's bounce-buffer refill reads
  PSRAM through, so the panel stops for the length of each. An operation
  that holds the refill off for more than 512 us restarts the scan at the
  first VBlank (vertical blanking interval) interrupt after it ends, which
  can be mid-frame, and the console prints `display: scan restarted`; the
  first whole frame after it is in place.
  Settings saves do the same.
- **The write waits for a safe bench.** It takes one step per 5 ms control
  pass, and only while the bench is SAFE with the supply off. ARMED or with
  the supply on, `keeping hardware 1's drawing waits for a bench in SAFE
  with the supply off` appears once on the console and `kept` follows after
  the switch to SAFE.
- **It must not disturb the heartbeat.** The transfer is sliced and the flash
  write is held back while ARMED for that reason. If the heartbeat drops
  during the fetch, stop and write it down.

On the next link-up the panel reads the drawing's length, CRC (cyclic
redundancy check) and hash from the coprocessor and shows the kept copy
when all three match:

    I (…) rcbench: hardware 1's drawing is kept: 1560 bytes, hash 8f99cd48

A coprocessor flashed with another drawing reports another hash, and the
panel fetches the drawing again. A coprocessor on protocol 4.19 or older
serves no drawing; the panel logs `hardware 1 serves no board drawing
(protocol 4.19); PICK A PIN draws its shape` and draws the board from its
outline.

---

## 5. Arm with nothing connected

Nothing is wired to an output yet. This step tests the interlock, not a pin.

1. **Heartbeat wire removed** → switching to ARMED must be **refused**. The coprocessor
   answers `NOT_ARMED` because `!beat.alive`. The panel shows the refusal.
2. **Heartbeat wire fitted, panel ARMED** → the coprocessor accepts.
3. **Pull the heartbeat wire while ARMED** → it must fail safe within
   **150 ms**.
4. **Press STOP** → latches. It must take an explicit ARM to leave, not a
   link that recovers.
5. **Pull the CAN wire while ARMED** → the coprocessor gives up after
   **200 ms**; the panel escalates after **1 s**.
6. **Cover the touch panel / let touch die** → after **500 ms** of silence
   switching to ARMED is blocked.

Every one of these is host-tested. **None of the numbers below has been seen on an instrument:** no scope or analyser has read any pulse width, frame period or reply delay in this tree.

---

## 6. First pin on an instrument

Use a **servo**, not the ESC. A servo is the forgiving case and the one the
scope reads most easily.

**Pins free for an output:** GP0, GP1, GP2, GP4, GP5, GP6, GP7, GP13 to GP22
and GP26 to GP28. GP13 is the programming port's pin on a coprocessor that
has stored none and is refused for an output until the port's pin is moved
([Link](Link.md#the-programming-ports)).
**Reserved and refused:** GP3 (heartbeat), GP8–GP12 (CAN), GP23, GP24, GP25 and
GP29 (used by the module, not on the header) and GP30 and up (not on the part).
**Refused as a second PWM pin:** a pin whose PWM compare register is already
taken by a bound pin. On the RP2350 the slice is `(pin / 2) modulo 8` below
GP32 and the channel is the pin's low bit, so GP0 and GP16, GP1 and GP17, GP2
and GP18, GP4 and GP20, GP5 and GP21, GP6 and GP22 are pairs that share one
compare register. The second of a pair is refused rather than muxed onto the
first one's pulse width. The OUTPUTS page carries no bit saying "bound", so
the screen goes on looking configured while the lead produces no pulse.

On the panel: **Setup → OUTPUTS**, choose `SERVO PWM`, tick one pin. Or
**Setup → PICK A PIN**, choose `SERVO PWM` in its list and tap one key on the
board drawing — grounds are marked `G`, rails carry their voltage.

**Scope the pin before connecting the servo.**

| | |
|---|---|
| Frame rate | 40 to 560 Hz (50 Hz by default) |
| Pulse | 400 to 2500 µs, refused outside |
| Resolution | 1 µs |

**Measure and record:** the actual frame period, the pulse at both ends of
travel, and the jitter. Bit timings are the largest unverified surface in the
tree.

**Then:** confirm the servo screen lets go when it is told to, and only
then. Lifting the finger does not stop the output: the screen holds the
position it was given and says it again every **100 ms** (`SERVO_HOLD_MS`),
against the coprocessor's **500 ms** (`OUT_DEFAULT_TIMEOUT_MS`), so a servo
stays where it was put. **RELEASE** returns the surfaces to centre; it does
not clear the slot and the pin keeps pulsing. What stops the edges is a
switch to SAFE, a STOP, or leaving the screen, which switches to SAFE. Check on the scope that
RELEASE moves the pulse to the middle of the channel's travel and that a
switch to SAFE is what stops it.

A channel nobody is refreshing does still go to rest after 500 ms, which is
what happens to a bound pin the servo screen is not holding.

---

## 7. The numbers nobody has measured

Written from the specification, exercised only against frames the same code
builds. Record real values for each:

- **Every DShot bit timing**, against an ESC's tolerance rather than the spec.
- **The reply rate** of five quarters of the DShot rate.
- **The leading-bit convention** of the group code.
- **The turnaround delay** — whether 30 µs is what an ESC actually waits.
- **The extended-telemetry frame types** and their units.
- **The PPM DMA ring** playing a frame.
- **The flash erase window** on the coprocessor, and whether the heartbeat
  re-acquires across a save.

---

## 8. Current monitors (INA228, INA3221)

The coprocessor's core 1 reads an INA228 in the ESC's power path and an
INA3221 on the servo rail over I2C (Inter-Integrated Circuit) at 400 kHz,
every 1 ms. Every step below says what good
looks like; anything else is a finding to write down.

**Have to hand:** the MATEK I2C-INA-BM (INA228, 200 µΩ) at the address it
ships with, the DAOKAI INA3221 (0.1 Ω per channel, 1.638 A full scale) with
its A0 solder bridge closed to GND for 0x40 (as shipped all four A0 bridges
are open and the address floats between 0x40 and 0x41), two
2.2 kΩ resistors, a multimeter, a scope or logic analyser with I2C decode,
a servo, and the ESC and motor or a resistive load on a current-limited
supply.

**Set it up on the panel.** SETUP → INTERFACES: `INA228` and `INA3221` ON.
The other sensor settings default to the modules above: INA228
at 0x45 with 200 µΩ, INA3221 at 0x40 with 0.1 Ω, SDA on GP16 and SCL on
GP17. Without a panel, the build option `IOMCU_SENSE_BRINGUP=ON` enables both
parts at the page's defaults while no set-up is stored in flash:

```bash
cmake -S firmware/iomcu -B firmware/iomcu/build-sense -DIOMCU_SENSE_BRINGUP=ON
cmake --build firmware/iomcu/build-sense
```

**Wire it** (all coprocessor pads on the RP2350-CAN module):

| Wire | From | To |
|---|---|---|
| SDA | GP16 (pad 21) | INA3221 SDA, MATEK SDA |
| SCL | GP17 (pad 22) | INA3221 SCL, MATEK SCL |
| Pull-ups | 2.2 kΩ from SDA and from SCL | 3V3 (pad 36) |
| INA3221 power | 3V3 (pad 36), GND | INA3221 VS, GND |
| MATEK power | VBUS (pad 40, 5 V from USB), GND | MATEK 5V, G |

The firmware switches the pads' own pull-ups and pull-downs off on both
pins: the bus runs on the modules' pull-ups and the two 2.2 kΩ. **Before
connecting the coprocessor**, power the modules and measure SDA and SCL to
GND: both must read 3.3 V or less. The MATEK's pull-up rail is not known.

**The console.** Every 3 s, while a part is enabled, the coprocessor prints
two lines:

```
rcbench-iomcu: sense flags 0x0133 present 0x0021 ids 0x2281 0x3220 errors 0 | bench 1680 cV 1200 cA 33 mAh 6 dWh flags 0x63 | temp 254 dC
rcbench-iomcu: sense window 4711 CH1 120 mA 6000 mV ch_flags 0x01 | capture state 0 seq 0 move 0 arrive 0
```

`flags` is the SENSE page's FLAGS: 0x0001 INA228 online, 0x0002 its
identity read was an INA228's, 0x0004 something else answers at its
address, 0x0008 a current clipped, 0x0010, 0x0020 and 0x0040 the same for
the INA3221, 0x0100 the bus is open, 0x0200 the bus is stuck. `present`
has bit n for address 0x40 + n. `bench` is the BENCH page: 10 mV and
10 mA steps, charge in mAh, energy in 0.1 Wh, and its flags (0x01
voltage, 0x02 current, 0x20 the INA228's, 0x40 the INA228's totals).

### 8.1 The parts answer

Power everything, nothing ARMED.

**Good:** `flags 0x0133`, `present 0x0021`, `ids 0x228x 0x3220`, `errors`
not rising between two lines.

**Write down:** the two IDs as printed. An INA238 on the MATEK answers
0x238x and is refused (flag 0x0004). A DAOKAI unit that answers 0x1408
(one buyer's reading) is refused the same way (0x0040).

### 8.2 The bus on a scope

Probe SDA and SCL at the coprocessor.

**Good:** SCL at 400 kHz (2.5 µs a clock); rise time, 30 % to 70 %, at most
300 ns; a read of the INA3221's register 0x01 every 1.0 ms; the INA228's
registers 0x07 and 0x05 in turn, one each 1.0 ms; no NACK in the decode.

**Write down:** the rise time, the clock, the time from one transaction's
STOP to the next START (the controller's own time, not measured), and the
period of the CH1 read.

### 8.3 Readings against a meter

Run a steady current through the MATEK's shunt and measure it and the
pack voltage with the meter. Then a servo, or a resistor drawing under
1.6 A, on INA3221 CH1, with the meter in series.

**Good:** `bench` voltage and current, and `CH1` mA and mV (CH1's voltage
is at the load side of the shunt), agree with the meter within the meter's
accuracy and the shunt's tolerance, which is not known for either module.
`temp` near room temperature, in 0.1 °C. `ch_flags 0x01`; 0x10 appears
when CH1 passes 1.638 A, and CH1's current is then a bound, not a value.

**Write down:** each pair of numbers, the meter's and the console's.

### 8.4 A run: peaks and totals

Press ARM on MOTOR & ESC and hold a steady current for 60 s, then switch to SAFE.

**Good:** `bench flags` reads 0x63 (with 0x04 and 0x08 too when a
bidirectional ESC answers). `mAh` counts current × time: 2.00 A for 60 s
is 33 mAh. A new switch to ARMED starts charge and energy from 0.
The MOTOR & ESC screen shows the INA228's voltage and current.

**Write down:** current, time and the charge reached, and the peaks the
panel shows against the scope or meter.

### 8.5 A lead pulled mid-run

While ARMED, pull the MATEK's SDA for about 5 s, then plug it back.

**Good:** within about 6 ms `flags` loses 0x0001 and keeps 0x0002; `bench
flags` reads 0x20: the INA228 is still the source, its fields empty, not
the ESC's numbers; `errors` rises; the bench stays ARMED. About 1 s after
the lead is back, 0x0001 returns, and 0x40 stays clear until the next switch to ARMED.
With the bench SAFE and the lead out, `bench` carries the ESC's telemetry again.

### 8.6 A stuck bus

Hold SDA to GND with a wire for 2 s, then let go. The lines are open drain:
the pin is never driven high, so the short is safe.

**Good:** `flags` gains 0x0200 while it is held; on SCL the scope shows 9
clocks and a STOP every 100 ms; after release, 0x0200 clears and both
parts are online within about 1 s.

**Write down:** the clock rate of the 9 clocks (meant to be 100 kHz), and
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

The move capture on SERVO_SENSE -- the PWM frame stamped to 1 µs and the
travel times -- is armed by the panel's servo test, which does not use it
yet. A travel time against a scope on the PWM pin and on the shunt waits
for that.

### 8.9 CH1's 1 ms samples on the console

A coprocessor image built with `-DSENSE_TRACE=ON` prints INA3221 CH1's
1 ms samples on its USB (Universal Serial Bus) console as text. The link
carries 50 ms windows only, so this build is the one way to see the single
samples. It is a debug build: a released image is built without the option
and holds none of its code. `tools/sense_trace.py` reads the captured
console. The bench session has two
parts: part A needs no encoder, part B repeats the moves with an AS5600
on the servo's shaft.

**What starts a trace.**

| Trigger | Trace |
| --- | --- |
| a PWM (pulse-width modulation) output renders another pulse width than in the pass before, 50 ms or more after that output's last change | 4 s from the frame that carries the pulse; trigger line `$C` |
| `t` typed on the console | 10 s; trigger line `$K` |
| the capture's PWM edge | 4 s; trigger line `$E`. No released panel arms the capture, so this trigger does not occur with one |

Every MOVE of the automatic servo test is a changed pulse, so a test with
the released panel is traced without a capture. A trigger during a trace
adds its line and moves the end to 4 s (10 s for `t`) after itself when
that is later: moves less than 4 s apart are one trace. A command that is
slewed changes the pulse in every pass: its `$C` line has the first pulse,
and a `$D` line the pulse it ended at, once the output has held still for
50 ms. The `$D` line's `cmd=` is its command's `$C` time, and the tool
ends that command there, also when the output was let go and the next
command came within 30 ms. A command that waits for the next trace takes its later changes
with it: that trace ends 4 s after the last of them and has the `$D`
line. An output that is let go ends a slewed command there; the next
pulse on it is a command, however soon it comes. The automatic test's
moves are steps and have no `$D` line. `x` on the console
ends a trace; a trigger line whose time lies at or after the stop is
dropped. Each trace also holds the samples of up to 64 ms before its
trigger.

**What it costs.** Core 1 reads CH1 every 1 ms as in the released image
and copies the sample into a ring of 4096 records (49,152 bytes; 3.9 s of
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
is written once the output's pulse has stood still for 50 ms, so it follows
samples up to about 50 ms later than its `t=`; its `t=` is its time. Core 0 writes a trigger line and
the end line once core 1 has finished a tick that started at or after the
line's time. The end line's `e=` is the reason the trace ended: `t` its
time, `s` a changed set-up, `k` the console. A capital letter (`T`, `S`,
`K`) says that core 1 was not seen past a trigger line's time or the end
within 100 ms and the line was written all the same: a sample from before
that time can be missing or stand after the line. The tool reports such a
trace as a problem and replays none of its moves.

A sample line is the time since the sample before it in 0.1 ms and the
shunt code, 40 µV a step: 0.4 mA on the 0.1 Ω shunt. A sample line 1 ms
after the sample before it is 8 bytes with a 3-digit code and 10 bytes at
most with a 4-digit code. The first sample line of a trace counts from the
trigger time, so a sample from before the trigger carries a negative time,
and a sample after dropped records carries a longer one: those lines are
longer, up to the full signed 32-bit range of the time. With the
voltage lines (a 7-byte line, one in 20 samples in the output of
`sense_trace_gen`) a trace is 8.35 bytes a sample, 8.35 kB a second, at the
simulated servo's 0.12 A, whose codes have 3 digits. Above about 0.4 A on the
0.1 Ω shunt the codes have 4 digits and the rate is 9.35 kB a second. Every
line ends in CR LF. The rate on hardware is not measured.

**Have to hand:** the PD mini (WeAct PD Power Mini V1) at a limit of
2.00 A, the INA3221 module with CH1's 0.1 Ω shunt in the servo's supply,
the servos to measure, and a Windows PC with PuTTY. Part A needs no
encoder. Part B needs an AS5600 on the servo's shaft.

**The terminal.** The console is the coprocessor module's own USB-C
socket, the one the image is flashed through, not the panel's. In the
Windows Device Manager it is under "Ports (COM & LPT)" as "USB Serial
Device (COMn)", hardware ID `VID_2E8A`. PuTTY settings:

| Where | Setting |
| --- | --- |
| Session | Connection type Serial, Serial line `COMn`, Speed 115200 |
| Connection, Serial | Data bits 8, Stop bits 1, Parity None, Flow control None |
| Session, Logging | "All session output", a new log file name for each run |

The port is USB CDC (communications device class): the speed is not used,
any value works. The coprocessor prints only while the terminal holds DTR
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
3. Open the port in PuTTY with logging on. Within 3 s it prints a line
   starting `rcbench-iomcu:`. On the panel, SETUP → INTERFACES: `INA3221`
   ON.
4. Noise, no servo: with no servo connected and the supply's output on at
   6.00 V, press `t`. 10 s later the console prints a line starting `$Z`.
   Close PuTTY; this log is `noise.log`.
5. Servo at rest: new log file. Connect the servo, press ARM, leave it at the
   centre, supply on at 4.80 V. Press `t` and wait for the `$Z` line.
6. Moves, in the same log: on the SERVO screen's TEST page set TEST LENGTH BY
   to MOVEMENTS, MOVEMENTS 20, DWELL TIME 1000 ms, STEP 4.8 V on and every
   other step and BROWN-OUT off. START TEST. The trace runs from the first
   move to 4 s after the last; wait for its `$Z` line, then close PuTTY.
7. Repeat steps 5 and 6 at 6.00 V with STEP 6.0 V, and both for each
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
band at 0.02, 0.05 and 0.10 A: seen or not, and the arrival in ms from the
frame. For the log as a whole: the settings that see every move and time
its arrival, each setting's median arrival and its distance from the
capture's setting (filter 4, band 0.05 A), and how far apart the settings
put one move's arrival. It ends `not compared with the horn`: without an
encoder an arrival is the current back at its holding level, which can
lead or lag the horn. One `<log>-trace-<n>.csv` per trace holds four
columns, separated by `;`: time in ms, current in A, the bus voltage in V
on the rows that have one, and `clipped`. `clipped` is 1 for a sample at
or past the top of the range, -1 at or past the bottom, and 0 for a value.
A clipped row's current is the end of the range, a bound and not a value:
1.6380 A or -1.6384 A on a 0.1 Ω shunt. A file with three columns has no
clip mark, and its clipped rows read as values.

**What the tool refuses.** Between a trace's `$T` and `$Z` line every
line is a whole line of the format or a console line of the coprocessor's
own, which starts `rcbench-iomcu:`. A line is whole with every field of
its type in its order, nothing before, between or after the fields, and
each number in the range the coprocessor holds it in: a `$C` or `$D` line
has `ch=` and `us=`, a `$D` line `cmd=` after them, a `$E` or `$K` line
has neither. The tool reads format versions 3 and 2 (`$T v=`). A version 2
`$D` line has no `cmd=`, and the tool ends with it the latest command of
its channel no more than 30 ms after its `t=`: a command that follows a
let-go output within 30 ms takes that `$D` line as its own. A `$D` line of
the other version's form is damage. A line ends LF or
CR LF. Anything else is damage. So are a second `$H` line, a line other
than `$L` before the `$H` line, a trace length other than 4000 ms
(10000 ms for `t`), a sample period other than 1000 µs, a sample timed
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
| the 50 ms before the move | its level before |
| the 200 ms before the move | its threshold; the holding level of each later move back to the pulse width this move left |
| from 50 ms before the move to the next move or the trace's end | the samples the replay is fed |

A window with no sample, with a `$L` line in it or at either end, or with
a `$S` line in it or at either end gives no level. Each move that needs
that level reads `not replayed`, with the window named. A clipped sample
in a window counts as 4094 steps towards that end of the range, as in the
replay, and the move's line says how many of its level samples are
clipped.

**More than one output.** `ch=` in a `$C` or `$D` line is the output
channel, not the pin: the channels count from 0 over the bound pins in
ascending pin order, 1 channel per PWM pin. With GP0, GP1, GP2 and GP4
bound, GP2 is channel 2 and GP4 is channel 3. The shunt carries the
current of every servo on the supply, and the SERVO screen commands every
servo output in the same frame. The tool replays no command that has
another command within 30 ms of it, so with 2 servo outputs bound it
replays no move. `--loaded-channel N` names a channel with a servo
connected to its pin; the option repeats, 1 channel each:

```bash
python3 tools/sense_trace.py mg90s-4v8.log --loaded-channel 2
```

The `$C` and `$D` lines of every other channel are then no moves and
count for no 30 ms. A `$E` line has no channel: the commands of the named
channels take their edge lines first, and an edge line left within 30 ms
of another channel's command is left out. The report's second line names
the channels and counts what is left out:

```text
loaded channel(s) 2: 25 command line(s) of channel(s) 1, 3 left out
```

It ends `no command line of channel(s) N` for a named channel that no `$C`
line of the log has; the exit code stays 0. Two named channels commanded
within 30 ms of each other are not replayed. Without the option every
channel counts. The tool does not check that a named channel's pin has a
servo on it: a servo on a channel that is not named moves in the replayed
current.

`--floor` takes 0.000001 A to 1000 A, `--pair-ms` more than 0 ms up to
60000 ms, `--csv-offset` up to 1e9 s either side of 0, `--loaded-channel`
a whole number from 0 to 65535 with no sign and no leading zero; any
other value exits 2. A servo CSV file
that cannot be read, a CSV file that cannot be written and a replay that
answers another number of moves than it was sent exit 2.

**Write down:** the tool's output for each log. Part A answers: the noise
of CH1's 1 ms samples with no servo and with a servo at rest, against the
threshold's floor (`SERVO_MOVE_MIN_A`, 0.020 A); which filter lengths and
bands see all 22 moves of each test (2 that place the servo and 20
counted); and how much the arrival moves with the setting. It does not
answer whether an arrival is the horn's.

**Part B: with the AS5600.** The same moves with the encoder on the shaft.

1. SETUP → INTERFACES: `AS5600` ON, with the bank SAFE. On the SERVO
   screen's DUT page, with the servo at its neutral, tap ENC CENTRE.
2. Repeat part A's steps 5 to 7.
3. Keep each log together with its test's `SERVOnnn.CSV` from the SD card.
4. On the host, for each pair:

   ```bash
   python3 tools/sense_trace.py mg90s-4v8.log --servo-csv SERVO012.CSV
   ```

The tool then also prints, per move, the arrival less the encoder's
`travel angle (ms)`, and its median per setting. Part B answers what the
capture's filter length (`SENSE_CAP_FILTER_N`, 4) and arrival band
(`SERVO_MOVE_BAND_A`, 0.05 A) are to be: both are chosen, not measured.

**Not known:**

- Part B: the encoder's travel time counts from the command as the panel
  issues it, the trace's arrival from the PWM frame at the pin. The
  difference the tool prints contains the time between the two: up to one
  poll and one frame, not measured.
- A command's frame time is computed from the PWM counter read after the
  pulse is written. A frame that ends between the two puts that one
  command's time one frame (20 ms at 50 Hz) late. How often: not measured.
- Part B: the two files have different clocks. The tool pairs rows and commands by
  the spacing of the moves; when two offsets pair equally many rows, and
  not the same rows with the same moves, it says so, exits 1 and takes
  `--csv-offset`.
- A log with a restart of the coprocessor in it has one clock per boot.
  The tool takes a `$T` line for a restart when its millisecond tick lies
  before the last one's or its trace number is not past the last one's;
  missing numbers are traces no console took, not a restart. A restart
  after which neither goes back is not told.
  The tool pairs each boot's moves on their own. When the rows of a later
  boot do not lie after those of the boot before it, it pairs no row, says
  so and exits 1: evaluate each boot's part of the log with its part of
  the CSV file.
- Commands within 30 ms of each other, as two outputs written in one
  frame, change the current together. The tool lists them and replays
  none of them, unless `--loaded-channel` leaves all but one of them
  out. A command that follows another output's command later
  than that, before the first servo arrived, is replayed with the first
  servo's current in it; the tool does not tell the two apart.
- Whether a terminal keeps up with 8.35 to 9.35 kB a second without loss. A trace
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
| Switch to ARMED refused, wire fitted | Heartbeat not edging 4 times yet, or gap over 150 ms. Scope GP3. |
| Switch to ARMED refused, no obvious reason | Touch dead for 500 ms blocks it. Touch the panel. |
| Link up, screen offers no pins | The board's catalogue did not arrive. Console says which page failed. |
| Heartbeat drops at the first link-up | **The drawing transfer or the flash keeper.** See §4. |
| Display freezes briefly | A flash write. Expected while the board drawing is kept, once per flash operation, 4 for the RP2350-CAN's drawing. |
| Coprocessor does not boot after flashing | Image past 4 MB. Check the size line from §1. |
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

The first two are inert on a coprocessor that reports no drawing, which is
the quickest way to rule them out. The third is inert with no card in the
slot. With a card it appends a line to `RCBENCH.LOG` whenever a fault is
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
