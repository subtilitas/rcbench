# Performance

<sub>**English** · [Deutsch](Performance-de.md)</sub>

The rendering budget for anyone adding or changing a screen.

## The constraint

The ESP32-P4 draws into one of two DPI (display pixel interface) frame
buffers of 1,228,800 bytes in PSRAM (pseudo-static random-access memory)
while the DPI scans the other out: 1024 x 600px of RGB565 at 60.38Hz,
74MB/s of reads. The CPU reaches the buffers through two write-back caches:
the L1 data cache (64KB, 2-way, 64-byte lines) and the L2 cache (128KB,
8-way, 64-byte lines). Every line the L2 does not hold costs a 64-byte fill
from PSRAM and, once written, a 64-byte write-back. The table below counts
those fills, which can be measured exactly on the host.

A frame's time is its PSRAM traffic and its instructions, and which of the
two bounds a frame on this board is not measured. The one screen with
figures from both sides points at the instructions: SERVO > LIVE during a
sweep is 2,171 fills in the table, 271KiB of traffic, and the console read
DRAW 13.8 to 16.5ms ([On the board](#on-the-board)); as traffic alone that
is 16 to 20MB/s. The fills hold the traffic side; `test_frame_work` holds
the instruction side in pixel visits ([Rules](#rules)).

With Screen rotation 180°, a screen draws into a logical buffer of the same
size in PSRAM, and the flip has the PPA (pixel-processing accelerator) copy
the blocks the frame changed into the frame buffer, turned, before it
presents it. The model counts the drawing, not the copy. The copy's time is
the console's TURN; it is part of WAIT and adds to every frame that changes.

`tools/frame_cost.py` builds the real screens for the host and runs them under
cachegrind with the ESP32-P4's two caches: the L1 as cachegrind's first level,
the L2 as its last, sized from `firmware/panel/sdkconfig.defaults`. It reports
the difference between rendering one frame and eleven, so process start-up and
the first paint of each buffer cancel out. The model sends every data access
through both caches, the stack's included, and models no instruction fetch:
the host runs x86 code.

What a fill costs in time is the effective bandwidth of the CPU's writes into
PSRAM while the DPI reads. It is not measured, so the table gives fills and
traffic and no time. The panel measures it at start-up ([On the
board](#on-the-board)); `PROBE_CLEAR_US` in the tool takes that figure, and
the table then gives an estimated time and frame rate for each mode, from
the traffic only.

<!-- framecost:start -->
```
$ python3 tools/frame_cost.py
panel 60.4 Hz, 64-byte lines; effective bandwidth not measured

mode                   lines/frame     traffic
----------------------------------------------
frame                        9,341    1168 KiB
frame-idle                       0       0 KiB
held                         4,914     614 KiB
sim                         12,998    1625 KiB
throttle                    11,164    1396 KiB
chrome                      51,588    6448 KiB
overview                         1       0 KiB
servo                        2,170     271 KiB
servo-grip                     532      66 KiB
servo-current                3,127     391 KiB
supply                       9,685    1211 KiB
supply-chrome               47,592    5949 KiB
analyser                         0       0 KiB
logs                             0       0 KiB
settings                         0       0 KiB
settings-list                    0       0 KiB
battery                          0       0 KiB
balance                          0       0 KiB
motor-stick                  1,550     194 KiB
balance-sim                  3,078     385 KiB
settings-sim                 3,023     378 KiB
battery-sim                  3,054     382 KiB
analyser-chrome             60,990    7624 KiB
logs-chrome                 26,185    3273 KiB
settings-chrome             37,805    4726 KiB
settings-list-chrome        36,903    4613 KiB
battery-chrome              58,001    7250 KiB
balance-chrome              63,351    7919 KiB
motor-stick-chrome          41,817    5227 KiB
picker                           0       0 KiB
picker-chrome               41,256    5157 KiB
wifi                             0       0 KiB
wifi-chrome                 22,130    2766 KiB
wifi-keys                   36,851    4606 KiB
clear                       19,206    2401 KiB
vlines                      10,203    1275 KiB
hlines                         180      22 KiB

est. ms and est. fps: not measured
```
<!-- framecost:end -->

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
therefore checks the table to a tolerance of 2%, and never less than 100
fills. It fails, too, when the line that names the panel's rate and the
bandwidth differs from the tool's profile.

## Rules

**Draw row-major.** Seventeen full-height vertical lines cost 10,202 fills;
the same pixel count as horizontal lines costs 180, because each line is
resident from the previous pixel. The shell is laid out in horizontal bands, and a
vertical rule is a deliberate expense.

**Cache the chrome.** Repainting everything costs 22,000 to 64,000 fills,
the `-chrome` modes, and a cached steady frame of the same screen none:
60,990 fills for RECEIVER's full repaint against 0 for its steady frame. Every screen keeps a per-framebuffer bitmask of what it has already
painted; that is what the `buffer_index` argument of `render()` is for. The
panel alternates between two buffers, so a screen that invalidates only the
buffer being drawn leaves the other one a frame behind, which reads as flicker.

**Cache a stencil that does not move.** SIMULATION is drawn on every frame
whenever the bench numbers carry `LINK_BN_SIMULATED`, which includes an
IOmcu answering with simulated numbers. Rotated text scans its rotated
bounding box, which corner to corner is the whole canvas, and rotates and
divides per pixel to write the pixels the mark covers: 3,439 on an
800 x 480px canvas, 0.9% of it, and 5,377 on 1024 x 600px. `ui_watermark`
records those points once, in a table of 6,144, and writes them thereafter:
on 800 x 480px, 144,721 instructions per frame in place of 8,412,078. The
count on 1024 x 600px is not measured.

**A count of fills is not a count of cycles.** The table above measures
cache-line fills, and the mark costs only 3,668 of them, `sim` against
`frame`: its time is arithmetic per pixel, not traffic, and the table cannot
show it. Where a mode's measured frame time exceeds what its fills
predict, count instructions before trusting the estimate.

**Give a control that moves every frame its own counter.** A drag delivers a
touch per frame. A revision shared with the controls beside it would
repaint them all on every touch. The motor screen's throttle carries its
own, and repaints the readout's box and the slider's painted region: the `throttle` mode in the table above measures it against a sample
frame. The flip quantises to whole panel frames: 60.4, 30.2 or 20.1fps.
The drag's frame time on the board is not measured.

Clear what a widget paints, not what it occupies. The slider's thumb and its
shadow stand proud of the track by 5px above and 7px below, and the hero
numerals are 32px tall with 3px of slant on a 30px row, so a clear sized
to the track or to the row leaves a thumb at every position the finger passed
and the numerals' feet behind. `ui_slider_painted_rect()` reports the region
rather than leaving a caller to derive it.

**Paint only on frames that have something to paint.** Samples arrive at 20Hz
and the panel refreshes at 60.38Hz (measured, the console's VSYNC figure),
so two frames in three have nothing new. The DPI clock is 48MHz, not the
52MHz `board_pins.h` requests; at 48MHz its 1354 x 636 frame alone would
give 55.74Hz. ESP-IDF keeps the requested line time by shortening the
bridge's front porch to a 1250-clock line, so the frame takes as long as
1354 x 636 clocks at 52MHz: 60.38Hz, the rate `tools/frame_cost.py`
computes from `board_pins.h`. That this sets the rate is read from
ESP-IDF's source, not from the registers.
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

CI (continuous integration) holds each mode below to a ceiling of fills per
frame. A ceiling sits 13 to 29% above the largest figure of its modes in the
table above, and a mode under 600 fills is held at 1,000. A ceiling catches
a change that makes the model draw more, not a frame that misses the panel:

| Modes | Ceiling (fills) | Catches |
| --- | ---: | --- |
| `overview`, `analyser`, `logs`, `settings`, `settings-list`, `battery`, `balance`, `picker`, `wifi` | 1,000 | a screen with cached chrome that has started repainting |
| `servo` | 2,700 | a growing arm and grip drawing |
| `servo-grip` | 1,000 | a breath that repaints more than its box |
| `servo-current` | 4,000 | a changed digit of the current that repaints the whole card |
| `motor-stick` | 2,000 | a growing steady frame of ESC STICK, 1,551 fills in the table above |
| the three `-sim` modes | 3,600 | a watermark that has grown past the full canvas |
| the ten `-chrome` modes | 72,000 | a growing full repaint |
| `wifi-keys` | 42,000 | a key of the Wi-Fi keyboard that repaints more than the screen |

`tools/check_docs.py` holds this table to the `--max-lines` arguments in
`.github/workflows/ci.yml`: a ceiling that differs, a mode CI holds that has
no row here, and a row CI does not run each fail. `test_frame_work` holds
budgets in pixel visits beside it ([Rules](#rules)).

No ceiling holds the bench frame (`frame`, `sim`, `supply`) to one telemetry
sample. A sample arrives every 50ms, every third panel frame, and a frame
keeps up with the samples while its traffic fits three panel frames at the
effective bandwidth. That bandwidth is not measured, so the ceiling is not
set.

If a future pane needs more room, the remaining levers in order of bluntness
are the plot's height, its width, and clipping the simulation watermark to the
region that was repainted.

## On the board

The console lines of this section come with the build option
`RCBENCH_PERF_LOG`, ON by default (`firmware/panel/CMakeLists.txt`):

```sh
idf.py -D RCBENCH_PERF_LOG=OFF build
```

builds a panel without the start-up probe, the lines and the code behind
them: the render loop then reads no clock for DRAW and calls nothing for
them. CI builds that, with the touch injector left out as well, as the
extra panel build.

Release images are built with `RCBENCH_PERF_LOG=OFF` and print none of
these lines. The CI build, a local `idf.py build` and a build with
`-D RCBENCH_PERF_LOG=ON` keep them; to measure on a release's sources,
build without the flag. `tools/check_perf_log.py` holds the release image
to the setting: it fails when the image holds the start-up probe's text,
the stack scan's text or the `scan_stacks` symbol.

The console is UART0 at 115,200 baud with no driver installed, beside the
USB Serial/JTAG port. A line that does not fit the UART's 128-byte transmit
FIFO (first in, first out) holds the render loop 87us for each byte past
it, and 128 bytes drain in 11.1ms, inside a frame of 16.6ms at 60.4fps. The
panel therefore prints at most one of these lines a frame:

- every 300 frames, 5.0s at 60.4fps and 9.9s at 30.2fps, the rate line;
- on the frame after it, the window line;
- on the frames between, one line of the lows while one is due: the stack
  scan's time, the internal heap, then one stack.

```text
I (…) rcbench: <f> fps  DRAW <n> us  WAIT <n> us  TURN <n> us  VSYNC <f> Hz  FLIP TIMEOUTS <n>  TOUCHLOST <panel>/<driver>
I (…) rcbench: frames <n> on <title> #<id>: DRAW <min>/<mean>/<max> WAIT <min>/<mean>/<max> TURN <min>/<mean>/<max> us
```

The two lines are 110 to 135 bytes, by the counters' digits and the
screen's title; a byte past 128 holds the loop 87us. The rate line's DRAW,
WAIT and TURN are those of its frame; VSYNC is the panel's refresh as the
panel counts it since the rate line before; FLIP TIMEOUTS counts the flips
since start-up that waited 200ms for a frame-done event in vain. DRAW is the
paint time of a frame, WAIT how long the flip blocked after it, and TURN
the part of WAIT Screen rotation 180° spent copying the frame turned, 0 in
NORMAL. The window line covers the frames drawn on the screen shown since
the rate line before, fewer than 300 when the screen changed in between;
its figures are minimum, mean and maximum in us. A healthy frame is mostly
WAIT; DRAW climbing until WAIT reaches zero is the frame budget being spent.

What the option costs each frame when ON: the frame added to the window,
about ten comparisons and three 64-bit additions, and a test for a due line;
every 300 frames one query of the internal heap; every 6th rate line one
stack scan ([High-water marks on the board](#high-water-marks-on-the-board)).
Their time on the board is not measured, except the stack scan's, which the
panel logs.

Once at start-up, before the splash's first frame, the panel times four
clears of the back buffer, after one it does not time:

```text
I (…) rcbench: PSRAM probe: 4 clears of 1228800B in <n> us, <n> us each
```

A clear writes the buffer once: 2,457,600 bytes of traffic in the model, a
fill and a write-back for every line. The time of one clear, set as
`PROBE_CLEAR_US`, is what turns the table's traffic into time.

| Mode | On the panel | DRAW min (ms) | DRAW mean (ms) | DRAW max (ms) | WAIT mean (ms) | fps |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `clear` | the start-up probe; DRAW mean is the time of one clear | not measured | not measured | not measured | not measured | not measured |
| `overview` | OVERVIEW, untouched | not measured | not measured | not measured | not measured | not measured |
| `frame` | MOTOR & ESC > LIVE during an ESC run | not measured | not measured | not measured | not measured | not measured |
| `sim` | MOTOR & ESC > LIVE with the IOmcu's simulated numbers | not measured | not measured | not measured | not measured | not measured |
| `servo` | SERVO > LIVE during a sweep | not measured | not measured | not measured | not measured | not measured |
| `servo-grip` | SERVO > LIVE with the arm at rest | not measured | not measured | not measured | not measured | not measured |
| `supply` | SUPPLY with its output on | not measured | not measured | not measured | not measured | not measured |
| `analyser` | RECEIVER with no receiver connected | not measured | not measured | not measured | not measured | not measured |
| `logs` | LOGS, the file list | not measured | not measured | not measured | not measured | not measured |
| `settings` | SETUP's tiles | not measured | not measured | not measured | not measured | not measured |
| `settings-list` | SETUP > PANEL's list | not measured | not measured | not measured | not measured | not measured |
| `battery` | BATTERY | not measured | not measured | not measured | not measured | not measured |
| `balance` | BALANCE | not measured | not measured | not measured | not measured | not measured |
| `motor-stick` | ESC STICK open on MOTOR & ESC > PROGRAM | not measured | not measured | not measured | not measured | not measured |
| `picker` | PICK A PIN | not measured | not measured | not measured | not measured | not measured |
| `wifi` | WI-FI with ACCESS POINT on | not measured | not measured | not measured | not measured | not measured |
| `wifi-keys` | WI-FI > CHOOSE HOME NETWORK, the password keyboard, a key typed every second | not measured | not measured | not measured | not measured | not measured |

`frame_cost.py --check-doc` reads this table: a mode the tool does not draw
fails, and the clear row's DRAW mean and `PROBE_CLEAR_US` are measured
together or not at all and agree to 0.05ms. The other figures are a record
of the board, held by no check; the mean and the extremes of a screen's
frames are what the model's estimate for its mode is set against.

No window line has been read on a board yet. The rate line of a station run
on SERVO > LIVE, the bench ARMED, gave:

| The arm | Screen rotation | fps | DRAW (ms) | TURN (ms) |
| --- | --- | ---: | ---: | ---: |
| at rest | NORMAL | 60.4 | not measured | 0 |
| at rest | 180° | not measured | not measured | not measured |
| sweeping | NORMAL | 39 to 45 | not measured | 0 |
| sweeping | 180° | 30.2 | not measured | 3.8 to 4.8 |

VSYNC read 60.38Hz. A second run, of an earlier build of the same frame
path, read DRAW 13.8 to 16.5ms on the sweep in NORMAL, at 35 to 43fps. The
flip quantises to whole panel frames, 60.4, 30.2 or 20.1fps, so a rate
between two of them is a mix of frames that met one panel frame and frames
that took two. With TURN in the flip, a sweep's DRAW and TURN together are
17.6 to 21.3ms (derived from the two runs), over the 16.6ms of one panel
frame, and every frame takes two.

## Stacks

Every panel task runs on a fixed stack, and a call chain that runs past the
end of one restarts the panel. `tools/stack_check.py` reads each task's
deepest call chain out of the panel ELF (Executable and Linkable Format)
file. The ESP32-P4 runs RISC-V code, which `tools/stack_riscv.py` decodes:
the frame of every function is the lowest point its stack pointer reaches
(`addi sp, sp, -N` and its compressed forms), and the depth is the largest
sum of frames along any chain from the task's entry point. A jump out of a
function, the form a tail call takes, counts as a call. The tasks are every
`xTaskCreatePinnedToCore()` and `xTaskCreate()` in the panel's own code
under `firmware/panel`, `managed_components/` left out, and the main task.
CI runs the tool after the panel build and fails when a task's depth exceeds
its stack less 1024 bytes.

| Task | Entry | Stack (bytes) | Deepest chain (bytes) | Spare below the margin (bytes) |
| --- | --- | ---: | ---: | ---: |
| `main` | `main_task`, which calls `app_main` and runs the UI | 8,192 | 5,648 | 1,520 |
| `control` | `control_task` | 6,144 | 4,720 | 400 |
| `runlog` | `log_task` | 6,656 | 4,528 | 1,104 |
| `knob` | `knob_task`, the rotary knob's reader | 3,072 | 1,056 | 992 |
| `touch` | `touch_task`, the GT911 reader (`components/gt911`) | 4,096 | 1,296 | 1,776 |
| `wifi_ctl` | `worker`, which starts and stops the radio and the page's server (`components/wifi_access`) | 6,912 | 3,744 | 2,144 |
| `mdns` | `mdns_task`, the mDNS (multicast domain name system) responder while the panel is joined to a home network | 4,096 | 1,344 | 1,728 |
| `wifi_get` | `fetch_task`, one release check or download from GitHub (`components/wifi_access`) | 8,192 | 3,040 | 4,128 |
| `cardjob` | `files_task`, FILES' card operations (`panel_files.c`) | 6,656 | 3,504 | 2,128 |

The page's HTTP (hypertext transfer protocol) server runs on a task of
8,192 bytes, and the TCP/IP (transmission control protocol, internet
protocol) stack, the event loop and esp_hosted's link to the ESP32-C6 on
tasks of their own. ESP-IDF and esp_hosted create them, so they are not in
the table: their depth is not measured, and the console reports what they
leave unused (below). `wifi_get`'s depth leaves out the TLS (transport layer
security) handshake and every read under it: esp_http_client reaches its
transport through a function pointer. Its stack is the page server's, which
never runs beside it; the handshake's depth is not measured.

`runlog`'s deepest chain mounts the SD card: the listing of the servo
files the card serves (`hitec_card_run()`, `list()`, `servo_store_preset()`,
`io_walk()`, `storage_walk()`), the mount again of a card found gone
(`storage_read_end()`, `mount_locked()`) and the `snprintf()` under it. The tool follows every call the mount can
make, `f_mkfs()` among them, which a mount that never formats does not
reach. A run's start mounts the card on this task as well
(`storage_check()`). The chain needs 4,528 bytes; the stack of 6,656
bytes leaves 1,104 above the 1,024-byte margin.

`wifi_ctl`'s deepest chain is the radio's stop: `radio_down()`, the NTP
(network time protocol) client's last poll (`wifi_ntp_poll()`) and the time
line it logs (`wall_ntp_line()`, `wall_utc_text()`), whose `snprintf()`
reaches newlib's float conversion. It leaves 2,144 bytes above the margin.

Measured on the ESP32-P4 build with ESP-IDF v5.5.5 at -O2. Of the margin,
560 bytes are spent outside the frames:

- 128 for the registers an interrupt saves on the task's stack before it
  moves to the interrupt stack;
- 32 for the coprocessor save area's header at the top of every stack;
- 384 for the contexts the port takes from the bottom of a stack the first
  time its task uses the unit: the FPU (floating-point unit) 132, the
  hardware loops 24, the vector unit 216, and up to 12 of alignment;
- 16 for FreeRTOS's canary at the bottom of the stack.

The thread-local storage at the top of each stack is read from the ELF and
charged on top; the panel's build has none. The other 464 bytes cover what
the tool cannot see, and each depth above is a lower bound for that reason:

- calls through a function pointer, except the router's calls into a screen,
  which the tool reads out of every screen's table, and the link port's
  callbacks, the drawing store's flash callbacks, the start check's after
  an update, the SERVO screen link's callbacks into the panel, the
  hooks of the sessions SERVO > PROGRAM and MOTOR & ESC > PROGRAM open
  and the card functions the run logs, the fault log and the servo files
  are written through,
  which it follows by name
  (`PORT_CALLS`, `FLASH_CALLS`, `BOOT_CALLS`, `SERVO_CALLS`,
  `PROG_CALLS`, `TAKE_CALLS`, `CARD_CALLS`, `FAULT_CALLS`, `SERVO_STORE_CALLS`):
  220 such
  calls are reachable from `main_task`, most of them in ESP-IDF's storage
  and display drivers;
- jumps through a register: a switch table, which stays inside its
  function, or a tail call through a pointer, which the decode cannot tell
  apart;
- calls into the ESP32-P4's ROM (read-only memory), whose frames are not in
  the ELF;
- a function that moves its stack pointer by an amount known only at run
  time: its frame is the part the constants give;
- recursion: a function already on a chain is not entered again, so a
  chain runs round a cycle at most once.

CI runs the tool with `--check-doc`, which fails when the table above or the
count of calls through a pointer differs from the build.

### High-water marks on the board

The first rate line and every 6th after it, 30s apart at 60.4fps, want a
scan of every task's stack. The scan is FreeRTOS's `uxTaskGetSystemState()`.
It holds the kernel lock, with the render core's interrupts masked, while it
reads each task's unused stack byte by byte, so a kernel call on the other
core, the control task's among them, waits for it. The scan therefore runs
on a quiet frame only, by `flash_gate_open()`'s rule: the bench not ARMED
and no arm waiting, the supply's output off and no ON queued. A scan wanted
while the bench is ARMED runs on the first rate line after it is SAFE; the
high-water mark keeps what the run used. The scan reports each task's name
as a pointer into the task's control block, which a task that ends frees:
the panel copies the names at once, 15 bytes at most, and keeps the scan
only if the number of tasks after the copy is the number the scan read;
otherwise the next rate line scans again. The panel logs the scan's time at
the first scan and at every scan that takes longer than all before it, and
after each scan every task whose high-water mark fell, one line a frame:

```text
I (…) rcbench: stack scan: <n> tasks in <n> us
I (…) rcbench: stack <task>: <n> B never used
```

The first scan lists every task, ESP-IDF's and esp_hosted's included, over
the frames after it, one task a frame. The scan's time on the board is not
measured.

The figure is FreeRTOS's high-water mark: the bytes at the bottom of the
stack that no frame has written since the task started, the coprocessor
contexts left out. It covers only the paths the panel has run, so it bounds
nothing; set against the deepest chain above, it shows what the tool does
not follow.

| Task | Stack (bytes) | Never used on the board (bytes) |
| --- | ---: | ---: |
| `main` | 8,192 | not measured |
| `control` | 6,144 | not measured |
| `runlog` | 6,656 | not measured |
| `knob` | 3,072 | not measured |
| `touch` | 4,096 | not measured |
| `wifi_ctl` | 6,912 | not measured |
| `mdns` | 4,096 | not measured |
| `wifi_get` | 8,192 | not measured |
| `cardjob` | 6,656 | not measured |

The tasks of ESP-IDF and esp_hosted are not in the table: their names and
figures come from the console of a sample, and none is recorded.

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
| core 0 | `main` | 4,096 | 1,468 | 528 | 1,844 |
| core 1 | `core1_main` | 4,096 | 644 | 528 | 2,668 |

Measured on the image CI builds, pico-sdk 2.3.0 with arm-none-eabi-gcc
14.2.1 (the package `gcc-arm-none-eabi` 15:14.2.rel1 of Ubuntu 26.04, the
same compiler as ARM GNU 14.2.Rel1). CI runs the tool with `--check-doc`,
which fails when core 0's row, or the count of calls through a register
below, differs from that build. Core 1's row is not held: its chain moves
with the compiler (arm-none-eabi-gcc 13.2.1 gives 640 bytes), and CI builds
the image with the runner's packaged one. The frames named below are those
of the same build.

Core 0's deepest chain is a request that binds an output and fails in the
pico-sdk: `main` (256 bytes), `can_service` (344), `link_dev_dispatch`,
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
  handler, which the tool reads out of the link's page table: 171 such calls
  are reachable from `main` in the image CI builds (arm-none-eabi-gcc
  14.2.1);
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
which the P4 reaches over SDIO (secure digital input output) through
`esp_wifi_remote` and `esp_hosted`. No tool estimates the internal RAM free
at run time: the radio's host side allocates its buffers and tasks at run
time, which the build's map does not show. The panel prints the figures
instead. At start-up, every task of the panel running and the radio off:

```text
I (…) rcbench: internal RAM: <n> B free, largest block <n> B; PSRAM: <n> B free
```

When the radio is up, the mode numbered as in `wa_session.h` (0 ACCESS
POINT, 1 joined to a home network, 2 a network scan, 3 a release check or
download):

```text
I (…) wifi: radio up, mode <n>; internal RAM <n> B free
```

And on a frame after a rate line, when the lowest free figure since
start-up has fallen ([On the board](#on-the-board)):

```text
I (…) rcbench: internal RAM: <n> B free at the lowest, <n> B free now, largest block <n> B
```

| When | Free (bytes) | Lowest since start-up (bytes) |
| --- | ---: | ---: |
| start-up, the radio off | not measured | not measured |
| mode 0, ACCESS POINT | not measured | not measured |
| mode 1, joined to a home network | not measured | not measured |
| mode 2, a network scan | not measured | not measured |
| mode 3, a release check | not measured | not measured |

PSRAM free at start-up: not measured.

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

The radio task `wifi_ctl` takes 6,912 bytes of internal RAM for its stack
at the first radio start or release check and keeps it until the panel
restarts. The build's static internal figures (below) do not change with it.

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
| .bss in internal RAM (DIRAM) | 30,952 | 243,504 |
| .bss in PSRAM (`.ext_ram.bss`) | 212,552 | 0 |
| internal RAM the image holds: .text, .data and .bss in DIRAM, of 655,360 | 128,624 | 341,176 |
| application image, built without `RCBENCH_IOMCU_BIN` (no IOmcu image carried) | 2,439,488 | 2,439,488 |

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
