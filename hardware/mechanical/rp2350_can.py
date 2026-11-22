"""Waveshare RP2350-CAN, 21.0 x 51.0mm, blue, part by part.

    rp2350_can   the bare board: plate, 40 edge pads, the parts and the
                 print of both sides

The board of hardware/kicad/modules/rp2350_can.py; its pad grid is the
maker's dimension drawing.

Sources, none in the repository: Waveshare's product photos of 1600 x
1200px straight from above, top side and underside (rp2350-can-2.jpg
and rp2350-can-3.jpg), and the schematic RP2350-CAN-Schematic.pdf for
the parts' names. The top view is laid onto the board by 8 pad holes,
which fit within 0.02mm, and part places are read from it at 40px/mm,
within about 0.2mm. The underside's print is read at 19px/mm, within
about 0.4mm.

Drawn on the top side: the USB-C socket, 1.1 past the board's end; BOOT
and RESET buttons 3.3 x 4.3 with oval caps; the RP2350A (QFN-60, 7.0 x
7.0); the XL2515 (QFN-20, 4.0 x 4.0); the SIT65HVD230 (SOIC-8); the
flash memory and the buck converter as leadless packages of 3.0 x 2.0
and 3.5 x 2.0; 2 crystals; the inductor; a diode (SOD-123); a 3-lead
and a 5-lead small package; the slide switch 5.45 x 3.0 with its knob;
the 3-way screw terminal, 10.9 wide, 0.5 past the board's end; the LED;
52 chip parts; the 3 debug pads; the print. Underside: the 40 pad
names, the debug names, CAN with L, G, H, and "Waveshare".

Not measured: every height. The terminal is drawn 8.8 high, the height
of the KF350 terminal the schematic names; the buttons 2.0, the switch
1.5 with a knob to 2.3. The board is drawn 1.6 thick. The kinds of the
chip parts are read from their colour. Not drawn: the half holes at the
board's edges, the maker's sign on the underside, the print's frames
round single parts. The terminal's 3 wire openings, 2.4 x 2.6, are
drawn at the board's end, their size estimated. The print has no
underscore: 3V3_EN prints as "3V3 EN".

Mounted, the board stands on 2 pin headers of 20, its underside 2.5
over the carrier.

Frame: the KiCad model's. Origin at pad 1 on the board's underside (on
the carrier's top side for the mounted model), x to the right, y up.
Places below are mm from the board's upper left corner seen from the
top side, x to the right, y down, the USB-C socket at the top.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _board
import _parts as p
import _solid as s

kicad = _board.kicad_spec("rp2350_can")
STEM = "rp2350_can"
BLUE = (0.06, 0.20, 0.48)
TERMINAL_GREEN = (0.36, 0.74, 0.36)
W, H = kicad.W, kicad.H
B = _board.Board(kicad)

PADS = [kicad._pad_xy(n) for n in range(1, 41)]
EDGE_PAD = (2.4, 1.6)           # from the edge inwards, along the edge
USB = (10.55, 6.25)             # the shell's rear end
BUTTONS = ((4.6, 12.1), (16.3, 12.1))
BUTTON = (3.3, 4.3, 1.4, 1.9, 3.0, 2.0)  # body x, y, height; cap x, y, top
MCU = (10.55, 23.6)
CAN_CHIP = (5.55, 34.95)
TRANSCEIVER = (15.5, 35.9)
FLASH = (12.1, 11.25)
CONVERTER = (7.0, 18.0)
CRYSTALS = ((5.9, 30.05, 2.0, 2.5), (10.6, 33.8, 3.2, 2.5))
INDUCTOR = (10.9, 14.8)
CHOKE = (12.6, 17.8)
DIODE = (16.9, 4.1)
SMALL_3 = (8.6, 12.1)           # 3 leads, 1.25 x 2.0
SMALL_TVS = (10.4, 38.0)        # SOT-23, the CAN lines' protection
SWITCH = (10.5, 41.45, 5.45, 3.0, 1.5)
KNOB = (10.45, 41.5)
TERMINAL = (5.0, 44.05, 15.9, 51.5, 8.8)
SCREWS = ((6.6, 48.4), (10.25, 48.4), (13.8, 48.4))
ENTRY = (2.4, 2.6, 1.2, 3.0)    # a wire opening: width, height, its
#                                 sill over the board, depth
TERMINAL_PINS = ((6.9, 48.1), (10.4, 48.1), (13.9, 48.1))
DEBUG = [(x, kicad.SWD_Y) for x in kicad.SWD_X]
LED = (4.1, 3.75)
TEST_PAD = (6.9, 40.3)
# Chip parts: (x, y, turn). The turn is 90 where the part's length runs
# along y.
C0805 = ((8.4, 14.9, 90.0), (13.5, 14.9, 90.0))
C0603 = ((10.8, 17.75, 90.0), (14.5, 17.75, 90.0), (16.2, 19.55, 0.0),
         (5.05, 23.3, 0.0), (4.55, 40.1, 0.0))
R0402 = tuple((6.43 + 0.8 * i, 8.25, 90.0) for i in range(12)) + tuple(
    (x, 10.3, 90.0) for x in (7.2, 8.0, 8.75, 9.55)) + tuple(
    (16.2, 20.5 + 0.81 * i, 0.0) for i in range(5)) + (
    (4.1, 6.4, 90.0), (7.05, 14.8, 90.0), (9.8, 17.1, 90.0),
    (9.8, 18.5, 90.0), (15.6, 17.75, 90.0), (16.4, 17.75, 90.0),
    (5.05, 20.5, 0.0), (5.05, 22.3, 0.0), (5.05, 25.25, 0.0),
    (5.05, 25.9, 0.0), (16.4, 26.05, 0.0), (3.8, 29.9, 90.0),
    (7.9, 29.0, 90.0), (7.9, 30.4, 90.0), (8.8, 29.75, 90.0),
    (9.0, 35.8, 0.0), (10.5, 35.8, 0.0), (12.0, 35.8, 0.0),
    (4.1, 38.75, 90.0), (5.05, 38.75, 90.0), (6.2, 38.75, 90.0),
    (7.05, 38.75, 90.0), (16.4, 41.25, 90.0), (17.25, 41.25, 90.0))
FRAME = (9.6, 28.25, 17.4, 31.6)
# The print round the terminal: 3 lines, open at the board's end.
TERMINAL_LINES = ((4.55, 43.05, 16.05, 43.05), (4.55, 43.22, 4.55, 43.9),
                  (16.05, 43.22, 16.05, 43.9))
TOP_PRINT = (("1", 2.95, 1.4, 0.8, 0.4), ("40", 17.75, 1.3, 0.8, 1.1),
             ("20", 3.2, 49.6, 0.8, 1.1), ("21", 17.75, 49.6, 0.8, 1.1),
             ("BOOT", 4.6, 15.7, 0.9, 3.1), ("RESET", 16.3, 15.7, 0.9, 3.6),
             ("RP2350-CAN", 3.4, 22.0, 1.2, 11.2, 90.0))
LEFT_NAMES = ("GP0", "GP1", "GND", "GP2", "GP3", "GP4", "GP5", "GND",
              "GP6", "GP7", "GP8", "GP9", "GND", "GP10", "GP11", "GP12",
              "GP13", "GND", "GP14", "GP15")
RIGHT_NAMES = ("VBUS", "VSYS", "GND", "3V3 EN", "3V3", "ADC VREF",
               "GP28 A2", "AGND", "GP27 A1", "GP26 A0", "RUN", "GP22",
               "GND", "GP21", "GP20", "GP19", "GP18", "GND", "GP17",
               "GP16")
NAME = (0.85, 0.63, 3.05)       # letter height, width, gap from the edge
GP22_LIFT = 1.5                 # its name stands clear of the debug pads
UNDER_PRINT = (("CAN", 10.5, 45.6, 1.6, 4.8),
               ("H", 6.96, 50.2, 1.1, 0.9), ("G", 10.5, 50.2, 1.1, 0.9),
               ("L", 14.06, 50.2, 1.1, 0.9),
               ("Waveshare", 10.5, 42.0, 1.1, 7.4),
               ("SWCLK", 11.25, 26.9, 0.8, 3.9, 90.0),
               ("GND", 13.2, 27.7, 0.8, 2.2, 90.0),
               ("SWDIO", 14.5, 26.9, 0.8, 3.9, 90.0))
UNDER_FRAME = (5.7, 44.35, 15.35, 50.9)


def at(x, y):
    """A place from the board's upper left corner as Board takes it."""
    return x - W / 2, y - H / 2


