#!/usr/bin/env python3
"""Printed case of the remote INA228 sensor node, in 3 parts.

    ina228_node_plate   the bottom plate under the INA228 module
    ina228_node_frame   the wall round both boards with 4 columns
    ina228_node_bezel   the rim round the display with its window

The case holds the layout of ina228_node.py: the INA228 module with its
2 XT90PW connectors at the bottom, the Matek mBEC12S beside the plus
bar, the ES3C28P display board on top. It has no ears; the node hangs
in its cable. The bezel's top edge and the plate's bottom edge are
rounded 1.5 all round.

How it is held: 4 screws M3 x 8 go from below through the plate and the
INA228 board's 4 holes into threaded inserts in the frame's columns,
which stand on the board. 4 screws M3 x 10 go from above through the
bezel and the display board's 4 holes into inserts in the columns'
tops. Each board is clamped between two parts. The inserts are ruthex
RX-M3Sx4.0, 8 in all, melted into holes of 4.0 that are 6.0 deep.

Openings:
    XT90     one in each end wall, 21.4 x 10.5, for the mating half
    USB      a notch of 13.0 over the socket half's opening, through
             frame and bezel
    buttons  RESET and BOOT are pressed from the display board's back,
             towards the display. A slot of 26.6 x 2.6 under the USB
             notch lets a tool of 2mm at most reach under each
             plunger, 5.9 inside the wall's outer face.
    microSD  a notch of 16.0 x 2.8 in the long wall at the card's edge
    I2C      a pocket of 10.16 x 2.5 x 8.0 in the same long wall over
             the INA228 board's header, under the display board, every
             edge inside rounded 2.0, so the opening at the split is
             0.5 deep, and its edge in the wall's inner face rounded
             2.0 on 3 sides; the wall bulges out 1.3 behind it, its edges
             rounded 3.2 round the same centres, so 1.2 of wall stays
             everywhere; the bulge's top reaches 0.57 into the bezel
    speaker  4 slots of 1.6 x 7.0 in the other long wall beside the
             speaker socket; the room behind them is 18 x 14 x 10
    mic      a hole of 1.6 in the bezel over the board's microphone
             hole
    display  a window of 59.1 x 44.6 over the view area

Printing: the frame with its upper face on the bed, the bezel with its
front on the bed, the plate with its underside on the bed. No part
needs support: the columns and the notches start on the bed, the XT90
openings are bridges of 21.4.

Not drawn: the 8 screws and inserts, the wires, the speaker, and the
foam pad of 1.5 that carries the converter over the board. A USB plug
thicker than 7.0 meets the plug in the XT90 housing under it: the
socket's axis is 3.6 over that housing's top.

Shapes are boxes, upright cylinders and, for the wedges, prisms over
triangles with given corners that meet flat faces only. The I2C pocket
and its bulge add cylinders along x and y and balls, each meeting flat
faces or a round of the same radius tangentially. A cone, or a
slanted cut through a round face, meets its neighbours in curves
OpenCASCADE computes differently from one processor to the next, and
the STEP text is compared in CI.

Frame: the INA228 module's, as ina228_node.py. All sizes are mm.

Usage:
    python3 hardware/mechanical/_gen_node_case.py [--check]
    xvfb-run -a python3 hardware/mechanical/_gen_node_case.py --render DIR

Writes models/casing/ina228_node_{plate,frame,bezel}.step and, for printing,
the same 3 as .stl. --check fails on a stale file, on a part that is
not one piece, on two parts sharing volume and on a part sharing volume
with a solid of models/pcb/ina228_node_layout.step. --render draws the
case closed, taken apart and cut open to DIR/casing/, 1200 x 1000
pixels. Needs OCP (pip install cadquery-ocp==8.0.1.1.0).

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import re
import sys
from pathlib import Path

from OCP.BRepAlgoAPI import (BRepAlgoAPI_Common, BRepAlgoAPI_Cut,
                             BRepAlgoAPI_Fuse)
from OCP.BRepBuilderAPI import (BRepBuilderAPI_MakeFace,
                                BRepBuilderAPI_MakePolygon)
from OCP.BRepCheck import BRepCheck_Analyzer
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.BRepPrimAPI import (BRepPrimAPI_MakeCylinder, BRepPrimAPI_MakePrism,
                             BRepPrimAPI_MakeSphere, BRepPrimAPI_MakeTorus)
from OCP.gp import gp_Ax2, gp_Dir, gp_Pnt, gp_Vec
from OCP.StlAPI import StlAPI_Writer

import _gen_dial_base as base
import _models
from _gen_dial_base import _box, _cylinder, _extent, _merged, _moved

HERE = Path(__file__).resolve().parent
STEMS = ("ina228_node_plate", "ina228_node_frame", "ina228_node_bezel")
LAYOUT = "ina228_node_layout"

# The 2 boards in the layout's frame: x0, y0, x1, y1.
INA = (-56.16, -4.2, 23.94, 44.4)
DISPLAY = (-59.11, -4.9, 26.89, 45.1)
# Heights: the INA228 board's faces, the housings' top, the display
# board's back and front, the touch panel's front.
INA_Z = (0.0, 1.6)
HOUSING_TOP = 11.5
DISPLAY_Z = (16.7, 18.3)
TOUCH = 22.6
# The 4 holes of each board; a column joins the two of one corner.
INA_HOLES = ((-51.46, 0.7), (19.19, 0.7), (-51.46, 39.5), (19.19, 39.5))
DISPLAY_HOLES = ((-55.11, -0.9), (22.89, -0.9), (-55.11, 41.1),
                 (22.89, 41.1))

GAP = 0.3               # round the display board
# The parts seat on what the models draw on the boards round the
# holes: copper rings of 0.05 on the INA228 board, print of 0.02 on
# the display board.
FOIL, PRINT = 0.05, 0.02
# A column's lobe at the INA228 hole steps back under the display
# board, where chip parts of that board hang 0.9 down: z of the first
# step, how far from the hole's axis the lobe reaches there, the
# steps' size and number. The print carries each step.
STEPS = (15.3, 1.0, 0.65, 4)
# Room for the display board's microphone: x0, y0, x1, y1, z0.
MIC_ROOM = (-56.3, 34.0, -52.9, 36.25, 16.2)
WALL = 2.4
RADIUS = 3.5 + GAP      # the cavity's corners, round the display's
EDGE = 1.5              # the round on the bezel's top and the plate's
                        # underside, all round the outline
PLATE = (2.4, 1.6)      # thickness, its upper face under the board
STAND = 6.4             # the plate's stands under the board, diameter
TOP = 1.2               # the bezel's plate over the touch panel
OVER = 0.2              # between that plate and the touch panel
# Threaded insert, ruthex RX-M3Sx4.0: hole diameter, hole depth, wall
# round the hole.
INSERT = (4.0, 6.0, 1.6)
# Screw M3 with a pan head: clearance hole, the head's recess diameter
# and depth.
SCREW = (3.4, 6.0, 2.2)
# Openings. XT90: y0, y1, z0, z1. Tool slot: y0, y1, z1. USB: y0, y1, z1
# in the bezel. Card: x0, x1, z0. Grille: the slots' x, width, z0, z1.
# Window: x0, y0, x1, y1. Microphone: x, y, diameter.
XT90 = (9.05, 30.45, 1.3, 11.8)
TOOL = (6.8, 33.4, 14.4)
USB = (13.6, 26.6, 18.8)
CARD = (-32.15, -16.15, 13.9)
GRILLE = ((-7.8, -4.6, -1.4, 1.8), 1.6, 3.5, 10.5)
WINDOW = (-48.5, -2.2, 10.6, 42.4)
MIC = (-55.11, 35.1, 1.6)
# Room for the plug on the INA228 board's I2C header (pins at y 0, its
# plastic x -1.27 to 8.89): a pocket in the long wall at -y under the
# display board, x0, x1, depth into the wall, height under the split,
# the wall left outside it, the edges' radius. The wall bulges out
# behind the pocket by depth + skin - WALL.
I2C = (-1.27, 8.89, 2.5, 8.0, 1.2, 2.0)
# The joints hold the frame's wall, which the screws clamp at its 4
# columns only, from moving inwards.
# Plate to frame: a lip all round on the plate, inside the lower edge
# of the wall, whose inner half is cut away for it: the lip's
# thickness and height, the room beside it and over it.
LIP = (1.2, 1.5, 0.15, 0.1)
# Frame to bezel: the wall's upper inner edge is cut off at 45 degrees
# and a wedge under the bezel's rim lies in the cut: the wedge's size,
# the cut's size. They run along the straight walls between the
# columns: x0, x1 along the long walls, y0, y1 along the short ones.
# With the frame's upper face on the bed the cut prints without
# support; a step like the plate's would hang.
WEDGE = (1.2, 1.4)
WEDGE_X = (-46.0, 14.0)
WEDGE_Y = (5.5, 34.7)

X0, Y0 = DISPLAY[0] - GAP, DISPLAY[1] - GAP
X1, Y1 = DISPLAY[2] + GAP, DISPLAY[3] + GAP
Z_PLATE = -PLATE[1] - PLATE[0]          # the case's underside
Z_FLOOR = -PLATE[1]                     # the plate's upper face
Z_SPLIT = DISPLAY_Z[0]                  # frame to bezel
Z_TOP = TOUCH + OVER + TOP              # the case's front
BIG = 200.0


def _fuse(*shapes):
    out = shapes[0]
    for shape in shapes[1:]:
        out = BRepAlgoAPI_Fuse(out, shape).Shape()
    return out


def _cut(shape, *tools):
    for tool in tools:
        shape = BRepAlgoAPI_Cut(shape, tool).Shape()
    return shape


def rounded(x0, y0, x1, y1, radius, z0, z1):
    """An upright block with rounded corners."""
    r = radius
    return _fuse(_box(x0 + r, y0, z0, x1 - r, y1, z1),
                 _box(x0, y0 + r, z0, x1, y1 - r, z1),
                 *[_cylinder(x, y, r, z0, z1)
                   for x in (x0 + r, x1 - r) for y in (y0 + r, y1 - r)])


def rounded_box(x0, y0, z0, x1, y1, z1, radius):
    """A block with every edge rounded: boxes, rods and corner balls."""
    r = radius
    xs, ys, zs = (x0 + r, x1 - r), (y0 + r, y1 - r), (z0 + r, z1 - r)
    parts = [_box(x0 + r, y0 + r, z0, x1 - r, y1 - r, z1),
             _box(x0 + r, y0, z0 + r, x1 - r, y1, z1 - r),
             _box(x0, y0 + r, z0 + r, x1, y1 - r, z1 - r)]
    for along, a0, a1 in (((1, 0, 0), x0 + r, x1 - r),
                          ((0, 1, 0), y0 + r, y1 - r),
                          ((0, 0, 1), z0 + r, z1 - r)):
        axis = along.index(1)
        for u in (xs, ys, zs)[(axis + 1) % 3]:
            for v in (xs, ys, zs)[(axis + 2) % 3]:
                at = [0.0, 0.0, 0.0]
                at[axis], at[(axis + 1) % 3], at[(axis + 2) % 3] = a0, u, v
                parts.append(BRepPrimAPI_MakeCylinder(
                    gp_Ax2(gp_Pnt(*at), gp_Dir(*along)), r, a1 - a0).Shape())
    parts += [BRepPrimAPI_MakeSphere(gp_Pnt(x, y, z), r).Shape()
              for x in xs for y in ys for z in zs]
    return _fuse(*parts)


def outer(z0, z1):
    """The case's outline between two heights."""
    return rounded(X0 - WALL, Y0 - WALL, X1 + WALL, Y1 + WALL,
                   RADIUS + WALL, z0, z1)


