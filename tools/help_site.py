#!/usr/bin/env python3
"""Build the panel's web help from help/content/ into static HTML.

One Markdown file per screen and language, help/content/<lang>/<id>.md,
becomes one HTML (HyperText Markup Language) page; the screenshots are the
views tools/render_ui.py renders into docs/img/.  The format is a fixed
Markdown subset, described in help/site/FORMAT.md; this script carries its
own renderer for exactly that subset, so it needs nothing outside the Python
standard library.  Anything outside the subset fails with file:line.

    python3 tools/help_site.py                     # build into build/help/
    python3 tools/help_site.py --out DIR           # build into DIR
    python3 tools/help_site.py --check             # check, write nothing
    python3 tools/help_site.py --page FILE --out DIR   # one page, a preview

A build runs every check first and writes nothing when one fails, so a
published help never links to a page or an image that is not there.  The
checks: every SCREEN_* of shared/ui/include/ui_screen.h but the splash has a
page in both languages, every page has its counterpart, every view exists in
render_ui.py and has a PNG (Portable Network Graphics) file in the page's
language, the English and German page of one id have the same heading levels
and the same views in the same order, and every link between pages lands.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import html
import pathlib
import re
import shutil
import string
import struct
import sys
from dataclasses import dataclass, field

REPO = pathlib.Path(__file__).resolve().parent.parent
CONTENT = REPO / "help" / "content"
SITE = REPO / "help" / "site"
IMG = REPO / "docs" / "img"
SCREEN_HEADER = REPO / "shared" / "ui" / "include" / "ui_screen.h"
DEFAULT_OUT = REPO / "build" / "help"

LANGS = ("en", "de")
# Where a language's pages and screenshots sit under the help root, and where
# its screenshots sit under docs/img/: the layout render_ui.py writes.
LANG_DIR = {"en": "", "de": "de/"}

# Screens with no page: the splash shows for 1 s at start-up and offers
# nothing to do.
NO_PAGE = {"SCREEN_SPLASH"}

HEADER_KEYS = ("screen", "title", "order")
ORDER_MAX = 9999
PAGE_ID = re.compile(r"[a-z0-9][a-z0-9_-]*")
VIEW_NAME = re.compile(r"[a-z0-9][a-z0-9-]*")

# The words of the help's own frame, outside the content files.
CHROME = {
    "en": {
        "name": "rcbench panel help",
        "intro": "The panel of the rcbench test bench, screen by screen: "
                 "what each screen shows and what it lets the user do. "
                 "The screenshots are rendered by the panel's own drawing "
                 "code.",
        "pages": "Pages",
        "switch": "Deutsch",
        "configurator": "Bench configurator",
        "repository": "Repository",
    },
    "de": {
        "name": "rcbench Panel-Hilfe",
        "intro": "Das Panel des rcbench-Prüfstands, Bildschirm für "
                 "Bildschirm: was jeder Bildschirm zeigt und was er "
                 "bedienen lässt. Die Screenshots zeichnet der Code, der "
                 "auch auf dem Panel zeichnet.",
        "pages": "Seiten",
        "switch": "English",
        "configurator": "Bench-Konfigurator",
        "repository": "Repository",
    },
}
REPOSITORY_URL = "https://github.com/subtilitas/rcbench"


# ------------------------------------------------------------------ model

@dataclass
class Heading:
    level: int
    text: str
    slug: str
    line: int


@dataclass
class Link:
    page: str | None    # target id; None for a link within the page
    anchor: str | None
    line: int


@dataclass
class Page:
    path: pathlib.Path
    lang: str
    id: str
    screen: str = ""
    title: str = ""
    order: int = 0
    body: str = ""          # rendered HTML of the body
    lead: str = ""          # rendered HTML of the first paragraph
    has_h1: bool = False
    headings: list[Heading] = field(default_factory=list)
    views: list[tuple[str, int]] = field(default_factory=list)
    links: list[Link] = field(default_factory=list)


class Problems(list):
    """Problems as `path:line: message`, in the order found."""

    def add(self, path, line, message):
        try:
            shown = pathlib.Path(path).resolve().relative_to(REPO)
        except ValueError:
            shown = path
        self.append(f"{shown}:{line}: {message}")


# ------------------------------------------------------------------ inputs

def screen_names(header: pathlib.Path = SCREEN_HEADER) -> list[str]:
    """The SCREEN_* names of the router's enum, in order, without
    SCREEN_COUNT."""
    text = header.read_text(encoding="utf-8")
    m = re.search(r"typedef\s+enum\s*\{(.*?)\}\s*ui_screen_id_t\s*;",
                  text, re.S)
    if not m:
        raise SystemExit(f"{header}: no ui_screen_id_t enum")
    body = re.sub(r"/\*.*?\*/|//[^\n]*", "", m.group(1), flags=re.S)
    names = re.findall(r"\b(SCREEN_[A-Z0-9_]+)\b", body)
    return [n for n in names if n != "SCREEN_COUNT"]


def screen_id(name: str) -> str:
    """SCREEN_COPROC_UPDATE -> coproc_update."""
    return name[len("SCREEN_"):].lower()


def view_files() -> dict[str, str]:
    """render_ui.py's committed views: name -> PNG file name."""
    sys.path.insert(0, str(REPO / "tools"))
    try:
        import render_ui
    finally:
        sys.path.pop(0)
    return {name: entry[0] for name, entry in render_ui.SCREENS.items()}


