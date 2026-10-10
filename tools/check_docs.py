#!/usr/bin/env python3
"""Check the docs against the code for the claims a machine can verify.

Not a linter and not a spell checker.  This looks only at assertions whose
truth is decidable from the tree: does that screenshot exist, does that link
go anywhere, is that list of test binaries the list CMake builds, is that
count the count.

Covered: docs/ (the wiki source), README.md and README-de.md, STATUS.md, and
the pages under hardware/.  The wiki pages are additionally held to the
sidebar and to having a German counterpart.  The `Who compiles what` table in
STATUS.md and docs/Building.md is derived from the three build files, and the
screenshot count in STATUS.md from docs/img.  The stick pages' table of how
a run ends is held to esc_stick_reason_is_fault().  A German page that
quotes an interface string in backticks quotes the German the screen shows,
not its English.

Numbers a page states are held to the constant that sets them: the link
protocol's version, the heartbeat and link timings, the heartbeat and CAN
pins, the coverage floors and the stack margin, each as one sentence in
FACTS below; every table row that names a C constant; the ceiling table in
Performance.md against the --max-lines arguments in ci.yml; and the pin
counts in hardware/docs/Pins.md against pinmap.json.  A sentence in FACTS
that is reworded so its pattern matches nowhere fails, so a check does not
lapse with an edit.

    python3 tools/check_docs.py

Prints one line per problem and exits 1 if there were any.  Frame-cost numbers
are checked separately by `tools/frame_cost.py --check-doc`, and the coverage
table by `tools/coverage.py --check`, because both of those have to run a
measurement first.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
DOCS = REPO / "docs"
IMG = DOCS / "img"
HARDWARE = REPO / "hardware"

# Markdown outside docs/ that the link and anchor checks also cover: the
# README in both languages, the running record, and the hardware pages.
ROOT_PAGES = [REPO / "README.md", REPO / "README-de.md", REPO / "STATUS.md"]

# Pages that are navigation rather than content, and so are not expected to be
# linked from the sidebar like the rest.
SIDEBAR_EXEMPT = {"_Sidebar.md", "Home.md", "Home-de.md"}

# The wiki is bilingual: every English page has a German one beside it, named
# with a -de suffix because a wiki page is addressed by its title and there is
# no folder to put a language in.  Checked rather than trusted: the sidebar
# offers a translation whether or not the file exists.
DE_SUFFIX = "-de.md"


def english_pages() -> list[pathlib.Path]:
    return [p for p in pages() if not p.name.endswith(DE_SUFFIX)]

WORDS = {
    "no": 0, "one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6,
    "seven": 7, "eight": 8, "nine": 9, "ten": 10, "eleven": 11, "twelve": 12,
    "thirteen": 13, "fourteen": 14, "fifteen": 15, "sixteen": 16,
    "seventeen": 17, "eighteen": 18, "nineteen": 19, "twenty": 20,
}

LINK_RE = re.compile(r"\]\(([^)\s#]+)(?:#[^)\s]*)?\)")


def as_number(text: str) -> int | None:
    text = text.strip().lower().replace(",", "")
    if text.isdigit():
        return int(text)
    return WORDS.get(text)


def read(path: pathlib.Path) -> str:
    with open(path, encoding="utf-8", newline="") as fh:
        return fh.read()


def pages() -> list[pathlib.Path]:
    return sorted(DOCS.glob("*.md"))


def hardware_pages() -> list[pathlib.Path]:
    return sorted(HARDWARE.rglob("*.md"))


def linked_pages() -> list[pathlib.Path]:
    """Every markdown file whose links and anchors are checked.

    A root page that does not exist is reported by check_translations rather
    than crashing the link walk.
    """
    return pages() + [p for p in ROOT_PAGES if p.exists()] + hardware_pages()


HEADING_RE = re.compile(r"^#{1,6}\s+(.*?)\s*$", re.M)


def anchors(text: str) -> set[str]:
    """The fragment ids GitHub derives from a page's headings.

    Lowercase, punctuation dropped, spaces to hyphens.  Close enough to
    GitHub's rule for the links this project actually writes.
    """
    out = set()
    for heading in HEADING_RE.findall(text):
        slug = re.sub(r"[^\w\s-]", "", heading.lower()).strip()
        out.add(re.sub(r"\s+", "-", slug))
    return out


ANCHOR_RE = re.compile(r"\]\(([^)\s#]*)#([^)\s]+)\)")


def check_anchors(problems: list[str]) -> None:
    """A link to a heading points at a heading that exists.

    A link check on the file alone passes a dead fragment: the reader lands
    at the top of the page.
    """
    known = {}
    for page in linked_pages():
        known[page.resolve()] = anchors(read(page))

    for page in linked_pages():
        for target, fragment in ANCHOR_RE.findall(read(page)):
            if "://" in target:
                continue
            resolved = ((page.parent / target).resolve() if target
                        else page.resolve())
            if resolved not in known:
                continue          # a non-markdown target; check_links has it
            if fragment not in known[resolved]:
                where = ("itself" if resolved == page.resolve()
                         else resolved.name)
                problems.append(
                    f"{page.name}: #{fragment} points into {where}, "
                    "which has no such heading")


def check_links(problems: list[str]) -> None:
    """Every relative link and image reference resolves to a real file."""
    referenced: set[pathlib.Path] = set()
    for page in linked_pages():
        base = page.parent
        for target in LINK_RE.findall(read(page)):
            if "://" in target or target.startswith("mailto:"):
                continue
            resolved = (base / target).resolve()
            if not resolved.exists():
                problems.append(f"{page.name}: link to {target} goes nowhere")
            elif resolved.suffix.lower() == ".png":
                referenced.add(resolved)

    # docs/img/de holds the same screens in German, for the German pages.
    for image in sorted(IMG.rglob("*.png")):
        if image.resolve() not in referenced:
            problems.append(
                f"docs/img/{image.relative_to(IMG)} is committed but no page "
                "shows it")


def check_translations(problems: list[str]) -> None:
    """Every page exists in both languages, and each says so at the top."""
    for page in english_pages():
        if page.name == "_Sidebar.md":
            continue
        german = DOCS / (page.stem + DE_SUFFIX)
        if not german.exists():
            problems.append(f"{page.name} has no {german.name}")
            continue
        # The switch is the only way across, so a page without one is a page
        # the other language cannot be reached from.
        if f"]({german.name})" not in read(page):
            problems.append(f"{page.name} does not link {german.name}")
        if f"]({page.name})" not in read(german):
            problems.append(f"{german.name} does not link {page.name}")

    for page in pages():
        if not page.name.endswith(DE_SUFFIX):
            continue
        english = DOCS / (page.name[:-len(DE_SUFFIX)] + ".md")
        if not english.exists():
            problems.append(f"{page.name} translates {english.name}, "
                            "which does not exist")

    # The README pair follows the same rule as a wiki page.
    english, german = REPO / "README.md", REPO / "README-de.md"
    if not german.exists():
        problems.append(f"{english.name} has no {german.name}")
    else:
        if f"]({german.name})" not in read(english):
            problems.append(f"{english.name} does not link {german.name}")
        if f"]({english.name})" not in read(german):
            problems.append(f"{german.name} does not link {english.name}")


def check_sidebar(problems: list[str]) -> None:
    """The sidebar is the wiki's only navigation, so it has to be complete."""
    sidebar = read(DOCS / "_Sidebar.md")
    linked = {t for t in LINK_RE.findall(sidebar) if t.endswith(".md")}
    for page in pages():
        if page.name in SIDEBAR_EXEMPT:
            continue
        if page.name not in linked:
            problems.append(f"_Sidebar.md does not link {page.name}")


