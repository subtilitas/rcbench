"""Electronic parts as shapes for _solid.py.

Every function returns shapes in the part's own frame: the origin is
the centre of the part's footprint on the board, z = 0 is the board's
surface and +z points away from it. All sizes are mm. Package sizes are
the packages' nominal ones, not measured on a board.
"""

from _solid import X, Y, box, circle, ext, place, rect, rounded, stadium

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