def image_path(img_root: pathlib.Path, lang: str, filename: str):
    return img_root / LANG_DIR[lang] / filename


def png_size(path: pathlib.Path) -> tuple[int, int] | None:
    """Width and height from the IHDR chunk, or None for a file that is not
    a PNG."""
    with open(path, "rb") as f:
        head = f.read(24)
    if len(head) < 24 or head[:8] != b"\x89PNG\r\n\x1a\n" \
            or head[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", head[16:24])


# ------------------------------------------------------------- the inline

# A backslash before one of these is the character itself.
ESCAPABLE = set("\\`*_[]()#|!<>-")

LINK_TARGET = re.compile(
    r"(?:(?P<ext>https?://[^\s<>\"'`]+)"
    r"|(?P<page>[a-z0-9][a-z0-9_-]*)\.md(?:#(?P<panchor>[^\s#]+))?"
    r"|#(?P<anchor>[^\s#]+))\Z")

UNDERSCORE_EMPHASIS = re.compile(r"(?<![\w\\])_(?=[^\s_])[^_\n]*?(?<=\S)_"
                                 r"(?!\w)")


def slugify(text: str) -> str:
    """The anchor of a heading: lower case, every run of characters other
    than letters and digits as one '-', none at either end."""
    return re.sub(r"[\W_]+", "-", text.lower()).strip("-")


def plain(text: str) -> str:
    """Inline Markdown as text: the marks dropped, link targets dropped."""
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"\\(.)", r"\1", text)
    return text.replace("**", "").replace("`", "")


