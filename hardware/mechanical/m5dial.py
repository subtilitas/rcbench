"""M5Stack Dial (SKU K130), panel-mount rotary encoder with a round display.

An ESP32-S3 (Stamp-S3) module in a round housing: a rotating knob ring
around a fixed 1.28in 240 x 240 touch display, a threaded body for a
45mm panel hole and a ring nut. Rear: a 2-pin 2.54mm power terminal
(6 to 36V), PORT.A and PORT.B (HY2.0-4P), the Stamp-S3 under a cover
held by 2 M2 screws, the Stamp-S3's USB-C in a notch of the body.
51.0 x 51.0 x 32.3mm, 46.3g (M5Stack's product page).

The plastic parts are M5Stack's geometry: the 9 parts of the mesh
vendor/m5stack_dial/Dial.stl (MIT licence), each fitted again with the
surfaces its triangles are chords of (_refit.py): planes, cylinders,
cones, tori, and for the thread the flanks and rounded corners as
B-spline surfaces along the helix. A face keeps the mesh's outline; its
edges are lines, circles and helices. Triangles that fit no surface
with their neighbours stay flat faces: the "^" and "M5" marks, the
thread's ends, small blends. The mesh's vertices are within 0.0006 of
the fitted surfaces; the mesh's triangles are chords up to 0.085 off
them, 0.13 in the thread.

The file holds each part twice, assembled about the z axis and set
apart along -x; the assembled copy is used. Its axis is at
x = -0.000157, y = 0.001857 in the mesh; the models put it on z. The
parts, by their z range in the mesh, mm:

- -17.32 to -11.22: bezel, fixed, with the "^" and "M5" marks
- -16.87 to -4.30: knob, rotating, 20 grooves
- -15.47 to 1.00: carrier inside the knob
- -5.30 to 0.00: ring under the bezel's clips, inside the knob
- -5.00 to 11.70: body with the thread, 2.6 pitch, 44.0 over the
  crests, 42.0 at the root, flanks 11.4 degrees from the radial,
  corners rounded 0.2
- -3.70 to -2.70: ring on the body's flange; the panel seats on it
- -0.90 to 4.10: ring nut, 20 notches
- 6.80 to 11.50: Stamp-S3 (its board and cap)
- 10.60 to 13.00: cover over the Stamp-S3

Checked against M5Stack's dimension drawing ("Module Size",
docs.m5stack.com, Dial), lengths from the knob's front face: 14.6 to
the panel seat (mesh 14.62), 28.8 to the body's rear face (mesh 28.82),
51 across (mesh 50.82 over the nut), nut 5 thick (mesh 5.0), 24 between
the cover screws (mesh 24.0), 32.3 overall (mesh 30.32 without the
screw heads, 32.32 with heads of 2.0).

Added, because the mesh lacks them:

- Power terminal header, Cixi Kefa KF2EDGV-2.54-2P (the type on
  M5Stack's product page; Kefa's drawing): 6.88 x 8.1, 10.1 tall, pins
  2.54 apart. Estimated: the 2 round sockets 2.2 across and 7.0 deep,
  the pins 1.0 square.
- PORT.A and PORT.B, HY2.0-4P vertical (Xunpu's drawing of
  WAFER-HY2.0-4PZZ): 10.0 x 5.1, 4 pins 0.5 square 2.0 apart, cavity
  8.9 x 4.0. The drawing's part is 8.0 tall, which would end 2.5 below
  the rear face; the photos show the sockets near the face, so the
  height is set to end 0.3 below it and is an estimate, as is the
  cavity's depth of 5.0. The key slots are not modelled.
- USB-C receptacle on the Stamp-S3, from M5Stack's Stamp-S3.step (MIT
  licence, same repository): shell 8.94 x 3.25 and 7.3 long, reaching
  1.95 past the board's edge; tongue 6.7 x 0.7 and 4.6 long, 0.7 behind
  the opening; body 2.0 long behind it. Shell walls 0.3 are estimated;
  the contacts are not modelled.
- Cover screws: M2 socket head, head 3.8 across and 2.0 tall, hex
  socket 1.5 across flats and 1.0 deep (ISO 4762); the type is inferred
  from the M2 hex key in the box and the drawing's 32.3. Heads only.
- Display lens: 38.0 across and 0.68 thick, the size of the bezel's
  pocket (38.06 to 38.22 across, 0.68 deep); its top is 0.82 behind the
  bezel's front face, where the pocket puts it. The lens's own size is
  not known.
- Display module under the lens: 35.6 across (the outline of a generic
  1.28in GC9A01 module; M5Stack's own is not published), filling the
  3.02 from the lens to the carrier's floor.
- Main board: 40.8 across and 1.0 thick, inferred from the body: its
  rear side on the ledge with the 3 screw bosses, 3.7 behind the panel
  seat, its front side where the posts of the ring under the bezel end.
  Outline, holes and parts are not known; the connectors stand on it.

The encoder (EC30 in M5Stack's schematic), the springs, the battery
connector, the buzzer, the 1.27mm headers between the Stamp-S3 and the
main board and the print on the Stamp-S3 are not modelled: neither
their geometry nor their positions are published.

Colours from M5Stack's product photos. The colours of the carrier, of
the ring on the flange (black) and of the ring under the bezel (orange)
are not visible there and are assumed.

Model frame: z = 0 on the panel's front face, where the ring on the
body's flange seats; the knob points to +z, the body to -z; x to the
right and y up seen from the front, the "^" mark at +y. The mesh's frame
is turned 180 degrees about y and moved: x = -(x_mesh + 0.000157),
y = y_mesh - 0.001857, z = -(z_mesh + 2.7). The terminal is at +y,
USB-C at -y, PORT.A at -x, PORT.B at +x. The nut file has its own
origin: z = 0 on the face towards the panel, the nut below it. In
M5Stack's mesh the nut's face is 1.8 behind the panel seat.

On a panel thicker than 2.2 the nut, 5.0 thick, reaches past the front
of the body's USB-C notch, 7.2 behind the panel seat; not checked on a
part.
"""

