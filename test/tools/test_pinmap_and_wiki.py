"""pinmap_check.py and wiki_links.py: the checks one map and one page
exercise.

SPDX-License-Identifier: MIT
"""

import pinmap_check as pm
import wiki_links as wl

# What io_bank0.h gives the two pins the cases use.
FUNCTIONS = {2: {"SPI0_SCLK", "UART0_CTS", "I2C1_SDA"},
             3: {"SPI0_TX", "UART0_RTS", "I2C1_SCL"}}


def chip(*pins):
    return {"chip": "main", "pins": list(pins)}


def pin(gpio, function, **more):
    return {"gpio": gpio, "signal": f"signal {gpio}", "function": function,
            **more}


def test_a_map_the_silicon_can_serve_has_no_failure():
    fails, used = pm.check_chip(chip(
        pin(2, "I2C1_SDA", bus="sensors"),
        pin(3, "I2C1_SCL", bus="sensors"),
        pin(40, "ADC"), pin(10, "SIO")), FUNCTIONS)
    assert fails == [] and used == 4


def test_a_function_the_pin_does_not_have_fails():
    fails, _ = pm.check_chip(chip(pin(2, "UART1_TX", bus="u")), FUNCTIONS)
    assert fails == ["main GPIO2 (signal 2): UART1_TX is not a function of "
                     "GPIO2"]


def test_a_pin_used_twice_fails():
    fails, used = pm.check_chip(chip(pin(10, "SIO"), pin(10, "SIO")),
                                FUNCTIONS)
    assert fails == ["main GPIO10 (signal 10): also used by signal 10"]
    assert used == 1


def test_adc_outside_its_pins_and_an_external_signal_past_39_fail():
    fails, _ = pm.check_chip(chip(pin(5, "ADC"),
                                  pin(41, "SIO", external=True)), FUNCTIONS)
    assert fails == [
        "main GPIO5 (signal 5): ADC needs GPIO 40 to 47",
        "main GPIO41 (signal 41): an external signal needs a "
        "fault-tolerant pin, GPIO 0 to 39"]


def test_an_i2c_bus_of_one_pin_and_a_bus_of_two_instances_fail():
    fails, _ = pm.check_chip(chip(pin(2, "I2C1_SDA", bus="a")), FUNCTIONS)
    assert fails == ["main bus a: an I2C bus has 2 pins, not 1"]
    fails, _ = pm.check_chip(chip(pin(2, "SPI0_SCLK", bus="b"),
                                  pin(3, "UART0_RTS", bus="b")), FUNCTIONS)
    assert fails == ["main bus b: mixes SPI0, UART0"]


def test_a_pio_pin_names_its_protocol_and_a_block_fits_one_window():
    fails, _ = pm.check_chip(chip(pin(4, "PIO0"),
                                  pin(40, "PIO0", protocol="dshot")),
                             FUNCTIONS)
    assert fails == [
        "main GPIO4 (signal 4): a PIO pin names no protocol",
        "main PIO0: GPIO 4 to 40 do not fit one window, 0 to 31 or 16 to 47"]


def test_a_page_link_loses_its_extension_and_keeps_its_anchor():
    assert wl.rewrite("[a](Screens.md) [b](Link-de.md#protokoll)") == (
        "[a](Screens) [b](Link-de#protokoll)")


def test_urls_images_and_absolute_paths_are_left_alone():
    text = "[a](https://example.com/x.md) ![i](img/a.png) [r](/abs/x.md)"
    assert wl.rewrite(text) == text


def test_the_rewrite_changes_only_the_copy_it_is_given(tmp_path):
    page = tmp_path / "Home.md"
    page.write_text("[a](Screens.md)\n", encoding="utf-8")
    assert wl.main(["wiki_links.py", "--check", str(tmp_path)]) == 0
    assert page.read_text(encoding="utf-8") == "[a](Screens.md)\n"
    assert wl.main(["wiki_links.py", str(tmp_path)]) == 0
    assert page.read_text(encoding="utf-8") == "[a](Screens)\n"


R = "https://github.com/subtilitas/rcbench"


def test_a_file_above_docs_becomes_a_repository_url():
    assert wl.rewrite("[n](../NOTICE) [r](../README.md#x)") == (
        f"[n]({R}/blob/main/NOTICE) [r]({R}/blob/main/README.md#x)")


