"""security_parsers.py: the SECURITY.md table follows the public headers.

SPDX-License-Identifier: MIT
"""

import security_parsers as sp

TABLE = """# Security

## Input parsing

| Module | Input |
| --- | --- |
{rows}

## Reporting
"""


def empty_exclusions(monkeypatch):
    monkeypatch.setattr(sp, "NOT_UNTRUSTED", {})


def tree(tmp_path, headers, rows):
    for rel, body in headers.items():
        h = tmp_path / rel
        h.parent.mkdir(parents=True, exist_ok=True)
        h.write_text(body)
    (tmp_path / "SECURITY.md").write_text(
        TABLE.format(rows="\n".join(f"| `{r}` | x |" for r in rows)))
    return tmp_path


def test_the_real_tree_agrees():
    assert sp.problems() == []


def test_matching_table_passes(tmp_path, monkeypatch):
    empty_exclusions(monkeypatch)
    root = tree(tmp_path,
                {"protocols/a/include/a.h": "bool a_frame_parse(int x);\n"},
                ["protocols/a"])
    assert sp.problems(root) == []


def test_missing_row_is_reported(tmp_path, monkeypatch):
    empty_exclusions(monkeypatch)
    root = tree(tmp_path,
                {"protocols/a/include/a.h": "bool a_decode_byte(int x);\n"},
                [])
    assert sp.problems(root) == [
        "protocols/a declares a parse function and is neither in the "
        "SECURITY.md table nor in NOT_UNTRUSTED"]


def test_row_without_parser_is_reported(tmp_path, monkeypatch):
    empty_exclusions(monkeypatch)
    root = tree(tmp_path,
                {"protocols/b/include/b.h": "int b_sum(int x);\n"},
                ["protocols/b"])
    assert sp.problems(root) == [
        "SECURITY.md names protocols/b, which declares no parse function"]


def test_comment_lines_do_not_count(tmp_path, monkeypatch):
    empty_exclusions(monkeypatch)
    root = tree(tmp_path,
                {"shared/c/include/c.h":
                 " * see c_frame_parse(x)\n// c_rx(y)\n"},
                [])
    assert sp.problems(root) == []


def test_pointer_return_and_rx_and_feed_count(tmp_path, monkeypatch):
    empty_exclusions(monkeypatch)
    root = tree(tmp_path,
                {"protocols/d/include/d.h": "void d_rx(int x);\n",
                 "protocols/e/include/e.h": "const char *e_feed(int x);\n"},
                [])
    assert len(sp.problems(root)) == 2


def test_not_untrusted_entries_are_checked(tmp_path, monkeypatch):
    monkeypatch.setattr(sp, "NOT_UNTRUSTED",
                        {"shared/f": "r", "shared/g": "r"})
    root = tree(tmp_path,
                {"shared/f/include/f.h": "void f_feed(int x);\n"},
                ["shared/f"])
    assert sp.problems(root) == [
        "NOT_UNTRUSTED names shared/g, which is no candidate",
        "shared/f is in the SECURITY.md table and in NOT_UNTRUSTED"]


def test_check_exit_codes(capsys):
    assert sp.main(["--check"]) == 0
    assert sp.main([]) == 0
    assert "protocols/kst" in capsys.readouterr().out
