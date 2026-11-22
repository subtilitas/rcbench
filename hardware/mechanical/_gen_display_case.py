#!/usr/bin/env python3
"""Printed case of the display node with XT30 connectors, in 2 parts.

    display_node_box   floor, wall, 4 bosses with threaded inserts,
                       one XT30 opening in each short wall
    display_node_lid   front plate with the display window and a plug
                       that reaches into the box down to the board

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

Printing: the box with its underside on the bed, the lid with its front
on the bed. No part needs support; the XT30 openings are 11.2 wide with
a flat top.

Not drawn: screws, inserts, the 2 boards on the floor, the XT30
connectors, wires. Not measured: the height of the parts under the
board; 3.6 is free under the board's speaker socket and 8.3 elsewhere.
The hole of 3.5 through the floor at FLOOR_HOLE is the measured case's;
what it is for is not stated.

Shapes are boxes and cylinders along an axis of the frame only, so the
STEP text is the same on every processor.

Frame: origin at the box's center on its underside, x across, y along
the long side with the board's USB socket towards -y, z up. All sizes
are mm.

Usage:
    python3 hardware/mechanical/_gen_display_case.py [--check]
    xvfb-run -a python3 hardware/mechanical/_gen_display_case.py \
        --render DIR

Writes models/casing/display_node_{box,lid}.step and the same 2 as
.stl. --check fails on a stale file, on a part that is not one valid
piece, on the 2 parts sharing volume, on a part sharing volume with a
solid of the display board's model, and on a stack that does not
close. --render draws the case closed, taken apart and cut open to
DIR/casing/, 1200 x 1000 pixels. Needs OCP (pip install
cadquery-ocp==8.0.1.1.0).

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import re
import sys

from OCP.BRepAlgoAPI import BRepAlgoAPI_Common, BRepAlgoAPI_Cut
from OCP.BRepCheck import BRepCheck_Analyzer
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.BRepPrimAPI import BRepPrimAPI_MakeCylinder
from OCP.gp import gp_Ax2, gp_Dir, gp_Pnt
from OCP.StlAPI import StlAPI_Writer

import _gen_dial_base as base
import _models
import es3c28p
from _gen_dial_base import _box, _cylinder, _extent, _merged, _moved
from _gen_node_case import _cut, _fuse, rounded

STEMS = ("display_node_box", "display_node_lid")
BOARD_MODEL = "es3c28p"

# Outline, wall, the corners' outer radius, the floor.
SIZE = (59.8, 101.2)
WALL, RADIUS, FLOOR = 1.8, 3.2, 1.7
# Heights over the underside: the bosses' tops, the box's rim, the
# lid's front.
BOSS_TOP, RIM, FRONT = 10.0, 16.0, 17.8
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
# its end at -y, the lid's thickness left over it.
RECESS = (-17.5, 19.5, -45.6, 2.6)
# XT30 opening: width, height, its lower edge over the underside; x of
# its straight end in the wall at +y and in the wall at -y. The other
# end is a half circle.
XT30 = (11.2, 5.6, 1.9)
XT30_AT = (-4.0, -5.9)
# Recesses in the wall's inner face, (a0, a1, z0, z1, depth): in the
# wall at +x along y, in the wall at -y along x.
SIDE_RECESS = (-8.35, 23.15, 4.2, 12.1, 1.0)
END_RECESS = (-9.6, 5.4, 7.0, 12.05, 1.0)
FLOOR_HOLE = (5.6, -37.5, 3.5)

X1, Y1 = SIZE[0] / 2, SIZE[1] / 2
X0, Y0 = -X1, -Y1
BOARD_BACK = BOSS_TOP + PRINT
BOARD_FRONT = BOARD_BACK + BOARD[2]
PLUG_END = RIM - PLUG[0]
BIG = 200.0


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


def stack():
    """How the case closes, in heights over the underside.

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
    touch = BOARD_FRONT + es3c28p.TOUCH_Z[1]
    fold = BOARD_FRONT + es3c28p.LCD_Z[1] + es3c28p.FLEX
    under_head = FRONT - SCREW[2]
    return {
        "play": PLUG_END - BOARD_FRONT,
        "window": min(panel[0] - wx0, wx1 - panel[1]),
        "touch": FRONT - touch,
        "fold": FRONT - RECESS[3] - fold,
        "under": BOARD_BACK - 4.7 - FLOOR,
        "thread": 10.0 - (under_head - BOSS_TOP),
    }


def _along_y(x, z, radius, y0, y1):
    return BRepPrimAPI_MakeCylinder(
        gp_Ax2(gp_Pnt(x, y0, z), gp_Dir(0, 1, 0)), radius, y1 - y0).Shape()


def xt30(straight, y0, y1):
    """The cut of one XT30 opening through a short wall."""
    wide, high, low = XT30
    radius = high / 2
    return _fuse(_box(straight - wide + radius, y0, low, straight, y1,
                      low + high),
                 _along_y(straight - wide + radius, low + radius, radius,
                          y0, y1))


