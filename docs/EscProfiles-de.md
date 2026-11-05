# ESC-Programmierprofile

[English](EscProfiles.md)

Ein ESC-Profil beschreibt die Programmierung einer Familie elektronischer
Drehzahlregler (ESC, Electronic Speed Controller) über ihr Gasknüppel-Menü.
Es legt den Einstieg ins Menü, die Pieptonfolgen zur Ausgabe von Zahlen
sowie die verfügbaren Menüpunkte und Werte fest. Die
[Stick-Programmierung](StickProgramming-de.md) verwendet diese Daten: Du
wählst einen ESC und die zu ändernden Werte; der Prüfstand steuert das Gas
und zählt die Pieptöne im Versorgungsstrom. Er kann 24 der 72 Profile
ausführen.

## Was im Satz steht

| | Anzahl |
| --- | --- |
| Profile (Familien) | 72 |
| Modelle | 451 |
| Marken | 20 |
| Menüpunkte | 360 |

| Wer ein Profil ausführen kann | Profile | Modelle |
| --- | --- | --- |
| Der Prüfstand allein: Gassignal und Versorgung | 44 | 340 |
| Ein Mensch setzt einen Jumper, drückt einen Taster oder liest eine LED | 26 | 108 |
| davon mit den Schritten in `manual` | 24 | 88 |
| Niemand: das Handbuch nennt kein brauchbares Verfahren | 2 | 3 |

Jedes Profil ist `"verified": false`. Die Profile stammen aus 153
Handbüchern und sind ohne ESC am Prüfstand entstanden. Kein Handbuch nennt
eine Pieptondauer, eine Pause oder eine Schleifenzeit in Millisekunden. Die
zugehörigen Zeitfelder bleiben deshalb null, bis eine Aufzeichnung am
Prüfstand Messwerte liefert.

## Wo Profile liegen

| Ablage | Pfad | Gelesen |
| --- | --- | --- |
| Eingebaut | `shared/esc/profiles/*.json`, in das Panel-Image übersetzt | immer |
| SD-Karte | `/BENCH/ESC/CONF/*.json` | einmal, beim Start |

Eine Datei auf der Karte trägt ihre `id` als Namen: `/BENCH/ESC/CONF/hobbywing-flyfun-8item.json`
enthält `"id": "hobbywing-flyfun-8item"`. Groß- und Kleinschreibung dürfen
abweichen; andere Unterschiede werden abgelehnt. Eine Datei mit der `id`
eines eingebauten Profils ersetzt dieses Profil. Eine neue `id` fügt ein
Profil hinter den eingebauten an. Eine Datei wird abgelehnt, wenn dadurch
für einen Hersteller mehr als 512 Modelle (`ESC_MAKER_MODELS_MAX`) geladen
würden. Dabei zählen seine eingebauten Profile und die Profile von der
Karte zusammen. Dateinamen ab 64 Zeichen werden nicht gelesen. Das Panel
lädt höchstens 32 Kartenprofile und liest die Namen der ersten 64
`.json`-Dateien in `/BENCH/ESC/CONF/`. Abgelehnte Dateien erscheinen auf der
Konsole mit dem Grund, zum Beispiel:

```
ESC profiles: MYESC.JSON refused: items[2].values: not 1-255 entries
```

Abgelehnt wird eine Datei, die:

- größer als 64KiB ist, kein JSON ist oder kein UTF-8 ist;
- tiefer als 16 Ebenen unter dem obersten Objekt verschachtelt ist oder ein
  Objekt mit mehr als 64 Einträgen hat;
- in einem Objekt einen Schlüssel zweimal hat, verglichen nach dem Dekodieren
  (`"sch\u0065ma"` ist `"schema"`);
- in einem String `\u0000` oder ein halbes Surrogat-Paar enthält;
- anders heißt als ihre id;
- eine Regel unten verletzt.

Der Generator wendet dieselben Regeln auf die Dateien im Repository an.
Das Panel läuft auch ohne Karte; dann stehen nur die eingebauten Profile
zur Verfügung.

## Die Datei

Ein JSON-Objekt je Datei. `id` ist der Dateiname ohne `.json`, 1 bis 48
Zeichen aus `a-z 0-9 -`.

```json
{
  "schema": 1,
  "id": "hobbywing-flyfun-8item",
  "brand": "Hobbywing",
  "family": "FlyFun 6A-100A sensorless, 8-item menu",
  "verified": false,
  "automatable": "full",
  "automatable_note": "",
  "sources": [{"file": "...pdf", "pages": "3-4", "url": "https://..."}],
  "models": [...],
  "scheme": {...},
  "items": [...],
  "unknowns": ["Beep duration in ms is not stated."],
  "notes": []
}
```

