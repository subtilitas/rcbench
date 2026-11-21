"""LCDWIKI ES3C28P, the display board of the display node, part by part.

An ESP32-S3 board of 50.0 x 86.0mm under a 2.8" display of 240 x 320
pixels (ILI9341V) with a capacitive touch panel (FT6336G). The model
draws the board with its 4 mounting holes, the window and the slot of
the touch panel's flat cable and the holes of the USB socket's legs; the
display: touch panel with its black border, optical glue, display module,
2 strips of tape, the folds of the display's and the touch panel's flat
cables round the display's end and the touch cable's run to its socket;
on the back: the ESP32-S3, the flash, the audio codec, the speaker
amplifier, the RGB LED, the crystal, the microphone, 3 SOT-23 and 3
SOT-23-5 packages, a diode, 103 chip parts, the USB Type-C socket, the
microSD socket, the 6-way flat cable socket, the 5 wire sockets of
1.25mm pitch (UART, I2C, GPIO, BAT side-entry, SPEAKER top-entry), the
BOOT and RESET buttons, the touch controller with 6 chip parts on its
cable; the print of both sides.

Sources:
    M: LCDWIKI's STEP file ES3C28P_3D.step in ES3C28P_3D.zip
    (lcdwiki.com/res/ES3C28P/), 17.7MB, 1238 solids, written by
    OpenCASCADE 6.8 on 2025-06-10. It states no licence, so it is not in
    the repository. Read with hardware/mechanical/_measure_step.py's
    ``measure`` and OpenCASCADE's assembly tree, which names each part.
    M gives the stack, the board's outline, holes and cut-outs, and the
    place, size and height of every part drawn here but the print.
    D: LCDWIKI's outline drawing ES3C28P_Size.pdf, V1.0 of 2025-06-11:
    board 50.00 x 86.00 x 1.60, holes 3.20 on 42.00 x 78.00 with rings
    of 5.60, corner radius 3.50, display and touch panel 50.00 x 69.20
    (+-0.2), view area 43.60 x 58.05, 10.60 over all, 5.90 from the
    touch panel's front to the board's back, parts 4.70 at most, touch
    panel 1.00, display 2.30, two layers of glue of 0.50.
    P: 17 tester photos of 2026-11-19 to 2026-11-21, not in the
    repository: top side and back on 5mm squared paper, the edges on
    squared paper, angled views, side views against a folding rule, and
    a sharp photo of the back. The back photos are laid onto the board by
    its 4 holes. On the paper the paper grid fits within 0.24mm RMS (root
    mean square) over 1102 crossings; the holes lie 42.35 x 78.73 apart
    on the paper, which puts the camera about 680mm over it, so a part
    4.7 high leans by less than 0.3. M's parts lie on P's within 0.3.

Where M and P differ the model follows P:

- M draws the touch panel's flat cable from the window to its socket as
  a sheet of no thickness and draws neither fold. P shows both cables
  folded round the display's end at the USB socket: the display's from
  x -38.4 to -16.0, the touch panel's from -15.45 to -10.25 (M's x),
  read off the front photo within 0.3.

The folds are half circles of 12 flat facets round the display's end.
Not measured, estimated: the cables' thickness (0.12), their run behind
the display (a sheet between display and board, a riser through the
window and the slot), the length of the display cable's tail (5.1), the
diameter of the pegs of the USB and microSD sockets, the walls and
pins of the wire sockets, the cavities of the USB and microSD sockets,
the touch controller's name (a chip of 5.0 x 5.0 with 40 pads on the
touch cable, taken to be the FT6336G). P gives the top-entry socket 4.5
+-0.3 high and the side-entry ones 3.6 +-0.3, M 4.7 and 3.4; the model
keeps M's. Not drawn: the display cable's joint to the board, the
cables' traces, and 3 chip parts of 0402 beside the RESET button that P
shows and M lacks.

The print: the names of the sockets, the buttons and the pins, 3 lines
of the board's name, the CE and RoHS frames, the rings round the holes
and the microphone hole and MIC on the front, read off P within 0.3 and
set in _font.py's letters to the height and width each line has in P.
The font has no inch mark and no brackets: "2.8" LCD Display" is drawn
without the mark, "IO15(SCL)" as "IO15 SCL". Not drawn: the antenna's
copper, tracks, vias, pads, the frames round chip parts, the arrows,
the microphone symbol, the CE and RoHS marks' own shapes and the notches
the panel's tabs leave in the edges.

Parts: the packages' nominal sizes in M's boxes. ESP32-S3 QFN (quad
flat no-lead) 7.0 x 7.0 with 56 pads, flash SOP-8 (small outline
package) 208mil, codec QFN 3.0 x 3.0 with 20 pads, amplifier SOP-8
150mil, RGB LED 5.0 x 5.0, crystal 3.2 x 2.5, microphone 2.7 x 1.6.

Frames: the model as panel_7b.py's: origin at the center of the view
area on the touch panel's front face, z = 0 on that face and +z towards
the viewer, x to the right and y up seen from the front with the USB
socket down; the microphone hole is at the top right. The parts are
listed in M's frame, x from -52.75 to -2.75 and y from 6.0 to 92.0 over
the board, z = 0 on the board's front face, the display at +z, the parts
on the back from z = -1.6 down. All sizes are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import math

import _parts as p
import _solid as s

STEM = "es3c28p"
MASK = (0.06, 0.07, 0.09)
SCREEN = (0.10, 0.11, 0.13)
GLUE = (0.30, 0.32, 0.34)
TAPE = (0.20, 0.20, 0.20)
STIFF = (0.52, 0.28, 0.00)
LCD_CABLE = (0.30, 0.20, 0.10)

# Board (D, M): outline, corner radius, thickness, holes.
BOARD = (-52.75, 6.0, -2.75, 92.0)
RADIUS, BACK = 3.5, -1.6
HOLES = ((-6.75, 10.0), (-48.75, 10.0), (-6.75, 88.0), (-48.75, 88.0))
HOLE_D, RING_D = 3.2, 5.6
MIC_HOLE = (-12.75, 88.0, 0.6)
CARD_PEGS = ((-46.22, 52.1), (-46.22, 60.08))
USB_PEGS = ((-30.64, 11.88), (-24.86, 11.88))
# The USB socket's leg slots, 0.4 wide: x and (y0, y1).
USB_SLOTS_X = (-32.07, -23.43)
USB_SLOTS_Y = ((7.55, 8.85), (11.53, 13.23))
WINDOW = (-19.5, 21.91, -7.49, 35.91)
SLOT = (-15.74, 48.79, -11.24, 50.79)

# Display stack (D, M): display, touch panel, view area, glue, tape.
LCD = (-52.75, 14.4, -2.75, 83.6)
LCD_Z = (0.5, 2.8)
GLUE_Z = (2.8, 3.3)
TOUCH_Z = (3.3, 4.3)
VIEW = (-49.55, 22.81, -5.95, 80.86)
TAPES = ((-9.05, 20.19, -4.05, 80.21), (-51.45, 20.19, -46.45, 80.21))
# Flat cables (P): thickness, the folds' x, the display cable's tail.
FLEX = 0.12
FOLD_FACETS = 12
LCD_FOLD = (-38.4, -16.0)
LCD_TAIL = 19.5
TOUCH_FOLD = (-15.45, -10.25)
# The touch cable (M): its stiffener in the window, its strip to the
# slot, the end of its run in the socket.
STIFFENER = (-19.07, 22.01, -7.92, 35.81)
STIFF_Z = (-1.6, -1.1)
TOUCH_STRIP = (-15.25, -11.74)
TOUCH_END = 62.2
TOUCH_CHIP = (-14.25, 27.91)
FLEX_CAPS = (-16.27, -14.21, -13.18, -11.52)
FLEX_RES = (-15.27, -10.40)

# Chip parts on the back (M): (x, y), along x, then along y.
R0603 = (
    ((-46.73, 15.44), (-18.32, 38.23), (-19.05, 41.6), (-19.05, 43.1),
     (-19.05, 44.6), (-44.66, 44.97), (-13.06, 46.01), (-44.66, 46.47),
     (-21.97, 46.8), (-38.14, 47.47), (-31.79, 49.82), (-31.79, 51.27),
     (-31.79, 52.72), (-31.79, 55.98), (-31.79, 57.43), (-31.79, 58.88),
     (-10.29, 68.04), (-36.45, 69.06), (-10.29, 69.59), (-15.26, 82.9)),
    ((-34.46, 10.5), (-21.05, 10.5), (-35.64, 18.18), (-20.86, 18.18),
     (-19.31, 18.18), (-50.72, 18.19), (-41.54, 22.33), (-43.05, 22.38),
     (-34.13, 24.16), (-32.63, 24.16), (-29.63, 24.16), (-23.27, 24.16),
     (-28.13, 24.2), (-41.89, 35.76), (-38.28, 38.78), (-35.28, 38.78),
     (-25.78, 40.84), (-25.79, 43.87), (-32.51, 77.64)),
)
C0603 = (
    ((-49.44, 23.48), (-28.75, 36.61), (-28.75, 38.12), (-15.28, 38.22),
     (-28.73, 39.62), (-19.03, 40.1), (-28.75, 41.12), (-28.75, 42.62),
     (-38.17, 42.66), (-28.73, 44.12), (-38.15, 44.16), (-28.75, 45.62),
     (-38.15, 45.66), (-35.13, 47.47), (-32.13, 47.47), (-31.76, 54.35),
     (-21.41, 64.23), (-22.23, 65.74), (-41.22, 67.97), (-21.72, 70.45),
     (-36.43, 71.77), (-34.79, 73.22), (-41.2, 73.97), (-21.72, 74.15),
     (-36.28, 74.72)),
    ((-44.04, 17.48), (-24.56, 18.14), (-34.08, 18.16), (-15.54, 18.16),
     (-8.24, 18.16), (-44.55, 22.35), (-35.63, 24.19), (-31.13, 24.19),
     (-38.28, 35.79), (-36.78, 35.79), (-35.28, 35.79), (-33.78, 35.79),
     (-36.78, 38.81), (-33.78, 38.81), (-32.28, 38.81), (-24.28, 40.86),
     (-22.78, 40.86), (-21.28, 40.86), (-24.28, 43.84), (-22.78, 43.84),
     (-21.28, 43.84), (-30.96, 76.88), (-26.75, 79.8), (-25.25, 79.8),
     (-23.75, 79.8), (-9.25, 81.81), (-19.0, 82.13), (-17.5, 82.15),
     (-13.06, 82.15)),
)
C0805 = (
    ((-49.43, 21.59), (-11.89, 38.23)),
    ((-42.16, 17.49), (-26.45, 18.15), (-17.43, 18.15), (-10.12, 18.15),
     (-32.2, 18.16), (-11.14, 81.8)),
)
# Two capacitors of 0603 at 45 degrees, from low x and y to high.
C0603_SLANT = ((-34.69, 67.0), (-28.84, 78.15))

# Larger parts on the back (M): centers.
ESP32 = (-28.735, 70.615)
FLASH = (-28.2, 62.55)
CODEC = (-33.45, 43.52)
AMPLIFIER = (-13.08, 42.36)
LED = (-23.76, 51.59)
CRYSTAL = (-41.21, 70.97)
MICROPHONE = (-12.74, 87.5)
# SOT-23 with 2 leads towards -y, and the one with 2 leads towards +y.
SOT_2_DOWN = ((-38.41, 18.32), (-44.72, 35.95))
SOT_2_UP = (-25.70, 24.22)
# SOT-23-5: (x, y, turn); turn 0 has the leads along x.
SOT5 = ((-47.10, 18.35, 0.0), (-29.40, 18.15, 90.0), (-13.07, 18.15, 90.0))
DIODE = (-22.71, 18.15)
USB_REAR = (-27.75, 12.98)
CARD = (-42.77, 57.03)
CARD_SIZE = (14.1, 14.9, 1.85)
# Buttons: center, plunger colour.
BUTTONS = (((-16.17, 9.26), p.BLACK), ((-39.30, 9.26), p.WHITE))
# Side-entry wire sockets: pins, center of the box with its leads, turn
# as seen from the back (90: plug from -x, -90: plug from +x). UART,
# BAT, I2C, GPIO; BAT's opening faces the board's middle, its leads the
# edge.
SOCKETS = ((4, (-49.645, 40.985), 90.0), (2, (-48.085, 29.195), -90.0),
           (4, (-5.855, 55.915), -90.0), (4, (-5.855, 73.765), -90.0))
SOCKET_BOX = (5.19, 3.4, 0.99, 1.0, 1.7, 2.0, 2.8)
# The top-entry speaker socket: housing, its height, the leads' end.
SPEAKER = (-7.14, 38.54, -3.54, 46.19)
SPEAKER_H, SPEAKER_LEADS = 4.7, -7.64
# The flat cable socket: housing, actuator, ears, contacts' end.
FPC_BODY = (-17.1, 62.31, -9.9, 65.22)
FPC_LOCK = (-16.8, 59.97, -10.2, 62.31)
FPC_EARS = (60.67, 0.3)
FPC_H, FPC_SLIT, FPC_TAIL = 2.0, 0.37, 65.97

# Print on the back (P), in M's frame: (text, x, y, height, width, turn
# as seen from the back). Fields: white boxes (x0, y0, x1, y1) with
# their text in the board's colour.
BACK_PRINT = (
    ("IO2", -10.2, 75.5, 0.9, 2.1, 0.0),
    ("IO3", -10.2, 74.3, 0.9, 2.1, 0.0),
    ("IO14", -10.52, 73.1, 0.9, 2.75, 0.0),
    ("IO21", -10.52, 71.85, 0.9, 2.75, 0.0),
    ("3.3V", -18.27, 59.0, 0.85, 2.25, 0.0),
    ("GND", -18.2, 57.75, 0.85, 2.1, 0.0),
    ("IO15 SCL", -20.4, 56.4, 0.85, 6.5, 0.0),
    ("IO16 SDA", -20.45, 55.0, 0.85, 6.6, 0.0),
    ("RXD", -44.82, 42.9, 0.9, 2.35, 0.0),
    ("TXD", -44.82, 41.65, 0.9, 2.35, 0.0),
    ("GND", -44.82, 40.35, 0.9, 2.35, 0.0),
    ("5V", -45.2, 39.1, 0.9, 1.6, 0.0),
    ("2.8 LCD Display", -45.63, 76.37, 1.6, 18.25, 90.0),
    ("ESP32-S3 240x320", -48.5, 75.5, 1.2, 16.5, 90.0),
    ("Capacitive Touch", -50.75, 74.62, 1.2, 14.75, 90.0),
    ("CE", -23.25, 29.75, 4.0, 5.5, 0.0),
    ("RoHS", -32.55, 30.5, 2.4, 6.8, 0.0),
    ("COMPLIANT", -32.55, 27.4, 0.8, 8.2, 0.0),
)
FIELDS = (
    ("SPEAKER", (-5.75, 30.0, -3.9, 37.25), 0.9, 6.6, -90.0),
    ("I2C", (-9.85, 48.75, -8.4, 51.25), 0.9, 1.9, -90.0),
    ("UART", (-43.0, 38.75, -41.25, 43.0), 1.0, 3.5, 90.0),
    ("BAT", (-44.65, 27.25, -42.75, 30.5), 1.0, 2.4, 90.0),
    ("RESET", (-18.75, 11.5, -13.5, 13.0), 1.0, 4.6, 0.0),
    ("BOOT", (-42.0, 11.5, -36.75, 13.0), 1.0, 3.8, 0.0),
)
FRAMES = ((-26.5, 26.25, -20.0, 33.25), (-37.25, 26.25, -27.85, 33.25))
FRONT_PRINT = (("MIC", -12.8, 90.5, 1.5, 3.9),)
MIC_RING = 2.5


def under(shapes, x, y, turn=0.0, z=BACK):
    """Shapes of _parts.py put on the back at (x, y) of M.

    Args:
        shapes: The shapes, z = 0 on the face they stand on, +z away.
        x: The place's x in M.
        y: The place's y in M.
        turn: Degrees counter-clockwise as seen from the back; turn 0
            puts the shapes' x along the board's x, mirrored.
        z: The face's z in M.
    """
    return s.place(shapes, x, y, z, turn, flip=True)


def ring(cx, cy, diameter, line, z, rgb=p.WHITE):
    """A printed ring, ``line`` wide, its middle on ``diameter``."""
    r = diameter / 2
    return s.ext(s.circle(cx, cy, r + line / 2), z, z + p.PRINT, rgb,
                 holes=[s.circle(cx, cy, r - line / 2)])


def board():
    """The board with its holes, window and slot."""
    x0, y0, x1, y1 = BOARD
    holes = [s.circle(x, y, HOLE_D / 2) for x, y in HOLES]
    holes.append(s.circle(MIC_HOLE[0], MIC_HOLE[1], MIC_HOLE[2] / 2))
    holes += [s.circle(x, y, 0.5) for x, y in CARD_PEGS]
    holes += [s.circle(x, y, 0.35) for x, y in USB_PEGS]
    holes += [s.stadium(x - 0.2, a, x + 0.2, b) for x in USB_SLOTS_X
              for a, b in USB_SLOTS_Y]
    holes += [s.rect(*WINDOW), s.rect(*SLOT)]
    return [s.ext(s.rounded(x0, y0, x1, y1, RADIUS), BACK, 0.0, MASK,
                  holes=holes)]


def fold(x0, x1, tail, rgb):
    """A flat cable folded round the display's end and on its back.

    The cable leaves the display's front edge, turns round its end at
    the USB socket in a half circle of FOLD_FACETS flat facets and runs
    on the display's back to y = ``tail``.

    Args:
        x0: The cable's edge at -x.
        x1: The cable's edge at +x.
        tail: The y its run on the display's back ends at.
        rgb: The cable's colour.
    """
    yc, zc = LCD[1], (LCD_Z[0] + LCD_Z[1]) / 2
    r = (LCD_Z[1] - LCD_Z[0]) / 2
    steps = [math.pi / 2 + math.pi * k / FOLD_FACETS
             for k in range(FOLD_FACETS + 1)]
    outer = [(yc + (r + FLEX) * math.cos(a), zc + (r + FLEX) * math.sin(a))
             for a in steps]
    inner = [(yc + r * math.cos(a), zc + r * math.sin(a))
             for a in reversed(steps)]
    profile = outer + [(tail, zc - r - FLEX), (tail, zc - r)] + inner
    return [s.ext(profile, x0, x1, rgb, frame=s.X)]


def display():
    """Touch panel, glue, display module, tape, the cables' folds."""
    lo, hi = LCD_Z[0] - FLEX, LCD_Z[0]
    out = [s.ext(s.rect(*LCD), *TOUCH_Z, p.BLACK, holes=[s.rect(*VIEW)]),
           s.box(*VIEW[:2], *VIEW[2:], *TOUCH_Z, SCREEN),
           s.box(*LCD[:2], *LCD[2:], *GLUE_Z, GLUE),
           s.box(*LCD[:2], *LCD[2:], *LCD_Z, p.WHITE)]
    out += [s.box(*t[:2], *t[2:], 0.0, LCD_Z[0], TAPE) for t in TAPES]
    out += fold(*LCD_FOLD, LCD_TAIL, LCD_CABLE)
    # The touch cable: fold and tail, a riser into the window, the
    # stiffener, a riser out of it, the strip to the slot, a riser
    # through the slot and the strip on the back to the socket.
    tx0, tx1 = TOUCH_FOLD
    sx0, sx1 = TOUCH_STRIP
    wy0, wy1 = STIFFENER[1], STIFFENER[3]
    out += fold(tx0, tx1, wy0 + FLEX, p.KAPTON)
    out += [s.box(tx0, wy0, tx1, wy0 + FLEX, STIFF_Z[1], lo, p.KAPTON),
            s.box(*STIFFENER[:2], *STIFFENER[2:], *STIFF_Z, STIFF),
            s.box(sx0, wy1 - FLEX, sx1, wy1, STIFF_Z[1], lo, p.KAPTON),
            s.box(sx0, wy1 - FLEX, sx1, SLOT[1] + FLEX, lo, hi, p.KAPTON),
            s.box(sx0, SLOT[1], sx1, SLOT[1] + FLEX, BACK, lo, p.KAPTON),
            s.box(sx0, SLOT[1], sx1, TOUCH_END, BACK - FLEX, BACK,
                  p.KAPTON)]
    out += under(p.qfn(5.0, 5.0, 0.55, 10, 10, 0.4), *TOUCH_CHIP)
    for x in FLEX_CAPS:
        out += under(p.c0402(), x, 33.16, 90.0)
    for x in FLEX_RES:
        out += under(p.r0402(), x, 33.13, 90.0)
    return out


