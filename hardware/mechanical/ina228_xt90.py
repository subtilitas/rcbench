"""INA228 current sensor module with two XT90PW connectors, part by part.

The board, print "BIFRC V1.1", is the one of
hardware/kicad/modules/ina228_bifrc_xt90.py; outline, screw holes,
header, bars, shunt and slots come from that spec. This model adds the
shapes: the two XT90PW halves with their bent tails, the solder on the
bars of both sides and round the tails, the shunt, the chip and its
chip parts, the header, the bare pads and the print. A second model
holds the printed case the board is screwed into and its 4 screws.

Sources:
    P: three owner photos on 5mm squared paper (top side, underside,
    case; not in the repository), laid onto the board frame by
    _photo_grid.py. Positions on the board within about 0.2mm.
    W: a dealer's dimension drawing of XT90PW Male/Female: pitch 10.90;
    socket half 20.80 x 9.90 collar, 19.20 x 8.30 nose, 19.00 long,
    24.00 with the rear plate, pegs 8.90 from the tails; pin half 20.80
    x 9.90, 20.70 long, 25.70 with the rear plate, pegs 9.40 from the
    tails; tails and pegs 3.00 under the housing.
    O: the owner's measurements: the board is 1.6 thick; the shunt's
    ends are 1.2 high, its middle is 1.8 high and stands about 0.2 off
    the board.
    Chip: VSSOP-10, the INA228's package. Chip parts: size 0603.
    Print: the legends BIFRC V1.1, GND, 5V and L A V G 5 are traced
    from the top photo by _trace_ina228_print.py into _ina228_print.py;
    the 4 round marks, the rings round the header's labels, the frames
    round the chip parts and the header and the lines round the shunt
    and the underside's diode are drawn from their places in P.

The two housings: the top photo shows their upper faces, 9.9 over the
board; with the camera 196 over the board at x = 54 the rear edges lie
at x = 18.45 and x = 62.15. The pegs' solder joints on the underside,
at x = 12.87 and x = 67.2, give 18.45 and 62.1 with W's distances.

Case: the 4 holes of the case are 73.3 x 40.3 apart on the paper and
70.15 x 38.8 on the board, so the standoffs' tops are 7.9 over the
paper (camera 195.1 over the paper). A printed case showed the holes'
long spacing 0.5 short: the board's holes are 70.65 apart (the owner's
correction), and the case's holes follow them. The standoffs' 7.9 is
kept from the photo. The outline is 89.4 x 56.8 on the
paper, 84.8 x 53.9 at the rim's top.

Not measured, estimated or drawn from the sources above:
    the solder's height (0.8) on both
    sides and round the tails' ends (1.0); how far tails (1.1), pegs
    and header pins (0.7) stand out of the underside; the tails'
    diameter (3.5) and bend radius (3.8), scaled from W; the pegs'
    section (1.0 x 0.4); the depth of the shroud (12.5); which end of
    the mating outline has the cut corners (drawn at the minus
    contact). The shunt's 1.8 is taken as the height of the middle's
    top over the board. Of the case: the floor's thickness (1.6), the
    rim's height over the board (0.6), the gap round the board (0.25)
    and the wall (2.25), which add up to the outline; the width of the
    two openings in the rim (21.4). The screws: M3, pan head 5.3
    across and 2.0 high, drawn 2.2 thick where they cut their thread
    in the case.

Frame: as the KiCad model of the module. Origin at header pin 1 (L) on
the board's underside, x along the header towards pin 5, y from the
header towards the plus bar, z up. All sizes are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import importlib.util
import math
from pathlib import Path

import _parts as p
import _solid as s
from _ina228_print import PRINT

_SPEC = (Path(__file__).resolve().parents[1] / "kicad" / "modules"
         / "ina228_bifrc_xt90.py")
_spec = importlib.util.spec_from_file_location("ina228_bifrc_xt90", _SPEC)
kicad = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(kicad)

STEM = "ina228_xt90"
CASE_STEM = "ina228_xt90_case"
T = 1.6
OX, OY = kicad.FOOTPRINT["origin"]
BLUE = (0.05, 0.22, 0.55)

# The two halves: x of the housing's rear edge, the row's center y.
PLUG_X, SOCKET_X, ROW_Y = kicad.PLUG[0][2], kicad.SOCKET[1][0], 24.65
TAIL_X = (PLUG_X + p.XT90_BEND, SOCKET_X - p.XT90_BEND)
TAIL_Y = (ROW_Y - p.XT90_PITCH / 2, ROW_Y + p.XT90_PITCH / 2)
# Under the board: the tails' and the pegs' length, the solder round a
# tail's end (radius, height) and round a peg's and a header pin's.
TAIL_OUT, PEG_OUT = 1.1, 0.7
TAIL_JOINT, SMALL_JOINT = (3.3, 1.0), (1.2, 0.6)
SOLDER = 0.8
HOLE, RING, FOIL = 3.05, 5.1, 0.05
# Slots: length, width, slant in degrees.
SLOT = (2.2, 0.9, 45.0)
# Shunt: the ends' height, the middle's length, the height of its top
# and of its underside.
SHUNT = (1.2, 5.3, 1.8, 0.2)
# The underside's bars, (x0, y0, x1, y1): the plus bar, and of each
# minus bar the strip and the block under the shunt's end. A bar's end
# at a tail follows the solder round the tail.
UNDER_PLUS = (TAIL_X[0], 16.9, TAIL_X[1], 21.8)
UNDER_MINUS = (((TAIL_X[0], 27.3, 31.45, 32.8), (31.45, 26.4, 37.6, 34.4)),
               ((48.3, 27.5, TAIL_X[1], 32.9), (43.0, 26.3, 48.3, 34.3)))
# Bare pads of the underside: 3 slanted ovals (x, y) of OVAL (length,
# width) and a diode's 2 pads.
OVALS = ((14.6, 44.5), (22.0, 44.5), (29.5, 44.4))
OVAL = (5.8, 2.9)
DIODE = ((14.5, 19.5, 17.7, 22.0), (14.5, 27.2, 17.7, 29.7))
DIODE_LINES = (
    (13.2, 20.2, 13.4, 28.4), (18.8, 20.2, 19.0, 28.4),
    (13.4, 20.2, 14.4, 20.4), (17.8, 20.2, 18.8, 20.4),
    (13.4, 22.4, 18.8, 22.6),
    (13.4, 28.2, 14.4, 28.4), (17.8, 28.2, 18.8, 28.4),
)

# Chip parts, (kind, x, y, turn): the part's length along x at turn 0.
CHIP = (40.65, 44.35)
SMALL = (
    ("r", 38.95, 37.8, 90), ("r", 40.95, 37.8, 90), ("c", 37.75, 40.9, 90),
    ("c", 39.9, 39.95, 0), ("c", 42.85, 39.95, 0),
    ("c", 43.25, 42.75, 90), ("c", 45.1, 42.75, 90), ("r", 46.6, 42.7, 90),
    ("c", 43.25, 45.9, 90), ("c", 45.1, 45.9, 90), ("r", 46.6, 45.8, 90),
    ("r", 56.0, 41.25, 90), ("r", 58.4, 41.25, 90),
)
# Bare pads of the parts the board does not carry, (x0, y0, x1, y1).
BARE = (
    (16.75, 5.75, 18.3, 6.65), (16.75, 7.05, 18.3, 7.95),
    (20.5, 2.6, 21.4, 3.3), (20.5, 4.1, 21.4, 4.85),
    (20.5, 5.6, 21.4, 6.3), (20.5, 7.2, 21.4, 7.9),
    (24.8, 1.3, 25.95, 4.75), (24.8, 6.6, 25.95, 10.0),
    (29.5, 4.1, 30.4, 4.85), (29.5, 5.6, 30.4, 6.3),
    (31.3, 3.0, 33.4, 3.6), (31.3, 5.55, 33.4, 6.1), (31.3, 6.8, 33.4, 7.4),
    (37.3, 3.0, 39.4, 3.65), (37.3, 4.3, 39.4, 4.9),
    (37.3, 5.55, 39.4, 6.15), (37.3, 6.8, 39.4, 7.45),
    (34.2, 3.75, 36.6, 6.8),
    (30.7, 1.1, 31.5, 1.85), (32.1, 1.1, 32.9, 1.85),
    (33.7, 1.1, 34.4, 1.85), (35.2, 1.1, 36.0, 1.85),
    (36.0, 8.6, 36.85, 9.5), (37.5, 8.6, 38.3, 9.5),
    (42.85, 2.0, 45.25, 4.2), (47.0, 2.0, 49.5, 4.2),
    (40.45, 6.95, 42.9, 10.3), (46.5, 6.95, 49.0, 10.3),
    (50.3, 3.2, 51.9, 4.5), (53.5, 3.2, 55.1, 4.5),
    (50.3, 6.4, 51.9, 7.8), (53.5, 6.4, 55.1, 7.8),
    (50.4, 8.95, 51.1, 9.85), (51.9, 8.95, 52.7, 9.85),
    (48.25, 40.75, 48.95, 41.45), (48.25, 43.95, 48.95, 44.65),
    (36.55, 45.0, 37.25, 45.4), (36.55, 46.3, 37.25, 46.7),
)
# Round bare pads, (x, y, radius): the 2 test pads and 6 small points.
DOTS = tuple((x, y, 0.8) for x, y in kicad.TEST_PADS) + tuple(
    (x, y, 0.18) for x, y in (
        (36.9, 44.5), (36.0, 45.25), (37.8, 45.25), (36.0, 46.5),
        (37.85, 46.5), (36.9, 47.2)))

# Print: thickness, line width; the 4 round marks as (x, y, cross), each
# MARK_R in radius with a line along y and, with cross, one along x;
# the rings round the header's labels (x of each, y, radius, line); the
# frame round a chip part (length, width, corner radius, line); the
# header's frame; the lines round the shunt.
INK, LINE = 0.02, 0.2
MARKS = ((12.94, 9.59, True), (66.94, 9.84, True), (12.71, 39.06, False),
         (67.64, 39.26, False))
MARK_R = 2.95
LABEL_X = (55.9, 58.5, 61.1, 63.75, 66.35)
LABEL = (47.0, 1.2, 0.2)
FRAME = (2.7, 1.45, 0.3, 0.15)
HEADER_FRAME = (54.65, 43.0, 67.5, 45.78, 0.12)
SHUNT_LINES = (
    (31.2, 25.4, 36.6, 25.6), (42.6, 25.4, 47.9, 25.6),
    (31.2, 25.6, 31.4, 27.3), (47.7, 25.6, 47.9, 27.1),
    (31.2, 33.2, 31.4, 34.9), (31.2, 34.9, 47.2, 35.1),
)

# Case: gap round the board, wall, floor, the standoffs' tops over the
# case's underside, the rim's height over the board's top side, the
# width of the rim's two openings, the pocket's distance from the
# board's short and long edges, the standoffs' radius, the screw
# holes' diameter.
CASE = (0.25, 2.25, 1.6, 7.9, 0.6, 21.4, 0.9, 1.6, 4.6, 2.2)
# Screw: thread, head diameter, head height, length under the head.
SCREW = (3.0, 5.3, 2.0, 7.5)


def at(x, y):
    """A point of the KiCad spec's board frame (y down) in the model's."""
    return (x - OX, OY - y)


def box(x0, y0, x1, y1, z0, z1, rgb):
    """A box given by its corners in the board frame."""
    (ax, ay), (bx, by) = at(x0, y1), at(x1, y0)
    return s.box(ax, ay, bx, by, z0, z1, rgb)


def slot(x, y, slant=0):
    """The outline of one slot; slant 1 rises to the right, -1 falls."""
    length, width, angle = SLOT
    cx, cy = at(x, y)
    flat = s.stadium(cx - length / 2, cy - width / 2, cx + length / 2,
                     cy + width / 2)
    return s.turned(flat, cx, cy, slant * angle) if slant else flat


def slots():
    """The outlines of the 42 milled slots."""
    out = [slot(x0 + kicad.SLOT_PITCH * i, y, 1)
           for x0, y, n in kicad.SLOT_ROWS for i in range(n)]
    out += [slot(x, kicad.SLOT_FLAT_Y) for x in kicad.SLOT_FLAT_X]
    out += [slot(x, y, first * (-1) ** row) for x, first in kicad.SLOT_UNDER
            for row, y in enumerate(kicad.SLOT_UNDER_Y)]
    return out


def tails():
    """The 4 places where a tail passes the board, board frame."""
    return [(x, y) for x in TAIL_X for y in TAIL_Y]


def pegs():
    """The 4 places where a peg passes the board, board frame."""
    return ([(TAIL_X[0] - p.XT90_PEG[1], ROW_Y + k * p.XT90_PEG_Y)
             for k in (-1, 1)]
            + [(TAIL_X[1] + p.XT90_PEG[0], ROW_Y + k * p.XT90_PEG_Y)
               for k in (-1, 1)])


def board():
    """The bare board with its 59 holes and the rings of the 4 screws."""
    x0, y1 = at(0.0, 0.0)
    x1, y0 = at(kicad.BW, kicad.BH)
    holes = [s.circle(*at(x, y), HOLE / 2) for x, y in kicad.HOLES]
    holes += [s.circle(*at(x, kicad.HDR_Y), 0.5) for x in kicad.HDR_X]
    holes += [s.circle(*at(x, y), p.XT90_TAIL / 2) for x, y in tails()]
    holes += [s.circle(*at(x, y), 0.65) for x, y in pegs()]
    out = [s.ext(s.rounded(x0, y0, x1, y1, 2.4), 0.0, T, BLUE,
                 holes=holes + slots())]
    for x, y in kicad.HOLES:
        ring = (s.circle(*at(x, y), RING / 2),
                [s.circle(*at(x, y), HOLE / 2)])
        out += [s.ext(ring[0], T, T + FOIL, p.TIN, holes=ring[1]),
                s.ext(ring[0], -FOIL, 0.0, p.TIN, holes=ring[1])]
    return out


def bar_end(x, y0, y1, center, radius, side):
    """One end of a bar's outline where solder or a tail stands.

    Args:
        x: The end's x, model frame.
        y0: The bar's lower edge, model frame.
        y1: The bar's upper edge, model frame.
        center: The (x, y) of the round thing in the end, model frame.
        radius: Its radius.
        side: -1 for the bar's left end, 1 for its right end.

    Returns:
        The end's corners from the lower edge to the upper one at a
        right end, from the upper to the lower one at a left end.
    """
    cx, cy = center
    arc = (cx, cy, -1)
    reach = math.sqrt(max(radius * radius - (x - cx) ** 2, 0.0))
    if cy - reach > y0 and cy + reach < y1:
        # The round thing cuts a notch into a straight end.
        up = [(x, y0), (x, cy - reach, arc), (x, cy + reach), (x, y1)]
        down = [(x, y1), (x, cy + reach, arc), (x, cy - reach), (x, y0)]
        return up if side > 0 else down

    # The round thing covers the whole end: the end is its arc.
    def on(y):
        return cx - side * math.sqrt(radius * radius - (y - cy) ** 2)

    if side > 0:
        return [(on(y0), y0, arc), (on(y1), y1)]
    return [(on(y1), y1, arc), (on(y0), y0)]


def bar(x0, y0, x1, y1, z0, z1, radius, round_left, round_right):
    """Solder on a bar, cut round what stands in its ends.

    Args:
        x0: The bar's left end, board frame.
        y0: The bar's edge towards the board's top edge, board frame.
        x1: The bar's right end, board frame.
        y1: The bar's other edge, board frame.
        z0: The solder's underside.
        z1: The solder's top.
        radius: Radius of what stands in an end.
        round_left: The tail's (x, y) in the left end, board frame, or
            None.
        round_right: The same for the right end.
    """
    (ax, ay), (bx, by) = at(x0, y1), at(x1, y0)
    outline = []
    for x, center, side in ((bx, round_right, 1), (ax, round_left, -1)):
        if center is None:
            outline += [(x, ay), (x, by)] if side > 0 else [(x, by), (x, ay)]
        else:
            outline += bar_end(x, ay, by, at(*center), radius, side)
    return s.ext(outline, z0, z1, p.TIN)


def joint(x, y, radius, height, hole):
    """Solder round a pin's end on the underside; hole is its outline."""
    return s.ext(s.circle(*at(x, y), radius), -height, 0.0, p.TIN,
                 holes=[hole])


