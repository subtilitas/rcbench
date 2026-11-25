#!/usr/bin/env python3
r"""Write a 3D model of the hardware test bench as GLB, for the video.

The bench is the rig the hwbench tooling drives: the 7 inch panel, the
IOmcu on its carrier, the touch injector, the servo with its AS5600,
the supplies, the logic analyzer, the host computer and the camera on
its arm, on a clear acrylic sheet over a black board, and the ring lamp
above it.

The model is drawn from the owner's 8 photos of 2026-11-25. Two parts
set the scale: the panel (glass 164.3 x 99.2, panel_7b.step) and the
Pico on the IOmcu's carrier (51 x 21). In the near top-down photo the
panel's long side is 415 pixels of 2000, 2.53 pixels per mm; the
photo's scale is taken as the same over the whole sheet. Every other
size and place is read from the photos at that scale and is an
estimate; README.md lists them. Cables are not drawn.

Frame of the source: mm, z up, the origin at the sheet's front left
corner on its top face, x to the right, y away from the viewer (to the
panel and the wall). The GLB is in metres with y up, as glTF defines
it: GLB (x, y, z) = (x, z, -y) / 1000.

Every part is its own named node; the camera's pan and tilt are nodes
whose origins lie on their axes, so the video can turn them.

Usage:
    python3 hardware/mechanical/_gen_bench.py tools/video/cad/bench.glb
    xvfb-run -a python3 hardware/mechanical/_gen_bench.py \
        tools/video/cad/bench.glb --render hardware/mechanical/renders

tools/video/cad/ is not in git: the video tools write their GLB files
there from their sources. --render draws the bench to
renders/parts/bench_iso.png, bench_top.png and bench_front.png.

Needs OCP (pip install cadquery-ocp==8.0.1.1.0), numpy and trimesh.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import math
import sys
from pathlib import Path

import numpy as np
from OCP.BRep import BRep_Tool
from OCP.BRepAlgoAPI import BRepAlgoAPI_Cut, BRepAlgoAPI_Fuse
from OCP.BRepBuilderAPI import BRepBuilderAPI_Transform
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.BRepPrimAPI import BRepPrimAPI_MakeBox, BRepPrimAPI_MakeCylinder
from OCP.gp import gp_Ax1, gp_Ax2, gp_Dir, gp_Pnt, gp_Trsf, gp_Vec
from OCP.TopAbs import TopAbs_FACE, TopAbs_REVERSED
from OCP.TopExp import TopExp_Explorer
from OCP.TopLoc import TopLoc_Location
from OCP.TopoDS import TopoDS

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "kicad" / "tools"))
import _models  # noqa: E402

ACRYLIC = (0.80, 0.88, 0.92, 0.30)
BLACK = (0.08, 0.08, 0.09)
DARK = (0.18, 0.18, 0.20)
GREY = (0.55, 0.56, 0.58)
LIGHT = (0.85, 0.85, 0.82)
BLUE = (0.10, 0.30, 0.65)
GREEN = (0.10, 0.45, 0.20)
TERMINAL = (0.15, 0.60, 0.30)
GOLD = (0.85, 0.62, 0.20)
METAL = (0.70, 0.70, 0.72)
FOAM = (0.20, 0.20, 0.21)
SCREEN = (0.05, 0.12, 0.16)
RED = (0.75, 0.12, 0.10)

# Sheet and board, mm: x0, y0, x1, y1, thickness.
SHEET = (0.0, 0.0, 605.0, 500.0, 5.0)
BOARD = (37.0, 49.0, 500.0, 441.0, 18.0)


def box(x0, y0, z0, x1, y1, z1):
    return BRepPrimAPI_MakeBox(gp_Pnt(min(x0, x1), min(y0, y1), min(z0, z1)),
                               gp_Pnt(max(x0, x1), max(y0, y1),
                                      max(z0, z1))).Shape()


def cbox(cx, cy, z0, lx, ly, h):
    """A box centred on (cx, cy), from z0 up by h."""
    return box(cx - lx / 2, cy - ly / 2, z0, cx + lx / 2, cy + ly / 2, z0 + h)


def cyl(p, axis, r, length):
    return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(*p), gp_Dir(*axis)), r,
                                    length).Shape()


def fuse(*shapes):
    out = shapes[0]
    for s in shapes[1:]:
        out = BRepAlgoAPI_Fuse(out, s).Shape()
    return out


def cut(shape, *tools):
    for t in tools:
        shape = BRepAlgoAPI_Cut(shape, t).Shape()
    return shape


def place(shape, dx=0.0, dy=0.0, dz=0.0, rz=0.0, rx=0.0, ry=0.0):
    """Turn about x, then y, then z (degrees), then move."""
    t = gp_Trsf()
    for ang, axis in ((rx, (1, 0, 0)), (ry, (0, 1, 0)), (rz, (0, 0, 1))):
        if ang:
            r = gp_Trsf()
            r.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(*axis)),
                          math.radians(ang))
            t = r.Multiplied(t)
    m = gp_Trsf()
    m.SetTranslation(gp_Vec(dx, dy, dz))
    return BRepBuilderAPI_Transform(shape, m.Multiplied(t), True).Shape()


def read_step(name):
    import step_check
    return step_check.read(HERE / "models" / name)[1]


# ---- the parts, each a list of (solid, rgba) in the source frame ----

def sheet():
    x0, y0, x1, y1, t = SHEET
    return [(box(x0, y0, -t, x1, y1, 0.0), ACRYLIC)]


def base_board():
    x0, y0, x1, y1, t = BOARD
    return [(box(x0, y0, -SHEET[4] - t, x1, y1, -SHEET[4]), BLACK)]


def panel():
    """panel_7b.step, glass up, tilted 15 degrees, far edge raised."""
    out = []
    tilt, lift = 15.0, 14.0
    for s, c in read_step("pcb/panel_7b.step"):
        # Model: glass front at z 0, rear to z -15.1, centre at (0.9, -2.4).
        s = place(s, dx=-0.9, dy=2.4, dz=15.1)
        s = place(s, rx=tilt)
        out.append((place(s, dx=398.0, dy=447.0, dz=lift), c or DARK))
    # Two stand legs under the raised edge.
    for x in (330.0, 466.0):
        out.append((cbox(x, 482.0, 0.0, 12.0, 8.0, 34.0), BLACK))
    return out


def pico(cx, cy, z0, rot):
    """A Raspberry Pi Pico 2 sized board: 51 x 21 x 1.0, RP2350, USB."""
    parts = [(cbox(0, 0, 0, 51.0, 21.0, 1.0), GREEN),
             (cbox(0, 0, 1.0, 7.0, 7.0, 0.9), BLACK),
             (cbox(-23.0, 0, 1.0, 6.0, 8.0, 3.0), METAL)]
    return [(place(s, dx=cx, dy=cy, dz=z0, rz=rot), c) for s, c in parts]


def iomcu():
    """The IOmcu: a Pico on a blue screw-terminal carrier, 89 x 73."""
    cx, cy, rot = 410.0, 352.0, 0.0
    parts = [(cbox(0, 0, 3.0, 89.0, 73.0, 1.6), BLUE)]
    for sy in (-1, 1):
        parts.append((cbox(0, sy * 31.0, 4.6, 80.0, 7.0, 8.5), TERMINAL))
        parts.append((cbox(0, sy * 13.0, 4.6, 52.0, 2.5, 8.5), BLACK))
    for x in (-40.0, 40.0):
        for y in (-32.0, 32.0):
            parts.append((cbox(x, y, 0.0, 4.0, 4.0, 3.0), METAL))
    out = [(place(s, dx=cx, dy=cy, rz=rot), c) for s, c in parts]
    return out + pico(cx, cy, 4.6 + 8.5, rot)


def touch_injector():
    """A small RP2040 board with two pin header rows, 23.5 x 18."""
    cx, cy = 470.0, 236.0
    parts = [(cbox(0, 0, 0, 23.5, 18.0, 1.0), BLUE),
             (cbox(0, 0, 1.0, 5.0, 5.0, 0.8), BLACK)]
    for sy in (-1, 1):
        parts.append((cbox(0, sy * 7.6, 1.0, 22.0, 2.5, 8.5), BLACK))
    return [(place(s, dx=cx, dy=cy, rz=90.0), c) for s, c in parts]


def sensor_perfboard():
    """Green perfboard 50 x 40 with a black square module on it."""
    cx, cy = 478.0, 288.0
    return [(cbox(cx, cy, 0, 50.0, 40.0, 1.6), GREEN),
            (cbox(cx - 4.0, cy, 4.0, 23.0, 23.0, 1.6), BLACK),
            (cbox(cx - 4.0, cy, 1.6, 20.0, 2.5, 2.4), BLACK)]


def servo():
    """servo_standard.step standing at the sheet's right edge."""
    out = []
    for s, c in read_step("parts/servo_standard.step"):
        out.append((place(s, dx=600.0, dy=205.0, rz=90.0), c or BLACK))
    return out


