# Touch injector

The touch injector is a Waveshare RP2350-Zero on the panel's touch bus.
It emulates a GT911 touch controller at I2C (Inter-Integrated Circuit)
address 0x14 and presses where a USB (Universal Serial Bus) serial command
tells it to. A panel image polls 0x14
beside the glass's controller and merges both when 0x14 answers at
start-up; `RCBENCH_TOUCH_INJECTOR` defaults to ON, and a panel built with
`-D RCBENCH_TOUCH_INJECTOR=OFF` never reads 0x14 as an injector. A panel
that finds nothing at 0x14 at start-up does not look again until its next
start.

The glass's GT911 takes 0x5D or 0x14 at the end of its reset, from the
level on its INT line; on the 7-inch ESP32-P4 board no GPIO drives that
line, and which address it takes is not measured. A panel built with the
injector accepts the glass at 0x5D only: a controller that answers only at
0x14 is taken as the injector, and the console says
`0x14 refused as the glass`.

It is a test-bench tool. No release carries its UF2 (USB Flashing Format)
image.

## Pins

| RP2350-Zero | Connects to | Notes |
| --- | --- | --- |
| GP4 | panel I2C SDA (connector pin 3) | I2C0 SDA once confirmed; an input before |
| GP5 | panel I2C SCL (connector pin 4) | I2C0 SCL once confirmed; an input before |
| GND | the bench's star point | short wire; the one second ground path the bench accepts (`testbench/WIRING.md`, section 5) |
| GP6 | not connected | reserved for the GT911 INT line; an input, never driven |
| GP7 | not connected | reserved for a panel reset sense; an input |
| GP16 | on-board WS2812 LED, bytes in RGB order | status, see below |
| 3V3 | not connected | the Zero runs from its own USB port; the bus has its pull-ups on the panel |

The connector is the panel's 4-pin PH2.0 I2C connector, pinned out in
`docs/Building.md`: 1 I2C_VCC, 2 GND, 3 SDA, 4 SCL. The injector has no pull-ups and enables none.

## Build

Needs pico-sdk 2.3.0, an `arm-none-eabi` GCC, CMake 3.13 or later and Ninja.
The project builds on its own from this directory; it uses nothing else of
the repository.

On Debian 13 (the bench Raspberry Pi 5, aarch64):

```bash
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib cmake ninja-build

cd testbench/touch-injector
export PICO_SDK_PATH=~/pico-sdk-2.3.0
# Optional: a picotool 2.3.0 install.  pico-sdk 2.3.0 refuses Debian's
# picotool 2.1.1; without this line it fetches picotool 2.3.0 from git and
# builds it on the first configure.
PICOTOOL_DIR=/opt/picotool-2.3.0/lib/cmake/picotool

cmake -S . -B build -G Ninja -DPICO_BOARD=waveshare_rp2350_zero \
    ${PICOTOOL_DIR:+-Dpicotool_DIR=$PICOTOOL_DIR}
cmake --build build
```

Output: `build/rcbench-touch-injector.uf2`, about 75 KiB.

Tested with arm-none-eabi-gcc 14.2.1 (Debian), CMake 3.31.6 and Ninja 1.12.1
on the Pi, and with Arm GNU Toolchain 14.2.rel1 on x86-64. The CI (continuous
integration) workflow builds it on every push.

The pure-C core in `core/` also builds for the host. Its suites are
`test_inj_line_id` and `test_inj_emu` in `test/host`.

## Flash

Hold BOOT, plug the Zero in, release BOOT, then either copy the UF2 to the
`RP2350` drive or run:

```bash
picotool load -f -x build/rcbench-touch-injector.uf2
```

`-f` reboots a running injector into BOOTSEL first; `-x` starts the image
after loading. The port appears as:

```text
/dev/serial/by-id/usb-Raspberry_Pi_rcbench_injector_<serial>-if00
```

The name comes from the USB product string `rcbench injector`. pico-sdk
2.3.0 sends at most 19 characters of it; `rp2350/main.c` fails the build on
a longer one.

