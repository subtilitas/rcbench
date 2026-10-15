---
screen: MOTOR
title: Motor und ESC
order: 10
---

# Motor und ESC

MOTOR & ESC steuert einen ESC (Electronic Speed Controller) und zeichnet einen Lauf auf: Spannung, Strom, Leistung und Drehzahl über der Zeit. Der Bildschirm öffnet über die Kachel MOTOR & ESC im [Menü](overview.md). Die Grafik und der Throttle belegen die linke Spalte; die vier Anzeigen, ARM und die Summen die rechte.

## Vor dem ersten Lauf

![Link steht, noch nie ARMED, kein Pin als Motor gebunden](view:motor-idle)

- Das Tag der Grafik lautet `TELEMETRIE RUHT`, und die Grafik zeigt `kein Lauf aufgezeichnet`, bis zum ersten Schalten auf ARMED.
- Die Zeile unter dem Balken lautet `ZUERST ARM`, solange der Prüfstand SAFE ist. Der Balken und die Tasten `-1` und `+1` sind gedimmt und nehmen keinen Befehl an.
- Die Anzeigen zeigen `---`, solange nichts einen Wert gemeldet hat.
- `ZIEL: KEIN PIN ALS MOTOR GEBUNDEN`, in der Warnfarbe, sagt: Die Bindung ist gelesen, und kein Pin ist als Motor gebunden. Einen Pin unter [Ausgänge](outputs.md) binden.

## Das Throttle-Feld nennt seine Pins

Das Throttle-Feld nennt links neben dem Prozentwert die Pins, die der Throttle treibt: `ZIEL: GP0 GP1`. Es sind die Pins, die unter SETUP, OUTPUTS als MOTOR PWM oder mit einem DShot-Protokoll gebunden sind. Die Zeile fasst 49 Zeichen; Pins, die nicht passen, werden gezählt, `+7`.

| Zeile | Bedingung |
|---|---|
| `ZIEL: GP0 GP1` | die Pins, die der Throttle treibt, in Pin-Reihenfolge |
| `ZIEL: KEIN PIN ALS MOTOR GEBUNDEN` | die Bindung ist gelesen, und kein Pin ist als Motor gebunden |
| `ZIEL: BINDUNG NICHT GELESEN` | der Link steht nicht, die Seiten wurden nicht gelesen, oder die Platine hat keine Pin-Karte |

ARM schaltet die ganze Gruppe der Throttle-Ausgänge auf ARMED, nicht nur die hier genannten Pins.

## Ein Lauf

![Ein Lauf bei ARMED ohne Koprozessor: SIM und das Wasserzeichen SIMULATION](view:motor)

1. ARM 2 s halten. Die Füllung geht von Grün nach Rot über; der Prüfstand schaltet auf ARMED, wenn der Übergang vollständig ist. Früheres Loslassen schaltet nichts.
2. Den Throttle ziehen. Er bewegt sich um den Weg des Fingers, nicht dorthin, wo der Finger aufsetzt. `-1` und `+1` ändern um einen Prozentpunkt.
3. DISARM tippen oder STOP drücken, um den Lauf zu beenden. Beide stoppen den Ausgang sofort, ohne Rampe, und setzen den Throttle auf 0 %.

Bei ARMED lautet das Tag der Grafik `TELEMETRIE LIVE`, und am rechten Rand der Grafik steht `JETZT`. ARM ist gesperrt gezeichnet, solange der [Servo-Programmierer](servo-programmer.md) einen Port hält.

| Bedienelement | SAFE | ARMED |
|---|---|---|
| Der Balken | abgelehnt; gedimmt gezeichnet | bewegt den Throttle um den Weg des Fingers |
| `-1`, `+1` | abgelehnt; gedimmt gezeichnet | ändern um einen Prozentpunkt |
| Drehknopf | abgelehnt; die Drehung wird nicht gemerkt | bewegt den Throttle von seinem Wert aus |
| ARM | schaltet nach 2 s Halten auf ARMED | DISARM: schaltet bei einem Tipp auf SAFE |
| SPITZEN ZURÜCKSETZEN | löscht die Spitzenmarken | löscht die Spitzenmarken |
| GRAFIK, TABELLE | wechseln die Ansicht | wechseln die Ansicht |