def test_a_directory_above_docs_becomes_a_tree_url():
    assert wl.rewrite("[d](../protocols/kst/)") == (
        f"[d]({R}/tree/main/protocols/kst/)")


def test_a_title_and_a_link_text_over_two_lines_survive():
    text = '[a\nb](../README.md "the readme")'
    assert wl.rewrite(text) == f'[a\nb]({R}/blob/main/README.md "the readme")'


def test_an_image_above_docs_becomes_a_raw_url():
    assert wl.rewrite("![i](../img/a.png)") == f"![i]({R}/raw/main/img/a.png)"


def test_an_image_inside_docs_is_unchanged():
    assert wl.rewrite("![i](img/a.png)") == "![i](img/a.png)"


def test_a_subdirectory_page_resolves_against_its_own_directory():
    assert wl.rewrite("[a](../Home.md) [b](../../README.md)", "sub") == (
        f"[a](../Home) [b]({R}/blob/main/README.md)")


def test_nested_climbing_that_lands_in_the_repository():
    assert wl.rewrite("[a](../../src/x.c)", "sub") == (
        f"[a]({R}/blob/main/src/x.c)")


def test_a_target_above_the_repository_fails_the_check(tmp_path, capsys):
    (tmp_path / "Home.md").write_text("[a](../../x.md)\n", encoding="utf-8")
    assert wl.main(["wiki_links.py", str(tmp_path)]) == 1
    assert "leaves docs/" in capsys.readouterr().err


def test_a_converted_tree_leaves_nothing_behind(tmp_path):
    (tmp_path / "Home.md").write_text(
        "[a](../README.md#x)\n[b](Screens.md)\n[c](../NOTICE)\n",
        encoding="utf-8")
    assert wl.main(["wiki_links.py", str(tmp_path)]) == 0
    assert "](../" not in (tmp_path / "Home.md").read_text(encoding="utf-8")


def test_a_target_in_angle_brackets_is_rewritten_and_keeps_them():
    assert wl.rewrite("[n](<../Legal Notice.md#a b>)") == (
        f"[n](<{R}/blob/main/Legal Notice.md#a b>)")
    assert wl.rewrite("[s](<Servo Page.md>)") == "[s](<Servo Page>)"


def test_a_target_in_angle_brackets_above_the_repository_fails(tmp_path):
    (tmp_path / "Home.md").write_text("[a](<../../x y.md>)\n",
                                      encoding="utf-8")
    assert wl.main(["wiki_links.py", str(tmp_path)]) == 1


def test_a_directory_without_a_slash_becomes_a_tree_url(tmp_path):
    (tmp_path / "protocols" / "kst").mkdir(parents=True)
    (tmp_path / "NOTICE").write_text("n\n", encoding="utf-8")
    text = "[k](../protocols/kst) [n](../NOTICE) [r](..)"
    assert wl.rewrite(text, root=tmp_path) == (
        f"[k]({R}/tree/main/protocols/kst/) [n]({R}/blob/main/NOTICE) "
        f"[r]({R}/tree/main/)")


def test_a_directory_of_this_repository_is_found_by_default():
    assert wl.rewrite("[k](../protocols/kst)") == (
        f"[k]({R}/tree/main/protocols/kst/)")


def test_spaces_inside_the_parentheses_are_rewritten_and_kept():
    assert wl.rewrite("[n]( ../README.md)") == f"[n]( {R}/blob/main/README.md)"
    assert wl.rewrite('[n](\t<../a b.md> "t" )') == (
        f'[n](\t<{R}/blob/main/a b.md> "t" )')
    assert wl.rewrite("[s]( Screens.md )") == "[s]( Screens )"


def test_a_spaced_target_above_the_repository_fails(tmp_path):
    (tmp_path / "Home.md").write_text("[a]( ../../x.md)\n", encoding="utf-8")
    assert wl.main(["wiki_links.py", str(tmp_path)]) == 1


