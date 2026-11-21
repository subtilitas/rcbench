"""hardware/mechanical: the module boards and cells drawn part by part.

The print's font fits a line to a width, every solid of the models
closes, each model is as large as its spec's sources give it, the
KiCad library writes the same solids, and each footprint's pads lie
where the model has its holes.

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
import boost_5v_en  # noqa: E402
import core2350b  # noqa: E402
import esp32_c6_zero  # noqa: E402
import gen_all  # noqa: E402
import ina219_cjmcu219  # noqa: E402
import lipo_cells  # noqa: E402
import lipo_charger_boost_5v  # noqa: E402

BOARDS = (ina219_cjmcu219, lipo_charger_boost_5v, boost_5v_en,
          esp32_c6_zero)


def open_edges(item):
    """The edges of a brep item not used once in each direction."""
    _kind, _points, edges, faces, _rgb = item
    used = Counter()
    for _surface, _same, loops in faces:
        for loop in loops:
            for edge, forward in loop:
                used[(edge, forward)] += 1
    return [e for e in range(len(edges))
            if used[(e, True)] != 1 or used[(e, False)] != 1]


def extent(items):
    """The box round the points of brep items."""
    points = [q for _k, pts, _e, _f, _rgb in items for q in pts]
    return [min(q[k] for q in points) for k in range(3)] + [
        max(q[k] for q in points) for k in range(3)]


def model(spec):
    """The solids of a spec's one model."""
    (solids,) = spec.parts(None).values()
    return solids


def test_text_fits_its_width():
    """A line set to a width is that wide and one capital high."""
    box = extent(_solid.items(_parts.text("INA219 DC", 0.95, width=7.7)))
    assert box[3] - box[0] == pytest.approx(7.7, abs=0.15)
    assert box[4] - box[1] == pytest.approx(0.95, abs=0.1)
    assert (box[2], box[5]) == (0.0, _parts.PRINT)


def test_text_is_one_solid_per_stroke_group():
    """A blank draws nothing; i and the colon are two shapes each."""
    assert len(_parts.text("A B", 1.0)) == 2
    assert len(_parts.text("i:", 1.0)) == 4
    with pytest.raises(KeyError):
        _parts.text("ä", 1.0)


def test_hole_pad_is_two_pads_and_a_barrel():
    """The pads lie on both faces, the barrel fills the hole's wall."""
    top, under, barrel = (extent(_solid.items([shape])) for shape in
                          _parts.hole_pad(1.0, 1.8, 1.6))
    assert (top[2], top[5]) == pytest.approx((1.6, 1.6 + _parts.PAD))
    assert (under[2], under[5]) == pytest.approx((-_parts.PAD, 0.0))
    assert (barrel[0], barrel[3], barrel[2], barrel[5]) == pytest.approx(
        (-0.5, 0.5, 0.0, 1.6))


@pytest.mark.parametrize("spec, count", [
    (ina219_cjmcu219, 225), (lipo_charger_boost_5v, 121),
    (boost_5v_en, 69), (esp32_c6_zero, 263)])
def test_board_solids_close(spec, count):
    """Every solid of a board is a closed shell."""
    solids = model(spec)
    assert len(solids) == count
    assert [n for n, item in enumerate(solids) if open_edges(item)] == []


def test_cell_solids_close():
    """Every solid of the two cells is a closed shell."""
    for solids in lipo_cells.parts(None).values():
        assert [n for n, item in enumerate(solids)
                if open_edges(item)] == []


@pytest.mark.parametrize("spec", BOARDS[:3])
def test_board_is_as_large_as_its_footprint(spec):
    """The plate has the footprint's size and is 1.6 thick."""
    fp = spec.kicad.FOOTPRINT
    box = extent(_solid.items(spec.board()[:1]))
    assert (box[3] - box[0], box[4] - box[1]) == pytest.approx(fp["board"])
    assert (box[2], box[5]) == (0.0, 1.6)
    assert (-box[0], box[4]) == pytest.approx(fp["origin"])


@pytest.mark.parametrize("spec", BOARDS)
def test_nothing_but_plugs_and_buttons_passes_the_edge(spec):
    """Pads, print and parts stay on the board, bar the listed parts."""
    fp = spec.kicad.FOOTPRINT
    box = extent(model(spec))
    past = {esp32_c6_zero: (0.0, 0.0, 0.0, 1.21),
            lipo_charger_boost_5v: (0.0, 0.0, 1.5, 0.0)}.get(spec, (0,) * 4)
    ox, oy = fp["origin"]
    w, h = fp["board"]
    got = (-ox - box[0], oy - h - box[1], box[3] - (w - ox), box[4] - oy)
    assert got == pytest.approx(past, abs=0.11)


