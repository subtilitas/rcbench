"""Tests merge_texts.py, old_texts.py and c_tables.py.

The per-file form of the interface's words becomes the one table, and a
branch still in the per-file form merges into it.

SPDX-License-Identifier: MIT
"""

import os
import shutil
import subprocess

import pytest

import c_tables as ct
import merge_texts as mt
import old_texts as ot

HAVE_CC = bool(shutil.which("cc") or shutil.which("gcc"))
needs_cc = pytest.mark.skipif(not HAVE_CC, reason="no C compiler on PATH")


def test_a_compiler_exists_when_CI_is_set():
    """The cases marked needs_cc skip without one; in CI that is a failure,
    not a pass."""
    if os.environ.get("CI") and not HAVE_CC:
        pytest.fail("CI is set and no C compiler is on PATH")


# A tree in the per-file form, as small as the tool's reading allows.
OLD = {
    ot.SETTINGS_H: """\
typedef enum {
    SET_CAT_APP = 0,
    SET_CAT_SENSORS,
    SET_CAT_COUNT
} setting_cat_t;

typedef enum {
    /* --- application ------------------------------------------------ */
    SET_PACK_CELLS = 0,
    SET_THEME,
    /* SET_INA_FIT_<a> for part a. */
    SET_INA_FIT_0, SET_INA_FIT_1,
    SET_INA_FIT_LAST = SET_INA_FIT_1,
    SETTING_COUNT
} setting_id_t;
""",
    mt.SETTINGS_C: """\
#include "settings_sensors.h"

static const char *const k_theme[] = { "DARK", "LIGHT" };

ENUM_OPTS_LINE

static const setting_def_t k_defs[SETTING_COUNT] = {
    [SET_PACK_CELLS] = {
        "pack_cells", "Cells",
        "Series count", "S",
        SET_CAT_APP, SET_TYPE_INT, 1, 14, 1, 6, NULL, 0 },
    [SET_THEME] = {
        "theme", "Theme", "", "",
        SET_CAT_APP, SET_TYPE_ENUM, 0, 0, 1, 0, ENUM_OPTS(k_theme) },
};

static const char *const k_cat_names[SET_CAT_COUNT] = {
    [SET_CAT_APP]     = "PANEL",
    [SET_CAT_SENSORS] = "SENSORS",
};
""",
    mt.SENSORS_C: """\
#include "settings_sensors.h"

/* What a part watches. */
static const char *const k_watch[2] = { "NONE", "OUT 1" };

#define WATCH_OPTS .options = k_watch, .option_count = (uint8_t)SENS_W_COUNT

#define AT(id) ((int)(id) - (int)SET_INA_FIT_0)

#define FIT_ROW(a)                                                         \\
    [AT(SET_INA_FIT(a))] = {                                               \\
        "ina_fit_" #a, "0x4" #a " fitted", "An INA3221 here", "",          \\
        SET_CAT_SENSORS, SET_TYPE_ENUM, 0, 0, 1, 0, WATCH_OPTS, 0.0f }

static const setting_def_t k_rows[2] = {
    FIT_ROW(0), FIT_ROW(1),
};
""",
    mt.OLD_DEF: """\
/*
 * The English.
 */

/* ---------------------------------------------------------- shared words */
UI_TEXT(ON,                 0, "ON")
#ifdef FEATURE
UI_TEXT(INJ,               20, "Injector")
#endif
/* Two numbers. */
UI_TEXT(FMT,                0, "%u of %u")
UI_TEXT(NEW,                8, "NEW")
""",
    mt.OLD_DE: """\
#include "ui_text.h"

static const char *const k_text[TX_COUNT] = {
    /* ------------------------------------------------------ shared words */
    [TX_ON]                     = "EIN",
#ifdef FEATURE
    [TX_INJ]                    = "Injektor",
#endif
    /* the formats */
    /* The German puts the words between. */
    [TX_FMT]                    = "%u von "
                                  "%u",
};

#define DE_INA_FIT(a) [SET_INA_FIT(a)] = "0x4" #a " bestückt",
#define DE_INA_LABELS DE_INA_FIT(0) DE_INA_FIT(1)
#define DE_INA_FIT_HELP(a) [SET_INA_FIT(a)] = "Ein INA3221 hier",
#define DE_INA_HELP DE_INA_FIT_HELP(0) DE_INA_FIT_HELP(1)
#define DE_INA_W(a) [SET_INA_FIT(a)] = k_watch,

static const char *const k_label[SETTING_COUNT] = {
    [SET_PACK_CELLS]          = "Zellen",
    [SET_THEME]               = "Design",
    DE_INA_LABELS
};

static const char *const k_help[SETTING_COUNT] = {
    [SET_PACK_CELLS]          = "Zellen in Serie",
    [SET_THEME]               = "",
    DE_INA_HELP
};

static const char *const k_theme[]  = { "DUNKEL", "HELL" };
static const char *const k_watch[2] = { "KEINE", "AUSGANG 1" };

static const char *const *const k_options[SETTING_COUNT] = {
    [SET_THEME]               = k_theme,
    DE_INA_W(0) DE_INA_W(1)
};

static const char *const k_category[SET_CAT_COUNT] = {
    [SET_CAT_APP]             = "PANEL",
    [SET_CAT_SENSORS]         = "SENSOREN",
};

static const char *const k_servo[SERVO_STR_COUNT] = {
    [SERVO_STR_PASS]          = "BESTANDEN",
};
""",
    mt.SERVO_C: """\
#include "servo_str.h"

static const char *const k_str[SERVO_STR_COUNT] = {
    [SERVO_STR_NONE]   = "",
    /* The verdicts. */
    [SERVO_STR_PASS]   = "PASS",
};
""",
    mt.WA_C: """\
#include "wa_page.h"

/* The page's texts, English and German. */
typedef enum {
    P_NAME = 0,
    P_COUNT
} p_text_t;

static const char *const k_en[P_COUNT] = {
    [P_NAME]  = "Name",
};

static const char *const k_de[P_COUNT] = {
    [P_NAME]  = "Name",
};

static const char *pt(bool de, p_text_t t);
""",
}


