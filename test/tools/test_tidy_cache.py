"""tidy_cache.py: a recorded pass skips a file until an input changes.

A header change reaches every file that includes it.

The cases run the script over a small tree with the host compiler and a
stand-in clang-tidy, which logs each file it is given and fails a file
whose text, or the text of a header it includes, holds the word BAD.

SPDX-License-Identifier: MIT
"""

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

import tidy_cache as tc
from conftest import skip_or_fail

TOOL = tc.__file__

FAKE = r'''#!/usr/bin/env python3
import json, os, re, sys
args = sys.argv[1:]
if args == ["--version"]:
    print(os.environ.get("FAKE_TIDY_VERSION", "fake clang-tidy 1"))
    sys.exit(0)
db = args[args.index("-p") + 1]
name = args[-1]
entries = [e for e in json.load(open(os.path.join(db,
           "compile_commands.json")))
           if os.path.realpath(e["file"]) == os.path.realpath(name)]
with open(os.environ["FAKE_TIDY_LOG"], "a") as log:
    log.write(f"{name} {len(entries)}\n")
text = open(name).read()
for inc in re.findall(r'#include "([^"]+)"', text):
    text += open(os.path.join(os.path.dirname(name), inc)).read()
if "BAD" in text:
    print(f"{name}:1:1: error: BAD [fake-check]")
    sys.exit(1)
print("3 warnings generated.", file=sys.stderr)
'''


@pytest.fixture
def tree(tmp_path, monkeypatch):
    """Builds a tree of three C files and a compile database.

    Two of the files include one header.  The stand-in clang-tidy logs to
    a file in tmp_path.

    Args:
        tmp_path: The case's directory.
        monkeypatch: Sets the stand-in's log path.

    Returns:
        The tree, the stand-in, its log and the cache directory.
    """
    cc = shutil.which("cc")
    if cc is None:
        skip_or_fail("cc not found")
    root = tmp_path / "tree"
    (root / "build").mkdir(parents=True)
    (root / "src").mkdir()
    (root / ".clang-tidy").write_text("Checks: '-*,fake-check'\n")
    (root / "src" / "common.h").write_text("#define COMMON 1\n")
    (root / "src" / "a.c").write_text('#include "common.h"\nint a;\n')
    (root / "src" / "b.c").write_text('#include "common.h"\nint b;\n')
    (root / "src" / "c.c").write_text("int c;\n")
    fake = tmp_path / "fake-tidy"
    fake.write_text(FAKE)
    fake.chmod(0o755)
    log = tmp_path / "calls.log"
    monkeypatch.setenv("FAKE_TIDY_LOG", str(log))
    write_db(root, ["a.c", "b.c", "c.c"])
    return root, fake, log, tmp_path / "cache"


def write_db(root, names, extra=()):
    """Writes compile_commands.json.

    Args:
        root: The tree.
        names: Files that get one entry each with -DX=1.
        extra: Further entries as (name, flag, output) triples.
    """
    build = root / "build"
    entries = []
    for name, flag, out in [(n, "-DX=1", n + ".o") for n in names] + \
            list(extra):
        src = root / "src" / name
        entries.append({
            "directory": str(build),
            "command": f"cc {flag} -I{root}/src -o {out} -c {src}",
            "file": str(src),
        })
    (build / "compile_commands.json").write_text(json.dumps(entries))


def run(tree, *names, prune=False):
    """Runs the script over the tree.

    Args:
        tree: The fixture.
        *names: The files; all three when empty.
        prune: Passes --prune.

    Returns:
        The exit status, the output and the files the stand-in was
        given, in order.
    """
    root, fake, log, cache = tree
    if log.exists():
        log.unlink()
    argv = [sys.executable, TOOL, "--build", str(root / "build"),
            "--cache", str(cache), "--clang-tidy", str(fake),
            "--root", str(root), "-j", "1"]
    if prune:
        argv.append("--prune")
    files = names or ("src/a.c", "src/b.c", "src/c.c")
    done = subprocess.run(argv + list(files), cwd=root, capture_output=True,
                          text=True, check=False)
    calls = log.read_text().split("\n") if log.exists() else []
    return (done.returncode, done.stdout + done.stderr,
            [c.split()[0] for c in calls if c])