## States

After every reset the injector is in LISTEN. GP4 to GP7 are inputs with
their pulls off, the I2C block is not attached, and nothing on the bus is
acknowledged. It leaves LISTEN only on `confirm scl=GP5` after a `wiring`
check that read `ok`. ACTIVE answers at 0x14 until the next reset.

A panel that found the injector at start-up detaches it after 1000 ms
without an answer from 0x14. Its injected
contacts are released, `INJECT` leaves the mode chip, and the glass alone
decides whether touch is alive. The panel then reads the product ID at 0x14
once every 1000 ms; the first answer re-attaches the injector with no
contacts down. An injector reset therefore needs no panel restart, provided the panel found
the injector at start-up: after
`wiring` and `confirm` the panel re-attaches it within about 1 s.

The watchdog resets the chip when the main loop stalls for 200 ms. A reset
is LISTEN again.

## Protocol

One command per line, at most 63 characters, LF or CRLF. Every command gets
one reply line; a gesture gets a second when it ends.

| Command | Reply |
| --- | --- |
| `ping` | `pong rcbench-touch 1 addr=0x14 res=1024x600 state=listen` (or `active`) |
| `wiring` | `ok wiring`, then the verdict about 500 ms later |
| `confirm scl=GP5` | `ok confirm`; the target answers from then on |
| `status` | `status <state> reads= acks= last_read_ms= down= x= y=` |
| `tap x y [ms]` | `ok tap`, then `done tap frames= ms=`; ms defaults to 100 |
| `hold x y ms` | `ok hold`, then `done hold frames= ms=` |
| `swipe x0 y0 x1 y1 ms` | `ok swipe`, then `done swipe frames= ms= slowed=` |
| `release` | `ok release`; a gesture under way first gets `fail <gesture> released` |

Coordinates are panel pixels: x 0 to 1023, y 0 to 599. `ms` is 1 to 30000.
They include the 60 px status band across the top of the 1024 x 600 panel
(`UI_BAND_H`); they are not screen-body coordinates, which start 60 px lower
down. The boot question that offers the IOmcu update has its START button
at panel (719, 376) and its CANCEL button at panel (305, 376). Their body
coordinates, panel y minus 60 px, are (719, 316) and (305, 316), the centres
of the rectangles `Q_START` and `Q_CANCEL` in
`shared/ui/coproc_update_screen.c` (398 x 56 px each). A tap at y 316 lands
at body y 256, inside the question box above the buttons, and misses both.

The panel takes injected points as pixels of its 1024 x 600 UI (user
interface) area, unmirrored, and lands each on the pixel named; a point
past the area is clamped to its edge. The glass's own report (1024 x 600,
mirrored in x and y) is mapped separately before the two are merged.

A tap or a hold is two frames: one puts the contact down, one lifts it at
the end of `ms`. The panel keeps a contact down while the controller has
nothing new to report, as it does for the glass.

A swipe moves at most 100 px (|dx| + |dy|) between two frames, under the
panel's 120 px jump split; `slowed=1` says that limit stretched it.

Errors are one line and the command is not run:

| Reply | Cause |
| --- | --- |
| `err syntax <cmd>` | missing, extra or non-numeric argument |
| `err range <field>` | a coordinate or `ms` out of range |
| `err unknown <cmd>` | no such command |
| `err toolong` | line over 63 characters |
| `err busy` | a gesture or a `wiring` check is under way |
| `err unconfirmed` | a gesture in LISTEN |
| `err state` | `wiring` or `confirm` in ACTIVE |
| `err swapped` | `confirm` after a `swapped` verdict |
| `err noverdict` | `confirm` without an `ok` verdict |

A gesture that fails after its `ok` gets one of:

| Reply | Cause |
| --- | --- |
| `fail <gesture> no_master` | the panel did not read a frame within 100 ms, or stopped reading for 100 ms while a contact was down |
| `fail <gesture> panel_reset` | the panel read the product ID again: it restarted, or it re-attached the injector; the contact is withdrawn |
| `fail <gesture> released` | `release` arrived |

