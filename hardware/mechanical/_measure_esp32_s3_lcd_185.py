#!/usr/bin/env python3
"""Measure Waveshare's STEP file of the ESP32-S3-Touch-LCD-1.85.

The file is ESP32-S3-Touch-LCD-1_85-20260703.stp from
ESP32-S3-Touch-LCD-1.85.rar ("Mechanical Files" on the board's page at
docs.waveshare.com). Waveshare states no licence for it, so it is not in
this repository; this tool reads it with OpenCASCADE and writes the
dimensions esp32_s3_lcd_185.py draws the board from.

Frame of the output, mm: the board seen from its back, the component
side. The origin is the center of the board's round outline; z = 0 on
the component side and +z away from the display; x to the right and y
up with the USB Type-C socket at the bottom. The file's own frame has
z = 0 on the glass's front face less 0.70 and the parts at -z; this
frame is the file's turned 180 degrees about its y axis and moved along
z by the board's back face, z = -5.00 in the file.

Written:

- the board's outline and holes, and the spacer ring's, as polygons;
  the outline's arcs are the file's own short lines, simplified to
  SIMPLIFY;
- CHIPS and CHIPS_FRONT: (kind, x, y, turn) of each chip part on the
  back and on the front, by _measure_step.py's sizes, matched against
  the least rectangle round the part, so a turned part is found too;
- CAPPED: (length, width, height, colour, x, y, turn, side) of each
  other part of 3 solids that is a body with a cap at each end of its
  length, side "back" or "front";
- LEADED: (body box, feet) of each leaded package, as _measure_step.py
  finds them, less a package whose feet overlap: its "feet" are its
  base;
- NAMED: the solids of the USB Type-C socket, the card slot and the 3
  threaded inserts, by their place, as boxes; esp32_s3_lcd_185.py draws
  these as shapes of _parts.py;
- PRISMS: every other solid as the outline of its largest flat face
  parallel to the board, with the z it reaches from and to and its
  colour. A solid that lies inside another's box, or within the board's
  thickness, is left out. A part whose solids overlap one another is
  one prism: the least rectangle round all of them, over their height.

The glass, the display and its flat cable are one solid in the file;
esp32_s3_lcd_185.py holds their dimensions, read off that solid's flat
faces.

Usage:
    python3 hardware/mechanical/_measure_esp32_s3_lcd_185.py FILE.stp

Writes hardware/mechanical/_esp32_s3_lcd_185_data.py. Needs OCP (pip
install cadquery-ocp==8.0.1.1.0).

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import math
import pprint
import sys
from pathlib import Path

from OCP.BRepAdaptor import BRepAdaptor_Curve, BRepAdaptor_Surface
from OCP.BRepAlgoAPI import BRepAlgoAPI_Common
from OCP.BRepBuilderAPI import (BRepBuilderAPI_MakeFace,
                                BRepBuilderAPI_MakePolygon)
from OCP.BRepPrimAPI import BRepPrimAPI_MakePrism
from OCP.gp import gp_Pnt, gp_Vec
from OCP.BRepGProp import BRepGProp
from OCP.BRepTools import BRepTools, BRepTools_WireExplorer
from OCP.GProp import GProp_GProps
from OCP.TopAbs import TopAbs_FACE, TopAbs_REVERSED, TopAbs_WIRE
from OCP.TopExp import TopExp_Explorer
from OCP.TopoDS import TopoDS

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "kicad" / "tools"))
sys.path.insert(0, str(HERE))
import _measure_step as m  # noqa: E402
import step_check  # noqa: E402

BACK = -5.00            # mm, the board's component side in the file
SHEET = 45.0            # mm, a solid this long is board, display or ring
SIMPLIFY = 0.01         # mm, tolerance of the outlines' polylines
ARC_STEP = 10.0         # degrees between points on a sampled arc
CHIP_REACH = 6.0        # mm, a group as long as this may be a chip part
SHARED = 1e-3           # mm3, two solids sharing more than this clash
# Boxes (x0, y0, x1, y1) in the output frame; a solid inside one is
# that part's.
NAMED_AT = {"usb_c": (-5.1, -25.8, 4.7, -17.9)}
# A solid of this size (long, short, height) is a threaded insert.
INSERT = (4.0, 4.0, 5.5)


def _xy(p):
    """A point of the file in the output frame, x and y only."""
    return (-p.X(), p.Y())


def _box(b):
    """A box (x0, y0, z0, x1, y1, z1) of the file in the output frame."""
    return (-b[3], b[1], BACK - b[5], -b[0], b[4], BACK - b[2])


def _wire_points(wire, face):
    """The corners of a wire in order, arcs sampled every ARC_STEP."""
    pts = []
    walk = BRepTools_WireExplorer(wire, face)
    while walk.More():
        edge = walk.Current()
        curve = BRepAdaptor_Curve(edge)
        t0, t1 = curve.FirstParameter(), curve.LastParameter()
        if str(curve.GetType()).endswith("Line"):
            n = 1
        elif str(curve.GetType()).endswith("Circle"):
            n = max(2, math.ceil(math.degrees(abs(t1 - t0)) / ARC_STEP))
        else:
            n = 8
        if edge.Orientation() == TopAbs_REVERSED:
            ts = [t1 - (t1 - t0) * k / n for k in range(n)]
        else:
            ts = [t0 + (t1 - t0) * k / n for k in range(n)]
        pts += [_xy(curve.Value(t)) for t in ts]
        walk.Next()
    return pts


def _simplify(pts, tol):
    """Douglas-Peucker on a closed polygon."""
    def dp(chain):
        if len(chain) < 3:
            return chain
        (x0, y0), (x1, y1) = chain[0], chain[-1]
        dx, dy = x1 - x0, y1 - y0
        length = math.hypot(dx, dy) or 1e-12
        far, at = -1.0, 0
        for k, (x, y) in enumerate(chain[1:-1], 1):
            d = abs(dy * (x - x0) - dx * (y - y0)) / length
            if d > far:
                far, at = d, k
        if far <= tol:
            return [chain[0], chain[-1]]
        return dp(chain[:at + 1])[:-1] + dp(chain[at:])

    if len(pts) < 4:
        return pts
    # Split at the point farthest from the first, so both halves are
    # open chains.
    far = max(range(len(pts)), key=lambda k: math.dist(pts[0], pts[k]))
    a = dp(pts[:far + 1])
    b = dp(pts[far:] + [pts[0]])
    return a[:-1] + b[:-1]


def _flat_faces(solid):
    """(z, area, face) of the solid's flat faces parallel to the board."""
    out = []
    walk = TopExp_Explorer(solid, TopAbs_FACE)
    while walk.More():
        face = TopoDS.Face(walk.Current())
        surface = BRepAdaptor_Surface(face)
        if (str(surface.GetType()).endswith("Plane") and abs(
                surface.Plane().Axis().Direction().Z()) > 0.999):
            props = GProp_GProps()
            BRepGProp.SurfaceProperties_s(face, props)
            out.append((BACK - surface.Plane().Location().Z(), props.Mass(),
                        face))
        walk.Next()
    return out