# The schema's macro as the per-file form wrote it, on one line.
OLD[mt.SETTINGS_C] = OLD[mt.SETTINGS_C].replace(
    "ENUM_OPTS_LINE", "#define ENUM_OPTS(a) .options = (a), .option_count = "
    "(uint8_t)(sizeof(a) / sizeof((a)[0]))")


def convert(tree=None):
    """Converts a tree in the per-file form.

    Args:
        tree: The files by path; OLD when None.

    Returns:
        The converted files by path.
    """
    return mt.convert((tree or OLD).__getitem__)


def edited(path, old, new):
    """Copies OLD with one replacement in one file.

    Args:
        path: The file.
        old: Text the file holds.
        new: Its replacement.

    Returns:
        The edited tree.
    """
    tree = dict(OLD)
    assert old in tree[path]
    tree[path] = tree[path].replace(old, new)
    return tree


# ----------------------------------------------------------- c_tables


def test_a_literal_run_spans_comments_and_line_breaks():
    """Adjacent literals are read across comments and line breaks."""
    src = '= "a" /* x */\n   "b\\"c" , "d"'
    parts, end = ct.literal_run(src, 1)
    assert parts == ['"a"', '"b\\"c"']
    assert src[end:].lstrip().startswith(",")
    assert ct.literal_run("= 12", 1) == ([], 1)


def test_an_unterminated_string_is_an_error():
    """A literal cut by a line break is refused."""
    with pytest.raises(ct.TableError):
        ct.literal_run('"abc\n"', 0)


def test_elements_split_at_top_level_commas_only():
    """Commas in strings, calls and inner braces split nothing."""
    src = '{ "a,b", f(1, 2), { 3, 4 }, \\\n x }'
    elems = ct.elements(src, ct.code_mask(src), 0)
    assert [src[a:b].strip(" \\\n") for a, b in elems] == [
        '"a,b"', "f(1, 2)", "{ 3, 4 }", "x"]


def test_entries_keep_order_comments_and_conditions():
    """Entries, names, conditions and macros come in their order."""
    body = """
    /* one */
    [A] = "a",
#ifdef X
    [B] = k_b,
#endif
    MACRO
"""
    items = ct.entries(body)
    assert items[0] == ("entry", "A", ['"a"'], ["/* one */"])
    assert items[1] == ("pp", "#ifdef X")
    assert items[2] == ("ref", "B", "k_b")
    assert items[3] == ("pp", "#endif")
    assert items[4] == ("macro", "MACRO")


def test_join_keeps_literals_apart_where_an_escape_could_run_on():
    """Literals join unless an escape could run across the joint."""
    assert ct.join(['"0x4"', '"0"', '" x"']) == ['"0x40 x"']
    assert ct.join(['"\\x4"', '"0"']) == ['"\\x4"', '"0"']