class Inline:
    """Renders one run of inline Markdown, reporting problems to a page."""

    def __init__(self, page: Page, problems: Problems):
        self.page = page
        self.problems = problems

    def fail(self, line, message):
        self.problems.add(self.page.path, line, message)

    def render(self, text: str, line: int, in_link: bool = False) -> str:
        outside_code = re.sub(r"`[^`]*`", "", text)
        m = UNDERSCORE_EMPHASIS.search(outside_code)
        if m:
            self.fail(line, f"_ emphasis is outside the format: "
                            f"{m.group(0)[:40]!r}; write \\_ for an "
                            "underscore")
        out = []
        bold = False
        i, n = 0, len(text)
        while i < n:
            c = text[i]
            if c == "\\" and i + 1 < n and text[i + 1] in ESCAPABLE:
                out.append(html.escape(text[i + 1]))
                i += 2
            elif c == "`":
                if text.startswith("``", i):
                    self.fail(line, "a code span takes single backticks")
                    return html.escape(text)
                end = text.find("`", i + 1)
                if end < 0:
                    self.fail(line, "unclosed ` (code span)")
                    return html.escape(text)
                out.append(f"<code>{html.escape(text[i + 1:end])}</code>")
                i = end + 1
            elif c == "*":
                if text.startswith("**", i):
                    out.append("</strong>" if bold else "<strong>")
                    bold = not bold
                    i += 2
                else:
                    self.fail(line, "a single * is outside the format "
                                    "(emphasis); write \\* for an asterisk")
                    out.append("*")
                    i += 1
            elif c == "!" and text.startswith("![", i):
                self.fail(line, "an image stands on a line of its own, "
                                "in a block of images")
                out.append("!")
                i += 1
            elif c == "[":
                j = self._link(text, i, line, out, in_link)
                i = j
            else:
                # Plain text up to the next character that may start a mark.
                j = i + 1
                while j < n and text[j] not in "\\`*![":
                    j += 1
                out.append(html.escape(text[i:j]))
                i = j
        if bold:
            self.fail(line, "unclosed **")
            out.append("</strong>")
        return "".join(out)

    def _link(self, text, i, line, out, in_link) -> int:
        """A link at text[i] == '['; returns the index after it.  A '['
        that does not open a link is the character itself."""
        close = self._closing_bracket(text, i)
        if close < 0 or close + 1 >= len(text) \
                or text[close + 1] not in "([":
            out.append("[")
            return i + 1
        if text[close + 1] == "[":
            self.fail(line, "a reference link is outside the format; "
                            "write [text](target)")
            out.append("[")
            return i + 1
        end = text.find(")", close + 2)
        if end < 0:
            self.fail(line, "unclosed link target: a ( without )")
            out.append(html.escape(text[i:]))
            return len(text)
        label, target = text[i + 1:close], text[close + 2:end].strip()
        if in_link:
            self.fail(line, "a link inside a link")
        if not label.strip():
            self.fail(line, f"a link without text, to {target!r}")
        href = self._href(target, line)
        inner = self.render(label, line, in_link=True)
        if href is None:
            out.append(inner)
        elif href.startswith("http"):
            out.append(f'<a href="{html.escape(href)}" '
                       f'rel="noopener noreferrer">{inner}</a>')
        else:
            out.append(f'<a href="{html.escape(href)}">{inner}</a>')
        return end + 1

    @staticmethod
    def _closing_bracket(text, i) -> int:
        j = i + 1
        while j < len(text):
            if text[j] == "\\":
                j += 2
                continue
            if text[j] == "]":
                return j
            if text[j] == "[":
                return -1
            j += 1
        return -1

    def _href(self, target: str, line: int) -> str | None:
        m = LINK_TARGET.match(target)
        if not m:
            self.fail(line, f"link target {target[:60]!r} is outside the "
                            "format: https://..., <id>.md, <id>.md#anchor "
                            "or #anchor")
            return None
        if m.group("ext"):
            return m.group("ext")
        if m.group("page"):
            pid, anchor = m.group("page"), m.group("panchor")
            self.page.links.append(Link(pid, anchor, line))
            return f"{pid}.html" + (f"#{anchor}" if anchor else "")
        self.page.links.append(Link(None, m.group("anchor"), line))
        return f"#{m.group('anchor')}"


# -------------------------------------------------------------- the blocks

HEADING = re.compile(r"(#+)(\s+)(.*)\Z")
BULLET = re.compile(r"- (.*)\Z")
NUMBER = re.compile(r"(\d{1,9})\. (.*)\Z")
IMAGE_LINE = re.compile(r"!\[([^\]\[]*)\]\(([^()]*)\)\Z")
SEPARATOR_CELL = re.compile(r":?-+:?\Z")


def starts_block(line: str) -> str | None:
    """The kind of block a line opens, for lines that cannot sit inside
    another block, or None for a plain line."""
    s = line.lstrip()
    if s.startswith("#"):
        return "heading"
    if s.startswith("|"):
        return "table"
    if s.startswith("![") and line == s:
        return "image"
    if BULLET.match(s) or NUMBER.match(s):
        return "list"
    return None


def split_cells(row: str) -> list[str]:
    """The cells of a row that starts and ends with '|', split on every
    '|' not escaped with a backslash."""
    inner = row[1:-1]
    cells, cur, i = [], [], 0
    while i < len(inner):
        if inner[i] == "\\" and i + 1 < len(inner):
            cur.append(inner[i:i + 2])
            i += 2
            continue
        if inner[i] == "|":
            cells.append("".join(cur).strip())
            cur = []
        else:
            cur.append(inner[i])
        i += 1
    cells.append("".join(cur).strip())
    return cells


