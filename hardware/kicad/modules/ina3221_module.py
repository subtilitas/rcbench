"""DAOKAI INA3221 board (CJMCU-3221 style), 38.0 x 31.8mm, 3 channels.

Measured from the owner's photos 16-ina3221-front.webp (component side) and
17-ina3221-back.webp. Both photos are rectified by their 4 board corners to
a rectangle of aspect 1.197 (both photos agree within 0.1%); the scale comes
from the 8-hole header, 7 x 2.54mm = 561px in the rectified front photo, so
31.54px/mm. The back photo, rectified and mirrored top to bottom, gives the
same hole spacings within 0.1mm and the same absolute positions within
0.3mm. Positions are within about 0.3mm (estimate, not checked with a
caliper); spacings of 5.08mm are taken where the photo gives 5.04-5.11mm.

Holes: 8-hole header 2.54mm, row 1.95mm below the top edge, pin 1 (square,
VS) 26.7mm from the left edge; 3 large plated holes at x 34.6mm, 5.08mm
apart, VPU on top, then GND, GND; 6 channel holes at y 28.65mm in 3 pairs
5.08mm apart, pair centres 6.99, 19.12 and 31.16mm; 3 mounting holes 3.2mm
at (2.85, 2.82), (2.93, 20.93) and (35.10, 20.88)mm, drawn on F.Fab only.
Large and channel holes: drill about 2.6mm, ring about 4.0mm (photo).
Header drill: 1.0mm by the photo's ring of 1.5mm; footprint pads 1.7mm.

Unknown: board thickness, whether the mounting holes connect to GND, the
exact drill of the large holes.
"""

NAME = "INA3221_Module"
DESC = ("DAOKAI/CJMCU-3221 INA3221 3-channel monitor, 38.0 x 31.8mm, three "
        "0.1Ω (R100) shunts. Pads 1-8 = header VS GND SCL SDA PV CRI WAR TC, "
        "2.54mm, pad 1 square at the right end; pad 2 is also on the two "
        "large GND holes (same net); pad 15 = large hole VPU; pads 9-14 = "
        "channel holes VIN1- VIN1+ VIN2- VIN2+ VIN3- VIN3+, 5.08mm within a "
        "pair. A0 is set by 4 solder-bridge pads at the left edge, not on "
        "the header. Positions scaled from the owner's photos, about 0.3mm; "
        "check with a caliper before ordering a PCB.")

HDR_Y = 1.95
PIN1_X = 26.7
NAMES = ("VS", "GND", "SCL", "SDA", "PV", "CRI", "WAR", "TC")
BIG_X = 34.6
BIG = (("15", 3.59), ("2", 8.67), ("2", 13.75))
CH_Y = 28.65
CH_C = (6.99, 19.12, 31.16)
MOUNT = ((2.85, 2.82), (2.93, 20.93), (35.10, 20.88))
LED_Y = (5.94, 7.44, 8.94, 10.44, 11.94)


def _pads():
    pads = [{"num": str(i + 1), "x": PIN1_X - i * 2.54, "y": HDR_Y,
             "shape": "rect" if i == 0 else "circle", "size": (1.7, 1.7),
             "drill": 1.0} for i in range(8)]
    pads += [{"num": n, "x": BIG_X, "y": y, "shape": "circle",
              "size": (4.0, 4.0), "drill": 2.6} for n, y in BIG]
    num = 9
    for c in CH_C:
        for dx in (-2.54, 2.54):
            pads.append({"num": str(num), "x": c + dx, "y": CH_Y,
                         "shape": "circle", "size": (4.0, 4.0), "drill": 2.6})
            num += 1
    return pads


def _board_print():
    """Silk boxes and labels as printed on the component side."""
    items = [("rect", 32.0, 0.95, 37.2, 16.3)]
    for c, label in zip(CH_C, ("CH1", "CH2", "CH3"), strict=True):
        items += [("rect", c - 5.0, 26.0, c + 5.1, 31.2),
                  ("text", label, c, 23.75, 1.2)]
    items += [("text", "VPU", 30.8, 3.59, 0.7),
              ("text", "GND", 30.8, 8.67, 0.7),
              ("text", "GND", 30.8, 13.75, 0.7)]
    return items


