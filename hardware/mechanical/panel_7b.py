"""Waveshare ESP32-P4-WIFI6-Touch-LCD-7B, the bench's panel board.

A facsimile for designing the enclosure: the cover glass, the display,
the board with its cut-outs, the 4 standoffs, the two flat cables and
every part on the board. It is not Waveshare's file. The dimensions
come from six sources, all mm:

- D: Waveshare's outline drawing of the board, a PDF of 2025-11-08 in
  ESP32-P4-WIFI6-Touch-LCD-7B-3D.zip on the board's wiki page
  (waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-7B): glass 164.28 x
  99.17, view area 154.58 x 86.42 set 3.06 and 4.02 from the glass's
  edges, display 164 x 97.1, board 164 x 97 and 1.6 thick, 4 M2.5
  standoffs 156 x 89 apart, 5.7 from the glass's front to the board, the
  4 wire sockets of the left edge 14.9 apart.
- E: Waveshare's STEP file of the board in the same archive, read with
  OpenCASCADE by _measure_step.py into _panel_7b_data.py. Taken from
  it: the stack, the board's outline, holes and slots, and the place
  and size of every part but those taken from P.
- P: Waveshare's photo of the board's rear, 1600 x 1200
  (esp32-p4-wifi6-touch-lcd-7b-3.jpg on the product page), fitted to the
  holes of the 4 standoffs at 7.35 pixels per mm, with 1.1 pixels left
  at each. A position read from it is good to 0.3. Taken from it: the
  7 top-entry wire sockets of 2.0 pitch, whose pins it shows end on,
  the shield can over the ESP32-P4 and the coin-cell holder.
- Q: Waveshare's angled photo of the rear
  (esp32-p4-wifi6-touch-lcd-7b-5.jpg), for heights against the 6.0
  standoffs, each +- 0.5: the wire sockets 5.5, the coin-cell holder
  3.8, the shield can 1.9.
- N: the nominal height of the ESP32-P4's package, 0.9; E draws it 2.0
  high, which the shield can does not clear.
- S: Waveshare's schematic of the board, a PDF of 2025-09-25, for the
  parts' names.

E and D draw other wire sockets than the board carries: 4 side-entry
shrouds 6.5 high at the left edge, where the board has top-entry
sockets, and sockets of 1.0 pitch 14.35 long where the board has two
12-pin sockets of 2.0 pitch 27.9 long.
E has neither the shield can nor the coin-cell holder. P shows the
board; where P and E differ the model follows P. D's 15.3 over all is
E's, with the 6.5 shrouds; the model is 15.1, to the top of the USB
Type-A socket.

Neither E nor the photos are in this repository: Waveshare states no
licence for them. E draws 1337 solids; this model draws each part again
from its measured box: chip parts with their end caps, leaded packages
as a body and one lead a foot, the connectors as the shapes of
_parts.py, every other solid as its box.

Estimated, in no source: the walls of the connectors, the plungers of
the two buttons (2.4 x 1.6), the flat cables' paths through the board's
slots, the display as a steel back 0.3 thick with a black panel in it.

Differences from the board: E has two spacers 20 x 96 between display
and board; the one under the touch cable is drawn cut back to clear it.
The print on the board, tracks, vias, solder and pads are not modelled.
No dimension is verified with a caliper on a board.

Model frame: the origin is the center of the view area on the glass's
front face; z = 0 is that face, +z towards the viewer; x to the right
and y up seen from the front with the USB sockets on the left. The
parts are listed in E's frame, as the rear shows them: x and y from the
board's corner with the USB sockets on the right, z = 0 on the board's
component side and +z away from the display.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _parts as p
from _panel_7b_data import BOXES, CHIPS, LEADED, NAMED
from _solid import X, box, circle, ext, items, place, rect, strip

BLUE = (0.05, 0.20, 0.47)
GLASS = (0.16, 0.17, 0.19)
FLEX = (0.88, 0.50, 0.10)
FOAM = (0.20, 0.20, 0.20)
SHADES = {"epoxy": p.EPOXY, "tin": p.TIN}

# Glass (D, E): outline, the smaller outline of its rear part, z of
# front, step and rear, and the view area.
LENS = (-0.14, -1.59, 164.14, 97.59)
LENS_REAR = (0.06, -1.38, 163.94, 97.38)
LENS_Z = (-8.80, -8.55, -6.90)
VIEW = (6.50, 7.15, 161.08, 93.56)
PRINT_T = 0.02          # the printed border, as E draws it
# Display (D, E): outline, z of front and rear.
LCD = (0.0, -0.05, 164.0, 97.05)
LCD_Z = (-6.70, -3.10)
# Board (D, E): size, thickness, corner radius, the cut-out at the
# antenna, the step at the two buttons, the two slots for the cables.
BOARD_X, BOARD_Y, BOARD_T, BOARD_R = 164.0, 97.0, 1.6, 1.0
ANTENNA_GAP = (126.50, 91.85, 140.75, 0.6)      # x0, y0, x1, corner radius
BUTTON_STEP = (72.40, 73.90, 88.77, 90.27, 1.5)     # 4 y of its edge, depth
SLOTS = ((33.75, 49.50, 41.75, 52.50), (73.40, 41.50, 95.40, 44.50))
# Standoffs (D, E): places, hole in the board, body, top, height, bore
# below the thread, thread, height the thread starts at.
STANDOFFS = ((4.0, 4.0), (160.0, 4.0), (4.0, 93.0), (160.0, 93.0))
HOLE_D, STANDOFF_D, TOP_D, STANDOFF_H = 4.3, 5.5, 5.1, 6.0
BORE_D, THREAD_D, THREAD_FROM, NECK_D, NECK_H = 3.1, 2.5, 1.75, 4.19, 1.5
# Spacers between display and board (E), the left one cut back.
SPACERS = ((1.5, 24.9, 21.5, 96.51), (1.5, 0.5, 8.6, 24.9),
           (142.5, 0.5, 162.5, 96.51))
# Display cable (E): its box on the display's rear, the x of the part
# that rises through the slot, and z of its run on the board.
DISPLAY_CABLE = (68.0, -0.05, 97.0, 40.28, -3.05, -2.90)
DISPLAY_RISER = (76.5, 92.2, 0.675)
# Touch cable (E): x of the part round the glass's edge, y and z it
# starts at, z of the part on the display's rear, its outline there,
# x of the part that rises through the slot, z of its run on the board.
TOUCH_TAIL = (8.86, 27.16, -1.485, -8.40)
TOUCH_Z = (-3.00, -2.85)
TOUCH_SHEET = [
    (39.71, 49.41), (36.11, 49.41), (36.11, 48.91), (28.86, 48.91),
    (28.86, 29.41, (23.86, 29.41, -1)), (23.86, 24.41),
    (12.86, 24.41, (12.86, 20.41, 1)), (8.86, 20.41), (8.86, 0.56),
    (27.16, 0.56), (27.16, 2.91, (32.16, 2.91, -1)), (32.16, 7.91),
    (42.96, 7.91, (42.96, 11.91, 1)), (46.96, 11.91), (46.96, 48.91),
    (39.71, 48.91),
]
TOUCH_RISER = (36.11, 39.71, 1.225)
# The touch controller on its cable (E): stiffener, chip, further parts.
TOUCH_PARTS = (
    ((28.86, 33.72, 46.96, 48.91), -2.85, -2.45, FLEX),
    ((33.86, 35.91, 39.86, 41.91), -2.45, -1.75, p.EPOXY),
    ((31.46, 43.91, 44.46, 46.91), -2.45, -1.85, p.EPOXY),
    ((41.46, 41.91, 44.46, 43.91), -2.45, -1.85, p.EPOXY),
)
CABLE_T = 0.15
# From P: the shield can over the ESP32-P4 and the coin-cell holder.
SHIELD = (47.7, 37.2, 69.0, 54.2, 1.9)
HOLDER = (17.9, 85.7, 15.4, 3.8)        # center, diameter, height
P4_H = 0.9              # height of the ESP32-P4's package (N); E draws 2.0
# From P: the wire sockets, 2.0 pitch, top entry: pins, center of the
# housing, turn (the solder leads point to +y at turn 0), and the shift
# of the row of pins along the housing. The 4 at the left edge are 14.9
# apart (D).
WIRE_SOCKETS = (
    (4, 4.0, 17.7, -90.0, 0.0),         # RS485
    (4, 4.0, 32.6, -90.0, 0.0),         # CAN
    (4, 4.0, 47.5, -90.0, 0.0),         # UART
    (4, 4.0, 62.4, -90.0, 0.0),         # I2C
    (12, 44.1, 4.1, 0.0, -0.5),         # 3V3 to IO36
    (12, 120.85, 4.1, 0.0, -0.5),       # BAT to IO52
    (2, 33.8, 93.35, 180.0, 0.0),       # speaker
)


def _center(found):
    x0, y0, _z0, x1, y1, _z1 = found
    return (x0 + x1) / 2, (y0 + y1) / 2


def _board_outline():
    """The board's outline with the antenna's gap and the buttons' step."""
    x, y, r = BOARD_X, BOARD_Y, BOARD_R
    g0, gy, g1, s = ANTENNA_GAP
    b0, b1, b2, b3, depth = BUTTON_STEP
    return [
        (r, 0.0), (x - r, 0.0, (x - r, r, 1)), (x, r),
        (x, y - r, (x - r, y - r, 1)), (x - r, y),
        (g1 + s, y, (g1 + s, y - s, 1)), (g1, y - s),
        (g1, gy + s, (g1 - s, gy + s, -1)), (g1 - s, gy),
        (g0 + s, gy, (g0 + s, gy + s, -1)), (g0, gy + s),
        (g0, y - s, (g0 - s, y - s, 1)), (g0 - s, y),
        (r, y, (r, y - r, 1)), (0.0, y - r),
        (0.0, b3), (depth, b2), (depth, b1), (0.0, b0),
        (0.0, r, (r, r, 1)),
    ]


