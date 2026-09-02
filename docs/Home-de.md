# rcbench

<sub>[English](Home.md) · **Deutsch**</sub>

<sub>Diese Seiten werden aus `docs/` im Repository erzeugt und bei jedem Push
auf `main` überschrieben. Die Dateien bearbeiten, nicht das Wiki.</sub>

rcbench ist ein Prüfstand für Motoren, ESCs (Electronic Speed Controller,
Motorregler) und Servos, aufgebaut aus zwei Prozessoren: einem
ESP32-S3-Touchpanel (Bedienoberfläche, Einstellungen, SD-Karte) und einem
RP2350-Koprozessor (Messung, Ausgänge und jedes Protokoll mit
Zeitanforderungen). Beide sind über CAN (Controller Area Network) mit
1 Mbit/s verbunden.

## Den Prüfstand benutzen

| Aufgabe | Seite |
| --- | --- |
| Funktionsliste und Stand jeder Funktion | [Worum es geht](Manifest-de.md) |
| Beide Platinen bauen und flashen | [Bauen](Building-de.md) |
| Die beiden Platinen verkabeln und den Bus prüfen | [Den Link in Betrieb nehmen](Bringup-de.md) |
| Die Bildschirme bedienen | [Bildschirme](Screens-de.md) |
| Einen Propeller oder Impeller auswuchten | [Auswuchten](Balance-de.md) |
| Die eingebaute Endlage eines Servos messen oder zwei Servos abgleichen | [Servoverfahren](Servo-de.md) |
| Einen Empfänger anschließen und seine Ausgabe prüfen | [Empfängerbusse](Receivers-de.md) |
| Stoppmechanismen und die externe Schaltung, die sie brauchen | [Sicherheit](Safety-de.md) |
| BLHeli_32-ESCs: was unterstützt wird und was nicht | [BLHeli_32-Parameter](BLHeli32-de.md) |

## Referenz

| Seite | Inhalt |
| --- | --- |
| [Der Link](Link-de.md) | CAN-Verkabelung, Verhalten bei Ausfall, das Page-Protokoll |
| [Das OpenYGE-Protokoll](OpenYGE-de.md) | Spezifikation des ESC-Telemetrie- und Parameterprotokolls |
| [Performance](Performance-de.md) | Zeichenbudget in Cache-Line-Fills |

Der Stand des Projekts, die offenen Punkte und die getroffenen Entscheidungen
stehen in
[STATUS.md](https://github.com/subtilitas/rcbench/blob/main/STATUS.md)
(englisch).

Jede Seite gibt es auch auf Englisch; der Umschalter steht oben auf jeder
Seite.
