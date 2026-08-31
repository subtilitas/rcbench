#!/usr/bin/env python3
"""Check the docs against the code for the claims a machine can verify.

Not a linter and not a spell checker.  This looks only at assertions whose
truth is decidable from the tree: does that screenshot exist, does that link
go anywhere, is that list of test binaries the list CMake builds, is that
count still the count.

It exists because those are exactly the claims that rot.  A doc audit found a
page still naming seven test binaries when CMake built ten, a components tree
missing three components, and a "six rows are not wired up" that had been
thirteen for months -- none of which any reader would check, and all of which
a script checks in under a second.

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

# Pages that are navigation rather than content, and so are not expected to be
# linked from the sidebar like the rest.
SIDEBAR_EXEMPT = {"_Sidebar.md", "Home.md", "Home-de.md"}

# The wiki is bilingual: every English page has a German one beside it, named
# with a -de suffix because a wiki page is addressed by its title and there is
# no folder to put a language in.  Checked rather than trusted, because a
# translation that quietly stops existing is worse than one that was never
# started -- the sidebar still offers it.
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

    Added because a section was deleted and the link to it kept passing: the
    file still existed, so the link check was happy, and the anchor pointed at
    nothing.  A dead fragment is invisible until somebody clicks it -- the
    reader lands at the top of the page and never learns they were meant to be
    somewhere else.
    """
    known = {}
    for page in pages() + [REPO / "README.md", REPO / "STATUS.md"]:
        known[page.resolve()] = anchors(read(page))

    for page in pages() + [REPO / "README.md", REPO / "STATUS.md"]:
        for target, fragment in ANCHOR_RE.findall(read(page)):
            if "://" in target:
                continue
            resolved = (page.parent / target).resolve() if target else page.resolve()
            if resolved not in known:
                continue          # a non-markdown target; check_links has it
            if fragment not in known[resolved]:
                where = "itself" if resolved == page.resolve() else resolved.name
                problems.append(
                    f"{page.name}: #{fragment} points into {where}, "
                    "which has no such heading")


def check_links(problems: list[str]) -> None:
    """Every relative link and image reference resolves to a real file."""
    referenced: set[pathlib.Path] = set()
    for page in pages() + [REPO / "README.md", REPO / "STATUS.md"]:
        base = page.parent
        for target in LINK_RE.findall(read(page)):
            if "://" in target or target.startswith("mailto:"):
                continue
            resolved = (base / target).resolve()
            if not resolved.exists():
                problems.append(f"{page.name}: link to {target} goes nowhere")
            elif resolved.suffix.lower() == ".png":
                referenced.add(resolved)

    for image in sorted(IMG.glob("*.png")):
        if image.resolve() not in referenced:
            problems.append(
                f"docs/img/{image.name} is committed but no page shows it")


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

    This lived on Testing-and-CI.md in the predecessor and then in the README,
    which was the running record until the record moved to STATUS.md to leave
    the README a short front page.  The claim travels with the record.
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


# Settings.md's "<N> rows reach no code" check and the unwired-schema scan
# behind it are not here.  They need the settings *screen* -- showing a row is
# what makes an unwired setting a trap rather than a placeholder -- and the
# screen is being re-cut.  Both return with it.

OPTIONS_RE = re.compile(
    r"static const char \*const (k_\w+)\s*\[\]\s*=\s*\{(.*?)\}\s*;", re.S)

# Files whose option arrays the docs quote.  Not every array in the tree --
# only the ones that name user-visible choices, because those are what prose
# enumerates and therefore what prose gets wrong.
OPTION_SOURCES = (
    REPO / "shared" / "settings" / "settings.c",
)


def option_lists() -> dict[str, list[str]]:
    lists: dict[str, list[str]] = {}
    for source in OPTION_SOURCES:
        for name, body in OPTIONS_RE.findall(read(source)):
            members = re.findall(r'"([^"]+)"', body)
            if len(members) > 1:
                lists[f"{source.name}:{name}"] = members
    return lists


def check_option_lists(problems: list[str]) -> None:
    """A doc that enumerates a menu enumerates all of it.

    Settings.md listed four of the five telemetry sources for months, dropping
    the one that is both the default and the only honest option -- which
    inverted what the paragraph was trying to say.  A line that names two
    members of a list and not the rest is nearly always that mistake rather
    than a deliberate aside.
    """
    lists = option_lists()
    for page in pages() + [REPO / "README.md", REPO / "STATUS.md"]:
        # Paragraphs, not lines: prose wraps, and a list that happens to break
        # across two lines is still one enumeration.
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


def check_shared_modules(problems: list[str]) -> None:
    """Building.md's tree lists every module under shared/.

    shared/ is the one directory three build systems read, so a module that
    exists and is undocumented is a module somebody will not know to add to
    their build.
    """
    doc = DOCS / "Building.md"
    text = read(doc)
    for module in sorted(p.name for p in (REPO / "shared").iterdir()
                         if p.is_dir()):
        if not re.search(rf"^\s+{re.escape(module)}/", text, re.M):
            problems.append(f"{doc.name}: the tree omits shared/{module}/")


def check_spdx(problems: list[str]) -> None:
    """Every source file carries an SPDX licence line.

    The header travelled by copy-paste before this, so about half the tree had
    it and half did not -- and which half was an accident of where a file was
    started from.  A machine holds the whole tree to it now, so a new file
    without one fails the build rather than the pattern eroding further.
    """
    import os
    for base in ("shared", "firmware", "test"):
        for dp, dn, fn in os.walk(REPO / base):
            # Prune build trees in place -- build, build-san, build-cov -- so
            # the walk never reaches a toolchain's own probe files.
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
    check_anchors(problems)
    check_sidebar(problems)
    check_translations(problems)
    check_suites(problems)
    check_option_lists(problems)
    check_shared_modules(problems)
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
