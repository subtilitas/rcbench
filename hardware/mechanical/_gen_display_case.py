#!/usr/bin/env python3
"""Printed case of the display node, in 2 parts and 2 variants.

    display_node_box        floor, wall, 4 bosses with threaded inserts,
                            one XT30 opening in each short wall
    display_node_lid        front plate with the display window and a
                            plug that reaches into the box down to the
                            board
    display_node_xt60_box   the same with a seat for a panel-mount XT60
                            with 2 screws in each short wall
    display_node_xt60_lid   its lid

The case holds the display node of docs/Nodes.md: the LCDWIKI ES3C28P
display board on the 4 bosses, its display in the lid's window. Under
the board, on the floor, lie a current sensor board (MATEK I2C-INA-BM)
and a 5V buck converter; a bay of 11.4 beyond the board's end at the
USB socket holds an inline pair of XT30. A panel-mount XT30 sits in
each short wall.

Source: a tester's printed case of the same node, measured from its STL
file (2 bodies, 44890 triangles), which is not in the repository.
Outline, heights, the bosses' places, the window, the recesses and the
XT30 openings are that case's, within 0.1. 3 things differ, each to
close the gap of about 1 the tester reports between lid and box:

1. Insert holes. The measured case has straight holes of 3.5 through
   bosses and floor. Here each boss has a blind hole of 4.0, 6.0 deep,
   for a ruthex RX-M3Sx4.0 insert, and a relief of 4.6 x 0.5 at its
   mouth that takes the plastic the insert pushes up. An insert or a
   burr that stands 1.0 out of a boss lifts the board and with it the
   lid by 0.8.
2. Window. The measured window is 50.5 wide round a touch panel of
   50.0 +- 0.2 and lies 0.08 off the board's axis: 0.17 of room on one
   side. The panel enters the window by 3.9; where it binds, the lid
   stops short. Here the window is 51.0 wide and centred on the board:
   0.5 on each side.
3. Screws. The measured lid has recesses of 4.5 for the heads. Here
   they are 6.0 wide and 2.2 deep, for M3 pan heads of 5.6: the board's
   holes are 3.2, so M3 fits.

In nominal sizes the measured case closes: its plug ends 0.18 over the
board. Which of 1 and 2 held the tester's lid up is not known; the
inserts and screws the tester used are not known either.

How it closes: the lid's flange lies on the box's rim. The plug ends
0.18 over the board's front, so the board lies loose by that much
between bosses and plug, and the rim carries the screws' force. 4
screws M3 x 10 go through the lid and the board's holes into the
inserts.

The XT60 variant. Its connector is the panel-mount XT60 with a flange
and 2 screws (XT60E): flange 33.2 x 15.8 x 2.5, body 18.8 x 11.7
reaching 15.5 behind the flange's front, contacts to 19.8, screw holes
25.6 apart. Its seat is the one of the bench's front case
(rcbench_hardware, cad/case_front), measured from that part's mesh:

- the short wall is 3.0 thick;
- from outside, a recess 2.0 deep in the flange's outline with 0.75 of
  room: 34.7 x 13.3 at the ears, 20.7 x 17.3 in the middle, slanted
  between; the flange stands 0.5 out of the wall;
- through the 1.0 that is left, an opening of 19.6 x 12.4 for the body,
  2 of its corners rounded with 3.0;
- 2 holes of 4.0, 25.6 apart, through the wall into bosses of 8.0 that
  stand 4.0 behind it: 5.0 deep, for a ruthex RX-M3Sx4.0 insert melted
  in from outside. 2 screws M3 x 6 hold the flange.

The front case's notch of 10.2 x 2.5 under the recess is not drawn; what
it is for is not stated. The flange is 15.8 high and the body 11.7, so
this variant is 24.4 high in place of 17.8: the connectors' bodies lie
under the display board, whose bosses are 16.6 high. It is 105.6 long
in place of 101.2: 1.2 for each thicker short wall, 2.0 more at the USB
socket's end, where the body ends 1.4 before the board.

Printing: the box with its underside on the bed, the lid with its front
on the bed. The XT30 openings are 11.2 wide with a flat top. In the XT60
variant the body's opening is a bridge of 19.6 and the 2 bosses behind
each short wall hang 4.0 out of it: they need support or a slow bridge.

Not drawn: screws, inserts, the 2 boards on the floor, the XT30
connectors, wires. Not measured: the height of the parts under the
board; 3.6 is free under the board's speaker socket and 8.3 elsewhere.
The hole of 3.5 through the floor at FLOOR_HOLE is the measured case's;
what it is for is not stated.

Shapes are boxes, cylinders along an axis of the frame and prisms over
polygons with given corners: no cone and no turned shape, so the STEP
text is the same on every processor.

Frame: origin at the XT30 box's center on its underside, x across, y along
the long side with the board's USB socket towards -y, z up. All sizes
are mm.

Usage:
    python3 hardware/mechanical/_gen_display_case.py [--check]
    xvfb-run -a python3 hardware/mechanical/_gen_display_case.py \
        --render DIR

Writes models/casing/display_node_{box,lid}.step, the XT60 variant's
display_node_xt60_{box,lid}.step, and the same 4 as .stl. --check
fails on a stale file, on a part that is not one valid piece, on the
2 parts of a variant sharing volume, on a part or a connector sharing
volume with a solid of the display board's model, on a connector
sharing volume with its box, and on a stack that does not close.
--render draws each case closed, taken apart and cut open to
DIR/casing/, 1200 x 1000 pixels. Needs OCP (pip install
cadquery-ocp==8.0.1.1.0).

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import re
import sys

from OCP.BRepAlgoAPI import BRepAlgoAPI_Common, BRepAlgoAPI_Cut
from OCP.BRepBuilderAPI import (BRepBuilderAPI_MakeFace,
                                BRepBuilderAPI_MakePolygon)
from OCP.BRepCheck import BRepCheck_Analyzer
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.BRepPrimAPI import BRepPrimAPI_MakeCylinder, BRepPrimAPI_MakePrism
from OCP.gp import gp_Ax2, gp_Dir, gp_Pnt, gp_Vec
from OCP.StlAPI import StlAPI_Writer

import _gen_dial_base as base
import _models
import es3c28p
from _gen_dial_base import _box, _cylinder, _extent, _merged, _moved
from _gen_node_case import _cut, _fuse, rounded

BOARD_MODEL = "es3c28p"

# Width, the long walls, the corners' outer radius, the floor.
WIDTH = 59.8
WALL, RADIUS, FLOOR = 1.8, 3.2, 1.7
X1 = WIDTH / 2
X0 = -X1
PLUG = (4.2, 0.4)       # the plug's depth into the box, its room all round
# The board lies on the print its model draws on its back, 0.02 thick.
PRINT = 0.02
# The display board's center, and its size.
BOARD_AT = (1.0, 5.6)
BOARD = (50.0, 86.0, 1.6)
BOSS = 8.0
# Threaded insert, ruthex RX-M3Sx4.0: hole diameter, hole depth, and
# the relief at the hole's mouth, diameter and depth.
INSERT = (4.0, 6.0, 4.6, 0.5)
# Screw M3 with a pan head: clearance hole, the head's recess diameter
# and depth.
SCREW = (3.4, 6.0, 2.2)
# Window: room at each long side of the touch panel, at its 2 ends.
WINDOW = (0.5, 0.7)
# Recess in the plug over the flat cables' folds and the bay: x0, x1,
# its end's distance from the short wall's inner face at -y, the lid's
# thickness left over it.
RECESS = (-17.5, 19.5, 3.2, 2.6)
# Recess in the inner face of the wall at +x, for the plugs of the
# board's side sockets: y0, y1, its lower and upper edge under and over
# the board's back, depth.
SIDE_RECESS = (-8.35, 23.15, -5.8, 2.1, 1.0)
FLOOR_HOLE = (5.6, -37.5, 3.5)

# XT30 opening: width, height, its lower edge over the underside; x of
# its straight end in the wall at +y and in the wall at -y. The other
# end is a half circle.
XT30 = (11.2, 5.6, 1.9)
XT30_AT = (-4.0, -5.9)
# Recess in the inner face of the XT30 box's wall at -y: x0, x1, z0, z1,
# depth.
END_RECESS = (-9.6, 5.4, 7.0, 12.05, 1.0)

# Panel-mount XT60 with 2 screws: the flange (width, height, thickness),
# the body (width, height, its end behind the flange's front), the
# contacts' end behind the flange's front, their radius and distance
# from the axis, the screws' distance.
XT60 = ((33.2, 15.8, 2.5), (18.8, 11.7, 15.5), 19.8, 2.14, 3.6, 25.6)
# Its seat as in the front case: the wall, the recess's depth, the
# room round the flange, the half width of the flange's ears and of its
# full height, the ears' height, the body's opening (width, height,
# corner radius), the bosses behind the wall (diameter, length), the
# insert's hole.
SEAT = (3.0, 2.0, 0.75, 16.6, 9.6, 11.8, (19.6, 12.4, 3.0), (8.0, 4.0),
        4.0)
BIG = 200.0


class Case:
    """One variant of the case: its sizes along y and z."""

    def __init__(self, stems, y0, y1, end, boss_top, rim, connector):
        """Set the variant's sizes.

        Args:
            stems: The file stems of box and lid.
            y0: The outer face of the short wall at -y.
            y1: The outer face of the short wall at +y.
            end: The short walls' thickness.
            boss_top: The bosses' tops over the underside.
            rim: The box's rim over the underside.
            connector: "xt30", or the XT60's axis over the underside.
        """
        self.stems = stems
        self.y0, self.y1, self.end = y0, y1, end
        self.boss_top, self.rim = boss_top, rim
        self.connector = connector
        self.front = rim + 1.8
        self.board_back = boss_top + PRINT
        self.board_front = self.board_back + BOARD[2]
        self.plug_end = rim - PLUG[0]


XT30_CASE = Case(("display_node_box", "display_node_lid"), -50.6, 50.6,
                 WALL, 10.0, 16.0, "xt30")
XT60_CASE = Case(("display_node_xt60_box", "display_node_xt60_lid"), -53.8,
                 51.8, SEAT[0], 16.6, 22.6, 9.8)
CASES = (XT30_CASE, XT60_CASE)
STEMS = tuple(stem for case in CASES for stem in case.stems)


def holes():
    """The 4 screw axes: the display board's holes."""
    dx, dy = es3c28p.HOLES[0][0] - es3c28p.HOLES[1][0], (
        es3c28p.HOLES[2][1] - es3c28p.HOLES[0][1])
    return [(BOARD_AT[0] + kx * dx / 2, BOARD_AT[1] + ky * dy / 2)
            for kx in (-1, 1) for ky in (-1, 1)]


