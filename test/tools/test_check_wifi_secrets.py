"""Tests of tools/check_wifi_secrets.py.

SPDX-License-Identifier: MIT
"""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools"))

import check_wifi_secrets as tool  # noqa: E402


def test_a_log_call_naming_a_value_fails():
    """A log line with a kept value fails, also over several lines."""
    bad = tool.check_file(
        "shared/webaccess/wa_session.c",
        'void f(void)\n{\n    ESP_LOGI(TAG, "%s",\n      c->ap_pass);\n}\n')
    assert len(bad) == 1
    assert "log or console" in bad[0]
    assert tool.check_file(
        "firmware/panel/main/panel_update.c",
        'void f(void)\n{\n    printf("%s", a->w.code);\n}\n')


def test_punctuation_in_a_format_string_hides_no_log_call():
    """A URL, a semicolon, a brace and a quote in the format split nothing."""
    for fmt in ('"http://panel/code=%s"', '"a;b %s"', '"{%s}"', r'"\"//;%s"'):
        bad = tool.check_file(
            "shared/webaccess/wa_session.c",
            f"void f(void)\n{{\n    ESP_LOGI(TAG, {fmt}, c->web_code);\n}}\n")
        assert len(bad) == 1, fmt
        assert "log or console" in bad[0]


def test_a_name_in_a_string_or_a_character_is_no_use():
    """Text about a value is not a value; '"' opens no string."""
    assert not tool.check_file("shared/webaccess/wa_api.c",
                               'const char *k = "web_code";\n')
    bad = tool.check_file("shared/webaccess/wa_api.c",
                          "char q = '\"'; void f(void) { g(c->web_code); }\n")
    assert len(bad) == 1


def test_a_use_outside_the_list_fails():
    """The file API may not name a value at all."""
    bad = tool.check_file("shared/webaccess/wa_api.c",
                          "void f(void) { g(c->web_code); }\n")
    assert len(bad) == 1
    assert "outside" in bad[0]


def test_what_is_allowed_passes():
    """A use in a listed source with no log call, and a comment, pass."""
    assert not tool.check_file("shared/webaccess/wa_session.c",
                               "void f(void) { g(c->web_code); }\n")
    assert not tool.check_file("shared/webaccess/wa_api.c",
                               "/* web_code */ int x;\n// w.code\n")
    assert not tool.check_file("shared/x.c",
                               "void f(void) { ESP_LOGI(TAG, \"up\"); }\n")


def test_the_tree_holds_the_rule(capsys):
    """The repository as it stands."""
    assert tool.main([]) == 0
    assert "0 failure(s)" in capsys.readouterr().out
