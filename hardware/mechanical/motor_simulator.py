"""Motor simulator: 3 resistors in a triangle with 3 crocodile clips.

    motor_simulator   3 resistors of 600 ohm joined corner to corner,
                      a yellow wire from each corner to a black
                      crocodile clip

It stands in for a brushless motor on the 3 motor leads of an ESC
(electronic speed controller): each pair of leads sees a resistance in
place of a winding, so the ESC and the phase tap
(protocols/phase_tap/README.md) can be tried with no motor on the
bench. The 3 resistors form a delta: between any 2 clips lie 600 ohm
in parallel with 2 x 600 ohm, which is 400 ohm.

Sources:
    O: the owner, 2026-11-22: a motor stand-in; each resistor is 600
    ohm; of the 2 built, this is the one with black clips and 3
    resistors.
    P: one owner photo of 2026-11-22 (not in the repository), taken at
    an angle with no scale in it: blue resistors with axial leads
    soldered corner to corner, a yellow wire from each corner, the 3
    wires running side by side to 3 crocodile clips in black boots.

Every size is estimated from P by eye; a resistor's body is taken as
6.5 long and 2.5 across, the size of a common 0.6W metal film part, and
the rest is scaled from it: the triangle's side (13.0), the leads
(0.6), the solder at a corner (1.6 across), the wires (1.4 across, 20
long), the clips (boot 7.0 across and 20 long with a neck of 4.5, jaws
9 long). Not measured: anything. What the resistors' rating is and what
voltage the simulator bears are not stated.

Drawn straight and regular: the triangle stands across the wires, so
that the 3 wires leave side by side, 13.0 apart; on the built part
they are bent together. Not drawn: the resistors' colour bands, the
clips' teeth, spring and hinge, the wires' strands.

Frame: origin at the middle of the triangle's lower side. The triangle
stands in the y-z plane, corner A at -y, B at +y, C on top. The wires
run towards -x. All sizes are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _parts as p
import _solid as s

STEM = "motor_simulator"
OHM = 600                       # each resistor (O)
SIDE = 13.0                     # the triangle's side, corner to corner
BODY = (6.5, 2.5)               # a resistor's body: length, diameter
LEAD = 0.6
SOLDER = 1.6                    # the joint at a corner: diameter
WIRE = (1.4, 20.0)              # diameter, length
# Clip: the boot's neck (diameter, length), its body (diameter,
# length), the jaws (length, width, thickness, gap between them).
CLIP = ((4.5, 5.0), (7.0, 15.0), (9.0, 3.6, 0.8, 1.0))
RESISTOR_BLUE = (0.16, 0.42, 0.72)
WIRE_YELLOW = (0.93, 0.80, 0.12)
CORNERS = {"A": (-SIDE / 2, 0.0), "B": (SIDE / 2, 0.0),
           "C": (0.0, SIDE * 3 ** 0.5 / 2)}        # (y, z)
SIDES = (("A", "B"), ("B", "C"), ("C", "A"))


def between_clips():
    """The resistance between any 2 clips, ohm: a delta of 3 equal ones."""
    return OHM * (2 * OHM) / (3 * OHM)


def _along(a, b, start, end):
    """2 points on the line from a to b, at 2 distances from a."""
    length = ((b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2) ** 0.5
    u = ((b[0] - a[0]) / length, (b[1] - a[1]) / length)
    return [(a[0] + u[0] * d, a[1] + u[1] * d) for d in (start, end)]


def resistors():
    """The 3 resistors: on each side a body and its 2 leads."""
    out = []
    clear, half = SOLDER / 2, BODY[0] / 2
    for a, b in SIDES:
        pa, pb = CORNERS[a], CORNERS[b]
        for start, end, radius, rgb in (
                (clear, SIDE / 2 - half, LEAD / 2, p.TIN),
                (SIDE / 2 - half, SIDE / 2 + half, BODY[1] / 2,
                 RESISTOR_BLUE),
                (SIDE / 2 + half, SIDE - clear, LEAD / 2, p.TIN)):
            out.append(s.pipe(_along(pa, pb, start, end), radius, rgb,
                              frame=s.X))
    return out


def joints():
    """The solder at the 3 corners, a short round piece along x."""
    return [s.ext(s.circle(y, z, SOLDER / 2), -SOLDER / 2, SOLDER / 2,
                  p.TIN, frame=s.X) for y, z in CORNERS.values()]


def clip_end(name):
    """The tip of the clip on corner ``name``: (x, y, z)."""
    (_nd, neck), (_bd, body), (jaws, _w, _t, _g) = CLIP
    y, z = CORNERS[name]
    return (-SOLDER / 2 - WIRE[1] - neck - body - jaws, y, z)


def lead(name):
    """The wire from corner ``name``: its start, middle and end, (x, y, z)."""
    y, z = CORNERS[name]
    x = -SOLDER / 2
    return ((x, y, z), (x - WIRE[1] / 2, y, z), (x - WIRE[1], y, z))


def wire_and_clip(name):
    """One yellow wire and its crocodile clip, along -x."""
    (neck_d, neck), (body_d, body), (jaws, wide, thick, gap) = CLIP
    y, z = CORNERS[name]
    start, _middle, end = lead(name)
    frame = ((0.0, y, z), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0),
             (0.0, 0.0, 1.0))
    out = [s.pipe([(start[0], 0.0), (end[0], 0.0)], WIRE[0] / 2,
                  WIRE_YELLOW, frame=frame)]
    x = end[0]
    out.append(s.ext(s.circle(y, z, neck_d / 2), x - neck, x, p.BLACK,
                     frame=s.X))
    x -= neck
    out.append(s.ext(s.circle(y, z, body_d / 2), x - body, x, p.BLACK,
                     frame=s.X))
    x -= body
    for side in (1, -1):
        low = z + side * gap / 2
        out.append(s.box(x - jaws, y - wide / 2, x, y + wide / 2,
                         min(low, low + side * thick),
                         max(low, low + side * thick), p.STEEL))
    return out


def simulator():
    """The simulator as shapes."""
    out = resistors() + joints()
    for name in CORNERS:
        out += wire_and_clip(name)
    return out


def parts(_colors):
    """The model of the simulator.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items(simulator())}