def at_board(x, y):
    """A place of the display board's spec, es3c28p.py, in this frame."""
    b = es3c28p.BOARD
    return (x - (b[0] + b[2]) / 2 + BOARD_AT[0],
            y - (b[1] + b[3]) / 2 + BOARD_AT[1])


def window():
    """The window round the touch panel: x0, y0, x1, y1."""
    x0, y0 = at_board(*es3c28p.LCD[:2])
    x1, y1 = at_board(*es3c28p.LCD[2:])
    return (x0 - WINDOW[0], y0 - WINDOW[1], x1 + WINDOW[0], y1 + WINDOW[1])


def stack(case):
    """How a case closes, in heights over the underside.

    Args:
        case: The variant.

    Returns:
        A dict: the play between the plug's end and the board's front,
        the room at each long side of the touch panel in the window,
        the touch panel's front under the lid's front, the room over
        the flat cables' folds, the room under the board's highest
        part on its back, and how far a screw M3 x 10 goes into the
        boss.
    """
    lcd = es3c28p.LCD
    panel = at_board(lcd[0], lcd[1])[0], at_board(lcd[2], lcd[3])[0]
    wx0, _wy0, wx1, _wy1 = window()
    touch = case.board_front + es3c28p.TOUCH_Z[1]
    fold = case.board_front + es3c28p.LCD_Z[1] + es3c28p.FLEX
    under_head = case.front - SCREW[2]
    return {
        "play": case.plug_end - case.board_front,
        "window": min(panel[0] - wx0, wx1 - panel[1]),
        "touch": case.front - touch,
        "fold": case.front - RECESS[3] - fold,
        "under": case.board_back - 4.7 - FLOOR,
        "thread": 10.0 - (under_head - case.boss_top),
    }


