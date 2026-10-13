# SPDX-License-Identifier: MIT
"""tools/gen_board_drawing.py: the board drawing the coprocessor serves is
the configurator's panel drawing, laid out as link_pages.h says, and a
drawing the panel would refuse is refused here with the generator's own
error."""

import copy
import json
import pathlib
import re
import struct

import pytest

import gen_board_drawing as g

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
PANEL = REPO / "configurator" / "drawings" / "panel.json"
C_FILE = REPO / "firmware" / "iomcu" / "src" / "board_drawing_rp2350_can.c"


def panel():
    return json.loads(PANEL.read_text(encoding="utf-8"))


def c_bytes(text):
    """The payload array of a generated C file, as bytes."""
    body = text.split("] = {", 1)[1].split("};", 1)[0]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    return bytes(int(v, 16) for v in re.findall(r"0x([0-9A-F]{2})", body))


def c_const(text, name):
    m = re.search(rf"{name}\s*=\s*(0x[0-9A-F]+|\d+)u", text)
    assert m, name
    return int(m.group(1), 0)


def test_the_checked_in_file_is_what_the_generator_writes():
    assert g.main(["--check"]) == 0


def test_the_checksums_are_the_links_own():
    # The check values of CRC-16/XMODEM and FNV-1a 32 over "123456789":
    # link_crc() seeds zero, link_fnv1a32() seeds LINK_FNV_INIT.
    assert g.link_crc(b"123456789") == 0x31C3
    assert g.link_fnv1a32(b"123456789") == 0xBB86B11C
    assert g.link_fnv1a32(b"") == 0x811C9DC5


def test_the_c_file_carries_the_payload_and_its_sums():
    text = C_FILE.read_text(encoding="utf-8")
    payload = g.encode(panel())
    assert c_bytes(text) == payload
    sym = "board_drawing_rp2350_can"
    assert c_const(text, f"{sym}_bytes") == len(payload)
    assert c_const(text, f"{sym}_crc") == g.link_crc(payload)
    assert c_const(text, f"{sym}_hash") == g.link_fnv1a32(payload)
    assert c_const(text, f"{sym}_width") == panel()["width"]
    assert c_const(text, f"{sym}_height") == panel()["height"]


def test_the_payload_is_laid_out_as_link_pages_says():
    d = panel()
    b = g.encode(d)
    pads, shapes = len(d["pads"]), len(d["shapes"])
    assert len(b) == g.HEAD_BYTES + pads * g.PAD_BYTES \
        + shapes * g.SHAPE_BYTES
    assert len(b) <= g.MAX_BYTES == 5328
    (ver, npad, nshape, w, h, colour, ring, corner, ring_r, hole_r,
     zero) = struct.unpack_from("<BBHHHHHBBBB", b, 0)
    assert (ver, npad, nshape, w, h) == (1, pads, shapes, d["width"],
                                         d["height"])
    assert (colour, ring) == (g.rgb565(d["colour"]), g.rgb565(d["ring"]))
    assert (corner, ring_r, hole_r, zero) == (d["corner"], d["ringRadius"],
                                              d["holeRadius"], 0)
    # Pad 1, the square one, first.
    n, kind, value, flags, x, y = struct.unpack_from("<BBBBHH", b, 16)
    p = d["pads"][0]
    assert (n, kind, value, flags, x, y) == (1, 4, p["value"], 1, p["x"],
                                             p["y"])
    # The last shape last.
    at = len(b) - g.SHAPE_BYTES
    s = d["shapes"][-1]
    assert struct.unpack_from("<BBHHHHH", b, at) == (
        g.SHAPE_KINDS[s["kind"]], s["param"], g.rgb565(s["colour"]),
        s["a"], s["b"], s["c"], s["d"])


@pytest.mark.parametrize("colour,want", [
    ("#000000", 0x0000), ("#ffffff", 0xFFFF), ("#ff0000", 0xF800),
    ("#00ff00", 0x07E0), ("#0000ff", 0x001F), ("#1f4f97", 0x1A72),
])
def test_a_colour_is_rgb565(colour, want):
    assert g.rgb565(colour) == want


def change(fn):
    d = panel()
    fn(d)
    return d


def pad(i, **kv):
    def fn(d):
        d["pads"][i].update(kv)
    return fn


def shape(of, **kv):
    """Change the first shape of kind @of."""
    def fn(d):
        for s in d["shapes"]:
            if s["kind"] == of:
                s.update(kv)
                return
        raise AssertionError(f"no {of} in the panel drawing")
    return fn


def top(**kv):
    def fn(d):
        d.update(kv)
    return fn


def drop(key):
    def fn(d):
        del d[key]
    return fn


def many_shapes(d):
    d["shapes"] = [copy.deepcopy(d["shapes"][0])
                   for _ in range(g.MAX_SHAPES + 1)]


@pytest.mark.parametrize("fn", [
    drop("width"), drop("pads"), top(width=0), top(height=0),
    top(width=65536), top(corner=1100), top(ringRadius=0),
    top(holeRadius=65), top(colour="blue"), top(colour="#12345g"),
    top(pads=[]), top(pads={}), top(shapes="x"), many_shapes,
    pad(0, pad=0), pad(0, pad=64), pad(1, pad=1), pad(0, kind="via"),
    pad(2, value=1), pad(0, x=64), pad(0, y=2100 - 64),
    pad(0, square="yes"), pad(0, x=1.5), pad(0, value=True),
    shape("rrect", kind="oval"), shape("rect", param=1),
    shape("rrect", param=255), shape("disc", d=1), shape("disc", param=1),
    shape("line", param=0), shape("rect", a=5100), shape("line", c=5100),
    shape("disc", colour="#fff"),
], ids=lambda f: getattr(f, "__name__", "change"))
def test_a_drawing_the_panel_would_refuse_is_refused(fn):
    with pytest.raises(g.DrawingError):
        g.encode(change(fn))


@pytest.mark.parametrize("record", [None, 1, "x", []])
def test_a_record_that_is_not_an_object_is_refused(record):
    d = panel()
    d["pads"][3] = record
    with pytest.raises(g.DrawingError):
        g.encode(d)


def test_a_ring_touching_the_edge_is_inside():
    d = panel()
    r = d["ringRadius"]
    d["pads"][0]["x"] = r
    g.encode(d)


def test_check_finds_a_file_that_drifted_or_is_missing(tmp_path,
                                                      monkeypatch):
    src = tmp_path / "panel.json"
    src.write_text(PANEL.read_text(encoding="utf-8"), encoding="utf-8")
    dst = tmp_path / "out.c"
    monkeypatch.setattr(g, "REPO", tmp_path)
    monkeypatch.setattr(g, "BOARDS", [("t", "panel.json", "out.c")])

    assert g.main(["--check"]) == 1             # missing
    assert g.main([]) == 0                      # written
    assert g.main(["--check"]) == 0
    dst.write_text(dst.read_text(encoding="utf-8").replace("0x01", "0x02",
                                                           1),
                   encoding="utf-8")
    assert g.main(["--check"]) == 1             # drifted

    # A source the panel would refuse writes nothing and fails.
    d = panel()
    d["pads"][0]["pad"] = 0
    src.write_text(json.dumps(d), encoding="utf-8")
    before = dst.read_text(encoding="utf-8")
    assert g.main([]) == 1
    assert dst.read_text(encoding="utf-8") == before
    src.write_text("{", encoding="utf-8")
    assert g.main(["--check"]) == 1