# ------------------------------------------------------------ convert


@needs_cc
def test_every_text_lands_in_one_entry_with_both_columns():
    """A screen string is one entry with its English and German."""
    table = convert()[mt.NEW_DEF]
    assert 'UI_TEXT(ON, 0,\n    "ON",\n    "EIN")' in table
    # A multi-literal German keeps its literals; the German comment that
    # explains goes along, the one that only marks a place does not.
    assert ('/* The German puts the words between. */\n'
            'UI_TEXT(FMT, 0,\n    "%u of %u",\n    "%u von "\n    "%u")'
            in table)
    assert "the formats" not in table
    assert 'UI_TEXT(NEW, 8,\n    "NEW",\n    NULL)' in table
    assert '#ifdef FEATURE\nUI_TEXT(INJ, 20,' in table
    assert '/* Two numbers. */' in table


@needs_cc
def test_the_settings_words_leave_the_schema_for_the_table():
    """Labels, help, categories and options move to the table."""
    out = convert()
    table = out[mt.NEW_DEF]
    assert ('UI_SETTING_LABEL(SET_PACK_CELLS,\n    "Cells",\n    "Zellen")'
            in table)
    assert ('UI_SETTING_HELP(SET_PACK_CELLS,\n    "Series count",\n'
            '    "Zellen in Serie")' in table)
    # Rows a macro writes become one entry each, the setting by its name.
    assert ('UI_SETTING_LABEL(SET_INA_FIT_1,\n    "0x41 fitted",\n'
            '    "0x41 bestückt")' in table)
    assert ('UI_SETTING_CATEGORY(SET_CAT_SENSORS,\n    "SENSORS",\n'
            '    "SENSOREN")' in table)
    assert 'UI_OPTIONS(theme)\nUI_OPTION("DARK", "DUNKEL")' in table
    assert ('/* What a part watches. */\nUI_OPTIONS(watch)\n'
            'UI_OPTION("NONE", "KEINE")' in table)
    assert "/* --- application" in table
    settings = out[mt.SETTINGS_C]
    assert '"pack_cells", "S",\n        SET_CAT_APP' in settings
    assert "Cells" not in settings and "k_theme" not in settings
    assert "ENUM_OPTS(theme)" in settings
    assert "settings_opts_n_##name" in settings
    sensors = out[mt.SENSORS_C]
    assert '"ina_fit_" #a, "",' in sensors and "fitted" not in sensors
    assert "settings_opts_watch" in sensors and "k_watch" not in sensors
    assert all(len(ln) == mt.BACKSLASH_COL + 1
               for ln in sensors.splitlines() if ln.endswith("\\"))


@needs_cc
def test_the_servo_words_and_the_page_read_the_table():
    """The servo words and the Wi-Fi page read the table."""
    out = convert()
    table = out[mt.NEW_DEF]
    assert ('/* The verdicts. */\nUI_SERVO_TEXT(SERVO_STR_PASS,\n'
            '    "PASS",\n    "BESTANDEN")' in table)
    assert 'UI_SERVO_TEXT(SERVO_STR_NONE,\n    "",\n    NULL)' in table
    assert 'UI_WEB_TEXT(P_NAME,\n    "Name",\n    "Name")' in table
    assert '"PASS"' not in out[mt.SERVO_C]
    assert "[id] = (en)," in out[mt.SERVO_C]
    assert "P_COUNT\n} p_text_t;" in out[mt.WA_C]
    assert '"Name"' not in out[mt.WA_C]


@needs_cc
def test_the_table_defines_and_undefines_every_macro():
    """Every entry macro has an empty default and is undefined at the end."""
    table = convert()[mt.NEW_DEF]
    for name, args in mt.MACROS:
        assert f"#ifndef {name}\n#define {name}({args})\n#endif" in table
        assert table.rstrip().endswith("#undef UI_WEB_TEXT")
        assert f"#undef {name}\n" in table


@needs_cc
def test_german_without_english_is_refused():
    """German for an ID with no English stops the conversion."""
    tree = edited(mt.OLD_DE, '    [TX_ON]', '    [TX_GONE] = "WEG",\n'
                  '    [TX_ON]')
    with pytest.raises(mt.ConvertError, match="TX_GONE"):
        convert(tree)


