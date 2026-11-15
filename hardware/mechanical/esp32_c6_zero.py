# SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""Waveshare ESP32-C6-Zero, part by part.

The board is the one of hardware/kicad/modules/esp32_c6_zero.py;
outline and pads come from that spec. This model adds the half holes
in the edges, the copper of the 18 edge pads and the 7 underside pads,
the USB socket and the slots its shell stands in, the chip, the
antenna, the 2 push buttons, the LED, the regulator, the diode, the
crystal, 21 chip parts and the print of both sides.

Sources:
    D: the maker's dimension drawing: outline, pads.
    M: the maker's STEP model ESP32-C6-Zero_asm.stp (not in the
    repository; it states no licence), measured with
    hardware/mechanical/_measure_step.py: the USB socket's shell ends
    1.21 past the board's edge and stands 3.25 high; the buttons are
    4.1 x 2.5 and 1.4 high, their caps 1.5 square and 1.75 high; the
    antenna is 1.0 high.
    P: two owner photos on 5mm squared paper (top side, underside; not
    in the repository). Positions are in mm from the board's centre,
    seen from the top side with the USB socket up, within about 0.3
    for the chip parts. The camera stands 171 over the paper and 63
    from the board, so a part's top lies 0.37 further from the camera
    per mm of height; the chip, the buttons and the crystal are set
    back by that. The print's letters are the font of _font.py set to
    the height and width each line has in the photos, not traced.

Packages by their look in P: chip QFN 5 x 5 with 40 leads, regulator
SOT-23-5, chip parts 0402, LED 2.0 x 2.0; their heights are the
packages' nominal ones. M shows 10 chip parts, P shows more; the model
draws the 21 that P resolves.

Not measured, estimated: the half holes' diameter (0.9), the slots'
size (0.7 x 2.0 and 0.7 x 1.4), the crystal's height (0.6). Not drawn:
the tracks, the frames printed round the chip parts, the maker's name
and the pad names on the underside, about 6 chip parts beside the USB
socket that P does not resolve.

Frame: as the KiCad model of the module. Origin at the 5V pad's hole
on the board's underside, x towards the TX pad, y towards the USB
socket, z up. All sizes are mm.
"""

import _board
import _parts as p
import _solid as s

kicad = _board.kicad_spec("esp32_c6_zero")
STEM = "esp32_c6_zero"
BLUE = (0.07, 0.17, 0.42)
CERAMIC = (0.72, 0.45, 0.42)
B = _board.Board(kicad)
HX, HY = kicad.W / 2, kicad.H / 2

ROWS = [kicad.FIRST + 2.54 * i - HY for i in range(9)]
# Edge pad: its length from the edge, its width, the half hole's
# radius, the hole's diameter.
PAD_LEN, PAD_W, NOTCH, DRILL = 2.2, 1.6, 0.45, 1.0
UNDER_PADS = [(kicad.UNDER_X - HX,
               kicad.UNDER_Y0 - kicad.UNDER_PITCH * i - HY)
              for i in range(7)]
# USB socket: y of the shell's rear end; the slots of its 4 legs as
# (y, length), 0.7 wide at x = -4.63 and 4.63.
USB_Y = -5.61
SLOTS = ((-6.24, 2.0), (-10.38, 1.4))
SLOT_X, SLOT_W, LEG = 4.63, 0.7, 0.9
CHIP = (-1.1, 2.17)
ANTENNA = (-0.03, 10.14)
BUTTONS = ((3.95, -2.09), (-4.04, -2.09))
LED = (-0.11, -2.19)
REGULATOR = (4.28, 5.04)
DIODE = (5.68, 1.43)
CRYSTAL = (-5.36, 2.61)
CAPS_FLAT = ((5.24, 7.61), (3.31, 7.61))
CAPS_UPRIGHT = ((-4.81, 7.07), (-6.05, 7.07), (-4.81, 5.04), (-6.05, 5.04),
                (4.15, 2.71), (4.1, 0.63), (-4.66, 0.44))
RES_FLAT = ((1.28, 7.61), (-0.53, 7.61), (1.28, 6.33), (-0.53, 6.33),
            (-2.75, 8.65), (-2.78, 5.78))
RES_UPRIGHT = ((-3.06, 7.32), (2.96, 2.71), (2.86, 0.63), (-6.0, 0.44),
               (5.81, -4.61), (-5.8, -4.46))
# Print at the pads, from the USB socket: the left and the right edge.
LEFT = ("5V", "GND", "3V3", "0", "1", "2", "3", "4", "5")
RIGHT = ("TX", "RX", "14", "15", "18", "19", "20", "21", "22")
LETTER, LETTER_W = 0.7, 0.55
TOP_PRINT = (("BOOT", 3.7, -3.97, 0.8, 2.8), ("RGB", 0.14, -3.97, 0.8, 2.2),
             ("RST", -4.12, -3.97, 0.8, 2.1))
UNDER_PRINT = (("ESP32-C6-Zero", 0.0, 10.1, 1.0, 9.6),)


def _outline():
    """The board's outline with a half hole at each edge pad."""
    x, y, r, n = HX, HY, kicad.RADIUS, NOTCH
    ox, oy = B.at(0.0, 0.0)

    def put(q):
        point = (ox + q[0], oy + q[1])
        if len(q) == 2:
            return point
        return (*point, (ox + q[2][0], oy + q[2][1], q[2][2]))

    out = [(-x + r, -y), (x - r, -y, (x - r, -y + r, 1)), (x, -y + r)]
    for row in sorted(-v for v in ROWS):
        out += [(x, row - n, (x, row, -1)), (x - n, row, (x, row, -1)),
                (x, row + n)]
    out += [(x, y - r, (x - r, y - r, 1)), (x - r, y),
            (-x + r, y, (-x + r, y - r, 1)), (-x, y - r)]
    for row in sorted((-v for v in ROWS), reverse=True):
        out += [(-x, row + n, (-x, row, -1)), (-x + n, row, (-x, row, -1)),
                (-x, row - n)]
    out += [(-x, -y + r, (-x + r, -y + r, 1))]
    return [put(q) for q in out]