def stack():
    """Glass, display, spacers and board, in E's frame."""
    front, step, rear = LENS_Z
    lcd_front, lcd_rear = LCD_Z
    x0, y0, x1, y1 = LCD
    rim = 0.3
    holes = [circle(x, y, HOLE_D / 2) for x, y in STANDOFFS]
    holes += [rect(*slot) for slot in SLOTS]
    out = [
        ext(rect(*LENS), front, front + PRINT_T, p.BLACK,
            holes=[rect(*VIEW)]),
        ext(rect(*LENS), front + PRINT_T, step, GLASS),
        ext(rect(*LENS_REAR), step, rear, GLASS),
        box(x0, y0, x1, y1, lcd_rear - rim, lcd_rear, p.STEEL),
        ext(rect(x0, y0, x1, y1), lcd_front, lcd_rear - rim, p.WHITE,
            holes=[rect(x0 + rim, y0 + rim, x1 - rim, y1 - rim)]),
        box(x0 + rim, y0 + rim, x1 - rim, y1 - rim, lcd_front,
            lcd_rear - rim, p.BLACK),
        ext(_board_outline(), -BOARD_T, 0.0, BLUE, holes=holes),
    ]
    for a, b, c, d in SPACERS:
        out.append(box(a, b, c, d, lcd_rear, -BOARD_T, FOAM))
    return out