`sources`, `unknowns`, `notes` und jedes `description`-Feld bleiben im
JSON. Das Panel lädt sie nicht.

### Felder, die das Panel liest

| Feld | Werte |
| --- | --- |
| `automatable` | `full`, `assisted`, `none`; alles außer `full` braucht `automatable_note` |
| `manual` | die Handgriffe eines `assisted`-Profils am ESC; siehe [Handgriffe](#handgriffe) |
| `scheme.type` | `count`, `short_long`, `melody_groups`, `yes_no`, `stick_position`, `other` |
| `scheme.entry.throttle` | `min`, `mid`, `max`: die Knüppelstellung, die das Menü öffnet |
| `scheme.entry.when` | `before_power_on`, `after_power_on` |
| `scheme.entry.hold_ms` | 0 bis 600000, oder null, wenn nicht angegeben |
| `scheme.entry.steps` | 1 bis 255 Bedienschritte, in Reihenfolge |
| `scheme.announce.what` | `item`, `value`, `item_then_value` |
| `scheme.announce.encoding` | `count`, `short_long`, `melody`, `yes_no` |
| `scheme.announce.long_equals_short` | kurze Pieptöne je langem; Pflicht bei `short_long` |
| `scheme.announce.beep_ms`, `gap_ms`, `group_gap_ms` | 0 bis 60000, oder null |
| `scheme.announce.repeat` | 0 bis 255: 0 wiederholt bis zur Auswahl, null unbekannt |
| `scheme.select.throttle`, `scheme.skip.throttle` | `min`, `mid`, `max`, `none` |
| `scheme.listen` | Knüppelstellung während der Pieptonfolge des Menüs: `{"throttle": "min"}`, `mid` oder `max`. Fehlt es oder ist es null, bleibt die Knüppelstellung vom Einstieg erhalten. Gesetzt in den 9 Profilen für YGE-ESCs, deren Menü nach einem Einstieg bei Maximum mit dem Knüppel auf Minimum läuft; die Stick-Programmierung führt ein solches Profil nur aus, wenn `hold_ms` die Länge des Einstiegs nennt |
| `scheme.store` | die Bewegung, die eine Auswahl speichert, nachdem der ESC sie beantwortet hat: `{"throttle": "min"}`, `mid` oder `max`. Fehlt es oder ist es null, speichert bereits die Auswahlbewegung den Wert. Gesetzt in den 8 Mode-Setups der YGE-ESCs, wo der Knüppel zurück auf Minimum den Modus speichert |
| `scheme.select.within_ms` | 0 bis 60000: die Zeit nach dem Ton, in der die Bewegung zählt, oder null, wenn nicht angegeben |
| `scheme.value_select` | nur bei zweistufigen Menüs: `select` wählt den Punkt, dann speichert `value_select.throttle` (`min`, `mid`, `max`, `none`) den angesagten Wert; `within_ms` wie bei `select`. Fehlt es oder ist es null, speichert die `select`-Bewegung den Wert |
| `scheme.changes_per_entry` | `one`, `many` |

### Modelle

| Feld | Werte |
| --- | --- |
| `name` | eindeutig innerhalb des Profils |
| `cells_min`, `cells_max` | 0 bis 255, oder null |
| `cell_type` | `lipo`, `nimh`: was `cells_*` zählen |
| `v_max_mv` | höchste Eingangsspannung in mV, oder null |
| `v_min_mv` | niedrigste Eingangsspannung in mV, oder null; nicht über `v_max_mv` |
| `current_a` | Dauerstrom in A, 0 bis 65535, oder null |

### Menüpunkte

| Feld | Werte |
| --- | --- |
| `number` | 1 bis 255, wie der ESC sie ausgibt |
| `name` | wie das Handbuch ihn nennt |
| `key` | 1 bis 32 aus `a-z 0-9 _`; eine Bedeutung über alle Marken: `brake`, `timing`, `cutoff_voltage`, `cutoff_type`, `battery_type`, `cell_count`, `startup`, `governor`, `direction`, `throttle_range`, `pwm_freq`, `aircraft_type`, `mode`, `reset` |
| `values` | 1 bis 255 aus `{"number": 0-255, "name": "...", "default": true}`; Nummern eindeutig, höchstens ein Standardwert |
| `values[].entry_throttle` | `min`, `mid`, `max`: die laut Handbuch für diesen Wert erforderliche Knüppelstellung, falls sie von der Einstiegsstellung abweicht. Fehlt sie oder ist sie null, gilt die Einstiegsstellung. Die Stick-Programmierung schaltet den ESC für diesen Wert in dieser Stellung ein |
| `values[].entry_hold_ms` | 0 bis 600000: Wartezeit vom Einschalten bis zum Menü für diesen Wert, falls das Handbuch von `scheme.entry.hold_ms` abweicht. Fehlt sie oder ist sie null, gilt die Wartezeit des Einstiegs. Kontronik SUN PLUS Modi 4 bis 6 warten 5000ms |
| `values[].after_select` | 1 bis 4 Stellungen aus `min`, `mid`, `max`: die nach der Auswahl dieses Werts erforderlichen Bewegungen. Sie laufen der Reihe nach, jeweils nachdem der ESC die vorige Bewegung bestätigt hat. Fehlt das Feld oder ist es null, gibt es keine weiteren Bewegungen. Kontronik-Car-Modi und PIX Modus 2 gehen auf `min`, die Bremse. Jeder Eintrag muss die Knüppelstellung ändern. Der erste muss von der vorherigen Stellung aus `scheme.store` abweichen; ohne Speicherbewegung gilt die Auswahlstellung (oder `value_select`). Weitere Einträge müssen sich jeweils vom vorigen unterscheiden. Eine Datei, die den Knüppel stehen lässt, wird abgelehnt. Die Stick-Programmierung führt diese SPEICHERN-Bewegungen nach der Bewegung aus `scheme.store` aus. Zweistufige Profile erlauben dieses Feld nicht |
| `applies_to` | Modellnamen dieses Profils, oder null für alle. Die Stick-Programmierung bietet den Punkt nur an diesen Modellen an |
| `applies_when` | eine Bedingung in Worten, z. B. `"model type heli"` |

Zwei Menüpunkte dürfen dieselbe Nummer nur tragen, wenn beide `applies_to`
oder `applies_when` haben.

### Handgriffe

`manual` listet die Handgriffe am ESC, die zusätzlich zur Gassteuerung und
Stromversorgung nötig sind, in ihrer Ausführungsreihenfolge. Fehlt das Feld
oder ist es null, sind keine Handgriffe hinterlegt.

```json
"manual": [
  {"when": "before_power", "action": "Fit the jumper on any 2 of the 3 programming contacts.",
   "source": "Kontronik_Beat.pdf p.5"},
  {"when": "before_menu", "action": "Pull the jumper off after 2s or the tone sequence."}
]
```

| Feld | Werte |
| --- | --- |
| `manual` | 1 bis 4 Schritte; nur in einem Profil mit `automatable: "assisted"` |
| `when` | `before_power`, `at_power_up`, `before_menu`, `during_menu`, `before_power_off`, `after_programming`; kein Schritt früher als der darüber |
| `action` | 1 bis 120 Byte UTF-8, englisch: zwei Zeilen des Pop-ups |
| `action_de` | derselbe Schritt auf Deutsch, 1 bis 120 Byte UTF-8, Umlaute eingeschlossen; fehlt es oder ist null, erscheint auch auf Deutsch das Englische |
| `starts_menu` | `true`, wo die Handlung selbst die Folge des Menüs startet, wie ein abgezogener Jumper oder ein gedrückter Taster bei Kontronik; fehlt es, null oder `false` sonst. Nur an einem Schritt `before_menu`, höchstens einer je Profil, und kein Schritt `before_menu` danach. Wo `scheme.listen` gesetzt ist, gleicht es der Einstiegsstellung und jedem `entry_throttle` der Werte: das Menü ruht, wo nach dem Schritt gefragt wird. Die Stick-Programmierung hört ab dem Moment zu, in dem sie nach diesem Schritt fragt ([Stick-Programmierung](StickProgramming-de.md#handgriffe)) |
| `locks` | `true` an einem Schritt `before_power_off`, wo das Handbuch sagt, dass der ESC sich sperrt, wenn seine Versorgung vor dem Schritt ausgeht, wie bei Kontronik KOBY, JIVE Pro, KOLIBRI, KONTROL-X und KOSMIK; fehlt es, null oder `false` sonst, und nur an dieser Art. Die Stick-Programmierung sagt dann, dass der ESC gesperrt sein kann, nicht nur, dass der Wert womöglich nicht gespeichert ist |
| `hold_ms` | nur `at_power_up`: 0 bis 60000, wie lange der Schritt nach dem Einschalten gehalten wird; fehlt oder null, wo nicht angegeben |

`source` und jedes andere Feld eines Schritts bleiben im JSON. Der Text
der Handlung stammt aus dem Profil. Er erscheint auf Deutsch, wenn das
Profil `action_de` enthält und die Oberfläche auf Deutsch eingestellt ist,
sonst auf Englisch. Alle hinterlegten Schritte haben eine deutsche
Fassung. Die Anzeige zeigt `when` in der Sprache der Oberfläche.
Was ein Lauf mit jedem Schritt tut, steht unter
[Stick-Programmierung](StickProgramming-de.md#handgriffe).

24 Profile haben Schritte: die 22 Familien der Kontronik-ESCs,
`turnigy-aquastar` (sein Schalter bei Vollgas) und `greatplanes-electrifly-c-series` (sein
Ein/Aus-Taster). `graupner-brushless-control-t`, dessen Menü an LEDs
abgelesen wird, und `hacker-master-senstrol`, das den
Identifikationschip seines Motors, einen zweiten Kanal und eine JetiBox
braucht, sind als `assisted` ohne Schritte hinterlegt. Ihre Anforderungen
lassen sich nicht als Aufforderung zu einem einzelnen Handgriff an einem
bestimmten Punkt des Ablaufs abbilden.

## Ein Profil hinzufügen oder korrigieren

Für einen einzelnen Prüfstand: Kopiere die Datei nach `/BENCH/ESC/CONF/`
auf die Karte und starte das Panel neu. Lege den Ordner am Computer an; das
Panel liest ihn nur.

Firmware 0.21.0 und älter liest `/ESC/`. Das aktuelle Panel liest diesen
Ordner nicht. Verschiebe seine Dateien am Computer nach
`/BENCH/ESC/CONF/`, damit sie wieder geladen werden.

Im Repository: Ändere oder ergänze die Datei unter `shared/esc/profiles/`.
Erzeuge anschließend die C-Datei neu und prüfe sie:

```sh
python3 tools/gen_esc_profiles.py
python3 tools/gen_esc_profiles.py --check
python3 tools/check_units.py --check
```

Die Texte, die das Panel mitführt, schreiben Zahl und Einheit zusammen:
`2s`, `1.2-1.8ms`, `3.0V per cell`, `8kHz`. Ein Wort nach einer Zahl behält
sein Leerzeichen (`3 beeps`). Das sind `brand`, `family`,
`automatable_note`, die `steps` des Einstiegs, der `name` eines Modells, der
`name` und `applies_when` eines Menüpunkts, der `name` eines Werts und
`action` und `action_de` eines Handgriffs: die Strings, die
`gen_esc_profiles.py` in die C-Datei übernimmt. `check_units.py` prüft diese
Strings. Die Recherchefelder (`sources`, `notes`, `unknowns`, die
Beschreibungen, `spec_source` und `firmware_note` eines Modells) prüft es nicht.

CI (Continuous Integration) führt `--check` aus. Die Host-Suite liest jede
Datei mit dem Parser des Panels und vergleicht das Ergebnis Feld für Feld mit
der erzeugten Tabelle, damit Generator und Kartenleser dieselben Dateien
annehmen. Der Test `esc_parity` der Suite ändert diese Dateien 3000-mal, in
Struktur und Text, und schlägt bei einem Ergebnis fehl, das Generator und
Kartenleser verschieden lesen.

## Aktuelle Einschränkungen

- Menü- und Wertnummern,
  Standardwerte und Einstiegsgesten sind so, wie die Handbücher sie angeben,
  und manche Handbücher widersprechen einander; die `notes` jedes Profils
  nennen die Widersprüche.
- Die Pieptonfelder (`beep_ms`, `gap_ms`, `group_gap_ms`) sind in jedem
  Profil null, und die Stick-Programmierung liest sie nicht: ihre Zeiten
  kommen aus ihren Einstellungen.
- Das Netzteil des Prüfstands ist der PD mini mit höchstens 20V. ESCs, deren
  Mindesteingang darüber liegt, etwa YGE Opto und Navy ab 6S, brauchen eine
  externe Versorgung.
- Wo ein Menü die Punkte durch Tonhöhe statt durch Anzahl unterscheidet
  (die Profile für ESCs von Hitec, Mystery und Readytosky), trennt der Versorgungsstrom die Punkte
  möglicherweise nicht. Das ist nicht gemessen.
- Eine Karte, die bei laufendem Panel geändert wird, wird beim nächsten
  Start gelesen.
