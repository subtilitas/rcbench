"""doc_check.py: Doxygen's warnings against the baseline.

SPDX-License-Identifier: MIT
"""

import collections
import os
import pathlib
import re
import shutil
import subprocess

import pytest

import doc_check as dc
from conftest import skip_or_fail

REPO = pathlib.Path(__file__).resolve().parent.parent.parent

DOCUMENTED_H = """\
/**
 * @file
 * @brief A documented header.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

/**
 * @brief Double a value.
 * @param a The value.
 * @return Twice a.
 */
int twice(int a);
"""

OLD_GAP_H = """\
/**
 * @file
 * @brief A header with an old gap.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

int old_gap(int a);

/**
 * @brief Double a value.
 * @param a The value.
 * @return Twice a.
 */
int twice(int a);
"""

UNDOCUMENTED_FN_H = """\
/**
 * @file
 * @brief A header with one undocumented public function.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

int undocumented(int a);
"""


def git(root, *args):
    """Run git in a scratch repository.

    Args:
        root: The repository.
        *args: The git arguments.

    Returns:
        Standard output.
    """
    return subprocess.run(
        ["git", "-c", "user.name=t", "-c", "user.email=t@t", *args],
        cwd=root, check=True, capture_output=True, text=True).stdout


def scratch_repo(tmp_path, files):
    """Make a git repository with the repo's Doxyfile reading src/ only.

    Args:
        tmp_path: The directory to make it in.
        files: Paths under src/ mapped to their text; committed as the base.

    Returns:
        The repository root.
    """
    root = tmp_path / "repo"
    (root / "src").mkdir(parents=True)
    (root / "tools").mkdir()
    (root / "Doxyfile").write_text(
        (REPO / "Doxyfile").read_text(encoding="utf-8")
        + "\nINPUT = src\n", encoding="utf-8")
    for name, text in files.items():
        (root / "src" / name).write_text(text, encoding="utf-8")
    git(root, "init", "-q", "-b", "base")
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "base")
    return root


def W(file, message, line=1):
    """Make a warning.

    Args:
        file: The file.
        message: The message.
        line: The line.

    Returns:
        The warning.
    """
    return dc.DocWarning(file, line, message)


# --- the log -----------------------------------------------------------------

def test_a_log_line_becomes_file_line_and_message_relative_to_the_root():
    """The root is stripped from the path and the line kept apart."""
    log = ("/r/shared/a.h:12: warning: Member f(int a) (function) of file "
           "a.h is not documented.\n")
    assert dc.parse_log(log, pathlib.Path("/r")) == [
        W("shared/a.h", "warning: Member f(int a) (function) of file a.h is "
          "not documented.", 12)]


def test_an_indented_line_continues_the_warning_above():
    """A multi-line warning becomes one message."""
    log = ("/r/a.c:3: warning: The following parameter of f(int a, int b) "
           "is not documented:\n  parameter 'b'\n")
    [warning] = dc.parse_log(log, pathlib.Path("/r"))
    assert warning.message.endswith("is not documented: parameter 'b'")


def test_a_warning_without_a_file_keeps_an_empty_file():
    """A global warning is still counted."""
    [warning] = dc.parse_log("warning: tag FOO is obsolete\n",
                             pathlib.Path("/r"))
    assert warning == W("", "warning: tag FOO is obsolete", 0)


def test_the_baseline_key_has_no_line_number():
    """Code that moves inside a file keeps its baseline line."""
    a = W("a.h", "warning: x", 10)
    b = W("a.h", "warning: x", 99)
    assert a.key == b.key == "a.h: warning: x"


def test_a_line_naming_an_absolute_path_is_machine_bound():
    """A checkout's place in the baseline fails on every other machine."""
    root = pathlib.Path("/w/rcbench")
    lines = [
        "a.h: warning: x",
        "a.h: warning: end of comment block (Probable start "
        "'/home/u/rcbench/a.h' at line 40)",
        "b.h: warning: found in /w/rcbench/b.h",
    ]
    assert dc.machine_bound(lines, root) == lines[1:]


