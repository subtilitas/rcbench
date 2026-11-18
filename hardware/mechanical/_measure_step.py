#!/usr/bin/env python3
"""Measure Waveshare's STEP file of the 7-inch panel board for panel_7b.py.

The file is ESP32-P4-WIFI6-Touch-LCD-7B_3d-20251108.stp from
ESP32-P4-WIFI6-Touch-LCD-7B-3D.zip on Waveshare's wiki page of the
board. Waveshare states no licence for it, so it is not in this
repository; this tool reads it with OpenCASCADE and writes the
dimensions panel_7b.py draws the board from.

Solids whose boxes touch form one component. A component is written as
one of four kinds, all in the file's frame, mm: x and y from the
board's corner, z = 0 on the board's component side.

- A chip part, when its box is one of CHIP_SIZES: kind, center and
  turn.
- A named part, when its box is one of NAMED: its box, for the drawing
  panel_7b.py holds of that part. The parts in WRONG are left out: the
  file draws other wire sockets than the board carries.
- A leaded package, when it is one solid that fills less than BOXY of
  its box, or a body with further solids: the body's box, and the box
  of each foot.
- Boxes otherwise: the box and colour of each solid, less a box that
  lies inside another of its component.

The board, the display and the glass, over SHEET long, are not written;
panel_7b.py holds their dimensions.

Usage:
    python3 hardware/mechanical/_measure_step.py FILE.stp

Writes hardware/mechanical/_panel_7b_data.py. Needs OCP (pip install
cadquery-ocp==8.0.1.1.0).

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import sys
from pathlib import Path

from OCP.Bnd import Bnd_Box
from OCP.BRepAdaptor import BRepAdaptor_Surface
from OCP.BRepBndLib import BRepBndLib
from OCP.BRepGProp import BRepGProp
from OCP.GProp import GProp_GProps
from OCP.TopAbs import TopAbs_FACE
from OCP.TopExp import TopExp_Explorer
from OCP.TopoDS import TopoDS

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "kicad" / "tools"))
import step_check  # noqa: E402

SHEET = 100.0           # mm, a solid longer than this is board or display
TOUCH = 0.02            # mm, boxes this near touch
BOXY = 0.80             # share of its box a plain block fills
NEAR = 0.08             # mm, a size this near a table's entry matches it
# Length, width and height of a chip part to its kind.
CHIP_SIZES = {(1.6, 0.8, 0.8): "c0603", (1.6, 0.8, 0.4): "r0603",
              (1.0, 0.5, 0.5): "c0402", (1.0, 0.5, 0.4): "r0402",
              (0.6, 0.3, 0.3): "c0201", (0.7, 0.3, 0.3): "r0201"}
# Longer side, shorter side and number of solids to the part's name.
NAMED = {(16.1, 15.0, 1): "card_slot", (14.5, 11.4, 1): "usb_a",
         (19.9, 6.0, 1): "camera_socket", (19.2, 6.0, 32): "display_socket",
         (12.0, 7.4, 3): "wire_socket", (9.6, 7.7, 22): "usb_c",
         (9.2, 7.5, 5): "switch", (8.0, 7.9, 1): "speaker_socket",
         (14.4, 3.2, 1): "pin_socket", (14.3, 3.2, 1): "pin_socket",
         (7.7, 5.2, 1): "battery_socket", (7.5, 5.0, 2): "capacitor",
         (5.5, 5.5, 1): "standoff", (6.8, 4.3, 2): "touch_socket",
         (6.3, 4.0, 1): "uart_socket", (5.2, 4.0, 1): "button",
         (2.8, 2.0, 1): "battery_spring", (10.0, 10.0, 1): "p4"}
# Named parts the file draws as other parts than the board carries, by
# Waveshare's photo of the board: they are not written, and panel_7b.py
# draws the board's from the photo.
WRONG = ("wire_socket", "speaker_socket", "pin_socket")


def _box(shape):
    box = Bnd_Box()
    box.SetGap(0.0)
    BRepBndLib.AddOptimal_s(shape, box, False, False)
    lo, hi = box.CornerMin(), box.CornerMax()
    return (lo.X(), lo.Y(), lo.Z(), hi.X(), hi.Y(), hi.Z())


def measure(path):
    """Read the solids.

    Args:
        path: The STEP file.

    Returns:
        A list of dicts, one a solid: ``box`` (x0, y0, z0, x1, y1, z1),
        ``rgb`` or None, ``fill`` the share of its box it fills, and
        ``flats``, a list of (z, area, box) of its faces that face up
        or down.
    """
    out = []
    for solid, rgb in step_check.read(path)[1]:
        props = GProp_GProps()
        BRepGProp.VolumeProperties_s(solid, props)
        box = _box(solid)
        size = [box[k + 3] - box[k] for k in range(3)]
        flats = []
        walk = TopExp_Explorer(solid, TopAbs_FACE)
        while walk.More():
            face = TopoDS.Face(walk.Current())
            surface = BRepAdaptor_Surface(face)
            if (str(surface.GetType()).endswith("Plane") and abs(
                    surface.Plane().Axis().Direction().Z()) > 0.999):
                area = GProp_GProps()
                BRepGProp.SurfaceProperties_s(face, area)
                flats.append((surface.Plane().Location().Z(), area.Mass(),
                              _box(face)))
            walk.Next()
        whole = size[0] * size[1] * size[2]
        out.append({"box": box, "rgb": rgb, "flats": flats,
                    "fill": props.Mass() / whole if whole > 0 else 1.0})
    return out


def components(solids):
    """Group solids whose boxes touch.

    Args:
        solids: The solids of ``measure``, without the sheets.

    Returns:
        A list of lists of solids, the largest solid of each first.
    """
    parent = list(range(len(solids)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    order = sorted(range(len(solids)), key=lambda i: solids[i]["box"][0])
    for n, i in enumerate(order):
        a = solids[i]["box"]
        for j in order[n + 1:]:
            b = solids[j]["box"]
            if b[0] > a[3] + TOUCH:
                break
            if all(a[k] <= b[k + 3] + TOUCH and b[k] <= a[k + 3] + TOUCH
                   for k in (1, 2)):
                parent[find(i)] = find(j)
    groups = {}
    for i, solid in enumerate(solids):
        groups.setdefault(find(i), []).append(solid)

    def volume(s):
        b = s["box"]
        return s["fill"] * (b[3] - b[0]) * (b[4] - b[1]) * (b[5] - b[2])

    return [sorted(g, key=volume, reverse=True) for g in groups.values()]


def _union(group):
    boxes = [s["box"] for s in group]
    return tuple(min(b[k] for b in boxes) for k in range(3)) + tuple(
        max(b[k] for b in boxes) for k in range(3, 6))


def _r(values):
    return tuple(round(v, 2) for v in values)


def _shade(rgb, body):
    """A colour name for a solid: the file's own when it has one."""
    if rgb is None or min(rgb) > 0.95:
        return "epoxy" if body else "tin"
    if max(rgb) < 0.35:
        return "epoxy"
    return _r(rgb)


