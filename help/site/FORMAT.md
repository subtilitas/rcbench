# Web help: content format

`tools/help_site.py` builds the panel's web help from `help/content/`. The
help is static HTML (HyperText Markup Language), in English and German, and
is published on GitHub Pages under `help/` beside the configurator. This file
is the format the generator accepts. Anything outside it fails the build with
`file:line: message`.

```
python3 tools/help_site.py                  # build into build/help/
python3 tools/help_site.py --out DIR        # build into DIR
python3 tools/help_site.py --check          # run every check, write nothing
python3 tools/help_site.py --page FILE --out DIR   # one page, for a preview
```

`--page` renders one file with the checks of that file alone: syntax, views
and images. The checks across pages (the screen list, the two languages,
links between pages) run with a full build and with `--check`. A preview
also writes the page's counterpart in the other language when it exists,
and the index of each language, so the header's links land.

A build empties its output folder first. It refuses a folder that holds
the repository or a source of the help, and an existing folder that is not
empty and has no `.help-site` file, which marks a folder the tool wrote.

## Files

- One file per page and language: `help/content/en/<id>.md` and
  `help/content/de/<id>.md`.
- `<id>` of a screen page is the router screen in lower case without
  `SCREEN_`: `SCREEN_COPROC_UPDATE` is `coproc_update`.
- Every `SCREEN_*` of `shared/ui/include/ui_screen.h` except `SCREEN_SPLASH`
  and `SCREEN_COUNT` has a page in both languages.
- Extra pages for a large part of a screen use any id of lower-case letters,
  digits, `-` and `_`, for example `servo-programmer`.
- A file whose name starts with `_` is not a page. The build and the checks
  skip it. `_example.md` shows the format.
- Every page exists in both languages.

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

- The three keys are required, each once. Other keys fail.
- A screen page's `screen` matches its file name: `servo.md` has
  `screen: SERVO`. One page per screen and language.
- `screen` and `order` are the same in both languages.

## Body

Blocks are separated by a blank line. A heading line is a block of its own;
every other block runs to the next blank line or heading.

| Block | Syntax | Notes |
|---|---|---|
| Heading | `# T`, `## T`, `### T` | `#` is at most once, as the first block; it then replaces the title as `<h1>` |
| Paragraph | lines of text | lines join with a space |
| Bulleted list | `- item` | one level; an indented line continues the item above |
| Numbered list | `1. item` | one level; numbering starts at the first number |
| Table | `\| a \| b \|` rows | every row starts and ends with `\|`; the second row is the separator `\|---\|---\|`, with `:` for alignment; every row has the header's cell count |
| Images | `![caption](view:<name>)` | one image per line, nothing else in the block; 2 or more lines form a row of screenshots |

Inline, in paragraphs, list items, table cells and headings:

| Inline | Syntax |
|---|---|
| Bold | `**text**` |
| Code | `` `text` `` (single backticks; nothing inside is formatted) |
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

An anchor is the heading text in lower case, with every run of characters
other than letters and digits replaced by `-`, and no `-` at either end:
`## Run a test` is `#run-a-test`. Every other target fails, including
relative paths into the repository and `javascript:`.

### Images

`![caption](view:servo-model)` shows the screenshot of the view
`servo-model` of `tools/render_ui.py` (its `SCREENS` table). The English page
takes `docs/img/servo-model.png`, the German page
`docs/img/de/servo-model.png`. The caption is the image's text alternative
and the line under it; it is plain text without links. The build copies only
the referenced images.

### Not in the format

These fail: headings `####` and deeper, `#` without a space after it, setext
headings (`===` or `---` under a line), a `---` rule, fenced code blocks,
block quotes (`>`), `*` and `+` list markers, nested lists, single `*` or
`_` emphasis, reference links (`[text][ref]`), images inside text, image
targets without `view:`, an unclosed `**` or backtick.

HTML in the text is escaped: `<b>` shows as the characters `<b>`.

## Checks across pages

`--check` and every build fail on:

- a `SCREEN_*` without a page in either language;
- a page in one language without its counterpart in the other;
- a view that is not in `render_ui.py`'s `SCREENS`, or has no PNG (Portable
  Network Graphics) file in that page's language;
- an English and a German page of one id whose heading levels, in order, or
  views, in order, differ;
- a link to a page or anchor that does not exist;
- anything outside the format above.

## Output

| Path under `help/` | Content |
|---|---|
| `index.html`, `<id>.html` | English |
| `de/index.html`, `de/<id>.html` | German |
| `style.css` | the one stylesheet, from `help/site/help.css` |
| `img/<view>.png`, `img/de/<view>.png` | the referenced screenshots |

Every page links to its counterpart in the other language. The pages carry
no script and load no external font, script or style.