def _along_y(x, z, radius, y0, y1):
    return BRepPrimAPI_MakeCylinder(
        gp_Ax2(gp_Pnt(x, y0, z), gp_Dir(0, 1, 0)), radius, y1 - y0).Shape()


def _prism_y(points, y0, y1):
    """A prism along y over a polygon of (x, z) corners."""
    wire = BRepBuilderAPI_MakePolygon()
    for x, z in points:
        wire.Add(gp_Pnt(x, y0, z))
    wire.Close()
    face = BRepBuilderAPI_MakeFace(wire.Wire()).Face()
    return BRepPrimAPI_MakePrism(face, gp_Vec(0.0, y1 - y0, 0.0)).Shape()


def xt30(straight, y0, y1):
    """The cut of one XT30 opening through a short wall."""
    wide, high, low = XT30
    radius = high / 2
    return _fuse(_box(straight - wide + radius, y0, low, straight, y1,
                      low + high),
                 _along_y(straight - wide + radius, low + radius, radius,
                          y0, y1))


def _keyed(x, z, wide, high, radius, side, y0, y1):
    """The XT60 body's outline along y: 2 corners rounded at one end.

    The rounded corners lie at +x for side 1, at -x for side -1.
    """
    round_at = side * (wide / 2 - radius)
    return _fuse(
        _box(x - wide / 2 + (radius if side < 0 else 0.0), y0,
             z - high / 2, x + wide / 2 - (radius if side > 0 else 0.0),
             y1, z + high / 2),
        _box(x + min(round_at, side * wide / 2), y0, z - high / 2 + radius,
             x + max(round_at, side * wide / 2), y1, z + high / 2 - radius),
        *[_along_y(x + round_at, z + k * (high / 2 - radius), radius, y0,
                   y1) for k in (-1, 1)])


