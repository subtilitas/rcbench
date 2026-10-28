#!/usr/bin/env python3
"""Hold the Markdown documents to the owner's writing rules.

The rules, each a name in the output and in tools/check_prose_allow.txt:

- dash            " -- " used as a dash.
- marketing       powerful, seamless, robust, blazing, comprehensive,
                  simply, just, easy (whole words, any case).
- narrator-en     we, our, us in an English page.
- narrator-de     wir, unser in a German page.
- history-en      previously, now, no longer, used to, formerly in an
                  English page.  "no longer than" and "used to" after a
                  form of "be" or a modal verb ("is used to") are left out.
- history-de      bisher, jetzt, nicht mehr in a German page.  "nicht mehr
                  als" (no more than) is left out.  A word in capitals is a
                  label of the panel and is left out of both.
- exclamation     "!" in prose.
- emoji           an emoji, a pictograph or a check mark symbol in prose.
- stale-esp32s3   ESP32-S3: the panel is an ESP32-P4.
- stale-simulation  SIMULATION: the mark on a tile and a pill read
                  SIMULATED (SIMULIERT).  The watermark across a simulated
                  bench reads SIMULATION and stands in the allowlist.
- stale-coprocessor  coprocessor, Koprozessor: the board is the IOmcu.
- stale-usable    "usable release": the word is "stable release".

A number and its unit stand together with no space between.  That rule is
tools/check_units.py, which already covers every file this tool reads (the
test holds the two to each other), so it is not repeated here.

Read are README.md, README-de.md, STATUS.md, SECURITY.md, CONTRIBUTING.md,
every page under docs/, help/content/, testbench/ and hardware/, the
README.md of each directory under protocols/ and configurator/.  CHANGELOG
pages are history and are not read.  Left out of every page: fenced and
indented code blocks, inline code, link targets, bare URLs, HTML tags and
comments, the front matter, and table separator rows.  A page is German
when its name ends in -de.md or it lies under a directory named de.

A deliberate exception is an entry in tools/check_prose_allow.txt: comment
lines with the reason, then `path: rule: text`, where text is a part of the
line that holds the hit, or `re:pattern`, a regular expression searched in
that line.  An entry that matches no hit fails, so an exception does not
outlive its text.

    tools/check_prose.py                  every hit, one per line, and the
                                          count per rule and per file
    tools/check_prose.py --check          and exit 1 if there is a hit or a
                                          stale allowlist entry
    tools/check_prose.py --long           also list sentences of more than
                                          40 words (never part of --check)
    tools/check_prose.py --report FILE    write the hits as Markdown, grouped
                                          by file, with a split in two
                                          halves of equal hit count
    tools/check_prose.py PATH...          read these files only

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ALLOW = ROOT / "tools" / "check_prose_allow.txt"
MASK = "\0"
LONG_WORDS = 40

SCOPE_FILES = frozenset({"README.md", "README-de.md", "STATUS.md",
                         "SECURITY.md", "CONTRIBUTING.md"})
SCOPE_DIRS = ("docs/", "help/content/", "testbench/", "hardware/")
SCOPE_README_DIRS = ("protocols/", "configurator/")


def in_scope(path: str) -> bool:
    """Tells whether a tracked path is a page this tool reads.

    Args:
        path: The path relative to the tree's root, with "/" separators.

    Returns:
        True for a Markdown page of the scope in the module docstring.
    """
    name = path.rsplit("/", 1)[-1]
    if name.startswith("CHANGELOG"):
        return False
    if "/node_modules/" in "/" + path:
        return False
    if path in SCOPE_FILES:
        return True
    if not path.endswith(".md"):
        return False
    if path.startswith(SCOPE_DIRS):
        return True
    return path.startswith(SCOPE_README_DIRS) and name == "README.md"


def is_german(path: str) -> bool:
    """Tells whether a page is German, by its name or its directory."""
    return path.endswith("-de.md") or "/de/" in "/" + path


def blank(m: re.Match) -> str:
    """Returns the match's text with every character but newlines masked."""
    return "".join(c if c == "\n" else MASK for c in m.group(0))


