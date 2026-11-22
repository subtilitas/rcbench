"""check_formats.py: every translated format against the call that uses it.

A scratch shared/ of four files holds one lookup of each kind (TR, S, LS,
DF).  The tree as written passes; a format whose conversion does not fit
its argument, a format with an argument too few and a format that is no
literal fail.

SPDX-License-Identifier: MIT
"""

import shutil

import pytest

import check_formats as cf
from conftest import skip_or_fail

DEF = """\
UI_TEXT(BF_LOST, 0, "lost %lu frames", "%lu Frames verloren")
UI_TEXT(TWO_PART, 0, "a %d "
        "b %d", "a %d b %d")
UI_SERVO_TEXT(SERVO_STR_STEP, "step %d", "Schritt %d")
UI_DIAL_TEXT(DIAL_PCT, "%d%%", "%d%%")
"""

UI_TEXT_H = """\
#pragma once
const char *ui_tr(int id);
enum { TX_BF_LOST, TX_TWO_PART };
#define TR(id) ui_tr(TX_##id)
"""

SCREEN_C = """\
#include <stdio.h>
#include "ui_text.h"
void draw(char *b, unsigned long n, int a)
{
    snprintf(b, 32, TR(BF_LOST), n);
    snprintf(b, 32, TR(TWO_PART), a, a);
}
"""

REPORT_C = """\
#include <stdio.h>
typedef struct { const char *const *text; } ctx_t;
const char *servo_str_in(const char *const *text, int id);
enum { SERVO_STR_STEP };
#define %s(id) servo_str_in(c->text, SERVO_STR_##id)
void report_%s(const ctx_t *c, char *b, int n)
{
    (void)c;
    snprintf(b, 32, %s(STEP), n);
}
"""

DIAL_C = """\
#include <stdio.h>
const char *dial_text(int l, int id);
enum { DIAL_PCT };
#define DF(id) dial_text(l, (id))
void face(int l, char *b, int n)
{
    (void)l;
    snprintf(b, 32, DF(DIAL_PCT), n);
}
"""


@pytest.fixture
def tree(tmp_path, monkeypatch):
    """A scratch shared/ and protocols/ the tool is pointed at."""
    if shutil.which("cc") is None and shutil.which("gcc") is None:
        skip_or_fail("no C compiler on PATH")
    shared = tmp_path / "shared"
    for d in ("text/include", "ui/include", "ui", "servo", "dial"):
        (shared / d).mkdir(parents=True, exist_ok=True)
    (tmp_path / "protocols").mkdir()
    (shared / "text" / "include" / "ui_text.def").write_text(DEF)
    (shared / "ui" / "include" / "ui_text.h").write_text(UI_TEXT_H)
    (shared / "ui" / "screen.c").write_text(SCREEN_C)
    lookups = []
    for name, macro in (("servo_report.c", "S"),
                        ("servo_limrun_report.c", "LS"),
                        ("servo_rudder_auto_report.c", "LS")):
        path = shared / "servo" / name
        path.write_text(REPORT_C % (macro, name[:-2], macro))
        lookups.append((path, macro))
    (shared / "dial" / "dial_scene.c").write_text(DIAL_C)
    monkeypatch.setattr(cf, "ROOT", tmp_path)
    monkeypatch.setattr(cf, "SHARED", shared)
    monkeypatch.setattr(cf, "PROTOCOLS", tmp_path / "protocols")
    monkeypatch.setattr(cf, "DEF", shared / "text" / "include"
                        / "ui_text.def")
    monkeypatch.setattr(cf, "UI_TEXT_H", shared / "ui" / "include"
                        / "ui_text.h")
    monkeypatch.setattr(cf, "LOOKUPS", tuple(lookups))
    monkeypatch.setattr(cf, "DIAL_LOOKUP", shared / "dial" / "dial_scene.c")
    return shared


def swap(path, old, new):
    """Replace one text of a file that holds it once."""
    text = path.read_text()
    assert text.count(old) == 1, old
    path.write_text(text.replace(old, new))


def test_formats_that_match_their_calls_pass(tree, capsys):
    """The tree as written: 5 files, no warning."""
    assert cf.main() == 0
    assert "5 files: every format matches its arguments" in \
        capsys.readouterr().out


