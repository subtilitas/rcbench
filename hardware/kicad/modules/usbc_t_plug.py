"""USB-C tap "T-plug": USB-C board F-F with a plug board, pads G D+ D- V.

The assembly is a black T-shaped board, 17.0 x 19.6mm, with the USB-C
receptacles IN (left) and OUT (right) along its top bar and a third
receptacle facing down from its stem. A USB-C plug board sits in the third
receptacle: plug shell, overmould and a green board, 8.15 x 4.75mm, with the
4 solder pads G, D+, D-, V. The footprint frame is the top bar of the black
board (17.0 x 13.1mm); the stem, plug and green board are drawn below it.

Measured from the owner's photo 04-usb-c-tap-closeup.webp. Scale: the USB-C
plug shell is 8.25mm wide (USB Type-C specification), 282px in the photo,
and the third receptacle shell 8.94mm, 303px: 34.0px/mm (the two agree
within 1%). No dimension drawing of either board is known. Positions are
within about 0.5mm (estimate); the plug board sits about 3mm higher than the
black board, so its scale may differ by 1-2%.

Pads: 2.0mm pitch (photo 2.05, 1.85, 2.1mm), 1.1-1.55mm wide and 2.0mm
long on the plug board. The footprint pads are THT wire points, 1.4 x 2.0mm
with a 0.9mm drill, under the module's pads; a short wire joins each module
pad to its carrier pad. Pad 1 = G (square).

Unknown: the maker and part number of both boards, board thickness, the
position of the plug board when the plug is not fully seated, what the V
pad connects to on the plug board beyond VBUS.
"""

NAME = "USBC_T-plug_17x30mm"
DESC = ("USB-C tap (T-plug): USB-C board F-F with receptacles IN and OUT and "
        "a USB-C plug board with 4 pads. Pads: 1 G (GND), 2 D+, 3 D-, "
        "4 V (VBUS), 2.0mm pitch, THT wire points under the plug board's "
        "pads. Assembly 17.5 x 30.1mm. The bench wires G, D+ and D- to the "
        "IOmcu; V carries no wire. Scaled from an owner photo, about 0.5mm.")

BAR = (17.0, 13.1)
STEM = (2.1, 14.7, 19.6)
IN = (-0.47, 1.47, 6.88, 10.29)
OUT = (9.38, 1.47, 16.88, 10.29)
DOWN = (3.62, 11.47, 12.53, 19.56)
PLUG = (4.06, 19.56, 12.35, 23.4)
MOULD = (3.8, 23.4, 12.6, 25.35)
GREEN = (4.09, 25.35, 12.24, 30.1)
PAD_Y = 28.85
PAD_X = (5.2, 7.2, 9.2, 11.2)
LABELS = ("G", "D+", "D-", "V")


def _receptacle(box, mouth):
    """Shell outline with the mouth marked on side ``mouth``."""
    x0, y0, x1, y1 = box
    items = [("rect",) + box]
    if mouth == "W":
        items.append(("rect", x0, y0 + 2.9, x0 + 0.6, y1 - 2.9))
    elif mouth == "E":
        items.append(("rect", x1 - 0.6, y0 + 2.9, x1, y1 - 2.9))
    else:
        items.append(("rect", x0 + 2.9, y1 - 0.6, x1 - 2.9, y1))
    return items


def _drawing():
    x0, x1, y1 = STEM
    items = [("line", x0, BAR[1], x0, y1), ("line", x0, y1, x1, y1),
             ("line", x1, y1, x1, BAR[1])]
    items += _receptacle(IN, "W") + _receptacle(OUT, "E")
    items += _receptacle(DOWN, "S")
    items.append(("rect",) + GREEN)
    return items


def _fab_only():
    items = [("rect",) + PLUG, ("rect",) + MOULD]
    items += [("line", 4.6 + i * 0.6, 25.35, 4.6 + i * 0.6, 26.2)
              for i in range(12)]
    items += [("text", "IN", 3.2, 5.88, 1.0),
              ("text", "OUT", 13.13, 5.88, 1.0),
              ("text", "USB-C", 8.08, 15.5, 0.8)]
    return items


FOOTPRINT = {
    "name": NAME,
    "desc": DESC,
    "board": BAR,
    "origin": (PAD_X[0], PAD_Y),
    "pads": [{"num": str(i + 1), "x": x, "y": PAD_Y,
              "shape": "rect" if i == 0 else "oval", "size": (1.4, 2.0),
              "drill": 0.9} for i, x in enumerate(PAD_X)],
    "silk": _drawing() + [("text", "IN", 4.4, 0.75, 0.8),
                          ("text", "OUT", 12.2, 0.75, 0.8)]
    + [("text", s, x, 26.95, 0.7) for x, s in zip(PAD_X, LABELS, strict=True)],
    "fab": _drawing() + _fab_only(),
}

SYMBOL = {
    "name": NAME,
    "desc": DESC,
    "footprint": NAME,
    "body": (-10.16, 10.16, 10.16, -10.16),
    "graphics": [
        ("rect", -10.16, 8.89, -3.56, 4.32),
        ("rect", -10.16, 7.62, -9.65, 5.59, "outline"),
        ("text", "IN", -6.6, 6.6, 1.0),
        ("rect", 3.56, 8.89, 10.16, 4.32),
        ("rect", 9.65, 7.62, 10.16, 5.59, "outline"),
        ("text", "OUT", 6.6, 6.6, 1.0),
        ("rect", -3.05, 3.81, 3.05, 1.27),
        ("text", "USB-C", 0, 2.54, 0.8),
        ("rect", -5.08, 0.51, 7.62, -9.14),
        ("text", "plug board", 1.27, -3.3, 0.8),
        ("text", "G D+ D- V", 1.27, -5.08, 0.8),
    ],
    "pins": [
        ("power_in", "G", "1", -12.7, 0, 0),
        ("bidirectional", "D+", "2", -12.7, -2.54, 0),
        ("bidirectional", "D-", "3", -12.7, -5.08, 0),
        ("passive", "V", "4", -12.7, -7.62, 0),
    ],
}
