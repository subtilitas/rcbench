"""Hitec D485HW, a standard size digital servo, with a horn and its lead.

    hitec_d485hw    case of 3 shells, 2 ears, boss, shaft, cross horn
                    HD-X25, type plate, print, lead bushing, 3 wires
                    and the 3 pin plug

Sources, none in the repository:
    D: the maker's dimension drawing of the D485HW (HiTEC Europe /
    Multiplex shop, 240718_technische_zeichnung_servo_d485hw.jpg): case
    39.8 x 19.8, 53.0 over the ears, mounting holes of 4.5, 48.0 apart
    along the case and 10.0 across, shaft axis 14.0 from a row of
    holes, ears 2.5 thick with their underside 28.0 over the case's
    underside, 38.0 high.
    M: the maker's STEP model D485HW.stp (same shop, written by Creo on
    2023-04-24, 10 solids), read with OpenCASCADE: the seams between
    the 3 shells (2.05 and 23.55 over the underside), the deck (37.65),
    the boss (to 39.85) and the shaft (6.0 across, to 43.05), the type
    plate (10.2 x 15.2 x 0.24), the lead's bushing (9.0 x 5.0 at the
    case, 6.0 long, its axis 6.25 over the underside) and the 3 wires
    (1.3 across, 1.8 apart). M is 40.0 x 20.0 where D gives 39.8 x
    19.8; the model follows D.
    S: the maker's data sheet "D485HW General Specification", Ver2.1:
    lead 300 long, 24AWG; spline 25T of 6.0; 40.0 x 20.0 x 38.0 with a
    general tolerance of +-0.1.
    H: Hitec's "Servo Actuator Horn & Spline Technical Data Sheet",
    page 43, horn HD-X25, one of the 4 horns in the box: hub 9.0
    across, 6.2 high, bore 3.3; arms with holes of 1.6 at 7, 10, 13
    and 16 from the axis on one pair and at 6, 9, 12 and 15 on the
    other; the arms' ends round with 3.0.
    I: the maker's operating instructions for its servos: "Minus pole
    (-) black (brown), Plus pole (+) red, Impulse yellow (orange)".

The lead: 3 wires in a flat band, red in the middle. I gives each
wire's colour; which edge of the band carries the yellow signal wire is
not in the sources: drawn at +y. The plug is the maker's "3 Pin (Uni)"
(shop listing): 3 contacts 2.54 apart in a row. LEAD draws 60 of the
lead's 300 (S), straight, so that the part stays small enough to turn
in a picture.

Not in the sources, estimated: the ears' slots (2.4 wide), the case's
corner radius (1.0), the boss's 2 diameters (13.2 and 10.3, read from a
picture of M at 22 pixels a mm), the bushing's 3 steps in place of its
taper, the plug's housing (7.7 x 2.6 x 14.0, contact holes of 0.7), the
horn's place on the shaft (its hub 0.3 over the boss), the thickness of
its arms (2.5) and their width at the hub (8.0), the colours: case and
horn black as in the maker's product pictures, the shaft dark grey as
in M.

Not drawn: the spline's 25 teeth, the horn's screw, the case's 4
screws, the ribs under the ears, the raised gear cover on the deck
(0.4), the maker's sign. The print is the type as plain text: "D485HW"
on the type plate and on both long sides of the middle shell.

Frame: as servo_standard.py. Origin on the shaft's axis, on the case's
underside. x along the case, the long end towards -x; the lead leaves
the end at +x. y across; z up. All sizes are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _parts as p
import _solid as s

STEM = "hitec_d485hw"
LENGTH, WIDTH = 39.8, 19.8
SHAFT = 29.9            # shaft axis from the case's long end: 19.9 + 10.0
SEAMS = (2.05, 23.55)   # bottom cover to middle, middle to top shell
DECK = 37.65            # the top shell's upper face
EAR = (6.6, 2.5, 28.05)  # length, thickness, underside over the case's
HOLES = (48.0, 10.0, 4.5, 2.4)  # along, across, diameter, slot
CORNER = 1.0
BOSS = ((13.2, 39.25), (10.3, 39.85))   # diameter, top
SHAFT_D, SHAFT_TOP = 6.0, 43.05
PLATE = (-23.25, -7.6, -13.05, 7.6, 0.24)   # x0, y0, x1, y1, thickness
SILVER = (0.78, 0.78, 0.76)
DARK = (0.32, 0.32, 0.32)
LABEL = ("D485HW", 2.6, 0.1)    # text, letter height, added gap
# Horn HD-X25: hub diameter and height, the screw's bore, the hub's
# underside over the boss, the arms' thickness, their half width at
# the hub and at their end, hole diameter, the holes' distances from
# the axis along x and along y.
HORN = (9.0, 6.2, 3.3, 0.3, 2.5, 4.0, 3.0, 1.6)
HORN_X = (7.0, 10.0, 13.0, 16.0)
HORN_Y = (6.0, 9.0, 12.0, 15.0)
# Lead: the bushing's 3 steps (width, height) and each step's length,
# its axis over the underside; the wires' diameter and distance, the
# length drawn; the plug's width, thickness and length, the contacts'
# distance and holes.
BUSHING = (((9.0, 5.0), (6.6, 3.8), (4.9, 2.6)), 2.0, 6.25)
LEAD = (1.3, 1.8, 60.0)
PLUG = (7.7, 2.6, 14.0, 2.54, 0.7)
# The wires from -y to +y: minus, plus, signal (I).
WIRES = (("minus", p.BLACK), ("plus", p.RED), ("signal", p.YELLOW))

X0, X1 = -SHAFT, LENGTH - SHAFT


def _ear(side):
    """One ear's outline with its two open holes, seen from above.

    Args:
        side: +1 for the ear at +x, -1 for the ear at -x.

    Returns:
        The profile, counter-clockwise.
    """
    x0 = X1 if side > 0 else X0
    x1 = x0 + side * EAR[0]
    cx = (X0 + X1) / 2 + side * HOLES[0] / 2
    r, h = HOLES[2] / 2, HOLES[3] / 2
    mouth = cx + side * (r * r - h * h) ** 0.5
    out = [(x0, -side * WIDTH / 2), (x1, -side * WIDTH / 2)]
    for cy in (-side * HOLES[1] / 2, side * HOLES[1] / 2):
        a, b = cy - side * h, cy + side * h
        arc = (cx, cy, -1)
        out += [(x1, a), (mouth, a, arc), (cx, cy - side * r, arc),
                (cx - side * r, cy, arc), (cx, cy + side * r, arc),
                (mouth, b), (x1, b)]
    return out + [(x1, side * WIDTH / 2), (x0, side * WIDTH / 2)]


def label(rgb):
    """The type as shapes on z = 0, its middle at the origin."""
    string, height, gap = LABEL
    steps = [p.GLYPHS[c][0] * height + gap for c in string]
    out, x = [], -(sum(steps) - gap) / 2
    for c, step in zip(string, steps, strict=True):
        out += s.place(p.text(c, height, rgb), x + (step - gap) / 2)
        x += step
    return out


def _on_side(shapes, x, z, side):
    """Stand shapes drawn on z = 0 on a long side of the case.

    Args:
        shapes: Shapes in the frame Z, moved but not turned.
        x: The place of the shapes' origin along the case.
        z: Its height.
        side: -1 for the side at -y, +1 for the side at +y.

    Returns:
        The shapes, their x along the side as read from outside, their
        y up and their z out of the side.
    """
    u, w = (-side * 1.0, 0.0, 0.0), (0.0, side * 1.0, 0.0)
    out = []
    for outer, holes, d0, d1, rgb, (o, _u, _v, _w) in shapes:
        origin = (x + o[0] * u[0], side * WIDTH / 2 + o[2] * w[1],
                  z + o[1])
        out.append((outer, holes, d0, d1, rgb,
                    (origin, u, (0.0, 0.0, 1.0), w)))
    return out


def case():
    """The 3 shells, the ears, the boss, the type plate and the print."""
    outline = s.rounded(X0, -WIDTH / 2, X1, WIDTH / 2, CORNER)
    out = [s.ext(outline, lo, hi, p.BLACK)
           for lo, hi in zip((0.0, *SEAMS), (*SEAMS, DECK), strict=True)]
    out += [s.ext(_ear(side), EAR[2], EAR[2] + EAR[1], p.BLACK)
            for side in (1, -1)]
    low = DECK
    for diameter, top in BOSS:
        out.append(s.ext(s.circle(0.0, 0.0, diameter / 2), low, top,
                         p.BLACK,
                         holes=[s.circle(0.0, 0.0, SHAFT_D / 2)]))
        low = top
    x0, y0, x1, y1, thick = PLATE
    out.append(s.box(x0, y0, x1, y1, DECK, DECK + thick, SILVER))
    out += s.place(label(p.BLACK), (x0 + x1) / 2, 0.0, DECK + thick,
                   turn=90.0)
    for side in (-1, 1):
        out += _on_side(label(p.WHITE), (X0 + X1) / 2,
                        (SEAMS[0] + SEAMS[1]) / 2, side)
    return out


def shaft():
    """The output shaft from the deck up, without its teeth."""
    return [s.ext(s.circle(0.0, 0.0, SHAFT_D / 2), DECK, SHAFT_TOP, DARK)]


def horn_outline():
    """The cross of the horn HD-X25 seen from above, counter-clockwise."""
    root, tip = HORN[5], HORN[6]
    near = root + 1.2
    out = []
    # Arm by arm: its 2 sides and the half circle at its end, in the
    # arm's own (along, across), turned to +x, +y, -x, -y.
    for (ax, ay), reach in (((1, 0), HORN_X[-1]), ((0, 1), HORN_Y[-1]),
                            ((-1, 0), HORN_X[-1]), ((0, -1), HORN_Y[-1])):
        def at(along, across, ax=ax, ay=ay):
            return (along * ax - across * ay, along * ay + across * ax)

        center = (*at(reach, 0.0), 1)
        out += [at(near, -root), (*at(reach, -tip), center),
                (*at(reach + tip, 0.0), center), at(reach, tip),
                at(near, root)]
    return out


def horn_holes():
    """The centres of the horn's 16 holes, (x, y)."""
    return ([(side * r, 0.0) for r in HORN_X for side in (1, -1)]
            + [(0.0, side * r) for r in HORN_Y for side in (1, -1)])