def suites() -> list[str]:
    """The suite names test/host/CMakeLists.txt actually builds."""
    text = read(REPO / "test" / "host" / "CMakeLists.txt")
    m = re.search(r"foreach\s*\(\s*suite([^)]*)\)", text)
    if not m:
        sys.exit("could not find the `foreach(suite ...)` list in "
                 "test/host/CMakeLists.txt")
    return m.group(1).split()


def check_suites(problems: list[str]) -> None:
    """STATUS.md names every binary, and counts them correctly.

    The list lives in STATUS.md, the running record.
    """
    doc = REPO / "STATUS.md"
    text = read(doc)
    built = set(suites())
    named = set(re.findall(r"`test_(\w+)`", text))
    for missing in sorted(built - named):
        problems.append(f"{doc.name}: does not mention test_{missing}, "
                        "which CMake builds")
    for ghost in sorted(named - built):
        problems.append(f"{doc.name}: names test_{ghost}, which CMake does "
                        "not build")

    m = re.search(r"(\w+) binaries", text)
    if not m:
        problems.append(f"{doc.name}: no '<N> binaries' sentence to check")
    else:
        said = as_number(m.group(1))
        if said != len(built):
            problems.append(f"{doc.name}: says {m.group(1)} binaries; CMake "
                            f"builds {len(built)}")


# Not implemented: a check that every settings row shown on screen reaches
# code.

OPTIONS_RE = re.compile(
    r"static const char \*const (k_\w+)\s*\[\]\s*=\s*\{(.*?)\}\s*;", re.S)

# Files whose option arrays the docs quote: only the arrays that name
# user-visible choices, because those are what prose enumerates.
OPTION_SOURCES = (
    REPO / "shared" / "settings" / "settings.c",
)


# Option arrays that are a range of numbers offered one by one, not choices
# with names: the current monitors' I2C addresses, 0x40 to 0x4F and 0x40 to
# 0x43.  Prose names an address it means, and that is not an enumeration of
# the menu.
NOT_MENUS = {"settings.c:k_ina228_addr", "settings.c:k_ina3221_addr"}


def option_lists() -> dict[str, list[str]]:
    lists: dict[str, list[str]] = {}
    for source in OPTION_SOURCES:
        for name, body in OPTIONS_RE.findall(read(source)):
            members = re.findall(r'"([^"]+)"', body)
            key = f"{source.name}:{name}"
            if len(members) > 1 and key not in NOT_MENUS:
                lists[key] = members
    return lists


def check_option_lists(problems: list[str]) -> None:
    """A doc that enumerates a menu enumerates all of it.

    A paragraph that names two or more members of an option list and not the
    rest is treated as an incomplete enumeration.
    """
    lists = option_lists()
    for page in linked_pages():
        # Paragraphs, not lines: prose wraps, and a list that happens to break
        # across two lines counts as one enumeration.
        line_no = 1
        for para in re.split(r"\n\s*\n", read(page)):
            at = line_no
            line_no += para.count("\n") + 2
            for name, members in lists.items():
                present = [m for m in members if m in para]
                if len(present) < 2 or len(present) == len(members):
                    continue
                missing = [m for m in members if m not in para]
                problems.append(
                    f"{page.name}:{at}: names {len(present)} of the "
                    f"{len(members)} {name} options; missing "
                    f"{', '.join(missing)}")


UI_DIR = REPO / "shared" / "ui"
UI_TEXT_DEF = UI_DIR / "include" / "ui_text.def"
UI_TEXT_DE = UI_DIR / "ui_text_de.c"
SETTINGS_C = REPO / "shared" / "settings" / "settings.c"
SETTINGS_H = REPO / "shared" / "settings" / "include" / "settings.h"
SERVO_REPORT_C = REPO / "shared" / "servo" / "servo_report.c"

# Quotes on a German page that stay English although the screen translates
# the same word: values of the servo test's CSV, which is English in every
# language, and values of an ESC profile's fields.
ENGLISH_QUOTES = {
    ("Servo-de.md", "STEP"),
    ("Servo-de.md", "SET"),
    ("Servo-de.md", "SETTLE"),
    ("Servo-de.md", "IDLE"),
    ("Servo-de.md", "MOVE"),
    ("Servo-de.md", "MODEL"),
    ("Servo-de.md", "OFF"),
    ("EscProfiles-de.md", "none"),
    ("StickProgramming-de.md", "none"),
}

# A C string literal, and a run of adjacent ones the compiler joins.
C_STR = r'"(?:[^"\\]|\\.)*"'
C_STRS = rf"((?:{C_STR}\s*)+)"

# One printf conversion, and what each kind prints: a number, a character,
# or any text.
CONVERSION = re.compile(
    r"%[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|h|ll|l|z)?([diouxXfeEgGcs])")
PRINTS = {"c": ".", "s": ".+?"}
NUMBER = r"[-+]?[0-9A-Fa-f][0-9A-Fa-f.,]*"

# A format is matched against a quote only when its words say something:
# "%u ms" would match any number of milliseconds a page mentions.
MIN_FORMAT_LETTERS = 4


def c_strings(source: str) -> str:
    """The text of adjacent C string literals, joined, escapes resolved
    for the quote and the backslash."""
    parts = re.findall(C_STR, source)
    return "".join(p[1:-1].replace('\\"', '"').replace("\\\\", "\\")
                   for p in parts)


def keyed(source: str, prefix: str) -> dict[str, str]:
    """`[PREFIX_ID] = "text"` entries of a designated initialiser."""
    return {k: c_strings(v) for k, v in re.findall(
        rf"\[({prefix}\w+)\]\s*=\s*{C_STRS}", source)}


def table(source: str, name: str) -> str:
    """The body of the C array @p name."""
    start = source.index(name)
    return source[start:source.index("\n};", start)]


