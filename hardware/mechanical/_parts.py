"""Electronic parts as shapes for _solid.py.

Every function returns shapes in the part's own frame: the origin is
the centre of the part's footprint on the board, z = 0 is the board's
surface and +z points away from it. All sizes are mm. Package sizes are
the packages' nominal ones, not measured on a board.
"""

import math

from _font import GLYPHS, SPACE
from _solid import (
    X, Y, box, circle, ext, pipe, place, rect, rounded, stadium,
)

TIN = (0.80, 0.80, 0.78)
STEEL = (0.72, 0.72, 0.72)
GOLD = (0.83, 0.65, 0.22)
BLACK = (0.07, 0.07, 0.07)
EPOXY = (0.13, 0.13, 0.14)
TAN = (0.70, 0.58, 0.42)
CREAM = (0.92, 0.89, 0.80)
WHITE = (0.95, 0.95, 0.93)
GREY = (0.45, 0.45, 0.45)
LENS = (0.95, 0.90, 0.60)
YELLOW = (0.95, 0.75, 0.10)
COPPER = (0.80, 0.48, 0.28)
BRONZE = (0.66, 0.42, 0.24)
ORANGE = (0.85, 0.45, 0.12)
RED = (0.75, 0.08, 0.06)
FOIL = (0.80, 0.81, 0.83)
KAPTON = (0.85, 0.62, 0.10)
PRINT = 0.02            # thickness of print on a board
PAD = 0.035             # thickness of a board's copper


def rows(n, pitch):
    """Positions of n things at ``pitch``, centred on 0."""
    return [(i - (n - 1) / 2) * pitch for i in range(n)]


def chip(length, width, height, rgb):
    """A chip part with a tinned end cap at each end of its length (x)."""
    cap, x, y = 0.22 * length, length / 2, width / 2
    return [box(-x, -y, -x + cap, y, 0.0, height, TIN),
            box(-x + cap, -y, x - cap, y, 0.0, height, rgb),
            box(x - cap, -y, x, y, 0.0, height, TIN)]


def r0402():
    return chip(1.0, 0.5, 0.35, EPOXY)


def c0402():
    return chip(1.0, 0.5, 0.5, TAN)


def c0201():
    return chip(0.6, 0.3, 0.3, TAN)


def r0201():
    return chip(0.6, 0.3, 0.3, EPOXY)


def r0603():
    return chip(1.6, 0.8, 0.4, EPOXY)


def c0603():
    return chip(1.6, 0.8, 0.8, TAN)


def c0805():
    return chip(2.0, 1.25, 0.85, TAN)


def led0603():
    return chip(1.6, 0.8, 0.6, LENS)


def lead(inner, tip, top, width, thick=0.15):
    """A gull-wing lead on the +x side at y = 0.

    Args:
        inner: x where the lead leaves the body.
        tip: x of the foot's end.
        top: Height of the lead's top where it leaves the body.
        width: The lead's width along y.
        thick: The lead's thickness.
    """
    knee = inner + 0.3 * (tip - inner)
    profile = [(inner, top - thick), (knee, top - thick), (knee, 0.0),
               (tip, 0.0), (tip, thick), (knee + thick, thick),
               (knee + thick, top), (inner, top)]
    return ext(profile, -width / 2, width / 2, TIN, frame=Y)


def leaded(body_x, body_y, height, left, right, span, lead_width):
    """A moulded package with gull-wing leads on its -x and +x sides.

    Args:
        body_x: The body's size along x.
        body_y: The body's size along y.
        height: The body's top above the board; the body stands 0.1 off.
        left: The leads' y positions on the -x side.
        right: The leads' y positions on the +x side.
        span: Tip to tip across x.
        lead_width: Each lead's width.
    """
    out = [box(-body_x / 2, -body_y / 2, body_x / 2, body_y / 2, 0.1,
               height, EPOXY)]
    one = [lead(body_x / 2, span / 2, 0.55 * height, lead_width)]
    for y in right:
        out += place(one, y=y)
    for y in left:
        out += place(one, y=y, turn=180.0)
    return out


def sot23(left, right):
    """SOT-23 family: 1.6 x 2.9 body, leads 0.95 apart, 2.8 span."""
    return leaded(1.6, 2.9, 1.1, rows(left, 1.9 if left == 2 else 0.95),
                  rows(right, 1.9 if right == 2 else 0.95), 2.8, 0.4)


def sod(body, width, height, span, lead_width):
    """A diode with one lead at each end of its length (x)."""
    return leaded(body, width, height, [0.0], [0.0], span, lead_width)


