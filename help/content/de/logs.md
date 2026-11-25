---
screen: LOGS
title: Logs
order: 50
---

# Logs

LOGS listet CSV-Aufzeichnungen (Comma-Separated Values) und Berichte auf der SD-Karte (Secure Digital) des Panels. Die Seite zeigt eine Importvorschau, zeichnet bis zu 4 CSV-Spalten und öffnet Berichte im [Textbetrachter von Files](files.md#der-textbetrachter). Öffne sie über die Kachel LOGS im [Menü](overview.md).

Drei Reiter oben links wählen den Ordner: STAMM für das Stammverzeichnis, USER für den Ordner Testaufzeichnungen (Vorgabe `USER/LOGS`) und BENCH für den Ordner Auto-Aufzeichnungen (Vorgabe `BENCH/LOGS`). WI-FI unten links öffnet [WLAN](wifi.md). FILES daneben öffnet [Files](files.md) zum Kopieren, Verschieben, Umbenennen und Löschen.

Das Home-Feld führt wie ZURÜCK jeweils eine Ebene zurück: von der Grafik dorthin, woher sie kam (nach ÖFFNEN zur Dateiliste, nach GRAFIK zur Importansicht, nach GRAFIK einer Ergebniskarte zu dieser Karte), von der Importansicht zur Dateiliste und zum Menü. Über der Liste von MEHR schließt es die Liste. In der Rückfrage zu LÖSCHEN wirkt es wie ABBRECHEN.

## Keine Karte

![LOGS ohne Karte im Steckplatz](view:logs-nocard)

Ohne eingebundene Karte zeigt der Bereich `KEINE AUFZEICHNUNGEN` und `Keine Karte. Setze eine Karte ein und tippe auf AKTUALISIEREN.` Bei einem leeren oder noch nicht angelegten Ordner erscheint `Keine .csv-Dateien in BENCH/LOGS.` oder `Keine .csv-Dateien in USER/LOGS.`, im Stammverzeichnis `Keine .csv-Dateien im Hauptverzeichnis.`

Die Zeile darunter beschreibt den Reiter: `Läufe heißen SERVOnnn, MOTORnnn oder SUPPLnnn.CSV.`, `Tests heißen ATESTnnn, LIMITnnn und RUDDRnnn.CSV.` oder `Läufe von Firmware 0.21.0 und älter.` Sind AUTO-AUFZEICHNUNGEN oder TESTAUFZEICHNUNGEN unter [SETUP > SD CARD](setup.md) ausgeschaltet, zeigt BENCH beziehungsweise USER stattdessen `AUTO-AUFZEICHNUNGEN AUS unter SD CARD.` oder `TESTAUFZEICHNUNGEN AUS unter SD CARD.`

AKTUALISIEREN liest die Karte erneut. Die Reiter funktionieren auch ohne Karte; jeder Tipp prüft erneut, ob eine eingesetzt wurde.

## Eine Karte, die sich nicht einbinden lässt

![LOGS mit einer Karte ohne FAT-Volume](view:logs-nofat)

Eine Karte mit NTFS (New Technology File System), GPT (GUID-Partitionstabelle) oder ohne Dateisystem zeigt `Karte ohne FAT32 oder exFAT. Formatiere sie am Computer.` Bei anderen Einbindefehlern erscheint `Karte nicht lesbar. Prüfe sie am PC, dann tippe AKTUALISIEREN.` [Kartenformate](#kartenformate) beschreibt die unterstützten Formate und das Formatieren. Formatieren löscht die Karte. Setze sie anschließend wieder ein und tippe auf AKTUALISIEREN.

## Die Dateiliste

![Die Dateiliste](view:logs)

Die Kopfzeile zeigt rechts neben den Reitern den gewählten Ordner, etwa `BENCH/LOGS`. Ganz rechts steht die Datenträgerbezeichnung, etwa `UNITEST`, ersatzweise der von der Karte gemeldete Name oder `SD CARD`. Die Liste zeigt 5 Dateien mit ihren Größen. Jede Zeile bietet eine 48px (etwa 7,2mm) hohe Berührungsfläche.

| Reiter | Ordner | Inhalt |
|---|---|---|
| STAMM | Stammverzeichnis | Läufe von Firmware 0.21.0 und älter |
| USER | Ordner Testaufzeichnungen, Vorgabe `USER/LOGS` | Dateien des AUTOMATISCHEN TESTS und von LIMIT RUN |
| BENCH | Ordner Auto-Aufzeichnungen, Vorgabe `BENCH/LOGS` | automatische Aufzeichnungen bei ARMED und Netzteilläufen |

LOGS öffnet den Reiter der zuletzt vom Panel geschriebenen Datei. Nach dem Start gilt BENCH, bis eine Datei geschrieben wurde. Ohne neue Datei seit dem letzten Besuch bleibt der zuvor gewählte Reiter erhalten. Ein Tipp auf einen Reiter liest seinen Ordner ab der ersten Seite.

Die obere Leiste LOGS | FILES wechselt zwischen den beiden Seiten der Kachel. Das Home-Feld führt jeweils zum Menü. Die Leiste erscheint nur bei der Dateiliste, nicht über einer offenen Datei, Rückfrage oder einem Dialogfeld.

GRAFIK auf LIMIT RUN, RUDER AUTO, TEST, MOTOR & ESC und SUPPLY öffnet den letzten Lauf dieser Karte seit dem Einschalten hier als Grafik. Beim Verlassen schaltet die Karte den Prüfstand auf SAFE. Das Home-Feld auf der Grafik führt zur Karte zurück, deren Ergebnis weiter dasteht. Die Taste ist grau, solange der Prüfstand ARMED ist oder die Datei des Laufs noch offen ist; ein Tipp sagt dann warum.

WI-FI in der Tastenzeile öffnet [WLAN](wifi.md) für Downloads im Browser. Das dortige Home-Feld führt hierher zurück. Über der Löschrückfrage oder einer offenen Datei fehlt die Taste. Auch bei ARMED lässt sich WI-FI öffnen; die Bedienelemente der WLAN-Seite prüfen ihre eigenen Sperrbedingungen.

| Taste | Was sie tut |
|---|---|
| AKTUALISIEREN | liest die gezeigte Seite erneut und wählt ihren neuesten Lauf |
| VORIGE, NÄCHSTE | die Seite davor und danach; sichtbar mit mehr als 48 Dateien auf der Karte |
| LÖSCHEN | fragt nach und entfernt dann die gewählte Datei; grau, solange nichts gewählt ist, bei ARMED und während ein Lauf aufgezeichnet wird |
| ÖFFNEN | öffnet eine Laufdatei des Prüfstands in der Grafik, jede andere CSV-Datei in der Importansicht, einen Bericht im Textbetrachter; grau, solange nichts gewählt ist |

Die Fußleiste unter der Liste nennt den Bildschirm, LOGS. OPTIONEN rechts öffnet die Liste SD CARD von [SETUP](setup.md#sd-card): die Aufzeichnungsschalter und die beiden Aufzeichnungsordner. Deren Home-Feld führt hierher zurück.

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
| `USER/LOGS` | `LIMITnnn.CSV` | ein Lauf von LIMIT RUN, eine Zeile je bewerteter Stellung; sein Bericht ist `LIMITnnn.TXT`, allein bei einem Lauf, der keine Stellung bewertet hat, oder einem von Firmware 0.23.17 und älter |
| `USER/LOGS` | `RUDDRnnn.CSV` | ein Lauf von RUDER AUTO, eine Zeile je Messung; sein Bericht ist `RUDDRnnn.TXT` |
| Stammverzeichnis | `BENCHnnn.CSV`, `ESCnnn.CSV`, `SERVOnnn.CSV` | Läufe von Firmware 0.21.0 und älter; im Stammverzeichnis ist `SERVOnnn` ein Servotest |
| Stammverzeichnis | `SERVOnnn.TXT` allein | ein Endlagenlauf von Firmware 0.21.0 und älter |

Die letzte Spalte jeder CSV des Prüfstands, `info`, beschreibt den Lauf in der ersten Datenzeile: `dut=MG90S&note=greased&date=2026-11-24T10:31&fw=0.23.18`. `dut` ist der Name des Prüflings und `note` die Notiz zum Lauf aus den [SERVO-Listen](servo-settings.md), je bis 23 Zeichen; `date` ist das lokale Datum mit Uhrzeit des Panels beim Start und fehlt, solange die Uhr nicht gestellt ist; `fw` ist die Firmware. AUTOMATISCHER TEST, LIMIT RUN und RUDER AUTO fügen `servo`, `meter` und `angle` an; MOTOR- und SUPPLY-Dateien tragen kein `dut`. In einem Wert wird jedes `&`, `=`, `;` und `"` als `_` geschrieben. LOGS von Firmware 0.23.17 liest jede solche Datei außer einer SERVO-Datei mit drei INA3221-Gruppen, die 26 Spalten hat.

Der Name richtet sich nach der Seite, deren ARM den Prüfstand aktiviert hat. Das gilt auch bei IOmcu-Versionen vor 0.18.0, die bei jedem Schalten auf ARMED die Gasausgänge mit aktivieren.

Ein Laufname darf hinter seiner Nummer ein Label tragen, etwa `MOTOR042_kv1200.CSV`; LOG UMBENENNEN auf [Motor und ESC](motor.md#das-log-eines-laufs) schreibt eines. Das Label folgt auf ein `_`, hat hinter einem Präfix aus fünf Buchstaben höchstens 46 Bytes und darf Punkte und weitere `_` enthalten. Der Lauf behält Nummer, Art und Platz in der Liste, und der nächste Lauf erhält die Nummer darüber.

Sind AUTO-AUFZEICHNUNGEN unter [SETUP > SD CARD](setup.md) ausgeschaltet, entsteht keine Datei in `BENCH/LOGS`, und es wird keine Nummer vergeben. Ausgeschaltete TESTAUFZEICHNUNGEN verhindern Dateien in `USER/LOGS`; SERVO zeigt statt des Dateinamens `TESTAUFZEICHNUNGEN AUS`. Nach dem Start eines neuen Panels oder einem Update sind beide Einstellungen eingeschaltet.

Wähle andere Speicherorte mit Ordner Auto-Aufzeichnungen und Ordner Testaufzeichnungen unter [SETUP > SD CARD](setup.md#sd-card). Die Änderung gilt ab der nächsten Datei. Ihre Nummer folgt dem Inhalt des neuen Ordners. Dateien im bisherigen Ordner erscheinen erst wieder, wenn du diesen erneut auswählst.

Das Panel legt jeden Ordner an, wenn es zum ersten Mal hineinschreibt, eine Ebene nach der anderen. Eine Datei namens `BENCH` oder `USER` im Stammverzeichnis verhindert das: Das Band zeigt `Ordner BENCH/LOGS nicht anlegbar. Lauf nicht aufgezeichnet.`

Jeder Ordner hat eine eigene Nummernfolge. Ein neuer Lauf erhält die höchste vorhandene Nummer plus eins, unabhängig vom Namenspräfix und einschließlich der Namen aus Firmware 0.21.0. Gelöschte Nummern werden nicht erneut verwendet. Enthält ein Ordner die Nummer 999, nimmt er keinen weiteren Lauf an, auch bei freien niedrigeren Nummern.

Die Liste zeigt Berichte mit Laufnamen: `ATESTnnn.TXT` mit oder ohne zugehörige `ATESTnnn.CSV`, `LIMITnnn.TXT` mit oder ohne zugehörige `LIMITnnn.CSV` und im Stammverzeichnis `SERVOnnn.TXT` aus Firmware 0.21.0 und älter. Lauf und Bericht stehen in getrennten Zeilen, die CSV zuerst. LÖSCHEN beim Lauf löscht den Bericht mit. Andere `.TXT`-Dateien zeigt nur FILES.

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

![Der Bericht eines Laufs von LIMIT RUN im Textbetrachter](view:logs-report)

ÖFFNEN zeigt einen Bericht schreibgeschützt im [Textbetrachter](files.md#der-textbetrachter). Im Band steht dann FILES. Das Home-Feld führt zu LOGS zurück, wo der Bericht wieder ausgewählt ist. Berichte über 1MiB bleiben in der Liste; die Fußzeile meldet `Über 1MB: zu groß. Lade die Datei über WLAN herunter.`

Berichte lassen sich auch über [WLAN](wifi.md) herunterladen.

## Was wo öffnet

ÖFFNEN liest die Kopfzeile der Datei. Eine Laufdatei des Prüfstands öffnet sofort die Grafik, mit den Spalten ihrer Art. Die Art ergibt sich zuerst aus den Spaltennamen der Kopfzeile, dann aus dem Dateinamen. So werden auch ein Name mit Zusatz (`MOTOR042_kv1200.CSV`), ein Name in Kleinbuchstaben und die Dateien ab Firmware 0.21.0 erkannt.

| Art | Name | Gezeichnete Spalten |
|---|---|---|
| LIMIT RUN | `LIMITnnn.CSV` | `pulse`, `current`, `angle` |
| RUDDER AUTO | `RUDDRnnn.CSV` | `pulse`, `current`, `measured` |
| AUTOMATIC TEST | `ATESTnnn.CSV` | `command`, `current`, `voltage`, `angle` |
| SUPPLY-Lauf | `SUPPLnnn.CSV` | `set`, `voltage`, `limit`, `current` |
| MOTOR-Lauf | `MOTORnnn.CSV`, `ESCnnn.CSV` | `throttle`, `rpm`, `current`, `voltage` |
| SERVO-Lauf | `SERVOnnn.CSV`, `BENCHnnn.CSV` | `pulse`, `angle`, `current`, `voltage` |

Fehlt einem AUTOMATIC TEST, einem SUPPLY-, MOTOR- oder SERVO-Lauf eine Spalte, nimmt er die erste Zahlenspalte, deren Name auf diesen Namen endet: ein Lauf mit leeren Zellen in `current` und `voltage` zeichnet `ch1 current` und `ch1 voltage` von INA3221-Kanal 1. Jede andere Spalte, die der Datei fehlt, entfällt. Eine LIMIT-RUN- oder RUDDER-AUTO-Datei öffnet in [der Grafik](#die-grafik-eines-limit-run-oder-rudder-auto) über ihrem Puls, die anderen Arten in [der Grafik über ihrer Zeit](#ein-lauf-über-seiner-zeit). Jede andere CSV und ein Laufname über Spalten keiner Art öffnen die Importansicht.

## Die Importansicht

![Was der Import erkannt hat](view:logs-import)

Die Importansicht zeigt vor dem Zeichnen das erkannte Importformat. MEHR > SPALTEN in der Grafik öffnet sie für eine Laufdatei, mit den Spalten der Grafik gewählt.

| Zeile | Zeigt |
|---|---|
| TRENNZEICHEN | SEMIKOLON, KOMMA, TAB oder SENKRECHTER STRICH |
| ZAHLENFORMAT | ENGLISCH (Dezimalpunkt) oder DEUTSCH (Dezimalkomma) |
| ZEILEN, SPALTEN | die gefundenen Datenzeilen und Spalten |
| ZEITACHSE | die Spalte, die als Zeit gilt, oder ZEILENINDEX. Eine Einheit nach einem Unterstrich im Spaltenkopf wird gelesen (`time_ms`, `t_us`). Eine erste Spalte mit dem Namen eines Zählers (`step`, `row`, `index`, `sample`, `n`) ist keine Zeit: `RUDDRnnn.CSV` wird über dem Zeilenindex gezeichnet, und die Kopfzeile des Diagramms nennt seine Zeilen ohne Dauer und Abtastrate |
| UNGLEICHE ZEILEN | Zeilen mit einer anderen Zellenzahl als die Kopfzeile |
| BELEGE | die Zellen, die Deutsch, Englisch oder keins von beiden belegt haben |

Die rechte Liste zeigt jede Spalte mit ihrem ersten Wert und dem Bereich vom kleinsten bis zum größten Wert. Für eine Datei keiner Art werden bis zu 4 Spalten automatisch gewählt: hat die Datei eine numerische Spalte `pulse`, zuerst die numerischen Spalten `pulse`, `angle`, `current` und `voltage`, danach weitere Zahlenspalten; jede andere Datei beginnt mit ihren ersten 4 Zahlenspalten.

Tippe auf eine Zahlenspalte, um sie zur Grafik hinzuzufügen oder daraus zu entfernen. Der Titel zählt die Auswahl, etwa `GRAFIK  4/4`. Sehen Werte oder Spalten falsch aus, wechsle mit TRENNZEICHEN und ZAHLENFORMAT durch die Möglichkeiten, beginnend bei AUTO. ZURÜCK führt zur Dateiliste. GRAFIK zeichnet die gewählten Spalten.

Eine letzte Zeile ohne Zeilenvorschub gilt als unvollständig, etwa nach Stromausfall oder bei voller Karte. Sie wird übersprungen. ZEILEN erscheint in der Warnfarbe, daneben steht `LETZTE ZEILE UNVOLLSTÄNDIG. ÜBERSPRUNGEN.`.

## Die Grafik

![Die Grafik mit vier Spalten](view:logs-plot)

Die Kopfzeile nennt Datei, Zeilenzahl, Dauer und Abtastrate. Jede Spalte hat ein Anzeigefeld mit Wertebereich und Cursorwert.

- Bewege den Cursor mit einem Finger oder mit `<` und `>`. Unten rechts stehen Zeit und Messpunkt.
- Zoome mit zwei Fingern: auseinanderziehen zum Vergrößern, zusammenziehen zum Verkleinern. Bewege beide Finger zum Verschieben. Die Ansicht reicht von mindestens 8 Messpunkten bis zum gesamten Lauf.
- Ist Ein Finger schiebt unter SETUP, PANEL eingeschaltet, verschiebt ein Finger eine vergrößerte Ansicht seitwärts, sobald er sich mehr als 10px bewegt.
- ZURÜCK führt zur Dateiliste, oder zur Importansicht, wenn deren GRAFIK die Grafik geöffnet hat.
- MEHR öffnet eine Liste mit SPALTEN, das zur Importansicht führt, um andere Spalten zu wählen. Ein Druck neben die Liste schließt sie.

![Die Liste von MEHR](view:logs-plot-more)

Eine Laufdatei des Prüfstands steht über ihrer Zeit, wie der nächste Abschnitt zeigt. Eine Datei, die GRAFIK der Importansicht öffnet, verteilt ihre Zeilen gleichmäßig nach Zeilennummer: ihre Beschriftung stammt aus der Zeitspalte, eine Datenlücke erscheint daher als Sprung in den Zeitangaben.

## Ein Lauf über seiner Zeit

![Ein AUTOMATIC TEST: seine Schritte und seine Phasen, das Halten eingeschlossen](view:logs-atest-graph)

Ein AUTOMATIC TEST, ein SUPPLY-, ein MOTOR- und ein SERVO-Lauf öffnen in der Grafik mit jeder Zeile bei ihrer Zeit. Die Kopfzeile nennt nach Dauer und Rate die Art: `ATEST012.CSV   1734 ZEILEN   186.20s   9Hz   AUTOMATISCHER TEST`.

- Ein Zeitschritt länger als 3 mittlere Zeilenabstände (Median) und länger als 0.5s ist eine Lücke: die Linien brechen ab, die Lücke ist schraffiert, mit `ZEILEN FEHLEN`, wo es hineinpasst. Der AUTOMATIC TEST schreibt eine Zeile je Messwert, auch beim Halten; ein Halten ist darum keine Lücke.
- Eine Datei, deren Zeitspalte zurückspringt, steht nach Zeilennummer und zeigt keine Dauer.
- Jeder Schritt eines AUTOMATIC TEST beginnt mit einer Linie und seiner Nummer auf einem Schild.
- Ein Balken am oberen Rand zeigt Zeilen über ihrem Bereich (`>65535` rpm, Schild `>`) in der Warnfarbe und begrenzte Messfenster (`clipped` über 0, Schild `BEGRENZT`) in der Gefahrenfarbe.
- Tippe auf einen Chip, um seine Reihe hervorzuheben, erneut, um sie auszublenden, ein drittes Mal, um sie zurückzuholen. Ein Finger, zwei Finger, `<` und `>` wirken wie in der Grafik oben.

![Ein SUPPLY-Lauf: die Streifen CC und MODELL und eine Lücke](view:logs-supply-graph)

Bis zu zwei Streifen von 6px stehen unter den Linien, jeder mit seiner Beschriftung am linken Ende:

| Streifen | Läufe | Gefärbt, wo |
|---|---|---|
| PHASE | AUTOMATIC TEST | jede `phase` in ihrer Farbe; die Beschriftung nennt die Phase am Cursor, `PHASE MOVE` |
| CC | AUTOMATIC TEST, SUPPLY | `mode` CC ist |
| MODELL | SERVO, MOTOR, SUPPLY | `source` `model` ist: die Zeile stammt aus dem Modell des Panels |

Ein Streifen erscheint nur, wo eine Zeile ihn setzt.

## Die Grafik eines LIMIT RUN oder RUDDER AUTO

![Ein LIMIT RUN: current und angle über dem Puls](view:logs-limit-graph)

Die Datei eines LIMIT RUN und eines RUDDER AUTO öffnet in der Grafik: jede Zeile über ihrem Puls, eine Linie je Suche und Seite. Die Kopfzeile nennt Datei, Zeilen und Art, `LIMIT006.CSV   116 ZEILEN   LIMIT RUN`. Die Grafik zeigt die ganze Datei mit bis zu 2.048 Zeilen; eine längere öffnet in der Kurvenansicht nach Zeilennummer.

| Buchstabe | Markierung | Wo |
|---|---|---|
| M | Mitte | der Puls, an dem jede Suche beginnt |
| A | Anschlag | der Puls, an dem die letzte Suche der Seite den Anschlag fand |
| E | Endpunkt | der für die Seite geschriebene Endpunkt, 25us vor dem Anschlag |
| L | letzte Position | der äußerste Puls einer Seite, wo er nicht ihr Anschlag ist: ohne Anschlag, oder eine frühere Suche lief weiter |
| F | Freistrom | ein Pegel über den Pulsen der Seite |
| S | Schwelle | der Strom, über dem eine Position als Anschlag gilt |

- Die erste Suche ist durchgezogen, die Suche nach dem Überstellen (2) und die letzte Prüfung (3) gestrichelt.
- Der kleinste und der größte Puls stehen auf der ersten und der letzten Spalte der Grafik.
- Tippe auf einen Chip: seine Reihe wird 2px dick, Ober- und Unterende ihrer Skala stehen am linken Rand der Grafik. Erneutes Tippen blendet die Reihe aus, ein drittes holt sie zurück.
- Ein Finger setzt den Cursor auf den nächsten Punkt; `<` und `>` gehen zum nächsten gezeichneten Punkt. Jeder Chip zeigt seinen Wert am Cursor; die Zeile in der Fußleiste lautet `pulse 1040us   Zeile 112 von 116`.
- Zwei Finger zoomen die Grafik nicht.

![Die Suche eines RUDDER AUTO](view:logs-ruddr-search)

Die Datei eines RUDDER AUTO hat zwei Bilder auf den Reitern SUCHE und SCHRITTE. SUCHE zeichnet Strom und Winkel über den Pulsen der Suche, eine Linie je Seite, mit M, E am Endpunkt jeder Seite und L an der äußersten Position jeder Seite.

![Die Schritte eines RUDDER AUTO](view:logs-ruddr-steps)

SCHRITTE zeichnet jede Messung und ihr Ziel über dem Schritt, eine Linie je Seite, und die 3 Prüfmessungen als Punkte. Eine Messung außerhalb der Toleranz erscheint in der Warnfarbe; ein Strich am Fuß der Grafik markiert jede Messung nach einem Schreiben.

## Eine Datei löschen

![Die Rückfrage von LÖSCHEN](view:logs-delete)

LÖSCHEN fragt nach und nennt Dateiname und Größe. Drücke und löse LÖSCHEN innerhalb dieses Dialogs, um die Datei zu entfernen. ABBRECHEN oder das Verlassen der Seite behält sie. Eine noch geöffnete Aufzeichnung lässt sich nicht löschen. Solange der Prüfstand ARMED ist oder ein Lauf aufgezeichnet wird, ist LÖSCHEN grau; ein Tipp darauf zeigt `Prüfstand ARMED. Die Karte ist nur lesbar.` oder `Ein Lauf wird aufgezeichnet. Die Karte ist nur lesbar.`

## Kartenformate

Das Panel liest FAT12, FAT16, FAT32 und exFAT (extended File Allocation Table). Es unterstützt das Dateisystem auf der ganzen Karte oder die erste der 4 MBR-Partitionen (Master Boot Record) mit einem dieser Formate. SDHC-Karten von 4GB bis 32GB sind ab Werk mit FAT32, SDXC-Karten von 64GB bis 2TB mit exFAT formatiert. Beide lassen sich im Auslieferungszustand einbinden.

- Nicht unterstützt werden GPT-Partitionierung (GUID Partition Table; GUID: Globally Unique Identifier), Volumes über 2TiB sowie NTFS, ext4, HFS+ und APFS. Solche Karten lassen sich nicht einbinden. Das Panel formatiert keine Karten.
- Verwende zum Formatieren den SD Memory Card Formatter der SD Association oder formatiere am Computer mit FAT32 oder exFAT auf MBR. Unter macOS heißt das Schema `Master Boot Record`.
- Dateien ab 4GiB auf exFAT erscheinen mit ihrer Größe, etwa `8.00GB`. ÖFFNEN liest bis zu 2GiB minus 1 Byte. Größere Dateien bleiben in der Liste, mit der Meldung `BENCH001.CSV ist 2GB oder größer. Nicht geöffnet.` Das Panel schreibt kB, MB und GB für Vielfache von 1.024: `2GB` sind 2GiB. Über [WLAN](wifi.md) lassen sie sich herunterladen.

## Verwandte Seiten

- [WLAN](wifi.md): die Dateien der Karte im Browser herunterladen.
- [Motor und ESC](motor.md), [Netzteil](supply.md) und [Servo](servo.md): die Läufe, die diese Dateien enthalten.
