---
screen: LOGS
title: Logs
order: 50
---

# Logs

LOGS liest die SD-Karte (Secure Digital) des Panels: Der Bildschirm listet die CSV-Dateien (Comma-Separated Values) im Hauptverzeichnis der Karte, zeigt, was der Import in einer davon gefunden hat, und zeichnet bis zu 4 ihrer Spalten. Er öffnet über die Kachel LOGS im [Menü](overview.md); das Band nennt ihn LOG VIEWER. WLAN, unten links, öffnet [Dateien und Update](wifi.md).

## Keine Karte

![LOGS ohne Karte im Steckplatz](view:logs-nocard)

Ist keine Karte eingebunden, zeigt der Kasten `KEINE AUFZEICHNUNGEN` und `Keine Karte. Setze eine Karte ein und tippe auf AKTUALISIEREN.` Eine Karte ohne CSV-Datei im Hauptverzeichnis zeigt `Keine .csv-Dateien im Hauptverzeichnis.` Läufe werden als `BENCHnnn.CSV`, `ESCnnn.CSV` oder `SERVOnnn.CSV` geschrieben, nummeriert von 001 bis 999. AKTUALISIEREN liest die Karte erneut.

## Die Dateiliste

![Die Dateiliste](view:logs)

Der Kopf nennt die Karte mit ihrer Datenträgerbezeichnung; eine Karte ohne Bezeichnung zeigt den Namen, den die Karte meldet, oder `SD-KARTE`. Jede Zeile ist eine Datei mit ihrer Größe.

| Taste | Was sie tut |
|---|---|
| AKTUALISIEREN | liest die gezeigte Seite erneut und wählt ihren neuesten Lauf |
| VORIGE, NÄCHSTE | die Seite davor und danach; sichtbar mit mehr als 48 Dateien auf der Karte |
| WLAN | öffnet [Dateien und Update](wifi.md) |
| LÖSCHEN | fragt nach und entfernt dann die gewählte Datei; grau, solange nichts gewählt ist |
| ÖFFNEN | öffnet die gewählte Datei in der Importansicht; grau, solange nichts gewählt ist |

![Die vierte Seite einer vollen Karte](view:logs-pages)

- Eine Seite fasst 48 Einträge; eine Karte fasst bis zu 999 Läufe. Die Seiten beginnen mit dem neuesten Lauf, dann folgen die Dateien, die der Prüfstand nicht geschrieben hat, und die Zeilen einer Seite stehen in derselben Reihenfolge, der neueste Lauf oben. Mit mehr als 48 Dateien nennt der Reiter den Teil der Karte, den die Seite zeigt, `DATEIEN 145 BIS 192 VON 1000`.
- Die Seiten werden von oben zu je 48 geschnitten. Stehen vor der gezeigten Seite weniger als 48 Einträge, etwa nachdem ein Lauf aufgezeichnet wurde, zeigt VORIGE die erste Seite.
- Läufe, die aufgezeichnet werden, während eine spätere Seite offen ist, stehen auf Seite 1. VORIGE überspringt dann auf dem Rückweg ab Seite 3 so viele Einträge, wie aufgezeichnet wurden; NÄCHSTE von Seite 1 zeigt sie, und die Zahlen im Reiter zeigen die Verschiebung.
- Der Rollbalken am rechten Rand, 8,1 mm breit, rollt innerhalb der Seite: Tippen auf die Bahn springt, Ziehen am Griff rollt.
- Auf einer Seite nach der ersten liest AKTUALISIEREN ab dem ersten Eintrag dieser Seite; enthält die Karte inzwischen weniger Einträge, zeigt es die letzte Seite.
- Nach LÖSCHEN wird die Seite neu gelesen, und die Rollposition bleibt; nichts ist gewählt, LÖSCHEN und ÖFFNEN sind grau, bis eine Zeile gewählt ist.
- Wird eine Karte eingesteckt, während keine eingebunden ist, bindet AKTUALISIEREN sie ein; sie öffnet auf ihrer ersten Seite. Eine Karte, die gegen eine andere getauscht wird, während eine eingebunden ist, wird bis zum Neustart des Panels nicht neu eingebunden; was die Liste in diesem Zustand zeigt, ist nicht gemessen.
- Neueste heißt die höchste Nummer im Namen. Das Panel hat keine Uhr, die das Abschalten übersteht; jede Datei trägt das Datum 1980-01-01.
- Ein Lauf, der gerade geschrieben wird, steht nicht in der Liste.
- Eine Zeile nimmt einen Tipp an. Ein Ziehen über mehr als 8 px auf den Zeilen scrollt die Liste und wählt nichts.

## Was ein Lauf schreibt

Eine Datei ist ein Lauf. Ihr Name sagt, welcher Schreiber sie angelegt hat:

| Name | Lauf |
|---|---|
| `SERVOnnn.CSV` | ein automatischer Servotest; sein Bericht ist `SERVOnnn.TXT` |
| `SERVOnnn.TXT` | der Bericht eines Endlagenlaufs, ohne CSV daneben |
| `ESCnnn.CSV` | ein Schalten der Throttle-Ausgänge auf ARMED, von MOTOR & ESC oder einem Stick-Lauf auf PROGRAMMER |
| `BENCHnnn.CSV` | ein Schalten der Surfaces auf ARMED, von SERVO, oder ein Einschalten des SUPPLY-Ausgangs, solange der Prüfstand SAFE ist |

An einem Koprozessor älter als Protokoll 4.18 umfasst jedes Schalten auf ARMED auch die Throttle-Ausgänge. Ein Schalten auf ARMED von SERVO schreibt dann `ESCnnn.CSV`.

