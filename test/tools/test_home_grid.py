"""The home grid's column check: overview_screen.c compiles with its tiles as
they are, and a fourth workbench or a fourth instrument at 4 columns fails
the build with the assertion's message.

SPDX-License-Identifier: MIT
"""

import pathlib
import shutil
import subprocess

import pytest

import render_ui

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
SRC = REPO / "shared" / "ui" / "overview_screen.c"
# The renderer's include path, which builds this file with every screen.
INCLUDES = render_ui.INCLUDES

pytestmark = pytest.mark.skipif(shutil.which("cc") is None,
                                reason="no C compiler on PATH")


def compile_copy(tmp_path, text):
    """cc -fsyntax-only on @p text in a copy next to nothing else: the
    includes are the tree's."""
    copy = tmp_path / "overview_screen.c"
    copy.write_text(text, encoding="utf-8")
    cmd = ["cc", "-std=c11", "-fsyntax-only", str(copy)]
    for inc in INCLUDES:
        cmd += ["-I", str(REPO / inc)]
    return subprocess.run(cmd, capture_output=True, text=True)


def with_extra(table, row):
    """overview_screen.c with @p row added as the first entry of @p table."""
    text = SRC.read_text(encoding="utf-8")
    head = f"static const tile_t {table}[] = {{\n"
    assert text.count(head) == 1, table
    return text.replace(head, head + row)


EXTRA = ('    { SCREEN_LOGS, "EXTRA", TX_OV_LOGS, ui_icon_record, 0, '
         'false },\n')


def test_the_grid_as_it_is_compiles(tmp_path):
    proc = compile_copy(tmp_path, SRC.read_text(encoding="utf-8"))
    assert proc.returncode == 0, proc.stderr


def test_a_fourth_workbench_fails_the_build(tmp_path):
    proc = compile_copy(tmp_path, with_extra("k_workbenches", EXTRA))
    assert proc.returncode != 0
    assert "a workbench past column COLS - 1 needs COLS 5" in proc.stderr


def test_a_fourth_instrument_fails_the_build(tmp_path):
    proc = compile_copy(tmp_path, with_extra("k_instruments", EXTRA))
    assert proc.returncode != 0
    assert "an instrument past column COLS - 1 needs COLS 5" in proc.stderr


def test_a_third_tile_in_the_last_column_fails_the_build(tmp_path):
    proc = compile_copy(tmp_path, with_extra("k_last_column", EXTRA))
    assert proc.returncode != 0
    assert "the last column holds LOGS over SETUP" in proc.stderr