class Renderer:
    """Turns one content file into a Page."""

    def __init__(self, page: Page, problems: Problems,
                 views: dict[str, str], img_root: pathlib.Path):
        self.page = page
        self.problems = problems
        self.views = views
        self.img_root = img_root
        self.inline = Inline(page, problems)
        self.out: list[str] = []
        self.root = "../" if LANG_DIR[page.lang] else ""

    def fail(self, line, message):
        self.problems.add(self.page.path, line, message)

    # The header -----------------------------------------------------------

    def header(self, lines: list[str]) -> int:
        """Reads the header block; returns the index of the first body
        line."""
        if not lines or lines[0].rstrip() != "---":
            self.fail(1, "the file starts with a header block: ---, "
                         "screen:, title:, order:, ---")
            return 0
        seen: dict[str, str] = {}
        for k in range(1, len(lines)):
            text = lines[k].rstrip()
            if text == "---":
                break
            m = re.match(r"([a-z]+):\s*(.*)\Z", text)
            if not m:
                self.fail(k + 1, f"header line {text[:60]!r} is not "
                                 "key: value")
                continue
            key, value = m.groups()
            if key not in HEADER_KEYS:
                self.fail(k + 1, f"unknown header key {key!r}; the keys "
                                 f"are {', '.join(HEADER_KEYS)}")
            elif key in seen:
                self.fail(k + 1, f"header key {key!r} twice")
            else:
                seen[key] = value.strip()
        else:
            self.fail(1, "the header block has no closing ---")
            return len(lines)
        end = k + 1
        for key in HEADER_KEYS:
            if key not in seen:
                self.fail(1, f"header key {key!r} is missing")
        page = self.page
        page.title = seen.get("title", "")
        if "title" in seen and not page.title:
            self.fail(1, "the title is empty")
        page.screen = seen.get("screen", "")
        if page.screen and page.screen != "none" \
                and not re.fullmatch(r"[A-Z0-9_]+", page.screen):
            self.fail(1, f"screen {page.screen!r} is a SCREEN_* name "
                         "without SCREEN_, or none")
        order = seen.get("order")
        if order is not None:
            if re.fullmatch(r"\d{1,4}", order):
                page.order = int(order)
            else:
                self.fail(1, f"order {order!r} is an integer from 0 to "
                             f"{ORDER_MAX}")
        return end

    # The body -------------------------------------------------------------

    def body(self, lines: list[str], start: int) -> None:
        i, n = start, len(lines)
        first = True
        while i < n:
            if not lines[i].strip():
                i += 1
                continue
            line = lines[i].rstrip()
            if line.lstrip().startswith("#"):
                self.heading(line, i + 1, first)
                i += 1
                first = False
                continue
            j = i
            while j < n and lines[j].strip() \
                    and not lines[j].lstrip().startswith("#"):
                j += 1
            block = [ln.rstrip() for ln in lines[i:j]]
            self.block(block, i + 1)
            first = False
            i = j

    def heading(self, line: str, num: int, first: bool) -> None:
        s = line.lstrip()
        m = HEADING.match(s)
        if line != s:
            self.fail(num, "a heading starts in the first column")
        if not m:
            self.fail(num, "a heading needs a space after its #")
            return
        level, text = len(m.group(1)), m.group(3).strip()
        if level > 3:
            self.fail(num, f"heading level {level} is outside the format; "
                           "#, ## and ### only")
            return
        if not text:
            self.fail(num, "an empty heading")
            return
        if level == 1:
            if not first:
                self.fail(num, "# is the page title: once, as the first "
                               "block; use ## and ###")
            self.page.has_h1 = True
        slug = slugify(plain(text))
        if any(h.slug == slug for h in self.page.headings):
            self.fail(num, f"a second heading with the anchor #{slug}")
        self.page.headings.append(Heading(level, text, slug, num))
        inner = self.inline.render(text, num)
        self.out.append(f'<h{level} id="{html.escape(slug)}">{inner}'
                        f'</h{level}>')

    def block(self, block: list[str], num: int) -> None:
        first = block[0]
        s = first.lstrip()
        if re.match(r"(```|~~~)", s):
            self.fail(num, "a fenced code block is outside the format; "
                           "use `code` in a line")
            return
        if s.startswith(">"):
            self.fail(num, "a block quote is outside the format")
            return
        if re.fullmatch(r"(-{3,}|\*{3,}|_{3,}|={3,})", s.replace(" ", "")):
            self.fail(num, "a rule (---) is outside the format")
            return
        if re.match(r"[*+] ", s):
            self.fail(num, "a list item starts with '- ' or '1. '")
            return
        kind = starts_block(first)
        if first != s and kind is not None:
            self.fail(num, f"an indented {kind}: a block starts in the "
                           "first column")
            return
        if kind == "table":
            self.table(block, num)
        elif kind == "image":
            self.images(block, num)
        elif kind == "list":
            self.list(block, num)
        else:
            self.paragraph(block, num)

    def paragraph(self, block, num) -> None:
        for k, line in enumerate(block):
            if k and re.fullmatch(r"\s*(=+|-+)\s*", line):
                self.fail(num + k, "a setext heading (=== or --- under a "
                                   "line) is outside the format; use ##")
                return
            kind = starts_block(line)
            if k and kind:
                self.fail(num + k, f"a {kind} needs a blank line before "
                                   "it")
                return
        # Each line is rendered on its own so a problem names its line.
        parts = [self.inline.render(line.strip(), num + k)
                 for k, line in enumerate(block)]
        html_ = f"<p>{' '.join(parts)}</p>"
        if not self.page.lead and not self.page.headings[1:]:
            self.page.lead = html_
        self.out.append(html_)

    def list(self, block, num) -> None:
        ordered = NUMBER.match(block[0]) is not None
        items: list[list[tuple[str, int]]] = []
        start = 1
        for k, line in enumerate(block):
            bullet, number = BULLET.match(line), NUMBER.match(line)
            if bullet or number:
                if (number is not None) != ordered:
                    self.fail(num + k, "a list mixes '- ' and '1. ' items; "
                                       "leave a blank line between lists")
                    return
                if number and not items:
                    start = int(number.group(1))
                items.append([((number or bullet).groups()[-1], num + k)])
                continue
            s = line.strip()
            if line != s and (BULLET.match(s) or NUMBER.match(s)
                              or re.match(r"[*+] ", s)):
                self.fail(num + k, "a nested list is outside the format")
                return
            kind = starts_block(line)
            if kind:
                self.fail(num + k, f"a {kind} needs a blank line before "
                                   "it")
                return
            items[-1].append((s, num + k))
        tag = "ol" if ordered else "ul"
        attr = f' start="{start}"' if ordered and start != 1 else ""
        lis = []
        for item in items:
            parts = [self.inline.render(t.strip(), ln) for t, ln in item]
            lis.append(f"<li>{' '.join(parts)}</li>")
        self.out.append(f"<{tag}{attr}>" + "".join(lis) + f"</{tag}>")

    def table(self, block, num) -> None:
        for k, line in enumerate(block):
            if not line.startswith("|"):
                self.fail(num + k, "a table row starts with |")
                return
            if len(line) < 2 or not line.endswith("|") \
                    or line.endswith("\\|"):
                self.fail(num + k, "a table row ends with | (unclosed "
                                   "table row)")
                return
        if len(block) < 2:
            self.fail(num, "a table needs a header row and a separator "
                           "row |---|")
            return
        head = split_cells(block[0])
        sep = split_cells(block[1])
        if not all(SEPARATOR_CELL.match(c) for c in sep):
            self.fail(num + 1, "the second row of a table is the separator "
                               "|---|---|")
            return
        for k, line in enumerate(block[1:], start=1):
            cells = split_cells(line)
            if len(cells) != len(head):
                self.fail(num + k, f"a table row with {len(cells)} cells "
                                   f"under a header of {len(head)}")
                return
        aligns = []
        for c in sep:
            if c.startswith(":") and c.endswith(":"):
                aligns.append(' class="c"')
            elif c.endswith(":"):
                aligns.append(' class="r"')
            else:
                aligns.append("")
        rows = ["<thead><tr>" + "".join(
            f"<th{a}>{self.inline.render(c, num)}</th>"
            for c, a in zip(head, aligns, strict=True)) + "</tr></thead>"]
        body = []
        for k, line in enumerate(block[2:], start=2):
            body.append("<tr>" + "".join(
                f"<td{a}>{self.inline.render(c, num + k)}</td>"
                for c, a in zip(split_cells(line), aligns, strict=True))
                + "</tr>")
        if body:
            rows.append("<tbody>" + "".join(body) + "</tbody>")
        self.out.append('<div class="table"><table>' + "".join(rows)
                        + "</table></div>")

    def images(self, block, num) -> None:
        figures = []
        for k, line in enumerate(block):
            m = IMAGE_LINE.match(line.strip())
            if not m:
                self.fail(num + k, "a block of images holds one "
                                   "![caption](view:<name>) per line and "
                                   "nothing else")
                return
            caption, target = m.group(1).strip(), m.group(2).strip()
            if not target.startswith("view:"):
                self.fail(num + k, f"an image takes view:<name>, not "
                                   f"{target[:60]!r}")
                return
            name = target[len("view:"):]
            if not caption:
                self.fail(num + k, f"the image of view {name!r} has no "
                                   "caption")
            if not VIEW_NAME.fullmatch(name):
                self.fail(num + k, f"view name {name[:60]!r} is not a "
                                   "render_ui.py view")
                return
            self.page.views.append((name, num + k))
            figures.append(self.figure(name, caption, num + k))
        cls = "shots" if len(figures) > 1 else "shot"
        self.out.append(f'<div class="{cls}">' + "".join(figures)
                        + "</div>")

    def figure(self, name, caption, num) -> str:
        filename = self.views.get(name)
        size = None
        if filename is None:
            self.fail(num, f"view {name!r} is not in render_ui.py's "
                           "SCREENS")
            filename = f"{name}.png"
        else:
            path = image_path(self.img_root, self.page.lang, filename)
            if not path.is_file():
                shown = f"docs/img/{LANG_DIR[self.page.lang]}{filename}"
                self.fail(num, f"view {name!r} has no image {shown}")
            else:
                size = png_size(path)
                if size is None:
                    self.fail(num, f"{path.name} is not a PNG file")
        src = f"{self.root}img/{LANG_DIR[self.page.lang]}{filename}"
        dims = f' width="{size[0]}" height="{size[1]}"' if size else ""
        alt = html.escape(plain(caption))
        cap = self.inline.render(caption, num)
        return (f'<figure><img src="{html.escape(src)}" alt="{alt}"{dims} '
                f'loading="lazy"><figcaption>{cap}</figcaption></figure>')


