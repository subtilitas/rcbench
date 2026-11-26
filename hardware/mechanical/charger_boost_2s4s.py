"""Boost charger board for 2S to 4S lithium packs, 17.9 x 36.8mm, green.

    charger_boost_2s4s   the bare board: plate, 2 mounting holes, the
                         BAT and GND pads with their slots, the GND and
                         VIN pads, the parts and the print of both sides

Sold as "Multi-Cell 2S 3S 4S Type-C To 8.4V 12.6V 16.8V Step-Up Boost
LiPo Polymer Li-Ion Charger", variant 4S 1A (the bag's label). The top
side prints DDTCCRUB, the underside "Lithium battery charger". Solder
jumpers on the underside set the cells (2S, 3S, 4S) and the current
(1A, 2A, 4A); this board has 4S and 1A closed.

Sources, none in the repository: 6 owner photos of 3072 x 4096px on
millimetre paper (1mm, 5mm and 10mm lines): 3 of the top side, 1 of the
underside, 1 edge-on with the board standing on its long BAT side and
seen from the VIN side, 1 of the bag's label. The 1mm grid gives
33.9px/mm; 6000 to 7300 grid crossings fit the plane projection of each
photo within 0.1mm. The 35mm equivalent focal length of 76mm (the
photos' metadata) puts the camera 265 over the paper. The board's top
side lies 1.6 over the paper, so the top photo is read on that plane:
1.006 times larger than the paper. Parts with a high top (inductor,
diode, USB-C socket) are read on their top's plane about the point under
the image's center. Places are within about 0.2mm; the inductor's within
about 0.4mm.

Measured, top photo: the outline 17.9 x 36.8 (18.0 x 37.0 on the
paper); the 2 mounting holes 3.1 across, 2.1 from the BAT end, not
plated; the BAT and GND pads 4.1 x 3.0, each with a slot of two
overlapping holes 1.85 and 1.3 across; the GND and VIN pads 2.0 square
with a hole 1.2 across; every part's place and size. The USB-C socket
is 8.9 wide, its shell 6.35 long; it stands 1.4 past the board's edge
(the edge-on photo gives 1.45) and has 6 pins.

Measured, edge-on photo, each height read off the paper and divided by
1.01 to 1.07 for the height of the part over the paper: inductor 2.7,
diode 2.2, SOP-8 chips 1.4, socket 3.25 (3.35 read), 1206 capacitors
1.2, 0805 capacitors 0.85, board 1.5 to 1.6. Heights within about 0.15.
The edge-on photo gives the socket's shell 6.8 long; the top photo's
6.35 is drawn.

Packages by their look and size: the diode SMB (body 3.6 x 4.3), the
shunt R100 a 1206 resistor, 3 capacitors 1206 at the BAT end and 3
0805 at the VIN side, 2 resistors 0805 (222), 2 LEDs 0805, 2 chips
SOP-8 (body 3.9 x 4.9). The inductor is 6.5 x 6.7 on its top and
prints 100; the shunt prints R100, the resistors 222. Beside the shunt
are 2 bare pads 1.6 x 2.0.

The board is drawn 1.6 thick. Estimated, not measured: the LEDs'
height (0.75) and the resistors' (0.5). The inductor's body stands
0.7 left of the middle of its 2 pads in the photo; drawn as seen. Not
drawn: the tracks, the vias, the print's frames round the parts, the
chips' print, the arrows and the "OUT" of the underside, an "N" before
"3-6V" on the underside, and the holes of the socket's 4 legs.

Frame: origin at the board's center on its underside, x to the right
seen from the top side with the USB-C socket at the top, y towards the
socket, z up. Places below are mm in that frame. All sizes are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import math

import _parts as p
import _solid as s

STEM = "charger_boost_2s4s"
GREEN = (0.05, 0.42, 0.30)
FERRITE = (0.50, 0.50, 0.48)
INK = (0.15, 0.15, 0.15)
T = 1.6                         # the board's thickness
W, H = 17.9, 36.8               # the board's outline

HOLES = ((-6.95, -16.27), (6.75, -16.25))
HOLE = 3.1
CORNERS = ((-7.5, 17.1), (7.4, 17.05))      # GND, VIN
CORNER = (2.0, 1.2)                         # pad side, drill
# BAT and GND: the pad's center and size, then the slot's 2 holes as
# (x, radius) on the line y.
SLOTS = (((-2.62, -16.34), (4.1, 3.05), -16.35,
          ((-2.57, 0.93), (-1.26, 0.65))),
         ((2.34, -16.31), (4.07, 3.0), -16.4,
          ((1.2, 0.65), (2.33, 0.93))))
USB = (-0.38, 13.45)            # the shell's rear end
USB_LEN = 6.35
USB_PINS = (-2.67, -1.53, -0.52, 0.52, 1.53, 2.67)
INDUCTOR = (-5.3, 6.45, 6.5, 6.7, 2.7)
INDUCTOR_PADS = ((-4.6, 10.52, 3.2, 1.05), (-4.6, 2.7, 3.2, 0.8))
DIODE = (-5.32, -1.92)
BIG_CAPS = ((-5.2, -7.0), (-5.2, -9.47), (-5.2, -11.94))
SMALL_CAPS = ((6.3, 10.1), (6.3, 8.0), (6.3, 5.9))
SHUNT = (1.43, 9.35)
SPARE_PADS = ((-0.08, 6.9, 1.65, 2.0), (2.75, 6.9, 1.6, 1.95))
CHIPS = ((3.06, 1.92), (3.06, -4.1))
RESISTORS = ((1.2, -8.25), (1.2, -10.5))
LEDS = ((5.05, -8.27), (5.1, -10.5))
TOP_PRINT = (("GND", -7.31, 15.12, 1.1, 2.5, 180.0),
             ("VIN", 7.37, 14.97, 1.4, 2.4, 180.0),
             ("RCS", 1.44, 10.94, 0.9, 2.4),
             ("OK", 7.72, -8.1, 1.15, 1.8, -90.0),
             ("CR.", 7.7, -10.65, 1.15, 2.1, -90.0),
             ("DDTCCRUB", 0.6, -13.28, 0.95, 5.45),
             ("BAT", -6.78, -14.1, 1.2, 2.7),
             ("GND", 6.69, -14.1, 1.2, 2.75))
# The underside, places in the top side's frame; a line's turn is as
# the underside is seen, the board turned over about y.
UNDER_PRINT = (("Lithium battery charger", -6.9, -1.0, 1.3, 25.2, 90.0),
               ("GND", 2.61, -14.03, 1.3, 3.4),
               ("BAT", -2.47, -14.03, 1.3, 3.3),
               ("VIN", 7.2, 15.0, 1.4, 2.5, 180.0),
               ("GND", -6.56, 15.3, 1.4, 3.3, 180.0),
               ("3-6V", 1.53, 15.0, 1.2, 3.8, 180.0),
               ("4S", 5.72, 5.17, 1.1, 1.9, -90.0),
               ("3S", 5.72, 1.2, 1.1, 1.9, -90.0),
               ("2S", 5.72, -2.88, 1.1, 1.9, -90.0),
               ("4A", 0.69, 5.17, 1.1, 1.9, 90.0),
               ("2A", 0.69, 1.17, 1.1, 1.9, 90.0),
               ("1A", 0.69, -2.83, 1.1, 1.9, 90.0))
# Solder jumpers: center and whether solder closes it.
JUMPERS = ((7.94, 5.17, True), (7.94, 1.22, False), (7.94, -2.88, False),
           (2.96, 5.17, False), (2.96, 1.17, False), (2.96, -2.83, True))
LEG_PADS = ((-5.01, 17.3), (4.25, 17.3), (-5.01, 13.45), (4.25, 13.45))


def top(shapes, x, y, turn=0.0, z=0.0):
    """Shapes put on the top side, z over its surface."""
    return s.place(shapes, x, y, T + z, turn)


def under(shapes, x, y, turn=0.0):
    """Shapes put on the underside, turned over about y."""
    return s.place(shapes, x, y, 0.0, turn, flip=True)


def words(lines, below=False, rgb=p.WHITE, z=0.0):
    """Lines of print.

    Args:
        lines: (text, x, y, height, width) or (text, x, y, height, width,
            turn) of each line, (x, y) its center.
        below: Whether the print is on the underside.
        rgb: The print's colour.
        z: The print's height over the top side.
    """
    out = []
    for string, x, y, height, width, *turn in lines:
        shapes = p.text(string, height, rgb, width)
        if below:
            out += under(shapes, x, y, *turn)
        else:
            out += top(shapes, x, y, *turn, z=z)
    return out


def figure8(holes, y, grow=0.0):
    """The outline of 2 overlapping round holes on the line y.

    Args:
        holes: (x, radius) of each hole, the left one first.
        y: The holes' line.
        grow: Added to each radius.
    """
    (c1, r1), (c2, r2) = holes
    r1, r2 = r1 + grow, r2 + grow
    d = c2 - c1
    a = (d * d + r1 * r1 - r2 * r2) / (2 * d)
    h = math.sqrt(r1 * r1 - a * a)
    out = []
    for cx, r, start, end in (
            (c1, r1, math.atan2(h, a), 2 * math.pi - math.atan2(h, a)),
            (c2, r2, math.atan2(-h, a - d), math.atan2(h, a - d))):
        for k in range(3):
            t = start + (end - start) * k / 3
            out.append((cx + r * math.cos(t), y + r * math.sin(t),
                        (cx, y, 1)))
    return out


def outline():
    """The board's outline, corners rounded 0.3."""
    return s.rounded(-W / 2, -H / 2, W / 2, H / 2, 0.3)


