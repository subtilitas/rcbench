# ESC-Programmierprofile

[English](EscProfiles.md)

Ein ESC-Profil (Electronic Speed Controller) beschreibt, wie eine Familie
von ESCs über ihr Gasknüppel-Menü programmiert wird. Es enthält, wie das Menü
betreten wird, wie der ESC eine Zahl ausgibt und welche Punkte und Werte das
Menü hat. Es sind die Daten, die die
[Stick-Programmierung](StickProgramming-de.md) ausführt: Der Bediener wählt
einen ESC und die zu ändernden Werte, und der Prüfstand bewegt das Gas und
zählt die Pieptöne im Versorgungsstrom. 24 der 72 Profile sind von einer
Art, die sie ausführt.

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
eine Pieplänge, eine Pause oder eine Schleifenzeit in Millisekunden, deshalb
ist jedes Zeitfeld null, bis eine Aufnahme am Prüfstand es misst.

## Wo Profile liegen

| Ablage | Pfad | Gelesen |
| --- | --- | --- |
| Eingebaut | `shared/esc/profiles/*.json`, in das Panel-Image übersetzt | immer |
| SD-Karte | `/ESC/*.json` | einmal, beim Start |

Eine Datei auf der Karte heißt wie ihre id: `/ESC/hobbywing-flyfun-8item.json`
enthält `"id": "hobbywing-flyfun-8item"`. Groß- und Kleinschreibung dürfen
abweichen; alles andere wird abgelehnt. Eine Datei mit der id eines
eingebauten Profils ersetzt dieses Profil. Eine Datei mit neuer id fügt ein
Profil hinter den eingebauten an. Eine Datei, die ihren Hersteller über
512 Modelle (`ESC_MAKER_MODELS_MAX`) brächte, gezählt über seine eingebauten
Profile und die von der Karte, wird abgelehnt. Dateinamen ab 64 Zeichen werden nicht
gelesen. Die
Karte hält höchstens 32 Profile; das Panel liest die Namen der ersten 64
`.json`-Dateien in `/ESC/`. Eine abgelehnte Datei wird auf der Konsole mit
Grund genannt, zum Beispiel:

```
ESC profiles: MYESC.JSON refused: items[2].values: not 1-255 entries
```

Abgelehnt wird eine Datei, die:

- größer als 64 KiB ist, kein JSON ist oder kein UTF-8 ist;
- tiefer als 16 Ebenen unter dem obersten Objekt verschachtelt ist oder ein
  Objekt mit mehr als 64 Einträgen hat;
- in einem Objekt einen Schlüssel zweimal hat, verglichen nach dem Dekodieren
  (`"sch\u0065ma"` ist `"schema"`);
- in einem String `\u0000` oder ein halbes Surrogat-Paar enthält;
- anders heißt als ihre id;
- eine Regel unten verletzt.

Der Generator wendet dieselben Regeln auf die Dateien im Repository an. Das Panel läuft ohne Karte; dann sind die
eingebauten Profile der ganze Satz.

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
| `scheme.listen` | wo der Knüppel ruht, während das Menü läuft: `{"throttle": "min"}`, `mid` oder `max`. Fehlt es oder ist es null: wo der Einstieg ihn ließ. Gesetzt in den 9 YGE-Profilen, deren Menü nach einem Einstieg bei Maximum mit dem Knüppel auf Minimum läuft; die Stick-Programmierung führt ein solches Profil nur aus, wenn `hold_ms` die Länge des Einstiegs nennt |
| `scheme.store` | die Bewegung, die eine Auswahl speichert, nachdem der ESC sie beantwortet hat: `{"throttle": "min"}`, `mid` oder `max`. Fehlt es oder ist es null: die Auswahl speichert. Gesetzt in den 8 YGE-Mode-Setups, wo der Knüppel zurück auf Minimum den Modus speichert |
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
| `current_a` | Dauerstrom in A, 0 bis 65535, oder null |

### Menüpunkte

| Feld | Werte |
| --- | --- |
| `number` | 1 bis 255, wie der ESC sie ausgibt |
| `name` | wie das Handbuch ihn nennt |
| `key` | 1 bis 32 aus `a-z 0-9 _`; eine Bedeutung über alle Marken: `brake`, `timing`, `cutoff_voltage`, `cutoff_type`, `battery_type`, `cell_count`, `startup`, `governor`, `direction`, `throttle_range`, `pwm_freq`, `aircraft_type`, `mode`, `reset` |
| `values` | 1 bis 255 aus `{"number": 0-255, "name": "...", "default": true}`; Nummern eindeutig, höchstens ein Standardwert |
| `values[].entry_throttle` | `min`, `mid`, `max`: die Knüppelstellung, aus der das Handbuch diesen Wert programmiert, wo sie nicht die des Einstiegs ist; fehlt sie oder ist null, die des Einstiegs. Die Stick-Programmierung schaltet den ESC für diesen Wert dort ein |
| `values[].entry_hold_ms` | 0 bis 600000: Einschalten bis Menü, wenn dieser Wert programmiert wird, wo das Handbuch eine andere Wartezeit als `scheme.entry.hold_ms` nennt; fehlt es oder ist null, die des Einstiegs. Kontronik SUN PLUS Modi 4 bis 6 warten 5000 ms |
| `values[].after_select` | 1 bis 4 aus `min`, `mid`, `max`: die Bewegungen, die das Handbuch nach der Auswahlbewegung dieses Werts verlangt, der Reihe nach, jede, wenn der ESC die vorige beantwortet hat; fehlt es oder ist null, keine. Die Kontronik-Car-Modi und PIX Modus 2 gehen auf `min`, die Bremse. Jede ist eine Bewegung: die erste nicht dorthin, wo der Knüppel steht, wenn sie beginnen -- die Bewegung `scheme.store` des Profils, sonst die Auswahlbewegung (oder `value_select`) --, und keine gleich der davor; eine Datei mit einer, die stehen bleibt, wird abgelehnt. Die Stick-Programmierung macht jede SPEICHERN nach der vorigen, nach der Bewegung `scheme.store` des Profils; ein zweistufiges Profil nimmt keine |
| `applies_to` | Modellnamen dieses Profils, oder null für alle |
| `applies_when` | eine Bedingung in Worten, z. B. `"model type heli"` |

