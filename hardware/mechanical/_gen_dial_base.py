#!/usr/bin/env python3
"""Write the two parts of a hand-held base for the M5Stack Dial.

The base is a body of revolution about the dial's axis, shaped like a
pebble: a flat top with the 45.2 hole the dial mounts in, a side that
swells on one arc of radius 49.5 from 64 across at the top to 72 at the
belly, and an underside rounded on a radius of 11.5, which the palm
closes round. The rounding ends 50 degrees round, where a cone of the
same slope runs on to a flat foot 57 across, so the part prints
without support. 18 shallow flutes, 5.6 wide and 0.7 deep, cross the
belly and fade out above and below it: they keep the base from turning
in the hand while the knob is turned, and repeat the knob's grooves.
Every face is a plane, a cylinder, a cone or a piece of a torus. It
splits level at the belly into two printed parts:

    dial_base_top.step      the dial's seat: top plate 2.0 thick, the
                            wall down to the split, 4 bosses for M3
                            screws, an opening for a USB Type-C plug
                            with a tube that guides the plug to the
                            dial's socket
    dial_base_bottom.step   the floor with a pocket for the lithium
                            polymer cell 102540 (25 x 40.5 x 10), a
                            rim that centers it in the top part and 4
                            columns the screws pass through

Frame: the dial's (hardware/mechanical/m5dial.py). z = 0 is the top
plate's upper face, where the ring on the dial's flange seats; the knob
points to +z; the dial's "^" mark is at +y and its USB socket at -y.
All sizes are mm.

The dial's ring nut holds it from below the top plate, on a plate 2.0
thick so the nut stays clear of the dial's USB notch. Each M3 x 12 pan
head screw enters a column of the bottom part from below, its head
sunk 8.0 into the foot, and turns into a brass threaded insert (ruthex
RX-M3Sx4.0, 4.0 long) melted into a boss of the top part from the
joint: a hole of 4.0, 5.0 deep, in a boss 7.2 across.
The cell lies flat in its pocket, its length across the "^" mark, its
wires towards +y, the side away from the USB opening. The dial's
battery socket (1.25 pitch, 2 pins) is on its main board under the
Stamp-S3, at the USB end (M5Stack's picture "Dial Lithium Battery
Expansion Interface", docs.m5stack.com). The wires run under the
Stamp-S3 and leave the dial at its +y end, away from the USB plug, then
drop to the cell. A pad of foam 2 thick lies between the cell and the
dial; it holds the cell and leaves it room to swell.

``check`` fails when a part is not one valid solid or has material in
a flute, and puts the dial, its nut and the cell where they sit and
fails when one shares volume with a part of the base. The bosses' webs,
the rim's ledge, the screw columns and the USB tube are cut to the
fluted shape, so each flute is open over its whole length.

Usage:
    python3 hardware/mechanical/_gen_dial_base.py           write
    python3 hardware/mechanical/_gen_dial_base.py --check   exit 1 when
                                                            a file
                                                            differs or
                                                            a part
                                                            collides

    xvfb-run -a python3 hardware/mechanical/_gen_dial_base.py --render DIR

Writes models/casing/dial_base_top.step and dial_base_bottom.step and,
for printing, the same 2 as .stl.
--render draws the base with dial, nut and cell to DIR/casing/dial_base.png,
taken apart to DIR/casing/dial_base_exploded.png and cut open to
DIR/casing/dial_base_section.png, 1200 x 1000 pixels. Needs OCP, the
OpenCASCADE binding (pip install cadquery-ocp==8.0.1.1.0); --render
needs an OpenGL context: an X display, or xvfb-run with Mesa.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import math
import re
import sys
from pathlib import Path

from OCP.Bnd import Bnd_Box
from OCP.BRepAlgoAPI import (BRepAlgoAPI_Common, BRepAlgoAPI_Cut,
                             BRepAlgoAPI_Fuse)
from OCP.BRepBndLib import BRepBndLib
from OCP.BRepBuilderAPI import (BRepBuilderAPI_MakeEdge,
                                BRepBuilderAPI_MakeFace,
                                BRepBuilderAPI_MakeWire,
                                BRepBuilderAPI_Transform)
from OCP.BRepCheck import BRepCheck_Analyzer
from OCP.BRepGProp import BRepGProp
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.BRepPrimAPI import (BRepPrimAPI_MakeBox, BRepPrimAPI_MakeCylinder,
                             BRepPrimAPI_MakeRevol)
from OCP.GC import GC_MakeArcOfCircle
from OCP.gp import gp_Ax1, gp_Ax2, gp_Dir, gp_Pnt, gp_Trsf, gp_Vec
from OCP.GProp import GProp_GProps
from OCP.Interface import Interface_Static
from OCP.Quantity import Quantity_Color, Quantity_TOC_RGB
from OCP.STEPCAFControl import STEPCAFControl_Writer
from OCP.ShapeUpgrade import ShapeUpgrade_UnifySameDomain
from OCP.STEPControl import STEPControl_AsIs
from OCP.StlAPI import StlAPI_Writer
from OCP.TCollection import TCollection_ExtendedString
from OCP.TDataStd import TDataStd_Name
from OCP.TDocStd import TDocStd_Document
from OCP.TopAbs import TopAbs_SOLID
from OCP.TopExp import TopExp_Explorer
from OCP.XCAFDoc import XCAFDoc_ColorGen, XCAFDoc_DocumentTool

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import _models  # noqa: E402

sys.path.insert(0, str(HERE.parent / "kicad" / "tools"))

GRAPHITE = (0.20, 0.21, 0.23)
WALL = 2.4
# Outer profile, (radius, z): the belly's widest point, the radius of
# the underside's rounding, the radius of the top's edge, and the
# radius the side meets the top at before that edge.
BELLY, UNDER, EDGE, TOP_R = (36.0, -21.0), 11.5, 2.0, 32.0
TURN = 50.0             # degrees of the underside's rounding
# Flutes: number of places round the base, radius, depth at the belly,
# and the half angle about the USB opening that stays plain.
FLUTES = (20, 6.0, 0.7, 14.0)
HEIGHT = -BELLY[1] + UNDER
# The side's arc: through (TOP_R, -1.5) and upright at the belly.
SIDE = ((BELLY[0] - TOP_R) ** 2 + (BELLY[1] + 1.5) ** 2) / (
    2 * (BELLY[0] - TOP_R))
SIDE_AT = (BELLY[0] - SIDE, BELLY[1])
PLATE = 2.0             # top plate at the dial's hole
HOLE = 45.2             # the dial mounts in 45.0 +- 0.2
SPLIT = -19.0           # z of the joint
FLOOR = -HEIGHT + WALL   # z of the floor's upper face
# Rim of the bottom part inside the top part: height, thickness, gap.
RIM = (2.0, 1.0, 0.2)
LEDGE = 1.5             # height of the ledge's upright side under the rim
# Screws: circle radius, angles, boss diameter beside the ring nut,
# clearance hole, head recess diameter, z of the head's seat, diameter
# of the bottom part's columns.
SCREW_R, SCREW_AT = 29.0, (45.0, 135.0, 225.0, 315.0)
BOSS, CLEAR, HEAD, SEAT, COLUMN = 6.0, 3.4, 6.2, -24.5, 7.2
# Threaded insert for melting in, ruthex RX-M3Sx4.0: its hole's
# diameter and depth and the wall round the hole, as the maker's label
# gives them (4.0, 5.0 at least, 1.6 at least), and the insert's
# length. RELIEF is the bore above the hole for the screw's end: its
# diameter and the z it reaches up to. The boss is INSERT[0] + 2 x
# INSERT[2] across up to WIDE_TO, 1.0 under the ring nut, and narrows
# to BOSS in 2 steps of 0.3 above it.
INSERT = (4.0, 5.0, 1.6, 4.0)
RELIEF = (3.2, -11.5)
WIDE_TO = -8.0
# USB opening at -y: width, height, z of its center, corner radius.
USB = (13.4, 7.6, -12.13, 1.5)
# Guide tube round the opening: its wall, the distance of its inner end
# from the axis, and the distance from the axis beyond which a block
# ties it to the top plate, clear of the ring nut. The inner end stays
# 2.5 off the dial's thread.
TUBE = (1.0, 24.5, 26.5)
# Cell pocket: the cell's length (along x) and width, the play at each
# end and at each side, the rails' thickness and height.
CELL = (40.5, 25.0, 1.2, 0.5, 1.4, 6.0)
GROOVE = 0.4            # shadow gap at the joint


def _arc(center, radius, a0, a1):
    """An arc in the x-z plane (radius, z) from angle a0 to a1, degrees."""
    def at(a):
        return gp_Pnt(center[0] + radius * math.cos(math.radians(a)), 0.0,
                      center[1] + radius * math.sin(math.radians(a)))
    return BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(
        at(a0), at((a0 + a1) / 2), at(a1)).Value()).Edge()


def _on(center, radius, a):
    return (center[0] + radius * math.cos(math.radians(a)),
            center[1] + radius * math.sin(math.radians(a)))


def _revolve(edges):
    """The solid a closed chain of edges sweeps about z."""
    wire = BRepBuilderAPI_MakeWire()
    for edge in edges:
        wire.Add(edge)
    face = BRepBuilderAPI_MakeFace(wire.Wire()).Face()
    return BRepPrimAPI_MakeRevol(
        face, gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1))).Shape()


def _line(a, b):
    return BRepBuilderAPI_MakeEdge(gp_Pnt(a[0], 0.0, a[1]),
                                   gp_Pnt(b[0], 0.0, b[1])).Edge()


def _profile(inset, top):
    """The edges of the pebble's half section, ``inset`` inside it.

    Args:
        inset: Distance inside the outer surface, 0 for the surface.
        top: z of the flat top of this section.

    Returns:
        The closed chain of edges, from the axis round to the axis.
    """
    side, under = SIDE - inset, UNDER - inset
    low = (BELLY[0] - UNDER, BELLY[1])
    floor = (0.0, -HEIGHT + inset)
    if inset:
        # A plain corner where the side meets the flat top.
        angle = math.degrees(math.asin((top - SIDE_AT[1]) / side))
        start = _on(SIDE_AT, side, angle)
        edges = [_line((0.0, top), start)]
    else:
        # The top's edge, rounded: tangent to the top and to the side.
        reach = math.sqrt((side - EDGE) ** 2
                          - (top - EDGE - SIDE_AT[1]) ** 2)
        center = (SIDE_AT[0] + reach, top - EDGE)
        angle = math.degrees(math.atan2(center[1] - SIDE_AT[1], reach))
        edges = [_line((0.0, top), (center[0], top)),
                 _arc(center, EDGE, 90.0, angle)]
    end = _on(low, under, -TURN)
    slope = math.tan(math.radians(TURN))
    foot = (end[0] - (end[1] - floor[1]) * slope, floor[1])
    return edges + [_arc(SIDE_AT, side, angle, 0.0),
                    _arc(low, under, 0.0, -TURN), _line(end, foot),
                    _line(foot, (0.0, floor[1])),
                    _line((0.0, floor[1]), (0.0, top))]


def inner_radius(z):
    """The inner wall's radius at height z."""
    if z >= BELLY[1]:
        return SIDE_AT[0] + math.sqrt((SIDE - WALL) ** 2
                                      - (z - SIDE_AT[1]) ** 2)
    low, under = BELLY[0] - UNDER, UNDER - WALL
    end = _on((low, BELLY[1]), under, -TURN)
    if z >= end[1]:
        return low + math.sqrt(under ** 2 - (z - BELLY[1]) ** 2)
    return end[0] - (end[1] - z) * math.tan(math.radians(TURN))