def test_ina219_extent():
    """24.2 x 21.9; the chip's 1.1 is the highest part."""
    box = extent(model(ina219_cjmcu219))
    assert (box[3] - box[0], box[4] - box[1]) == pytest.approx((24.2, 21.9))
    assert box[5] == pytest.approx(1.6 + 1.1)
    assert box[2] == pytest.approx(-_parts.PAD)


def test_ina219_library_model_stands_on_a_header():
    """6 pins reach 6.0 under the plastic; the board lies 2.5 up."""
    solids = ina219_cjmcu219.kicad_model()
    assert len(solids) == 225 + 7
    box = extent(solids)
    assert box[2] == pytest.approx(-6.0)
    assert box[5] == pytest.approx(2.5 + _parts.PAD + 1.6 + 1.4, abs=0.04)


def test_charger_button_stands_past_the_edge():
    """The plunger ends 1.5 past the board's long edge."""
    box = extent(model(lipo_charger_boost_5v))
    kicad = lipo_charger_boost_5v.kicad
    assert box[3] + kicad.ROW_X0 == pytest.approx(kicad.W + 1.5, abs=0.11)
    assert box[5] == pytest.approx(1.6 + 3.0)


def test_boost_pads_are_named_as_printed():
    """IN+ and OUT+ lie on the left, seen from the top side."""
    kicad = boost_5v_en.kicad
    pins = {num: name for _k, name, num, *_at in kicad.SYMBOL["pins"]}
    place = {n: (x, y) for n, x, y, _size in kicad.PADS}
    assert pins == {"1": "IN+", "2": "IN-", "3": "OUT+", "4": "OUT-",
                    "5": "EN"}
    assert place["1"][0] < place["2"][0] and place["3"][0] < place["4"][0]
    assert place["3"][1] < place["1"][1]


def test_esp32_follows_the_makers_drawing():
    """18.0 wide; the socket's shell ends 1.21 past the 23.5 board."""
    box = extent(model(esp32_c6_zero))
    kicad = esp32_c6_zero.kicad
    assert box[3] - box[0] == pytest.approx(18.0)
    assert box[4] - kicad.FIRST == pytest.approx(1.21, abs=0.01)
    assert box[4] - box[1] == pytest.approx(23.5 + 1.21, abs=0.01)
    assert box[5] == pytest.approx(1.6 + 3.25)
    holes = [pad for pad in kicad.FOOTPRINT["pads"] if pad.get("drill")]
    assert len(holes) == 18 and len(kicad.FOOTPRINT["pads"]) == 25
    xs = sorted({pad["x"] for pad in holes})
    assert xs[1] - xs[0] == pytest.approx(15.24)


def test_esp32_symbol_names_every_pad_once():
    """25 pins, numbered as the footprint's pads."""
    kicad = esp32_c6_zero.kicad
    pins = {num: name for _k, name, num, *_at in kicad.SYMBOL["pins"]}
    assert sorted(pins, key=int) == [p["num"] for p in
                                     kicad.FOOTPRINT["pads"]]
    assert (pins["1"], pins["9"], pins["10"], pins["18"]) == (
        "5V", "GP5", "GP22", "TX")
    assert (pins["19"], pins["23"], pins["25"]) == ("GP6", "GP23", "GP13")


@pytest.mark.parametrize("stem, size", [
    ("lipo_702030_380mah", (20.0, 31.0, 7.0)),
    ("lipo_102540_1000mah", (25.0, 40.5, 10.0))])
def test_cell_sizes(stem, size):
    """Pouch and head have the spec's size; the wires leave to -x."""
    width, length, thick = size
    shapes = lipo_cells.cell(stem)
    lead = len(_parts.cell_lead(lipo_cells.WIRE))
    box = extent(_solid.items(shapes[:-lead]))
    assert (box[0], box[3]) == pytest.approx((-width / 2, width / 2))
    assert (box[1], box[4]) == pytest.approx((0.0, length))
    assert (box[2], box[5]) == pytest.approx((0.0, thick + _parts.PRINT))
    whole = extent(_solid.items(shapes))
    assert whole[0] == pytest.approx(-width / 2 - lipo_cells.WIRE - 4.4)


@pytest.mark.parametrize("spec", BOARDS[1:])
def test_library_writes_the_same_solids(spec):
    """A flat board's library model is its mechanical model."""
    assert gen_all.detail(spec.kicad.MODEL) == model(spec)


