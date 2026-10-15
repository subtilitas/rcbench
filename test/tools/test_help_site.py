"""help_site.py: the Markdown subset, the checks across pages, the output.

SPDX-License-Identifier: MIT
"""

import pathlib
import re
import time

import pytest

import help_site as hs

PNG = (b"\x89PNG\r\n\x1a\n" + b"\x00\x00\x00\x0dIHDR"
       + (800).to_bytes(4, "big") + (480).to_bytes(4, "big")
       + b"\x08\x02\x00\x00\x00" + b"\x00" * 16)

VIEWS = {"servo": "servo.png", "servo-ina": "servo-ina.png",
         "motor": "motor.png", "only-en": "only-en.png"}
SCREENS = ["SCREEN_SPLASH", "SCREEN_SERVO", "SCREEN_MOTOR"]

HEADER = "---\nscreen: {screen}\ntitle: {title}\norder: {order}\n---\n\n"


@pytest.fixture
def tree(tmp_path):
    """A content tree and an image tree of its own, so the tests do not
    depend on what help/content/ holds today."""
    img = tmp_path / "img"
    (img / "de").mkdir(parents=True)
    for name in ("servo.png", "servo-ina.png", "motor.png", "only-en.png"):
        (img / name).write_bytes(PNG)
    for name in ("servo.png", "servo-ina.png", "motor.png"):
        (img / "de" / name).write_bytes(PNG)
    for lang in hs.LANGS:
        (tmp_path / "content" / lang).mkdir(parents=True)
    return tmp_path


def put(tree, lang, pid, body, screen=None, title="T", order=1):
    if screen is None:
        screen = pid.upper() if pid in ("servo", "motor") else "none"
    path = tree / "content" / lang / f"{pid}.md"
    path.write_text(HEADER.format(screen=screen, title=title, order=order)
                    + body, encoding="utf-8")
    return path


def both(tree, pid, body, body_de=None, **kw):
    put(tree, "en", pid, body, **kw)
    put(tree, "de", pid, body if body_de is None else body_de, **kw)


def render(tree, body, lang="en", header=None):
    """One page: (Page, problems)."""
    path = tree / "content" / lang / "page.md"
    path.write_text((HEADER.format(screen="none", title="T", order=1)
                     if header is None else header) + body,
                    encoding="utf-8")
    problems = hs.Problems()
    page = hs.parse(path, lang, problems, VIEWS, tree / "img")
    return page, problems


def ok(tree, body, lang="en"):
    page, problems = render(tree, body, lang)
    assert list(problems) == []
    return page.body


def fails(tree, body, pattern, line=None, header=None):
    _, problems = render(tree, body, header=header)
    assert problems, f"no problem for {body!r}"
    assert any(re.search(pattern, p) for p in problems), list(problems)
    if line is not None:
        assert any(p.split(":")[1] == str(line) for p in problems), \
            list(problems)


def run(tree):
    return hs.run(tree / "content", SCREENS, VIEWS, tree / "img")


# ---------------------------------------------------------- the subset

def test_headings_carry_anchors(tree):
    html = ok(tree, "# Servo\n\n## Run a test\n\n### Lösen und Halten\n")
    assert '<h1 id="servo">Servo</h1>' in html
    assert '<h2 id="run-a-test">Run a test</h2>' in html
    assert '<h3 id="lösen-und-halten">' in html


def test_lines_of_a_paragraph_join(tree):
    assert ok(tree, "one\ntwo\n\nthree\n") == "<p>one two</p>\n<p>three</p>"


def test_bold_code_and_escapes(tree):
    html = ok(tree, r"**ARM** then `SAFE **x**` and \*star\* \_u\_" "\n")
    assert "<strong>ARM</strong>" in html
    assert "<code>SAFE **x**</code>" in html
    assert "*star* _u_" in html


def test_an_underscore_inside_a_word_is_text(tree):
    assert "SCREEN_SERVO" in ok(tree, "SCREEN_SERVO and a_b_c\n")


def test_lists(tree):
    html = ok(tree, "- a\n- b\n  continued\n\n3. x\n4. y\n")
    assert "<ul><li>a</li><li>b continued</li></ul>" in html
    assert '<ol start="3"><li>x</li><li>y</li></ol>' in html


