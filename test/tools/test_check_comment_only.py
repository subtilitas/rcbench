"""check_comment_only.py: a change between two commits is comments only.

Each case commits one file, changes it and runs the tool against the first
commit: a comment or docstring edit passes, a code edit, a comment the
compiler warns about, an added file and a file of another kind fail.

SPDX-License-Identifier: MIT
"""

import shutil
import subprocess

import pytest

import check_comment_only as cc
from conftest import skip_or_fail

C_BASE = """\
/* The limit. */
#define LIMIT 10
int over(int n)
{
    return n > LIMIT; // above
}
"""

PY_BASE = '''\
"""A module."""


def over(n):
    """Tell whether n is over."""
    # ten
    return n > 10
'''


@pytest.fixture
def repo(tmp_path, monkeypatch):
    """A repository with one C file and one Python file committed."""
    for tool in ("git", "gcc"):
        if shutil.which(tool) is None:
            skip_or_fail(f"{tool} is not on PATH")
    (tmp_path / "a.c").write_text(C_BASE)
    (tmp_path / "a.py").write_text(PY_BASE)
    (tmp_path / "notes.txt").write_text("one\n")

    def git(*args):
        subprocess.run(["git", "-c", "user.name=t", "-c",
                        "user.email=t@example.com", *args], cwd=tmp_path,
                       check=True, capture_output=True)

    git("init", "-q", "-b", "main")
    git("add", "-A")
    git("commit", "-q", "-m", "base")
    monkeypatch.chdir(tmp_path)
    return tmp_path, git


def run(monkeypatch, capsys, *args):
    """Run the tool with its arguments; return (exit status, output)."""
    monkeypatch.setattr("sys.argv", ["check_comment_only.py", *args])
    code = cc.main()
    return code, capsys.readouterr().out


def test_no_change_passes(repo, monkeypatch, capsys):
    """A tree equal to BASE has nothing to report."""
    code, out = run(monkeypatch, capsys, "HEAD")
    assert code == 0
    assert "0 file(s) changed, 0 failure(s)" in out


def test_a_c_comment_edit_passes(repo, monkeypatch, capsys):
    """New comment text, a new comment and other spacing are no code."""
    tmp, _ = repo
    (tmp / "a.c").write_text(
        C_BASE.replace("/* The limit. */", "/**\n * @brief The limit.\n */")
        .replace("// above", "/* over it */")
        .replace("int over(int n)", "/* new */\nint  over(int n)"))
    code, out = run(monkeypatch, capsys, "HEAD")
    assert code == 0, out
    assert "1 file(s) changed, 0 failure(s)" in out


@pytest.mark.parametrize("old, new", [
    ("n > LIMIT", "n >= LIMIT"),
    ("#define LIMIT 10", "#define LIMIT 11"),
    ("return n > LIMIT; // above", "// return n > LIMIT;\n    return 0;"),
])
def test_a_c_code_edit_fails(repo, monkeypatch, capsys, old, new):
    """A changed token, a changed #define and a commented-out line fail."""
    tmp, _ = repo
    assert old in C_BASE
    (tmp / "a.c").write_text(C_BASE.replace(old, new))
    code, out = run(monkeypatch, capsys, "HEAD")
    assert code == 1
    assert "a.c: code changed" in out


def test_a_nested_comment_opener_fails(repo, monkeypatch, capsys):
    """A /* inside a block comment leaves the tokens and fails -Wcomment."""
    tmp, _ = repo
    (tmp / "a.c").write_text(C_BASE.replace(
        "/* The limit. */", "/* The limit, /* nested. */"))
    code, out = run(monkeypatch, capsys, "HEAD")
    assert code == 1
    assert "a.c: a comment holds a nested /*" in out


