#!/usr/bin/env python3
"""Hold every number in the human-read text to its unit with no space between.

The rule: a number and its unit symbol stand together, as in 40s, 6.00V,
20ms, 1.6MB/s, 180°, 100mΩ and 4.8 to 6.0V.  A space lets a line wrap
between the number and its unit.  A word after a number is not a unit and
keeps its space (3 servos, 8 values, 115200 baud, 20 AWG), and so does an
x between two numbers of a size (1024x600 has none to begin with).

Covered, each as the text a reader sees:

- the documents: README.md, README-de.md, STATUS.md, CONTRIBUTING.md,
  SECURITY.md, the unreleased section of CHANGELOG.md, every Markdown page
  under docs/, help/, hardware/, protocols/, testbench/ and configurator/;
- the text the interface shows and the reports it writes: every string of
  shared/text/include/ui_text.def (screens, settings, the servo test and
  its .TXT reports, the Wi-Fi page) and every string literal of
  STRING_FILES below;
- the texts the panel carries from the ESC profiles of
  shared/esc/profiles/: the strings of PROFILE_TEXTS below, the ones
  tools/gen_esc_profiles.py compiles into the C, which the ESC STICK
  screen and its lists show;
- the comments of the code: C, C++, headers, TypeScript, JavaScript and PIO
  (/* */ and //, `;` in PIO), Python (# and docstrings), and shell, YAML,
  CMake and TOML (#).

Not covered: the released sections of CHANGELOG.md, which are history;
CHANGELOG-0.9.md; the research records under hardware/research/; string
literals outside the files above (log lines, console and wire formats,
CSV headers, test fixtures of external formats); data files (JSON, CSV,
SVG) other than the ESC profiles; the profiles' keys and their research
fields (sources, notes, unknowns, descriptions, a model's spec_source and
firmware_note), which no screen shows; a Markdown heading's section
number; and the frame cost table tools/frame_cost.py writes into
docs/Performance.md.

A deliberate exception is an entry in tools/check_units_allow.txt: a
comment line with the reason, then `path: text`, where text is a part of
the line that holds the number and its unit, or `path: re:pattern`, a
regular expression searched in the line.  An entry that matches no line
fails, so an exception does not outlive its text.

    tools/check_units.py            every number-space-unit, one per line
    tools/check_units.py --check    and exit 1 if there is one, or a stale
                                    allowlist entry
    tools/check_units.py --fix      remove the space in place, except the
                                    allowed ones

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import ast
import io
import re
import subprocess
import sys
import tokenize
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ALLOW = ROOT / "tools" / "check_units_allow.txt"

# The unit symbols, re-derived from the tree.  Longest first, so that kHz is
# tried before Hz and °C before °.  The panel's ui_unit_gap() decides by the
# same list, k_unit_symbols in shared/ui/ui_text.c; test_check_units.py
# holds the two to each other.
UNITS = sorted("""
    kbit/s Mbit/s bit/s kB/s MB/s KB/s B/s KiB/s MiB/s °/s deg/s
    mAh mWh kWh Wh Ah
    GHz MHz kHz Hz
    µs μs us ns ms min s h
    kV mV µV μV V mA µA μA uA nA A mW kW W
    mΩ kΩ MΩ Ω mOhm kOhm Ohm ohm
    °C °F ° %% % deg
    KiB MiB GiB kB KB MB GB B kbps Mbps bps
    px dBm dB rpm RPM eRPM fps ppm sps
    mm cm µm nm m kg g Ncm Nm N
    µF μF uF nF pF mF F µH uH mH nH H K C
