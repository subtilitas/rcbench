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
screenshot count in STATUS.md from docs/img.

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
    """STATUS.md's count of committed screenshots is the number in docs/img."""
    text = read(REPO / "STATUS.md")
    m = re.search(r"(\w+) committed screenshots", text)
    if not m:
        problems.append("STATUS.md: no '<N> committed screenshots' sentence")
        return
    said = as_number(m.group(1))
    real = len(list(IMG.glob("*.png")))
    if said != real:
        problems.append(f"STATUS.md: says {m.group(1)} committed screenshots; "
                        f"docs/img holds {real}")


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
    check_anchors(problems)
    check_sidebar(problems)
    check_translations(problems)
    check_suites(problems)
    check_option_lists(problems)
    check_shared_modules(problems)
    check_compile_table(problems)
    check_screenshot_count(problems)
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
