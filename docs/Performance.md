# Performance

<sub>**English** · [Deutsch](Performance-de.md)</sub>

The rendering budget for anyone adding or changing a screen.

## The constraint

The frame rate on this panel is limited by PSRAM (pseudo-static random-access
memory) bandwidth, not by the CPU. The LCD (liquid-crystal display) peripheral
scans one framebuffer out of PSRAM continuously at about 30 MB/s, and the
framebuffers sit behind a write-back, write-allocate data cache (64 KB, 8-way,
64-byte lines). Every pixel the CPU writes costs a 64-byte line fill and a
64-byte write-back unless the line is already resident. The cost of a frame is
therefore a count of cache-line fills, which can be measured exactly on the
host.

`tools/frame_cost.py` builds the real screens for the host and runs them under
cachegrind with the ESP32-S3's cache geometry. It reports the difference
between rendering one frame and eleven, so process start-up and the first paint
of each buffer cancel out.

<!-- framecost:start -->
```
$ python3 tools/frame_cost.py
panel 39.0 Hz, ~39 MB/s effective -> 976 KiB of traffic per panel frame

mode       lines/frame     traffic   est. ms  est. fps
-------------------------------------------------------
frame            7,623      953 KiB     25.0      39.0
frame-idle        1,276      160 KiB      4.2      39.0
held             4,194      524 KiB     13.8      39.0
sim              8,639     1080 KiB     28.4      19.5
throttle         9,346     1168 KiB     30.7      19.5
chrome          31,564     3946 KiB    103.6       7.8
overview           956      120 KiB      3.1      39.0
servo           14,675     1834 KiB     48.2      19.5
servo-grip        2,998      375 KiB      9.8      39.0
servo-current        4,018      502 KiB     13.2      39.0
supply           7,749      969 KiB     25.4      39.0
supply-chrome       30,089     3761 KiB     98.8       9.8
analyser           869      109 KiB      2.9      39.0
logs               877      110 KiB      2.9      39.0
settings           869      109 KiB      2.9      39.0
settings-list          869      109 KiB      2.9      39.0
battery            885      111 KiB      2.9      39.0
balance            864      108 KiB      2.8      39.0
motor-stick        1,018      127 KiB      3.3      39.0
balance-sim        2,424      303 KiB      8.0      39.0
settings-sim        2,405      301 KiB      7.9      39.0
battery-sim        2,435      304 KiB      8.0      39.0
analyser-chrome       38,002     4750 KiB    124.7       7.8
logs-chrome       16,687     2086 KiB     54.8      13.0
settings-chrome       19,582     2448 KiB     64.3      13.0
settings-list-chrome       23,416     2927 KiB     76.9      13.0
battery-chrome       36,614     4577 KiB    120.2       7.8
balance-chrome       40,453     5057 KiB    132.8       6.5
motor-stick-chrome       31,464     3933 KiB    103.3       7.8
picker             902      113 KiB      3.0      39.0
picker-chrome       22,218     2777 KiB     72.9      13.0
wifi               873      109 KiB      2.9      39.0
wifi-chrome       13,770     1721 KiB     45.2      19.5
wifi-keys       23,050     2881 KiB     75.7      13.0
clear           12,006     1501 KiB     39.4      19.5
vlines           8,160     1020 KiB     26.8      19.5
hlines               0        0 KiB      0.0      39.0
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
therefore checks the table to a tolerance of 1%.

## Rules

**Draw row-major.** Seventeen full-height vertical lines cost 8,160 fills; the
same pixel count as horizontal lines costs zero, because each line is resident
from the previous pixel. The shell is laid out in horizontal bands, and a
vertical rule is a deliberate expense.

**Cache the chrome.** Repainting everything costs about thirty times the steady
state. Every screen keeps a per-framebuffer bitmask of what it has already
painted; that is what the `buffer_index` argument of `render()` is for. The
panel alternates between two buffers, so a screen that invalidates only the
buffer being drawn leaves the other one a frame behind, which reads as flicker.

**Cache a stencil that does not move.** SIMULATION is drawn on every frame
whenever the bench numbers carry `LINK_BN_SIMULATED`, which includes an
IOmcu answering with simulated numbers. Rotated text scans its rotated
bounding box, which corner to corner is the whole canvas, and rotates and
divides per pixel to write the 3,439 pixels the mark covers, 0.9% of the
canvas. `ui_watermark` records those points once and writes them thereafter:
144,721 instructions per frame in place of 8,412,078.

**A count of fills is not a count of cycles.** The table above measures
cache-line fills, and the mark costs only 1,586 of them: it is arithmetic per
pixel, not traffic. It was 58 times the cost the table implied, and the table
could not show it. Where a mode's measured frame time exceeds what its fills
predict, count instructions before trusting the estimate.

**Give a control that moves every frame its own counter.** A drag delivers a
touch per frame. The motor screen's buttons and its throttle shared one
revision, so dragging repainted every control beside it. The throttle now
carries its own, and repaints the readout's box and the slider's painted
region: the `throttle` mode in the table above measures it against a sample
frame. The flip quantises to whole panel frames, so a drag either lands
inside two of them or waits for a third: 19.5 fps or 13.0. Measured on
hardware at 50.5 ms, the old drag took the third.

Clear what a widget paints, not what it occupies. The slider's thumb and its
shadow stand proud of the track by 5 px above and 7 px below, and the hero
numerals are 32 px tall with 3 px of slant on a 30 px row, so a clear sized
to the track or to the row leaves a thumb at every position the finger passed
and the numerals' feet behind. `ui_slider_painted_rect()` reports the region
rather than leaving a caller to derive it.

**Paint only on frames that have something to paint.** Samples arrive at 20 Hz
and the panel refreshes at 39 Hz, so about every other frame has nothing new.
The bench screen keeps the plot's push count and a control revision, each per
framebuffer, and repaints the plot, the readouts and the controls only when the
corresponding counter has moved. `each_framebuffer_is_updated_independently` in
`test_motor` pins the per-buffer counters.

## Ceilings

The panel moves 976 KiB per frame at 39 Hz. A render costing twice that lands
at 19.5 fps, which is one frame per 20 Hz telemetry sample; drawing faster
would repaint identical pixels, drawing slower would drop samples. CI
(continuous integration) holds each mode to a ceiling:

| Modes | Ceiling (fills) | Catches |
| --- | ---: | --- |
| `frame`, `sim`, `supply` | 15,600 | a bench frame that exceeds one telemetry sample |
| `overview` | 2,000 | a chrome-cached screen that has started repainting |
| `servo` | 17,000 | the arm and grip drawing growing |
| `servo-grip` | 4,000 | a breath repainting the whole card |
| `servo-current` | 4,500 | a changed digit of the current repainting the whole card |
| `analyser`, `logs`, `settings`, `battery`, `balance`, `motor-stick`, `picker`, `wifi` | 1,200 | a screen that has started repainting |
| `settings-list` | 1,200 | a SETUP list that has started repainting |
| the three `-sim` modes | 2,800 | the watermark growing past a full canvas |
| the ten `-chrome` modes | 45,000 | a full repaint growing |
| `wifi-keys` | 24,000 | a key of the Wi-Fi keyboard repainting more than the screen |

`tools/check_docs.py` holds this table to the `--max-lines` arguments in
`.github/workflows/ci.yml`: a ceiling that differs, a mode CI holds with no
row here, and a row CI does not run each fail.

If a future pane needs more room, the remaining levers in order of bluntness
are the plot's height, its width, and clipping the simulation watermark to the
region that was repainted.

The ESP32-S3 log line `DRAW … WAIT …`, printed every 300 frames, is the
on-hardware check: DRAW is the paint time, WAIT is how long the flip blocked. A
healthy frame is mostly WAIT.

## Stacks

Every panel task runs on a fixed stack, and a call chain that runs past the
end of one restarts the panel. `tools/stack_check.py` reads each task's
deepest call chain out of the panel ELF (Executable and Linkable Format)
file: the frame of every function is the `entry a1, N` it opens with, and the
depth is the largest sum of frames along any chain from the task's entry
point. A jump out of a function, the form a tail call takes, counts as a
call. The tasks are every `xTaskCreatePinnedToCore()` and `xTaskCreate()`
in `firmware/panel`, and the main task. CI runs it after both panel builds
and fails when a task's depth exceeds its stack less 1024 bytes.

| Task | Entry | Stack (bytes) | Deepest chain (bytes) | Spare below the margin (bytes) |
| --- | --- | ---: | ---: | ---: |
| `main` | `main_task`, which calls `app_main` and runs the UI | 8,192 | 5,088 | 2,080 |
| `control` | `control_task` | 6,144 | 4,384 | 736 |
| `runlog` | `log_task` | 5,632 | 3,744 | 864 |
| `knob` | `knob_task`, the rotary knob's reader | 3,072 | 1,328 | 720 |
| `touch` | `touch_task`, the GT911 reader (`components/gt911`) | 4,096 | 1,680 | 1,392 |
| `wifi_ctl` | `worker`, which starts and stops the radio and the page's server (`components/wifi_access`) | 5,120 | 3,360 | 736 |
| `mdns` | `mdns_task`, the mDNS (multicast domain name system) responder while the panel is joined to a home network | 4,096 | 1,808 | 1,264 |
| `wifi_get` | `fetch_task`, one release check or download from GitHub (`components/wifi_access`) | 8,192 | 2,832 | 4,336 |

The page's HTTP (hypertext transfer protocol) server runs on a task of
8,192 bytes, and the Wi-Fi driver, the TCP/IP (transmission control
protocol, internet protocol) stack and the event loop on tasks of their own.
ESP-IDF creates them, so they are not in the table: their depth is not
measured. `wifi_get`'s depth leaves out the TLS (transport layer security)
handshake and every read under it: esp_http_client reaches its transport
through a function pointer. Its stack is the page server's, which never runs
beside it; the handshake's depth is not measured.

`runlog`'s deepest chain saves a servo file: `kst_store_save()`, the create
in `kst_files.c` and a folder level of `BENCH/KST/CONF` made through
`card_make_dirs()`. `CARD_CALLS` names every function a caller hands
`card_make_dirs()`, and the tool counts each of them under every call, so
that step is counted at its deepest callee: 64 bytes over the chain a save
takes. The chain needs 3,744 bytes; the stack of 5,632 bytes leaves 864
above the 1,024-byte margin.

Measured on ESP-IDF v5.4 at -O2. Of the margin, 528 bytes are spent outside
the frames: 320 for the FPU (floating-point unit) and vector-unit state saved
at the top of every stack, 192 for the frame an interrupt pushes, and 16 below
the deepest frame. The other
496 bytes cover what the tool cannot see, and each depth above is a lower
bound for that reason:

- calls through a function pointer, except the router's calls into a screen,
  which the tool reads out of every screen's table, and the link port's
  callbacks, the drawing store's flash callbacks, the start check's after
  an update, the SERVO screen link's callbacks into the panel, the
  hooks of the sessions SERVO > PROGRAM and MOTOR & ESC > PROGRAM open
  and the card functions the run logs, the fault log and the servo files
  are written through,
  which it follows by name
  (`PORT_CALLS`, `FLASH_CALLS`, `BOOT_CALLS`, `SERVO_CALLS`,
  `PROG_CALLS`, `TAKE_CALLS`, `CARD_CALLS`, `FAULT_CALLS`, `KST_CALLS`):
  230 such
  calls are reachable from `main_task`, most of them in ESP-IDF's storage
  and display drivers;
- calls into the ESP32-S3's ROM (read-only memory), whose frames are not in
  the ELF;
- recursion: a function already on a chain is not entered again, so a
  chain runs round a cycle at most once.

CI runs the tool with `--check-doc` on the ESP-IDF v5.4 build, which fails
when the table above or the count of calls through a pointer differs from
that build.

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
| core 0 | `main` | 4,096 | 1,748 | 528 | 1,564 |
| core 1 | `core1_main` | 4,096 | 640 | 528 | 2,672 |

Measured on the image CI builds, pico-sdk 2.3.0 with arm-none-eabi-gcc
13.2.1. CI runs the tool with `--check-doc`, which fails when core 0's row,
or the count of calls through a register below, differs from that build. Core 1's row is not held: its chain moves with the
compiler (ARM GNU 14.2 gives 644 bytes), and CI builds the image with the
runner's packaged one. The frames named below are those of a build with
ARM GNU 14.2, which gives 1,692 bytes for core 0.

Core 0's deepest chain is a request that binds an output and fails in the
pico-sdk: `main` (448 bytes), `can_service` (344), `link_dev_dispatch`,
`slots_write`, `slots_take`, `ports_apply`, `slots_rerate`,
`outputs_hw_apply_only`, `out_dshot_bind`, the
pico-sdk's claim of a PIO (programmable input/output) state machine, and
`panic()` with what it calls to print, 228 bytes. The deepest handler is
the USB stack's worker, `low_priority_worker_irq`, at 420 bytes; its chain
ends in `panic()` as well.

A write command to the programming port for KST and Chaservo servos reaches 1,288 bytes: `main`
(448), `can_service` (344), `link_dev_dispatch` (32), `kst_write` (24),
`kst_port_write` (88), `kst_session_edit` (40) and the start of a planner
job, 312 bytes from `kst_plan_edit_begin` down: `kst_plan_edit_begin`
(40), the limit check `kst_limits_edit` (248), `load` (16) and
`kst_field_get` (8). A pass that plans reaches 1,008 bytes: `main`,
`kst_port_step` (48), `step_programming` (40), `kst_session_step` (264) and one slice of the
planner, 208 bytes from `kst_plan_step` down. The planner does not
recurse. The planner's job, the plan and the staged image are held in the
session's and the port's state, not on the stack.

The port plans a write in slices. The write command checks the request and
starts the planner's job, 64 us of planner work. Each pass of core 0's
main loop then runs 1 slice in `kst_session_step()`, 100 to 109 units of
work: 892 us at most, 816 us the longest found. The planning ends after
216,000 units, at most 2,161 passes, with a plan, with no plan, or with
the result "too complex" when the units are spent first. The costliest
request found takes 120,136 units, at most 1,202 passes and 397 to 647 ms
of planner time. All times are for 150 MHz, computed in an emulator from
the instructions executed with ARM GNU 14.2 at -O3, and not measured on
hardware. The time of a pass with the link, the heartbeat and the outputs
in it is not measured. The heartbeat gap that switches every output off is
150 ms.

Each depth is a lower bound:

- calls through a register are not followed, except a page's read and write
  handler, which the tool reads out of the link's page table: 156 such calls
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

The UI keeps its frames small where it is cheap. A page's drawer is a
separate function, so only the page on screen holds its buffers: in ESC
STICK, `esc_stick_panel_render()`'s frame is 32 bytes and the largest
page's 464. MOTOR & ESC's `render()` takes 48 bytes and calls the session's
way and its body's, 416 bytes, as two functions, so a session's pages do not
sit on the body's frame. A
line copied for display is copied without `snprintf()`, which reaches
newlib's float conversion: 2,368 bytes deep in the call graph.

## Internal RAM

The radio needs internal RAM (random-access memory) that PSRAM cannot
stand in for. `esp_wifi_init()` takes the Wi-Fi driver's static RX
(receive) and TX (transmit) buffers: 16 of each, about 1,600 bytes each,
51,200 bytes of DMA-capable (direct memory access) internal RAM.
`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` makes the TX buffers static. When
they do not fit, the radio does not start, and WI-FI shows "Wi-Fi could not start.".

`tools/ram_budget.py` reads the internal heap a panel build leaves and
subtracts what the panel allocates at run time, then what each Wi-Fi mode
allocates on top. CI runs it after both panel builds and fails when the
heap left with the largest mode up is under 32,768 bytes: the size of the
pool ESP-IDF keeps for internal and DMA allocations at run time
(`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL`). The radio's sockets, the
hardware AES (Advanced Encryption Standard) and SHA (Secure Hash Algorithm)
engines' buffers and a card transfer take from it while the radio runs.

| Item | What it is | Bytes |
| --- | --- | ---: |
| `static` | internal RAM the image's code and data hold, up to `_heap_start` | 128,104 |
| `heap` | internal heap at start: `_heap_start` to 0x3FCE9710, and the ROM's (read-only memory's) region to 0x3FCF0000, which the heap takes over after start-up | 297,880 |
| `start-up` | the task stacks and control blocks of ESP-IDF and of the panel, every queue and semaphore, the display's two bounce buffers, and an allowance of 12,288 bytes for the drivers | 106,056 |
| `before` | heap left with the panel up and the radio off | 191,824 |
| `AP` | ACCESS POINT: the radio, the page's server and its task | 98,724 |
| `JOIN` | joined to a home network: AP's and the mDNS responder | 103,160 |
| `SCAN` | the network search: the radio alone | 86,096 |
| `FETCH` | a release check or download: the radio, the `wifi_get` task and an allowance of 4,096 bytes for TLS | 98,724 |
| `left` | heap left with the largest mode up | 88,664 |

Read from the ESP-IDF v5.4 build. CI runs the tool with `--check-doc` on
that build, which fails when the table differs from it. `-v` lists every
allocation and where its size comes from.

Each figure is an estimate. A task's stack, a queue's length and the Wi-Fi
buffer count are read from the build and the source; the sizes of a task's
control block, a queue's header and a queue's item are `sizeof()` values
read with GDB from the v5.4 build. Three allowances are not read from
anything: 12,288 bytes for the drivers' start-up allocations, 16,384 bytes
for the Wi-Fi driver's blocks beyond its buffers and its task, and 4,096
bytes for TLS. The tool leaves out RTC (real-time clock) fast memory, about
8 KB of heap that is not DMA-capable. The free heap on a panel is not
measured. The panel prints it at start-up:

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

What lives in PSRAM instead: the zero-initialised data (.bss) of 20 objects,
163,504 bytes, named in `firmware/panel/main/psram_bss.lf`, and, through
`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`, ESP-IDF's own choice of the
.bss of lwIP (lightweight IP stack), net80211, pp and wpa_supplicant,
13,742 bytes.

| Object | Component | .bss (bytes) |
| --- | --- | ---: |
| `log_viewer_screen` | ui | 35,568 |
| `servo_screen` | ui | 29,509 |
| `ui_watermark` | ui | 20,504 |
| `supply_screen` | ui | 20,508 |
| `motor_screen` | ui | 20,008 |
| `kst_card` | bench | 6,208 |
| `esc_stick_panel` | ui | 6,096 |
| `kst_panel` | ui | 5,185 |
| `log_csv` | logfile | 4,904 |
| `esc_registry` | esc | 4,228 |
| `analyser_screen` | ui | 4,036 |
| `wa_zip` | webaccess | 2,433 |
| `wa_page` | webaccess | 1,408 |
| `wifi_screen` | ui | 1,320 |
| `ui_band` | ui | 1,040 |
| `esc_stick_pages` | ui | 252 |
| `motor_settings_page` | ui | 128 |
| `motor_workbench` | ui | 124 |
| `motor_program_page` | ui | 44 |
| `log_reader` | logfile | 1 |

Read from the map file of the ESP-IDF v5.4 build. CI runs
`tools/ram_budget.py --check-doc` on that build, which fails when a row,
the count, the total or ESP-IDF's 13,742 bytes differ from the map, or
when a row is given twice. It also fails on a line of `psram_bss.lf` other
than `object (extram_bss)` entries under a `[mapping:...]` header, on an
object the fragment names and the map places no .bss of, and on an object
in PSRAM that the fragment does not name, outside lwIP, net80211, pp and
wpa_supplicant. `-v` lists every object.

An object moves only when its state

- is read and written by no interrupt handler: the panel's IRAM
  (instruction RAM) handlers are `display.c`'s and `touch.c`'s, and their
  state stays internal;
- is read and written by no DMA: the card's SPI (Serial Peripheral
  Interface) driver copies a buffer that is not DMA-capable through one
  that is;
- holds no atomic, spinlock, mutex or queue: the ESP32-S3's
  compare-and-swap instruction works on internal RAM only.

A flash write closes the cache, and PSRAM with it. For its length the
writing task runs from IRAM and the other core waits in IRAM, so no code
that reads these objects runs.

The screens' state is read through the same 64 KB data cache as the
framebuffers. `tools/frame_cost.py` models the frames it renders through
that cache with every read and write, these objects' included. The frame rate on a
panel with them in PSRAM is not measured.
