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
  `https://github.com/subtilitas/rcbench/blob/main/<path>`, a target that
  ends in `/` becomes `.../tree/main/<path>`, and an image becomes
  `.../raw/main/<path>`.  An anchor stays.  A link title stays.
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

# `[text](target "title")`, with an optional `!` for an image.  The text may
# span lines and hold one level of brackets.
LINK = re.compile(
    r"(!?)\[((?:[^\[\]]|\[[^\[\]]*\])*)\]"
    r"\((?!\w+:)(?!/)(?!#)([^()\s#]+)(#[^()\s]*)?(\s+\"[^\"]*\")?\)")


def leaves_docs(target: str, reldir: str) -> bool:
    """True when `target`, written in docs/<reldir>, is outside docs/."""
    joined = posixpath.normpath(posixpath.join(reldir, target))
    return joined.startswith("..")


def rewrite(text: str, reldir: str = "") -> str:
    """Translate the links of one page.  `reldir` is the page's directory
    relative to docs/, in POSIX form, empty for a page at the top."""

    def one(m: re.Match) -> str:
        bang, label, target, anchor, title = m.groups()
        anchor, title = anchor or "", title or ""
        joined = posixpath.normpath(posixpath.join(reldir, target))
        if joined.startswith(".."):
            repo = posixpath.normpath(posixpath.join("docs", reldir, target))
            if repo.startswith(".."):
                return m.group(0)  # above the repository: left for the check
            kind = "blob"
            if bang:
                kind = "raw"
            elif target.endswith("/"):
                kind = "tree"
            tail = "/" if kind == "tree" else ""
            target = f"{REPO}/{kind}/main/{repo}{tail}"
        elif target.endswith(".md"):
            target = target[:-3]
        return f"{bang}[{label}]({target}{anchor}{title})"

    return LINK.sub(one, text)


def stranded(text: str, reldir: str = "") -> list[str]:
    """Relative targets that still leave docs/ in already rewritten text."""
    return [m.group(3) for m in LINK.finditer(text)
            if leaves_docs(m.group(3), reldir)]


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
        n = sum(1 for _ in LINK.finditer(before))
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