def _parts():
    """Chip, shunts, LEDs and A0 pads seen from the top."""
    items = [("rect", 14.4, 9.4, 18.4, 13.4)]
    for c in (10.75, 19.25, 27.57):
        items += [("rect", c - 3.15, 19.05, c + 3.15, 22.2),
                  ("line", c - 2.55, 19.05, c - 2.55, 22.2),
                  ("line", c + 2.55, 19.05, c + 2.55, 22.2)]
    return items


def _fab_only():
    items = []
    for x, y in MOUNT:
        items += [("circle", x, y, 1.6), ("circle", x, y, 1.95)]
    for y in LED_Y:
        items.append(("rect", 27.5, y - 0.4, 29.1, y + 0.4))
    for y in (7.75, 10.3, 12.9, 15.4):
        items += [("rect", 0.95, y - 0.3, 1.55, y + 0.3),
                  ("rect", 1.85, y - 0.3, 2.45, y + 0.3)]
    for i, s in enumerate(NAMES):
        items.append(("text", s, PIN1_X - i * 2.54, 3.6, 0.55))
    items += [("text", "INA3221", 16.4, 11.4, 0.6),
              ("text", "A0", 1.7, 6.0, 0.7),
              ("text", "R100", 10.75, 20.6, 0.8),
              ("text", "R100", 19.25, 20.6, 0.8),
              ("text", "R100", 27.57, 20.6, 0.8),
              ("text", "VIN1-", 4.45, 25.4, 0.5),
              ("text", "VIN1+", 9.53, 25.4, 0.5),
              ("text", "VIN2-", 16.58, 25.4, 0.5),
              ("text", "VIN2+", 21.66, 25.4, 0.5),
              ("text", "VIN3-", 28.62, 25.4, 0.5),
              ("text", "VIN3+", 33.70, 25.4, 0.5)]
    return items


FOOTPRINT = {
    "name": NAME,
    "desc": DESC,
    "datasheet": "https://www.ti.com/lit/ds/symlink/ina3221.pdf",
    "board": (38.0, 31.8),
    "origin": (PIN1_X, HDR_Y),
    "pads": _pads(),
    "silk": _board_print() + _parts(),
    "fab": _board_print() + _parts() + _fab_only(),
}

# Pins as in the INA3221_Module symbol of rcbench_modules.kicad_sym
# (2026-10-24): names, numbers, types and places.
SYMBOL = {
    "name": NAME,
    "desc": DESC,
    "datasheet": "https://www.ti.com/lit/ds/symlink/ina3221.pdf",
    "footprint": NAME,
    "body": (-7.62, 15.24, 7.62, -15.24),
    "graphics": [
        ("rect", 0.51, 2.54, 5.59, -2.54),
        ("text", "INA3221", 3.05, 0, 0.8),
        *[g for i, s in enumerate(("CH1", "CH2", "CH3")) for g in (
            ("rect", 0.76, -5.33 - i * 2.79, 6.6, -7.37 - i * 2.79),
            ("text", s + " R100", 3.68, -6.35 - i * 2.79, 0.7))],
    ],
    "pins": [
        ("power_in", "VS", "1", -10.16, 12.7, 0),
        ("power_in", "GND", "2", -10.16, 10.16, 0),
        ("input", "SCL", "3", -10.16, 7.62, 0),
        ("bidirectional", "SDA", "4", -10.16, 5.08, 0),
        ("passive", "VPU", "15", -10.16, 2.54, 0),
        ("passive", "VIN1-", "9", -10.16, 0, 0),
        ("passive", "VIN1+", "10", -10.16, -2.54, 0),
        ("passive", "VIN2-", "11", -10.16, -5.08, 0),
        ("passive", "VIN2+", "12", -10.16, -7.62, 0),
        ("passive", "VIN3-", "13", -10.16, -10.16, 0),
        ("passive", "VIN3+", "14", -10.16, -12.7, 0),
        ("output", "PV", "5", 10.16, 12.7, 180),
        ("output", "CRI", "6", 10.16, 10.16, 180),
        ("output", "WAR", "7", 10.16, 7.62, 180),
        ("output", "TC", "8", 10.16, 5.08, 180),
    ],
}