def as5600():
    """The AS5600 module, blue, taped over the servo's output."""
    cx, cy, z = 600.0, 198.0, 47.0
    return [(cbox(cx, cy, z, 23.0, 23.0, 1.6), BLUE),
            (cbox(cx, cy, z + 1.6, 5.0, 4.0, 1.5), BLACK),
            (cbox(cx, cy + 6.0, z + 3.1, 26.0, 16.0, 0.3), LIGHT)]


def load_resistor():
    """A gold aluminium-housed power resistor, 50 x 28 x 15, finned."""
    body = fuse(cbox(0, 0, 0, 50.0, 16.0, 15.0),
                cbox(0, 0, 0, 36.0, 28.0, 3.0))
    body = cut(body, *[cbox(0, y, 12.0, 52.0, 2.0, 4.0)
                       for y in (-3.5, 0.0, 3.5)])
    parts = [(body, GOLD)]
    for x in (-27.0, 27.0):
        parts.append((cbox(x, 0, 6.0, 4.0, 2.0, 4.0), METAL))
    return [(place(s, dx=326.0, dy=318.0, rz=30.0), c) for s, c in parts]


def logic_analyzer():
    """Kingst LA2016, about 105 x 61 x 18, grey case, white top."""
    cx, cy = 342.0, 260.0
    return [(cbox(cx, cy, 0, 105.0, 61.0, 16.0), GREY),
            (cbox(cx, cy, 16.0, 101.0, 57.0, 1.5), LIGHT),
            (cbox(cx + 54.0, cy, 4.0, 5.0, 22.0, 8.0), BLACK)]