def initialiser(source: str, name: str) -> str:
    """The braces of the array @p name, written on one line or several."""
    m = re.search(rf"\b{name}\[\w*\]\s*=\s*\{{([^}}]*)\}}", source)
    return m.group(1) if m else ""


def string_pairs() -> list[tuple[str, str]]:
    """(English, German) for every interface string, setting label, help
    line, option, category and servo test word the German table holds."""
    de = read(UI_TEXT_DE)
    pairs: list[tuple[str, str]] = []

    english = {k: c_strings(v) for k, v in re.findall(
        rf"UI_TEXT\((\w+),\s*\d+,\s*{C_STRS}\)", read(UI_TEXT_DEF))}
    german = keyed(table(de, "k_text["), "TX_")
    pairs += [(english[k[3:]], v) for k, v in german.items()
              if k[3:] in english]

    settings = read(SETTINGS_C)
    defs = {k: (label, help_) for k, label, help_ in re.findall(
        rf'\[(SET_\w+)\]\s*=\s*\{{\s*"[^"]*",\s*({C_STR}),\s*({C_STR})',
        settings)}
    for name, col in (("k_label[", 0), ("k_help[", 1)):
        for k, v in keyed(table(de, name), "SET_").items():
            if k in defs:
                pairs.append((c_strings(defs[k][col]), v))

    english_opts = {k: v for k, v in re.findall(
        r"\[(SET_\w+)\][^\[]*?ENUM_OPTS\((\w+)\)", settings)}
    german_opts = dict(re.findall(r"\[(SET_\w+)\]\s*=\s*(k_\w+),",
                                  table(de, "k_options[")))
    for k, de_name in german_opts.items():
        if k not in english_opts:
            continue
        en_list = re.findall(C_STR, initialiser(settings, english_opts[k]))
        de_list = re.findall(C_STR, initialiser(de, de_name))
        pairs += [(c_strings(e), c_strings(d))
                  for e, d in zip(en_list, de_list, strict=True)]

    header = read(SETTINGS_H)
    enum = header[header.index("SET_CAT_ESC = 0"):
                  header.index("SET_CAT_COUNT")]
    cats = re.findall(r"^\s*(SET_CAT_\w+)", enum, re.M)
    en_cats = [c_strings(s) for s in
               re.findall(C_STR, table(settings, "k_cat_names["))]
    de_cats = keyed(table(de, "k_category["), "SET_CAT_")
    pairs += [(e, de_cats[c])
              for c, e in zip(cats, en_cats, strict=True) if c in de_cats]

    en_servo = keyed(read(SERVO_REPORT_C), "SERVO_STR_")
    de_servo = keyed(table(de, "k_servo["), "SERVO_STR_")
    pairs += [(en_servo[k], v) for k, v in de_servo.items() if k in en_servo]

    # A quote's spaces are collapsed (quoted_spans), so both sides are too:
    # a column padded with two spaces is quoted with one.
    flat = [(" ".join(e.split()), " ".join(d.split())) for e, d in pairs]
    return [(e, d) for e, d in flat if len(e) >= 2 and e != d]


def format_pattern(fmt: str) -> re.Pattern[str] | None:
    """A format as a pattern a quote of its output matches, or None for a
    literal or a format with too few words of its own."""
    pieces = CONVERSION.split(fmt.replace("%%", "\0"))
    if len(pieces) == 1:
        return None
    literal = "".join(pieces[0::2])
    if sum(ch.isalpha() for ch in literal) < MIN_FORMAT_LETTERS:
        return None
    body = ""
    for n, piece in enumerate(pieces):
        if n % 2:
            body += PRINTS.get(piece, NUMBER)
        else:
            body += re.escape(piece).replace("\0", "%")
    return re.compile(body)


def quoted_spans(page: pathlib.Path):
    """(line, quote) for every backtick quote outside a code block, a quote
    wrapped over two lines joined with one space, and whether it follows
    "Konsole: "."""
    text = re.sub(r"```.*?```", lambda m: "\n" * m.group().count("\n"),
                  read(page), flags=re.S)
    for m in re.finditer(r"`([^`]+)`", text):
        if "\n\n" in m.group(1):
            continue
        line = text.count("\n", 0, m.start()) + 1
        quote = " ".join(m.group(1).split())
        console = text[:m.start()].endswith("Konsole: ")
        yield line, quote, console


def check_quoted_labels(problems: list[str]) -> None:
    """A German page quotes the German of a translated interface string.

    Covers the interface strings, the settings' labels, help, options and
    categories, and the servo test's words; a format matches any quote of
    its output.  Only backtick quotes are held to it: prose also names
    protocol pages, supply commands and board markings that share a word
    with a label.  A quote right after "Konsole: " is the console's, which
    stays English.
    """
    literal: dict[str, str] = {}
    formats: list[tuple[re.Pattern[str], re.Pattern[str] | None, str]] = []
    for english, german in string_pairs():
        pattern = format_pattern(english)
        if pattern is not None:
            formats.append((pattern, format_pattern(german), german))
        elif CONVERSION.search(english) is None:
            literal.setdefault(english, german)
    for page in pages() + [REPO / "README-de.md"]:
        if not page.name.endswith(DE_SUFFIX):
            continue
        for no, quote, console in quoted_spans(page):
            if console or (page.name, quote) in ENGLISH_QUOTES:
                continue
            shown = literal.get(quote)
            if shown is None:
                for pattern, german, text in formats:
                    if pattern.fullmatch(quote) and not (
                            german is not None and german.fullmatch(quote)):
                        shown = text
                        break
            if shown is not None:
                problems.append(
                    f"{page.name}:{no}: quotes `{quote}`, which the screen "
                    f"shows as `{shown}`")


def check_shared_modules(problems: list[str]) -> None:
    """Building.md's tree lists every module under shared/.

    shared/ is the one directory three build systems read, so an undocumented
    module is one a build can miss.
    """
    doc = DOCS / "Building.md"
    text = read(doc)
    for module in sorted(p.name for p in (REPO / "shared").iterdir()
                         if p.is_dir()):
        if not re.search(rf"^\s+{re.escape(module)}/", text, re.M):
            problems.append(f"{doc.name}: the tree omits shared/{module}/")


# --- the compile table -------------------------------------------------------

# The host suite spells the path "${SHARED}/gfx", the coprocessor
# ".../../shared/link": the last path element is the module either way.
MODULE_DIR_RE = re.compile(r'add_subdirectory\(\s*"[^"]*?/(\w+)"')
REQUIRES_RE = re.compile(r"REQUIRES\s+([^)\n]+)")
TABLE_ROW_RE = re.compile(
    r"^\|\s*((?:`\w+`\s*(?:·\s*)?)+)\|([^|]*)\|([^|]*)\|([^|]*)\|\s*$", re.M)


def shared_modules() -> list[str]:
    return sorted(p.name for p in (REPO / "shared").iterdir() if p.is_dir())


