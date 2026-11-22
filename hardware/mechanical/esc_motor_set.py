"""The FlyDragon Lite 40A and the AT2814, joined lead to lead.

    esc_motor_set   the ESC lying, the motor standing on its base
                    beside it, each motor lead of the ESC ending where
                    a lead of the motor ends

A set for pictures of the phase tap (protocols/phase_tap/README.md):
the bench counts the ESC's beeps from one of these 3 leads. No part is
changed; each is the model of its own spec, moved.

    flycolor_flydragon_lite_40a.py   the ESC, in its own frame
    tmotor_at2814.py                 the motor, moved by MOTOR_AT

The leads meet end to end; the joint itself, solder or plugs, is not
drawn. TAP names the lead a picture tags as the tapped one; which lead
the bench taps is free: the 3 carry the same beeps.

Frame: the ESC's. All sizes are mm.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import _solid as s
import flycolor_flydragon_lite_40a as esc
import tmotor_at2814 as motor

STEM = "esc_motor_set"
TAP = 2                 # the lead at +y
# The motor's origin: its leads' ends on the ESC's motor leads' ends.
MOTOR_AT = tuple(esc.motor_lead(1)[2][k] - motor.lead(1)[2][k]
                 for k in range(3))


def joint(k):
    """Where the ESC's motor lead k meets the motor's lead k."""
    return esc.motor_lead(k)[2]


def tap():
    """The point a picture tags as the phase tap: a lead's joint."""
    return joint(TAP)


def placed():
    """The motor's shapes in the ESC's frame."""
    return s.place(motor.motor(), *MOTOR_AT)


def parts(_colors):
    """The set as one model.

    Returns:
        A dict of file stem to solids.
    """
    return {STEM: s.items(esc.esc() + placed())}