def flutes():
    """The cylinders that cut the flutes, one for each flute."""
    count, radius, depth, plain = FLUTES
    reach = BELLY[0] + radius - depth
    out = []
    for k in range(count):
        angle = 360.0 * (k + 0.5) / count
        if abs((angle - 270.0 + 180.0) % 360.0 - 180.0) < plain:
            continue
        out.append(_turned(_cylinder(reach, 0.0, radius, -HEIGHT - 1, 1.0),
                           angle))
    return out


_FLUTED = []


def _fluted():
    """The pebble with its flutes, without its cavity.

    Everything added inside the base is cut to this shape, so nothing
    fills a flute.
    """
    if not _FLUTED:
        solid = _body()
        for flute in flutes():
            solid = BRepAlgoAPI_Cut(solid, flute).Shape()
        _FLUTED.append(solid)
    return _FLUTED[0]


def shell():
    """The base in one piece: hollow, fluted, with the dial's hole."""
    solid = BRepAlgoAPI_Cut(_fluted(),
                            _revolve(_profile(WALL, -PLATE))).Shape()
    return BRepAlgoAPI_Cut(solid, _cylinder(0, 0, HOLE / 2, -PLATE - 1,
                                            1.0)).Shape()


def _cylinder(x, y, radius, z0, z1):
    return BRepPrimAPI_MakeCylinder(
        gp_Ax2(gp_Pnt(x, y, z0), gp_Dir(0, 0, 1)), radius, z1 - z0).Shape()