def horn():
    """The horn: its hub on the shaft and the cross at the hub's top."""
    hub, high, bore, over, thick, _root, _tip, hole = HORN
    z0 = BOSS[-1][1] + over
    z1 = z0 + high
    ring = s.circle(0.0, 0.0, hub / 2)
    out = [s.ext(ring, z0, SHAFT_TOP, p.BLACK,
                 holes=[s.circle(0.0, 0.0, SHAFT_D / 2)]),
           s.ext(ring, SHAFT_TOP, z1 - thick, p.BLACK,
                 holes=[s.circle(0.0, 0.0, bore / 2)])]
    holes = [s.circle(0.0, 0.0, bore / 2)] + [
        s.circle(x, y, hole / 2) for x, y in horn_holes()]
    out.append(s.ext(horn_outline(), z1 - thick, z1, p.BLACK, holes=holes))
    return out


def lead_exit():
    """Where the lead leaves the case: (x, y, z) on the end at +x."""
    return (X1, 0.0, BUSHING[2])


def wire_y(k):
    """The y of wire k of WIRES at the bushing and along the lead."""
    return (k - 1) * LEAD[1]


def plug_pin(k):
    """The middle of contact k of WIRES in the plug's end face."""
    steps, step, z = BUSHING
    x = X1 + len(steps) * step + LEAD[2] + PLUG[2]
    return (x, (k - 1) * PLUG[3], z)