# --- the comparison ----------------------------------------------------------

def test_a_warning_moved_to_another_line_passes():
    """The baseline holds file and message, not the line."""
    baseline = collections.Counter(["a.h: warning: x"])
    result = dc.compare([W("a.h", "warning: x", 40)], baseline, [])
    assert not result.failed


def test_a_warning_not_in_the_baseline_fails():
    """A new gap is new."""
    result = dc.compare([W("a.h", "warning: y")], collections.Counter(),
                        [])
    assert result.new == [W("a.h", "warning: y")]
    assert result.failed


def test_a_second_copy_of_a_baselined_warning_fails():
    """The baseline counts: one entry covers one occurrence."""
    baseline = collections.Counter(["a.h: warning: x"])
    result = dc.compare([W("a.h", "warning: x"), W("a.h", "warning: x", 5)],
                        baseline, [])
    assert result.new == [W("a.h", "warning: x", 5)]


def test_a_baseline_line_that_no_longer_occurs_fails():
    """A fixed gap is taken out of the baseline with --update."""
    baseline = collections.Counter(["a.h: warning: x"])
    result = dc.compare([], baseline, [])
    assert result.stale == ["a.h: warning: x"]
    assert "--update" in dc.report(result)


def test_a_touched_warning_fails_although_the_baseline_holds_it():
    """New or changed code is held to the rule, baseline or not."""
    baseline = collections.Counter(["a.h: warning: x", "b.h: warning: x"])
    result = dc.compare([W("a.h", "warning: x"), W("b.h", "warning: x")],
                        baseline, [W("a.h", "warning: x")])
    assert result.touched == [W("a.h", "warning: x")]
    assert result.new == []
    assert "a.h:1: warning: x" in dc.report(result)


# --- what a change touches ---------------------------------------------------

def test_the_hunks_are_the_new_sides_line_ranges():
    """A hunk's range is the new side's lines it adds or rewrites.

    A deletion covers the line before the cut.
    """
    diff = ("diff --git a/x.h b/x.h\n--- a/x.h\n+++ b/x.h\n"
            "@@ -3 +3 @@\n-a\n+b\n"
            "@@ -10,0 +11,4 @@\n+c\n"
            "@@ -20,2 +24,0 @@\n-d\n-e\n"
            "diff --git a/y.c b/y.c\n--- a/y.c\n+++ /dev/null\n"
            "@@ -1 +0,0 @@\n-f\n")
    assert dc.parse_hunks(diff) == {"x.h": [(3, 3), (11, 14), (24, 24)]}


SPAN_H = """\
/** @file
 *  @brief Spans. */

int undocumented(int a);

/**
 * @brief Documented, parameters missing.
 */
int partly(int a);

int far_below(int a);
"""


@pytest.mark.parametrize("line, span", [
    (4, (4, 4)),    # an undocumented declaration: its line
    (6, (6, 9)),    # a block start: the block and the declaration
    (9, (6, 9)),    # a declaration under a block: the block too
    (11, (11, 11)),
])
def test_a_warning_spans_its_line_and_the_comment_block_next_to_it(
        line, span):
    """The span of a warning is the reported line and its comment block."""
    lines = SPAN_H.splitlines()
    assert dc.warning_span(W("s.h", "warning: x", line), lines) == span


def test_a_changed_line_selects_the_warnings_it_touches(tmp_path):
    """A change selects the warnings of the lines it touches.

    A new file fails whole; a changed file by the lines it changes and by
    a missing @file block.
    """
    (tmp_path / "s.h").write_text(SPAN_H, encoding="utf-8")
    (tmp_path / "bare.h").write_text("int b;\n", encoding="utf-8")
    warnings = [W("s.h", "warning: undocumented", 4),
                W("s.h", "warning: partly", 6),
                W("s.h", "warning: far_below", 11),
                W("bare.h", dc.NO_FILE_BLOCK, 1),
                W("n.h", "warning: new", 30),
                W("other.h", "warning: untouched", 2)]
    diff = dc.Changes({"n.h"}, {"s.h": [(7, 7)], "bare.h": [(9, 9)]})
    assert dc.select_touched(warnings, diff, tmp_path) == [
        W("s.h", "warning: partly", 6),
        W("bare.h", dc.NO_FILE_BLOCK, 1),
        W("n.h", "warning: new", 30)]


