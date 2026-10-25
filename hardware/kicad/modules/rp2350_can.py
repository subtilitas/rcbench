"""Waveshare RP2350-CAN, 21.00 x 51.00mm, 40 pads in the Pico pinout.

Pad grid from the maker's dimension drawing (Waveshare wiki RP2350-CAN,
800px-RP2350-CAN-details-size.jpg): board 21.00 x 51.00mm, pad 1 centre
1.61mm from the long side and 1.37mm from the USB end, pitch 2.54mm, rows
17.78mm apart. The pad grid is exact to the drawing. Each pad is a
castellated half-hole at the edge plus a 1.0mm hole at the grid point; the
footprint has THT pads at the grid points for pin headers (the owner's
module carries soldered headers) and draws the half-holes on F.Fab.

Parts drawn top view are scaled from the same drawing (9.68px/mm both axes,
board edges detected at x 297.5px, y 102px), within about 0.3mm (estimate):
USB-C receptacle 8.95mm wide, 6.17mm from the right edge (maker figures),
front edge about 1.2mm beyond the board end (photo); BOOT and RESET
buttons; RP2350 (QFN-60, 7 x 7mm); XL2515QF20 CAN controller (QFN-20);
SIT65HVD230DR CAN transceiver (SOIC-8); slide switch SW1 that puts 120Ω
across CANH and CANL (part names from the Waveshare schematic PDF); SWD
pads at 2.54mm pitch, square pad
SWCLK, then GND and SWDIO (Waveshare pinout image, bottom view mirrored);
3-way CAN screw terminal, 3.5mm pitch (photo, 3.58mm measured), order H G L
from the pad 20 side (Waveshare pinout image). The terminal is drawn on
F.Fab only; it has no pads. Unknown: terminal and USB-C heights, the
SWD pad drill, which slider position of SW1 is ON in the top view.
"""

NAME = "RP2350-CAN"
DESC = ("Waveshare RP2350-CAN, 21.00 x 51.00mm, 40 castellated pads with "
        "1.0mm holes, Pico pinout: pads 1-20 left, 21-40 right, pitch "
        "2.54mm, rows 17.78mm apart, USB-C at the 1/40 end. GP8-GP12 drive "
        "the on-board XL2515 CAN controller over SPI1: GP8 INT (active low), "
        "GP9 CS, GP10 SCK, GP11 MOSI, GP12 MISO. The 3-way CAN terminal "
        "(CANH, GND, CANL from the pad 20 side) at the 20/21 end has no "
        "pads. Pad grid from the Waveshare dimension drawing.")

W, H = 21.0, 51.0
X_L, X_R = 1.61, 19.39
Y0, P = 1.37, 2.54


def _pad_xy(n):
    """Grid point of pad ``n``: 1-20 down the left side, 21-40 up the right."""
    if n <= 20:
        return X_L, Y0 + (n - 1) * P
    return X_R, Y0 + (40 - n) * P


USB = (5.88, -1.2, 14.83, 6.6)
BOOT = (3.0, 10.2, 6.3, 14.3)
RESET = (14.7, 10.2, 18.0, 14.3)
MCU = (7.25, 20.1, 14.25, 27.1)
XTAL1 = (4.6, 28.2, 7.0, 31.0)
XTAL2 = (9.6, 32.3, 12.4, 35.0)
CANC = (2.8, 32.65, 6.8, 36.65)
XCVR = (13.15, 33.55, 18.05, 37.45)
SWITCH = (7.8, 40.0, 13.0, 43.0)
SWD_Y = 29.8
SWD_X = (11.08, 13.62, 16.16)
TERM = (4.65, 43.6, 15.95, 50.9)
SCREWS = (6.8, 10.3, 13.8)


def _button(box):
    x0, y0, x1, y1 = box
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    return [("rect",) + box, ("circle", cx, cy, 1.0)]


def _drawing():
    """Parts seen from the top, on F.SilkS and F.Fab."""
    x0, y0, x1, y1 = USB
    items = [("rect",) + USB,
             ("rect", x0 + 2.9, y0, x1 - 2.9, y0 + 0.6)]
    items += _button(BOOT) + _button(RESET)
    items += [("rect",) + MCU, ("circle", MCU[0] + 0.7, MCU[1] + 0.7, 0.25),
              ("rect",) + CANC, ("rect",) + XTAL1, ("rect",) + XTAL2,
              ("rect",) + XCVR]
    for i in range(4):
        x = 13.7 + i * 1.27
        items += [("line", x, 32.3, x, 33.55), ("line", x, 37.45, x, 38.7)]
    sx0, sy0, sx1, sy1 = SWITCH
    items += [("rect",) + SWITCH,
              ("rect", 10.3, 41.0, 11.3, 42.0, "fill"),
              ("line", 6.3, 41.5, sx0, 41.5), ("line", sx1, 41.5, 15.5, 41.5)]
    items += [("rect", 9.8, 28.2, 17.5, 31.4),
              ("rect", SWD_X[0] - 0.8, SWD_Y - 0.8, SWD_X[0] + 0.8,
               SWD_Y + 0.8),
              ("circle", SWD_X[1], SWD_Y, 0.8),
              ("circle", SWD_X[2], SWD_Y, 0.8)]
    return items