def outer_rounded(z0, z1, edge, at):
    """The outline between two heights, its edge at z0 or z1 rounded.

    The round's centre line is the outline grown in by the radius, the
    radius from the face: rods along the straight sides, a torus at each
    corner, kept to its corner's quarter.
    """
    x0, y0 = X0 - WALL, Y0 - WALL
    x1, y1 = X1 + WALL, Y1 + WALL
    big, r = RADIUS + WALL, edge
    zc = z1 - r if at == "top" else z0 + r
    body = (rounded(x0, y0, x1, y1, big, z0, zc) if at == "top"
            else rounded(x0, y0, x1, y1, big, zc, z1))
    parts = [body, rounded(x0 + r, y0 + r, x1 - r, y1 - r, big - r, z0, z1)]
    for y in (y0 + r, y1 - r):
        parts.append(BRepPrimAPI_MakeCylinder(
            gp_Ax2(gp_Pnt(x0 + big, y, zc), gp_Dir(1, 0, 0)), r,
            x1 - x0 - 2 * big).Shape())
    for x in (x0 + r, x1 - r):
        parts.append(BRepPrimAPI_MakeCylinder(
            gp_Ax2(gp_Pnt(x, y0 + big, zc), gp_Dir(0, 1, 0)), r,
            y1 - y0 - 2 * big).Shape())
    for x, sx in ((x0 + big, -1), (x1 - big, 1)):
        for y, sy in ((y0 + big, -1), (y1 - big, 1)):
            ring = BRepPrimAPI_MakeTorus(
                gp_Ax2(gp_Pnt(x, y, zc), gp_Dir(0, 0, 1)), big - r,
                r).Shape()
            far = x + sx * 2 * big, y + sy * 2 * big
            quarter = _box(min(x, far[0]), min(y, far[1]), zc - r,
                           max(x, far[0]), max(y, far[1]), zc + r)
            parts.append(BRepAlgoAPI_Common(ring, quarter).Shape())
    # A part thinner than 2 radii: the rods stand out of the far face.
    return BRepAlgoAPI_Common(_fuse(*parts),
                              _box(x0, y0, z0, x1, y1, z1)).Shape()


