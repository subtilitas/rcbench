"""hardware/mechanical: 4 module boards drawn part by part.

The RP2350-CAN, the INA3221 board, the Mini560 buck converter and the
USB-C PD trigger board: every solid closes, each board is as large as
its footprint, its holes are the footprint's, the parts named in its
spec are there, and the mounted model stands as the board is mounted.

SPDX-License-Identifier: MIT
"""

import sys
from collections import Counter
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "hardware" / "mechanical"))
sys.path.insert(0, str(ROOT / "hardware" / "kicad" / "tools"))
import _parts  # noqa: E402
import _solid  # noqa: E402
import dcdc_buck_mini  # noqa: E402
import gen_all  # noqa: E402
import ina3221_module  # noqa: E402
import rp2350_can  # noqa: E402
import usbc_pd_trigger  # noqa: E402

SPECS = [(rp2350_can, 632, 674), (ina3221_module, 299, 308),
         (dcdc_buck_mini, 137, 137), (usbc_pd_trigger, 158, 158)]


def extent(shapes):
    """The box round shapes."""
    points = [q for _k, pts, _e, _f, _rgb in _solid.items(shapes)
              for q in pts]
    return [min(q[k] for q in points) for k in range(3)] + [
        max(q[k] for q in points) for k in range(3)]


def of_colour(shapes, rgb):
    """The shapes of one colour."""
    return [shape for shape in shapes if shape[4] == rgb]


@pytest.mark.parametrize("spec,bare,mounted", SPECS)
def test_solids_close(spec, bare, mounted):
    """Each edge of each solid is used once in each direction."""
    (items,) = spec.parts(None).values()
    assert len(items) == bare
    assert len(spec.kicad_model()) == mounted
    for _kind, _points, edges, faces, _rgb in items:
        used = Counter((edge, forward) for _s, _same, loops in faces
                       for loop in loops for edge, forward in loop)
        assert all(used[(e, d)] == 1 for e in range(len(edges))
                   for d in (True, False))


@pytest.mark.parametrize("spec,_bare,_mounted", SPECS)
def test_board_is_as_large_as_its_footprint(spec, _bare, _mounted):
    """The plate has the footprint's size and the origin at pad 1."""
    wide, high = spec.kicad.FOOTPRINT["board"]
    ox, oy = spec.kicad.FOOTPRINT["origin"]
    box = extent(spec.board()[:1])
    assert box == pytest.approx([-ox, oy - high, 0.0, wide - ox, oy, 1.6])


@pytest.mark.parametrize("spec,_bare,_mounted", SPECS)
def test_every_drilled_pad_is_a_hole(spec, _bare, _mounted):
    """The plate has a hole at each drilled pad of the footprint."""
    fp = spec.kicad.FOOTPRINT
    ox, oy = fp["origin"]
    (_outer, holes, *_rest) = spec.board()[0]
    centres = [(sum(q[0] for q in hole) / len(hole),
                sum(q[1] for q in hole) / len(hole)) for hole in holes]
    for pad in fp["pads"]:
        if pad.get("drill"):
            want = (pad["x"] - ox, oy - pad["y"])
            assert min(abs(cx - want[0]) + abs(cy - want[1])
                       for cx, cy in centres) < 0.01


@pytest.mark.parametrize("spec,_bare,_mounted", SPECS)
def test_library_model_is_the_mounted_board(spec, _bare, _mounted):
    """The KiCad library takes the spec's mounted model."""
    assert spec.kicad.MODEL == {"detail": spec.STEM}
    assert gen_all.detail(spec.kicad.MODEL) == spec.kicad_model()


def test_boards_have_their_colours():
    """Blue, black, blue and purple (owner, 2026-11-21, and photos)."""
    assert rp2350_can.board()[0][4] == rp2350_can.BLUE
    assert ina3221_module.board()[0][4] == ina3221_module.BLACK
    assert dcdc_buck_mini.board()[0][4] == dcdc_buck_mini.BLUE
    purple = usbc_pd_trigger.board()[0][4]
    assert purple == usbc_pd_trigger.PURPLE
    assert purple[0] > purple[1] and purple[2] > purple[0]


