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
    monkeypatch.setattr(sp, "FORCED", {})


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
        "SECURITY.md names protocols/b, which declares no parse function "
        "and is not in FORCED"]


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
    monkeypatch.setattr(sp, "FORCED", {})
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


def test_page_handlers_and_utf8_decoder_count(tmp_path, monkeypatch):
    empty_exclusions(monkeypatch)
    root = tree(tmp_path,
                {"shared/h/include/h.h": "uint8_t h_page_write(int x);\n",
                 "shared/i/include/i.h": "bool i_page_read(int x);\n",
                 "shared/j/include/j.h": "void j_link_done(int x);\n",
                 "shared/k/include/k.h":
                 "uint32_t k_utf8_next(const char **s);\n"},
                [])
    assert [p.split(" ")[0] for p in sp.problems(root)] == [
        "shared/h", "shared/i", "shared/j", "shared/k"]


def test_names_that_only_contain_the_suffix_do_not_count(tmp_path,
                                                         monkeypatch):
    empty_exclusions(monkeypatch)
    root = tree(tmp_path,
                {"shared/m/include/m.h":
                 "int m_written(int x);\nint m_readout(int x);\n"
                 "int m_done_count(int x);\nint m_next(int x);\n"
                 "int m_write_all(int x);\nint m_read_back(int x);\n"
                 "int m_utf8_next_cell(int x);\n"},
                [])
    assert sp.problems(root) == []


def test_the_exclusion_table_is_empty():
    assert sp.NOT_UNTRUSTED == {}


def test_names_without_an_underscore_before_the_suffix_do_not_count(
        tmp_path, monkeypatch):
    empty_exclusions(monkeypatch)
    root = tree(tmp_path,
                {"shared/n/include/n.h":
                 "int overwrite(int x);\nint reread(int x);\n"
                 "int undone(int x);\nint xutf8_next(int x);\n"},
                [])
    assert sp.problems(root) == []


def test_an_excluded_candidate_needs_no_row(tmp_path, monkeypatch):
    empty_exclusions(monkeypatch)
    monkeypatch.setattr(sp, "NOT_UNTRUSTED", {"shared/f": "r"})
    root = tree(tmp_path,
                {"shared/f/include/f.h": "void f_feed(int x);\n"},
                [])
    assert sp.problems(root) == []


def test_check_fails_with_the_problems_on_stderr(monkeypatch, capsys):
    monkeypatch.setattr(sp, "problems", lambda root=sp.REPO: ["x", "y"])
    assert sp.main(["--check"]) == 1
    assert capsys.readouterr().err == ("security_parsers: x\n"
                                       "security_parsers: y\n")


def test_listing_adds_forced_and_drops_excluded(monkeypatch, capsys):
    monkeypatch.setattr(sp, "candidates",
                        lambda root=sp.REPO: {"shared/f", "shared/g"})
    monkeypatch.setattr(sp, "NOT_UNTRUSTED", {"shared/f": "r"})
    monkeypatch.setattr(sp, "FORCED", {"shared/h": "r"})
    assert sp.main([]) == 0
    assert capsys.readouterr().out == "shared/g\nshared/h\n"


def test_forced_module_needs_a_row(tmp_path, monkeypatch):
    empty_exclusions(monkeypatch)
    monkeypatch.setattr(sp, "FORCED", {"shared/p": "r", "shared/q": "r"})
    root = tree(tmp_path,
                {"shared/p/include/p.h": "void p_tick(int x);\n",
                 "shared/q/include/q.h": "void q_tick(int x);\n"},
                ["shared/q"])
    assert sp.problems(root) == [
        "shared/p is in FORCED and not in the SECURITY.md table"]


def test_stale_forced_entries_fail(tmp_path, monkeypatch):
    empty_exclusions(monkeypatch)
    monkeypatch.setattr(sp, "FORCED", {"shared/r": "r", "shared/s": "r"})
    root = tree(tmp_path,
                {"shared/s/include/s.h": "void s_frame_parse(int x);\n"},
                ["shared/r", "shared/s"])
    assert sp.problems(root) == [
        "FORCED names shared/r, which does not exist",
        "FORCED names shared/s, which the name rule finds"]


def test_a_forced_module_cannot_also_be_excluded(tmp_path, monkeypatch):
    empty_exclusions(monkeypatch)
    monkeypatch.setattr(sp, "FORCED", {"shared/t": "r"})
    monkeypatch.setattr(sp, "NOT_UNTRUSTED", {"shared/t": "r"})
    root = tree(tmp_path,
                {"shared/t/include/t.h": "void t_tick(int x);\n"},
                [])
    assert sp.problems(root) == [
        "shared/t is in FORCED and not in the SECURITY.md table",
        "NOT_UNTRUSTED names shared/t, which is no candidate"]


def test_settings_and_touch_are_forced():
    assert set(sp.FORCED) == {"shared/settings", "shared/touch"}
    assert all(reason.strip() for reason in sp.FORCED.values())