FENCE = re.compile(r"^[ \t]*(`{3,}|~{3,})")
FRONT_MATTER = re.compile(r"\A---\n.*?\n---[ \t]*(?:\n|\Z)", re.S)
COMMENT = re.compile(r"<!--.*?-->", re.S)
# A code span: a run of backticks, text that holds no blank line, the same
# run again.
CODE_SPAN = re.compile(r"(?<!`)(`+)(?!`)((?:(?!\n[ \t]*\n).)+?)(?<!`)\1(?!`)",
                       re.S)
LINK_TARGET = re.compile(r"(?<=\])\((?:[^()\s]|\([^()]*\))*"
                         r"(?:[ \t]+\"[^\"]*\")?\)")
REF_DEF = re.compile(r"^ {0,3}\[[^\]\n]+\]:[ \t]*\S+.*$", re.M)
URL = re.compile(r"<https?://[^>\s]+>|https?://[^\s)>\]]+")
TAG = re.compile(r"</?[A-Za-z][^>\n]*>")
TABLE_RULE = re.compile(r"^[ \t]*\|?[ \t:|-]*-[ \t:|-]*$", re.M)


def mask_fences(text: str) -> str:
    """Masks fenced code blocks, fence lines included.

    A block ends at a fence of the same character and at least the same
    length, or at the end of the text.

    Args:
        text: The page's text.

    Returns:
        The text with each block's characters masked, newlines kept.
    """
    out = []
    fence = None
    for line in text.split("\n"):
        m = FENCE.match(line)
        if fence is None:
            if m:
                fence = m.group(1)
                out.append(MASK * len(line))
            else:
                out.append(line)
        else:
            out.append(MASK * len(line))
            if m and m.group(1)[0] == fence[0] and \
                    len(m.group(1)) >= len(fence) and \
                    not line.strip().strip(fence[0]):
                fence = None
    return "\n".join(out)


LIST_ITEM = re.compile(r"^(?:[-*+]|\d+[.)])[ \t]")


def mask_indented(text: str) -> str:
    """Masks indented code blocks.

    A run of lines indented by 4 spaces or a tab is code when a blank line
    or the start of the page comes before it and no list is open.  A list is
    open from an item at the left margin until the next line there that is
    no item.

    Args:
        text: The page's text.

    Returns:
        The text with each block's characters masked, newlines kept.
    """
    out = []
    in_list = False
    prev_blank = True
    code = False
    for line in text.split("\n"):
        if not line.strip():
            out.append(line if not code else MASK * len(line))
            prev_blank = True
            continue
        indented = line.startswith(("    ", "\t"))
        if not indented:
            in_list = LIST_ITEM.match(line) is not None
            code = False
        elif not in_list and (code or prev_blank):
            code = True
        else:
            code = False
        out.append(MASK * len(line) if code else line)
        prev_blank = False
    return "\n".join(out)


def prose_view(text: str) -> str:
    """Masks everything of a page that is not its own prose.

    Args:
        text: The page's text.

    Returns:
        A string as long as text, newlines kept, so offsets and line
        numbers stay those of the page.
    """
    view = mask_indented(mask_fences(text))
    for pat in (FRONT_MATTER, COMMENT, CODE_SPAN, LINK_TARGET, REF_DEF, URL,
                TAG, TABLE_RULE):
        view = pat.sub(blank, view)
    return view


def word(*words: str) -> str:
    """Returns a pattern for whole words, none a part of a longer word."""
    return r"(?<!\w)(?:" + "|".join(words) + r")(?!\w)"


# The previous word before "used to" that makes it a passive or a modal use.
PASSIVE = frozenset("""is are was were be been being get gets getting got
    also can could may might must should shall will would often only
    commonly typically usually then""".split())

EMOJI = re.compile("[⌀-⏿☀-➿⭐⭕⤴⤵"
                   "〰〽️\U0001f000-\U0001faff]")


def skip_used_to(view: str, m: re.Match) -> bool:
    """True for "used to" after a form of "be" or a modal verb."""
    prev = re.findall(r"[A-Za-z]+", view[max(0, m.start() - 20):m.start()])
    return bool(prev) and prev[-1].lower() in PASSIVE


def skip_history_en(view: str, m: re.Match) -> bool:
    """True for "no longer than", a passive "used to", a label in capitals."""
    hit = m.group(0).lower()
    if m.group(0).isupper():
        return True
    if hit.startswith("no"):
        return re.match(r"\s+than\b", view[m.end():m.end() + 10]) is not None
    if hit.startswith("used"):
        return skip_used_to(view, m)
    return False


