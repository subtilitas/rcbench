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
  same way as an inline link.  Its target and its title may stand on the
  next line, one line ending each: `[label]:` then `  ../README.md`.
- A definition that an image uses, `![alt][label]`, `![label][]` or
  `![label]`, gets the `raw` form.  Labels are compared as CommonMark
  compares them: case folded, white space collapsed.  When an image and a
  link share one definition, the image wins, because `raw` also opens in a
  browser.
- A link title is `"t"`, `'t'` or `(t)`.
- A label may nest square brackets to any depth.
- A URL is anything that starts with a scheme, `[A-Za-z][A-Za-z0-9+.-]*:`,
  such as `https:`, `mailto:` or `git+https:`.
- A bare target may hold backslash escapes and one level of balanced
  parentheses, `[api](../API_(v2).md)`; they stay as written.
- A target is outside docs/ only when its normalised path is `..` or starts
  with `../`.  `..notes.md` is a page named `..notes`.  The same rule decides
  whether a target climbs above the repository.
- Any other link inside docs/ (an image, a file) is unchanged.
- Text in a code span, in a fenced code block (``` or ~~~) and after a
  backslash-escaped `[` or `![` is not a link and stays as written.  A
  fence opens at 0 to 3 spaces of indent, also after block quote markers
  (`> ```); an indented code block is not recognised, so links in it are
  rewritten.  An image may stand in link text; a `]` after a backslash
  does not close the text.  A line ending may follow `(`.
- `--verify` checks every link and image the parser finds: a link is an
  absolute URL, an anchor or a page name with no `.md` and no `../`, and
  never `/raw/main/`; an image in docs/ is relative, one above docs/ is a
  `/raw/main/` URL.

`--check` also fails when a relative link still leaves docs/ after the
rewrite, which is the case for a target that climbs above the repository.

    tools/wiki_links.py <dir>          rewrite in place
    tools/wiki_links.py --check <dir>  report what would change, change nothing
    tools/wiki_links.py --verify <dir> fail on a rewritten page whose links
                                       or images a wiki cannot open; the
                                       page is read with markdown-it-py, a
                                       CommonMark parser, not by this
                                       script's scanner

SPDX-License-Identifier: MIT
"""

import posixpath
import re
import sys
from pathlib import Path
from urllib.parse import unquote

REPO = "https://github.com/subtilitas/rcbench"
# The repository this script belongs to, which holds docs/.  The wiki copy
# is rewritten elsewhere, so a directory is recognised here.
ROOT = Path(__file__).resolve().parents[1]

TITLE = r"""(?:"[^"]*"|'[^']*'|\([^()\n]*\))"""

# A URI (uniform resource identifier) scheme, as the standard writes it.
NOT_URL = r"(?![A-Za-z][A-Za-z0-9+.-]*:)(?!/)(?!#)"

# The part of `[text](target "title")` from the opening parenthesis on.  The
# target may stand in angle brackets, `[text](<a b.md>)`, and may then hold
# spaces.  A bare target holds no spaces, but may hold backslash escapes and
# one level of balanced parentheses, `[api](API_(v2).md)`.  Spaces and tabs
# may stand inside the parentheses, `[text]( a.md )`.  The title is `"t"`,
# `'t'` or `(t)`.  The label in front is found by `find_links`, because a
# label may nest brackets to any depth.
TAIL = re.compile(
    r"\((?P<lead>[ \t]*(?:\n[ \t]*)?)(?P<lt><)?" + NOT_URL +
    r"(?P<target>(?(lt)[^<>#\n]+|(?:\\.|[^()\s#\\]|\((?:\\.|[^()\s\\])*\))+))"
    r"(?P<anchor>#(?(lt)[^<>\n]*|(?:\\.|[^()\s\\])*))?(?(lt)>)"
    r"(?P<title>\s+" + TITLE + r")?(?P<trail>[ \t]*(?:\n[ \t]*)?)\)")

# Any inline destination, URL or not: `(dest "title")`.  An image followed by
# one is an inline image, whatever the destination, and no reference.
INLINE_ANY = re.compile(
    r"\([ \t]*(?:\n[ \t]*)?(?:<[^<>\n]*>|(?:\\.|[^()\s\\]|"
    r"\((?:\\.|[^()\s\\])*\))*)(?:\s+" + TITLE + r")?\s*\)")

# `[label]: target "title"`, at most 3 spaces in.  The target is bare or in
# angle brackets.  It follows on the same line or, after one line ending, on
# the next.  The title follows the target on its line or on the next line.
DEFINITION = re.compile(
    r"^(?P<head> {0,3}\[(?P<label>[^\]\n]+)\]:(?:[ \t]*\n[ \t]*|[ \t]*))"
    r"(?P<lt><)?" + NOT_URL +
    r"(?P<target>(?(lt)[^<>#\n]+|[^\s#<>]+))"
    r"(?P<anchor>#(?(lt)[^<>\n]*|\S*))?(?(lt)>)"
    r"(?P<title>(?:[ \t]+|[ \t]*\n[ \t]*)" + TITLE + r")?(?P<trail>[ \t]*)$",
    re.M)

HOLE = "\ue000"  # stands in for a masked character; matches no pattern above
# A fence opens at 0-3 spaces of indent, after any number of block quote
# markers `>`.  Four spaces make an indented code block.
FENCE = re.compile(r"(?:[ ]{0,3}>[ ]?)*[ ]{0,3}(`{3,}|~{3,})(.*)")
SPAN = re.compile(
    r"(?<![`\\])(`+)(?!`)(?:(?!\n[ \t]*\n)[\s\S])*?(?<!`)\1(?!`)")
ESCAPED_OPENER = re.compile(r"(?<!\\)(?:\\\\)*\\(\[)")


def _hole(m: re.Match, group: int = 0) -> str:
    """The text of `m` with every character but line endings masked."""
    return re.sub(r"[^\n]", HOLE, m.group(group))


def mask(text: str) -> str:
    """`text` with code and escaped openers blanked, same length and same
    line endings, so offsets found in the result hold in `text`.  A fenced
    code block (``` or ~~~, closed by a fence of the same kind and at least
    the same length, or by the end of the page) and a code span are
    blanked, and so is a `[` after an odd number of backslashes.  An
    indented code block is not recognised: its indent also marks list
    continuations."""
    out: list[str] = []
    fence = ""
    for line in text.splitlines(keepends=True):
        body = line.rstrip("\r\n")
        end = line[len(body):]
        m = FENCE.fullmatch(body)
        if fence:
            if m and m.group(1)[0] == fence[0] and len(m.group(1)) >= len(
                    fence) and not m.group(2).strip():
                fence = ""
            out.append(HOLE * len(body) + end)
        elif m and not (m.group(1)[0] == "`" and "`" in m.group(2)):
            fence = m.group(1)
            out.append(HOLE * len(body) + end)
        else:
            out.append(line)
    masked = SPAN.sub(_hole, "".join(out))
    return ESCAPED_OPENER.sub(
        lambda m: m.group(0)[:-1] + HOLE, masked)


def _close(masked: str, i: int) -> int | None:
    """Index of the `]` that closes the `[` at `i`, to any depth of
    nesting, or None.  A blank line ends the search."""
    depth = 0
    n = len(masked)
    while i < n:
        c = masked[i]
        if c == "\\" and i + 1 < n and masked[i + 1] != "\n":
            i += 2
            continue
        if c == "[":
            depth += 1
        elif c == "]":
            depth -= 1
            if depth == 0:
                return i
        elif c == "\n":
            k = i + 1
            while k < n and masked[k] in " \t":
                k += 1
            if k < n and masked[k] == "\n":
                return None
        i += 1
    return None


def norm(label: str) -> str:
    """A reference label as CommonMark compares it: folded case, runs of
    white space as one space, no white space at the ends."""
    return " ".join(label.split()).casefold()


class Link:
    """One inline link or image: its span in the page, whether it is an
    image, its label and the match of the part from `(` on."""

    def __init__(self, start, bang, label, m):
        self.start, self.bang, self.label, self.m = start, bang, label, m


def find_links(text: str, masked: str) -> tuple[list[Link], set[str]]:
    """The inline links of a page in order, and the normalised labels that
    an image uses by reference: `![alt][label]`, `![label][]` or
    `![label]`."""
    links: list[Link] = []
    images: set[str] = set()
    i = 0
    while (i := masked.find("[", i)) != -1:
        j = _close(masked, i)
        if j is None:
            i += 1
            continue
        k = i - 2
        while k >= 0 and masked[k] == "\\":
            k -= 1
        bang = i > 0 and masked[i - 1] == "!" and (i - 2 - k) % 2 == 0
        label = text[i + 1:j]
        m = TAIL.match(masked, j + 1)
        if m:
            links.append(Link(i - 1 if bang else i, bang, label, m))
            i += 1  # an image may stand in the label
            continue
        if bang and not INLINE_ANY.match(masked, j + 1):
            ref = label
            if masked[j + 1:j + 2] == "[":
                e = masked.find("]", j + 2)
                if e != -1 and masked.find("[", j + 2, e) == -1:
                    ref = text[j + 2:e] or label
            images.add(norm(ref))
        i += 1
    return links, images


def _group(text: str, m: re.Match, name: str) -> str | None:
    """Group `name` of a match on the masked page, read from the page."""
    if m.start(name) < 0:
        return None
    return text[m.start(name):m.end(name)]


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
    masked = mask(text)
    links, images = find_links(text, masked)
    edits: list[tuple[int, int, str]] = []

    for link in links:
        m = link.m
        lt = m.group("lt")
        target = _group(text, m, "target")
        new = convert(target, reldir, root, link.bang)
        if new is None:
            continue
        dest = f"{new}{_group(text, m, 'anchor') or ''}"
        dest = f"<{dest}>" if lt else dest
        lead, title, trail = (_group(text, m, g) or ""
                              for g in ("lead", "title", "trail"))
        edits.append((m.start(), m.end(),
                      f"({lead}{dest}{title}{trail})"))

    for m in DEFINITION.finditer(masked):
        target = _group(text, m, "target")
        image = norm(_group(text, m, "label")) in images
        new = convert(target, reldir, root, image)
        if new is None:
            continue
        dest = f"{new}{_group(text, m, 'anchor') or ''}"
        if m.group("lt"):
            dest = f"<{dest}>"
        head, title, trail = (_group(text, m, g) or ""
                              for g in ("head", "title", "trail"))
        edits.append((m.start(), m.end(), f"{head}{dest}{title}{trail}"))

    for start, end, new in sorted(edits, reverse=True):
        text = text[:start] + new + text[end:]
    return text


def targets(text: str):
    """Every relative target of an inline link or a definition line, code
    left out."""
    masked = mask(text)
    for link in find_links(text, masked)[0]:
        yield _group(text, link.m, "target")
    for m in DEFINITION.finditer(masked):
        yield _group(text, m, "target")


def stranded(text: str, reldir: str = "") -> list[str]:
    """Relative targets that still leave docs/ in already rewritten text."""
    return [t for t in targets(text) if leaves_docs(t, reldir)]


SCHEME = re.compile(r"[A-Za-z][A-Za-z0-9+.-]*:")


def judge(href: str, image: bool, reldir: str = "") -> str | None:
    """Why `href` is wrong on a wiki page, or None.  A link is an absolute
    URL, an anchor or a page name with no `.md` that stays in docs/.  An
    image inside docs/ stays relative; one above docs/ is a `/raw/main/`
    URL, and no other repository URL serves an image."""
    if SCHEME.match(href):
        if href.startswith(REPO + "/"):
            kind = href[len(REPO) + 1:].split("/", 1)[0]
            if image and kind != "raw":
                return f"an image served from /{kind}/"
            if not image and kind == "raw":
                return "a link to /raw/main/"
        return None
    if href.startswith(("#", "/")):
        return None
    path = unquote(re.split(r"[#?]", href, maxsplit=1)[0])
    if leaves_docs(path, reldir):
        return "a relative target above docs/"
    if not image and path.endswith(".md"):
        return "a page name with .md"
    return None


def verify_text(text: str, reldir: str = "") -> list[tuple[int, str, str]]:
    """Every link and image of a page that a wiki cannot open, as (line,
    href, reason).  The page is read by a CommonMark parser, not by this
    file's scanner, so a construct the rewriter does not see still counts."""
    from markdown_it import MarkdownIt

    lines = text.splitlines()
    found: list[tuple[int, str, str]] = []

    def walk(tokens, first: int, last: int) -> None:
        for tok in tokens:
            if tok.map:
                first, last = tok.map
            if tok.children:
                walk(tok.children, first, last)
            if tok.type == "link_open":
                href, image = tok.attrGet("href"), False
            elif tok.type == "image":
                href, image = tok.attrGet("src"), True
            else:
                continue
            why = judge(str(href), image, reldir)
            if why:
                # The line holding the href: in the block, else a definition
                # elsewhere on the page, else the block's first line.
                needle = str(href).split("#")[0]
                at = next((n for n in [*range(first, last),
                                       *range(len(lines))]
                           if n < len(lines) and needle in lines[n]), first)
                found.append((at + 1, str(href), why))

    walk(MarkdownIt("commonmark").parse(text), 0, 0)
    return found


def verify_tree(root: Path) -> int:
    """Report every link of a rewritten tree that a wiki cannot open."""
    try:
        import markdown_it  # noqa: F401
    except ImportError:
        print("--verify needs markdown-it-py: pip install markdown-it-py",
              file=sys.stderr)
        return 2
    bad = 0
    for page in sorted(root.rglob("*.md")):
        reldir = page.parent.relative_to(root).as_posix()
        reldir = "" if reldir == "." else reldir
        text = page.read_text(encoding="utf-8")
        for line, href, why in verify_text(text, reldir):
            print(f"{page.relative_to(root)}:{line}: {href} is {why}",
                  file=sys.stderr)
            bad += 1
    if bad:
        print(f"{bad} link(s) a wiki cannot open", file=sys.stderr)
        return 1
    print("every link opens on the wiki")
    return 0


def main(argv: list[str]) -> int:
    check = "--check" in argv
    verify = "--verify" in argv
    args = [a for a in argv[1:] if not a.startswith("-")]
    if len(args) != 1:
        print(__doc__, file=sys.stderr)
        return 2

    root = Path(args[0])
    if not root.is_dir():
        print(f"{root} is not a directory", file=sys.stderr)
        return 2

    if verify:
        return verify_tree(root)

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
