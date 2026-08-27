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
frame           13,602     1700 KiB     44.6      19.5
sim             15,161     1895 KiB     49.8      19.5
chrome          24,450     3056 KiB     80.2       9.8
overview           751       94 KiB      2.5      39.0
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

**Cache the chrome.** Repainting everything every frame costs 14,686 fills
against **740** for the steady state — twenty times the traffic for pixels
that did not change. Every screen keeps a per-framebuffer bitmask of what it
has already painted, which is what the `buffer_index` argument to `render()`
is for. The panel alternates between two buffers, so a screen that invalidates
only the one being drawn leaves the other a frame behind — and with alternating
buffers that reads as *flicker* rather than as an obviously stale pixel.

## What the bench costs, and why 19.5 fps is the target

| | fills/frame | fps |
| --- | ---: | ---: |
| The menu, and any chrome-cached screen | **746** | 39.0 |
| The motor bench, live plot | **13,423** | 19.5 |
| The same, with the simulation watermark | **15,079** | 19.5 |
| Nothing cached at all | 24,268 | 9.8 |

The bench screen runs at half the panel's rate, and that is the design point
rather than a shortfall. The panel moves 976 KiB per frame at 39 Hz; a render
costing twice that lands at **19.5 fps — exactly one frame per 20 Hz telemetry
sample**. Drawing faster than the numbers arrive would repaint identical
pixels; drawing slower would drop samples on the floor.

So the ceiling is **15,600 fills** for the bench modes, which is that threshold
and not a preference: above it the panel can no longer deliver a frame per
sample, and that is the regression worth catching. Chrome-cached screens are
held to **2,000**, which catches a menu that has stopped caching.

The margin in simulation is thin on purpose rather than by accident: 15,079 of
15,600 is 97% of the budget. If a future pane needs room, the levers in order
of bluntness are the plot's height, skipping the plot on frames where no new
sample arrived, and narrowing the plot.

The table above is checked by `frame_cost.py --check-doc`, to a tolerance
rather than byte for byte: absolute fills shift by a few between machines
because argv and the environment land in the same cache the framebuffer is
competing for.