def sot23_3():
    """SOT-23 with 2 leads on the -x side, 1.2 high."""
    return p.leaded(1.6, 2.9, 1.2, p.rows(2, 1.9), [0.0], 2.8, 0.4)


def sot23_5():
    """SOT-23-5 with 3 leads on the -x side, 0.9 high."""
    return p.leaded(1.6, 2.9, 0.9, p.rows(3, 0.95), p.rows(2, 1.9), 2.8,
                    0.4)


def rgb_led():
    """A 5050 RGB LED: white body, round window, 2 leads a side."""
    out = [s.box(-2.5, -2.5, 2.5, 2.5, 0.0, 1.57, p.WHITE),
           s.ext(s.circle(0.0, 0.0, 1.8), 1.57, 1.6, p.LENS)]
    for side in (-1, 1):
        a, b = sorted((side * 2.5, side * 2.7))
        for y in (-1.6, 1.6):
            out.append(s.box(a, y - 0.5, b, y + 0.5, 0.0, 0.5, p.TIN))
    return out


def button(plunger):
    """A push button pressed from the back: body 4.2 x 3.2, plunger."""
    return [s.box(-2.1, -1.6, 2.1, 1.6, 0.0, 1.7, p.STEEL),
            s.box(-1.495, -1.06, 1.495, 1.06, 1.7, 2.5, plunger)]


