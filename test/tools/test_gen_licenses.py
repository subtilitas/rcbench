"""Tests for gen_licenses.py.

SPDX-License-Identifier: MIT
"""

import gen_licenses as g


def test_the_committed_files_are_the_tools_output():
    """--check holds about_gen.c and both doc tables to the list."""
    assert g.main(["--check"]) == 0


def test_a_doc_table_edited_by_hand_fails_the_check(tmp_path, monkeypatch):
    """A row changed in docs/Licenses.md is drift."""
    page = tmp_path / "Licenses.md"
    text = g.DOCS["en"].read_text(encoding="utf-8")
    page.write_text(text.replace("| `MIT` |", "| `BSD-3-Clause` |", 1),
                    encoding="utf-8")
    monkeypatch.setitem(g.DOCS, "en", page)
    assert g.main(["--check"]) == 1


def test_the_screens_table_edited_by_hand_fails_the_check(tmp_path,
                                                          monkeypatch):
    """A row changed in about_gen.c is drift."""
    out = tmp_path / "about_gen.c"
    out.write_text(g.OUT_C.read_text(encoding="utf-8").replace(
        '"ESP-IDF', '"ESP-IDE', 1), encoding="utf-8")
    monkeypatch.setattr(g, "OUT_C", out)
    assert g.main(["--check"]) == 1


def test_a_component_added_to_the_list_reaches_both(monkeypatch):
    """One more component changes the screen's table and both pages'."""
    before = g.outputs()
    extra = g.Component("zlib", "1.3.1", "Zlib",
                        ["Copyright (C) 1995-2024 Jean-loup Gailly and Mark "
                         "Adler"], "panel", [])
    monkeypatch.setattr(g, "COMPONENTS", [*g.COMPONENTS, extra])
    after = g.outputs()
    for path in before:
        assert before[path] != after[path]
        assert "zlib" in after[path]


def test_every_row_fits_and_every_component_is_in_the_doc():
    """Rows hold at most CELLS cells; the doc lists every name."""
    for part in g.parts():
        for style, text in part:
            assert style in "NDSTB"
            assert len(text) <= g.CELLS
    table = g.doc_table("en")
    for c in g.COMPONENTS:
        assert g.md(c.name) in table
        assert f"`{c.spdx}`" in table


def test_a_long_line_breaks_at_a_space_and_keeps_its_indent():
    """The rest of a broken row keeps the row's indent."""
    rows = g.wrap("    " + "word " * 30, 40)
    assert all(len(r) <= 40 for r in rows)
    assert all(r.startswith("    w") for r in rows)


def test_characters_outside_the_faces_are_spelled():
    """© becomes (c), an accent its letter, the German letters stay."""
    assert g.face("© Frédéric Häne\tx") == "(c) Frederic Häne       x"


def test_notice_agrees_with_the_list_of_linked_components():
    """NOTICE names esp_hosted as linked and nothing the list lacks."""
    text = g.NOTICE.read_text(encoding="utf-8")
    assert g.notice_problems(text) == []
    stale = text.replace(
        "  espressif/esp_hosted 2.13.0",
        "  espressif/esp_wifi_remote 1.6.5        Apache License 2.0\n"
        "  espressif/esp_hosted 2.13.0", 1)
    assert g.notice_problems(stale) == [
        "NOTICE lists espressif/esp_wifi_remote 1.6.5 as linked; the list "
        "does not"]
    gone = text.replace("  espressif/esp_hosted 2.13.0",
                        "  espressif/esp_hosted 2.13.0 (not linked)", 1)
    assert g.notice_problems(gone) == [
        "NOTICE does not list espressif/esp_hosted 2.13.0 as linked"]
