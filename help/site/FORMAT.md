# Web help: content format

`tools/help_site.py` builds the panel's web help from `help/content/`.
The English and German help pages are static HTML (HyperText Markup
Language), published on GitHub Pages under `help/` beside the
configurator. This document defines the accepted format. Unsupported
syntax fails the build with `file:line: message`.

```
python3 tools/help_site.py                  # build into build/help/
python3 tools/help_site.py --out DIR        # build into DIR
python3 tools/help_site.py --check          # run every check, write nothing
python3 tools/help_site.py --page FILE --out DIR   # one page, for a preview
```

`--page` previews one file and checks its syntax, views and images.
Checks across pages, including screen coverage, language pairs and links,
run only during a full build or with `--check`. A preview also writes the
page's counterpart when available and both language indexes, so the
header links work.

A build empties its output folder before writing. It refuses any folder
that contains the repository or help sources. It also refuses a nonempty
folder without a `.help-site` file, the marker identifying output from
this tool.

## Files

- Use one file per page and language: `help/content/en/<id>.md` and
  `help/content/de/<id>.md`.
- A screen page's `<id>` is its router screen name in lower case, without
  `SCREEN_`: `SCREEN_COPROC_UPDATE` becomes `coproc_update`.
- Every `SCREEN_*` in `shared/ui/include/ui_screen.h`, except
  `SCREEN_SPLASH` and `SCREEN_COUNT`, needs a page in both languages.
- An extra page for part of a screen may use an id containing lower-case
  letters, digits, `-` and `_`, such as `servo-programmer`.
- Files whose names start with `_` are excluded from the build and
  checks. `_example.md` demonstrates the format.
- Every page needs an English and a German version.

## Header

Every file starts with this block:

```
---
screen: SERVO
title: Servo
order: 3
---
```

| Key | Value |
|---|---|
| `screen` | the `SCREEN_*` name without `SCREEN_`, upper case, or `none` for an extra page |
| `title` | the page title: the browser tab, the index and the `<h1>` when the body has no `#` heading |
| `order` | an integer from 0 to 9999; the index lists pages by it, then by id |

- Each of the three keys is required exactly once. Other keys are rejected.
- A screen page's `screen` value must match its filename: `servo.md` has
  `screen: SERVO`. Use one page per screen and language.
- `screen` and `order` must match in both languages.

## Body

Separate blocks with a blank line. Each heading occupies one line, with
a blank line before and after it. Other blocks continue until the next
blank line.

| Block | Syntax | Notes |
|---|---|---|
| Heading | `# T`, `## T`, `### T` | `#` is at most once, as the first block; it then replaces the title as `<h1>` |
| Paragraph | lines of text | lines join with a space |
| Bulleted list | `- item` | one level; an indented line continues the item above |
| Numbered list | `1. item` | one level; numbering starts at the first number |
| Table | `\| a \| b \|` rows | every row starts and ends with `\|`; the second row is the separator `\|---\|---\|`, 3 characters a cell at least, with `:` for alignment; every row has the header's cell count |
| Images | `![caption](view:<name>)` | one image per line, nothing else in the block; 2 or more lines form a row of screenshots |

Inline, in paragraphs, list items, table cells and headings:

| Inline | Syntax |
|---|---|
| Bold | `**text**` |
| Code | `` `text` `` (single backticks; nothing inside is formatted, and every space is kept) |
| Link | `[text](target)` |
| Escape | `\` before one of ``\ ` * _ [ ] ( ) # | ! < > -`` |

### Links

| Target | Goes to |
|---|---|
| `https://...` or `http://...` | an external page |
| `<id>.md` | another help page of the same language |
| `<id>.md#<anchor>` | a heading on another help page |
| `#<anchor>` | a heading on the same page |
| `index.md` | the help's index of the same language; no content page is named `index.md` |

Anchors are derived from the displayed heading text. Code spans retain
their characters, and links contribute only their labels. The text is
converted to lower case; each run of characters other than letters or
digits becomes `-`, with leading and trailing hyphens removed. For
example, `## Run a test` becomes `#run-a-test`. Targets outside the table
above are rejected, including relative repository paths and `javascript:`.

### Images

`![caption](view:servo-model)` inserts the `servo-model` screenshot from
the `SCREENS` table in `tools/render_ui.py`. English pages use
`docs/img/servo-model.png`; German pages use `docs/img/de/servo-model.png`.
The caption supplies both the image's text alternative and the visible
line below it. Captions are plain text without links. The build copies
only images referenced by the pages.

### Not in the format

The following are rejected:

- headings at level `####` or deeper, `#` without a following space, setext
  headings (`===` or `---` below a line), and `---` rules;
- fenced code blocks, indented code blocks (4 spaces or a tab), and block
  quotes (`>`);
- `*` or `+` list markers anywhere in a block, and nested lists;
- single `*`, `_` or `__` emphasis, reference links (`[text][ref]`), and
  reference definitions (`[label]: target`);
- hard line breaks (two spaces or a `\` at the end of a line);
- images inside text, image targets without `view:`, and links or captions
  that render as empty text;
- unclosed `**` or backticks.

HTML in the text is escaped: `<b>` shows as the characters `<b>`.

## Checks across pages

`--check` and every build fail on:

- a `SCREEN_*` without a page in either language;
- a page without its counterpart in the other language;
- a view absent from `render_ui.py`'s `SCREENS`, or without a PNG (Portable
  Network Graphics) file for the page's language;
- different heading-level sequences or view sequences in the English and
  German versions of a page;
- a link to a missing page or anchor;
- syntax outside the format defined here.

## Output

| Path under `help/` | Content |
|---|---|
| `index.html`, `<id>.html` | English |
| `de/index.html`, `de/<id>.html` | German |
| `style.css` | the one stylesheet, from `help/site/help.css` |
| `img/<view>.png`, `img/de/<view>.png` | the referenced screenshots |

Every page links to its counterpart in the other language. The pages carry
no script and load no external font, script or style.
