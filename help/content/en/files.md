---
screen: FILES
title: Files
order: 53
---

# Files

FILES shows the SD (Secure Digital) card in two panes. It copies, moves, renames and deletes files, makes folders and empty text files, shows a file as text and edits it. It is a tab of the strip on the [LOGS](logs.md) tile. The home tag closes a box or the keyboard first, then leads to the menu. FILES drives no output.

## The two panes

![FILES](view:files)

The left pane opens on the card's root, the right one on the User log folder (`USER/LOGS` by default). The pane with the coloured heading is the active one: OPEN, RENAME, DELETE, NEW and INFO act in it. COPY and MOVE take its selected entry to the other pane's folder. A tap on a row selects it and makes its pane the active one; a second tap on the same row is OPEN.

The button over a pane shows the folder's path and goes up one folder. PREV and NEXT stand beside it while the folder holds more than 48 entries. The heading counts the entries, and `+ 1 NOT LISTED` counts the names the panel does not list: names that start with a full stop, such as `.DS_Store`, and names over 63 bytes. A folder that holds only such entries is not empty.

![The second page of a folder of 120 entries](view:files-paged)

Each row shows the name, the size (`512B`, `14kB`, `7.0MB`; 1kB is 1,024 bytes), the kind and a state word:

| State | Meaning |
|---|---|
| FIXED | a folder the bench keeps, such as `BENCH`, `USER` and the log folders: no RENAME, MOVE or DELETE |
| LOCKED | the servo programmer's record, `BENCH/KST/CONF/KST.TXT`: INFO only |
| WRITING | a file the logger has open: INFO only |

![The ESC and the servo folders](view:files-conf)

The fault log, `BENCH/RCBENCH.LOG`, takes COPY and DELETE only while the IOmcu link is up.

## The buttons

A button that would be refused is dimmed. A tap on it says why on the line over the buttons.