def _frame(box, z):
    x0, y0, x1, y1 = box
    (ax, ay), (bx, by) = B.at(*at(x0, y0)), B.at(*at(x1, y1))
    return s.place(p.frame(ax, by, bx, ay), z=z)


def board():
    """The plate, its 40 edge pads, the debug pads, the terminal's holes."""
    holes = [(x, y, 1.0) for x, y in PADS] + [
        (x, y, 0.9) for x, y in DEBUG] + [(x, y, 1.2)
                                          for x, y in TERMINAL_PINS]
    out = B.plate(BLUE, B.outline(0.5), [(*at(x, y), d) for x, y, d in holes])
    reach, wide = EDGE_PAD
    for x, y in PADS:
        x0, x1 = (0.0, reach) if x < W / 2 else (W - reach, W)
        (ax, ay), (bx, by) = (B.at(*at(x0, y - wide / 2)),
                              B.at(*at(x1, y + wide / 2)))
        cut = [s.circle(*B.at(*at(x, y)), 0.5)]
        for z0 in (-p.PAD, B.t):
            out.append(s.ext(s.rect(ax, by, bx, ay), z0, z0 + p.PAD, p.GOLD,
                             holes=cut))
    for i, (x, y) in enumerate(DEBUG):
        out += B.through(p.hole_pad(0.9, 1.7, B.t, p.GOLD, square=i == 0),
                         *at(x, y))
    return out


