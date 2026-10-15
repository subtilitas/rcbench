---
screen: LOGS
title: Logs
order: 50
---

# Logs

LOGS reads the panel's SD (Secure Digital) card: it lists the CSV (comma-separated values) files in the card's root, shows what the import found in one, and plots up to 4 of its columns. It opens from the LOGS tile on the [menu](overview.md); the band names it LOG VIEWER. WI-FI, at the bottom left, opens [Files and update](wifi.md).

## No card

![LOGS with no card in the slot](view:logs-nocard)

With no card mounted, the box reads `NO LOGS` and `No card. Insert one and tap RESCAN.` A card without a CSV file in its root reads `No .csv files in the root.` Runs are written as `BENCH001.CSV` and upwards. RESCAN reads the card again.

## The file list

![The file list](view:logs)

The header names the card and its free space. Each row is one file with its size.

| Button | What it does |
|---|---|
| RESCAN | reads the card again and selects the newest run |
| WI-FI | opens [Files and update](wifi.md) |
| DELETE | asks, then removes the selected file; grey with nothing selected |
| OPEN | opens the selected file in the import view; grey with nothing selected |

- The list holds 48 entries; a card holds up to 999 runs. With more files than fit, the list keeps the newest runs and its tab reads `48 OF 137 FILES`.
- Newest means the highest number in the name. The panel has no clock that survives a power cycle, so every file is dated 1980-01-01.
- A run being written is not listed.
- A row takes a tap. A drag of more than 8 px scrolls the list and selects nothing.

## What a run writes

One file is one run: one switch to ARMED, one switch-on of the SUPPLY output with the bench SAFE, or one automatic servo test. A servo test also writes a report, `BENCHnnn.TXT`, which the list does not show. DELETE on a run removes its report too.

A run is committed to the card every 20 rows or 1000 ms, whichever comes first. A power loss mid-run costs under 1.0 s of rows while the card keeps up, up to 84 rows, 4.20 s at 20 Hz, when it has stalled.

The band reports what the card did to a run:

| Message | Meaning |
|---|---|
| `no card -- this run is not recorded` | nothing is mounted |
| `card unreadable -- run not recorded` | the card does not list |
| `card full or unwritable -- run not recorded` | no run file could be created |
| `the card did not keep up -- run not recorded` | every row was dropped |
| `the card fell behind -- the log has gaps` | some rows were dropped; the time column shows where |
| `the card stopped taking rows -- run not recorded past here` | a write failed mid-run |
| `the card stopped taking rows -- the log is short` | the same, said again when the run closes |
| `the card failed on the last write -- the log is short` | the rows since the last commit are not in the file |

The first four leave no file. The last four leave a file that stops early.

## The import view

![What the import detected](view:logs-import)

OPEN reads the file and shows what it decided before anything is plotted.

| Line | Shows |
|---|---|
| SEPARATOR | SEMICOLON, COMMA, TAB or PIPE |
| NUMBERS | ENGLISH (decimal point) or GERMAN (decimal comma) |
| ROWS, COLUMNS | the data rows and columns found |
| TIME AXIS | the column taken as time, or ROW INDEX |
| RAGGED | rows with another cell count than the header |
| EVIDENCE | the cells that proved German, English or neither |

The right list names each column with its first value and its range, lowest to highest. A tap on a numeric column adds it to the plot or takes it off; the title counts them, `PLOT 4/4`. SEP and NUM step through the choices, AUTO first, when the values or columns look wrong. BACK returns to the list; PLOT draws the chosen columns.

A last row without a line feed is a row cut off by a power loss or a full card. It is left out, ROWS shows in the warning colour and the view reads `LAST ROW CUT OFF - LEFT OUT`.

## The plot

![The plot with four columns](view:logs-plot)

The header names the file, its rows, its duration and its sample rate. Each column has a chip with its range and the value at the cursor.

- One finger, `<` and `>` move the cursor. The line at the bottom right shows its time and sample.
- Two fingers zoom: spread to zoom in, pinch to zoom out, move both to pan. The narrowest view is 8 samples, the widest the whole run.
- With SETUP, APPLICATION, One finger pans on, a finger that moves more than 10 px drags a zoomed view sideways.
- BACK returns to the import view.

The rows are spaced evenly by row number. The labels under the plot are the file's time column, so a gap in the rows is a jump in the labels.

## Deleting a file

![The DELETE question](view:logs-delete)

DELETE opens a question that names the file and its size. Only its own DELETE, pressed and released on that button, removes the file. CANCEL, or leaving the screen, deletes nothing. The run the logger has open is refused.

## Related pages

- [Files and update](wifi.md): download the card's files in a browser.
- [Motor and ESC](motor.md), [Supply](supply.md) and [Servo](servo.md): the runs these files hold.
