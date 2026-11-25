"""hardware/mechanical/_measure_rcbench_case.py: failures of the input.

A missing file, a file that is not STEP and a STEP file without a shape
exit 1 with a message on stderr; the delivered file exits 0.

SPDX-License-Identifier: MIT
"""

import sys
from pathlib import Path

import pytest

pytest.importorskip("OCP")
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "hardware" / "mechanical"))
import _measure_rcbench_case as measure  # noqa: E402

EMPTY_STEP = """ISO-10303-21;
HEADER;
FILE_DESCRIPTION((''),'2;1');
FILE_NAME('x','2026-01-01T00:00:00',(''),(''),'','','');
FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));
ENDSEC;
DATA;
ENDSEC;
END-ISO-10303-21;
"""


def test_missing_file_fails(tmp_path, capsys):
    """A path that does not exist exits 1 and names the path."""
    path = tmp_path / "none.step"
    assert measure.main([str(path)]) == 1
    err = capsys.readouterr()
    assert "no such file" in err.err and str(path) in err.err
    assert "valid" not in err.out


def test_non_step_file_fails(tmp_path, capsys):
    """A text file that is not STEP exits 1 and prints no result."""
    path = tmp_path / "text.step"
    path.write_text("not a STEP file\n")
    assert measure.main([str(path)]) == 1
    err = capsys.readouterr()
    assert "not read" in err.err and "valid" not in err.out


def test_step_without_shape_fails(tmp_path, capsys):
    """A STEP file with no shape exits 1 and prints no result."""
    path = tmp_path / "empty.step"
    path.write_text(EMPTY_STEP)
    assert measure.main([str(path)]) == 1
    err = capsys.readouterr()
    assert err.err and "valid" not in err.out


def test_delivered_file_passes(capsys):
    """The enclosure's file reads and every shape is valid."""
    assert measure.main([]) == 0
    assert capsys.readouterr().out.rstrip().endswith("valid")
