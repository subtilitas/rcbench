"""Trace the INA228 module's printed legends from the owner's top photo.

The photo is the owner's ina228_top.jpg, taken on squared paper; it is
not in the repository. _photo_grid.py lays it onto the board frame of
hardware/kicad/modules/ina228_bifrc_xt90.py (mm, y down) at 40 pixels
per mm.

LEGENDS lists a box round each legend. Inside a box the photo is split
into print and board by Otsu's threshold on each pixel's darkest colour
channel. Shapes under MIN_AREA are dropped. Each shape left is one
outline with its holes, simplified to within STRAIGHT of the traced
edge.

Usage:
    python3 hardware/mechanical/_trace_ina228_print.py PHOTOS
        [--preview OUT.png]

PHOTOS is the directory that holds the photo.

Writes hardware/mechanical/_ina228_print.py. Needs numpy and OpenCV
(pip install opencv-python-headless).
"""

import argparse
import sys
from pathlib import Path

import cv2
import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import _photo_grid  # noqa: E402

OUT = HERE / "_ina228_print.py"

MIN_AREA = 0.03         # mm2
STRAIGHT = 0.03         # mm
# Legend, then x0, y0, x1, y1 of its box on the board.
LEGENDS = (
    ("BIFRC V1.1", 11.6, 43.3, 30.6, 47.95),
    ("GND", 56.8, 3.6, 60.05, 5.2),
    ("5V", 58.0, 6.2, 60.05, 7.8),
    ("L", 55.35, 46.3, 56.45, 47.7),
    ("A", 57.95, 46.3, 59.05, 47.7),
    ("V", 60.55, 46.3, 61.65, 47.7),
    ("G", 63.20, 46.3, 64.30, 47.7),
    ("5", 65.80, 46.3, 66.90, 47.7),
)


def _simple(points):
    """Whether a closed polygon does not cross or touch itself."""
    n = len(points)
    if n < 3 or len({tuple(q) for q in points}) < n:
        return False

    def side(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])

    def within(a, b, c):
        return (min(a[0], b[0]) <= c[0] <= max(a[0], b[0])
                and min(a[1], b[1]) <= c[1] <= max(a[1], b[1]))

    for i in range(n):
        a, b = points[i], points[(i + 1) % n]
        for j in range(i + 2, n):
            if i == 0 and j == n - 1:
                continue
            c, d = points[j], points[(j + 1) % n]
            s1, s2 = side(a, b, c), side(a, b, d)
            s3, s4 = side(c, d, a), side(c, d, b)
            if s1 * s2 < 0 and s3 * s4 < 0:
                return False
            if ((s1 == 0 and within(a, b, c)) or (s2 == 0 and within(a, b, d))
                    or (s3 == 0 and within(c, d, a))
                    or (s4 == 0 and within(c, d, b))):
                return False
    return True


def trace(photos):
    """Find the legends.

    Args:
        photos: The directory that holds the photo.

    Returns:
        (shapes, flat): shapes as (legend, outline, holes) with the
        outlines as (x, y) points on the board in mm, and the photo
        laid onto the board.
    """
    flat, _matrix = _photo_grid.flat("top", photos)
    scale, lo = _photo_grid.SCALE, (-_photo_grid.MARGIN, -_photo_grid.MARGIN)
    dark = np.ascontiguousarray(flat.min(2))
    shapes = []
    for legend, x0, y0, x1, y1 in LEGENDS:
        c0, r0 = (int((x0 - lo[0]) * scale), int((y0 - lo[1]) * scale))
        c1, r1 = (int((x1 - lo[0]) * scale), int((y1 - lo[1]) * scale))
        _level, part = cv2.threshold(dark[r0:r1, c0:c1], 0, 1,
                                     cv2.THRESH_BINARY + cv2.THRESH_OTSU)
        # A frame of board keeps a shape off the box's edge; opening
        # removes bridges one pixel wide, which make an outline touch
        # itself.
        mask = np.zeros((r1 - r0 + 2, c1 - c0 + 2), np.uint8)
        mask[1:-1, 1:-1] = part
        mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN,
                                np.ones((3, 3), np.uint8))
        found, tree = cv2.findContours(mask, cv2.RETR_CCOMP,
                                       cv2.CHAIN_APPROX_NONE)

        def to_mm(contour, c0=c0, r0=r0):
            # Simplify less when the simplified outline crosses itself.
            for straight in (STRAIGHT, STRAIGHT / 2, STRAIGHT / 4):
                simple = cv2.approxPolyDP(contour, straight * scale,
                                          True)[:, 0, :]
                out = [(round(float((c - 1 + c0) / scale + lo[0]), 2),
                        round(float((r - 1 + r0) / scale + lo[1]), 2))
                       for c, r in simple]
                if _simple(out):
                    return out
            return None

        for k, contour in enumerate(found or ()):
            if tree[0][k][3] != -1:
                continue
            if cv2.contourArea(contour) < MIN_AREA * scale * scale:
                continue
            outline = to_mm(contour)
            if outline is None:
                continue
            holes, child = [], tree[0][k][2]
            while child != -1:
                if cv2.contourArea(found[child]) >= (
                        MIN_AREA * scale * scale):
                    hole = to_mm(found[child])
                    if hole is not None:
                        holes.append(hole)
                child = tree[0][child][0]
            shapes.append((legend, outline, holes))
    shapes.sort()
    return shapes, flat


def text(shapes):
    """The data file's text."""
    lines = [
        '"""The INA228 module\'s printed legends, traced from the photo.',
        "",
        "Written by _trace_ina228_print.py; do not edit. Each shape is",
        "(legend, outline, holes) with (x, y) points in mm in the board",
        "frame of hardware/kicad/modules/ina228_bifrc_xt90.py, y down.",
        '"""',
        "",
        "PRINT = (",
    ]
    for legend, outline, holes in shapes:
        lines.append(f"    ({legend!r},")
        lines.append("     " + _points(outline) + ",")
        lines.append("     (" + "".join(
            "\n      " + _points(h) + "," for h in holes) + ")),")
    lines.append(")")
    return "\n".join(lines) + "\n"


def _points(points):
    """Points as a tuple literal, wrapped at 79 columns."""
    out, line = [], "("
    for q in points:
        item = f"({q[0]}, {q[1]}), "
        if len(line) + len(item) > 72:
            out.append(line.rstrip())
            line = "       "
        line += item
    out.append(line.rstrip() + ")")
    return "\n".join(out)


def main(argv):
    """Run from the command line.

    Args:
        argv: Command-line arguments without the program name.

    Returns:
        0.
    """
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("photos", help="directory of ina228_top.jpg")
    parser.add_argument("--preview", help="write the traced shapes over "
                        "the photo to this file")
    args = parser.parse_args(argv)
    shapes, flat = trace(args.photos)
    OUT.write_text(text(shapes))
    print(f"wrote {OUT.relative_to(HERE.parents[1])}: {len(shapes)} shapes")
    if args.preview:
        scale, lo = _photo_grid.SCALE, (-_photo_grid.MARGIN,
                                        -_photo_grid.MARGIN)
        for _legend, outline, holes in shapes:
            for points, colour in ((outline, (0, 255, 0)),
                                   *((h, (0, 0, 255)) for h in holes)):
                pts = np.int32([[(x - lo[0]) * scale, (y - lo[1]) * scale]
                                for x, y in points])
                cv2.polylines(flat, [pts], True, colour, 1)
        cv2.imwrite(args.preview, flat)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