def _box(x0, y0, z0, x1, y1, z1):
    return BRepPrimAPI_MakeBox(gp_Pnt(x0, y0, z0), gp_Pnt(x1, y1, z1)).Shape()


def _turned(shape, angle):
    turn = gp_Trsf()
    turn.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)),
                     math.radians(angle))
    return BRepBuilderAPI_Transform(shape, turn, True).Shape()


def _screw_points():
    return [(SCREW_R * math.cos(math.radians(a)),
             SCREW_R * math.sin(math.radians(a)), a) for a in SCREW_AT]


def parts():
    """Build both parts.

    Returns:
        A dict of file stem to solid.
    """
    solid = shell()
    big = 60.0
    top = BRepAlgoAPI_Common(
        solid, _box(-big, -big, SPLIT, big, big, 5.0)).Shape()
    bottom = BRepAlgoAPI_Common(
        solid, _box(-big, -big, -HEIGHT - 5, big, big, SPLIT)).Shape()
    # Shadow gap: the top part's outer edge at the joint steps back.
    ring = BRepAlgoAPI_Cut(
        _cylinder(0, 0, big, SPLIT, SPLIT + GROOVE),
        _cylinder(0, 0, BELLY[0] - GROOVE, SPLIT - 1, SPLIT + 1)).Shape()
    top = BRepAlgoAPI_Cut(top, ring).Shape()
    # Bosses of the top part: each hangs from the top plate, so the
    # part prints top down without support, and a web ties it to the
    # wall above the bottom part's rim.
    for x, y, angle in _screw_points():
        web = _turned(_box(SCREW_R, -1.5, SPLIT + RIM[0] + 0.3,
                           BELLY[0], 1.5, -PLATE), angle)
        hole, deep, wall, _length = INSERT
        boss = BRepAlgoAPI_Fuse(_cylinder(x, y, BOSS / 2, SPLIT, -PLATE),
                                web).Shape()
        wide = hole / 2 + wall
        step = (wide - BOSS / 2) / 2
        boss = BRepAlgoAPI_Fuse(boss, _cylinder(
            x, y, wide, SPLIT, WIDE_TO)).Shape()
        # The boss widens in 2 steps of 0.3, each as high as the 2
        # together are wide: an overhang the print carries. A cone
        # would meet the web in a curve OpenCASCADE computes
        # differently from one processor to the next.
        boss = BRepAlgoAPI_Fuse(boss, _cylinder(
            x, y, wide - step, WIDE_TO, WIDE_TO + 2 * step)).Shape()
        top = BRepAlgoAPI_Fuse(
            top, BRepAlgoAPI_Common(boss, _fluted()).Shape()).Shape()
        top = BRepAlgoAPI_Cut(top, _cylinder(x, y, hole / 2, SPLIT - 1,
                                             SPLIT + deep)).Shape()
        top = BRepAlgoAPI_Cut(top, _cylinder(x, y, RELIEF[0] / 2,
                                             SPLIT + deep - 0.5,
                                             RELIEF[1])).Shape()
    # USB opening at -y, in a tube that guides the plug to the socket.
    width, height, z, corner = USB
    wall, near, tie = TUBE
    tube = _bar(width + 2 * wall, height + 2 * wall, corner + wall, z, near,
                big)
    tube = BRepAlgoAPI_Fuse(tube, _box(
        -width / 2 - wall, -big, z, width / 2 + wall, -tie,
        -PLATE)).Shape()
    top = BRepAlgoAPI_Fuse(
        top, BRepAlgoAPI_Common(tube, _fluted()).Shape()).Shape()
    top = BRepAlgoAPI_Cut(top, _bar(width, height, corner, z, 20.0,
                                    big)).Shape()
    # Rim of the bottom part, rising into the top part. It stands on a
    # ledge that fills the wall in to the rim's inner side below the
    # joint and ends in a 45 degree slope, so it prints without support.
    rise, thick, gap = RIM
    outer = inner_radius(SPLIT + rise) - gap
    inside, drop = outer - thick, LEDGE
    slope = BELLY[0] - inside
    rim = _revolve([
        _line((inside, SPLIT + rise), (outer, SPLIT + rise)),
        _line((outer, SPLIT + rise), (outer, SPLIT)),
        _line((outer, SPLIT), (BELLY[0], SPLIT)),
        _line((BELLY[0], SPLIT), (BELLY[0], SPLIT - drop - slope)),
        _line((BELLY[0], SPLIT - drop - slope), (inside, SPLIT - drop)),
        _line((inside, SPLIT - drop), (inside, SPLIT + rise))])
    bottom = BRepAlgoAPI_Fuse(
        bottom, BRepAlgoAPI_Common(rim, _fluted()).Shape()).Shape()
    # The rim leaves each boss room: a notch gap wider than the boss.
    for x, y, _angle in _screw_points():
        bottom = BRepAlgoAPI_Cut(bottom, _cylinder(
            x, y, INSERT[0] / 2 + INSERT[2] + gap, SPLIT,
            SPLIT + rise + 1.0)).Shape()
    # Screw columns with clearance holes and head recesses.
    for x, y, _angle in _screw_points():
        column = BRepAlgoAPI_Common(
            _cylinder(x, y, COLUMN / 2, -HEIGHT, SPLIT),
            _fluted()).Shape()
        bottom = BRepAlgoAPI_Fuse(bottom, column).Shape()
    for x, y, _angle in _screw_points():
        bottom = BRepAlgoAPI_Cut(bottom, _cylinder(
            x, y, CLEAR / 2, -HEIGHT - 1, SPLIT + 1)).Shape()
        bottom = BRepAlgoAPI_Cut(bottom, _cylinder(
            x, y, HEAD / 2, -HEIGHT - 1, SEAT)).Shape()
    # Rails round the cell, open at the corners: the wires leave at one.
    length, width, end, side, rail, high = CELL
    half_x, half_y = length / 2 + end, width / 2 + side
    for x0, y0, x1, y1 in (
            (-half_x + 6, half_y, half_x - 6, half_y + rail),
            (-half_x + 6, -half_y - rail, half_x - 6, -half_y),
            (half_x, -half_y + 5, half_x + rail, half_y - 5),
            (-half_x - rail, -half_y + 5, -half_x, half_y - 5)):
        bottom = BRepAlgoAPI_Fuse(bottom, _box(
            x0, y0, FLOOR - 0.2, x1, y1, FLOOR + high)).Shape()
    # Faces that lie on one surface become one face: the end of a web
    # that meets a flute leaves no outline in it.
    return {"dial_base_top": _merged(top),
            "dial_base_bottom": _merged(bottom)}


