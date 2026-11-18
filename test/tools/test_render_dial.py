"""render_dial.py: the committed Dial faces against the current render.

One face is rendered into a scratch folder with the renderer built once:
--check passes on the image the tool wrote and fails on one changed pixel,
on an image of another size and on a missing image.

SPDX-License-Identifier: MIT
"""

import importlib.util
import shutil

import pytest

import render_dial as rt
from conftest import skip_or_fail

NAME = "motor-armed"
PNG = "dial-motor-armed.png"
SIZE = (240, 240)


@pytest.fixture(scope="module")
def renderer(tmp_path_factory):
    """The renderer, built once for the module."""
    if shutil.which("cc") is None:
        skip_or_fail("cc is not on PATH")
    if importlib.util.find_spec("PIL") is None:
        skip_or_fail("Pillow is not installed")
    return rt.build(tmp_path_factory.mktemp("render_dial"))


@pytest.fixture
def images(renderer, tmp_path, monkeypatch, capsys):
    """A scratch image folder holding the English render of one face."""
    monkeypatch.setattr(rt, "build", lambda tmp: renderer)
    assert run(monkeypatch, NAME, "--lang", "en", "--dir",
               str(tmp_path)) == 0
    assert f"wrote {tmp_path / PNG}" in capsys.readouterr().out
    return tmp_path


def run(monkeypatch, *args):
    """Run the tool with its arguments; return the exit status."""
    monkeypatch.setattr("sys.argv", ["render_dial.py", *args])
    return rt.main()


def check(monkeypatch, images):
    """Run --check on the one image of the scratch folder."""
    return run(monkeypatch, NAME, "--lang", "en", "--dir", str(images),
               "--check")


def test_a_written_image_passes_the_check(images, monkeypatch, capsys):
    """What the tool writes is what --check wants."""
    from PIL import Image
    assert Image.open(images / PNG).size == SIZE
    assert check(monkeypatch, images) == 0
    assert f"{PNG} matches" in capsys.readouterr().out


def test_one_changed_pixel_fails(images, monkeypatch, capsys):
    """The compare is by pixel: one of them changed is out of date."""
    from PIL import Image
    image = Image.open(images / PNG).convert("RGB")
    r, g, b = image.getpixel((SIZE[0] // 2, SIZE[1] // 2))
    image.putpixel((SIZE[0] // 2, SIZE[1] // 2), ((r + 128) % 256, g, b))
    image.save(images / PNG)
    assert check(monkeypatch, images) == 1
    err = capsys.readouterr().err
    assert f"{PNG} is out of date: 1 pixels differ" in err
    assert "Run tools/render_dial.py and review the new images." in err


def test_the_same_pixels_in_another_encoding_pass(images, monkeypatch):
    """A PNG written with other settings holds the same pixels."""
    from PIL import Image
    before = (images / PNG).read_bytes()
    Image.open(images / PNG).convert("RGB").save(images / PNG,
                                                 compress_level=1)
    assert (images / PNG).read_bytes() != before
    assert check(monkeypatch, images) == 0


def test_an_image_of_another_size_fails(images, monkeypatch, capsys):
    """A committed image that is not the frame's size fails."""
    from PIL import Image
    Image.new("RGB", (SIZE[0] // 2, SIZE[1])).save(images / PNG)
    assert check(monkeypatch, images) == 1
    assert "the render is" in capsys.readouterr().err


def test_a_missing_image_fails(images, monkeypatch, capsys):
    """A face without its committed image fails."""
    (images / PNG).unlink()
    assert check(monkeypatch, images) == 1
    err = capsys.readouterr().err
    assert "does not exist; run tools/render_dial.py" in err


def test_the_german_image_goes_into_the_de_folder(images, monkeypatch):
    """German renders are written under de/."""
    assert run(monkeypatch, NAME, "--lang", "de", "--dir",
               str(images)) == 0
    assert (images / "de" / PNG).is_file()


def test_an_unknown_name_stops_the_tool(renderer, monkeypatch):
    """A name that is no face is refused before anything is drawn."""
    monkeypatch.setattr(rt, "build", lambda tmp: renderer)
    with pytest.raises(SystemExit) as stop:
        run(monkeypatch, "no-such-face", "--check")
    assert "unknown" in str(stop.value)


def test_an_output_file_takes_one_face_and_one_language(
        renderer, monkeypatch, tmp_path):
    """--output with two languages is refused; with one it is written."""
    monkeypatch.setattr(rt, "build", lambda tmp: renderer)
    with pytest.raises(SystemExit):
        run(monkeypatch, NAME, "-o", str(tmp_path / "x.png"))
    assert run(monkeypatch, NAME, "--lang", "de", "-o",
               str(tmp_path / "x.png")) == 0
    assert (tmp_path / "x.png").is_file()


def test_every_face_has_its_two_committed_images():
    """docs/img holds each face in English and in German."""
    for name in rt.FACES:
        for sub in rt.LANGS.values():
            path = rt.REPO / "docs" / "img" / sub / f"dial-{name}.png"
            assert path.is_file(), path