def test_a_warm_run_checks_nothing(tree):
    """A second run over an unchanged tree runs clang-tidy on no file."""
    status, _, calls = run(tree)
    assert status == 0
    assert sorted(calls) == ["src/a.c", "src/b.c", "src/c.c"]
    status, out, calls = run(tree)
    assert status == 0
    assert calls == []
    assert "3 files, 3 unchanged, 0 to check" in out


def test_the_count_line_is_not_echoed(tree):
    """The "N warnings generated." line carries no finding and is dropped."""
    _, out, _ = run(tree)
    assert "warnings generated" not in out


def test_a_changed_file_runs_alone(tree):
    """A changed source file runs; the others stay recorded."""
    root = tree[0]
    run(tree)
    (root / "src" / "c.c").write_text("int c2;\n")
    status, _, calls = run(tree)
    assert status == 0
    assert calls == ["src/c.c"]


def test_a_defect_in_a_changed_file_fails(tree):
    """A defect in a changed file fails the call and names the file."""
    root = tree[0]
    run(tree)
    (root / "src" / "c.c").write_text("int BAD;\n")
    status, out, calls = run(tree)
    assert status == 1
    assert calls == ["src/c.c"]
    assert "src/c.c:1:1: error: BAD" in out
    assert "FAILED src/c.c" in out


def test_a_header_change_reruns_every_includer(tree):
    """A header change runs every file that includes it, and only those."""
    root = tree[0]
    run(tree)
    (root / "src" / "common.h").write_text("#define COMMON BAD\n")
    status, _, calls = run(tree)
    assert status == 1
    assert sorted(calls) == ["src/a.c", "src/b.c"]


def test_a_comment_in_a_header_is_a_change(tree):
    """A comment-only header change runs its includers; NOLINT is a comment."""
    # The preprocessor drops comments; a NOLINT is one.
    root = tree[0]
    run(tree)
    (root / "src" / "common.h").write_text(
        "#define COMMON 1 /* NOLINT */\n")
    _, _, calls = run(tree)
    assert sorted(calls) == ["src/a.c", "src/b.c"]


def test_a_failure_is_not_recorded(tree):
    """A failing file runs on every call until it passes."""
    root = tree[0]
    (root / "src" / "c.c").write_text("int BAD;\n")
    assert run(tree)[0] == 1
    status, _, calls = run(tree)
    assert status == 1
    assert calls == ["src/c.c"]
    (root / "src" / "c.c").write_text("int c;\n")
    status, _, calls = run(tree)
    assert status == 0
    assert calls == ["src/c.c"]


def test_a_config_change_reruns_everything(tree):
    """A new .clang-tidy anywhere in the tree runs every file."""
    root = tree[0]
    run(tree)
    sub = root / "src" / ".clang-tidy"
    sub.write_text("InheritParentConfig: true\n")
    _, _, calls = run(tree)
    assert len(calls) == 3


def test_a_new_clang_tidy_reruns_everything(tree, monkeypatch):
    """A different clang-tidy version runs every file."""
    run(tree)
    monkeypatch.setenv("FAKE_TIDY_VERSION", "fake clang-tidy 2")
    _, _, calls = run(tree)
    assert len(calls) == 3


def test_a_changed_flag_reruns_the_file(tree):
    """A changed compile flag runs that file."""
    root = tree[0]
    run(tree)
    write_db(root, ["a.c", "b.c"], [("c.c", "-DX=2", "c.c.o")])
    _, _, calls = run(tree)
    assert calls == ["src/c.c"]


