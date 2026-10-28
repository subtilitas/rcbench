---
screen: FILES
title: Files
order: 53
---

# Files

FILES zeigt die SD-Karte (Secure Digital) in zwei Spalten. Er kopiert, verschiebt, benennt um und löscht Dateien, legt Ordner und leere Textdateien an zeigt eine Datei als Text und bearbeitet sie. Er öffnet über DATEIEN unten auf [LOGS](logs.md) und über die Kachel FILES auf [SETUP](setup.md). Das Home-Tag schließt zuerst eine Box oder die Tastatur und führt dann zu dem Bildschirm zurück, der FILES geöffnet hat. FILES steuert keinen Ausgang.

## Die zwei Spalten

![FILES](view:files)

Die linke Spalte öffnet im Stammverzeichnis der Karte, die rechte im Ordner User-Logs (vorgegeben `USER/LOGS`). Die Spalte mit der farbigen Überschrift ist die aktive: ÖFFNEN, UMBENENNEN, LÖSCHEN, NEU und INFO wirken in ihr. KOPIEREN und VERSCHIEBEN bringen ihren gewählten Eintrag in den Ordner der anderen Spalte. Ein Tipp auf eine Zeile wählt sie und macht ihre Spalte zur aktiven; ein zweiter Tipp auf dieselbe Zeile ist ÖFFNEN.

Die Taste über einer Spalte zeigt den Pfad des Ordners und geht einen Ordner höher. VORIGE und NÄCHSTE stehen daneben, solange der Ordner mehr als 48 Einträge hält. Die Überschrift zählt die Einträge, und `+ 1 NICHT GELISTET` zählt die Namen, die das Panel nicht listet: Namen, die mit einem Punkt beginnen, etwa `.DS_Store`, und Namen über 63 Bytes. Ein Ordner, der nur solche Einträge hält, ist nicht leer.

![Die zweite Seite eines Ordners mit 120 Einträgen](view:files-paged)

Jede Zeile zeigt den Namen, die Größe (`512B`, `14kB`, `7.0MB`; 1kB sind 1.024 Bytes), die Art und ein Zustandswort:

| Zustand | Bedeutung |
|---|---|
| FEST | ein Ordner, den der Prüfstand führt, etwa `BENCH`, `USER` und die Log-Ordner: kein UMBENENNEN, VERSCHIEBEN oder LÖSCHEN |
| GESPERRT | der Vermerk der Servoprogrammierung, `BENCH/KST/CONF/KST.TXT`: nur INFO |
| WIRD GESCHRIEBEN | eine Datei, die der Logger offen hat: nur INFO |

![Die Ordner für ESC und Servo](view:files-conf)

Das Fehlerprotokoll, `BENCH/RCBENCH.LOG`, nimmt KOPIEREN und LÖSCHEN nur bei bestehender Verbindung zur IOmcu an.

## Die Tasten

Eine Taste, die abgelehnt würde, ist gedimmt. Ein Tipp darauf nennt den Grund in der Zeile über den Tasten.

