---
screen: none
title: Settings backup
order: 61
---

# Settings backup

The panel keeps its settings in NVS (non-volatile storage) on its own flash. A full flash erases them, and the bindings of the Dial, the nodes and the IMU with them. The settings backup keeps copies on the card, in `BENCH/SETUP`, as text files a person reads and edits.

## Save settings and Load settings

The last two rows of [SETUP > SD CARD](setup.md#sd-card):

![SD CARD at the end of its list](view:setup-card-backup)

| Row | What a tap does |
|---|---|
| Save settings | writes the saved settings as `BENCH/SETUP/SETTINGS.TXT`, replacing the one there. The row reads `WRITING`, `WRITTEN`, `CARD FULL` or `FAILED, CODE n`. It takes no tap without a card, and none while SAVE is offered: the file holds what the panel keeps, not an edit. |
| Load settings | opens BACKUP. It takes no tap while ARMED. |

## The automatic copies

Each time the panel writes its settings to NVS with a card in, it also writes a copy: `AUTO1.TXT` is the newest, `AUTO2.TXT` and `AUTO3.TXT` the two before it. A copy is written whole under `AUTO.TMP` first, and only then do the others move on, so a full card leaves the copies as they were. A copy that does not reach the card shows once in the alert band, `Settings copy not written: the card is full.` or `Settings copy not written: card code n.`; the next failure shows again after a copy that worked. A failed copy is no error of the bench: outputs, supply and ARMED stay as they are.

## BACKUP

![BACKUP with the newest copy chosen](view:setup-backup)

- Each file shows its date and the panel version that wrote it. `date not known`: the panel's clock was not set; the panel sets it only from the network.
- Tap a file to choose it. HOLD 2s TO LOAD loads it; a release before 2s shows `Hold 2s to load the settings file.` The hold takes no press while ARMED.
- BACK or the home tag returns to SD CARD.

Loading replaces every setting: the file's value, or the default where the file has none. The values take effect at once and are written to NVS when the bench is idle. The answer lists each line the panel refused, with its line number, key, code and reason:

![A hand-edited file loaded, three lines refused](view:setup-backup-loaded)

| Code | Reason |
|---|---|
| 1 | not key = value |
| 2 | unknown key |
| 3 | key given twice: the first line counts |
| 4 | value not readable: not a number, no option, not ON or OFF, a string without quotes |
| 5 | out of range, or a pin the setting does not take |
| 6 | text too long: over 23 bytes |
| 7 | follows another: `sns_scl` follows Sensor SDA |
| 8 | in use, kept |

A file without `end` as its last line is cut short and loads nothing: `refused: code 6`. A file of a newer format reads code 5.

## The offer at start

A panel that starts with nothing but the defaults reads the card's newest file. If it holds other settings, BACKUP opens over the [menu](overview.md) once the bench is SAFE, with that file chosen. HOLD 2s TO LOAD loads it; SKIP or the home tag returns to the menu. No automatic copy is written before the offer is answered.

![The offer at start](view:setup-backup-offer)

## Not in the file

The Wi-Fi home network and its password, HOTSPOT PASSWORD and ACCESS CODE stay out: a card is read on every computer it goes into, and a loaded file must not switch the access code off. Set them again on [Wi-Fi](wifi.md) after a full flash.

## Editing a file

| Line | What it is |
|---|---|
| `rcbench settings, format 1` | the first line: the file's format |
| `Panel: 0.23.15` | the panel version that wrote it |
| `Written: 2026-11-21 14:05:33 UTC` | its date, `not known` without a set clock |
| `pack_cells = 6  # Cells, 1 to 14S` | a setting: key, value, and a comment with its name and range |
| `theme = DARK  # Theme: DARK, LIGHT` | a choice, by the English text of its option |
| `srv_dut = "SERVO"` | a string, in double quotes |
| `end` | the last line |

One line a setting, `<key> = <value>`; `#` starts a comment. A number as written, a switch `ON` or `OFF`, a choice the English text of its option, a string in double quotes. Keep the last line, `end`.

## Related pages

- [Setup](setup.md)
- [Wi-Fi](wifi.md)
- [Files](files.md)