def _merged(shape):
    """The shape with neighbouring faces of one surface made one."""
    unify = ShapeUpgrade_UnifySameDomain(shape, True, True, True)
    unify.Build()
    return unify.Shape()


def _bar(width, height, corner, z, near, far):
    """A bar along -y with rounded edges, from y = -near to y = -far."""
    x, h, r = width / 2, height / 2, corner
    bar = BRepAlgoAPI_Fuse(_box(-x + r, -far, z - h, x - r, -near, z + h),
                           _box(-x, -far, z - h + r, x, -near,
                                z + h - r)).Shape()
    for cx in (-x + r, x - r):
        for cz in (z - h + r, z + h - r):
            edge = BRepPrimAPI_MakeCylinder(
                gp_Ax2(gp_Pnt(cx, -near, cz), gp_Dir(0, -1, 0)), r,
                far - near).Shape()
            bar = BRepAlgoAPI_Fuse(bar, edge).Shape()
    return bar


_BODY = []


def _body():
    """The pebble without its cavity."""
    if not _BODY:
        _BODY.append(_revolve(_profile(0.0, 0.0)))
    return _BODY[0]


def step_text(stem, solid):
    """The STEP text of one part, with its colour and a fixed header."""
    doc = TDocStd_Document(TCollection_ExtendedString("rcbench"))
    shapes = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())
    label = shapes.AddShape(solid, False)
    TDataStd_Name.Set_s(label, TCollection_ExtendedString(stem))
    XCAFDoc_DocumentTool.ColorTool_s(doc.Main()).SetColor(
        label, Quantity_Color(*GRAPHITE, Quantity_TOC_RGB), XCAFDoc_ColorGen)
    Interface_Static.SetCVal_s("write.step.schema", "AP214IS")
    writer = STEPCAFControl_Writer()
    writer.SetColorMode(True)
    writer.SetNameMode(True)
    writer.Transfer(doc, STEPControl_AsIs)
    path = _models.ROOT / f".{stem}.tmp"
    writer.Write(str(path))
    text = path.read_text()
    path.unlink()
    text = re.sub(r"FILE_NAME\(.*?\);", "FILE_NAME('" + stem + ".step',"
                  "'2000-01-01T00:00:00',(''),(''),'rcbench "
                  "_gen_dial_base.py','OpenCASCADE','');", text, count=1,
                  flags=re.S)
    return text


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