| Taste | Was sie tut |
|---|---|
| AKTUALISIEREN | sieht in den Steckplatz und liest jede Spalte an der Seite neu, die sie zeigt. Die oberste Zeile und der gewählte Eintrag bleiben; ist der gewählte Eintrag von der Karte verschwunden, ist nichts gewählt, wie nach LÖSCHEN |
| KOPIEREN | kopiert die gewählte Datei in den Ordner der anderen Spalte. Ordner werden nicht kopiert |
| VERSCHIEBEN | verschiebt die gewählte Datei oder den gewählten Ordner in den Ordner der anderen Spalte |
| UMBENENNEN | öffnet die Tastatur mit dem Namen des Eintrags |
| LÖSCHEN | fragt zuerst. Die CSV-Datei eines Laufs nimmt ihren Bericht mit. Ein Ordner wird nur gelöscht, wenn er leer ist |
| NEU | ORDNER oder TEXTDATEI, dann die Tastatur für den Namen |
| INFO | Name, Ordner, Art, Größe oder Einträge, Zeit und Regel |
| ÖFFNEN | öffnet einen Ordner. Eine CSV-Datei in STAMM, USER oder BENCH öffnet auf [LOGS](logs.md). Jede andere Datei, auch ein Bericht, öffnet im [Textbetrachter](#der-textbetrachter). Eine Datei über 1MiB bleibt hier, `Über 1MB: zu groß. Lade die Datei über Wi-Fi herunter.`, ebenso eine Datei, die der Prüfstand noch schreibt, `ATEST012.TXT wird gerade geschrieben.` |

Ein vorhandener Name wird nie überschrieben: `NOTES.TXT gibt es dort schon.` Eine Kopie wird als `NAME.NEW` geschrieben und umbenannt, wenn sie vollständig ist.

![Die Auswahl von NEU](view:files-new)
![Die Tastatur für eine neue Textdatei](view:files-name)

Die Tastatur fasst bis zu 59 Bytes; Ä Ö Ü ä ö ü ß zählen je 2. OK lehnt einen leeren Namen ab, eines von `" * / : < > ? \ |`, ein Leerzeichen oder einen Punkt an einem Ende und Gerätenamen wie `CON` oder `COM1`, und nennt den Grund neben dem Titel.

## Der Textbetrachter

![Der Bericht eines Servotests im Textbetrachter](view:files-view)

Der Betrachter zeigt eine Datei als Text, nur lesend. Das Home-Tag schließt ihn: zurück zu den Spalten, oder zurück zu [LOGS](logs.md), wenn LOGS ihn geöffnet hat. Er öffnet eine Datei von bis zu 1MiB und bis zu 65.536 Zeilen; während die Karte gelesen wird, steht `Lese die Karte.` da.

- Die Kopfzeile zeigt Ordner und Namen der Datei, ihre Größe, NUR LESEN und `ZEILE 23 VON 57`: die Zeile in der obersten Bildschirmzeile und die Zeilen der Datei.
- 18 Zeilen zu 92 Zeichen stehen auf einmal da. KLEIN schaltet auf eine kleinere Schrift, 22 Zeilen zu 115 Zeichen, für die Stufentabelle eines Berichts; NORMAL schaltet zurück. Die Wahl gilt, bis das Panel neu startet.
- Eine Zeile, die breiter ist als eine Bildschirmzeile, setzt sich in der nächsten fort, links mit `..` markiert.
- Ziehen auf dem Text oder am Rollbalken rollt ihn. SEITE HOCH und SEITE RUNTER bewegen einen Bildschirm; jeder ist am ersten oder letzten Bildschirm grau.
- Die Wörter des Prüfstands haben Farben: Überschriften, Bezeichnungen wie `Ergebnis:`, BESTANDEN grün, NICHT BESTANDEN rot, `nicht gemessen` und die anderen Wörter für das, was nicht gelaufen ist, in der Warnfarbe, eine Zahl mit Einheit in der Farbe der Einheit, `#`-Kommentare blass. Englische Berichte erscheinen genauso. Eine `.JSON`-Datei ist als JSON gefärbt.
- Eine Datei mit Zeilenenden LF und CR LF zeigt `GEMISCHTE ZEILENENDEN`.
- Eine Datei mit einem NUL-Byte in den ersten 4kB meldet `Keine Textdatei: ein NUL-Byte in den ersten 4kB.` Eine leere Datei meldet `Die Datei ist leer.`

![Derselbe Bericht mit KLEIN](view:files-view-small)
![Eine Servodatei des KST-Programmierers](view:files-view-kst)

## Der Texteditor

![Eine Zeile in einen Bericht getippt](view:files-edit)

BEARBEITEN im Textbetrachter öffnet den Editor und seine Tastatur. Der Text ändert sich im Speicher des Panels; SPEICHERN schreibt ihn auf die Karte.

- Über der Tastatur stehen 8 Zeilen des Texts, 10 mit KLEIN. Die Tastatur hat keine Ziffernreihe: `ABC` gibt Großbuchstaben, `123` die Ziffern und die häufigen Zeichen, `#+=` auf dieser Seite die übrigen Zeichen; `abc` führt zurück. DEL nimmt das Zeichen vor dem Cursor. ENTER beginnt eine neue Zeile mit dem Zeilenende der Datei. `<-` und `->` bewegen den Cursor; ein Tipp auf den Text setzt ihn dorthin. FERTIG schließt die Tastatur, TASTATUR öffnet sie wieder.
- Die Kopfzeile zeigt GEÄNDERT, sobald der Text von der Karte abweicht, und die Zeile des Cursors.
- SPEICHERN schreibt den Text als `<Name>.NEW`, löscht die alte Datei und gibt der neuen ihren Namen. SPEICHERN ist gesperrt, solange der Prüfstand ARMED ist oder ein Lauf aufgezeichnet wird; der Text bleibt. Ein Wechsel auf ARMED während des Speicherns hält es an, und die alte Datei bleibt.
- Eine KST-Datei des Servo-Programmierers verliert ihre Prüfsummenzeile, wenn sie geändert gespeichert wird, in jeder Form, die der Programmierer als eine liest (`checksum`, `CHECKSUM=`, `Checksum:`): der Programmierer liest sie dann als Datei, die ein Mensch bearbeitet hat.
- SPEICHERN lehnt einen Text mit einem NUL-Byte in den ersten 4kB ab, da FILES die Datei sonst nicht wieder öffnet: `Nicht gespeichert: ein NUL-Byte in den ersten 4kB. FILES öffnet die Datei nicht wieder.`
- Ein ESC-Profil in `BENCH/ESC/CONF` liest das Panel beim nächsten Start.
- VERWERFEN setzt den Text auf die Datei auf der Karte zurück. Es fragt zuerst, wie das Home-Tag bei ungespeichertem Text: BLEIBEN behält den Text, VERWERFEN lässt die Änderungen fallen.
- Der Text hält bis zu 1MiB und 65.536 Zeilenenden. Die Tastatur tippt ASCII; einzelne Tasten lassen sich nicht rückgängig machen.
- SETUP → PANEL → Tastatur durchsichtig auf 25% oder 50% lässt den übrigen Text durch die Tastatur scheinen; der Cursor bleibt auf den 8 Zeilen darüber.

![Eine KST-Datei mit einem Kommentar gespeichert](view:files-edit-kst)
![Die Frage vor ungespeicherten Änderungen](view:files-edit-unsaved)

## ARMED und Aufzeichnung

![FILES, während der Prüfstand ARMED ist](view:files-armed)

FILES schreibt nur, solange der Prüfstand SAFE ist und kein Lauf aufgezeichnet wird. Solange der Prüfstand ARMED ist oder ein Lauf aufgezeichnet wird, sind alle Tasten außer INFO und AKTUALISIEREN gedimmt, und die Zeile über den Tasten zeigt `Prüfstand ARMED. Die Karte ist nur lesbar.` oder `Ein Lauf wird aufgezeichnet. Die Karte ist nur lesbar.` Eine laufende Kopie hält vor ihren nächsten 16KiB an, wenn der Prüfstand auf ARMED schaltet oder ein Lauf die Aufzeichnung beginnt.

![FILES, während ein Test aufgezeichnet wird](view:files-logging)

## Die Boxen

![Die Rückfrage zu LÖSCHEN](view:files-delete)
![Eine laufende Kopie](view:files-copy)
![INFO zu einer Datei](view:files-info)
![INFO zu einem Ordner](view:files-info-folder)

Eine Aufgabe, die länger als 0.25s dauert, zeigt ihren Fortschritt in einer Box; ABBRECHEN hält eine Kopie vor ihren nächsten 16KiB an. INFO zeigt die Zeit, die die Karte führt, in der Zone von SETUP > PANEL, Zeitzone. Ein Datum in 1980 liest sich `nicht bekannt`: Die Uhr war nicht gestellt, als die Datei geschrieben wurde ([Setup](setup.md#panel)).

## Keine Karte

![FILES ohne Karte](view:files-nocard)

Ohne Karte zeigen beide Spalten `Keine Karte. Setze eine Karte ein und tippe auf AKTUALISIEREN.` Eine eingesetzte, entnommene oder getauschte Karte findet AKTUALISIEREN; beide Spalten gehen dann zurück zum Stammverzeichnis und zum Ordner User-Logs.
