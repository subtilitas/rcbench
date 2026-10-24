#!/usr/bin/env python3
"""Generate the board drawings the IOmcu serves over the link.

The pin picker of the panel draws the IOmcu's board from a drawing the
IOmcu carries in its own image: the outline, the parts as flat shapes
and every pad with what it is.  A board brings its drawing in its firmware;
the panel needs no change for a new board.

    python3 tools/gen_board_drawing.py           # regenerate every board
    python3 tools/gen_board_drawing.py --check   # the checked-in files agree

The source is the panel drawing the configurator writes
(configurator/drawings/panel.json, `npm run drawings`), so the panel shows the
parts the configurator draws.  The C file is generated from it and checked in,
so the firmware build needs no Python and no Node.js; CI (continuous
integration) runs --check so the two cannot drift apart.

The payload is LINK_ART_DRAWING, laid out in shared/link/include/link_pages.h
under LINK_DRAW_*.  This script refuses a drawing the panel would refuse
(shared/link/link_drawing.c), so a bad drawing fails here and not on a bench.
The CRC (cyclic redundancy check) and the FNV-1a (Fowler-Noll-Vo) hash are
emitted beside the bytes: the panel checks a finished transfer against both
and keeps the drawing under both.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import struct
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent

# symbol, source JSON, output C file
BOARDS = [
    ("rp2350_can",
     "configurator/drawings/panel.json",
     "firmware/iomcu/src/board_drawing_rp2350_can.c"),
]

# shared/link/include/link_pages.h
BD_VERSION = 1
HEAD_BYTES = 16
PAD_BYTES = 8
SHAPE_BYTES = 12
MAX_PADS = 64
MAX_SHAPES = 400
MAX_BYTES = HEAD_BYTES + MAX_PADS * PAD_BYTES + MAX_SHAPES * SHAPE_BYTES
PAD_KINDS = {"ground": 1, "rail": 2, "other": 3, "gpio": 4}
SHAPE_KINDS = {"rect": 1, "rrect": 2, "disc": 3, "line": 4}
PAD_SQUARE = 0x01


class DrawingError(ValueError):
    """A drawing the panel would refuse."""


def link_crc(data: bytes) -> int:
    """CCITT polynomial 0x1021, seeded zero: link_crc() in link_crc.c."""
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 \
                else (crc << 1) & 0xFFFF
    return crc


def link_fnv1a32(data: bytes) -> int:
    """FNV-1a, 32 bits: link_fnv1a32(LINK_FNV_INIT, ...) in link_crc.c."""
    h = 0x811C9DC5
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def rgb565(colour: str) -> int:
    """'#rrggbb' as RGB565 (red, green, blue in five, six and five bits)."""
    if not (isinstance(colour, str) and len(colour) == 7 and colour[0] == "#"):
        raise DrawingError(f"colour {colour!r} is not #rrggbb")
    try:
        r, g, b = (int(colour[i:i + 2], 16) for i in (1, 3, 5))
    except ValueError as e:
        raise DrawingError(f"colour {colour!r} is not #rrggbb") from e
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def _u(value, bits: int, what: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) \
            or not 0 <= value < (1 << bits):
        raise DrawingError(f"{what} {value!r} is not a {bits}-bit number")
    return value


def _inside(lo: int, hi: int, limit: int) -> bool:
    return lo >= 0 and hi <= limit


def encode(drawing: dict) -> bytes:
    """The LINK_ART_DRAWING payload of a panel drawing, checked whole.

    Raises DrawingError, and no other exception, for anything the panel
    would refuse or this script cannot read.
    """
    try:
        return _encode(drawing)
    except KeyError as e:
        raise DrawingError(f"a record has no {e.args[0]}") from e
    except (TypeError, AttributeError) as e:
        raise DrawingError(f"a record is not an object: {e}") from e


def _encode(drawing: dict) -> bytes:
    width = _u(drawing["width"], 16, "width")
    height = _u(drawing["height"], 16, "height")
    corner = _u(drawing["corner"], 8, "corner")
    ring_r = _u(drawing["ringRadius"], 8, "ringRadius")
    hole_r = _u(drawing["holeRadius"], 8, "holeRadius")
    colour = rgb565(drawing["colour"])
    ring = rgb565(drawing["ring"])
    pads = drawing["pads"]
    shapes = drawing["shapes"]
    if not isinstance(pads, list) or not isinstance(shapes, list):
        raise DrawingError("pads and shapes are lists")
    if width == 0 or height == 0 or 2 * corner > min(width, height):
        raise DrawingError("an outline that is not one")
    if ring_r == 0 or hole_r >= ring_r:
        raise DrawingError("a pad ring with no ring left around its hole")
    if not 1 <= len(pads) <= MAX_PADS or len(shapes) > MAX_SHAPES:
        raise DrawingError(f"{len(pads)} pads and {len(shapes)} shapes: "
                           f"1 to {MAX_PADS} pads, up to {MAX_SHAPES} shapes")

    out = bytearray(struct.pack("<BBHHHHHBBBB", BD_VERSION, len(pads),
                                len(shapes), width, height, colour, ring,
                                corner, ring_r, hole_r, 0))
    last = 0
    for p in pads:
        n = _u(p["pad"], 8, "pad")
        if not 1 <= n <= 63:
            raise DrawingError(f"pad {n}: pads are 1 to 63")
        if n <= last:
            raise DrawingError(f"pad {n}: the pads are not in ascending order")
        last = n
        kind = PAD_KINDS.get(p["kind"])
        if kind is None:
            raise DrawingError(f"pad {n}: kind {p['kind']!r} is not one of "
                               f"{sorted(PAD_KINDS)}")
        value = _u(p["value"], 8, f"pad {n} value")
        if kind in (PAD_KINDS["ground"], PAD_KINDS["other"]) and value != 0:
            raise DrawingError(f"pad {n}: a {p['kind']} pad carries no value")
        x = _u(p["x"], 16, f"pad {n} x")
        y = _u(p["y"], 16, f"pad {n} y")
        if not (_inside(x - ring_r, x + ring_r, width)
                and _inside(y - ring_r, y + ring_r, height)):
            raise DrawingError(f"pad {n}: the ring leaves the outline")
        square = p.get("square", False)
        if not isinstance(square, bool):
            raise DrawingError(f"pad {n}: square is true or false")
        flags = PAD_SQUARE if square else 0
        out += struct.pack("<BBBBHH", n, kind, value, flags, x, y)

    for i, s in enumerate(shapes):
        kind = SHAPE_KINDS.get(s["kind"])
        if kind is None:
            raise DrawingError(f"shape {i}: kind {s['kind']!r} is not one of "
                               f"{sorted(SHAPE_KINDS)}")
        param = _u(s["param"], 8, f"shape {i} param")
        a, b, c, d = (_u(s[k], 16, f"shape {i} {k}") for k in "abcd")
        if s["kind"] in ("rect", "rrect"):
            ok = (c > 0 and d > 0 and 2 * param <= min(c, d)
                  and (param == 0 or s["kind"] == "rrect")
                  and _inside(a, a + c, width) and _inside(b, b + d, height))
        elif s["kind"] == "disc":
            ok = (param == 0 and c > 0 and d == 0
                  and _inside(a - c, a + c, width)
                  and _inside(b - c, b + c, height))
        else:
            h = (param + 1) // 2
            ok = (param > 0
                  and _inside(min(a, c) - h, max(a, c) + h, width)
                  and _inside(min(b, d) - h, max(b, d) + h, height))
        if not ok:
            raise DrawingError(f"shape {i} ({s['kind']}) is out of place or "
                               f"leaves the outline")
        out += struct.pack("<BBHHHHH", kind, param, rgb565(s["colour"]),
                           a, b, c, d)
    assert len(out) <= MAX_BYTES
    return bytes(out)


def _row(data: bytes) -> str:
    return "    " + ", ".join(f"0x{b:02X}" for b in data) + ","


def render(name: str, src: str, drawing: dict) -> str:
    """The C file for one board: the payload a record a line, commented."""
    payload = encode(drawing)
    pads = drawing["pads"]
    shapes = drawing["shapes"]
    w, h, n = drawing["width"], drawing["height"], len(payload)
    sym = f"board_drawing_{name}"
    lines = [
        "/*",
        " * Generated by tools/gen_board_drawing.py -- do not edit by hand.",
        " *",
        f" * {drawing.get('name', name)}: the board drawing the IOmcu"
        " serves over",
        " * LINK_PAGE_ART_DATA, LINK_ART_DRAWING as link_pages.h lays it out.",
        f" * Outline {w} x {h} (0.01mm), {len(pads)} pads, {len(shapes)}"
        f" shapes, {n} bytes.",
        f" * Source: {src}",
        " *",
        " * SPDX-License-Identifier: MIT",
        " */",
        "",
        "#include <stdint.h>",
        "",
        f"const uint16_t {sym}_width  = {w}u;",
        f"const uint16_t {sym}_height = {h}u;",
        f"const uint32_t {sym}_bytes  = {n}u;",
        f"const uint16_t {sym}_crc    = 0x{link_crc(payload):04X}u;",
        f"const uint32_t {sym}_hash   = 0x{link_fnv1a32(payload):08X}uL;",
        "",
        f"const uint8_t {sym}[{n}] = {{",
        f"    /* header: version {BD_VERSION}, {len(pads)} pads,"
        f" {len(shapes)} shapes, {w} x {h},"
        f" board {drawing['colour']}, ring {drawing['ring']} */",
        _row(payload[:HEAD_BYTES]),
    ]
    at = HEAD_BYTES
    for p in pads:
        if p["kind"] == "gpio":
            what = f"GPIO {p['value']}"
        elif p["kind"] == "rail" and p["value"]:
            what = f"rail {p['value'] / 10:.1f}V"
        else:
            what = p["kind"]
        sq = ", square" if p.get("square") else ""
        lines.append(f"    /* pad {p['pad']}: {what}{sq},"
                     f" x {p['x']}, y {p['y']} */")
        lines.append(_row(payload[at:at + PAD_BYTES]))
        at += PAD_BYTES
    for i, s in enumerate(shapes):
        lines.append(f"    /* shape {i}: {s['kind']} {s['colour']}"
                     f" {s['param']}, {s['a']} {s['b']} {s['c']} {s['d']} */")
        lines.append(_row(payload[at:at + SHAPE_BYTES]))
        at += SHAPE_BYTES
    lines.append("};")
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true",
                    help="write nothing; fail when a checked-in file differs")
    args = ap.parse_args(argv)
    bad = 0
    for name, src, dst in BOARDS:
        try:
            drawing = json.loads((REPO / src).read_text(encoding="utf-8"))
            text = render(name, src, drawing)
        except (OSError, ValueError, KeyError, TypeError) as e:
            print(f"{src}: {e}", file=sys.stderr)
            bad += 1
            continue
        path = REPO / dst
        if args.check:
            have = path.read_text(encoding="utf-8") if path.exists() else None
            if have != text:
                print(f"{dst} differs from {src}:"
                      " run tools/gen_board_drawing.py", file=sys.stderr)
                bad += 1
        else:
            path.write_text(text, encoding="utf-8")
            print(f"wrote {dst}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
