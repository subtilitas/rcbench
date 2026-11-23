"""Motor simulator: 3 resistors in a triangle with 3 crocodile clips.

    motor_simulator   3 resistors of 600Ω joined corner to corner,
                      a yellow wire from each corner to a black
                      crocodile clip, and the phase tap's lead on one
                      clip, ending in a 3.5mm mono plug

It stands in for a brushless motor on the 3 motor leads of an ESC
(electronic speed controller): each pair of leads sees a resistance in
place of a winding, so the ESC and the phase tap
(protocols/phase_tap/README.md) can be tried with no motor on the
bench. The 3 resistors form a delta: between any 2 clips lie 600Ω
in parallel with 2 x 600Ω, which is 400Ω.

The phase tap's lead is connected to one of the 3 clips (O): the bench
hears the ESC's beeps on that lead. Which clip is free, as the 3 carry
the same beeps; TAP names the clip on corner C. The lead leaves that
clip's boot at its rear face, beside the yellow wire, as 2 wires
crimped to one clip do: it runs along the yellow wire, bends up and
ends in a 3.5mm mono headphone plug (O): 2 contacts, tip and sleeve.
How the lead is fastened inside the boot, its colour, diameter and
length, and which of the plug's contacts carries the tap are not
stated: drawn white, 1.2 across, TAP_LEAD.

The plug's shaft is 3.5 across and 14.0 long as such plugs are: a
sleeve of 9.5, one black ring of 1.0 and a tip of 3.5 with its groove
(2.6 across) behind the ball. The split of the 14.0, the groove and
the handle (a shoulder of 5.0, a barrel of 7.0 x 14.0 and a strain
relief of 3 steps down to 3.0 where the lead enters) are estimated
from common plugs of this kind, not measured on the owner's. The
tip's round end is drawn as 2 steps.

The plug is drawn at PLUG_SCALE, 0.6 of these sizes (O2): its shaft is
2.1 across and 8.4 long in the model, its barrel 4.2 x 8.4, the whole
plug 22.5 long. The sizes above are the plug's own; the model's are
not.

Sources:
    O: the owner, 2026-11-22: a motor stand-in; each resistor is 600
    ohm; of the 2 built, this is the one with black clips and 3
    resistors; the phase tap's lead is connected to one of the clips
    and ends in a 3.5mm mono headphone plug.
    O2: the owner, 2026-11-23: the plug is drawn 40% smaller.
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

import math

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
TAP = "C"                       # the clip the phase tap's lead is on
# The tap's lead: diameter, its distance from the boot's axis, the
# straight piece out of the boot, the bend's radius, the piece up.
TAP_LEAD = (1.2, 1.45, 5.0, 4.0, 20.0)
# The mono plug on the lead's end, from the lead on, each (diameter,
# length): the strain relief's steps, the barrel, the shoulder.
HANDLE = ((3.0, 2.5), (4.0, 2.5), (5.0, 2.5), (7.0, 14.0), (5.0, 2.0))
PLUG_SCALE = 0.6                # the plug as drawn, of its own size (O2)
SHAFT = 3.5                     # the plug's contacts: diameter
SLEEVE = 9.5                    # the sleeve contact's length
RING = 1.0                      # the insulating ring's length
# The tip contact: groove, ball, 2 steps of the round end.
TIP = ((2.6, 1.1), (3.5, 1.3), (2.9, 0.6), (2.0, 0.5))
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


def tap_lead():
    """The phase tap's lead: 3 points (x, y, z).

    Returns:
        Where it leaves the boot of the clip TAP, where its bend ends,
        and its end in the plug's strain relief.
    """
    _across, off, out, bend, up = TAP_LEAD
    x, y, z = lead(TAP)[2]
    return ((x, y, z + off), (x + out + bend, y, z + off + bend),
            (x + out + bend, y, z + off + bend + up))


def tap_lead_length():
    """The lead's length along its middle line."""
    _across, _off, out, bend, up = TAP_LEAD
    return out + bend * math.pi / 2 + up


def plug_steps():
    """The plug as drawn, from the lead on: (diameter, length, colour)."""
    steps = [(d, n, p.BLACK) for d, n in HANDLE]
    steps += [(SHAFT, SLEEVE, p.STEEL), (SHAFT, RING, p.BLACK)]
    steps += [(d, n, p.STEEL) for d, n in TIP]
    return [(d * PLUG_SCALE, n * PLUG_SCALE, rgb) for d, n, rgb in steps]


def plug_tip():
    """The end of the mono plug's tip: (x, y, z)."""
    x, y, z = tap_lead()[2]
    return (x, y, z + sum(length for _d, length, _rgb in plug_steps()))


def tap():
    """The phase tap's lead out of the boot of TAP, and its mono plug."""
    start, turned, end = tap_lead()
    _across, _off, out, bend, _up = TAP_LEAD
    x, y = end[0], end[1]
    # The lead's plane: x and z through the corner's y.
    frame = ((0.0, y, 0.0), (1.0, 0.0, 0.0), (0.0, 0.0, 1.0),
             (0.0, -1.0, 0.0))
    corner = (start[0] + out, start[2])
    shapes = [s.pipe([(start[0], start[2]),
                    (*corner, (corner[0], corner[1] + bend, 1)),
                    (turned[0], turned[2]), (end[0], end[2])],
                   TAP_LEAD[0] / 2, p.WHITE, frame=frame)]
    z = end[2]
    for diameter, length, rgb in plug_steps():
        shapes.append(s.ext(s.circle(x, y, diameter / 2), z, z + length, rgb))
        z += length
    return shapes


def simulator():
    """The simulator as shapes."""
    out = resistors() + joints()
    for name in CORNERS:
        out += wire_and_clip(name)
    return out + tap()


def parts(_colors):
    """The model of the simulator.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items(simulator())}
