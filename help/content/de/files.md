---
screen: FILES
title: Files
order: 53
---

# Files

FILES zeigt die SD-Karte (Secure Digital) in zwei Spalten. Er kopiert, verschiebt, benennt um und löscht Dateien und legt Ordner und leere Textdateien an. Er öffnet über DATEIEN unten auf [LOGS](logs.md) und über die Kachel FILES auf [SETUP](setup.md). Das Home-Tag schließt zuerst eine Box oder die Tastatur und führt dann zu dem Bildschirm zurück, der FILES geöffnet hat. FILES steuert keinen Ausgang.

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
| ÖFFNEN | öffnet einen Ordner. Eine CSV-Datei oder ein Bericht in STAMM, USER oder BENCH öffnet auf [LOGS](logs.md), ein Bericht als Text. Eine Datei, die der Prüfstand noch schreibt, bleibt hier, `ATEST012.TXT wird gerade geschrieben.`, und jede andere Datei zeigt `LOGS öffnet .csv-Dateien und Berichte in STAMM, USER und BENCH.` |

Ein vorhandener Name wird nie überschrieben: `NOTES.TXT gibt es dort schon.` Eine Kopie wird als `NAME.NEW` geschrieben und umbenannt, wenn sie vollständig ist.

![Die Auswahl von NEU](view:files-new)
![Die Tastatur für eine neue Textdatei](view:files-name)

Die Tastatur fasst bis zu 59 Bytes; Ä Ö Ü ä ö ü ß zählen je 2. OK lehnt einen leeren Namen ab, eines von `" * / : < > ? \ |`, ein Leerzeichen oder einen Punkt an einem Ende und Gerätenamen wie `CON` oder `COM1`, und nennt den Grund neben dem Titel.

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