def _extent(shape):
    """A shape's box as (x0, y0, z0, x1, y1, z1)."""
    box = Bnd_Box()
    BRepBndLib.AddOptimal_s(shape, box, False, False)
    lo, hi = box.CornerMin(), box.CornerMax()
    return (lo.X(), lo.Y(), lo.Z(), hi.X(), hi.Y(), hi.Z())


def _moved(shape, x=0.0, y=0.0, z=0.0, angle=0.0):
    move = gp_Trsf()
    move.SetTranslation(gp_Vec(x, y, z))
    return BRepBuilderAPI_Transform(_turned(shape, angle), move,
                                    True).Shape()


def neighbours():
    """The dial, its nut and the cell where they sit in the base.

    Returns:
        A dict of name to a list of solids. The cell's wires and plug
        are left out: they are drawn straight and bend in the base.
    """
    import step_check
    read = step_check.read
    cell = [s for s, _c in read(_models.path("lipo_102540_1000mah"))[1]]
    return {
        "dial": [s for s, _c in read(_models.path("m5dial"))[1]],
        "nut": [_moved(s, z=-PLATE)
                for s, _c in read(_models.path("m5dial_nut"))[1]],
        "cell": [_moved(s, x=-CELL[0] / 2, z=FLOOR, angle=-90.0)
                 for s in cell[:-3]],
    }


