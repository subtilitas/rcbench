# Touch injector

The touch injector is a Waveshare RP2350-Zero on the panel's touch bus.
It emulates a GT911 touch controller at I2C (Inter-Integrated Circuit)
address 0x14 and presses where a USB (Universal Serial Bus) serial command
tells it to. A panel image polls 0x14
beside the glass's controller and merges both when 0x14 answers at
start-up with the injector's product ID; `RCBENCH_TOUCH_INJECTOR` defaults to ON, and a panel built with
`-D RCBENCH_TOUCH_INJECTOR=OFF` never reads 0x14 as an injector. A panel
that finds nothing at 0x14 at start-up does not look again until its next
start.

The panel tells the injector from the glass by its product ID, not by its
address. The injector reports `9INJ` at registers 0x8140 to 0x8143
(`GT911_EMU_PRODUCT_ID`). A Goodix controller reports its part number
there, digits with at most one trailing letter: `911` and a NUL on the
GT911 this board's glass carries, `9271`, `917S`. `9INJ` has letters from
its second byte on, so no glass reports it.

The glass's GT911 takes 0x5D or 0x14 at the end of its reset, from the
level on its INT line; on the 7-inch ESP32-P4 board no GPIO drives that
line, and which address it takes is not measured. A panel built with the
injector reads what it finds as follows (`touch_merge_role()`):

| 0x5D | 0x14 | Touch is | Console |
| --- | --- | --- | --- |
| glass | `9INJ` | the glass and the injector, merged; `INJECT` on the band | `touch injector attached at 0x14` |
| glass | another GT911 | the glass alone; 0x14 is not read | `0x14 answers as a GT911 with product ID "...", not the touch injector's 9INJ: not read` |
| glass | nothing | the glass alone | `no injector at 0x14` |
| nothing | `9INJ` | the injector alone, unmirrored and never turned; `INJECT` on the band | `0x14 reports product ID 9INJ: touch is the touch injector alone, unmirrored and never turned` |
| nothing | another GT911 | the glass at 0x14, with its mirrors, rotation and the 180° turn; no injector | `0x14 reports product ID "911", not the touch injector's 9INJ: read as the glass` |

When both answer, 0x5D is the glass, whatever it reports. An injector
image without `9INJ`, one whose `ping` reply has no `id=`, reports `911`:
beside the glass it is not read, and alone it is read as the glass,
mirrored in both axes. Flash the current image.

It is a test-bench tool. No release carries its UF2 (USB Flashing Format)
image.

## Pins