def test_same_commands_collapse_distinct_ones_stay(tree):
    """Entries that differ only in the output file collapse to one."""
    root, _, log, _ = tree
    write_db(root, ["b.c", "c.c"],
             [("a.c", "-DX=1", "one.o"), ("a.c", "-DX=1", "two.o"),
              ("a.c", "-DX=1", "three.o"), ("a.c", "-DX=2", "four.o")])
    run(tree, "src/a.c")
    # The stand-in sees two entries for a.c: one per distinct command.
    assert log.read_text().split() == ["src/a.c", "2"]


def test_an_unlisted_file_is_a_usage_error(tree):
    """A file without a compile command exits 2 and runs nothing."""
    root = tree[0]
    (root / "src" / "d.c").write_text("int d;\n")
    status, out, calls = run(tree, "src/d.c")
    assert status == 2
    assert "src/d.c: no compile command" in out
    assert calls == []


def test_a_file_with_a_missing_header_always_runs(tree):
    """A file whose inputs cannot be listed is never recorded."""
    root = tree[0]
    (root / "src" / "c.c").write_text('#include "gone.h"\nint c;\n')
    write_db(root, ["a.c", "b.c", "c.c"])
    run(tree)
    _, _, calls = run(tree)
    assert calls == ["src/c.c"]


def test_prune_keeps_only_this_calls_records(tree):
    """--prune deletes the records no file of the call matched."""
    root, _, _, cache = tree
    run(tree)
    (root / "src" / "c.c").write_text("int c2;\n")
    run(tree)
    assert len(list(cache.glob("*.pass"))) == 4
    run(tree, prune=True)
    assert len(list(cache.glob("*.pass"))) == 3
    _, _, calls = run(tree)
    assert calls == []


def test_the_slowest_file_starts_first(tree):
    """Files run in the order of their recorded time, slowest first."""
    root, _, _, cache = tree
    run(tree)
    times = {"src/a.c": 1.0, "src/b.c": 9.0, "src/c.c": 5.0}
    (cache / "times.json").write_text(json.dumps(times))
    for rec in cache.glob("*.pass"):
        rec.unlink()
    (root / "src" / "a.c").write_text('#include "common.h"\nint a2;\n')
    _, _, calls = run(tree)
    assert calls == ["src/b.c", "src/c.c", "src/a.c"]


def test_an_unreadable_times_record_is_ignored(tree):
    """A broken times.json is read as empty."""
    cache = tree[3]
    cache.mkdir()
    (cache / "times.json").write_text("not json")
    status, _, calls = run(tree)
    assert status == 0
    assert len(calls) == 3


def test_parse_deps_reads_continued_lines():
    """The make rule of cc -M is read across continued lines."""
    rule = "a.o: /x/a.c \\\n /x/common.h \\\n /usr/include/stdio.h\n"
    assert tc.parse_deps(rule) == ["/x/a.c", "/x/common.h",
                                   "/usr/include/stdio.h"]


def test_deps_args_drops_the_output():
    """The dependency command has no -o and no -c."""
    args = ["cc", "-DX", "-o", "a.o", "-c", "a.c"]
    assert tc.deps_args(args) == ["cc", "-DX", "a.c", "-M", "-MF",
                                  "/dev/stdout"]


def test_quiet_drops_only_count_lines():
    """Only the count lines are dropped from the output."""
    text = ("12 warnings generated.\n"
            "a.c:1:1: error: x [y]\n"
            "1 warning and 2 errors generated.\n")
    assert tc.quiet(text) == "a.c:1:1: error: x [y]\n"


def test_the_script_runs_as_a_program():
    """The script is executable and answers --help."""
    done = subprocess.run([sys.executable, TOOL, "--help"],
                          capture_output=True, text=True, check=False)
    assert done.returncode == 0
    assert os.path.basename(TOOL) == "tidy_cache.py"
    assert Path(TOOL).stat().st_mode & 0o100
