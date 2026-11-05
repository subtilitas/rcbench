---
screen: LOGS
title: Logs
order: 50
---

# Logs

LOGS listet CSV-Aufzeichnungen (Comma-Separated Values) und Berichte auf der SD-Karte (Secure Digital) des Panels. Die Seite zeigt eine Importvorschau, zeichnet bis zu 4 CSV-Spalten und öffnet Berichte im [Textbetrachter von Files](files.md#der-textbetrachter). Öffne sie über die Kachel LOGS im [Menü](overview.md).

Drei Reiter oben links wählen den Ordner: STAMM für das Stammverzeichnis, USER für den Ordner Testaufzeichnungen (Vorgabe `USER/LOGS`) und BENCH für den Ordner Auto-Aufzeichnungen (Vorgabe `BENCH/LOGS`). WI-FI unten links öffnet [WLAN](wifi.md). FILES daneben öffnet [Files](files.md) zum Kopieren, Verschieben, Umbenennen und Löschen.

Das Home-Feld führt wie ZURÜCK jeweils eine Ebene zurück: von der Grafik zur Importansicht, dann zur Dateiliste und zum Menü. In der Rückfrage zu LÖSCHEN wirkt es wie ABBRECHEN.

## Keine Karte

![LOGS ohne Karte im Steckplatz](view:logs-nocard)

Ohne eingebundene Karte zeigt der Bereich `KEINE AUFZEICHNUNGEN` und `Keine Karte. Setze eine Karte ein und tippe auf AKTUALISIEREN.` Bei einem leeren oder noch nicht angelegten Ordner erscheint `Keine .csv-Dateien in BENCH/LOGS.` oder `Keine .csv-Dateien in USER/LOGS.`, im Stammverzeichnis `Keine .csv-Dateien im Hauptverzeichnis.`

Die Zeile darunter beschreibt den Reiter: `Läufe heißen SERVOnnn, MOTORnnn oder SUPPLnnn.CSV.`, `Tests werden als ATESTnnn.CSV und LIMITnnn.TXT gespeichert.` oder `Läufe von Firmware 0.21.0 und älter.` Sind AUTO-AUFZEICHNUNGEN oder TESTAUFZEICHNUNGEN unter [SETUP > SD CARD](setup.md) ausgeschaltet, zeigt BENCH beziehungsweise USER stattdessen `AUTO-AUFZEICHNUNGEN AUS unter SD CARD.` oder `TESTAUFZEICHNUNGEN AUS unter SD CARD.`

AKTUALISIEREN liest die Karte erneut. Die Reiter funktionieren auch ohne Karte; jeder Tipp prüft erneut, ob eine eingesetzt wurde.

## Eine Karte, die sich nicht einbinden lässt

![LOGS mit einer Karte ohne FAT-Volume](view:logs-nofat)

Eine Karte mit NTFS (New Technology File System), GPT (GUID-Partitionstabelle) oder ohne Dateisystem zeigt `Karte ohne FAT32 oder exFAT. Formatiere sie am Computer.` Bei anderen Einbindefehlern erscheint `Karte nicht lesbar. Prüfe sie am PC, dann tippe AKTUALISIEREN.` [Kartenformate](#kartenformate) beschreibt die unterstützten Formate und das Formatieren. Formatieren löscht die Karte. Setze sie anschließend wieder ein und tippe auf AKTUALISIEREN.

## Die Dateiliste

![Die Dateiliste](view:logs)

Die Kopfzeile zeigt rechts neben den Reitern den gewählten Ordner, etwa `BENCH/LOGS`. Ganz rechts steht die Datenträgerbezeichnung, etwa `UNITEST`, ersatzweise der von der Karte gemeldete Name oder `SD CARD`. Die Liste zeigt 7 Dateien mit ihren Größen. Jede Zeile bietet eine 48px (etwa 7,2mm) hohe Berührungsfläche.

| Reiter | Ordner | Inhalt |
|---|---|---|
| STAMM | Stammverzeichnis | Läufe von Firmware 0.21.0 und älter |
| USER | Ordner Testaufzeichnungen, Vorgabe `USER/LOGS` | Dateien des AUTOMATISCHEN TESTS und des ENDLAGENLAUFS |
| BENCH | Ordner Auto-Aufzeichnungen, Vorgabe `BENCH/LOGS` | automatische Aufzeichnungen bei ARMED und Netzteilläufen |

LOGS öffnet den Reiter der zuletzt vom Panel geschriebenen Datei. Nach dem Start gilt BENCH, bis eine Datei geschrieben wurde. Ohne neue Datei seit dem letzten Besuch bleibt der zuvor gewählte Reiter erhalten. Ein Tipp auf einen Reiter liest seinen Ordner ab der ersten Seite.

Die obere Leiste LOGS | FILES wechselt zwischen den beiden Seiten der Kachel. Das Home-Feld führt jeweils zum Menü. Die Leiste erscheint nur bei der Dateiliste, nicht über einer offenen Datei, Rückfrage oder einem Dialogfeld.

WI-FI in der Fußzeile öffnet [WLAN](wifi.md) für Downloads im Browser. Das dortige Home-Feld führt hierher zurück. Über der Löschrückfrage oder einer offenen Datei fehlt die Taste. Auch bei ARMED lässt sich WI-FI öffnen; die Bedienelemente der WLAN-Seite prüfen ihre eigenen Sperrbedingungen.

| Taste | Was sie tut |
|---|---|
| AKTUALISIEREN | liest die gezeigte Seite erneut und wählt ihren neuesten Lauf |
| VORIGE, NÄCHSTE | die Seite davor und danach; sichtbar mit mehr als 48 Dateien auf der Karte |
| LÖSCHEN | fragt nach und entfernt dann die gewählte Datei; grau, solange nichts gewählt ist |
| ÖFFNEN | öffnet die gewählte CSV-Datei in der Importansicht, einen Bericht im Textbetrachter; grau, solange nichts gewählt ist |

![Die vierte Seite einer vollen Karte](view:logs-pages)

- Eine Seite fasst 48 Einträge; eine Karte fasst bis zu 999 Läufe. Die Sortierung beginnt beim neuesten Lauf. Berichte folgen ihrer eigenen Nummer und stehen hinter der zugehörigen CSV-Datei; danach folgen prüfstandsfremde Dateien. Innerhalb einer Seite gilt dieselbe Reihenfolge. Bei mehr als 48 Dateien zeigt der Reiter den sichtbaren Bereich, etwa `DATEIEN 145 BIS 192 VON 1000`; auf einer einzigen Seite zählt er die ganze Liste, etwa `DATEIEN 1 BIS 7 VON 7`, und ein leerer Ordner zeigt `DATEIEN`.
- Seiten werden vom Listenanfang in Gruppen zu 48 Einträgen gebildet. Liegen vor der aktuellen Seite weniger als 48 Einträge, etwa nach einer neuen Aufzeichnung, führt VORIGE zur ersten Seite.
- Neue Aufzeichnungen erscheinen auf Seite 1, auch wenn gerade eine spätere Seite offen ist. Beim Zurückblättern ab Seite 3 überspringt VORIGE dadurch so viele Einträge, wie hinzugekommen sind. NÄCHSTE von Seite 1 zeigt sie; die Zahlen im Reiter machen die Verschiebung sichtbar.
- Der Rollbalken am rechten Rand ist 56px (etwa 8,4mm) breit und scrollt innerhalb der Seite. Tippe auf seine Bahn zum Springen oder ziehe den Griff.
- Beim Öffnen liest LOGS die erste Seite und wählt den neuesten Lauf. Aufzeichnungen, die bei geschlossenem LOGS entstanden sind, erscheinen ohne AKTUALISIEREN.
- Auf späteren Seiten liest AKTUALISIEREN ab dem ersten Eintrag der aktuellen Seite. Gibt es inzwischen weniger Einträge, erscheint die letzte Seite.
- Nach LÖSCHEN wird die Seite neu eingelesen; die Scrollposition bleibt erhalten. Die Auswahl wird aufgehoben. LÖSCHEN und ÖFFNEN bleiben grau, bis du eine Zeile wählst.
- Kartenwechsel bei laufendem Panel werden ohne Neustart erkannt: bei AKTUALISIEREN, beim Öffnen von LOGS, beim Seitenwechsel, bei ÖFFNEN oder beim Start eines Laufs. Eine neue Karte öffnet auf Seite 1. Ohne Karte erscheint `Keine Karte. Setze eine Karte ein und tippe auf AKTUALISIEREN.`
- Neuester Lauf bedeutet höchste Nummer im Namen. Dateidaten hängen von der Paneluhr ab. Sie ist nach jedem Einschalten ungestellt; bis NTP sie im Heimnetz stellt, erhalten Dateien das Jahr 1980 ([Setup](setup.md#panel)).
- Eine noch laufende Aufzeichnung wird nicht gelistet. Dateien mit derselben Nummer in anderen Ordnern werden angezeigt.
- Tippe auf eine Zeile zum Auswählen. Ziehen über mehr als 8px scrollt die Liste ohne Auswahl.

## Was ein Lauf schreibt

Jede Datei enthält einen Lauf und liegt im Ordner seiner Art. Der Name kennzeichnet den Auslöser:

| Ordner | Name | Lauf |
|---|---|---|
| `BENCH/LOGS` | `SERVOnnn.CSV` | ein Schalten auf ARMED von SERVO |
| `BENCH/LOGS` | `MOTORnnn.CSV` | ein Schalten auf ARMED von ARM auf MOTOR & ESC oder einem Stick-Lauf auf dessen PROGRAMMIEREN |
| `BENCH/LOGS` | `SUPPLnnn.CSV` | ein Einschalten des SUPPLY-Ausgangs, solange der Prüfstand SAFE ist |
| `USER/LOGS` | `ATESTnnn.CSV` | ein AUTOMATISCHER TEST; sein Bericht ist `ATESTnnn.TXT` |
| `USER/LOGS` | `LIMITnnn.TXT` | der Bericht eines ENDLAGENLAUFS, ohne CSV daneben |
| Stammverzeichnis | `BENCHnnn.CSV`, `ESCnnn.CSV`, `SERVOnnn.CSV` | Läufe von Firmware 0.21.0 und älter; im Stammverzeichnis ist `SERVOnnn` ein Servotest |
| Stammverzeichnis | `SERVOnnn.TXT` allein | ein Endlagenlauf von Firmware 0.21.0 und älter |

Der Name richtet sich nach der Seite, deren ARM den Prüfstand aktiviert hat. Das gilt auch bei IOmcu-Versionen vor 0.18.0, die bei jedem Schalten auf ARMED die Gasausgänge mit aktivieren.

Sind AUTO-AUFZEICHNUNGEN unter [SETUP > SD CARD](setup.md) ausgeschaltet, entsteht keine Datei in `BENCH/LOGS`, und es wird keine Nummer vergeben. Ausgeschaltete TESTAUFZEICHNUNGEN verhindern Dateien in `USER/LOGS`; SERVO zeigt statt des Dateinamens `TESTAUFZEICHNUNGEN AUS`. Nach dem Start eines neuen Panels oder einem Update sind beide Einstellungen eingeschaltet.

Wähle andere Speicherorte mit Ordner Auto-Aufzeichnungen und Ordner Testaufzeichnungen unter [SETUP > SD CARD](setup.md#sd-card). Die Änderung gilt ab der nächsten Datei. Ihre Nummer folgt dem Inhalt des neuen Ordners. Dateien im bisherigen Ordner erscheinen erst wieder, wenn du diesen erneut auswählst.

Das Panel legt jeden Ordner an, wenn es zum ersten Mal hineinschreibt, eine Ebene nach der anderen. Eine Datei namens `BENCH` oder `USER` im Stammverzeichnis verhindert das: Das Band zeigt `Ordner BENCH/LOGS nicht anlegbar. Lauf nicht aufgezeichnet.`

Jeder Ordner hat eine eigene Nummernfolge. Ein neuer Lauf erhält die höchste vorhandene Nummer plus eins, unabhängig vom Namenspräfix und einschließlich der Namen aus Firmware 0.21.0. Gelöschte Nummern werden nicht erneut verwendet. Enthält ein Ordner die Nummer 999, nimmt er keinen weiteren Lauf an, auch bei freien niedrigeren Nummern.

Die Liste zeigt Berichte mit Laufnamen: `ATESTnnn.TXT` mit oder ohne zugehörige `ATESTnnn.CSV`, `LIMITnnn.TXT` und im Stammverzeichnis `SERVOnnn.TXT` aus Firmware 0.21.0 und älter. Lauf und Bericht stehen in getrennten Zeilen, die CSV zuerst. LÖSCHEN beim Lauf löscht den Bericht mit. Andere `.TXT`-Dateien zeigt nur FILES.

Die Sortierung folgt absteigenden Nummern über alle Präfixe hinweg: `MOTOR043.CSV`, dann `SERVO042.CSV`, dann `SUPPL041.CSV`.

Firmware 0.21.0 und älter schreibt ins Stammverzeichnis und liest `BENCH/LOGS` und `USER/LOGS` nicht.

Ein Lauf wird alle 20 Zeilen oder alle 1000ms auf die Karte festgeschrieben, je nachdem, was zuerst eintritt. Ein Stromausfall mitten im Lauf kostet unter 1,0s an Zeilen, solange die Karte mithält, und bis zu 84 Zeilen, 4,20s bei 20Hz, wenn sie stockt.

Das Band meldet Kartenfehler, die die Aufzeichnung betreffen:

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

![Der Bericht eines ENDLAGENLAUFS im Textbetrachter](view:logs-report)

ÖFFNEN zeigt einen Bericht schreibgeschützt im [Textbetrachter](files.md#der-textbetrachter). Im Band steht dann FILES. Das Home-Feld führt zu LOGS zurück, wo der Bericht wieder ausgewählt ist. Berichte über 1MiB bleiben in der Liste; die Fußzeile meldet `Über 1MB: zu groß. Lade die Datei über WLAN herunter.`

Berichte lassen sich auch über [WLAN](wifi.md) herunterladen.

## Die Importansicht

![Was der Import erkannt hat](view:logs-import)

ÖFFNEN liest die Datei und zeigt vor dem Zeichnen das erkannte Importformat.

| Zeile | Zeigt |
|---|---|
| TRENNZEICHEN | SEMIKOLON, KOMMA, TAB oder SENKRECHTER STRICH |
| ZAHLENFORMAT | ENGLISCH (Dezimalpunkt) oder DEUTSCH (Dezimalkomma) |
| ZEILEN, SPALTEN | die gefundenen Datenzeilen und Spalten |
| ZEITACHSE | die Spalte, die als Zeit gilt, oder ZEILENINDEX |
| UNGLEICHE ZEILEN | Zeilen mit einer anderen Zellenzahl als die Kopfzeile |
| BELEGE | die Zellen, die Deutsch, Englisch oder keins von beiden belegt haben |

Die rechte Liste zeigt jede Spalte mit ihrem ersten Wert und dem Bereich vom kleinsten bis zum größten Wert. Bis zu 4 Spalten werden automatisch gewählt. Bei SERVO-Dateien haben die numerischen Spalten `pulse`, `angle`, `current` und `voltage` Vorrang, danach folgen weitere Zahlenspalten. Bei anderen Dateien werden die ersten 4 Zahlenspalten gewählt.

Tippe auf eine Zahlenspalte, um sie zur Grafik hinzuzufügen oder daraus zu entfernen. Der Titel zählt die Auswahl, etwa `GRAFIK  4/4`. Sehen Werte oder Spalten falsch aus, wechsle mit TRENNZEICHEN und ZAHLENFORMAT durch die Möglichkeiten, beginnend bei AUTO. ZURÜCK führt zur Dateiliste. GRAFIK zeichnet die gewählten Spalten.

Eine letzte Zeile ohne Zeilenvorschub gilt als unvollständig, etwa nach Stromausfall oder bei voller Karte. Sie wird übersprungen. ZEILEN erscheint in der Warnfarbe, daneben steht `LETZTE ZEILE UNVOLLSTÄNDIG. ÜBERSPRUNGEN.`.

## Die Grafik

![Die Grafik mit vier Spalten](view:logs-plot)

Die Kopfzeile nennt Datei, Zeilenzahl, Dauer und Abtastrate. Jede Spalte hat ein Anzeigefeld mit Wertebereich und Cursorwert.

- Bewege den Cursor mit einem Finger oder mit `<` und `>`. Unten rechts stehen Zeit und Messpunkt.
- Zoome mit zwei Fingern: auseinanderziehen zum Vergrößern, zusammenziehen zum Verkleinern. Bewege beide Finger zum Verschieben. Die Ansicht reicht von mindestens 8 Messpunkten bis zum gesamten Lauf.
- Ist Ein Finger schiebt unter SETUP, PANEL eingeschaltet, verschiebt ein Finger eine vergrößerte Ansicht seitwärts, sobald er sich mehr als 10px bewegt.
- ZURÜCK führt zur Importansicht.

Die Grafik verteilt Datenzeilen gleichmäßig nach Zeilennummer. Ihre Beschriftung stammt aus der Zeitspalte. Eine Datenlücke erscheint daher als Sprung in den Zeitangaben.

## Eine Datei löschen

![Die Rückfrage von LÖSCHEN](view:logs-delete)

LÖSCHEN fragt nach und nennt Dateiname und Größe. Drücke und löse LÖSCHEN innerhalb dieses Dialogs, um die Datei zu entfernen. ABBRECHEN oder das Verlassen der Seite behält sie. Eine noch geöffnete Aufzeichnung lässt sich nicht löschen.

## Kartenformate

Das Panel liest FAT12, FAT16, FAT32 und exFAT (extended File Allocation Table). Es unterstützt das Dateisystem auf der ganzen Karte oder die erste der 4 MBR-Partitionen (Master Boot Record) mit einem dieser Formate. SDHC-Karten von 4GB bis 32GB sind ab Werk mit FAT32, SDXC-Karten von 64GB bis 2TB mit exFAT formatiert. Beide lassen sich im Auslieferungszustand einbinden.

- Nicht unterstützt werden GPT-Partitionierung (GUID Partition Table; GUID: Globally Unique Identifier), Volumes über 2TiB sowie NTFS, ext4, HFS+ und APFS. Solche Karten lassen sich nicht einbinden. Das Panel formatiert keine Karten.
- Verwende zum Formatieren den SD Memory Card Formatter der SD Association oder formatiere am Computer mit FAT32 oder exFAT auf MBR. Unter macOS heißt das Schema `Master Boot Record`.
- Dateien ab 4GiB auf exFAT erscheinen mit ihrer Größe, etwa `8.00GB`. ÖFFNEN liest bis zu 2GiB minus 1 Byte. Größere Dateien bleiben in der Liste, mit der Meldung `BENCH001.CSV ist 2GB oder größer. Nicht geöffnet.` Über [WLAN](wifi.md) lassen sie sich herunterladen.

## Verwandte Seiten

- [WLAN](wifi.md): die Dateien der Karte im Browser herunterladen.
- [Motor und ESC](motor.md), [Netzteil](supply.md) und [Servo](servo.md): die Läufe, die diese Dateien enthalten.
