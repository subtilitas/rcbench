#!/usr/bin/env python3
"""Move the interface's words from the per-file form into the one table.

The one table is shared/text/include/ui_text.def: every text with its
English and its German in one entry.  The per-file form it replaces held
them in seven places:

- shared/ui/include/ui_text.def: `UI_TEXT(id, cells, en)`, the screens'
  strings in English;
- shared/ui/ui_text_de.c: the German of those, and of the settings' labels,
  help lines, option names and categories and of the servo test's words;
- shared/settings/settings.c and settings_sensors.c: the settings' English,
  inside the schema rows and in the option arrays;
- shared/servo/servo_str.c: the servo test's English (k_str);
- shared/webaccess/wa_page.c: the Wi-Fi page's English and German.

    tools/merge_texts.py convert
        Converts the working tree in place: writes the one table, rewrites
        the four C files to read it and deletes the two old tables.
    tools/merge_texts.py merge BASE THEIRS
        During a merge of THEIRS, which still has the per-file form: converts
        BASE and THEIRS, carries THEIRS' changes since BASE into the one
        table and the four C files by a three-way merge with HEAD's, and
        deletes the old tables.  A change both sides made to the same lines
        is left with conflict markers.

A merge of a branch in the per-file form runs as:

    git merge --no-commit origin/main      # old-form files conflict
    tools/merge_texts.py merge "$(git merge-base HEAD origin/main)" origin/main
    # resolve what is left, build, test, commit

Each run needs a C compiler on PATH: the sensor rows and their German are
written by macros, which the preprocessor expands.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

from c_tables import TableError, dedent_comment, skip_space, table_body
from old_texts import (KEEP_COMMENT, OLD_DE, OLD_DEF, SENSORS_C, SERVO_C,
                       SETTINGS_C, WA_C, ConvertError, Texts, option_arrays,
                       schema_rows)

REPO = pathlib.Path(__file__).resolve().parent.parent

NEW_DEF = "shared/text/include/ui_text.def"

# The files a conversion writes, the one table first.
WRITTEN = (NEW_DEF, SETTINGS_C, SENSORS_C, SERVO_C, WA_C)
DELETED = (OLD_DEF, OLD_DE)

# The column a backslash ends a macro line at in settings_sensors.c.
BACKSLASH_COL = 75


def strip_rows(src: str) -> str:
    """Takes the label and the help line out of every schema row.

    Args:
        src: The C source.

    Returns:
        src with each row's second and third element removed.
    """
    for _, elems in reversed(schema_rows(src)):
        cut_from = elems[0][1] + 1
        cut_to = skip_space(src, elems[3][0])
        src = src[:cut_from] + " " + src[cut_to:]
    return src


def realign_backslashes(src: str) -> str:
    """Puts every macro line's backslash at BACKSLASH_COL.

    Args:
        src: The C source.

    Returns:
        src with the continuation backslashes in one column.
    """
    out = []
    for line in src.split("\n"):
        if line.endswith("\\") and not line.lstrip().startswith(("/*", "*")):
            body = line[:-1].rstrip()
            line = body.ljust(BACKSLASH_COL) + "\\" \
                if len(body) < BACKSLASH_COL else body + " \\"
        out.append(line)
    return "\n".join(out)


# ------------------------------------------------------------- writing


HEADER = """\
/**
 * @file
 * @brief Every word the interface shows or writes in the operator's
 *        language, one entry per text, each language in a column of its
 *        own.
 *
 *   UI_TEXT(ID, cells, en, de)             a screen's string, TX_ID
 *   UI_SETTING_LABEL(SET_..., en, de)      a setting's label on SETUP
 *   UI_SETTING_HELP(SET_..., en, de)       its help line
 *   UI_SETTING_CATEGORY(SET_CAT_..., en, de)
 *   UI_OPTIONS(name) ... UI_OPTIONS_END(name)
 *                                          an enum setting's options, one
 *                                          UI_OPTION(en, de) each, in order
 *   UI_SERVO_TEXT(SERVO_STR_..., en, de)   the servo test's words and report
 *   UI_WEB_TEXT(P_..., en, de)             the panel's Wi-Fi page
 *
 * The English column comes first, then the German, each on a line of its
 * own.  NULL in a language's column shows the English.
 *
 * A file that reads the table defines the macros it needs and includes it;
 * this file defines the others as empty and undefines all of them at its
 * end.  ui_text.h makes the TX_ IDs, ui_text.c the lookup and every
 * language's tables, settings.c the settings' English, servo_str.c the
 * servo test's and wa_page.c the Wi-Fi page's.  docs/Language.md says how
 * to add a text and a language.
 *
 * cells is the width of the field a UI_TEXT string is drawn into, in cells
 * of its face, for a string no screenshot reaches: test_text holds every
 * language's entry to it.  0 means a rendered screen shows the string and
 * `tools/render_ui.py --fit` measures it there.  The fit check fails on an
 * ID that is neither.
 *
 * A format string's conversions are part of its contract: a translation
 * converts the same arguments in the same order (test_text checks).
 *
 * The text faces hold printable ASCII (0x20 to 0x7E) and the German
 * letters Ä Ö Ü ä ö ü ß; any other code point draws as '?'.  test_text
 * holds every string the panel draws to that set.  Text is UTF-8.
 *
 * What stays English in every language is not in the table and is written
 * where it is drawn: ARM, DISARM, STOP, the screen titles, protocol and
 * mode names, units, and the field's own terms.
 *
 * The German: a label is a noun where a noun will do.  An instruction
 * speaks to the operator in the du-imperative (Prüfe, Tippe, Halte); no
 * "Sie".  The terms the field uses in English stay English: ARM, DISARM,
 * STOP, the screen titles, protocol and mode names, units, Sweep, Hold,
 * Endpoint, CSV.  Throttle is Gas, Frame Rate is Pulsrate, the link is the
 * Verbindung.  One German word per concept across every screen:
 * docs/Language.md holds the glossary.  An uppercase label writes the sharp
 * s as SS (SCHLIESSEN for schließen); mixed-case text writes ß.
 *
 * SPDX-License-Identifier: MIT
 */