def _outline(face, tol=SIMPLIFY):
    """(outer, holes) of a face, as simplified polygons."""
    outer = BRepTools.OuterWire_s(face)
    holes = []
    walk = TopExp_Explorer(face, TopAbs_WIRE)
    while walk.More():
        wire = TopoDS.Wire(walk.Current())
        if not wire.IsSame(outer):
            holes.append(_simplify(_wire_points(wire, face), tol))
        walk.Next()
    return _simplify(_wire_points(outer, face), tol), holes


def _least_rect(pts):
    """(cx, cy, long, short, angle) of the least rectangle round pts.

    The angle, degrees in [0, 180), is the long side's direction.
    """
    best = None
    hull = sorted(set(pts))
    for i, a in enumerate(hull):
        for b in hull[i + 1:]:
            dx, dy = b[0] - a[0], b[1] - a[1]
            if dx * dx + dy * dy < 1e-6:
                continue
            t = math.atan2(dy, dx)
            c, s = math.cos(t), math.sin(t)
            us = [x * c + y * s for x, y in pts]
            vs = [-x * s + y * c for x, y in pts]
            lu, lv = max(us) - min(us), max(vs) - min(vs)
            if best is None or lu * lv < best[0] - 1e-9:
                uc, vc = (max(us) + min(us)) / 2, (max(vs) + min(vs)) / 2
                angle = math.degrees(t) if lu >= lv else math.degrees(t) + 90
                best = (lu * lv, uc * c - vc * s, uc * s + vc * c,
                        max(lu, lv), min(lu, lv), angle % 180.0)
    return best[1:] if best else None


