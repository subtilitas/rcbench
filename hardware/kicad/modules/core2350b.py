"""Waveshare Core2350B, 25.40 x 25.40mm, 64 plated holes at 2.54mm.

An RP2350B core board: the RP2350B (QFN-80), 16MB QSPI (quad serial
peripheral interface) flash, an optional 2MB PSRAM (pseudo-static RAM)
on the underside, a 12MHz crystal, a 3.3V regulator and a 6-pin flat
cable socket for the USB adapter board. The 64 holes form a 10 x 10
grid at 2.54mm with the middle 6 x 6 left out: two rows round each
edge. The board has no castellations and no buttons; BOOT and RESET
are on the USB adapter board.

Sources:
    D: Waveshare's dimension drawing Core2350B0-details-size.jpg
    (waveshare.com/wiki/Core2350B0; not in the repository): board
    25.40 x 25.40mm, hole centers 22.86mm apart corner to corner,
    2.54mm pitch, so the outer holes lie 1.27mm from each edge. The
    photo in D fits a 2.54mm grid with 0.66px (0.03mm) RMS (root mean
    square) error over the 64 holes; the board's edge in it lies 0.15mm
    inside D's 25.40mm.
    S: Waveshare's schematic Core2350B.pdf (same page): the four 2 x 8
    headers P1 to P4 and the name on each of their 64 pins; the 6-pin
    socket U4: 1 USB D-, 2 USB D+, 3 RUN (through 1kOhm), 4 BOOTSEL,
    5 GND, 6 VBUS; GPIO47 reaches its pad through a 0Ohm resistor
    (R11) and is also the PSRAM's chip select.
    P: Waveshare's product photos core2350b0-2.jpg (top) and
    core2350b0-3.jpg (underside), 1600 x 1200px, about 31.8px per mm.
    The holes fit a 2.54mm grid with 0.55px (0.02mm) RMS error; part
    positions are read off a mm grid laid on that fit, within about
    0.2mm. P gives the place of every pad on the grid (the names printed
    beside the holes agree with S), the parts, and the 6 octagonal
    GND pads.

Pad numbers are S's header pins: P1 pins 1-16 are pads 1-16, P2 pins
1-16 pads 17-32, P3 pins 1-16 pads 33-48, P4 pins 1-16 pads 49-64. Each
header covers 2 x 8 places of the ring, turned 90 degrees from the
next: seen from the top side with the flat cable socket at the top
right, P1 is the left edge (outer row odd pins, inner row even pins,
from the top), P2 the top edge (from the right), P3 the bottom edge
(from the left) and P4 the right edge (from the bottom). Pad 1, P1 pin
1, is GND in the top left corner.

The footprint is for 2.54mm pin headers: the underside carries the
flash, a SOIC-8 (small outline, 8 leads) about 1.8mm high, and the
PSRAM's place, so the board cannot lie flat on a carrier without a
cut-out of about 15.0 x 15.0mm under its middle. On 2.54mm headers
(2.5mm plastic) the underside clears the carrier by 2.5mm. The holes
are 1.0mm, for a 0.64mm square pin (0.91mm across its corners); the
pads are 1.7mm round, 0.1mm more than the board's own 1.6mm rings, and
leave 0.84mm between pads for one track.

Estimated, not measured: the board's corner radius (0.5mm), its
hole diameter (1.0mm; P shows 0.95mm), its thickness (1.6mm).

The 3D model is hardware/mechanical/core2350b.py.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import math

NAME = "Core2350B_Waveshare_25.4x25.4mm"
W = H = 25.40
RADIUS = 0.5
PITCH, EDGE = 2.54, 1.27
DRILL, PAD = 1.0, 1.7

# Each header's 16 pin names (S), pin 1 first.
HEADERS = (
    ("GND", "GP0", "GP2", "GP1", "GP4", "GP3", "GP6", "GP5", "GP8", "GP7",
     "GP10", "GP9", "GP12", "GP11", "GP14", "GP13"),
    ("GND", "GP45", "GP47", "GP46", "BOOTSEL", "SWDIO", "USB_DM", "GND",
     "USB_DP", "SWCLK", "RUN", "ADC_VREF", "3V3_EN", "GND", "VBUS", "3V3"),
    ("GND", "GP15", "GP17", "GP16", "GP19", "GP18", "GP21", "GP20", "GP23",
     "GP22", "GP25", "GP24", "GP27", "GP26", "GP29", "GP28"),
    ("GND", "GP30", "GP32", "GP31", "GP34", "GP33", "GP36", "GP35", "GP38",
     "GP37", "GP40", "GP39", "GP42", "GP41", "GP44", "GP43"),
)


def place(header, pin):
    """The grid place of a header pin.

    Args:
        header: 1 to 4 for P1 to P4.
        pin: 1 to 16.

    Returns:
        (column, row), both 0 to 9 from the top left, seen from the top
        side.
    """
    k, inner = (pin - 1) // 2, (pin - 1) % 2
    return {1: (inner, k), 2: (9 - k, inner), 3: (k, 9 - inner),
            4: (9 - inner, 9 - k)}[header]


def grid(col, row):
    """The board frame's (x, y) of a grid place."""
    return EDGE + PITCH * col, EDGE + PITCH * row