Closing the port lifts a contact that is down, without a reply.

## Wiring check

`wiring` samples GP4 and GP5 for 500 ms and applies two rules to every
transaction: out of idle the line that falls first is SDA (START), into idle
the line that rises last is SDA (STOP). Idle is both lines high for at least
10 µs. A transaction names a line when both rules name the same one.

A verdict needs at least 20 whole transactions that name a line, and at
least half of all whole transactions. `agree=` is the share of the named
ones that name the majority line, and a verdict needs 95 %. A transaction
that names no line is counted in `tx=` and left out of `agree=`. The panel's
poll of 0x14 while the injector listens is one: the address goes
unacknowledged, both lines go high after the ninth clock, and a bare STOP
follows more than 10 µs later. SDA and SCL shorted together name no line at
all and give `noise`.

| Verdict | Meaning | Next step |
| --- | --- | --- |
| `wiring ok scl=GP5 sda=GP4 tx=<n> named=<n> agree=<pct> rate=<Msps>` | wired as in the table | `confirm scl=GP5` |
| `wiring swapped scl=GP4 sda=GP5 ...` | SDA and SCL crossed | swap the two wires, reset, check again |
| `wiring idle gp4=1 gp5=1` | both high, no traffic | the panel is not polling: check that it runs |
| `wiring low gp4=<0/1> gp5=<0/1>` | a line low all window | the 0 names the line: wiring, ground or a held bus |
| `wiring noise edges4= edges5= tx= named= agree=` | edges, no verdict | too few named transactions, or noise |

A level shorter than 2 samples is dropped as a glitch before the rules run.
The host suite checks the rules at 10 Msps (mega-samples per second) with
400 kHz traffic and at 4 Msps with 100 kHz traffic. The rate the RP2350
reaches is not measured; the verdict line reports it as `rate=`.

## Bring-up on the bench

1. Wire GP4, GP5 and GND as in the pin table. Leave 3V3, GP6 and GP7 open.
2. Power the panel and let it run. Plug the Zero into the Pi.
3. `ping` returns `state=listen`. The LED blinks white at 1 Hz.
4. `wiring` returns `wiring ok scl=GP5 sda=GP4 ...`. Any other verdict: fix
   the wiring first; the LED is yellow for `swapped`, `low` and `noise`.
5. `confirm scl=GP5`. The LED goes dim green once the panel reads 0x14.
6. Reset the panel. The panel looks once, at start-up; it
   finds 0x14 at start and shows `INJECT` in the status band's mode chip.
   After an injector reset, repeat steps 3 to 5 only: the panel (which found
   the injector at start-up) detached the
   silent injector after 1000 ms and re-attaches it within about 1 s of
   `confirm`, and `INJECT` shows again.
7. `tap 83 370` presses the SETUP tile. The reply is
   `done tap frames=2 ms=<100 to 110>`.

The IOmcu's update question, which opens on the menu when the IOmcu runs
another build, has CANCEL at panel (305, 376) and START at (719, 376).
`tap 305 376` declines it; `hold 719 376 2500` starts the update after the
2 s hold.

## LED

| Colour | State |
| --- | --- |
| white, blinking 1 Hz | LISTEN |
| yellow | LISTEN, last verdict `swapped`, `low` or `noise` |
| off | ACTIVE, no read for 100 ms |
| dim green | ACTIVE, the panel reads 0x14 |
| blue | a contact is down |
| red | the last gesture failed |

## Timing

| Item | Value |
| --- | --- |
| frame interval | at least 10 ms |
| frame acknowledge timeout | 100 ms |
| default tap | 100 ms |
| longest gesture | 30 000 ms |
| reply queue | 8 lines; when full the oldest is dropped |
| watchdog | 200 ms |
| USB write timeout | 10 ms per line |

Licence: MIT (`SPDX-License-Identifier: MIT` in every source file).