def lead():
    """The bushing, the 3 wires and the plug, straight along +x."""
    steps, step, z = BUSHING
    wire, _apart, length = LEAD
    wide, thick, long, _pitch, hole = PLUG
    out, x = [], X1
    for w, h in steps:
        out.append(s.ext(s.rect(-w / 2, z - h / 2, w / 2, z + h / 2), x,
                         x + step, p.BLACK, frame=s.X))
        x += step
    for k, (_name, rgb) in enumerate(WIRES):
        out.append(s.pipe([(x, wire_y(k)), (x + length, wire_y(k))],
                          wire / 2, rgb,
                          frame=((0.0, 0.0, z), (1.0, 0.0, 0.0),
                                 (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))))
    x += length
    out.append(s.ext(
        s.rect(-wide / 2, z - thick / 2, wide / 2, z + thick / 2), x,
        x + long, p.BLACK,
        holes=[s.rect(plug_pin(k)[1] - hole / 2, z - hole / 2,
                      plug_pin(k)[1] + hole / 2, z + hole / 2)
               for k in range(3)], frame=s.X))
    return out


def servo():
    """The servo as shapes: case, shaft, horn and lead."""
    return case() + shaft() + horn() + lead()


def parts(_colors):
    """The model of the servo.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items(servo())}