def xt60_seat(case, side):
    """One XT60 seat: what it adds behind the wall, what it cuts.

    Args:
        case: The variant; its connector is the axis's height.
        side: 1 for the wall at +y, -1 for the wall at -y. The 2 seats
            are one turned by half a turn about z, so the body's
            rounded corners lie at +x in the wall at +y.

    Returns:
        (bosses, cuts): 2 lists of shapes.
    """
    _wall, sunk, room, ear, full, ear_high, opening, boss, bore = SEAT
    x, z = BOARD_AT[0], case.connector
    face = case.y1 if side > 0 else case.y0
    inner = face - side * case.end

    def span(a, b):
        return (min(a, b), max(a, b))

    half_high = XT60[0][1] / 2 + room
    corners = [(-ear - room, -ear_high / 2 - room),
               (-ear + 3.4, -ear_high / 2 - room),
               (-full - room, -half_high), (full + room, -half_high),
               (ear - 3.4, -ear_high / 2 - room),
               (ear + room, -ear_high / 2 - room),
               (ear + room, ear_high / 2 + room),
               (ear - 3.4, ear_high / 2 + room), (full + room, half_high),
               (-full - room, half_high), (-ear + 3.4, ear_high / 2 + room),
               (-ear - room, ear_high / 2 + room)]
    y0, y1 = span(face - side * sunk, face + side * 1.0)
    cuts = [_prism_y([(x + u, z + w) for u, w in corners], y0, y1)]
    wide, high, radius = opening
    y0, y1 = span(inner - side * (boss[1] + 1.0), face)
    cuts.append(_keyed(x, z, wide, high, radius, side, y0, y1))
    bosses = []
    for k in (-1, 1):
        u = x + k * XT60[5] / 2
        b0, b1 = span(inner, inner - side * boss[1])
        bosses.append(_along_y(u, z, boss[0] / 2, b0, b1))
        cuts.append(_along_y(u, z, bore / 2, y0, y1))
    return bosses, cuts