def qfn(size_x, size_y, height, n_x, n_y, pitch, rgb=EPOXY):
    """A leadless package; each pad shows as a 0.12 solder fillet.

    Args:
        size_x: The body's size along x.
        size_y: The body's size along y.
        height: The body's height.
        n_x: Pads on each of the two sides along x (at -y and +y).
        n_y: Pads on each of the two sides along y (at -x and +x).
        pitch: Pad pitch.
        rgb: Body colour.
    """
    x, y, pad, out = size_x / 2, size_y / 2, 0.5 * pitch, []
    out.append(box(-x, -y, x, y, 0.0, height, rgb))
    for p in rows(n_y, pitch):
        out.append(box(x, p - pad / 2, x + 0.12, p + pad / 2, 0.0, 0.25, TIN))
        out.append(box(-x - 0.12, p - pad / 2, -x, p + pad / 2, 0.0, 0.25,
                       TIN))
    for p in rows(n_x, pitch):
        out.append(box(p - pad / 2, y, p + pad / 2, y + 0.12, 0.0, 0.25, TIN))
        out.append(box(p - pad / 2, -y - 0.12, p + pad / 2, -y, 0.0, 0.25,
                       TIN))
    return out


def can(length, width, height, rgb):
    """A crystal: ceramic base 0.2 high and a lid 0.15 smaller all round."""
    x, y = length / 2, width / 2
    return [box(-x, -y, x, y, 0.0, 0.2, TAN),
            ext(rounded(-x + 0.15, -y + 0.15, x - 0.15, y - 0.15, 0.15),
                0.2, height, rgb)]


def block(length, width, height, rgb, radius=0.3):
    """A moulded block with rounded corners, an inductor."""
    return [ext(rounded(-length / 2, -width / 2, length / 2, width / 2,
                        radius), 0.0, height, rgb)]


def socket(pins, pitch, width, depth, height, lead_len, rear, wing=0.0,
           wing_height=0.0, wing_depth=0.0, rgb=CREAM):
    """A side-entry wire-to-board socket; the plug enters from -y.

    The origin is the centre of the socket's box, leads included.

    Args:
        pins: Number of pins.
        pitch: Pin pitch.
        width: Overall width along x, wings included.
        depth: Overall depth along y, leads included.
        height: Height of the housing.
        lead_len: Length of the solder leads behind the housing.
        rear: Depth of the housing's closed rear part.
        wing: Width of the lower wing at each side, 0 for none.
        wing_height: The wings' height.
        wing_depth: The wings' depth; a wing ends ``rear`` before the
            housing's rear.
        rgb: Housing colour.
    """
    front, back = -depth / 2, depth / 2 - lead_len
    x = width / 2 - wing
    wall, floor, roof = 0.6, 0.5, 0.6
    out = [
        ext(rect(-x, 0.0, x, height), -(back - rear), -front, rgb,
            holes=[rect(-x + wall, floor, x - wall, height - roof)], frame=Y),
        box(-x, back - rear, x, back, 0.0, height - 0.44, rgb),
    ]
    if wing:
        for side in (-1, 1):
            a, b = sorted((side * x, side * width / 2))
            out.append(box(a, back - rear - wing_depth, b, back - rear, 0.0,
                           wing_height, rgb))
    mid = floor + (height - roof - floor) / 2
    for p in rows(pins, pitch):
        out.append(box(p - 0.15, front + 0.9, p + 0.15, back - rear,
                       mid - 0.15, mid + 0.15, TIN))
        out.append(box(p - 0.15, back, p + 0.15, depth / 2, 0.0, 0.25, TIN))
    return out


def usb_c(pin_len=0.93):
    """A USB Type-C receptacle, 16 pins, the plug enters from -y.

    The origin is the centre of the shell's rear end on the board. The
    shell is 8.94 wide and 3.16 high, 0.09 above the board, 7.35 long.

    Args:
        pin_len: Length of the solder pins behind the shell.
    """
    w, z0, z1, length, cap = 4.47, 0.09, 3.25, 7.35, 0.97
    out = [
        ext(rounded(-w, z0, w, z1, 1.28), cap, length, STEEL,
            holes=[rounded(-w + 0.3, z0 + 0.3, w - 0.3, z1 - 0.3, 0.98)],
            frame=Y),
        ext(rounded(-w, z0, w, z1, 1.28), 0.0, cap, STEEL, frame=Y),
        box(-3.35, -length + 0.42, 3.35, -cap, 1.36, 1.98, BLACK),
    ]
    for side in (-1, 1):
        a, b = sorted((side * w, side * (w + 0.32)))
        out.append(box(a, -1.42, b, 0.16, 0.0, 1.6, STEEL))
        out.append(box(a, -5.17, b, -4.37, 0.0, 1.6, STEEL))
    for p in rows(12, 0.5):
        out.append(box(p - 0.14, 0.0, p + 0.14, pin_len, 0.0, 0.2, TIN))
    return out