def test_core2350b_solids_close():
    """The Core2350B and its adapter board: every solid closes."""
    models = core2350b.parts(None)
    assert {k: len(v) for k, v in models.items()} == {
        "core2350b": 445, "core2350b_usb_adapter": 70}
    for solids in models.values():
        assert [n for n, item in enumerate(solids)
                if open_edges(item)] == []


def test_core2350b_underside_chips():
    """Flash SOIC-8 208mil, PSRAM SOP-8 150mil, both on 4 pads a side."""
    kicad = core2350b.kicad
    assert kicad.FLASH[1] == (5.3, 5.3)
    assert kicad.PSRAM[1] == (3.9, 4.9)
    for chip, span in ((core2350b.soic8_wide(), 7.9),
                       (core2350b.p.soic8(), 6.0)):
        box = extent(core2350b.s.items(chip))
        assert box[3] - box[0] == pytest.approx(span)
    # The pads reach past both packages' lead tips.
    reach = core2350b.CHIP_PAD_AT + core2350b.CHIP_PAD[0] / 2
    assert reach == pytest.approx(4.5)
    assert reach > 7.9 / 2


def test_core2350b_follows_the_makers_drawing():
    """64 holes on a 2.54 grid, 1.27 from the edges, the middle empty."""
    kicad = core2350b.kicad
    pads = kicad.FOOTPRINT["pads"]
    assert [p["num"] for p in pads] == [str(n) for n in range(1, 65)]
    assert (pads[0]["x"], pads[0]["y"]) == kicad.FOOTPRINT["origin"]
    assert pads[0]["shape"] == "rect"
    places = set()
    for pad in pads:
        col, row = ((pad[k] - 1.27) / 2.54 for k in ("x", "y"))
        assert (col, row) == pytest.approx((round(col), round(row)))
        places.add((round(col), round(row)))
    middle = {(c, r) for c in range(2, 8) for r in range(2, 8)}
    assert places == {(c, r) for c in range(10) for r in range(10)} - middle
    box = extent(core2350b.parts(None)["core2350b"])
    assert box[3] - box[0] == pytest.approx(25.4)
    assert box[4] - box[1] == pytest.approx(25.4)


def test_core2350b_symbol_names_every_pad_once():
    """64 pins numbered as the pads; the names of Waveshare's headers."""
    kicad = core2350b.kicad
    pins = {num: name for _k, name, num, *_at in kicad.SYMBOL["pins"]}
    assert sorted(pins, key=int) == [str(n) for n in range(1, 65)]
    assert [pins[n] for n in ("1", "17", "21", "31", "32", "49")] == [
        "GND", "GND", "BOOTSEL", "VBUS", "3V3", "GND"]
    assert pins["19"] == "GP47_ADC7_PSRAM_CS"
    gpio = {name.split("_")[0] for name in pins.values()
            if name.startswith("GP")}
    assert gpio == {f"GP{i}" for i in range(48)}


def test_core2350b_library_model_stands_on_headers():
    """64 pins reach 6.0 under the plastic; the board lies 2.5 up."""
    solids = core2350b.kicad_model()
    assert len(solids) == 445 + 8 * 9
    box = extent(solids)
    assert box[2] == pytest.approx(-6.0)


@pytest.mark.parametrize("stem, names", [
    ("es3c28p_pwr", ["5V", "GND"]),
    ("es3c28p_uart", ["5V", "GND", "TXD", "RXD"]),
    ("es3c28p_i2c", ["3V3", "GND", "SCL", "SDA"]),
    ("es3c28p_gpio", ["IO2", "IO3", "IO14", "IO21"])])
def test_es3c28p_headers_follow_the_socket(stem, names):
    """Pins in the socket's order on a 2.0mm JST PH row, pin 1 first."""
    spec = next(s for s in gen_all.load_specs()
                if s.__name__ == stem)
    pins = [(num, name) for _k, name, num, *_at in spec.SYMBOL["pins"]]
    assert pins == [(str(i), n) for i, n in enumerate(names, 1)]
    pads = spec.FOOTPRINT["pads"]
    assert [p["num"] for p in pads] == [str(i) for i in
                                        range(1, len(names) + 1)]
    xs = [p["x"] for p in pads]
    steps = [b - a for a, b in zip(xs[:-1], xs[1:], strict=True)]
    assert steps == pytest.approx([2.0] * (len(names) - 1))
    assert spec.FOOTPRINT["board"] == pytest.approx(
        (2.0 * (len(names) - 1) + 3.9, 4.5))
    assert spec.SYMBOL["footprint"] == spec.FOOTPRINT["name"]
