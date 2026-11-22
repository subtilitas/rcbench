"""hardware/mechanical: the motor simulator, 3 resistors in a triangle.

Every solid closes, the 3 resistors of 600 ohm join corner to corner,
any 2 clips see 400 ohm, each corner has its solder, its yellow wire
and its black clip, the 3 wires leave side by side, and the phase tap's
lead stands on one clip and ends in a 3.5mm mono plug.

SPDX-License-Identifier: MIT
"""

import sys
from collections import Counter
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "hardware" / "mechanical"))
import _parts  # noqa: E402
import _solid  # noqa: E402
import motor_simulator as sim  # noqa: E402


def extent(shapes):
    """The box round shapes."""
    points = [q for _k, pts, _e, _f, _rgb in _solid.items(shapes)
              for q in pts]
    return [min(q[k] for q in points) for k in range(3)] + [
        max(q[k] for q in points) for k in range(3)]


def test_solids_close():
    """Each edge of each of the 32 solids is used once in each direction."""
    (items,) = sim.parts(None).values()
    assert len(items) == 32
    for _kind, _points, edges, faces, _rgb in items:
        used = Counter((edge, forward) for _s, _same, loops in faces
                       for loop in loops for edge, forward in loop)
        assert all(used[(e, d)] == 1 for e in range(len(edges))
                   for d in (True, False))


def test_resistance_between_two_clips():
    """A delta of 3 x 600 ohm: 600 in parallel with 1200 is 400."""
    assert sim.OHM == 600
    assert sim.between_clips() == pytest.approx(400.0)
    assert sim.between_clips() == pytest.approx(
        1 / (1 / sim.OHM + 1 / (2 * sim.OHM)))


def test_triangle_is_regular():
    """3 corners 13.0 apart, one resistor on each side."""
    points = list(sim.CORNERS.values())
    for i in range(3):
        a, b = points[i], points[(i + 1) % 3]
        assert ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5 == (
            pytest.approx(sim.SIDE))
    assert len(sim.SIDES) == 3
    assert {name for side in sim.SIDES for name in side} == set(sim.CORNERS)
    shapes = sim.resistors()
    assert len(shapes) == 9
    bodies = [shape for shape in shapes if shape[4] == sim.RESISTOR_BLUE]
    assert len(bodies) == 3
    assert all(shape[2] == pytest.approx(sim.BODY[1] / 2)
               for shape in bodies)
    box = extent(shapes)
    assert box[3] - box[0] == pytest.approx(sim.BODY[1], abs=0.01)


def test_leads_end_at_the_solder():
    """A lead stops at the joint's surface, 0.8 from the corner."""
    for a, b in sim.SIDES:
        start, end = sim._along(sim.CORNERS[a], sim.CORNERS[b],
                                sim.SOLDER / 2, sim.SIDE - sim.SOLDER / 2)
        for point, corner in ((start, sim.CORNERS[a]),
                              (end, sim.CORNERS[b])):
            assert ((point[0] - corner[0]) ** 2
                    + (point[1] - corner[1]) ** 2) ** 0.5 == (
                        pytest.approx(sim.SOLDER / 2))
    assert len(sim.joints()) == 3
    assert all(shape[4] == _parts.TIN for shape in sim.joints())


def test_wires_and_clips():
    """A yellow wire of 20 and a black clip on each corner, along -x."""
    for name, (y, z) in sim.CORNERS.items():
        shapes = sim.wire_and_clip(name)
        assert [shape[4] for shape in shapes] == [
            sim.WIRE_YELLOW, _parts.BLACK, _parts.BLACK, _parts.STEEL,
            _parts.STEEL]
        start, middle, end = sim.lead(name)
        assert start == pytest.approx((-0.8, y, z))
        assert middle == pytest.approx((-10.8, y, z))
        assert end == pytest.approx((-20.8, y, z))
        wire = extent(shapes[:1])
        assert (wire[0], wire[3]) == pytest.approx((-20.8, -0.8))
        assert sim.clip_end(name) == pytest.approx((-49.8, y, z))
        assert extent(shapes)[0] == pytest.approx(-49.8)
        boot = extent(shapes[2:3])
        assert boot[4] - boot[1] == pytest.approx(7.0, abs=0.01)


def test_wires_leave_side_by_side():
    """The clips' boots, 7.0 across, lie 13.0 apart and do not touch."""
    points = list(sim.CORNERS.values())
    for i in range(3):
        a, b = points[i], points[(i + 1) % 3]
        apart = ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5
        assert apart - sim.CLIP[1][0] > 5.0


def test_whole_model_has_its_size():
    """51.05 x 20.0 x 78.06 over all; 18.26 high without the tap's lead."""
    box = extent(sim.simulator())
    assert [round(box[k + 3] - box[k], 2) for k in range(3)] == [
        51.05, 20.0, 78.06]
    own = extent(sim.resistors() + sim.joints() + [
        shape for name in sim.CORNERS for shape in sim.wire_and_clip(name)])
    assert round(own[5] - own[2], 2) == 18.26


def test_phase_tap_lead_is_on_one_clip():
    """A white wire of 30 standing on the upper jaw of the clip on C."""
    assert sim.TAP == "C"
    start, middle, end = sim.tap_lead()
    tip = sim.clip_end("C")
    assert start[0] == pytest.approx(tip[0] + 4.5)
    assert start[1] == pytest.approx(tip[1])
    # It starts on the jaw's upper face: no gap, no shared volume.
    jaw = extent(sim.wire_and_clip("C")[3:4])
    assert start[2] == pytest.approx(jaw[5])
    assert jaw[0] < start[0] < jaw[3]
    assert end[2] - start[2] == pytest.approx(30.0)
    assert middle[2] == pytest.approx((start[2] + end[2]) / 2)
    wire = sim.tap()[0]
    assert wire[4] == _parts.WHITE
    box = extent([wire])
    assert box[3] - box[0] == pytest.approx(1.2, abs=0.01)
    assert (box[2], box[5]) == pytest.approx((start[2], end[2]))


def test_tap_lead_ends_in_a_mono_plug():
    """Handle, sleeve, ring and tip in a row: a shaft of 3.5 x 14.0."""
    shapes = sim.tap()[1:]
    assert [shape[4] for shape in shapes] == [
        _parts.BLACK, _parts.STEEL, _parts.BLACK, _parts.STEEL]
    end = sim.tap_lead()[2]
    handle = extent(shapes[:1])
    assert handle[2] == pytest.approx(end[2])
    assert handle[3] - handle[0] == pytest.approx(6.0, abs=0.01)
    assert handle[5] - handle[2] == pytest.approx(18.0)
    shaft = extent(shapes[1:])
    assert shaft[3] - shaft[0] == pytest.approx(3.5, abs=0.01)
    assert shaft[5] - shaft[2] == pytest.approx(14.0)
    assert shaft[2] == pytest.approx(handle[5])
    # 2 contacts, tip and sleeve, with one ring between them: mono.
    assert sum(1 for shape in shapes[1:] if shape[4] == _parts.STEEL) == 2
    tip = sim.plug_tip()
    assert tip[:2] == pytest.approx(end[:2])
    assert tip[2] == pytest.approx(shaft[5])
    assert tip[2] - end[2] == pytest.approx(32.0)