def modules_built_by(cmake: pathlib.Path) -> set[str]:
    return set(MODULE_DIR_RE.findall(read(cmake))) & set(shared_modules())


def panel_modules() -> set[str]:
    """The shared/ modules the panel compiles: main's REQUIRES, closed over
    each module's own REQUIRES."""
    shared = set(shared_modules())
    wanted = set()
    text = read(REPO / "firmware" / "panel" / "main" / "CMakeLists.txt")
    for req in REQUIRES_RE.findall(text):
        wanted |= set(req.split()) & shared
    frontier = list(wanted)
    while frontier:
        mod = frontier.pop()
        for req in REQUIRES_RE.findall(read(REPO / "shared" / mod
                                            / "CMakeLists.txt")):
            for dep in set(req.split()) & shared:
                if dep not in wanted:
                    wanted.add(dep)
                    frontier.append(dep)
    return wanted


def compile_table(text: str) -> dict[str, tuple[bool, bool, bool]]:
    """The `Module | panel | iomcu | host` rows a page carries."""
    out = {}
    for mods, panel, iomcu, host in TABLE_ROW_RE.findall(text):
        for mod in re.findall(r"`(\w+)`", mods):
            out[mod] = ("✔" in panel, "✔" in iomcu, "✔" in host)
    return out


def check_compile_table(problems: list[str]) -> None:
    """The `Who compiles what` table matches the three build files.

    Derived from firmware/panel/main/CMakeLists.txt (REQUIRES, closed over
    each module's REQUIRES), firmware/iomcu/CMakeLists.txt and
    test/host/CMakeLists.txt (add_subdirectory), not copied from the page.
    """
    truth = {}
    panel = panel_modules()
    iomcu = modules_built_by(REPO / "firmware" / "iomcu" / "CMakeLists.txt")
    host = modules_built_by(REPO / "test" / "host" / "CMakeLists.txt")
    for mod in shared_modules():
        truth[mod] = (mod in panel, mod in iomcu, mod in host)

    for doc in (REPO / "STATUS.md", DOCS / "Building.md"):
        table = compile_table(read(doc))
        if not table:
            problems.append(f"{doc.name}: no `Module | panel | iomcu | host` "
                            "table found")
            continue
        for mod, want in truth.items():
            have = table.get(mod)
            if have is None:
                problems.append(f"{doc.name}: the compile table omits "
                                f"`{mod}`")
            elif have != want:
                cols = ("panel", "iomcu", "host")
                said = ", ".join(c for c, v in zip(cols, have, strict=True)
                                 if v) or "none"
                real = ", ".join(c for c, v in zip(cols, want, strict=True)
                                 if v) or "none"
                problems.append(f"{doc.name}: says `{mod}` is compiled by "
                                f"{said}; the build files say {real}")
        for mod in table:
            if mod not in truth:
                problems.append(f"{doc.name}: the compile table names "
                                f"`{mod}`, which is not under shared/")


def check_screenshot_count(problems: list[str]) -> None:
    """STATUS.md's count of committed screenshots is the number in docs/img,
    the German ones in docs/img/de included."""
    text = read(REPO / "STATUS.md")
    m = re.search(r"(\w+) committed screenshots", text)
    if not m:
        problems.append("STATUS.md: no '<N> committed screenshots' sentence")
        return
    said = as_number(m.group(1))
    real = len(list(IMG.rglob("*.png")))
    if said != real:
        problems.append(f"STATUS.md: says {m.group(1)} committed screenshots; "
                        f"docs/img holds {real}")
    # And the split by language, wherever STATUS.md gives it.
    en = len(list(IMG.glob("*.png")))
    de = len(list((IMG / "de").glob("*.png")))
    for m in re.finditer(r"(\d+) in English and the same (\d+) in German",
                         text):
        if (int(m.group(1)), int(m.group(2))) != (en, de):
            problems.append(f"STATUS.md: says {m.group(0)}; docs/img holds "
                            f"{en} and docs/img/de {de}")
    for m in re.finditer(r"the (\d+) German screenshots", text):
        if int(m.group(1)) != de:
            problems.append(f"STATUS.md: says {m.group(0)}; docs/img/de "
                            f"holds {de}")

    # And the views the fit check draws, in the language page: render_ui's
    # SCREENS table, one view per entry.
    views = render_ui_views()
    pages = (("Language.md", r"draws all (\d+) views"),
             ("Language-de.md", r"zeichnet alle (\d+) Ansichten"))
    for name, pattern in pages:
        doc = read(DOCS / name)
        found = re.search(pattern, doc)
        if not found:
            problems.append(f"{name}: no count of the views the fit check "
                            "draws")
        elif int(found.group(1)) != views:
            problems.append(f"{name}: says {found.group(0)}; "
                            f"tools/render_ui.py draws {views}")


def render_ui_views() -> int:
    """How many views tools/render_ui.py renders: its SCREENS table."""
    import ast
    tree = ast.parse(read(REPO / "tools" / "render_ui.py"))
    for node in tree.body:
        if (isinstance(node, ast.Assign) and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name)
                and node.targets[0].id == "SCREENS"
                and isinstance(node.value, ast.Dict)):
            return len(node.value.keys)
    return -1


VERSION_H = REPO / "shared" / "link" / "include" / "rcbench_version.h"


def check_version(problems: list[str]) -> None:
    """The version on the wire is the version in the changelog.

    The coprocessor publishes these three numbers on its identity page, so a
    release cut without moving them ships a board that misreports what it is
    running -- and nobody notices until one is asked in the field.
    """
    text = read(VERSION_H)
    got = {}
    for part in ("MAJOR", "MINOR", "PATCH"):
        m = re.search(rf"^#define RCBENCH_VERSION_{part}\s+(\d+)$", text,
                      re.MULTILINE)
        if m is None:
            problems.append(f"rcbench_version.h has no RCBENCH_VERSION_{part}")
            return
        got[part] = int(m.group(1))

    m = re.search(r"^## (\d+)\.(\d+)\.(\d+) - ", read(REPO / "CHANGELOG.md"),
                  re.MULTILINE)
    if m is None:
        problems.append("CHANGELOG.md has no released version heading")
        return
    want = tuple(int(g) for g in m.groups())
    have = (got["MAJOR"], got["MINOR"], got["PATCH"])
    if have != want:
        problems.append(
            "rcbench_version.h says %d.%d.%d; the newest CHANGELOG entry is "
            "%d.%d.%d" % (*have, *want))

    # RCBENCH_VERSION_STRING composes its value from the three defines, so a
    # wrong number in the comment above it changes nothing and compiles.  It
    # is the one place in that file a reader looks for the version, and it
    # was hand-bumped at every release until one was missed.
    m = re.search(r'^/\*\* "(\d+\.\d+\.\d+)", for anything that prints it',
                  text, re.MULTILINE)
    if m is None:
        problems.append(
            "rcbench_version.h has no documented RCBENCH_VERSION_STRING")
    elif m.group(1) != "%d.%d.%d" % have:
        problems.append(
            "rcbench_version.h documents RCBENCH_VERSION_STRING as %s; the "
            "defines make it %d.%d.%d" % (m.group(1), *have))

    # The servo test's sample report prints RCBENCH_VERSION_STRING on its
    # Firmware line, so the sample shows what this release writes.
    # Servo.md holds the sample; the German page links to it.
    samples = 0
    for page in sorted(DOCS.glob("Servo*.md")):
        for n, line in enumerate(read(page).splitlines(), 1):
            m = re.match(r"\s*Firmware:\s+rcbench\s+(\S+)", line)
            if m is None:
                continue
            if page.name == "Servo.md":
                samples += 1
            if m.group(1) != "%d.%d.%d" % have:
                problems.append(
                    "%s:%d: the sample report says rcbench %s; this release "
                    "writes %d.%d.%d" % (page.name, n, m.group(1), *have))
    if samples == 0:
        problems.append(
            "Servo.md has no sample report Firmware line to hold to the "
            "version")