def supplies():
    """The PD meter with its display, a buck module, a USB-C board."""
    out = []
    pd = [(cbox(0, 0, 0, 37.0, 53.0, 1.6), BLACK),
          (cbox(0, 6.0, 1.6, 26.0, 16.0, 3.0), (0.10, 0.20, 0.45)),
          (cbox(0, -20.0, 1.6, 10.0, 10.0, 9.0), TERMINAL),
          (cbox(0, 24.0, 1.6, 9.0, 7.5, 3.2), METAL)]
    out.append(("pd_meter", [(place(s, dx=230.0, dy=368.0), c)
                             for s, c in pd]))
    buck = [(cbox(0, 0, 0, 22.0, 17.0, 1.2), BLUE),
            (cbox(0, 0, 1.2, 8.0, 8.0, 4.0), DARK)]
    out.append(("buck", [(place(s, dx=271.0, dy=383.0), c)
                         for s, c in buck]))
    usbc = [(cbox(0, 0, 0, 25.0, 15.0, 1.2), BLUE),
            (cbox(11.0, 0, 1.2, 7.5, 9.0, 3.2), METAL),
            (cbox(-6.0, 3.0, 1.2, 1.6, 0.8, 0.6), RED)]
    out.append(("usbc_board", [(place(s, dx=287.0, dy=452.0), c)
                               for s, c in usbc]))
    return out


def host_computer():
    """The host: a black vented case 70 x 115 x 35, Ethernet forward."""
    body = cbox(105.0, 362.0, 0, 70.0, 115.0, 35.0)
    vents = [cyl((x, y, 30.0), (0, 0, 1), 1.5, 6.0)
             for x in range(80, 132, 8) for y in range(320, 405, 8)]
    return [(cut(body, *vents), DARK),
            (cbox(105.0, 304.0, 4.0, 16.0, 2.0, 13.5), METAL),
            (cbox(90.0, 372.0, 35.0, 4.0, 4.0, 0.5), RED)]


def usb_hub():
    hub = [(cbox(162.0, 350.0, 0, 30.0, 120.0, 22.0), BLACK)]
    for k in range(4):
        hub.append((cbox(147.9, 310.0 + 22.0 * k, 6.0, 0.3, 12.5, 5.0),
                    (0.10, 0.30, 0.80)))
    return hub


