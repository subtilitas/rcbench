"""T-Motor AT2814, a brushless outrunner for fixed-wing models.

    tmotor_at2814   base plate, windings, bell (can and front cap),
                    shaft, print, 3 leads

Sources, none in the repository:
    D: the maker's 2D drawing "AT2814 V2.0" (tmotor-at2814-motor-2d-
    drawings.pdf): 54.5 long over the shaft, 35.2 across, the body 36
    long, the shaft 5.0 across and 15.5 long in front of the body with
    a collar of 1.75, 2 behind it; 4 threads M3 on a circle of 15 in
    the front cap; 4 threads M3 in the base, 19 apart on one axis and
    25 on the other.
    T: the maker's page "AT2814 Long Shaft": 35.2 x 54.5, 12N14P (12
    slots, 14 poles), shaft 5 in and out, KV900, KV1050 and KV1200,
    107 to 108g with cable, "Lead: Enameled Wire 100mm".
    P: the maker's product picture of the AT2814 KV1050: front cap and
    base bright metal with openings, the can black with white print,
    copper windings behind the openings.

Chosen (owner, 2026-11-22): this motor stands for the one a 40A ESC
drives; the repository names no motor for its measurements. KV1050 is
drawn; D is the same for the 3 windings.

How the body's 36 splits is read from D at 6.15 pixels a mm, within
0.5: base 9.5, can 17.5, front cap 9.0. The base's cage is drawn as its
end plate (3.0) alone, so that the windings show as a ring of 31.0
between plate and can, where P shows them through the cage's openings.

Not in the sources, estimated: the windings' diameter (31.0), the
collar's diameter (8.0), the leads' diameter (1.6), their distance
(4.0) and where they leave the base (its side, 6.25 over the mounting
face, towards -x), the colours' values. The leads are drawn straight,
100 long (T), in the copper of enamelled wire; P shows no sleeve on
them.

Not drawn: the openings of cap and cage, the magnets, the bearings, the
flats on the shaft, the propeller adapter and the cross mount of D's
lower half. The print is the type as plain text, on the base's
mounting face: flat letters do not lie on the can.

Frame: origin on the shaft's axis, on the base's mounting face. z along
the shaft towards the propeller; the leads leave towards -x; the base's
threads lie 19 apart along x and 25 along y. All sizes are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _parts as p
import _solid as s

STEM = "tmotor_at2814"
ACROSS, BODY, LENGTH = 35.2, 36.0, 54.5   # LENGTH: over the shaft
PLATE, BASE, CAN, CAP = 3.0, 9.5, 17.5, 9.0     # along the shaft
WINDINGS = 31.0
SHAFT = (5.0, 2.0, 15.5)        # diameter, behind the body, in front
COLLAR = (8.0, 1.75)            # diameter, length
FRONT_THREADS = (15.0, 3.0)     # circle, thread
BASE_THREADS = (19.0, 25.0, 3.0)    # along x, along y, thread
METAL = (0.78, 0.78, 0.80)
CAN_BLACK = (0.10, 0.10, 0.11)
LABEL = ("AT2814", 2.4)
# Leads: diameter, distance, length, height over the mounting face.
LEADS = (1.6, 4.0, 100.0, 6.25)
PHASES = ("A", "B", "C")        # the leads from -y to +y


def lead_y(k):
    """The y of lead k of PHASES."""
    return (k - 1) * LEADS[1]


def lead(k):
    """Lead k: where it leaves the base, its middle, its end.

    Returns:
        3 points (x, y, z).
    """
    _across, _apart, length, z = LEADS
    x, y = -ACROSS / 2, lead_y(k)
    return ((x, y, z), (x - length / 2, y, z), (x - length, y, z))


def base():
    """The base's end plate with its 4 threads, and the print."""
    along, across, thread = BASE_THREADS
    holes = [s.circle(0.0, 0.0, SHAFT[0] / 2)]
    holes += [s.circle(side * along / 2, 0.0, thread / 2)
              for side in (1, -1)]
    holes += [s.circle(0.0, side * across / 2, thread / 2)
              for side in (1, -1)]
    out = [s.ext(s.circle(0.0, 0.0, ACROSS / 2), 0.0, PLATE, METAL,
                 holes=holes)]
    # The print on the mounting face, read from behind the motor.
    out += s.place(p.text(LABEL[0], LABEL[1], p.BLACK), 0.0, -7.5, 0.0,
                   flip=True)
    return out


def windings():
    """The stator's windings as a ring between plate and can."""
    return [s.ext(s.circle(0.0, 0.0, WINDINGS / 2), PLATE, BASE, p.COPPER,
                  holes=[s.circle(0.0, 0.0, SHAFT[0] / 2)])]


def bell():
    """The can and the front cap with its 4 threads."""
    circle, thread = FRONT_THREADS
    bore = [s.circle(0.0, 0.0, SHAFT[0] / 2)]
    r = circle / 2 * 2 ** -0.5
    return [
        s.ext(s.circle(0.0, 0.0, ACROSS / 2), BASE, BASE + CAN, CAN_BLACK,
              holes=bore),
        s.ext(s.circle(0.0, 0.0, ACROSS / 2), BASE + CAN, BODY, METAL,
              holes=bore + [s.circle(sx * r, sy * r, thread / 2)
                            for sx in (1, -1) for sy in (1, -1)]),
        s.ext(s.circle(0.0, 0.0, COLLAR[0] / 2), BODY, BODY + COLLAR[1],
              METAL, holes=bore)]


def shaft():
    """The shaft from behind the base to its end in front."""
    across, behind, _front = SHAFT
    return [s.ext(s.circle(0.0, 0.0, across / 2), -behind,
                  LENGTH - behind, p.STEEL)]


def leads():
    """The 3 leads, straight towards -x."""
    across, _apart, _length, z = LEADS
    out = []
    for k in range(3):
        start, _middle, end = lead(k)
        out.append(s.pipe([(start[0], start[1]), (end[0], end[1])],
                          across / 2, p.COPPER,
                          frame=((0.0, 0.0, z), (1.0, 0.0, 0.0),
                                 (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))))
    return out


def motor():
    """The motor as shapes."""
    return base() + windings() + bell() + shaft() + leads()


def parts(_colors):
    """The model of the motor.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items(motor())}