Zwei Menüpunkte dürfen dieselbe Nummer nur tragen, wenn beide `applies_to`
oder `applies_when` haben.

### Handgriffe

`manual` listet, was ein Mensch außer Gas und Versorgung am ESC tut, in der
Reihenfolge, in der ein Lauf darauf trifft. Fehlt es oder ist null: keine.

```json
"manual": [
  {"when": "before_power", "action": "Fit the jumper on any 2 of the 3 programming contacts.",
   "source": "Kontronik_Beat.pdf p.5"},
  {"when": "before_menu", "action": "Pull the jumper off after 2 s or the tone sequence."}
]
```

| Feld | Werte |
| --- | --- |
| `manual` | 1 bis 4 Schritte; nur in einem Profil mit `automatable: "assisted"` |
| `when` | `before_power`, `at_power_up`, `before_menu`, `during_menu`, `after_programming`; kein Schritt früher als der darüber |
| `action` | 1 bis 120 Byte UTF-8, englisch: zwei Zeilen des Pop-ups |
| `action_de` | derselbe Schritt auf Deutsch, 1 bis 120 Byte UTF-8, Umlaute eingeschlossen; fehlt es oder ist null, erscheint auch auf Deutsch das Englische |
| `starts_menu` | `true`, wo die Handlung selbst die Folge des Menüs startet, wie ein abgezogener Jumper oder ein gedrückter Taster bei Kontronik; fehlt es, null oder `false` sonst. Nur an einem Schritt `before_menu`, höchstens einer je Profil, und kein Schritt `before_menu` danach. Die Stick-Programmierung hört ab dem Moment zu, in dem sie nach diesem Schritt fragt ([Stick-Programmierung](StickProgramming-de.md#handgriffe)) |
| `hold_ms` | nur `at_power_up`: 0 bis 60000, wie lange der Schritt nach dem Einschalten gehalten wird; fehlt oder null, wo nicht angegeben |

`source` und jedes andere Feld eines Schritts bleiben im JSON. Die Aktion
ist der eigene Text des Profils: deutsch, wo das Profil `action_de` nennt
und die Oberfläche deutsch ist, sonst englisch. Jeder Schritt im Satz hat
sein Deutsch. Der Bildschirm übersetzt, wann sie fällig ist. Was ein Lauf mit jedem Schritt tut, steht unter
[Stick-Programmierung](StickProgramming-de.md#handgriffe).

24 Profile haben Schritte: die 22 Kontronik-Familien, `turnigy-aquastar`
(sein Schalter bei Vollgas) und `greatplanes-electrifly-c-series` (sein
Ein/Aus-Taster). `graupner-brushless-control-t`, dessen Menü an LEDs
abgelesen wird, und `hacker-master-senstrol`, das den
Identifikationschip seines Motors, einen zweiten Kanal und eine JetiBox
braucht, sind `assisted` ohne Schritte: kein Handgriff, um den ein Mensch zu
einem Zeitpunkt gebeten werden kann.

## Ein Profil hinzufügen oder korrigieren

An einem Prüfstand: die Datei nach `/ESC/` auf die Karte kopieren und das
Panel neu starten.

Im Repository: die Datei unter `shared/esc/profiles/` ändern oder anlegen,
dann die C-Datei neu erzeugen und prüfen:

```sh
python3 tools/gen_esc_profiles.py
python3 tools/gen_esc_profiles.py --check
```

CI (Continuous Integration) führt `--check` aus. Die Host-Suite liest jede
Datei mit dem Leser des Panels und vergleicht das Ergebnis Feld für Feld mit
der erzeugten Tabelle, damit Generator und Kartenleser dieselben Dateien
annehmen.

## Aktuelle Einschränkungen

- Kein Profil ist an einem ESC gelaufen. Menü- und Wertnummern,
  Standardwerte und Einstiegsgesten sind so, wie die Handbücher sie angeben,
  und manche Handbücher widersprechen einander; die `notes` jedes Profils
  nennen die Widersprüche.
- Die Pieptonfelder (`beep_ms`, `gap_ms`, `group_gap_ms`) sind in jedem
  Profil null, und die Stick-Programmierung liest sie nicht: ihre Zeiten
  kommen aus ihren Einstellungen.
- Die Versorgung des Prüfstands ist das PD mini, höchstens 20 V. ESCs, deren
  Mindesteingang darüber liegt, etwa YGE Opto und Navy ab 6S, brauchen eine
  externe Versorgung.
- Wo ein Menü die Punkte durch Tonhöhe statt durch Anzahl unterscheidet
  (Hitec, Mystery, Readytosky), trennt der Versorgungsstrom die Punkte
  möglicherweise nicht. Das ist nicht gemessen.
- Eine Karte, die bei laufendem Panel geändert wird, wird beim nächsten
  Start gelesen.
