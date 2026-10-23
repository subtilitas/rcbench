---
screen: LOGS
title: Logs
order: 50
---

# Logs

LOGS liest die SD-Karte (Secure Digital) des Panels: Der Bildschirm listet die CSV-Dateien (Comma-Separated Values) eines Ordners und die allein stehenden Berichte, zeigt, was der Import in einer CSV-Datei gefunden hat, zeichnet bis zu 4 ihrer Spalten und zeigt einen Bericht als Text. Drei Tabs oben links wählen den Ordner, in dieser Reihenfolge: STAMM (das Stammverzeichnis der Karte), USER (Ordner User-Logs, vorgegeben `USER/LOGS`) und BENCH (Ordner Auto-Logs, vorgegeben `BENCH/LOGS`). Er öffnet über die Kachel LOGS im [Menü](overview.md). WLAN, unten links, öffnet [WLAN](wifi.md). Das Home-Tag geht eine Ebene zurück, wie ZURÜCK: vom Plot zur Importansicht, von der Importansicht und vom Text eines Berichts zur Dateiliste, von der Dateiliste zum Menü. Über der Rückfrage zu LÖSCHEN ist es ABBRECHEN.

## Keine Karte

![LOGS ohne Karte im Steckplatz](view:logs-nocard)

Ist keine Karte eingebunden, zeigt der Kasten `KEINE AUFZEICHNUNGEN` und `Keine Karte. Setze eine Karte ein und tippe auf AKTUALISIEREN.` Ein leerer Ordner, oder einer, den das Panel noch nicht angelegt hat, zeigt `Keine .csv-Dateien in BENCH/LOGS.` oder `Keine .csv-Dateien in USER/LOGS.`, das Stammverzeichnis ohne CSV-Datei `Keine .csv-Dateien im Hauptverzeichnis.` Die Zeile darunter sagt, was der Tab enthält: `Läufe heißen SERVOnnn, MOTORnnn oder SUPPLnnn.CSV.`, `Tests werden als ATESTnnn.CSV und LIMITnnn.TXT gespeichert.` oder `Läufe von Firmware 0.21.0 und älter.` Sind AUTO-LOGS oder USER-LOGS unter [SETUP > SD-KARTE](setup.md) aus, zeigt BENCH bzw. USER stattdessen `AUTO-LOGS AUS unter SETUP > SD-KARTE.` oder `USER-LOGS AUS unter SETUP > SD-KARTE.` AKTUALISIEREN liest die Karte erneut. Ohne Karte wechseln die Tabs wie mit Karte, und jeder Tipp auf einen Tab sucht erneut nach einer Karte.

## Eine Karte, die sich nicht einbinden lässt

![LOGS mit einer Karte ohne FAT-Volume](view:logs-nofat)