def edge_pad():
    """An edge pad's copper: both sides and the hole's barrel.

    The origin is the middle of the half hole on the board's edge, on
    the underside; the pad runs along +x into the board.
    """
    h, n, hole = PAD_W / 2, NOTCH, kicad.INSET
    shape = [(0.0, -h), (PAD_LEN, -h), (PAD_LEN, h), (0.0, h),
             (0.0, n, (0.0, 0.0, -1)), (n, 0.0, (0.0, 0.0, -1)), (0.0, -n)]
    ring = [s.circle(hole, 0.0, DRILL / 2)]
    return [s.ext(shape, B.t, B.t + p.PAD, p.GOLD, holes=ring),
            s.ext(shape, -p.PAD, 0.0, p.GOLD, holes=ring),
            s.ext(s.circle(hole, 0.0, DRILL / 2), 0.0, B.t, p.GOLD,
                  holes=[s.circle(hole, 0.0, DRILL / 2 - p.PAD)])]


def _slot(x, y, length):
    """A slot's outline in the model, its length along y."""
    cx, cy = B.at(x, y)
    return s.stadium(cx - SLOT_W / 2, cy - length / 2, cx + SLOT_W / 2,
                     cy + length / 2)


def board():
    """The plate with holes, half holes and slots, and the pads."""
    holes = [s.circle(*B.at(side * (HX - kicad.INSET), y), DRILL / 2)
             for side in (-1, 1) for y in ROWS]
    holes += [_slot(side * SLOT_X, y, length) for side in (-1, 1)
              for y, length in SLOTS]
    out = [s.ext(_outline(), 0.0, B.t, BLUE, holes=holes)]
    for y in ROWS:
        out += B.through(edge_pad(), -HX, y)
        out += B.through(edge_pad(), HX, y, 180.0)
    for x, y in UNDER_PADS:
        out += B.under(p.pad(*kicad.UNDER_PAD, p.GOLD), x, y)
    return out


def usb():
    """The USB socket and the 4 legs of its shell in the slots."""
    out = B.top(p.usb_c(), 0.0, USB_Y, 180.0)
    for side in (-1, 1):
        for y, length in ((-6.24, 1.58), (-10.38, 0.8)):
            cx, cy = B.at(side * SLOT_X, y)
            out.append(s.box(cx - 0.16, cy - length / 2, cx + 0.16,
                             cy + length / 2, B.t - LEG, B.t, p.STEEL))
    return out


def antenna():
    """The ceramic antenna: 7.45 x 2.0, 1.0 high, ends tinned."""
    return [s.box(-3.72, -1.0, -2.87, 1.0, 0.0, 1.0, p.TIN),
            s.box(-2.87, -1.0, 2.87, 1.0, 0.0, 1.0, CERAMIC),
            s.box(2.87, -1.0, 3.72, 1.0, 0.0, 1.0, p.TIN)]


def labels():
    """The names printed at the edge pads."""
    lines = []
    for side, names in ((-1, LEFT), (1, RIGHT)):
        for y, name in zip(ROWS, names, strict=True):
            width = LETTER_W * len(name)
            if y == ROWS[0]:
                lines.append((name, side * 6.15, y, LETTER, width))
            else:
                lines.append((name, side * 7.45, y - 1.27, LETTER, width))
    return lines


def top_side():
    """The parts and the print of the top side."""
    out = usb() + B.top(antenna(), *ANTENNA)
    out += B.top(p.qfn(5.0, 5.0, 0.85, 10, 10, 0.4), *CHIP)
    for x, y in BUTTONS:
        out += B.top(p.tact_switch(4.1, 2.4, 1.4, 1.5, 1.75), x, y)
    out += B.top(p.block(2.0, 2.0, 0.85, p.WHITE, 0.2), *LED)
    out += B.top(p.sot23(3, 2), *REGULATOR, 180.0)
    out += B.top(p.sod(2.7, 1.3, 1.0, 3.5, 0.9), *DIODE, 90.0)
    out += B.top(p.can(2.0, 1.5, 0.6, p.CREAM), *CRYSTAL, 90.0)
    for places, part, turn in ((CAPS_FLAT, p.c0402, 0.0),
                               (CAPS_UPRIGHT, p.c0402, 90.0),
                               (RES_FLAT, p.r0402, 0.0),
                               (RES_UPRIGHT, p.r0402, 90.0)):
        for x, y in places:
            out += B.top(part(), x, y, turn)
    return out + B.words(list(TOP_PRINT) + labels())


def kicad_model():
    """The model of the KiCad footprint: the board lying flat."""
    return s.items(board() + top_side()
                   + B.words(UNDER_PRINT, under=True))


def parts(_colors):
    """The model of the board.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: kicad_model()}