def parse(path: pathlib.Path, lang: str, problems: Problems,
          views: dict[str, str], img_root: pathlib.Path = IMG) -> Page:
    """One content file as a Page; its problems go to @p problems."""
    page = Page(path=path, lang=lang, id=path.stem)
    # A file named _*.md is skipped by the build; --page still renders it.
    if not PAGE_ID.fullmatch(page.id.lstrip("_")):
        problems.add(path, 1, f"page id {page.id!r} is lower-case letters, "
                              "digits, - and _")
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError as e:
        problems.add(path, 1, f"not UTF-8: {e.reason}")
        return page
    if "\0" in text:
        problems.add(path, 1, "a NUL character")
        return page
    lines = text.replace("\r\n", "\n").replace("\r", "\n").split("\n")
    r = Renderer(page, problems, views, img_root)
    start = r.header(lines)
    r.body(lines, start)
    # The same-page anchors are known only once every heading is read.
    slugs = {h.slug for h in page.headings}
    for link in page.links:
        if link.page is None and link.anchor not in slugs:
            problems.add(path, link.line, f"no heading with the anchor "
                                          f"#{link.anchor} on this page")
    page.body = "\n".join(r.out)
    return page


# ---------------------------------------------------------- across pages

def load(content: pathlib.Path, problems: Problems,
         views: dict[str, str], img_root: pathlib.Path = IMG
         ) -> dict[str, dict[str, Page]]:
    """Every page of every language: lang -> id -> Page."""
    pages: dict[str, dict[str, Page]] = {}
    for lang in LANGS:
        pages[lang] = {}
        folder = content / lang
        if not folder.is_dir():
            problems.add(folder, 0, f"no content folder for {lang}")
            continue
        for path in sorted(folder.glob("*.md")):
            if path.name.startswith("_"):
                continue
            pages[lang][path.stem] = parse(path, lang, problems, views,
                                           img_root)
        for path in sorted(folder.iterdir()):
            if path.suffix != ".md" and not path.name.startswith("."):
                problems.add(path, 0, "not a page: content files end in .md")
    return pages


