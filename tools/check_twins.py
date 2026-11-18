#!/usr/bin/env python3
"""Compare each English page with its German twin by structure.

A page and its translation hold the same facts in the same order.  This
tool reads the two files of a pair and compares what a machine can decide
without reading either language: the sequence of heading levels, and under
each heading the number of rows of every table.  A section or a table row
that one language has and the other lacks fails.

Pairs: `docs/X.md` with `docs/X-de.md`, `README.md` with `README-de.md`,
and `help/content/en/X.md` with `help/content/de/X.md`.  A page without a
twin is reported by tools/check_docs.py and tools/help_site.py, not here.

Text inside a fenced code block is not structure: a `#` or a `|` there is
skipped.  A table is a run of consecutive lines that start with `|`; its
delimiter row (`| --- |`) is not counted.

    python3 tools/check_twins.py --check

Prints one line per difference and exits 1 if there were any.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from dataclasses import dataclass, field

REPO = pathlib.Path(__file__).resolve().parent.parent
DE_SUFFIX = "-de.md"

HEADING_RE = re.compile(r"^(#{1,6})\s+(.*?)\s*$")
FENCE_RE = re.compile(r"^\s*(```|~~~)")
DELIMITER_RE = re.compile(r"^\|(\s*:?-+:?\s*\|)+\s*$")


@dataclass
class Section:
    """One heading of a page and the tables under it."""

    level: int      # 1 to 6; 0 for the text before the first heading
    title: str
    line: int       # the heading's line, 1-based; 0 before the first
    tables: list[int] = field(default_factory=list)   # rows of each table


def sections(text: str) -> list[Section]:
    """Splits a page into its headings and counts each table's rows.

    Args:
        text: The page's Markdown.

    Returns:
        One Section per heading in order, led by one of level 0 that holds
        the tables before the first heading.
    """
    out = [Section(0, "", 0)]
    fence = None
    rows = 0
    for num, line in enumerate(text.splitlines(), start=1):
        mark = FENCE_RE.match(line)
        if mark:
            if fence is None:
                fence = mark.group(1)
            elif mark.group(1) == fence:
                fence = None
        is_row = fence is None and not mark and line.startswith("|")
        if is_row:
            if not DELIMITER_RE.match(line):
                rows += 1
            continue
        if rows:
            out[-1].tables.append(rows)
            rows = 0
        if fence is not None or mark:
            continue
        heading = HEADING_RE.match(line)
        if heading:
            out.append(Section(len(heading.group(1)), heading.group(2), num))
    if rows:
        out[-1].tables.append(rows)
    return out


def compare(english: str, german: str, en_name: str,
            de_name: str) -> list[str]:
    """Lists where a German page's structure leaves its English twin's.

    Args:
        english: The English page's Markdown.
        german: The German page's Markdown.
        en_name: The English page's name, for the messages.
        de_name: The German page's name, for the messages.

    Returns:
        One message per difference: the first heading whose level differs
        (the comparison of that pair ends there, since every later section
        would be off by one), or each section whose tables differ.
    """
    en = sections(english)
    de = sections(german)
    for k in range(1, max(len(en), len(de))):
        a = en[k] if k < len(en) else None
        b = de[k] if k < len(de) else None
        if a is None or b is None or a.level != b.level:
            def say(name, s):
                if s is None:
                    return f"{name} ends"
                return (f"{name}:{s.line} has level {s.level} "
                        f"\"{s.title}\"")
            return [f"{de_name}: heading {k} differs: "
                    f"{say(en_name, a)}, {say(de_name, b)}"]
    problems = []
    for a, b in zip(en, de, strict=True):
        if a.tables != b.tables:
            where = f"under \"{a.title}\"" if a.line else "before a heading"
            problems.append(
                f"{de_name}:{b.line}: tables {where} ({en_name}:{a.line}) "
                f"differ: rows {a.tables} in English, {b.tables} in German")
    return problems


def pairs(
        repo: pathlib.Path | None = None,
) -> list[tuple[pathlib.Path, pathlib.Path]]:
    """The English and German file of every page that has both.

    Args:
        repo: The repository's root; the tool's own by default.

    Returns:
        (English, German) paths, sorted: the wiki pages under docs/, the
        README, and the web help's pages under help/content.
    """
    repo = REPO if repo is None else repo
    out = []
    for page in sorted((repo / "docs").glob("*.md")):
        if page.name.endswith(DE_SUFFIX):
            continue
        german = page.with_name(page.stem + DE_SUFFIX)
        if german.exists():
            out.append((page, german))
    if (repo / "README-de.md").exists() and (repo / "README.md").exists():
        out.append((repo / "README.md", repo / "README-de.md"))
    help_en = repo / "help" / "content" / "en"
    for page in sorted(help_en.glob("*.md")):
        german = repo / "help" / "content" / "de" / page.name
        if german.exists():
            out.append((page, german))
    return out


def check(repo: pathlib.Path | None = None) -> list[str]:
    """Compares every pair under @p repo.

    Args:
        repo: The repository's root; the tool's own by default.

    Returns:
        The differences of all pairs, one message each.
    """
    repo = REPO if repo is None else repo
    problems = []
    for english, german in pairs(repo):
        problems += compare(
            english.read_text(encoding="utf-8"),
            german.read_text(encoding="utf-8"),
            english.relative_to(repo).as_posix(),
            german.relative_to(repo).as_posix())
    return problems


def main(argv=None) -> int:
    """Runs the comparison.

    Args:
        argv: The command line, without the program name.

    Returns:
        0 when every pair agrees, 1 otherwise.
    """
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--check", action="store_true",
                        help="compare every pair; exit 1 on a difference")
    parser.parse_args(argv)
    problems = check()
    for line in problems:
        print(line)
    if problems:
        print(f"{len(problems)} difference(s) in {len(pairs())} pairs",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