def test_a_line_comment_that_takes_the_next_line_fails(repo, monkeypatch,
                                                       capsys):
    """A backslash at the end of a // comment is reported.

    The compiler joins the next line to the comment; the lexer run here
    returns the same tokens, so the comment count is what fails.
    """
    tmp, _ = repo
    (tmp / "a.c").write_text(C_BASE.replace("// above", "// above \\"))
    code, out = run(monkeypatch, capsys, "HEAD")
    assert code == 1
    assert "ends a line comment in a backslash" in out


def test_the_hazards_are_counted_outside_literals():
    """A /* inside a string is no nested comment."""
    assert cc.comment_hazards('const char *s = "/* /* */";\n') == 0
    assert cc.comment_hazards("/* a /* b */\n") == 1
    assert cc.comment_hazards("// a \\\nint x;\n") == 1
    assert cc.comment_hazards("// a\n/* b */\n") == 0


def test_a_docstring_and_comment_edit_passes(repo, monkeypatch, capsys):
    """Docstrings and # comments are not part of the compared tree."""
    tmp, _ = repo
    (tmp / "a.py").write_text(
        PY_BASE.replace('"""A module."""', '"""A module.\n\nMore.\n"""')
        .replace('"""Tell whether n is over."""',
                 '"""Tell whether n is over 10.\n\n    Args:\n        n: A '
                 'number.\n    """')
        .replace("# ten", "# the limit"))
    code, out = run(monkeypatch, capsys, "HEAD")
    assert code == 0, out


@pytest.mark.parametrize("old, new", [
    ("n > 10", "n > 11"),
    ('    """Tell whether n is over."""\n', ""),
    ("return n > 10", 'return n > 10 or "x" == "y"'),
])
def test_a_python_code_edit_fails(repo, monkeypatch, capsys, old, new):
    """A changed constant fails; a removed docstring alone does not."""
    tmp, _ = repo
    assert old in PY_BASE
    (tmp / "a.py").write_text(PY_BASE.replace(old, new))
    code, out = run(monkeypatch, capsys, "HEAD")
    removed_docstring = new == ""
    assert code == (0 if removed_docstring else 1)
    assert ("a.py: code changed" in out) != removed_docstring


def test_python_that_does_not_parse_fails(repo, monkeypatch, capsys):
    """A syntax error is a failure of that file, not a crash."""
    tmp, _ = repo
    (tmp / "a.py").write_text("def over(:\n")
    code, out = run(monkeypatch, capsys, "HEAD")
    assert code == 1
    assert "a.py: " in out and "1 failure(s)" in out


def test_a_removed_file_and_another_kind_fail(repo, monkeypatch, capsys):
    """A sweep removes no file and changes no file of another kind."""
    tmp, _ = repo
    (tmp / "a.c").unlink()
    (tmp / "notes.txt").write_text("two\n")
    code, out = run(monkeypatch, capsys, "HEAD")
    assert code == 1
    assert "a.c: added or removed" in out
    assert "notes.txt: not a C, header or Python file" in out
    assert "2 file(s) changed, 2 failure(s)" in out


def test_an_added_file_fails_between_two_commits(repo, monkeypatch, capsys):
    """BASE and HEAD named: a file HEAD adds is reported."""
    tmp, git = repo
    (tmp / "b.c").write_text("int b;\n")
    git("add", "-A")
    git("commit", "-q", "-m", "add")
    code, out = run(monkeypatch, capsys, "HEAD~1", "HEAD")
    assert code == 1
    assert "b.c: added or removed" in out


def test_a_path_limits_the_comparison(repo, monkeypatch, capsys):
    """--path leaves a code change elsewhere out."""
    tmp, _ = repo
    (tmp / "sub").mkdir()
    (tmp / "a.c").write_text(C_BASE.replace("n > LIMIT", "n < LIMIT"))
    code, out = run(monkeypatch, capsys, "HEAD", "--path", "a.py")
    assert code == 0
    assert "0 file(s) changed" in out
    code, out = run(monkeypatch, capsys, "HEAD", "--path", "a.c")
    assert code == 1
