"""MATEK I2C-INA-BM, INA228 current and voltage monitor, 27 x 20mm.

Sources: the maker's 3D model I2C-INA-BM.step (mateksys.com download,
2024-03-23) for the board, shunt, connector, chip and hole positions, read
from its solids' vertices; the maker's product image I2C-INA-BM_1.jpg (27mm
and 20mm dimension arrows, hole "3mm", 17.6px/mm) for the BAT+ and ESC+ pads,
which the 3D model leaves out, and for the board print. Board frame: the 3D
model's centre is the board centre, x right, y up; x_board = x + 13.5,
y_board = 10 - y.

Accuracy: positions from the 3D model within 0.05mm of the model (the model
against a real board: not checked). BAT+ and ESC+ pads within about 0.3mm
(photo). Address jumpers within about 0.3mm (photo).

Pads: the board has no header. I2C and 5V leave through two JST-GH 4-pin
side-entry connectors; the footprint marks their solder leads as SMD pads,
1.25mm pitch. Wire pads BAT+ and ESC+ are SMD pads. The 3.0mm hole is a
non-plated hole. Bottom side: one large pad without a net (maker text), not
modelled.

Unknown: which lead the maker calls JST pin 1 (numbering here follows the
print, top to bottom); the copper extent of BAT+ and ESC+ under the shunt;
board thickness (the maker states 6mm overall height; the 3D model's
connectors are 4.35mm tall).
"""

NAME = "INA228_Matek_I2C-INA-BM_27x20mm"
DESC = ("MATEK I2C-INA-BM, INA228 (or INA238) current and voltage monitor, "
        "27 x 20mm, I2C address 0x45 by default (print: 69, decimal). "
        "Shunt in the plus line between BAT+ and ESC+. "
        "Pads 1-4: left JST-GH 4-pin 5V SCL SDA G; pads 5-8: right JST-GH "
        "4-pin G SDA SCL 5V; 9 BAT+; 10 ESC+. 5V input 4-9V (maker). "
        "Pads 1-8 mark the connector leads; the bench connects through a "
        "JST-GH cable. Drawn from the maker's 3D model and photo.")

# Pin rows of the two JST-GH connectors, y in the board frame (3D model).
JST_Y = (13.325, 14.575, 15.825, 17.075)
JST_LX, JST_RX = 4.95, 22.05          # lead foot centres
JST_Y0, JST_Y1 = 11.075, 19.325       # body incl. mounting tabs

SHUNT = (6.0, 0.6, 21.0, 8.4)
ESC_PAD = (0.4, 0.35, 5.4, 8.3)
BAT_PAD = (21.6, 0.35, 26.6, 8.3)
CHIP = (15.3, 11.0, 18.3, 14.0)
HOLE = (10.0, 16.5, 1.5)


def _jst(x_out, x_in):
    """One JST-GH 4-pin side-entry body: mouth at x_out, leads at x_in."""
    x0, x1 = min(x_out, x_in), max(x_out, x_in)
    tab = 2.2 if x_out < x_in else -2.2
    items = [("rect", x0, JST_Y0 + 0.3, x1, JST_Y1 - 0.3),
             ("rect", x_out, JST_Y0, x_out + tab, JST_Y0 + 0.5),
             ("rect", x_out, JST_Y1 - 0.5, x_out + tab, JST_Y1),
             ("rect", min(x_out, x_out + tab * 0.4), JST_Y0 + 1.6,
              max(x_out, x_out + tab * 0.4), JST_Y1 - 1.6)]
    return items


def _drawing():
    _, y0, _, y1 = SHUNT
    items = [("rect",) + SHUNT, ("rect", 11.0, y0, 16.0, y1),
             ("rect",) + CHIP, ("circle", 15.8, 11.5, 0.2)]
    items += _jst(0.6, 4.65) + _jst(26.4, 22.35)
    for i in range(5):
        y = 11.25 + i * 0.5
        items += [("line", 14.35, y, 15.3, y), ("line", 18.3, y, 19.25, y)]
    items += [("circle",) + HOLE]
    return items


def _leads():
    """JST lead feet, fab only: silk stays off the pads."""
    return [("line", x0, y, x1, y) for x0, x1 in ((4.45, 5.45), (21.55, 22.55))
            for y in JST_Y]


def _jst_pads(first, x):
    return [{"num": str(first + i), "x": x, "y": y,
             "shape": "rect" if first + i == 1 else "oval",
             "size": (1.0, 0.6), "drill": None}
            for i, y in enumerate(JST_Y)]


def _box_pad(num, box):
    x0, y0, x1, y1 = box
    return {"num": num, "x": (x0 + x1) / 2, "y": (y0 + y1) / 2,
            "shape": "rect", "size": (x1 - x0, y1 - y0), "drill": None}


