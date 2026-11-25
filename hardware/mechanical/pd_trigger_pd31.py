"""USB-C PD trigger board up to 48V, 13.0 x 10.0mm, purple.

    pd_trigger_pd31   the bare board: plate, 2 output holes, the USB-C
                      socket, the chip, 5 chip parts, the 3 solder
                      jumpers and the print of both sides

Sold as "PD3.0 PD3.1 QC3.0 decoy fast charging module, DC
5/9/12/15/20/28/36/48V voltage trigger", the low-voltage model, made by
Shenzhen Chonghai Technology. The chip carries no legible marking; its
kind is unknown.

Sources, none in the repository: an owner photo of 3072 x 4096px of 3
boards on millimetre paper (lines every 1mm, 5mm and 10mm), one board
edge-on, one showing the underside, one the top side, and a photo of
the bag's label for the product name. 23 to 24 1mm lines in a crop
round each board fit at 33.5 to 33.8px/mm, within 0.5px. The lens is
76mm (35mm equivalent), so the camera stands about 265mm over the
paper; a thing h over the paper looks larger by 265 / (265 - h): 0.4%
for the board's top side, 1.6% for the underside of the board that lies
on its socket, 3.9% for the edge-on board's upper edge. Sizes below
are corrected for it. One pixel is 0.03mm; edges blur over 3 to 5px,
so an edge is good to about 0.15mm, a place on the top side to about
0.1mm, a place on the underside to about 0.3mm: on the second board
the holes sit 0.3mm off centre against its edges.

Measured:
  - The board, 13.0 x 10.0: the width on the top side, the length on
    the edge-on board (13.5 on the paper at its upper edge) and on the
    underside (12.9 to 13.1).
  - The board's thickness, 1.0: the edge-on board's edge face.
  - The socket's mouth, 1.5 past the board's end, on the top side and
    edge-on. The shell is 8.8 to 8.9 wide and 7.5 long on the top
    side and stands 3.0 over the board edge-on, give or take 0.3: the
    board leans and the shell's round sides blur the reading.
    _parts.usb_c is drawn (8.94 x 3.25 x 7.35); it fits within those
    errors. Its 2 tabs a side meet the 4 slots of the underside.
  - The output holes, 7.9 apart and 0.95 in from the board's end, in
    square pads of 1.5 on both sides; the drill, 0.9, from the inner
    edge of the plating.
  - The chip, 2.0 x 2.0, 3 pads 0.65 apart at each of 2 sides.
  - 4 black and 1 tan chip parts of 1.1 x 0.6 with their solder,
    drawn as 0402 resistors and a 0402 capacitor.
  - The 3 solder jumpers 1, 2 and 3: 2 oval pads of 0.45 x 0.8 each,
    0.52 apart, rows 1.4 apart, and a printed frame. A second pair of
    such pads in a frame by the + hole has no label.
  - The tinned traces to the + and - holes.
  - The underside's print: the pads 1 2 3 4 with a white box under
    each, the columns A (5V 9V 12V 15V 20V) and B (5V 20V 28V 36V
    48V), + and -; 2 holes of 0.6 by the socket.

Estimated: every height but the board's and the socket's (the chip
0.75, the chip parts their class figures); the chip's pads under its
body; the drill of the 2 small holes. Not drawn: the vias, the
underside's traces, the arrow by the unlabelled pads, the solder in the
socket's slots. What the jumpers and the table set is not marked on the
board: which of A and B applies is unknown.

Frame: origin at the board's centre on its underside, z up, x towards
the USB-C mouth, y to the left seen from the top side with x pointing
away; the + hole at -y. All mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _parts as p
import _solid as s

STEM = "pd_trigger_pd31"
PURPLE = (0.30, 0.10, 0.42)
L, W, T = 13.0, 10.0, 1.0
USB = (0.65, 0.0)               # the shell's rear end; mouth at x 8.0
HOLES = ((-5.55, -3.95), (-5.55, 3.95))
DRILL, HOLE_PAD = 0.9, 1.5
PEGS = ((1.7, -2.95), (1.7, 2.95))      # 2 small holes, 0.6
CHIP = (-1.95, 1.52)
RESISTORS = ((-1.49, -1.30), (-3.78, -1.00), (-3.78, 0.84), (-3.78, 2.80))
CAP = (-2.60, -0.78)
JUMPERS = (-1.40, 0.02, 1.42)          # y of the rows 1, 2, 3 ... 3 at -y
JUMPER_X = (-5.62, -6.14)
SPARE = ((-2.26, -4.17), (-2.79, -4.17))
SPARE_FRAME = (-3.26, -4.84, -1.12, -3.51)
JUMPER_FRAME = (-6.40, -2.18, -4.59, 2.17)
JUMPER_LINES = (-0.71, 0.77)
GND_TRACE = (-4.80, 3.97, 0.13, 4.68)
VBUS_TRACE = ((-0.38, -2.45), (-2.67, -2.65), (-4.62, -3.19))
# The underside's slots for the socket's tabs: x0, x1, y.
SLOTS = ((0.33, 2.23, -4.45), (0.33, 2.23, 4.45), (4.62, 6.22, -4.45),
         (4.62, 6.22, 4.45))
TOP_PRINT = (("+", -4.37, -4.40, 0.8, 0.8, 180.0),
             ("-", -4.44, 3.20, 0.8, 0.8, -90.0),
             ("3", -4.95, JUMPERS[0], 0.5, 0.4, -90.0),
             ("2", -4.95, JUMPERS[1], 0.5, 0.4, -90.0),
             ("1", -4.95, JUMPERS[2], 0.5, 0.4, -90.0))
# The underside's table: x of the 4 pads' columns, the voltages of A at
# y -0.05 and of B at y 2.3, all reading towards +y.
TABLE_X = (-3.55, -2.12, -0.65, 0.85, 2.43)
COLUMN_A = ("5V", "9V", "12V", "15V", "20V")
COLUMN_B = ("5V", "20V", "28V", "36V", "48V")
UNDER_PRINT = (("+", -3.99, -4.41, 0.8, 0.8, 180.0),
               ("-", -4.50, 3.24, 0.8, 0.8, 90.0),
               ("A", 3.42, -0.05, 0.7, 0.55, 90.0),
               ("B", 3.57, 2.35, 0.7, 0.55, 90.0))
BOX = (0.8, 1.2, -1.91)         # the white boxes: x size, y size, y


def top(shapes, x, y, turn=0.0, z=0.0):
    """Shapes put on the top side, z over the board's surface."""
    return s.place(shapes, x, y, T + z, turn)