def test_a_reference_definition_is_rewritten_like_a_link():
    text = ("[notice][legal] [s][s]\n\n"
            "[legal]: ../README.md#x\n"
            '[s]: Screens.md "t"\n'
            "[p]: <../a b.md> 'x'\n"
            "   [q]: ../NOTICE (note)\n")
    assert wl.rewrite(text) == (
        "[notice][legal] [s][s]\n\n"
        f"[legal]: {R}/blob/main/README.md#x\n"
        '[s]: Screens "t"\n'
        f"[p]: <{R}/blob/main/a b.md> 'x'\n"
        f"   [q]: {R}/blob/main/NOTICE (note)\n")


def test_a_definition_of_a_url_or_an_anchor_is_unchanged():
    text = "[a]: https://x.org/a.md\n[b]: #top\n[c]: /abs.md\n"
    assert wl.rewrite(text) == text


def test_a_definition_above_the_repository_is_stranded(tmp_path, capsys):
    assert wl.stranded("[a]: ../README.md\n[b]: Screens\n") == ["../README.md"]
    (tmp_path / "Home.md").write_text("[a]: ../../x.md\n", encoding="utf-8")
    assert wl.main(["wiki_links.py", str(tmp_path)]) == 1
    assert "leaves docs/" in capsys.readouterr().err


def test_a_title_in_single_quotes_or_parentheses_stays():
    assert wl.rewrite("[s](Screens.md 'x') [t](Screens.md (y))") == (
        "[s](Screens 'x') [t](Screens (y))")
    assert wl.rewrite("[n](../README.md 'x')") == (
        f"[n]({R}/blob/main/README.md 'x')")


def test_a_name_starting_with_two_dots_is_a_page_inside_docs():
    assert wl.rewrite("[a](..notes.md)") == "[a](..notes)"
    assert wl.stranded("[a](..notes)") == []


def test_a_dotted_name_above_docs_is_inside_the_repository(tmp_path):
    assert wl.rewrite("[a](../..config)") == f"[a]({R}/blob/main/..config)"
    assert wl.stranded(wl.rewrite("[a](../..config)")) == []
    (tmp_path / "Home.md").write_text("[a](../..config)\n", encoding="utf-8")
    assert wl.main(["wiki_links.py", str(tmp_path)]) == 0


def test_a_name_starting_with_two_dots_is_not_above_the_repository():
    # From docs/sub, ../../..x resolves to the repository's ..x.
    assert wl.rewrite("[a](../../..x)", "sub") == f"[a]({R}/blob/main/..x)"
    assert wl.rewrite("[a](../../../x)", "sub") == "[a](../../../x)"


def test_parentheses_in_a_bare_target_are_kept():
    assert wl.rewrite("[api](../API_(v2).md)") == (
        f"[api]({R}/blob/main/API_(v2).md)")
    assert wl.rewrite("[api](API_(v2).md#x 'T')") == "[api](API_(v2)#x 'T')"
    assert wl.rewrite(r"[api](../API_\(v2\).md)") == (
        rf"[api]({R}/blob/main/API_\(v2\).md)")
    assert wl.rewrite("[a](Screens.md) and (b)") == "[a](Screens) and (b)"


def test_code_spans_fences_and_escaped_openers_are_left_alone():
    text = ("`[x](../NOTICE)` and ``a ` [x](../NOTICE) ``\n"
            "\\[x](../NOTICE)\n"
            "```\n[x](../NOTICE)\n[d]: ../README.md\n```\n"
            "~~~~ md\n[x](../NOTICE)\n~~~\n[y](../NOTICE)\n~~~~\n"
            "after [real](../NOTICE)\n")
    out = wl.rewrite(text)
    assert out == text.replace("after [real](../NOTICE)",
                               f"after [real]({R}/blob/main/NOTICE)")
    assert wl.verify_text(out) == []
    assert wl.stranded(out) == []


def test_an_escaped_backslash_does_not_escape_the_opener():
    assert wl.rewrite("\\\\[x](../NOTICE)") == (
        f"\\\\[x]({R}/blob/main/NOTICE)")


def test_an_unclosed_fence_runs_to_the_end_of_the_page():
    text = "[a](../NOTICE)\n```\n[b](../NOTICE)\n"
    assert wl.rewrite(text) == (
        f"[a]({R}/blob/main/NOTICE)\n```\n[b](../NOTICE)\n")