def skip_history_de(view: str, m: re.Match) -> bool:
    """True for "nicht mehr als" (no more than) and for a label in capitals."""
    if m.group(0).isupper():
        return True
    if m.group(0).lower().startswith("nicht"):
        return re.match(r"\s+als\b", view[m.end():m.end() + 8]) is not None
    return False


def skip_narrator_en(view: str, m: re.Match) -> bool:
    """True for "us" as the microsecond unit.

    The unit follows a number, a comma, a bar, an opening parenthesis, or
    "in", "whole", "of", "per".
    """
    if m.group(0) != "us":
        return False
    before = view[max(0, m.start() - 12):m.start()]
    return re.search(r"(?:\d|[,|(]|\b(?:in|whole|of|per))[ \t\u00a0]*$",
                     before) is not None


def skip_exclamation(view: str, m: re.Match) -> bool:
    """True for "!" in an image, an alert marker, a comment or "!="."""
    before = view[m.start() - 1:m.start()]
    after = view[m.end():m.end() + 1]
    return before in ("<", "[") or after in ("[", "=")


# (name, language or None for both, pattern, skip function or None).
RULES = (
    ("dash", None, re.compile(r"(?<=[ \t])--(?=[ \t])"), None),
    ("marketing", None,
     re.compile(word("powerful", "seamless", "robust", "blazing",
                     "comprehensive", "simply", "just", "easy"), re.I),
     None),
    ("narrator-en", "en", re.compile(word("[Ww]e", "[Oo]ur", "[Uu]s")),
     skip_narrator_en),
    ("narrator-de", "de", re.compile(word("[Ww]ir", r"[Uu]nser\w*")), None),
    ("history-en", "en",
     re.compile(word("previously", "now", r"no\s+longer", r"used\s+to",
                     "formerly"), re.I), skip_history_en),
    ("history-de", "de",
     re.compile(word("bisher", "jetzt", r"nicht\s+mehr"), re.I),
     skip_history_de),
    ("exclamation", None, re.compile("!"), skip_exclamation),
    ("emoji", None, EMOJI, None),
    ("stale-esp32s3", None, re.compile(r"ESP32-?S3"), None),
    ("stale-simulation", None, re.compile(word("SIMULATION")), None),
    ("stale-coprocessor", None,
     re.compile(r"(?<!\w)(?:co-?processor|koprozessor)\w*", re.I), None),
    ("stale-usable", None, re.compile(r"usable\s+releases?", re.I), None),
)
RULE_NAMES = tuple(r[0] for r in RULES)


def find(path: str, text: str) -> list:
    """Finds the hits of every rule in one page.

    Args:
        path: The page's path in the tree; a German page gets the German
            rules, any other the English ones.
        text: The page's text.

    Returns:
        (line number, rule, first matched text of the line, line) per
        line and rule, in line order.
    """
    lang = "de" if is_german(path) else "en"
    view = prose_view(text)
    lines = text.split("\n")
    hits = []
    for name, rule_lang, pattern, skip in RULES:
        if rule_lang not in (None, lang):
            continue
        seen = set()
        for m in pattern.finditer(view):
            if skip is not None and skip(view, m):
                continue
            row = text.count("\n", 0, m.start())
            if row not in seen:
                seen.add(row)
                hits.append((row + 1, name, text[m.start():m.end()],
                             lines[row]))
    return sorted(hits, key=lambda h: (h[0], h[1]))


ABBREVIATIONS = re.compile(
    r"(?<![A-Za-z])(e\.g|i\.e|etc|vs|approx|ca|bzw|usw|ggf|inkl|Nr|Abb"
    r"|z\.\s?B|d\.\s?h|u\.\s?a|o\.\s?ä|Fig|cf|resp)\.")
BLOCK_START = re.compile(r"^[ \t]*(?:[-*+]|\d+[.)])[ \t]+")
SENTENCE_END = re.compile(r"(?<=[.!?])\s+(?=[A-ZÄÖÜ0-9\"'(\[\0])")