Eine Karte im Steckplatz mit NTFS (New Technology File System), einer GPT (GUID-Partitionstabelle) oder ohne Format zeigt `Karte ist weder FAT32 noch exFAT. Am Computer formatieren.` Eine Karte, die sich aus anderem Grund nicht einbinden lässt, zeigt `Karte nicht lesbar. Am Computer prüfen, dann AKTUALISIEREN.` [Kartenformate](#kartenformate) sagt, was mountet und wie eine Karte formatiert wird. Formatieren löscht die Karte. AKTUALISIEREN bindet die wieder eingesetzte Karte ein.

## Die Dateiliste

![Die Dateiliste](view:logs)

Der Kopf zeigt rechts der Tabs den gezeigten Ordner, `BENCH/LOGS`, und nennt die Karte mit ihrer Datenträgerbezeichnung, `UNITEST`, am rechten Ende der Zeile; eine Karte ohne Bezeichnung zeigt den Namen, den die Karte meldet, oder `SD-KARTE`. Die Liste zeigt 7 Zeilen auf einmal, jede 48 px (etwa 7,2 mm) des Glases zum Tippen; jede Zeile ist eine Datei mit ihrer Größe.

| Tab | Ordner | Enthält |
|---|---|---|
| STAMM | Stammverzeichnis | die Läufe von Firmware 0.21.0 und älter |
| USER | Ordner User-Logs, vorgegeben `USER/LOGS` | die Dateien des AUTOMATISCHEN TESTS und des ENDLAGENLAUFS |
| BENCH | Ordner Auto-Logs, vorgegeben `BENCH/LOGS` | das automatische Log jedes ARMED- und Netzteil-Laufs |

LOGS öffnet auf dem Tab der Datei, die das Panel zuletzt geschrieben hat, und nach einem Start auf BENCH, bis eine geschrieben ist. Ist seit dem letzten Besuch nichts geschrieben worden, bleibt der Tab, wie er verlassen wurde. Ein Tipp auf einen Tab liest seinen Ordner ab der ersten Seite.

| Taste | Was sie tut |
|---|---|
| AKTUALISIEREN | liest die gezeigte Seite erneut und wählt ihren neuesten Lauf |
| VORIGE, NÄCHSTE | die Seite davor und danach; sichtbar mit mehr als 48 Dateien auf der Karte |
| WLAN | öffnet [WLAN](wifi.md) |
| LÖSCHEN | fragt nach und entfernt dann die gewählte Datei; grau, solange nichts gewählt ist |
| ÖFFNEN | öffnet die gewählte CSV-Datei in der Importansicht, einen Bericht als Text; grau, solange nichts gewählt ist |

![Die vierte Seite einer vollen Karte](view:logs-pages)

- Eine Seite fasst 48 Einträge; eine Karte fasst bis zu 999 Läufe. Die Seiten beginnen mit dem neuesten Lauf, ein allein stehender Bericht nach seiner eigenen Nummer, dann folgen die Dateien, die der Prüfstand nicht geschrieben hat, und die Zeilen einer Seite stehen in derselben Reihenfolge, der neueste Lauf oben. Mit mehr als 48 Dateien nennt der Reiter den Teil der Karte, den die Seite zeigt, `DATEIEN 145 BIS 192 VON 1000`.
- Die Seiten werden von oben zu je 48 geschnitten. Stehen vor der gezeigten Seite weniger als 48 Einträge, etwa nachdem ein Lauf aufgezeichnet wurde, zeigt VORIGE die erste Seite.
- Läufe, die aufgezeichnet werden, während eine spätere Seite offen ist, stehen auf Seite 1. VORIGE überspringt dann auf dem Rückweg ab Seite 3 so viele Einträge, wie aufgezeichnet wurden; NÄCHSTE von Seite 1 zeigt sie, und die Zahlen im Reiter zeigen die Verschiebung.
- Der Rollbalken am rechten Rand, 56 px (etwa 8,4 mm) breit, rollt innerhalb der Seite: Tippen auf die Bahn springt, Ziehen am Griff rollt.
- Das Öffnen von LOGS liest die Karte: die erste Seite, ihr neuester Lauf gewählt. Ein Lauf, der aufgezeichnet wurde, während LOGS geschlossen war, steht ohne AKTUALISIEREN in der Liste.
- Auf einer Seite nach der ersten liest AKTUALISIEREN ab dem ersten Eintrag dieser Seite; enthält die Karte inzwischen weniger Einträge, zeigt es die letzte Seite.
- Nach LÖSCHEN wird die Seite neu gelesen, und die Rollposition bleibt; nichts ist gewählt, LÖSCHEN und ÖFFNEN sind grau, bis eine Zeile gewählt ist.
- Eine Karte, die eingesteckt, entnommen oder getauscht wird, während das Panel läuft, findet das nächste AKTUALISIEREN, das nächste Öffnen von LOGS, die nächste Seite oder ÖFFNEN und der Start des nächsten Laufs, ohne Neustart. Eine neue Karte öffnet auf ihrer ersten Seite; ein leerer Steckplatz sagt `Keine Karte. Setze eine Karte ein und tippe auf AKTUALISIEREN.`
- Neueste heißt die höchste Nummer im Namen. Das Panel hat keine Uhr, die das Abschalten übersteht; jede Datei trägt das Datum 1980-01-01.
- Ein Lauf, der gerade geschrieben wird, steht nicht in der Liste. Seine Nummer in einem anderen Ordner schon.
- Eine Zeile nimmt einen Tipp an. Ein Ziehen über mehr als 8 px auf den Zeilen scrollt die Liste und wählt nichts.

## Was ein Lauf schreibt

Eine Datei ist ein Lauf, im Ordner seiner Art. Ihr Name sagt, was den Lauf gestartet hat:

| Ordner | Name | Lauf |
|---|---|---|
| `BENCH/LOGS` | `SERVOnnn.CSV` | ein Schalten auf ARMED von SERVO |
| `BENCH/LOGS` | `MOTORnnn.CSV` | ein Schalten auf ARMED von ARM auf MOTOR & ESC oder einem Stick-Lauf auf dessen PROGRAMMIEREN |
| `BENCH/LOGS` | `SUPPLnnn.CSV` | ein Einschalten des SUPPLY-Ausgangs, solange der Prüfstand SAFE ist |
| `USER/LOGS` | `ATESTnnn.CSV` | ein AUTOMATISCHER TEST; sein Bericht ist `ATESTnnn.TXT` |
| `USER/LOGS` | `LIMITnnn.TXT` | der Bericht eines ENDLAGENLAUFS, ohne CSV daneben |
| Stammverzeichnis | `BENCHnnn.CSV`, `ESCnnn.CSV`, `SERVOnnn.CSV` | Läufe von Firmware 0.21.0 und älter; im Stammverzeichnis ist `SERVOnnn` ein Servotest |
| Stammverzeichnis | `SERVOnnn.TXT` allein | ein Endlagenlauf von Firmware 0.21.0 und älter |

Der Name folgt dem Bildschirm, dessen ARM den Prüfstand geschaltet hat, auch an einem IOmcu älter als Protokoll 4.18, der mit jedem Schalten auf ARMED die Throttle-Ausgänge mitschaltet.

AUTO-LOGS aus unter [SETUP > SD-KARTE](setup.md) schreibt keine Datei nach `BENCH/LOGS` und nimmt keine Nummer; USER-LOGS aus schreibt keine nach `USER/LOGS`, und der SERVO-Bildschirm nennt `USER-LOGS AUS` an der Stelle der Datei. Nach dem Start eines neuen Panels oder einem Update sind beide an.

Ordner Auto-Logs und Ordner User-Logs unter [SETUP > SD-KARTE](setup.md#sd-karte) stellen andere Ordner ein. Ein geänderter Ordner gilt ab der nächsten Datei, nummeriert nach dem, was dieser Ordner enthält. Dateien in einem vorher benutzten Ordner werden erst wieder gelistet, wenn der Ordner zurückgestellt ist.

Das Panel legt jeden Ordner an, wenn es zum ersten Mal hineinschreibt, eine Ebene nach der anderen. Eine Datei namens `BENCH` oder `USER` im Stammverzeichnis verhindert das: Das Band zeigt `Ordner BENCH/LOGS nicht anlegbar. Lauf nicht aufgezeichnet.`

Jeder Ordner hat seine eigene Reihe: ein neuer Lauf nimmt eins über der höchsten Nummer in seinem Ordner, gleich unter welchem Namen, die Namen von Firmware 0.21.0 eingeschlossen. Eine Lücke, die eine gelöschte Datei hinterlässt, wird nicht gefüllt. Ein Ordner mit einer Nummer 999 nimmt keinen weiteren Lauf an, ob niedrigere Nummern frei sind oder nicht. Die Liste zeigt einen allein stehenden Bericht, `LIMITnnn.TXT` oder im Stammverzeichnis `SERVOnnn.TXT` ohne `SERVOnnn.CSV` daneben. Ein Bericht neben der CSV-Datei seines Laufs, `ATESTnnn.TXT`, steht nicht in der Liste, und LÖSCHEN auf dem Lauf entfernt ihn mit. Die Läufe stehen nach Nummer, der neueste zuerst, über alle Namen: `MOTOR043.CSV`, dann `SERVO042.CSV`, dann `SUPPL041.CSV`.

Firmware 0.21.0 und älter schreibt ins Stammverzeichnis und liest `BENCH/LOGS` und `USER/LOGS` nicht.

Ein Lauf wird alle 20 Zeilen oder alle 1000 ms auf die Karte festgeschrieben, je nachdem, was zuerst eintritt. Ein Stromausfall mitten im Lauf kostet unter 1,0 s an Zeilen, solange die Karte mithält, und bis zu 84 Zeilen, 4,20 s bei 20 Hz, wenn sie stockt.

Das Band meldet, was die Karte mit einem Lauf gemacht hat:

| Meldung | Bedeutung |
|---|---|
| `Keine Karte. Dieser Lauf wird nicht aufgezeichnet.` | nichts ist eingebunden |
| `Ordner BENCH/LOGS nicht anlegbar. Lauf nicht aufgezeichnet.` | eine Ebene des Ordners lässt sich nicht anlegen: eine Datei trägt ihren Namen, oder die Karte nimmt keinen neuen Ordner an |
| `Karte nicht lesbar. Lauf nicht aufgezeichnet.` | die Karte lässt sich nicht auflisten |
| `Karte voll oder nicht beschreibbar. Lauf nicht aufgezeichnet.` | keine Laufdatei ließ sich anlegen |
| `Karte zu langsam. Lauf nicht aufgezeichnet.` | jede Zeile ging verloren |
| `Die Karte war zu langsam. In der Aufzeichnung fehlen Daten.` | einige Zeilen gingen verloren; die Zeitspalte zeigt wo |
| `Daten können nicht mehr auf die Karte gespeichert werden. Aufzeichnung endet hier.` | ein Schreiben ist mitten im Lauf gescheitert |
| `Daten konnten nicht mehr auf die Karte geschrieben werden. Aufzeichnung unvollständig.` | dasselbe, beim Schließen des Laufs erneut gemeldet |
| `Letzter Schreibvorgang auf die Karte fehlgeschlagen. Aufzeichnung unvollständig.` | die Zeilen seit dem letzten Festschreiben fehlen in der Datei |

Die ersten fünf hinterlassen keine Datei. Die letzten vier hinterlassen eine Datei, die zu früh endet.

## Ein Bericht

![Der Bericht eines ENDLAGENLAUFS als Text](view:logs-report)

ÖFFNEN auf einem allein stehenden Bericht zeigt seinen Text: Der Dateiname steht über dem Feld, 15 Zeilen zu bis zu 92 Zeichen stehen auf einmal darin.

- Eine Zeile, die länger ist als eine Bildschirmzeile, bricht am letzten Leerzeichen um; ein längeres Wort wird am Zeilenende abgeschnitten.
- Ziehen auf den Zeilen oder am Rollbalken rollt sie. ZURÜCK führt zur Liste und gibt den Text frei; das Verlassen von LOGS gibt ihn auch frei.
- Eine Zeile endet an einem Zeilenvorschub, an einem Wagenrücklauf und an einem Paar aus beiden.
- Eine Datei von 0 Bytes zeigt `Die Datei ist leer.`
- Die Ansicht fasst die ersten 32 KiB einer Datei. Eine längere endet mit `Nur die ersten 32 kB werden gezeigt.`
- Ein Bericht, der sich nicht öffnen lässt, bleibt in der Liste, und die Fußzeile sagt warum, zum Beispiel `LIMIT044.TXT lässt sich nicht öffnen.`

[WLAN](wifi.md) lädt jeden Bericht herunter, auch die neben einer CSV-Datei.

## Die Importansicht

![Was der Import erkannt hat](view:logs-import)

ÖFFNEN liest die Datei und zeigt, was der Import entschieden hat, bevor etwas gezeichnet wird.

| Zeile | Zeigt |
|---|---|
| TRENNZEICHEN | SEMIKOLON, KOMMA, TAB oder SENKRECHTER STRICH |
| ZAHLENFORMAT | ENGLISCH (Dezimalpunkt) oder DEUTSCH (Dezimalkomma) |
| ZEILEN, SPALTEN | die gefundenen Datenzeilen und Spalten |
| ZEITACHSE | die Spalte, die als Zeit gilt, oder ZEILENINDEX |
| UNGLEICHE ZEILEN | Zeilen mit einer anderen Zellenzahl als die Kopfzeile |
| BELEGE | die Zellen, die Deutsch, Englisch oder keins von beiden belegt haben |

Die rechte Liste nennt jede Spalte mit ihrem ersten Wert und ihrem Bereich, vom kleinsten zum größten. Eine Datei öffnet mit bis zu 4 gewählten Spalten: in einer SERVO-Datei `pulse`, `angle`, `current` und `voltage`, jede, wo sie Zahlen hält, dann die nächsten Zahlenspalten; in jeder anderen Datei ihre ersten 4 Zahlenspalten. Ein Tipp auf eine Zahlenspalte nimmt sie in die Grafik auf oder heraus; der Titel zählt sie, `GRAFIK  4/4`. TRENNZEICHEN und ZAHLENFORMAT schalten durch die Möglichkeiten, AUTO zuerst, wenn Werte oder Spalten falsch aussehen. ZURÜCK führt zur Liste; GRAFIK zeichnet die gewählten Spalten.

Eine letzte Zeile ohne Zeilenvorschub ist eine Zeile, die ein Stromausfall oder eine volle Karte abgeschnitten hat. Sie wird weggelassen, ZEILEN steht in der Warnfarbe, und die Ansicht zeigt `LETZTE ZEILE UNVOLLSTÄNDIG. ÜBERSPRUNGEN.`.

## Die Grafik

![Die Grafik mit vier Spalten](view:logs-plot)

Der Kopf nennt die Datei, ihre Zeilen, ihre Dauer und ihre Abtastrate. Jede Spalte hat einen Chip mit ihrem Bereich und dem Wert am Cursor.

- Ein Finger, `<` und `>` bewegen den Cursor. Die Zeile unten rechts zeigt seine Zeit und seinen Messpunkt.
- Zwei Finger zoomen: spreizen zum Vergrößern, zusammenziehen zum Verkleinern, beide bewegen zum Verschieben. Die engste Ansicht sind 8 Messpunkte, die weiteste der ganze Lauf.
- Mit SETUP, PANEL, Ein Finger schiebt eingeschaltet verschiebt ein Finger, der sich mehr als 10 px bewegt, eine vergrößerte Ansicht seitwärts.
- ZURÜCK führt zur Importansicht.

Die Zeilen stehen gleichmäßig nach Zeilennummer. Die Beschriftungen unter der Grafik sind die Zeitspalte der Datei; eine Lücke in den Zeilen ist ein Sprung in den Beschriftungen.

## Eine Datei löschen

![Die Rückfrage von LÖSCHEN](view:logs-delete)

LÖSCHEN öffnet eine Rückfrage, die die Datei und ihre Größe nennt. Nur ihr eigenes LÖSCHEN, auf dieser Taste gedrückt und losgelassen, entfernt die Datei. ABBRECHEN oder das Verlassen des Bildschirms löscht nichts. Der Lauf, den der Logger offen hat, wird abgelehnt.

## Kartenformate

Das Panel liest Karten mit FAT12, FAT16, FAT32 oder exFAT (extended File Allocation Table): die ganze Karte oder die erste der 4 Partitionen eines MBR (Master Boot Record), die eines davon trägt. SDHC-Karten (4 GB bis 32 GB) kommen mit FAT32, SDXC-Karten (64 GB bis 2 TB) mit exFAT; beide mounten so, wie sie kommen.

- Nicht unterstützt: GPT-Partitionierung (GUID Partition Table; GUID: Globally Unique Identifier), Volumes über 2 TiB, NTFS, ext4, HFS+ und APFS. Eine solche Karte mountet nicht. Das Panel formatiert nie eine Karte.
- Zum Formatieren dient der SD Memory Card Formatter der SD Association, oder am Rechner FAT32 oder exFAT auf einem MBR (unter macOS: Schema `Master Boot Record`).
- Eine Datei ab 4 GiB auf einer exFAT-Karte steht mit ihrer Größe in der Liste, zum Beispiel `8.00 GB`. OPEN liest Dateien bis 2 GiB weniger 1 Byte; eine größere bleibt in der Liste, und OPEN meldet `BENCH001.CSV ist 2 GB oder größer. Nicht geöffnet.` [WLAN](wifi.md) lädt sie herunter.

## Verwandte Seiten

- [WLAN](wifi.md): die Dateien der Karte im Browser herunterladen.
- [Motor und ESC](motor.md), [Netzteil](supply.md) und [Servo](servo.md): die Läufe, die diese Dateien enthalten.