| RP2350-Zero | Connects to | Notes |
| --- | --- | --- |
| GP4 | panel I2C SDA (the header's SDA pin) | I2C0 SDA once confirmed; an input before |
| GP5 | panel I2C SCL (the header's SCL pin) | I2C0 SCL once confirmed; an input before |
| GND | the bench's star point | short wire; the one second ground path the bench accepts (`testbench/WIRING.md`, section 5) |
| GP6 | not connected | reserved for the GT911 INT line; an input, never driven |
| GP7 | not connected | reserved for a panel reset sense; an input |
| GP16 | on-board WS2812 LED, bytes in RGB order | status, see below |
| 3V3 | not connected | the Zero runs from its own USB port |

The connector is the board's 4-pin I2C header ("The touch bus header" in
`docs/Building.md`); the order of its pins is not known, so SDA and SCL are
found with a meter before a lead goes on. The injector has no pull-ups and
enables none. Whether the board's bus has pull-ups of its own is not measured.

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

Output: `build/rcbench-touch-injector.uf2`, about 79KiB.

Tested with arm-none-eabi-gcc 14.2.1 (Debian), CMake 3.31.6 and Ninja 1.12.1
on the Pi, and with Arm GNU Toolchain 14.2.rel1 on x86-64. The CI (continuous
integration) workflow builds it on every push.

The pure-C core in `core/` also builds for the host. Its suites are
`test_inj_line_id`, `test_inj_emu` and `test_inj_multi` in `test/host`.

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

A panel that found the injector at start-up detaches it after 1000ms
without an answer from 0x14. Its injected
contacts are released, `INJECT` leaves the mode chip, and the glass alone
decides whether touch is alive. The panel then reads the product ID at 0x14
once every 1000ms; the first `9INJ` re-attaches the injector with no
contacts down. An injector reset therefore needs no panel restart, provided the panel found
the injector at start-up: after
`wiring` and `confirm` the panel re-attaches it within about 1s.

The watchdog resets the chip when the main loop stalls for 200ms. A reset
is LISTEN again.

## Protocol

One command per line, at most 63 characters, LF or CRLF. Every command gets
one reply line; a gesture gets a second when it ends.

| Command | Reply |
| --- | --- |
| `ping` | `pong rcbench-touch 1 addr=0x14 res=1024x600 state=listen id=9INJ` (or `active`); `id=` is the product ID the panel identifies the injector by, and an image that predates it sends no `id=` |
| `wiring` | `ok wiring`, then the verdict about 500ms later |
| `confirm scl=GP5` | `ok confirm`; the target answers from then on |
| `status` | `status <state> reads= acks= last_read_ms= down= x= y=`; state, down, x and y are contact 0's |
| `contacts` | `contacts n=<k>`, then `<id>=<gesture>,<down>,<x>,<y>` for each contact id in use, lowest first |
| `tap x y [ms] [id=N]` | `ok tap`, then `done tap frames= ms=`; ms defaults to 100 |
| `hold x y ms [id=N]` | `ok hold`, then `done hold frames= ms=` |
| `swipe x0 y0 x1 y1 ms [id=N]` | `ok swipe`, then `done swipe frames= ms= slowed=` |
| `release [id=N]` | `ok release`; each gesture under way first gets `fail <gesture> released` |

Coordinates are panel pixels: x 0 to 1023, y 0 to 599, the 60px status
band at y 0 to 59 included. `ms` is 1 to 30000. They are not screen-body
coordinates, which start 60px lower down (`UI_BAND_H`). The boot question
that offers the IOmcu update has its START button at panel (719, 376) and
its CANCEL button at panel (305, 376). Their body coordinates, panel y minus
60px, are (719, 316) and (305, 316), the centres of the rectangles `Q_START`
and `Q_CANCEL` in `shared/ui/coproc_update_screen.c` (398x56px each). A tap
at y 316 lands at body y 256, inside the question box above the buttons, and
misses both.

The panel takes injected points as pixels of its 1024 x 600 UI (user
interface) area, unmirrored, and lands each on the pixel named; a point past
the area is clamped to its edge. The glass's own report (1024 x 600,
mirrored in x and y) is mapped separately before the two are merged.

A tap or a hold is two frames: one puts the contact down, one lifts it at
the end of `ms`. The panel keeps a contact down while the controller has
nothing new to report, as it does for the glass.

A swipe moves at most 100px (|dx| + |dy|) between two frames, under the
panel's 120px jump split; `slowed=1` says that limit stretched it.

### Contacts

Up to 5 contacts are down at once, the GT911's 5 points. Each has a
contact id from 0 to 15, sent as its GT911 track ID; the panel reads it as
track id `0x80 + id` (`TOUCH_MERGE_INJECT_ID`), never one of the glass's.

- A command without `id=` drives contact 0. Its replies carry no id, word
  for word as with one contact: `ok tap`, `done tap frames=2 ms=<100 to 110>`,
  `err busy`.
- `id=N` is the last argument. A gesture started with it names its
  contact in every reply about it: `ok hold id=1`,
  `done hold id=1 frames=2 ms=<2000 to 2010>`, `fail hold id=1 released`.
- An id is in use from its `ok` until the panel has read the frame that
  lifts it. A gesture on an id in use gets `err busy`; a sixth id while 5
  are in use gets `err full`.
- `release` without `id=` releases every contact: one `fail` line per
  gesture under way, lowest id first, then `ok release`. `release id=N`
  releases contact N alone and answers `ok release id=N`; the others go on.
- A frame carries every contact that is down, lowest id first. A
  contact's `frames=` counts the frames that put it down, moved it or
  lifted it, not the frames other contacts needed.