def test_a_table_with_alignment(tree):
    html = ok(tree, "| a | b | c |\n|---|:-:|--:|\n| 1 | `x\\|y` | 3 |\n")
    assert '<th class="c">b</th>' in html and '<td class="r">3</td>' in html
    assert "<code>x|y</code>" in html


def test_a_table_without_body_rows(tree):
    assert "<thead>" in ok(tree, "| a |\n|---|\n")


def test_links(tree):
    html = ok(tree, "## Here\n\n[a](https://example.com/x?y=1&z=2) "
                    "[b](motor.md) [c](motor.md#run) [d](#here) "
                    "[e](index.md)\n")
    assert ('<a href="https://example.com/x?y=1&amp;z=2" '
            'rel="noopener noreferrer">a</a>') in html
    assert '<a href="motor.html">b</a>' in html
    assert '<a href="motor.html#run">c</a>' in html
    assert '<a href="#here">d</a>' in html
    assert '<a href="index.html">e</a>' in html


def test_a_link_target_keeps_balanced_parentheses(tree):
    html = ok(tree, "[spec](https://example.com/Foo_(bar)) after\n")
    assert '<a href="https://example.com/Foo_(bar)" ' in html
    assert "</a> after" in html


def test_an_escaped_parenthesis_in_a_link_target(tree):
    html = ok(tree, "[x](https://example.com/a\\)b) after\n")
    assert '<a href="https://example.com/a)b" ' in html
    assert "</a> after" in html


def test_a_heading_link_with_an_escaped_bracket_gets_its_label_anchor(tree):
    html = ok(tree, "## [A\\]B](https://example.com)\n\n[x](#a-b)\n")
    assert '<h2 id="a-b">' in html


def test_an_underscore_in_a_link_target_is_no_emphasis(tree):
    html = ok(tree, "[x](https://example.com/_private_)\n")
    assert 'href="https://example.com/_private_"' in html


def test_a_cell_ending_in_a_literal_backslash(tree):
    html = ok(tree, "| Path |\n|---|\n| C:\\\\|\n")
    assert "C:\\</td>" in html


def test_a_heading_anchor_keeps_the_marks_of_a_code_span(tree):
    html = ok(tree, "## `C**D`\n\n[x](#c-d)\n")
    assert '<h2 id="c-d"><code>C**D</code></h2>' in html


def test_a_heading_anchor_drops_a_link_target_but_not_code(tree):
    html = ok(tree, "## `[a](b)` and [c](https://x.example)\n\n"
                    "[x](#a-b-and-c)\n")
    assert '<h2 id="a-b-and-c">' in html


def test_a_nested_link_names_its_line(tree):
    fails(tree, "## X\n\n## Y\n\n[a [b](#x)](#y)\n",
          "a link inside a link", line=11)


def test_a_bracket_in_a_code_span_does_not_close_a_label(tree):
    html = ok(tree, "## A\n\n[x `]` y](#a)\n")
    assert '<a href="#a">x <code>]</code> y</a>' in html


def test_a_bracket_in_a_code_span_does_not_open_a_label(tree):
    html = ok(tree, "`[` [x](https://x.example)\n")
    assert '<code>[</code> <a href="https://x.example"' in html


def test_brackets_inside_a_link_label_stay_text(tree):
    html = ok(tree, "[see [1] here](https://x.example)\n")
    assert ">see [1] here</a>" in html


def test_an_image_alt_keeps_the_marks_of_a_code_span(tree):
    html = ok(tree, "![The `C**D` key](view:motor)\n")
    assert 'alt="The C**D key"' in html


def test_a_heading_without_an_anchor_fails(tree):
    fails(tree, "## ???\n", "no letter or digit for its anchor")


def test_an_unindented_line_after_a_list_fails(tree):
    fails(tree, "- item\nparagraph\n", "leave a blank line after a list",
          line=8)


def test_an_indented_line_continues_a_list_item(tree):
    assert "<li>item more</li>" in ok(tree, "- item\n  more\n")


def test_a_bracket_without_a_target_is_text(tree):
    assert "[1] and [x]" in ok(tree, "[1] and [x]\n")


