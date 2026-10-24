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
    print(os.environ.get("FAKE_TIDY_VERSION", "LLVM version 18.1.8"))
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

# The clang driver beside the stand-in clang-tidy: it reports a version and
# lists inputs as cc does, with __clang__ defined as clang defines it.
FAKE_CLANG = r'''#!/usr/bin/env python3
import os, sys
if sys.argv[1:] == ["--version"]:
    print(os.environ.get("FAKE_CLANG_VERSION", "clang version 18.1.8"))
    sys.exit(0)
if os.environ.get("FAKE_CLANG_RULE"):
    print(os.environ["FAKE_CLANG_RULE"])
    sys.exit(0)
os.execvp("cc", ["cc", "-D__clang__=1", *sys.argv[1:]])
'''


def make_tree(base, monkeypatch):
    """Builds a tree of three C files and a compile database.

    Two of the files include one header.  The stand-in clang-tidy logs to
    a file in base.

    Args:
        base: The directory to build it in.
        monkeypatch: Sets the stand-in's log path.

    Returns:
        The tree, the stand-in, its log and the cache directory.
    """
    cc = shutil.which("cc")
    if cc is None:
        skip_or_fail("cc not found")
    root = base / "tree"
    (root / "build").mkdir(parents=True)
    (root / "src").mkdir()
    (root / ".clang-tidy").write_text("Checks: '-*,fake-check'\n")
    (root / "src" / "common.h").write_text("#define COMMON 1\n")
    (root / "src" / "a.c").write_text('#include "common.h"\nint a;\n')
    (root / "src" / "b.c").write_text('#include "common.h"\nint b;\n')
    (root / "src" / "c.c").write_text("int c;\n")
    llvm = base / "llvm" / "bin"
    llvm.mkdir(parents=True)
    fake = llvm / "clang-tidy"
    fake.write_text(FAKE)
    fake.chmod(0o755)
    clang = llvm / "clang"
    clang.write_text(FAKE_CLANG)
    clang.chmod(0o755)
    log = base / "calls.log"
    monkeypatch.setenv("FAKE_TIDY_LOG", str(log))
    write_db(root, ["a.c", "b.c", "c.c"])
    return root, fake, log, base / "cache"


@pytest.fixture
def tree(tmp_path, monkeypatch):
    """The tree of make_tree in the case's directory.

    Args:
        tmp_path: The case's directory.
        monkeypatch: Sets the stand-in's log path.

    Returns:
        The tree, the stand-in, its log and the cache directory.
    """
    return make_tree(tmp_path, monkeypatch)


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
            "arguments": ["cc", flag, f"-I{root}/src", "-o", out, "-c",
                          str(src)],
            "file": str(src),
        })
    (build / "compile_commands.json").write_text(json.dumps(entries))


def run(tree, *names, prune=False, extra=()):
    """Runs the script over the tree.

    Args:
        tree: The fixture.
        *names: The files; all three when empty.
        prune: Passes --prune.
        extra: Further arguments for the script.

    Returns:
        The exit status, the output and the files the stand-in was
        given, in order.
    """
    root, fake, log, cache = tree
    if log.exists():
        log.unlink()
    argv = [sys.executable, TOOL, "--build", str(root / "build"),
            "--cache", str(cache), "--clang-tidy", str(fake),
            "--root", str(root), "-j", "1", *extra]
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
    monkeypatch.setenv("FAKE_TIDY_VERSION", "LLVM version 18.1.9")
    monkeypatch.setenv("FAKE_CLANG_VERSION", "clang version 18.1.9")
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


def test_a_tree_path_with_a_space_keeps_its_inputs(tmp_path, monkeypatch):
    """A tree path with a space keeps its inputs in the key.

    The compiler writes the space as a backslash and a space; the file's
    content is still hashed, so an edit runs it again.
    """
    base = tmp_path / "with space"
    base.mkdir()
    tree = make_tree(base, monkeypatch)
    root = tree[0]
    assert run(tree)[0] == 0
    assert run(tree)[2] == []
    (root / "src" / "c.c").write_text("int BAD;\n")
    status, _, calls = run(tree)
    assert status == 1
    assert calls == ["src/c.c"]
    (root / "src" / "common.h").write_text("#define COMMON 2\n")
    _, _, calls = run(tree)
    assert sorted(calls) == ["src/a.c", "src/b.c", "src/c.c"]


def test_an_unreadable_input_is_never_recorded(tree, monkeypatch):
    """A listed input that cannot be read leaves the file unrecorded."""
    monkeypatch.setenv("FAKE_CLANG_RULE", "c.o: /nonexistent/gone.h")
    run(tree)
    _, _, calls = run(tree)
    assert len(calls) == 3
    assert list(tree[3].glob("*.pass")) == []