def check(built):
    """Check the parts and their fit round dial, nut and cell.

    Args:
        built: The parts as ``parts`` gives them.

    Returns:
        A list of error texts, empty when all is well.
    """
    errors = []
    for stem, solid in built.items():
        if not BRepCheck_Analyzer(solid).IsValid():
            errors.append(f"{stem}: not a valid solid")
        count = pieces(solid)
        if count != 1:
            errors.append(f"{stem}: {count} separate pieces")
    for stem, solid in built.items():
        filled = sum(volume(BRepAlgoAPI_Common(solid, flute).Shape())
                     for flute in flutes())
        if filled > 1e-3:
            errors.append(f"{stem}: {filled:.2f}mm3 of it lie in the "
                          "flutes")
    top, bottom = built["dial_base_top"], built["dial_base_bottom"]
    shared = volume(BRepAlgoAPI_Common(top, bottom).Shape())
    if shared > 1e-6:
        errors.append(f"the two parts share {shared:.3f}mm3")
    boxes = {stem: _extent(part) for stem, part in built.items()}
    for name, solids in neighbours().items():
        for i, solid in enumerate(solids):
            box = _extent(solid)
            for stem, part in built.items():
                other = boxes[stem]
                if not all(box[k] < other[k + 3] and other[k] < box[k + 3]
                           for k in range(3)):
                    continue
                shared = volume(BRepAlgoAPI_Common(part, solid).Shape())
                if shared > 1e-3:
                    errors.append(f"{stem} and {name} solid {i} share "
                                  f"{shared:.3f}mm3")
    return errors