""".split(), key=len, reverse=True)

# A number: digits with decimal or group separators, a size such as
# 1024x600 (whose last part is the number before the unit), or a printf
# conversion in a format string, also in two as in "%u.%02u".
CONVERSION = (r"%[-+ #0']*(?:\d+|\*)?(?:\.(?:\d+|\*))?"
              r"(?:hh|h|ll|l|z|j|t|L)?[diouxXfFeEgGs]")
NUMBER = (r"(?:\d+(?:[.,]\d+)*(?:x\d+(?:[.,]\d+)*)?"
          r"|" + CONVERSION + r"(?:[.,]" + CONVERSION + r")*)")
# A number starts after no letter, digit or point, or after the ".." of a
# range such as 2392..14639rpm.  A unit ends where no letter, digit, % or =
# follows (ms=110 is a key), a %s conversion excepted, which appends a
# mark to the unit (ESC STICK's ENTRY seconds and "(profile)"), and a lone
# % is not the start of the next
# conversion, as in "%6s %-6s" or "%d %.*f"; a "**" after it closes
# Markdown bold.
PERCENT = r"%(?![-+#'\d\w]|\.[\d*]|\*[\w.])"
UNIT = "|".join(re.escape(u) if u != "%" else PERCENT for u in UNITS)
PATTERN = re.compile(r"(?:(?<![\w.])|(?<=\.\.))(" + NUMBER
                     + r")([ \u00a0\u202f]+)(" + UNIT
                     + r")(?![\w=]|%(?!s\b))")

# The C files outside ui_text.def whose string literals are text a screen
# shows or a report on the card holds.
STRING_FILES = (
    "shared/text/",
    "shared/ui/",
    "shared/webaccess/wa_page.c",
    "shared/servo/servo_report.c",
    "shared/servo/servo_limrun_report.c",
    "firmware/panel/main/panel_boot.c",
)

DOC_FILES = {"README.md", "README-de.md", "STATUS.md", "CONTRIBUTING.md",
             "SECURITY.md", "CHANGELOG.md"}
DOC_DIRS = ("docs/", "help/", "hardware/", "protocols/", "testbench/",
            "configurator/")
NOT_COVERED = ("hardware/research/",)
C_SUFFIXES = (".c", ".h", ".cpp", ".cc", ".hpp", ".def", ".ts", ".tsx",
              ".js", ".mjs", ".cjs")
HASH_SUFFIXES = (".sh", ".yml", ".yaml", ".cmake", ".toml")
HASH_NAMES = ("CMakeLists.txt", "Doxyfile", ".clang-tidy", ".gitattributes",
              ".gitignore")
MASK = "\0"

# The ESC profiles, and in each the strings the panel carries, by their
# place in the file: a key per object, "[]" per list element.  These are
# the strings tools/gen_esc_profiles.py writes into the C;
# test_check_units.py holds the two to each other.
PROFILE_DIR = "shared/esc/profiles/"
PROFILE_TEXTS = frozenset({
    ("brand",), ("family",), ("automatable_note",),
    ("scheme", "entry", "steps", "[]"),
    ("models", "[]", "name"),
    ("items", "[]", "name"), ("items", "[]", "applies_when"),
    ("items", "[]", "values", "[]", "name"),
    ("manual", "[]", "action"), ("manual", "[]", "action_de"),
})


def mask(text: str, keep: list) -> str:
    """Returns text with every character outside keep replaced by MASK.

    Args:
        text: The file's text.
        keep: (start, end) offsets of the parts that are covered.

    Returns:
        A string as long as text, newlines kept, so offsets and line
        numbers stay those of the file.
    """
    out = [MASK if ch != "\n" else ch for ch in text]
    for start, end in keep:
        out[start:end] = text[start:end]
    return "".join(out)


def c_parts(text: str, strings: bool, pio: bool = False,
            groups: list | None = None) -> list:
    """Finds the comments, and with strings the string literals, of C.

    Args:
        text: The source.
        strings: True to cover string literals too.  An escape sequence in
            a literal is left out, so an escaped newline before a number
            does not hide it.
        pio: True for a PIO program, where `;` starts a comment too.
        groups: With strings, filled with one entry per run of adjacent
            literals ("in the last 200 " "ms"), each a list of literals,
            each a list of the (start, end) parts between its escapes.

    Returns:
        (start, end) offsets of the covered parts.
    """
    keep = []
    i, n = 0, len(text)
    last = -1
    while i < n:
        ch = text[i]
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            keep.append((i, end))
            i = end
        elif text.startswith("//", i) or (pio and ch == ";"):
            end = text.find("\n", i)
            end = n if end < 0 else end
            keep.append((i, end))
            i = end
        elif ch in "\"'`":
            j = i + 1
            run = j
            parts = []
            while j < n and text[j] != ch and (ch == "`"
                                               or text[j] != "\n"):
                if text[j] == "\\":
                    parts.append((run, j))
                    j += 2
                    run = j
                    continue
                j += 1
            parts.append((run, min(j, n)))
            if strings and ch == '"':
                keep.extend(parts)
                if groups is not None:
                    if last >= 0 and not text[last:i].strip():
                        groups[-1].append(parts)
                    else:
                        groups.append([parts])
                last = j + 1
            i = j + 1
        else:
            i += 1
    return keep


def python_parts(text: str) -> list:
    """Finds the comments and the docstrings of Python source.

    Args:
        text: The source.

    Returns:
        (start, end) offsets; none for a file that does not parse.
    """
    lines = text.splitlines(keepends=True)
    starts = [0]
    for line in lines:
        starts.append(starts[-1] + len(line))

    def at_byte(row: int, col: int) -> int:
        return starts[row - 1] + len(lines[row - 1].encode()[:col]
                                     .decode(errors="ignore"))

    keep = []
    try:
        tree = ast.parse(text)
        for tok in tokenize.generate_tokens(io.StringIO(text).readline):
            if tok.type == tokenize.COMMENT:
                keep.append((starts[tok.start[0] - 1] + tok.start[1],
                             starts[tok.end[0] - 1] + tok.end[1]))
    except (SyntaxError, tokenize.TokenError):
        return []
    for node in ast.walk(tree):
        if isinstance(node, (ast.Module, ast.ClassDef, ast.FunctionDef,
                             ast.AsyncFunctionDef)) and node.body:
            first = node.body[0]
            if (isinstance(first, ast.Expr)
                    and isinstance(first.value, ast.Constant)
                    and isinstance(first.value.value, str)):
                v = first.value
                keep.append((at_byte(v.lineno, v.col_offset),
                             at_byte(v.end_lineno, v.end_col_offset)))
    return keep


def hash_parts(text: str) -> list:
    """Finds the # comments of shell, YAML, CMake and TOML.

    A # starts a comment at the start of a line or after a space, outside
    quotes.

    Args:
        text: The file's text.

    Returns:
        (start, end) offsets of the comments.
    """
    keep = []
    pos = 0
    for line in text.splitlines(keepends=True):
        quote = None
        for k, ch in enumerate(line):
            if quote:
                if ch == quote:
                    quote = None
            elif ch in "\"'":
                quote = ch
            elif ch == "#" and (k == 0 or line[k - 1] in " \t"):
                keep.append((pos + k, pos + len(line.rstrip("\n"))))
                break
        pos += len(line)
    return keep


def json_strings(text: str) -> list:
    """Finds the string values of a JSON text with their place in it.

    A scan, not a parse: a file that does not parse is read as far as it
    goes, and a string ends at its closing quote or at the line's end.

    Args:
        text: The JSON text.

    Returns:
        (place, start, end) per string value, place a tuple of the keys and
        "[]" from the root down, start and end the offsets of the text
        between the quotes.  Keys are not values and are not listed.
    """
    out = []
    stack = []      # [is_object, key, wants_key] per open container
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if ch in "{[":
            stack.append([ch == "{", None, ch == "{"])
        elif ch in "}]":
            if stack:
                stack.pop()
        elif ch == "," and stack and stack[-1][0]:
            stack[-1][2] = True
        elif ch == '"':
            j = i + 1
            while j < n and text[j] not in '"\n':
                j += 2 if text[j] == "\\" else 1
            end = min(j, n)
            if stack and stack[-1][0] and stack[-1][2]:
                stack[-1][1] = text[i + 1:end]
                stack[-1][2] = False
            else:
                place = tuple(f[1] if f[0] else "[]" for f in stack)
                out.append((place, i + 1, end))
            i = j
        i += 1
    return out


def json_parts(text: str) -> list:
    """Covers the strings of PROFILE_TEXTS in an ESC profile.

    An escape sequence is left out, as in c_parts().

    Args:
        text: The profile's text.

    Returns:
        (start, end) offsets of the covered parts.
    """
    keep = []
    for place, start, end in json_strings(text):
        if place not in PROFILE_TEXTS:
            continue
        run = j = start
        while j < end:
            if text[j] == "\\":
                keep.append((run, j))
                j += 2
                run = j
                continue
            j += 1
        keep.append((run, min(j, end)))
    return keep


SECTION = re.compile(r"^(#{1,6}[ \t]+)(\d+(?:\.\d+)*)", re.M)


# A block a tool writes into a page, between <!-- name:start --> and
# <!-- name:end -->, as that tool prints it: the frame cost table of
# tools/frame_cost.py --update-doc.
TOOL_BLOCKS = re.compile(r"<!-- (framecost):start -->.*?<!-- \1:end -->",
                         re.S)


def markdown_view(text: str) -> str:
    """Masks what a page holds that is not its own prose.

    A heading's section number, so the "8.4" of a heading "8.4" + "A run"
    reads as no current, and the blocks of TOOL_BLOCKS.
    """
    text = SECTION.sub(lambda m: m.group(1) + MASK * len(m.group(2)), text)
    return TOOL_BLOCKS.sub(lambda m: "".join(c if c == "\n" else MASK
                                             for c in m.group(0)), text)


def changelog_parts(text: str) -> list:
    """Covers CHANGELOG.md up to its first released section."""
    m = re.search(r"^## \d", text, re.M)
    return [(0, m.start() if m else len(text))]


def covered(path: str, text: str, groups: list | None = None) -> str | None:
    """Returns the file's text masked to its covered parts, or None.

    Args:
        path: The file's path in the tree.
        text: Its text.
        groups: Filled with the runs of adjacent string literals of a file
            whose literals are covered (c_parts()).
    """
    if path.startswith(NOT_COVERED):
        return None
    name = path.rsplit("/", 1)[-1]
    if path == "CHANGELOG.md":
        return markdown_view(mask(text, changelog_parts(text)))
    if path.endswith(".md"):
        if path in DOC_FILES or path.startswith(DOC_DIRS):
            return markdown_view(text)
        return None
    if path.endswith(C_SUFFIXES):
        return mask(text, c_parts(text, path.startswith(STRING_FILES),
                                  groups=groups))
    if path.startswith(PROFILE_DIR) and path.endswith(".json"):
        return mask(text, json_parts(text))
    if path.endswith(".pio"):
        return mask(text, c_parts(text, False, pio=True))
    if path.endswith(".py"):
        return mask(text, python_parts(text))
    if path.endswith(HASH_SUFFIXES) or name in HASH_NAMES:
        return mask(text, hash_parts(text))
    return None


def tracked() -> list:
    """Returns the paths git tracks, relative to the tree's root."""
    out = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, check=True,
                         capture_output=True).stdout.decode()
    return [p for p in out.split("\0") if p]


