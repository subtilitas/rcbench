"""5V boost converter board with an enable pad, part by part.

The board is the one of hardware/kicad/modules/boost_5v_en.py; outline,
pads and holes come from that spec. This model adds the plating of the
4 corner holes, the chip, the inductor and its 2 pads, the 2 diodes,
the capacitors and resistors, the enable pad, the print of both sides
and the underside's white field, which carries no print.

Sources: two owner photos on 5mm squared paper (top side, underside;
not in the repository). Positions are in mm from the board's center,
within about 0.2mm. The camera stands 171 over the paper and 63 from
the board, so a part's top lies 0.36 further from the camera per mm of
height; the inductor, the diodes, the chip and the capacitors are set
back by that. The inductor's 2 pads, which the photo shows beside it,
confirm its place. The print's letters are the font of _font.py set to
the height and width each line has in the photos, not traced.

Packages by their look: chip SOIC-8, diodes SMA, capacitors 1206,
resistors 0603 and 0402; their sizes and heights are the packages'
nominal ones. The inductor is 6.7 square in the photo.

Not measured, estimated: the board's thickness (1.6), the inductor's
height (3.0). The corner pads are drawn alike on both sides; the
photos show the two input pads 2.6 and 3.8 wide on the underside. Not
drawn: the tracks, the solder on the corner pads and the print 2R2 on
the inductor.

Frame: as the KiCad model of the module. Origin at the IN+ hole on the
board's underside, x towards IN-, y towards the outputs, z up. All
sizes are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _board
import _parts as p
import _solid as s

kicad = _board.kicad_spec("boost_5v_en")
STEM = "boost_5v_en"
GREEN = (0.05, 0.33, 0.20)
FERRITE = (0.40, 0.38, 0.35)
B = _board.Board(kicad)
CX, CY = kicad.W / 2, kicad.H / 2

CORNERS = [(x - CX, y - CY, size) for _n, x, y, size in kicad.PADS]
ENABLE = (kicad.EN[0] - CX, kicad.EN[1] - CY)
HOLE = (kicad.HOLE[0] - CX, kicad.HOLE[1] - CY)
INDUCTOR = (-0.2, 5.65)
INDUCTOR_PADS = ((-4.05, 5.65), (3.65, 5.65))
CHIP = (3.1, -2.35)
DIODES = ((-5.4, -1.35), (-2.0, -1.35))
CAPS = ((0.45, -9.55), (0.45, -7.25), (0.5, 10.0))
RESISTORS = ((-4.9, -5.45), (-2.2, -5.45))
R3 = (5.75, 4.65)
TOP_PRINT = (("L1", -5.7, 6.2, 0.8, 1.3, 90.0),
             ("R3", 4.5, 3.3, 0.7, 1.2, 90.0))
UNDER_PRINT = (("IN+", -5.3, 6.2, 0.9, 2.6, -90.0),
               ("IN-", 3.2, 9.1, 0.9, 2.6, -90.0),
               ("OUT+", -1.3, -8.95, 0.9, 3.4, -90.0),
               ("OUT-", 1.95, -8.95, 0.9, 3.4, -90.0))
# The underside's white field, corner by corner.
FIELD = ((6.75, -3.65), (1.25, -3.65), (1.1, -2.75), (-0.05, -1.45),
         (-3.55, -1.45), (-3.55, 6.85), (6.75, 6.85))


def board():
    """The plate, the plating of the corner holes and the enable pad."""
    out = B.plate(GREEN, B.outline(0.2),
                  [(x, y, kicad.DRILL) for x, y, _size in CORNERS]
                  + [(*HOLE, kicad.HOLE[2])])
    for x, y, (w, h) in CORNERS:
        out += B.through(p.hole_pad(kicad.DRILL, w, B.t, p.TIN,
                                    square=True, long=h), x, y)
    out += B.top([s.ext(s.circle(0.0, 0.0, kicad.EN[2] / 2), 0.0, p.PAD,
                        p.TIN)], *ENABLE)
    return out


def top_side():
    """The parts and the print of the top side."""
    out = B.top(p.block(6.7, 6.7, 3.0, FERRITE, 0.8), *INDUCTOR)
    for x, y in INDUCTOR_PADS:
        out += B.top(p.pad(0.9, 3.5), x, y)
    out += B.top(p.soic8(), *CHIP, 90.0)
    for x, y in DIODES:
        out += B.top(p.sma(), x, y, 90.0)
    for x, y in CAPS:
        out += B.top(p.c1206(), x, y)
    for x, y in RESISTORS:
        out += B.top(p.r0603(), x, y)
    out += B.top(p.r0402(), *R3)
    return out + B.words(TOP_PRINT)


def underside():
    """The white field and the print of the underside."""
    return [s.ext([B.at(x, y) for x, y in FIELD], -p.PRINT, 0.0,
                  p.WHITE)] + B.words(UNDER_PRINT, under=True)


def kicad_model():
    """The model of the KiCad footprint: the board lying flat."""
    return s.items(board() + top_side() + underside())


def parts(_colors):
    """The model of the board.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: kicad_model()}
