"""ui_sources.py against shared/ui/CMakeLists.txt: every file the panel
builds there is in SOURCES or in EXCLUDED, so a screen split into more
files reaches render_ui.py and frame_cost.py.

SPDX-License-Identifier: MIT
"""

import pathlib
import re

import ui_sources

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
UI = "shared/ui"


def panel_ui_sources(text):
    """shared/ui paths of the .c files in the set(SRCS ...) of @p text."""
    block = re.search(r"set\(SRCS\b(.*?)\)", text, re.S)
    assert block, "no set(SRCS ...) in shared/ui/CMakeLists.txt"
    return [f"{UI}/{name}"
            for name in re.findall(r"^\s*([\w.]+\.c)\s*$", block.group(1),
                                   re.M)]


def missing(built, sources, excluded):
    """The files the panel builds that neither list names."""
    return [p for p in built if p not in sources and p not in excluded]


def test_the_parser_reads_the_srcs_block():
    text = "set(SRCS\n    a.c\n    b_c.c\n)\nset(OTHER x.c)\n"
    assert panel_ui_sources(text) == [f"{UI}/a.c", f"{UI}/b_c.c"]


def test_a_file_in_neither_list_is_reported():
    built = [f"{UI}/a.c", f"{UI}/a_draw.c", f"{UI}/b.c"]
    assert missing(built, [f"{UI}/a.c"], {f"{UI}/b.c": "why"}) == [
        f"{UI}/a_draw.c"]


def test_every_panel_ui_file_is_listed_or_excluded():
    text = (REPO / UI / "CMakeLists.txt").read_text()
    built = panel_ui_sources(text)
    assert len(built) > 50
    assert missing(built, ui_sources.SOURCES, ui_sources.EXCLUDED) == []


def test_an_exclusion_names_a_built_file_the_list_leaves_out():
    built = panel_ui_sources((REPO / UI / "CMakeLists.txt").read_text())
    for path, why in ui_sources.EXCLUDED.items():
        assert path in built, f"{path} is excluded but not built"
        assert path not in ui_sources.SOURCES, f"{path} is in both"
        assert why.strip(), f"{path} has no reason"


def test_every_source_exists_once():
    assert len(set(ui_sources.SOURCES)) == len(ui_sources.SOURCES)
    for path in ui_sources.SOURCES:
        assert (REPO / path).is_file(), path
    for path in ui_sources.INCLUDES:
        assert (REPO / path).is_dir(), path
