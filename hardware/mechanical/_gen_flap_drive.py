#!/usr/bin/env python3
"""Build a KST X10 on a frame with counter bearing, driving a flap.

    flap_drive            frame, double arm, pushrod and horn
    flap_drive_wing       a slice of wing with the flap, cut open, and
                          the fairing over the rod's slot
    flap_drive_assembly   both and the servo of kst_x10.py

A slice of wing, 88 of span, carries a flap hinged in the lower skin.
The servo lies flat under the upper skin. A carbon plate is bonded to
the skin; the servo's two ears and its tab are screwed to it. Two
blocks on the plate hold an arched strut in front of the servo's output
end; a brass bush in the strut carries the outer end of the double arm.
The arm points away from the plate, to the lower skin. A pushrod runs
from the pin between the arm's two cheeks up through a slot in the
upper skin to a horn of two carbon plates. The plates stand in the
flap, bonded to its lower skin, and pass through its upper skin; their
eye lies above the flap's surface, over the hinge. The servo comes out
through an opening in the lower skin, which a cover closes.

Sources: five photos of an X10 on a frame with counter bearing and a
double arm, one shop photo of such a frame's parts (not in the
repository), and kst_x10.py. The photos give the kind of each part and
its look; no part is measured, every size below is chosen here. The
airfoil is _rg15.py.

Chosen: chord 230, skins 0.8, hinge at 0.75 of the chord, shaft at
0.38. Plate 2.0, strut 2.0, blocks 6 x 7 x 5. Arm 7.0 from shaft to
pin, cheeks 2.2 thick and 4.0 apart. Horn eye 3.5 over the flap's upper
surface and 1.0 behind the hinge, plates 1.5 thick and 2.8 apart. Rod
3.0 across, screws M2.5 in brass inserts. The plate lies 0.25 under
the skin at the nearest point, turned to the skin's slope; the gap is
the adhesive's and is not drawn. The wing's upper skin ends on a plane
through the hinge that leans 22 degrees forward, 4.45 before the flap
at the upper surface, so the flap turns up. A fairing, half an
ellipsoid 40 long with a wall of 0.6, covers the slot; it is open
where the skin ends.

Not drawn: threads, the servo's splines, the hinge's tape, adhesive,
the spar's caps, ribs, a seal over the gap. The files hold the parts
at neutral. ``travel`` gives the flap's angle for an angle of the arm
from the same sizes, ``posed`` the parts at that angle, and ``sweep``
checks them from -45 to 75 degrees of the arm.

Frames. The frame's parts are drawn in the servo's frame (kst_x10.py:
origin on the shaft's axis at the case's output end, z = 0 on the face
the ears lie on) and then turned over and set into the wing. The files
are in the wing's frame: origin at the nose on the chord line, x along
the chord to the trailing edge, y along the span, z up. The shaft
points to -y. All sizes are mm.

Usage:
    python3 hardware/mechanical/_gen_flap_drive.py           write
    python3 hardware/mechanical/_gen_flap_drive.py --check   exit 1 when
        a file differs, a solid is not valid, or two solids share volume
        at neutral or at one of 8 angles of the arm
    xvfb-run -a python3 hardware/mechanical/_gen_flap_drive.py \
        --render hardware/mechanical/renders

Needs OCP (pip install cadquery-ocp==8.0.1.1.0).

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import math
import re
import sys
from pathlib import Path

from OCP.Approx import Approx_ParametrizationType
from OCP.Bnd import Bnd_Box
from OCP.BRep import BRep_Builder
from OCP.BRepAlgoAPI import (BRepAlgoAPI_Common, BRepAlgoAPI_Cut,
                             BRepAlgoAPI_Fuse)
from OCP.BRepBndLib import BRepBndLib
from OCP.BRepBuilderAPI import (BRepBuilderAPI_GTransform,
                                BRepBuilderAPI_MakeEdge,
                                BRepBuilderAPI_MakeFace,
                                BRepBuilderAPI_MakeWire,
                                BRepBuilderAPI_Transform)
from OCP.BRepCheck import BRepCheck_Analyzer
from OCP.BRepGProp import BRepGProp
from OCP.BRepPrimAPI import (BRepPrimAPI_MakeBox, BRepPrimAPI_MakeCylinder,
                             BRepPrimAPI_MakePrism, BRepPrimAPI_MakeSphere)
from OCP.collections import Array1_gp_Pnt
from OCP.GC import GC_MakeArcOfCircle
from OCP.GeomAPI import GeomAPI_PointsToBSpline
from OCP.gp import (gp_Ax1, gp_Ax2, gp_Dir, gp_GTrsf, gp_Pnt, gp_Trsf,
                    gp_Vec)
from OCP.GProp import GProp_GProps
from OCP.Interface import Interface_Static
from OCP.ShapeUpgrade import ShapeUpgrade_UnifySameDomain
from OCP.STEPControl import STEPControl_AsIs, STEPControl_Writer
from OCP.TopAbs import TopAbs_SOLID
from OCP.TopExp import TopExp_Explorer
from OCP.TopoDS import TopoDS_Compound

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import _models  # noqa: E402

sys.path.insert(0, str(HERE.parent / "kicad" / "tools"))
import _rg15  # noqa: E402
import kst_x10  # noqa: E402

CARBON = (0.12, 0.12, 0.13)
SKIN = (0.90, 0.91, 0.93)
WOOD = (0.78, 0.68, 0.48)
GOLD = (0.86, 0.60, 0.12)
BRASS = (0.80, 0.66, 0.30)
ALU = (0.80, 0.81, 0.82)
STEEL = (0.62, 0.63, 0.65)

# Wing: chord, skin, the slice's ends along the span, hinge and shaft as
# parts of the chord, the gap at the hinge.
CHORD, SHELL = 230.0, 0.8
SPAN = (-28.0, 60.0)
HINGE_AT, SHAFT_AT, GAP = 0.75, 0.38, 0.6
# The wing's upper skin ends on a plane through the hinge that leans
# forward by UP degrees: the flap turns up that far less the gap.
UP = 22.0
# The arm's travel the collision check covers: from, to, step, degrees.
SWEEP = (-45, 75, 15)
SPAR = (0.27, 2.0)          # part of the chord, thickness
REAR = (13.0, 1.0)          # before the hinge, thickness
NOSE = (3.5, 1.0)           # the flap's nose wall behind the hinge
HATCH = (22.0, 38.0, 4.0, 0.2)  # before and behind the shaft, past the
#                                 plate along the span, play of the cover
BED = 0.25
# Frame, in the servo's frame.
PLATE = 2.0
EDGE = (9.0, 3.0, 2.6, 16.0)    # plate past the case: at each side, at
#                                 the tab's end; corner radius; in front
WINDOW = (-2.0, -31.0, 19.0, -10.0)
BLOCK = (6.0, 9.0, 16.0, 5.0)   # along x, y from, y to, height
STRUT = (11.0, 13.0, 1.5, 5.0, 6.5)  # y from, y to, lower edge, end
#                                      height, radius round the bush
BUSH = (6.0, 3.0, 8.0, 0.6)     # diameter, bore, flange, its thickness
HEAD = (5.6, 1.6)               # the bush screw's head
SCREW = (2.5, 4.5, 2.5, 5.5, 0.5, 3.6)  # thread, head, its height,
#                                         washer, its thickness, insert
# Arm: shaft to pin, hub radius, eye radius, inner cheek from and to,
# outer cheek from and to, boss diameter and its end, barrel diameter.
ARM = (7.0, 3.6, 2.4, 0.8, 3.0, 7.0, 9.2, 5.0, 10.8, 5.4)
PIN = (2.0, 0.6, 9.4)
ROD = 3.0
# Rod end at the arm: thickness, half height, straight length, taper's
# end, half height there, collar's end; collar diameter.
END = (3.4, 1.2, 11.0, 17.0, 1.6, 21.0, 4.2)
# Horn: eye over the flap's upper surface, eye behind the hinge, plate
# thickness, space between the plates, reach into the flap, eye radius,
# pin.
HORN = (3.5, 1.0, 1.5, 2.8, 24.0, 2.2, 1.6)
# Fairing over the slot, half an ellipsoid round the hinge line: length
# before the hinge, height there over the upper surface, half width
# inside, wall.
FAIRING = (40.0, 7.8, 4.5, 0.6)
EXIT = (3.5, 6.0)               # the rod's slot in the upper skin: half
#                                 width, length before the rod meets it
FORK = (2.0, 7.0, 10.0)         # eye radius, straight length, collar's end

X0, X1 = kst_x10.X0, kst_x10.X1
AXIS = kst_x10.THICK / 2
MID = (ARM[4] + ARM[5]) / 2      # the rod's plane, along the shaft


def _split():
    points = [(x * CHORD, z * CHORD) for x, z in _rg15.POINTS]
    nose = min(range(len(points)), key=lambda i: points[i][0])
    return list(reversed(points[:nose + 1])), points[nose:]


UPPER, LOWER = _split()
HINGE = HINGE_AT * CHORD
SHAFT = SHAFT_AT * CHORD


def height(curve, x):
    """The z of one side of the airfoil at x, between its points."""
    for (x0, z0), (x1, z1) in zip(curve, curve[1:], strict=False):
        if x0 <= x <= x1 and x1 > x0:
            return z0 + (z1 - z0) * (x - x0) / (x1 - x0)
    raise ValueError(x)


def inside(x):
    """The inner faces of the two skins at x, (lower, upper)."""
    return height(LOWER, x) + SHELL, height(UPPER, x) - SHELL


# ---- shapes ---------------------------------------------------------------

def _box(x0, y0, z0, x1, y1, z1):
    return BRepPrimAPI_MakeBox(gp_Pnt(x0, y0, z0),
                               gp_Pnt(x1, y1, z1)).Shape()


def _cyl(axis, a, b, radius, d0, d1):
    """A cylinder along x, y or z through (a, b) of the other two."""
    at, way = {"x": ((d0, a, b), (1, 0, 0)), "y": ((a, d0, b), (0, 1, 0)),
               "z": ((a, b, d0), (0, 0, 1))}[axis]
    return BRepPrimAPI_MakeCylinder(
        gp_Ax2(gp_Pnt(*at), gp_Dir(*way)), radius, d1 - d0).Shape()


def _tube(axis, a, b, outer, bore, d0, d1):
    return _cut(_cyl(axis, a, b, outer, d0, d1),
                _cyl(axis, a, b, bore, d0 - 1.0, d1 + 1.0))


def _cut(shape, *tools):
    for tool in tools:
        shape = BRepAlgoAPI_Cut(shape, tool).Shape()
    return shape


def _fuse(shape, *others):
    for other in others:
        shape = BRepAlgoAPI_Fuse(shape, other).Shape()
    tidy = ShapeUpgrade_UnifySameDomain(shape, True, True, False)
    tidy.Build()
    return tidy.Shape()


def _edges(outline, point):
    """Edges of a closed outline.

    Args:
        outline: Corners (a, b); a corner (a, b, (ma, mb)) starts an arc
            through (ma, mb) to the next corner.
        point: Maps (a, b) to a gp_Pnt.
    """
    out = []
    for here, there in zip(outline, outline[1:] + outline[:1], strict=True):
        p, q = point(here[0], here[1]), point(there[0], there[1])
        if len(here) == 3:
            out.append(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(
                p, point(*here[2]), q).Value()).Edge())
        else:
            out.append(BRepBuilderAPI_MakeEdge(p, q).Edge())
    return out


def _prism(edges, way, length):
    wire = BRepBuilderAPI_MakeWire()
    for edge in edges:
        wire.Add(edge)
    face = BRepBuilderAPI_MakeFace(wire.Wire()).Face()
    return BRepPrimAPI_MakePrism(face, gp_Vec(*[k * length
                                                for k in way])).Shape()


def _slab(outline, y0, y1):
    """An outline in (x, z), from y0 to y1."""
    return _prism(_edges(outline, lambda a, b: gp_Pnt(a, y0, b)),
                  (0, 1, 0), y1 - y0)


def _sheet(outline, z0, z1):
    """An outline in (x, y), from z0 to z1."""
    return _prism(_edges(outline, lambda a, b: gp_Pnt(a, b, z0)),
                  (0, 0, 1), z1 - z0)


def _rounded(x0, y0, x1, y1, r):
    k = r * (1 - math.sqrt(0.5))
    return [(x0 + r, y0), (x1 - r, y0, (x1 - k, y0 + k)), (x1, y0 + r),
            (x1, y1 - r, (x1 - k, y1 - k)), (x1 - r, y1),
            (x0 + r, y1, (x0 + k, y1 - k)), (x0, y1 - r),
            (x0, y0 + r, (x0 + k, y0 + k))]


def _spline(points, y):
    array = Array1_gp_Pnt(1, len(points))
    for i, (x, z) in enumerate(points):
        array.SetValue(i + 1, gp_Pnt(x, y, z))
    return BRepBuilderAPI_MakeEdge(GeomAPI_PointsToBSpline(
        array, Approx_ParametrizationType.Approx_ChordLength, 3,
        8).Curve()).Edge()


def _moved(shape, move):
    return BRepBuilderAPI_Transform(shape, move, True).Shape()


def _about_y(x, z, degrees):
    """A turn about the line along y through (x, z)."""
    move = gp_Trsf()
    move.SetRotation(gp_Ax1(gp_Pnt(x, 0.0, z), gp_Dir(0, 1, 0)),
                     math.radians(degrees))
    return move


# ---- place of the frame in the wing ---------------------------------------

def _at(move, x, y, z):
    p = gp_Pnt(x, y, z).Transformed(move)
    return p.X(), p.Y(), p.Z()


def setting():
    """The move from the servo's frame into the wing.

    The frame is turned over about x, turned to the upper skin's slope
    over the plate and lifted until the plate is BED under the skin at
    the nearest point.

    Returns:
        (move, slope in degrees).
    """
    a, b = SHAFT + X0 - EDGE[0], SHAFT + X1 + EDGE[0]
    slope = math.atan2(inside(b)[1] - inside(a)[1], b - a)
    over = gp_Trsf()
    over.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0)), math.pi)
    tilt = gp_Trsf()
    tilt.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0)), -slope)
    turn = tilt * over

    def gap(u):
        x, _y, z = _at(turn, u, 0.0, -PLATE)
        return inside(SHAFT + x)[1] - z

    lift = min(gap(X0 - EDGE[0] + k * (b - a) / 40) for k in range(41))
    shift = gp_Trsf()
    shift.SetTranslation(gp_Vec(SHAFT, 0.0, lift - BED))
    return shift * tilt * over, math.degrees(slope)


def joints():
    """The linkage's points in the wing's (x, z) at neutral.

    Returns:
        A dict: "shaft", "pin", "hinge", "eye".
    """
    move, _slope = setting()
    shaft = _at(move, 0.0, 0.0, AXIS)
    pin = _at(move, 0.0, 0.0, AXIS + ARM[0])
    hinge = (HINGE, height(LOWER, HINGE) + SHELL / 2)
    return {"shaft": (shaft[0], shaft[2]), "pin": (pin[0], pin[2]),
            "hinge": hinge,
            "eye": (HINGE + HORN[1],
                    height(UPPER, HINGE + HORN[1]) + HORN[0])}


def travel(arm):
    """The flap's angle for an angle of the arm.

    Args:
        arm: Degrees the arm is turned from neutral; positive moves the
            pin towards the flap.

    Returns:
        Degrees the flap is turned from neutral, positive trailing edge
        down, or None when the rod does not reach.
    """
    j = joints()
    rod = math.dist(j["pin"], j["eye"])

    # Seen from -y a positive turn about y is clockwise in (x, z): a
    # point over the centre moves to +x.
    pin = _turned(j["pin"], j["shaft"], -arm)

    def miss(flap):
        return math.dist(pin, _turned(j["eye"], j["hinge"], flap)) - rod

    if arm == 0:
        return 0.0
    for way in (1.0, -1.0):
        lo = 0.0
        for step in range(1, 181):
            hi = way * step * 0.5
            if miss(lo) * miss(hi) <= 0:
                for _ in range(50):
                    mid = (lo + hi) / 2
                    lo, hi = (mid, hi) if miss(lo) * miss(mid) > 0 else (
                        lo, mid)
                return (lo + hi) / 2
            lo = hi
    return None


# ---- the frame, in the servo's frame --------------------------------------

def _strut_outline():
    y0, y1, low, end, r = STRUT
    del y0, y1
    a, b = X0 - EDGE[0], X1 + EDGE[0]

    def touch(x, side):
        d = abs(x)
        t = math.acos(r / d)
        angle = math.pi - t if side < 0 else t
        return r * math.cos(angle), AXIS + r * math.sin(angle)

    left, right = touch(a, -1), touch(b, 1)
    return [(a, low), (b, low), (b, end), (right[0], right[1],
                                           (0.0, AXIS + r)),
            left, (a, end)]


def _cheek(y0, y1):
    reach, hub, eye = ARM[:3]
    return _fuse(_cyl("y", 0.0, AXIS, hub, y0, y1),
                 _cyl("y", 0.0, AXIS + reach, eye, y0, y1),
                 _box(-eye, y0, AXIS, eye, y1, AXIS + reach))


def frame():
    """The frame's fixed parts and the arm, in the servo's frame.

    Returns:
        A list of (name, shape, colour).
    """
    a, b = X0 - EDGE[0], X1 + EDGE[0]
    rear = -kst_x10.HEIGHT - kst_x10.TAB[1] - EDGE[1]
    thread, head, head_h, washer, washer_h, insert = SCREW
    places = kst_x10.hole_places()
    plate = _cut(
        _sheet(_rounded(a, rear, b, EDGE[3], EDGE[2]), -PLATE, 0.0),
        _sheet(_rounded(*WINDOW, 2.0), -PLATE - 1.0, 1.0),
        *[_cyl("z", x, y, insert / 2, -PLATE - 1.0, 1.0)
          for x, y in places])
    out = [("plate", plate, CARBON)]
    ear = kst_x10.EAR[2]
    for i, (x, y) in enumerate(places):
        out.append((f"insert {i + 1}",
                    _tube("z", x, y, insert / 2, thread / 2, -PLATE, 0.0),
                    BRASS))
        out.append((f"washer {i + 1}",
                    _tube("z", x, y, washer / 2, thread / 2, ear,
                          ear + washer_h), STEEL))
        top = ear + washer_h + head_h
        screw = _cut(_fuse(_cyl("z", x, y, thread / 2, -PLATE,
                                ear + washer_h),
                           _cyl("z", x, y, head / 2, ear + washer_h, top)),
                     _cyl("z", x, y, 1.0, top - 1.5, top + 1.0))
        out.append((f"screw {i + 1}", screw, STEEL))
    y0, y1 = STRUT[:2]
    for name, x0 in (("block 1", a), ("block 2", b - BLOCK[0])):
        out.append((name, _cut(
            _box(x0, BLOCK[1], 0.0, x0 + BLOCK[0], BLOCK[2], BLOCK[3]),
            _box(x0 - 1.0, y0, STRUT[2], x0 + BLOCK[0] + 1.0, y1,
                 BLOCK[3] + 1.0)), CARBON))
    out.append(("strut", _cut(_slab(_strut_outline(), y0, y1),
                              _cyl("y", 0.0, AXIS, BUSH[0] / 2, y0 - 1.0,
                                   y1 + 1.0)), CARBON))
    out.append(("bush", _fuse(
        _tube("y", 0.0, AXIS, BUSH[0] / 2, BUSH[1] / 2, y0, y1),
        _tube("y", 0.0, AXIS, BUSH[2] / 2, BUSH[1] / 2, y1, y1 + BUSH[3])),
        BRASS))
    seat = y1 + BUSH[3]
    out.append(("bush screw", _cut(
        _fuse(_cyl("y", 0.0, AXIS, BUSH[1] / 2, ARM[5] + 1.0, seat),
              _cyl("y", 0.0, AXIS, HEAD[0] / 2, seat, seat + HEAD[1])),
        _box(-2.0, seat + HEAD[1] - 0.8, AXIS - 0.4, 2.0, seat + HEAD[1]
             + 1.0, AXIS + 0.4),
        _box(-0.4, seat + HEAD[1] - 0.8, AXIS - 2.0, 0.4, seat + HEAD[1]
             + 1.0, AXIS + 2.0)), BRASS))
    shaft = kst_x10.SHAFT
    arm = _cut(
        _fuse(_cheek(ARM[3], ARM[4]), _cheek(ARM[5], ARM[6]),
              _cyl("y", 0.0, AXIS, ARM[9] / 2, ARM[4], ARM[5]),
              _cyl("y", 0.0, AXIS, ARM[7] / 2, ARM[6], ARM[8])),
        _cyl("y", 0.0, AXIS, shaft[0] / 2, 0.0, shaft[1]),
        _cyl("y", 0.0, AXIS, BUSH[1] / 2, ARM[5] + 1.0, ARM[8] + 1.0),
        _cyl("y", 0.0, AXIS + ARM[0], PIN[0] / 2, 0.0, ARM[6] + 1.0))
    out.append(("arm", arm, GOLD))
    out.append(("arm pin", _cyl("y", 0.0, AXIS + ARM[0], PIN[0] / 2,
                                PIN[1], PIN[2]), STEEL))
    return out


# ---- the linkage and the wing, in the wing's frame ------------------------

def _along(start, end, shape):
    """Turn a shape drawn along +x from ``start`` towards ``end``."""
    angle = math.degrees(math.atan2(end[1] - start[1], end[0] - start[0]))
    return _moved(shape, _about_y(start[0], start[1], -angle))


def _rod(pin, eye):
    """Rod end, fork and rod from the pin to the eye, both in (x, z)."""
    y = -MID
    thick, half, straight, taper, small, collar, across = END
    px, pz = pin
    end = _cut(_fuse(
        _cyl("y", px, pz, ARM[2], y - thick / 2, y + thick / 2),
        _slab([(px, pz - half), (px + straight, pz - half),
               (px + taper, pz - small), (px + taper, pz + small),
               (px + straight, pz + half), (px, pz + half)],
              y - thick / 2, y + thick / 2),
        _cyl("x", y, pz, across / 2, px + taper, px + collar)),
        _cyl("y", px, pz, PIN[0] / 2, y - thick, y + thick))
    out = [("rod end", _along(pin, eye, end), ALU)]
    radius, straight, collar = FORK
    ex, ez = eye
    tongue = _cut(_fuse(
        _cyl("y", ex, ez, radius, y - HORN[3] / 2, y + HORN[3] / 2),
        _box(ex - straight, y - HORN[3] / 2, ez - radius, ex,
             y + HORN[3] / 2, ez + radius),
        _cyl("x", y, ez, across / 2, ex - collar, ex - straight)),
        _cyl("y", ex, ez, HORN[6] / 2, y - HORN[3], y + HORN[3]))
    out.append(("fork", _along(eye, (2 * ex - px, 2 * ez - pz), tongue),
                ALU))
    length = math.dist(pin, eye)
    rod = _cyl("x", y, pz, ROD / 2, px + END[5], px + length - collar)
    out.append(("rod", _along(pin, eye, rod), CARBON))
    return out


def _turned(point, centre, degrees):
    """A point of (x, z) turned about a centre, as ``_about_y`` turns."""
    a = math.radians(degrees)
    dx, dz = point[0] - centre[0], point[1] - centre[1]
    return (centre[0] + dx * math.cos(a) + dz * math.sin(a),
            centre[1] - dx * math.sin(a) + dz * math.cos(a))


def linkage(arm=0.0):
    """Rod, its two ends and the horn, in the wing's frame.

    Args:
        arm: Degrees the arm is turned from neutral, as in ``travel``.

    Returns:
        A list of (name, shape, colour). The horn is turned with the
        flap, the rod and its ends run from the turned pin to the turned
        eye.
    """
    j = joints()
    hinge = j["hinge"]
    flap = travel(arm)
    if flap is None:
        raise ValueError(f"the rod does not reach at {arm} degrees")
    swing = _about_y(*hinge, flap)
    pin = _turned(j["pin"], j["shaft"], -arm)
    eye = _turned(j["eye"], hinge, flap)
    out = _rod(pin, eye)
    ex, ez = j["eye"]
    y = -MID
    reach, r = HORN[4], HORN[5]
    front = hinge[0] + NOSE[0] + NOSE[1] + 1.0
    low = inside(front)[0] + 0.2, inside(hinge[0] + reach)[0] + 0.2
    outline = [(ex - r, ez - 1.0), (front, low[0]),
               (hinge[0] + reach, low[1]), (hinge[0] + reach, low[1] + 1.5),
               (ex + r, ez + r), (ex - r, ez + r)]
    for i, side in enumerate((-1, 1)):
        y0 = y + side * (HORN[3] / 2 + HORN[2] / 2) - HORN[2] / 2
        out.append((f"horn plate {i + 1}", _cut(
            _slab(outline, y0, y0 + HORN[2]),
            _cyl("y", ex, ez, HORN[6] / 2, y0 - 1.0, y0 + HORN[2] + 1.0)),
            CARBON))
    span = HORN[3] / 2 + HORN[2] + 0.4
    out.append(("horn pin", _cyl("y", ex, ez, HORN[6] / 2, y - span,
                                 y + span), STEEL))
    if arm:
        out[3:] = [(name, _moved(shape, swing), rgb)
                   for name, shape, rgb in out[3:]]
    return out


def wing():
    """The slice of wing and flap, in the wing's frame.

    Returns:
        A list of (name, shape, colour).
    """
    y0, y1 = SPAN
    outer = _prism([_spline(UPPER, y0),
                    _spline(list(reversed(LOWER)), y0)], (0, 1, 0), y1 - y0)
    xs = [CHORD * (0.012 + 0.9 * k / 60) for k in range(61)]
    up = [(x, inside(x)[1]) for x in xs]
    low = [(x, inside(x)[0]) for x in xs]

    def straight(p, q):
        return BRepBuilderAPI_MakeEdge(gp_Pnt(p[0], y0 - 1, p[1]),
                                       gp_Pnt(q[0], y0 - 1, q[1])).Edge()

    hollow = _prism([_spline(up, y0 - 1), straight(up[-1], low[-1]),
                     _spline(list(reversed(low)), y0 - 1),
                     straight(low[0], up[0])], (0, 1, 0), y1 - y0 + 2)
    shell = _cut(outer, hollow)
    hz = joints()["hinge"][1]
    lean = 60.0 * math.tan(math.radians(UP))
    behind = _slab(
        [(HINGE - GAP / 2, -50.0), (CHORD + 5, -50.0), (CHORD + 5, hz + 60.0),
         (HINGE - GAP / 2 - lean, hz + 60.0), (HINGE - GAP / 2, hz)],
        y0 - 2, y1 + 2)
    main = _cut(shell, behind)
    plates = [shape for name, shape, _rgb in linkage()
              if name.startswith("horn plate")]
    flap = _cut(shell, _box(-5, y0 - 2, -50, HINGE + GAP / 2, y1 + 2, 50),
                *plates)
    j = joints()
    rise = (j["eye"][1] - j["pin"][1]) / (j["eye"][0] - j["pin"][0])
    meets = next(x for x in [j["pin"][0] + k * 0.5 for k in range(400)]
                 if j["pin"][1] + rise * (x - j["pin"][0]) + END[6] / 2
                 > inside(x)[1])
    reach, _high, half, wall = FAIRING
    main = _cut(main, BRepAlgoAPI_Common(
        _box(meets - EXIT[1], -MID - EXIT[0], 2.0, HINGE + 1.0,
             -MID + EXIT[0], 50),
        _stretched(_cyl("z", 0.0, 0.0, 1.0, 0.0, 50.0), reach - 2 * wall,
                   half, 1.0, HINGE, -MID, 0.0)).Shape())
    move, _slope = setting()
    a, b = SHAFT - HATCH[0], SHAFT + HATCH[1]
    rear = -kst_x10.HEIGHT - kst_x10.TAB[1] - EDGE[1]
    ya, yb = -EDGE[3] - HATCH[2], -rear + HATCH[2]
    del move
    hole = _box(a, ya, -50, b, yb, 0.0)
    cover = BRepAlgoAPI_Common(main, _box(
        a + HATCH[3], ya + HATCH[3], -50, b - HATCH[3], yb - HATCH[3],
        0.0)).Shape()
    main = _cut(main, hole)

    def web(x, thick):
        lower, upper = inside(x + thick / 2)
        return _box(x, y0, lower - 0.05, x + thick, y1, upper + 0.05)

    y = -MID
    slot = _box(HINGE - REAR[0] - 1, y - 3.5, inside(HINGE)[0] + 2.0,
                HINGE - REAR[0] + REAR[1] + 1, y + 3.5, 50)
    walls = []
    for name, x, thick, rgb in (("spar web", SPAR[0] * CHORD, SPAR[1],
                                 CARBON),
                                ("rear web", HINGE - REAR[0], REAR[1], WOOD),
                                ("nose wall", HINGE + NOSE[0], NOSE[1],
                                 WOOD)):
        wall = BRepAlgoAPI_Common(web(x, thick), hollow).Shape()
        if name == "rear web":
            wall = _cut(wall, slot)
        if name == "nose wall":
            wall = _cut(wall, *plates)
        walls.append((name, wall, rgb))
    return [("wing", main, SKIN), ("flap", flap, SKIN),
            ("cover", cover, SKIN)] + walls + [
                ("fairing", _fairing(outer, behind), SKIN)]


def _stretched(shape, a, b, c, x, y, z):
    """A shape scaled by (a, b, c) along the axes, then set at a point."""
    scale = gp_GTrsf()
    for k, factor in enumerate((a, b, c)):
        scale.SetValue(k + 1, k + 1, factor)
    place = gp_Trsf()
    place.SetTranslation(gp_Vec(x, y, z))
    return _moved(BRepBuilderAPI_GTransform(shape, scale, True).Shape(),
                  place)


def _fairing(outer, behind):
    """The shell over the rod's slot, on the wing's upper skin.

    Half an ellipsoid with its centre on the hinge line, turned to the
    upper surface's slope. Its inside is an ellipsoid a wall smaller.

    Args:
        outer: The airfoil as a solid; the shell ends on its surface.
        behind: Everything behind the wing's upper skin; the shell ends
            where the skin does and is open there.
    """
    reach, high, half, wall = FAIRING
    top = height(UPPER, HINGE)
    slope = (top - height(UPPER, HINGE - reach)) / reach
    turn = _about_y(HINGE, top, math.degrees(math.atan(-slope)))

    def body(shrink):
        ball = BRepPrimAPI_MakeSphere(1.0).Shape()
        return _moved(_stretched(ball, reach - 2 * shrink,
                                 half + wall - shrink, high - shrink,
                                 HINGE, -MID, top), turn)

    return _cut(body(0.0), body(wall), outer, behind)


def servo():
    """The servo's solids of kst_x10.step, set into the wing."""
    import step_check
    move, _slope = setting()
    return [(f"servo {i + 1}", _moved(solid, move), rgb or ALU)
            for i, (solid, rgb) in enumerate(
                step_check.read(_models.path("kst_x10"))[1])]