"""

MACROS = (
    ("UI_TEXT", "id, cells, en, de"),
    ("UI_SETTING_LABEL", "id, en, de"),
    ("UI_SETTING_HELP", "id, en, de"),
    ("UI_SETTING_CATEGORY", "id, en, de"),
    ("UI_OPTIONS", "name"),
    ("UI_OPTION", "en, de"),
    ("UI_OPTIONS_END", "name"),
    ("UI_SERVO_TEXT", "id, en, de"),
    ("UI_WEB_TEXT", "id, en, de"),
)

SETTINGS_HEAD = """\
/* ================================================================ settings
 * The schema's rows, by setting_id_t (settings.h).  SETUP draws a label in
 * the heading face in 18 cells and its help in 36; the SUPPLY, SERVO and
 * ESC STICK categories are drawn on their own screens, whose fields
 * test_text holds them to. */
"""

CATEGORIES_HEAD = """\
/* --------------------------------------------------- the categories */
"""

OPTIONS_HEAD = """\
/* ------------------------------------------------------ the options
 * Each list is bound to its settings by the schema (ENUM_OPTS in
 * settings.c, the rows of settings_sensors.c).  A list whose words are
 * the same in every language -- the language names, addresses, channels,
 * baud rates -- has NULL German. */
"""

SERVO_HEAD = """\
/* ===================================================== the servo test
 * The SERVO screen's words for a run, and the TXT report (servo_report.c,
 * servo_limrun_report.c).  The CSV a run writes stays English
 * (servo_str()), so a tool reads every run alike.  The report is written
 * in the language the run started in.  The report's labels pad to column
 * 17 in English and to column 18 in German, because "Strombegrenzung:" is
 * 16 characters. */
"""

WEB_HEAD = """\
/* ===================================================== the Wi-Fi page
 * The page the panel serves on Wi-Fi (wa_page.c), in the language the
 * panel shows.  A browser draws it, so the fonts' letters do not limit
 * it. */
