# rcbench

<sub>[English](Home.md) · **Deutsch**</sub>

<sub>Diese Seiten werden aus `docs/` im Repository erzeugt und bei jedem Push
auf `main` überschrieben. Bearbeite deshalb die Dateien im Repository.</sub>

rcbench ist ein Prüfstand für Motoren, ESCs (Electronic Speed Controller,
Motorregler) und Servos mit zwei Prozessoren. Das Panel, ein Waveshare
ESP32-P4 7B, übernimmt die Bedienoberfläche auf einem 1024x600-Display an
MIPI DSI (Mobile Industry Processor Interface, Display Serial Interface),
die Einstellungen, die SD-Karte (Secure Digital) und WLAN über seinen
ESP32-C6. Der RP2350-IOmcu (Input/Output-Mikrocontroller) übernimmt Messungen,
Ausgänge und alle Protokolle mit Zeitanforderungen. CAN (Controller Area
Network) mit 1Mbit/s verbindet die beiden.

## Den Prüfstand benutzen

| Aufgabe | Seite |
| --- | --- |
| Funktionsliste | [Worum es geht](Manifest-de.md) |
| Die Teile eines Prüfstands auswählen und verkabeln | [Bench configurator](https://subtilitas.github.io/rcbench/) (Webseite: Teileliste, Einkaufsliste mit Herstellerseiten und Links zur Websuche, Verdrahtungszeichnung und Pin-zu-Pin-Tabelle für den gewählten Aufbau) |
| Beide Platinen bauen und flashen | [Bauen](Building-de.md) |
| Die beiden Platinen verkabeln und den Bus prüfen | [Den Link in Betrieb nehmen](Bringup-de.md) |
| Beide Platinen flashen und den Prüfstand zum ersten Mal betreiben | [Erster Lauf auf der Hardware](FirstRun-de.md) |
| Die Seiten der Oberfläche bedienen | [Bildschirme](Screens-de.md) |
| Einen Propeller oder Impeller auswuchten | [Auswuchten](Balance-de.md) |
| Die mechanische Endlage eines eingebauten Servos messen oder zwei Servos abgleichen | [Servoverfahren](Servo-de.md) |
| Die Einstellungen eines KST- oder Chaservo-Servos auslesen, sichern und ändern | [Servoprogrammierung für KST- und Chaservo-Servos](ServoProgrammer-de.md) |
| Die Einstellungen eines Hitec-Servos auslesen, sichern und ändern | [Servoprogrammierung für Hitec-Servos](HitecProgrammer-de.md) |
| Einen ESC über sein Gasknüppel-Menü programmieren | [Stick-Programmierung](StickProgramming-de.md) |
| Einen Empfänger anschließen und seine Ausgabe prüfen | [Empfängerbusse](Receivers-de.md) |
| Stoppmechanismen und die externe Schaltung, die sie brauchen | [Sicherheit](Safety-de.md) |
| Mit STOP stoppen und STOP lösen | [STOP als Not-Aus](Stop-de.md) |
| ESCs mit BLHeli_32-Firmware: was unterstützt wird und was nicht | [Parameter von ESCs mit BLHeli_32-Firmware](BLHeli32-de.md) |

## Referenz

| Seite | Inhalt |
| --- | --- |
| [Der rbmod-Carrier](Carrier-de.md) | Pins, Stecker, Stromversorgung und Nacharbeiten an Rev 1 der Trägerplatine |
| [Der Link](Link-de.md) | CAN-Verkabelung, Verhalten bei Ausfall, das Page-Protokoll |
| [Der Programmierport für Servos von KST und Chaservo](LinkKST-de.md) | Das Drahtprotokoll des Programmierports für Servos von KST und Chaservo |
| [Der Programmierport für Servos von Hitec](LinkHitec-de.md) | Das Drahtprotokoll des Programmierports für Servos von Hitec |
| [Die Pages für ESC-Einstellungen](LinkEsc-de.md) | Pages zum Auslesen und Schreiben von ESC-Einstellungen über die Signalleitung |
| [Die INA3221-Map und ihre Fenster](LinkSensors-de.md) | Die Map der INA3221-Strommonitore und ihre Abtastfenster |
| [Die Prüfstandskonsole](BenchConsole-de.md) | Die Befehlskonsole des IOmcu an seinem seriellen USB-Anschluss (Universal Serial Bus), ein Werkzeug für den Prüfstand |
| [DShot und die Output-Treiber](DShot-de.md) | DShot-Frames, Telemetrie und die Ausgangstreiber |
| [Das OpenYGE-Protokoll](OpenYGE-de.md) | Spezifikation des ESC-Telemetrie- und Parameterprotokolls |
| [ESC-Programmierprofile](EscProfiles-de.md) | Die Profile für die ESC-Programmierung |
| [ESC-Signalleitung](EscWire-de.md) | Einstellungen eines ESC über die Signalleitung auslesen und schreiben: AM32, Bluejay, BLHeli_S, ESCape32 |
| [Performance](Performance-de.md) | Zeichenbudget in Cache-Line-Fills |
| [Sprache der Oberfläche](Language-de.md) | Die Sprache der Oberfläche und ihre Texte |

Der Stand des Projekts, die offenen Punkte und die getroffenen Entscheidungen
stehen in
[STATUS.md](https://github.com/subtilitas/rcbench/blob/main/STATUS.md)
(englisch).

Hersteller-, Produkt-, Protokoll- und Firmware-Namen gehören ihren Inhabern
und dienen nur dazu, Kompatibilität zu beschreiben:
[Rechtliche Hinweise und Haftungsausschluss](https://github.com/subtilitas/rcbench/blob/main/README-de.md#rechtliche-hinweise-und-haftungsausschluss).

Jede Seite gibt es auch auf Englisch; der Umschalter steht oben auf jeder
Seite.
