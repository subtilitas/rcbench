"""USB-C PD trigger board with a 3-way switch, 30.0 x 20.0mm, purple.

    usbc_pd_trigger   the bare board: plate, 2 output holes, 4 small
                      holes, the parts and the print of both sides

The board of hardware/kicad/modules/usbc_pd_trigger.py, sold as a
"PD/QC/AFC decoy trigger" with the print Usb-Pwr; sellers name the
CH224K as its chip.

Sources, none in the repository: a seller's picture of 517 x 405px with
the top side and the underside straight from above (punoscho.in,
USB-QC-PD-AFC.jpg), laid onto the board by its 4 corners and read at
10.6px/mm, places within about 0.5mm; a second seller's 3 angled
pictures of 800 x 800px (zaitronics.com.au) for the kinds of the parts
and the print Usb-Pwr; three owner photos of the board on a carrier,
which show it purple. The output holes fall within 0.3mm of the KiCad
spec's places.

Drawn on the top side: the USB-C socket, 1.1 past the board's edge, the
switch 5.8 x 5.55 with 3 sliders and 6 leads, the chip as a 10-lead
package of 3.9 x 4.9, a 3-lead regulator package of 2.5 x 4.5 with its
tab, 2 capacitors (1206), 9 small chip parts, the LED and the print.
Underside: the table of the 5 voltages against S1, S2 and S3.

Not measured: every height (switch 2.5, packages their class figures),
the board's thickness (drawn 1.6), the small holes' drill (1.0). The
kinds of the small chip parts are read from their colour. The sliders
stand at the switch's left; their real places set the voltage.

Mounted, the board lies on the carrier, held by its 2 output holes;
what joins them to the carrier is not drawn.

Frame: the KiCad model's. Origin at pad 1 on the board's underside (on
the carrier's top side for the mounted model), x to the right, y up.
Places below are mm from the board's upper left corner seen from the
top side, x to the right, y down, the USB-C socket at the left.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _board
import _parts as p
import _solid as s

kicad = _board.kicad_spec("usbc_pd_trigger")
STEM = "usbc_pd_trigger"
PURPLE = (0.30, 0.10, 0.42)
W, H = kicad.FOOTPRINT["board"]
B = _board.Board(kicad)

OUTPUT = [(pad["x"], pad["y"]) for pad in kicad.FOOTPRINT["pads"]]
SMALL = ((25.6, 0.8), (28.3, 0.8), (25.6, 19.2), (28.3, 19.2))
USB = (6.25, 9.9)               # the shell's rear end
SWITCH = (12.9, 3.3, 5.8, 5.55, 2.5)
SWITCH_ROWS = (2.03, 3.3, 4.57)
CHIP = (12.7, 9.7)
REGULATOR = (13.4, 16.95)
CAPS = ((2.75, 2.3), (5.75, 17.4))
RESISTORS = ((6.75, 1.2, 0.0), (6.75, 3.1, 0.0), (8.95, 13.25, 0.0),
             (18.5, 15.6, 90.0))
SMALL_CAP = (17.6, 12.1)
TINY = ((20.6, 11.8), (21.65, 11.8), (20.6, 13.5), (21.65, 13.5))
LED = (21.1, 16.75)
FRAME = (25.1, 5.05, 29.9, 15.2)
TOP_PRINT = (("GND", 27.1, 3.4, 1.5, 4.3, 180.0),
             ("VCC", 27.25, 16.6, 1.5, 4.0, 180.0),
             ("S1 S2 S3", 18.75, 5.2, 1.0, 5.4, -90.0),
             ("Pwr", 22.6, 16.6, 0.7, 1.9, -90.0),
             ("Usb-Pwr", 23.9, 10.1, 0.7, 4.2, -90.0))
# The underside's table, in the top side's places: its box, the lines
# between the voltages (along x) and between the switches (along y).
TABLE = (5.8, 2.9, 25.4, 18.1)
TABLE_X = (9.7, 15.6, 21.2)
TABLE_Y = (5.5, 8.3, 11.0, 13.5, 15.9)
VOLTAGES = (("20V", "011"), ("15V", "001"), ("12V", "101"), ("9V", "111"),
            ("5V", "110"))


def at(x, y):
    """A place from the board's upper left corner as Board takes it."""
    return x - W / 2, y - H / 2


def board():
    """The plate and the plating of its 6 holes."""
    out = B.plate(PURPLE, B.outline(0.3),
                  [(*at(x, y), 2.0) for x, y in OUTPUT]
                  + [(*at(x, y), 1.0) for x, y in SMALL])
    for i, (x, y) in enumerate(OUTPUT):
        out += B.through(p.hole_pad(2.0, 3.0, B.t, p.TIN, square=i == 0),
                         *at(x, y))
    for i, (x, y) in enumerate(SMALL):
        out += B.through(p.hole_pad(1.0, 1.6, B.t, p.TIN, square=i % 2 == 0),
                         *at(x, y))
    return out


