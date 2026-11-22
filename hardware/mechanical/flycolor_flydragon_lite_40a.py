"""Flycolor FlyDragon Lite 40A, a brushless ESC for 2 to 4 cells.

    flycolor_flydragon_lite_40a   the ESC in its heat-shrink with its
                                  capacitor, print, 2 power leads with
                                  an XT60 plug, 3 motor leads and the
                                  signal lead with its 3 pin plug

ESC is electronic speed controller, BEC battery eliminator circuit.

Sources, none in the repository:
    M: the maker's user manual "Brushless ESC for airplane, FlyDragon
    Lite", 251400-1100 V1.0, section 02: FlyDragon Lite 40A, 40A
    continuous, 50A burst, BEC 5V / 3A, LiPo 2-4S, 51g, 65 x 26 x 15.5
    ("for reference"). M names no lead: no colour, gauge or length.
    P: a seller's product picture of the 40A (flyingrobot.co,
    Flydragon-Lite-40A, 749 x 700 pixels, taken at an angle): black
    heat-shrink, white print "FLYCOLOR 40A 2-4S 5V 3A FlyDragon Lite",
    a black capacitor standing on the end with the power leads, a red
    and a black power lead, 3 black motor leads with bare tinned ends,
    a signal lead of a white, a red and a black wire to a black 3 pin
    plug, leaving at the power leads' end.
    O: the owner, 2026-11-22: the bench powers the ESC through an XT60.
    X: the owner's model of the panel-mount XT60 (rcbench_hardware,
    cad/XT60): 2 contacts 7.2 apart, socket cavity 16.0 x 8.6.

Colours of the leads: from P. Signal lead: red is the middle wire and
the plus of the BEC; white as signal and black as minus follow the
common use of such a lead and are not stated by M.

Not in the sources, estimated from P, whose scale changes along the
picture: how M's 15.5 splits (heat-shrink 9.0 thick, the capacitor 8.0
across to 15.5), the corner radius (2.0), the leads' diameters (power
and motor 3.4, signal 1.2), their places across the ESC, and their
lengths: LEADS draws 60 of the power leads, 80 of the motor leads and
100 of the signal lead. The plugs: the XT60 as a housing of 15.7 x 8.1
x 16.0 with its contacts as in X; the signal plug 7.7 x 2.6 x 14.0
with contacts 2.54 apart. Which power contact lies at which side of
the XT60's key is not drawn: the housing has no key.

Not drawn: the maker's sign, the red "Lite" tag, the board and its
parts under the heat-shrink, bullet plugs on the motor leads (P shows
bare ends; the maker sells the series with 3.5mm bullets too). The
print is plain text.

Frame: origin at the middle of the ESC's underside. x along the ESC:
the power leads and the signal lead leave the end at -x, the motor
leads the end at +x. y across, the red power lead at +y; z up. All
sizes are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _parts as p
import _solid as s

STEM = "flycolor_flydragon_lite_40a"
SIZE = (65.0, 26.0, 15.5)       # M, over all
SHRINK, CORNER = 9.0, 2.0       # the heat-shrink's thickness
CAP = (-27.0, 6.0, 8.0)         # the capacitor: x, y, diameter
CAP_TOP = (0.62, 0.62, 0.60)
SHRINK_BLACK = (0.09, 0.09, 0.10)
PRINT = (("FlyDragon Lite 40A", 3.0, 2.6), ("2-4S  5V 3A", 2.2, -2.8))
# Leads: the height of their axes; power (diameter, distance, length),
# motor (diameter, distance, length, the tinned end's diameter and
# length), signal (diameter, distance, length, y of its middle wire).
LEAD_Z = 4.5
POWER = (3.4, 7.2, 60.0)
MOTOR = (3.4, 4.0, 80.0, 2.0, 4.0)
SIGNAL = (1.2, 1.3, 100.0, -10.0)
# XT60: width, height, length, contact diameter, its sleeve's bore.
XT60 = (15.7, 8.1, 16.0, 3.5, 5.5)
PLUG = (7.7, 2.6, 14.0, 2.54, 0.7)
# The signal lead's wires from -y to +y (P; see above).
WIRES = (("minus", p.BLACK), ("plus", p.RED), ("signal", p.WHITE))
PHASES = ("A", "B", "C")        # the motor leads from -y to +y

X0, X1 = -SIZE[0] / 2, SIZE[0] / 2


def _rod(x0, x1, y, diameter, rgb):
    """A straight round rod along x at the leads' height."""
    return s.pipe([(x0, y), (x1, y)], diameter / 2, rgb,
                  frame=((0.0, 0.0, LEAD_Z), (1.0, 0.0, 0.0),
                         (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)))


