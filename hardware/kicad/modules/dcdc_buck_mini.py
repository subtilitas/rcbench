"""Mini buck DC-DC module, 30 x 18mm, 4R7 inductor, input 6-20V, EN pad.

Hole grid from the dimensions printed on the vendor photo: pairs 2.54mm
apart, the inner holes of the + and - pairs 8.92mm apart, 26mm between the
input and output columns; every outer hole sits 2.0mm in from both edges.
The EN pad is read from the top-view photo (about 18.7px/mm), within about
0.5mm (estimate). Output voltage: not stated on the photos.
"""

NAME = "DCDC_Buck_Mini_30x18mm_EN"
DESC = ("Mini buck DC-DC module 30 x 18mm, 4R7 inductor, input 6-20V. "
        "EN: high = on, low = off, open = on. "
        "Pads: 1 IN+, 2 IN-, 3 EN, 4 OUT+, 5 OUT-; each terminal is a hole "
        "pair 2.54mm apart. Drawn from vendor photos; check against a board.")


def _pair(num, x, ys, first_square=False):
    return [{"num": num, "x": x, "y": y,
             "shape": "rect" if first_square and i == 0 else "circle",
             "size": (1.7, 1.7), "drill": 1.0} for i, y in enumerate(ys)]


TOP, BOTTOM = (2.0, 4.54), (13.46, 16.0)

FOOTPRINT = {
    "name": NAME,
    "desc": DESC,
    "board": (30.0, 18.0),
    "origin": (2.0, 2.0),
    "pads": (_pair("1", 2.0, TOP, first_square=True) + _pair("2", 2.0, BOTTOM)
             + [{"num": "3", "x": 9.4, "y": 17.0, "shape": "circle",
                 "size": (1.4, 1.4), "drill": 0.8}]
             + _pair("4", 28.0, TOP) + _pair("5", 28.0, BOTTOM)),
    "silk": [("text", "IN+", 4.9, 3.27, 1.0), ("text", "IN-", 4.9, 14.73, 1.0),
             ("text", "OUT+", 24.6, 3.27, 1.0),
             ("text", "OUT-", 24.6, 14.73, 1.0),
             ("text", "EN", 9.4, 15.3, 1.0)],
    "fab": [("rect", 7.8, 0.4, 18.5, 10.4), ("text", "4R7", 13.15, 5.4, 1.0)],
}

SYMBOL = {
    "name": NAME,
    "desc": DESC,
    "footprint": NAME,
    "body": (-10.16, 5.08, 10.16, -7.62),
    "graphics": [("text", "BUCK", 0, 1.27, 1.27),
                 ("text", "6-20V in", 0, -1.27, 1.0)],
    "pins": [
        ("power_in", "IN+", "1", -12.7, 2.54, 0),
        ("input", "EN", "3", -12.7, -2.54, 0),
        ("power_in", "IN-", "2", -12.7, -5.08, 0),
        ("power_out", "OUT+", "4", 12.7, 2.54, 180),
        ("passive", "OUT-", "5", 12.7, -5.08, 180),
    ],
}

# 3D model. The inductor height (4.5mm) is a part class figure, not
# measured; other parts are not drawn. No source for this board was found.
MODEL = {
    "mount": "header",
    "pins": ["1", "2", "3", "4", "5"],
    "bodies": [("box", 7.8, 0.4, 18.5, 10.4, 4.5, "chip")],
}