def side_button():
    """A side-push button; the plunger points to -x.

    The origin is the centre of the body's footprint: 2.3 x 4.55, 1.8
    high. The plunger is 1.1 long, 1.6 wide and 1.0 high.
    """
    return [
        box(-1.15, -2.275, 1.15, 2.275, 0.0, 1.8, STEEL),
        box(-2.25, -0.8, -1.15, 0.8, 0.4, 1.4, GREY),
        box(1.15, 1.4, 1.83, 2.275, 0.0, 0.3, TIN),
        box(1.15, -2.275, 1.83, -1.4, 0.0, 0.3, TIN),
    ]


def trimmer(size, height, rotor):
    """A single-turn trimmer: square body, round rotor with a slot mark.

    The two end terminals are at -y, the wiper terminal at +y.
    """
    s = size / 2
    out = [
        box(-s, -s, s, s, 0.0, height - 0.25, CREAM),
        ext(circle(0.0, 0.0, rotor / 2), height - 0.25, height, WHITE),
        box(-1.75, -s - 0.5, -0.85, -s, 0.0, 0.3, TIN),
        box(0.85, -s - 0.5, 1.75, -s, 0.0, 0.3, TIN),
        box(-0.45, s, 0.45, s + 0.45, 0.0, 0.3, TIN),
    ]
    out += place([box(-0.4 * rotor, -0.2, 0.4 * rotor, 0.2, height,
                      height + 0.1, GREY)], turn=45.0)
    return out


def flat_cable_socket(width, body, latch, ear, ear_len, height):
    """A flat-cable socket: the cable enters from -y, the latch is at +y.

    The origin is the centre of the housing's front edge on the board.

    Args:
        width: Overall width along x.
        body: Depth of the housing.
        latch: Depth of the latch behind the housing.
        ear: Width of the housing's ear at each side of the latch.
        ear_len: Length of the ears.
        height: Height of the housing; the latch is 0.5 lower.
    """
    x = width / 2
    return [
        box(-x, 0.0, x, body, 0.0, height, WHITE),
        box(-x, body, -x + ear, body + ear_len, 0.0, height, WHITE),
        box(x - ear, body, x, body + ear_len, 0.0, height, WHITE),
        box(-x + ear, body, x - ear, body + latch, 0.0, height - 0.5, BLACK),
    ]


def micro_sd():
    """A push-push microSD card slot; the card enters from -x.

    The origin is the centre of the slot's footprint: 11.25 x 11.35,
    1.32 high. The shield's top has a finger notch at the card's end
    and 8 openings over the contacts, 1.1 apart.
    """
    x, y, top, sheet = 5.625, 5.675, 1.32, 0.15
    inner = y - sheet
    lid = [(-x, -inner), (x, -inner), (x, inner), (-x, inner),
           (-x, 4.235), (-4.215, 4.235), (-4.215, -3.565), (-x, -3.565)]
    contacts = [4.835 - 1.1 * i for i in range(8)]
    out = [
        ext(lid, top - sheet, top, STEEL,
            holes=[stadium(1.535, c - 0.3, 2.835, c + 0.3)
                   for c in contacts]),
        box(-x, -y, x, -inner, 0.0, top, STEEL),
        box(-x, inner, x, y, 0.0, top, STEEL),
        box(x - sheet, -inner, x, inner, 0.0, top - sheet, STEEL),
        box(-3.825, -inner, x - sheet, inner, 0.0, 0.34, BLACK),
    ]
    for c in contacts:
        out.append(box(1.4, c - 0.25, 3.0, c + 0.25, 0.34, 0.6, GOLD))
    return out