def test_header_boards_stand_on_their_headers():
    """Underside 2.5 over the carrier, pins 6.0 under it."""
    for spec, pins in ((rp2350_can, 40), (ina3221_module, 8)):
        shapes = spec.mounted()
        assert extent(shapes)[2] == pytest.approx(-6.0)
        plate = spec.mounted()[0]
        assert extent([plate])[2] == pytest.approx(2.5, abs=0.06)
        gold = [s for s in of_colour(shapes, _parts.GOLD)
                if extent([s])[2] == pytest.approx(-6.0)]
        assert len(gold) == pins


def test_flat_boards_lie_on_the_carrier():
    """The buck converter and the PD trigger board lie flat."""
    for spec in (dcdc_buck_mini, usbc_pd_trigger):
        box = extent(spec.mounted())
        assert box[2] == pytest.approx(0.02, abs=0.001)
        assert extent(spec.mounted()[:1])[2] == pytest.approx(0.055,
                                                              abs=0.001)


def test_rp2350_can_parts():
    """USB-C 1.1 past the end, terminal 8.8 high, 52 chip parts."""
    box = extent(rp2350_can.bare())
    wide, high = rp2350_can.W, rp2350_can.H
    assert box[4] - 1.37 == pytest.approx(1.1, abs=0.01)
    assert box[1] == pytest.approx(1.37 - high - 0.5, abs=0.01)
    assert box[5] == pytest.approx(1.6 + 8.8)
    assert box[3] - box[0] == pytest.approx(wide)
    assert len(rp2350_can.PADS) == 40
    chips = (len(rp2350_can.C0805) + len(rp2350_can.C0603)
             + len(rp2350_can.R0402))
    assert chips == 52
    green = of_colour(rp2350_can.terminal(), rp2350_can.TERMINAL_GREEN)
    tbox = extent(green)
    assert tbox[3] - tbox[0] == pytest.approx(10.9)
    assert len(rp2350_can.LEFT_NAMES) == len(rp2350_can.RIGHT_NAMES) == 20


def test_ina3221_parts():
    """3 shunts 8.4 apart, 20 holes, 5 LEDs, nothing past the edge."""
    xs = [x for x, _y, _turn in ina3221_module.SHUNTS]
    assert xs[1] - xs[0] == pytest.approx(8.37)
    assert xs[2] - xs[1] == pytest.approx(8.33)
    (_outer, holes, *_rest) = ina3221_module.board()[0]
    assert len(holes) == 20
    assert len(ina3221_module.LED_Y) == 5
    box = extent(ina3221_module.bare())
    assert box[3] - box[0] == pytest.approx(38.0)
    assert box[4] - box[1] == pytest.approx(31.8)
    assert box[5] == pytest.approx(1.6 + 0.9)


def test_buck_parts():
    """Inductor 10.3 x 10.2, 5.4 over all, 8 corner holes 26 apart."""
    x, y, wide, deep, high = dcdc_buck_mini.INDUCTOR
    assert (wide, deep) == (10.3, 10.2)
    box = extent(dcdc_buck_mini.bare())
    assert box[5] == pytest.approx(1.6 + high + _parts.PRINT)
    assert 1.6 + high == pytest.approx(5.4)
    holes = [(cx, cy) for cx, ys in dcdc_buck_mini.CORNERS for cy in ys]
    assert len(holes) == 8
    assert max(h[0] for h in holes) - min(h[0] for h in holes) == 26.0
    assert len(dcdc_buck_mini.INPUT_CAPS) == 2
    assert len(dcdc_buck_mini.OUTPUT_CAPS) == 4


def test_pd_trigger_parts():
    """USB-C 1.1 past the edge, 3 sliders, the table's 5 voltages."""
    box = extent(usbc_pd_trigger.bare())
    assert box[0] == pytest.approx(-28.0 - 1.1, abs=0.01)
    assert len(usbc_pd_trigger.SWITCH_ROWS) == 3
    assert [name for name, _bits in usbc_pd_trigger.VOLTAGES] == [
        "20V", "15V", "12V", "9V", "5V"]
    # S1 S2 S3 as the KiCad spec's description gives them.
    desc = usbc_pd_trigger.kicad.DESC
    for name, bits in usbc_pd_trigger.VOLTAGES:
        assert f"{name} {bits}" in desc