def connectors(case):
    """The room the 2 XT60 take: flange, body and contacts.

    The flange is drawn as its ears and its middle without the slants
    between them, the body with its 2 rounded corners.

    Returns:
        A list of shapes; empty for the XT30 variant, whose connectors
        are not measured.
    """
    if case.connector == "xt30":
        return []
    flange, body, reach, radius, apart, _screws = XT60
    x, z = BOARD_AT[0], case.connector
    out = []
    for side, face in ((1, case.y1), (-1, case.y0)):
        front = face + side * (flange[2] - SEAT[1])

        def slab(wide, high, a, b, side=side, front=front):
            y0, y1 = sorted((front - side * a, front - side * b))
            return _box(x - wide / 2, y0, z - high / 2, x + wide / 2, y1,
                        z + high / 2)

        out.append(slab(flange[0], SEAT[5], 0.0, flange[2]))
        out.append(slab(2 * SEAT[4], flange[1], 0.0, flange[2]))
        y0, y1 = sorted((front - side * flange[2], front - side * body[2]))
        out.append(_keyed(x, z, body[0], body[1],
                          SEAT[6][2] - (SEAT[6][0] - body[0]) / 2, side,
                          y0, y1))
        out.append(slab(2 * (apart + radius), 2 * radius, body[2], reach))
    return out


def box(case):
    """The box: floor, wall, bosses, openings and recesses."""
    hole, deep, relief, relief_deep = INSERT
    top = case.boss_top
    solid = _cut(rounded(X0, case.y0, X1, case.y1, RADIUS, 0.0, case.rim),
                 rounded(X0 + WALL, case.y0 + case.end, X1 - WALL,
                         case.y1 - case.end, RADIUS - WALL, FLOOR,
                         case.rim + 1))
    cuts = []
    for x, y in holes():
        solid = _fuse(solid, _cylinder(x, y, BOSS / 2, FLOOR - 0.1, top))
        cuts.append(_cylinder(x, y, hole / 2, top - deep, top + 1))
        cuts.append(_cylinder(x, y, relief / 2, top - relief_deep, top + 1))
    if case.connector == "xt30":
        cuts.append(xt30(XT30_AT[0], case.y1 - case.end - 1, case.y1 + 1))
        cuts.append(xt30(XT30_AT[1], case.y0 - 1, case.y0 + case.end + 1))
        a0, a1, z0, z1, depth = END_RECESS
        cuts.append(_box(a0, case.y0 + case.end - depth, z0, a1,
                         case.y0 + case.end + 1, z1))
    else:
        for side in (1, -1):
            bosses, seat = xt60_seat(case, side)
            solid = _fuse(solid, *bosses)
            cuts += seat
    a0, a1, low, high, depth = SIDE_RECESS
    cuts.append(_box(X1 - WALL - 1, a0, top + low, X1 - WALL + depth, a1,
                     top + high))
    cuts.append(_cylinder(FLOOR_HOLE[0], FLOOR_HOLE[1], FLOOR_HOLE[2] / 2,
                          -1.0, FLOOR + 1))
    return _cut(solid, *cuts)


def lid(case):
    """The lid: flange, plug, window, recess and the screws' holes."""
    clear, head, sunk = SCREW
    room = PLUG[1]
    solid = _fuse(
        rounded(X0, case.y0, X1, case.y1, RADIUS, case.rim, case.front),
        rounded(X0 + WALL + room, case.y0 + case.end + room,
                X1 - WALL - room, case.y1 - case.end - room, RADIUS - WALL,
                case.plug_end, case.rim + 0.1))
    wx0, wy0, wx1, wy1 = window()
    cuts = [_box(wx0, wy0, case.plug_end - 1, wx1, wy1, case.front + 1),
            _box(RECESS[0], case.y0 + case.end + RECESS[2],
                 case.plug_end - 1, RECESS[1], wy0 + 0.1,
                 case.front - RECESS[3])]
    for x, y in holes():
        cuts.append(_cylinder(x, y, clear / 2, case.plug_end - 1,
                              case.front + 1))
        cuts.append(_cylinder(x, y, head / 2, case.front - sunk,
                              case.front + 1))
    return _cut(solid, *cuts)


def parts(case=None):
    """Build the parts of one variant, or of all.

    Args:
        case: The variant; every variant when None.

    Returns:
        A dict of file stem to solid.
    """
    out = {}
    for one in CASES if case is None else (case,):
        out.update(zip(one.stems, (_merged(box(one)), _merged(lid(one))),
                       strict=True))
    return out


def step_text(stem, solid):
    """The STEP text of one part, with its colour and a fixed header."""
    return re.sub(r"_gen_dial_base\.py", "_gen_display_case.py",
                  base.step_text(stem, solid), count=1)


_BOARD = []


