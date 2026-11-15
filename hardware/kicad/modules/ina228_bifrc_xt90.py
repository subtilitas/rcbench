"""INA228 module with two XT90PW connectors, board print "BIFRC V1.1".

Source: three owner photos of the board on 5mm squared paper (top
side, underside, case; not in the repository), measured with
hardware/mechanical/_photo_grid.py. The
grid gives the scale; the board's size and the screw holes' spacing come
from the underside photo, corrected for the board's height over the
paper (11.65mm, camera 196mm over the paper). Every other position is
read from the top and the underside photo, laid onto each other by the
4 screw holes.

Accuracy: positions on the board within about 0.2mm; the board's size
and the hole spacing within about 0.3mm (0.5mm of doubt in the board's
height over the paper is 0.2mm in 80mm). The header's pitch reads
2.52mm on the underside and 2.56mm on the top side against 2.54mm. The
connectors' bodies stand 10mm over the board; their outlines are
corrected for that height (camera 196mm over the board, over the point
x = 54, y = 24.7) and are within about 0.5mm.

Pads: header L A V G 5 (THT, 2.54mm). The connectors' tails are
soldered into 3 tinned bars; the footprint gives each bar an SMD pad:
the plus bar runs from connector to connector, the minus bars meet at
the shunt. GND and 5V are round test pads at the top edge. The 4 screw
holes are non-plated 3.2mm holes in the footprint; on the board they
are 3.05mm holes in tinned rings of 5.1mm.

Slots: 26 milled slots of 2.2 x 0.9mm beside the bars and 16 under the
two connectors.

Unknown: which side the board expects the battery (the pin half's side
is named BAT after the configurator drawing); pin 1 marking under the
header plastic; copper extent under the solder. The board print warns
that the screw holes carry circuitry ("do not short").
"""

import math

NAME = "INA228_BIFRC_XT90_80x48mm"
DESC = ("INA228 current and voltage monitor with 2 XT90PW, board print "
        "BIFRC V1.1, 80.1 x 48.6mm, 4 screw holes 70.15 x 38.8mm apart. "
        "Shunt (print 0m50) in the minus line between BAT- and ESC-; the plus "
        "bar runs straight through. Header print L A V G 5: 1 L = SCL, "
        "2 A = SDA, 3 V = 3.3V chip supply, 4 G = GND, 5 = hole without pin. "
        "Pads 6 + bar, 7 BAT-, 8 ESC-, 9 GND, 10 5V test pads. "
        "The screw holes carry circuitry (board print). Drawn from owner "
        "photos on squared paper; positions within about 0.3mm, "
        "connectors within about 0.5mm.")

BW, BH = 80.1, 48.6
HDR_Y = 44.4
HDR_X = [56.16 + 2.54 * i for i in range(5)]
HOLES = [(4.95, 4.9), (75.1, 4.9), (4.95, 43.7), (75.1, 43.7)]

PLUS_BAR = (23.45, 16.8, 57.15, 21.9)
BATN_BAR = (23.45, 27.5, 31.95, 33.0)
ESCN_BAR = (47.25, 27.5, 57.15, 33.0)
SHUNT = (31.95, 26.3, 47.25, 34.2)
CONTACT_Y = (19.3, 30.2)
# XT90PW-M at the left, XT90PW-F at the right, as boxes (x0, y0, x1, y1):
# housing, rear plate; rear plate, collar, nose.
PLUG = ((-2.25, 14.35, 18.45, 35.15), (18.45, 15.3, 23.45, 34.2))
SOCKET = ((57.15, 15.3, 62.15, 34.2), (62.15, 14.35, 69.95, 35.15),
          (69.95, 15.15, 81.15, 34.35))
# Milled slots, 2.2 x 0.9mm: rows of (first x, y, count) at SLOT_PITCH,
# slanted 45 degrees; the x of 5 slots along x at SLOT_FLAT_Y; and under
# the connectors 4 columns of (x, slant of the first row) with a slot at
# each y of SLOT_UNDER_Y, the slant changing from row to row. A slant
# of 1 rises to the right in the top view.
SLOT_ROWS = ((23.93, 12.88, 12), (18.17, 37.76, 6), (49.17, 34.85, 3))
SLOT_PITCH = 2.95
SLOT_FLAT_X = (22.85, 26.4, 29.94, 50.1, 53.69)
SLOT_FLAT_Y = 25.6
SLOT_UNDER = ((3.15, 1), (6.15, 1), (70.85, -1), (73.85, -1))
SLOT_UNDER_Y = (16.1, 22.1, 26.9, 32.9)