def power_bank():
    return [(cyl((192.0, 292.0, 12.0), (0, 1, 0), 12.0, 115.0), BLACK)]


# The camera rig: a clamp on the sheet's front edge, a pole with a foam
# grip leaning back, a ball arm and the pan-tilt camera hanging under
# it, its lens down to the bench.
CLAMP = (480.0, 0.0)
POLE_TOP = (452.0, 70.0, 330.0)
CAM = (425.0, 135.0, 345.0)        # the camera's mount on its base
PAN_AXIS = (425.0, 135.0, 322.0)   # pan turns about z through here
TILT_AXIS = (425.0, 135.0, 287.0)  # tilt turns about x through here


def _rod(a, b, r):
    a, b = np.array(a, float), np.array(b, float)
    v = b - a
    length = float(np.linalg.norm(v))
    return cyl(tuple(a), tuple(v / length), r, length)


def clamp():
    x, y = CLAMP
    jaw = fuse(box(x - 18, y - 22, 4.0, x + 18, y + 14, 16.0),
               box(x - 18, y - 22, -36.0, x + 18, y - 10, 16.0),
               box(x - 18, y - 22, -36.0, x + 18, y + 14, -24.0))
    return [(jaw, BLACK),
            (cyl((x, y + 2.0, -60.0), (0, 0, 1), 3.0, 36.0), METAL),
            (cyl((x, y - 16.0, 16.0), (0, 0, 1), 8.0, 14.0), BLACK)]


def pole():
    base = (CLAMP[0], CLAMP[1] - 16.0, 30.0)
    a = np.array(base)
    b = np.array(POLE_TOP)
    grip0 = a + (b - a) * 0.35
    grip1 = a + (b - a) * 0.85
    return [(_rod(base, POLE_TOP, 8.0), BLACK),
            (_rod(tuple(grip0), tuple(grip1), 14.0), FOAM)]


def arm():
    joints = [(BRepPrimAPI_MakeSphere_(POLE_TOP, 11.0), BLACK)]
    mid = tuple((np.array(POLE_TOP) + np.array(CAM)) / 2 + [0, 0, 18.0])
    joints.append((BRepPrimAPI_MakeSphere_(mid, 9.0), BLACK))
    joints.append((_rod(POLE_TOP, mid, 7.0), BLACK))
    joints.append((_rod(mid, CAM, 7.0), BLACK))
    knob = (mid[0] - 18.0, mid[1], mid[2])
    joints.append((_rod(mid, knob, 3.0), METAL))
    joints.append((BRepPrimAPI_MakeSphere_(knob, 7.0), (0.40, 0.33, 0.25)))
    return joints


def BRepPrimAPI_MakeSphere_(p, r):
    from OCP.BRepPrimAPI import BRepPrimAPI_MakeSphere
    return BRepPrimAPI_MakeSphere(gp_Pnt(*p), r).Shape()


def camera_base():
    """The camera's base, hanging from its mount, 47 across."""
    x, y, z = CAM
    return [(cyl((x, y, PAN_AXIS[2]), (0, 0, 1), 23.5, z - PAN_AXIS[2]),
             DARK),
            (cyl((x, y, z), (0, 0, 1), 5.0, 8.0), METAL)]


def camera_pan():
    """The yoke under the base; turns about z through PAN_AXIS."""
    x, y, z = PAN_AXIS
    yoke = fuse(cyl((x, y, z - 6.0), (0, 0, 1), 20.0, 6.0),
                box(x - 27.0, y - 10.0, TILT_AXIS[2] - 4.0, x - 22.0,
                    y + 10.0, z - 6.0),
                box(x + 22.0, y - 10.0, TILT_AXIS[2] - 4.0, x + 27.0,
                    y + 10.0, z - 6.0))
    return [(yoke, DARK)]


def camera_tilt():
    """The head with the lens; turns about x through TILT_AXIS."""
    x, y, z = TILT_AXIS
    head = cyl((x - 21.5, y, z), (1, 0, 0), 21.0, 43.0)
    lens = cyl((x, y, z - 21.0), (0, 0, -1), 9.0, 3.0)
    glass = cyl((x, y, z - 24.0), (0, 0, -1), 7.0, 0.5)
    # The lens looks down and away, 30 degrees off the vertical.
    turned = [place(place(s, dx=-x, dy=-y, dz=-z), rx=-30.0, dx=x, dy=y,
                    dz=z) for s in (head, lens, glass)]
    return [(turned[0], DARK), (turned[1], BLACK), (turned[2], SCREEN)]