STICK_C = REPO / "shared" / "esc" / "esc_stick.c"

# The table in each stick programming page that gives every end of a run
# its red light: the page, the header row, and the words for lit and dark.
RED_TABLES = [
    (DOCS / "StickProgramming.md", "| Result | Cause | Red light |",
     "en", {"lit": True, "dark": False}),
    (DOCS / "StickProgramming-de.md", "| Ergebnis | Ursache | Rote Leuchte |",
     "de", {"an": True, "aus": False}),
]


def reason_faults() -> dict[str, bool]:
    """ESC_STICK_R_* suffix -> whether esc_stick_reason_is_fault() lights
    red for it, read from the switch's case labels and their returns."""
    src = read(STICK_C)
    start = src.index("bool esc_stick_reason_is_fault(")
    body = src[start:src.index("\n}\n", start)]
    faults: dict[str, bool] = {}
    pending: list[str] = []
    for line in body.splitlines():
        m = re.match(r"\s*case ESC_STICK_R_(\w+):", line)
        if m:
            pending.append(m.group(1))
        m = re.search(r"return (true|false);", line)
        if m and pending:
            for name in pending:
                faults[name] = m.group(1) == "true"
            pending = []
    return faults


def check_red_light_tables(problems: list[str]) -> None:
    """The stick pages' table of how a run ends says, for every reason, what
    esc_stick_reason_is_fault() does with it, under the words the screen
    shows in that language: one row per reason, DONE dark, nothing else."""
    faults = reason_faults()
    if not faults:
        problems.append("esc_stick.c: no esc_stick_reason_is_fault() cases")
        return
    english = {k: c_strings(v) for k, v in re.findall(
        rf"UI_TEXT\((\w+),\s*\d+,\s*{C_STRS}\)", read(UI_TEXT_DEF))}
    german = {k[3:]: v for k, v in keyed(table(read(UI_TEXT_DE), "k_text["),
                                         "TX_").items()}
    for page, header, lang, words in RED_TABLES:
        names = english if lang == "en" else german
        want: dict[str, bool] = {names["SP_PH_DONE"]: False}
        for reason, lit in faults.items():
            if reason == "NONE":
                continue
            word = names.get(f"SP_R_{reason}")
            if word is None:
                problems.append(f"{page.name}: ESC_STICK_R_{reason} has no "
                                f"SP_R_{reason} in {lang}")
                continue
            want[word] = lit
        text = read(page)
        if header not in text:
            problems.append(f"{page.name}: no table headed '{header}'")
            continue
        rows = text[text.index(header):].split("\n\n", 1)[0].splitlines()[2:]
        have: dict[str, bool | None] = {}
        for row in rows:
            cells = [c.strip() for c in row.strip().strip("|").split("|")]
            have[cells[0]] = words.get(cells[-1])
        for word, lit in want.items():
            if word not in have:
                problems.append(f"{page.name}: the red light table has no "
                                f"row for {word}")
            elif have[word] is not lit:
                problems.append(f"{page.name}: the red light table says "
                                f"{word} is {'dark' if lit else 'lit'}; "
                                "esc_stick_reason_is_fault() says otherwise")
        for word in have:
            if word not in want:
                problems.append(f"{page.name}: the red light table has a "
                                f"row for {word}, which is no end of a run")


# --- numbers the docs state, held to the constants that set them -------------

DEFINE_RE = re.compile(
    r"^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+((?:[^\n\\]|\\\n)+)$", re.M)
C_COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)

# Where a constant the docs quote is defined.  Every #define with a value in
# these trees is read; one defined with two different values is ambiguous
# and counts as unknown, so a fact that names it fails.
DEFINE_DIRS = ("shared", "firmware")

LINK_PAGES_H = "shared/link/include/link_pages.h"
CI_YML = REPO / ".github" / "workflows" / "ci.yml"


def defines() -> dict[str, str | None]:
    """NAME -> the text of its value, for every #define with one under
    shared/ and firmware/; None where two definitions disagree."""
    import os
    found: dict[str, str | None] = {}
    for base in DEFINE_DIRS:
        for dp, dn, fn in os.walk(REPO / base):
            dn[:] = sorted(d for d in dn if not d.startswith("build")
                           and d != "managed_components")
            for f in sorted(fn):
                if not f.endswith((".c", ".h")):
                    continue
                text = C_COMMENT.sub(" ", read(pathlib.Path(dp) / f))
                for name, value in DEFINE_RE.findall(text):
                    value = " ".join(value.replace("\\\n", " ").split())
                    if not value:
                        continue
                    if name in found and found[name] != value:
                        found[name] = None
                    else:
                        found[name] = value
    return found


C_NUMBER = re.compile(
    r"\b(0[xX][0-9A-Fa-f]+|\d+\.\d*(?:[eE][-+]?\d+)?|\d+)[uUlLfF]*\b")


