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
frame           10,837     1355 KiB     35.6      19.5
frame-idle          904      113 KiB      3.0      39.0
sim             11,645     1456 KiB     38.2      19.5
chrome          30,338     3792 KiB     99.6       9.8
overview           896      112 KiB      2.9      39.0
servo           15,417     1927 KiB     50.6      19.5
servo-grip        2,956      370 KiB      9.7      39.0
analyser           839      105 KiB      2.8      39.0
logs               880      110 KiB      2.9      39.0
settings           839      105 KiB      2.8      39.0
battery            830      104 KiB      2.7      39.0
balance            833      104 KiB      2.7      39.0
programmer          850      106 KiB      2.8      39.0
balance-sim        2,188      274 KiB      7.2      39.0
settings-sim        2,202      275 KiB      7.2      39.0
battery-sim        2,196      274 KiB      7.2      39.0
analyser-chrome          851      106 KiB      2.8      39.0
logs-chrome       16,000     2000 KiB     52.5      13.0
settings-chrome       22,668     2834 KiB     74.4      13.0
battery-chrome          842      105 KiB      2.8      39.0
balance-chrome          850      106 KiB      2.8      39.0
programmer-chrome          869      109 KiB      2.9      39.0
clear           12,006     1501 KiB     39.4      19.5
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

**The watermark is paid on every frame.** SIMULATION blends over the whole
canvas rather than writing, so it costs 1,355 fills, 169 KiB and about 4.4 ms
whatever else the frame does: an idle screen goes from 2.8 ms to 7.2 ms. It is
drawn whenever the bench numbers carry `LINK_BN_SIMULATED`, which includes a
coprocessor answering with simulated numbers.

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
| the three `-sim` modes | 2,500 | the watermark growing past a full canvas |

If a future pane needs more room, the remaining levers in order of bluntness
are the plot's height, its width, and clipping the simulation watermark to the
region that was repainted.

The ESP32-S3 log line `DRAW … WAIT …`, printed every 300 frames, is the
on-hardware check: DRAW is the paint time, WAIT is how long the flip blocked. A
healthy frame is mostly WAIT.
