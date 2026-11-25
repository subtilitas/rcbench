"""The QR codes of the label fixtures, read by a decoder rcbench did not write.

test/host/render_label.c draws each label of
test/host/fixtures/label/cases.txt and the host suite holds the PNG beside
it to the layout byte for byte.  Here OpenCV's QR detector or zbarimg
(ZBar) decodes each code from that PNG and must return the label's id, or
`RCBENCH TEST` for the test label.  A machine with neither skips the
decode; the size of each picture is checked everywhere.

SPDX-License-Identifier: MIT
"""

import pathlib
import shutil
import struct
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
FIXTURES = REPO / "test" / "host" / "fixtures" / "label"


def cases():
    """Read cases.txt.

    Returns:
        A list of (name, width mm, height mm, the code's text).
    """
    out = []
    for line in (FIXTURES / "cases.txt").read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        name, roll, kind, text, _dut = line.split("|")
        w, h = (int(v) for v in roll.split("x"))
        out.append((name, w, h, text if kind == "id" else "RCBENCH TEST"))
    return out


CASES = cases()


def png_size(path):
    """Read a PNG's width and height from its IHDR chunk.

    Args:
        path: The file.

    Returns:
        (width, height) in pixels.
    """
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    assert data[12:16] == b"IHDR", path
    return struct.unpack(">II", data[16:24])


def decode_opencv(path):
    """Decode with OpenCV, or return None when it is not installed.

    The picture gets a white border of 4 modules and is scaled 4 times with
    nearest-neighbour sampling: the label's own quiet zone is 2 modules.

    Args:
        path: The PNG.

    Returns:
        The decoded text, "" when nothing was found, None without OpenCV.
    """
    try:
        import cv2
    except ImportError:
        return None
    img = cv2.imread(str(path), cv2.IMREAD_GRAYSCALE)
    img = cv2.copyMakeBorder(img, 32, 32, 32, 32, cv2.BORDER_CONSTANT,
                             value=255)
    img = cv2.resize(img, None, fx=4, fy=4, interpolation=cv2.INTER_NEAREST)
    text, _points, _straight = cv2.QRCodeDetector().detectAndDecode(img)
    return text


def decode_zbar(path):
    """Decode with zbarimg, or return None when it is not on the path.

    Args:
        path: The PNG.

    Returns:
        The decoded text, "" when nothing was found, None without zbarimg.
    """
    exe = shutil.which("zbarimg")
    if exe is None:
        return None
    run = subprocess.run([exe, "--quiet", "--raw", "-Sdisable",
                          "-Sqrcode.enable", str(path)],
                         capture_output=True, text=True, check=False)
    return run.stdout.strip()


def test_the_cases_cover_the_rolls_of_the_plan():
    """Every roll of the plan has a fixture, and each fixture is listed."""
    rolls = {(w, h) for _n, w, h, _t in CASES}
    assert {(40, 30), (48, 30), (30, 20), (15, 30), (25, 100)} <= rolls
    listed = {n for n, _w, _h, _t in CASES}
    on_disk = {p.stem for p in FIXTURES.glob("*.png")}
    assert listed == on_disk


@pytest.mark.parametrize("name,w,h,text", CASES)
def test_a_label_picture_is_the_roll_in_dots(name, w, h, text):
    """W = width x 8 dots, H = height x 8 - 16 image columns."""
    del text
    assert png_size(FIXTURES / f"{name}.png") == (w * 8, h * 8 - 16)


@pytest.mark.parametrize("name,w,h,text", CASES)
def test_the_code_decodes_to_the_id(name, w, h, text):
    """An independent decoder reads the label's id from its code."""
    del w, h
    path = FIXTURES / f"{name}.png"
    results = [r for r in (decode_opencv(path), decode_zbar(path))
               if r is not None]
    if not results:
        pytest.skip("no QR decoder: neither OpenCV nor zbarimg")
    for got in results:
        assert got == text, f"{name}: decoded {got!r}"