"""

INDENT = "    "


def column(parts: list[str] | None) -> list[str]:
    """Writes one language's column of an entry.

    Args:
        parts: The literals, or None for NULL.

    Returns:
        The column's lines, indented.
    """
    if parts is None:
        return [INDENT + "NULL"]
    return [INDENT + p for p in parts]


def entry(head: str, cols: list[list[str] | None]) -> str:
    """Writes one entry of the table, a line per language.

    Args:
        head: The macro and the columns before the languages, without the
            comma after them.
        cols: Each language's literals, None for NULL.

    Returns:
        The entry's text.
    """
    lines = [head + ","]
    for k, parts in enumerate(cols):
        rows = column(parts)
        rows[-1] += "," if k + 1 < len(cols) else ")"
        lines += rows
    return "\n".join(lines)


def write_table(t: Texts) -> str:
    """Writes the one table.

    Args:
        t: The texts of the per-file form.

    Returns:
        The text of shared/text/include/ui_text.def.

    Raises:
        ConvertError: An ID has German and no English, or its German is
            under another #if than its English.
    """
    out = [HEADER, "/** @brief The entry macros a reader leaves undefined, "
           "as empty ones. */"]
    for name, args in MACROS:
        out.append(f"#ifndef {name}\n#define {name}({args})\n#endif")
    out.append("")
    out.append("/* " + "=" * 64 + " screens */")
    seen = set()
    cond: list[str] = []
    for item in t.tx_items:
        if item[0] == "line":
            if item[1].startswith("#if"):
                cond.append(item[1].strip())
            elif item[1].startswith("#endif"):
                cond.pop()
            out.append(item[1])
            continue
        _, ident, cells, parts = item
        de = t.tx_de.get(ident)
        if de is not None:
            if de[2] != tuple(cond):
                raise ConvertError(f"TX_{ident}: under {list(cond)} in the "
                                   f"English, {list(de[2])} in the German")
            out += de[1]
        seen.add(ident)
        out.append(entry(f"UI_TEXT({ident}, {cells}",
                         [parts, de[0] if de else None]))
    for ident in t.tx_de:
        if ident not in seen:
            raise ConvertError(f"TX_{ident} has German and no English")
    out.append("")
    out.append(SETTINGS_HEAD.rstrip())
    for ident in t.settings:
        if ident in t.groups:
            out.append(t.groups[ident])
        out.append(entry(f"UI_SETTING_LABEL({ident}",
                         [t.label[ident], t.label_de.get(ident)]))
        out.append(entry(f"UI_SETTING_HELP({ident}",
                         [t.help[ident], t.help_de.get(ident)]))
    out.append("")
    out.append(CATEGORIES_HEAD.rstrip())
    for ident in t.cats:
        out.append(entry(f"UI_SETTING_CATEGORY({ident}",
                         [t.cat[ident], t.cat_de.get(ident)]))
    out.append("")
    out.append(OPTIONS_HEAD.rstrip())
    for name, comment, items in t.lists:
        short = name.removeprefix("k_")
        if comment:
            out.append(dedent_comment(comment.rstrip("\n")))
        out.append(f"UI_OPTIONS({short})")
        de = t.list_de.get(name)
        for k, parts in enumerate(items):
            en = " ".join(parts)
            de_text = " ".join(de[k]) if de else "NULL"
            out.append(f"UI_OPTION({en}, {de_text})")
        out.append(f"UI_OPTIONS_END({short})")
        out.append("")
    out.append(SERVO_HEAD.rstrip())
    for ident, parts, comments in t.servo:
        de = t.servo_de.get(ident)
        out += comments
        if de is not None:
            out += de[1]
        out.append(entry(f"UI_SERVO_TEXT({ident}",
                         [parts, de[0] if de else None]))
    out.append("")
    out.append(WEB_HEAD.rstrip())
    for ident, parts, comments in t.web:
        de = t.web_de[ident]
        out += [c for c in comments if "NOLINT" not in c
                and "three literals" not in c]
        out.append(entry(f"UI_WEB_TEXT({ident}", [parts, de[0]]))
    out.append("")
    for name, _ in MACROS:
        out.append(f"#undef {name}")
    return "\n".join(out) + "\n"


# --------------------------------------------------- the four C files


ENUM_OPTS = """\
/** @brief An enum row's options: list @p name of ui_text.def
 *  (settings_opts.h). */