def constant(name: str, table: dict[str, str | None],
             depth: int = 0) -> float | None:
    """The value of the #define @p name: a number, another constant, or
    arithmetic over both.  None when it is not one, or not known."""
    import ast
    text = table.get(name)
    if text is None or depth > 8:
        return None
    # GPIO_NUM_6 is ESP-IDF's name for pin 6.
    text = re.sub(r"\bGPIO_NUM_(\d+)\b", r"\1", text)
    text = re.sub(r"\(\s*(?:u?int\d+_t|unsigned|int|float)\s*\)", "", text)

    def number(m: re.Match[str]) -> str:
        raw = m.group(1)
        return str(int(raw, 16)) if raw[:2].lower() == "0x" else raw

    text = C_NUMBER.sub(number, text)

    def evaluate(node: ast.AST) -> float | None:
        if isinstance(node, ast.Expression):
            return evaluate(node.body)
        if isinstance(node, ast.Constant) and isinstance(node.value,
                                                         (int, float)):
            return float(node.value)
        if isinstance(node, ast.Name):
            return constant(node.id, table, depth + 1)
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
            inner = evaluate(node.operand)
            return None if inner is None else -inner
        if isinstance(node, ast.BinOp):
            a, b = evaluate(node.left), evaluate(node.right)
            if a is None or b is None:
                return None
            if isinstance(node.op, ast.Add):
                return a + b
            if isinstance(node.op, ast.Sub):
                return a - b
            if isinstance(node.op, ast.Mult):
                return a * b
            if isinstance(node.op, ast.Div) and b != 0:
                return a / b
        return None

    try:
        return evaluate(ast.parse(text.strip(), mode="eval"))
    except SyntaxError:
        return None


def doc_number(text: str, german: bool) -> float | None:
    """A number as a page writes it: 15,600 and 0.020 in English, 15 600 and
    0,020 in German."""
    text = re.sub(r"[\s  ]", "", text)
    text = text.replace(",", ".") if german else text.replace(",", "")
    try:
        return float(text)
    except ValueError:
        return WORDS_DE.get(text.lower()) if german else as_number(text)


WORDS_DE = {
    "eine": 1, "zwei": 2, "drei": 3, "vier": 4, "fünf": 5, "sechs": 6,
    "sieben": 7, "acht": 8, "neun": 9, "zehn": 10, "elf": 11, "zwölf": 12,
}


def python_constant(tool: str, name: str) -> float | None:
    """A number a tool assigns to a module-level name."""
    m = re.search(rf"^{name}\s*=\s*([0-9.]+)\s*$",
                  read(REPO / "tools" / tool), re.M)
    return float(m.group(1)) if m else None


# One statement a page makes: the page, a pattern over its text with line
# breaks folded to spaces, and what each group has to read.  A value is a
# constant's name, arithmetic over constants, a number, or (name, factor)
# for a constant in another unit.  A pattern that matches nowhere fails: a
# reworded sentence takes its check along.
N = r"(\d[\d,.]*)"
FACTS = [
    # The link protocol's version.
    ("docs/Link.md", rf"Protocol version {N}\.{N}\.",
     ("LINK_PROTOCOL_MAJOR", "LINK_PROTOCOL_MINOR")),
    ("docs/Link-de.md", rf"Protokollversion {N}\.{N}\.",
     ("LINK_PROTOCOL_MAJOR", "LINK_PROTOCOL_MINOR")),
    ("STATUS.md", rf"Protocol version {N}\.{N}\.",
     ("LINK_PROTOCOL_MAJOR", "LINK_PROTOCOL_MINOR")),
    # The heartbeat.
    ("docs/Safety.md", rf"\| Panel edge interval \| {N} ms \|",
     ("HEARTBEAT_PERIOD_MS",)),
    ("docs/Safety-de.md", rf"\| Flankenabstand des Panels \| {N} ms \|",
     ("HEARTBEAT_PERIOD_MS",)),
    ("docs/Safety.md", rf"\| Accepted interval \| {N}–{N} ms \|",
     ("HEARTBEAT_MIN_GAP_MS", "HEARTBEAT_MAX_GAP_MS")),
    ("docs/Safety-de.md", rf"\| Akzeptierter Abstand \| {N}–{N} ms \|",
     ("HEARTBEAT_MIN_GAP_MS", "HEARTBEAT_MAX_GAP_MS")),
    ("docs/Safety.md",
     rf"\| Good intervals before the line is trusted \| {N} \| {N} ms",
     ("HEARTBEAT_GOOD_RUN", "HEARTBEAT_GOOD_RUN * HEARTBEAT_PERIOD_MS")),
    ("docs/Safety-de.md",
     rf"\| Gute Abstände, bevor der Leitung vertraut wird \| {N} \| {N} ms",
     ("HEARTBEAT_GOOD_RUN", "HEARTBEAT_GOOD_RUN * HEARTBEAT_PERIOD_MS")),
    ("docs/Safety.md", rf"The panel waits {N} ms, then reads the STATUS",
     ("HEARTBEAT_SETTLE_MS",)),
    ("docs/Safety-de.md", rf"Das Panel wartet {N} ms, liest dann",
     ("HEARTBEAT_SETTLE_MS",)),
    ("docs/Safety.md",
     rf"one interval under {N} ms or over {N} ms, or {N} ms without an edge",
     ("HEARTBEAT_MIN_GAP_MS", "HEARTBEAT_MAX_GAP_MS",
      "HEARTBEAT_MAX_GAP_MS")),
    ("docs/Safety-de.md",
     rf"ein Abstand unter {N} ms oder über {N} ms, oder {N} ms ohne Flanke",
     ("HEARTBEAT_MIN_GAP_MS", "HEARTBEAT_MAX_GAP_MS",
      "HEARTBEAT_MAX_GAP_MS")),
    ("STATUS.md", rf"\({N} to {N} ms between edges",
     ("HEARTBEAT_MIN_GAP_MS", "HEARTBEAT_MAX_GAP_MS")),
    ("STATUS.md", rf"The task runs every {N} ms; the line edges every {N} ms",
     ("CONTROL_PERIOD_MS", "HEARTBEAT_PERIOD_MS")),
    ("docs/Safety.md", rf"runs every {N} ms on the core that does not draw",
     ("CONTROL_PERIOD_MS",)),
    ("docs/Safety-de.md", rf"alle {N} ms auf dem Kern läuft, der nicht",
     ("CONTROL_PERIOD_MS",)),
    # The link's two time limits.
    ("docs/Safety.md", rf"Coprocessor silence watchdog, {N} ms",
     ("LINK_DEV_SILENCE_MS",)),
    ("docs/Safety-de.md", rf"Stille-Watchdog des Koprozessors, {N} ms",
     ("LINK_DEV_SILENCE_MS",)),
    ("docs/Safety.md", rf"\| Link silence \| {N} ms without a request",
     ("LINK_DEV_SILENCE_MS",)),
    ("docs/Safety-de.md", rf"\| Stille auf dem Link \| {N} ms ohne Anfrage",
     ("LINK_DEV_SILENCE_MS",)),
    ("docs/Link.md", rf"failsafe values after {N} ms without a request",
     ("LINK_DEV_SILENCE_MS",)),
    ("STATUS.md", rf"fails safe after {N} ms of link silence",
     ("LINK_DEV_SILENCE_MS",)),
    ("docs/Safety.md", rf"after the {N} ms exchange timeout",
     ("LINK_HOST_TIMEOUT_MS",)),
    ("docs/Safety-de.md", rf"nach dem Timeout von {N} ms bemerkt",
     ("LINK_HOST_TIMEOUT_MS",)),
    ("STATUS.md", rf"the panel escalates after {N} s\.",
     (("LINK_HOST_TIMEOUT_MS", 0.001),)),
    # Pins.
    ("docs/Safety.md", rf"The safety line is panel GPIO{N} ",
     ("PANEL_HEARTBEAT_PIN",)),
    ("docs/Safety-de.md", rf"Die Sicherheitsleitung ist GPIO{N} ",
     ("PANEL_HEARTBEAT_PIN",)),
    ("docs/Safety.md", rf"coprocessor's GP{N} by a direct wire",
     ("IOMCU_HEARTBEAT_PIN",)),
    ("docs/Safety-de.md", rf"den GP{N} des Koprozessors",
     ("IOMCU_HEARTBEAT_PIN",)),
    ("STATUS.md", rf"control task drives GPIO{N} \(J8\)",
     ("PANEL_HEARTBEAT_PIN",)),
    ("docs/Link.md", rf"on GPIO{N} \(RX\) and GPIO{N} \(TX\)",
     ("PANEL_CAN_PIN_RX", "PANEL_CAN_PIN_TX")),
    ("docs/Link-de.md", rf"auf GPIO{N} \(RX\) und GPIO{N} \(TX",
     ("PANEL_CAN_PIN_RX", "PANEL_CAN_PIN_TX")),
    ("docs/Link.md",
     rf"SCK GP{N}, MOSI GP{N}, MISO GP{N}, CS GP{N}, INT GP{N};",
     ("IOMCU_CAN_PIN_SCK", "IOMCU_CAN_PIN_MOSI", "IOMCU_CAN_PIN_MISO",
      "IOMCU_CAN_PIN_CS", "IOMCU_CAN_PIN_INT")),
    ("docs/Link-de.md",
     rf"SCK GP{N}, MOSI GP{N}, MISO GP{N}, CS GP{N}, INT GP{N};",
     ("IOMCU_CAN_PIN_SCK", "IOMCU_CAN_PIN_MOSI", "IOMCU_CAN_PIN_MISO",
      "IOMCU_CAN_PIN_CS", "IOMCU_CAN_PIN_INT")),
    ("docs/Link.md", rf"\| Bus \| classic CAN [^|]*?, {N} Mbit/s",
     (("IOMCU_CAN_BITRATE", 1e-6),)),
    ("docs/Link.md", rf"\| Bus \| classic CAN [^|]*?, {N} Mbit/s",
     (("PANEL_CAN_BITRATE", 1e-6),)),
    # Limits a check enforces.
    ("CONTRIBUTING.md",
     rf"coverage drops below {N}% overall, or any single file below {N}%",
     ("coverage.py:MIN_TOTAL_COVERAGE", "coverage.py:MIN_FILE_COVERAGE")),
    ("STATUS.md", rf"Coverage floors: {N}% overall, {N}% for every file",
     ("coverage.py:MIN_TOTAL_COVERAGE", "coverage.py:MIN_FILE_COVERAGE")),
    ("codecov.yml", rf"{N}% overall and {N}% for any single file",
     ("coverage.py:MIN_TOTAL_COVERAGE", "coverage.py:MIN_FILE_COVERAGE")),
    ("codecov.yml", rf"project: default: target: {N}%",
     ("coverage.py:MIN_TOTAL_COVERAGE",)),
    ("CONTRIBUTING.md", rf"exceeds its stack less {N} bytes",
     ("stack_check.py:MARGIN",)),
    ("STATUS.md", rf"deepest call chain to its stack less {N} bytes",
     ("stack_check.py:MARGIN",)),
    ("docs/Performance.md", rf"exceeds its stack less {N} bytes",
     ("stack_check.py:MARGIN",)),
    ("docs/Performance-de.md", rf"abzüglich {N} Bytes überschreitet",
     ("stack_check.py:MARGIN",)),
]


