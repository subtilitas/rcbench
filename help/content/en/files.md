---
screen: FILES
title: Files
order: 53
---

# Files

FILES manages the SD (Secure Digital) card in two panes. You can copy, move, rename and delete files, create folders and empty text files, and view or edit text. Open it from the strip on the [LOGS](logs.md) tile. The home tag closes an open box or keyboard first, then returns to the menu. FILES drives no output.

## The two panes

![FILES](view:files)

The left pane opens at the card root; the right opens in the User log folder (`USER/LOGS` by default). A coloured heading marks the active pane, where OPEN, RENAME, DELETE, NEW and INFO act. COPY and MOVE send the selected entry to the other pane's folder. Tap a row to select it and activate its pane. Tap the same row again to open it.

The button above each pane shows its folder path; tap it to go up one folder. PREV and NEXT appear beside it when the folder has more than 48 entries. The heading counts the entries. A separate count, such as `+ 1 NOT LISTED`, covers omitted names: those starting with a full stop, such as `.DS_Store`, and those longer than 63 bytes. A folder containing only omitted entries is not empty.

![The second page of a folder of 120 entries](view:files-paged)

Each row shows the name, the size (`512B`, `14kB`, `7.0MB`; 1kB is 1,024 bytes), the kind and a state word:

| State | Meaning |
|---|---|
| FIXED | a folder the bench keeps, such as `BENCH`, `USER` and the log folders: no RENAME or MOVE; DELETE only when empty, and the bench makes it again when it next writes there |
| LOCKED | the servo programmer's record, `BENCH/KST/CONF/KST.TXT`: INFO only |
| WRITING | a file the logger has open: INFO only |

![The ESC and the servo folders](view:files-conf)

The fault log, `BENCH/RCBENCH.LOG`, takes COPY and DELETE only while the IOmcu link is up.

## The buttons

Unavailable buttons are dimmed. Tap one to see the reason above the button row.

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

An existing name is never overwritten: `NOTES.TXT already exists there.` A copy is written as `NAME.NEW` and renamed when it is complete.

![NEW's choice](view:files-new)
![The keyboard for a new text file](view:files-name)

The keyboard accepts names of up to 59 bytes; each of Ä Ö Ü ä ö ü ß uses 2 bytes. OK rejects empty names, any of `" * / : < > ? \ |`, leading or trailing spaces or full stops, and device names such as `CON` or `COM1`. The reason appears beside the title.

## The text viewer

![A servo test's report in the text viewer](view:files-view)

The viewer opens files read-only, up to 1MiB and 65,536 lines. While reading, it shows `Reading the card.` The home tag returns to the panes, or to [LOGS](logs.md) if opened from there.

- The header shows the folder, filename, size, READ-ONLY and `LINE 23 OF 57`: the top visible line and the total line count.
- The normal font shows 18 rows of 92 characters. SMALL shows 22 rows of 115 characters, useful for a report's step table. NORMAL switches back. The choice lasts until the panel restarts.
- Long lines wrap to the next row, marked `..` at the left.
- Drag the text or scroll bar to scroll. PAGE UP and PAGE DOWN move by one screen; they are grey at the corresponding end.
- Reports use colours for headings and labels such as `Result:`. PASS is green, FAIL red, and `not measured` and other unperformed-result words use the warning colour. Numbers with units use the unit's colour; `#` comments are faint. German reports use the same highlighting. `.JSON` files use JSON highlighting.
- Files containing both LF and CR LF line endings show `MIXED LINE ENDS`.
- A NUL byte in the first 4kB produces `Not a text file: a NUL byte in its first 4kB.` An empty file shows `The file is empty.`

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
- SETUP > PANEL > Keyboard transparency at 25% or 50% lets the rest of the text show through the keyboard; the cursor stays on the 8 rows over it.

![A KST file saved with a comment](view:files-edit-kst)
![The question over unsaved changes](view:files-edit-unsaved)

## ARMED and logging

![FILES while the bench is ARMED](view:files-armed)

FILES writes only while the bench is SAFE and no run is being logged. During ARMED or logging, COPY, MOVE, RENAME, DELETE and NEW are dimmed. Browsing, reading files, INFO and RESCAN remain available, subject to the normal restrictions on locked files and files being written. The line above the buttons reads `Bench ARMED. The card is read-only.` or `A run is being logged. The card is read-only.` If either condition begins during a copy, the copy stops before its next 16KiB block.

![FILES while a test is logged](view:files-logging)

## The boxes

![The DELETE question](view:files-delete)
![A copy under way](view:files-copy)
![INFO on a file](view:files-info)
![INFO on a folder](view:files-info-folder)

A job lasting more than 0.25s shows a progress box. CANCEL stops a copy before its next 16KiB block. INFO displays the card's timestamp in the time zone set under SETUP > PANEL. A date in 1980 appears as `not known`: the clock was unset when the file was written ([Setup](setup.md#panel)).

## No card

![FILES with no card](view:files-nocard)

Without a card, both panes show `No card. Insert one and tap RESCAN.` Tap RESCAN after inserting, removing or replacing a card. Both panes then return to the root and User log folder.