Die Nummern sind eine Reihe über alle drei Namen: ein neuer Lauf nimmt eins über der höchsten Nummer auf der Karte, gleich unter welchem Namen. Eine Lücke, die eine gelöschte Datei hinterlässt, wird nicht gefüllt. Eine Karte mit einer Nummer 999 nimmt keinen weiteren Lauf an, ob niedrigere Nummern frei sind oder nicht. Eine Karte, die nur `BENCHnnn`-Dateien trägt, zählt über ihren Nummern weiter. Firmware 0.20.0 und älter nummeriert Läufe nur über der höchsten `BENCHnnn`-Datei. Läuft sie wieder auf einer Karte, die diese Firmware beschrieben hat, kann ein Lauf eine Nummer bekommen, die schon eine `ESCnnn`- oder `SERVOnnn`-Datei trägt. Nichts wird überschrieben: Die beiden Dateien haben verschiedene Namen. Die Liste zeigt die `.TXT`-Berichte nicht, und LÖSCHEN auf einem Lauf entfernt auch seinen Bericht. Die Läufe stehen nach Nummer, der neueste zuerst, über alle drei Namen: `ESC043.CSV`, dann `SERVO042.CSV`, dann `BENCH041.CSV`.

Ein Lauf wird alle 20 Zeilen oder alle 1000 ms auf die Karte festgeschrieben, je nachdem, was zuerst eintritt. Ein Stromausfall mitten im Lauf kostet unter 1,0 s an Zeilen, solange die Karte mithält, und bis zu 84 Zeilen, 4,20 s bei 20 Hz, wenn sie stockt.

Das Band meldet, was die Karte mit einem Lauf gemacht hat:

| Meldung | Bedeutung |
|---|---|
| `Keine Karte. Dieser Lauf wird nicht aufgezeichnet.` | nichts ist eingebunden |
| `Karte nicht lesbar. Lauf nicht aufgezeichnet.` | die Karte lässt sich nicht auflisten |
| `Karte voll oder nicht beschreibbar. Lauf nicht aufgezeichnet.` | keine Laufdatei ließ sich anlegen |
| `Karte zu langsam. Lauf nicht aufgezeichnet.` | jede Zeile ging verloren |
| `Die Karte war zu langsam. In der Aufzeichnung fehlen Daten.` | einige Zeilen gingen verloren; die Zeitspalte zeigt wo |
| `Daten können nicht mehr auf die Karte gespeichert werden. Aufzeichnung endet hier.` | ein Schreiben ist mitten im Lauf gescheitert |
| `Daten konnten nicht mehr auf die Karte geschrieben werden. Aufzeichnung unvollständig.` | dasselbe, beim Schließen des Laufs erneut gemeldet |
| `Letzter Schreibvorgang auf die Karte fehlgeschlagen. Aufzeichnung unvollständig.` | die Zeilen seit dem letzten Festschreiben fehlen in der Datei |

Die ersten vier hinterlassen keine Datei. Die letzten vier hinterlassen eine Datei, die zu früh endet.

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

Die rechte Liste nennt jede Spalte mit ihrem ersten Wert und ihrem Bereich, vom kleinsten zum größten. Ein Tipp auf eine Zahlenspalte nimmt sie in die Grafik auf oder heraus; der Titel zählt sie, `GRAFIK  4/4`. TRENNZEICHEN und ZAHLENFORMAT schalten durch die Möglichkeiten, AUTO zuerst, wenn Werte oder Spalten falsch aussehen. ZURÜCK führt zur Liste; GRAFIK zeichnet die gewählten Spalten.

Eine letzte Zeile ohne Zeilenvorschub ist eine Zeile, die ein Stromausfall oder eine volle Karte abgeschnitten hat. Sie wird weggelassen, ZEILEN steht in der Warnfarbe, und die Ansicht zeigt `LETZTE ZEILE UNVOLLSTÄNDIG. ÜBERSPRUNGEN.`.

## Die Grafik

![Die Grafik mit vier Spalten](view:logs-plot)

Der Kopf nennt die Datei, ihre Zeilen, ihre Dauer und ihre Abtastrate. Jede Spalte hat einen Chip mit ihrem Bereich und dem Wert am Cursor.

- Ein Finger, `<` und `>` bewegen den Cursor. Die Zeile unten rechts zeigt seine Zeit und seinen Messpunkt.
- Zwei Finger zoomen: spreizen zum Vergrößern, zusammenziehen zum Verkleinern, beide bewegen zum Verschieben. Die engste Ansicht sind 8 Messpunkte, die weiteste der ganze Lauf.
- Mit SETUP, ANWENDUNG, Ein Finger schiebt eingeschaltet verschiebt ein Finger, der sich mehr als 10 px bewegt, eine vergrößerte Ansicht seitwärts.
- ZURÜCK führt zur Importansicht.

Die Zeilen stehen gleichmäßig nach Zeilennummer. Die Beschriftungen unter der Grafik sind die Zeitspalte der Datei; eine Lücke in den Zeilen ist ein Sprung in den Beschriftungen.

## Eine Datei löschen

![Die Rückfrage von LÖSCHEN](view:logs-delete)

LÖSCHEN öffnet eine Rückfrage, die die Datei und ihre Größe nennt. Nur ihr eigenes LÖSCHEN, auf dieser Taste gedrückt und losgelassen, entfernt die Datei. ABBRECHEN oder das Verlassen des Bildschirms löscht nichts. Der Lauf, den der Logger offen hat, wird abgelehnt.

## Verwandte Seiten

- [Dateien und Update](wifi.md): die Dateien der Karte im Browser herunterladen.
- [Motor und ESC](motor.md), [Netzteil](supply.md) und [Servo](servo.md): die Läufe, die diese Dateien enthalten.