def test_verify_ignores_code_and_fails_on_a_real_survivor(tmp_path, capsys):
    page = tmp_path / "Home.md"
    page.write_text("`[x](../NOTICE)`\n```\n[a](B.md)\n```\n",
                    encoding="utf-8")
    assert wl.main(["wiki_links.py", "--verify", str(tmp_path)]) == 0
    page.write_text("[a](B.md)\n[b][c]\n\n[c]: ../NOTICE\n",
                    encoding="utf-8")
    assert wl.main(["wiki_links.py", "--verify", str(tmp_path)]) == 1
    err = capsys.readouterr().err
    assert "Home.md:1: B.md is" in err and "Home.md:4: ../NOTICE is" in err
    assert page.read_text(encoding="utf-8").startswith("[a](B.md)")


def test_an_image_by_reference_gets_the_raw_url():
    text = ("![logo][Asset] ![Pic][] ![shot] [l][doc]\n\n"
            "[asset]: ../hardware/logo.png\n"
            "[pic]: ../hardware/pic.png\n"
            "[shot]: ../hardware/shot.png\n"
            "[doc]: ../README.md\n")
    out = wl.rewrite(text)
    assert f"[asset]: {R}/raw/main/hardware/logo.png\n" in out
    assert f"[pic]: {R}/raw/main/hardware/pic.png\n" in out
    assert f"[shot]: {R}/raw/main/hardware/shot.png\n" in out
    assert f"[doc]: {R}/blob/main/README.md\n" in out


def test_a_definition_used_by_an_image_and_a_link_is_raw():
    out = wl.rewrite("![a][x] [b][x]\n\n[x]: ../hardware/a.png\n")
    assert f"[x]: {R}/raw/main/hardware/a.png" in out


def test_brackets_in_a_label_nest_to_any_depth():
    assert wl.rewrite("[outer [middle [inner]]](../README.md)") == (
        f"[outer [middle [inner]]]({R}/blob/main/README.md)")
    assert wl.rewrite("![a [b [c]]](../hardware/x.png)") == (
        f"![a [b [c]]]({R}/raw/main/hardware/x.png)")


def test_a_scheme_with_plus_dash_or_dot_is_a_url():
    text = ("[a](git+https://x.org/a.md) [b](svn+ssh://x/b.md) "
            "[c](a-b.c://x/d.md)\n\n[d]: git+https://x.org/e.md\n")
    assert wl.rewrite(text) == text
    assert wl.stranded(text) == []


def test_a_definition_target_and_title_on_later_lines():
    text = ("[legal]:\n  ../README.md\n  'title'\n"
            "[b]: ../NOTICE\n  \"t\"\n"
            "[c]:\n<../a b.md>\n"
            "[s]:\n  Screens.md\n")
    assert wl.rewrite(text) == (
        f"[legal]:\n  {R}/blob/main/README.md\n  'title'\n"
        f"[b]: {R}/blob/main/NOTICE\n  \"t\"\n"
        f"[c]:\n<{R}/blob/main/a b.md>\n"
        "[s]:\n  Screens\n")
    assert wl.stranded("[legal]:\n  ../README.md\n") == ["../README.md"]
    assert wl.verify_text("[s]:\n  Screens.md\n\n[s]\n")[0][1] == "Screens.md"


def test_an_image_in_a_link_is_rewritten_too():
    out = wl.rewrite("[![logo](../hardware/logo.png)](Screens.md)")
    assert out == f"[![logo]({R}/raw/main/hardware/logo.png)](Screens)"
    out = wl.rewrite("[![logo][a]](Screens.md)\n\n[a]: ../hardware/l.png\n")
    assert f"[a]: {R}/raw/main/hardware/l.png" in out


def test_an_escaped_image_marker_makes_a_link_not_an_image():
    assert wl.rewrite(r"\![doc](../README.md)") == (
        rf"\![doc]({R}/blob/main/README.md)")
    out = wl.rewrite("\\![asset]\n\n[asset]: ../README.md\n")
    assert f"[asset]: {R}/blob/main/README.md" in out
    out = wl.rewrite("\\\\![asset]\n\n[asset]: ../hardware/a.png\n")
    assert f"[asset]: {R}/raw/main/hardware/a.png" in out


