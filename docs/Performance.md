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
frame           10,881     1360 KiB     35.7      19.5
frame-idle          895      112 KiB      2.9      39.0
sim             11,891     1486 KiB     39.0      19.5
chrome          30,559     3820 KiB    100.3       9.8
overview           889      111 KiB      2.9      39.0
servo           15,544     1943 KiB     51.0      19.5
servo-grip        2,986      373 KiB      9.8      39.0
analyser           835      104 KiB      2.7      39.0
logs               860      108 KiB      2.8      39.0
settings           830      104 KiB      2.7      39.0
battery            836      104 KiB      2.7      39.0
balance            822      103 KiB      2.7      39.0
programmer          815      102 KiB      2.7      39.0
balance-sim        2,408      301 KiB      7.9      39.0
settings-sim        2,417      302 KiB      7.9      39.0
battery-sim        2,417      302 KiB      7.9      39.0
analyser-chrome          850      106 KiB      2.8      39.0
logs-chrome       16,205     2026 KiB     53.2      13.0
settings-chrome       22,607     2826 KiB     74.2      13.0
battery-chrome          851      106 KiB      2.8      39.0
balance-chrome          833      104 KiB      2.7      39.0
programmer-chrome          833      104 KiB      2.7      39.0
clear           12,005     1501 KiB     39.4      19.5
vlines           8,160     1020 KiB     26.8      19.5
hlines               0        0 KiB      0.0      39.0
```
<!-- framecost:end -->

| Mode | What it measures |
| --- | --- |
| `frame` | the motor bench on a frame where a telemetry sample lands |
| `frame-idle` | the motor bench on a frame between samples, nothing touched |
| `sim` | as `frame`, with the SIMULATION watermark |
| `chrome` | the motor bench with nothing cached, repainted in full |
| `overview` | the menu, chrome cached |
| `servo` | the servo screen with the arm redrawn |
| `servo-grip` | the servo screen with only the grip repainted |
| `analyser`, `logs`, `settings`, `battery`, `balance`, `programmer` | one steady frame of that screen, chrome cached |
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
| `frame`, `sim` | 15,600 | a bench frame that exceeds one telemetry sample |
| `overview` | 2,000 | a chrome-cached screen that has started repainting |
| `servo` | 17,000 | the arm and grip drawing growing |
| `servo-grip` | 4,000 | a breath repainting the whole card |
| the six per-screen modes | 1,200 | a screen that has started repainting |
| the three `-sim` modes | 2,800 | the watermark growing past a full canvas |

If a future pane needs more room, the remaining levers in order of bluntness
are the plot's height, its width, and clipping the simulation watermark to the
region that was repainted.

The ESP32-S3 log line `DRAW … WAIT …`, printed every 300 frames, is the
on-hardware check: DRAW is the paint time, WAIT is how long the flip blocked. A
healthy frame is mostly WAIT.