- Frames go out at most one per 10ms, each after the panel acknowledged
  the one before. A tap's or a hold's lift that falls due while another
  contact's frame is out goes in the next frame: the host suite holds it to
  20ms after its deadline beside a swipe.
- Lines that arrive in one USB read start in the same frame. The main loop
  reads up to 64 characters before it runs the gestures, so write the
  lines of a pinch in one write of at most 64 characters. A line that
  arrives later starts in a later frame, 10ms or more after.
- `no_master` and `panel_reset` end every contact, each gesture under way
  with its own `fail` line.
- `ping` reports protocol 1 as before. An injector built before contacts
  answers `contacts` with `err unknown contacts`.

```text
swipe 495 258 495 368 4000       -> ok swipe
tap 826 514 id=1                 -> ok tap id=1
contacts                         -> contacts n=2 0=swipe,1,495,286 1=tap,1,826,514
                                 -> done tap id=1 frames=2 ms=110
                                 -> done swipe frames=403 ms=4020 slowed=0
```

A two-finger pinch, both lines in one write of 62 characters:

```text
swipe 400 300 200 300 500 id=1
swipe 600 300 800 300 500 id=2   -> ok swipe id=1
                                 -> ok swipe id=2
                                 -> done swipe id=1 frames=53 ms=520 slowed=0
                                 -> done swipe id=2 frames=53 ms=520 slowed=0
```

The `frames=` and `ms=` figures in these examples come from the host
suite's panel, which reads every 10ms; on the bench they are not measured.

### On the panel

- The glass's contacts come first. When the glass and the injector
  together have more than 5, an injected contact is left out: one that came
  down last first, then the last one in the injector's report. It stays
  out until the injector lifts it, so a finger lifting from the glass never
  brings it back as a new press. Its UP carries `TOUCH_FLAG_NO_TAP`: only a
  control that stops what the bench drives acts on it.
- A finger and an injected contact on the same pixel are two contacts with
  two track ids.
- A touch event the panel loses aborts what it would abort for the glass
  (`shared/safety/touch_loss.h`). 5 contacts that all jump more than 120px
  in one report make 10 events, which the touch task's event buffer of
  `TOUCH_MAX_POINTS * 2` holds.

### Errors

Errors are one line and the command is not run:

| Reply | Cause |
| --- | --- |
| `err syntax <cmd>` | missing, extra or non-numeric argument, or an `id=` not last |
| `err range <field>` | a coordinate, `ms` or `id` out of range |
| `err unknown <cmd>` | no such command |
| `err toolong` | line over 63 characters |
| `err busy` | the contact's gesture (contact 0 without `id=`) or a `wiring` check is under way |
| `err full` | 5 contact ids are in use |
| `err unconfirmed` | a gesture in LISTEN |
| `err state` | `wiring` or `confirm` in ACTIVE |
| `err swapped` | `confirm` after a `swapped` verdict |
| `err noverdict` | `confirm` without an `ok` verdict |

A gesture that fails after its `ok` gets one of:

| Reply | Cause |
| --- | --- |
| `fail <gesture> no_master` | the panel did not read a frame within 100ms, or stopped reading for 100ms while a contact was down; every contact ends |
| `fail <gesture> panel_reset` | the panel read the product ID again: it restarted, or it re-attached the injector; every contact is withdrawn |
| `fail <gesture> released` | `release` arrived |

With `id=N` the gesture reads `<gesture> id=N`: `fail swipe id=2 no_master`.
Closing the port lifts every contact that is down, without a reply.

## Station check: DISARM and STOP during a drag

The check that a second finger stops the bench while a drag drives the
output. The points are the SERVO screen's (`test/host/servo_spots.h`), in
panel pixels: ARM and DISARM at 826, 514; the band's STOP at 934, 30; the
dial drag at x 495 from 30 degrees above the shaft (y 258) to 30 degrees
below (y 368). The host suite runs the same check
(`a_second_contact_disarms_a_drag_under_way` in `test_servo_arm`).