def test_a_fence_in_a_block_quote_hides_its_links():
    text = "> ```\n> [x](../NOTICE)\n> ```\n[y](../NOTICE)\n"
    assert wl.rewrite(text) == (
        f"> ```\n> [x](../NOTICE)\n> ```\n[y]({R}/blob/main/NOTICE)\n")
    nested = ">> ~~~\n>> [x](../NOTICE)\n>> ~~~\n"
    assert wl.rewrite(nested) == nested


def test_an_external_image_does_not_claim_a_definition():
    text = ("![manual](https://example.com/logo.png)\n"
            "[manual]\n\n[manual]: ../README.md\n")
    assert f"[manual]: {R}/blob/main/README.md" in wl.rewrite(text)


def test_an_escaped_bracket_stays_in_the_link_text():
    assert wl.rewrite(r"[a \] b](Screens.md)") == r"[a \] b](Screens)"
    assert wl.rewrite(r"[a \\](Screens.md)") == r"[a \\](Screens)"


def test_a_fence_indented_four_spaces_is_code_not_a_fence():
    text = "    ```\n[x](../NOTICE)\n"
    assert wl.rewrite(text) == f"    ```\n[x]({R}/blob/main/NOTICE)\n"
    three = "   ```\n[x](../NOTICE)\n"
    assert wl.rewrite(three) == three


def test_a_line_ending_may_follow_the_opening_parenthesis():
    assert wl.rewrite("[notice](\n../NOTICE)") == (
        f"[notice](\n{R}/blob/main/NOTICE)")
    assert wl.rewrite("[a](\n  Screens.md\n)") == "[a](\n  Screens\n)"


def test_verify_reads_the_page_with_a_parser(tmp_path, capsys):
    def verdict(text):
        (tmp_path / "Home.md").write_text(text, encoding="utf-8")
        return wl.main(["wiki_links.py", "--verify", str(tmp_path)])

    assert verdict("[a](Screens) [b](#x) [c](https://e.com/a.md)\n"
                   f"![i](pic.png) ![r]({R}/raw/main/hardware/a.png)\n") == 0
    # What the rewriter misses, each one wrong on the wiki.
    for bad in ("[a](B.md)\n", "[a](../NOTICE)\n", f"[a]({R}/raw/main/x)\n",
                "![i](../hardware/a.png)\n", f"![i]({R}/blob/main/a.png)\n",
                "[b][c]\n\n[c]: ../NOTICE\n", "text\n\n[a](B.md#top)\n"):
        assert verdict(bad) == 1, bad
    err = capsys.readouterr().err
    assert "Home.md:1: B.md is a page name with .md" in err
    assert "Home.md:3: B.md#top" in err
    assert "above docs/" in err and "/raw/main/" in err
    # Code is not a link.
    assert verdict("`[x](B.md)`\n\n    [y](B.md)\n") == 0


def test_verify_judges_references_and_subdirectories():
    assert wl.verify_text("[a][b]\n\n[b]: C.md\n")[0][1] == "C.md"
    assert wl.verify_text("[a](Sub/P.md)\n", "")[0][1] == "Sub/P.md"
    assert wl.verify_text("[a](../P)\n", "sub") == []
    assert wl.verify_text("[a](../../P.md)\n", "sub")[0][2] == (
        "a relative target above docs/")


# The parser gate: each page against its rewrite, as markdown-it-py reads
# them.  A case the scanner reads right passes it; one it misreads fails it
# with the page, the line and both values.

def converted(tmp_path, text):
    """Exit code, error output and page text after a rewrite of a page."""
    page = tmp_path / "Home.md"
    page.write_text(text, encoding="utf-8")
    return wl.main(["wiki_links.py", str(tmp_path)]), page


def gated(text):
    out = wl.rewrite(text)
    return out, wl.gate(text, out)


def test_the_gate_passes_a_rewrite_of_every_kind():
    text = ("[a](Screens.md#x) [b](<Servo Page.md> 't') ![i](img/a.png)\n"
            "[n](../NOTICE) [d](../protocols/kst) ![l](../hardware/l.png)\n"
            "[api](../API_\\(v2\\).md) [r][x] ![s][x]\n\n"
            "[x]: ../hardware/a.png\n")
    out, diffs = gated(text)
    assert out != text and diffs == []


