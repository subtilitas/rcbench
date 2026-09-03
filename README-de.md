# rcbench

<sub>[English](README.md) · **Deutsch**</sub>

[![CI](https://github.com/subtilitas/rcbench/actions/workflows/ci.yml/badge.svg)](https://github.com/subtilitas/rcbench/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/subtilitas/rcbench/branch/main/graph/badge.svg)](https://codecov.io/gh/subtilitas/rcbench)

Ein Motor-, ESC- (Electronic Speed Controller, Motorregler) und Servoprüfstand
auf zwei Prozessoren: ein ESP32-S3-Touchpanel (Bedienoberfläche, Einstellungen,
SD-Karte) und ein RP2350-Koprozessor (Messung, Ausgänge und jedes Protokoll mit
Zeitanforderungen), verbunden über CAN (Controller Area Network) mit 1 Mbit/s.

**Stand: im Aufbau.** Das Panel bootet, jeder Bildschirm existiert, und der
CAN-Link läuft auf Hardware. Kein Ausgang erzeugt ein Signal, und die
meisten Messungen warten auf Bauteile, die nicht bestückt sind. Bildschirme mit
simulierten Werten sind mit SIMULATION markiert.

[STATUS.md](STATUS.md) (englisch) hält fest, was gebaut ist, was offen ist und
was nicht geplant ist.

## Sicherheit

Dieser Prüfstand treibt Motoren und Servos. Bei scharfem Prüfstand außerhalb
der Propellerebene bleiben.

Das Scharfschalten ist bewusst: ein zwei Sekunden langes Halten auf ARM, wobei
das Kommando abgeht, wenn das Halten durchgelaufen ist, und nicht wenn der
Finger abhebt. Entschärfen und STOP sind ein einzelner Druck.

Drei Stoppmechanismen sind vorgesehen: ein Heartbeat, dessen Ausbleiben die
Ausgänge abschaltet, der Link-Watchdog des Koprozessors und ein STOP-Kommando
über den Link. Der Heartbeat braucht ein retriggerbares Monoflop, das auf
keiner Platine vorhanden ist, und auch die Leitung zwischen J8 des Panels und
GP3 des Koprozessors ist nicht bestückt: ein angeschlossener Koprozessor
verweigert deshalb jedes Scharfschalten. Das STOP-Kommando über den Link ist
geschrieben und nicht auf Hardware gelaufen.
[Sicherheit](https://github.com/subtilitas/rcbench/wiki/Safety-de) spezifiziert
alle drei.

Nichts hiervon hat eine Sicherheitszertifizierung durchlaufen. Das „without
warranty of any kind" der MIT-Lizenz gilt.

## Funktionen

| Bildschirm | Funktion | Stand |
| --- | --- | --- |
| Motor & ESC | Spannung, Strom, Verbrauch, Drehzahl und Temperaturen live geplottet | Bildschirm gebaut; Werte simuliert |
| Servo | befohlene und gemessene Stellung; Suche nach der eingebauten Endlage; Abgleich zweier Servos | Bildschirm gebaut und steuert über den Link; kein Ausgangstreiber |
| Analyser | sechzehn Empfängerkanäle mit Verlauf, die Digitalkanäle, LIVE / FRAME LOST / FAILSAFE / SILENT | S.BUS-Decoder gebaut; PIO-Empfänger (Programmable Input/Output) nicht geschrieben |
| Programmierer | Parametertabellen für BLHeli_S, AM32, ESCape32, VESC und Hitec | Bildschirm gebaut; kein Protokoll auf einer Leitung |
| Auswuchten | Blattzahl, Korrekturmasse und -winkel, Anleitungen zur Sensorplatzierung | Bildschirm gebaut; Sensoren nicht bestückt |
| Akku | Zellenspreizung und Bewertung | Bildschirm gebaut; Zellenmonitor nicht bestückt |
| Logs | CSV (Comma-Separated Values) von der Karte durchsehen, importieren und plotten; Läufe werden im scharfen Zustand aufgezeichnet | gebaut |
| Setup | Einstellungen in beiden Themes, gespeichert im NVS | gebaut; Speicherung auf Hardware bestätigt |

## Bauen

Die Host-Suite braucht einen C-Compiler und CMake:

```bash
cmake -S test/host -B test/host/build -DCMAKE_BUILD_TYPE=Debug
cmake --build test/host/build
ctest --test-dir test/host/build --output-on-failure
```

Die Panel-Firmware braucht ESP-IDF (Espressif Internet-of-Things Development
Framework) v5.4 oder neuer, die Koprozessor-Firmware pico-sdk 2.0 oder neuer
(SDK, Software Development Kit).
[Bauen](https://github.com/subtilitas/rcbench/wiki/Building-de) beschreibt
Toolchains, Befehle und die CI-Gates (Continuous Integration).

## Dokumentation

Das [Wiki](https://github.com/subtilitas/rcbench/wiki) wird aus [`docs/`](docs)
erzeugt, auf Englisch und Deutsch. Die Dateien bearbeiten, nicht das Wiki.

| Seite | Inhalt |
| --- | --- |
| [Worum es geht](https://github.com/subtilitas/rcbench/wiki/Manifest-de) | Anforderungen und ihr Stand |
| [Bauen](https://github.com/subtilitas/rcbench/wiki/Building-de) | Toolchains, Befehle, CI |
| [Den Link in Betrieb nehmen](https://github.com/subtilitas/rcbench/wiki/Bringup-de) | die beiden Platinen verkabeln und den Bus prüfen |
| [Bildschirme](https://github.com/subtilitas/rcbench/wiki/Screens-de) | den Prüfstand bedienen |
| [Auswuchten](https://github.com/subtilitas/rcbench/wiki/Balance-de) · [Servoverfahren](https://github.com/subtilitas/rcbench/wiki/Servo-de) · [Empfängerbusse](https://github.com/subtilitas/rcbench/wiki/Receivers-de) | die Messungen |
| [Sicherheit](https://github.com/subtilitas/rcbench/wiki/Safety-de) | Stoppmechanismen und die nötige externe Schaltung |
| [Der Link](https://github.com/subtilitas/rcbench/wiki/Link-de) · [OpenYGE](https://github.com/subtilitas/rcbench/wiki/OpenYGE-de) · [Performance](https://github.com/subtilitas/rcbench/wiki/Performance-de) | Firmware-Referenz |

## Mitarbeit

[CONTRIBUTING.md](CONTRIBUTING.md) (englisch) enthält die Regeln und die
Prüfungen, die CI ausführt. Zwei davon lassen sich nachträglich nicht
korrigieren: Protokolle werden aus Spezifikationen implementiert, nicht aus
anderen Implementierungen (eine Lizenzregel), und die Coverage hat neben der
Gesamtuntergrenze eine Untergrenze je Datei.

Sicherheitsmeldungen: [SECURITY.md](SECURITY.md).

## Lizenzen und Quellen

rcbench steht unter der MIT-Lizenz (Massachusetts Institute of Technology):
[LICENSE](LICENSE).

- **DejaVu Sans Mono.** Die drei Fonttabellen unter `shared/gfx/` sind
  Glyphen-Bitmaps, die `tools/gen_font.py` aus DejaVu Sans Mono erzeugt, unter
  dem Bitstream Vera Fonts Copyright ([NOTICE](NOTICE)). Es wird keine
  Fontdatei weitergegeben.
- **Protokolle** werden aus veröffentlichten Spezifikationen implementiert.
  Permissiver Referenzcode (PX4-Empfängerdecoder unter BSD-Lizenz, Berkeley
  Software Distribution; MIT-Referenzcode für SRXL2, JETI EX Bus, DShot und
  DroneCAN) wurde nur zur Bestätigung gelesen.
- **Designreferenzen:** ArduPilots IOMCU (die Aufteilung auf zwei Prozessoren
  und das Verhältnis der Watchdogs); YGEs OpenYGE-Material, aus dem die
  [Spezifikation](https://github.com/subtilitas/rcbench/wiki/OpenYGE-de)
  geschrieben ist; die veröffentlichten Protokolle und Konfiguratoren von
  BLHeli, AM32, ESCape32, VESC und Hitec.
- **Fremdcode im Baum: keiner.** `test/host/greatest.h` ist ein für dieses
  Projekt geschriebenes Test-Harness; es ist nicht die Bibliothek `greatest`.
