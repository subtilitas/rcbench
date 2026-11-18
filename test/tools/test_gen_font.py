"""gen_font.py: the checked-in font tables against their generator.

One small face is written into a scratch folder: --check passes on what the
tool wrote and fails on a changed byte, a missing file and, for a face
that has to fit its cell, on solid ink the cell cuts.

SPDX-License-Identifier: MIT
"""

import pathlib
import re

import pytest

from conftest import REPO, import_or_fail, skip_or_fail

# gen_font.py imports Pillow at its top: without it the whole file skips,
# and fails when CI is set.
import_or_fail("PIL", "Pillow is not installed")

import gen_font as gf  # noqa: E402

# name, cell, block, TrueType size, offsets, threshold, output, extra: the
# 6x12 face's row, written to a scratch file.
SMALL = ("t_font_6x12", 6, 12, 0x20, 0x7E, 10, 0, 0, 110, "out/t_font.c",
         gf.TEXT_EXTRA)
# The same face at twice the size in the same cell: ink leaves the cell.
CUT = ("t_font_cut", 6, 12, 0x41, 0x43, 26, 0, 0, 110, "out/t_cut.c", [])


@pytest.fixture
def scratch(tmp_path, monkeypatch):
    """A scratch folder as the working directory, one face in FONTS."""
    if not any(pathlib.Path(p).exists() for p in gf.FONT_CANDIDATES):
        skip_or_fail("no DejaVu Sans Mono or Liberation Mono font")
    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr(gf, "FONTS", [SMALL])
    return tmp_path


def run(monkeypatch, *args):
    """Run the tool with its arguments; return the exit status."""
    monkeypatch.setattr("sys.argv", ["gen_font.py", *args])
    return gf.main()


def test_a_written_table_passes_the_check(scratch, monkeypatch, capsys):
    """What the tool writes is what --check wants."""
    assert run(monkeypatch) == 0
    assert "wrote out/t_font.c (104 glyphs" in capsys.readouterr().out
    assert run(monkeypatch, "--check") == 0
    assert "out/t_font.c is up to date" in capsys.readouterr().out


def test_a_changed_byte_of_the_table_fails(scratch, monkeypatch, capsys):
    """One coverage byte edited by hand is out of date."""
    assert run(monkeypatch) == 0
    out = scratch / "out" / "t_font.c"
    text = out.read_text()
    m = re.search(r"0x[0-9a-f]{2},", text)
    byte = "0x01," if m.group() != "0x01," else "0x02,"
    out.write_text(text[:m.start()] + byte + text[m.end():])
    assert run(monkeypatch, "--check") == 1
    assert "out/t_font.c is out of date" in capsys.readouterr().err


def test_a_missing_table_fails(scratch, monkeypatch, capsys):
    """A face without its file is out of date."""
    assert run(monkeypatch, "--check") == 1
    assert "out/t_font.c is out of date" in capsys.readouterr().err


def test_a_changed_generator_row_fails(scratch, monkeypatch, capsys):
    """A threshold changed in FONTS no longer matches the written table."""
    assert run(monkeypatch) == 0
    monkeypatch.setattr(gf, "FONTS", [SMALL[:8] + (140,) + SMALL[9:]])
    assert run(monkeypatch, "--check") == 1
    assert "is out of date" in capsys.readouterr().err


def test_cut_ink_fails_only_a_face_that_must_fit(scratch, monkeypatch,
                                                 capsys):
    """Solid ink outside the cell: reported for any face, fatal in MUST_FIT."""
    monkeypatch.setattr(gf, "FONTS", [CUT])
    assert gf.clipped(CUT)
    assert run(monkeypatch) == 0
    capsys.readouterr()
    assert run(monkeypatch, "--check") == 0
    assert "t_font_cut cuts solid ink (pixels per glyph)" in \
        capsys.readouterr().out
    monkeypatch.setattr(gf, "MUST_FIT", {"t_font_cut"})
    assert run(monkeypatch, "--check") == 1
    assert "t_font_cut cuts solid ink at the cell edge" in \
        capsys.readouterr().err


def test_the_small_face_keeps_its_ink_inside_the_cell(scratch):
    """The 6x12 row cuts no glyph."""
    assert gf.clipped(SMALL) == {}


def test_a_face_holds_its_block_then_its_extra_code_points():
    """Printable ASCII first, then degree, the German letters and ohm."""
    codes = gf.codes(SMALL)
    assert codes[:95] == list(range(0x20, 0x7F))
    assert codes[95:] == [0xB0, 0xC4, 0xD6, 0xDC, 0xDF, 0xE4, 0xF6, 0xFC,
                          0x3A9]
    assert gf.codes(CUT) == [0x41, 0x42, 0x43]


def test_a_glyph_is_rendered_into_its_cell(scratch):
    """Every glyph is h rows of w coverage bytes; a space holds no ink."""
    glyphs = gf.render(CUT)
    assert sorted(glyphs) == [0x41, 0x42, 0x43]
    for rows in glyphs.values():
        assert len(rows) == 12 and all(len(row) == 6 for row in rows)
        assert any(any(row) for row in rows)
    space = gf.render(SMALL)[0x20]
    assert not any(any(row) for row in space)


def test_the_preview_prints_and_writes_nothing(scratch, monkeypatch, capsys):
    """--preview draws the named face as text."""
    assert run(monkeypatch, "--preview", "6x12", "--chars", "A") == 0
    out = capsys.readouterr().out
    assert "--- t_font_6x12 0x41 'A'" in out
    assert not (scratch / "out").exists()


def test_every_face_of_the_tree_is_a_file_under_shared_gfx():
    """Each row of FONTS names a checked-in table."""
    for spec in gf.FONTS:
        assert (REPO / spec[9]).is_file(), spec[9]
        assert spec[9].startswith("shared/gfx/gfx_font")