def render(built, directory):
    """Draw the base with the dial, its nut and the cell.

    Args:
        built: The parts as ``parts`` gives them.
        directory: Where the three PNGs go.
    """
    import step_check
    import step_render
    from OCP.AIS import AIS_Shape
    from OCP.Aspect import Aspect_TypeOfLine
    from OCP.Graphic3d import (Graphic3d_BufferType,
                               Graphic3d_MaterialAspect,
                               Graphic3d_NameOfMaterial)
    from OCP.Image import Image_AlienPixMap
    from OCP.Prs3d import Prs3d_LineAspect
    from OCP.TCollection import TCollection_AsciiString
    from OCP.V3d import V3d_TypeOfOrientation

    def colours(name):
        return [c or (0.5, 0.5, 0.5)
                for _s, c in step_check.read(_models.path(name))[1]]

    near = neighbours()
    dial = list(zip(near["dial"], colours("m5dial"), strict=True))
    dial += [(solid, (0.85, 0.85, 0.83)) for solid in near["nut"]]
    cell = list(zip(near["cell"], colours("lipo_102540_1000mah"),
                    strict=False))
    top = [(built["dial_base_top"], GRAPHITE)]
    bottom = [(built["dial_base_bottom"], GRAPHITE)]
    view = step_render.Renderer(1200, 1000)
    line = Prs3d_LineAspect(step_render._rgb(0.1, 0.1, 0.1),
                            Aspect_TypeOfLine.Aspect_TOL_SOLID, 1.0)

    def draw(items, towards, name, edges):
        view.ctx.RemoveAll(False)
        for solid, rgb in items:
            shown = AIS_Shape(solid)
            shown.SetColor(step_render._rgb(*rgb))
            shown.SetMaterial(Graphic3d_MaterialAspect(
                Graphic3d_NameOfMaterial.Graphic3d_NOM_PLASTIC))
            shown.Attributes().SetFaceBoundaryDraw(edges)
            shown.Attributes().SetFaceBoundaryAspect(line)
            view.ctx.Display(shown, 1, -1, False)
        view.view.SetProj(towards)
        view.view.FitAll(0.06, False)
        view.view.Redraw()
        image = Image_AlienPixMap()
        view.view.ToPixMap(image, view.w, view.h,
                           Graphic3d_BufferType.Graphic3d_BT_RGB)
        image.Save(TCollection_AsciiString(str(_models.render(
            directory, name.removesuffix(".png")))))

    def lifted(items, z):
        return [(_moved(solid, z=z), rgb) for solid, rgb in items]

    def cut(items):
        half = _box(-80, -80, -80, 80, 0, 80)
        return [(BRepAlgoAPI_Cut(solid, half).Shape(), rgb)
                for solid, rgb in items]

    side = V3d_TypeOfOrientation.V3d_XposYnegZpos
    draw(top + bottom + dial, side, "dial_base.png", False)
    draw(lifted(dial, 34.0) + top + lifted(cell, -22.0)
         + lifted(bottom, -30.0), side, "dial_base_exploded.png", True)
    draw(cut(top) + cut([(built["dial_base_bottom"], (0.35, 0.36, 0.40))])
         + dial + cell, V3d_TypeOfOrientation.V3d_Yneg,
         "dial_base_section.png", True)


def main(argv):
    """Write or check the files.

    Args:
        argv: Command-line arguments without the program name.

    Returns:
        0, or 1 for a stale file or a collision.
    """
    built = parts()
    if "--render" in argv:
        render(built, argv[argv.index("--render") + 1])
        return 0
    errors = check(built)
    for stem, solid in built.items():
        text = step_text(stem, solid)
        path = _models.path(stem)
        if "--check" in argv:
            if not path.exists() or path.read_text() != text:
                errors.append(f"STALE {path.name}")
            continue
        path.write_text(text)
        BRepMesh_IncrementalMesh(solid, 0.03, False, 0.2)
        stl = StlAPI_Writer()
        stl.ASCIIMode = False
        stl.Write(solid, str(_models.path(stem, ".stl")))
        print(f"wrote {stem}.step and .stl, {volume(solid) / 1000:.1f}cm3")
    for error in errors:
        print(error)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
