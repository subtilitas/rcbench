#!/usr/bin/env python3
"""Generate the cat drawings the panel shows on screens with a free spot.

Each cat is drawn with Pillow at 4 times its size and scaled down with the
Lanczos filter, so its edges come out antialiased.  The result is cut to the
pixels it covers and written as a run-length encoded table of premultiplied
RGB565 (red, green, blue in five, six and five bits) with a 4-bit alpha,
which gfx_sprite_draw() (shared/gfx/include/gfx_sprite.h) blends onto a
canvas.

    python3 tools/gen_cats.py                 # regenerate the tables
    python3 tools/gen_cats.py --check         # the checked-in files agree
    python3 tools/gen_cats.py --preview DIR   # each pose decoded, as a PNG

The pose list is data: POSES holds one row per cat, with its drawing
function, its colours, the size it is drawn at and the spots it may take.
Adding or dropping a cat is one row there and a regeneration.  A spot is a
place on one screen (SPOTS); the screen says where its spots are and
ui_cats.c picks a pose among the ones that fit.

The generated files are checked in, so the firmware build needs no Python
and no Pillow.  CI (continuous integration) runs --check so the two cannot
drift apart.  The output depends on the Pillow version; CI pins 12.3.0.

Encoding, row by row from the top, each row exactly the sprite's width:
one byte per run, its top two bits the kind and its low six bits the run
length less one (1 to 64 pixels), followed by the run's data.

    0 SKIP   n transparent pixels; no data
    1 FILL   n opaque pixels of one colour; 2 bytes, RGB565 little-endian
    2 COPY   n opaque pixels; 2n bytes
    3 BLEND  n partly covered pixels; ceil(n/2) bytes of 4-bit alpha, first
             pixel in the low nibble, then 2n bytes of premultiplied RGB565

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import math
import pathlib
import sys
from dataclasses import dataclass
from typing import Callable

REPO = pathlib.Path(__file__).resolve().parent.parent
OUT_C = "shared/ui/ui_cats_art.c"
OUT_H = "shared/ui/include/ui_cats_art.h"

# The scale each cat is drawn at before it is scaled down.
S = 4

# The spots, in the order of their bits in a pose's mask.  A spot is one
# free place on one screen; the screen anchors it (ui_cats.h).
SPOTS = (
    "picker_left",    # OUTPUTS, bottom left, under the PINS table
    "picker_right",   # OUTPUTS, bottom right, under the IN USE table
)

RUN_SKIP, RUN_FILL, RUN_COPY, RUN_BLEND = 0, 1, 2, 3
RUN_MAX = 64
# A repeat of this many pixels or more is a FILL rather than part of a COPY.
FILL_MIN = 3

Rgb = tuple[int, int, int]


# --------------------------------------------------------------- drawing


def _box(*v: float) -> list[float]:
    """Scale coordinates from drawing units to the 4x canvas.

    Args:
        *v: Coordinates in drawing units.

    Returns:
        The same coordinates times S.
    """
    return [c * S for c in v]


def _pts(points) -> list[tuple[float, float]]:
    """Scale a list of points to the 4x canvas.

    Args:
        points: (x, y) pairs in drawing units.

    Returns:
        The pairs times S.
    """
    return [(x * S, y * S) for x, y in points]


def _canvas(w: int, h: int):
    """A transparent 4x canvas and a drawing context on it.

    Args:
        w: The cat's width in pixels.
        h: Its height in pixels.

    Returns:
        (image, draw) for a w*S x h*S RGBA image.
    """
    from PIL import Image, ImageDraw
    im = Image.new("RGBA", (w * S, h * S), (0, 0, 0, 0))
    return im, ImageDraw.Draw(im)


def _down(im, w: int, h: int):
    """Scale a 4x canvas down to the cat's size with the Lanczos filter.

    Args:
        im: The 4x RGBA image.
        w: The width to scale to.
        h: The height to scale to.

    Returns:
        The w x h RGBA image.
    """
    from PIL import Image
    return im.resize((w, h), Image.LANCZOS)


def sit(w: int, h: int, fur: Rgb, dark: Rgb, pink: Rgb):
    """A kitten sitting upright, its tail curled round on the right.

    Args:
        w: The width in pixels (196 holds the tail).
        h: The height in pixels.
        fur: The coat.
        dark: The outline, the eyes' ring, the mouth and the whiskers.
        pink: The inner ears and the nose.

    Returns:
        The RGBA image.
    """
    im, d = _canvas(w, h)
    line = {"outline": dark, "width": 3 * S}
    pts = []
    for t in range(0, 60):
        a = math.radians(200 + t * 4.2)
        r = 38 - t * 0.25
        pts.append(((118 + r * math.cos(a)) * S, (150 + r * math.sin(a)) * S))
    tail = [(x + 30 * S, y - 10 * S) for x, y in pts]
    d.line(tail, fill=dark, width=15 * S, joint="curve")
    d.line(tail, fill=fur, width=9 * S, joint="curve")
    d.ellipse(_box(38, 92, 132, 176), fill=fur, **line)
    d.ellipse(_box(62, 118, 108, 172), fill=(255, 236, 214))
    d.ellipse(_box(52, 160, 80, 178), fill=(255, 236, 214), **line)
    d.ellipse(_box(90, 160, 118, 178), fill=(255, 236, 214), **line)
    for sx in (1, -1):
        cx = 85
        p = [(cx + sx * 52, 48), (cx + sx * 44, 4), (cx + sx * 14, 30)]
        d.polygon(_pts(p), fill=fur, outline=dark, width=3 * S)
        q = [(cx + sx * 46, 40), (cx + sx * 42, 14), (cx + sx * 22, 31)]
        d.polygon(_pts(q), fill=pink)
    d.ellipse(_box(22, 18, 148, 112), fill=fur, **line)
    for dx in (-10, 0, 10):
        d.line([((85 + dx) * S, 22 * S), ((85 + dx * 0.6) * S, 34 * S)],
               fill=(230, 140, 70), width=4 * S)
    for cx in (60, 110):
        d.ellipse(_box(cx - 13, 52, cx + 13, 80), fill=(40, 30, 40))
        d.ellipse(_box(cx - 7, 56, cx + 1, 65), fill=(255, 255, 255))
        d.ellipse(_box(cx + 3, 68, cx + 7, 72), fill=(255, 255, 255))
    d.ellipse(_box(36, 78, 54, 90), fill=(255, 140, 150, 180))
    d.ellipse(_box(116, 78, 134, 90), fill=(255, 140, 150, 180))
    d.polygon([(80 * S, 80 * S), (90 * S, 80 * S), (85 * S, 86 * S)],
              fill=pink, outline=dark)
    d.arc(_box(76, 82, 86, 92), 0, 180, fill=dark, width=2 * S)
    d.arc(_box(84, 82, 94, 92), 0, 180, fill=dark, width=2 * S)
    for dy, a in ((0, 0), (6, 4)):
        d.line([(30 * S, (82 + dy) * S), (6 * S, (78 + dy + a) * S)],
               fill=dark, width=2 * S)
        d.line([(140 * S, (82 + dy) * S), (164 * S, (78 + dy + a) * S)],
               fill=dark, width=2 * S)
    return _down(im, w, h)


def sleep(w: int, h: int, fur: Rgb, dark: Rgb, pink: Rgb):
    """A kitten asleep on its side, eyes shut, with a "zZ" over it.

    Args:
        w: The width in pixels.
        h: The height in pixels.
        fur: The coat.
        dark: The outline and the shut eyes.
        pink: The inner ears and the nose.

    Returns:
        The RGBA image.
    """
    im, d = _canvas(w, h)
    line = {"outline": dark, "width": 3 * S}
    d.ellipse(_box(44, 44, 184, 106), fill=fur, **line)
    d.arc(_box(70, 58, 180, 112), 190, 330, fill=dark, width=3 * S)
    d.ellipse(_box(150, 70, 186, 98), fill=fur, **line)
    for p, q in (([(24, 40), (22, 8), (48, 26)],
                  [(28, 34), (26, 15), (42, 26)]),
                 ([(80, 26), (104, 8), (104, 40)],
                  [(86, 26), (100, 15), (100, 34)])):
        d.polygon(_pts(p), fill=fur, outline=dark, width=3 * S)
        d.polygon(_pts(q), fill=pink)
    d.ellipse(_box(14, 20, 114, 98), fill=fur, **line)
    for cx in (44, 84):
        d.arc(_box(cx - 10, 54, cx + 10, 68), 10, 170, fill=dark,
              width=3 * S)
    d.ellipse(_box(24, 68, 40, 78), fill=(255, 140, 150, 170))
    d.ellipse(_box(88, 68, 104, 78), fill=(255, 140, 150, 170))
    d.polygon([(60 * S, 70 * S), (68 * S, 70 * S), (64 * S, 75 * S)],
              fill=pink, outline=dark)
    for x, y, s in ((122, 22, 9), (138, 10, 12)):
        d.line([(x * S, y * S), ((x + s) * S, y * S), (x * S, (y + s) * S),
                ((x + s) * S, (y + s) * S)], fill=(150, 170, 230),
               width=2 * S)
    return _down(im, w, h)


def _face(d, cx: float, cy: float, r: float, fur: Rgb, dark: Rgb,
          pink: Rgb, eyes: str = "open", cheeks: bool = True) -> None:
    """A cat's head of radius @p r centred on (cx, cy), with its ears.

    Args:
        d: The drawing context on a 4x canvas.
        cx: The head's centre, drawing units.
        cy: Its centre's row.
        r: Its radius.
        fur: The coat.
        dark: The outline and the whiskers.
        pink: The inner ears and the nose.
        eyes: "open", "happy" (arcs up) or "closed" (arcs down).
        cheeks: Whether to draw the pink cheeks.
    """
    for s in (1, -1):
        p = [(cx + s * r * 0.85, cy - r * 0.45),
             (cx + s * r * 0.75, cy - r * 1.2),
             (cx + s * r * 0.2, cy - r * 0.8)]
        d.polygon(_pts(p), fill=fur, outline=dark, width=3 * S)
        q = [(cx + s * r * 0.74, cy - r * 0.55),
             (cx + s * r * 0.68, cy - r * 1.0),
             (cx + s * r * 0.35, cy - r * 0.78)]
        d.polygon(_pts(q), fill=pink)
    d.ellipse(_box(cx - r, cy - r * 0.85, cx + r, cy + r * 0.75), fill=fur,
              outline=dark, width=3 * S)
    ex = r * 0.4
    ey = cy - r * 0.05
    for e in (cx - ex, cx + ex):
        if eyes == "open":
            d.ellipse(_box(e - r * 0.2, ey - r * 0.22, e + r * 0.2,
                           ey + r * 0.22), fill=(40, 30, 40))
            d.ellipse(_box(e - r * 0.11, ey - r * 0.16, e + r * 0.01,
                           ey - r * 0.02), fill=(255, 255, 255))
        elif eyes == "happy":
            d.arc(_box(e - r * 0.16, ey - r * 0.05, e + r * 0.16,
                       ey + r * 0.22), 190, 350, fill=dark, width=3 * S)
        else:
            d.arc(_box(e - r * 0.16, ey - r * 0.1, e + r * 0.16,
                       ey + r * 0.12), 10, 170, fill=dark, width=3 * S)
    if cheeks:
        d.ellipse(_box(cx - r * 0.78, cy + r * 0.2, cx - r * 0.5,
                       cy + r * 0.38), fill=(255, 140, 150, 170))
        d.ellipse(_box(cx + r * 0.5, cy + r * 0.2, cx + r * 0.78,
                       cy + r * 0.38), fill=(255, 140, 150, 170))
    n = r * 0.08
    d.polygon(_pts([(cx - n, cy + r * 0.22), (cx + n, cy + r * 0.22),
                    (cx, cy + r * 0.3)]), fill=pink, outline=dark)
    for s in (1, -1):
        d.line(_pts([(cx + s * r * 0.75, cy + r * 0.3),
                     (cx + s * r * 1.25, cy + r * 0.25)]),
               fill=dark, width=2 * S)
        d.line(_pts([(cx + s * r * 0.75, cy + r * 0.4),
                     (cx + s * r * 1.25, cy + r * 0.48)]),
               fill=dark, width=2 * S)


def peek(w: int, h: int, fur: Rgb, dark: Rgb, pink: Rgb, edge: Rgb):
    """A cat's head and front paws over an edge drawn under them.

    Laid out at 170 x 90 and scaled by w / 170, the outlines kept at the
    3px every cat has; 170 x 90 is the owner's sheet of variations.

    Args:
        w: The width in pixels.
        h: The height in pixels, 90 x w / 170 for the sheet's proportions.
        fur: The coat.
        dark: The outline.
        pink: The inner ears and the nose.
        edge: The ledge it looks over, across the full width, 12px tall at
            170 x 90.

    Returns:
        The RGBA image.
    """
    f = w / 170
    im, d = _canvas(w, h)
    _face(d, 85 * f, 62 * f, 48 * f, fur, dark, pink)
    d.rectangle([0, (h - 12 * f) * S, w * S, h * S], fill=edge)
    for x in (40 * f, 130 * f):
        d.ellipse(_box(x - 16 * f, h - 22 * f, x + 16 * f, h + 6 * f),
                  fill=fur, outline=dark, width=3 * S)
    return _down(im, w, h)


def loaf(w: int, h: int, fur: Rgb, dark: Rgb, pink: Rgb, patch: Rgb):
    """A cat sitting as a loaf, paws tucked, eyes happy, with patches.

    Args:
        w: The width in pixels.
        h: The height in pixels.
        fur: The coat.
        dark: The outline.
        pink: The inner ears and the nose.
        patch: The patches on the back and the head.

    Returns:
        The RGBA image.
    """
    im, d = _canvas(w, h)
    d.rounded_rectangle(_box(20, 52, 160, 116), radius=30 * S, fill=fur,
                        outline=dark, width=3 * S)
    d.ellipse(_box(110, 56, 150, 90), fill=patch)
    _face(d, 62, 52, 40, fur, dark, pink, eyes="happy")
    d.ellipse(_box(34, 22, 56, 40), fill=patch)
    return _down(im, w, h)


def wave(w: int, h: int, fur: Rgb, dark: Rgb, pink: Rgb):
    """The sitting kitten with a paw raised beside its body.

    The paw is drawn on the scaled-down image, as in the owner's sheet of
    variations.

    Args:
        w: The width in pixels.
        h: The height in pixels.
        fur: The coat.
        dark: The outline.
        pink: The inner ears, the nose and the paw's pads.

    Returns:
        The RGBA image.
    """
    from PIL import ImageDraw
    im = sit(w, h, fur, dark, pink)
    d = ImageDraw.Draw(im)
    d.ellipse([132, 96, 160, 124], fill=fur, outline=dark, width=3)
    for k in (0, 1, 2):
        d.ellipse([137 + k * 7, 100, 142 + k * 7, 105], fill=pink)
    d.ellipse([140, 108, 152, 118], fill=pink)
    return im


# ------------------------------------------------------------- the poses

PINK = (255, 150, 170)


@dataclass(frozen=True)
class Pose:
    """One cat: how it is drawn and where it may sit.

    Attributes:
        name: The C name of its table, lower case.
        draw: The drawing function.
        size: (width, height) it is drawn at, before the cut to its pixels.
        colours: The drawing function's colour arguments.
        spots: The names in SPOTS it may take.
    """

    name: str
    draw: Callable
    size: tuple[int, int]
    colours: dict
    spots: tuple[str, ...]


# One row per cat, the owner's set of 2026-10-24.  The first is the one
# render_ui draws for the docs.  Sitting cats, the raised paw and the loaf
# take the bottom right of OUTPUTS; the sleeping and peeking ones, low
# and wide, the bottom left.
GINGER = {"fur": (255, 176, 96), "dark": (120, 70, 40), "pink": PINK}
POSES = (
    Pose("sit_ginger", sit, (196, 180), GINGER, ("picker_right",)),
    Pose("sit_black", sit, (196, 180),
         {"fur": (55, 55, 65), "dark": (15, 15, 22), "pink": PINK},
         ("picker_right",)),
    Pose("sit_white", sit, (196, 180),
         {"fur": (245, 245, 245), "dark": (110, 110, 120), "pink": PINK},
         ("picker_right",)),
    Pose("wave_grey", wave, (196, 180),
         {"fur": (120, 120, 135), "dark": (40, 40, 55), "pink": PINK},
         ("picker_right",)),
    Pose("sleep_grey", sleep, (190, 110),
         {"fur": (205, 205, 222), "dark": (70, 70, 95), "pink": PINK},
         ("picker_left",)),
    Pose("sleep_ginger", sleep, (190, 110), GINGER, ("picker_left",)),
    Pose("peek_black", peek, (170, 90),
         {"fur": (60, 60, 70), "dark": (20, 20, 28), "pink": PINK,
          "edge": (30, 58, 85)},
         ("picker_left",)),
    Pose("loaf_calico", loaf, (170, 120),
         {"fur": (240, 240, 240), "dark": (90, 90, 100), "pink": PINK,
          "patch": (240, 160, 80)},
         ("picker_right",)),
)


# -------------------------------------------------------------- encoding


def _q(v: float, top: int) -> int:
    """Round @p v (0 to 1) to an integer 0 to @p top.

    Args:
        v: The fraction.
        top: The largest value.

    Returns:
        round(v * top), clamped to 0..top.
    """
    return max(0, min(top, int(math.floor(v * top + 0.5))))


def pixels(im) -> tuple[int, int, int, int, list[list[tuple[int, int]]]]:
    """Quantise an RGBA image and cut it to the pixels it covers.

    Args:
        im: The RGBA image.

    Returns:
        (x0, y0, w, h, rows): the cut's offset in the image, its size and
        its rows, each pixel an (alpha 0 to 15, RGB565) pair.  The colour of
        an opaque pixel is plain RGB565, of a partly covered one RGB565
        premultiplied by its alpha, of a transparent one 0.
    """
    wi, hi = im.size
    data = im.load()
    grid = []
    for y in range(hi):
        row = []
        for x in range(wi):
            r, g, b, a8 = data[x, y]
            a = _q(a8 / 255.0, 15)
            f = a / 15.0
            if a == 0:
                row.append((0, 0))
                continue
            c = ((_q(r / 255.0 * f, 31) << 11) | (_q(g / 255.0 * f, 63) << 5)
                 | _q(b / 255.0 * f, 31))
            row.append((a, c))
        grid.append(row)
    ys = [y for y in range(hi) if any(a for a, _ in grid[y])]
    xs = [x for x in range(wi) if any(grid[y][x][0] for y in range(hi))]
    if not ys:
        raise ValueError("a pose with no pixel")
    x0, x1, y0, y1 = xs[0], xs[-1] + 1, ys[0], ys[-1] + 1
    rows = [grid[y][x0:x1] for y in range(y0, y1)]
    return x0, y0, x1 - x0, y1 - y0, rows


def _u16(c: int) -> bytes:
    """A colour as two bytes, little-endian.

    Args:
        c: The RGB565 colour.

    Returns:
        Its two bytes, low first.
    """
    return bytes((c & 0xFF, c >> 8))


def _kind(p: tuple[int, int]) -> int:
    """The run kind a pixel belongs to: SKIP, COPY (opaque) or BLEND.

    Args:
        p: (alpha, colour).

    Returns:
        RUN_SKIP, RUN_COPY or RUN_BLEND.
    """
    return RUN_SKIP if p[0] == 0 else (RUN_COPY if p[0] == 15 else RUN_BLEND)


def encode_row(row: list[tuple[int, int]]) -> bytes:
    """One row as runs.

    Args:
        row: (alpha, colour) pairs.

    Returns:
        The row's bytes.
    """
    out = bytearray()
    i, n = 0, len(row)
    while i < n:
        kind = _kind(row[i])
        j = i
        while j < n and j - i < RUN_MAX and _kind(row[j]) == kind:
            j += 1
        if kind == RUN_SKIP:
            out.append((RUN_SKIP << 6) | (j - i - 1))
        elif kind == RUN_BLEND:
            run = row[i:j]
            out.append((RUN_BLEND << 6) | (len(run) - 1))
            alphas = [a for a, _ in run] + [0]
            out += bytes(alphas[k] | (alphas[k + 1] << 4)
                         for k in range(0, len(run), 2))
            for _, c in run:
                out += _u16(c)
        else:
            j = _opaque(row, i, j, out)
        i = j
    return bytes(out)


def _repeat(row: list[tuple[int, int]], i: int, end: int) -> int:
    """How many pixels from @p i, up to @p end, share pixel i's colour.

    Args:
        row: (alpha, colour) pairs.
        i: The first pixel.
        end: One past the last pixel to look at.

    Returns:
        The repeat's length, at least 1.
    """
    k = i + 1
    while k < end and row[k][1] == row[i][1]:
        k += 1
    return k - i


def _opaque(row, i: int, j: int, out: bytearray) -> int:
    """Encode the opaque pixels row[i:j] as FILL and COPY runs.

    Args:
        row: (alpha, colour) pairs.
        i: The first opaque pixel.
        j: One past the last, at most RUN_MAX after @p i.
        out: The row's bytes, appended to.

    Returns:
        @p j.
    """
    while i < j:
        r = _repeat(row, i, j)
        if r >= FILL_MIN:
            out.append((RUN_FILL << 6) | (r - 1))
            out += _u16(row[i][1])
            i += r
            continue
        k = i
        while k < j and _repeat(row, k, j) < FILL_MIN:
            k += 1
        out.append((RUN_COPY << 6) | (k - i - 1))
        for _, c in row[i:k]:
            out += _u16(c)
        i = k
    return j


def encode(rows: list[list[tuple[int, int]]]) -> bytes:
    """Every row of a cut image as runs.

    Args:
        rows: The rows pixels() returns.

    Returns:
        The table's bytes.
    """
    return b"".join(encode_row(r) for r in rows)


def decode(data: bytes, w: int, h: int) -> list[list[tuple[int, int]]]:
    """Read a table back to (alpha, colour) rows, as gfx_sprite_draw() does.

    Args:
        data: The table.
        w: The sprite's width.
        h: Its height.

    Returns:
        The rows.

    Raises:
        ValueError: A run crosses the end of its row, or the table ends
            early or carries bytes past its last row.
    """
    rows, at = [], 0
    try:
        for _ in range(h):
            row: list[tuple[int, int]] = []
            while len(row) < w:
                head = data[at]
                at += 1
                kind, n = head >> 6, (head & 0x3F) + 1
                if len(row) + n > w:
                    raise ValueError("a run crosses the end of its row")
                if kind == RUN_SKIP:
                    row += [(0, 0)] * n
                elif kind == RUN_FILL:
                    c = data[at] | (data[at + 1] << 8)
                    at += 2
                    row += [(15, c)] * n
                elif kind == RUN_COPY:
                    for _k in range(n):
                        row.append((15, data[at] | (data[at + 1] << 8)))
                        at += 2
                else:
                    nib = data[at:at + (n + 1) // 2]
                    at += (n + 1) // 2
                    for k in range(n):
                        a = (nib[k // 2] >> (4 * (k & 1))) & 0xF
                        row.append((a, data[at] | (data[at + 1] << 8)))
                        at += 2
            rows.append(row)
    except IndexError:
        raise ValueError("the table ends inside a row")
    if at != len(data):
        raise ValueError("bytes after the last row")
    return rows


# ---------------------------------------------------------------- output


@dataclass
class Art:
    """One pose, drawn, cut and encoded.

    Attributes:
        pose: Its row of POSES.
        w: The cut's width in pixels.
        h: Its height.
        rows: Its (alpha, colour) rows.
        data: The encoded table.
    """

    pose: Pose
    w: int
    h: int
    rows: list
    data: bytes


def build(pose: Pose) -> Art:
    """Draw, cut and encode one pose.

    Args:
        pose: The row of POSES.

    Returns:
        The pose's Art.

    Raises:
        ValueError: The pose names a spot SPOTS does not hold, or draws
            nothing.
    """
    for s in pose.spots:
        if s not in SPOTS:
            raise ValueError(f"{pose.name}: no spot {s!r} in SPOTS")
    if not pose.spots:
        raise ValueError(f"{pose.name}: no spot")
    im = pose.draw(*pose.size, **pose.colours)
    _x0, _y0, w, h, rows = pixels(im)
    return Art(pose, w, h, rows, encode(rows))


def _mask(pose: Pose) -> int:
    """The bit mask of a pose's spots.

    Args:
        pose: The row of POSES.

    Returns:
        Bit i set for SPOTS[i].
    """
    return sum(1 << SPOTS.index(s) for s in pose.spots)


def render_c(arts: list[Art]) -> str:
    """The C file of every pose's table.

    Args:
        arts: Every pose, in POSES' order.

    Returns:
        The file's text.
    """
    total = sum(len(a.data) for a in arts)
    lines = [
        "/**",
        " * @file",
        " * @brief The cats' drawings, run-length encoded premultiplied"
        " RGB565",
        " *        with 4-bit alpha (gfx_sprite.h).",
        " *",
        " * Generated by tools/gen_cats.py -- do not edit by hand.",
        " *",
        f" * {len(arts)} poses, {total} bytes of table:",
    ]
    for a in arts:
        opaque = sum(1 for r in a.rows for p in r if p[0] == 15)
        part = sum(1 for r in a.rows for p in r if 0 < p[0] < 15)
        lines.append(f" *   {a.pose.name}: {a.w} x {a.h}px, {len(a.data)}"
                     f" bytes, {opaque} opaque and {part} partly covered"
                     " pixels")
    lines += [
        " *",
        " * SPDX-License-Identifier: MIT",
        " */",
        "",
        '#include "ui_cats.h"',
        "",
    ]
    for a in arts:
        lines.append(f"/** @brief {a.pose.name}: {a.w} x {a.h}px. */")
        lines.append(f"static const uint8_t s_{a.pose.name}[{len(a.data)}]"
                     " = {")
        for k in range(0, len(a.data), 16):
            lines.append("    " + ", ".join(f"0x{b:02X}"
                                            for b in a.data[k:k + 16]) + ",")
        lines.append("};")
        lines.append("")
    lines.append("const ui_cat_pose_t ui_cat_poses[UI_CAT_POSES] = {")
    for a in arts:
        spots = " | ".join(f"UI_CAT_IN({_spot_enum(s)})"
                           for s in a.pose.spots)
        lines.append(f'    {{ "{a.pose.name}", {{ {a.w}, {a.h}, '
                     f"{len(a.data)}u, s_{a.pose.name} }},")
        lines.append(f"      {spots} }},")
    lines.append("};")
    return "\n".join(lines) + "\n"


def _spot_enum(spot: str) -> str:
    """The C enumerator of a spot.

    Args:
        spot: A name in SPOTS.

    Returns:
        UI_CAT_SPOT_ and the name in capitals.
    """
    return "UI_CAT_SPOT_" + spot.upper()


def render_h(arts: list[Art]) -> str:
    """The header of the pose count and the spots.

    Args:
        arts: Every pose, in POSES' order.

    Returns:
        The file's text.
    """
    lines = [
        "/**",
        " * @file",
        " * @brief The number of cat poses and the spots they take: the part",
        " *        of ui_cats.h tools/gen_cats.py writes.",
        " *",
        " * Generated by tools/gen_cats.py -- do not edit by hand.",
        " *",
        " * SPDX-License-Identifier: MIT",
        " */",
        "#pragma once",
        "",
        f"/** @brief The poses in ui_cat_poses[], {len(arts)}. */",
        f"#define UI_CAT_POSES {len(arts)}",
        "",
        "/** @brief The free places on the screens a cat may take. */",
        "typedef enum {",
    ]
    for i, s in enumerate(SPOTS):
        lines.append(f"    {_spot_enum(s)} = {i},")
    lines += [
        f"    UI_CAT_SPOTS = {len(SPOTS)}, /**< the number of spots */",
        "} ui_cat_spot_id_t;",
    ]
    return "\n".join(lines) + "\n"


def preview(arts: list[Art], where: pathlib.Path) -> None:
    """Write each pose, decoded from its table, as a PNG.

    Args:
        arts: The poses.
        where: The directory to write into.
    """
    from PIL import Image
    where.mkdir(parents=True, exist_ok=True)
    for a in arts:
        im = Image.new("RGBA", (a.w, a.h))
        for y, row in enumerate(decode(a.data, a.w, a.h)):
            for x, (al, c) in enumerate(row):
                if al == 0:
                    continue
                f = al / 15.0
                r = ((c >> 11) & 31) * 255 / 31 / f
                g = ((c >> 5) & 63) * 255 / 63 / f
                b = (c & 31) * 255 / 31 / f
                im.putpixel((x, y), (min(255, round(r)), min(255, round(g)),
                                     min(255, round(b)), round(f * 255)))
        path = where / f"cat-{a.pose.name}.png"
        im.save(path)
        print(f"wrote {path}")


def main(argv: list[str] | None = None) -> int:
    """Regenerate, check or preview the cat tables.

    Args:
        argv: The command line, without the program name.

    Returns:
        0 on success, 1 when --check finds a file that differs.
    """
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true",
                    help="write nothing; fail when a checked-in file differs")
    ap.add_argument("--preview", type=pathlib.Path, metavar="DIR",
                    help="write each pose as a PNG into DIR")
    args = ap.parse_args(argv)
    arts = [build(p) for p in POSES]
    if args.preview:
        preview(arts, args.preview)
        return 0
    bad = 0
    for rel, text in ((OUT_C, render_c(arts)), (OUT_H, render_h(arts))):
        path = REPO / rel
        if args.check:
            have = path.read_text(encoding="utf-8") if path.exists() else None
            if have != text:
                print(f"{rel} is out of date: run tools/gen_cats.py",
                      file=sys.stderr)
                bad += 1
            else:
                print(f"{rel} is up to date")
        else:
            path.write_text(text, encoding="utf-8")
            print(f"wrote {rel}")
    if not args.check:
        for a in arts:
            print(f"  {a.pose.name}: {a.w} x {a.h}px, {len(a.data)} bytes")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
