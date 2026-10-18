---
screen: LOGS
title: Logs
order: 50
---

# Logs

LOGS reads the panel's SD (Secure Digital) card: it lists the CSV (comma-separated values) files in the card's root, shows what the import found in one, and plots up to 4 of its columns. It opens from the LOGS tile on the [menu](overview.md). WI-FI, at the bottom left, opens [Wi-Fi](wifi.md). The home tag climbs one level, as BACK does: the plot to the import view, the import view to the file list, the file list to the menu. Over the DELETE question it is CANCEL.

## No card

![LOGS with no card in the slot](view:logs-nocard)

With no card mounted, the box reads `NO RECORDINGS` and `No card. Insert one and tap RESCAN.` A card without a CSV file in its root reads `No .csv files in the top-level folder.` Runs are written as `BENCHnnn.CSV`, `ESCnnn.CSV` or `SERVOnnn.CSV`, numbered from 001 to 999. RESCAN reads the card again.

## The file list

![The file list](view:logs)

The header names the card by its volume label; a card without one shows the name the card reports, or `SD CARD`. Each row is one file with its size.

| Button | What it does |
|---|---|
| RESCAN | reads the page on screen again and selects its newest run |
| PREV, NEXT | the page before and after; shown with more than 48 files on the card |
| WI-FI | opens [Wi-Fi](wifi.md) |
| DELETE | asks, then removes the selected file; grey with nothing selected |
| OPEN | opens the selected file in the import view; grey with nothing selected |

![The fourth page of a full card](view:logs-pages)

- A page holds 48 entries; a card holds up to 999 runs. Pages run newest run first, then the files the bench did not write, and the rows of a page run the same way, the newest run at the top. With more than 48 files the tab names the page's part of the card, `FILES 145 TO 192 OF 1000`.
- Pages are cut from the top, 48 at a time. With fewer than 48 entries ahead of the page on screen, for example after a run was recorded, PREV shows the first page.
- Runs recorded while a later page is open are on page 1. PREV back from page 3 or later then passes over as many entries as were recorded; NEXT from page 1 shows them, and the tab numbers show the shift.
- The scroll bar at the right edge, 8.1 mm wide, scrolls within the page: tap the track to jump, drag the thumb to scroll.
- On a page past the first, RESCAN reads from that page's first entry; when the card now holds fewer entries, it shows the last page.
- After DELETE the page is read again and the scroll stays; nothing is selected, so DELETE and OPEN are grey until a row is chosen.
- A card put in while none is mounted is mounted by RESCAN and opens on its first page. A card swapped for another while one is mounted is not mounted again until the panel restarts; what the list shows in that state is not measured.
- Newest means the highest number in the name. The panel has no clock that survives a power cycle, so every file is dated 1980-01-01.
- A run being written is not listed.
- A row takes a tap. A drag of more than 8 px on the rows scrolls the list and selects nothing.

## What a run writes

One file is one run. Its name says which writer made it:

| Name | Run |
|---|---|
| `SERVOnnn.CSV` | one automatic servo test; its report is `SERVOnnn.TXT` |
| `SERVOnnn.TXT` | one limit run's report, with no CSV beside it |
| `ESCnnn.CSV` | one switch of the throttle outputs to ARMED, from MOTOR & ESC's ARM or a stick run on its PROGRAM |
| `BENCHnnn.CSV` | one switch of the surfaces to ARMED, from SERVO, or one switch-on of the SUPPLY output while the bench is SAFE |

With an IOmcu older than protocol 4.18, every switch to ARMED covers the throttle outputs too, so a switch to ARMED from SERVO writes `ESCnnn.CSV`.

The numbers are one series across the three names: a new run takes one above the highest number on the card, whichever name carries it. A gap left by a deleted file is not filled. A card holding a number 999 takes no further run, with lower numbers free or not. A card that holds only `BENCHnnn` files continues above their numbers. Firmware 0.20.0 and older numbers runs above the highest `BENCHnnn` file only. Going back to it on a card written by this firmware can give a run a number that an `ESCnnn` or `SERVOnnn` file already carries. Nothing is overwritten: the two files have different names. The list does not show the `.TXT` reports, and DELETE on a run removes its report too. The list shows runs newest first by number, across all three names: `ESC043.CSV`, then `SERVO042.CSV`, then `BENCH041.CSV`.

A run is committed to the card every 20 rows or 1000 ms, whichever comes first. A power loss mid-run costs under 1.0 s of rows while the card keeps up, up to 84 rows, 4.20 s at 20 Hz, when it has stalled.

The band reports what the card did to a run:

| Message | Meaning |
|---|---|
| `No card. This run is not being recorded.` | nothing is mounted |
| `Cannot read the card. Run not recorded.` | the card does not list |
| `Card full or not writable. Run not recorded.` | no run file could be created |
| `Card too slow. Run not recorded.` | every row was dropped |
| `The card was too slow. Some data is missing from the recording.` | some rows were dropped; the time column shows where |
| `Data can no longer be saved to the card. Recording stopped here.` | a write failed mid-run |
| `Data could no longer be saved to the card. Recording incomplete.` | the same, said again when the run closes |
| `The last write to the card failed. Recording incomplete.` | the rows since the last commit are not in the file |

The first four leave no file. The last four leave a file that stops early.

## The import view

![What the import detected](view:logs-import)

OPEN reads the file and shows what it decided before anything is plotted.

| Line | Shows |
|---|---|
| SEPARATOR | SEMICOLON, COMMA, TAB or PIPE |
| NUMBER FORMAT | ENGLISH (decimal point) or GERMAN (decimal comma) |
| ROWS, COLUMNS | the data rows and columns found |
| TIME AXIS | the column taken as time, or ROW INDEX |
| UNEVEN ROWS | rows with another cell count than the header |
| EVIDENCE | the cells that proved German, English or neither |

The right list names each column with its first value and its range, lowest to highest. A tap on a numeric column adds it to the plot or takes it off; the title counts them, `PLOT  4/4`. SEP and NUM step through the choices, AUTO first, when the values or columns look wrong. BACK returns to the list; PLOT draws the chosen columns.

A last row without a line feed is a row cut off by a power loss or a full card. It is left out, ROWS shows in the warning colour and the view reads `INCOMPLETE LAST ROW. SKIPPED.`.

## The plot

![The plot with four columns](view:logs-plot)

The header names the file, its rows, its duration and its sample rate. Each column has a chip with its range and the value at the cursor.

- One finger, `<` and `>` move the cursor. The line at the bottom right shows its time and sample.
- Two fingers zoom: spread to zoom in, pinch to zoom out, move both to pan. The narrowest view is 8 samples, the widest the whole run.
- With SETUP, PANEL, One finger pans on, a finger that moves more than 10 px drags a zoomed view sideways.
- BACK returns to the import view.

The rows are spaced evenly by row number. The labels under the plot are the file's time column, so a gap in the rows is a jump in the labels.

## Deleting a file

![The DELETE question](view:logs-delete)

DELETE opens a question that names the file and its size. Only its own DELETE, pressed and released on that button, removes the file. CANCEL, or leaving the screen, deletes nothing. The run the logger has open is refused.

## Related pages

- [Wi-Fi](wifi.md): download the card's files in a browser.
- [Motor and ESC](motor.md), [Supply](supply.md) and [Servo](servo.md): the runs these files hold.