def _capped(group, rect):
    """Whether a group is a body with a cap at each end of its length.

    The group's solids are its body, the largest, first; the other two
    must each lie at one end of the least rectangle's long side.
    """
    if len(group) != 3:
        return False
    cx, cy, length, _width, angle = rect
    c, s = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    ends = []
    for solid in group[1:]:
        b = solid["box"]
        u = ((b[0] + b[3]) / 2 - cx) * c + ((b[1] + b[4]) / 2 - cy) * s
        ends.append(u)
    return (min(ends) < -0.25 * length and max(ends) > 0.25 * length)


def _prism_solid(outline, z0, z1):
    """An OpenCASCADE solid: the outline pushed from z0 to z1."""
    poly = BRepBuilderAPI_MakePolygon()
    for x, y in outline:
        poly.Add(gp_Pnt(x, y, z0))
    poly.Close()
    face = BRepBuilderAPI_MakeFace(poly.Wire()).Face()
    return BRepPrimAPI_MakePrism(face, gp_Vec(0.0, 0.0, z1 - z0)).Shape()


def _solids_overlap(group, outlines):
    """Whether the prisms drawn for two solids of a group clash.

    Two prisms clash when they share more than SHARED of volume and
    neither solid lies inside the other's box.
    """
    def inside(a, b):
        return all(b[k] <= a[k] + 1e-6 and b[k + 3] >= a[k + 3] - 1e-6
                   for k in range(3))

    shapes = [_prism_solid(o, s["box"][2], s["box"][5])
              if len(o) >= 3 and s["box"][5] - s["box"][2] > 1e-6 else None
              for s, o in zip(group, outlines, strict=True)]
    for i, s in enumerate(group):
        for j in range(i + 1, len(group)):
            t = group[j]
            a, b = s["box"], t["box"]
            if (shapes[i] is None or shapes[j] is None or inside(a, b)
                    or inside(b, a) or not all(
                        a[k] < b[k + 3] and b[k] < a[k + 3]
                        for k in range(3))):
                continue
            both = BRepAlgoAPI_Common(shapes[i], shapes[j]).Shape()
            props = GProp_GProps()
            BRepGProp.VolumeProperties_s(both, props)
            if props.Mass() > SHARED:
                return True
    return False


def _feet_overlap(feet):
    """Whether two feet boxes (x0, y0, x1, y1) of a package overlap."""
    for i, a in enumerate(feet):
        for b in feet[i + 1:]:
            if (a[0] < b[2] - 1e-6 and b[0] < a[2] - 1e-6
                    and a[1] < b[3] - 1e-6 and b[1] < a[3] - 1e-6):
                return True
    return False


def _r(values, places=3):
    return tuple(round(v, places) for v in values)


def _pts(pts):
    return tuple(_r(p) for p in pts)