def test_one_image(tree):
    html = ok(tree, "![The **SERVO** screen](view:servo)\n")
    assert ('<div class="shot"><figure><a href="img/servo.png">'
            '<img src="img/servo.png"') in html
    assert 'alt="The SERVO screen" width="800" height="480"' in html
    assert "<figcaption>The <strong>SERVO</strong> screen</figcaption>" \
        in html


def test_images_in_one_block_form_a_row(tree):
    html = ok(tree, "![a](view:servo)\n![b](view:servo-ina)\n")
    assert html.startswith('<div class="shots">')
    assert html.count("<figure>") == 2


def test_a_german_page_takes_the_german_image(tree):
    page, problems = render(tree, "![a](view:servo)\n", lang="de")
    assert list(problems) == []
    assert 'src="../img/de/servo.png"' in page.body


def test_the_views_are_recorded_in_order(tree):
    page, _ = render(tree, "![a](view:servo-ina)\n\n![b](view:servo)\n")
    assert [v for v, _ in page.views] == ["servo-ina", "servo"]


# ------------------------------------------------------------ hostile input

def test_html_in_text_is_escaped(tree):
    html = ok(tree, '<script>alert("x")</script> & <b onclick=1>\n\n'
                    "- <img src=x onerror=1>\n\n"
                    "| <i>h</i> |\n|---|\n| <a href=x>c</a> |\n\n"
                    "## <em>t</em>\n")
    assert "<script" not in html and "<b " not in html
    assert "<img src=x" not in html and "<i>" not in html
    assert "<em>" not in html and "<a href=x>" not in html
    assert "&lt;script&gt;" in html


def test_html_in_a_caption_is_escaped(tree):
    html = ok(tree, '![<img src=x onerror=1> "q"](view:servo)\n')
    assert "<img src=x" not in html
    assert 'alt="&lt;img src=x onerror=1&gt; &quot;q&quot;"' in html


def test_a_link_target_cannot_break_out_of_its_attribute(tree):
    fails(tree, '[a](https://x.example/"onmouseover="1)\n', "link target")


@pytest.mark.parametrize("target", [
    "javascript:alert(1)", "../docs/Home.md", "Home", "mailto:a@b.c",
    "data:text/html,x", "view:servo", "MOTOR.md", "",
])
def test_a_link_target_outside_the_format_fails(tree, target):
    fails(tree, f"[a]({target})\n", "link target", line=7)


def test_an_unclosed_table_row_fails(tree):
    fails(tree, "| a | b |\n|---|---|\n| 1 | 2\n", "unclosed table row",
          line=9)


def test_a_row_ending_in_an_escaped_pipe_is_unclosed(tree):
    fails(tree, "| a |\n|---|\n| 1 \\|\n", "unclosed table row")


def test_a_table_without_separator_fails(tree):
    fails(tree, "| a | b |\n| 1 | 2 |\n", "separator", line=8)


def test_a_table_of_one_row_fails(tree):
    fails(tree, "| a | b |\n", "separator")


def test_a_row_with_another_cell_count_fails(tree):
    fails(tree, "| a | b |\n|---|---|\n| 1 | 2 | 3 |\n", "3 cells", line=9)


def test_a_table_row_that_does_not_start_with_a_pipe_fails(tree):
    fails(tree, "| a |\n|---|\nx |\n", "starts with \\|")


def test_an_image_without_view_fails(tree):
    fails(tree, "![a](servo.png)\n", "takes view:", line=7)
    fails(tree, "![a](https://x.example/a.png)\n", "takes view:")


def test_an_image_inside_text_fails(tree):
    fails(tree, "see ![a](view:servo) here\n", "line of its own", line=7)


def test_text_in_a_block_of_images_fails(tree):
    fails(tree, "![a](view:servo)\nand text\n", "one !\\[caption\\]",
          line=8)


def test_an_image_without_caption_fails(tree):
    fails(tree, "![](view:servo)\n", "no caption")


def test_an_unknown_view_fails(tree):
    fails(tree, "![a](view:nothing)\n", "not in render_ui.py")


def test_a_view_without_a_german_image_fails(tree):
    page, problems = render(tree, "![a](view:only-en)\n", lang="de")
    assert any("docs/img/de/only-en.png" in p for p in problems)