def parse_allow(text: str) -> list:
    """Reads the allowlist: [path, text, reason, line number] per entry."""
    entries, reason = [], ""
    for num, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            reason = ""
        elif line.startswith("#"):
            reason = (reason + " " + line[1:].strip()).strip()
        else:
            path, sep, part = line.partition(": ")
            if not sep or not part or not reason:
                raise SystemExit(f"{ALLOW.name}:{num}: not `path: text` "
                                 "under a reason")
            entries.append([path, part, reason, num])
    return entries


def allows(part: str, line: str) -> bool:
    """True when an allowlist entry's text matches the line."""
    if part.startswith("re:"):
        return re.search(part[3:], line) is not None
    return part in line


def joined_hits(text: str, groups: list) -> list:
    """Finds a number and its unit split over two adjacent literals.

    Args:
        text: The file's text.
        groups: Runs of adjacent literals, from c_parts().

    Returns:
        (match start, offsets of the space) per hit that crosses a literal
        boundary; one inside a single literal the masked view finds.
    """
    hits = []
    for group in groups:
        if len(group) < 2:
            continue
        chars, where = [], []
        for parts in group:
            for k, (a, b) in enumerate(parts):
                if k:
                    chars.append(MASK)
                    where.append(-1)
                chars.extend(text[a:b])
                where.extend(range(a, b))
        joined = "".join(chars)
        for m in PATTERN.finditer(joined):
            span = where[m.start():m.end()]
            if span != list(range(span[0], span[0] + len(span))):
                hits.append((span[0], where[m.start(2):m.end(2)]))
    return hits