def cross_check(pages: dict[str, dict[str, Page]], screens: list[str],
                problems: Problems) -> None:
    """The checks that need more than one page."""
    for lang in LANGS:
        by_screen: dict[str, Page] = {}
        for page in pages[lang].values():
            if not page.screen or page.screen == "none":
                continue
            name = f"SCREEN_{page.screen}"
            if name not in screens:
                problems.add(page.path, 1, f"{name} is not a screen of "
                                           "ui_screen.h")
            elif page.id != screen_id(name):
                problems.add(page.path, 1, f"the page of {name} is "
                                           f"{screen_id(name)}.md")
            if name in by_screen:
                problems.add(page.path, 1, f"a second page of {name}, "
                                           "after "
                                           f"{by_screen[name].path.name}")
            by_screen.setdefault(name, page)
        for name in screens:
            if name in NO_PAGE:
                continue
            if screen_id(name) not in pages[lang]:
                problems.add(CONTENT / lang / f"{screen_id(name)}.md", 0,
                             f"{name} has no {lang} page")
        for page in pages[lang].values():
            for link in page.links:
                if link.page is None:
                    continue
                if link.page == "index":
                    if link.anchor:
                        problems.add(page.path, link.line,
                                     "the index has no anchors")
                    continue
                target = pages[lang].get(link.page)
                if target is None:
                    problems.add(page.path, link.line,
                                 f"link to a missing page {link.page}.md")
                elif link.anchor and link.anchor not in \
                        {h.slug for h in target.headings}:
                    problems.add(page.path, link.line,
                                 f"no heading #{link.anchor} on "
                                 f"{link.page}.md")

    en, de = pages["en"], pages["de"]
    for pid in sorted(set(en) ^ set(de)):
        have = en.get(pid) or de.get(pid)
        missing = "de" if pid in en else "en"
        problems.add(have.path, 0, f"no {missing} counterpart "
                                   f"help/content/{missing}/{pid}.md")
    for pid in sorted(set(en) & set(de)):
        compare(en[pid], de[pid], problems)