def long_sentences(text: str) -> list:
    """Finds the sentences of more than LONG_WORDS words.

    A block is a paragraph or a list item; table rows, headings and
    masked code are not sentences.  A code span counts as one word.

    Args:
        text: The page's text.

    Returns:
        (line number, word count, sentence start) per long sentence.
    """
    view = prose_view(text)
    blocks = []     # [first line number, [lines]]
    cur = None
    for num, line in enumerate(view.split("\n"), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith(("|", "#")) or \
                not stripped.strip(MASK):
            cur = None
            continue
        if BLOCK_START.match(line) or cur is None:
            cur = [num, []]
            blocks.append(cur)
        cur[1].append(BLOCK_START.sub("", line))
    hits = []
    for first, parts in blocks:
        joined = ABBREVIATIONS.sub(lambda m: m.group(0).replace(".", "\u0001"),
                                   "\n".join(parts))
        pos = 0
        for piece in SENTENCE_END.split(joined):
            start = joined.find(piece, pos)
            pos = start + len(piece)
            words = [w for w in piece.split()
                     if re.search(r"\w", w) or MASK in w]
            if len(words) > LONG_WORDS:
                row = first + joined.count("\n", 0, start)
                shown = " ".join(re.sub(MASK + "+", "`..`", piece).split())
                hits.append((row, len(words), shown[:100]))
    return hits


def parse_allow(text: str) -> list:
    """Reads the allowlist.

    Args:
        text: The allowlist file's text.

    Returns:
        [path, rule, text, reason, line number] per entry.
    """
    entries, reason = [], ""
    for num, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            reason = ""
        elif line.startswith("#"):
            reason = (reason + " " + line[1:].strip()).strip()
        else:
            path, _, rest = line.partition(": ")
            rule, sep, part = rest.partition(": ")
            if not sep or not part or not reason or rule not in RULE_NAMES:
                raise SystemExit(f"{ALLOW.name}:{num}: not `path: rule: "
                                 "text` under a reason, or an unknown rule")
            entries.append([path, rule, part, reason, num])
    return entries


def allows(part: str, line: str) -> bool:
    """True when an allowlist entry's text matches the line."""
    if part.startswith("re:"):
        return re.search(part[3:], line) is not None
    return part in line


def tracked() -> list:
    """Returns the paths git tracks, relative to the tree's root."""
    out = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, check=True,
                         capture_output=True).stdout.decode()
    return [p for p in out.split("\0") if p]


def scan(paths: list, allow: list, root: Path | None = None,
         long: bool = False) -> tuple:
    """Reads the pages and collects hits and long sentences.

    Args:
        paths: Paths relative to root.
        allow: The entries of parse_allow().
        root: The tree's root; the repository when None.
        long: True to collect long sentences too.

    Returns:
        (hits, longs, used): hits as (path, line number, rule, matched text,
        line), longs as (path, line number, word count, start), used as the
        set of allowlist line numbers that matched.
    """
    root = root or ROOT
    hits, longs, used = [], [], set()
    for path in paths:
        full = root / path
        if not full.is_file():
            continue
        try:
            text = full.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for row, rule, matched, line in find(path, text):
            ok = [e for e in allow if e[0] == path and e[1] == rule
                  and allows(e[2], line)]
            if ok:
                used.update(e[4] for e in ok)
                continue
            hits.append((path, row, rule, matched, line))
        if long:
            longs += [(path, row, n, start)
                      for row, n, start in long_sentences(text)]
    return hits, longs, used