def top_side():
    """Everything on the board's top side, model frame."""
    out = []
    out += s.place(p.xt90pw_m(bevel=1, under=T + TAIL_OUT),
                   *at(PLUG_X, ROW_Y), T, turn=180.0)
    out += s.place(p.xt90pw_f(bevel=-1, under=T + TAIL_OUT),
                   *at(SOCKET_X, ROW_Y), T)
    r = p.XT90_TAIL / 2
    left, right = (TAIL_X[0], TAIL_Y[0]), (TAIL_X[1], TAIL_Y[0])
    out.append(bar(*kicad.PLUS_BAR, T, T + SOLDER, r, left, right))
    left, right = (TAIL_X[0], TAIL_Y[1]), (TAIL_X[1], TAIL_Y[1])
    out.append(bar(*kicad.BATN_BAR, T, T + SOLDER, r, left, None))
    out.append(bar(*kicad.ESCN_BAR, T, T + SOLDER, r, None, right))
    x0, y0, x1, y1 = kicad.SHUNT
    out += s.place(p.shunt(x1 - x0, y1 - y0, *SHUNT),
                   *at((x0 + x1) / 2, (y0 + y1) / 2), T)
    out += s.place(p.vssop10(), *at(*CHIP), T, turn=90.0)
    for kind, x, y, turn in SMALL:
        part = p.r0603() if kind == "r" else p.c0603()
        out += s.place(part, *at(x, y), T, turn=turn)
    hx = (kicad.HDR_X[0] + kicad.HDR_X[3]) / 2
    out += s.place(p.pin_header(4, below=T + PEG_OUT),
                   *at(hx, kicad.HDR_Y), T)
    for pad in BARE:
        out.append(box(*pad, T, T + FOIL, p.TIN))
    for x, y, radius in DOTS:
        out.append(s.ext(s.circle(*at(x, y), radius), T, T + FOIL, p.TIN))
    return out


