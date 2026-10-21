#!/usr/bin/env python3
"""Reads the interface's words in the per-file form.

The per-file form held them in seven places: the screens' English in
shared/ui/include/ui_text.def, all German in shared/ui/ui_text_de.c, the
settings' English in settings.c and settings_sensors.c, the servo test's in
servo_str.c and the Wi-Fi page's English and German in wa_page.c.
tools/merge_texts.py writes them into the one table.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import re

from c_tables import (TableError, array_items, code_mask, dedent_comment,
                      defines, elements, entries, join, literal_run,
                      preprocess, table_body)

OLD_DEF = "shared/ui/include/ui_text.def"
OLD_DE = "shared/ui/ui_text_de.c"
SETTINGS_C = "shared/settings/settings.c"
SENSORS_C = "shared/settings/settings_sensors.c"
SETTINGS_H = "shared/settings/include/settings.h"
SERVO_C = "shared/servo/servo_str.c"
WA_C = "shared/webaccess/wa_page.c"


# An option array whose comment is about the rows after it, not the array:
# the comment stays where it is.
KEEP_COMMENT = {"k_old_addr"}


class ConvertError(TableError):
    """The per-file form is not in a shape this tool reads."""


SEPARATOR = re.compile(r"/\*\s*-{3,}|-{3,}\s*\*/")

# Comments of the per-file form about an entry that no longer exists: the
# German table's note on TX_SV_LAST, removed with that ID.
STALE = ("The verdict and not the file",)


def carried(comments: list[str], german: bool) -> list[str]:
    """Picks the comments of an entry that go into the one table.

    Section rules and the comments in STALE stay behind; of the German
    table's, only those that explain (a sentence) go, not the short markers
    of where a screen's strings begin.

    Args:
        comments: The comments before the entry, as written.
        german: Whether they are the German table's.

    Returns:
        The comments to write before the entry, moved to column 0.
    """
    out = []
    for c in comments:
        if SEPARATOR.search(c) or any(x in c for x in STALE):
            continue
        if german and not re.search(r"\.(\s|\*/|$)", c):
            continue
        out.append(dedent_comment(c))
    return out


# ------------------------------------------------ macro-written entries


def expand(macros: str, text: str) -> str:
    """Expands the macros that write rows of the INA3221 settings.

    Each SET_INA_<KIND>(x) first becomes the marker @KIND(x)@, so a row
    keeps which setting it is (unmark()).

    Args:
        macros: The #define lines to expand by.
        text: The text to expand.

    Returns:
        text expanded, the settings as markers.

    Raises:
        TableError: No C compiler is on PATH, or the preprocessor fails.
    """
    kinds = sorted(set(re.findall(r"\bSET_INA_([A-Z]+)\(", macros + text)))
    head = "".join(f"#define SET_INA_{k}(x) @{k}(x)@\n" for k in kinds)
    return preprocess(head + macros, text)


def marker_id(kind: str, arg: str) -> str:
    """Names the setting a marker stands for.

    Args:
        kind: FIT, or the kind of a channel's row (W, SH, ...).
        arg: The part's number for FIT, else the channel's, 3a + n - 1 for
            part a's CHn, as a C expression of digits.

    Returns:
        SET_INA_FIT_<a>, or SET_INA_<KIND>_<a><n>.

    Raises:
        ConvertError: arg is more than digits and arithmetic.
    """
    if not re.fullmatch(r"[\d\s()+\-*]+", arg):
        raise ConvertError(f"a channel expression {arg!r}")
    value = eval(arg, {"__builtins__": {}})  # noqa: S307 - digits only
    if kind == "FIT":
        return f"SET_INA_FIT_{value}"
    return f"SET_INA_{kind}_{value // 3}{value % 3 + 1}"


MARKED = re.compile(r"\[\s*@(\w+)\(([^@]*)\)@\s*\]")


def unmark(text: str) -> str:
    """Writes every marker as the setting it stands for.

    Args:
        text: Text from expand().

    Returns:
        text with each [@KIND(x)@] as [SET_INA_...].

    Raises:
        ConvertError: A marker's argument is more than arithmetic.
    """
    return MARKED.sub(lambda m: f"[{marker_id(m.group(1), m.group(2))}]",
                      text)


# ------------------------------------------------------- the schema rows


def schema_rows(src: str) -> list[tuple[int, list[tuple[int, int]]]]:
    """Finds every setting_def_t initialiser of a source.

    A row is a brace list whose fifth element is a SET_CAT_ category.

    Args:
        src: The C source, macros included.

    Returns:
        (open brace, elements) of each row, in order.
    """
    mask = code_mask(src)
    out = []
    for i, c in enumerate(src):
        if c != "{" or not mask[i]:
            continue
        try:
            elems = elements(src, mask, i)
        except ConvertError:
            continue
        if len(elems) > 4 and \
                src[elems[4][0]:elems[4][1]].strip(" \t\n\\").startswith(
                    "SET_CAT_"):
            out.append((i, elems))
    return out


def row_id(src: str, open_at: int) -> str | None:
    """Names the setting a schema row is for.

    Args:
        src: The C source.
        open_at: The index of the row's opening brace.

    Returns:
        The SET_ ID of `[SET_X] = {` before the brace or of a `/* SET_X */`
        comment after it; None for a row a macro writes.
    """
    before = re.search(r"\[\s*(SET_\w+)\s*\]\s*=\s*$", src[:open_at])
    if before:
        return before.group(1)
    after = re.compile(r"\{\s*/\*\s*(SET_\w+)\s*\*/").match(src, open_at)
    return after.group(1) if after else None


def row_texts(src: str, elems) -> tuple[list[str], list[str], str]:
    """Reads a schema row's label, help line and options.

    Args:
        src: The C source.
        elems: The row's elements (schema_rows()).

    Returns:
        The label's literals, the help line's literals and the text of the
        options element, "" when the row has none.

    Raises:
        ConvertError: The label or the help line is not a literal.
    """
    def lits(k):
        parts, _ = literal_run(src, elems[k][0])
        if not parts:
            raise ConvertError("a schema row without a literal label or help")
        return parts
    options = src[elems[10][0]:elems[10][1]] if len(elems) > 10 else ""
    return lits(1), lits(2), options


OPTION_ARRAY = re.compile(
    r"(?P<comment>/\*(?:(?!\*/).)*\*/\n)?"
    r"static const char \*const (?P<name>k_\w+)\[\w*\]\s*=\s*\{"
    r"(?P<body>[^\[\]{}]*?)\};\n", re.S)


def option_arrays(src: str) -> list[re.Match]:
    """Finds the option arrays of a source.

    Args:
        src: The C source.

    Returns:
        A match per `static const char *const k_<name>[] = { ... };` of
        strings, with the comment right before it as "comment".
    """
    return list(OPTION_ARRAY.finditer(src))


# ------------------------------------------------------------ reading


class Texts:
    """Everything the per-file form holds, read from one tree.

    Args:
        read: Gives a file's text by its path from the repository root.

    Raises:
        ConvertError: A file is not in the shape the tool reads, or the
            English and the German disagree on which texts there are.
    """

    def __init__(self, read):
        self.read = read
        self.tx_items: list[tuple] = []     # the old def, in order
        self.tx_de: dict[str, tuple] = {}
        self.settings: list[str] = []       # setting_id_t's order
        self.groups: dict[str, str] = {}    # first setting -> its group
        self.label: dict[str, list] = {}
        self.help: dict[str, list] = {}
        self.label_de: dict[str, list] = {}
        self.help_de: dict[str, list] = {}
        self.cats: list[str] = []
        self.cat: dict[str, list] = {}
        self.cat_de: dict[str, list] = {}
        self.lists: list[tuple] = []        # (name, comment, items)
        self.list_de: dict[str, list] = {}
        self.servo: list[tuple] = []
        self.servo_de: dict[str, tuple] = {}
        self.web: list[tuple] = []
        self.web_de: dict[str, tuple] = {}
        self.old_def()
        self.german()
        self.schema()
        self.servo_words()
        self.web_page()

    # The screens' strings and their German.
    def old_def(self):
        """Reads the screens' strings and the comments between them.

        Raises:
            ConvertError: A row spans lines or holds no string literal.
        """
        text = self.read(OLD_DEF)
        end = text.index("*/") + 2
        lines = text[end:].lstrip("\n").split("\n")
        row = re.compile(r"^UI_TEXT\((\w+),\s*(\d+),\s*(.*)\)\s*$")
        for line in lines:
            m = row.match(line)
            if m is None:
                if line.startswith("UI_TEXT("):
                    raise ConvertError(f"a row over several lines: {line}")
                self.tx_items.append(("line", line))
                continue
            parts, j = literal_run(m.group(3), 0)
            if not parts or m.group(3)[j:].strip():
                raise ConvertError(f"{m.group(1)}: not a string literal")
            self.tx_items.append(("row", m.group(1), m.group(2), parts))
        while self.tx_items and self.tx_items[-1] == ("line", ""):
            self.tx_items.pop()

    def german(self):
        """Reads every German table, the macro-written rows expanded.

        Raises:
            ConvertError: An ID has German twice.
        """
        de = self.read(OLD_DE)
        a, b = table_body(de, "k_text")
        cond: list[str] = []
        for item in entries(de[a:b]):
            if item[0] == "pp":
                if item[1].startswith("#if"):
                    cond.append(item[1])
                elif item[1].startswith("#endif"):
                    cond.pop()
            elif item[0] == "entry":
                ident = item[1].removeprefix("TX_")
                if ident in self.tx_de:
                    raise ConvertError(f"TX_{ident} twice in the German")
                self.tx_de[ident] = (item[2], carried(item[3], True),
                                     tuple(cond))
        macros = defines(de, set())
        for name, store in (("k_label", self.label_de),
                            ("k_help", self.help_de)):
            a, b = table_body(de, name)
            for item in entries(unmark(expand(macros, de[a:b]))):
                if item[0] == "entry":
                    store[item[1]] = join(item[2])
        a, b = table_body(de, "k_category")
        for item in entries(de[a:b]):
            if item[0] == "entry":
                self.cat_de[item[1]] = item[2]
        arrays = {m.group("name"): array_items(m.group("body"))
                  for m in option_arrays(de)}
        a, b = table_body(de, "k_options")
        self.bind_de = {}
        for item in entries(unmark(expand(macros, de[a:b]))):
            if item[0] == "ref":
                self.bind_de[item[1]] = arrays[item[2]]
        a, b = table_body(de, "k_servo")
        for item in entries(de[a:b]):
            if item[0] == "entry":
                self.servo_de[item[1]] = (item[2], carried(item[3], False))

    # The settings' English, from the schema.
    def schema(self):
        """Reads the settings' English, their order and their option lists.

        Raises:
            ConvertError: A setting lacks its English, or the German options
                do not pair with the English ones.
        """
        header = self.read(SETTINGS_H)
        first = header.index("SET_PACK_CELLS = 0")
        enum = header[header.rindex("typedef enum {", 0, first):
                      header.index("SETTING_COUNT")]
        group = None
        for m in re.finditer(r"(/\*.*?\*/)|\b(SET_\w+)\b(\s*=\s*"
                             r"[A-Za-z_]\w*)?", enum, re.S):
            if m.group(1):
                if m.group(1).startswith("/* ---"):
                    group = m.group(1)
            elif not m.group(3):
                if group is not None:
                    self.groups[m.group(2)] = group
                    group = None
                self.settings.append(m.group(2))
        cats = header[header.index("SET_CAT_APP = 0"):
                      header.index("SET_CAT_COUNT")]
        self.cats = re.findall(r"^\s*(SET_CAT_\w+)", cats, re.M)

        bind_en: dict[str, str] = {}
        for path in (SETTINGS_C, SENSORS_C):
            src = self.read(path)
            for m in option_arrays(src):
                comment = m.group("comment")
                if m.group("name") in KEEP_COMMENT:
                    comment = None
                self.lists.append((m.group("name"), comment,
                                   array_items(m.group("body"))))
            for open_at, elems in schema_rows(src):
                ident = row_id(src, open_at)
                if ident is None:
                    continue        # a macro's row: expanded below
                label, help_, options = row_texts(src, elems)
                self.label[ident], self.help[ident] = label, help_
                named = re.search(r"\b(k_\w+)\b", options)
                if named:
                    bind_en[ident] = named.group(1)
        src = self.read(SENSORS_C)
        a, b = table_body(src, "k_rows")
        rows = unmark(expand(defines(src, {"AT", "N_ROWS"})
                             + "\n#define AT(x) x\n", src[a:b]))
        for open_at, elems in schema_rows(rows):
            ident = row_id(rows, open_at)
            label, help_, options = row_texts(rows, elems)
            self.label[ident] = join(label)
            self.help[ident] = join(help_)
            named = re.search(r"\b(k_\w+)\b", options)
            if named:
                bind_en[ident] = named.group(1)
        for name in ("label", "help"):
            for k in self.settings:
                if k not in getattr(self, name):
                    raise ConvertError(f"{k} has no English {name}")

        settings = self.read(SETTINGS_C)
        a, b = table_body(settings, "k_cat_names")
        for item in entries(settings[a:b]):
            if item[0] == "entry":
                self.cat[item[1]] = item[2]

        # A German list belongs to the English list of the settings that
        # show it.
        for ident, de_items in self.bind_de.items():
            name = bind_en.get(ident)
            if name is None:
                raise ConvertError(f"German options for {ident}, which "
                                   "has no English list")
            have = self.list_de.setdefault(name, de_items)
            if have != de_items:
                raise ConvertError(f"{name}: two German lists")
        for name, _, items in self.lists:
            de_items = self.list_de.get(name)
            if de_items is not None and len(de_items) != len(items):
                raise ConvertError(f"{name}: {len(items)} options in "
                                   f"English, {len(de_items)} in German")

    def servo_words(self):
        """Reads the servo test's English.

        Raises:
            ConvertError: The table holds more than entries and comments, or an
                ID has German and no English.
        """
        src = self.read(SERVO_C)
        a, b = table_body(src, "k_str")
        for item in entries(src[a:b]):
            if item[0] == "entry":
                self.servo.append((item[1], item[2], carried(item[3],
                                                             False)))
            elif item[0] != "comment":
                raise ConvertError(f"servo_str.c: {item}")
        known = {s[0] for s in self.servo}
        for ident in self.servo_de:
            if ident not in known:
                raise ConvertError(f"{ident} has German and no English")

    def web_page(self):
        """Reads the Wi-Fi page's English and German, in p_text_t's order.

        Raises:
            ConvertError: The page has no p_text_t, or an ID has no English.
        """
        src = self.read(WA_C)
        m = re.search(r"typedef enum \{(.*?)\} p_text_t;", src, re.S)
        if m is None:
            raise ConvertError("wa_page.c: no p_text_t")
        names = [n for n in re.findall(r"^\s*(P_\w+)", m.group(1), re.M)
                 if n != "P_COUNT"]
        tables = {}
        for name in ("k_en", "k_de"):
            a, b = table_body(src, name)
            tables[name] = {i[1]: (i[2], carried(i[3], False))
                            for i in entries(src[a:b]) if i[0] == "entry"}
        for n in names:
            en = tables["k_en"].get(n)
            if en is None:
                raise ConvertError(f"{n} has no English")
            self.web.append((n, en[0], en[1]))
            self.web_de[n] = tables["k_de"].get(n, (None, []))