def _fab_only():
    """Castellations, CAN terminal and labels, on F.Fab only."""
    items = []
    for n in range(1, 41):
        x, y = _pad_xy(n)
        items.append(("circle", 0.0 if n <= 20 else W, y, 0.45))
    tx0, ty0, tx1, ty1 = TERM
    items.append(("rect",) + TERM)
    for x, s in zip(SCREWS, ("H", "G", "L"), strict=True):
        items += [("circle", x, 48.1, 1.1),
                  ("line", x - 0.8, 48.9, x + 0.8, 47.3),
                  ("rect", x - 1.2, 50.1, x + 1.2, ty1),
                  ("text", s, x, 45.2, 1.0)]
    items += [("text", "CAN", 10.3, 46.6, 0.7),
              ("text", "USB-C", 10.36, 3.0, 1.0),
              ("text", "RP2350", 10.75, 21.9, 1.0),
              ("text", "XL2515", 4.8, 34.65, 0.6),
              ("text", "SWCLK GND SWDIO", 13.62, 27.6, 0.5),
              ("text", "ON", 14.0, 40.3, 0.6),
              ("text", "1", 3.0, Y0, 0.8), ("text", "20", 3.2, 49.63, 0.8),
              ("text", "21", 17.8, 49.63, 0.8), ("text", "40", 17.8, Y0, 0.8)]
    return items


PADS = []
for _n in range(1, 41):
    _x, _y = _pad_xy(_n)
    PADS.append({"num": str(_n), "x": _x, "y": _y,
                 "shape": "rect" if _n == 1 else "circle",
                 "size": (1.7, 1.7), "drill": 1.0})

FOOTPRINT = {
    "name": NAME,
    "desc": DESC,
    "datasheet": "https://www.waveshare.com/wiki/RP2350-CAN",
    "board": (W, H),
    "origin": (X_L, Y0),
    "pads": PADS,
    "silk": _drawing() + [("text", "BOOT", 4.65, 15.4, 0.7),
                          ("text", "RESET", 16.35, 15.4, 0.7),
                          ("text", "1", 3.0, Y0, 0.8)],
    "fab": _drawing() + _fab_only(),
}

# Pins as in the RP2350-CAN symbol of rcbench_modules.kicad_sym (2026-10-24):
# names, numbers, types and places.
_LEFT = [("bidirectional", "GP0"), ("bidirectional", "GP1"),
         ("power_in", "GND"), ("bidirectional", "GP2"),
         ("bidirectional", "GP3"), ("bidirectional", "GP4"),
         ("bidirectional", "GP5"), ("power_in", "GND"),
         ("bidirectional", "GP6"), ("bidirectional", "GP7"),
         ("bidirectional", "GP8_CAN_INT"), ("bidirectional", "GP9_CAN_CS"),
         ("power_in", "GND"), ("bidirectional", "GP10_CAN_SCK"),
         ("bidirectional", "GP11_CAN_MOSI"),
         ("bidirectional", "GP12_CAN_MISO"), ("bidirectional", "GP13"),
         ("power_in", "GND"), ("bidirectional", "GP14"),
         ("bidirectional", "GP15")]
_RIGHT = [("passive", "VBUS"), ("power_in", "VSYS"), ("power_in", "GND"),
          ("passive", "3V3_EN"), ("power_out", "3V3"),
          ("power_in", "ADC_VREF"), ("bidirectional", "GP28_ADC2"),
          ("power_in", "AGND"), ("bidirectional", "GP27_ADC1"),
          ("bidirectional", "GP26_ADC0"), ("passive", "RUN"),
          ("bidirectional", "GP22"), ("power_in", "GND"),
          ("bidirectional", "GP21"), ("bidirectional", "GP20"),
          ("bidirectional", "GP19"), ("bidirectional", "GP18"),
          ("power_in", "GND"), ("bidirectional", "GP17"),
          ("bidirectional", "GP16")]

_PINS = ([(k, nm, str(i + 1), -17.78, 25.4 - i * 2.54, 0)
          for i, (k, nm) in enumerate(_LEFT)]
         + [(k, nm, str(40 - i), 17.78, 25.4 - i * 2.54, 180)
            for i, (k, nm) in enumerate(_RIGHT)])

SYMBOL = {
    "name": NAME,
    "desc": DESC,
    "datasheet": "https://www.waveshare.com/wiki/RP2350-CAN",
    "footprint": NAME,
    "body": (-15.24, 27.94, 15.24, -25.4),
    "graphics": [
        # USB-C receptacle at the 1/40 end
        ("rect", -3.81, 27.94, 3.81, 23.62),
        ("rect", -1.9, 27.94, 1.9, 27.43, "outline"),
        ("text", "USB-C", 0, 25.4, 1.0),
        # BOOT and RESET
        ("rect", -5.08, 21.59, -2.54, 19.05),
        ("text", "BOOT", -3.81, 17.78, 0.8),
        ("rect", 1.27, 21.59, 3.81, 19.05),
        ("text", "RESET", 2.54, 17.78, 0.8),
        # RP2350
        ("rect", -5.08, 11.43, 2.54, 3.81),
        ("text", "RP2350", -1.27, 7.62, 1.0),
        # XL2515 and transceiver
        ("rect", 1.52, -3.81, 7.62, -8.89),
        ("text", "XL2515", 4.57, -6.35, 0.8),
        ("rect", 1.52, -10.16, 7.62, -13.97),
        ("text", "SIT65HVD230", 4.57, -12.07, 0.5),
        # termination switch
        ("rect", -2.54, -15.24, 2.54, -16.76),
        ("rect", -0.25, -15.49, 1.27, -16.51, "outline"),
        ("text", "120R", 0, -17.78, 0.7),
        # CAN terminal at the 20/21 end
        ("rect", -5.08, -19.05, 5.08, -25.4),
        *[g for x, s in ((-3.3, "H"), (0, "G"), (3.3, "L")) for g in (
            ("rect", x - 1.0, -22.35, x + 1.0, -24.35),
            ("text", s, x, -20.32, 1.0))],
    ],
    "pins": _PINS,
}