def standoffs():
    """The 4 threaded standoffs pressed into the board, in E's frame."""
    out = []
    for x, y in STANDOFFS:
        bore, thread = circle(x, y, BORE_D / 2), circle(x, y, THREAD_D / 2)
        out += [
            ext(circle(x, y, NECK_D / 2), -NECK_H, 0.0, p.STEEL,
                holes=[bore]),
            ext(circle(x, y, STANDOFF_D / 2), 0.0, THREAD_FROM, p.STEEL,
                holes=[bore]),
            ext(circle(x, y, STANDOFF_D / 2), THREAD_FROM, STANDOFF_H - 0.2,
                p.STEEL, holes=[thread]),
            ext(circle(x, y, TOP_D / 2), STANDOFF_H - 0.2, STANDOFF_H,
                p.STEEL, holes=[thread]),
        ]
    return out


def cables():
    """The display's and the touch panel's flat cables, in E's frame."""
    t = CABLE_T
    x0, y0, x1, y1, z0, z1 = DISPLAY_CABLE
    mid = (z0 + z1) / 2
    r0, r1, top = DISPLAY_RISER
    socket = NAMED["display_socket"][0]
    out = [
        box(x0, y0, x1, y1, z0, z1, FLEX),
        ext(strip([(y1, mid), (42.2, mid), (43.8, top), (socket[1], top)],
                  t), r0, r1, FLEX, frame=X),
    ]
    t0, t1, edge, start = TOUCH_TAIL
    low, high = TOUCH_Z
    mid = (low + high) / 2
    r0, r1, top = TOUCH_RISER
    socket = NAMED["touch_socket"][0]
    out += [
        ext(strip([(edge, start), (edge, mid), (0.56, mid)], t), t0, t1,
            p.BLACK, frame=X),
        ext(TOUCH_SHEET, low, high, p.BLACK),
        ext(strip([(49.41, mid), (50.3, mid), (51.7, top), (socket[1], top)],
                  t), r0, r1, p.BLACK, frame=X),
    ]
    for (a, b, c, d), z0, z1, rgb in TOUCH_PARTS:
        out.append(box(a, b, c, d, z0, z1, rgb))
    return out


