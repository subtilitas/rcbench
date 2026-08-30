# rcbench

<sub>[English](Home.md) · **Deutsch**</sub>

<sub>Diese Seiten werden aus `docs/` im Repository erzeugt und bei jedem Push
auf `main` überschrieben — bearbeite die Dateien, nicht das Wiki.</sub>

Ein **Motor-, ESC- und Servoprüfstand** in zwei Hälften: ein
ESP32-S3-Touchpanel, das entscheidet, zeichnet und speichert, und ein
RP2350-Koprozessor, der misst, treibt und alles bedient, was eine Deadline
hat.

## Hier anfangen

| Wenn du … willst | Lies |
| --- | --- |
| wissen, was der Prüfstand heute kann und was jedem Punkt noch fehlt | [Worum es geht](Manifest-de.md) |
| beide Platinen bauen und flashen | [Bauen](Building-de.md) |
| die beiden Platinen verkabeln und die Leitung beweisen | [Den Link in Betrieb nehmen](Bringup-de.md) |
| die Bildschirme bedienen | [Bildschirme](Screens-de.md) |
| einen Propeller oder Impeller auswuchten | [Auswuchten](Balance-de.md) |
| die echte Endlage eines Servos finden, oder zwei Servos abstimmen | [Servoverfahren](Servo-de.md) |
| einen Empfänger anschließen und sehen, was er wirklich sendet | [Empfängerbusse](Receivers-de.md) |
| verstehen, wie der Prüfstand anhält, und was du dafür verdrahten musst | [Sicherheit](Safety-de.md) |
| einen BLHeli_32-Regler programmieren | [BLHeli_32-Parameter](BLHeli32-de.md) — erkennen und ansteuern ja, Parameter nein |

## Referenz

Für die Arbeit an der Firmware statt am Prüfstand:

| | |
| --- | --- |
| [Der Link](Link-de.md) | Der CAN-Link zwischen den Platinen: zuerst die Verkabelung, dann das Protokoll |
| [Das OpenYGE-Protokoll](OpenYGE-de.md) | ESC-Telemetrie und Parameter, von der Leitung aufwärts spezifiziert |
| [Performance](Performance-de.md) | Warum Zeichencode in Cache-Line-Fills budgetiert wird |

Diese Seiten sagen dir, wie du benutzt und anschließt, was es gibt. Das
[README](https://github.com/subtilitas/rcbench) des Repositories ist das
laufende Protokoll — Designentscheidungen, ihre Gründe und das noch Offene
stehen dort.

Jede Seite gibt es auch auf Englisch — der Umschalter steht oben auf jeder.
