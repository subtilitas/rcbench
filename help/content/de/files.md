---
screen: FILES
title: Files
order: 53
---

# Files

FILES verwaltet die SD-Karte (Secure Digital) in zwei Spalten. Du kannst Dateien kopieren, verschieben, umbenennen und löschen, Ordner und leere Textdateien anlegen sowie Texte anzeigen und bearbeiten. Öffne die Seite über die Leiste der Kachel [LOGS](logs.md). Das Home-Feld schließt zuerst ein offenes Dialogfeld oder die Tastatur und führt dann zur aufrufenden Seite zurück. FILES steuert keinen Ausgang.

## Die zwei Spalten

![FILES](view:files)

Die linke Spalte beginnt im Stammverzeichnis der Karte, die rechte im Ordner Testaufzeichnungen (Vorgabe `USER/LOGS`). Eine farbige Überschrift kennzeichnet die aktive Spalte. Dort wirken ÖFFNEN, UMBENENNEN, LÖSCHEN, NEU und INFO. KOPIEREN und VERSCHIEBEN übertragen den gewählten Eintrag in den Ordner der anderen Spalte. Tippe auf eine Zeile, um sie auszuwählen und ihre Spalte zu aktivieren. Ein zweiter Tipp auf dieselbe Zeile öffnet den Eintrag.

Die Taste über jeder Spalte zeigt den Ordnerpfad. Tippe darauf, um einen Ordner höher zu gehen. Bei mehr als 48 Einträgen erscheinen daneben VORIGE und NÄCHSTE. Die Überschrift nennt die Anzahl der Einträge. Eine zusätzliche Angabe wie `+ 1 NICHT GELISTET` zählt ausgelassene Namen: solche mit einem Punkt am Anfang, etwa `.DS_Store`, und solche mit mehr als 63 Bytes. Ein Ordner mit ausschließlich ausgelassenen Einträgen ist nicht leer.

![Die zweite Seite eines Ordners mit 120 Einträgen](view:files-paged)

Jede Zeile zeigt den Namen, die Größe (`512B`, `14kB`, `7.0MB`; 1kB sind 1.024 Bytes), die Art und ein Zustandswort:

| Zustand | Bedeutung |
|---|---|
| FEST | Vom Prüfstand angelegter Ordner wie `BENCH`, `USER` oder ein Aufzeichnungsordner. UMBENENNEN und VERSCHIEBEN sind gesperrt. LÖSCHEN ist nur bei leerem Ordner möglich; beim nächsten Schreiben legt der Prüfstand ihn wieder an. |
| GESPERRT | Vermerk der Servoprogrammierung, `BENCH/KST/CONF/KST.TXT`; nur INFO verfügbar. |
| WIRD GESCHRIEBEN | Eine Datei ist für die Aufzeichnung geöffnet; nur INFO verfügbar. |

![Die Ordner für ESC und Servo](view:files-conf)

KOPIEREN und LÖSCHEN sind für das Fehlerprotokoll `BENCH/RCBENCH.LOG` nur bei bestehender Verbindung zum IOmcu verfügbar.

## Die Tasten

Nicht verfügbare Tasten sind gedimmt. Tippe darauf, um den Grund über der Tastenzeile zu lesen.