| Button | What it does |
|---|---|
| RESCAN | looks at the card slot and reads each pane again at the page it shows. The top row and the selected entry stay; when the selected entry has gone from the card, nothing is selected, as after DELETE |
| COPY | copies the selected file into the other pane's folder. Folders are not copied |
| MOVE | moves the selected file or folder into the other pane's folder |
| RENAME | opens the keyboard with the entry's name |
| DELETE | asks first. A run's CSV file takes its report along. A folder is deleted only when it is empty |
| NEW | FOLDER or TEXT FILE, then the keyboard for the name |
| INFO | name, folder, kind, size or entries, time and rule |
| OPEN | enters a folder. A CSV file in ROOT, USER or BENCH opens on [LOGS](logs.md). Every other file, a report among them, opens in the [text viewer](#the-text-viewer). A file over 1MiB stays here, `Over 1MB: too large to open. Download it over Wi-Fi.`, and so does a file the bench is still writing, `ATEST012.TXT is being written.` |

An existing name is never overwritten: `NOTES.TXT exists there already.` A copy is written as `NAME.NEW` and renamed when it is complete.

![NEW's choice](view:files-new)
![The keyboard for a new text file](view:files-name)

The keyboard takes up to 59 bytes; Ä Ö Ü ä ö ü ß take 2 each. OK refuses an empty name, one of `" * / : < > ? \ |`, a space or a full stop at either end, and device names such as `CON` or `COM1`, and says why beside the title.

## The text viewer

![A servo test's report in the text viewer](view:files-view)

The viewer shows a file as text, read-only. The home tag closes it: back to the panes, or back to [LOGS](logs.md) when LOGS opened it. It opens a file of up to 1MiB and up to 65,536 lines; `Reading the card.` shows while the card is read.

- The header shows the file's folder and name, its size, READ-ONLY and `LINE 23 OF 57`: the line on the top row and the file's lines.
- 18 rows of 92 characters show at once. SMALL switches to a smaller face, 22 rows of 115 characters, for a report's step table; NORMAL switches back. The choice holds until the panel restarts.
- A line wider than a row continues on the next row, marked `..` at the left.
- A drag on the text or on the scroll bar scrolls it. PAGE UP and PAGE DOWN move a screen; each is grey at the text's first or last screen.
- The bench's words take colours: headings, labels such as `Result:`, PASS green, FAIL red, `not measured` and the other words of what did not run in the warning colour, a number with its unit in the unit's colour, `#` comments faint. German reports read the same way. A `.JSON` file is coloured as JSON.
- A file with LF and CR LF line ends shows `MIXED LINE ENDS`.
- A file with a NUL byte in its first 4kB reads `Not a text file: a NUL byte in its first 4kB.` An empty file reads `The file is empty.`

![The same report in SMALL](view:files-view-small)
![A servo file of the KST programmer](view:files-view-kst)

## The text editor

![A line typed into a report](view:files-edit)

EDIT in the viewer opens the editor and its keyboard. The text changes in the panel's memory; SAVE writes it to the card.

- 8 rows of the text show over the keyboard, 10 with SMALL. The keyboard has no digit row: `ABC` gives capitals, `123` gives the digits and the common marks, and `#+=` on that page the other marks; `abc` goes back. DEL takes the character before the cursor. ENTER starts a new line with the file's own line end. `<-` and `->` move the cursor; a tap on the text puts it there. DONE closes the keyboard, KEYBOARD opens it again.
- The header shows MODIFIED once the text differs from the card, and the cursor's line.
- SAVE writes the text as `<name>.NEW`, deletes the old file and gives the new one its name. SAVE is shut while the bench is ARMED or a run is logged; the text stays. A switch to ARMED during a save stops it, and the old file stays.
- A KST file of the servo programmer loses its checksum line when it is saved changed, in every form the programmer reads as one (`checksum`, `CHECKSUM=`, `Checksum:`): the programmer then reads it as a file a person edited.
- SAVE refuses a text with a NUL byte in its first 4kB, as FILES would not open the file again: `Not saved: a NUL byte in the first 4kB. FILES would not open the file again.`
- An ESC profile in `BENCH/ESC/CONF` is read at the panel's next start.
- SAVE writes only over the file as the editor opened it. A file changed on the card since, by the Wi-Fi page's editor or an upload, is not written: the note says `<name> changed on the card since it was read.`, and the question offers STAY, which keeps the text, READ AGAIN, which drops it and reads the file from the card, and OVERWRITE, held 2s, which saves the text over the file that the refused SAVE found. A further change during that time is refused the same way. A file that is gone cannot be overwritten: OVERWRITE is dimmed.
- REVERT puts the text back to the file on the card. It asks first, as the home tag does while the text is unsaved: STAY keeps the text, DISCARD drops the changes.
- The text holds up to 1MiB and 65,536 line ends. The keyboard types ASCII; there is no undo of single keys.
- SETUP → PANEL → Keyboard see-through at 25% or 50% lets the rest of the text show through the keyboard; the cursor stays on the 8 rows over it.

![A KST file saved with a comment](view:files-edit-kst)
![The question over unsaved changes](view:files-edit-unsaved)

## ARMED and logging

![FILES while the bench is ARMED](view:files-armed)

FILES writes only while the bench is SAFE and no run is being logged. While the bench is ARMED or a run is logged, every button but INFO and RESCAN is dimmed, and the line over the buttons reads `Bench ARMED. The card is read-only.` or `A run is being logged. The card is read-only.` A copy under way stops before its next 16KiB when the bench switches to ARMED or a run starts logging.

![FILES while a test is logged](view:files-logging)

## The boxes

![The DELETE question](view:files-delete)
![A copy under way](view:files-copy)
![INFO on a file](view:files-info)
![INFO on a folder](view:files-info-folder)

A job that takes longer than 0.25s shows its progress in a box; CANCEL stops a copy before its next 16KiB. INFO shows the time the card holds, in the zone of SETUP > PANEL's Time zone. A 1980 date reads `not known`: the clock was not set when the file was written ([Setup](setup.md#panel)).

## No card

![FILES with no card](view:files-nocard)

With no card, both panes read `No card. Insert one and tap RESCAN.` A card put in, taken out or swapped is found by RESCAN; both panes then go back to the root and the User log folder.
