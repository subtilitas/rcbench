# Interface language

[Deutsch](Language-de.md)

The panel shows its interface in English or in German. SETUP, APPLICATION,
Language picks it (ENGLISH, DEUTSCH). The choice is applied with the theme,
so the next frame is drawn in the new language; nothing restarts. It is
kept in NVS (non-volatile storage) like every setting, once SAVE takes it.

![SETUP in German](img/de/setup.png)

## What follows the language and what does not

| Follows the language | Stays as it is |
| --- | --- |
| labels, setting names and their help, alerts in the band, warning panels, refusal notes, the splash's details | the terms listed under [Terms that stay English](#terms-that-stay-english) |
| the servo test's TXT (plain text) report, in the language showing when the run starts | the servo test's CSV (comma-separated values) file: its header and the words in its rows (`test`, `phase`, `mode`), so tools read every run alike |
| the stick programmer's notes and refusals | the ESC (electronic speed controller) profiles' names, items and values, which are the manuals' |
| | the programmer's parameter names and their values, which are the firmware's own, as its configurators show them |
| | the console log |

Current limitations:

- An alert already on the band keeps the language it was raised in until
  it is replaced, tapped away or cleared after 30 s (`UI_ALERT_SHOW_S`).
  The alert held for a touch controller that did not answer at start is
  raised in the language kept in NVS and stays until restart; without touch
  the language cannot change meanwhile.
- The message line of LOG VIEWER (a file that cannot be opened or deleted,
  a deleted file) keeps its language until the next file opened or deleted
  replaces or clears it.
- The title of a keypad or a choice list already open keeps its language
  until it closes.
- The stick engine reports a refusal in English, and the screen finds the
  translation by that English. A refusal the table does not know is shown in
  English. `test_text` checks that every refusal the compiled-in profiles
  produce has a German entry.
- Two English help lines on SETUP are longer than the 36 cells the row
  shows and are cut there: Capacity's and Rated kV's. `render_ui.py --fit`
  lists them as notes.

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

- The safety and drive controls: ARM, DISARM, ARMED, DISARMED, STOP, SWEEP,
  HOLD, CENTRE, TRIM, REVERSE.
- The screen titles: MOTOR & ESC, SERVO, SUPPLY, ANALYSER, LOGS, SETUP,
  SETTINGS, BATTERY, BALANCE, PROGRAMMER, OUTPUTS, PICK A PIN, LOG VIEWER,
  CAN BUS FAULT, LINK LOST. A note that names a screen names it by its
  title: "der Seite SUPPLY".
- Protocol and mode names: DShot, PWM (pulse-width modulation), PPM
  (pulse-position modulation), OneShot, S.BUS, CAN (Controller Area
  Network), BLHeli_S, AM32, ESCape32, VESC, KISS, PD mini, AUTO, CV
  (constant voltage), CC (constant current), STANDARD PWM, HELI CYCLIC,
  ESC STICK, BUS OFF.
- Two state words short enough for their place: SAFE on the status band and
  SILENT on the ANALYSER's verdict.
- The field's own terms: Frame Rate, Throttle, Failsafe, Brown-out, Timing,
  Endpoint, Link, Live, Online, Touch, Display, Pin, Slot, Pad, Resync,
  Bad tail, tx err, rx err, bus err, Frame.
- Units, the stick positions MIN, MID and MAX, and the key legends DEL, CLR
  and OK.

## Glossary

One German word per English concept, on every screen and in the report.

