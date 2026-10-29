# SPDX-License-Identifier: MIT
"""tools/gen_web_blob.py: what the page carries of each source, the pieces
it is cut into, the C strings, the refusals and --check."""

import pathlib
import shutil

import pytest

import gen_web_blob as g

REPO = pathlib.Path(__file__).resolve().parent.parent.parent


def test_comments_blank_lines_and_indents_are_left_out():
    js = "// a comment\n\n  var a = 1; // kept\n    //x\n  b();\n"
    assert g.served(js, "js") == "var a = 1; // kept\nb();\n"
    css = "/* one\n two */\n  a{b:c}\n\n/* x */d{e:f}\n"
    assert g.served(css, "css") == "a{b:c}\nd{e:f}\n"


@pytest.mark.parametrize("text", ["a\u00e4\n", "a\tb\n", "a\rb\n",
                                  "x = '</script>';\n", "/* </STYLE> */\n",
                                  "<!-- a\n"])
def test_what_the_page_cannot_carry_is_refused(text):
    with pytest.raises(g.Bad):
        g.served(text, "js", "x.js")


def test_pieces_are_whole_lines_of_at_most_the_limit():
    text = "".join(f"line {i:04d} {'x' * 90}\n" for i in range(100))
    parts = g.pieces(text)
    assert "".join(parts) == text
    assert all(len(p) <= g.PIECE_MAX and p.endswith("\n") for p in parts)
    assert len(parts) == -(-len(text) // g.PIECE_MAX)
    # A piece exactly at the limit, then one byte more.
    line = "y" * (g.PIECE_MAX - 1) + "\n"
    assert g.pieces(line) == [line]
    assert g.pieces(line + "z\n") == [line, "z\n"]
    with pytest.raises(g.Bad):
        g.pieces("z" * g.PIECE_MAX + "\n")


def test_c_strings_escape_quotes_backslashes_and_trigraphs():
    assert g.c_string('a"b\\c??=\n') == '    "a\\"b\\\\c\\?\\?=\\n"'
    assert g.c_string("1\n2\n").count("\n") == 1


def test_the_checked_in_file_is_current_and_drift_fails(tmp_path, capsys):
    assert g.main(["--check"]) == 0
    root = tmp_path / "repo"
    (root / "shared/webaccess").mkdir(parents=True)
    shutil.copytree(REPO / g.SRC, root / g.SRC)
    shutil.copy(REPO / g.OUT, root / g.OUT)
    assert g.main(["--check", "--root", str(root)]) == 0
    app = root / g.SRC / "app.js"
    app.write_text(app.read_text() + "var z = 1;\n")
    assert g.main(["--check", "--root", str(root)]) == 1
    assert "out of date" in capsys.readouterr().err
    assert g.main(["--root", str(root)]) == 0
    assert g.main(["--check", "--root", str(root)]) == 0
    assert "var z = 1;" in (root / g.OUT).read_text()


def test_a_bad_source_fails_the_run(tmp_path, capsys):
    root = tmp_path / "repo"
    (root / "shared/webaccess").mkdir(parents=True)
    shutil.copytree(REPO / g.SRC, root / g.SRC)
    css = root / g.SRC / "app.css"
    css.write_text("a{content:'\u00e4'}\n")
    assert g.main(["--root", str(root)]) == 1
    assert "app.css:1" in capsys.readouterr().err
    assert not (root / g.OUT).exists()


def test_sizes_are_the_served_bytes(capsys):
    assert g.main(["--sizes"]) == 0
    out = capsys.readouterr().out
    total = sum(len(g.served((REPO / g.SRC / name).read_text(), kind))
                for _, name, kind in g.FILES)
    assert f"total: {total} bytes" in out