def fact_value(want, table: dict[str, str | None]) -> float | None:
    factor = 1.0
    if isinstance(want, tuple):
        want, factor = want
    if isinstance(want, (int, float)):
        return float(want) * factor
    if ":" in want:
        tool, name = want.split(":")
        value = python_constant(tool, name)
    else:
        value = constant("", {**table, "": want})
    return None if value is None else value * factor


def same(a: float, b: float) -> bool:
    return abs(a - b) <= 1e-6 * max(1.0, abs(a), abs(b))


def check_facts(problems: list[str], facts=None,
                table: dict[str, str | None] | None = None) -> None:
    """Every statement in FACTS reads what its constant is."""
    table = defines() if table is None else table
    for page, pattern, wants in (FACTS if facts is None else facts):
        path = REPO / page
        name = path.name
        # Comment markers of a YAML file go with the line breaks.
        text = " ".join(re.sub(r"^\s*#", "", read(path), flags=re.M).split())
        matches = list(re.finditer(pattern, text))
        if not matches:
            problems.append(f"{name}: no statement matches /{pattern}/, "
                            f"which held {', '.join(map(str, wants))}")
            continue
        german = name.endswith(DE_SUFFIX)
        for m in matches:
            for group, want in zip(m.groups(), wants, strict=True):
                value = fact_value(want, table)
                label = want[0] if isinstance(want, tuple) else want
                if value is None:
                    problems.append(f"{name}: {label} is not a constant "
                                    "with one value in the tree")
                    continue
                said = doc_number(group, german)
                if said is None or not same(said, value):
                    problems.append(
                        f"{name}: says {group} in '{m.group(0)}'; "
                        f"{label} makes it {value:g}")


CONSTANT_ROW = re.compile(
    r"^\|\s*`([A-Z][A-Z0-9_]+)`\s*\|\s*([-+]?\d[\d.,  ]*?)\s*"
    r"([A-Za-zµ%°]*)\s*\|", re.M)


def check_constant_tables(problems: list[str],
                          table: dict[str, str | None] | None = None,
                          docs: list[pathlib.Path] | None = None) -> None:
    """A table row that gives a C constant by name gives its value: the
    servo test's constants in Servo.md and Servo-de.md, and any row of the
    same shape, `| \\`NAME\\` | 1000 ms |`, on any page."""
    table = defines() if table is None else table
    for page in (linked_pages() if docs is None else docs):
        german = page.name.endswith(DE_SUFFIX)
        text = read(page)
        for m in CONSTANT_ROW.finditer(text):
            name, said_text = m.group(1), m.group(2)
            if name not in table:
                continue                  # a register or a label, no #define
            line = text.count("\n", 0, m.start()) + 1
            value = constant(name, table)
            said = doc_number(said_text, german)
            if value is None:
                problems.append(f"{page.name}:{line}: {name} is not a "
                                "constant with one value in the tree")
            elif said is None or not same(said, value):
                problems.append(f"{page.name}:{line}: gives {name} as "
                                f"{said_text.strip()}; the header says "
                                f"{value:g}")