def cavity(z0, z1):
    """The room inside the wall between two heights."""
    return rounded(X0, Y0, X1, Y1, RADIUS, z0, z1)


def _corner(hole, radius, z0, z1):
    """A round post at a display hole, filled out to both walls."""
    x, y = hole
    wx = X0 if x < (X0 + X1) / 2 else X1
    wy = Y0 if y < (Y0 + Y1) / 2 else Y1
    block = _fuse(_cylinder(x, y, radius, z0, z1),
                  _box(min(x, wx), min(y, wy), z0, max(x, wx), max(y, wy),
                       z1),
                  _box(min(x, wx), y - radius, z0, max(x, wx), y + radius,
                       z1),
                  _box(x - radius, min(y, wy), z0, x + radius, max(y, wy),
                       z1))
    return BRepAlgoAPI_Common(block, cavity(z0, z1)).Shape()


def column(k):
    """A column of the frame: on the INA228 board, under the display's.

    Its foot holds the insert of the screw from below, at the INA228
    board's hole; its top holds the insert of the screw from above, at
    the display board's hole. The two holes lie 4.2 apart.
    """
    hole, deep, wall = INSERT
    radius = hole / 2 + wall
    (ax, ay), (bx, by) = INA_HOLES[k], DISPLAY_HOLES[k]
    z0, z1 = INA_Z[1] + FOIL, Z_SPLIT - PRINT
    lobe = _fuse(_cylinder(ax, ay, radius, z0, z1),
                 _box(min(ax, bx), ay - radius, z0, max(ax, bx),
                      ay + radius, z1))
    top, reach, step, count = STEPS
    away = 1.0 if ax > bx else -1.0
    for i in range(count):
        edge = ax + away * (reach + step * i)
        lobe = _cut(lobe, _box(min(edge, edge + away * BIG), -BIG,
                               top - step * i, max(edge, edge + away * BIG),
                               BIG, BIG))
    body = _cut(_fuse(_corner((bx, by), radius, z0, z1), lobe),
                _box(*MIC_ROOM[:2], MIC_ROOM[4], *MIC_ROOM[2:4], BIG))
    return body, (_cylinder(ax, ay, hole / 2, z0 - 1, z0 + deep),
                  _cylinder(bx, by, hole / 2, z1 - deep, z1 + 1))