# --- the static helpers ------------------------------------------------------

HELPERS_C = """\
/** @file
 *  @brief Helpers. */

/**
 * @brief A private helper.
 */
static int
helper(int a)
{
    return a;
}

/**
 * @brief A public function.
 */
int public_fn(int a)
{
    return helper(a);
}
"""


@pytest.mark.parametrize("message, line, dropped", [
    ("warning: parameters of member helper are not documented", 4, True),
    ("warning: return type of member helper is not documented", 4, True),
    ("warning: parameters of member public_fn are not documented", 13,
     False),
    ("warning: Member helper(int a) (function) of file h.c is not "
     "documented.", 4, False),
])
def test_a_static_helper_needs_a_brief_only(tmp_path, message, line,
                                            dropped):
    """A static helper in a .c file needs a brief only.

    Its parameters and return may go undocumented; its brief, and a public
    function's parameters, may not.
    """
    (tmp_path / "h.c").write_text(HELPERS_C, encoding="utf-8")
    assert dc.is_helper_noise(W("h.c", message, line), tmp_path) is dropped


def test_a_static_inline_function_in_a_header_needs_its_parameters(
        tmp_path):
    """The helper rule is for .c files; a header's function is public."""
    (tmp_path / "h.h").write_text("/** @brief B. */\nstatic inline int "
                                  "f(int a) { return a; }\n",
                                  encoding="utf-8")
    message = "warning: parameters of member f are not documented"
    assert not dc.is_helper_noise(W("h.h", message, 1), tmp_path)


# --- the @file block ---------------------------------------------------------

@pytest.mark.parametrize("text, expected", [
    ("/**\n * @file\n * @brief B.\n */\nint x;\n", []),
    ("/*!\n * \\file\n * \\brief B.\n */\n", []),
    ("/* @file\n * @brief Not a doc comment.\n */\n", [dc.NO_FILE_BLOCK]),
    ("int x;\n", [dc.NO_FILE_BLOCK]),
    ("/**\n * @file\n * No brief.\n */\n", [dc.NO_FILE_BRIEF]),
])
def test_every_file_has_a_file_block_with_a_brief(tmp_path, text, expected):
    """A file without @file, or without its brief, warns.

    Doxygen documents nothing of such a file and says so nowhere.
    """
    (tmp_path / "f.h").write_text(text, encoding="utf-8")
    found = dc.file_block_warnings(tmp_path, ["f.h"])
    assert [w.message for w in found] == expected


# --- the Doxyfile ------------------------------------------------------------

def test_the_doxyfile_reader_joins_continued_lines_and_appends(tmp_path):
    """Continuations, += and comment lines read as Doxygen reads them."""
    (tmp_path / "Doxyfile").write_text(
        '# INPUT = no\nINPUT = a \\\n        b\nINPUT += c\n'
        'PROJECT_BRIEF = "two words"\n', encoding="utf-8")
    settings = dc.read_doxyfile(tmp_path / "Doxyfile")
    assert settings["INPUT"] == ["a", "b", "c"]
    assert settings["PROJECT_BRIEF"] == ["two words"]