import math
from pathlib import Path

import _refit
import _stl

MESH = Path(__file__).resolve().parent / "vendor" / "m5stack_dial" / "Dial.stl"

ORANGE = (0.95, 0.45, 0.08)
GREY = (0.36, 0.37, 0.39)

# The mesh: its axis, z of the panel seat and of the nut's face towards
# it.
AXIS = (-0.000157, 0.001857)
SEAT = -2.7
NUT_FACE = -0.9
# The mesh's parts: lowest and highest z, 0.1mm, to name and colour.
MESH_PARTS = {
    (-17.3, -11.2): ("bezel", ORANGE),
    (-16.9, -4.3): ("knob", GREY),
    (-15.5, 1.0): ("carrier", "black"),
    (-5.3, 0.0): ("inner_ring", ORANGE),
    (-5.0, 11.7): ("body", GREY),
    (-3.7, -2.7): ("seat_ring", "black"),
    (-0.9, 4.1): ("nut", ORANGE),
    (6.8, 11.5): ("stamp", "white"),
    (10.6, 13.0): ("cover", ORANGE),
}
DIAL = ("knob", "bezel", "carrier", "inner_ring", "body", "seat_ring",
        "stamp", "cover")

# Mesh frame, mm.
FRONT = -17.32
REAR, COVER_TOP = 11.5, 13.0
BOARD_FRONT, BOARD_REAR, BOARD_D = 0.0, 1.0, 40.8
LENS_TOP, LENS_T, LENS_D = -16.5, 0.68, 38.0
DISPLAY_D, DISPLAY_FLOOR = 35.6, -12.8
SCREW_X, SCREW_HEAD_D, SCREW_HEAD_H = 12.0, 3.8, 2.0
SCREW_HEX, SCREW_HEX_DEPTH = 1.5, 1.0
TERMINAL_AT = (0.0, 14.665)                # centre of the body's cut-out
TERMINAL_L, TERMINAL_W, TERMINAL_H, TERMINAL_PITCH = 6.88, 8.1, 10.1, 2.54
TERMINAL_SOCKET_D, TERMINAL_SOCKET_DEPTH, TERMINAL_PIN = 2.2, 7.0, 1.0
PORT_AT = (17.1, 0.0)                      # PORT.A; PORT.B is its mirror
PORT_L, PORT_W, PORT_PITCH, PORT_PIN = 10.0, 5.1, 2.0, 0.5
PORT_CAVITY_L, PORT_CAVITY_W, PORT_CAVITY_DEPTH = 8.9, 4.0, 5.0
PORT_BELOW = 0.3                           # socket top below the rear face
PIN_BELOW = 0.4                            # pin tips below the housing's top
STAMP_BOARD_TOP, STAMP_EDGE = 7.8, -16.5
USB_W, USB_H, USB_WALL, USB_OUT, USB_LEN = 8.94, 3.25, 0.3, 1.95, 7.3
USB_TONGUE_W, USB_TONGUE_Z, USB_TONGUE_LEN, USB_TONGUE_IN = (
    6.7, (1.3, 2.0), 4.6, 0.7)