def test_a_view_whose_file_is_not_a_png_fails(tree):
    (tree / "img" / "motor.png").write_bytes(b"GIF89a")
    fails(tree, "![a](view:motor)\n", "not a PNG")


@pytest.mark.parametrize("body, pattern", [
    ("**open\n", r"unclosed \*\*"),
    ("`open\n", "unclosed `"),
    ("``x``\n", "single backticks"),
    ("a *b* c\n", r"single \*"),
    ("a _b_ c\n", "_ emphasis"),
    ("[a][ref]\n", "reference link"),
    ("[a](https://x.example\n", r"unclosed link"),
    ("[](https://x.example)\n", "without text"),
    ("## X\n\n## Y\n\n[a [b](#x)](#y)\n", "a link inside a link"),
    ("[****](https://x.example)\n", "without text"),
    ("[``](https://x.example)\n", "without text|single backticks"),
    ("text\n## Heading\n", "blank line before it"),
    ("## Heading\ntext\n", "blank line after it"),
    ("text\n+ item\n", "starts with '- '"),
    ("text\n* item\n", "starts with '- '"),
    ("text\n> quote\n", "block quote"),
    ("text\n```\n", "fenced code"),
    ("text\n***\n", "rule"),
    ("```\ncode\n```\n", "fenced code"),
    ("> quote\n", "block quote"),
    ("---\n", "rule"),
    ("* item\n", "starts with '- '"),
    ("+ item\n", "starts with '- '"),
    ("- a\n  - b\n", "nested list"),
    ("- a\n1. b\n", "mixes"),
    ("#### Deep\n", "level 4"),
    ("#NoSpace\n", "space after"),
    ("   ## Indented\n", "first column"),
    ("Title\n=====\n", "setext"),
    ("Title\n-----\n", "setext"),
    ("text\n- item\n", "blank line before"),
    ("text\n| a |\n", "blank line before"),
    ("- item\n| a |\n", "blank line before"),
    ("  | a |\n  |---|\n", "indented table"),
    ("## A\n\n# B\n", "once, as the first block"),
    ("## A\n\n## A\n", "second heading"),
    ("[a](#nowhere)\n", "no heading with the anchor"),
    ("##\n", "empty heading|space after"),
])
def test_outside_the_subset_fails(tree, body, pattern):
    fails(tree, body, pattern)


def test_a_problem_names_its_line(tree):
    _, problems = render(tree, "fine\n\nfine\n**open\n")
    assert len(problems) == 1
    assert problems[0].endswith(":10: unclosed **")


@pytest.mark.parametrize("header, pattern", [
    ("", "header block"),
    ("---\nscreen: none\ntitle: T\norder: 1\n", "no closing"),
    ("---\nscreen: none\ntitle: T\n---\n", "'order' is missing"),
    ("---\nscreen: none\ntitle: T\norder: 1\nmood: x\n---\n", "unknown"),
    ("---\nscreen: none\ntitle: T\norder: 1\norder: 2\n---\n", "twice"),
    ("---\nscreen: none\ntitle: T\norder: -1\n---\n", "integer"),
    ("---\nscreen: none\ntitle: T\norder: 10000\n---\n", "integer"),
    ("---\nscreen: none\ntitle:\norder: 1\n---\n", "title is empty"),
    ("---\nscreen: SCREEN_X\ntitle: T\norder: 1\n---\n", "without SCREEN_"),
    ("---\nscreen: none\nnot a key\norder: 1\n---\n", "key: value"),
])
def test_a_bad_header_fails(tree, header, pattern):
    fails(tree, "text\n", pattern, header=header)


