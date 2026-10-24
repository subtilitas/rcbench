# Performance

<sub>**English** · [Deutsch](Performance-de.md)</sub>

The rendering budget for anyone adding or changing a screen.

## The constraint

The panel's DSI (Display Serial Interface) host scans one of two frame
buffers of 1024 x 600px out of PSRAM (pseudo-static random-access memory)
at 60.38Hz, about 74MB/s, by DMA (direct memory access). The CPU draws
into the other buffer through the ESP32-P4's caches, and both share the
PSRAM's bandwidth. What a frame costs on the ESP32-P4, in cache-line fills,
in PSRAM traffic or in time, is not measured, and no profile of its caches
models it ([Ceilings](#ceilings)).

`tools/frame_cost.py` builds the real screens for the host and runs them
under cachegrind with a model of the ESP32-S3 panel: a 64KiB, 8-way data
cache of 64-byte lines, a scan-out at 39.0Hz and 39MB/s of PSRAM
throughput for the CPU, at 800 x 480px. It reports the difference between
rendering one frame and eleven, so process start-up and the first paint of
each buffer cancel out. The renderer draws 1024 x 600px, and the model's
cache is not the ESP32-P4's, so this page holds no table of its output.

<!-- framecost:start -->
No table: the frame cost on the ESP32-P4 panel is not measured.
<!-- framecost:end -->

The tool's modes:

| Mode | What it measures |
| --- | --- |
| `frame` | the motor bench on a frame where a telemetry sample lands |
| `frame-idle` | the motor bench on a frame between samples, nothing touched |
| `held` | the motor bench between runs: live readouts over a plot that is holding the last one |
| `sim` | as `frame`, with the SIMULATION watermark |
| `throttle` | the motor bench with a finger on the throttle, the drag case |
| `chrome` | the motor bench with nothing cached, repainted in full |
| `overview` | the menu, chrome cached |
| `servo` | the servo screen with the arm redrawn |
| `servo-grip` | the servo screen with only the grip repainted |
| `servo-current` | as `servo-grip`, with the INA3221 as the servo rail's meter and a window in every frame whose current differs in the digits shown: the grip, the CURRENT row's value, the line and the plot repainted, each clipped to itself |
| `supply` | the supply screen with its output on, a sample landing every frame |
| `analyser`, `logs`, `settings`, `battery`, `balance`, `motor-stick`, `picker`, `wifi` | one steady frame of that screen, chrome cached; `settings` is SETUP's tiles, `motor-stick` ESC STICK open on MOTOR & ESC > PROGRAM |
| `settings-list` | SETUP with PANEL's list open, chrome cached |
| `wifi-keys` | WI-FI with the Wi-Fi keyboard open, a key typed every frame |
| `<screen>-sim` | the same screen with the SIMULATION watermark |
| `<screen>-chrome` | the same screen invalidated on every frame |
| `clear` | a full-screen clear |
| `vlines` | seventeen full-height vertical lines |
| `hlines` | the same pixel count as horizontal lines |

Absolute counts shift by a few fills between machines, because argv and the
environment share the cache with the framebuffer. `frame_cost.py --check-doc`
therefore checks a table to a tolerance of 2%, and at least 100 fills.

## Rules

**Draw row-major.** A framebuffer row lies in consecutive bytes, so a
horizontal line fills each cache line once and a vertical line fills one
for every pixel. The shell is laid out in horizontal bands, and a vertical
rule is a deliberate expense. What either costs on the ESP32-P4 is not
measured.

**Cache the chrome.** Repainting everything visits every pixel of the
canvas, 614,400 at 1024 x 600px, where a steady frame visits what changed.
Every screen keeps a per-framebuffer bitmask of what it has already
painted; that is what the `buffer_index` argument of `render()` is for. The
panel alternates between two buffers, so a screen that invalidates only the
buffer being drawn leaves the other one a frame behind, which reads as flicker.

**Cache a stencil that does not move.** SIMULATION is drawn on every frame
whenever the bench numbers carry `LINK_BN_SIMULATED`, which includes an
IOmcu answering with simulated numbers. Rotated text scans its rotated
bounding box, which corner to corner is the whole canvas, and rotates and
divides per pixel to write the pixels the mark covers: 5,377 on 1024 x
600px. `ui_watermark` records those points once, in a table of 6,144, and
writes them thereafter. The instructions per frame at 1024 x 600px, with
the table and without it, are not measured.

**A count of fills is not a count of cycles.** A cache model counts
cache-line fills. The watermark writes few pixels and computes much for
each, so its cost is arithmetic, not traffic, and a count of fills does
not show it. Where a mode's measured frame time exceeds what its fills
predict, count instructions before trusting the estimate.

**Give a control that moves every frame its own counter.** A drag delivers
a touch per frame. The throttle on the motor screen carries its own
revision, apart from the buttons beside it, and repaints the readout's box
and the slider's painted region: the `throttle` mode of
`tools/frame_cost.py` measures it against a sample frame. The flip
quantises to whole panel frames, so a drag either lands inside two of them
or waits for a third: at 60.38Hz, 30.2fps or 20.1fps. The drag's frame time
on the ESP32-P4 is not measured.

Clear what a widget paints, not what it occupies. The slider's thumb and its
shadow stand proud of the track by 5px above and 7px below, and the hero
numerals are 32px tall with 3px of slant on a 30px row, so a clear sized
to the track or to the row leaves a thumb at every position the finger passed
and the numerals' feet behind. `ui_slider_painted_rect()` reports the region
rather than leaving a caller to derive it.

**Paint only on frames that have something to paint.** Samples arrive at
20Hz and the panel refreshes at 60.38Hz (measured, the console's VSYNC
figure), so most frames have nothing new. The DPI (Display Pixel
Interface) clock is 48MHz, not the 52MHz `board_pins.h` requests; at 48MHz
its 1354 x 636 frame alone would give 55.74Hz. ESP-IDF keeps the requested
line time by shortening the bridge's front porch to a 1250-clock line, so
the frame takes as long as 1354 x 636 clocks at 52MHz: 60.38Hz. That this
sets the rate is read from ESP-IDF's source, not from the registers.
The bench screen keeps the plot's push count and a control revision, each per
framebuffer, and repaints the plot, the readouts and the controls only when the
corresponding counter has moved. `each_framebuffer_is_updated_independently` in
`test_motor_draw` pins the per-buffer counters.

**A clipped repaint walks only what lies in its clip.** A clip limits what a
primitive writes, not what it computes. The SERVO card repaints its breathing
grip through a box 73px square and a moving horn through boxes of about 170px
square, each by drawing the whole card under the clip. So the rasteriser skips
a glyph or a seven-segment digit whose cell lies outside the clip, walks an arc
only in the directions in which the clip lies, walks the antialiased disc,
capsule and ring over the pixels each row of them can cover, and works out a
partial ring's angle per pixel only near its ends and its fade.
`test_gfx_clip` holds each shortcut to the loop over the whole box, pixel for
pixel.

**The band is repainted when it changes.** Drawn whole, the band costs 102,288
pixel visits SAFE and 114,281 ARMED, with its red chip and run clock. The
router keeps `ui_band_key()` of the band each framebuffer last got and repaints
it only when the key moves: the run clock once a second while the bench is
ARMED, a chip, a press of STOP or the home tag. Over a simulated bench it is
drawn every frame, under the watermark. `test_frame_work` counts the pixels the
rasteriser visits per frame (`gfx_work()`, built into the host suite only) and
holds ARMED at rest to SAFE at rest plus the band once a second, a held
position to SAFE plus six layers of the grip's box, a swept or dragged horn to
140,000 visits a frame and MOTOR & ESC LIVE ARMED at rest to 300,000.

## Ceilings

No CI (continuous integration) step holds a frame to a ceiling in cache lines;
`test_frame_work` holds budgets in pixel visits, as above.
`tools/frame_cost.py` models the ESP32-S3's cache and bandwidth at 800 x
480px. The host renderer draws 1024 x 600px, so `frame_cost.py
--check-doc` reports the table as not checked and `--update-doc` writes
nothing. A profile of the ESP32-P4's caches, and the ceilings CI holds,
need measurements on the board.
`tools/check_docs.py` fails on a ceiling table in this page while
`.github/workflows/ci.yml` holds none.

If a future pane needs more room, the remaining levers in order of bluntness
are the plot's height, its width, and clipping the simulation watermark to the
region that was repainted.

The panel's log line `… fps  DRAW … us  WAIT … us  VSYNC … Hz`, printed
every 300 frames (`panel_frame.c`), is the on-hardware check: DRAW is the paint time, WAIT is how long the flip blocked. A
healthy frame is mostly WAIT.

## Stacks

Every panel task runs on a fixed stack, and a call chain that runs past the
end of one restarts the panel. The depth of each task's deepest chain on
the ESP32-P4 is not measured, and no CI step checks a panel task's stack.

`tools/stack_check.py` reads each task's deepest call chain out of a panel
ELF (Executable and Linkable Format) file of Xtensa code: the frame of every
function is the `entry a1, N` it opens with, and the depth is the largest
sum of frames along any chain from the task's entry point. The tool fails
when a task's depth exceeds its stack less 1024 bytes. It stops on the
ESP32-P4's image, which is RISC-V code. A RISC-V decoder in the tool, or a
run-time high-water log of every task's stack, gives the ESP32-P4 its
figures. The IOmcu's stacks are checked in CI, as below.

The tasks the panel creates, from `xTaskCreatePinnedToCore()` and
`xTaskCreate()` in `firmware/panel`, and the main task:

| Task | Entry | Stack (bytes) | Deepest chain |
| --- | --- | ---: | --- |
| `main` | `main_task`, which calls `app_main` and runs the UI | 8,192 | not measured |
| `control` | `control_task` | 6,144 | not measured |
| `runlog` | `log_task` | 6,656 | not measured |
| `knob` | `knob_task`, the rotary knob's reader | 3,072 | not measured |
| `touch` | `touch_task`, the GT911 reader (`components/gt911`) | 4,096 | not measured |
| `wifi_ctl` | `worker`, which starts and stops the radio and the page's server (`components/wifi_access`) | 5,120 | not measured |
| `mdns` | `mdns_task`, the mDNS (multicast domain name system) responder while the panel is joined to a home network | 4,096 | not measured |
| `wifi_get` | `fetch_task`, one release check or download from GitHub (`components/wifi_access`) | 8,192 | not measured |
| `cardjob` | `files_task`, FILES' card jobs, started at FILES' first request (`main/panel_files.c`) | 6,656 | not measured |

The page's HTTP (hypertext transfer protocol) server runs on a task of
8,192 bytes, and the Wi-Fi driver, the TCP/IP (transmission control
protocol, internet protocol) stack and the event loop on tasks of their own.
ESP-IDF creates them, and their depth is not measured either.

`runlog` mounts the SD card: at a run's start (`storage_check()`) and again
for a card found gone (`storage_read_end()`, `esp_vfs_fat_sdmmc_mount()`).
Its stack holds one line of a servo test, `SERVO_TEST_LINE_MAX` bytes,
beside that chain.

### The IOmcu

`tools/stack_check.py --iomcu` reads the RP2350 image the same way. Core 0
runs `main()` on the pico-sdk's main stack: 4096 bytes, `__StackBottom` to
`__StackTop` in the ELF, set by `PICO_STACK_SIZE=0x1000` in
`firmware/iomcu/CMakeLists.txt`. Core 1 runs `core1_main()` on the 4096-byte array
its launch passes. Neither stack has a guard. An interrupt runs on the stack
of the core it interrupts, so each core is charged its deepest chain and one
interrupt: a 108-byte exception frame and the deepest handler the image
installs. CI runs the tool after the IOmcu build and fails when a
core's chain, one interrupt and a margin of 256 bytes exceed its stack.

| Core | Entry | Stack (bytes) | Deepest chain (bytes) | One interrupt (bytes) | Spare below the margin (bytes) |
| --- | --- | ---: | ---: | ---: | ---: |
| core 0 | `main` | 4,096 | 1,484 | 528 | 1,828 |
| core 1 | `core1_main` | 4,096 | 640 | 528 | 2,672 |

Measured on the image CI builds, pico-sdk 2.3.0 with arm-none-eabi-gcc
13.2.1. CI runs the tool with `--check-doc`, which fails when core 0's row,
or the count of calls through a register below, differs from that build. Core 1's row is not held: its chain moves with the
compiler (ARM GNU 14.2 gives 644 bytes), and CI builds the image with the
runner's packaged one. The frames named below are those of a build with
ARM GNU 14.2, which gives 1,508 bytes for core 0.

Core 0's deepest chain is a request that binds an output and fails in the
pico-sdk: `main` (264 bytes), `can_service` (344), `link_dev_dispatch`,
`slots_write`, `slots_take`, `ports_apply`, `slots_rerate`,
`outputs_hw_apply_only`, `out_dshot_bind`, the
pico-sdk's claim of a PIO (programmable input/output) state machine, and
`panic()` with what it calls to print, 228 bytes. The deepest handler is
the USB stack's worker, `low_priority_worker_irq`, at 420 bytes; its chain
ends in `panic()` as well.

A write command to the programming port for KST and Chaservo servos reaches 1,104 bytes: `main`
(264), `can_service` (344), `link_dev_dispatch` (32), `kst_write` (24),
`kst_port_write` (88), `kst_session_edit` (40) and the start of a planner
job, 312 bytes from `kst_plan_edit_begin` down: `kst_plan_edit_begin`
(40), the limit check `kst_limits_edit` (248), `load` (16) and
`kst_field_get` (8). A pass that plans reaches 824 bytes: `main`,
`kst_port_step` (48), `step_programming` (40), `kst_session_step` (264) and one slice of the
planner, 208 bytes from `kst_plan_step` down. The planner does not
recurse. The planner's job, the plan and the staged image are held in the
session's and the port's state, not on the stack.

The port plans a write in slices. The write command checks the request and
starts the planner's job, 64us of planner work. Each pass of core 0's
main loop then runs 1 slice in `kst_session_step()`, 100 to 109 units of
work: 892us at most, 816us the longest found. The planning ends after
216,000 units, at most 2,161 passes, with a plan, with no plan, or with
the result "too complex" when the units are spent first. The costliest
request found takes 120,136 units, at most 1,202 passes and 397 to 647ms
of planner time. All times are for 150MHz, computed in an emulator from
the instructions executed with ARM GNU 14.2 at -O3, and not measured on
hardware. The time of a pass with the link, the heartbeat and the outputs
in it is not measured. The heartbeat gap that switches every output off is
150ms.

Each depth is a lower bound:

- calls through a register are not followed, except a page's read and write
  handler, which the tool reads out of the link's page table: 161 such calls
  are reachable from `main` in the image CI builds (arm-none-eabi-gcc
  13.2.1; ARM GNU 14.2 gives 165);
- the 1,088-byte frame of newlib's `two_way_long_needle()` is left out.
  `strstr()` calls it for a needle of 255 characters or more, and the two
  needles in the firmware are 3 and 5 characters. The tool fails on a
  `strstr()` call under `shared/` or `firmware/iomcu/src` whose needle is
  not a string literal under 255 characters;
- 17 hand-written arithmetic routines of the pico-sdk carry no size in the
  ELF, and their frames are not read;
- one interrupt is counted, not a second one on top of it;
- recursion: a function already on a chain is not entered again, so a
  chain runs round a cycle at most once.

Core 0's stack is the whole of the RAM region `SCRATCH_Y`, 0x20081000 to
0x20082000; the image places nothing else in it, and the link fails when
something is. Below it, in `SCRATCH_X`, lies the 4096-byte array the
pico-sdk reserves as its own core 1 stack, which this image does not run
on. The same definition sizes that array.

`-v` lists all of them, and each task's deepest chain.

The panel's UI keeps its frames small where it is cheap. A page's drawer
is a separate function, so only the page on screen holds its buffers: in
ESC STICK, `esc_stick_panel_render()` calls the page on screen, and MOTOR &
ESC's `mo_render()` calls the session's way and its body's as two
functions, so a session's pages do not sit on the body's frame. A line
copied for display is copied without `snprintf()`, which reaches newlib's
float conversion. The frame sizes on the ESP32-P4 are not measured.

## Internal RAM

The ESP32-P4 has no radio. Wi-Fi runs on the ESP32-C6 of the 7-inch board,
which the P4 reaches over SDIO (secure digital input output). The internal
RAM free at run time is not measured, and no tool or CI step estimates it. The panel prints the free memory at start-up:

```text
I (…) rcbench: internal RAM: <n> B free, largest block <n> B; PSRAM: <n> B free
```

A radio start that fails logs the step, the error's name and the internal
heap at that moment:

```text
E (…) wifi: esp_wifi_init: ESP_ERR_NO_MEM; internal RAM <n> B free, largest block <n> B
```

The steps are `esp_netif_init`, `esp_event_loop_create_default`, the two
event handlers, the network interface, `esp_wifi_init`,
`esp_wifi_set_mode`, `esp_wifi_set_config`, `esp_wifi_start`,
`httpd_start`, and the creation of the `wifi_ctl`, `wifi_get` and `mdns`
tasks. A network interface ESP-IDF cannot allocate stops the panel with an
assertion inside ESP-IDF instead, and the panel restarts.

FILES' card task `cardjob` takes 6,656 bytes of internal RAM for its stack
and 16,384 bytes of DMA-capable internal RAM for its copy buffer. Both are
taken at FILES' first request, not at start-up, and the panel logs the
internal RAM free before and after:

```text
I (…) rcbench: files: cardjob started; internal RAM <n>B free before, <n>B after
```

Without the RAM the line is `W (…) rcbench: files: no internal RAM for
cardjob` and FILES says `Not enough memory.` The figures on a panel
are not measured.

`firmware/panel/main/psram_bss.lf` places the zero-initialised data (.bss)
of 30 objects in PSRAM (pseudo-static random-access memory): the screens'
state and the card, page, settings and output helpers. The fragment names
each object and the rules it meets. `tools/psram_bss.py` reads the build's
linker map and fails CI when a named object keeps .bss in internal RAM.
Static figures of the P4 build at ESP-IDF v5.5.5, from `idf.py size`:

| Item | With the fragment | Without it |
| --- | ---: | ---: |
| .bss in internal RAM (DIRAM) | 30,936 | 243,488 |
| .bss in PSRAM (`.ext_ram.bss`) | 212,552 | 0 |
| internal RAM the image holds: .text, .data and .bss in DIRAM, of 655,360 | 128,600 | 341,152 |
| application image, built without `RCBENCH_IOMCU_BIN` (no IOmcu image carried) | 2,437,488 | 2,437,488 |

All figures are bytes. The column without the fragment adds the PSRAM .bss
to the internal figures; it is not a build of its own. ESP-IDF's own PSRAM entries for lwIP (lightweight
IP stack) and wpa_supplicant use a scheme that only `esp_wifi`'s fragment
defines, which the P4 build does not include; lwIP's 3,774 bytes of .bss
stay in internal RAM.

## Wi-Fi downloads

A card file or the ZIP file goes from the panel to the browser over one TCP
(transmission control protocol) connection. The page's server answers one
request at a time: a request that arrives during a download waits for its
end. On a home network a download of a 16MiB file ran at 688-757kB/s, the
card read and the radio together, not separated, at a ping average of
14.6ms, and an upload of a 2.56MB image to `/update` at 556-599kB/s. The
panel logged no SDIO (Secure Digital input/output) error and drew 60fps
during the downloads. What bounds the rate:

| Link | Setting | Bound |
| --- | --- | --- |
| power save | none while joined to a home network (`esp_wifi_set_ps(WIFI_PS_NONE)`); an access point does not sleep | none |
| send buffer | 11,520 bytes, 8 segments of 1,440 bytes (`CONFIG_LWIP_TCP_SND_BUF_DEFAULT`) | 11,520 bytes per round trip: 1,152,000B/s at a 10ms round trip, 576,000B/s at 20ms |
| card | SDMMC (the SD/MMC host), 4 data lines at 20MHz (`BOARD_SD_FREQ_KHZ`), 10,000,000B/s on the wire | not measured |
| radio | the ESP32-C6 over SDIO, 4 data lines at 40MHz (`BOARD_C6_FREQ_KHZ`), 20,000,000B/s on the wire, on the SDMMC host the card uses | not measured |
| processor | the server's task at priority 1 on core 0, beside the UI (user interface) loop at priority 1 | not measured |

ESP-IDF's default power save, `WIFI_PS_MIN_MODEM`, wakes the radio for
each DTIM (delivery traffic indication message) beacon only. The access
point holds the frames for the panel until then, TCP's acknowledgements
among them, so each send buffer waits up to one DTIM period: 102.4ms at
the common beacon interval and a DTIM period of 1, 307.2ms at a DTIM
period of 3. With ESP-IDF's default send buffer of 5,760 bytes, that is
18,750 to 56,250B/s. Without power save the radio draws more current
while it runs; how much more on the panel is not measured.

The page's piece buffer, 4,096 bytes, is in PSRAM. ESP-IDF v5.5.5 reads a
piece into it by DMA (direct memory access) when the buffer is aligned to a
cache line, and through a buffer of its own otherwise; which of the two the
page's buffer takes is not measured. At the measured 14.6ms round trip the
send buffer bounds a download at 789kB/s (11,520 bytes a round trip,
derived), above the measured 688-757kB/s. While a download runs, the
server's task takes processor time from the UI loop; the panel drew 60fps
during the measured downloads.

A download ends early, and the connection is reset, when

- the receiver closes the connection: seen within 250ms
  (`WA_SEND_SLICE_MS`, `shared/webaccess/include/wa_send.h`);
- the connection fails;
- the receiver takes nothing for 30s (`WA_SEND_QUIET_MS`);
- the radio goes off: within 250ms;
- the card read fails, or the file is not the size the response announced.

The browser then has fewer bytes than announced and reports the download
as failed. The reset frees the bytes still queued for the connection at
once (`CONFIG_LWIP_SO_LINGER`); the server takes the next request. The
console logs each file:

```text
I (…) wifi: file: sent, 16777216 of 16777216 B read in <n> ms
W (…) wifi: file: receiver gone, <n> of 16777216 B read in <n> ms
```

The other endings read `nothing taken for 30 s`, `radio going off` and
`card read failed`.

### Measuring a download

On a computer on the same home network as the panel, with `curl` and, on
Linux, `iw`:

1. Copy a file of 16,777,216 bytes to the card's root:
   `head -c 16777216 /dev/urandom > RATE16M.BIN`.
2. Open WI-FI on the panel, join the home network and switch the access
   code off. `<panel>` below is the address WI-FI shows.
3. Note the computer's own link and switch its power save off:
   `iw dev wlan0 link`, `iw dev wlan0 get power_save`,
   `sudo iw dev wlan0 set power_save off`. Note the round trip:
   `ping -c 20 <panel>`.
4. The rate, 3 times:
   `curl -s -o /dev/null --max-time 600 -w '%{http_code} %{size_download} B %{time_total} s %{speed_download} B/s\n' http://<panel>/card/RATE16M.BIN`.
   Each run answers `200 16777216 B`, and the console logs
   `file: sent, 16777216 of 16777216 B read in <n> ms`.
5. A download broken off part way, and the next request:
   `curl -s -o /dev/null --max-time 5 http://<panel>/card/RATE16M.BIN; curl -s -o /dev/null -w '%{http_code} %{time_total} s\n' --max-time 10 http://<panel>/`.
   The second answers `200`; the console logs `file: receiver gone`.
6. A receiver that stops reading: a connection that asks for the file and
   then reads nothing,
   `python3 -c 'import socket,time; s=socket.create_connection(("<panel>",80)); s.sendall(b"GET /card/RATE16M.BIN HTTP/1.1\r\nHost: p\r\n\r\n"); time.sleep(60)' &`,
   then after 35s
   `curl -s -o /dev/null -w '%{http_code} %{time_total} s\n' --max-time 10 http://<panel>/`.
   The console logs `file: nothing taken for 30 s`; the request
   answers `200`.

## The SD card's file system

The panel reads FAT12, FAT16, FAT32 and exFAT (extended File Allocation
Table) through FatFs, the file system library in ESP-IDF. ESP-IDF v5.5
builds FatFs without exFAT, and no Kconfig option turns it on. The panel
build takes a copy of ESP-IDF's `fatfs` component with `FF_FS_EXFAT` set to
1 (`firmware/panel/fatfs_exfat.cmake`), and `storage_list.c` stops a build
whose FatFs has no exFAT. Long file names stay as `sdkconfig.defaults` sets
them: on, 255 characters, the working buffer on the heap.

What exFAT costs, read from the P4 build at ESP-IDF v5.5.5 against the same
tree built with the `fatfs_exfat.cmake` include and that stop taken out,
both without `RCBENCH_IOMCU_BIN`:

| Item | Without exFAT | With exFAT | Where |
| --- | ---: | ---: | --- |
| application image | 2,425,520 | 2,434,880 | flash, `ota_0` holds 8,126,464 |
| internal RAM the image holds: .text, .data and .bss in DIRAM, from `idf.py size` | 128,576 | 128,576 | internal |
| the mount's context: `FATFS`, 4 open files (`FIL`) and ESP-IDF's VFS (virtual file system) state | 25,376 | 25,560 | heap, PSRAM first |
| the long name working buffer, held for one FatFs call | 512 | 1,120 | heap, PSRAM first |
| one directory walk's buffer (`storage_walk()`) | 328 | 368 | heap, internal |
| `FILINFO`, one entry as FatFs reads it, in `storage_stat()`'s buffer beside the path, for one call | 280 | 288 | heap, internal |

All figures are bytes. The heap rows are `sizeof()` values read with GDB
from the two builds; the heap at run time is not measured. FatFs puts its
long name buffer on the heap, not on the calling task's stack.

A file's size is 64-bit from FatFs to the screen. stat() through the VFS
gives a size in `st_size`, an `off_t` of 32 bits on the panel, so every
size the panel uses comes from FatFs's `f_stat()` or `f_readdir()`
instead.

`FF_LBA64` (64-bit sector numbers) stays 0. It is needed for a card
partitioned GPT (GUID Partition Table; GUID: globally unique identifier) and for a volume over 2TiB. With
it the P4 image grows by a further 5,056 bytes, `FATFS` from 4,160 to 4,184
bytes and `FIL` from 4,176 to 4,192 bytes.