@pytest.mark.parametrize("rel, expected", [
    ("shared/safety/include/arming.h", True),
    ("shared/safety/arming.c", True),
    ("protocols/kst/rp2350/kst_pio.c", True),
    ("firmware/panel/main/main.c", True),
    ("firmware/iomcu/src/main.c", True),
    ("testbench/touch-injector/core/injector.c", True),
    ("testbench/touch-injector/rp2350/main.c", False),
    ("shared/gfx/gfx_font8x16.c", False),
    ("shared/esc/esc_profiles_gen.c", False),
    ("firmware/iomcu/src/board_drawing_rp2350_can.c", False),
    ("firmware/panel/managed_components/x/y.c", False),
    ("firmware/panel/build/config/sdkconfig.h", False),
    ("firmware/iomcu/build-x/a.c", False),
    ("test/host/test_arming.c", False),
    ("shared/safety/README.md", False),
])
def test_the_doxyfile_reads_the_sources_and_leaves_out_generated_ones(
        rel, expected):
    """The repo's Doxyfile reads the C sources and no generated file.

    Read: shared/, protocols/, both firmwares and the injector core.  Left
    out: generated tables, build trees and the host tests.
    """
    settings = dc.read_doxyfile(REPO / "Doxyfile")
    assert dc.in_scope(rel, settings) is expected


def test_the_doxyfile_leaves_the_settings_to_the_check():
    """The Doxyfile leaves pass and fail to doc_check.py.

    Briefs are explicit, undocumented items warn, Doxygen's exit code
    stays 0, and the output is HTML only.
    """
    settings = dc.read_doxyfile(REPO / "Doxyfile")
    expected = {"JAVADOC_AUTOBRIEF": "NO", "EXTRACT_ALL": "NO",
                "WARN_IF_UNDOCUMENTED": "YES", "WARN_NO_PARAMDOC": "YES",
                "WARN_AS_ERROR": "NO", "GENERATE_HTML": "YES",
                "GENERATE_LATEX": "NO", "HAVE_DOT": "NO"}
    assert {k: settings[k][0] for k in expected} == expected


# --- the pins ----------------------------------------------------------------

SHA_RE = r"DOXYGEN_SHA256:\s*(?:>-\s*)?([0-9a-f]{64})"


@pytest.mark.parametrize("workflow", ["ci.yml", "pages.yml"])
def test_the_workflows_install_the_release_the_baseline_is_written_by(
        workflow):
    """The workflows pin the Doxygen release of the baseline.

    Another release changes the warnings; ci.yml and pages.yml pin the same
    release and hash.
    """
    text = (REPO / ".github" / "workflows" / workflow).read_text(
        encoding="utf-8")
    versions = set(re.findall(r"DOXYGEN_VERSION: ([\d.]+)", text))
    hashes = set(re.findall(SHA_RE, text))
    ci = (REPO / ".github" / "workflows" / "ci.yml").read_text(
        encoding="utf-8")
    assert versions == {dc.DOXYGEN_VERSION}
    assert hashes == set(re.findall(SHA_RE, ci))
    assert len(hashes) == 1


def test_another_doxygen_release_stops_the_check(tmp_path):
    """The baseline is one release's; another one exits 2 before running."""
    fake = tmp_path / "doxygen"
    fake.write_text("#!/bin/sh\necho 1.9.8\n", encoding="utf-8")
    fake.chmod(0o755)
    assert dc.main(["--check", "--doxygen", str(fake)]) == 2


def test_a_missing_doxygen_stops_the_check(tmp_path):
    """No binary, exit 2 with the install hint."""
    missing = str(tmp_path / "nothing")
    assert dc.main(["--check", "--doxygen", missing]) == 2


# --- a log through main ------------------------------------------------------

def test_update_then_check_passes_and_a_new_warning_fails(tmp_path):
    """--update writes what --check then accepts; one more warning fails."""
    root = scratch_repo(tmp_path, {"a.h": DOCUMENTED_H})
    log = tmp_path / "w.log"
    log.write_text(f"{root}/src/a.h:3: warning: x\n", encoding="utf-8")
    args = ["--root", str(root), "--log", str(log)]
    assert dc.main(["--update", *args]) == 0
    baseline = (root / "tools" / "doc_check_baseline.txt").read_text()
    assert "src/a.h: warning: x\n" in baseline
    assert dc.main(["--check", *args]) == 0
    log.write_text(f"{root}/src/a.h:3: warning: x\n"
                   f"{root}/src/a.h:9: warning: y\n", encoding="utf-8")
    assert dc.main(["--check", *args]) == 1