def parts():
    """Every part in the wing's frame.

    Returns:
        A dict of file stem to a list of (name, shape, colour).
    """
    move, _slope = setting()
    drive = [(name, _moved(shape, move), rgb)
             for name, shape, rgb in frame()] + linkage()
    section = wing()
    return {"flap_drive": drive, "flap_drive_wing": section,
            "flap_drive_assembly": section + drive + servo()}


def posed(arm, fixed=None):
    """The assembly with the arm turned.

    Args:
        arm: Degrees the arm is turned from neutral, as in ``travel``.
        fixed: (wing(), frame(), servo()) when they are built already.

    Returns:
        A list of (name, shape, colour): arm and its pin turned about
        the shaft, flap, nose wall and horn turned about the hinge, the
        rod between them.
    """
    section, drive, case = fixed or (wing(), frame(), servo())
    j = joints()
    move, _slope = setting()
    turn = _about_y(*j["shaft"], -arm)
    swing = _about_y(*j["hinge"], travel(arm))
    out = []
    for name, shape, rgb in section:
        if name in ("flap", "nose wall"):
            shape = _moved(shape, swing)
        out.append((name, shape, rgb))
    for name, shape, rgb in drive:
        shape = _moved(shape, move)
        if name in ("arm", "arm pin"):
            shape = _moved(shape, turn)
        out.append((name, shape, rgb))
    return out + linkage(arm) + case


