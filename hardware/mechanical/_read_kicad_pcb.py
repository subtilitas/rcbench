#!/usr/bin/env python3
"""Read a KiCad board file into the data a board spec draws from.

    python3 hardware/mechanical/_read_kicad_pcb.py BOARD.kicad_pcb NAME

writes hardware/mechanical/_NAME_data.py: the board's thickness, its
outline and round cut-outs from Edge.Cuts, and each footprint with its
place, its turn, whether it is surface-mounted, whether the file marks
it as not populated, its drilled pads and the box of its F.Fab drawing.
The board file is not in the repository; the data file is, so the model
builds without it.

Frame of the data: origin at the outline's lower left corner seen from
the top side, x to the right, y up, mm. KiCad's y points down; a
footprint's turn is counter-clockwise seen from the top side in both.

Read: gr_line and gr_circle on Edge.Cuts, and of each footprint its
"at", "attr", properties Reference and Value, pads with a drill, and
fp_line, fp_rect and fp_poly on F.Fab. An arc on Edge.Cuts or a
footprint on the bottom side stops the tool: neither is handled.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import math
import pprint
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def parse(text):
    """An s-expression as nested lists of strings."""
    stack = [[]]
    for m in re.finditer(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+', text):
        token = m.group(0)
        if token == "(":
            new = []
            stack[-1].append(new)
            stack.append(new)
        elif token == ")":
            stack.pop()
        else:
            stack[-1].append(token.strip('"'))
    return stack[0][0]


def kids(node, name):
    """The child lists of a node that start with ``name``."""
    return [c for c in node if isinstance(c, list) and c and c[0] == name]


def kid(node, name):
    """The first such child, or None."""
    found = kids(node, name)
    return found[0] if found else None


def _xy(node):
    return float(node[1]), float(node[2])


def _chain(lines):
    """Line segments joined end to end into one closed polygon."""
    def same(a, b):
        return math.dist(a, b) < 1e-4

    rest = list(lines)
    path = list(rest.pop(0))
    while rest:
        for i, (a, b) in enumerate(rest):
            if same(path[-1], a):
                path.append(b)
            elif same(path[-1], b):
                path.append(a)
            else:
                continue
            rest.pop(i)
            break
        else:
            raise ValueError("Edge.Cuts lines do not join")
    if not same(path[0], path[-1]):
        raise ValueError("Edge.Cuts outline is open")
    return path[:-1]


def _area(points):
    return sum(a[0] * b[1] - b[0] * a[1]
               for a, b in zip(points, points[1:] + points[:1],
                               strict=True)) / 2


def read(text):
    """The board's data in the frame the module describes.

    Args:
        text: The .kicad_pcb file's text.

    Returns:
        A dict: "thickness", "outline", "holes", "footprints".

    Raises:
        ValueError: For an arc on Edge.Cuts, an outline that does not
            close, or a footprint on the bottom side.
    """
    pcb = parse(text)
    edge = [g for name in ("gr_line", "gr_arc", "gr_circle", "gr_rect",
                           "gr_poly")
            for g in kids(pcb, name) if kid(g, "layer")[1] == "Edge.Cuts"]
    if any(g[0] not in ("gr_line", "gr_circle") for g in edge):
        raise ValueError("Edge.Cuts holds more than lines and circles")
    raw = _chain([(_xy(kid(g, "start")), _xy(kid(g, "end")))
                  for g in edge if g[0] == "gr_line"])
    x0 = min(x for x, _y in raw)
    y0 = max(y for _x, y in raw)

    def at(x, y):
        return (round(x - x0, 4), round(y0 - y, 4))

    outline = [at(*p) for p in raw]
    if _area(outline) < 0:
        outline.reverse()
    holes = sorted(
        at(*_xy(kid(g, "center"))) + (round(2 * math.dist(
            _xy(kid(g, "center")), _xy(kid(g, "end"))), 4),)
        for g in edge if g[0] == "gr_circle")
    footprints = []
    for fp in kids(pcb, "footprint"):
        props = {p[1]: p[2] for p in kids(fp, "property")}
        if kid(fp, "layer")[1] != "F.Cu":
            raise ValueError(f"{props['Reference']} is not on the top side")
        place = kid(fp, "at")
        fx, fy = _xy(place)
        turn = float(place[3]) if len(place) > 3 else 0.0
        c, s = math.cos(math.radians(turn)), math.sin(math.radians(turn))

        def absolute(x, y, c=c, s=s, fx=fx, fy=fy):
            return at(fx + x * c + y * s, fy - x * s + y * c)

        drills = []
        for pad in kids(fp, "pad"):
            drill = kid(pad, "drill")
            if drill:
                drills.append(absolute(*_xy(kid(pad, "at")))
                              + (float(drill[1]),))
        fab = []
        for name in ("fp_line", "fp_rect", "fp_poly"):
            for g in kids(fp, name):
                if kid(g, "layer")[1] != "F.Fab":
                    continue
                fab += [_xy(c) for c in g if isinstance(c, list)
                        and c[0] in ("start", "end")]
                fab += [_xy(q) for p in kids(g, "pts")
                        for q in kids(p, "xy")]
        xs, ys = [p[0] for p in fab], [-p[1] for p in fab]
        footprints.append({
            "ref": props["Reference"], "value": props["Value"],
            "footprint": fp[1], "at": at(fx, fy), "turn": turn,
            "smd": "smd" in (kid(fp, "attr") or []),
            "dnp": "dnp" in (kid(fp, "attr") or []),
            "drills": sorted(drills),
            "fab": tuple(round(v, 4) for v in (
                min(xs), min(ys), max(xs), max(ys))) if fab else None})

    def order(fp):
        letters, digits = re.match(r"(\D+)(\d+)", fp["ref"]).groups()
        return letters, int(digits)

    return {"thickness": float(kid(kid(pcb, "general"), "thickness")[1]),
            "outline": outline, "holes": holes,
            "footprints": sorted(footprints, key=order)}


def main(argv):
    """Write the data file.

    Args:
        argv: The board file and the data file's name.

    Returns:
        0, or 2 for wrong arguments.
    """
    if len(argv) != 2:
        print(__doc__)
        return 2
    board, name = argv
    data = read(Path(board).read_text(encoding="utf-8"))
    lines = [f'"""Data of {Path(board).name}, written by '
             '_read_kicad_pcb.py.', "",
             "Frame: origin at the outline's lower left corner seen from "
             "the top side,", "x to the right, y up, mm. A footprint's "
             '"fab" box is in its own frame,', "before its turn.", "",
             "SPDX-License-Identifier: CC-BY-NC-SA-4.0", '"""',
             "# ruff: noqa: E501", ""]
    for key in ("thickness", "outline", "holes", "footprints"):
        lines.append(f"{key.upper()} = " + pprint.pformat(
            data[key], width=79, sort_dicts=False))
    (HERE / f"_{name}_data.py").write_text("\n".join(lines) + "\n",
                                           encoding="utf-8")
    print(f"wrote _{name}_data.py: {len(data['footprints'])} footprints")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
