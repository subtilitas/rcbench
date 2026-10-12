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
  compares them: case folded, each run of spaces, tabs and line endings
  as one space; other white space, U+00A0 among it, stays.  When an image
  and a link share one definition, the image wins, because `raw` also
  opens in a browser.
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
- Text in a code span, in a fenced code block (``` or ~~~), in a URI or
  email autolink (`<https://...>`) and after a backslash-escaped `[` or
  `![` is not a link and stays as written.  Backslash escapes are read left
  to right, so `\\\\` before a backtick leaves the backtick to open a code
  span.  A fence opens at 0 to 3 spaces of indent, also after block quote
  markers (`> ```), and ends where its block quote ends.  An image may stand
  in link text; a `]` after a backslash does not close the text.  A line
  ending may follow `(`; a blank line between a target and a title ends
  the link.
- `--verify` checks every link and image the parser finds: a link is an
  absolute URL, an anchor or a page name with no `.md` and no `../`, and
  never `/raw/main/`; an image in docs/ is relative, one above docs/ is a
  `/raw/main/` URL.  Two exceptions: a name that ends in `.md` passes when
  a mirrored page has that title, and a link that shares a definition with
  an image may have the image's `/raw/main/` form.

Every page is parsed before and after the rewrite with markdown-it-py
3.0.0, a CommonMark parser, and the two token streams are compared token by
token.  They match in kind, tag, text and attributes, except a link's href
and an image's src, which must be what the rules above give the original.
A link the scanner misses and text it alters both fail the run, with page,
line and both values.  The page is then not written.

Current limitations.  The scanner reads less than CommonMark; each case
below fails the conversion with page and line, and the page needs the
form given:

- A reference definition inside a block quote or list item,
  `> [ref]: ../README.md`, is not rewritten.  A page that uses one fails;
  write the definition at the top level, 0 to 3 spaces in.
- A definition-shaped line right after a paragraph line, `text` then
  `[ref]: ../README.md`, is paragraph text in CommonMark but is rewritten.
  Put a blank line in front of a definition, or write `\\[` for text.
- A definition label over two lines, `[Foo` then `  bar]: ../README.md`, is
  not seen.  A page that uses it fails; write the label on one line.
- A raw HTML block (`<pre>`, `<div>`, `<!-- -->` at the start of a line) is
  not recognised, and a relative link-shaped text in it is rewritten.  Put
  the example in a fenced code block.  The same holds for an indented code
  block and for inline HTML.
- markdown-it-py collapses all Unicode white space in a label, CommonMark
  only spaces, tabs and line endings.  A page where an image label and a
  link label differ only in other white space (a no-break
  space, U+00A0, against a space) fails; write plain spaces.
- Any other construct the scanner reads differently from the parser fails
  the same way.

`--check` also fails when a relative link still leaves docs/ after the
rewrite, which is the case for a target that climbs above the repository.

    tools/wiki_links.py <dir>          rewrite in place, then compare each
                                       page with its original
    tools/wiki_links.py --check <dir>  report what would change and run the
                                       comparison, change nothing
    tools/wiki_links.py --verify <dir> fail on an already rewritten page
                                       whose links or images a wiki cannot
                                       open; the page is read with
                                       markdown-it-py, not by this script's
                                       scanner

All three modes need markdown-it-py (`pip install markdown-it-py==3.0.0`)
and exit 2 without it.

SPDX-License-Identifier: MIT
"""

import posixpath
import re
import string
import sys
from pathlib import Path
from urllib.parse import unquote

REPO = "https://github.com/subtilitas/rcbench"
# The repository this script belongs to, which holds docs/.  The wiki copy
# is rewritten elsewhere, so a directory is recognised here.
ROOT = Path(__file__).resolve().parents[1]

# A line ending that does not end in a blank line: a title and the space in
# front of it may cross one, a blank line ends the link.
_NB = r"\n(?![ \t]*\n)"
TITLE = (r'(?:"(?:\\[^\n]|\\(?=\n)|[^"\\\n]|' + _NB + r')*"'
         r"|'(?:\\[^\n]|\\(?=\n)|[^'\\\n]|" + _NB + r")*'"
         r"|\((?:\\[^\n]|\\(?=\n)|[^()\\\n]|" + _NB + r")*\))")
# White space holding at most one line ending, between a target and a title.
SEP = r"(?:[ \t]+(?:\n[ \t]*)?|\n[ \t]*)"

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
    r"(?P<title>" + SEP + TITLE + r")?(?P<trail>[ \t]*(?:\n[ \t]*)?)\)")

# Any inline destination, URL or not: `(dest "title")`.  An image followed by
# one is an inline image, whatever the destination, and no reference.
INLINE_ANY = re.compile(
    r"\([ \t]*(?:\n[ \t]*)?(?:<[^<>\n]*>|(?:\\.|[^()\s\\]|"
    r"\((?:\\.|[^()\s\\])*\))*)(?:" + SEP + TITLE +
    r")?[ \t]*(?:\n[ \t]*)?\)")

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
# Block quote markers `>` in front of a line, each at 0-3 spaces of indent.
QUOTE = re.compile(r"(?:[ ]{0,3}>[ ]?)*")
# A fence opens at 0-3 spaces of indent, after any number of block quote
# markers.  Four spaces make an indented code block.
FENCE = re.compile(r"((?:[ ]{0,3}>[ ]?)*)[ ]{0,3}(`{3,}|~{3,})(.*)")
# A URI autolink, `<scheme:...>`, or an email autolink, `<a@b.c>`.
AUTOLINK = re.compile(
    r"<(?:[A-Za-z][A-Za-z0-9+.-]{1,31}:[^\x00-\x20<>]*"
    r"|[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9]"
    r"(?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?"
    r"(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*)>")
# The characters a backslash escapes.
PUNCT = frozenset(string.punctuation)
BLANK = re.compile(r"\n[ \t]*\n")


def _fences(text: str) -> str:
    """`text` with fenced code blocks blanked.  A fence closes on a fence of
    the same kind, at least as long, behind the same number of block quote
    markers.  A fence inside a block quote also ends where the quote ends,
    on a line with fewer markers; a fence outside one ends at the end of
    the page."""
    out: list[str] = []
    fence = ""
    depth = 0
    for line in text.splitlines(keepends=True):
        body = line.rstrip("\r\n")
        end = line[len(body):]
        m = FENCE.fullmatch(body)
        if fence and QUOTE.match(body).group(0).count(">") < depth:
            fence = ""
        if fence:
            if (m and m.group(1).count(">") == depth
                    and m.group(2)[0] == fence[0]
                    and len(m.group(2)) >= len(fence)
                    and not m.group(3).strip()):
                fence = ""
            out.append(HOLE * len(body) + end)
        elif m and not (m.group(2)[0] == "`" and "`" in m.group(3)):
            fence, depth = m.group(2), m.group(1).count(">")
            out.append(HOLE * len(body) + end)
        else:
            out.append(line)
    return "".join(out)


def _blank(chars: list[str], start: int, end: int) -> None:
    """Mask `chars[start:end]`, line endings kept."""
    for k in range(start, end):
        if chars[k] != "\n":
            chars[k] = HOLE


def mask(text: str) -> str:
    """`text` with code, autolinks and escaped openers blanked, same length
    and same line endings, so offsets found in the result hold in `text`.

    Fenced code blocks go first.  The rest is read left to right, as a
    CommonMark parser reads inline text: a backslash escapes the
    punctuation character after it, and an escaped `[` is blanked; a URI or
    email autolink is blanked whole; a run of backticks opens a code span
    that the next run of the same length closes, within one paragraph, and
    the span is blanked.  A run with no closer is literal.  An escaped
    backtick is literal, and the rest of its run may still open a span."""
    s = _fences(text)
    out = list(s)
    n = len(s)
    i = 0
    while i < n:
        c = s[i]
        if c == "\\" and i + 1 < n and s[i + 1] in PUNCT:
            if s[i + 1] == "[":
                out[i + 1] = HOLE
            i += 2
            continue
        if c == "<":
            m = AUTOLINK.match(s, i)
            if m:
                _blank(out, i, m.end())
                i = m.end()
                continue
        if c == "`":
            j = i
            while j < n and s[j] == "`":
                j += 1
            closer = re.compile(r"(?<!`)`{%d}(?!`)" % (j - i)).search(s, j)
            if closer and not BLANK.search(s, j, closer.start()):
                _blank(out, i, closer.end())
                i = closer.end()
            else:
                i = j
            continue
        i += 1
    return "".join(out)


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
    """A reference label as CommonMark compares it: folded case, each run
    of spaces, tabs and line endings as one space, none at the ends.  Other
    white space, a no-break space (U+00A0) among it, stays as it is."""
    return re.sub(r"[ \t\r\n]+", " ", label).strip(" ").casefold()


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


def split_query(target: str) -> tuple[str, str]:
    """`target` as (path, tail): the tail starts at the first `?` or `#` and
    is kept verbatim, the path is the part that names a file."""
    m = re.match(r"([^?#]*)(.*)", target, re.S)
    return m.group(1), m.group(2)


def leaves_docs(target: str, reldir: str) -> bool:
    """True when `target`, written in docs/<reldir>, is outside docs/.  A
    query string or fragment is not part of the path."""
    path = split_query(target)[0]
    return outside(posixpath.normpath(posixpath.join(reldir, path)))


def convert(target: str, reldir: str, root: Path, image: bool) -> str | None:
    """The wiki form of one relative `target`, or None when it climbs above
    the repository and is left for the check.  Only the path is normalised;
    a query string or fragment is appended as written."""
    path, query = split_query(target)
    joined = posixpath.normpath(posixpath.join(reldir, path))
    if not outside(joined):
        return (path[:-3] if path.endswith(".md") else path) + query
    repo = posixpath.normpath(posixpath.join("docs", reldir, path))
    if outside(repo):
        return None
    kind = "blob"
    if image:
        kind = "raw"
    elif path.endswith("/") or (root / repo.replace("\\", "")).is_dir():
        kind = "tree"
    repo = "" if repo == "." else repo
    tail = "/" if kind == "tree" and repo else ""
    return f"{REPO}/{kind}/main/{repo}{tail}{query}"


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


def judge(href: str, image: bool, reldir: str = "",
          pages: set[str] | None = None) -> str | None:
    """Why `href` is wrong on a wiki page, or None.  A link is an absolute
    URL, an anchor or a page name with no `.md` that stays in docs/.  A
    name that ends in `.md` is a page title when `pages`, the mirrored page
    names without their last `.md`, holds it.  An
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
    page = posixpath.normpath(posixpath.join(reldir, path))
    if not image and path.endswith(".md") and page not in (pages or ()):
        return "a page name with .md"
    return None


def verify_text(text: str, reldir: str = "",
                pages: set[str] | None = None) -> list[tuple[int, str, str]]:
    """Every link and image of a page that a wiki cannot open, as (line,
    href, reason).  The page is read by a CommonMark parser, not by this
    file's scanner, so a construct the rewriter does not see still counts.
    A link that shares a definition with an image may have the image's
    `/raw/main/` form, as in `gate`.  `pages` is as in `judge`."""
    from markdown_it import MarkdownIt

    lines = text.splitlines()
    found: list[tuple[int, str, str]] = []
    tokens = MarkdownIt("commonmark").parse(text)

    def srcs(toks):
        for t in toks:
            if t.type == "image":
                yield t.attrGet("src")
            yield from srcs(t.children or [])

    images = set(srcs(tokens))
    raw = REPO + "/raw/"

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
            if not image and href in images and str(href).startswith(raw):
                continue
            why = judge(str(href), image, reldir, pages)
            if why:
                # The line holding the href: in the block, else a definition
                # elsewhere on the page, else the block's first line.
                needle = str(href).split("#")[0]
                at = next((n for n in [*range(first, last),
                                       *range(len(lines))]
                           if n < len(lines) and needle in lines[n]), first)
                found.append((at + 1, str(href), why))

    walk(tokens, 0, 0)
    return found


def _line(lines: list[str], first: int, last: int, needle: str) -> int:
    """The 1-based line that holds `needle`: in lines first to last, else
    anywhere on the page, else `first`."""
    needle = needle.strip().split("\n")[0][:60]
    if needle:
        for n in [*range(first, min(last, len(lines))), *range(len(lines))]:
            if needle in lines[n]:
                return n + 1
    return first + 1


def wiki_hrefs(md, href: str, image: bool, reldir: str,
               root: Path) -> set[str]:
    """The hrefs the rewrite gives a parsed link or image `href`, as the
    parser writes them: the rule of `convert` on the part before `#`,
    tried on the target as parsed and with its percent escapes decoded."""
    if not href or SCHEME.match(href) or href.startswith(("#", "/")):
        return {href}
    path, hash_, anchor = href.partition("#")
    out = set()
    for target in (path, unquote(path)):
        new = convert(target, reldir, root, image)
        out.add(href if new is None
                else md.normalizeLink(new + hash_ + anchor))
    return out


# What two token streams must share, token by token.  `content` of an
# `inline` token is its source text, links included; its children carry
# what it means and are compared instead.
FIELDS = ("type", "tag", "nesting", "level", "map", "markup", "info",
          "block", "hidden", "content")
HREF = {("link_open", "href"), ("image", "src")}


def gate(before: str, after: str, reldir: str = "",
         root: Path = ROOT) -> list[tuple[int, str, str, str]]:
    """Every difference between a page and its rewrite as a CommonMark
    parser reads them, as (line, what, before, after).  Both pages are
    parsed with markdown-it-py and their tokens are walked side by side.
    Each token matches in every field of `FIELDS` and every attribute, but
    a link's href and an image's src, which are what `convert` gives the
    original: a page name for a `.md` page, a repository URL above docs/.
    A link that shares a definition with an image may have the image's
    `raw` form.  A missed link and altered prose both show here.  At the
    first token that differs in kind the walk stops: what follows no
    longer pairs up."""
    from markdown_it import MarkdownIt

    md = MarkdownIt("commonmark")
    env_b: dict = {}
    env_a: dict = {}
    tb = md.parse(before, env_b)
    ta = md.parse(after, env_a)
    lines = before.splitlines()
    found: list[tuple[int, str, str, str]] = []

    def flat(tokens):
        for t in tokens:
            yield t
            yield from flat(t.children or [])

    images = {t.attrGet("src") for t in flat(tb) if t.type == "image"}

    def walk(xs, ys, span) -> bool:
        for x, y in zip(xs, ys, strict=False):
            if x.map:
                span = x.map
            for f in FIELDS:
                if f == "content" and x.type == "inline":
                    continue
                vx, vy = getattr(x, f), getattr(y, f)
                if vx != vy:
                    found.append((_line(lines, *span, str(vx)),
                                  f"{x.type} {f}", str(vx), str(vy)))
                    if f in ("type", "tag", "nesting"):
                        return False
            for key in sorted({*x.attrs, *y.attrs}):
                vx, vy = x.attrGet(key), y.attrGet(key)
                if (x.type, key) in HREF:
                    image = x.type == "image"
                    ok = wiki_hrefs(md, str(vx), image, reldir, root)
                    if not image and vx in images:
                        ok |= wiki_hrefs(md, str(vx), True, reldir, root)
                    if vy not in ok:
                        want = " or ".join(sorted(ok))
                        found.append((
                            _line(lines, *span, unquote(str(vx))),
                            f"{x.type} {key}, wanted {want}",
                            str(vx), str(vy)))
                elif vx != vy:
                    found.append((_line(lines, *span, str(vx)),
                                  f"{x.type} {key}", str(vx), str(vy)))
            if not walk(x.children or [], y.children or [], span):
                return False
        if len(xs) != len(ys):
            found.append((span[0] + 1, "token count", str(len(xs)),
                          str(len(ys))))
            return False
        return True

    walk(tb, ta, [0, 1])
    refs_b = env_b.get("references", {})
    refs_a = env_a.get("references", {})
    if refs_b.keys() != refs_a.keys():
        found.append((1, "definition labels", " ".join(sorted(refs_b)),
                      " ".join(sorted(refs_a))))
    return found


def _need_parser() -> bool:
    """True when markdown-it-py can be imported; else say how to get it."""
    try:
        import markdown_it  # noqa: F401
    except ImportError:
        print("wiki_links.py needs markdown-it-py: "
              "pip install markdown-it-py", file=sys.stderr)
        return False
    return True


def verify_tree(root: Path) -> int:
    """Report every link of a rewritten tree that a wiki cannot open."""
    if not _need_parser():
        return 2
    bad = 0
    pages = {p.relative_to(root).as_posix()[:-3]
             for p in root.rglob("*.md")}
    for page in sorted(root.rglob("*.md")):
        reldir = page.parent.relative_to(root).as_posix()
        reldir = "" if reldir == "." else reldir
        text = page.read_text(encoding="utf-8")
        for line, href, why in verify_text(text, reldir, pages):
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
    if not _need_parser():
        return 2

    changed = 0
    bad = 0
    for page in sorted(root.rglob("*.md")):
        name = page.relative_to(root)
        reldir = page.parent.relative_to(root).as_posix()
        reldir = "" if reldir == "." else reldir
        before = page.read_text(encoding="utf-8")
        after = rewrite(before, reldir)
        for target in stranded(after, reldir):
            print(f"{name}: {target} leaves docs/", file=sys.stderr)
            bad += 1
        diffs = gate(before, after, reldir)
        for line, what, was, now in diffs:
            print(f"{name}:{line}: {what}: before {was!r}, after {now!r}",
                  file=sys.stderr)
        bad += len(diffs)
        if after == before:
            continue
        changed += 1
        n = sum(1 for _ in targets(before))
        print(f"{name}: {n} link(s)")
        if not check and not diffs:
            page.write_text(after, encoding="utf-8")

    if changed == 0:
        print("no wiki links to rewrite")
    elif check:
        print(f"\n{changed} page(s) would change")
    else:
        print(f"\nrewrote {changed} page(s)")
    if bad:
        print(f"{bad} link(s) leave docs/ or differ from the original page "
              "as a CommonMark parser reads it", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