def ph_top(pins, pin_shift=0.0):
    """A top-entry wire socket of 2.0 pitch; the plug enters from +z.

    The origin is the centre of the housing's footprint, which is
    2 x (pins - 1) + 5.9 wide and 5.8 deep; the solder leads reach 2.2
    past it at +y. The housing is 5.5 high: a floor 0.8 thick and four
    walls 1.0 thick round the upright pins. The wall at -y is 2.0 high
    between its two ends, for the plug's key. A solder tab stands at
    each end.

    Args:
        pins: Number of pins.
        pin_shift: Distance along x of the row of pins from the
            housing's centre.
    """
    half, front, back, height = pins - 1 + 2.95, -2.9, 2.9, 5.5
    key = 0.6 * (half - 1.0)
    out = [
        box(-half + 1.0, front + 1.0, half - 1.0, back - 1.0, 0.0, 0.8,
            CREAM),
        box(-half, front, -half + 1.0, back, 0.0, height, CREAM),
        box(half - 1.0, front, half, back, 0.0, height, CREAM),
        box(-half + 1.0, back - 1.0, half - 1.0, back, 0.0, height, CREAM),
        box(-half + 1.0, front, -key, front + 1.0, 0.0, height, CREAM),
        box(key, front, half - 1.0, front + 1.0, 0.0, height, CREAM),
        box(-key, front, key, front + 1.0, 0.0, 2.0, CREAM),
    ]
    for side in (-1, 1):
        a, b = sorted((side * half, side * (half + 0.7)))
        out.append(box(a, -1.6, b, 1.4, 0.0, 1.6, TIN))
    for p in rows(pins, 2.0):
        x = p + pin_shift
        out.append(box(x - 0.25, 0.05, x + 0.25, 0.55, 0.8, height - 1.5,
                       TIN))
        out.append(box(x - 0.35, back, x + 0.35, back + 2.2, 0.0, 0.4, TIN))
    return out


def top_socket(pins, pitch, width, depth, height):
    """A top-entry wire-to-board socket; the plug enters from +z.

    The origin is the centre of the socket's footprint.

    Args:
        pins: Number of pins.
        pitch: Pin pitch.
        width: Width along x.
        depth: Depth along y.
        height: Height of the housing.
    """
    x, y, wall, floor = width / 2, depth / 2, 0.5, 0.9
    out = [box(-x, -y, x, y, 0.0, floor, CREAM),
           ext(rect(-x, -y, x, y), floor, height, CREAM,
               holes=[rect(-x + wall, -y + wall, x - wall, y - wall)])]
    for p in rows(pins, pitch):
        out.append(box(p - 0.15, -0.15, p + 0.15, 0.15, floor, height - 0.8,
                       TIN))
    return out


def usb_a():
    """A USB Type-A receptacle lying on the board; the plug enters from +x.

    The origin is the centre of the shell's rear end on the board. The
    shell is 12.2 wide and 5.7 high, 0.6 above the board, 10.0 long; the
    tongue is in its upper half. Four leads 2.3 long and two side tabs.
    """
    w, z0, z1, length = 6.1, 0.6, 6.3, 10.0
    out = [
        ext(rect(-w, z0, w, z1), 1.0, length, STEEL,
            holes=[rect(-w + 0.3, z0 + 0.3, w - 0.3, z1 - 0.3)], frame=X),
        box(0.0, -w, 1.0, w, z0, z1, STEEL),
        box(1.0, -4.75, 8.63, 4.75, 3.4, 5.4, WHITE),
    ]
    for side in (-1, 1):
        a, b = sorted((side * w, side * (w + 1.15)))
        out.append(box(0.0, a, 4.0, b, 2.0, 5.1, STEEL))
    for p in (-3.5, -1.0, 1.0, 3.5):
        out.append(box(-2.3, p - 0.4, 0.0, p + 0.4, 0.0, 0.3, TIN))
    return out


def card_tray(width, depth, height):
    """A microSD card slot under a steel shield; the card enters from -y.

    The origin is the centre of the slot's footprint. The shield is a
    lid 0.2 thick on two side walls and a rear wall over a black base
    0.8 high.

    Args:
        width: Width along x.
        depth: Depth along y.
        height: Height of the shield.
    """
    x, y, sheet = width / 2, depth / 2, 0.2
    return [
        box(-x, -y, x, y, height - sheet, height, STEEL),
        box(-x, -y, -x + sheet, y, 0.0, height - sheet, STEEL),
        box(x - sheet, -y, x, y, 0.0, height - sheet, STEEL),
        box(-x + sheet, y - sheet, x - sheet, y, 0.0, height - sheet, STEEL),
        box(-x + sheet, -y + 0.4, x - sheet, y - sheet, 0.0, 0.8, BLACK),
    ]


