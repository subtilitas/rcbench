# rcbench

<sub>[English](Home.md) · **Deutsch**</sub>

<sub>Diese Seiten werden aus `docs/` im Repository erzeugt und bei jedem Push
auf `main` überschrieben. Die Dateien bearbeiten, nicht das Wiki.</sub>

rcbench ist ein Prüfstand für Motoren, ESCs (Electronic Speed Controller,
Motorregler) und Servos, aufgebaut aus zwei Prozessoren: einem
ESP32-S3-Touchpanel (Bedienoberfläche, Einstellungen, SD-Karte) und einem
RP2350-IOmcu (IO-Mikrocontroller: Messung, Ausgänge und jedes Protokoll mit
Zeitanforderungen). Beide sind über CAN (Controller Area Network) mit
1 Mbit/s verbunden.

## Den Prüfstand benutzen

| Aufgabe | Seite |
| --- | --- |
| Funktionsliste | [Worum es geht](Manifest-de.md) |
| Die Teile eines Prüfstands auswählen und verkabeln | [Bench configurator](https://subtilitas.github.io/rcbench/) (Webseite: Teileliste, Einkaufsliste mit Herstellerseiten und Links zur Websuche, Verdrahtungszeichnung und Pin-zu-Pin-Tabelle für den gewählten Aufbau) |
| Beide Platinen bauen und flashen | [Bauen](Building-de.md) |
| Die beiden Platinen verkabeln und den Bus prüfen | [Den Link in Betrieb nehmen](Bringup-de.md) |
| Die Bildschirme bedienen | [Bildschirme](Screens-de.md) |
| Einen Propeller oder Impeller auswuchten | [Auswuchten](Balance-de.md) |
| Die eingebaute Endlage eines Servos messen oder zwei Servos abgleichen | [Servoverfahren](Servo-de.md) |
| Die Einstellungen eines KST- oder Chaservo-Servos lesen, sichern und ändern | [Servo-Programmierer für KST- und Chaservo-Servos](ServoProgrammer-de.md) |
| Einen ESC über sein Gasknüppel-Menü programmieren | [Stick-Programmierung](StickProgramming-de.md) |
| Einen Empfänger anschließen und seine Ausgabe prüfen | [Empfängerbusse](Receivers-de.md) |
| Stoppmechanismen und die externe Schaltung, die sie brauchen | [Sicherheit](Safety-de.md) |
| ESCs mit BLHeli_32-Firmware: was unterstützt wird und was nicht | [Parameter von ESCs mit BLHeli_32-Firmware](BLHeli32-de.md) |

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

Hersteller-, Produkt-, Protokoll- und Firmware-Namen gehören ihren Inhabern
und dienen nur dazu, Kompatibilität zu beschreiben:
[Rechtliche Hinweise und Haftungsausschluss](https://github.com/subtilitas/rcbench/blob/main/README-de.md#rechtliche-hinweise-und-haftungsausschluss).

Jede Seite gibt es auch auf Englisch; der Umschalter steht oben auf jeder
Seite.