def at(x, y):
    """The board frame's (x, y) of a place given from the board's center."""
    return W / 2 + x, H / 2 + y


# Parts, seen from the top side, from the board's center (P): center
# and size along x and y. The flash and the PSRAM are on the underside;
# their places are given as seen through the board from the top.
MCU = ((-1.93, 2.0), (10.0, 10.0))
REGULATOR = ((-3.6, -5.8), (2.8, 2.9))
INDUCTOR = ((0.0, -5.15), (2.2, 1.85))
CRYSTAL = ((5.88, 6.45), (2.5, 2.0))
LED = ((5.95, 2.93), (1.6, 0.8))
# Flat cable socket: the housing, then the flip lock on the side the
# cable enters (towards the RP2350B), as (x0, y0, x1, y1).
FFC = (2.67, -7.22, 7.97, -5.33)
FFC_LOCK = (3.21, -5.33, 7.75, -3.67)
FLASH = ((-1.71, -1.70), (5.3, 5.3))
PSRAM = ((-1.62, 3.92), (5.3, 5.3))


def _box(part):
    """A part's outline as a footprint rectangle in the board frame."""
    (x, y), (w, h) = part
    return ("rect", *at(x - w / 2, y - h / 2), *at(x + w / 2, y + h / 2))


def _pads():
    pads = []
    for header in range(1, 5):
        for pin in range(1, 17):
            x, y = grid(*place(header, pin))
            num = 16 * (header - 1) + pin
            pads.append({"num": str(num), "x": x, "y": y,
                         "shape": "rect" if num == 1 else "circle",
                         "size": (PAD, PAD), "drill": DRILL})
    return sorted(pads, key=lambda p: int(p["num"]))


def _rounded(w, h, r, n=4):
    """Closed outline of a w x h board with corner radius r."""
    pts = []
    for cx, cy, a0 in ((w - r, r, -90), (w - r, h - r, 0), (r, h - r, 90),
                       (r, r, 180)):
        for k in range(n + 1):
            a = math.radians(a0 + 90 * k / n)
            pts.append((round(cx + r * math.cos(a), 3),
                        round(cy + r * math.sin(a), 3)))
    return pts


DESC = ("Waveshare Core2350B (RP2350B), 25.40 x 25.40mm, 64 plated holes "
        "of 1.0mm in a 10 x 10 grid at 2.54mm without its middle 6 x 6, "
        "for 2.54mm headers. Pads 1-64 = Waveshare's headers P1-P4, 16 "
        "pins each: P1 left edge, P2 top edge, P3 bottom edge, P4 right "
        "edge, with the flat cable socket at the top right; pad 1 = P1 "
        "pin 1 = GND, top left. GPIO0-47, VBUS, 3V3, 3V3_EN, ADC_VREF, "
        "RUN, BOOTSEL, SWCLK, SWDIO, USB_DP, USB_DM, 6 GND. GP40-GP47 are "
        "ADC0-ADC7; GP47 is the PSRAM's chip select. Flash and PSRAM on "
        "the underside. From the Waveshare dimension drawing and "
        "schematic.")