def underside():
    """Solder, bare pads and print on the board's underside."""
    radius, height = TAIL_JOINT
    out = []
    for x, y in tails():
        out.append(joint(x, y, radius, height,
                         s.circle(*at(x, y), p.XT90_TAIL / 2)))
    left, right = (TAIL_X[0], TAIL_Y[0]), (TAIL_X[1], TAIL_Y[0])
    out.append(bar(*UNDER_PLUS, -SOLDER, 0.0, radius, left, right))
    for (strip, block), tail in zip(UNDER_MINUS, (0, 1), strict=True):
        center = (TAIL_X[tail], TAIL_Y[1])
        out.append(bar(*strip, -SOLDER, 0.0, radius,
                       None if tail else center, center if tail else None))
        out.append(box(*block, -SOLDER, 0.0, p.TIN))
    radius, height = SMALL_JOINT
    for x, y in pegs():
        cx, cy = at(x, y)
        out.append(joint(x, y, radius, height,
                         s.rect(cx - 0.5, cy - 0.2, cx + 0.5, cy + 0.2)))
    for x in kicad.HDR_X[:4]:
        cx, cy = at(x, kicad.HDR_Y)
        out.append(joint(x, kicad.HDR_Y, 0.9, height,
                         s.rect(cx - 0.32, cy - 0.32, cx + 0.32, cy + 0.32)))
    length, width = OVAL
    for x, y in OVALS:
        cx, cy = at(x, y)
        oval = s.stadium(cx - length / 2, cy - width / 2, cx + length / 2,
                         cy + width / 2)
        out.append(s.ext(s.turned(oval, cx, cy, 45.0), -FOIL, 0.0, p.TIN))
    for pad in DIODE:
        out.append(box(*pad, -FOIL, 0.0, p.TIN))
    for line in DIODE_LINES:
        out.append(box(*line, -INK, 0.0, p.WHITE))
    return out


