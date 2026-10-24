"""Tests for check_perf_log.py.

The marks RCBENCH_PERF_LOG leaves in an image and the check against the
setting.

SPDX-License-Identifier: MIT
"""

import check_perf_log as cp

OFF = b"\x7fELF" + b"\0" * 64 + b"scan_stack\0stack scan\0PSRAM probe\0"
ON = (b"\x7fELF\0" + b"I (%u) rcbench: PSRAM probe: %u us\0"
      b"I (%u) rcbench: stack scan: %u us\0" + b"\0scan_stacks\0")


def test_an_image_without_the_marks_matches_off_and_not_on():
    """An image without the marks matches OFF and fails ON."""
    assert cp.problems(OFF, False) == []
    assert len(cp.problems(OFF, True)) == 3


def test_an_image_with_the_marks_matches_on_and_not_off():
    """An image with the marks matches ON and fails OFF."""
    assert cp.problems(ON, True) == []
    assert len(cp.problems(ON, False)) == 3


def test_one_mark_is_enough_to_fail_off():
    """Any one mark in an image fails OFF and is named."""
    for mark in cp.MARKS:
        out = cp.problems(b"\0" + mark + b"\0", False)
        assert len(out) == 1 and mark.decode() in out[0]


def test_one_missing_mark_fails_on_and_names_it():
    """Any one mark missing from an image fails ON and is named."""
    for mark in cp.MARKS:
        image = ON.replace(mark, b"x" * len(mark))
        out = cp.problems(image, True)
        assert len(out) == 1 and mark.decode() in out[0]


def test_a_part_of_a_mark_is_not_the_mark():
    """A part of a mark is not the mark."""
    assert cp.found(b"scan_stack PSRAM probe stack scan") == []


def test_an_empty_image_holds_none():
    """An empty image holds none of the marks."""
    assert cp.problems(b"", False) == []
    assert len(cp.problems(b"", True)) == 3


def test_main_reads_a_file(tmp_path, capsys):
    """main() reads the file and returns the check's result."""
    elf = tmp_path / "rcbench-panel.elf"
    elf.write_bytes(ON)
    assert cp.main(["c", "on", str(elf)]) == 0
    assert "matches RCBENCH_PERF_LOG=ON" in capsys.readouterr().out
    assert cp.main(["c", "off", str(elf)]) == 1
    assert "holds 'scan_stacks'" in capsys.readouterr().err
    elf.write_bytes(OFF)
    assert cp.main(["c", "off", str(elf)]) == 0


def test_main_reports_a_missing_file_and_a_bad_command_line(tmp_path, capsys):
    """main() reports a missing file and a bad command line."""
    assert cp.main(["c", "off", str(tmp_path / "none.elf")]) == 1
    assert "build the panel first" in capsys.readouterr().err
    assert cp.main(["c"]) == 2
    assert cp.main(["c", "maybe", "x"]) == 2
    assert "usage" in capsys.readouterr().err