def _prism(points, vector):
    """A prism over a polygon of (x, y, z) corners, pushed by a vector."""
    wire = BRepBuilderAPI_MakePolygon()
    for point in points:
        wire.Add(gp_Pnt(*point))
    wire.Close()
    face = BRepBuilderAPI_MakeFace(wire.Wire()).Face()
    return BRepPrimAPI_MakePrism(face, gp_Vec(*vector)).Shape()


def ring(grow, z0, z1):
    """The inner part of the wall: ``grow`` wide from the cavity out."""
    return _cut(rounded(X0 - grow, Y0 - grow, X1 + grow, Y1 + grow,
                        RADIUS + grow, z0, z1), cavity(z0 - 1, z1 + 1))


def wedges(size):
    """Prisms along the wall's upper inner edge, 45 degrees, at the split.

    Args:
        size: The length of the 2 short sides of each prism's section.

    Returns:
        One prism for each of the 4 walls, between the columns.
    """
    z = Z_SPLIT
    out = []
    for y, away in ((Y0, -1.0), (Y1, 1.0)):
        out.append(_prism([(WEDGE_X[0], y, z), (WEDGE_X[0], y + away * size,
                                                z),
                           (WEDGE_X[0], y, z - size)],
                          (WEDGE_X[1] - WEDGE_X[0], 0.0, 0.0)))
    for x, away in ((X0, -1.0), (X1, 1.0)):
        out.append(_prism([(x, WEDGE_Y[0], z), (x + away * size, WEDGE_Y[0],
                                                z),
                           (x, WEDGE_Y[0], z - size)],
                          (0.0, WEDGE_Y[1] - WEDGE_Y[0], 0.0)))
    return out