def body():
    """The heat-shrink, the capacitor and the print."""
    outline = s.rounded(X0, -SIZE[1] / 2, X1, SIZE[1] / 2, CORNER)
    x, y, across = CAP
    out = [s.ext(outline, 0.0, SHRINK, SHRINK_BLACK),
           s.ext(s.circle(x, y, across / 2), SHRINK, SIZE[2] - 0.3,
                 p.BLACK),
           s.ext(s.circle(x, y, across / 2 - 0.6), SIZE[2] - 0.3, SIZE[2],
                 CAP_TOP)]
    for string, height, at in PRINT:
        out += s.place(p.text(string, height, p.WHITE), 4.0, at, SHRINK)
    return out


def power_y(k):
    """The y of power lead k: 0 minus (black), 1 plus (red)."""
    return (k - 0.5) * POWER[1]


def xt60_end():
    """The middle of the XT60's mating face, (x, y, z)."""
    return (X0 - POWER[2] - XT60[2], 0.0, LEAD_Z)


def power():
    """The 2 power leads and the XT60 plug."""
    across, _apart, length = POWER
    wide, high, long, pin, bore = XT60
    x = X0 - length
    out = [_rod(x, X0, power_y(k), across, rgb)
           for k, rgb in enumerate((p.BLACK, p.RED))]
    out.append(s.ext(
        s.rect(-wide / 2, LEAD_Z - high / 2, wide / 2, LEAD_Z + high / 2),
        x - long, x, p.YELLOW,
        holes=[s.circle(power_y(k), LEAD_Z, bore / 2) for k in (0, 1)],
        frame=s.X))
    out += [_rod(x - long + 2.0, x, power_y(k), pin, p.GOLD)
            for k in (0, 1)]
    return out


def motor_y(k):
    """The y of motor lead k of PHASES."""
    return (k - 1) * MOTOR[1]


def motor_lead(k):
    """Motor lead k: where it leaves the ESC, its middle, its end.

    Returns:
        3 points (x, y, z); the end is the tinned tip's end.
    """
    _across, _apart, length, _tip, tip_long = MOTOR
    y = motor_y(k)
    return ((X1, y, LEAD_Z), (X1 + length / 2, y, LEAD_Z),
            (X1 + length + tip_long, y, LEAD_Z))


def motor():
    """The 3 motor leads with their tinned ends."""
    across, _apart, length, tip, tip_long = MOTOR
    out = []
    for k in range(3):
        y = motor_y(k)
        out.append(_rod(X1, X1 + length, y, across, p.BLACK))
        out.append(_rod(X1 + length, X1 + length + tip_long, y, tip,
                        p.TIN))
    return out


def signal_y(k):
    """The y of wire k of WIRES along the signal lead."""
    return SIGNAL[3] + (k - 1) * SIGNAL[1]


def signal_pin(k):
    """The middle of contact k of WIRES in the plug's end face."""
    return (X0 - SIGNAL[2] - PLUG[2], SIGNAL[3] + (k - 1) * PLUG[3],
            LEAD_Z)


def signal():
    """The signal lead's 3 wires and its plug."""
    across, _apart, length, middle = SIGNAL
    wide, thick, long, _pitch, hole = PLUG
    x = X0 - length
    out = [_rod(x, X0, signal_y(k), across, rgb)
           for k, (_name, rgb) in enumerate(WIRES)]
    out.append(s.ext(
        s.rect(middle - wide / 2, LEAD_Z - thick / 2, middle + wide / 2,
               LEAD_Z + thick / 2), x - long, x, p.BLACK,
        holes=[s.rect(signal_pin(k)[1] - hole / 2, LEAD_Z - hole / 2,
                      signal_pin(k)[1] + hole / 2, LEAD_Z + hole / 2)
               for k in range(3)], frame=s.X))
    return out


def esc():
    """The ESC as shapes: body, power leads, motor leads, signal lead."""
    return body() + power() + motor() + signal()


def parts(_colors):
    """The model of the ESC.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items(esc())}