def ink(outline, holes=()):
    """Print on the board's top side; the outlines in the model frame."""
    return s.ext(outline, T, T + INK, p.WHITE, holes=list(holes))


def flat(points):
    """A traced outline of the board frame in the model frame."""
    return [at(x, y) for x, y in points]


def printed():
    """The top side's print: legends, marks, rings, frames and lines."""
    out = [ink(flat(outline), [flat(h) for h in holes])
           for _legend, outline, holes in PRINT]
    for x, y, cross in MARKS:
        cx, cy = at(x, y)
        r, h = MARK_R - LINE - 0.01, LINE / 2
        out.append(ink(s.circle(cx, cy, MARK_R),
                       [s.circle(cx, cy, MARK_R - LINE)]))
        out.append(ink(s.rect(cx - h, cy - r, cx + h, cy + r)))
        if cross:
            out += [ink(s.rect(cx - r, cy - h, cx - h, cy + h)),
                    ink(s.rect(cx + h, cy - h, cx + r, cy + h))]
    y, r, line = LABEL
    for x in LABEL_X:
        c = at(x, y)
        out.append(ink(s.circle(*c, r), [s.circle(*c, r - line)]))
    length, width, radius, line = FRAME
    for _kind, x, y, turn in SMALL:
        cx, cy = at(x, y)
        ring = [s.turned(s.rounded(cx - length / 2 + k, cy - width / 2 + k,
                                   cx + length / 2 - k, cy + width / 2 - k,
                                   radius - k / 2), cx, cy, turn)
                for k in (0.0, line)]
        out.append(ink(ring[0], ring[1:]))
    x0, y0, x1, y1, line = HEADER_FRAME
    (ax, ay), (bx, by) = at(x0, y1), at(x1, y0)
    out.append(ink(s.rect(ax, ay, bx, by),
                   [s.rect(ax + line, ay + line, bx - line, by - line)]))
    for x0, y0, x1, y1 in SHUNT_LINES:
        (ax, ay), (bx, by) = at(x0, y1), at(x1, y0)
        out.append(ink(s.rect(ax, ay, bx, by)))
    return out


