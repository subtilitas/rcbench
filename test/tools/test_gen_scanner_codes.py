"""Tests for gen_scanner_codes.py.

SPDX-License-Identifier: MIT
"""

import gen_scanner_codes as g


def flip(rows, r, c):
    """The rows with module (r, c) turned over."""
    out = list(rows)
    row = out[r]
    out[r] = row[:c] + ("." if row[c] == "#" else "#") + row[c + 1:]
    return tuple(out)


def test_the_serial_output_code_passes_every_check():
    """Finders, timing, format, Reed-Solomon: none fails; it decodes."""
    found, level, mask, payload = g.problems(g.SERIAL_OUTPUT)
    assert found == []
    assert (level, mask) == ("M", 3)
    assert payload == b"\x800000020."


def test_the_committed_table_is_the_tools_output():
    """--check holds shared/scanner/scanner_codes.c to the text."""
    assert g.main(["--check"]) == 0


def test_a_finder_module_turned_over_fails():
    """One module of each finder's ring fails its finder."""
    for r, c, name in ((0, 3, "top left"), (0, 17, "top right"),
                       (20, 3, "bottom left")):
        found, *_ = g.problems(flip(g.SERIAL_OUTPUT, r, c))
        assert f"no finder pattern {name}" in found


def test_a_format_bit_turned_over_fails():
    """One format bit breaks the copies' agreement and the BCH word."""
    found, *_ = g.problems(flip(g.SERIAL_OUTPUT, 8, 2))
    assert any("format copies differ" in p for p in found)
    assert any("no BCH code word" in p for p in found)


def test_a_data_module_turned_over_fails_the_reed_solomon_check():
    """One data module breaks a code word."""
    found, *_ = g.problems(flip(g.SERIAL_OUTPUT, 12, 12))
    assert found == ["the code words fail the Reed-Solomon check"]


def test_the_timing_and_the_dark_module_are_checked():
    """A timing module and the dark module, each turned over, fail."""
    found, *_ = g.problems(flip(g.SERIAL_OUTPUT, 6, 10))
    assert any("timing" in p for p in found)
    found, *_ = g.problems(flip(g.SERIAL_OUTPUT, 13, 8))
    assert "the dark module is light" in found


def test_a_matrix_of_the_wrong_size_is_refused():
    """20 rows are no version 1 code."""
    assert g.problems(g.SERIAL_OUTPUT[:20])[0] == ["the matrix is not 21 x 21"]
