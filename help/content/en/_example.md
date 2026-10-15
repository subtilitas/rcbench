---
screen: none
title: Format example
order: 9999
---

# Format example

This file shows every construct of the help's format. The build skips files
whose name starts with `_`; `tools/help_site.py --page` renders it. The
format is in `help/site/FORMAT.md`.

## Text

A paragraph runs to the next blank line. **Bold** marks a word, `CODE`
quotes a label the screen shows. HTML such as <b>this</b> shows as text.

- A bulleted item.
- A second item that runs onto
  an indented second line.

1. A numbered step.
2. The next step.

## A table

| Control | Range | Unit |
|---|:---:|---:|
| Pulse width | 500 to 2500 | µs |
| Frame period | 2.5 to 20 | ms |

## Screenshots

One image:

![The SERVO screen at rest](view:servo)

Two or more images in one block form a row:

![The rail current from the INA3221](view:servo-ina)
![A clipped measurement window](view:servo-clipped)

## Links

### Targets

- A heading on this page: [Text](#text).
- The index: [all pages](index.md).
- An external page: [the repository](https://github.com/subtilitas/rcbench).