def classify(path):
    """Read the file and sort its solids.

    Args:
        path: The STEP file.

    Returns:
        A dict of the data file's names to their values.
    """
    read = step_check.read(path)[1]
    solids = m.measure(path)
    for s, (shape, _rgb) in zip(solids, read, strict=True):
        s["shape"] = shape
        s["box"] = _box(s["box"])
        s["flats"] = [(BACK - z, a, _box(b)) for z, a, b in s["flats"]]
    sheets = [s for s in solids
              if max(s["box"][3] - s["box"][0],
                     s["box"][4] - s["box"][1]) >= SHEET]
    small = [s for s in solids if s not in sheets]
    data = {}
    for s in sheets:
        z0 = s["box"][2]
        if s["box"][5] > 0.0 and z0 < 0.0 and s["box"][5] < 0.05:
            name = "BOARD"
        elif s["box"][2] > -3.1 and s["box"][5] <= -1.55:
            name = "SPACER"
        else:
            continue
        face = max(_flat_faces(s["shape"]), key=lambda f: f[1])[2]
        outer, holes = _outline(face)
        data[name] = (_pts(outer), tuple(_pts(h) for h in holes),
                      _r((s["box"][2], s["box"][5])))
    named = {"usb_c": [], "card_slot": [], "insert": []}
    rest = []
    for s in small:
        b = s["box"]
        size = sorted((b[3] - b[0], b[4] - b[1]), reverse=True) + [
            b[5] - b[2]]
        cx, cy = (b[0] + b[3]) / 2, (b[1] + b[4]) / 2
        x0, y0, x1, y1 = NAMED_AT["usb_c"]
        if x0 <= b[0] and b[3] <= x1 and y0 <= b[1] and b[4] <= y1:
            named["usb_c"].append(_r(b))
        elif all(abs(a - c) < 0.1 for a, c in zip(size, INSERT,
                                                  strict=True)):
            named["insert"].append(_r(b))
        elif size[0] > 10.0 and math.hypot(cx - 14.5, cy - 7.5) < 3.0:
            named["card_slot"].append(_r(b))
        else:
            rest.append(s)
    front = data["BOARD"][2][0]
    chips, chips_front, capped, leaded, prisms = [], [], [], [], []
    for group in m.components(rest):
        box = m._union(group)
        back = box[5] > 0.05
        if not back and box[2] >= front - 0.01:
            continue            # inside the board's thickness
        outlines = []
        for s in group:
            faces = _flat_faces(s["shape"])
            outlines.append(_outline(max(faces, key=lambda f: f[1])[2],
                                     0.005)[0] if faces else [])
        height = box[5] - max(box[2], 0.0) if back else (
            min(box[5], front) - box[2])
        dx, dy = box[3] - box[0], box[4] - box[1]
        rect, kind = None, None
        if len(group) in (1, 3, 4) and max(dx, dy) < CHIP_REACH:
            rect = _least_rect([q for o in outlines for q in o])
        if rect:
            kind = m._match(m.CHIP_SIZES, (rect[2], rect[3], height))
        if not kind and rect and _capped(group, rect):
            capped.append((round(rect[2], 3), round(rect[3], 3),
                           round(height, 3),
                           m._shade(group[0]["rgb"], True),
                           round(rect[0], 3), round(rect[1], 3),
                           round(rect[4]) % 180, "back" if back else "front"))
            continue
        if kind:
            # The chip parts of _parts.py lie along x; a part is turned
            # by its long side's angle, to the nearest degree.
            turn = round(rect[4]) % 180
            (chips if back else chips_front).append(
                (kind, round(rect[0], 3), round(rect[1], 3), turn))
            continue
        package = m._leaded(group) if back else None
        if package and _feet_overlap(package[1]):
            package = None
        if package and max(dx, dy) < 12.0:
            leaded.append(package)
            continue
        if _solids_overlap(group, outlines):
            # The file draws this part as solids that share volume: it
            # is one block, the least rectangle round all of them.
            pts = [q for o in outlines for q in o]
            cx, cy, long, short, angle = _least_rect(pts)
            c, s = math.cos(math.radians(angle)), math.sin(math.radians(angle))
            corners = [(cx + u * c - v * s, cy + u * s + v * c)
                       for u, v in ((-long / 2, -short / 2),
                                    (long / 2, -short / 2),
                                    (long / 2, short / 2),
                                    (-long / 2, short / 2))]
            prisms.append((_pts(corners), _r((box[2], box[5])),
                           m._shade(group[0]["rgb"], True)))
            continue
        for n, (s, outer) in enumerate(zip(group, outlines, strict=True)):
            if len(outer) < 3 or any(
                    o is not s and all(
                        o["box"][k] <= s["box"][k] + 1e-6
                        and o["box"][k + 3] >= s["box"][k + 3] - 1e-6
                        for k in range(3)) for o in group):
                continue
            z0, z1 = s["box"][2], s["box"][5]
            if z1 <= 0.01 and z0 >= front - 0.01:
                continue        # inside the board's thickness
            prisms.append((_pts(outer), _r((z0, z1)),
                           m._shade(s["rgb"], n == 0)))
    data["CHIPS"] = tuple(sorted(chips))
    data["CHIPS_FRONT"] = tuple(sorted(chips_front))
    data["CAPPED"] = tuple(sorted(capped))
    data["LEADED"] = tuple(sorted(leaded))
    data["NAMED"] = {k: tuple(sorted(v)) for k, v in named.items()}
    data["PRISMS"] = tuple(sorted(prisms, key=lambda q: (q[1], q[0][0])))
    return data