FOOTPRINT = {
    "name": NAME,
    "desc": DESC,
    "datasheet": "https://www.mateksys.com/?portfolio=i2c-ina-bm",
    "board": (27.0, 20.0),
    "origin": (JST_LX, JST_Y[0]),
    "pads": (_jst_pads(1, JST_LX) + _jst_pads(5, JST_RX)
             + [_box_pad("9", BAT_PAD), _box_pad("10", ESC_PAD),
                {"num": "", "x": HOLE[0], "y": HOLE[1], "shape": "circle",
                 "size": (3.0, 3.0), "drill": 3.0, "type": "np"}]),
    "silk": _drawing() + [
        ("text", "ESC+", 2.9, 9.3, 0.8), ("text", "BAT+", 24.1, 9.3, 0.8),
        ("text", "5V", 6.6, 13.325, 0.7), ("text", "SCL", 6.8, 14.575, 0.7),
        ("text", "SDA", 6.8, 15.825, 0.7), ("text", "G", 6.4, 17.075, 0.7),
        ("text", "G", 20.6, 13.325, 0.7), ("text", "SDA", 20.2, 14.575, 0.7),
        ("text", "SCL", 20.2, 15.825, 0.7), ("text", "5V", 20.4, 17.075, 0.7)],
    "fab": _drawing() + _leads() + [
        ("rect", 15.8, 18.1, 17.0, 19.4), ("rect", 18.7, 18.1, 19.9, 19.4),
        ("text", "68", 16.4, 17.5, 0.6), ("text", "65", 19.3, 17.5, 0.6),
        ("text", "INA228", 16.8, 12.5, 0.5),
        ("text", "SHUNT", 13.5, 4.5, 0.8),
        ("text", "JST-GH", 2.6, 15.2, 0.5),
        ("text", "JST-GH", 24.4, 15.2, 0.5)],
}

SYMBOL = {
    "name": NAME,
    "desc": DESC,
    "footprint": NAME,
    "datasheet": "https://www.mateksys.com/?portfolio=i2c-ina-bm",
    "body": (-12.7, 7.62, 12.7, -10.16),
    "graphics": [
        # shunt between the two wire pads, top view
        ("rect", -5.08, 6.35, 5.08, 2.54),
        ("rect", -2.03, 6.35, 2.03, 2.54, "outline"),
        ("text", "SHUNT", 0, 1.52, 0.9),
        # the two JST-GH connectors
        ("rect", -12.19, 1.27, -6.35, -8.38),
        ("rect", 6.35, 1.27, 12.19, -8.38),
        ("text", "JST-GH", -7.0, -9.14, 0.8),
        ("text", "JST-GH", 7.0, -9.14, 0.8),
        # sensor chip
        ("rect", 1.27, -1.27, 5.08, -5.08),
        ("text", "INA228", 3.18, -3.18, 0.8),
    ],
    "pins": [
        ("passive", "ESC+", "10", -7.62, 10.16, 270),
        ("passive", "BAT+", "9", 7.62, 10.16, 270),
        ("power_in", "5V", "1", -15.24, 0, 0),
        ("input", "SCL", "2", -15.24, -2.54, 0),
        ("bidirectional", "SDA", "3", -15.24, -5.08, 0),
        ("power_in", "G", "4", -15.24, -7.62, 0),
        ("power_in", "G", "5", 15.24, 0, 180),
        ("bidirectional", "SDA", "6", 15.24, -2.54, 180),
        ("input", "SCL", "7", 15.24, -5.08, 180),
        ("power_in", "5V", "8", 15.24, -7.62, 180),
    ],
}

# 3D model. From the maker's 3D model I2C-INA-BM.step: the JST-GH bodies
# 4.35mm tall, the shunt 1.85mm. The maker's page gives 27 x 20 x 6mm; the
# third figure is not labelled and the 3D model reaches 4.35mm. Board
# thickness (1.6mm) and chip height (1.1mm, VSSOP-10 class figure) not
# measured. The board is wired, so it rests on the carrier.
MODEL = {
    "mount": "flat",
    "bodies": [
        ("box", 6.0, 0.6, 11.0, 8.4, 1.85, "metal"),
        ("box", 11.0, 0.6, 16.0, 8.4, 1.85, "black"),
        ("box", 16.0, 0.6, 21.0, 8.4, 1.85, "metal"),
        ("box",) + CHIP + (1.1, "chip"),
        ("box", 0.6, JST_Y0 + 0.3, 4.65, JST_Y1 - 0.3, 4.35, "beige"),
        ("box", 22.35, JST_Y0 + 0.3, 26.4, JST_Y1 - 0.3, 4.35, "beige"),
        ("box",) + ESC_PAD + (0.05, "gold"),
        ("box",) + BAT_PAD + (0.05, "gold"),
    ],
}