def test_a_change_fails_on_the_warnings_of_its_lines_only(tmp_path, capsys):
    """With --base, a warning on a changed line fails, baseline or not.

    A baselined warning elsewhere in the same file does not.
    """
    root = scratch_repo(tmp_path, {"a.h": DOCUMENTED_H})
    log = tmp_path / "w.log"
    log.write_text(f"{root}/src/a.h:14: warning: x\n", encoding="utf-8")
    args = ["--root", str(root), "--log", str(log)]
    assert dc.main(["--update", *args]) == 0
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "baseline")
    git(root, "checkout", "-q", "-b", "topic")
    assert dc.main(["--check", "--base", "base", *args]) == 0
    with open(root / "src" / "a.h", "a", encoding="utf-8") as f:
        f.write("\n")
    assert dc.main(["--check", "--base", "base", *args]) == 0
    text = (root / "src" / "a.h").read_text(encoding="utf-8")
    (root / "src" / "a.h").write_text(
        text.replace("int twice(int a);", "int twice(int b);"),
        encoding="utf-8")
    capsys.readouterr()
    assert dc.main(["--check", "--base", "base", *args]) == 1
    assert "src/a.h:14: warning: x" in capsys.readouterr().out


# --- Doxygen itself ----------------------------------------------------------

def doxygen_or_skip():
    """Find the pinned Doxygen release.

    Returns:
        The binary: $DOXYGEN, else doxygen on PATH.
    """
    binary = os.environ.get("DOXYGEN") or shutil.which("doxygen")
    if not binary:
        skip_or_fail("doxygen is not on PATH")
    if dc.doxygen_version(binary) != dc.DOXYGEN_VERSION:
        skip_or_fail(f"doxygen on PATH is not {dc.DOXYGEN_VERSION}")
    return binary


@pytest.mark.parametrize("with_base", [False, True])
def test_a_new_file_with_an_undocumented_public_function_fails(
        tmp_path, capsys, with_base, no_hang):
    """A new header with an undocumented public function fails.

    Doxygen runs on a tree whose baseline is clean.  The check fails by the
    baseline and by --base alike, and names the function.
    """
    binary = doxygen_or_skip()
    root = scratch_repo(tmp_path, {"a.h": DOCUMENTED_H})
    args = ["--root", str(root), "--doxygen", binary,
            "--out", str(tmp_path / "out")]
    base = ["--base", "base"] if with_base else []
    assert dc.main(["--update", *args]) == 0
    assert dc.main(["--check", *base, *args]) == 0
    (root / "src" / "new.h").write_text(UNDOCUMENTED_FN_H, encoding="utf-8")
    capsys.readouterr()
    assert dc.main(["--check", *base, *args]) == 1
    out = capsys.readouterr().out
    assert "src/new.h:9: warning: Member undocumented(int a) (function) of " \
        "file new.h is not documented." in out


def test_a_new_file_without_a_file_block_fails(tmp_path, capsys, no_hang):
    """A new file without @file fails.

    Doxygen warns about nothing in such a file; the check names the block.
    """
    binary = doxygen_or_skip()
    root = scratch_repo(tmp_path, {"a.h": DOCUMENTED_H})
    args = ["--root", str(root), "--doxygen", binary,
            "--out", str(tmp_path / "out")]
    assert dc.main(["--update", *args]) == 0
    (root / "src" / "bare.h").write_text("int bare(int a);\n",
                                         encoding="utf-8")
    capsys.readouterr()
    assert dc.main(["--check", *args]) == 1
    assert f"src/bare.h:1: {dc.NO_FILE_BLOCK}" in capsys.readouterr().out


@pytest.mark.parametrize("page", ["CONTRIBUTING.md", "docs/Building.md",
                                  "docs/Building-de.md", "STATUS.md",
                                  ".github/workflows/pages.yml"])