def usb():
    """The USB socket, the legs of its shell and its 2 pegs."""
    x, y = USB_REAR
    out = under(p.usb_c(0.23), x, y)
    for lx in USB_SLOTS_X:
        for a, b in USB_SLOTS_Y:
            out.append(s.box(lx - 0.15, a + 0.1, lx + 0.15, b - 0.1,
                             BACK - 0.09, 0.0, p.STEEL))
    for px, py in USB_PEGS:
        out.append(s.ext(s.circle(px, py, 0.3), BACK - 0.09, 0.0, p.BLACK))
    return out


def speaker_socket():
    """The top-entry socket of the speaker and its 2 leads."""
    x0, y0, x1, y1 = SPEAKER
    out = under(p.top_socket(2, 1.25, y1 - y0, x1 - x0, SPEAKER_H),
                (x0 + x1) / 2, (y0 + y1) / 2, 90.0)
    for y in p.rows(2, 1.25):
        cy = (y0 + y1) / 2 + y
        out.append(s.box(SPEAKER_LEADS, cy - 0.16, x0, cy + 0.16,
                         BACK - 0.25, BACK, p.TIN))
    return out


def fpc_socket():
    """The touch cable's socket: housing, actuator over the cable, ears."""
    lo = BACK - FPC_H
    out = [s.box(*FPC_BODY[:2], *FPC_BODY[2:], lo, BACK, p.WHITE),
           s.box(*FPC_LOCK[:2], *FPC_LOCK[2:], lo, BACK - FPC_SLIT,
                 p.GREY)]
    y, w = FPC_EARS
    for x in (FPC_BODY[0], FPC_LOCK[2]):
        out.append(s.box(x, y, x + w, FPC_LOCK[3], lo, BACK, p.WHITE))
    cx = (FPC_BODY[0] + FPC_BODY[2]) / 2
    for dx in p.rows(6, 0.5):
        out.append(s.box(cx + dx - 0.1, FPC_BODY[3], cx + dx + 0.1,
                         FPC_TAIL, BACK - 0.2, BACK, p.TIN))
    return out


