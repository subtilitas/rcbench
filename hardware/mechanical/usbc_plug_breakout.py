"""USB-C plug on a green breakout board, 15.3mm long, 8.4mm wide.

    usbc_plug_breakout   the plug with its board: the board and its
                         pads, the steel shell, the black collar, the
                         24 contact tails, the resistor and the print

The kind sold as a "24 pin USB-C male plug with PCB" for soldering
wires: a USB-C plug whose contact tails straddle the board's edge, 12
on each side, and 4 large pads for the wires on the underside.

Sources: three owner photos of 3072 x 4096px on millimetre paper (not
in the repository). The paper's 1mm lines give the scale, 34px/mm; a
lattice fit of the crossings as hardware/mechanical/_photo_grid.py
does it, on a 1mm pitch, gives the paper frame within 0.14mm and the
camera 264mm over the paper (76mm focal length, 35mm equivalent, from
the photos' metadata). One photo shows a plug board lying flat on each
side and a plug standing on its mouth; the other two show the top side
and the mouth held end-on, without scale. The boards lie on the shell,
so their top faces stand 1.6 to 2.4 over the paper and look 0.6 to 0.9
percent larger; the sizes below are corrected by that and hold to
about 0.1mm.

Measured: the board 4.6 long (from the collar to its rear edge) and
7.7 wide; the collar 2.1 long and 8.4 wide; the shell 8.6 long from
the collar to the mouth, 8.1 wide; the whole 15.3 long. The tails'
pads 12 per side at a pitch of 0.58, 0.35 wide, 1.7 long on the top
side and 1.5 on the underside. The 4 wire pads 1.95 along the board,
1.5 across (the 2 outer) and 1.25 (the 2 inner), with 1 or 2 holes
each. 5 vias in a row 0.3 from the board's middle. The resistor R1,
an 0603 by its size (1.46 x 0.67 in the photo), along y in a printed
frame. The underside's print G, D+ and D- beside the wire pads; the
fourth pad has no name in the photo and is taken to be VBUS. The D-
is drawn 0.35 off its place in the photo, clear of a via. The steel
band over the collar with 2 windows on the top side and 1 on the
underside.

The shell's section is the standard plug's 8.25 x 2.4; the photos give
8.1 +- 0.15 wide and do not resolve its height. The end view shows the
black insulator filling the shell, a slot in its middle and the
slot's floor pale grey.

Not measured, estimated: the board's thickness (0.8, from the side of
a plug held in the hand), the collar's height (2.8), the shell's wall
(0.2), the slot (6.8 x 1.2, 6.2 deep, its mouth 0.3 inside the
shell's), the tails' thickness (0.15) and width (0.25), the drills
(0.35 wire holes, 0.3 vias), all heights of R1 (its package's). The
board is centred in the shell's height and butts against the collar;
how far it reaches into the collar is unknown. Not drawn: the 24
contacts inside the slot, the shell's seam and its 3 notches, the
latch springs in the collar's windows, the tracks, the solder mask's
copper areas, the plating of the holes and the solder on the pads.

Frame: origin at the board's centre on its underside, z up, x towards
the plug's mouth, y to the left looking along x from the top side (the
side of R1). All sizes are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _parts as p
import _solid as s

STEM = "usbc_plug_breakout"
GREEN = (0.05, 0.36, 0.20)
PALE = (0.74, 0.78, 0.74)

T = 0.8                         # the board's thickness
BOARD = (-2.3, 2.3, 3.85)       # x from, x to, half width
# The shell: x from the collar to the mouth, half width, z of its
# bottom and top, its wall.
SHELL = (4.4, 13.0, 4.125, T / 2 - 1.2, T / 2 + 1.2, 0.2)
# The collar: x from, x to, half width, z of bottom and top; the steel
# band's half width and thickness.
COLLAR = (2.3, 4.4, 4.2, -1.0, 1.8)
BAND, BAND_T = 2.8, 0.1
TOP_WINDOWS = ((2.75, 0.56, 3.95, 2.2), (2.75, -2.2, 3.95, -0.56))
UNDER_WINDOW = (2.85, -1.55, 4.2, 1.55)
# The slot: half width, half height, depth from its mouth; its mouth's
# place inside the shell's.
SLOT = (3.4, 0.6, 6.2, 0.3)

TAILS = p.rows(12, 0.58)
TAIL_W, TAIL_T, TAIL_X = 0.25, 0.15, 1.0
PAD_W = 0.35
TOP_PAD_X, UNDER_PAD_X = 0.6, 0.8
# The wire pads on the underside: x from, x to, y from, y to, and their
# holes (x, y).
WIRE_PADS = ((-2.0, -0.05, 2.15, 3.65, ((-1.7, 2.97), (-0.7, 2.97))),
             (-2.0, -0.05, 0.255, 1.505, ((-1.72, 0.91),)),
             (-2.0, -0.05, -1.505, -0.255, ((-1.72, -0.91),)),
             (-2.0, -0.05, -3.65, -2.15, ((-1.7, -2.97), (-0.7, -2.97))))
WIRE_DRILL = 0.35
VIAS = ((0.3, 3.38), (0.3, 2.73), (0.3, 1.65), (0.3, 1.0), (0.3, -1.88))
VIA_DRILL = 0.3
RING = 0.6                      # tinned ring round a hole, diameter
TOP_RINGS = ((-1.72, 0.91), (-1.7, -2.97), (-0.7, -2.97))
UNDER_RINGS = ((0.3, -1.88),)
R1 = (-0.57, -0.12)
R1_FRAME = (1.27, 2.0)
TOP_PRINT = (("R1", -0.63, -1.8, 0.5, 0.7, 90.0),)
UNDER_PRINT = (("G", 0.37, -3.0, 0.45, 0.35, 90.0),
               ("D+", 0.37, -0.95, 0.45, 0.75, 90.0),
               ("D-", 0.37, 0.45, 0.45, 0.7, 90.0))


def board():
    """The plate with its holes."""
    x0, x1, half = BOARD
    holes = [s.circle(x, y, WIRE_DRILL / 2)
             for *_box, places in WIRE_PADS for x, y in places]
    holes += [s.circle(x, y, VIA_DRILL / 2) for x, y in VIAS]
    return [s.ext(s.rounded(x0, -half, x1, half, 0.2), 0.0, T, GREEN,
                  holes=holes)]


def pads():
    """The copper of both sides: tail pads, wire pads, tinned rings."""
    x1 = BOARD[1]
    out = []
    for y in TAILS:
        out.append(s.box(TOP_PAD_X, y - PAD_W / 2, x1, y + PAD_W / 2,
                         T, T + p.PAD, p.TIN))
        out.append(s.box(UNDER_PAD_X, y - PAD_W / 2, x1, y + PAD_W / 2,
                         -p.PAD, 0.0, p.TIN))
    for x0, x1, y0, y1, places in WIRE_PADS:
        out.append(s.ext(s.rect(x0, y0, x1, y1), -p.PAD, 0.0, p.TIN,
                         holes=[s.circle(x, y, WIRE_DRILL / 2)
                                for x, y in places]))
    for places, z0, drill in ((TOP_RINGS, T, WIRE_DRILL),
                              (UNDER_RINGS, -p.PAD, VIA_DRILL)):
        for x, y in places:
            out.append(s.ext(s.circle(x, y, RING / 2), z0, z0 + p.PAD,
                             p.TIN, holes=[s.circle(x, y, drill / 2)]))
    return out


def tails():
    """The contacts' tails, soldered onto the pads at the board's edge."""
    x1 = BOARD[1]
    out = []
    for y in TAILS:
        a, b = y - TAIL_W / 2, y + TAIL_W / 2
        out.append(s.box(TAIL_X, a, x1, b, T + p.PAD, T + p.PAD + TAIL_T,
                         p.TIN))
        out.append(s.box(TAIL_X, a, x1, b, -p.PAD - TAIL_T, -p.PAD,
                         p.TIN))
    return out


def collar():
    """The black collar: a core, 2 rounded ends, the steel band."""
    x0, x1, half, z0, z1 = COLLAR
    r = 0.5
    out = [s.box(x0, -BAND, x1, BAND, z0 + BAND_T, z1 - BAND_T, p.BLACK)]
    for k in (1, -1):
        end = [(k * BAND, z0), (k * (half - r), z0, (k * (half - r),
                                                     z0 + r, k)),
               (k * half, z0 + r), (k * half, z1 - r, (k * (half - r),
                                                       z1 - r, k)),
               (k * (half - r), z1), (k * BAND, z1)]
        out.append(s.ext(end, x0, x1, p.BLACK, frame=s.X))
    out.append(s.ext(s.rect(x0, -BAND, x1, BAND), z1 - BAND_T, z1, p.STEEL,
                     holes=[s.rect(*w) for w in TOP_WINDOWS]))
    out.append(s.ext(s.rect(x0, -BAND, x1, BAND), z0, z0 + BAND_T, p.STEEL,
                     holes=[s.rect(*UNDER_WINDOW)]))
    return out


def shell():
    """The steel shell, the insulator with its slot and the slot's floor."""
    x0, x1, half, z0, z1, wall = SHELL
    inner = s.stadium(-half + wall, z0 + wall, half - wall, z1 - wall)
    slot_w, slot_h, depth, back = SLOT
    zc = (z0 + z1) / 2
    floor = x1 - back - depth
    slot = s.rounded(-slot_w, zc - slot_h, slot_w, zc + slot_h, 0.3)
    return [s.ext(s.stadium(-half, z0, half, z1), x0, x1, p.STEEL,
                  holes=[inner], frame=s.X),
            s.ext(inner, floor, x1 - back, p.BLACK, holes=[slot],
                  frame=s.X),
            s.ext(inner, x0, floor, PALE, frame=s.X)]


def r1():
    """The resistor R1 and its printed frame."""
    x, y = R1
    w, h = R1_FRAME
    out = s.place(p.r0603(), x, y, T, 90.0)
    return out + s.place(p.frame(x - w / 2, y - h / 2, x + w / 2,
                                 y + h / 2), z=T)


def words():
    """The print of both sides."""
    out = []
    for string, x, y, height, width, turn in TOP_PRINT:
        out += s.place(p.text(string, height, width=width), x, y, T, turn)
    for string, x, y, height, width, turn in UNDER_PRINT:
        out += s.place(p.text(string, height, width=width), x, y, 0.0,
                       turn, flip=True)
    return out


def shapes():
    """The plug and its board, as shapes."""
    return board() + pads() + tails() + collar() + shell() + r1() + words()


def parts(_colors):
    """The model of the plug on its board.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items(shapes())}