def sweep():
    """Check the assembly at every SWEEP[2] degrees of the arm's travel.

    Returns:
        A list of error texts, each with its arm angle, empty when no
        two solids share volume from SWEEP[0] to SWEEP[1].
    """
    fixed = (wing(), frame(), servo())
    low, high, step = SWEEP
    errors = []
    for arm in range(low, high + 1, step):
        if arm:
            errors += [f"arm {arm} degrees: {text}"
                       for text in check(posed(arm, fixed))]
    return errors


# ---- files and checks -----------------------------------------------------

def step_text(stem, items):
    """The STEP text of named, coloured solids, with a fixed header.

    The solids are written as one compound, which gives the same text
    at each run; names and colours are then set in the text, one
    STYLED_ITEM for each MANIFOLD_SOLID_BREP in the file's order.
    """
    compound, builder = TopoDS_Compound(), BRep_Builder()
    builder.MakeCompound(compound)
    for _name, shape, _rgb in items:
        builder.Add(compound, shape)
    STEPControl_Writer()    # the settings exist once a writer does
    Interface_Static.SetCVal_s("write.step.schema", "AP214IS")
    if not Interface_Static.SetIVal_s("write.step.assembly", 0):
        raise RuntimeError("write.step.assembly is not set")
    writer = STEPControl_Writer()
    writer.Transfer(compound, STEPControl_AsIs)
    path = _models.ROOT / f".{stem}.tmp"
    writer.Write(str(path))
    text = path.read_text()
    path.unlink()
    text = re.sub(r"FILE_NAME\(.*?\);", "FILE_NAME('" + stem + ".step',"
                  "'2000-01-01T00:00:00',(''),(''),'rcbench "
                  "_gen_flap_drive.py','OpenCASCADE','');", text, count=1,
                  flags=re.S)
    solids = re.findall(r"#(\d+) = MANIFOLD_SOLID_BREP\(''", text)
    if len(solids) != len(items):
        raise RuntimeError(f"{stem}: {len(solids)} solids written for "
                           f"{len(items)} parts")
    names = iter(name for name, _shape, _rgb in items)
    text = re.sub(r"MANIFOLD_SOLID_BREP\(''",
                  lambda _m: f"MANIFOLD_SOLID_BREP('{next(names)}'", text)
    context = re.search(r"= SHAPE_REPRESENTATION\('',\(.*?\),\s*(#\d+)\)",
                        text, flags=re.S).group(1)
    last = max(int(n) for n in re.findall(r"^#(\d+) =", text, flags=re.M))
    lines, styled = [], []
    for solid, (_name, _shape, rgb) in zip(solids, items, strict=True):
        n = last + 1
        last += 8
        colour = ",".join(f"{c:.4f}" for c in rgb)
        lines += [
            f"#{n} = COLOUR_RGB('',{colour});",
            f"#{n + 1} = FILL_AREA_STYLE_COLOUR('',#{n});",
            f"#{n + 2} = FILL_AREA_STYLE('',(#{n + 1}));",
            f"#{n + 3} = SURFACE_STYLE_FILL_AREA(#{n + 2});",
            f"#{n + 4} = SURFACE_SIDE_STYLE('',(#{n + 3}));",
            f"#{n + 5} = SURFACE_STYLE_USAGE(.BOTH.,#{n + 4});",
            f"#{n + 6} = PRESENTATION_STYLE_ASSIGNMENT((#{n + 5}));",
            f"#{n + 7} = STYLED_ITEM('color',(#{n + 6}),#{solid});"]
        styled.append(f"#{n + 7}")
    lines.append(f"#{last + 1} = MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_"
                 f"REPRESENTATION('',({','.join(styled)}),{context});")
    head, _end, tail = text.rpartition("ENDSEC;")
    return head + "\n".join(lines) + "\nENDSEC;" + tail