def back_side():
    """Every part on the back."""
    out = under(p.qfn(7.0, 7.0, 0.9, 14, 14, 0.4), *ESP32)
    out += under(p.leaded(5.48, 5.28, 1.9, p.rows(4, 1.27),
                          p.rows(4, 1.27), 7.88, 0.5), *FLASH)
    out += under(p.qfn(3.0, 3.0, 0.8, 5, 5, 0.4), *CODEC)
    out += under(p.soic8(), *AMPLIFIER)
    out += under(rgb_led(), *LED)
    out += under(p.can(3.2, 2.5, 0.61, p.STEEL), *CRYSTAL, 90.0)
    out += under(p.can(2.7, 1.6, 0.92, p.STEEL), *MICROPHONE, 90.0)
    for x, y in SOT_2_DOWN:
        out += under(sot23_3(), x, y, 90.0)
    out += under(sot23_3(), *SOT_2_UP, -90.0)
    for x, y, turn in SOT5:
        out += under(sot23_5(), x, y, turn)
    out += under(p.sod(2.7, 1.6, 1.2, 3.3, 0.6), *DIODE, 90.0)
    for table, part in ((R0603, p.r0603), (C0603, p.c0603),
                        (C0805, p.c0805)):
        for turn, places in zip((0.0, 90.0), table, strict=True):
            for x, y in places:
                out += under(part(), x, y, turn)
    for x, y in C0603_SLANT:
        out += under(p.c0603(), x, y, -45.0)
    out += usb()
    out += under(p.card_tray(*CARD_SIZE), *CARD, 90.0)
    for x, y in CARD_PEGS:
        out.append(s.ext(s.circle(x, y, 0.45), BACK, 0.0, p.STEEL))
    for (x, y), plunger in BUTTONS:
        out += under(button(plunger), x, y)
    depth, height, leads, rear, wing, wing_h, wing_d = SOCKET_BOX
    for pins, (x, y), turn in SOCKETS:
        width = 1.25 * (pins - 1) + 3.0 + 2 * wing
        out += under(p.socket(pins, 1.25, width, depth, height, leads,
                              rear, wing, wing_h, wing_d), x, y, turn)
    return out + speaker_socket() + fpc_socket()


