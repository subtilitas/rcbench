"""tools/printer_files.py: the Python reader of printer files.

The files of record pass --check; a file that breaks a repo rule (licence,
SPDX line, source) or a format rule fails it with its code and line; the
dump of the T50M Pro file holds what prn_parse() reads; the reader's
codes for one change of each kind.  test/host/prn_parity.py holds the
reader to prn_parse() on 3,000 changed files in ctest.

SPDX-License-Identifier: MIT
"""

import pathlib

import pytest

import printer_files as pf

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
T50M = REPO / "protocols" / "printers" / "supvan-t50m-pro.prn"
FIXTURES = REPO / "test" / "host" / "fixtures" / "printers"


def _parse(text: str, name: str | None = None):
    """Read a text.

    Args:
        text: the file.
        name: its name, or None.

    Returns:
        (spec, code, line) as printer_files.parse() gives them.
    """
    return pf.parse(text.encode("latin-1"), name)


def test_the_files_of_record_pass_check(capsys):
    """Every file under protocols/printers passes --check."""
    files = sorted((REPO / "protocols" / "printers").glob("*.prn"))
    assert files
    assert pf.main(["--check", *map(str, files)]) == 0
    assert "hold" in capsys.readouterr().out


def test_the_t50m_pro_file_reads_as_its_c_reader_does():
    """The T50M Pro file's spec and dump hold what the C reader reads."""
    spec, code, line = pf.parse(T50M.read_bytes(), T50M.name)
    assert (code, line) == (0, 0)
    assert spec.id == "supvan-t50m-pro"
    assert spec.mtu_min == 131
    assert spec.frag == 128
    assert len(spec.step) == 12
    assert spec.step[10]["relink"] == 11
    assert spec.step[4]["relink"] == pf.RELINK_VERDICT
    assert [st["name"] for st in spec.step[:3]] == ["check", "material",
                                                     "ready"]
    assert spec.vd_key == 0x30
    line = pf.dump_line(T50M)
    assert "\tOK\t'73757076616e2d7435306d2d70726f' " in line
    assert " R0,1,2,3,4,5,6,7,8,9,10,11, a1 " in line


def test_the_suites_fixtures_read():
    """The suites' fixture printers read without a refusal."""
    for path in sorted(FIXTURES.glob("*.prn")):
        spec, code, _ = pf.parse(path.read_bytes(), path.name)
        assert code == 0, path
        assert spec.licence == "MIT"


@pytest.mark.parametrize("old,new,problem", [
    ("licence MIT", "licence BSD-3-Clause", "not MIT"),
    ("# SPDX-License-Identifier: MIT\n", "", "SPDX"),
    ("source https://github.com/heeen/supvan-cups\n", "", "no source"),
    ("mtu_min 131", "mtu_min 248", "E07 line"),
])
def test_check_names_each_broken_rule(tmp_path, capsys, old, new, problem):
    """--check fails on each repo rule and names it."""
    text = T50M.read_text()
    assert old in text
    path = tmp_path / T50M.name
    path.write_text(text.replace(old, new, 1))
    assert pf.main(["--check", str(path)]) == 1
    assert problem in capsys.readouterr().err


def test_check_holds_the_id_to_the_file_name(tmp_path, capsys):
    """A file whose name is not its id fails --check with E17."""
    path = tmp_path / "other.prn"
    path.write_bytes(T50M.read_bytes())
    assert pf.main(["--check", str(path)]) == 1
    assert "E17 line" in capsys.readouterr().err


@pytest.mark.parametrize("old,new,code", [
    ("rcbench printer, format 1", "rcbench printer, format 2", 5),
    ("density 4", "dens 4", 6),
    ("version 1\n", "version 1\nversion 1\n", 8),
    ("head_dots 384\n", "", 9),
    ("{a:u8}", "{a:u9}", 11),
    ("{ck:sum16le:10:15}", "{ck:sum16le:10:16}", 12),
    ("field 30 left 43 le32", "field 30 left 44 le32", 13),
    ("  send cmd a=0x12", "  send cmdx a=0x12", 14),
    ("job query check material ready", "job query nosuch", 15),
    ("\nend\n", "\n", 16),
    ("mtu_min 131", "mtu_min 130", 18),
    ("2800:15", "3000:15", 19),
    ("match oui A4:93:40\n", "", 20),
    ("label dot0 right", "label dot0 top", 21),
    ("{total:u8} {data}", "{total:u8}", 22),
    ("  expect 12\n  within 1000\n", "  expect 12\n", 24),
    ("  relink done\nstep done", "  relink nosuch\nstep done", 25),
    ("from start to done", "from done to start", 26),
    ("roll source record", "roll source fixed", 27),
    ("id supvan-t50m-pro", "id Supvan", 29),
])
def test_each_change_gives_its_code(old, new, code):
    """One change of the T50M Pro file gives its code and a line."""
    text = T50M.read_text()
    assert old in text
    _, got, line = _parse(text.replace(old, new, 1))
    assert got == code
    assert line > 0


def test_limits_of_size_lines_and_bytes():
    """The size, line and line-count limits, bad bytes and CRLF."""
    text = T50M.read_text()
    assert _parse("")[1:] == (1, 0)
    assert _parse("#" * 8193)[1:] == (1, 0)
    assert _parse("#" * 121)[1:] == (3, 1)
    assert _parse("#" * 120 + "\n")[1:] == (5, 0)
    assert _parse("#\n" * 256 + "x")[1:] == (4, 257)
    assert _parse(text.replace("name T50M PRO", "name T50M\x7fPRO"))[1] == 2
    crlf = text.replace("\n", "\r\n")
    assert _parse(crlf)[1] == 0
    assert _parse(text + "\r")[1] == 2


def test_dump_marks_a_file_it_cannot_read(tmp_path):
    """A missing file dumps as E00, a refused one with its code."""
    missing = tmp_path / "missing.prn"
    assert pf.dump_line(missing) == f"{missing}\tBAD\tE00 0"
    bad = tmp_path / "bad.prn"
    bad.write_text("rcbench printer, format 1\nend\n")
    assert pf.dump_line(bad) == f"{bad}\tBAD\tE09 2"


def test_dump_mode_prints_one_line_a_file(capsys):
    """--dump prints one line a file."""
    assert pf.main(["--dump", str(T50M)]) == 0
    out = capsys.readouterr().out.splitlines()
    assert len(out) == 1 and out[0].startswith(f"{T50M}\tOK\t")
