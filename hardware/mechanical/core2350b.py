"""Waveshare Core2350B and its USB adapter board, part by part.

The Core2350B is the board of hardware/kicad/modules/core2350b.py;
outline, holes and the place of its main parts come from that spec.
This model adds the copper of the 64 plated holes (6 GND pads
octagonal, as P shows them), the RP2350B, the regulator, the
inductor, the crystal, the LED, the flat cable socket, 4 chip parts
on the top side, the flash, the PSRAM, their 16 tinned pads and 29
chip parts on the underside, and the underside's print.

The USB adapter board carries the USB Type-C socket, a flat cable
socket for the 6-way cable to the Core2350B, the BOOT and RESET push
buttons, the power LED, 5 chip parts, 4 mounting holes and its print.

Sources:
    D: Waveshare's dimension drawing of the Core2350B (see the KiCad
    spec): board 25.40 x 25.40mm, 64 holes at 2.54mm.
    D2: Waveshare's dimension drawing of the RP2350-Tiny with its USB
    adapter board, 800px-RP2350-Tiny-details-size.jpg (waveshare.com/
    wiki/RP2350-Tiny; not in the repository): adapter 18.00 x 18.00mm,
    corner radius 1.00mm, hole centers 2.00mm from the edges and
    14.00mm apart.
    M: Waveshare's STEP model of that adapter board, RP2040-Tiny-Adapter
    V1.1.step from RP2040-Tiny-Adapter-Step_V1.1.zip (waveshare.com/
    wiki/RP2040-Tiny; not in the repository, it states no licence),
    read with hardware/mechanical/_measure_step.py's ``measure``:
    board 18.02 x 18.02 x 1.00mm, holes 2.02mm across at (+-7.0,
    +-7.0); USB Type-C shell 3.25mm over the board, its front 0.03mm
    inside the edge, 2 slots 0.5mm wide and 2 holes 0.76mm for its
    legs; push buttons 4.65 x 3.00mm, 2.50mm high; flat cable socket
    5.70 x 3.60mm, 1.90mm high; 5 chip parts 1.6 x 0.8mm, 0.38 to
    0.60mm high; positions of all of them.
    P: Waveshare's product photos of the Core2350B, top side and
    underside (see the KiCad spec), within about 0.2mm, and of its
    adapter board (Core2350B0-50.jpg, rectified by its 4 holes). P
    shows the Core2350B's adapter with D2's and M's layout turned by
    180 degrees, with the flat cable socket at the edge opposite the
    USB socket, BOOT left of RESET, and a socket of 6 contacts. P's
    underside is the B0 variant, without PSRAM.
    U: Waveshare's photo d-Core2350B0-details-intro.jpg (waveshare.com/
    wiki/Core2350B0; not in the repository), 960 x 682px, right board:
    the underside with the PSRAM fitted, 15.3px per mm. Its holes fit
    a 2.54mm grid with 0.34px (0.02mm) RMS (root mean square) error
    over 61 holes. U gives the underside: the flash, the PSRAM, their
    pads, the chip parts and the print. Each was read off a mm grid
    laid on that fit and checked against P's underside (31.9px per mm,
    44 holes, 0.64px RMS error), within about 0.2mm; the two agree
    within 0.15mm.

    U and P show the underside turned by 90 degrees against a plain
    flip of the top side: the top side's grid place (column c, row r,
    both 0 to 9 from the top left) is U's (9 - r, 9 - c). The inner
    GND pads at row 1, columns 3 and 6 of the top side lie at column 8,
    rows 3 and 6 in U, and the chamfered corner moves from the top left
    to the bottom right. In mm from the board's center, U's x is the
    specs' -y and U's y is their -x.

Packages by their look in P and U: RP2350B QFN 10 x 10mm with 80
leads at 0.4mm; regulator SOT-23-5; flash SOIC-8 (small outline, 8
leads), 5.3mm body, 7.9mm over the leads, as S's part W25Q128JVSIQ
is; PSRAM SOP-8 (small outline package, 8 leads), 150mil: body 3.9 x
4.9mm, 6.0mm over the leads; crystal 2.5 x 2.0mm; LED 0603; chip parts
0402 and 0603. Their heights are the packages' nominal ones. The
flash and the PSRAM sit on the same footprint: 4 tinned pads a side,
0.6 x 2.25mm, from 2.25 to 4.5mm off the chip's center, long enough
for both packages; P's empty PSRAM place shows it.

S does not name the PSRAM. Assumed: AP Memory APS1604M-3SQR-SN (2MB,
B1 variant) or APS6404L-3SQR-SN (8MB, B2), both SOP-8 150mil; their
sizes are those of JEDEC (Joint Electron Device Engineering Council)
outline MS-012 above. Which of the two U shows is unknown; the package
is the same.

Estimated, not measured: the Core2350B's thickness (1.6mm), its corner
radius (0.5mm), its hole diameter (1.0mm) and ring (1.6mm); the
inductor's height (1.0mm); the crystal's height (0.55mm); the flash's
height (1.9mm); the PSRAM's part number and height (1.5mm); the pads'
inner ends under the flash, which U does not show; the flat cable
socket's height on the Core2350B (taken as M's 1.90mm on the adapter)
and how it splits into housing and lock; the push buttons' body (3.6 x
3.0mm, 1.5mm high) and cap (2.4mm across) within M's box.

Not drawn: tracks and vias, the names printed beside the holes, the
logo on the top side, the frames printed round parts, the flat cable,
the adapter's underside, its print other than BOOT, RESET and PWR.

Frames: the Core2350B as the KiCad model of the module: origin at pad
1's hole on the board's underside, x towards P2's pin 1 (the GND corner
at the top right), y away from P3 (towards the top edge), z up; seen
from the top side the flat cable socket is at the top right. The
adapter: origin at the board's center on its underside, x towards
RESET, y towards the flat cable socket, z up.
All sizes are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import math

import _board
import _parts as p
import _solid as s

kicad = _board.kicad_spec("core2350b")
STEM = "core2350b"
ADAPTER = "core2350b_usb_adapter"
BLUE = (0.07, 0.17, 0.42)
LOCK = (0.20, 0.20, 0.22)
B = _board.Board(kicad)
HX, HY = kicad.W / 2, kicad.H / 2
RING = 1.6                 # the board's pad rings across
# The places of the GND pads, drawn octagonal.
OCTAGONS = [kicad.place(h, n) for h, names in enumerate(kicad.HEADERS, 1)
            for n, name in enumerate(names, 1) if name == "GND"]

# Top side, from the board's center, y down (P).
CAPS_TOP = (((-6.6, -6.75), p.c0402), ((-6.6, -5.8), p.c0603),
            ((-6.6, -4.85), p.r0402), ((-0.17, -6.92), p.c0603))
# Underside, as seen through the board from the top (U), each with its
# turn: 0 along x, 90 along y. In U: the row of 14 under "Waveshare",
# the column of 5 on the left, the 5 right of the flash, the row of 5
# under the chips.
UNDER_PARTS = (
    [((4.87, y), p.c0402, 0.0) for y in (7.13, 6.34, 5.56, 3.73, 2.51,
                                         1.72, -0.28, -0.98, -1.93, -2.80,
                                         -3.68, -4.46, -5.33)]
    + [((4.87, 0.68), p.c0603, 0.0)]
    + [((x, 7.15), p.c0402, 0.0) for x in (2.82, 1.42, 0.06, -2.85, -4.20)]
    + [((x, y), p.c0402, 0.0) for x, y in ((1.78, -4.72), (1.78, -5.52),
                                           (-3.73, -4.78), (-3.73, -5.56))]
    + [((-2.54, -5.25), p.c0603, 90.0)]
    + [((-6.75, y), p.r0402, 90.0) for y in (4.78, 3.33, 1.95, 0.61,
                                             -0.85)])
UNDER_PRINT = (("Waveshare", 6.7, -0.2, 1.0, 8.3, 90.0),
               ("Core2350B", 0.1, -7.0, 1.13, 9.2, 0.0))
# The chips' footprint: pad length and width, and the pads' centers off
# the chip's center along x (U).
CHIP_PAD = (2.25, 0.6)
CHIP_PAD_AT = 3.375
FFC_H, LOCK_H = 1.9, 1.4


def octagon(cx, cy, flat):
    """A regular octagon, ``flat`` across its flats, counter-clockwise."""
    r = flat / 2 / math.cos(math.pi / 8)
    return [(cx + r * math.cos(math.pi / 8 + k * math.pi / 4),
             cy + r * math.sin(math.pi / 8 + k * math.pi / 4))
            for k in range(8)]


def ring(octagonal):
    """The copper of a plated hole: a pad on each side and the barrel.

    The origin is the hole's center on the board's underside.
    """
    if not octagonal:
        return p.hole_pad(kicad.DRILL, RING, B.t)
    d = kicad.DRILL / 2
    hole = [s.circle(0.0, 0.0, d)]
    shape = octagon(0.0, 0.0, RING)
    return [s.ext(shape, B.t, B.t + p.PAD, p.GOLD, holes=hole),
            s.ext(shape, -p.PAD, 0.0, p.GOLD, holes=hole),
            s.ext(s.circle(0.0, 0.0, d), 0.0, B.t, p.GOLD,
                  holes=[s.circle(0.0, 0.0, d - p.PAD)])]


def places():
    """Every hole's grid place and its (x, y) from the board's center."""
    out = []
    for header in range(1, 5):
        for pin in range(1, 17):
            col, row = kicad.place(header, pin)
            x, y = kicad.grid(col, row)
            out.append(((col, row), (x - HX, y - HY)))
    return out


def board():
    """The plate with its 64 holes, and the copper of each."""
    holes = [(x, y, kicad.DRILL) for _cr, (x, y) in places()]
    out = B.plate(BLUE, B.outline(kicad.RADIUS), holes)
    for cr, (x, y) in places():
        out += B.through(ring(cr in OCTAGONS), x, y)
    return out


def ffc_socket(width, body, lock_width, lock, contacts, height, lock_h):
    """A flip-lock flat cable socket; the cable enters from -y.

    The origin is the center of the socket's rear edge, where the
    contacts' tails leave it.

    Args:
        width: The housing's size along x.
        body: The housing's depth along y.
        lock_width: The lock's size along x.
        lock: The lock's depth along y, in front of the housing.
        contacts: Number of contacts, 0.5 apart.
        height: The housing's height.
        lock_h: The lock's height.
    """
    x, lx = width / 2, lock_width / 2
    out = [s.box(-x, -body, x, 0.0, 0.0, height, p.WHITE),
           s.box(-lx, -body - lock, lx, -body, 0.0, lock_h, LOCK)]
    for c in p.rows(contacts, 0.5):
        out.append(s.box(c - 0.12, 0.0, c + 0.12, 0.4, 0.0, 0.15, p.GOLD))
    return out


def top_side():
    """The parts of the top side."""
    (mx, my), (mw, _mh) = kicad.MCU
    out = B.top(p.qfn(mw - 0.24, mw - 0.24, 0.85, 20, 20, 0.4), mx, my)
    out += B.top(p.sot23(3, 2), *kicad.REGULATOR[0])
    (ix, iy), (iw, ih) = kicad.INDUCTOR
    out += B.top(p.block(iw, ih, 1.0, p.EPOXY, 0.15), ix, iy)
    (cx, cy), (cw, ch) = kicad.CRYSTAL
    out += B.top(p.can(cw, ch, 0.55, p.STEEL), cx, cy)
    out += B.top(p.led0603(), *kicad.LED[0])
    for (x, y), part in CAPS_TOP:
        out += B.top(part(), x, y)
    x0, y0, x1, y1 = kicad.FFC
    lx0, _ly0, lx1, ly1 = kicad.FFC_LOCK
    socket = ffc_socket(x1 - x0, y1 - y0, lx1 - lx0, ly1 - y1, 6, FFC_H,
                        LOCK_H)
    # The socket's rear edge, where its tails leave, is the edge away
    # from the RP2350B; the cable enters from the RP2350B's side.
    out += B.top(socket, (x0 + x1) / 2, y0)
    return out


def soic8_wide():
    """SOIC-8 of 5.3mm body, 1.9 high, leads 1.27 apart, 7.9 span."""
    return p.leaded(5.3, 5.3, 1.9, p.rows(4, 1.27), p.rows(4, 1.27), 7.9,
                    0.4)


def underside():
    """The parts and the print of the underside.

    The chips stand on their pads.
    """
    out = []
    for part, chip in ((kicad.FLASH, soic8_wide()), (kicad.PSRAM,
                                                     p.soic8())):
        x, y = part[0]
        out += B.under(s.place(chip, z=p.PAD), x, y)
        for dx in (-CHIP_PAD_AT, CHIP_PAD_AT):
            for dy in p.rows(4, 1.27):
                out += B.under(p.pad(*CHIP_PAD), x + dx, y + dy)
    for (x, y), part, turn in UNDER_PARTS:
        out += B.under(part(), x, y, turn)
    return out + B.words(UNDER_PRINT, under=True)


def bare():
    """The board lying flat, its underside at z = 0."""
    return board() + top_side() + underside()


def kicad_model():
    """The board on four 2 x 8 pin headers, the model of the footprint.

    Returns:
        The solids: the headers' plastic stands on z = 0, the board's
        underside pads lie on it, 2.5 over the carrier.
    """
    rows = []
    for header in range(1, 5):
        for inner in (0, 1):
            first = kicad.grid(*kicad.place(header, 1 + inner))
            last = kicad.grid(*kicad.place(header, 15 + inner))
            x, y = (first[0] + last[0]) / 2 - HX, (first[1] + last[1]) / 2 - HY
            turn = 90.0 if header in (1, 4) else 0.0
            rows += B.through(p.pin_header(8, below=6.0, above=3.0), x, y,
                              turn)
    return s.items(s.place(bare(), z=2.5 + p.PAD) + rows)


# The adapter board, in its own frame (y towards the flat cable socket).
AD_W, AD_T, AD_R = 18.0, 1.0, 1.0
AD_HOLE, AD_HOLE_AT = 2.0, 7.0
USB_REAR = -1.63
USB_SLOTS = ((-2.82, -1.62), (-6.8, -6.0))
USB_SLOT_X, USB_SLOT_W = 4.33, 0.5
USB_PEGS = ((-2.89, -2.72), (2.89, -2.72))
USB_PEG = 0.76
BUTTONS = (("BOOT", -5.5), ("RESET", 5.5))
BUTTON_Y = 1.25
AD_FFC = (-2.875, 5.03, 2.825, 8.63)
AD_PARTS = (((6.9, -2.0), p.led0603, 0.0), ((6.9, -3.5), p.r0603, 0.0),
            ((1.74, 1.29), p.r0603, 90.0), ((-1.76, 1.29), p.r0603, 90.0),
            ((-6.76, -3.5), lambda: p.chip(1.6, 0.8, 0.6, p.TAN), 0.0),
            ((-6.76, -1.87), lambda: p.chip(1.6, 0.8, 0.6, p.TAN), 0.0))
AD_PRINT = (("BOOT", -5.4, 3.6, 0.9, 2.8, 180.0),
            ("RESET", 5.6, 3.6, 0.9, 3.6, 180.0),
            ("PWR", 8.25, -4.6, 0.8, 2.0, -90.0))


def push_button():
    """A push button pressed from above: body, round cap, 4 legs."""
    out = [s.box(-1.8, -1.5, 1.8, 1.5, 0.0, 1.5, p.CREAM),
           s.ext(s.circle(0.0, 0.0, 1.2), 1.5, 2.5, p.WHITE)]
    for sx in (-1, 1):
        a, b = sorted((sx * 1.8, sx * 2.325))
        for y in (-0.9, 0.9):
            out.append(s.box(a, y - 0.4, b, y + 0.4, 0.0, 0.3, p.TIN))
    return out


def adapter():
    """The USB adapter board, lying flat, its underside at z = 0."""
    h = AD_W / 2
    holes = [s.circle(sx * AD_HOLE_AT, sy * AD_HOLE_AT, AD_HOLE / 2)
             for sx in (-1, 1) for sy in (-1, 1)]
    holes += [s.stadium(sx * USB_SLOT_X - USB_SLOT_W / 2, y0 - USB_SLOT_W / 2,
                        sx * USB_SLOT_X + USB_SLOT_W / 2, y1 + USB_SLOT_W / 2)
              for sx in (-1, 1) for y0, y1 in USB_SLOTS]
    holes += [s.circle(x, y, USB_PEG / 2) for x, y in USB_PEGS]
    out = [s.ext(s.rounded(-h, -h, h, h, AD_R), 0.0, AD_T, BLUE,
                 holes=holes)]
    out += s.place(p.usb_c(), 0.0, USB_REAR, AD_T)
    for _name, x in BUTTONS:
        out += s.place(push_button(), x, BUTTON_Y, AD_T)
    x0, y0, x1, y1 = AD_FFC
    socket = ffc_socket(x1 - x0, 2.0, 4.6, y1 - y0 - 2.4, 6, FFC_H, LOCK_H)
    out += s.place(socket, (x0 + x1) / 2, y0 + 0.4, AD_T, 180.0)
    for (x, y), part, turn in AD_PARTS:
        out += s.place(part(), x, y, AD_T, turn)
    for text, x, y, height, width, turn in AD_PRINT:
        out += s.place(p.text(text, height, width=width), x, y, AD_T, turn)
    return out


def parts(_colors):
    """The models of the Core2350B and of its USB adapter board.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items(bare()), ADAPTER: s.items(adapter())}