def plate():
    """The bottom plate with its lip, 4 stands and the screws' recesses."""
    clear, head, sunk = SCREW
    solid = _fuse(outer_rounded(Z_PLATE, Z_FLOOR, EDGE, "bottom"),
                  ring(LIP[0], Z_FLOOR - 0.1, Z_FLOOR + LIP[1]))
    for x, y in INA_HOLES:
        solid = _fuse(solid, _cylinder(x, y, STAND / 2, Z_PLATE,
                                       INA_Z[0] - FOIL))
    for x, y in INA_HOLES:
        solid = _cut(solid, _cylinder(x, y, clear / 2, Z_PLATE - 1, 1.0),
                     _cylinder(x, y, head / 2, Z_PLATE - 1, Z_PLATE + sunk))
    return solid


def i2c_room():
    """The pocket for the I2C plug and the wall's bulge behind it."""
    x0, x1, depth, high, skin, r = I2C
    pocket = rounded_box(x0, Y0 - depth, Z_SPLIT - high, x1, Y0 + 2 * r,
                         Z_SPLIT, r)
    # The same box grown by the skin, kept to the wall's outer part.
    grown = rounded_box(x0 - skin, Y0 - depth - skin, Z_SPLIT - high - skin,
                        x1 + skin, Y0 + 2 * r, Z_SPLIT + skin, r + skin)
    bulge = _cut(grown, _box(-BIG, Y0 - WALL + 1, -BIG, BIG, BIG, BIG))
    return _fuse(pocket, mouth()), bulge


def _across(x0, z0, x1, z1, radius, y0, y1):
    """A block along y, its 4 edges along y rounded."""
    r = radius
    return _fuse(_box(x0 + r, y0, z0, x1 - r, y1, z1),
                 _box(x0, y0, z0 + r, x1, y1, z1 - r),
                 *[BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x, y0, z),
                                                   gp_Dir(0, 1, 0)),
                                            r, y1 - y0).Shape()
                   for x in (x0 + r, x1 - r) for z in (z0 + r, z1 - r)])


def mouth():
    """The rounding of the pocket's edge in the wall's inner face.

    The edge runs round the pocket's outline there, a rectangle with
    corners of the pocket's radius; the round is a quarter circle of the
    same radius, its centre line that outline grown by the radius, the
    radius into the wall: straight rods along x and z, a torus at each
    corner.
    """
    x0, x1, _, high, _, r = I2C
    z0, z1 = Z_SPLIT - high, Z_SPLIT
    y = Y0 - r
    grown = r + r
    slab = _across(x0 - r, z0 - r, x1 + r, z1 + r, grown, y, Y0 + 1)
    inside = _across(x0, z0, x1, z1, r, y - 1, Y0 + 2)
    tube = []
    for a, b in ((x0 + r, x1 - r),):
        for z in (z0 - r, z1 + r):
            tube.append(BRepPrimAPI_MakeCylinder(
                gp_Ax2(gp_Pnt(a, y, z), gp_Dir(1, 0, 0)), r, b - a).Shape())
    for x in (x0 - r, x1 + r):
        tube.append(BRepPrimAPI_MakeCylinder(
            gp_Ax2(gp_Pnt(x, y, z0 + r), gp_Dir(0, 0, 1)), r,
            z1 - z0 - 2 * r).Shape())
    # Each torus kept to its corner's quarter, away from the rods.
    for x, sx in ((x0 + r, -1), (x1 - r, 1)):
        for z, sz in ((z0 + r, -1), (z1 - r, 1)):
            ring = BRepPrimAPI_MakeTorus(
                gp_Ax2(gp_Pnt(x, y, z), gp_Dir(0, 1, 0)), grown, r).Shape()
            far = x + sx * 2 * grown, z + sz * 2 * grown
            quarter = _box(min(x, far[0]), y - grown, min(z, far[1]),
                           max(x, far[0]), y + grown, max(z, far[1]))
            tube.append(BRepAlgoAPI_Common(ring, quarter).Shape())
    return _cut(slab, inside, *tube)