def test_a_very_long_line_renders_in_linear_time(tree):
    # 200,000 characters of every mark that starts a scan: brackets that
    # never close, underscores, escapes, unclosed backticks last.
    chunk = "word [x _y_z \\* **b** `c` [l](#t) "
    line = chunk * (200_000 // len(chunk))
    start = time.monotonic()
    page, problems = render(tree, "## T\n\n" + line + "\n")
    assert time.monotonic() - start < 5
    assert list(problems) == []
    assert len(page.body) > 200_000
    for hostile in ("[" * 100_000, "_a " * 50_000, "`" + "x" * 100_000,
                    "| a " * 50_000, "*" * 100_001, "![" * 50_000):
        start = time.monotonic()
        render(tree, hostile + "\n")
        assert time.monotonic() - start < 5, hostile[:10]


def test_bytes_that_are_not_utf8_fail(tree):
    path = tree / "content" / "en" / "page.md"
    path.write_bytes(b"---\xff\n")
    problems = hs.Problems()
    hs.parse(path, "en", problems, VIEWS, tree / "img")
    assert "not UTF-8" in problems[0]


def test_crlf_line_ends_read_as_lf(tree):
    page, problems = render(tree, "# A\r\n\r\ntext\r\n",
                            header="---\r\nscreen: none\r\ntitle: T\r\n"
                                   "order: 1\r\n---\r\n")
    assert list(problems) == []
    assert "<p>text</p>" in page.body


# ---------------------------------------------------------- across pages

def full(tree):
    both(tree, "servo", "# Servo\n\nLead.\n\n## Run\n\n![a](view:servo)\n"
                        "\n[motor](motor.md#hold)\n", order=2)
    both(tree, "motor", "## Hold\n\n![m](view:motor)\n", order=1)


def test_a_complete_tree_passes(tree):
    full(tree)
    _, problems = run(tree)
    assert list(problems) == []


def test_a_screen_without_a_page_fails_in_each_language(tree):
    full(tree)
    (tree / "content" / "de" / "motor.md").unlink()
    _, problems = run(tree)
    assert any("SCREEN_MOTOR has no de page" in p for p in problems)
    assert not any("has no en page" in p for p in problems)


def test_a_screen_page_must_name_its_screen(tree):
    full(tree)
    put(tree, "de", "servo", "# Servo\n\nLead.\n\n## Run\n\n"
                             "![a](view:servo)\n\n[motor](motor.md#hold)\n",
        screen="none", order=2)
    _, problems = run(tree)
    assert any("SCREEN_SERVO has no de page" in p for p in problems)


def test_index_is_not_a_page_id(tree):
    full(tree)
    both(tree, "index", "text\n")
    _, problems = run(tree)
    assert any("index is the generated list" in p for p in problems)


def test_the_splash_needs_no_page(tree):
    full(tree)
    _, problems = run(tree)
    assert not any("SPLASH" in p for p in problems)


def test_an_extra_page_needs_its_counterpart(tree):
    full(tree)
    put(tree, "en", "servo-programmer", "text\n")
    _, problems = run(tree)
    assert any("no de counterpart" in p for p in problems)


def test_a_screen_page_is_named_after_its_screen(tree):
    full(tree)
    both(tree, "servo-x", "text\n", screen="SERVO")
    _, problems = run(tree)
    assert any("the page of SCREEN_SERVO is servo.md" in p
               for p in problems)
    assert any("a second page of SCREEN_SERVO" in p for p in problems)


def test_an_unknown_screen_fails(tree):
    full(tree)
    both(tree, "nothing", "text\n", screen="NOTHING")
    _, problems = run(tree)
    assert any("SCREEN_NOTHING is not a screen" in p for p in problems)


def test_heading_structures_that_differ_fail(tree):
    full(tree)
    both(tree, "motor", "## Hold\n\n![m](view:motor)\n",
         body_de="## Halten\n\n### Mehr\n\n![m](view:motor)\n")
    _, problems = run(tree)
    assert any("heading 2 differs" in p and "de/motor.md:9:" in p
               for p in problems), list(problems)


def test_a_missing_title_heading_in_one_language_fails(tree):
    full(tree)
    both(tree, "motor", "# Motor\n\n## Hold\n\n![m](view:motor)\n",
         body_de="## Hold\n\n![m](view:motor)\n")
    _, problems = run(tree)
    assert any("# title" in p for p in problems)


def test_view_lists_that_differ_fail(tree):
    full(tree)
    both(tree, "motor", "## Hold\n\n![m](view:motor)\n",
         body_de="## Hold\n\n![m](view:servo)\n")
    _, problems = run(tree)
    assert any("view 1 differs" in p and "servo in de, motor in en" in p
               for p in problems), list(problems)


def test_a_view_missing_in_one_language_fails(tree):
    full(tree)
    both(tree, "motor", "## Hold\n\n![m](view:motor)\n\n![s](view:servo)\n",
         body_de="## Hold\n\n![m](view:motor)\n")
    _, problems = run(tree)
    assert any("view 2 differs" in p for p in problems)


def test_order_and_screen_agree_across_languages(tree):
    full(tree)
    put(tree, "de", "motor", "## Hold\n\n![m](view:motor)\n", order=7)
    _, problems = run(tree)
    assert any("order 7 differs" in p for p in problems)


def test_a_link_to_a_missing_page_fails(tree):
    full(tree)
    both(tree, "motor", "## Hold\n\n![m](view:motor)\n\n[x](gone.md)\n")
    _, problems = run(tree)
    assert any("missing page gone.md" in p for p in problems)


def test_a_link_to_a_missing_anchor_fails(tree):
    full(tree)
    both(tree, "motor", "## Hold\n\n![m](view:motor)\n\n[x](servo.md#nope)\n")
    _, problems = run(tree)
    assert any("no heading #nope on servo.md" in p for p in problems)


def test_the_index_has_no_anchors(tree):
    full(tree)
    both(tree, "motor", "## Hold\n\n![m](view:motor)\n\n[x](index.md#a)\n")
    _, problems = run(tree)
    assert any("index has no anchors" in p for p in problems)


def test_files_that_are_not_pages(tree):
    full(tree)
    (tree / "content" / "en" / "_draft.md").write_text("anything")
    (tree / "content" / "en" / "notes.txt").write_text("x")
    _, problems = run(tree)
    assert len(problems) == 1 and "notes.txt" in problems[0]


def test_a_missing_language_folder_fails(tree):
    full(tree)
    for p in (tree / "content" / "de").iterdir():
        p.unlink()
    (tree / "content" / "de").rmdir()
    _, problems = run(tree)
    assert any("no content folder for de" in p for p in problems)


# ---------------------------------------------------------------- inputs

def test_the_screen_list_is_the_router_enum():
    names = hs.screen_names()
    assert names[0] == "SCREEN_SPLASH"
    assert "SCREEN_COPROC_UPDATE" in names and "SCREEN_COUNT" not in names
    assert len(names) == len(set(names))


def test_comments_in_the_enum_are_not_screens(tmp_path):
    h = tmp_path / "ui_screen.h"
    h.write_text("typedef enum {\n SCREEN_A = 0,\n /* SCREEN_OLD */\n"
                 " SCREEN_B, // SCREEN_C\n SCREEN_COUNT\n} ui_screen_id_t;\n")
    assert hs.screen_names(h) == ["SCREEN_A", "SCREEN_B"]


def test_every_view_has_its_images():
    for name, filename in hs.view_files().items():
        for lang in hs.LANGS:
            assert hs.image_path(hs.IMG, lang, filename).is_file(), name


def test_the_example_page_renders(tmp_path):
    example = hs.CONTENT / "en" / "_example.md"
    if not example.exists():
        pytest.skip("no _example.md")
    assert hs.main(["--page", str(example), "--out", str(tmp_path)]) == 0
    assert (tmp_path / "_example.html").is_file()


# ---------------------------------------------------------------- output

def test_the_site(tree):
    full(tree)
    pages, problems = run(tree)
    assert list(problems) == []
    out = tree / "out"
    hs.write(out, pages, VIEWS, tree / "img")
    files = sorted(str(p.relative_to(out)) for p in out.rglob("*")
                   if p.is_file())
    assert files == [".help-site", "de/index.html", "de/motor.html",
                     "de/servo.html",
                     "img/de/motor.png", "img/de/servo.png", "img/motor.png",
                     "img/servo.png", "index.html", "motor.html",
                     "servo.html", "style.css"]
    en = (out / "servo.html").read_text()
    de = (out / "de" / "servo.html").read_text()
    assert 'href="de/servo.html" hreflang="de"' in en
    assert 'href="../servo.html" hreflang="en"' in de
    assert 'href="../style.css"' in de
    assert '<a href="servo.html" aria-current="page">' in en
    index = (out / "index.html").read_text()
    # Ordered by `order`: motor (1) before servo (2); the lead is the first
    # paragraph.
    assert index.index("motor.html") < index.index("servo.html")
    assert "<p>Lead.</p>" in index
    # A lead's link to its own heading points at its page from the index.
    both(tree, "motor", "Lead [hold](#hold).\n\n## Hold\n\n"
                        "![m](view:motor)\n", order=1)
    pages, problems = run(tree)
    assert list(problems) == []
    hs.write(out, pages, VIEWS, tree / "img")
    assert 'href="motor.html#hold"' in (out / "index.html").read_text()
    assert 'href="de/index.html"' in index
    assert 'href="../index.html"' in (out / "de" / "index.html").read_text()


def test_a_page_without_a_title_heading_gets_one(tree):
    full(tree)
    pages, _ = run(tree)
    hs.write(tree / "out", pages, VIEWS, tree / "img")
    motor = (tree / "out" / "motor.html").read_text()
    assert "<h1>T</h1>" in motor
    servo = (tree / "out" / "servo.html").read_text()
    assert servo.count("<h1") == 1


def test_the_pages_load_nothing_from_elsewhere_and_run_no_script(tree):
    full(tree)
    pages, _ = run(tree)
    out = tree / "out"
    hs.write(out, pages, VIEWS, tree / "img")
    for page in out.rglob("*.html"):
        text = page.read_text()
        assert "<script" not in text
        for tag in re.findall(r"<(?:link|img|script)[^>]*>", text):
            assert "http" not in tag, (page, tag)
    css = (out / "style.css").read_text()
    assert "@import" not in css and "url(" not in css


def test_a_rebuild_empties_the_folder_first(tree):
    full(tree)
    pages, _ = run(tree)
    out = tree / "out"
    hs.write(out, pages, VIEWS, tree / "img")
    (out / "stale.html").write_text("x")
    hs.write(out, pages, VIEWS, tree / "img")
    assert not (out / "stale.html").exists()


def test_a_folder_the_tool_did_not_write_is_kept(tree):
    full(tree)
    pages, _ = run(tree)
    out = tree / "mine"
    out.mkdir()
    (out / "work.txt").write_text("x")
    with pytest.raises(SystemExit, match="not written by this tool"):
        hs.write(out, pages, VIEWS, tree / "img")
    assert (out / "work.txt").exists()


@pytest.mark.parametrize("out", [hs.REPO, hs.REPO / "help", hs.CONTENT,
                                 hs.REPO / "docs", hs.REPO.parent])
def test_an_output_folder_holding_sources_is_refused(tree, out):
    full(tree)
    pages, _ = run(tree)
    with pytest.raises(SystemExit, match="choose an output folder"):
        hs.write(out, pages, VIEWS, tree / "img")
    assert hs.CONTENT.exists()


def test_a_preview_navigates_only_to_files_it_writes(tmp_path):
    out = tmp_path / "out"
    assert hs.main(["--page", str(hs.CONTENT / "en" / "servo.md"),
                    "--out", str(out)]) == 0
    written = {p.relative_to(out).as_posix() for p in out.rglob("*.html")}
    assert {"index.html", "servo.html", "de/index.html",
            "de/servo.html"} <= written
    for page in out.rglob("*.html"):
        text = page.read_text()
        # The header's home and language switch and the list of pages;
        # links inside the text go to pages a preview does not render.
        nav = re.findall(r'class="(?:home|switch)" href="([^"]+)"', text)
        for block in re.findall(r'<nav class="pages".*?</nav>|'
                                r'<ol class="index">.*?</ol>', text, re.S):
            nav += re.findall(r'<li><a href="([^"]+)"', block)
        assert nav
        for href in nav:
            target = (page.parent / href).resolve()
            assert target.exists(), (page, href)


def test_check_writes_nothing(tmp_path, monkeypatch):
    monkeypatch.setattr(hs, "run", lambda *a: ({"en": {}, "de": {}},
                                               hs.Problems()))
    out = tmp_path / "out"
    assert hs.main(["--check", "--out", str(out)]) == 0
    assert not out.exists()


def test_a_problem_writes_nothing(tmp_path, monkeypatch):
    problems = hs.Problems()
    problems.add(pathlib.Path("x.md"), 3, "bad")
    monkeypatch.setattr(hs, "run", lambda *a: ({"en": {}, "de": {}},
                                               problems))
    out = tmp_path / "out"
    assert hs.main(["--out", str(out)]) == 1
    assert not out.exists()