def _xt90(contacts=True):
    """Both connectors, top view, their outlines at board level.

    The tails go on fab only, where they may cover the bar pads.
    """
    items = [("rect",) + box for box in PLUG + SOCKET]
    for y in CONTACT_Y if contacts else ():
        items += [("rect", PLUG[0][2], y - 1.75, 24.0, y + 1.75),
                  ("rect", 56.6, y - 1.75, SOCKET[1][0], y + 1.75)]
    return items


def _slots():
    """Milled slots through the board, drawn as their center lines."""
    items = []
    for x0, y, n in SLOT_ROWS:
        for i in range(n):
            x = x0 + SLOT_PITCH * i
            items.append(("line", x - 0.8, y + 0.7, x + 0.8, y - 0.7))
    for x in SLOT_FLAT_X:
        items.append(("line", x - 0.8, SLOT_FLAT_Y, x + 0.8, SLOT_FLAT_Y))
    for x, first in SLOT_UNDER:
        for row, y in enumerate(SLOT_UNDER_Y):
            d = 0.7 * first * (-1) ** row
            items.append(("line", x - 0.8, y + d, x + 0.8, y - d))
    return items


CHIP = (39.1, 42.8, 42.1, 45.9)
HEADER = (HDR_X[0] - 1.27, HDR_Y - 1.25, HDR_X[3] + 1.27, HDR_Y + 1.25)
TEST_PADS = ((61.0, 4.45), (61.0, 7.0))


def _chip():
    items = [("rect",) + CHIP, ("circle", 39.6, 45.4, 0.2)]
    for i in range(5):
        x = 39.65 + 0.5 * i
        items += [("line", x, 41.2, x, CHIP[1]), ("line", x, CHIP[3], x, 47.2)]
    return items


def _drawing(fab=True):
    items = _xt90(fab)
    if fab:
        items += [("rect",) + SHUNT, ("rect", 37.0, SHUNT[1], 42.3, SHUNT[3])]
    else:
        items += [("line", SHUNT[0] + 0.3, SHUNT[1], SHUNT[2] - 0.3, SHUNT[1]),
                  ("line", SHUNT[0] + 0.3, SHUNT[3], SHUNT[2] - 0.3, SHUNT[3])]
    items += _chip()
    items += [("rect",) + HEADER]
    for x, y in HOLES:
        items += [("circle", x, y, 1.6), ("circle", x, y, 2.55)]
    return items


def _bar_pad(num, box):
    x0, y0, x1, y1 = box
    return {"num": num, "x": (x0 + x1) / 2, "y": (y0 + y1) / 2,
            "shape": "rect", "size": (x1 - x0, y1 - y0), "drill": None}


PADS = ([{"num": str(i + 1), "x": x, "y": HDR_Y,
          "shape": "rect" if i == 0 else "circle", "size": (1.7, 1.7),
          "drill": 1.0} for i, x in enumerate(HDR_X)]
        + [_bar_pad("6", PLUS_BAR), _bar_pad("7", BATN_BAR),
           _bar_pad("8", ESCN_BAR),
           {"num": "9", "x": TEST_PADS[0][0], "y": TEST_PADS[0][1],
            "shape": "circle", "size": (1.6, 1.6), "drill": None},
           {"num": "10", "x": TEST_PADS[1][0], "y": TEST_PADS[1][1],
            "shape": "circle", "size": (1.6, 1.6), "drill": None}]
        + [{"num": "", "x": x, "y": y, "shape": "circle", "size": (3.2, 3.2),
            "drill": 3.2, "type": "np"} for x, y in HOLES])


def _outline(r=2.4, n=6):
    """Board outline with rounded corners (radius about 2.4mm, photo)."""
    pts = []
    for cx, cy, a0 in ((r, r, 180), (BW - r, r, 270), (BW - r, BH - r, 0),
                       (r, BH - r, 90)):
        pts += [(cx + r * math.cos(math.radians(a0 + 90 * i / n)),
                 cy + r * math.sin(math.radians(a0 + 90 * i / n)))
                for i in range(n + 1)]
    return pts