def button():
    """A push button with an oval cap; its center the origin."""
    wide, deep, high, cap_x, cap_y, top = BUTTON
    return [s.box(-wide / 2, -deep / 2, wide / 2, deep / 2, 0.0, high,
                  p.STEEL),
            s.ext(s.stadium(-cap_x / 2, -cap_y / 2, cap_x / 2, cap_y / 2),
                  high, top, p.CREAM)]


def crystal(wide, deep):
    """A crystal in a metal-lidded package; its center the origin."""
    return [s.box(-wide / 2, -deep / 2, wide / 2, deep / 2, 0.0, 0.3,
                  p.CREAM),
            s.ext(s.rounded(-wide / 2 + 0.15, -deep / 2 + 0.15,
                            wide / 2 - 0.15, deep / 2 - 0.15, 0.2), 0.3,
                  0.7, p.STEEL)]


def slide_switch():
    """The slide switch: body, 2 leads; its center the origin."""
    _x, _y, wide, deep, high = SWITCH
    out = [s.box(-wide / 2, -deep / 2, wide / 2, deep / 2, 0.0, high,
                 p.EPOXY)]
    for side in (-1, 1):
        a, b = sorted((side * wide / 2, side * (wide / 2 + 1.6)))
        out.append(s.box(a, -0.3, b, 0.3, 0.0, 0.3, p.GOLD))
    return out


def terminal():
    """The 3-way screw terminal, in the board's places.

    The wires enter at the board's end: an opening ENTRY wide and high
    under each screw, with the clamp's steel behind it.
    """
    x0, y0, x1, y1, high = TERMINAL
    wide, tall, sill, deep = ENTRY
    (ax, ay), (bx, by) = B.at(*at(x0, y0)), B.at(*at(x1, y1))
    low = B.t + high - 2.0
    wells = [s.circle(*B.at(*at(x, y)), 1.6) for x, y in SCREWS]
    mouths = [s.rect(cx - wide / 2, B.t + sill, cx + wide / 2,
                     B.t + sill + tall)
              for cx in (B.at(*at(x, y))[0] for x, y in SCREWS)]
    out = [s.ext(s.rect(ax, by + deep, bx, ay), B.t, low, TERMINAL_GREEN),
           s.ext(s.rect(ax, B.t, bx, low), -(by + deep), -by,
                 TERMINAL_GREEN, holes=mouths, frame=s.Y),
           s.ext(s.rect(ax, by, bx, ay), low, B.t + high, TERMINAL_GREEN,
                 holes=wells)]
    for x, y in SCREWS:
        cx, cy = B.at(*at(x, y))
        out.append(s.ext(s.circle(cx, cy, 1.4), low, B.t + high - 0.6,
                         p.STEEL, holes=[s.rect(cx - 1.0, cy - 0.2,
                                                cx + 1.0, cy + 0.2)]))
        out.append(s.box(cx - 1.0, cy - 0.2, cx + 1.0, cy + 0.2, low,
                         B.t + high - 1.0, p.STEEL))
        out.append(s.box(cx - wide / 2, by + deep - 1.0, cx + wide / 2,
                         by + deep, B.t + sill, B.t + sill + tall, p.STEEL))
    for x, y in TERMINAL_PINS:
        cx, cy = B.at(*at(x, y))
        out.append(s.box(cx - 0.4, cy - 0.4, cx + 0.4, cy + 0.4, -1.5, B.t,
                         p.TIN))
    return out