def board():
    """The plate and the plating of its pads with holes."""
    holes = [s.circle(x, y, HOLE / 2) for x, y in HOLES]
    holes += [s.circle(x, y, CORNER[1] / 2) for x, y in CORNERS]
    holes += [figure8(slot, y) for _c, _size, y, slot in SLOTS]
    out = [s.ext(outline(), 0.0, T, GREEN, holes=holes)]
    for x, y in CORNERS:
        out += s.place(p.hole_pad(CORNER[1], CORNER[0], T, p.TIN,
                                  square=True), x, y)
    for (x, y), (w, h), sy, slot in SLOTS:
        pad = s.rect(x - w / 2, y - h / 2, x + w / 2, y + h / 2)
        hole = figure8(slot, sy)
        out += [s.ext(pad, T, T + p.PAD, p.TIN, holes=[hole]),
                s.ext(pad, -p.PAD, 0.0, p.TIN, holes=[hole]),
                s.ext(hole, 0.0, T, p.TIN,
                      holes=[figure8(slot, sy, -p.PAD)])]
    return out


def usb_c6():
    """The USB-C socket with 6 pins; the plug enters from -y.

    The origin is the center of the shell's rear end on the board, as
    _parts.usb_c has it: the same shell section, 8.94 wide and 3.16
    high, 0.09 over the board, USB_LEN long. Pins and legs stand on
    their pads.
    """
    w, z0, z1, cap = 4.47, 0.09, 3.25, 0.97
    out = [
        s.ext(s.rounded(-w, z0, w, z1, 1.28), cap, USB_LEN, p.STEEL,
              holes=[s.rounded(-w + 0.3, z0 + 0.3, w - 0.3, z1 - 0.3,
                               0.98)], frame=s.Y),
        s.ext(s.rounded(-w, z0, w, z1, 1.28), 0.0, cap, p.STEEL,
              frame=s.Y),
        s.box(-3.35, -USB_LEN + 0.42, 3.35, -cap, 1.36, 1.98, p.BLACK),
    ]
    for side in (-1, 1):
        a, b = sorted((side * w, side * (w + 0.32)))
        pa, pb = sorted((side * (w - 0.25), side * (w + 0.55)))
        for y0, y1 in ((-0.45, 0.45), (-4.25, -3.45)):
            out.append(s.box(a, y0, b, y1, p.PAD, T, p.STEEL))
            out.append(s.box(pa, y0 - 0.4, pb, y1 + 0.4, 0.0, p.PAD,
                             p.TIN))
    for x in USB_PINS:
        out.append(s.box(x - 0.15, 0.0, x + 0.15, 1.25, p.PAD,
                         p.PAD + 0.15, p.TIN))
        out.append(s.box(x - 0.3, -0.15, x + 0.3, 1.45, 0.0, p.PAD,
                         p.TIN))
    return out