FOOTPRINT = {
    "name": NAME,
    "desc": DESC,
    "board": (BW, BH),
    "outline": _outline(),
    "origin": (HDR_X[0], HDR_Y),
    "pads": PADS,
    "silk": _drawing(False) + [
        ("text", c, x, 47.0, 1.0)
        for c, x in zip("LAVG5", HDR_X, strict=True)] + [
        ("text", "GND", 58.4, 4.45, 0.9), ("text", "5V", 58.9, 7.0, 0.9),
        ("text", "+", 21.0, 13.2, 1.2), ("text", "-", 21.0, 36.2, 1.2),
        ("text", "+", 58.9, 13.2, 1.2), ("text", "-", 58.9, 36.4, 1.2),
        ("text", "BIFRC V1.1", 22.0, 46.0, 1.5)],
    "fab": _drawing() + _slots() + [
        ("text", "XT90 BAT", 8.0, 24.75, 1.2),
        ("text", "XT90 ESC", 72.0, 24.75, 1.2),
        ("text", "SHUNT 0m50", 39.6, 35.3, 0.8),
        ("text", "INA228", 40.6, 40.9, 0.6),
        ("text", "screw holes carry circuitry", 40.05, 2.0, 0.8)],
}

SYMBOL = {
    "name": NAME,
    "desc": DESC,
    "footprint": NAME,
    "body": (-20.32, 10.16, 20.32, -20.32),
    "graphics": [
        # XT90 plugs at both ends, plus bar on top, shunt in the minus bar
        ("rect", -12.7, 8.89, -8.89, -3.81),
        ("rect", 8.89, 8.89, 12.7, -3.81),
        ("text", "XT90", -10.8, 2.54, 0.9, 90),
        ("text", "XT90", 10.8, 2.54, 0.9, 90),
        ("poly", [(-8.89, 6.35), (8.89, 6.35)]),
        ("poly", [(-8.89, -2.54), (-3.81, -2.54)]),
        ("poly", [(3.81, -2.54), (8.89, -2.54)]),
        ("rect", -3.81, -1.52, 3.81, -3.56),
        ("text", "SHUNT", 0, -0.51, 0.9),
        ("rect", -2.54, -8.89, 2.54, -13.97),
        ("text", "INA228", 0, -11.43, 0.8),
        ("text", "BIFRC V1.1", 0, -17.78, 1.0),
    ],
    "pins": [
        ("passive", "+", "6", -22.86, 5.08, 0),
        ("passive", "BAT-", "7", -22.86, -2.54, 0),
        ("power_in", "GND", "9", -22.86, -15.24, 0),
        ("passive", "5V", "10", -22.86, -17.78, 0),
        ("passive", "ESC-", "8", 22.86, -2.54, 180),
        ("input", "L", "1", 22.86, -7.62, 180),
        ("bidirectional", "A", "2", 22.86, -10.16, 180),
        ("power_in", "V", "3", 22.86, -12.7, 180),
        ("power_in", "G", "4", 22.86, -15.24, 180),
        ("passive", "5", "5", 22.86, -17.78, 180),
    ],
}

# 3D model: the board as boxes. hardware/mechanical/ina228_xt90.py draws
# the same board part by part, in the same frame. Connector heights: a
# dealer's dimension drawing of XT90PW, 9.9mm, the socket half's nose
# 8.3mm. Shunt: ends 1.2mm, the middle to 1.8mm and 0.2mm off the board,
# board 1.6mm (owner's measurements). Chip 1.1mm (VSSOP-10). Header:
# 2.5mm plastic on the board, 4 pins 6.0mm above it (pad 5 has no pin).
# The milled slots are not modelled. The board is wired, so it rests on
# the carrier.
MODEL = {
    "mount": "flat",
    "bodies": [
        ("box",) + PLUG[0] + (9.9, "yellow"),
        ("box",) + PLUG[1] + (1.1, "yellow"),
        ("box",) + SOCKET[0] + (1.1, "yellow"),
        ("box",) + SOCKET[1] + (9.9, "yellow"),
        ("box",) + SOCKET[2] + (8.3, "yellow", 0.8),
        ("box", SHUNT[0], SHUNT[1], 37.0, SHUNT[3], 1.2, "metal"),
        ("box", 37.0, SHUNT[1], 42.3, SHUNT[3], 1.6, "black", 0.2),
        ("box", 42.3, SHUNT[1], SHUNT[2], SHUNT[3], 1.2, "metal"),
        ("box",) + CHIP + (1.1, "chip"),
        ("box",) + HEADER + (2.5, "plastic"),
        ("box",) + PLUS_BAR + (0.05, "gold"),
        ("box",) + BATN_BAR + (0.05, "gold"),
        ("box",) + ESCN_BAR + (0.05, "gold"),
        ("cyl",) + TEST_PADS[0] + (0.8, 0.05, "gold"),
        ("cyl",) + TEST_PADS[1] + (0.8, 0.05, "gold"),
    ]
    + [("box", x - 0.32, HDR_Y - 0.32, x + 0.32, HDR_Y + 0.32, 10.1,
        "gold", -1.6) for x in HDR_X[:4]],
}
