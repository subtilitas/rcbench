---
screen: LOGS
title: Logs
order: 50
---

# Logs

LOGS lists CSV (comma-separated values) recordings and reports on the panel's SD (Secure Digital) card. It previews the CSV import, plots up to 4 columns, and opens reports in the [Files text viewer](files.md#the-text-viewer). Open it from the LOGS tile on the [menu](overview.md).

The three tabs at the top left select a folder: ROOT for the card root, USER for the User log folder (`USER/LOGS` by default), and BENCH for the Auto log folder (`BENCH/LOGS` by default). WI-FI at the bottom left opens [Wi-Fi](wifi.md). FILES beside it opens [Files](files.md) to copy, move, rename or delete card files.

The home tag works like BACK, returning one level at a time: plot to import view, import view to file list, file list to menu. In the DELETE question, it acts as CANCEL.

## No card

![LOGS with no card in the slot](view:logs-nocard)

Without a mounted card, the box shows `NO RECORDINGS` and `No card. Insert one and tap RESCAN.` An empty or uncreated folder shows `No .csv files in BENCH/LOGS.` or `No .csv files in USER/LOGS.` At the root, it shows `No .csv files in the top-level folder.`

The line below describes the selected tab: `Runs are saved as SERVOnnn, MOTORnnn or SUPPLnnn.CSV.`, `Tests are saved as ATESTnnn.CSV and LIMITnnn.TXT.` or `Runs of firmware 0.21.0 and older.` When AUTO LOGS or USER LOGS is off under [SETUP > SD CARD](setup.md), BENCH or USER instead shows `AUTO LOGS OFF on SD CARD.` or `USER LOGS OFF on SD CARD.`

RESCAN reads the card again. Tabs remain usable without a card; each tab press also checks for an inserted card.

## A card that does not mount

![LOGS with a card that holds no FAT volume](view:logs-nofat)

