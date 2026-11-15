# SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""Lithium polymer charger board with a 5V boost output, part by part.

The board is the one of hardware/kicad/modules/lipo_charger_boost_5v.py;
outline and pads come from that spec. This model adds the plating of
the 6 holes, the chip, the inductor, the capacitors and resistors, the
4 LEDs, the push button and the underside's print: a white field with
black letters.

Sources: two owner photos on 5mm squared paper (top side, underside;
not in the repository). Positions are in mm from the board's centre,
within about 0.2mm. The camera stands 171 over the paper and 26 from
the board, so a part's top lies 0.155 further from the camera per mm
of height; the inductor and the chip are set back by that. The print's
letters are the font of _font.py set to the height and width each line
has in the photo, not traced.

Packages by their look: chip SOIC-8, capacitors 1206 and 0603,
resistors 0603, LEDs 0603; their sizes and heights are the packages'
nominal ones. The inductor is 6.7 square in the photo.

Not measured, estimated: the board's thickness (1.6), the inductor's
height (3.0), the button's height (1.8). Not drawn: the tracks, the
frames printed round the parts, four labels of about 1 height printed
left of the LEDs, which the photo does not resolve, and the print 1R0
on the inductor.

Frame: as the KiCad model of the module. Origin at hole 1 (OUT 5V) on
the board's underside, x along the row towards hole 6, y from the LEDs
towards the row, z up. All sizes are mm.
"""

import _board
import _parts as p
import _solid as s

kicad = _board.kicad_spec("lipo_charger_boost_5v")
STEM = "lipo_charger_boost_5v"
MASK = (0.06, 0.06, 0.07)
FERRITE = (0.36, 0.36, 0.37)
B = _board.Board(kicad)
CX, CY = kicad.W / 2, kicad.H / 2

ROW = [(kicad.ROW_X0 + 2.54 * i - CX, kicad.ROW_Y - CY) for i in range(6)]
INDUCTOR = (-1.4, -4.65)
CHIP = (-0.1, 4.35)
BIG_CAPS = ((-6.4, -5.85), (6.15, -5.9), (3.9, 0.2), (-6.5, 3.2),
            (-4.3, 3.2))
SMALL_CAP = (-6.4, -1.1)
RESISTORS = ((3.2, -6.95), (3.2, -4.1), (3.85, 5.0))
SENSOR = (6.5, 0.3)
LEDS = ((-2.9, 11.0), (-0.85, 11.0), (1.2, 11.0), (2.85, 11.0))
BUTTON = (6.75, 8.0)
# Underside: the white field and the box under each hole's name, both
# (x0, y0, x1, y1); the names as they stand under the top view's holes.
FIELD = (-7.3, -4.6, 7.3, 11.3)
LABEL_Y, LABEL_W = (-8.5, -4.6), 2.1
NAMES = (("OUT 5V", 5.6), ("GND", 3.0), ("BAT", 2.8), ("GND", 3.0),
         ("GND", 3.0), ("VIN", 2.6))
TITLE = (("Charge", -2.4, 6.0, 1.5, 6.9, -90.0),
         ("&", -0.2, 6.0, 1.5, 1.2, -90.0),
         ("DisCharge", 2.0, 6.0, 1.5, 9.6, -90.0))


def board():
    """The plate and the plating of its 6 holes."""
    out = B.plate(MASK, B.outline(0.2),
                  [(x, y, kicad.DRILL) for x, y in ROW])
    for i, (x, y) in enumerate(ROW):
        out += B.through(p.hole_pad(kicad.DRILL, kicad.PAD[0], B.t,
                                    square=i in kicad.SQUARE,
                                    long=kicad.PAD[1]), x, y)
    return out


def top_side():
    """The parts of the top side."""
    out = B.top(p.block(6.7, 6.7, 3.0, FERRITE, 0.8), *INDUCTOR)
    for dy in (-3.6, 3.6):
        out += B.top(p.pad(3.0, 0.5), INDUCTOR[0], INDUCTOR[1] + dy)
    out += B.top(p.soic8(), *CHIP, 90.0)
    for x, y in BIG_CAPS:
        out += B.top(p.c1206(), x, y, 90.0)
    out += B.top(p.c0603(), *SMALL_CAP, 90.0)
    for x, y in RESISTORS:
        out += B.top(p.r0603(), x, y, 90.0)
    out += B.top(p.chip(2.0, 1.25, 0.6, p.ORANGE), *SENSOR, 90.0)
    for x, y in LEDS:
        out += B.top(p.led0603(), x, y, 90.0)
    return out + B.top(p.side_button(), *BUTTON, 180.0)


def underside():
    """The white field, the boxes of the names and the black letters."""
    x0, y0, x1, y1 = FIELD
    out = B.under(p.pad(x1 - x0, y1 - y0, p.WHITE, p.PRINT),
                  (x0 + x1) / 2, (y0 + y1) / 2)
    lines = list(TITLE)
    for (x, _y), (name, width) in zip(ROW, NAMES, strict=True):
        a, b = LABEL_Y
        out += B.under(p.pad(LABEL_W, b - a, p.WHITE, p.PRINT), x,
                       (a + b) / 2)
        lines.append((name, x, a + 0.5 + width / 2, 1.3, width, -90.0))
    return out + B.words(lines, under=True, rgb=p.BLACK, z=p.PRINT)


def kicad_model():
    """The model of the KiCad footprint: the board lying flat."""
    return s.items(board() + top_side() + underside())


def parts(_colors):
    """The model of the board.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: kicad_model()}