1. Bring the injector up (below) until the mode chip shows `INJECT`.
2. `tap 384 197` opens SERVO from OVERVIEW. Connect a servo, or a scope,
   to the output the output card names.
3. `hold 826 514 2500` arms: `done hold frames=2 ms=<2500 to 2510>`, the
   band reads ARMED.
4. `swipe 495 258 495 368 4000`: `ok swipe`. The output follows the drag.
5. About 1s later, `tap 826 514 id=1`: `ok tap id=1`, then
   `done tap id=1 frames=2 ms=<100 to 120>`. The band reads SAFE, and the
   output stops following the drag while the swipe goes on.
6. Wait for `done swipe ...`. `contacts` reads `contacts n=0`.
7. `hold 826 514 2500` re-arms from rest: ARMED, and the output stands at
   its rest until the next press.
8. `swipe 495 258 495 368 4000`, and about 1s later `tap 934 30 id=1`: the
   band's STOP. The band reads SAFE and the output stops following.
9. Wait for `done swipe ...`, then repeat step 7.

A `fail` line in any step, or a step whose band does not read as written,
fails the check. `release` lifts every contact at once.

## Wiring check

`wiring` samples GP4 and GP5 for 500ms and applies two rules to every
transaction: out of idle the line that falls first is SDA (START), into idle
the line that rises last is SDA (STOP). Idle is both lines high for at least
10µs. A transaction names a line when both rules name the same one.

A verdict needs at least 20 whole transactions that name a line, and at
least half of all whole transactions. `agree=` is the share of the named
ones that name the majority line, and a verdict needs 95%. A transaction
that names no line is counted in `tx=` and left out of `agree=`. The panel's
poll of 0x14 while the injector listens is one: the address goes
unacknowledged, both lines go high after the ninth clock, and a bare STOP
follows more than 10µs later. SDA and SCL shorted together name no line at
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
400kHz traffic and at 4 Msps with 100kHz traffic. The rate the RP2350
reaches is not measured; the verdict line reports it as `rate=`.

## Bring-up on the bench

1. Wire GP4, GP5 and GND as in the pin table. Leave 3V3, GP6 and GP7 open.
2. Power the panel and let it run. Plug the Zero into the Pi.
3. `ping` returns `state=listen`. The LED blinks white at 1Hz.
4. `wiring` returns `wiring ok scl=GP5 sda=GP4 ...`. Any other verdict: fix
   the wiring first; the LED is yellow for `swapped`, `low` and `noise`.
5. `confirm scl=GP5`. The LED goes dim green once the panel reads 0x14.
6. Reset the panel. The panel looks once, at start-up; it
   finds 0x14 at start and shows `INJECT` in the status band's mode chip.
   After an injector reset, repeat steps 3 to 5 only: the panel (which found
   the injector at start-up) detached the
   silent injector after 1000ms and re-attaches it within about 1s of
   `confirm`, and `INJECT` shows again.
7. `tap 890 462` presses the SETUP tile. The reply is
   `done tap frames=2 ms=<100 to 110>`.

The IOmcu's update question, which opens on the menu when the IOmcu runs
another build, has CANCEL at panel (305, 376) and START at (719, 376).
`tap 305 376` declines it; `hold 719 376 2500` starts the update after the
2s hold.

## LED

| Colour | State |
| --- | --- |
| white, blinking 1Hz | LISTEN |
| yellow | LISTEN, last verdict `swapped`, `low` or `noise` |
| off | ACTIVE, no read for 100ms |
| dim green | ACTIVE, the panel reads 0x14 |
| blue | a contact is down |
| red | the last gesture failed |

## Timing

| Item | Value |
| --- | --- |
| frame interval | at least 10ms |
| frame acknowledge timeout | 100ms |
| default tap | 100ms |
| longest gesture | 30000ms |
| contacts down at once | 5 |
| contact ids | 0 to 15 |
| reply queue | 8 lines; when full the oldest is dropped |
| watchdog | 200ms |
| USB write timeout | 10ms per line |

Licence: MIT (`SPDX-License-Identifier: MIT` in every source file).