def test_the_english_column_is_read_with_adjacent_literals(tree):
    """An entry over two lines gives its literals side by side."""
    assert cf.english("UI_TEXT", r"\s*\d+,") == [
        ("BF_LOST", '"lost %lu frames"'), ("TWO_PART", '"a %d " "b %d"')]
    assert cf.english("UI_SERVO_TEXT", "") == [
        ("SERVO_STR_STEP", '"step %d"')]
    assert '#define DFS_DIAL_PCT "%d%%"' in cf.literals_from_dial()
    assert '#define SVS_STEP "step %d"' in cf.literals_from_report()
    assert '#define TXS_BF_LOST "lost %lu frames"' in cf.literals_from_def()


@pytest.mark.parametrize("file, old, new, said", [
    ("text/include/ui_text.def", '"lost %lu frames"', '"lost %s frames"',
     "screen.c"),
    ("text/include/ui_text.def", '"step %d"', '"step %d of %d"',
     "servo_report.c"),
    ("text/include/ui_text.def", '"%d%%", "%d%%"', '"%u%%", "%u%%"',
     "dial_scene.c"),
    ("ui/screen.c", "TR(BF_LOST), n", "TR(BF_LOST), a", "screen.c"),
    ("servo/servo_limrun_report.c", "LS(STEP), n", "LS(STEP)",
     "servo_limrun_report.c"),
])
def test_a_format_that_does_not_fit_its_call_fails(tree, capsys, file, old,
                                                   new, said):
    """A wrong conversion, a missing argument and a wrong sign fail."""
    swap(tree / file, old, new)
    assert cf.main() == 1
    captured = capsys.readouterr()
    assert "warning:" in captured.out and said in captured.out
    assert "a format string does not match its call" in captured.err


def test_a_format_that_is_no_literal_fails(tree, capsys):
    """A lookup past the tables leaves a format the compiler cannot read."""
    swap(tree / "ui" / "screen.c", "TR(BF_LOST), n", "ui_tr(0), n")
    assert cf.main() == 1
    assert "format not a string literal" in capsys.readouterr().out


@pytest.mark.parametrize("file, old", [
    ("ui/include/ui_text.h", "#define TR(id) ui_tr(TX_##id)"),
    ("servo/servo_report.c",
     "#define S(id) servo_str_in(c->text, SERVO_STR_##id)"),
    ("dial/dial_scene.c", "#define DF(id) dial_text(l, (id))"),
])
def test_a_lookup_macro_that_changed_stops_the_tool(tree, file, old):
    """The swap needs each macro as it is written: no silent pass."""
    swap(tree / file, old, old.replace("(id)", "(ident)", 1))
    with pytest.raises(SystemExit) as stop:
        cf.main()
    assert "is not defined as" in str(stop.value)


def test_a_table_without_entries_stops_the_tool(tree):
    """No UI_TEXT entry: nothing would be checked."""
    (tree / "text" / "include" / "ui_text.def").write_text(
        'UI_SERVO_TEXT(SERVO_STR_STEP, "step %d", "Schritt %d")\n'
        'UI_DIAL_TEXT(DIAL_PCT, "%d%%", "%d%%")\n')
    with pytest.raises(SystemExit) as stop:
        cf.main()
    assert "no UI_TEXT entries" in str(stop.value)


def test_mbedtls_headers_are_found_or_their_users_skipped(tmp_path):
    """A tree under RCBENCH_MBEDTLS_DIR or a host build's _deps is used.

    Without one, a file that includes an Mbed TLS or PSA header is set
    aside, and every other file is kept.
    """
    tree = tmp_path / "mbed"
    (tree / "tf-psa-crypto" / "include" / "mbedtls").mkdir(parents=True)
    (tree / "tf-psa-crypto" / "include" / "mbedtls" / "pk.h").write_text("")
    got = cf.mbedtls_includes(tmp_path, {"RCBENCH_MBEDTLS_DIR": str(tree)})
    assert got == [tree / "include", tree / "tf-psa-crypto" / "include",
                   tree / "tf-psa-crypto" / "drivers" / "builtin" / "include"]
    deps = tmp_path / "test" / "host" / "build" / "_deps" / "mbedtls-src"
    shutil.copytree(tree, deps)
    assert cf.mbedtls_includes(tmp_path, {})[0] == deps / "include"
    assert cf.mbedtls_includes(tmp_path / "none", {}) == []
    a = tmp_path / "a.c"
    a.write_text('#include "mbedtls/pk.h"\n')
    b = tmp_path / "b.c"
    b.write_text('#  include <psa/crypto.h>\n')
    c = tmp_path / "c.c"
    c.write_text('#include "fw_sig.h"\n')
    kept, skipped = cf.without_mbedtls([a, b, c])
    assert kept == [c] and skipped == [a, b]