def first_difference(a: list, b: list) -> int:
    """The index of the first item where two lists differ."""
    for k, (x, y) in enumerate(zip(a, b, strict=False)):
        if x != y:
            return k
    return min(len(a), len(b))


def compare(en: Page, de: Page, problems: Problems) -> None:
    """The English and German page of one id: same header facts, same
    heading levels, same views, each in order."""
    if en.screen != de.screen:
        problems.add(de.path, 1, f"screen {de.screen!r} differs from the "
                                 f"English {en.screen!r}")
    if en.order != de.order:
        problems.add(de.path, 1, f"order {de.order} differs from the "
                                 f"English {en.order}")
    if en.has_h1 != de.has_h1:
        problems.add(de.path, 1, "one language has a # title and the "
                                 "other has none")
    a = [h.level for h in en.headings]
    b = [h.level for h in de.headings]
    if a != b:
        k = first_difference(a, b)
        where_en = en.headings[k].line if k < len(a) else "end"
        where_de = de.headings[k].line if k < len(b) else 0
        problems.add(de.path, where_de,
                     f"heading {k + 1} differs from the English page: "
                     f"{len(a)} headings {a} in en, {len(b)} {b} in de "
                     f"(en line {where_en})")
    va = [v for v, _ in en.views]
    vb = [v for v, _ in de.views]
    if va != vb:
        k = first_difference(va, vb)
        where_de = de.views[k][1] if k < len(vb) else 0
        got_en = va[k] if k < len(va) else "none"
        got_de = vb[k] if k < len(vb) else "none"
        problems.add(de.path, where_de,
                     f"view {k + 1} differs from the English page: "
                     f"{got_de} in de, {got_en} in en")


# ------------------------------------------------------------------ output

def template(name: str, site: pathlib.Path = SITE) -> string.Template:
    return string.Template((site / name).read_text(encoding="utf-8"))


def sorted_pages(pages: dict[str, Page]) -> list[Page]:
    return sorted(pages.values(), key=lambda p: (p.order, p.id))


def render_page(page: Page, siblings: dict[str, Page],
                site: pathlib.Path = SITE) -> str:
    lang = page.lang
    other = "de" if lang == "en" else "en"
    words = CHROME[lang]
    root = "../" if LANG_DIR[lang] else ""
    other_dir = LANG_DIR[other] if lang == "en" else ""
    body = page.body
    if not page.has_h1:
        body = f"<h1>{html.escape(page.title)}</h1>\n{body}"
    items = "".join(
        f'<li><a href="{p.id}.html"'
        + (' aria-current="page"' if p.id == page.id else "")
        + f">{html.escape(p.title)}</a></li>"
        for p in sorted_pages(siblings))
    pages = html.escape(words["pages"])
    nav = (f'<nav class="pages" aria-label="{pages}"><h2>{pages}</h2>'
           f"<ol>{items}</ol></nav>")
    return template("page.html", site).substitute(
        lang=lang,
        other_lang=other,
        title=html.escape(f"{page.title} - {words['name']}"),
        root=root,
        home="index.html",
        name=html.escape(words["name"]),
        switch_href=f"{root}{other_dir}{page.id}.html",
        switch=html.escape(words["switch"]),
        nav=nav,
        body=body,
        configurator=html.escape(words["configurator"]),
        repository=html.escape(words["repository"]),
        repository_url=REPOSITORY_URL,
    )