USB_BODY_LEN = 2.0

_cache = {}


def _to_model(p):
    return (-(p[0] - AXIS[0]), p[1] - AXIS[1], -(p[2] - SEAT))


def _to_nut(p):
    return (-(p[0] - AXIS[0]), p[1] - AXIS[1], -(p[2] - NUT_FACE))


def mesh_parts():
    """The mesh's assembled parts in the model frame.

    Returns:
        A dict of part name to (points, triangles); the triangles are
        index triples into the part's points. "nut" is in the nut's own
        frame.

    Raises:
        ValueError: When the mesh does not hold exactly the parts of
            MESH_PARTS about the z axis.
    """
    out = {}
    points, tris = _stl.read(MESH)
    for ids in _stl.parts(points, tris):
        lo, hi = _stl.bounds(points, tris, ids)
        if abs(lo[0] + hi[0]) > 1.0:        # the copy set apart
            continue
        key = (round(lo[2], 1), round(hi[2], 1))
        if key not in MESH_PARTS or MESH_PARTS[key][0] in out:
            raise ValueError(f"{MESH.name}: unexpected part, z {key}")
        name = MESH_PARTS[key][0]
        move = _to_nut if name == "nut" else _to_model
        index, own, local = {}, [], []
        for t in ids:
            tri = []
            for v in tris[t]:
                if v not in index:
                    index[v] = len(own)
                    own.append(move(points[v]))
                tri.append(index[v])
            local.append(tuple(tri))
        out[name] = (own, local)
    if len(out) != len(MESH_PARTS):
        raise ValueError(f"{MESH.name}: {len(out)} parts, "
                         f"{len(MESH_PARTS)} expected")
    return out


def refit():
    """The mesh's parts on fitted surfaces.

    Returns:
        A dict of part name to (points, edges, faces, report) as
        _refit.solid gives them.
    """
    if not _cache:
        for name, (points, tris) in mesh_parts().items():
            _cache[name] = _refit.solid(points, tris)
    return _cache


def _mesh(colors):
    """The mesh's assembled parts as solids.

    Args:
        colors: Colour name to RGB.

    Returns:
        A dict of part name to solid; "nut" is in the nut's own frame.
    """
    tint = {name: col for name, col in MESH_PARTS.values()}
    return {name: ("brep", points, edges, faces,
                   colors[tint[name]] if isinstance(tint[name], str)
                   else tint[name])
            for name, (points, edges, faces, _report) in refit().items()}


def _z(z_mesh):
    return -(z_mesh - SEAT)


def _box(x0, y0, x1, y1, z0, z1, col, holes=()):
    """A box given in the mesh frame, with rectangular holes."""
    def rect(a0, b0, a1, b1):
        return [(-a1, b0), (-a0, b0), (-a0, b1), (-a1, b1)]
    lo, hi = sorted((_z(z0), _z(z1)))
    return ("prism", rect(x0, y0, x1, y1), [rect(*h) for h in holes], lo, hi,
            col)


def _poly(n, r, cx=0.0, rot=0.0):
    return [(cx + r * math.cos(rot + 2 * math.pi * i / n),
             r * math.sin(rot + 2 * math.pi * i / n)) for i in range(n)]


def _terminal(colors):
    """The power terminal header, 2 round sockets with a pin each."""
    cx, cy = TERMINAL_AT
    hx, hy = TERMINAL_L / 2, TERMINAL_W / 2
    top = BOARD_REAR + TERMINAL_H
    floor = top - TERMINAL_SOCKET_DEPTH
    green = colors["termgreen"]
    xs = (cx - TERMINAL_PITCH / 2, cx + TERMINAL_PITCH / 2)
    rect = [(-cx - hx, cy - hy), (-cx + hx, cy - hy), (-cx + hx, cy + hy),
            (-cx - hx, cy + hy)]
    holes = [[(px, py + cy) for px, py in
              _poly(24, TERMINAL_SOCKET_D / 2, -x)] for x in xs]
    out = [_box(cx - hx, cy - hy, cx + hx, cy + hy, BOARD_REAR, floor, green),
           ("prism", rect, holes, _z(top), _z(floor), green)]
    h = TERMINAL_PIN / 2
    for x in xs:
        out.append(_box(x - h, cy - h, x + h, cy + h, floor, top - PIN_BELOW,
                        colors["metal"]))
    return out