def test_the_gate_fails_on_a_missed_link_and_on_altered_text():
    text = "Text.\n\n[a](../NOTICE) and `code`\n"
    missed = text
    assert wl.gate(text, missed) == [(
        3, f"link_open href, wanted {R}/blob/main/NOTICE",
        "../NOTICE", "../NOTICE")]
    altered = wl.rewrite(text).replace("code", "c0de")
    assert wl.gate(text, altered) == [(3, "code_inline content", "code",
                                       "c0de")]


def test_a_failing_gate_names_page_line_and_both_values(tmp_path, capsys):
    code, page = converted(tmp_path, "Intro.\n\n> [ref]: ../README.md\n\n"
                           "[ref]\n")
    assert code == 1
    err = capsys.readouterr().err
    assert (f"Home.md:3: link_open href, wanted {R}/blob/main/README.md: "
            "before '../README.md', after '../README.md'") in err
    assert page.read_text(encoding="utf-8").startswith("Intro.")
    assert wl.main(["wiki_links.py", "--check", str(tmp_path)]) == 1


# One case for each construct of review round 7.

def test_r7_a_fence_indented_four_spaces_keeps_the_next_link():
    out, diffs = gated("    ```\n[x](Screens.md)\n")
    assert out == "    ```\n[x](Screens)\n" and diffs == []


def test_r7_a_line_ending_before_the_destination():
    out, diffs = gated("[notice](\n../README.md)\n")
    assert out == f"[notice](\n{R}/blob/main/README.md)\n" and diffs == []


def test_r7_a_label_over_two_lines_fails_the_gate(tmp_path, capsys):
    text = "[Foo\n  bar]: ../README.md\n\n[Baz][Foo bar]\n"
    assert converted(tmp_path, text)[0] == 1
    assert "Home.md:2: link_open href" in capsys.readouterr().err


def test_r7_a_link_in_a_raw_html_block_fails_the_gate(tmp_path, capsys):
    text = "<pre>\n[x](../NOTICE)\n</pre>\n"
    code, page = converted(tmp_path, text)
    assert code == 1
    assert "Home.md:1: html_block content" in capsys.readouterr().err
    assert page.read_text(encoding="utf-8") == text


def test_r7_an_escaped_backslash_leaves_the_backtick_to_open_a_span():
    text = "\\\\`[x](../NOTICE)` [y](../NOTICE)\n"
    out, diffs = gated(text)
    assert out == f"\\\\`[x](../NOTICE)` [y]({R}/blob/main/NOTICE)\n"
    assert diffs == []
    # An odd run escapes the backtick, and the link is a link.
    out, diffs = gated("\\`[x](../NOTICE)`\n")
    assert out == f"\\`[x]({R}/blob/main/NOTICE)`\n" and diffs == []
    # The rest of an escaped run still opens a span.
    text = "\\```[x](../NOTICE)``\n"
    assert gated(text) == (text, [])


def test_r7_a_definition_line_in_a_paragraph_fails_the_gate(tmp_path,
                                                             capsys):
    text = "paragraph\n[ref]: ../README.md\n\n[ref]\n"
    code, page = converted(tmp_path, text)
    assert code == 1
    err = capsys.readouterr().err
    assert ("Home.md:2: text content: before '[ref]: ../README.md', after "
            f"'[ref]: {R}/blob/main/README.md'") in err
    assert page.read_text(encoding="utf-8") == text


def test_r7_a_definition_in_a_container_fails_the_gate(tmp_path, capsys):
    for text in ("[ref]\n\n> [ref]: ../README.md\n",
                 "[ref]\n\n- [ref]: ../README.md\n"):
        assert converted(tmp_path, text)[0] == 1, text
        assert "Home.md:3: link_open href" in capsys.readouterr().err


def test_r7_a_uri_autolink_is_left_whole():
    text = ("<http://example.com/[x](../NOTICE)> <a@b.org> "
            "[y](../NOTICE)\n")
    out, diffs = gated(text)
    assert out == text.replace("[y](../NOTICE)",
                               f"[y]({R}/blob/main/NOTICE)")
    assert diffs == []