def module():
    """All solids of the module."""
    return s.items(board() + top_side() + underside() + printed())


def pocket(a, b, radius):
    """The case's pocket: a rectangle less a round standoff per corner.

    Args:
        a: Half the pocket's length along x.
        b: Half its width along y.
        radius: The standoffs' radius about the screw holes.

    Returns:
        The outline about the board's center, counter-clockwise.
    """
    (x0, y0), (x1, _), (_, y1), _ = kicad.HOLES
    hx, hy = (x1 - x0) / 2, (y1 - y0) / 2
    dx = math.sqrt(radius * radius - (b - hy) ** 2)
    dy = math.sqrt(radius * radius - (a - hx) ** 2)
    k = radius / math.sqrt(2.0)
    corner = [(a, hy - dy), (hx - k, hy - k), (hx - dx, b)]
    out = []
    for sx, sy in ((1, 1), (-1, 1), (-1, -1), (1, -1)):
        pts = [(sx * x, sy * y) for x, y in corner]
        if sx * sy < 0:
            pts.reverse()
        center = (sx * hx, sy * hy, -1)
        out += [(*pts[0], center), (*pts[1], center), pts[2]]
    return out


def rim(a, b, r, wall, y_cut, side):
    """Half of the case's rim over the board, from one opening on.

    Args:
        a: Half the inner length along x.
        b: Half the inner width along y.
        r: The inner corners' radius.
        wall: The wall's thickness.
        y_cut: The y at which the half ends at both short sides.
        side: 1 for the half at +y, -1 for the one at -y.

    Returns:
        The outline about the board's center.
    """
    big_a, big_b, big_r = a + wall, b + wall, r + wall
    pts = [
        (-a, y_cut), (-a, b - r, (-a + r, b - r, -1)), (-a + r, b),
        (a - r, b, (a - r, b - r, -1)), (a, b - r), (a, y_cut),
        (big_a, y_cut), (big_a, big_b - big_r,
                         (big_a - big_r, big_b - big_r, 1)),
        (big_a - big_r, big_b), (-big_a + big_r, big_b,
                                 (-big_a + big_r, big_b - big_r, 1)),
        (-big_a, big_b - big_r), (-big_a, y_cut),
    ]
    if side > 0:
        return pts
    return [(q[0], -q[1]) if len(q) == 2
            else (q[0], -q[1], (q[2][0], -q[2][1], -q[2][2])) for q in pts]