# The ring lamp above the bench: its reflection in the sheet is the white
# ring in the photos. Outer and inner diameter, thickness, centre.
RING = (260.0, 200.0, 22.0, (330.0, 150.0, 600.0))
LAMP = (0.98, 0.98, 0.95)


def ring_lamp():
    """A ring lamp facing down, its mount not drawn."""
    outer, inner, thick, (x, y, z) = RING
    ring = cut(cyl((x, y, z), (0, 0, 1), outer / 2, thick),
               cyl((x, y, z - 1.0), (0, 0, 1), inner / 2, thick + 2.0))
    face = cut(cyl((x, y, z - 0.5), (0, 0, 1), outer / 2 - 3.0, 0.5),
               cyl((x, y, z - 1.5), (0, 0, 1), inner / 2 + 3.0, 2.5))
    return [(ring, LIGHT), (face, LAMP)]


# Node tree: name, parent, function, pivot (the node's origin).
NODES = [
    ("bench", None, None, (0, 0, 0)),
    ("sheet", "bench", sheet, (0, 0, 0)),
    ("base_board", "bench", base_board, (0, 0, 0)),
    ("panel", "bench", panel, (398.0, 447.0, 0)),
    ("iomcu", "bench", iomcu, (410.0, 352.0, 0)),
    ("injector", "bench", touch_injector, (470.0, 236.0, 0)),
    ("sensor_perfboard", "bench", sensor_perfboard, (478.0, 288.0, 0)),
    ("servo", "bench", servo, (600.0, 205.0, 0)),
    ("as5600", "servo", as5600, (600.0, 198.0, 47.0)),
    ("load_resistor", "bench", load_resistor, (326.0, 318.0, 0)),
    ("instruments", "bench", None, (0, 0, 0)),
    ("logic_analyzer", "instruments", logic_analyzer, (342.0, 260.0, 0)),
    ("supplies", "bench", None, (0, 0, 0)),
    ("host_computer", "bench", host_computer, (105.0, 362.0, 0)),
    ("usb_hub", "bench", usb_hub, (162.0, 350.0, 0)),
    ("power_bank", "bench", power_bank, (192.0, 350.0, 12.0)),
    ("ring_lamp", "bench", ring_lamp, RING[3]),
    ("camera", "bench", None, (0, 0, 0)),
    ("camera_clamp", "camera", clamp, (480.0, 0.0, 0)),
    ("camera_pole", "camera", pole, (480.0, -16.0, 30.0)),
    ("camera_arm", "camera", arm, POLE_TOP),
    ("camera_base", "camera", camera_base, CAM),
    ("camera_pan", "camera_base", camera_pan, PAN_AXIS),
    ("camera_tilt", "camera_pan", camera_tilt, TILT_AXIS),
]


def mesh(shape, deflection):
    """Vertices (mm, source frame) and triangles of a shape."""
    BRepMesh_IncrementalMesh(shape, deflection, False, 0.35, True)
    pts, tris = [], []
    ex = TopExp_Explorer(shape, TopAbs_FACE)
    while ex.More():
        f = TopoDS.Face(ex.Current())
        loc = TopLoc_Location()
        tri = BRep_Tool.Triangulation_s(f, loc)
        if tri is not None:
            tr = loc.Transformation()
            base = len(pts)
            for i in range(1, tri.NbNodes() + 1):
                p = tri.Node(i).Transformed(tr)
                pts.append((p.X(), p.Y(), p.Z()))
            rev = f.Orientation() == TopAbs_REVERSED
            for i in range(1, tri.NbTriangles() + 1):
                a, b, c = tri.Triangle(i).Get()
                tris.append((base + a - 1, base + c - 1, base + b - 1)
                            if rev else (base + a - 1, base + b - 1,
                                         base + c - 1))
        ex.Next()
    return np.array(pts, float), np.array(tris, int)


def to_gltf(v):
    """Source mm, z up -> glTF m, y up."""
    return np.column_stack([v[:, 0], v[:, 2], -v[:, 1]]) / 1000.0


