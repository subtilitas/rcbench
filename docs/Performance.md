# Performance

The frame rate on this panel is not decided by the CPU and not by the flip. It
is decided by **PSRAM bandwidth**.

The LCD peripheral scans one framebuffer out of PSRAM continuously at about
30 MB/s, and the framebuffer sits behind a write-back, write-allocate cache —
so every pixel the CPU writes costs 128 bytes of bus traffic unless its
64-byte line is already resident. What a frame costs is therefore a count of
cache-line fills, not a wall-clock number, and it can be measured exactly
rather than timed approximately.

`tools/frame_cost.py` does that: it builds the real screens for the host and
runs them under cachegrind, modelling the ESP32-S3's data cache (64 KiB,
8-way, 64-byte lines). It measures the *difference* between rendering zero
frames and rendering many, so process start-up, the first paint into each
buffer and cachegrind's own overhead cancel out.

<!-- framecost:start -->
```
$ python3 tools/frame_cost.py
panel 39.0 Hz, ~39 MB/s effective -> 976 KiB of traffic per panel frame

mode       lines/frame     traffic   est. ms  est. fps
-------------------------------------------------------
frame           10,891     1361 KiB     35.7      19.5
frame-idle          976      122 KiB      3.2      39.0
sim             11,705     1463 KiB     38.4      19.5
chrome          30,440     3805 KiB     99.9       9.8
overview           945      118 KiB      3.1      39.0
servo           15,467     1933 KiB     50.8      19.5
servo-grip        2,992      374 KiB      9.8      39.0
clear           12,006     1501 KiB     39.4      19.5
vlines           8,160     1020 KiB     26.8      19.5
hlines               0        0 KiB      0.0      39.0
```
<!-- framecost:end -->

## The two findings that shape every screen

**Draw row-major.** Seventeen full-height vertical lines cost 8,160 cache-line
fills. The identical pixel count drawn as horizontal lines costs **zero** —
each fill is already resident from the pixel before it. This is why the shell
stratifies into horizontal bands rather than columns, and why a vertical rule
is a deliberate expense rather than a free separator.

**Cache the chrome.** Repainting everything every frame costs 24,896 fills
against **740** for the steady state — thirty times the traffic for pixels
that did not change. Every screen keeps a per-framebuffer bitmask of what it
has already painted, which is what the `buffer_index` argument to `render()`
is for. The panel alternates between two buffers, so a screen that invalidates
only the one being drawn leaves the other a frame behind — and with alternating
buffers that reads as *flicker* rather than as an obviously stale pixel.

## What the bench costs, and why 19.5 fps is the target

| | fills/frame | fps |
| --- | ---: | ---: |
| The menu, and any chrome-cached screen | **821** | 39.0 |
| The motor bench, between samples | **740** | 39.0 |
| The motor bench, on the frame a sample lands | **10,264** | 19.5 |
| The same, with the simulation watermark | **11,825** | 19.5 |
| Nothing cached at all | 24,896 | 9.8 |

The bench screen runs at half the panel's rate, and that is the design point
rather than a shortfall. The panel moves 976 KiB per frame at 39 Hz; a render
costing twice that lands at **19.5 fps — exactly one frame per 20 Hz telemetry
sample**. Drawing faster than the numbers arrive would repaint identical
pixels; drawing slower would drop samples on the floor.

So the ceiling is **15,600 fills** for the bench modes, which is that threshold
and not a preference: above it the panel can no longer deliver a frame per
sample, and that is the regression worth catching. Chrome-cached screens are
held to **2,000**, which catches a menu that has stopped caching.

**Only paint on the frames that have something to paint.** Samples arrive at
20 Hz and the panel refreshes at 39, so roughly every other frame has nothing
new to show. The bench screen keeps the plot's push count and a control
revision, each per framebuffer, and repaints the plot, the readouts and the
controls only when the corresponding counter has moved. A frame between two
samples with nobody touching the screen costs **740 fills** — the cached
chrome and nothing else.

That is the change that bought the margin the table now shows: the worst-case
frame in simulation went from 15,603 to 11,825 against the 15,600 ceiling, and
the typical frame from 14,042 to 740. It matters more than the averages
suggest, because the traffic being avoided is not merely wasted — it is
contending with the LCD's own scan-out for the same PSRAM, which is what made
the first hardware boot tear.

The counters are per framebuffer for the reason given above: the panel
alternates between two, so a buffer whose last paint was a sample ago still
needs one even when the other is current. `test_motor`'s
`each_framebuffer_is_updated_independently` pins that, and collapsing either
counter to a single slot fails it.

If a future pane needs more room still, the remaining levers in order of
bluntness are the plot's height, its width, and clipping the simulation
watermark to the region that was actually repainted.

The table above is checked by `frame_cost.py --check-doc`, to a tolerance
rather than byte for byte: absolute fills shift by a few between machines
because argv and the environment land in the same cache the framebuffer is
competing for.
