"""Waveshare ESP32-S3-Touch-LCD-1.85, a round display board.

An ESP32-S3 board behind a round 1.85" display of 360 x 360 pixels with
a capacitive touch panel: cover glass 55.00 across, view area 46.08
across, 9.70 over all. A facsimile for designing a case: the glass with
its black print, the display, its flat cable folded under it, the foam
ring, the board with its outline and holes, the 3 threaded inserts and
every part on both sides of the board. It is not Waveshare's file.

Sources, all mm:

- D: Waveshare's outline drawing ESP32-S3-Touch-LCD-1.85.pdf in
  ESP32-S3-Touch-LCD-1.85.rar ("Mechanical Files" on the board's page
  at docs.waveshare.com): glass 55.00 +-0.1, view area 46.08, glass
  0.70, 5.70 from the glass's front to the board, 9.70 over all, 3 holes
  for M2.
- E: Waveshare's STEP file ESP32-S3-Touch-LCD-1_85-20260703.stp in the
  same archive, 522 solids, read with OpenCASCADE by
  _measure_esp32_s3_lcd_185.py into _esp32_s3_lcd_185_data.py. Taken
  from it: the board's outline, holes and parts, the foam ring, and the
  stack below, read off the flat faces of the solid that holds glass,
  display and flat cable.
- P: the seller's photos of the board's back (Amazon B0F3356..., owner's
  screenshots of 2026-11-24): the parts' layout agrees with E.

The stack, E, z from the glass's front: glass 0.70 thick, its front
print a ring 0.03 thick round the view area; display 48.08 across from
0.70 to 2.65 with a tab 12.0 wide at the USB end; the flat cable 0.10
thick leaves the tab, folds back under the display and runs 24.1 to the
display socket on the board's front, its end 0.25 thick over the last
2.96 before the socket; a stiffener with parts 1.0 thick
on it; the foam ring from 2.65 to 4.10; the board from 4.10 to 5.70.

The board's outline: a circle of 23.955 radius over the upper half with
a flat at the top, two arcs of about 28.8 radius meeting under the USB
socket and a flat of 10.0 at the socket. E draws the lower arcs as short
lines; the model keeps E's lines, simplified to 0.01.

Drawn as shapes of _parts.py at E's place: the USB Type-C socket (its
shell on the board; E sinks it 0.91 into the board) and the microSD
slot, turned 27 degrees with its mouth to the board's edge. The 3
inserts: 4.0 across and 4.0 high on the back, a neck of 2.8 in the
board's hole of 2.9, a bore of 1.6 (M2); E draws them as solids 4.0 across
from 1.5 into the board, and the neck is estimated. Every chip part is
drawn with its end caps, and so is every other part that E draws as a
body with a cap at each end, at its measured size; every leaded
package as a body and its feet, as panel_7b.py draws them. Every other
solid, the wire sockets, the side buttons, the microphone and the parts
on the board's front among them,
is the outline of its largest flat face, at E's angle, pushed from
where it starts to where it ends; a part that reaches into the board in
E is cut at the board's face. Not modelled: the print, tracks,
pads, the holes in the parts' bodies.

No dimension is verified with a caliper on a board.

Model frame: origin at the center of the view area on the glass's
front face; z = 0 on that face, +z towards the viewer; x to the right
and y up seen from the front, the USB socket at the bottom. The data is
in E's frame turned over, as the board's back shows it: z = 0 on the
board's component side, +z away from the display.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import math

import _parts as p
from _esp32_s3_lcd_185_data import (BOARD, CAPPED, CHIPS, CHIPS_FRONT,
                                    LEADED, NAMED, PRISMS, SPACER)
from _solid import box, circle, ext, items, place
from panel_7b import _package

STEM = "esp32_s3_lcd_185"
BLUE = (0.05, 0.20, 0.47)
GLASS = (0.16, 0.17, 0.19)
FOAM = (0.20, 0.20, 0.20)
SHADES = {"epoxy": p.EPOXY, "tin": p.TIN}

# Stack (D, E), z in the data's frame: the board's back at 0, the front
# at -1.6, the glass's front at -5.70.
FRONT = -5.70
GLASS_D, VIEW_D, GLASS_Z, PRINT_T = 55.0, 46.08, (-5.70, -5.00), 0.03
LCD_R, LCD_Z = 24.04, (-5.00, -3.05)
TAB = (-7.08, 4.92, -25.11)                 # x0, x1, its end in y
FOLD = (-25.90, -25.11, -4.65, -2.75)       # y0, y1, z0, z1
CABLE = (-25.11, -5.02, -2.85, -2.75)       # y0, y1, z0, z1
CABLE_END = (-5.02, -2.06, -3.00, -2.75)
STIFFENER = (-6.04, -24.45, 4.00, -15.02, -2.75, -1.75)
# Inserts (E): neck in the board's hole, body over the back, bore.
NECK_D, INSERT_D, BORE_D = 2.8, 4.0, 1.6
# The microSD slot (E): center, size of its top face, turn of its
# mouth, height; measured as the least rectangle round that face.
CARD = (14.48, 7.49, 11.05, 11.25, 117.0, 1.32)


def _poly(pts):
    return [tuple(q) for q in pts]


def stack():
    """Glass, display, flat cable, foam ring and board, data frame."""
    g0, g1 = GLASS_Z
    rg, rv = GLASS_D / 2, VIEW_D / 2
    x0, x1, tab_end = TAB
    # The display's outline: its circle with the tab at the USB end.
    start, end = (math.degrees(math.atan2(-math.sqrt(LCD_R ** 2 - x1 ** 2),
                                          x1)),
                  math.degrees(math.atan2(-math.sqrt(LCD_R ** 2 - x0 ** 2),
                                          x0)))
    arc = []
    steps = 4
    for k in range(steps + 1):
        a = math.radians(start + (end + 360.0 - start) * k / steps)
        arc.append((LCD_R * math.cos(a), LCD_R * math.sin(a)))
    lcd = [arc[0] + ((0.0, 0.0, 1),)]
    lcd += [q + ((0.0, 0.0, 1),) for q in arc[1:-1]]
    lcd += [arc[-1], (x0, tab_end), (x1, tab_end)]
    out = [
        ext(circle(0.0, 0.0, rg), g0, g0 + PRINT_T, p.BLACK,
            holes=[circle(0.0, 0.0, rv)]),
        ext(circle(0.0, 0.0, rg), g0 + PRINT_T, g1, GLASS),
        ext(lcd, *LCD_Z, p.BLACK),
    ]
    y0, y1, z0, z1 = FOLD
    out.append(box(x0, y0, x1, y1, z0, z1, p.KAPTON))
    y0, y1, z0, z1 = CABLE
    out.append(box(x0, y0, x1, y1, z0, z1, p.KAPTON))
    y0, y1, z0, z1 = CABLE_END
    out.append(box(x0, y0, x1, y1, z0, z1, p.KAPTON))
    out.append(box(*STIFFENER[:4], *STIFFENER[4:], p.EPOXY))
    outer, holes, (z0, z1) = SPACER
    out.append(ext(_poly(outer), z0, z1, FOAM,
                   holes=[_poly(h) for h in holes]))
    outer, holes, (z0, z1) = BOARD
    out.append(ext(_poly(outer), z0, z1, BLUE,
                   holes=[_poly(h) for h in holes]))
    return out


def inserts():
    """The 3 threaded inserts, data frame."""
    out = []
    for x0, y0, _z0, x1, y1, z1 in NAMED["insert"]:
        x, y = (x0 + x1) / 2, (y0 + y1) / 2
        bore = circle(x, y, BORE_D / 2)
        out += [ext(circle(x, y, NECK_D / 2), BOARD[2][0], 0.0, p.BRONZE,
                    holes=[bore]),
                ext(circle(x, y, INSERT_D / 2), 0.0, z1, p.BRONZE,
                    holes=[bore])]
    return out


def measured():
    """Chip parts, leaded packages and outlined solids, data frame."""
    out = []
    front = BOARD[2][0]
    for kind, x, y, turn in CHIPS:
        out += place(getattr(p, kind)(), x, y, turn=turn)
    for kind, x, y, turn in CHIPS_FRONT:
        out += place(getattr(p, kind)(), x, y, front, -turn, flip=True)
    for length, width, height, shade, x, y, turn, side in CAPPED:
        shapes = p.chip(length, width, height, SHADES.get(shade, shade))
        if side == "back":
            out += place(shapes, x, y, turn=turn)
        else:
            out += place(shapes, x, y, front, -turn, flip=True)
    for body, feet in LEADED:
        out += _package(body, feet)
    for outline, (z0, z1), shade in PRISMS:
        # A part reaches into the board in E; it is cut at the board's
        # face on its side.
        z0, z1 = (max(z0, 0.0), z1) if z1 > 0.0 else (z0, min(z1, front))
        out.append(ext(_poly(outline), z0, z1, SHADES.get(shade, shade)))
    return out


def named():
    """The USB Type-C socket and the microSD slot, data frame."""
    shell = max(NAMED["usb_c"], key=lambda b: (b[3] - b[0]) * (b[4] - b[1]))
    x0, y0, _z0, x1, _y1, _z1 = shell
    out = place(p.usb_c(), (x0 + x1) / 2, y0 + 7.35)
    x, y, width, depth, turn, height = CARD
    out += place(p.card_tray(width, depth, height), x, y, turn=turn)
    return out


def board():
    """The whole board, in the model frame.

    Returns:
        The solids, as step_gen.write takes them.
    """
    shapes = stack() + inserts() + measured() + named()
    return items(place(shapes, z=FRONT, flip=True))


def parts(_colors):
    """Every model file of the board.

    Args:
        _colors: Colour name to RGB; not used, the spec has its own.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: board()}