def render_index(lang: str, pages: dict[str, Page],
                 site: pathlib.Path = SITE) -> str:
    other = "de" if lang == "en" else "en"
    words = CHROME[lang]
    root = "../" if LANG_DIR[lang] else ""
    other_dir = LANG_DIR[other] if lang == "en" else ""
    entries = "".join(
        f'<li><a href="{p.id}.html">{html.escape(p.title)}</a>'
        + (f"{p.lead}" if p.lead else "") + "</li>"
        for p in sorted_pages(pages))
    body = (f"<h1>{html.escape(words['name'])}</h1>\n"
            f"<p>{html.escape(words['intro'])}</p>\n"
            f'<ol class="index">{entries}</ol>')
    return template("page.html", site).substitute(
        lang=lang,
        other_lang=other,
        title=html.escape(words["name"]),
        root=root,
        home="index.html",
        name=html.escape(words["name"]),
        switch_href=f"{root}{other_dir}index.html",
        switch=html.escape(words["switch"]),
        nav="",
        body=body,
        configurator=html.escape(words["configurator"]),
        repository=html.escape(words["repository"]),
        repository_url=REPOSITORY_URL,
    )


def write(out: pathlib.Path, pages: dict[str, dict[str, Page]],
          views: dict[str, str], img_root: pathlib.Path = IMG,
          site: pathlib.Path = SITE, index: bool = True) -> int:
    """Writes the site into @p out, which is emptied first.  Returns the
    number of files written."""
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    count = 0
    shutil.copyfile(site / "help.css", out / "style.css")
    count += 1
    for lang in LANGS:
        folder = out / LANG_DIR[lang]
        folder.mkdir(parents=True, exist_ok=True)
        if index and pages[lang]:
            (folder / "index.html").write_text(
                render_index(lang, pages[lang], site), encoding="utf-8")
            count += 1
        for page in pages[lang].values():
            (folder / f"{page.id}.html").write_text(
                render_page(page, pages[lang], site), encoding="utf-8")
            count += 1
            for name, _ in page.views:
                filename = views[name]
                dest = out / "img" / LANG_DIR[lang] / filename
                if dest.exists():
                    continue
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(image_path(img_root, lang, filename), dest)
                count += 1
    return count


def run(content: pathlib.Path, screens: list[str], views: dict[str, str],
        img_root: pathlib.Path = IMG):
    """Loads and checks every page: (pages, problems)."""
    problems = Problems()
    pages = load(content, problems, views, img_root)
    cross_check(pages, screens, problems)
    return pages, problems


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", type=pathlib.Path, default=DEFAULT_OUT,
                    help="the build folder, emptied first "
                         "(default: build/help)")
    ap.add_argument("--check", action="store_true",
                    help="run every check and write nothing")
    ap.add_argument("--page", type=pathlib.Path,
                    help="render this one content file with its own checks")
    args = ap.parse_args(argv)

    views = view_files()
    if args.page:
        path = args.page.resolve()
        lang = path.parent.name
        if lang not in LANGS:
            sys.exit(f"{args.page}: the parent folder is the language, "
                     f"one of {', '.join(LANGS)}")
        problems = Problems()
        page = parse(path, lang, problems, views)
        for p in problems:
            print(p, file=sys.stderr)
        if problems:
            return 1
        pages = {lg: {} for lg in LANGS}
        pages[lang][page.id] = page
        if not args.check:
            n = write(args.out, pages, views, index=False)
            print(f"wrote {n} files to {args.out}")
        return 0

    pages, problems = run(CONTENT, screen_names(), views)
    for p in problems:
        print(p, file=sys.stderr)
    if problems:
        print(f"{len(problems)} problems", file=sys.stderr)
        return 1
    count = sum(len(p) for p in pages.values())
    if args.check:
        print(f"{count} pages in {len(LANGS)} languages agree")
        return 0
    n = write(args.out, pages, views)
    print(f"wrote {n} files ({count} pages) to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