#define ENUM_OPTS(name)                                                    \\
    .options = settings_opts_##name,                                       \\
    .option_count = (uint8_t)settings_opts_n_##name"""

CAT_NAMES = """\
#define UI_SETTING_CATEGORY(id, en, de) [id] = (en),
#include "ui_text.def"
"""

SERVO_TABLE = """\
#define UI_SERVO_TEXT(id, en, de) [id] = (en),
#include "ui_text.def"
"""

WEB_TABLES = """\
/** @brief The page's texts, P_<ID> for each UI_WEB_TEXT() of ui_text.def. */
typedef enum {
#define UI_WEB_TEXT(id, en, de) id,
#include "ui_text.def"
    P_COUNT
} p_text_t;

/** @brief The page's texts in English. */
static const char *const k_en[P_COUNT] = {
#define UI_WEB_TEXT(id, en, de) [id] = (en),
#include "ui_text.def"
};

#ifndef WA_PAGE_DE
/**
 * @brief The German entry of text @p id as the table gives it.
 *
 * test_wa_page_de defines it before it includes this file, to blank one
 * entry.
 */
#define WA_PAGE_DE(id, de) (de)
#endif

/** @brief The page's texts in German; NULL where the table has no German. */
static const char *const k_de[P_COUNT] = {
#define UI_WEB_TEXT(id, en, de) [id] = WA_PAGE_DE(id, de),
#include "ui_text.def"
};

/**
 * @brief One text of the page in the page's language.
 *
 * @param de German page.
 * @param t  The text.
 * @return The German entry, or the English where the German is NULL.
 */
static const char *pt(bool de, p_text_t t)
{
    return (de && k_de[t] != NULL) ? k_de[t] : k_en[t];
}
"""


def replace_body(src: str, name: str, body: str, brief: str = "") -> str:
    """Replaces the initialiser of a C array.

    Args:
        src: The C source.
        name: The array's name.
        body: The new text between its braces.
        brief: A Doxygen line to put before the array's declaration, or ""
            for none.

    Returns:
        The source with the array's body replaced.
    """
    a, b = table_body(src, name)
    src = src[:a] + "\n" + body + src[b:]
    if brief:
        decl = src.rindex("\n", 0, src.rindex(name, 0, a)) + 1
        src = src[:decl] + brief + "\n" + src[decl:]
    return src


def add_include(src: str, after: str, line: str) -> str:
    """Adds an #include after another one, once.

    Args:
        src: The C source.
        after: The line to add it after, newline included.
        line: The line to add, newline included.

    Returns:
        src with the line, or src as it is when it holds the line already.

    Raises:
        ConvertError: src lacks the line to add it after.
    """
    if line in src:
        return src
    if after not in src:
        raise ConvertError(f"no {after.strip()} to add {line} after")
    return src.replace(after, after + line, 1)


def convert_settings(src: str) -> str:
    """Converts settings.c: no option arrays, labels or help lines.

    Args:
        src: settings.c in the per-file form.

    Returns:
        settings.c reading its words from the one table.

    Raises:
        TableError: The source is not in the expected shape.
    """
    for m in reversed(option_arrays(src)):
        keep = m.group("comment") if m.group("name") in KEEP_COMMENT else ""
        src = src[:m.start()] + (keep or "") + src[m.end():]
    src = re.sub(r"^#define ENUM_OPTS\(a\) .*$", lambda _: ENUM_OPTS, src,
                 count=1, flags=re.M)
    src = re.sub(r"ENUM_OPTS\(k_(\w+)\)", r"ENUM_OPTS(\1)", src)
    src = replace_body(src, "k_cat_names", CAT_NAMES,
                       "/** @brief The categories' names in English, from "
                       "ui_text.def. */")
    src = re.sub(r"\n{3,}", "\n\n", strip_rows(src))
    return add_include(src, '#include "settings_sensors.h"\n',
                       '#include "settings_opts.h"\n')