# --- the ceiling table -------------------------------------------------------

CEILING_PAGES = (
    (DOCS / "Performance.md", "| Modes | Ceiling (fills) | Catches |"),
    (DOCS / "Performance-de.md", "| Modi | Obergrenze (Fills) | Fängt |"),
)


def ci_ceilings(text: str) -> list[tuple[frozenset[str], int]]:
    """(modes, ceiling) for every frame_cost.py call with --max-lines in
    the CI workflow."""
    folded = re.sub(r"\n\s+(?=&&|--|[a-z])", " ", text)
    out = []
    for m in re.finditer(
            r"tools/frame_cost\.py ([a-z0-9 -]*?)\s*--max-lines (\d+)",
            folded):
        out.append((frozenset(m.group(1).split()), int(m.group(2))))
    return out


def doc_ceilings(text: str, header: str, german: bool):
    """(named modes, count, suffix, ceiling) for every row of the ceiling
    table: the modes a row names in backticks, or how many it speaks of
    and the suffix they share."""
    if header not in text:
        return None
    rows = text[text.index(header):].split("\n\n", 1)[0].splitlines()[2:]
    out = []
    for row in rows:
        cells = [c.strip() for c in row.strip().strip("|").split("|")]
        quoted = re.findall(r"`([a-z0-9-]+)`", cells[0])
        suffix = next((q for q in quoted if q.startswith("-")), None)
        named = frozenset(q for q in quoted if not q.startswith("-"))
        count = None
        if not named:
            for word in re.findall(r"\w+", cells[0]):
                n = doc_number(word, german)
                if n is not None:
                    count = int(n)
                    break
        ceiling = doc_number(cells[1], german)
        out.append((named, count, suffix,
                    None if ceiling is None else int(ceiling), cells[0]))
    return out


def check_ceilings(problems: list[str], ci_text: str | None = None,
                   pages=None) -> None:
    """The ceiling table in Performance.md gives, for every mode CI holds,
    the ceiling CI holds it to, and has no row CI does not run."""
    calls = ci_ceilings(read(CI_YML) if ci_text is None else ci_text)
    if not calls:
        problems.append("ci.yml: no frame_cost.py call with --max-lines")
        return
    for page, header in (CEILING_PAGES if pages is None else pages):
        german = page.name.endswith(DE_SUFFIX)
        rows = doc_ceilings(read(page), header, german)
        if rows is None:
            problems.append(f"{page.name}: no table headed '{header}'")
            continue
        left = list(calls)
        for named, count, suffix, ceiling, label in rows:
            hit = None
            for call in left:
                modes, limit = call
                if named:
                    fits = modes == named
                else:
                    fits = (count == len(modes) and all(
                        mode.endswith(suffix) if suffix else "-" not in mode
                        for mode in modes))
                if fits:
                    hit = call
                    break
            if hit is None:
                problems.append(f"{page.name}: the ceiling table's row "
                                f"'{label}' is no frame_cost.py call in "
                                "ci.yml")
                continue
            left.remove(hit)
            if ceiling != hit[1]:
                problems.append(f"{page.name}: the ceiling table gives "
                                f"'{label}' as {ceiling}; ci.yml holds it "
                                f"to {hit[1]}")
        for modes, limit in left:
            problems.append(f"{page.name}: the ceiling table has no row "
                            f"for {', '.join(sorted(modes))}, which ci.yml "
                            f"holds to {limit}")


# --- the IO board's pin count ------------------------------------------------

PINMAP = HARDWARE / "docs" / "pinmap.json"
PIN_ROWS = {"main": "Main coprocessor", "measurement":
            "Measurement coprocessor"}


def check_pin_counts(problems: list[str]) -> None:
    """Pins.md's count of the GPIO each coprocessor uses is the number of
    pins pinmap.json gives it, and STATUS.md repeats the same two."""
    import json
    chips = {c.get("chip"): len(c.get("pins", []))
             for c in json.loads(read(PINMAP)).get("chips", [])}
    text = read(HARDWARE / "docs" / "Pins.md")
    for chip, label in PIN_ROWS.items():
        m = re.search(rf"^\| {label} \| (\d+) of (\d+) \| (\d+)", text, re.M)
        if m is None:
            problems.append(f"Pins.md: no row for the {label.lower()}")
            continue
        used, total, free = (int(g) for g in m.groups())
        if used != chips.get(chip) or free != total - used:
            problems.append(
                f"Pins.md: says the {label.lower()} uses {used} of {total} "
                f"with {free} free; pinmap.json gives it {chips.get(chip)}")
    m = re.search(r"uses (\d+) and (\d+) of their (\d+) GPIO",
                  " ".join(read(REPO / "STATUS.md").split()))
    if m is None:
        problems.append("STATUS.md: no 'uses <N> and <N> of their 48 GPIO'")
    elif (int(m.group(1)), int(m.group(2))) != (chips.get("main"),
                                                chips.get("measurement")):
        problems.append(f"STATUS.md: says {m.group(0)}; pinmap.json gives "
                        f"{chips.get('main')} and "
                        f"{chips.get('measurement')}")


def check_spdx(problems: list[str]) -> None:
    """Every source file carries an SPDX (Software Package Data Exchange)
    licence line.  A new file without one fails the build.
    """
    import os
    for base in ("shared", "firmware", "test"):
        for dp, dn, fn in os.walk(REPO / base):
            # Prune build trees in place (build, build-san, build-cov) so the
            # walk never reaches a toolchain's own probe files.
            dn[:] = [d for d in dn if not d.startswith("build")]
            for f in fn:
                if not f.endswith((".c", ".h")):
                    continue
                path = pathlib.Path(dp) / f
                if "SPDX-License-Identifier" not in read(path):
                    rel = path.relative_to(REPO)
                    problems.append(f"{rel} has no SPDX-License-Identifier")


def main() -> int:
    problems: list[str] = []
    check_links(problems)
    check_version(problems)
    check_anchors(problems)
    check_sidebar(problems)
    check_translations(problems)
    check_suites(problems)
    check_option_lists(problems)
    check_quoted_labels(problems)
    check_shared_modules(problems)
    check_compile_table(problems)
    check_screenshot_count(problems)
    check_red_light_tables(problems)
    table = defines()
    check_facts(problems, table=table)
    check_constant_tables(problems, table=table)
    check_ceilings(problems)
    check_pin_counts(problems)
    check_spdx(problems)

    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        print(f"\n{len(problems)} stale claim(s) in the docs", file=sys.stderr)
        return 1
    print("docs agree with the tree")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
