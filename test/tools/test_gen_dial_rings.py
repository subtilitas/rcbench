# SPDX-License-Identifier: MIT
"""Tests of tools/gen_dial_rings.py.

The checked-in table file is what the generator writes, halves round as
C's lround() rounds them, and every ring's pixels run by angle with a
coverage of 1 to 255.  test_dial_face holds the file to gfx_ring_build()'s
output pixel by pixel.
"""

import math

import gen_dial_rings as g


def test_the_checked_in_file_is_what_the_generator_writes():
    """--check passes on the tree as committed."""
    assert g.main(["--check"]) == 0


def test_halves_round_away_from_zero_as_lround_does():
    """Python's round() rounds halves to even; the tables need lround()."""
    assert g.lround(0.5) == 1
    assert g.lround(1.5) == 2
    assert g.lround(2.5) == 3
    assert g.lround(2.4999999999999996) == 2
    assert g.lround(254.5) == 255


def test_each_ring_runs_by_angle_with_its_coverage():
    """Angles never fall; every pixel lies within 1px of its radius."""
    for i in range(g.RINGS):
        r = g.R0 - g.PITCH * i
        pixels = g.ring(g.C2, g.C2, r)
        assert len(pixels) > 8 * r
        last = -1
        for x, y, c in pixels:
            a = g.angle(g.C2, g.C2, x, y)
            assert a >= last
            last = a
            assert 1 <= c <= 255
            d = math.hypot(x - g.C2 / 2, y - g.C2 / 2)
            assert abs(d - r) < 1.0