def box():
    """The box: floor, wall, bosses, openings and recesses."""
    hole, deep, relief, relief_deep = INSERT
    solid = _cut(rounded(X0, Y0, X1, Y1, RADIUS, 0.0, RIM),
                 rounded(X0 + WALL, Y0 + WALL, X1 - WALL, Y1 - WALL,
                         RADIUS - WALL, FLOOR, RIM + 1))
    cuts = []
    for x, y in holes():
        solid = _fuse(solid, _cylinder(x, y, BOSS / 2, FLOOR - 0.1,
                                       BOSS_TOP))
        cuts.append(_cylinder(x, y, hole / 2, BOSS_TOP - deep,
                              BOSS_TOP + 1))
        cuts.append(_cylinder(x, y, relief / 2, BOSS_TOP - relief_deep,
                              BOSS_TOP + 1))
    cuts.append(xt30(XT30_AT[0], Y1 - WALL - 1, Y1 + 1))
    cuts.append(xt30(XT30_AT[1], Y0 - 1, Y0 + WALL + 1))
    a0, a1, z0, z1, depth = SIDE_RECESS
    cuts.append(_box(X1 - WALL - 1, a0, z0, X1 - WALL + depth, a1, z1))
    a0, a1, z0, z1, depth = END_RECESS
    cuts.append(_box(a0, Y0 + WALL - depth, z0, a1, Y0 + WALL + 1, z1))
    cuts.append(_cylinder(FLOOR_HOLE[0], FLOOR_HOLE[1], FLOOR_HOLE[2] / 2,
                          -1.0, FLOOR + 1))
    return _cut(solid, *cuts)


def lid():
    """The lid: flange, plug, window, recess and the screws' holes."""
    clear, head, sunk = SCREW
    room = WALL + PLUG[1]
    solid = _fuse(rounded(X0, Y0, X1, Y1, RADIUS, RIM, FRONT),
                  rounded(X0 + room, Y0 + room, X1 - room, Y1 - room,
                          RADIUS - WALL, PLUG_END, RIM + 0.1))
    wx0, wy0, wx1, wy1 = window()
    cuts = [_box(wx0, wy0, PLUG_END - 1, wx1, wy1, FRONT + 1),
            _box(RECESS[0], RECESS[2], PLUG_END - 1, RECESS[1], wy0 + 0.1,
                 FRONT - RECESS[3])]
    for x, y in holes():
        cuts.append(_cylinder(x, y, clear / 2, PLUG_END - 1, FRONT + 1))
        cuts.append(_cylinder(x, y, head / 2, FRONT - sunk, FRONT + 1))
    return _cut(solid, *cuts)


def parts():
    """Build the 2 parts.

    Returns:
        A dict of file stem to solid.
    """
    return dict(zip(STEMS, (_merged(box()), _merged(lid())), strict=True))


def step_text(stem, solid):
    """The STEP text of one part, with its colour and a fixed header."""
    return re.sub(r"_gen_dial_base\.py", "_gen_display_case.py",
                  base.step_text(stem, solid), count=1)


def neighbours():
    """The display board's solids on the bosses, and their colours."""
    import step_check
    view = es3c28p.VIEW
    x, y = at_board((view[0] + view[2]) / 2, (view[1] + view[3]) / 2)
    z = BOARD_FRONT + es3c28p.TOUCH_Z[1]
    return [(_moved(solid, x=x, y=y, z=z), rgb) for solid, rgb in
            step_check.read(_models.path(BOARD_MODEL))[1]]


def check(built):
    """Check the parts, their fit round the board and the stack.

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
    shared = base.volume(BRepAlgoAPI_Common(*built.values()).Shape())
    if shared > 1e-6:
        errors.append(f"the 2 parts share {shared:.3f}mm3")
    boxes = {stem: _extent(part) for stem, part in built.items()}
    for i, (solid, _rgb) in enumerate(neighbours()):
        near = _extent(solid)
        for stem, part in built.items():
            other = boxes[stem]
            if not all(near[k] < other[k + 3] and other[k] < near[k + 3]
                       for k in range(3)):
                continue
            shared = base.volume(BRepAlgoAPI_Common(part, solid).Shape())
            if shared > 1e-3:
                errors.append(f"{stem} and {BOARD_MODEL} solid {i} share "
                              f"{shared:.3f}mm3, box "
                              + ", ".join(f"{v:.2f}" for v in near))
    for name, least in (("play", 0.1), ("window", 0.4), ("touch", 0.5),
                        ("fold", 0.5), ("under", 3.0), ("thread", 4.0)):
        if stack()[name] < least - 1e-9:
            errors.append(f"stack: {name} is {stack()[name]:.2f}, under "
                          f"{least}")
    return errors


def render(built, directory):
    """Draw the case closed, taken apart and cut open.

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

    board = [(solid, rgb or (0.5, 0.5, 0.5)) for solid, rgb in neighbours()]
    box_, lid_ = ([(built[stem], base.GRAPHITE)] for stem in STEMS)
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
    draw(box_ + lid_ + board, side, "display_node_case", False)
    draw(lifted(lid_, 50.0) + lifted(board, 25.0) + box_, side,
         "display_node_case_exploded", True)
    draw(cut(box_) + cut([(built[STEMS[1]], (0.35, 0.36, 0.40))]) + board,
         V3d_TypeOfOrientation.V3d_Xpos, "display_node_case_section", True)
    draw(lifted([(_moved(built[STEMS[1]], z=0.0), base.GRAPHITE)], 0.0),
         V3d_TypeOfOrientation.V3d_XposYnegZneg, "display_node_lid_inside",
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