A card containing NTFS (New Technology File System), a GPT (GUID partition table) or no filesystem shows `Card is not FAT32 or exFAT. Format it on a computer.` Other mount failures show `Cannot read the card. Check it on a computer, then tap RESCAN.` See [Card formats](#card-formats) for supported formats and formatting instructions. Formatting erases the card. After reinserting it, tap RESCAN to mount it.

## The file list

![The file list](view:logs)

The header shows the selected folder, such as `BENCH/LOGS`, to the right of the tabs. At the far right it shows the volume label, such as `UNITEST`, or falls back to the card's reported name or `SD CARD`. The list shows 7 files with their sizes. Each row has a 48px (about 7.2mm) touch target.

| Tab | Folder | Holds |
|---|---|---|
| ROOT | the card's root | the runs of firmware 0.21.0 and older |
| USER | User log folder, `USER/LOGS` by default | the AUTOMATIC TEST's and the LIMIT RUN's files |
| BENCH | Auto log folder, `BENCH/LOGS` by default | the automatic log of each ARMED and supply run |

LOGS opens on the tab containing the last file written by the panel. After startup, it uses BENCH until a file is written. If nothing has been written since the last visit, it retains the tab you left. Selecting a tab reads its folder from the first page.

The top strip, LOGS | FILES, switches between the tile's two screens. Each screen's home tag returns to the menu. The strip appears only on the file list, not over an open file, question or box.

WI-FI in the footer opens [Wi-Fi](wifi.md) for browser downloads; its home tag returns here. The button is hidden over the DELETE question or an open file. It can open Wi-Fi while ARMED, but Wi-Fi's own controls enforce their restrictions.

| Button | What it does |
|---|---|
| RESCAN | reads the page on screen again and selects its newest run |
| PREV, NEXT | the page before and after; shown with more than 48 files on the card |
| DELETE | asks, then removes the selected file; grey with nothing selected |
| OPEN | opens the selected CSV file in the import view, a report in the text viewer; grey with nothing selected |

![The fourth page of a full card](view:logs-pages)

- A page holds 48 entries; a card holds up to 999 runs. Pages run newest run first, a report by its own number and after its run's CSV, then the files the bench did not write, and the rows of a page run the same way, the newest run at the top. With more than 48 files the tab names the page's part of the card, `FILES 145 TO 192 OF 1000`.
- Pages are cut from the top, 48 at a time. With fewer than 48 entries ahead of the page on screen, for example after a run was recorded, PREV shows the first page.
- Runs recorded while a later page is open are on page 1. PREV back from page 3 or later then passes over as many entries as were recorded; NEXT from page 1 shows them, and the tab numbers show the shift.
- The scroll bar at the right edge, 56px (about 8.4mm) wide, scrolls within the page: tap the track to jump, drag the thumb to scroll.
- Opening LOGS reads the card: the first page, its newest run selected. A run recorded while LOGS was closed is listed without RESCAN.
- On a page past the first, RESCAN reads from that page's first entry; when the card holds fewer entries, it shows the last page.
- After DELETE the page is read again and the scroll stays; nothing is selected, so DELETE and OPEN are grey until a row is chosen.
- A card put in, taken out or swapped while the panel runs is found by the next RESCAN, the next opening of LOGS, the next page or OPEN, and the next run's start, with no restart. A new card opens on its first page; an empty slot says `No card. Insert one and tap RESCAN.`.
- Newest means the highest number in the name. A file's date is only as good as the panel's clock: it starts unset at each power-on, and until NTP sets it on the home network the panel dates files 1980 ([Setup](setup.md#panel)).
- A run being written is not listed. Its number in another folder is.
- A row takes a tap. A drag of more than 8px on the rows scrolls the list and selects nothing.

## What a run writes

One file is one run, in the folder of its kind. Its name says what started it:

| Folder | Name | Run |
|---|---|---|
| `BENCH/LOGS` | `SERVOnnn.CSV` | one switch to ARMED from SERVO |
| `BENCH/LOGS` | `MOTORnnn.CSV` | one switch to ARMED from MOTOR & ESC's ARM or a stick run on its PROGRAM |
| `BENCH/LOGS` | `SUPPLnnn.CSV` | one switch-on of the SUPPLY output while the bench is SAFE |
| `USER/LOGS` | `ATESTnnn.CSV` | one AUTOMATIC TEST; its report is `ATESTnnn.TXT` |
| `USER/LOGS` | `LIMITnnn.TXT` | one LIMIT RUN's report, with no CSV beside it |
| root | `BENCHnnn.CSV`, `ESCnnn.CSV`, `SERVOnnn.CSV` | runs of firmware 0.21.0 and older; in the root, `SERVOnnn` is a servo test |
| root | `SERVOnnn.TXT` alone | a limit run of firmware 0.21.0 and older |

The name follows the screen whose ARM armed the bench, also on an IOmcu older than 0.18.0, which arms the throttle outputs with every switch to ARMED.

AUTO LOGS off on [SETUP > SD CARD](setup.md) writes no file to `BENCH/LOGS` and takes no number; USER LOGS off writes none to `USER/LOGS`, and the SERVO screen names `USER LOGS OFF` in place of the file. Both are on after a start from a new panel or an update.

Choose different folders with Auto log folder and User log folder under [SETUP > SD CARD](setup.md#sd-card). A change applies to the next file, whose number follows the contents of the new folder. Files left in the previous folder reappear only when you select that folder again.

The panel makes each folder the first time it writes there, one level at a time. A file named `BENCH` or `USER` in the root stops that: the band reads `Cannot make folder BENCH/LOGS. Run not recorded.`

Each folder has its own sequence. A new run uses one above the highest number found there, regardless of filename prefix, including names from firmware 0.21.0. Deleted numbers are not reused. Once a folder contains number 999, it accepts no further run, even if lower numbers are free.

The list includes reports named like runs: `ATESTnnn.TXT` with or without its `ATESTnnn.CSV`, `LIMITnnn.TXT`, and root-level `SERVOnnn.TXT` from firmware 0.21.0 and older. A run and its report occupy separate rows, CSV first. DELETE on the run removes its report too. Other `.TXT` files appear only in FILES.

Runs are ordered by descending number across all prefixes: `MOTOR043.CSV`, then `SERVO042.CSV`, then `SUPPL041.CSV`.

Firmware 0.21.0 and older writes to the root and does not read `BENCH/LOGS` or `USER/LOGS`.

A run is committed to the card every 20 rows or 1000ms, whichever comes first. A power loss mid-run costs under 1.0s of rows while the card keeps up, up to 84 rows, 4.20s at 20Hz, when it has stalled.

The band reports card errors affecting a run:

| Message | Meaning |
|---|---|
| `No card. This run is not being recorded.` | nothing is mounted |
| `Cannot make folder BENCH/LOGS. Run not recorded.` | a level of the folder cannot be made: a file holds its name, or the card takes no new folder |
| `Cannot read the card. Run not recorded.` | the card does not list |
| `Card full or not writable. Run not recorded.` | no run file could be created |
| `Card too slow. Run not recorded.` | every row was dropped |
| `The card was too slow. Some data is missing from the recording.` | some rows were dropped; the time column shows where |
| `Data can no longer be saved to the card. Recording stopped here.` | a write failed mid-run |
| `Data could no longer be saved to the card. Recording incomplete.` | the same, said again when the run closes |
| `The last write to the card failed. Recording incomplete.` | the rows since the last commit are not in the file |

The first five leave no file. The last four leave a file that stops early.

## A report

![A LIMIT RUN's report in the text viewer](view:logs-report)

OPEN on a report opens it in the [text viewer](files.md#the-text-viewer), read-only. The band then reads FILES. The home tag returns to LOGS, with the report chosen on the list again. A report over 1MiB stays on the list, and the footer reads `Over 1MB: too large to open. Download it over Wi-Fi.`

[Wi-Fi](wifi.md) downloads every report as well.

## The import view

![What the import detected](view:logs-import)

OPEN reads the file and previews the detected format before plotting it.

| Line | Shows |
|---|---|
| SEPARATOR | SEMICOLON, COMMA, TAB or PIPE |
| NUMBER FORMAT | ENGLISH (decimal point) or GERMAN (decimal comma) |
| ROWS, COLUMNS | the data rows and columns found |
| TIME AXIS | the column taken as time, or ROW INDEX |
| UNEVEN ROWS | rows with another cell count than the header |
| EVIDENCE | the cells that proved German, English or neither |

The right list shows each column's first value and minimum-to-maximum range. Up to 4 columns are selected automatically. For SERVO files, numeric `pulse`, `angle`, `current` and `voltage` columns take priority, followed by other numeric columns. Other files start with the first 4 numeric columns.

Tap a numeric column to add or remove it; the title counts the selection, for example `PLOT  4/4`. If columns or values look wrong, use SEP and NUM to cycle through the format choices, starting with AUTO. BACK returns to the file list. PLOT draws the selected columns.

A final row without a line feed is treated as incomplete, as can happen after power loss or a full card. It is skipped, ROWS uses the warning colour, and the view shows `INCOMPLETE LAST ROW. SKIPPED.`.

## The plot

![The plot with four columns](view:logs-plot)

The header names the file, its rows, its duration and its sample rate. Each column has a chip with its range and the value at the cursor.

- One finger, `<` and `>` move the cursor. The line at the bottom right shows its time and sample.
- Two fingers zoom: spread to zoom in, pinch to zoom out, move both to pan. The narrowest view is 8 samples, the widest the whole run.
- With SETUP, PANEL, One finger pans on, a finger that moves more than 10px drags a zoomed view sideways.
- BACK returns to the import view.

The rows are spaced evenly by row number. The labels under the plot are the file's time column, so a gap in the rows is a jump in the labels.

## Deleting a file

![The DELETE question](view:logs-delete)

DELETE asks for confirmation and shows the filename and size. To remove the file, press and release DELETE within that dialog. CANCEL or leaving the screen keeps the file. A run still open for logging cannot be deleted.

## Card formats

The panel reads cards formatted FAT12, FAT16, FAT32 or exFAT (extended File Allocation Table): the whole card, or the first of the 4 partitions of an MBR (master boot record) that holds one of them. SDHC cards (4GB to 32GB) come as FAT32, SDXC cards (64GB to 2TB) as exFAT; both mount as they come.

- Not supported: GPT (GUID Partition Table; GUID: globally unique identifier) partitioning, volumes over 2TiB, NTFS, ext4, HFS+ and APFS. Such a card does not mount. The panel never formats a card.
- To format a card, use the SD Association's SD Memory Card Formatter, or on a computer FAT32 or exFAT on an MBR (on macOS: Scheme `Master Boot Record`).
- A file of 4GiB and more on an exFAT card is listed with its size, for example `8.00GB`. OPEN reads files up to 2GiB less 1 byte; a larger one stays on the list and OPEN says `BENCH001.CSV is 2GB or larger. Not opened.` [Wi-Fi](wifi.md) downloads it.

## Related pages

- [Wi-Fi](wifi.md): download the card's files in a browser.
- [Motor and ESC](motor.md), [Supply](supply.md) and [Servo](servo.md): the runs these files hold.