def neighbours(case):
    """The display board's solids on the bosses, and their colours."""
    import step_check
    if not _BOARD:
        _BOARD.extend(step_check.read(_models.path(BOARD_MODEL))[1])
    view = es3c28p.VIEW
    x, y = at_board((view[0] + view[2]) / 2, (view[1] + view[3]) / 2)
    z = case.board_front + es3c28p.TOUCH_Z[1]
    return [(_moved(solid, x=x, y=y, z=z), rgb) for solid, rgb in _BOARD]


def _meets(solids, others, what, errors):
    """Add an error for each pair of the 2 lists that shares volume."""
    boxes = [(name, part, _extent(part)) for name, part in others]
    for i, solid in enumerate(solids):
        near = _extent(solid)
        for name, part, other in boxes:
            if not all(near[k] < other[k + 3] and other[k] < near[k + 3]
                       for k in range(3)):
                continue
            shared = base.volume(BRepAlgoAPI_Common(part, solid).Shape())
            if shared > 1e-3:
                errors.append(f"{name} and {what} {i} share "
                              f"{shared:.3f}mm3, box "
                              + ", ".join(f"{v:.2f}" for v in near))


def check(built):
    """Check the parts, their fit round board and connectors, the stack.

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
    for case in CASES:
        if case.stems[0] not in built:
            continue
        own = [(stem, built[stem]) for stem in case.stems]
        shared = base.volume(BRepAlgoAPI_Common(own[0][1],
                                                own[1][1]).Shape())
        if shared > 1e-6:
            errors.append(f"{case.stems[0]} and its lid share "
                          f"{shared:.3f}mm3")
        board = [solid for solid, _rgb in neighbours(case)]
        _meets(board, own, f"{BOARD_MODEL} solid", errors)
        plugs = connectors(case)
        _meets(plugs, own, "connector box", errors)
        _meets(board, [(f"{case.stems[0]}'s connector {i}", plug)
                       for i, plug in enumerate(plugs)],
               f"{BOARD_MODEL} solid", errors)
        for name, least in (("play", 0.1), ("window", 0.4), ("touch", 0.5),
                            ("fold", 0.5), ("under", 3.0), ("thread", 4.0)):
            value = stack(case)[name]
            if value < least - 1e-9:
                errors.append(f"{case.stems[0]}: stack {name} is "
                              f"{value:.2f}, under {least}")
    return errors


def render(built, directory):
    """Draw each case closed, taken apart and cut open.

    Args:
        built: The parts as ``parts`` gives them.
        directory: The renders' root.
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
            directory, name))))

    def lifted(items, z):
        return [(_moved(solid, z=z), rgb) for solid, rgb in items]

    def cut(items):
        half = _box(BOARD_AT[0], -BIG, -BIG, BIG, BIG, BIG)
        return [(BRepAlgoAPI_Cut(solid, half).Shape(), rgb)
                for solid, rgb in items]

    side = V3d_TypeOfOrientation.V3d_XposYnegZpos
    for case in CASES:
        name = case.stems[0].removesuffix("_box")
        board = [(solid, rgb or (0.5, 0.5, 0.5))
                 for solid, rgb in neighbours(case)]
        plugs = [(solid, (0.90, 0.72, 0.10)) for solid in connectors(case)]
        box_, lid_ = ([(built[stem], base.GRAPHITE)] for stem in case.stems)
        draw(box_ + lid_ + board + plugs, side, f"{name}_case", False)
        draw(lifted(lid_, 50.0) + lifted(board, 25.0) + box_ + plugs, side,
             f"{name}_case_exploded", True)
        draw(cut(box_) + cut([(built[case.stems[1]], (0.35, 0.36, 0.40))])
             + board + plugs, V3d_TypeOfOrientation.V3d_Xpos,
             f"{name}_case_section", True)
    draw([(built[XT30_CASE.stems[1]], base.GRAPHITE)],
         V3d_TypeOfOrientation.V3d_XposYnegZneg, "display_node_lid_inside",
         True)
    draw([(built[XT60_CASE.stems[0]], base.GRAPHITE)],
         V3d_TypeOfOrientation.V3d_XposYnegZpos, "display_node_xt60_seat",
         True)


def main(argv):
    """Write or check the files.

    Args:
        argv: Command-line arguments without the program name.

    Returns:
        0, or 1 for a stale file, a collision or an open stack.
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
