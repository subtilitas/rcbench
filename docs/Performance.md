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
frame              740       92 KiB      2.4      39.0
chrome          14,686     1836 KiB     48.2      19.5
overview           747       93 KiB      2.5      39.0
clear           12,004     1500 KiB     39.4      19.5
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

## What the shell costs

740 fills is 92 KiB per frame against a 976 KiB budget, so the shell as it
stands is not close to the limit: only the status band repaints, and the tiles
and stub copy are chrome. That headroom is the budget the instrument screens
will spend — a live plot redrawing a 760 × 210 region is where it goes.

CI holds the line at **12,000 fills** for `frame` and `overview`. That ceiling
is deliberately near the cost of a full-screen clear: a screen that exceeds it
is one that is repainting everything, which is the mistake worth catching
automatically rather than noticing on hardware.

The table above is checked by `frame_cost.py --check-doc`, to a tolerance
rather than byte for byte: absolute fills shift by a few between machines
because argv and the environment land in the same cache the framebuffer is
competing for.