def _package(body, feet):
    """A leaded package from its measured body box and feet."""
    x0, y0, z0, x1, y1, top = body
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    out = [box(x0, y0, x1, y1, z0, top, p.EPOXY)]
    for a, b, c, d in feet:
        fx, fy = (a + c) / 2, (b + d) / 2
        # How far the foot reaches past each side of the body; it is
        # drawn from the side it passes furthest, cut off at the body.
        past = {0.0: c - x1, 180.0: x0 - a, 90.0: d - y1, 270.0: y0 - b}
        turn = max(past, key=past.get)
        if past[turn] <= 0.02:
            continue
        inner, tip, width, offset = {
            0.0: (x1 - cx, c - cx, d - b, fy - cy),
            180.0: (cx - x0, cx - a, d - b, cy - fy),
            90.0: (y1 - cy, d - cy, c - a, cx - fx),
            270.0: (cy - y0, cy - b, c - a, fx - cx)}[turn]
        rise = 0.55 * top
        if tip - inner > 0.25 and rise > 0.32:
            shapes = [p.lead(inner, tip, rise, width)]
        else:
            shapes = [box(inner, -width / 2, tip, width / 2, 0.0,
                          min(0.2, top), p.TIN)]
        out += place(place(shapes, y=offset), cx, cy, turn=turn)
    return out


def measured():
    """Chip parts, leaded packages and plain blocks, in E's frame."""
    out = []
    for kind, x, y, turn in CHIPS:
        out += place(getattr(p, kind)(), x, y, turn=turn)
    for body, feet in LEADED:
        out += _package(body, feet)
    for (x0, y0, z0, x1, y1, z1), shade in BOXES:
        out.append(box(x0, y0, x1, y1, max(z0, 0.0), z1,
                       SHADES.get(shade, shade)))
    return out