def sop8():
    """SOP-8, 1.4 high, with the 0.8 of each pad past the leads."""
    out = p.leaded(3.9, 4.9, 1.4, p.rows(4, 1.27), p.rows(4, 1.27), 6.0,
                   0.4)
    for y in p.rows(4, 1.27):
        for x in (-3.4, 3.4):
            out += s.place(p.pad(0.8, 0.6), x, y)
    return out


def top_side():
    """The parts and the print of the top side."""
    out = top(usb_c6(), *USB, 180.0)
    x, y, w, h, high = INDUCTOR
    out += top(p.block(w, h, high, FERRITE, 0.4), x, y)
    out += words([("100", x, y - 0.15, 2.2, 3.9, -90.0)], rgb=INK, z=high)
    for x, y, w, h in INDUCTOR_PADS + SPARE_PADS:
        out += top(p.pad(w, h), x, y)
    out += top(p.sod(4.3, 3.6, 2.2, 5.4, 2.0), *DIODE, 90.0)
    for x, y in BIG_CAPS:
        out += top(p.chip(3.2, 1.6, 1.2, p.TAN), x, y)
    for x, y in SMALL_CAPS:
        out += top(p.c0805(), x, y)
    out += top(p.chip(3.1, 1.6, 0.55, p.EPOXY), *SHUNT)
    out += words([("R100", SHUNT[0], SHUNT[1], 0.8, 1.6)], z=0.55)
    for x, y in CHIPS:
        out += top(sop8(), x, y)
    for x, y in RESISTORS:
        out += top(p.r0805(), x, y)
        out += words([("222", x, y, 0.75, 1.0)], z=0.5)
    for x, y in LEDS:
        out += top(p.chip(2.0, 1.25, 0.75, p.LENS), x, y)
    return out + words(TOP_PRINT)


def underside():
    """The pads of the socket's legs, the jumpers and the print."""
    out = []
    for x, y in LEG_PADS:
        out += under(p.pad(1.2, 1.6), x, y)
        out += under(s.place(p.pad(0.8, 1.2, p.TIN, 0.3), z=p.PAD), x, y)
    for x, y, closed in JUMPERS:
        for dx in (-0.475, 0.475):
            out += under(p.pad(0.75, 1.7), x + dx, y)
        if closed:
            out += under(s.place(p.pad(1.7, 1.6, p.TIN, 0.3), z=p.PAD),
                         x, y)
    return out + words(UNDER_PRINT, below=True)


def parts(_colors):
    """The model of the bare board.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items(board() + top_side() + underside())}
