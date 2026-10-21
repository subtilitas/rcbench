# Interface language

[Deutsch](Language-de.md)

The panel shows its interface in English or in German. SETUP, PANEL,
Language picks it (ENGLISH, DEUTSCH). The choice is applied with the theme,
so the next frame is drawn in the new language; nothing restarts. It is
kept in NVS (non-volatile storage) like every setting, once SAVE takes it.

![SETUP in German](img/de/setup.png)

## What follows the language and what does not

| Follows the language | Stays as it is |
| --- | --- |
| labels, setting names and their help, alerts in the band, warning panels, refusal notes, the splash's details | the terms listed under [Terms that stay English](#terms-that-stay-english) |
| the servo test's TXT (plain text) report, in the language showing when the run starts | the servo test's CSV (comma-separated values) file: its header and the words in its rows (`test`, `phase`, `mode`), so tools read every run alike |
| the stick programmer's notes and refusals, and an ESC profile's manual steps where the profile gives them in German (`action_de`) | the ESC (electronic speed controller) profiles' names, items and values, which are the manuals' |
| | the programmer's parameter names and their values, which are the firmware's own, as its configurators show them |
| | the console log |

Current limitations:

- An alert already on the band keeps the language it was raised in until
  it is replaced, tapped away or cleared after 30 s (`UI_ALERT_SHOW_S`).
  The alert held for a touch controller that did not answer at start is
  raised in the language kept in NVS and stays until restart; without touch
  the language cannot change meanwhile.
- The message line of LOGS (a file that cannot be opened or deleted,
  a deleted file) keeps its language until the next file opened or deleted
  replaces or clears it.
- The title of a keypad or a choice list already open keeps its language
  until it closes.
- The stick engine reports a refusal in English, and the screen finds the
  translation by that English. A refusal the table does not know is shown in
  English. `test_text` checks that every refusal the compiled-in profiles
  produce has a German entry.

## Numbers

The screens, the TXT report and the CSV write the decimal point in every
language: 7.4 V, 0.05 A. It is the separator of the transmitter, the ESC
configurators and the supply's own display, and a CSV with a decimal comma
would need a different field separator. The German pages of this
documentation write the German decimal comma in their prose (7,4 V); a
string quoted from the screen keeps its point (`seit 1.5 s kein Messwert`).

## Terms that stay English

These are the words an operator meets on the transmitter, in an ESC
configurator, a manual or a datasheet, and the safety controls, which read
the same in every language. A translated ARM would not match the word in the
manual beside the bench.

- The safety and drive controls: ARM, DISARM, STOP, SWEEP, PAUSE. The SERVO
  rows are German where the word has a German equivalent: TRIMMUNG,
  UMKEHREN, PULS MITTE; the button that moves the servo to the centre reads
  ZENTRIEREN. HOLD stays
  English as the servo test's phase name. PAUSED, the sweep button of a
  paused sweep, is a state rather than the control and reads PAUSIERT;
  PAUSE, the control, stays English.
- The screen titles: MOTOR & ESC, SERVO, SUPPLY, RECEIVER, LOGS, SETUP,
  BATTERY, BALANCE, OUTPUTS, PICK A PIN, WI-FI, UPDATES, UPDATE,
  CAN BUS FAULT, LINK LOST, PANEL UPDATE. A tile and the band of the screen
  it opens carry the same word. A note that names a screen names it by its
  title: "der Seite SUPPLY".
- Protocol and mode names: DShot, PWM (pulse-width modulation), PPM
  (pulse-position modulation), OneShot, S.BUS, CAN (Controller Area
  Network), BLHeli_S, AM32, ESCape32, VESC, KISS, PD mini, AUTO, CV
  (constant voltage), CC (constant current), STANDARD PWM, HELI CYCLIC,
  ESC STICK, BUS OFF, and the status band's BENCH and SIM.
- The bench's state words, short enough for their place: SAFE and ARMED on
  the status band. ARM and DISARM are
  the buttons; SAFE and ARMED are the states.