def _match(table, size):
    """The entry of a size table within NEAR of ``size``, or None."""
    for key, value in table.items():
        if all(abs(a - b) <= NEAR for a, b in zip(key, size, strict=True)):
            return value
    return None


def _leaded(group):
    """Body box and feet boxes of a leaded package, or None."""
    main = group[0]
    if len(group) == 1 and main["fill"] >= BOXY:
        return None
    if len(group) == 1:
        top = max(z for z, _a, _b in main["flats"])
        lids = [(a, b) for z, a, b in main["flats"] if z > top - 0.01]
        body = max(lids)[1]
        low = min(z for z, _a, _b in main["flats"])
        feet = [b for z, _a, b in main["flats"] if z < low + 0.06
                and (b[3] <= body[0] + 0.05 or b[0] >= body[3] - 0.05
                     or b[4] <= body[1] + 0.05 or b[1] >= body[4] - 0.05)]
        # The body's sides slope: its top is smaller than its foot by
        # the same draft all round. A side without feet shows the draft.
        whole = main["box"]
        free = [abs(whole[k] - body[k]) for k in (0, 1, 3, 4)
                if not any(f[k] < body[k] - 0.05 if k < 3
                           else f[k] > body[k] + 0.05 for f in feet)]
        draft = min(free) if free else 0.0
        body = (body[0] - draft, body[1] - draft, 0.1, body[3] + draft,
                body[4] + draft, top)
    else:
        if main["fill"] < 0.6:
            return None
        body = main["box"]
        feet = [s["box"] for s in group[1:]]
    if not feet:
        return None
    return _r(body), [_r((f[0], f[1], f[3], f[4])) for f in feet]