def pieces(shape):
    """The number of solids a shape is made of."""
    count, walk = 0, TopExp_Explorer(shape, TopAbs_SOLID)
    while walk.More():
        count += 1
        walk.Next()
    return count


def volume(shape):
    """A shape's volume in mm3."""
    props = GProp_GProps()
    BRepGProp.VolumeProperties_s(shape, props)
    return props.Mass()


def extent(shape):
    """A shape's box as (x0, y0, z0, x1, y1, z1)."""
    box = Bnd_Box()
    BRepBndLib.AddOptimal_s(shape, box, False, False)
    lo, hi = box.CornerMin(), box.CornerMax()
    return (lo.X(), lo.Y(), lo.Z(), hi.X(), hi.Y(), hi.Z())


def check(items):
    """Check the assembly's solids.

    Args:
        items: (name, shape, colour) of every solid.

    Returns:
        A list of error texts, empty when every solid is valid and in
        one piece and no two share volume.
    """
    errors = []
    for name, shape, _rgb in items:
        if not BRepCheck_Analyzer(shape).IsValid():
            errors.append(f"{name}: not a valid solid")
        if pieces(shape) != 1:
            errors.append(f"{name}: {pieces(shape)} pieces")
    boxes = [extent(shape) for _n, shape, _c in items]
    for i, (name, shape, _rgb) in enumerate(items):
        for k in range(i):
            if name.startswith("servo") and items[k][0].startswith("servo"):
                continue
            a, b = boxes[i], boxes[k]
            if not all(a[n] < b[n + 3] and b[n] < a[n + 3]
                       for n in range(3)):
                continue
            shared = volume(BRepAlgoAPI_Common(shape, items[k][1]).Shape())
            if shared > 1e-3:
                errors.append(f"{items[k][0]} and {name} share "
                              f"{shared:.3f}mm3")
    return errors