| Taste | Wirkung |
|---|---|
| AKTUALISIEREN | Prüft den Karteneinschub und liest beide Spalten auf ihrer aktuellen Seite neu ein. Oberste Zeile und Auswahl bleiben erhalten. Fehlt der gewählte Eintrag auf der Karte, wird die Auswahl wie nach LÖSCHEN aufgehoben. |
| KOPIEREN | Kopiert die gewählte Datei in den Ordner der anderen Spalte. Ordner werden nicht kopiert. |
| VERSCHIEBEN | Verschiebt die gewählte Datei oder den gewählten Ordner in den Ordner der anderen Spalte. |
| UMBENENNEN | Öffnet die Tastatur mit dem Namen des Eintrags. |
| LÖSCHEN | Fragt vor dem Löschen nach. Beim Löschen einer Lauf-CSV wird der zugehörige Bericht mitgelöscht. Ordner müssen leer sein. |
| NEU | Bietet ORDNER oder TEXTDATEI und öffnet danach die Tastatur für den Namen. |
| INFO | Zeigt Name, Ordner, Art, Größe oder Anzahl der Einträge, Zeit und Regel. |
| ÖFFNEN | Öffnet einen Ordner. CSV-Dateien in STAMM, USER oder BENCH öffnen auf [LOGS](logs.md), andere Dateien einschließlich Berichten im [Textbetrachter](#der-textbetrachter). Dateien über 1MiB bleiben hier: `Über 1MB: zu groß. Lade die Datei über WLAN herunter.` Dasselbe gilt für noch beschriebene Dateien: `ATEST012.TXT wird gerade geschrieben.` |

Ein vorhandener Name wird nie überschrieben: `NOTES.TXT ist dort bereits vorhanden.` Eine Kopie wird als `NAME.NEW` geschrieben und umbenannt, wenn sie vollständig ist.

![Die Auswahl von NEU](view:files-new)
![Die Tastatur für eine neue Textdatei](view:files-name)

Die Tastatur nimmt Namen mit bis zu 59 Bytes an; Ä Ö Ü ä ö ü ß belegen je 2 Bytes. OK lehnt leere Namen, eines der Zeichen `" * / : < > ? \ |`, Leerzeichen oder Punkte am Anfang oder Ende sowie Gerätenamen wie `CON` oder `COM1` ab. Der Grund erscheint neben dem Titel.

## Der Textbetrachter

![Der Bericht eines Servotests im Textbetrachter](view:files-view)

Der Textbetrachter öffnet Dateien mit bis zu 1MiB und 65.536 Zeilen schreibgeschützt. Während des Einlesens zeigt er `Karte wird gelesen.` Das Home-Feld führt zu den Spalten zurück, oder zu [LOGS](logs.md), wenn die Datei dort geöffnet wurde.

- Die Kopfzeile zeigt Ordner, Dateiname, Größe, NUR LESEN und `ZEILE 23 VON 57`: die oberste sichtbare Zeile und die Gesamtzahl der Zeilen.
- Die normale Schrift zeigt 18 Zeilen zu 92 Zeichen. KLEIN zeigt 22 Zeilen zu 115 Zeichen, etwa für die Stufentabelle eines Berichts. NORMAL schaltet zurück. Die Wahl gilt bis zum Neustart des Panels.
- Lange Zeilen werden umgebrochen; Fortsetzungen sind links mit `..` markiert.
- Ziehe den Text oder den Rollbalken zum Scrollen. SEITE HOCH und SEITE RUNTER bewegen den Text um eine Bildschirmhöhe. Am jeweiligen Ende sind sie grau.
- Berichte heben Überschriften und Bezeichnungen wie `Ergebnis:` farbig hervor. BESTANDEN erscheint grün, NICHT BESTANDEN rot. `nicht gemessen` und andere Angaben zu nicht ausgeführten Schritten erscheinen in der Warnfarbe. Zahlen mit Einheit erhalten die Farbe der Einheit, `#`-Kommentare sind blass. Englische Berichte verwenden dieselbe Hervorhebung. `.JSON`-Dateien erhalten JSON-Farben.
- Dateien mit LF und CR LF als Zeilenenden zeigen `GEMISCHTE ZEILENENDEN`.
- Ein NUL-Byte in den ersten 4kB führt zu `Keine Textdatei: ein NUL-Byte in den ersten 4kB.` Eine leere Datei zeigt `Die Datei ist leer.`

![Derselbe Bericht mit KLEIN](view:files-view-small)
![Eine Servodatei des KST-Programmierers](view:files-view-kst)

## Der Texteditor

![Eine Zeile in einen Bericht getippt](view:files-edit)

BEARBEITEN im Textbetrachter öffnet den Editor und seine Tastatur. Der Text ändert sich im Speicher des Panels; SPEICHERN schreibt ihn auf die Karte.

- Über der Tastatur stehen 8 Textzeilen, mit KLEIN 10. Die Tastatur hat keine Ziffernreihe: `ABC` zeigt Großbuchstaben, `123` Ziffern und häufige Zeichen, `#+=` dort die übrigen Zeichen. `abc` führt zurück. DEL löscht das Zeichen vor dem Cursor. ENTER beginnt eine neue Zeile mit dem Zeilenende der Datei. `<-` und `->` bewegen den Cursor; ein Tipp auf den Text setzt ihn an diese Stelle. FERTIG schließt die Tastatur, TASTATUR öffnet sie wieder.
- Die Kopfzeile zeigt die Cursorzeile und GEÄNDERT, sobald der Text vom Karteninhalt abweicht.
- SPEICHERN schreibt den Text als `<Name>.NEW`, löscht die alte Datei und benennt die neue entsprechend um. Bei ARMED oder laufender Aufzeichnung ist SPEICHERN gesperrt; der Text bleibt erhalten. Ein Wechsel auf ARMED während des Speicherns bricht den Vorgang ab; die alte Datei bleibt erhalten.
- Wird eine geänderte KST-Datei der Servoprogrammierung gespeichert, entfällt ihre Prüfsummenzeile. Das gilt für alle erkannten Formen (`checksum`, `CHECKSUM=`, `Checksum:`). Die Servoprogrammierung behandelt sie dadurch als manuell bearbeitete Datei.
- SPEICHERN lehnt Text mit einem NUL-Byte in den ersten 4kB ab, da FILES ihn sonst nicht erneut öffnen könnte: `Nicht gespeichert: ein NUL-Byte in den ersten 4kB. FILES öffnet die Datei nicht wieder.`
- Ein ESC-Profil in `BENCH/ESC/CONF` wird beim nächsten Panelstart eingelesen.
- SPEICHERN überschreibt nur die Dateiversion, die der Editor geöffnet hat. Wurde sie auf der Karte durch den WLAN-Editor oder einen Upload verändert, erscheint `<Name> wurde seit dem Lesen auf der Karte geändert.` BLEIBEN behält den bearbeiteten Text. NEU LESEN verwirft ihn und liest die Datei von der Karte. Halte ÜBERSCHREIBEN 2s, um die beim abgelehnten Speicherversuch gefundene Version zu ersetzen. Ändert sich die Datei erneut, wird das Schreiben wieder abgelehnt. Fehlt sie, bleibt ÜBERSCHREIBEN gedimmt.
- VERWERFEN setzt den Text auf den Karteninhalt zurück und fragt vorher nach. Das Home-Feld fragt bei ungespeicherten Änderungen ebenfalls: BLEIBEN behält den Text, VERWERFEN verwirft die Änderungen.
- Der Text darf bis zu 1MiB und 65.536 Zeilenenden enthalten. Die Tastatur gibt ASCII ein; einzelne Tasteneingaben lassen sich nicht rückgängig machen.
- SETUP > PANEL > Tastaturtransparenz auf 25% oder 50% lässt den übrigen Text durch die Tastatur scheinen. Der Cursor bleibt in den 8 Zeilen darüber.

![Eine KST-Datei mit einem Kommentar gespeichert](view:files-edit-kst)
![Die Frage vor ungespeicherten Änderungen](view:files-edit-unsaved)

## ARMED und Aufzeichnung

![FILES, während der Prüfstand ARMED ist](view:files-armed)

FILES schreibt nur bei SAFE und ohne laufende Aufzeichnung. Bei ARMED oder laufender Aufzeichnung sind KOPIEREN, VERSCHIEBEN, UMBENENNEN, LÖSCHEN und NEU gedimmt. Durchsehen, Lesen von Dateien, INFO und AKTUALISIEREN bleiben verfügbar; die normalen Beschränkungen für gesperrte Dateien und Dateien im Schreibzugriff gelten weiter. Darüber steht `Prüfstand ARMED. Die Karte ist nur lesbar.` oder `Ein Lauf wird aufgezeichnet. Die Karte ist nur lesbar.` Beginnt einer dieser Zustände während des Kopierens, stoppt die Kopie vor dem nächsten Block von 16KiB.

![FILES, während ein Test aufgezeichnet wird](view:files-logging)

## Die Boxen

![Die Rückfrage zu LÖSCHEN](view:files-delete)
![Eine laufende Kopie](view:files-copy)
![INFO zu einer Datei](view:files-info)
![INFO zu einem Ordner](view:files-info-folder)

Ein Vorgang von mehr als 0,25s zeigt ein Fortschrittsfenster. ABBRECHEN stoppt eine Kopie vor dem nächsten Block von 16KiB. INFO zeigt den Zeitstempel der Karte in der Zeitzone aus SETUP > PANEL. Ein Datum im Jahr 1980 erscheint als `nicht bekannt`: Beim Schreiben war die Uhr nicht gestellt ([Setup](setup.md#panel)).

## Keine Karte

![FILES ohne Karte](view:files-nocard)

Ohne Karte zeigen beide Spalten `Keine Karte. Setze eine Karte ein und tippe auf AKTUALISIEREN.` Tippe nach dem Einsetzen, Entnehmen oder Wechseln einer Karte auf AKTUALISIEREN. Beide Spalten kehren dann zum Stammverzeichnis beziehungsweise zum Ordner Testaufzeichnungen zurück.