def connectors():
    """The parts drawn as shapes of _parts.py, in E's frame."""
    out = []

    def put(shapes, found, turn=0.0):
        x, y = _center(found)
        return place(shapes, x, y, turn=turn)

    for pins, x, y, turn, shift in WIRE_SOCKETS:
        out += place(p.ph_top(pins, shift), x, y, turn=turn)
    x0, _y0, _z0, x1, y1, z1 = NAMED["uart_socket"][0]
    out += place(p.top_socket(4, 1.0, 6.35, 3.05, z1), (x0 + x1) / 2,
                 y1 - 1.525)
    for pin in p.rows(4, 1.0):
        x = (x0 + x1) / 2 + pin
        out.append(box(x - 0.15, y1 - 4.05, x + 0.15, y1 - 3.05, 0.0, 0.25,
                       p.TIN))
    found = NAMED["battery_socket"][0]
    out += put(p.socket(2, 1.25, 7.65, 5.2, 3.3, 1.0, 0.8, 1.7, 2.2, 2.0),
               found)
    for found in NAMED["usb_c"]:
        out += place(p.usb_c(0.36), found[3] - 7.35,
                     (found[1] + found[4]) / 2, turn=90.0)
    found = NAMED["usb_a"][0]
    out += place(p.usb_a(), found[3] - 10.0, (found[1] + found[4]) / 2)
    found = NAMED["card_slot"][0]
    out += put(p.card_tray(found[3] - found[0], found[4] - found[1],
                           found[5]), found)
    for found in NAMED["button"]:
        out += put(p.push_button(3.15, found[4] - found[1], found[5], 0.85),
                   found)
    x0, y0, _z0, x1, y1, z1 = NAMED["switch"][0]
    out += place(p.slide_switch(2.9, 8.65, z1, 2.9), 161.55,
                 (y0 + y1) / 2)
    found = NAMED["capacitor"][0]
    out += put(p.can_capacitor(found[3] - found[0], found[4] - found[1]),
               found, 180.0)
    # Flat-cable sockets: housing, then the latch or the contact row.
    x0, y0, _z0, x1, y1, z1 = NAMED["display_socket"][0]
    out += [box(x0, y0 + 0.7, x1, y1, 0.0, z1, p.WHITE),
            box(x0 + 0.3, y0, x1 - 0.3, y0 + 0.7, 0.37, z1, p.BLACK)]
    x0, y0, _z0, x1, y1, z1 = NAMED["camera_socket"][0]
    out += [box(x0, y0 + 0.9, x1, y1, 0.0, z1, p.WHITE),
            box(x0 + 2.2, y0 + 0.1, x1 - 2.2, y0 + 0.9, 0.0, 0.3, p.TIN)]
    x0, y0, _z0, x1, y1, z1 = NAMED["touch_socket"][0]
    out += [box(x0, y0, x1, y1, 0.0, z1 - 0.1, p.WHITE),
            box(x0 + 0.42, y1 - 1.7, x1 - 0.46, y1, z1 - 0.1, z1, p.BLACK)]
    # The ESP32-P4 under its shield can, and the coin-cell holder.
    x0, y0, z0, x1, y1, _z1 = NAMED["p4"][0]
    out.append(box(x0, y0, x1, y1, z0, P4_H, p.EPOXY))
    x0, y0, x1, y1, height = SHIELD
    out += place(p.shield(x1 - x0, y1 - y0, height), (x0 + x1) / 2,
                 (y0 + y1) / 2)
    x0, y0, z0, x1, y1, z1 = NAMED["battery_spring"][0]
    out.append(box(x0, y0, x1, y1, z0, z1, p.GOLD))
    x, y, diameter, height = HOLDER
    out += place(p.coin_holder(diameter, height), x, y)
    return out


def board():
    """The whole board, in the model frame.

    Returns:
        The solids, as step_gen.write takes them.
    """
    shapes = stack() + standoffs() + cables() + measured() + connectors()
    front = LENS_Z[0]
    cx, cy = (VIEW[0] + VIEW[2]) / 2, (VIEW[1] + VIEW[3]) / 2
    return items(place(place(shapes, -cx, -cy, -front), flip=True))


def parts(_colors):
    """Every model file of the board.

    Args:
        _colors: Colour name to RGB; not used, the spec has its own.

    Returns:
        A dict of file stem to solids.
    """
    return {"panel_7b": board()}
