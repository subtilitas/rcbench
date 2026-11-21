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

Copper of the 2 outer layers: PADS_TOP and PADS_BOTTOM hold the pads,
COPPER_TOP and COPPER_BOTTOM the tracks, the vias and the zones' filled
areas less the pads. Each is a list of (outline, holes); every drilled
hole is cut out, so no outline crosses another and none covers a hole.
Round ends are polygons of 32 sides to a circle; points nearer than
0.01mm to the line through their neighbours are dropped. Pads are
circle, oval, rect or roundrect; another shape stops the tool. Inner
layers are not read. This part needs shapely (pip install
shapely==2.2.0).

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


def _pad_shape(pad, geometry):
    """A pad's copper as a polygon round (0, 0), unturned."""
    kind = pad[3]
    wide, high = _xy(kid(pad, "size"))
    if kind == "rect":
        return geometry.box(-wide / 2, -high / 2, wide / 2, high / 2)
    if kind == "circle":
        return geometry.Point(0.0, 0.0).buffer(wide / 2, quad_segs=8)
    if kind == "oval":
        r = min(wide, high) / 2
    elif kind == "roundrect":
        r = float(kid(pad, "roundrect_rratio")[1]) * min(wide, high)
    else:
        raise ValueError(f"pad shape {kind} is not handled")
    return geometry.box(-wide / 2 + r, -high / 2 + r, wide / 2 - r,
                        high / 2 - r).buffer(r, quad_segs=8)


def copper(pcb, at):
    """Pads and other copper of the 2 outer layers as polygons.

    Args:
        pcb: The parsed board.
        at: The move from KiCad's (x, y) to the data's frame.

    Returns:
        A dict: "pads_top", "pads_bottom", "copper_top", "copper_bottom",
        each a list of (outline, holes).
    """
    from shapely import affinity, geometry, make_valid, ops

    pads = {"F.Cu": [], "B.Cu": []}
    rest = {"F.Cu": [], "B.Cu": []}
    drills = []
    for fp in kids(pcb, "footprint"):
        place = kid(fp, "at")
        fx, fy = _xy(place)
        turn = math.radians(float(place[3]) if len(place) > 3 else 0.0)
        c, s = math.cos(turn), math.sin(turn)
        for pad in kids(fp, "pad"):
            where = kid(pad, "at")
            px, py = _xy(where)
            x, y = fx + px * c + py * s, fy - px * s + py * c
            angle = float(where[3]) if len(where) > 3 else 0.0
            shape = affinity.translate(affinity.rotate(
                _pad_shape(pad, geometry), -angle, origin=(0, 0)), x, y)
            layers = kid(pad, "layers")[1:]
            for layer, found in pads.items():
                if layer in layers or "*.Cu" in layers:
                    found.append(shape)
            drill = kid(pad, "drill")
            if drill:
                drills.append(geometry.Point(x, y).buffer(
                    float(drill[1]) / 2, quad_segs=8))
    for g in kids(pcb, "gr_circle"):
        if kid(g, "layer")[1] == "Edge.Cuts":
            centre = _xy(kid(g, "center"))
            drills.append(geometry.Point(*centre).buffer(
                math.dist(centre, _xy(kid(g, "end"))), quad_segs=8))
    for seg in kids(pcb, "segment"):
        layer = kid(seg, "layer")[1]
        if layer in rest:
            rest[layer].append(geometry.LineString(
                [_xy(kid(seg, "start")), _xy(kid(seg, "end"))]).buffer(
                    float(kid(seg, "width")[1]) / 2, quad_segs=8))
    for via in kids(pcb, "via"):
        for found in rest.values():
            found.append(geometry.Point(*_xy(kid(via, "at"))).buffer(
                float(kid(via, "size")[1]) / 2, quad_segs=8))
    for zone in kids(pcb, "zone"):
        for filled in kids(zone, "filled_polygon"):
            layer = kid(filled, "layer")[1]
            if layer in rest:
                rest[layer].append(make_valid(geometry.Polygon(
                    [_xy(q) for q in kids(kid(filled, "pts"), "xy")])))
    holes = ops.unary_union(drills)

    def polygons(shape):
        shape = shape.simplify(0.01)
        out = []
        for part in getattr(shape, "geoms", [shape]):
            if part.geom_type != "Polygon" or part.area < 0.01:
                continue
            out.append(([at(*q) for q in part.exterior.coords[:-1]],
                        [[at(*q) for q in ring.coords[:-1]]
                         for ring in part.interiors]))
        return sorted(out)

    out = {}
    for layer, side in (("F.Cu", "top"), ("B.Cu", "bottom")):
        own = ops.unary_union(pads[layer])
        out[f"pads_{side}"] = polygons(own.difference(holes))
        # The other copper keeps 0.02 from pads and holes, so no solid
        # of one touches a solid of the other along a line.
        out[f"copper_{side}"] = polygons(ops.unary_union(
            rest[layer]).difference(holes.buffer(0.02)).difference(
                own.buffer(0.02)))
    return out


def read(text):
    """The board's data in the frame the module describes.

    Args:
        text: The .kicad_pcb file's text.

    Returns:
        A dict: "thickness", "outline", "holes", "footprints", and the
        4 lists of ``copper``.

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
            "footprints": sorted(footprints, key=order),
            **copper(pcb, at)}


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
    for key in ("pads_top", "pads_bottom", "copper_top", "copper_bottom"):
        lines.append(f"{key.upper()} = [")
        lines += [f"    {polygon!r}," for polygon in data[key]]
        lines.append("]")
    (HERE / f"_{name}_data.py").write_text("\n".join(lines) + "\n",
                                           encoding="utf-8")
    print(f"wrote _{name}_data.py: {len(data['footprints'])} footprints")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