@needs_cc
def test_a_german_entry_under_another_condition_is_refused():
    """German under another #if than its English stops it."""
    tree = edited(mt.OLD_DE, "#ifdef FEATURE\n", "#ifdef OTHER\n")
    with pytest.raises(mt.ConvertError, match="TX_INJ"):
        convert(tree)


@needs_cc
def test_a_german_list_of_another_length_is_refused():
    """A German option list of another length stops it."""
    tree = edited(mt.OLD_DE, '{ "DUNKEL", "HELL" }', '{ "DUNKEL" }')
    with pytest.raises(mt.ConvertError, match="theme"):
        convert(tree)


@needs_cc
def test_a_setting_without_english_is_refused():
    """A setting with no English label stops it."""
    tree = edited(ot.SETTINGS_H, "    SET_THEME,\n",
                  "    SET_THEME,\n    SET_EXTRA,\n")
    with pytest.raises(mt.ConvertError, match="SET_EXTRA"):
        convert(tree)


# -------------------------------------------------------------- merge


def git(cwd, *args):
    """Runs git and fails the case on a non-zero exit.

    Args:
        cwd: The repository.
        *args: git's arguments.

    Returns:
        git's output, stripped.
    """
    return subprocess.run(["git", *args], cwd=cwd, check=True,
                          capture_output=True, text=True).stdout.strip()


def write(root, files):
    """Writes files under a root.

    Args:
        root: The directory.
        files: The texts by path.
    """
    for path, text in files.items():
        f = root / path
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text(text, encoding="utf-8")


@needs_cc
def test_a_branch_in_the_old_form_merges_into_the_table(tmp_path,
                                                         monkeypatch):
    """A branch in the per-file form merges into the converted tree."""
    monkeypatch.setattr(mt, "REPO", tmp_path)
    git(tmp_path, "init", "-q", "-b", "main")
    git(tmp_path, "config", "user.name", "t")
    git(tmp_path, "config", "user.email", "t@t")
    write(tmp_path, OLD)
    git(tmp_path, "add", "-A")
    git(tmp_path, "commit", "-q", "-m", "base")
    base = git(tmp_path, "rev-parse", "HEAD")

    # Theirs: a new string with its German, and a label reworded inside a
    # schema row, which a plain merge cannot place.
    git(tmp_path, "checkout", "-q", "-b", "theirs")
    theirs = dict(OLD)
    theirs[mt.OLD_DEF] += 'UI_TEXT(MORE,               0, "MORE")\n'
    theirs[mt.OLD_DE] = theirs[mt.OLD_DE].replace(
        "                                  \"%u\",\n",
        "                                  \"%u\",\n"
        '    [TX_MORE]                   = "MEHR",\n')
    theirs[mt.SETTINGS_C] = theirs[mt.SETTINGS_C].replace(
        '"pack_cells", "Cells",', '"pack_cells", "Cell count",')
    write(tmp_path, theirs)
    git(tmp_path, "commit", "-q", "-am", "theirs")

    # Ours: the conversion, then an edit of our own to the table.
    git(tmp_path, "checkout", "-q", "main")
    assert mt.main(["convert"]) == 0
    ours_def = tmp_path / mt.NEW_DEF
    ours_def.write_text(ours_def.read_text(encoding="utf-8").replace(
        "UI_TEXT(NEW, 8,", "UI_TEXT(NEW, 9,"), encoding="utf-8")
    git(tmp_path, "add", "-A")
    git(tmp_path, "commit", "-q", "-m", "ours")

    subprocess.run(["git", "merge", "--no-commit", "-q", "theirs"],
                   cwd=tmp_path, capture_output=True)
    assert mt.main(["merge", base, "theirs"]) == 0
    table = ours_def.read_text(encoding="utf-8")
    assert 'UI_TEXT(MORE, 0,\n    "MORE",\n    "MEHR")' in table
    assert 'UI_SETTING_LABEL(SET_PACK_CELLS,\n    "Cell count",' in table
    assert "UI_TEXT(NEW, 9," in table
    settings = (tmp_path / mt.SETTINGS_C).read_text(encoding="utf-8")
    assert '"pack_cells", "S",' in settings and "Cell" not in settings
    for path in mt.DELETED:
        assert not (tmp_path / path).exists()
    unmerged = {"DD", "AU", "UD", "UA", "DU", "AA", "UU"}
    status = git(tmp_path, "status", "--porcelain").splitlines()
    assert not [ln for ln in status if ln[:2] in unmerged], status
