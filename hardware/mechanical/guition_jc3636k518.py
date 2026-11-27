"""Guition JC3636K518, a rotary knob with a round 1.8in touch display.

    guition_jc3636k518    red ring, black glass, black base

Shenzhen Jingcai Intelligent (Guition) sells it as SKU 10160002, model
JC3636K518C_I_YR1, "ESP32 1.8-inch IPS capacitive touch": an ESP32-S3
behind a round 360 x 360 display under a round cover glass. A red
aluminium ring with 24 facets turns about the glass; the glass and the
black base below the ring stand still. The base carries a USB-C socket
(a CH340 serial bridge behind it), a round hole, a button and a small
hole on its side.

Sources:

- The display's drawing, Guition's "1.85" display sheet in the archive
  JC3636K518CN_knob_EN.zip (folder 3-Specification, pan.jczn1688.com):
  cover glass 55.00 +/- 0.1 across and 0.70 thick, the display under it
  49.95 across, 3.25 thick in all, active area 45.68 across. The
  archive holds no licence; the model takes the numbers, not the file.
- Guition's size picture (same archive, folder 5-Dimension, and page 9
  of "JC3636K518 Instructions-EN.pdf"): 66 across and 22 tall,
  "hand-measured dimensions with 0.2-0.5 cm tolerance". No drawing of
  the housing and no 3D file are published.
- Two owner photos, 2026-11-27, 1440 x 1920 pixels, not in the
  repository: knob-top.jpg, from the front about 41 degrees above the
  table; knob-side.jpg, from the front left about 37 degrees above it.
  The cover glass, 55.00 across, is the scale: 653 pixels across in
  knob-top.jpg, 11.87 pixels a mm.

Estimated from the photos, at that scale; each value is good to about
1mm:

- Ring: 64.0 across the corners (knob-top.jpg, 759 pixels; Guition's
  picture gives 66), 9.0 tall (knob-top.jpg 8.3, knob-side.jpg 9.7),
  24 flat facets (knob-top.jpg, 15 degrees between the corners along
  the front edge), bore 55.6. The facets on the part are twisted about
  the axis; the model's facets are parallel to it. The chamfer on the
  ring's top outer edge is not modelled.
- Base: 62.0 across (knob-top.jpg: its edges about 1 inside the ring's
  on each side), 7.0 tall (knob-top.jpg 6.9, knob-side.jpg 6.8).
- Gap between base and ring: 0.5 tall, a neck 61.0 across.
- Glass top: 0.5 below the ring's top face (both photos show the
  glass's edge inside the ring). The overall height is 16.5; Guition's
  picture gives 22 with up to 5 of tolerance, and the photos give
  16.1 to 16.5. A case drawn round this model needs the device itself
  checked for height.
- Side features, angles counter-clockwise from +x seen from above,
  read from knob-side.jpg (where all four show) and checked on
  knob-top.jpg (small hole and USB-C, 20 degrees apart there, 21 in
  knob-side.jpg):

      small hole    -111 deg, 1.0 across, 4.5 over the underside
      USB-C         -90 deg, opening 9.8 x 3.6, 3.5 over the underside
      round hole    -56 deg, 2.6 across, 4.0 over the underside
      button        -31 deg, 5.0 x 2.6, 0.6 proud, 3.0 over the
                    underside

  The USB-C receptacle in the opening is _parts.usb_c (8.94 x 3.16),
  its front 0.6 behind the base's surface. The round hole and the
  small hole are dark discs 0.05 thick on the surface: their depth is
  not known. What the round hole holds is not known.

Not modelled, because neither photo shows it: the underside, and any
foot, screw or mounting feature on it. The board, the ESP32-S3, the
encoder, the vibration motor and the speaker are not modelled.

Frame: z = 0 on the base's underside, the glass to +z; the USB-C
opening faces -y. All sizes are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import math

import _parts as p
import _solid as s

STEM = "guition_jc3636k518"

RED = (0.70, 0.14, 0.15)
BLACK = (0.06, 0.06, 0.07)
GLASS = (0.10, 0.12, 0.18)
DARK = (0.02, 0.02, 0.02)
DISPLAY = (0.12, 0.12, 0.13)

# Base, neck and ring along z.
BASE_D, BASE_H = 62.0, 7.0
NECK_D, NECK_H = 61.0, 0.5
RING_D, RING_H, RING_FACETS, RING_BORE = 64.0, 9.0, 24, 55.6
# Cover glass and display from the display's drawing; the glass's top
# under the ring's top face.
GLASS_D, GLASS_T, GLASS_DROP = 55.0, 0.70, 0.5
DISPLAY_D, STACK_T = 49.95, 3.25
# Side features: angle (degrees from +x), z of the middle, size.
USB_AT, USB_Z, USB_W, USB_H, USB_IN = -90.0, 3.5, 9.8, 3.6, 0.6
USB_POCKET = 8.4                  # the pocket's depth behind the surface
HOLE_AT, HOLE_Z, HOLE_D = -56.0, 4.0, 2.6
PIN_AT, PIN_Z, PIN_D = -111.0, 4.5, 1.0
BUTTON_AT, BUTTON_Z, BUTTON_W, BUTTON_H, BUTTON_OUT = -31.0, 3.0, 5.0, 2.6, 0.6
MARK = 0.05                       # thickness of a hole's dark disc


def _disc(d):
    return s.circle(0.0, 0.0, d / 2)


def _notched(r, half, depth):
    """A circle of radius r with a slot at -y, half wide, depth deep."""
    yc = -math.sqrt(r * r - half * half)
    arc = (0.0, 0.0, 1)
    return [(half, yc, arc), (r, 0.0, arc), (0.0, r, arc), (-r, 0.0, arc),
            (-half, yc), (-half, -r + depth), (half, -r + depth)]


def _facets(d, n):
    """A polygon of n corners on a circle d across, a flat at -y."""
    off = math.pi / n - math.pi / 2
    return [(d / 2 * math.cos(off + 2 * math.pi * i / n),
             d / 2 * math.sin(off + 2 * math.pi * i / n)) for i in range(n)]


def base():
    """The base, with the USB-C pocket at -y, and the neck over it."""
    r = BASE_D / 2
    z0, z1 = USB_Z - USB_H / 2, USB_Z + USB_H / 2
    pocket = _notched(r, USB_W / 2, USB_POCKET)
    return [s.ext(_disc(BASE_D), 0.0, z0, BLACK),
            s.ext(pocket, z0, z1, BLACK),
            s.ext(_disc(BASE_D), z1, BASE_H, BLACK),
            s.ext(_disc(NECK_D), BASE_H, BASE_H + NECK_H, BLACK)]


def ring():
    """The turning ring: 24 facets round a round bore."""
    z0 = BASE_H + NECK_H
    return [s.ext(_facets(RING_D, RING_FACETS), z0, z0 + RING_H, RED,
                  holes=[_disc(RING_BORE)])]


def face():
    """Glass, display and the fixed carrier under them, in the ring."""
    z0 = BASE_H + NECK_H
    top = z0 + RING_H - GLASS_DROP
    glass = top - GLASS_T
    floor = top - STACK_T
    return [s.ext(_disc(GLASS_D), z0, floor, BLACK),
            s.ext(_disc(GLASS_D), floor, glass, BLACK,
                  holes=[_disc(DISPLAY_D)]),
            s.ext(_disc(DISPLAY_D), floor, glass, DISPLAY),
            s.ext(_disc(GLASS_D), glass, top, GLASS)]


def _radial(shapes, angle):
    """Shapes drawn facing -y, turned to face the given angle."""
    return s.place(shapes, turn=angle + 90.0)


def side():
    """USB-C receptacle, button and the two holes on the base's side."""
    r = BASE_D / 2
    usb = s.place(p.usb_c(), y=-(r - USB_IN) + 7.35, z=USB_Z - 1.67)
    out = _radial(usb, USB_AT)
    # Frame s.Y pushes a profile in (x, z) along -y: d = -y.
    for at, z, d in ((HOLE_AT, HOLE_Z, HOLE_D), (PIN_AT, PIN_Z, PIN_D)):
        disc = s.ext(s.circle(0.0, z, d / 2), r, r + MARK, DARK,
                     frame=s.Y)
        out += _radial([disc], at)
    cap = s.ext(s.rounded(-BUTTON_W / 2, BUTTON_Z - BUTTON_H / 2,
                          BUTTON_W / 2, BUTTON_Z + BUTTON_H / 2, 0.8),
                r - 0.1, r + BUTTON_OUT, BLACK, frame=s.Y)
    out += _radial([cap], BUTTON_AT)
    return out


def knob():
    """Every solid of the knob."""
    return base() + ring() + face() + side()


def parts(_colors):
    """The model of the knob.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items(knob())}