def switch():
    """The 3-way switch: body, 3 sliders, 6 leads; its center the origin."""
    _x, _y, wide, deep, high = SWITCH
    out = [s.box(-wide / 2, -deep / 2, wide / 2, deep / 2, 0.1, high,
                 p.EPOXY)]
    for row in SWITCH_ROWS:
        y = SWITCH[1] - row
        out.append(s.box(-0.9, y - 0.3, -0.1, y + 0.3, high, high + 0.4,
                         p.WHITE))
        for side in (-1, 1):
            a, b = sorted((side * wide / 2, side * (wide / 2 + 1.6)))
            out.append(s.box(a, y - 0.3, b, y + 0.3, 0.0, 0.25, p.TIN))
    return out


def regulator():
    """A 3-lead package with a tab opposite, 2.5 x 4.5; leads at -x."""
    out = [s.box(-1.25, -2.25, 1.25, 2.25, 0.1, 1.5, p.EPOXY),
           s.box(1.25, -0.9, 2.2, 0.9, 0.0, 0.4, p.TIN)]
    for y in p.rows(3, 1.5):
        out.append(s.box(-2.2, y - 0.22, -1.25, y + 0.22, 0.0, 0.4, p.TIN))
    return out


def top_side():
    """The parts and the print of the top side."""
    out = B.top(p.usb_c(), *at(*USB), -90.0)
    out += B.top(switch(), *at(*SWITCH[:2]))
    out += B.top(p.leaded(3.9, 4.9, 1.5, p.rows(5, 1.0), p.rows(5, 1.0),
                          6.0, 0.4), *at(*CHIP))
    out += B.top(regulator(), *at(*REGULATOR))
    for x, y in CAPS:
        out += B.top(p.c1206(), *at(x, y))
    for x, y, turn in RESISTORS:
        out += B.top(p.r0603(), *at(x, y), turn)
    out += B.top(p.c0603(), *at(*SMALL_CAP), 90.0)
    for x, y in TINY:
        out += B.top(p.r0402(), *at(x, y), 90.0)
    out += B.top(p.led0603(), *at(*LED), 90.0)
    x0, y0, x1, y1 = FRAME
    (ax, ay), (bx, by) = B.at(*at(x0, y0)), B.at(*at(x1, y1))
    out += s.place(p.frame(ax, by, bx, ay), z=B.t)
    return out + B.words([(t, *at(x, y), *rest)
                          for t, x, y, *rest in TOP_PRINT])


def underside():
    """The underside's table: its lines, the voltages and the settings."""
    x0, y0, x1, y1 = TABLE
    half = 0.08
    xs = (x0,) + TABLE_X + (x1,)
    ys = (y0,) + TABLE_Y + (y1,)

    def cell(ax, ay, bx, by, grow):
        (px, py), (qx, qy) = B.at(*at(ax, ay)), B.at(*at(bx, by))
        return s.rect(min(px, qx) - grow, min(py, qy) - grow,
                      max(px, qx) + grow, max(py, qy) + grow)

    out = [s.ext(cell(x0, y0, x1, y1, half), -p.PRINT, 0.0, p.WHITE, holes=[
        cell(xa, ya, xb, yb, -half)
        for xa, xb in zip(xs, xs[1:], strict=False)
        for ya, yb in zip(ys, ys[1:], strict=False)])]
    words = []
    for k, (name, bits) in enumerate(VOLTAGES):
        y = (ys[k] + ys[k + 1]) / 2
        words.append((name, (xs[0] + xs[1]) / 2, y, 1.1, 0.8 * len(name),
                      180.0))
        for i, bit in enumerate(bits):
            words.append((bit, (xs[i + 1] + xs[i + 2]) / 2, y, 1.1, 0.7,
                          180.0))
    for i in range(3):
        words.append((f"S{i + 1}", (xs[i + 1] + xs[i + 2]) / 2,
                      (ys[5] + ys[6]) / 2, 1.1, 1.6, 180.0))
    return out + B.words([(t, *at(x, y), *rest)
                          for t, x, y, *rest in words], under=True)


def bare():
    """The board alone, as shapes."""
    return board() + top_side() + underside()


def mounted():
    """The board lying on the carrier, as shapes."""
    return s.place(bare(), z=p.PAD + p.PRINT)


def kicad_model():
    """The model of the KiCad footprint: the board lying flat."""
    return s.items(mounted())


def parts(_colors):
    """The model of the bare board.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items(bare())}