def i2c_span():
    """The wall's stretch beside the I2C pocket, bulge included."""
    x0, x1, _, _, skin, _ = I2C
    return _box(x0 - skin, Y0 - WALL - 2, Z_SPLIT - 3, x1 + skin, Y0 + 2,
                Z_SPLIT + 1)


def split(shape):
    """The parts of a shape under and over the split."""
    over = _box(-BIG, -BIG, Z_SPLIT, BIG, BIG, BIG)
    return _cut(shape, over), BRepAlgoAPI_Common(shape, over).Shape()


def frame():
    """The wall from the plate to the display board, with the columns."""
    pocket, bulge = i2c_room()
    solid = _cut(_fuse(outer(Z_FLOOR, Z_SPLIT), split(bulge)[0]),
                 cavity(Z_FLOOR - 1, Z_SPLIT + 1))
    holes = []
    for k in range(4):
        body, bores = column(k)
        solid = _fuse(solid, body)
        holes += bores
    y0, y1, z0, z1 = XT90
    for x in (X0 - WALL - 1, X1 - 1):
        holes.append(_box(x, y0, z0, x + WALL + 2, y1, z1))
    end = X1 - 1, X1 + WALL + 1
    holes.append(_box(end[0], TOOL[0], z1 - 0.1, end[1], TOOL[1], TOOL[2]))
    holes.append(_box(end[0], USB[0], TOOL[2] - 0.1, end[1], USB[1],
                      Z_SPLIT + 1))
    holes.append(_box(CARD[0], Y0 - WALL - 1, CARD[2], CARD[1], Y0 + 1,
                      Z_SPLIT + 1))
    xs, wide, z0, z1 = GRILLE
    for x in xs:
        holes.append(_box(x - wide / 2, Y1 - 1, z0, x + wide / 2,
                          Y1 + WALL + 1, z1))
    holes.append(ring(LIP[0] + LIP[2], Z_FLOOR - 1,
                      Z_FLOOR + LIP[1] + LIP[3]))
    # No groove for the bezel's wedge over the pocket's ceiling.
    grooves = [_cut(w, i2c_span()) for w in wedges(WEDGE[1])]
    return _cut(solid, *holes, *grooves, pocket)


def bezel():
    """The rim round the display board, its plate and its 4 posts."""
    clear, head, sunk = SCREW
    under = TOUCH + OVER
    solid = _cut(outer_rounded(Z_SPLIT, Z_TOP, EDGE, "top"),
                 cavity(Z_SPLIT - 1, under))
    solid = _fuse(solid, *wedges(WEDGE[0]), split(i2c_room()[1])[1])
    radius = INSERT[0] / 2 + INSERT[2]
    for hole in DISPLAY_HOLES:
        solid = _fuse(solid, _corner(hole, radius,
                                       DISPLAY_Z[1] + PRINT, under))
    holes = [_box(WINDOW[0], WINDOW[1], under - 1, WINDOW[2], WINDOW[3],
                  Z_TOP + 1),
             _cylinder(MIC[0], MIC[1], MIC[2] / 2, under - 1, Z_TOP + 1),
             _box(X1 - 1, USB[0], Z_SPLIT - 2, X1 + WALL + 1, USB[1],
                  USB[2]),
             _box(CARD[0], Y0 - WALL - 1, Z_SPLIT - 2, CARD[1], Y0 + 1,
                  Z_SPLIT),
             # The wedge stays out of the I2C plug's pocket.
             _cut(i2c_span(), _box(-BIG, -BIG, Z_SPLIT, BIG, BIG, BIG))]
    for x, y in DISPLAY_HOLES:
        holes.append(_cylinder(x, y, clear / 2, DISPLAY_Z[1] - 1,
                               Z_TOP + 1))
        holes.append(_cylinder(x, y, head / 2, Z_TOP - sunk, Z_TOP + 1))
    return _cut(solid, *holes)