def excerpt(line: str, matched: str, width: int = 150) -> str:
    """Cuts a line to width characters around the matched text."""
    line = line.strip()
    if len(line) <= width:
        return line
    at = max(0, line.find(matched) - width // 2)
    return ("..." if at else "") + line[at:at + width] + "..."


def split_halves(counts: dict) -> tuple:
    """Splits the files into two halves of nearly equal hit count.

    The files are taken in path order, a German page (-de) beside its twin.
    The cut falls between a page and the next group of twins when that
    leaves the halves within a tenth of all hits of each other, else
    between any two files.

    Args:
        counts: Hits per path.

    Returns:
        (half A paths, half B paths).
    """
    def key(path: str) -> str:
        return path[:-6] + ".md" if path.endswith("-de.md") else path

    ordered = sorted(counts, key=lambda p: (key(p), p))
    total = sum(counts.values())
    best = {True: (None, 0), False: (None, 0)}     # by intact twins
    run = 0
    for k, path in enumerate(ordered):
        run += counts[path]
        nxt = ordered[k + 1] if k + 1 < len(ordered) else None
        gap = abs(total - 2 * run)
        for intact in (False, True):
            if intact and nxt is not None and key(nxt) == key(path):
                continue
            if best[intact][0] is None or gap < best[intact][0]:
                best[intact] = (gap, k + 1)
    gap, cut = best[True]
    if gap is None or gap > total / 10:
        cut = best[False][1]
    return ordered[:cut], ordered[cut:]


def report(hits: list, paths: list, root: Path | None = None) -> str:
    """Formats the hits as Markdown, grouped by file, with the split.

    Args:
        hits: The hits of scan().
        paths: Every path that was read.
        root: The tree's root; the repository when None.

    Returns:
        The Markdown text.
    """
    root = root or ROOT
    by_file: dict = {}
    for hit in hits:
        by_file.setdefault(hit[0], []).append(hit)
    per_rule = Counter(h[2] for h in hits)
    counts = {p: len(v) for p, v in by_file.items()}
    size = {}
    for p in by_file:
        size[p] = len((root / p).read_text(encoding="utf-8").split("\n"))
    out = ["# Prose check hits", "",
           f"{len(hits)} hits in {len(by_file)} of {len(paths)} files.", "",
           "## Per rule", "", "| Rule | Hits |", "|---|---|"]
    out += [f"| {r} | {per_rule[r]} |" for r in RULE_NAMES if per_rule[r]]
    a, b = split_halves(counts)
    out += ["", "## Split for two fix agents", ""]
    for name, half in (("A", a), ("B", b)):
        out.append(f"Half {name}: {sum(counts[p] for p in half)} hits, "
                   f"{sum(size[p] for p in half)} lines, {len(half)} files"
                   + (f", {half[0]} to {half[-1]} in path order." if half
                      else "."))
        out.append("")
        out.append("| File | Hits | Lines |")
        out.append("|---|---|---|")
        out += [f"| {p} | {counts[p]} | {size[p]} |" for p in half]
        out.append("")
    out += ["## Per file", ""]
    for path in sorted(by_file):
        rules = Counter(h[2] for h in by_file[path])
        names = ", ".join(f"{r} {n}" for r, n in sorted(rules.items()))
        out += [f"### {path} ({counts[path]}: {names})", ""]
        out += [f"- L{h[1]} `{h[2]}` `{h[3]}`: {excerpt(h[4], h[3])}"
                for h in by_file[path]]
        out.append("")
    return "\n".join(out)


def main(argv: list) -> int:
    """Lists or checks the hits of the documents.

    Args:
        argv: The arguments: --check, --long, --report FILE, paths.

    Returns:
        1 for --check with a hit or a stale allowlist entry, else 0.
    """
    args = list(argv)
    check = "--check" in args
    long = "--long" in args
    out_file = None
    if "--report" in args:
        k = args.index("--report")
        if k + 1 >= len(args):
            raise SystemExit("--report needs a file")
        out_file = Path(args[k + 1])
        del args[k:k + 2]
    given = [a for a in args if not a.startswith("--")]
    allow = parse_allow(ALLOW.read_text(encoding="utf-8")) \
        if ALLOW.exists() else []
    paths = given or [p for p in tracked() if in_scope(p)]
    hits, longs, used = scan(paths, allow, long=long)
    for path, row, rule, _, line in hits:
        print(f"{path}:{row}: [{rule}] {line.strip()[:160]}")
    for path, row, n, start in longs:
        print(f"{path}:{row}: [long] {n} words: {start}")
    stale = [] if given else [e for e in allow if e[4] not in used]
    for path, rule, part, _, num in stale:
        print(f"{ALLOW.name}:{num}: matches nothing: {path}: {rule}: {part}")
    if out_file is not None:
        out_file.write_text(report(hits, paths), encoding="utf-8")
    rules = Counter(h[2] for h in hits)
    files = Counter(h[0] for h in hits)
    print(f"{len(hits)} hit(s) in {len(files)} of {len(paths)} file(s), "
          f"{len(stale)} stale allowlist entr(y/ies)", file=sys.stderr)
    for rule in RULE_NAMES:
        if rules[rule]:
            print(f"  rule {rule}: {rules[rule]}", file=sys.stderr)
    for path, n in sorted(files.items(), key=lambda kv: (-kv[1], kv[0])):
        print(f"  file {path}: {n}", file=sys.stderr)
    if long:
        print(f"{len(longs)} sentence(s) over {LONG_WORDS} words",
              file=sys.stderr)
    return 1 if check and (hits or stale) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
