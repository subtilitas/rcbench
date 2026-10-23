---
screen: MOTOR
title: Motor und ESC
order: 10
---

# Motor und ESC

MOTOR & ESC steuert einen ESC (Electronic Speed Controller) und zeichnet einen Lauf auf: Spannung, Strom, Leistung und Drehzahl über der Zeit. Der Bildschirm öffnet über die Kachel MOTOR & ESC im [Menü](overview.md), immer auf LIVE.

## Die Leiste

Die Leiste oben hält drei Aufgaben, LIVE, PROGRAMMIEREN und OPTIONEN. Ein Tipp wechselt die Aufgabe. LIVE behält beim Wechsel seine Ansicht, GRAFIK oder TABELLE.

| Aufgabe | Die linke Spalte |
|---|---|
| LIVE | die Grafik oder die Tabelle, und der Throttle |
| PROGRAMMIEREN | die Liste der ESC-Programmierer ([Programmieren](#programmieren)) |
| OPTIONEN | [Motor-Optionen](motor-settings.md): der Akku, der Motor und der Gasausgang |

Die rechte Spalte ist auf den drei Aufgaben dieselbe: die Temperaturen, die vier Anzeigen und die Karte STEUERUNG mit den Summen in ihrer Titelzeile, ZIEL, SPITZENWERTE ZURÜCKSETZEN und ARM. ARM steht am Fuß der Karte, wo SERVO sein ARM hat, und wirkt auf jeder Aufgabe. Eine Sitzung aus PROGRAMMIEREN deckt sie ab.

Das Home-Tag oben links steigt eine Ebene nach der anderen:

| Von | Das Home-Tag führt zu |
|---|---|
| dem Tastenfeld auf OPTIONEN | OPTIONEN; der Wert bleibt unverändert |
| einer Sitzung auf PROGRAMMIEREN | eine Ebene höher in der Sitzung, dann zur Liste von PROGRAMMIEREN |
| LIVE, der Liste von PROGRAMMIEREN oder OPTIONEN | dem Menü; der Prüfstand schaltet auf SAFE |

## Programmieren

![Die Liste von PROGRAMMIEREN](view:motor-program)

PROGRAMMIEREN listet die ESC-Programmierer. Seine eine Zeile, `ESCs per Gasknüppel` in der Gruppe STICK-MENÜ, treibt die als Throttle gebundenen Pins und öffnet [ESC STICK](motor-stick.md). Die modellierten Tabellen für BLHeli_S-, AM32-, ESCape32- und VESC-Firmware haben keinen Transport und keine Zeile.

ESC STICK öffnet über die volle Breite, über der linken und der rechten Spalte. Anstelle der Reiter zeigt die Zeile der Leiste den Ausgangsstreifen, eine Zeile:

`ZIEL DSHOT300 GP0 GP1   NETZTEIL 7.60 V  1.00 A   SAFE`

- ZIEL: das Protokoll und die Pins, die der Lauf treibt, in der Warnfarbe, solange kein Pin gebunden oder die Bindung nicht gelesen ist.
- NETZTEIL: der Ausgang des PD mini, wie er liest, `NETZTEIL EIN` ohne Messwerte, `NETZTEIL AUS` oder `NETZTEIL --`, solange das Netzteil nicht antwortet.
- SAFE oder ARMED: der Prüfstand.

Solange ESC STICK offen ist, erreicht kein Druck ARM oder einen Reiter. Das Öffnen auf einem Prüfstand, der nicht SAFE ist, schaltet ihn auf SAFE. Das Verlassen von MOTOR & ESC beendet einen Lauf, schaltet den Prüfstand auf SAFE und schließt ESC STICK.

## Vor dem ersten Lauf

![Link steht, noch nie ARMED, kein Pin als Motor gebunden](view:motor-idle)

- Das Tag der Grafik lautet `TELEMETRIE INAKTIV`, und die Grafik zeigt `Noch kein Lauf aufgezeichnet.`, bis zum ersten Schalten auf ARMED.
- Die Zeile unter dem Balken lautet `ZUERST ARM`, solange der Prüfstand SAFE ist. Der Balken und die Tasten `-1` und `+1` sind gedimmt und nehmen keinen Befehl an.
- Die Anzeigen zeigen `---`, solange nichts einen Wert gemeldet hat.
- ZIEL lautet `KEIN MOTOR-PIN` in der Warnfarbe: Die Bindung ist gelesen, und kein Pin ist als Motor gebunden. Einen Pin unter [Ausgänge](outputs.md) binden.

## ZIEL

Die Zeile ZIEL oben auf der Karte STEUERUNG nennt, was der Throttle treibt: das Protokoll, dann die Pins, `DSHOT300 GP0 GP1`. Es sind die Pins, die unter [Ausgänge](outputs.md) als MOTOR PWM oder mit einem DShot-Protokoll gebunden sind. Das Wertefeld fasst 25 Zeichen; Pins, die nicht passen, werden gezählt, `DSHOT300 GP0 GP1 GP2 +5`.

| ZIEL lautet | Bedingung |
|---|---|
| `DSHOT300 GP0 GP1` | das Protokoll und die Pins, die der Throttle treibt, in Pin-Reihenfolge |
| `GEMISCHT GP0 GP4` | die Throttle-Pins laufen mit zwei oder mehr Protokollen, oder einer davon mit einer Pulsrate, die kein Protokoll von OUTPUTS hat |
| `GP0 GP1` | jeder Throttle-Pin läuft mit einer Pulsrate, die kein Protokoll von OUTPUTS hat |
| `KEIN MOTOR-PIN` | die Bindung ist gelesen, und kein Pin ist als Motor gebunden; Warnfarbe |
| `PINS NICHT GELESEN` | der Link steht nicht, die Seiten wurden nicht gelesen, oder die Platine hat keine Pin-Karte; Warnfarbe |

Ein Tipp auf ZIEL öffnet [Ausgänge](outputs.md) auf dem Protokoll, an das die Gas-Pins gebunden sind, oder auf der Übersicht, solange sie an mehrere gebunden sind oder keines bekannt ist, und der Prüfstand schaltet auf SAFE wie bei jedem Weg vom Bildschirm. Das Home-Tag auf OUTPUTS führt zurück zu MOTOR & ESC, auf die Aufgabe, auf der ZIEL getippt wurde.

ZIEL nimmt keinen Tipp an und ist gesperrt gezeichnet:

- solange der Prüfstand nicht SAFE ist: ARMED, ein ARM gefragt und nicht beantwortet, ein DISARM gefragt und nicht beantwortet;
- solange ARM gehalten wird;
- solange der [Servo-Programmierer](servo-programmer.md) einen Port hält.

ARM schaltet die ganze Gruppe der Throttle-Ausgänge auf ARMED, nicht nur die hier genannten Pins.

## Ein Lauf

![Ein Lauf bei ARMED ohne IOmcu: SIM und das Wasserzeichen SIMULATION](view:motor)

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
| SPITZENWERTE ZURÜCKSETZEN | löscht die Spitzenmarken | löscht die Spitzenmarken |
| GRAFIK, TABELLE | wechseln die Ansicht von LIVE | wechseln die Ansicht von LIVE |

Das Verlassen des Bildschirms, ZIEL, ein verlorener Link oder ein Touch-Controller, der nicht mehr antwortet, schaltet ebenfalls auf SAFE und setzt den Throttle auf 0 %.

Solange kein IOmcu antwortet, zeigt das Band SIM, und SIMULATION steht quer über dem Bildschirm: Jeder Wert ist das Modell des Panels.

## Ein gehaltener Lauf

![Die Grafik hält den Lauf nach dem Schalten auf SAFE](view:motor-held)

Das Schalten auf SAFE hält die Grafik, wie sie stand: Das Tag lautet `LETZTE TELEMETRIE`, und am rechten Rand steht `ENDE`. Das nächste Schalten auf ARMED löscht sie. Die Grafik hat 600 Spalten bei 20 Messungen pro Sekunde, 30,0 s. Ein längerer Lauf zeigt seine letzten 30,0 s; der ganze Lauf steht in seiner CSV-Datei (Comma-Separated Values) auf der Karte.

Die Anzeigen, die Ansicht TABELLE, die Summen und der Temperaturstreifen bleiben immer live, ARMED oder nicht.

## Die Abfragerate und die Temperaturen

Auf LIVE trägt die Telemetriekarte unter ihrem Titel GRAFIK und TABELLE und daneben die Abfragerate und die Fehlerzahl des Links. Hat der IOmcu seit seinem Einschalten Link-Frames abgelehnt, fügt die Zeile darunter ihre Zahl an. Die drei Temperaturen stehen auf jeder Aufgabe in der Zeile über der rechten Spalte: ESC und MOT von der Prüfstandsseite des IOmcu, MCU vom Chip des ESP32-S3 im Panel.

![37 abgelehnte Link-Frames in der Zeile](view:motor-refused)

## TABELLE

![Die Ansicht TABELLE mit einem INA228 als Quelle](view:motor-table)

TABELLE listet Spannung, Strom, Leistung und Drehzahl, jeweils mit ihrer Spitze. Ein Kanal, für den nichts geantwortet hat, zeigt `--`.

- Ist der INA228 unter SETUP, ANSCHLÜSSE eingeschaltet und antwortet er, stammen Spannung, Strom und Leistung vom INA228: die Mittelwerte des letzten 50-ms-Fensters des IOmcu, gelesen mit 20 Hz. Die Überschrift nennt `INA228`.
- `ÜBERSTEUERT` folgt der Überschrift, solange ein Strom das Ende des Messbereichs erreicht hat: Strom und Leistung sind dann Grenzen, keine Werte.
- `ESC MELDET` listet Spannung und Strom aus der eigenen Telemetrie des ESC, und `Abw.` ist der Wert des ESC minus dem des INA228.
- Ohne INA228 sind die Kanäle die Telemetrie des ESC, und `ESC MELDET` fehlt.
- Die Drehzahl kommt aus der bidirektionalen DShot-Telemetrie. Eine Größe, die nichts misst, bleibt leer und wird nicht simuliert.

## kV, rpm/V und EFF

Der Kopf der Grafik zeigt den Nenn-kV-Wert, die gemessene Drehzahl je Volt und EFF, das Zweite geteilt durch das Erste, begrenzt auf 199 %. Der Nenn-kV-Wert ist die Einstellung `Nenn-kV` unter MOTOR & ESC, OPTIONEN, 0 bis 10000 rpm/V, Vorgabe 0. Bei 0 bleibt das kV-Feld leer, und EFF fehlt. EFF zählt nur Kupferverluste und schließt die Verluste des ESC ein; es ist eine obere Grenze, kein Wirkungsgrad an der Welle.

## Das Log eines Laufs

Jedes Schalten auf ARMED schreibt eine Datei, `MOTORnnn.CSV`, in `BENCH/LOGS` auf der SD-Karte (Secure Digital) des Panels, solange Auto-Logs unter [SETUP](setup.md#sd-karte) > SD-KARTE EIN ist. Bei stehendem Link ist eine Zeile eine beantwortete Abfrage, etwa 19 Zeilen pro Sekunde an einem Prüfstand; andere Prüfstände sind nicht gemessen. Die Datei schließt beim Schalten auf SAFE. [Logs](logs.md) öffnet sie und zeichnet sie.

## Verwandte Seiten

- [Motor-Optionen](motor-settings.md): der Akku, Nenn-kV, Motorpole, Leerlaufpuls und Vollpuls.
- [ESC STICK](motor-stick.md): einen ESC über sein Gasknüppel-Menü programmieren.
- [Ausgänge](outputs.md): welche Pins der Throttle treibt.
- [Setup](setup.md): der Drehknopf.
- [Anschlüsse](setup-interfaces.md): der INA228.