def parts():
    """Build the 3 parts.

    Returns:
        A dict of file stem to solid.
    """
    return dict(zip(STEMS, (_merged(plate()), _merged(frame()),
                            _merged(bezel())), strict=True))


def step_text(stem, solid):
    """The STEP text of one part, with its colour and a fixed header."""
    return re.sub(r"_gen_dial_base\.py", "_gen_node_case.py",
                  base.step_text(stem, solid), count=1)


def neighbours():
    """The solids of the layout and their colours."""
    import step_check
    return step_check.read(_models.path(LAYOUT))[1]


def check(built):
    """Check the parts and their fit round the boards.

    Args:
        built: The parts as ``parts`` gives them.

    Returns:
        A list of error texts, empty when all is well.
    """
    errors = []
    for stem, solid in built.items():
        if not BRepCheck_Analyzer(solid).IsValid():
            errors.append(f"{stem}: not a valid solid")
        if base.pieces(solid) != 1:
            errors.append(f"{stem}: {base.pieces(solid)} separate pieces")
    stems = list(built)
    for i, a in enumerate(stems):
        for b in stems[i + 1:]:
            shared = base.volume(
                BRepAlgoAPI_Common(built[a], built[b]).Shape())
            if shared > 1e-6:
                errors.append(f"{a} and {b} share {shared:.3f}mm3")
    boxes = {stem: _extent(part) for stem, part in built.items()}
    for i, (solid, _rgb) in enumerate(neighbours()):
        box = _extent(solid)
        for stem, part in built.items():
            other = boxes[stem]
            if not all(box[k] < other[k + 3] and other[k] < box[k + 3]
                       for k in range(3)):
                continue
            shared = base.volume(BRepAlgoAPI_Common(part, solid).Shape())
            if shared > 1e-3:
                errors.append(f"{stem} and {LAYOUT} solid {i} share "
                              f"{shared:.3f}mm3, box "
                              + ", ".join(f"{v:.2f}" for v in box))
    return errors


def render(built, directory):
    """Draw the case closed, taken apart and cut open.

    Args:
        built: The parts as ``parts`` gives them.
        directory: Where the three PNGs go.
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
    from OCP.V3d import V3d_TypeOfOrientation

    near = [(solid, rgb or (0.5, 0.5, 0.5)) for solid, rgb in neighbours()]
    low = [(s, c) for s, c in near if _extent(s)[5] < HOUSING_TOP + 0.3]
    high = [(s, c) for s, c in near if _extent(s)[5] >= HOUSING_TOP + 0.3]
    plate_, frame_, bezel_ = ([(built[stem], base.GRAPHITE)]
                              for stem in STEMS)
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
        half = _box(-BIG, -BIG, -BIG, BIG, (Y0 + Y1) / 2, BIG)
        return [(BRepAlgoAPI_Cut(solid, half).Shape(), rgb)
                for solid, rgb in items]

    side = V3d_TypeOfOrientation.V3d_XposYnegZpos
    draw(plate_ + frame_ + bezel_ + near, side, "ina228_node_case.png",
         False)
    draw(lifted(bezel_, 75.0) + lifted(high, 55.0) + lifted(frame_, 28.0)
         + low + lifted(plate_, -14.0), side,
         "ina228_node_case_exploded.png", True)
    draw(cut(plate_ + bezel_) + cut([(built[STEMS[1]], (0.35, 0.36, 0.40))])
         + near, V3d_TypeOfOrientation.V3d_Yneg,
         "ina228_node_case_section.png", True)
    draw(plate_ + frame_ + bezel_ + near,
         V3d_TypeOfOrientation.V3d_XnegYposZpos,
         "ina228_node_case_back.png", False)

    def across(items):
        half = _box(-16.0, -BIG, -BIG, BIG, BIG, BIG)
        return [(BRepAlgoAPI_Cut(solid, half).Shape(), rgb)
                for solid, rgb in items]

    draw(across(plate_ + bezel_)
         + across([(built[STEMS[1]], (0.35, 0.36, 0.40))]) + near,
         V3d_TypeOfOrientation.V3d_Xpos, "ina228_node_case_joints.png",
         True)


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
        print(f"wrote {stem}.step and .stl, "
              f"{base.volume(solid) / 1000:.1f}cm3")
    for error in errors:
        print(error)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