def test_extra_clang_tidy_arguments_are_refused(tree):
    """An argument for clang-tidy is a usage error.

    Only the compile command decides what clang-tidy reads.
    """
    status, out, calls = run(tree, extra=("--extra-arg=-DX",))
    assert status == 2
    assert calls == []


def test_parse_deps_unescapes_paths():
    """A backslash-space, a backslash-# and $$ are one character each."""
    rule = ("a.o: /x\\ y/a.c /x\\ y/h\\#1.h \\\n"
            " /p$$q/r.h\n")
    assert tc.parse_deps(rule) == ["/x y/a.c", "/x y/h#1.h", "/p$q/r.h"]


@pytest.mark.parametrize("arg", ["@flags.rsp", "-specs=x.specs",
                                 "--specs=x.specs"])
def test_a_response_or_specs_file_is_never_recorded(tree, arg):
    """A response or specs file is read but not listed by cc -M.

    Its content cannot reach the key, so the file is not recorded.
    """
    root = tree[0]
    (root / "build" / "flags.rsp").write_text("-DX=1\n")
    (root / "build" / "x.specs").write_text("")
    entries = json.loads((root / "build" / "compile_commands.json")
                         .read_text())
    entries[2]["arguments"].insert(1, arg)
    (root / "build" / "compile_commands.json").write_text(
        json.dumps(entries))
    run(tree)
    _, _, calls = run(tree)
    assert calls == ["src/c.c"]


def test_unlisted_input_names_only_those_arguments():
    """An include path or a define is listed through cc -M already."""
    assert tc.unlisted_input("@a.rsp")
    assert tc.unlisted_input("-specs=a")
    assert not tc.unlisted_input("-I/x")
    assert not tc.unlisted_input("-DA=@")


def test_a_header_only_clang_reads_is_in_the_key(tree):
    """A header included under __clang__ is listed by clang, not by cc.

    Its change runs the file again.
    """
    root = tree[0]
    (root / "src" / "only_clang.h").write_text("#define ONLY 1\n")
    (root / "src" / "c.c").write_text(
        '#ifdef __clang__\n#include "only_clang.h"\n#endif\nint c;\n')
    assert run(tree)[0] == 0
    assert run(tree)[2] == []
    (root / "src" / "only_clang.h").write_text("#define ONLY BAD\n")
    status, _, calls = run(tree)
    assert status == 1
    assert calls == ["src/c.c"]


def test_no_clang_beside_clang_tidy_records_nothing(tree):
    """Without a clang in clang-tidy's directory every file runs."""
    fake = tree[1]
    (fake.parent / "clang").unlink()
    status, out, calls = run(tree)
    assert status == 0
    assert "no clang beside" in out
    assert len(calls) == 3
    assert run(tree)[2] != []
    assert list(tree[3].glob("*.pass")) == []


def test_a_clang_of_another_version_records_nothing(tree, monkeypatch):
    """A clang whose version differs from clang-tidy's lists nothing."""
    monkeypatch.setenv("FAKE_CLANG_VERSION", "clang version 17.0.6")
    _, out, calls = run(tree)
    assert "is version 17.0.6, clang-tidy is version 18.1.8" in out
    assert len(calls) == 3
    assert len(run(tree)[2]) == 3


def test_clang_is_found_beside_the_real_clang_tidy(tree, tmp_path):
    """A clang-tidy reached through a symbolic link names its own install."""
    root, fake, log, cache = tree
    link_dir = tmp_path / "usr-bin"
    link_dir.mkdir()
    link = link_dir / "clang-tidy"
    link.symlink_to(fake)
    clang, why = tc.find_clang(str(link))
    assert clang == str(fake.parent / "clang")
    assert why == ""
    assert run((root, link, log, cache))[0] == 0
    assert run((root, link, log, cache))[2] == []


def test_a_missing_clang_tidy_has_no_clang():
    """A clang-tidy that is not on the path has no install to name."""
    clang, why = tc.find_clang("no-such-clang-tidy-0")
    assert clang is None
    assert "not found" in why


@pytest.mark.parametrize("text, version", [
    ("LLVM (http://llvm.org/):\n  LLVM version 18.1.3\n", "18.1.3"),
    ("Ubuntu clang version 18.1.3 (1ubuntu1)\n", "18.1.3"),
    ("clang version 22.1.8\n", "22.1.8"),
    ("no number here\n", None),
])
def test_llvm_version_reads_the_number(text, version):
    """The version number is read from either tool's --version text."""
    assert tc.llvm_version(text) == version


def test_deps_args_puts_clang_first():
    """The listing runs the given clang in place of the compiler."""
    assert tc.deps_args(["cc", "-DX", "-c", "a.c"], "/l/bin/clang") == [
        "/l/bin/clang", "-DX", "a.c", "-M", "-MF", "/dev/stdout"]