def push_button(depth, length, height, plunger):
    """A side-push button; the plunger points to -x.

    The origin is the centre of the button's box, plunger included.

    Args:
        depth: The body's size along x.
        length: The body's size along y.
        height: The body's height.
        plunger: The plunger's length; it is 2.4 wide and 1.6 high.
    """
    x0 = -(depth + plunger) / 2 + plunger
    return [
        box(x0, -length / 2, x0 + depth, length / 2, 0.0, height, GREY),
        box(x0 - plunger, -1.2, x0, 1.2, height / 2 - 0.8, height / 2 + 0.8,
            WHITE),
    ]


def slide_switch(depth, length, height, lever):
    """A slide switch; the lever points to +x. Three leads at -x.

    The origin is the centre of the body's footprint.

    Args:
        depth: The body's size along x.
        length: The body's size along y.
        height: The body's height.
        lever: The lever's length; it is 1.3 wide and 0.8 high.
    """
    x, y = depth / 2, length / 2
    out = [box(-x, -y, x, y, 0.0, height, EPOXY),
           box(x, -1.65, x + lever, -0.35, 0.6, 1.4, BLACK)]
    for p in (-3.0, -1.0, 1.0):
        out.append(box(-x - 1.75, p - 0.25, -x, p + 0.25, 0.0, 0.25, TIN))
    return out


def can_capacitor(diameter, length):
    """An aluminium capacitor lying on the board, its axis along y.

    The origin is the centre of the can's footprint.

    Args:
        diameter: The can's diameter.
        length: The can's length, its seal 0.7 long at -y included.
    """
    r, y = diameter / 2, length / 2
    return [
        ext(circle(0.0, r, r), -y, y - 0.7, STEEL, frame=Y),
        ext(circle(0.0, r, r - 0.1), y - 0.7, y, BLACK, frame=Y),
    ]


def shield(width, depth, height):
    """A shield can: a lid 0.2 thick on four walls 0.2 thick.

    The origin is the centre of the can's footprint.

    Args:
        width: Width along x.
        depth: Depth along y.
        height: Height.
    """
    x, y, sheet = width / 2, depth / 2, 0.2
    return [
        box(-x, -y, x, y, height - sheet, height, STEEL),
        ext(rect(-x, -y, x, y), 0.0, height - sheet, STEEL,
            holes=[rect(-x + sheet, -y + sheet, x - sheet, y - sheet)]),
    ]


def coin_holder(diameter, height):
    """A coin-cell holder: a ring with a floor and a clip across it.

    The origin is the centre of the ring. The clip runs along y and ends
    in a solder tab 3.0 long outside the ring at each end.

    Args:
        diameter: The ring's outer diameter; its wall is 1.0 thick.
        height: The ring's height.
    """
    r = diameter / 2
    return [
        ext(circle(0.0, 0.0, r), 0.0, 0.6, BLACK),
        ext(circle(0.0, 0.0, r), 0.6, height, BLACK,
            holes=[circle(0.0, 0.0, r - 1.0)]),
        box(-1.3, -r + 1.3, 1.3, r - 1.3, height - 0.3, height, GOLD),
        box(-1.0, r, 1.0, r + 3.0, 0.0, 0.3, GOLD),
        box(-1.0, -r - 3.0, 1.0, -r, 0.0, 0.3, GOLD),
    ]


def pin_header(pins, below=3.0, above=6.1):
    """A straight 2.54 pitch pin header, one row along x.

    The origin is the centre of the row on the board. The plastic is
    2.5 high and 2.5 deep; each 0.64 square pin reaches ``below`` under
    the board's surface and ``above`` over the plastic.
    """
    half, p = pins * 1.27, 0.32
    xs = rows(pins, 2.54)
    out = [ext(rect(-half, -1.25, half, 1.25), 0.0, 2.5, BLACK,
               holes=[rect(x - p, -p, x + p, p) for x in xs])]
    for x in xs:
        out.append(box(x - p, -p, x + p, p, -below, 2.5 + above, GOLD))
    return out


def shunt(length, width, ends, element, top, gap, radius=0.5):
    """A metal-strip current shunt, its length along x.

    Args:
        length: The footprint's length.
        width: The footprint's width.
        ends: Height of the two copper ends.
        element: Length of the resistance strip between the ends.
        top: Height of the strip's top over the board.
        gap: Height of the strip's underside over the board.
        radius: Radius of the upper edges across the shunt: the ends'
            outer ones and the strip's two.
    """
    x, y, e, r = length / 2, width / 2, element / 2, radius
    end = [(-x, 0.0), (-e, 0.0), (-e, ends),
           (-x + r, ends, (-x + r, ends - r, 1)), (-x, ends - r)]
    strip = [(-e, gap), (e, gap), (e, top - r, (e - r, top - r, 1)),
             (e - r, top), (-e + r, top, (-e + r, top - r, 1)),
             (-e, top - r)]
    return [ext(end, -y, y, COPPER, frame=Y),
            ext(strip, -y, y, BRONZE, frame=Y),
            ext([(-u, v) if len(q) == 2 else (-u, v, (-q[2][0], q[2][1], -1))
                 for q in end for u, v in [q[:2]]], -y, y, COPPER, frame=Y)]