def convert_sensors(src: str) -> str:
    """Converts settings_sensors.c: no option arrays, labels or help.

    Args:
        src: settings_sensors.c in the per-file form.

    Returns:
        settings_sensors.c reading its words from the one table.

    Raises:
        TableError: The source is not in the expected shape.
    """
    names = []
    for m in reversed(option_arrays(src)):
        names.append(m.group("name"))
        keep = m.group("comment") if m.group("name") in KEEP_COMMENT else ""
        src = src[:m.start()] + (keep or "") + src[m.end():]
    for name in names:
        src = re.sub(rf"\b{name}\b", "settings_opts_" + name[2:], src)
    src = re.sub(r"^#define WATCH_OPTS (.*), (\.option_count.*)$",
                 lambda m: "/** @brief A WATCHES row's options: list watch of "
                 "ui_text.def. */\n"
                 + "#define WATCH_OPTS".ljust(BACKSLASH_COL) + "\\\n"
                 + f"    {m.group(1)}, {m.group(2)}", src, flags=re.M)
    # A comment left in place belongs to the rows after the arrays.
    src = re.sub(r"(\*/\n)\n(static const setting_def_t)", r"\1\2", src)
    src = re.sub(r"\n{3,}", "\n\n", src)
    src = realign_backslashes(strip_rows(src))
    return add_include(src, '#include "settings_sensors.h"\n',
                       '#include "settings_opts.h"\n')


SERVO_WAS = """\
 * Every string the run writes or the SERVO screen shows about it is in
 * k_str, and nowhere else, so a translation is another table of the same
 * entries (the panel's German is in shared/ui/ui_text_de.c).  The report is
 * written in the language of the table the run was started with, and the
 * CSV always in English, because tools read it by its words.  A report is
 * UTF-8, as the table's strings are."""

SERVO_IS = """\
 * Every string the run writes or the SERVO screen shows about it is an
 * entry of ui_text.def (shared/text), with its English and its German;
 * k_str is the English column, and ui_text.c makes the other languages'
 * tables of the same entries.  The report is written in the language of
 * the table the run was started with, and the CSV always in English,
 * because tools read it by its words.  A report is UTF-8, as the table's
 * strings are."""


def convert_servo(src: str) -> str:
    """Converts servo_str.c: k_str reads the table's English column.

    Args:
        src: servo_str.c in the per-file form.

    Returns:
        servo_str.c reading its words from the one table.

    Raises:
        TableError: The source has no k_str.
    """
    return replace_body(src.replace(SERVO_WAS, SERVO_IS), "k_str",
                        SERVO_TABLE, "/** @brief The servo test's words in "
                        "English, from ui_text.def. */")


def convert_web(src: str) -> str:
    """Converts wa_page.c: its enum, tables and pt() read the one table.

    Args:
        src: wa_page.c in the per-file form.

    Returns:
        wa_page.c reading its words from the one table.

    Raises:
        ConvertError: The tables are not where expected.
    """
    m = re.search(r"/\* The page's texts, English and German\. \*/\n"
                  r".*?k_de\[P_COUNT\] = \{.*?\n\};\n"
                  r"(?:\nstatic const char \*pt\(.*?\n\}\n)?", src, re.S)
    if m is None:
        raise ConvertError("wa_page.c: the tables are not where expected")
    return src[:m.start()] + WEB_TABLES + src[m.end():]


def convert(read) -> dict[str, str]:
    """Converts a tree from the per-file form to the one table.

    Args:
        read: Gives a file's text by its path from the repository root.

    Returns:
        The text of every file in WRITTEN, by path.

    Raises:
        TableError: A file is not in the shape the tool reads.
    """
    t = Texts(read)
    return {
        NEW_DEF: write_table(t),
        SETTINGS_C: convert_settings(read(SETTINGS_C)),
        SENSORS_C: convert_sensors(read(SENSORS_C)),
        SERVO_C: convert_servo(read(SERVO_C)),
        WA_C: convert_web(read(WA_C)),
    }


# ---------------------------------------------------------- the commands


def read_tree(path: str) -> str:
    """Reads a file of the working tree.

    Args:
        path: Its path from the repository root.

    Returns:
        Its text.
    """
    return (REPO / path).read_text(encoding="utf-8")


def read_rev(rev: str):
    """Makes a reader of the files of a revision.

    Args:
        rev: A git revision.

    Returns:
        A function that gives a file's text at rev by its path.
    """
    def read(path: str) -> str:
        run = subprocess.run(["git", "show", f"{rev}:{path}"], cwd=REPO,
                             capture_output=True, text=True,
                             encoding="utf-8")
        if run.returncode != 0:
            raise ConvertError(f"{rev}:{path}: {run.stderr.strip()}")
        return run.stdout
    return read


