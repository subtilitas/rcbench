"""Matek mBEC12S, a buck converter for 6 to 60V in, 18.0 x 15.0mm.

    matek_mbec12s   the board: 4 edge pads, inductor, diode, converter
                    chip, 3 capacitors, LED, 2 voltage pads, chip parts,
                    the print of both sides

Output 5V as delivered; a solder bridge on the pad 9V or 12V sets that
voltage. The edge pads are OUT, G, G and IN, 2.54 apart.

Sources, none in the repository: 6 seller pictures the owner sent
(2026-11-21), one of them a top view with the dimensions 18mm, 15mm and
2.54mm printed on it, one of the underside; sellers' listings, which
give 18 x 15 x 4.5mm and 2g. Part places are read from the dimensioned
top view by eye at 23px/mm, within about 0.5mm.

Drawn: the inductor 6.8 x 6.0 with its print 100, the diode with the
print SS56 as a package of 4.3 x 3.0, the converter (print TPS54361) as
a leadless package of 4.0 x 3.8, 2 capacitors 1206 and 1 capacitor
1210, the LED, 13 chip parts, the 2 voltage pads, and the print.

Not measured: every height but the 4.5 over all, which the inductor's
3.3 over a board of 1.2 gives. The board's thickness is read off an
angled picture. The maker's sign on the underside is drawn as its white
square only. The half holes of the edge pads are not drawn.

Frame: origin at the pad OUT on the board's edge, on the underside, x
into the board, y up, z up. Places below are mm from the board's upper
left corner seen from the top side, x to the right, y down, the 4 pads
at the left.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

from types import SimpleNamespace

import _board
import _parts as p
import _solid as s

STEM = "matek_mbec12s"
BLACK = (0.06, 0.06, 0.07)
FERRITE = (0.62, 0.61, 0.58)
W, H, T = 18.0, 15.0, 1.2
PADS = tuple((name, 4.75 + 2.54 * i) for i, name in enumerate(
    ("OUT", "G", "G", "IN")))
PAD = (1.1, 1.7)                # into the board, along the edge
B = _board.Board(SimpleNamespace(FOOTPRINT={
    "board": (W, H), "origin": (0.0, PADS[0][1])}), T)

INDUCTOR = (11.1, 3.25, 6.8, 6.0, 3.3)
DIODE = (10.9, 8.8)
CHIP = (10.25, 12.45)
CAPS_1206 = ((3.1, 3.9), (5.4, 3.9))
CAP_1210 = (3.7, 10.05)
LED = (3.3, 1.25)
VOLTAGE_PADS = ((16.75, 2.65), (17.0, 8.85))
SMALL = ((0.85, 1.4, 90.0), (6.5, 10.5, 90.0), (3.1, 13.4, 90.0),
         (4.45, 13.4, 90.0), (6.3, 13.1, 90.0), (17.0, 6.25, 90.0),
         (14.8, 9.5, 90.0), (14.6, 11.0, 90.0), (15.7, 11.0, 90.0),
         (14.6, 12.6, 90.0), (15.7, 12.6, 90.0), (17.2, 13.2, 90.0),
         (15.2, 14.0, 0.0))
TOP_PRINT = (("12V", 16.2, 1.1, 0.8, 2.0),
             ("9V", 17.1, 11.9, 0.8, 1.4, -90.0),
             ("OUT", 1.75, 4.75, 0.7, 1.7, 90.0),
             ("G", 1.75, 7.29, 0.7, 0.6, 90.0),
             ("G", 1.75, 9.83, 0.7, 0.6, 90.0),
             ("IN", 1.75, 12.37, 0.7, 1.1, 90.0))
UNDER_PRINT = (("MATEKSYS", 16.3, 7.5, 1.2, 10.5, 90.0),
               ("SKU: mBEC12S", 6.0, 1.3, 0.75, 7.6),
               ("5-12V OUT", 6.3, 3.9, 0.75, 6.0),
               ("Default 5V", 6.0, 5.2, 0.75, 6.2),
               ("GND", 3.7, 7.3, 0.75, 2.2), ("GND", 3.7, 9.85, 0.75, 2.2),
               ("6-60V IN", 5.3, 12.4, 0.75, 5.4))
SIGN = (9.3, 6.2, 13.5, 10.4)   # the white square of the maker's sign


def at(x, y):
    """A place from the board's upper left corner as Board takes it."""
    return x - W / 2, y - H / 2


def board():
    """The plate and its 4 edge pads on both sides."""
    out = B.plate(BLACK, B.outline(0.3), [])
    reach, wide = PAD
    for _name, y in PADS:
        (ax, ay), (bx, by) = (B.at(*at(0.0, y - wide / 2)),
                              B.at(*at(reach, y + wide / 2)))
        for z0 in (-p.PAD, T):
            out.append(s.box(ax, by, bx, ay, z0, z0 + p.PAD, p.TIN))
    return out


def top_side():
    """The parts and the print of the top side."""
    x, y, wide, deep, high = INDUCTOR
    out = B.top(p.block(wide, deep, high, FERRITE, 0.8), *at(x, y))
    out += B.top(p.text("100", 2.6, p.EPOXY, 3.6), *at(x, y), z=high)
    out += B.top(p.sod(4.3, 3.0, 2.2, 5.6, 1.6), *at(*DIODE))
    out += B.top(p.text("SS56", 0.8, p.WHITE, 2.6), *at(*DIODE), 180.0,
                 z=2.2)
    out += B.top(p.qfn(4.0, 3.8, 0.8, 5, 0, 0.8), *at(*CHIP))
    for x, y in CAPS_1206:
        out += B.top(p.c1206(), *at(x, y), 90.0)
    out += B.top(p.chip(3.2, 2.5, 2.0, p.TAN), *at(*CAP_1210), 90.0)
    out += B.top(p.led0603(), *at(*LED))
    for x, y, turn in SMALL:
        out += B.top(p.r0402(), *at(x, y), turn)
    for x, y in VOLTAGE_PADS:
        out += B.top(p.pad(1.0, 2.0), *at(x, y))
    return out + B.words([(t, *at(x, y), *rest)
                          for t, x, y, *rest in TOP_PRINT])


def underside():
    """The print of the underside."""
    x0, y0, x1, y1 = SIGN
    (ax, ay), (bx, by) = B.at(*at(x0, y0)), B.at(*at(x1, y1))
    return [s.box(ax, by, bx, ay, -p.PRINT, 0.0, p.WHITE)] + B.words(
        [(t, *at(x, y), *rest) for t, x, y, *rest in UNDER_PRINT],
        under=True)


def bare():
    """The board alone, as shapes."""
    return board() + top_side() + underside()


def parts(_colors):
    """The model of the board.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items(bare())}
