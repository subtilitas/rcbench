"""hardware/mechanical: the Hitec D485HW is as large as its drawing.

Every solid closes, the model has the maker's dimensions, the mounting
holes lie where the drawing puts them and stay open to the ears' ends,
the horn is the HD-X25 of the maker's data sheet, and the lead leaves
the case as 3 wires in the maker's colours and ends in a 3 pin plug.

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
import hitec_d485hw as servo  # noqa: E402


def extent(shapes):
    """The box round shapes."""
    points = [q for _k, pts, _e, _f, _rgb in _solid.items(shapes)
              for q in pts]
    return [min(q[k] for q in points) for k in range(3)] + [
        max(q[k] for q in points) for k in range(3)]


def test_solids_close():
    """Each edge of each of the 37 solids is used once in each direction."""
    (items,) = servo.parts(None).values()
    assert len(items) == 37
    for _kind, _points, edges, faces, _rgb in items:
        used = Counter((edge, forward) for _s, _same, loops in faces
                       for loop in loops for edge, forward in loop)
        assert all(used[(e, d)] == 1 for e in range(len(edges))
                   for d in (True, False))


def test_case_has_the_drawings_size():
    """39.8 x 19.8, 53.0 over the ears, shaft 10.0 from the short end."""
    shells = servo.case()[:3]
    box = extent(shells)
    assert box[3] - box[0] == pytest.approx(39.8)
    assert box[4] - box[1] == pytest.approx(19.8)
    assert (box[2], box[5]) == pytest.approx((0.0, 37.65))
    assert box[3] == pytest.approx(9.9)
    ears = extent(servo.case()[3:5])
    assert ears[3] - ears[0] == pytest.approx(53.0)
    assert (ears[2], ears[5]) == pytest.approx((28.05, 30.55))
    assert [shape[4] for shape in shells] == [_parts.BLACK] * 3


def test_mounting_holes_lie_where_the_drawing_puts_them():
    """4.5 across, 48.0 x 10.0 apart, one row 14.0 from the shaft."""
    middle = (servo.X0 + servo.X1) / 2
    for side in (1, -1):
        outline = servo._ear(side)
        centres = {q[2][:2] for q in outline if len(q) == 3}
        assert centres == {(middle + side * 24.0, 5.0),
                           (middle + side * 24.0, -5.0)}
        # A hole's edge is 2.25 from its centre, and the slot is open.
        end = servo.X1 + servo.EAR[0] if side > 0 else (
            servo.X0 - servo.EAR[0])
        assert sum(1 for q in outline if q[0] == pytest.approx(end)) == 6
    assert middle + 24.0 == pytest.approx(14.0)
    assert servo.HOLES[2] == 4.5


def test_shaft_and_boss():
    """A shaft of 6.0 to 43.05 through a boss that ends at 39.85."""
    box = extent(servo.shaft())
    assert box[3] - box[0] == pytest.approx(6.0, abs=0.01)
    assert box[5] == pytest.approx(43.05)
    assert servo.BOSS[-1][1] == 39.85


def test_horn_is_the_hd_x25():
    """4 arms, 16 holes of 1.6 at the data sheet's distances, hub of 9.0."""
    holes = servo.horn_holes()
    assert len(holes) == len(set(holes)) == 16
    assert sorted({abs(x) for x, y in holes if y == 0}) == [7, 10, 13, 16]
    assert sorted({abs(y) for x, y in holes if x == 0}) == [6, 9, 12, 15]
    box = extent(servo.horn())
    assert (box[0], box[3]) == pytest.approx((-19.0, 19.0), abs=0.01)
    assert (box[1], box[4]) == pytest.approx((-18.0, 18.0), abs=0.01)
    assert box[5] - box[2] == pytest.approx(6.2)
    assert box[2] == pytest.approx(servo.BOSS[-1][1] + 0.3)
    hub = extent(servo.horn()[:1])
    assert hub[3] - hub[0] == pytest.approx(9.0, abs=0.01)


def test_lead_has_the_makers_colours():
    """Minus black, plus red in the middle, signal yellow; 60 drawn."""
    assert [name for name, _rgb in servo.WIRES] == ["minus", "plus",
                                                    "signal"]
    assert [rgb for _name, rgb in servo.WIRES] == [
        _parts.BLACK, _parts.RED, _parts.YELLOW]
    wires = [shape for shape in servo.lead() if shape[1] == _solid.PIPE]
    assert [shape[4] for shape in wires] == [rgb for _n, rgb in servo.WIRES]
    for k, shape in enumerate(wires):
        box = extent([shape])
        assert box[3] - box[0] == pytest.approx(60.0)
        assert (box[1] + box[4]) / 2 == pytest.approx(servo.wire_y(k))
        assert box[4] - box[1] == pytest.approx(1.3, abs=0.01)
        assert (box[2] + box[5]) / 2 == pytest.approx(6.25, abs=0.01)
    assert servo.wire_y(1) == 0.0
    assert servo.wire_y(2) - servo.wire_y(1) == pytest.approx(1.8)


def test_lead_leaves_the_end_and_ends_in_the_plug():
    """Bushing 6.0 long at the end at +x, then wires, then the plug."""
    assert servo.lead_exit() == pytest.approx((9.9, 0.0, 6.25))
    box = extent(servo.lead())
    assert box[0] == pytest.approx(servo.X1)
    assert box[3] == pytest.approx(9.9 + 6.0 + 60.0 + 14.0)
    plug = extent(servo.lead()[-1:])
    assert plug[3] - plug[0] == pytest.approx(14.0)
    assert plug[4] - plug[1] == pytest.approx(7.7)
    assert plug[5] - plug[2] == pytest.approx(2.6)
    assert servo.plug_pin(2) == pytest.approx((89.9, 2.54, 6.25))
    assert servo.plug_pin(1) == pytest.approx((89.9, 0.0, 6.25))
    assert servo.plug_pin(0) == pytest.approx((89.9, -2.54, 6.25))


def test_whole_model_has_its_size():
    """126.4 x 36.0 x 46.35 over all."""
    box = extent(servo.servo())
    assert [round(box[k + 3] - box[k], 2) for k in range(3)] == [
        126.4, 36.0, 46.35]
    assert box[:3] == pytest.approx([-36.5, -18.0, 0.0], abs=0.01)


def test_type_is_printed_on_plate_and_sides():
    """"D485HW": black on the silver plate, white on both long sides."""
    shapes = servo.case()
    assert sum(1 for shape in shapes if shape[4] == servo.SILVER) == 1
    assert servo.LABEL[0] == "D485HW"
    white = [shape for shape in shapes if shape[4] == _parts.WHITE]
    assert white
    sides = {round(extent([shape])[1], 1) < 0 for shape in white}
    assert sides == {True, False}