def test_the_pages_name_the_pinned_doxygen_release(page):
    """Every "Doxygen X.Y.Z" a page states is the pinned release."""
    text = (REPO / page).read_text(encoding="utf-8")
    stated = set(re.findall(r"Doxygen (\d+\.\d+\.\d+)", text))
    assert stated == {dc.DOXYGEN_VERSION}


def baselined_repo(tmp_path, binary):
    """Make a scratch repository whose baseline holds an old gap.

    Args:
        tmp_path: The directory to make it in.
        binary: The Doxygen binary.

    Returns:
        The repository root and the arguments for doc_check.main.
    """
    root = scratch_repo(tmp_path, {"old.h": OLD_GAP_H})
    args = ["--root", str(root), "--doxygen", binary,
            "--out", str(tmp_path / "out")]
    assert dc.main(["--update", *args]) == 0
    baseline = (root / "tools" / "doc_check_baseline.txt").read_text()
    assert "src/old.h: warning: Member old_gap(int a)" in baseline
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "baseline")
    git(root, "checkout", "-q", "-b", "topic")
    return root, args


def test_an_unrelated_change_in_a_file_with_old_gaps_passes(
        tmp_path, no_hang):
    """Doxygen run: a brief reworded in a file with an old gap passes."""
    binary = doxygen_or_skip()
    root, args = baselined_repo(tmp_path, binary)
    path = root / "src" / "old.h"
    path.write_text(path.read_text().replace(
        "@brief Double a value.", "@brief Double a value, wrapping."),
        encoding="utf-8")
    assert dc.main(["--check", "--base", "base", *args]) == 0


def test_an_undocumented_function_on_a_changed_line_fails(
        tmp_path, capsys, no_hang):
    """Doxygen run: an undocumented function added to a file fails.

    The file has an old gap; the new function is named, the gap is not.
    """
    binary = doxygen_or_skip()
    root, args = baselined_repo(tmp_path, binary)
    path = root / "src" / "old.h"
    path.write_text(path.read_text() + "\nint added(int a);\n",
                    encoding="utf-8")
    capsys.readouterr()
    assert dc.main(["--check", "--base", "base", *args]) == 1
    out = capsys.readouterr().out
    touched = out.split("not in the baseline")[0]
    assert "Member added(int a) (function)" in touched
    assert "old_gap" not in touched


# --- comment blocks without a brief ------------------------------------------

BLOCKS_H = """\
/** @file
 *  @brief Blocks. */

/** Counts, no brief. */
int counts(void);

typedef struct {
    int a; /**< A member's trailing block. */
} t;

/** @name Limits
 *  @{ */
#define LIMIT 1
/** @} */

/**
 * @brief Documented.
 */
int documented(void);
"""


@pytest.mark.parametrize("ranges, expected", [
    ([(4, 4)], [4]),      # the block without a brief, changed
    ([(5, 5)], []),       # the line below it only
    ([(8, 8)], []),       # a member's /**< block
    ([(11, 15)], []),     # the group blocks
    ([(17, 20)], []),     # a block with a brief
    ([(1, 2)], []),       # the @file block
])
def test_a_changed_block_without_a_brief_fails(tmp_path, ranges, expected):
    """A /** block on a changed line needs a @brief.

    A member's trailing block, a group block and the @file block are
    exempt.
    """
    (tmp_path / "Doxyfile").write_text("INPUT = src\nFILE_PATTERNS = *.h\n",
                                       encoding="utf-8")
    (tmp_path / "src").mkdir()
    (tmp_path / "src" / "b.h").write_text(BLOCKS_H, encoding="utf-8")
    settings = dc.read_doxyfile(tmp_path / "Doxyfile")
    diff = dc.Changes(set(), {"src/b.h": ranges})
    found = dc.block_brief_warnings(tmp_path, diff, settings)
    assert [w.line for w in found] == expected
    assert all(w.message == dc.NO_BLOCK_BRIEF for w in found)