def prints():
    """The print of the back and of the front."""
    out = []
    for text, x, y, height, width, turn in BACK_PRINT:
        out += under(p.text(text, height, width=width), x, y, turn)
    for text, (x0, y0, x1, y1), height, width, turn in FIELDS:
        out.append(s.box(x0, y0, x1, y1, BACK - p.PRINT, BACK, p.WHITE))
        out += under(p.text(text, height, MASK, width), (x0 + x1) / 2,
                     (y0 + y1) / 2, turn, BACK - p.PRINT)
    for frame in FRAMES:
        out += s.place(p.frame(*frame), z=BACK - p.PRINT)
    for x, y in HOLES:
        out.append(ring(x, y, RING_D, 0.3, 0.0))
    out.append(ring(MIC_HOLE[0], MIC_HOLE[1], MIC_RING, 0.25, 0.0))
    for text, x, y, height, width in FRONT_PRINT:
        out += s.place(p.text(text, height, width=width), x, y, 0.0)
    return out


def bare():
    """The whole board in M's frame."""
    return board() + display() + back_side() + prints()


def parts(_colors):
    """The model of the board, in the frame of panel_7b.py.

    Returns:
        A dict of file stem to solids.
    """
    cx, cy = (VIEW[0] + VIEW[2]) / 2, (VIEW[1] + VIEW[3]) / 2
    return {STEM: s.items(s.place(bare(), -cx, -cy, -TOUCH_Z[1]))}