def vssop10():
    """VSSOP-10: 3.0 x 3.0 body, 1.1 high, 0.5 pitch, 4.9 over the leads."""
    return leaded(3.0, 3.0, 1.1, rows(5, 0.5), rows(5, 0.5), 4.9, 0.23)


# XT90PW, the right-angle board version of the XT90, after a dealer's
# dimension drawing: contact pitch, contact pin diameter, the round
# tails' diameter and bend radius (both scaled from the drawing), the
# tails' and pegs' length under the housing, the rear plate (length,
# width, thickness), the pegs' distance from the tails (socket half,
# pin half) and from the centre line.
XT90_PITCH = 10.9
XT90_PIN = 4.5
XT90_TAIL = 3.5
XT90_BEND = 3.8
XT90_UNDER = 3.0
XT90_PLATE = (5.0, 18.9, 1.1)
XT90_PEG = (8.9, 9.4)
XT90_PEG_Y = 9.45


def _d_shape(y, z0, z1, cut, bevel):
    """The XT90's mating outline in (y, z): two corners cut at one end."""
    pts = [(-y, z0), (y - cut, z0), (y, z0 + cut), (y, z1 - cut),
           (y - cut, z1), (-y, z1)]
    return pts if bevel > 0 else [(-a, b) for a, b in pts]


def _xt90_rear(top, peg, under):
    """What both XT90PW have behind and under the housing.

    The rear plate with its window, a wall at each side that slopes
    from the housing's top to the plate, the two tails, which leave the
    rear at mid-height and bend down through the plate, and the two
    pegs.

    Args:
        top: Height of the housing.
        peg: Distance of the pegs from the tails.
        under: Length of the tails and pegs under the housing.
    """
    length, width, thick = XT90_PLATE
    r, half, tail, mid = XT90_TAIL / 2, width / 2, -XT90_BEND, top / 2
    reach = math.sqrt(r * r - (length + tail) ** 2)
    turn = math.degrees(math.atan2(reach, -length - tail))
    outline = [(0.0, -half), (0.0, half), (tail, half),
               (-length, half - 1.15)]
    for y in (XT90_PITCH / 2, -XT90_PITCH / 2):
        for angle in (turn, 45.0, -45.0):
            outline.append((tail + r * math.cos(math.radians(angle)),
                            y + r * math.sin(math.radians(angle)),
                            (tail, y, -1)))
        outline.append((-length, y - reach))
    outline += [(-length, -half + 1.15), (tail, -half)]
    out = [ext(outline, 0.0, thick, YELLOW,
               holes=[rect(-1.9, -1.5, -0.5, 1.5)])]
    wall = [(0.0, thick), (0.0, top), (tail, thick)]
    for a, b in ((half - 1.0, half), (-half, -half + 1.0)):
        out.append(ext(wall, -b, -a, YELLOW, frame=Y))
    path = [(0.0, mid, (0.0, mid - XT90_BEND, 1)), (tail, mid - XT90_BEND),
            (tail, -under)]
    for y in (XT90_PITCH / 2, -XT90_PITCH / 2):
        out += place([pipe(path, r, GOLD, frame=Y)], y=y)
    x = tail + peg
    for y in (-XT90_PEG_Y, XT90_PEG_Y):
        out.append(box(x - 0.5, y - 0.2, x + 0.5, y + 0.2, -under, 0.0, TIN))
    return out