def find(path: str, text: str) -> list:
    """Returns (line number, offsets of the space, line) per hit."""
    groups = []
    view = covered(path, text, groups)
    if view is None:
        return []
    found = [(m.start(), list(range(m.start(2), m.end(2))))
             for m in PATTERN.finditer(view)]
    found += joined_hits(text, groups)
    lines = text.split("\n")
    hits = []
    for start, gap in sorted(found):
        row = text.count("\n", 0, start)
        hits.append((row + 1, gap, lines[row]))
    return hits


def main(argv: list) -> int:
    """Lists, checks or fixes every number-space-unit of the tree.

    Args:
        argv: The arguments: --check, --fix, or none.

    Returns:
        1 for --check with a hit or a stale entry, else 0.
    """
    check = "--check" in argv
    fix = "--fix" in argv
    allow = parse_allow(ALLOW.read_text(encoding="utf-8")) \
        if ALLOW.exists() else []
    used = set()
    found = 0
    for path in tracked():
        full = ROOT / path
        if not full.is_file():
            continue
        try:
            text = full.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        cuts = []
        for row, gap, line in find(path, text):
            hit = [e for e in allow if e[0] == path and allows(e[1], line)]
            if hit:
                used.update(e[3] for e in hit)
                continue
            found += 1
            cuts.extend(gap)
            print(f"{path}:{row}: {line.strip()}")
        if fix and cuts:
            drop = set(cuts)
            full.write_text("".join(ch for k, ch in enumerate(text)
                                    if k not in drop), encoding="utf-8")
    stale = [e for e in allow if e[3] not in used]
    for path, part, _, num in stale:
        print(f"{ALLOW.name}:{num}: matches nothing: {path}: {part}")
    if found or stale:
        print(f"{found} number(s) with a space before the unit, "
              f"{len(stale)} stale allowlist entr(y/ies)", file=sys.stderr)
    return 1 if check and (found or stale) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
