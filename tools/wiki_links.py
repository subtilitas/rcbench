#!/usr/bin/env python3
"""Rewrite docs/ links for the GitHub wiki.

A wiki page is addressed by its title with no extension, so a link written
as `[Screens](Screens.md)` downloads the file instead of opening the page.
In the repository `docs/Screens.md` is what resolves, and `check_docs.py`
holds every link to it.

Neither source is edited to suit the other.  The mirror step translates on
the way out, and this is that step, in a file rather than in a workflow step
so it can be read and run.

Only relative links are touched.  A URL (uniform resource locator), an
absolute path and a bare anchor are left as they are.

- A link to a markdown page inside docs/ loses its `.md`; an anchor stays.
- A link whose target leaves docs/ points at the repository on GitHub,
  because the wiki holds nothing outside docs/.  A file becomes
  `https://github.com/subtilitas/rcbench/blob/main/<path>`, a directory
  becomes `.../tree/main/<path>/`, and an image becomes
  `.../raw/main/<path>`.  A target is a directory when it ends in `/` or
  names a directory of the repository this script lives in.  An anchor
  stays.  A link title stays.
- Spaces and tabs between the parentheses and the target stay.
- A reference definition, `[label]: ../README.md "title"`, is rewritten the
  same way as an inline link.  The title of a definition is read on the same
  line.
- A link title is `"t"`, `'t'` or `(t)`.
- A bare target may hold backslash escapes and one level of balanced
  parentheses, `[api](../API_(v2).md)`; they stay as written.
- A target is outside docs/ only when its normalised path is `..` or starts
  with `../`.  `..notes.md` is a page named `..notes`.  The same rule decides
  whether a target climbs above the repository.
- Any other link inside docs/ (an image, a file) is unchanged.

`--check` also fails when a relative link still leaves docs/ after the
rewrite, which is the case for a target that climbs above the repository.

    tools/wiki_links.py <dir>          rewrite in place
    tools/wiki_links.py --check <dir>  report what would change, change nothing
"""

import posixpath
import re
import sys
from pathlib import Path

REPO = "https://github.com/subtilitas/rcbench"
# The repository this script belongs to, which holds docs/.  The wiki copy
# is rewritten elsewhere, so a directory is recognised here.
ROOT = Path(__file__).resolve().parents[1]

TITLE = r"""(?:"[^"]*"|'[^']*'|\([^()\n]*\))"""

# `[text](target "title")`, with an optional `!` for an image.  The text may
# span lines and hold one level of brackets.  The target may stand in angle
# brackets, `[text](<a b.md>)`, and may then hold spaces.  A bare target
# holds no spaces, but may hold backslash escapes and one level of balanced
# parentheses, `[api](API_(v2).md)`.  Spaces and tabs may stand inside the
# parentheses, `[text]( a.md )`.  The title is `"t"`, `'t'` or `(t)`.
LINK = re.compile(
    r"(?P<bang>!?)\[(?P<label>(?:[^\[\]]|\[[^\[\]]*\])*)\]"
    r"\((?P<lead>[ \t]*)(?P<lt><)?(?!\w+:)(?!/)(?!#)"
    r"(?P<target>(?(lt)[^<>#\n]+|(?:\\.|[^()\s#\\]|\((?:\\.|[^()\s\\])*\))+))"
    r"(?P<anchor>#(?(lt)[^<>\n]*|(?:\\.|[^()\s\\])*))?(?(lt)>)"
    r"(?P<title>\s+" + TITLE + r")?(?P<trail>[ \t]*)\)")

# `[label]: target "title"` on a line of its own, at most 3 spaces in.  The
# target is bare or in angle brackets; the title follows on the same line.
DEFINITION = re.compile(
    r"^(?P<head> {0,3}\[[^\]\n]+\]:[ \t]*)(?P<lt><)?(?!\w+:)(?!/)(?!#)"
    r"(?P<target>(?(lt)[^<>#\n]+|[^\s#<>]+))"
    r"(?P<anchor>#(?(lt)[^<>\n]*|\S*))?(?(lt)>)"
    r"(?P<title>[ \t]+" + TITLE + r")?(?P<trail>[ \t]*)$", re.M)