def git(*args: str) -> None:
    """Runs git in the repository and fails on a non-zero exit.

    Args:
        *args: git's arguments.
    """
    subprocess.run(["git", *args], cwd=REPO, check=True)


def cmd_convert(done: list[str]) -> int:
    """Converts the working tree in place.

    Args:
        done: Gets the path of each file written or deleted, in order.

    Returns:
        The exit status, 0.

    Raises:
        TableError: A file is not in the shape the tool reads.
    """
    files = convert(read_tree)
    for path, text in files.items():
        (REPO / path).parent.mkdir(parents=True, exist_ok=True)
        (REPO / path).write_text(text, encoding="utf-8")
        done.append(path)
    for path in DELETED:
        if (REPO / path).exists():
            (REPO / path).unlink()
            done.append(path)
    print(f"wrote {NEW_DEF} ({files[NEW_DEF].count(chr(10))} lines) and "
          f"{len(files) - 1} C files; deleted {len(DELETED)} tables")
    return 0


def cmd_merge(base: str, theirs: str, done: list[str]) -> int:
    """Carries a branch's text changes into the one table during a merge.

    Converts BASE and THEIRS, merges THEIRS' converted changes since BASE
    into HEAD's files with git merge-file, writes them into the working tree
    and removes the old tables.

    Args:
        base: The merge base.
        theirs: The branch being merged, in the per-file form.
        done: Gets the path of each file written or removed, in order.

    Returns:
        0 when every file merged, 1 when one is left with conflict markers.

    Raises:
        TableError: A side is not in the shape the tool reads.
        subprocess.CalledProcessError: git add or git rm fails.
    """
    ours = read_rev("HEAD")
    before = convert(read_rev(base))
    after = convert(read_rev(theirs))
    conflicts = 0
    with tempfile.TemporaryDirectory() as td:
        for path in WRITTEN:
            files = []
            for tag, text in (("ours", ours(path)), ("base", before[path]),
                              ("theirs", after[path])):
                f = pathlib.Path(td) / tag
                f.write_text(text, encoding="utf-8")
                files.append(str(f))
            run = subprocess.run(["git", "merge-file", "-p", "-L", "HEAD",
                                  "-L", f"{base} converted", "-L",
                                  f"{theirs} converted", *files],
                                 capture_output=True, text=True,
                                 encoding="utf-8")
            if run.returncode < 0 or run.returncode > 127:
                raise ConvertError(f"git merge-file failed on {path}")
            (REPO / path).write_text(run.stdout, encoding="utf-8")
            done.append(path)
            conflicts += run.returncode
            state = f"{run.returncode} conflicts" if run.returncode \
                else "merged"
            print(f"{path}: {state}")
            if not run.returncode:
                git("add", path)
    for path in DELETED:
        git("rm", "-q", "-f", "--ignore-unmatch", path)
        if (REPO / path).exists():
            (REPO / path).unlink()
        done.append(path)
    return 1 if conflicts else 0


def main(argv: list[str] | None = None) -> int:
    """Runs the tool.

    Args:
        argv: The arguments, sys.argv[1:] when None.

    Returns:
        The exit status: 0, 1 for conflicts left by merge, 2 for a tree
        the tool cannot read or a git command that fails.  On 2 the files
        already written or removed are listed on stderr.
    """
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("convert", help="convert the working tree in place")
    m = sub.add_parser("merge", help="carry THEIRS' text changes since "
                       "BASE into the one table")
    m.add_argument("base")
    m.add_argument("theirs")
    args = ap.parse_args(argv)
    done: list[str] = []
    try:
        if args.cmd == "convert":
            return cmd_convert(done)
        return cmd_merge(args.base, args.theirs, done)
    except (TableError, subprocess.CalledProcessError) as e:
        print(f"merge_texts.py: {e}", file=sys.stderr)
        if done:
            print("merge_texts.py: already written or removed: "
                  + ", ".join(done), file=sys.stderr)
        else:
            print("merge_texts.py: no file written", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