def test_r7_a_no_break_space_is_not_label_white_space(tmp_path, capsys):
    text = "![img][a\u00a0b] [doc][a b]\n\n[a b]: ../README.md\n"
    assert wl.norm("a\u00a0b") != wl.norm("a b") == wl.norm(" A\t\nb ")
    # CommonMark leaves the image undefined; the link gets the blob form.
    assert f"[a b]: {R}/blob/main/README.md" in wl.rewrite(text)
    # markdown-it-py matches the image to the definition, so the page fails.
    assert converted(tmp_path, text)[0] == 1
    assert "Home.md:3: image src" in capsys.readouterr().err


def test_r7_a_quoted_fence_ends_with_its_block_quote():
    for text in ("> ```\n> code\n\n[x](../NOTICE)\n",
                 "> ```\n> code\n[x](../NOTICE)\n",
                 ">> ~~~\n>> code\n> [x](../NOTICE)\n"):
        out, diffs = gated(text)
        assert out == text.replace("](../NOTICE)",
                                   f"]({R}/blob/main/NOTICE)"), text
        assert diffs == [], text
    # Inside the quote the fence still holds; a deeper marker is content.
    text = "> ```\n> >\n> [x](../NOTICE)\n> ```\n"
    assert gated(text) == (text, [])


def test_r7_a_blank_line_before_a_title_ends_the_link():
    text = '[x](Screens.md\n\n"title")\n'
    assert gated(text) == (text, [])
    out, diffs = gated('[x](Screens.md\n"title")\n')
    assert out == '[x](Screens\n"title")\n' and diffs == []
    text = '[x](Screens.md "a\n\nb")\n'
    assert gated(text) == (text, [])


def test_verify_permits_a_link_that_shares_a_definition_with_an_image():
    text = "![a][x] [b][x]\n\n[x]: ../hardware/a.png\n"
    out = wl.rewrite(text)
    assert f"[x]: {R}/raw/main/hardware/a.png" in out
    assert wl.verify_text(out) == []
    assert wl.verify_text("[b][x]\n\n[x]: " + R + "/raw/main/a.png\n")


def test_a_query_string_stays_out_of_the_directory_probe(tmp_path):
    (tmp_path / "protocols" / "kst").mkdir(parents=True)
    (tmp_path / "docs").mkdir()
    out = wl.rewrite("[d][k] [i](../protocols/kst?tab=readme)\n\n"
                     "[k]: ../protocols/kst?tab=readme-ov-file\n",
                     root=tmp_path)
    assert f"[k]: {R}/tree/main/protocols/kst/?tab=readme-ov-file" in out
    assert f"[i]({R}/tree/main/protocols/kst/?tab=readme)" in out


def test_a_query_string_is_not_normalised_with_the_path(tmp_path):
    (tmp_path / "protocols" / "kst").mkdir(parents=True)
    (tmp_path / "docs").mkdir()
    out = wl.rewrite(
        "[a](../README.md?a=b/../c) [b](../protocols/kst?next=/../x) "
        "[c](../README.md?u=http://x/y)\n", root=tmp_path)
    assert f"[a]({R}/blob/main/README.md?a=b/../c)" in out
    assert f"[b]({R}/tree/main/protocols/kst/?next=/../x)" in out
    assert f"[c]({R}/blob/main/README.md?u=http://x/y)" in out


def test_a_query_string_does_not_decide_whether_a_target_leaves_docs():
    assert not wl.leaves_docs("page.md?a=/../../x", "")
    assert wl.leaves_docs("../README.md?a=b", "")


def test_verify_accepts_a_page_whose_title_ends_in_md(tmp_path, capsys):
    (tmp_path / "Guide.md.md").write_text("x\n", encoding="utf-8")
    (tmp_path / "Home.md").write_text("[g](Guide.md.md)\n", encoding="utf-8")
    assert wl.main(["wiki_links.py", str(tmp_path)]) == 0
    assert (tmp_path / "Home.md").read_text(encoding="utf-8") == (
        "[g](Guide.md)\n")
    assert wl.main(["wiki_links.py", "--verify", str(tmp_path)]) == 0
    (tmp_path / "Home.md").write_text("[g](Other.md)\n", encoding="utf-8")
    assert wl.main(["wiki_links.py", "--verify", str(tmp_path)]) == 1