def parts():
    """Node name -> list of (solid, rgba); supplies split in 3 nodes."""
    out = {}
    for name, _parent, fn, _pivot in NODES:
        if fn is not None:
            out[name] = fn()
    for sub, items in supplies():
        out["supply_" + sub] = items
    return out


def scene(deflection=0.15):
    import trimesh
    from trimesh.visual.material import PBRMaterial
    pivots = {n: p for n, _pa, _f, p in NODES}
    parents = {n: pa for n, pa, _f, _p in NODES}
    for sub in ("pd_meter", "buck", "usbc_board"):
        pivots["supply_" + sub] = (0, 0, 0)
        parents["supply_" + sub] = "supplies"
    sc = trimesh.Scene()
    order = [n for n, *_ in NODES] + ["supply_pd_meter", "supply_buck",
                                       "supply_usbc_board"]

    def world(n):
        return np.array(pivots[n], float)

    for n in order:
        parent = parents[n]
        rel = world(n) - (world(parent) if parent else 0.0)
        m = np.eye(4)
        m[:3, 3] = to_gltf(rel.reshape(1, 3))[0]
        sc.graph.update(frame_to=n, frame_from=parent or sc.graph.base_frame,
                        matrix=m)
    for n, items in parts().items():
        groups = {}
        for s, rgba in items:
            rgba = tuple(rgba) + ((1.0,) if len(rgba) == 3 else ())
            groups.setdefault(rgba, []).append(s)
        for k, (rgba, solids) in enumerate(sorted(groups.items())):
            vs, ts, off = [], [], 0
            for s in solids:
                v, t = mesh(s, deflection)
                vs.append(v - world(n))
                ts.append(t + off)
                off += len(v)
            mat = PBRMaterial(baseColorFactor=[int(255 * c) for c in rgba],
                              metallicFactor=0.1, roughnessFactor=0.6,
                              alphaMode="BLEND" if rgba[3] < 1 else
                              "OPAQUE", doubleSided=rgba[3] < 1)
            tm = trimesh.Trimesh(to_gltf(np.vstack(vs)), np.vstack(ts),
                                 process=False)
            tm.visual = trimesh.visual.TextureVisuals(material=mat)
            sc.add_geometry(tm, node_name="%s_mesh%d" % (n, k),
                            geom_name="%s_%d" % (n, k), parent_node_name=n)
    return sc


def render(directory):
    """Draw the bench from the viewer's side, from above and the side."""
    import step_render
    from OCP.AIS import AIS_Shape
    from OCP.Graphic3d import (Graphic3d_BufferType,
                               Graphic3d_MaterialAspect,
                               Graphic3d_NameOfMaterial)
    from OCP.Image import Image_AlienPixMap
    from OCP.TCollection import TCollection_AsciiString
    from OCP.V3d import V3d_TypeOfOrientation as O
    view = step_render.Renderer(1400, 1000)
    items = [it for v in parts().values() for it in v]
    for orient, tag in ((O.V3d_XposYnegZpos, "iso"), (O.V3d_Zpos, "top"),
                        (O.V3d_Yneg, "front")):
        view.ctx.RemoveAll(False)
        for s, rgba in items:
            a = AIS_Shape(s)
            a.SetColor(step_render._rgb(*rgba[:3]))
            a.SetMaterial(Graphic3d_MaterialAspect(
                Graphic3d_NameOfMaterial.Graphic3d_NOM_PLASTIC))
            if len(rgba) > 3 and rgba[3] < 1:
                a.SetTransparency(1 - rgba[3])
            view.ctx.Display(a, 1, -1, False)
        view.view.SetProj(orient)
        view.view.FitAll(0.04, False)
        view.view.Redraw()
        im = Image_AlienPixMap()
        view.view.ToPixMap(im, view.w, view.h,
                           Graphic3d_BufferType.Graphic3d_BT_RGB)
        im.Save(TCollection_AsciiString(str(_models.render(
            directory, "bench_" + tag))))


def main(argv):
    out = Path(argv[0])
    if "--render" in argv:
        render(argv[argv.index("--render") + 1])
    sc = scene()
    out.write_bytes(sc.export(file_type="glb"))
    print("wrote %s, %.1fMB, %d nodes" % (out, out.stat().st_size / 1e6,
                                          len(sc.graph.nodes)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