def classify(solids):
    """Sort the components into the four kinds.

    Args:
        solids: The solids of ``measure``.

    Returns:
        (chips, named, leaded, boxes): chips as (kind, x, y, turn);
        named as a dict of name to boxes; leaded as (body box, feet);
        boxes as (box, colour).
    """
    small = [s for s in solids
             if max(s["box"][3] - s["box"][0],
                    s["box"][4] - s["box"][1]) < SHEET]
    chips, named, leaded, boxes = [], {}, [], []
    for group in components(small):
        box = _union(group)
        dx, dy = box[3] - box[0], box[4] - box[1]
        height = box[5] - max(box[2], 0.0)
        long, short = max(dx, dy), min(dx, dy)
        kind = _match(CHIP_SIZES, (long, short, height))
        name = _match(NAMED, (long, short, len(group)))
        if kind and len(group) in (1, 3, 4):
            chips.append((kind, round((box[0] + box[3]) / 2, 2),
                          round((box[1] + box[4]) / 2, 2),
                          0 if dx >= dy else 90))
            continue
        if name in WRONG:
            continue
        if name:
            named.setdefault(name, []).append(_r(box))
            continue
        package = _leaded(group)
        if package and long < 8.0:
            leaded.append(package)
            continue
        kept = [s for s in group if not any(
            o is not s and all(o["box"][k] <= s["box"][k] + 1e-6
                               and o["box"][k + 3] >= s["box"][k + 3] - 1e-6
                               for k in range(3)) for o in group)]
        for n, s in enumerate(kept):
            boxes.append((_r(s["box"]), _shade(s["rgb"], n == 0)))
    return (sorted(chips, key=lambda c: (c[0], c[1], c[2])),
            {k: sorted(v) for k, v in sorted(named.items())},
            sorted(leaded), sorted(boxes, key=lambda b: b[0]))


def _lines(name, rows, per_line):
    """Python source of a tuple of rows, several rows to a line."""
    out = [f"{name} = ("]
    for k in range(0, len(rows), per_line):
        out.append("    " + " ".join(f"{row!r}," for row in
                                     rows[k:k + per_line]))
    out.append(")")
    return out


def main(argv):
    """Run from the command line.

    Args:
        argv: The STEP file.

    Returns:
        0.
    """
    chips, named, leaded, boxes = classify(measure(Path(argv[0])))
    lines = ['"""Dimensions of the 7-inch panel board, by _measure_step.py.',
             "",
             "Measured in Waveshare's STEP file of the board, in that",
             "file's frame, mm. CHIPS holds (kind, x, y, turn) of each chip",
             "part; NAMED the boxes (x0, y0, z0, x1, y1, z1) of each named",
             "part; LEADED (body box, feet as (x0, y0, x1, y1)) of each",
             "leaded package; BOXES (box, colour) of each other solid.",
             "Do not edit; run _measure_step.py.", "",
             "SPDX-License-Identifier: CC-BY-NC-SA-4.0", '"""', ""]
    lines += _lines("CHIPS", chips, 2)
    lines += ["", "NAMED = {"]
    for name, found in named.items():
        lines.append(f"    {name!r}: (")
        lines += [f"        {box!r}," for box in found]
        lines.append("    ),")
    lines += ["}", "", "LEADED = ("]
    for body, feet in leaded:
        lines.append(f"    ({body!r}, (")
        for k in range(0, len(feet), 2):
            lines.append("        " + " ".join(f"{f!r}," for f in
                                               feet[k:k + 2]))
        lines.append("    )),")
    lines += [")", ""]
    lines += _lines("BOXES", boxes, 1)
    (HERE / "_panel_7b_data.py").write_text("\n".join(lines) + "\n")
    print(f"{len(chips)} chip parts, "
          f"{sum(len(v) for v in named.values())} named parts, "
          f"{len(leaded)} leaded packages, {len(boxes)} boxes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