def outside(path: str) -> bool:
    """True when the normalised `path` is `..` or climbs with `../`.  A name
    that merely starts with two dots, `..notes.md`, is inside."""
    return path == ".." or path.startswith("../")


def leaves_docs(target: str, reldir: str) -> bool:
    """True when `target`, written in docs/<reldir>, is outside docs/."""
    return outside(posixpath.normpath(posixpath.join(reldir, target)))


def convert(target: str, reldir: str, root: Path, image: bool) -> str | None:
    """The wiki form of one relative `target`, or None when it climbs above
    the repository and is left for the check."""
    joined = posixpath.normpath(posixpath.join(reldir, target))
    if not outside(joined):
        return target[:-3] if target.endswith(".md") else target
    repo = posixpath.normpath(posixpath.join("docs", reldir, target))
    if outside(repo):
        return None
    kind = "blob"
    if image:
        kind = "raw"
    elif target.endswith("/") or (root / repo.replace("\\", "")).is_dir():
        kind = "tree"
    path = "" if repo == "." else repo
    tail = "/" if kind == "tree" and path else ""
    return f"{REPO}/{kind}/main/{path}{tail}"


def rewrite(text: str, reldir: str = "", root: Path = ROOT) -> str:
    """Translate the links of one page.  `reldir` is the page's directory
    relative to docs/, in POSIX form, empty for a page at the top.  `root`
    is the repository the targets are looked up in."""

    def inline(m: re.Match) -> str:
        bang, label, lead, lt, target, anchor, title, trail = m.group(
            "bang", "label", "lead", "lt", "target", "anchor", "title",
            "trail")
        new = convert(target, reldir, root, bool(bang))
        if new is None:
            return m.group(0)
        dest = f"{new}{anchor or ''}"
        dest = f"<{dest}>" if lt else dest
        return f"{bang}[{label}]({lead}{dest}{title or ''}{trail})"

    def definition(m: re.Match) -> str:
        head, lt, target, anchor, title, trail = m.group(
            "head", "lt", "target", "anchor", "title", "trail")
        new = convert(target, reldir, root, False)
        if new is None:
            return m.group(0)
        dest = f"{new}{anchor or ''}"
        dest = f"<{dest}>" if lt else dest
        return f"{head}{dest}{title or ''}{trail}"

    return DEFINITION.sub(definition, LINK.sub(inline, text))


def targets(text: str):
    """Every relative target of an inline link or a definition line."""
    for pattern in (LINK, DEFINITION):
        for m in pattern.finditer(text):
            yield m.group("target")


def stranded(text: str, reldir: str = "") -> list[str]:
    """Relative targets that still leave docs/ in already rewritten text."""
    return [t for t in targets(text) if leaves_docs(t, reldir)]


def main(argv: list[str]) -> int:
    check = "--check" in argv
    args = [a for a in argv[1:] if not a.startswith("-")]
    if len(args) != 1:
        print(__doc__, file=sys.stderr)
        return 2

    root = Path(args[0])
    if not root.is_dir():
        print(f"{root} is not a directory", file=sys.stderr)
        return 2

    changed = 0
    bad = 0
    for page in sorted(root.rglob("*.md")):
        reldir = page.parent.relative_to(root).as_posix()
        reldir = "" if reldir == "." else reldir
        before = page.read_text(encoding="utf-8")
        after = rewrite(before, reldir)
        for target in stranded(after, reldir):
            print(f"{page.relative_to(root)}: {target} leaves docs/",
                  file=sys.stderr)
            bad += 1
        if after == before:
            continue
        changed += 1
        n = sum(1 for _ in targets(before))
        print(f"{page.relative_to(root)}: {n} link(s)")
        if not check:
            page.write_text(after, encoding="utf-8")

    if changed == 0:
        print("no wiki links to rewrite")
    elif check:
        print(f"\n{changed} page(s) would change")
    else:
        print(f"\nrewrote {changed} page(s)")
    if bad:
        print(f"{bad} link(s) leave docs/ and cannot be rewritten",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