def test_every_block_without_a_brief_in_a_new_file_fails(tmp_path):
    """A new file is held whole: the one block without a brief fails."""
    (tmp_path / "Doxyfile").write_text("INPUT = src\nFILE_PATTERNS = *.h\n",
                                       encoding="utf-8")
    (tmp_path / "src").mkdir()
    (tmp_path / "src" / "b.h").write_text(BLOCKS_H, encoding="utf-8")
    settings = dc.read_doxyfile(tmp_path / "Doxyfile")
    found = dc.block_brief_warnings(tmp_path, dc.Changes({"src/b.h"}, {}),
                                    settings)
    assert found == [W("src/b.h", dc.NO_BLOCK_BRIEF, 4)]


def test_a_block_without_a_brief_on_a_changed_line_fails_the_check(
        tmp_path, capsys):
    """Through main: a brief removed from a block fails with --base."""
    root = scratch_repo(tmp_path, {"a.h": DOCUMENTED_H})
    log = tmp_path / "w.log"
    log.write_text("", encoding="utf-8")
    args = ["--root", str(root), "--log", str(log)]
    assert dc.main(["--update", *args]) == 0
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "baseline")
    path = root / "src" / "a.h"
    path.write_text(path.read_text().replace(" * @brief Double a value.\n",
                                             " * Double a value.\n"),
                    encoding="utf-8")
    capsys.readouterr()
    assert dc.main(["--check", "--base", "base", *args]) == 1
    assert f"src/a.h:9: {dc.NO_BLOCK_BRIEF}" in capsys.readouterr().out


# --- git configuration -------------------------------------------------------

def test_the_users_diff_configuration_does_not_blind_the_check(
        tmp_path, monkeypatch):
    """diff.noprefix, diff.mnemonicPrefix and color.diff change nothing."""
    root = scratch_repo(tmp_path, {"a.h": DOCUMENTED_H})
    git(root, "checkout", "-q", "-b", "topic")
    path = root / "src" / "a.h"
    path.write_text(path.read_text().replace("int twice(int a);",
                                             "int twice(int b);"),
                    encoding="utf-8")
    (root / "src" / "n.h").write_text("int n;\n", encoding="utf-8")
    settings = {"diff.noprefix": "true", "diff.mnemonicPrefix": "true",
                "color.diff": "always", "color.ui": "always"}
    monkeypatch.setenv("GIT_CONFIG_COUNT", str(len(settings)))
    for index, (key, value) in enumerate(settings.items()):
        monkeypatch.setenv(f"GIT_CONFIG_KEY_{index}", key)
        monkeypatch.setenv(f"GIT_CONFIG_VALUE_{index}", value)
    diff = dc.changes(root, "base")
    assert diff.hunks == {"src/a.h": [(14, 14)]}
    assert diff.new == {"src/n.h"}


# --- build flags ----------------------------------------------------------

GUARDED_H = """\
/**
 * @file
 * @brief Declarations behind the build flags.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#ifdef ESP_PLATFORM
int on_the_panel(int a);
#endif

#if SENSE_TRACE
int traced(int a);
#endif
"""


def test_an_undocumented_function_behind_a_build_flag_fails(
        tmp_path, capsys, no_hang):
    """Doxygen run: a function behind a build flag is checked.

    The Doxyfile sets the flags, so a function behind #ifdef ESP_PLATFORM
    or #if SENSE_TRACE is read and warned about.
    """
    binary = doxygen_or_skip()
    root = scratch_repo(tmp_path, {"a.h": DOCUMENTED_H})
    args = ["--root", str(root), "--doxygen", binary,
            "--out", str(tmp_path / "out")]
    assert dc.main(["--update", *args]) == 0
    (root / "src" / "guarded.h").write_text(GUARDED_H, encoding="utf-8")
    capsys.readouterr()
    assert dc.main(["--check", *args]) == 1
    out = capsys.readouterr().out
    assert "Member on_the_panel(int a) (function)" in out
    assert "Member traced(int a) (function)" in out