Das Verlassen des Bildschirms, ein verlorener Link oder ein Touch-Controller, der nicht mehr antwortet, schaltet ebenfalls auf SAFE und setzt den Throttle auf 0 %.

Solange kein Koprozessor antwortet, zeigt das Band SIM, und SIMULATION steht quer über dem Bildschirm: Jeder Wert ist das Modell des Panels.

## Ein gehaltener Lauf

![Die Grafik hält den Lauf nach dem Schalten auf SAFE](view:motor-held)

Das Schalten auf SAFE hält die Grafik, wie sie stand: Das Tag lautet `LETZTER LAUF`, und am rechten Rand steht `ENDE`. Das nächste Schalten auf ARMED löscht sie. Die Grafik hat 534 Spalten bei 20 Messungen pro Sekunde, 26,7 s. Ein längerer Lauf zeigt seine letzten 26,7 s; der ganze Lauf steht in seiner CSV-Datei (Comma-Separated Values) auf der Karte.

Die Anzeigen, die Ansicht TABELLE, die Summen und der Temperaturstreifen bleiben immer live, ARMED oder nicht.

## Der Streifen über der Grafik

Der Streifen trägt die Abfragerate, die Fehlerzahl des Links und drei Temperaturen: ESC und MOT von der Prüfstandsseite des Koprozessors, MCU vom Chip des ESP32-S3 im Panel. Hat der Koprozessor seit seinem Einschalten Link-Frames abgelehnt, fügt der Streifen ihre Zahl an.

![37 abgelehnte Link-Frames auf dem Streifen](view:motor-refused)

## TABELLE

![Die Ansicht TABELLE mit einem INA228 als Quelle](view:motor-table)

TABELLE listet Spannung, Strom, Leistung und Drehzahl, jeweils mit ihrer Spitze. Ein Kanal, für den nichts geantwortet hat, zeigt `--`.

- Ist der INA228 unter SETUP, ANSCHLÜSSE eingeschaltet und antwortet er, stammen Spannung, Strom und Leistung vom INA228: die Mittelwerte des letzten 50-ms-Fensters des Koprozessors, gelesen mit 20 Hz. Die Überschrift nennt `INA228`.
- `ÜBERSTEUERT` folgt der Überschrift, solange ein Strom das Ende des Messbereichs erreicht hat: Strom und Leistung sind dann Grenzen, keine Werte.
- `ESC MELDET` listet Spannung und Strom aus der eigenen Telemetrie des ESC, und `Abw.` ist der Wert des ESC minus dem des INA228.
- Ohne INA228 sind die Kanäle die Telemetrie des ESC, und `ESC MELDET` fehlt.
- Die Drehzahl kommt aus der bidirektionalen DShot-Telemetrie. Eine Größe, die nichts misst, bleibt leer und wird nicht simuliert.

## kV, rpm/V und EFF

Der Kopf der Grafik zeigt den Nenn-kV-Wert, die gemessene Drehzahl je Volt und EFF, das Zweite geteilt durch das Erste, begrenzt auf 199 %. Der Nenn-kV-Wert ist die Einstellung `Nenn-kV` unter SETUP, ESC / PRÜFSTAND, 0 bis 10000 rpm/V, Vorgabe 0. Bei 0 bleibt das kV-Feld leer, und EFF fehlt. EFF zählt nur Kupferverluste und schließt die Verluste des ESC ein; es ist eine obere Grenze, kein Wirkungsgrad an der Welle.

## Das Log eines Laufs

Jedes Schalten auf ARMED schreibt eine Datei, `BENCH001.CSV` und aufwärts, auf die SD-Karte (Secure Digital) des Panels. Bei stehendem Link ist eine Zeile eine beantwortete Abfrage, etwa 19 Zeilen pro Sekunde an einem Prüfstand; andere Prüfstände sind nicht gemessen. Die Datei schließt beim Schalten auf SAFE. [Logs](logs.md) öffnet sie und zeichnet sie.

## Verwandte Seiten

- [Ausgänge](outputs.md): welche Pins der Throttle treibt, und Leerlaufpuls und Vollpuls.
- [Setup](setup.md): Nenn-kV, Motorpole, der Drehknopf.
- [Anschlüsse](setup-interfaces.md): der INA228.