| English | German |
| --- | --- |
| bench | Prüfstand |
| supply | Netzteil |
| output | Ausgang |
| set point | Sollwert |
| current limit | Strombegrenzung |
| cap | Obergrenze |
| trip (the supply's own cut-off), current trip, voltage trip | Abschaltung (ABSCH. on the MODE card), Überstrom, Überspannung |
| card | Karte |
| run | Lauf |
| step (of the supply) | Stufe |
| report | Bericht |
| reading | Messwert |
| setting (a SETUP entry) | Einstellung |
| SETTINGS (a screen's own overlay) | OPTIONEN |
| save, reset, apply, close | speichern, zurücksetzen, übernehmen, schließen |
| cancel, abort | abbrechen |
| hold to … | … halten |
| refused | abgelehnt |
| screen (a page of the interface) | Seite |
| screen (the display) | Bildschirm |
| coprocessor | Koprozessor |
| board | Platine |
| pack, cell, spread | Akku, Zelle, Streuung |
| peak | Spitze (max on a card) |
| idle current, holding current | Ruhestrom, Haltestrom |
| travel, travel time | Weg, Stellzeit |
| stall | blockieren |
| movement, dwell, settle | Bewegung, Verweilen, Einschwingen |
| speed (of the servo test), range | Tempo, Bereich |
| device under test | Prüfling |
| pass, fail, aborted (a verdict) | bestanden, nicht bestanden, abgebrochen |
| FAULT (a state) | FEHLER |
| ON, OFF | EIN, AUS |
| beep | Piepton |
| item, value (of an ESC menu) | Punkt, Wert |
| entry (into an ESC menu), power-up | Einstieg, Einschalten |
| threshold | Schwelle |
| defaults | Vorgaben |
| channel | Kanal |
| probe (of the CAN self-test) | Testframe |
| terminator, branch (of a bus) | Abschluss, Stichleitung |
| separator, row, column | Trenner, Zeile, Spalte |
| plot, table | Grafik, Tabelle |
| simulated, modelled (computed by the panel) | simuliert |
| model (an aircraft, an ESC product) | Modell |
| protocol page (of the link) | Page |
| floor (of the current, stick programmer) | Grund |
| horn (of a servo) | Ruderhorn |
| start (a run) | starten, START on a button |
| surface (an output's role) | Ruder |
| connect, disconnect, read, write | verbinden, trennen, lesen, schreiben |

The register is an instrument's: a noun where a label will do, the
imperative where an instruction is needed, no "Sie". An uppercase label
writes ß as SS (SCHLIESSEN, GRÖSSE); mixed-case text writes ß. The capital
ẞ (U+1E9E) is not in the fonts.

## The encoding

Every string is UTF-8 (Unicode Transformation Format, 8-bit), from the C
source to the TXT report on the card. `gfx` decodes it, and one code point
is one cell:

- The two text faces hold printable ASCII (American Standard Code for
  Information Interchange, 0x20 to 0x7E) and the seven German letters
  Ä Ö Ü ß ä ö ü (U+00C4, U+00D6, U+00DC, U+00DF, U+00E4, U+00F6, U+00FC),
  102 glyphs each. The numeral face holds digits and punctuation only.
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

| File | Holds |
| --- | --- |
| `shared/ui/include/ui_text.def` | every translated string: its ID (identifier), the cells its field has where no screenshot shows it, and its English |
| `shared/ui/ui_text_de.c` | the German, by ID; the settings' labels, help, options and categories; the servo test's words and the report |
| `shared/ui/ui_text.c` | the lookup: `TR(ID)`, `ui_setting_label()`, `ui_servo_str()` and the rest |

An entry a language leaves `NULL` shows the English, so a partial table
works. A translated format converts the same arguments in the same order as
its English.

## The fit checks

```bash
python3 tools/render_ui.py --fit
```

builds the renderer with `GFX_TEXT_TRACE`, draws all 54 views in English and
in German, and fails when a German string

- is wider than the box `gfx_text_in()` was given,
- is cut at the edge of the area it is drawn in,
- runs past the filled shape it is printed on, or
- overlaps another string the frame shows.

English findings are listed as notes: English is the layout the screens were
drawn for. A German finding English shares word for word is the layout's,
not the translation's, and is a note too. The check also fails on a string
with no declared width that no view draws.

```bash
python3 tools/check_formats.py
```

compiles `shared/` with every `TR()` call and every report word replaced
by its English literal, under `-Wformat=2 -Wformat-nonliteral
-Wformat-signedness`, and fails on any warning: each English format
matches the arguments its call passes. The panel's `main.c` is not in
`shared/` and not covered.

`test_text` covers the rest: every ID has German, every German format
converts what its English does, every string with a declared width fits it
in every language, alerts and splash details fit their buffers, translated
settings fit SETUP and the TIMING page, and the report's columns and labels
line up.

`tools/check_docs.py` fails when a German wiki page quotes in backticks the
English of a string the screen shows in German. Prose without backticks is
not checked: it also names protocol pages, supply commands and board
markings that share a word with a label.

## Adding a language

1. Add `UI_LANG_xx` to `ui_lang_t` in `shared/ui/include/ui_text.h`, and the
   language's own name to `k_language` in `shared/settings/settings.c`, in
   the same position.
2. Copy `shared/ui/ui_text_de.c` to `ui_text_xx.c`, translate it, and add
   `ui_lang_xx` to `k_langs` in `shared/ui/ui_text.c` and its declaration to
   `ui_text.h`.
3. Add the file to `shared/ui/CMakeLists.txt` and to `SOURCES` in
   `tools/render_ui.py` and `tools/frame_cost.py`.
4. A letter the fonts do not hold goes into the list in `FONTS` in
   `tools/gen_font.py`, and `python3 tools/gen_font.py` regenerates them.
5. Add the language to `LANGS` in `tools/render_ui.py` and to the argument
   `test/host/render_screen.c` takes, and run
   `python3 tools/render_ui.py --fit` and `python3 tools/render_ui.py`.
6. Extend `german_has_every_string` in `test/host/test_text.c` to the new
   table, write its glossary here, and translate this page.
