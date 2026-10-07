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
frame            8,785     1098 KiB     28.8      19.5
frame-idle        1,243      155 KiB      4.1      39.0
held             4,711      589 KiB     15.5      39.0
sim              9,745     1218 KiB     32.0      19.5
throttle        10,508     1314 KiB     34.5      19.5
chrome          32,953     4119 KiB    108.2       7.8
overview           909      114 KiB      3.0      39.0
servo           15,390     1924 KiB     50.5      19.5
servo-grip        3,035      379 KiB     10.0      39.0
supply           8,502     1063 KiB     27.9      19.5
supply-chrome       31,405     3926 KiB    103.1       7.8
analyser           855      107 KiB      2.8      39.0
logs               904      113 KiB      3.0      39.0
settings           844      106 KiB      2.8      39.0
battery            854      107 KiB      2.8      39.0
balance            846      106 KiB      2.8      39.0
programmer          874      109 KiB      2.9      39.0
balance-sim        2,376      297 KiB      7.8      39.0
settings-sim        2,384      298 KiB      7.8      39.0
battery-sim        2,380      298 KiB      7.8      39.0
analyser-chrome       39,184     4898 KiB    128.6       6.5
logs-chrome       16,020     2002 KiB     52.6      13.0
settings-chrome       23,595     2949 KiB     77.4       9.8
battery-chrome       36,937     4617 KiB    121.2       7.8
balance-chrome       40,650     5081 KiB    133.4       6.5
programmer-chrome       28,426     3553 KiB     93.3       9.8
picker             872      109 KiB      2.9      39.0
picker-chrome       15,842     1980 KiB     52.0      13.0
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
| `supply` | the supply screen with its output on, a sample landing every frame |
| `analyser`, `logs`, `settings`, `battery`, `balance`, `programmer`, `picker` | one steady frame of that screen, chrome cached |
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
whenever the bench numbers carry `LINK_BN_SIMULATED`, which includes a
coprocessor answering with simulated numbers. Rotated text scans its rotated
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
| the seven per-screen modes | 1,200 | a screen that has started repainting |
| the three `-sim` modes | 2,800 | the watermark growing past a full canvas |
| the eight `-chrome` modes | 45,000 | a full repaint growing |

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
| `main` | `main_task`, which calls `app_main` and runs the UI | 8,192 | 3,856 | 3,312 |
| `control` | `control_task` | 6,144 | 4,272 | 848 |
| `runlog` | `log_task` | 4,096 | 2,896 | 176 |
| `artkeep` | `art_keep_task` | 4,096 | 944 | 2,128 |
| `touch` | `touch_task`, the GT911 reader (`components/gt911`) | 4,096 | 1,744 | 1,328 |

Measured on ESP-IDF v5.4 at -O2. Of the margin, 528 bytes are spent outside
the frames: 320 for the FPU (floating-point unit) and vector-unit state saved
at the top of every stack, 192 for the frame an interrupt pushes, and 16 below
the deepest frame. The other
496 bytes cover what the tool cannot see, and each depth above is a lower
bound for that reason:

- calls through a function pointer, except the router's calls into a screen,
  which the tool reads out of every screen's table: 165 such calls are
  reachable from `main_task`, most of them in ESP-IDF's storage and display
  drivers;
- calls into the ESP32-S3's ROM (read-only memory), whose frames are not in
  the ELF;
- recursion, which the tool counts once.

`-v` lists all of them, and each task's deepest chain.

The UI keeps its frames small where it is cheap. A page's drawer is a
separate function, so only the page on screen holds its buffers: on
PROGRAMMER, `render()`'s frame is 32 bytes and the largest page's 464. A
line copied for display is copied without `snprintf()`, which reaches
newlib's float conversion: 1,952 bytes deep in the call graph.