def top_side():
    """The parts and the print of the top side."""
    out = B.top(p.usb_c(), *at(*USB), 180.0)
    for x, y in BUTTONS:
        out += B.top(button(), *at(x, y))
    out += B.top(p.qfn(7.0, 7.0, 0.9, 15, 15, 0.4), *at(*MCU))
    out += B.top(p.qfn(4.0, 4.0, 0.75, 5, 5, 0.5), *at(*CAN_CHIP))
    out += B.top(p.soic8(), *at(*TRANSCEIVER), 90.0)
    out += B.top(p.qfn(3.0, 2.0, 0.55, 4, 0, 0.5), *at(*FLASH))
    out += B.top(p.qfn(3.5, 2.0, 0.75, 0, 4, 0.5), *at(*CONVERTER))
    for x, y, wide, deep in CRYSTALS:
        out += B.top(crystal(wide, deep), *at(x, y))
    out += B.top(p.chip(2.9, 2.0, 1.2, p.EPOXY), *at(*INDUCTOR))
    out += B.top(p.block(2.4, 2.0, 1.0, p.EPOXY), *at(*CHOKE))
    out += B.top(p.sod(2.7, 1.6, 1.1, 3.7, 0.6), *at(*DIODE), 90.0)
    out += B.top(p.leaded(1.25, 2.0, 0.95, [0.0], p.rows(2, 1.3), 2.1,
                          0.3), *at(*SMALL_3))
    out += B.top(p.sot23(1, 2), *at(*SMALL_TVS))
    out += B.top(slide_switch(), *at(*SWITCH[:2]))
    out += B.top([s.box(-0.35, -0.3, 0.35, 0.3, SWITCH[4], SWITCH[4] + 0.8,
                        p.CREAM)], *at(*KNOB))
    out += B.top(p.text("ON", 0.6, p.WHITE, 1.0), *at(12.45, 41.0), 90.0,
                 z=SWITCH[4])
    out += B.top(p.led0603(), *at(*LED), 90.0)
    out += B.top([s.ext(s.circle(0.0, 0.0, 0.5), 0.0, p.PAD, p.TIN)],
                 *at(*TEST_PAD))
    for part, places in ((p.c0805, C0805), (p.c0603, C0603),
                         (p.r0402, R0402)):
        for x, y, turn in places:
            out += B.top(part(), *at(x, y), turn)
    out += _frame(FRAME, B.t)
    for x0, y0, x1, y1 in TERMINAL_LINES:
        (ax, ay), (bx, by) = B.at(*at(x0, y0)), B.at(*at(x1, y1))
        out.append(s.box(min(ax, bx) - 0.08, min(ay, by) - 0.08,
                         max(ax, bx) + 0.08, max(ay, by) + 0.08, B.t,
                         B.t + p.PRINT, p.WHITE))
    return out + terminal() + B.words(
        [(t, *at(x, y), *rest) for t, x, y, *rest in TOP_PRINT])


def underside():
    """The print of the underside: the pads' names and the rest."""
    high, wide, gap = NAME
    words = list(UNDER_PRINT)
    for names, pads, side in ((LEFT_NAMES, PADS[:20], 1),
                              (RIGHT_NAMES, PADS[:19:-1], -1)):
        for name, (_x, y) in zip(names, pads, strict=True):
            length = wide * len(name)
            edge = 0.0 if side > 0 else W
            lift = GP22_LIFT if name == "GP22" else 0.0
            words.append((name, edge + side * (gap + length / 2), y - lift,
                          high, length))
    x0, y0, x1, y1 = UNDER_FRAME
    (ax, ay), (bx, by) = B.at(*at(x0, y0)), B.at(*at(x1, y1))
    frame = s.place(p.frame(ax, by, bx, ay), z=-p.PRINT)
    return frame + B.words([(t, *at(x, y), *rest)
                            for t, x, y, *rest in words], under=True)


def bare():
    """The board without its headers, as shapes."""
    return board() + top_side() + underside()


def mounted():
    """The board on 2 pin headers of 20, as shapes.

    The headers' plastic stands on z = 0 and the board's underside pads
    lie on it.
    """
    out = s.place(bare(), z=2.5 + p.PAD + p.PRINT)
    header = p.pin_header(20, below=6.0, above=3.0)
    y = (PADS[0][1] + PADS[19][1]) / 2
    for x in (kicad.X_L, kicad.X_R):
        out += B.through(header, *at(x, y), 90.0)
    return out


def kicad_model():
    """The model of the KiCad footprint: the board on its headers."""
    return s.items(mounted())


def parts(_colors):
    """The model of the bare board.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items(bare())}