def render(built, directory):
    """Draw the assembly: 5 PNGs.

    Args:
        built: The parts as ``parts`` gives them.
        directory: Where the images go.
    """
    import step_render
    from OCP.AIS import AIS_Shape
    from OCP.Aspect import Aspect_TypeOfLine
    from OCP.Graphic3d import (Graphic3d_BufferType,
                               Graphic3d_MaterialAspect,
                               Graphic3d_NameOfMaterial)
    from OCP.Image import Image_AlienPixMap
    from OCP.Prs3d import Prs3d_LineAspect
    from OCP.TCollection import TCollection_AsciiString
    from OCP.V3d import V3d_TypeOfOrientation as Towards

    view = step_render.Renderer(1600, 1100)
    line = Prs3d_LineAspect(step_render._rgb(0.1, 0.1, 0.1),
                            Aspect_TypeOfLine.Aspect_TOL_SOLID, 1.0)

    def draw(items, towards, name, clear=()):
        view.ctx.RemoveAll(False)
        for part, shape, rgb in items:
            shown = AIS_Shape(shape)
            shown.SetColor(step_render._rgb(*rgb))
            shown.SetMaterial(Graphic3d_MaterialAspect(
                Graphic3d_NameOfMaterial.Graphic3d_NOM_PLASTIC))
            shown.Attributes().SetFaceBoundaryDraw(True)
            shown.Attributes().SetFaceBoundaryAspect(line)
            view.ctx.Display(shown, 1, -1, False)
            if part in clear:
                view.ctx.SetTransparency(shown, 0.8, False)
        view.view.SetProj(towards)
        view.view.FitAll(0.03, False)
        view.view.Redraw()
        image = Image_AlienPixMap()
        view.view.ToPixMap(image, view.w, view.h,
                           Graphic3d_BufferType.Graphic3d_BT_RGB)
        image.Save(TCollection_AsciiString(str(Path(directory) / name)))

    move, _slope = setting()
    back = move.Inverted()
    unit = [(name, _moved(shape, back), rgb)
            for name, shape, rgb in built["flap_drive"] + servo()]
    section = built["flap_drive_wing"]
    inner = built["flap_drive"] + servo()
    open_wing = [item for item in section if item[0] != "cover"]
    half = _box(-20, -200, -60, CHORD + 20, -STRUT[1] - 0.5, 60)
    cut = [(name, _cut(shape, half), rgb) for name, shape, rgb in section]
    draw(unit, Towards.V3d_XposYposZpos, "flap_drive_unit.png")
    draw(unit, Towards.V3d_XnegYposZpos, "flap_drive_unit_2.png")
    draw(open_wing + inner, Towards.V3d_XposYnegZneg,
         "flap_drive_below.png")
    draw(cut + inner, Towards.V3d_Yneg, "flap_drive_section.png")
    draw(section + inner, Towards.V3d_XposYnegZpos, "flap_drive_above.png",
         clear=("wing", "flap"))


def main(argv):
    """Write or check the files.

    Args:
        argv: Command-line arguments without the program name.

    Returns:
        0, or 1 for a stale file, a bad solid or a collision.
    """
    built = parts()
    if "--render" in argv:
        render(built, argv[argv.index("--render") + 1])
        return 0
    errors = check(built["flap_drive_assembly"]) + sweep()
    for stem, items in built.items():
        text = step_text(stem, items)
        path = _models.path(stem)
        if "--check" in argv:
            if not path.exists() or path.read_text() != text:
                errors.append(f"STALE {path.name}")
            continue
        path.write_text(text)
        print(f"wrote {path.name}, {len(items)} solids")
    for error in errors:
        print(error)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