def under(shapes, x, y, turn=0.0):
    """Shapes put on the underside, turned over."""
    return s.place(shapes, x, y, 0.0, turn, flip=True)


def words(lines, side):
    """Lines of white print: (text, x, y, height, width, turn)."""
    out = []
    for text, x, y, height, width, turn in lines:
        out += side(p.text(text, height, p.WHITE, width), x, y, turn)
    return out


def board():
    """The plate, the output holes' plating and the 2 small holes."""
    out = [s.ext(s.rounded(-L / 2, -W / 2, L / 2, W / 2, 0.2), 0.0, T,
                 PURPLE, holes=[s.circle(x, y, DRILL / 2) for x, y in HOLES]
                 + [s.circle(x, y, 0.3) for x, y in PEGS])]
    for x, y in HOLES:
        out += s.place(p.hole_pad(DRILL, HOLE_PAD, T, p.TIN, square=True),
                       x, y)
    return out


def top_side():
    """The parts, pads, traces and print of the top side."""
    out = top(p.usb_c(), *USB, 90.0)
    out += top(p.qfn(2.0, 2.0, 0.75, 3, 0, 0.65), *CHIP)
    for x, y in RESISTORS:
        out += top(p.r0402(), x, y, 90.0)
    out += top(p.c0402(), *CAP)
    for y in JUMPERS:
        for x in JUMPER_X:
            out += top([s.ext(s.stadium(-0.225, -0.4, 0.225, 0.4), 0.0,
                              p.PAD, p.TIN)], x, y)
    for x, y in SPARE:
        out += top([s.ext(s.stadium(-0.225, -0.4, 0.225, 0.4), 0.0, p.PAD,
                          p.TIN)], x, y)
    out += top([s.box(*GND_TRACE[:2], *GND_TRACE[2:], 0.0, p.PAD, p.TIN),
                s.ext(s.strip(VBUS_TRACE, 0.6), 0.0, p.PAD, p.TIN)], 0, 0)
    out += top(p.frame(*SPARE_FRAME), 0, 0)
    x0, y0, x1, y1 = JUMPER_FRAME
    out += top(p.frame(x0, y0, x1, y1), 0, 0)
    half = 0.075
    for y in JUMPER_LINES:
        out += top([s.box(x0 + half, y - half, x1 - half, y + half, 0.0,
                          p.PRINT, p.WHITE)], 0, 0)
    return out + words(TOP_PRINT, top)


def underside():
    """The slots' pads and the print of the underside."""
    out = []
    for x0, x1, y in SLOTS:
        half = (x1 - x0) / 2
        out += under([s.ext(s.stadium(-half, -0.4, half, 0.4), 0.0, p.PAD,
                            p.TIN)], (x0 + x1) / 2, y)
    lines = list(UNDER_PRINT)
    for k, x in enumerate(TABLE_X):
        for name, y in ((COLUMN_A[k], -0.05), (COLUMN_B[k], 2.30)):
            lines.append((name, x, y, 0.65, 0.48 * len(name), 90.0))
        if k:
            lines.append((str(k), x, -3.09, 0.7, 0.45, 90.0))
            sx, sy, y = BOX
            out += under([s.box(-sx / 2, -sy / 2, sx / 2, sy / 2, 0.0,
                                p.PRINT, p.WHITE)], x, y)
    return out + words(lines, under)


def bare():
    """The board alone, as shapes."""
    return board() + top_side() + underside()


shapes = bare()


def parts(_colors):
    """The model of the bare board.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items(shapes)}