def xt90pw_f(bevel=1, under=XT90_UNDER):
    """XT90PW-F, the socket half, lying on the board; it mates at +x.

    The origin is the centre of the housing's rear edge on the board.
    Collar 20.8 wide, 9.9 high and 7.8 long, nose 19.2 x 8.3 and 11.2
    long, a rear plate 5.0 long: 24.0 in all. The two tails end 3.8
    behind the housing; the two pegs are 8.9 in front of the tails.

    Args:
        bevel: +1 for the nose's two cut corners at +y, -1 at -y.
        under: Length of the tails and pegs under the housing.
    """
    top, sleeve = 9.9, XT90_PIN / 2 + 0.5
    nose = _d_shape(9.6, 0.8, 9.1, 2.2, bevel)
    out = [ext(rounded(-10.4, 0.0, 10.4, top, 0.8), 0.0, 7.8, YELLOW,
               frame=X),
           ext(nose, 7.8, 9.0, YELLOW, frame=X),
           ext(nose, 9.0, 19.0, YELLOW, frame=X,
               holes=[circle(y, top / 2, sleeve)
                      for y in (-XT90_PITCH / 2, XT90_PITCH / 2)])]
    for y in (-XT90_PITCH / 2, XT90_PITCH / 2):
        out.append(ext(circle(y, top / 2, sleeve), 9.0, 18.5, GOLD, frame=X,
                       holes=[circle(y, top / 2, XT90_PIN / 2)]))
    return out + _xt90_rear(top, XT90_PEG[0], under)


def xt90pw_m(bevel=1, under=XT90_UNDER):
    """XT90PW-M, the pin half, lying on the board; it mates at +x.

    The origin is the centre of the housing's rear edge on the board.
    Housing 20.8 wide, 9.9 high and 20.7 long with a shroud 12.5 deep
    round the two 4.5 pins; a rear plate 5.0 long: 25.7 in all. Tails
    as XT90PW-F; the pegs are 9.4 in front of the tails.

    Args:
        bevel: +1 for the shroud's two cut corners at +y, -1 at -y.
        under: Length of the tails and pegs under the housing.
    """
    top, floor, end = 9.9, 8.2, 20.7
    section = rounded(-10.4, 0.0, 10.4, top, 0.8)
    out = [ext(section, 0.0, floor, YELLOW, frame=X),
           ext(section, floor, end, YELLOW, frame=X,
               holes=[_d_shape(9.8, 0.6, 9.3, 2.2, bevel)])]
    for p in (-XT90_PITCH / 2, XT90_PITCH / 2):
        out.append(ext(circle(p, top / 2, XT90_PIN / 2), floor, end - 1.0,
                       GOLD, frame=X))
    return out + _xt90_rear(top, XT90_PEG[1], under)


def text(string, height, rgb=WHITE, width=None, thick=PRINT):
    """A line of board print, the letters of _font.py.

    The origin is the centre of the line's box; the line reads along x
    and stands on z = 0.

    Args:
        string: The text.
        height: Height of a capital letter.
        rgb: The print's colour.
        width: The line's width; the letters are made narrower or wider
            to fit. None keeps the font's own width.
        thick: The print's thickness.
    """
    steps = [SPACE if c == " " else GLYPHS[c][0] for c in string]
    k = width / sum(steps) if width else height
    out, x = [], -sum(steps) * k / 2
    for c, step in zip(string, steps, strict=True):
        if c != " ":
            for outline, holes in GLYPHS[c][1]:
                def put(points, x=x):
                    return [(x + u * k, (v - 0.5) * height)
                            for u, v in points]
                out.append(ext(put(outline), 0.0, thick, rgb,
                               holes=[put(h) for h in holes]))
        x += step * k
    return out


def frame(x0, y0, x1, y1, line=0.15, rgb=WHITE, thick=PRINT):
    """A printed rectangle, its line's middle on the given box."""
    h = line / 2
    return [ext(rect(x0 - h, y0 - h, x1 + h, y1 + h), 0.0, thick, rgb,
                holes=[rect(x0 + h, y0 + h, x1 - h, y1 - h)])]


def hole_pad(drill, size, board, rgb=GOLD, square=False, long=None):
    """The copper of a plated hole: a pad on each side and the barrel.

    The origin is the hole's centre on the board's underside.

    Args:
        drill: The hole's diameter; the barrel is PAD thick inside it.
        size: The pad's diameter, or its side when square.
        board: The board's thickness.
        rgb: The plating's colour.
        square: Whether the pad is a square.
        long: The pad's length along y when it is longer than ``size``:
            a rectangle when square, else a slot shape.
    """
    r, d = size / 2, drill / 2
    half = (long or size) / 2
    if square:
        shape = rect(-r, -half, r, half)
    elif long:
        shape = stadium(-r, -half, r, half)
    else:
        shape = circle(0.0, 0.0, r)
    hole = [circle(0.0, 0.0, d)]
    return [ext(shape, board, board + PAD, rgb, holes=hole),
            ext(shape, -PAD, 0.0, rgb, holes=hole),
            ext(circle(0.0, 0.0, d), 0.0, board, rgb,
                holes=[circle(0.0, 0.0, d - PAD)])]