def _port(cx, col, colors):
    """One HY2.0-4P socket at x = cx in the mesh frame, pins along y."""
    cy = PORT_AT[1]
    hx, hy = PORT_W / 2, PORT_L / 2
    top = REAR - PORT_BELOW
    floor = top - PORT_CAVITY_DEPTH
    ix, iy = PORT_CAVITY_W / 2, PORT_CAVITY_L / 2
    out = [_box(cx - hx, cy - hy, cx + hx, cy + hy, BOARD_REAR, floor, col),
           _box(cx - hx, cy - hy, cx + hx, cy + hy, floor, top, col,
                [(cx - ix, cy - iy, cx + ix, cy + iy)])]
    h = PORT_PIN / 2
    for i in range(4):
        y = cy + (i - 1.5) * PORT_PITCH
        out.append(_box(cx - h, y - h, cx + h, y + h, floor, top - PIN_BELOW,
                        colors["gold"]))
    return out


def _usb(colors):
    """The USB-C receptacle on the Stamp-S3's board, opening towards -y."""
    y0 = STAMP_EDGE - USB_OUT
    y1 = y0 + USB_LEN
    z0, z1 = STAMP_BOARD_TOP, STAMP_BOARD_TOP + USB_H
    h, w = USB_W / 2, USB_WALL
    metal = colors["metal"]
    return [
        _box(-h, y0, h, y1, z0, z0 + w, metal),
        _box(-h, y0, h, y1, z1 - w, z1, metal),
        _box(-h, y0, -h + w, y1, z0 + w, z1 - w, metal),
        _box(h - w, y0, h, y1, z0 + w, z1 - w, metal),
        _box(-h + w, y1 - USB_BODY_LEN, h - w, y1, z0 + w, z1 - w,
             colors["black"]),
        _box(-USB_TONGUE_W / 2, y0 + USB_TONGUE_IN, USB_TONGUE_W / 2,
             y0 + USB_TONGUE_IN + USB_TONGUE_LEN,
             STAMP_BOARD_TOP + USB_TONGUE_Z[0],
             STAMP_BOARD_TOP + USB_TONGUE_Z[1], colors["black"]),
    ]


def _added(colors):
    """The parts the mesh lacks, in the model frame."""
    lens = [(LENS_D / 2, _z(LENS_TOP + LENS_T)), (LENS_D / 2, _z(LENS_TOP))]
    display = [(DISPLAY_D / 2, _z(DISPLAY_FLOOR)),
               (DISPLAY_D / 2, _z(LENS_TOP + LENS_T))]
    board = [(BOARD_D / 2, _z(BOARD_REAR)), (BOARD_D / 2, _z(BOARD_FRONT))]
    out = [("revolve", lens, 96, colors["glass"]),
           ("revolve", display, 96, colors["chip"]),
           ("revolve", board, 96, colors["black"])]
    head0, head1 = _z(COVER_TOP), _z(COVER_TOP + SCREW_HEAD_H)
    hex_top = head1 + SCREW_HEX_DEPTH
    for cx in (-SCREW_X, SCREW_X):
        disc = _poly(32, SCREW_HEAD_D / 2, cx)
        out.append(("prism", disc, [], hex_top, head0, colors["metal"]))
        out.append(("prism", disc,
                    [_poly(6, SCREW_HEX / math.sqrt(3), cx)], head1, hex_top,
                    colors["metal"]))
    out += _terminal(colors)
    out += _port(PORT_AT[0], colors["red"], colors)
    out += _port(-PORT_AT[0], colors["black"], colors)
    return out + _usb(colors)


def parts(colors):
    """Every model file of the dial.

    Args:
        colors: Colour name to RGB.

    Returns:
        A dict of file stem to solids.
    """
    mesh = _mesh(colors)
    return {
        "m5dial": [mesh[name] for name in DIAL] + _added(colors),
        "m5dial_nut": [mesh["nut"]],
    }