- The field's own terms: Failsafe, Brown-out, Timing, Endpoint, Live,
  Online, Touch, Display, Pin, Slot, Slice, Pad, Resync, Bad tail, tx err,
  rx err, bus err, Frame, Upload, Build.
- Units, the stick positions MIN, MID and MAX, and the key legends DEL, CLR
  and OK.
- The vendor tool's names of a KST or Chaservo servo's settings, as the
  rows of the servo programmer: DEAD BAND, MAX. DUTY, BOOST, STRETCH, SOFT
  START, NEUTRAL, PULSE LOWER, PULSE UPPER, LEFT ANGLE, RIGHT ANGLE,
  REVERSION and the rows of its LOCKED tab; and PORT, BACKUP and Pairing.

## Glossary

One German word per English concept, on every screen and in the report.

| English | German |
| --- | --- |
| bench | Prüfstand |
| supply | Netzteil |
| output | Ausgang |
| set point | Sollwert |
| commanded pulse (SERVO, COMMANDED) | SOLL-PULSBREITE, like PULS MIN, PULS MITTE and PULS MAX; the supply's SET line is SOLL |
| input (of a supply), wiring (the PD mini's connections) | Eingang, Anschlüsse |
| current limit | Strombegrenzung |
| cap | Grenze (Obergrenze only for the upper end of a pair) |
| trip (the supply's own cut-off), current trip, voltage trip | Abschaltung (ABGESCHALTET on the MODE card), Überstrom, Überspannung |
| card | Karte |
| run | Lauf |
| step (of the supply) | Stufe |
| report | Bericht |
| reading | Messwert |
| setting (a SETUP entry) | Einstellung |
| SETTINGS (a screen's own settings) | OPTIONEN |
| save, reset, apply, close | speichern, zurücksetzen, übernehmen, schließen |
| cancel, abort | abbrechen |
| hold to … | … halten |
| refused | abgelehnt |
| screen (a page of the interface) | Seite |
| screen (the display) | Anzeige |
| IOmcu | IOmcu |
| emergency stop | Not-Aus |
| board | Platine |
| pack, cell, spread (between cells) | Akku, Zelle, Unterschied |
| peak | Spitze (max on a card) |
| idle current, holding current | Ruhestrom, Haltestrom |
| travel, travel time | Weg, Stellzeit |
| stall | blockieren |
| movement, dwell, settle | Bewegung, Haltezeit, Einschwingzeit |
| paused (the sweep button) | PAUSIERT |
| speed (of the servo test), range | Tempo, Bereich |
| device under test | Prüfling |
| pass, fail, aborted (a verdict) | bestanden, nicht bestanden, abgebrochen |
| FAULT (a state) | FEHLER |
| ON, OFF | EIN, AUS |
| beep | Piepton |
| item, value (of an ESC menu) | Punkt, Wert |
| entry (into an ESC menu), power-up | Einstieg, Einschalten |
| manual step (at the ESC), MANUAL INTERVENTION REQUIRED, the MANUAL tag | Handgriff, MANUELLER EINGRIFF NÖTIG, MANUELL |
| DONE (a manual step confirmed) | ERLEDIGT |
| threshold | Schwelle |
| defaults | Vorgaben |
| channel | Kanal |
| probe (of the CAN self-test) | Testframe |
| terminator, branch (of a bus) | Abschluss, Stichleitung |
| separator, row, column | Trennzeichen, Zeile, Spalte |
| Wi-Fi | WLAN |
| network, home network, network name, hidden network | Netzwerk, Heimnetz, Netzwerkname, verstecktes Netzwerk |
| forget (a saved network), unbind | löschen |
| access code | Zugangscode |
| upload, download | hochladen, herunterladen |
| firmware (a file), install, download (the panel update's hold) | Firmware, installieren, laden |
| plot, table | Grafik, Tabelle |
| simulated, modelled (computed by the panel) | simuliert |
| model (an aircraft, an ESC product) | Modell |
| protocol page (of the link) | Page |
| floor (of the current, stick programmer) | Grund |
| horn (of a servo) | Ruderhorn |
| start (a run) | starten, START on a button |
| surface (an output's role) | Ruder |
| connect, disconnect, read (a device), write | verbinden, trennen, auslesen, schreiben |
| refresh (a list), rescan | aktualisieren |
| restore (a backup to a servo) | zurückspielen |
| enable (the write to a servo) | freigeben |
| record (of an interrupted write) | Vermerk |
| store (a backup), stored | speichern, gespeichert |
| verify (a servo against what was read) | prüfen |
| locked (a value shown and not written) | gesperrt |
| speed, speed limit (of a programmed servo) | Tempo, Tempogrenze |
| link (to the IOmcu), NO LINK | Verbindung, KEINE VERBINDUNG |
| binding (a pin's role), bind | Zuordnung, zuordnen |
| frame rate (of a servo) | Pulsrate |
| throttle | Gas |
| access point (the panel's own Wi-Fi) | WLAN-Hotspot |
| servo programmer | Servoprogrammierung |
| log (a recording), log run | Aufzeichnung, Aufzeichnungslauf |
| IP address | IP-Adresse |
| acknowledge, confirm | bestätigen |
| SILENT (the RECEIVER's verdict) | KEIN SIGNAL |

A label is a noun where a noun will do. An instruction speaks to the
operator in the du-imperative (Prüfe, Tippe, Halte); no "Sie". An uppercase label
writes ß as SS (SCHLIESSEN, GRÖSSE); mixed-case text writes ß. The capital
ẞ (U+1E9E) is not in the fonts.

## The encoding

Every string is UTF-8 (Unicode Transformation Format, 8-bit), from the C
source to the TXT report on the card. `gfx` decodes it, and one code point
is one cell:

- The five text faces hold printable ASCII (American Standard Code for
  Information Interchange, 0x20 to 0x7E) and the seven German letters
  Ä Ö Ü ß ä ö ü (U+00C4, U+00D6, U+00DC, U+00DF, U+00E4, U+00F6, U+00FC),
  102 glyphs each. The two numeral faces hold digits and punctuation only.
- The 10x20, 12x24 and 20x36 faces have cap heights of 12, 15 and 23 px and
  draw every glyph inside the cell; `gen_font.py --check` fails otherwise.
  The 32x44 numeral face (cap height 40 px) cuts ink at the cell edge in six
  characters: `#` 5 px, `$` 16 px, `(` 12 px, `)` 12 px, `,` 23 px and `/`
  6 px (pixels of coverage 128 or more outside the cell). `gen_font.py
  --check` prints these counts.
- A code point a face does not hold draws as `?`. A byte that does not start
  a well-formed sequence, a sequence cut short and an overlong form are one
  `?` each, and the decoder never reads past the terminator.
- A width is cells times the cell width: `gfx_text_width()` and
  `gfx_text_cells()` count code points, and `gfx_text_prefix()` gives the
  byte length of the first n cells, so a cut falls between characters.
- A buffer is bytes, and a German letter takes two of them. The alert band
  holds `UI_ALERT_MAX`, 128 bytes with the terminator.

UTF-8 rather than a single-byte code page: the tables stay readable in the
source, and the report opens correctly in any editor on a PC. The cost is
that no width may be taken from `strlen()`; the screens measure with the
functions above.

## The tables

Every text the interface shows or writes in the operator's language is one
entry of `shared/text/include/ui_text.def`, with its English and its German
side by side, each on a line of its own:

```c
UI_TEXT(PL_GRP_PROG_PORT, 40,
    "PROG PORT SERVOS",
    "SERVOS AM PROG-PORT")
```

| Entry | Holds |
| --- | --- |
| `UI_TEXT(ID, cells, en, de)` | a screen's string, `TR(ID)`, and the cells its field has where no screenshot shows it (0 where `--fit` measures it) |
| `UI_SETTING_LABEL(SET_..., en, de)` | a setting's label |
| `UI_SETTING_HELP(SET_..., en, de)` | a setting's help line |
| `UI_SETTING_CATEGORY(SET_CAT_..., en, de)` | a category's name |
| `UI_OPTIONS(name)`, `UI_OPTION(en, de)`, `UI_OPTIONS_END(name)` | an enum setting's options, one row each in order |
| `UI_SERVO_TEXT(SERVO_STR_..., en, de)` | the servo test's words and its TXT report |
| `UI_WEB_TEXT(P_..., en, de)` | the panel's Wi-Fi page |

`NULL` in the German column shows the English: the servo test's entries
whose English is empty, and the options that are the same word in every
language (the language names, addresses, channels, baud rates, `STABLE` and
`TESTING`, `PULL-UP` and `PRECHARGE`). A translated format converts the
same arguments in the same order as its English.

The table stays one file, listed in `tools/check_sizes.txt`. A file that
reads it defines the macros it needs and includes it; the table defines the
others as empty and undefines all of them at its end. Only `UI_TEXT`
entries stand under a condition, `#ifdef RCBENCH_TOUCH_INJECTOR`, and the
define comes with the include path of `shared/text`, so every file that
reads the table sees the same entries; `test/tools/test_merge_texts.py`
fails on any other entry under a condition.

| File | Reads |
| --- | --- |
| `shared/ui/include/ui_text.h` | the `TX_` IDs |
| `shared/ui/ui_text.c` | the English and the cells of the screens' strings, every other language's tables, and the lookup: `TR(ID)`, `ui_setting_label()`, `ui_servo_str()` and the rest |
| `shared/settings/settings_text.c`, `settings.c`, `settings_opts.h` | the settings' English: labels, help lines, options, categories |
| `shared/servo/servo_str.c` | the servo test's English, which the CSV also uses |
| `shared/webaccess/wa_page.c` | the Wi-Fi page's English and German |

The five text faces hold printable ASCII (0x20 to 0x7E) and the 7 German
letters Ä Ö Ü ä ö ü ß. Any other code point draws as `?`, and so does a
byte that starts no valid UTF-8 sequence.
`every_drawn_string_has_its_glyphs` in `test_text` holds every string of
the table the panel draws, in every language, to that set and to valid
UTF-8. The Wi-Fi page is drawn by the browser and is not held to it; a
`NULL` German entry shows its English there as well (`test_wa_page_de`).

## Adding a text

1. Add an entry in the section of the screen that draws it, English first:
   `UI_TEXT(ID, cells, "English", "Deutsch")`. `cells` is the field's width
   in cells of its face where no screenshot shows the string, and 0 where a
   rendered view does.
2. Draw it with `TR(ID)`. A setting's label and help line are a
   `UI_SETTING_LABEL` and a `UI_SETTING_HELP` entry for its `SET_` ID; an
   option list is a `UI_OPTIONS` block, bound to its setting by
   `ENUM_OPTS(name)` in `shared/settings/settings.c`.
3. Run the host suite, `python3 tools/render_ui.py --fit`,
   `python3 tools/check_formats.py` and `python3 tools/check_docs.py`.
   `test_text` fails on an entry without German, on a German format that
   converts other arguments than its English, on a string wider than its
   declared cells, on a letter the fonts lack, on a byte that is not valid
   UTF-8 and on a `SERVO_STR_` ID whose English is empty where it is not
   meant to be. `servo_str.c` does not build when the count of
   `UI_SERVO_TEXT` entries differs from `SERVO_STR_COUNT`.

A branch that still holds the per-file form of the texts (English in
`shared/ui/include/ui_text.def` and in the settings schema, German in
`shared/ui/ui_text_de.c`) merges with `tools/merge_texts.py`:

```bash
git merge --no-commit origin/main
python3 tools/merge_texts.py merge "$(git merge-base HEAD origin/main)" origin/main
```

It converts both sides of the merge and carries the branch's text changes
into the table by a three-way merge; a change both sides made to the same
lines is left with conflict markers.

## The fit checks

```bash
python3 tools/render_ui.py --fit
```

builds the renderer with `GFX_TEXT_TRACE`, draws all 283 views in English and
in German, and fails when a string in either language

- is wider than the box `gfx_text_in()` was given,
- is cut at the edge of the area it is drawn in,
- runs past the filled shape it is printed on,
- overlaps another string the frame shows, or
- is painted over by a fill drawn after it and not drawn again.
  Only the characters count: a fill over the trailing spaces of a padded
  label is not a finding. OUTPUTS with its protocol list open
  (`outputs-protocol`) is exempt, because the list covers the pins beside
  it by design.

An English overflow that is accepted for a time goes into `KNOWN_OVERFLOWS`
in the tool, with its view, its string and its measurement; the list is
empty. A listed one is printed as `known` and does not fail; a German finding that equals one word for word is
the same string in the same place and does not fail either. An entry the
check no longer finds fails until it is taken off the list. The check also
fails on a string with no declared width that no view draws.

```bash
python3 tools/check_formats.py
```

compiles `shared/` with every `TR()` call and every report word replaced
by its English literal, under `-Wformat=2 -Wformat-nonliteral
-Wformat-signedness`, and fails on any warning: each English format
matches the arguments its call passes. The panel's firmware in
`firmware/panel/main/` is not in `shared/` and not covered.

`test_text` covers the rest: every ID has German, every German format
converts what its English does, every string with a declared width fits it
in every language, alerts and splash details fit their buffers, translated
settings fit SETUP and the TIMING page, and the report's columns and labels
line up.

`tools/check_docs.py` fails when a German wiki page quotes in backticks the
English of a string the screen shows in German: an interface string, a
format's output, with numbers or text where its conversions are, a setting's label, help,
option or category, or a servo test word, a quote wrapped over two lines
included. Prose without backticks is not checked: it also names protocol
pages, supply commands and board markings that share a word with a label.

## Adding a language

A language is one more column in every entry of the table: 2,057 entries
with an ID and 64 options, 10 of them with an empty English.

1. Add the column to every entry of `shared/text/include/ui_text.def`, and
   the parameter to every macro that reads a column: 7 empty defaults at the
   head of the table and 20 `#define` lines in 7 files (`ui_text.h`,
   `ui_text.c`, `settings.c`, `settings_text.c`, `settings_opts.h`,
   `servo_str.c`, `wa_page.c`). `NULL` shows the English.
2. Add `UI_LANG_xx` to `ui_lang_t` in `shared/ui/include/ui_text.h`, and the
   language's own name to the `language` option list, in the same position.
3. Copy the German block in `shared/ui/ui_text.c` (`DE()`, its tables and
   `ui_lang_de`) for the new column, add `ui_lang_xx` to `k_langs` and its
   declaration to `ui_text.h`.
4. A letter the text faces do not hold goes into the list in `FONTS` in
   `tools/gen_font.py`, and `python3 tools/gen_font.py` regenerates them.
   Each letter takes one byte a pixel in each of the five text faces: 128,
   200, 288, 448 and 720 bytes in the 8x16, 10x20, 12x24, 16x28 and 20x36
   faces, 1,784 bytes in all.
5. Add the language to `LANGS` in `tools/render_ui.py` and to the argument
   `test/host/render_screen.c` takes, and run
   `python3 tools/render_ui.py --fit` and `python3 tools/render_ui.py`.
6. Extend `german_has_every_string` in `test/host/test_text.c` to the new
   table, write its glossary here, and translate this page.

Two readers take the German column by position: `TABLE_ENTRY` in
`tools/check_docs.py` and the servo controls' check in
`configurator/core/test/help.test.ts`. The Wi-Fi page chooses its language
by a flag, `wifi_access_set_german()`, and an ESC profile carries its
manual steps in English and in German (`action`, `action_de`); neither
takes a third language without a change of its own.
