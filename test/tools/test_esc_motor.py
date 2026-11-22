"""hardware/mechanical: the FlyDragon Lite 40A, the AT2814 and their set.

Every solid closes. The ESC has the maker's size, its leads their
colours and places, its plugs their contacts. The motor has the sizes
of the maker's drawing. In the set each lead of the ESC ends where a
lead of the motor ends, and no part is changed.

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
import esc_motor_set as both  # noqa: E402
import flycolor_flydragon_lite_40a as esc  # noqa: E402
import tmotor_at2814 as motor  # noqa: E402


def extent(shapes):
    """The box round shapes."""
    points = [q for _k, pts, _e, _f, _rgb in _solid.items(shapes)
              for q in pts]
    return [min(q[k] for q in points) for k in range(3)] + [
        max(q[k] for q in points) for k in range(3)]


@pytest.mark.parametrize("spec,count", [(esc, 44), (motor, 15), (both, 59)])
def test_solids_close(spec, count):
    """Each edge of each solid is used once in each direction."""
    (items,) = spec.parts(None).values()
    assert len(items) == count
    for _kind, _points, edges, faces, _rgb in items:
        used = Counter((edge, forward) for _s, _same, loops in faces
                       for loop in loops for edge, forward in loop)
        assert all(used[(e, d)] == 1 for e in range(len(edges))
                   for d in (True, False))


def test_esc_has_the_makers_size():
    """65 x 26 x 15.5 over heat-shrink and capacitor."""
    box = extent(esc.body())
    assert [round(box[k + 3] - box[k], 2) for k in range(3)] == [
        65.0, 26.0, 15.5]
    assert box[2] == pytest.approx(0.0)
    shrink = extent(esc.body()[:1])
    assert shrink[5] == pytest.approx(esc.SHRINK)


def test_esc_power_leads_end_in_an_xt60():
    """Black and red, 7.2 apart, 60 long, then the keyed plug of 16.0."""
    shapes = esc.power()
    assert [shape[4] for shape in shapes[:2]] == [_parts.BLACK, _parts.RED]
    red = extent(shapes[1:2])
    assert (red[1] + red[4]) / 2 == pytest.approx(3.6)
    assert red[3] - red[0] == pytest.approx(60.0)
    assert red[3] == pytest.approx(-32.5)
    shroud, rear = shapes[2], shapes[3]
    assert shroud[4] == rear[4] == _parts.YELLOW
    plug = extent([shroud, rear])
    assert [round(plug[k + 3] - plug[k], 2) for k in range(3)] == [
        16.0, 15.7, 8.1]
    assert esc.xt60_end() == pytest.approx((-108.5, 0.0, 4.5))
    assert plug[0] == pytest.approx(esc.xt60_end()[0])
    front = extent([shroud])
    assert front[3] - front[0] == pytest.approx(7.5)


def test_xt60_is_keyed_and_has_its_pins():
    """2 corners cut at the minus contact, an open shroud, 2 pins in it."""
    outline = esc._keyed(0.0)
    assert len(outline) == 6
    low = min(y for y, _z in outline)
    cut = [q for q in outline if q[0] == pytest.approx(low)]
    assert len(cut) == 2
    assert cut[0][1] - cut[1][1] == pytest.approx(8.1 - 2 * 2.5)
    # The cut end is the minus contact's: the black lead's side.
    assert esc.power_y(0) < 0 > low
    assert max(y for y, _z in outline) == pytest.approx(7.85)
    # The shroud is hollow: its hole is the outline, 1.2 inside.
    shroud = esc.power()[2]
    (hole,) = shroud[1]
    assert max(y for y, _z in hole) == pytest.approx(7.85 - 1.2)
    pins = esc.power()[4:]
    assert [shape[4] for shape in pins] == [_parts.GOLD] * 2
    for k, pin in enumerate(pins):
        box = extent([pin])
        assert (box[1] + box[4]) / 2 == pytest.approx(esc.power_y(k))
        assert box[4] - box[1] == pytest.approx(3.5, abs=0.01)
        # A pin ends 0.5 inside the shroud's mouth.
        assert box[0] == pytest.approx(esc.xt60_end()[0] + 0.5)
    assert esc.power_y(1) - esc.power_y(0) == pytest.approx(7.2)


def test_esc_motor_leads():
    """3 black leads 4.0 apart, 80 long, with tinned ends of 4."""
    shapes = esc.motor()
    assert [shape[4] for shape in shapes] == [_parts.BLACK, _parts.TIN] * 3
    for k in range(3):
        start, middle, end = esc.motor_lead(k)
        assert start == pytest.approx((32.5, (k - 1) * 4.0, 4.5))
        assert middle == pytest.approx((72.5, (k - 1) * 4.0, 4.5))
        assert end == pytest.approx((116.5, (k - 1) * 4.0, 4.5))
        tip = extent(shapes[2 * k + 1:2 * k + 2])
        assert tip[3] == pytest.approx(end[0])
        assert (tip[1] + tip[4]) / 2 == pytest.approx(end[1])
    assert esc.PHASES == ("A", "B", "C")


def test_esc_signal_lead():
    """Black, red in the middle, white; 100 long; a 3 pin plug."""
    assert [name for name, _rgb in esc.WIRES] == ["minus", "plus", "signal"]
    shapes = esc.signal()
    assert [shape[4] for shape in shapes[:3]] == [
        _parts.BLACK, _parts.RED, _parts.WHITE]
    for k in range(3):
        box = extent(shapes[k:k + 1])
        assert box[3] - box[0] == pytest.approx(100.0)
        assert (box[1] + box[4]) / 2 == pytest.approx(esc.signal_y(k))
    assert esc.signal_pin(2) == pytest.approx((-146.5, -7.46, 4.5))
    assert esc.signal_pin(1) == pytest.approx((-146.5, -10.0, 4.5))
    plug = extent(shapes[3:])
    assert plug[0] == pytest.approx(-146.5)
    assert plug[3] - plug[0] == pytest.approx(14.0)


def test_esc_has_its_print():
    """The type and the rating as plain white text on the heat-shrink."""
    assert [line[0] for line in esc.PRINT] == ["FlyDragon Lite 40A",
                                               "2-4S  5V 3A"]
    white = [shape for shape in esc.body() if shape[4] == _parts.WHITE]
    assert white
    assert all(extent([shape])[2] == pytest.approx(esc.SHRINK)
               for shape in white)


def test_motor_has_the_drawings_size():
    """35.2 across, body 36, 54.5 over the shaft, 2 of it behind."""
    body = extent(motor.base() + motor.windings() + motor.bell()[:2])
    assert body[3] - body[0] == pytest.approx(35.2, abs=0.01)
    assert (body[2], body[5]) == pytest.approx((0.0, 36.0), abs=0.03)
    shaft = extent(motor.shaft())
    assert shaft[3] - shaft[0] == pytest.approx(5.0, abs=0.01)
    assert (shaft[2], shaft[5]) == pytest.approx((-2.0, 52.5))
    assert shaft[5] - shaft[2] == pytest.approx(54.5)
    collar = extent(motor.bell()[2:])
    assert collar[5] - collar[2] == pytest.approx(1.75)
    assert motor.PLATE + (motor.BASE - motor.PLATE) == motor.BASE
    assert motor.BASE + motor.CAN + motor.CAP == pytest.approx(36.0)


def test_motor_threads_lie_where_the_drawing_puts_them():
    """Base: M3 at 19 x 25. Front cap: 4 x M3 on a circle of 15."""
    plate = motor.base()[0]
    centres = sorted((round(sum(q[0] for q in hole) / len(hole), 2),
                      round(sum(q[1] for q in hole) / len(hole), 2))
                     for hole in plate[1][1:])
    assert centres == [(-9.5, 0.0), (0.0, -12.5), (0.0, 12.5), (9.5, 0.0)]
    cap = motor.bell()[1]
    for hole in cap[1][1:]:
        x = sum(q[0] for q in hole) / len(hole)
        y = sum(q[1] for q in hole) / len(hole)
        assert (x * x + y * y) ** 0.5 == pytest.approx(7.5, abs=0.01)
    assert len(cap[1]) == 5


def test_motor_colours_carry_the_picture():
    """Bright base and cap, black can, copper windings and leads."""
    assert motor.base()[0][4] == motor.METAL
    assert motor.windings()[0][4] == _parts.COPPER
    can, cap, _collar = motor.bell()
    assert can[4] == motor.CAN_BLACK
    assert cap[4] == motor.METAL
    assert [shape[4] for shape in motor.leads()] == [_parts.COPPER] * 3
    ring = extent(motor.windings())
    assert (ring[2], ring[5]) == pytest.approx((3.0, 9.5))


def test_motor_leads():
    """3 leads of 100 towards -x, 4.0 apart, 6.25 over the face."""
    for k in range(3):
        start, middle, end = motor.lead(k)
        assert start == pytest.approx((-17.6, (k - 1) * 4.0, 6.25))
        assert middle == pytest.approx((-67.6, (k - 1) * 4.0, 6.25))
        assert end == pytest.approx((-117.6, (k - 1) * 4.0, 6.25))
        box = extent(motor.leads()[k:k + 1])
        assert box[3] - box[0] == pytest.approx(100.0)


def test_set_joins_the_leads_end_to_end():
    """Each ESC lead ends where the motor's lead of the same place ends."""
    assert both.MOTOR_AT == pytest.approx((234.1, 0.0, -1.75))
    for k in range(3):
        end = [motor.lead(k)[2][i] + both.MOTOR_AT[i] for i in range(3)]
        assert end == pytest.approx(esc.motor_lead(k)[2])
        assert both.joint(k) == pytest.approx(esc.motor_lead(k)[2])
    assert both.tap() == pytest.approx((116.5, 4.0, 4.5))
    box = extent(esc.esc() + both.placed())
    assert box == pytest.approx([-146.5, -17.6, -3.75, 251.7, 17.6, 50.75],
                                abs=0.01)


def test_set_changes_no_part():
    """The set holds the ESC's and the motor's solids, the motor moved."""
    (items,) = both.parts(None).values()
    assert len(items) == len(_solid.items(esc.esc())) + len(
        _solid.items(motor.motor()))
    moved, own = extent(both.placed()), extent(motor.motor())
    assert [moved[k] - own[k] for k in range(3)] == pytest.approx(
        both.MOTOR_AT)
    assert [moved[k + 3] - moved[k] for k in range(3)] == pytest.approx(
        [own[k + 3] - own[k] for k in range(3)])