def pad(length, width, rgb=TIN, thick=PAD):
    """A bare pad, a rectangle on the board."""
    return [box(-length / 2, -width / 2, length / 2, width / 2, 0.0, thick,
                rgb)]


def r0805():
    return chip(2.0, 1.25, 0.5, EPOXY)


def c1206():
    return chip(3.2, 1.6, 1.1, TAN)


def r2512():
    """A 2512 current-sense resistor: 6.3 x 3.2, 0.6 high, ends 0.6."""
    return [box(-3.15, -1.6, -2.55, 1.6, 0.0, 0.6, TIN),
            box(-2.55, -1.6, 2.55, 1.6, 0.0, 0.6, BLACK),
            box(2.55, -1.6, 3.15, 1.6, 0.0, 0.6, TIN)]


def soic8():
    """SOIC-8: 3.9 x 4.9 body 1.5 high, leads 1.27 apart, 6.0 span."""
    return leaded(3.9, 4.9, 1.5, rows(4, 1.27), rows(4, 1.27), 6.0, 0.4)


def sot23_8():
    """SOT-23-8: 1.6 x 2.9 body 1.1 high, leads 0.65 apart, 2.8 span."""
    return leaded(1.6, 2.9, 1.1, rows(4, 0.65), rows(4, 0.65), 2.8, 0.3)


def sma():
    """An SMA diode: 4.3 x 2.6 body 2.2 high, 5.2 over the leads."""
    return sod(4.3, 2.6, 2.2, 5.2, 1.5)


def tact_switch(length, width, height, cap, cap_height):
    """A surface-mount push button pressed from above.

    Args:
        length: The steel body's size along x.
        width: The body's size along y.
        height: The body's height.
        cap: The side of the square cap in the body's middle.
        cap_height: The cap's top over the board.
    """
    x, y, c = length / 2, width / 2, cap / 2
    return [box(-x, -y, x, y, 0.0, height, STEEL),
            box(-c, -c, c, c, height, cap_height, BLACK)]


def pouch_cell(width, length, thick, head, head_thick, tape, seam=0.0):
    """A lithium polymer cell in its pouch, with its protection board.

    The origin is the middle of the cell's width at the end away from
    the wires, on the face the cell lies on. The pouch runs along +y,
    then the head: the folded seal with the protection board under
    tape. The head lies in the middle of the cell's thickness.

    Args:
        width: Size along x, side seams included.
        length: Length along y, head included.
        thick: The pouch's thickness.
        head: The head's length.
        head_thick: The head's thickness.
        tape: Colour of the tape round the head.
        seam: How far the side seams stand out at each side: a seam
            leaves the pouch at half its thickness and is folded up.
            0 for seams that lie flat on the pouch.
    """
    x, r, sheet = width / 2 - seam, 0.3 * thick, 0.3
    body, mid = length - head, thick / 2
    out = [ext(rounded(-x, 0.0, x, thick, r), -body, 0.0, FOIL, frame=Y)]
    for side in (-1, 1):
        if seam:
            a, b = sorted((side * x, side * (width / 2 - sheet)))
            out.append(box(a, 0.0, b, body, mid - sheet / 2,
                           mid + sheet / 2, FOIL))
            a, b = sorted((side * (width / 2 - sheet), side * width / 2))
            out.append(box(a, 0.0, b, body, mid - sheet / 2, 0.9 * thick,
                           FOIL))
    z = (thick - head_thick) / 2
    out.append(ext(rounded(-width / 2, z, width / 2, z + head_thick, 0.8),
                   -length, -body, tape, frame=Y))
    return out


def cell_lead(length, pitch=1.25, wire=1.2):
    """A cell's two wires and their plug, drawn straight along +x.

    The origin is where the wires leave the cell, between the two, at
    the height of their axes. Red is at +y. The plug is a 2 pin
    housing 3.6 wide, 4.4 long and 2.1 high.

    Args:
        length: Length of the wires up to the plug.
        pitch: Distance between the two wires' axes at the plug.
        wire: Diameter of a wire's insulation.
    """
    h = max(pitch, wire) / 2
    out = []
    for side, rgb in ((1, RED), (-1, BLACK)):
        out.append(pipe([(0.0, side * h), (length, side * h)], wire / 2,
                        rgb))
    out.append(ext(rect(-1.8, -1.05, 1.8, 1.05), length, length + 4.4,
                   WHITE, holes=[rect(side * pitch / 2 - 0.4, -0.5,
                                      side * pitch / 2 + 0.4, 0.5)
                                 for side in (-1, 1)], frame=X))
    return out