def case():
    """The case's shell with its standoffs, and the 4 screws."""
    gap, wall, floor, stand, lip, opening, end, edge, boss, bore = CASE
    thread, head, head_h, length = SCREW
    cx, cy = at(kicad.BW / 2, kicad.BH / 2)
    a, b, r = kicad.BW / 2 + gap, kicad.BH / 2 + gap, 2.4 + gap
    outer = s.rounded(-a - wall, -b - wall, a + wall, b + wall, r + wall)
    inner = s.rounded(-a, -b, a, b, r)
    (x0, y0), (x1, _), (_, y1), _ = kicad.HOLES
    hx, hy = (x1 - x0) / 2, (y1 - y0) / 2
    holes = [(kx * hx, ky * hy) for kx in (-1, 1) for ky in (-1, 1)]
    row = at(0.0, ROW_Y)[1] - cy
    out = [
        s.ext(outer, -stand, -stand + floor, p.BLACK),
        s.ext(outer, -stand + floor, -FOIL, p.BLACK,
              holes=[pocket(kicad.BW / 2 - end, kicad.BH / 2 - edge, boss)]
              + [s.circle(x, y, bore / 2) for x, y in holes]),
        s.ext(outer, -FOIL, T, p.BLACK, holes=[inner]),
        s.ext(rim(a, b, r, wall, row + opening / 2, 1), T, T + lip,
              p.BLACK),
        s.ext(rim(a, b, r, wall, -(row - opening / 2), -1), T, T + lip,
              p.BLACK),
    ]
    for x, y in holes:
        out.append(s.ext(s.circle(x, y, bore / 2), T - length, 0.0,
                         p.STEEL))
        out.append(s.ext(s.circle(x, y, thread / 2), 0.0, T + FOIL,
                         p.STEEL))
        out.append(s.ext(s.circle(x, y, head / 2), T + FOIL,
                         T + FOIL + head_h, p.STEEL))
    return s.items(s.place(out, cx, cy))


def parts(_colors):
    """The models of the module and of its case.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: module(), CASE_STEM: case()}
