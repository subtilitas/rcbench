#!/usr/bin/env python3
# SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""Write _font.py, the outlines of the letters printed on boards.

The letters are OpenCV's single-stroke Hershey font, the kind of font
board print is drawn in. Each letter is drawn SIZE pixels high with
strokes STROKE of the height wide, and its edge is traced and
simplified to within STRAIGHT of the height. A capital letter is 1
high and stands on y = 0. The digit 0 is the letter O at ZERO of its
width: the font's own 0 has a stroke through it, board print has
none.

Usage:
    python3 hardware/mechanical/_gen_font.py [--preview OUT.png]

Writes hardware/mechanical/_font.py. Needs numpy and OpenCV (pip
install opencv-python-headless).
"""

import argparse
import string
import sys
from pathlib import Path

import cv2
import numpy as np

HERE = Path(__file__).resolve().parent
OUT = HERE / "_font.py"

LETTERS = string.ascii_letters + string.digits + "+-:/&.%"
FACE = cv2.FONT_HERSHEY_SIMPLEX
SIZE = 210              # pixels, height of a capital letter
STROKE = 0.11           # of the height
STRAIGHT = 0.02         # of the height
SPACE = 0.55            # of the height, width of a blank
ZERO = 0.85             # width of the digit 0 as a part of the letter O


def glyph(letter):
    """Trace one letter.

    Args:
        letter: The character.

    Returns:
        (advance, shapes): the step to the next letter, and a list of
        (outline, holes) with (x, y) points, y up from the baseline, a
        capital letter 1 high.
    """
    if letter == "0":
        advance, shapes = glyph("O")

        def narrow(points):
            return [(round(x * ZERO, 3), y) for x, y in points]

        return round(advance * ZERO, 3), [
            (narrow(outline), [narrow(h) for h in holes])
            for outline, holes in shapes]
    scale = SIZE / 21.0
    thick = max(1, round(STROKE * SIZE))
    (width, _height), _base = cv2.getTextSize(letter, FACE, scale, 1)
    pad = 2 * thick + SIZE
    image = np.zeros((SIZE + 2 * pad, width + 2 * pad), np.uint8)
    base = pad + SIZE
    cv2.putText(image, letter, (pad, base), FACE, scale, 255, thick,
                cv2.LINE_AA)
    _level, mask = cv2.threshold(image, 127, 1, cv2.THRESH_BINARY)
    found, tree = cv2.findContours(mask, cv2.RETR_CCOMP,
                                   cv2.CHAIN_APPROX_NONE)

    def points(contour):
        simple = cv2.approxPolyDP(contour, STRAIGHT * SIZE, True)[:, 0, :]
        return [(round(float(c - pad) / SIZE, 3),
                 round(float(base - r) / SIZE, 3)) for c, r in simple]

    shapes = []
    for k, contour in enumerate(found):
        if tree[0][k][3] != -1:
            continue
        holes, child = [], tree[0][k][2]
        while child != -1:
            holes.append(points(found[child]))
            child = tree[0][child][0]
        shapes.append((points(contour), holes))
    shapes.sort()
    return round(width / SIZE, 3), shapes


def _points(points, indent):
    """Points as a tuple literal, wrapped at 79 columns."""
    out, line = [], indent + "("
    for q in points:
        item = f"({q[0]}, {q[1]}), "
        if len(line) + len(item) > 78:
            out.append(line.rstrip())
            line = indent + " "
        line += item
    out.append(line.rstrip() + "),")
    return "\n".join(out)


def text():
    """The data file's text."""
    lines = [
        "# SPDX-License-Identifier: CC-BY-NC-SA-4.0",
        '"""Outlines of the letters printed on boards.',
        "",
        "Written by _gen_font.py; do not edit. GLYPHS maps a character",
        "to (advance, shapes); each shape is (outline, holes) with (x, y)",
        "points, y up from the baseline, a capital letter 1 high. SPACE",
        "is the width of a blank.",
        '"""',
        "",
        f"SPACE = {SPACE}",
        "",
        "GLYPHS = {",
    ]
    for letter in LETTERS:
        advance, shapes = glyph(letter)
        lines.append(f"    {letter!r}: ({advance}, (")
        for outline, holes in shapes:
            lines.append("        (")
            lines.append(_points(outline, "         "))
            lines.append("         (")
            lines += [_points(h, "          ") for h in holes]
            lines.append("         )),")
        lines.append("    )),")
    lines.append("}")
    return "\n".join(lines) + "\n"


def main(argv):
    """Run from the command line.

    Args:
        argv: Command-line arguments without the program name.

    Returns:
        0.
    """
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--preview", help="draw every letter's outline "
                        "to this file")
    args = parser.parse_args(argv)
    OUT.write_text(text())
    print(f"wrote {OUT.relative_to(HERE.parents[1])}")
    if args.preview:
        k, cols = 60, 16
        rows = -(-len(LETTERS) // cols)
        image = np.full((rows * 2 * k, cols * 2 * k, 3), 255, np.uint8)
        for i, letter in enumerate(LETTERS):
            ox, oy = (i % cols) * 2 * k + k // 2, (i // cols) * 2 * k + 90
            for outline, holes in glyph(letter)[1]:
                for pts, colour in ((outline, (0, 0, 0)),
                                    *((h, (0, 0, 255)) for h in holes)):
                    cv2.polylines(image, [np.int32(
                        [[ox + x * k, oy - y * k] for x, y in pts])],
                        True, colour, 1)
        cv2.imwrite(args.preview, image)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
