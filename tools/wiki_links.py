#!/usr/bin/env python3
"""Rewrite docs/ links for the GitHub wiki.

A wiki page is addressed by its title with no extension, so a link written
as `[Screens](Screens.md)` does not open the page -- it downloads the file.
In the repository
the opposite is true: `docs/Screens.md` is what resolves there, and it is what
`check_docs.py` holds every link to.

Both are right, so neither source is edited to suit the other. The mirror step
translates on the way out, and this is that step, in a file rather than in a
line of YAML so it can be read and run.

Only relative links to a markdown page are touched. A URL, an absolute path and
an image reference are all left exactly alone.

    tools/wiki_links.py <dir>          rewrite in place
    tools/wiki_links.py --check <dir>  report what would change, change nothing
"""

import re
import sys
from pathlib import Path

# A markdown link whose target is a bare page, optionally with an anchor.
# `!` in front makes it an image, and the negative lookbehind leaves those be.
LINK = re.compile(r"(?<!!)\]\((?!\w+:)(?!/)([^()\s#]+)\.md(#[^()\s]*)?\)")


def rewrite(text: str) -> str:
    return LINK.sub(lambda m: "](%s%s)" % (m.group(1), m.group(2) or ""), text)


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
    for page in sorted(root.rglob("*.md")):
        before = page.read_text(encoding="utf-8")
        after = rewrite(before)
        if after == before:
            continue
        changed += 1
        n = len(LINK.findall(before))
        print(f"{page.relative_to(root)}: {n} link(s)")
        if not check:
            page.write_text(after, encoding="utf-8")

    if changed == 0:
        print("no wiki links to rewrite")
    elif check:
        print(f"\n{changed} page(s) would change")
    else:
        print(f"\nrewrote {changed} page(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