def main(argv):
    """Run from the command line.

    Args:
        argv: The STEP file.

    Returns:
        0.
    """
    data = classify(Path(argv[0]))
    lines = ['"""ESP32-S3-Touch-LCD-1.85 dimensions, by its measuring tool.',
             "",
             "Measured in Waveshare's STEP file of the board, in the frame",
             "that tool states, mm. BOARD and SPACER hold (outline, holes,",
             "(z0, z1)); CHIPS and CHIPS_FRONT (kind, x, y, turn) of each",
             "chip part on the back and on the front; CAPPED (length,",
             "width, height, colour, x, y, turn, side) of each other part",
             "with two end caps;",
             "LEADED (body box, feet) of each leaded package; NAMED the",
             "boxes (x0, y0, z0, x1, y1, z1) of each named part's solids;",
             "PRISMS (outline, (z0, z1), colour) of each other solid.",
             "Written by _measure_esp32_s3_lcd_185.py; do not edit, run",
             "that tool.", "",
             "SPDX-License-Identifier: CC-BY-NC-SA-4.0", '"""', ""]
    def fmt(value, indent):
        text = pprint.pformat(value, width=79 - indent, compact=True)
        return text.replace("\n", "\n" + " " * indent)

    for name in ("BOARD", "SPACER"):
        lines.append(f"{name} = {fmt(data[name], len(name) + 3)}")
        lines.append("")
    for name in ("CHIPS", "CHIPS_FRONT", "CAPPED", "LEADED", "PRISMS"):
        lines.append(f"{name} = (")
        lines += [f"    {fmt(row, 4)}," for row in data[name]]
        lines += [")", ""]
    lines.append("NAMED = {")
    for name, boxes in data["NAMED"].items():
        lines.append(f"    {name!r}: (")
        lines += [f"        {fmt(b, 8)}," for b in boxes]
        lines.append("    ),")
    lines.append("}")
    out = HERE / "_esp32_s3_lcd_185_data.py"
    out.write_text("\n".join(lines) + "\n")
    print(f"{len(data['CHIPS'])} + {len(data['CHIPS_FRONT'])} chip parts, "
          f"{len(data['CAPPED'])} capped parts, "
          f"{len(data['LEADED'])} leaded "
          f"packages, {len(data['PRISMS'])} prisms, "
          f"{sum(len(v) for v in data['NAMED'].values())} named solids")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
