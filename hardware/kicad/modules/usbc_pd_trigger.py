"""USB-C PD trigger board, 30 x 20mm, 3-way DIP switch, two output holes.

Measured from a vendor photo of the component side (32.6px/mm across,
33.05px/mm down). Positions are within about 0.2mm (estimate, not checked
against a board). The holes are the two Usb-Pwr holes, 5.08mm apart, 2.0mm
in from the right edge. Polarity follows the board's GND and VCC print:
upper hole GND, lower hole VCC.
"""

NAME = "USBC_PD_Trigger_DIP3_30x20mm"
DESC = ("USB-C PD trigger board 30 x 20mm, 3-way DIP switch. "
        "S1 S2 S3: 5V 110, 9V 111, 12V 101, 15V 001, 20V 011 (1 = ON). "
        "Pads: 1 = + (VCC), 2 = - (GND), the two Usb-Pwr holes, 5.08mm apart. "
        "Drawn from a vendor photo; check polarity with a meter.")

USB = (-1.1, 5.53, 6.4, 14.47)
DIP = (10.86, 0.97, 16.07, 6.57)
DIP_ROWS = (2.30, 3.57, 4.84)


def _drawing():
    x0, y0, x1, y1 = USB
    items = [("rect", x0, y0, x1, y1),
             ("rect", x0, y0 + 2.9, x0 + 0.6, y1 - 2.9)]
    for tx0, tx1 in ((0.45, 2.1), (4.6, 6.4)):
        items += [("rect", tx0, y0 - 0.5, tx1, y0),
                  ("rect", tx0, y1, tx1, y1 + 0.5)]
    items += [("line", 6.4, 7.1 + i * 0.5, 7.1, 7.1 + i * 0.5)
              for i in range(12)]
    items.append(("rect",) + DIP)
    for y in DIP_ROWS:
        items += [("line", 8.47, y, 10.86, y), ("line", 16.07, y, 17.5, y),
                  ("rect", 11.93, y - 0.45, 15.0, y + 0.45),
                  ("rect", 12.08, y - 0.3, 13.28, y + 0.3, "fill")]
    items += [("line", 16.4, 6.0, 17.9, 6.0), ("line", 17.9, 6.0, 17.4, 5.6),
              ("line", 17.9, 6.0, 17.4, 6.4)]
    return items


FOOTPRINT = {
    "name": NAME,
    "desc": DESC,
    "board": (30.0, 20.0),
    "origin": (28.0, 12.54),
    "pads": [
        {"num": "1", "x": 28.0, "y": 12.54, "shape": "rect",
         "size": (3.0, 3.0), "drill": 2.0},
        {"num": "2", "x": 28.0, "y": 7.46, "shape": "circle",
         "size": (3.0, 3.0), "drill": 2.0},
    ],
    "silk": _drawing() + [("text", "+", 25.4, 12.54, 1.5),
                          ("text", "-", 25.4, 7.46, 1.5),
                          ("text", "ON", 17.15, 7.2, 0.8)],
    "fab": _drawing() + [("text", f"S{i + 1}", 7.4, y, 0.6)
                         for i, y in enumerate(DIP_ROWS)]
    + [("text", "USB-C", 2.65, 10.0, 1.0)],
}

SYMBOL = {
    "name": NAME,
    "desc": DESC,
    "footprint": NAME,
    "body": (-12.7, 7.62, 12.7, -7.62),
    "graphics": [
        ("rect", -13.97, 3.43, -7.62, -3.43),
        ("rect", -13.97, 1.27, -13.46, -1.27),
        *[("poly", [(-7.62, 2.0 - i * 0.8), (-6.86, 2.0 - i * 0.8)])
          for i in range(6)],
        ("text", "USB-C", -10.8, -4.83, 1.0),
        ("rect", -2.54, 6.6, 3.81, 0.76),
        *[g for i, y in enumerate((5.08, 3.68, 2.29)) for g in (
            ("rect", -1.52, y + 0.45, 2.79, y - 0.45),
            ("rect", -1.37, y + 0.3, 0.13, y - 0.3, "outline"),
            ("poly", [(-3.56, y), (-2.54, y)]),
            ("poly", [(3.81, y), (4.83, y)]),
            ("text", f"S{i + 1}", -4.83, y, 0.8))],
        ("poly", [(5.33, 3.68), (6.86, 3.68)]),
        ("poly", [(6.35, 4.06), (6.86, 3.68), (6.35, 3.3)]),
        ("text", "ON", 6.1, 2.4, 0.8),
        ("text", "5V 110  9V 111", 3.81, -3.81, 0.9),
        ("text", "12V 101  15V 001", 3.81, -5.08, 0.9),
        ("text", "20V 011", 3.81, -6.35, 0.9),
    ],
    "pins": [
        ("power_out", "+", "1", 15.24, 2.54, 180),
        ("power_out", "-", "2", 15.24, -2.54, 180),
    ],
}

# 3D model. Heights are part class figures, not measured: USB-C receptacle
# 3.2mm, DIP switch 2.0mm with 0.4mm sliders. Parts beyond the receptacle
# and the switch are not drawn. Board colour not stated; drawn green. The
# board is wired, so it rests on the carrier.
MODEL = {
    "mount": "flat",
    "bodies": [("box",) + USB + (3.2, "metal"),
               ("box",) + DIP + (2.0, "red")]
    + [("box", 12.08, y - 0.3, 13.28, y + 0.3, 0.4, "white", 2.0)
       for y in DIP_ROWS],
}