_TOP = [("rect", *at(*FFC[:2]), *at(*FFC[2:])),
        ("rect", *at(*FFC_LOCK[:2]), *at(*FFC_LOCK[2:])),
        _box(MCU)]

FOOTPRINT = {
    "name": NAME,
    "desc": DESC,
    "datasheet": "https://www.waveshare.com/wiki/Core2350B0",
    "board": (W, H),
    "outline": _rounded(W, H, RADIUS),
    "origin": grid(0, 0),
    "pads": _pads(),
    "silk": _TOP + [("circle", 2.54, 2.54, 0.3),
                    ("text", "FFC", *at(5.32, -6.3), 0.8)],
    "fab": _TOP + [_box(REGULATOR), _box(INDUCTOR), _box(CRYSTAL),
                   _box(LED), _box(FLASH), _box(PSRAM),
                   ("text", "RP2350B", *at(-1.93, -1.8), 1.2),
                   ("text", "flash, PSRAM underneath", *at(-1.93, 4.5),
                    0.6),
                   ("text", "FFC", *at(5.32, -6.3), 0.8),
                   ("text", "1", 2.54, 2.54, 0.7)],
}

# Symbol: system pins left, power at the top, GND at the bottom, GPIO
# on the left and right in order, ADC inputs last on the right.
_KIND = {"VBUS": "power_in", "3V3": "power_out", "3V3_EN": "input",
         "ADC_VREF": "passive", "RUN": "input", "BOOTSEL": "input",
         "SWCLK": "input", "GND": "power_in"}
_ADC = {f"GP{40 + i}": f"GP{40 + i}_ADC{i}" for i in range(8)}
_ADC["GP47"] = "GP47_ADC7_PSRAM_CS"
_LEFT = ["RUN", "BOOTSEL", "SWCLK", "SWDIO", "USB_DP", "USB_DM", None] + [
    f"GP{i}" for i in range(21)]
_RIGHT = [f"GP{i}" for i in range(21, 40)] + [None] + [
    f"GP{i}" for i in range(40, 48)]
_TOP_PINS = ["VBUS", "3V3", "3V3_EN", "ADC_VREF"]
_ROW0 = 33.02


def _numbers():
    """Pad numbers of each pin name; GND has 6."""
    out = {}
    for header, names in enumerate(HEADERS, 1):
        for pin, name in enumerate(names, 1):
            out.setdefault(name, []).append(str(16 * (header - 1) + pin))
    return out


def _pins():
    nums = _numbers()
    pins = []
    for side, x, rot in ((_LEFT, -20.32, 0), (_RIGHT, 20.32, 180)):
        for i, name in enumerate(side):
            if name is None:
                continue
            kind = _KIND.get(name, "bidirectional")
            pins.append((kind, _ADC.get(name, name), nums[name][0], x,
                         _ROW0 - 2.54 * i, rot))
    for i, name in enumerate(_TOP_PINS):
        pins.append((_KIND[name], name, nums[name][0], -3.81 + 2.54 * i,
                     38.1, 270))
    for i, num in enumerate(nums["GND"]):
        pins.append(("power_in", "GND", num, -6.35 + 2.54 * i, -45.72, 90))
    return pins


SYMBOL = {
    "name": NAME,
    "desc": DESC,
    "datasheet": "https://www.waveshare.com/wiki/Core2350B0",
    "footprint": NAME,
    "body": (-17.78, 35.56, 17.78, -43.18),
    "graphics": [("text", "Core2350B", 0, 2.54, 1.524),
                 ("text", "RP2350B", 0, 0, 1.27)],
    "pins": _pins(),
}

MODEL = {"detail": "core2350b"}
