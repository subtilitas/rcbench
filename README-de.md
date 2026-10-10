# rcbench

<sub>[English](README.md) · **Deutsch**</sub>

[![CI](https://github.com/subtilitas/rcbench/actions/workflows/ci.yml/badge.svg)](https://github.com/subtilitas/rcbench/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/subtilitas/rcbench/branch/main/graph/badge.svg)](https://codecov.io/gh/subtilitas/rcbench)

Ein Motor-, ESC- (Electronic Speed Controller, Motorregler) und Servoprüfstand
auf zwei Prozessoren: ein ESP32-S3-Touchpanel (Bedienoberfläche, Einstellungen,
SD-Karte) und ein RP2350-Koprozessor (Messung, Ausgänge und jedes Protokoll mit
Zeitanforderungen), verbunden über CAN (Controller Area Network) mit 1 Mbit/s.

**Stand: im Aufbau.** Das Panel bootet, jeder Bildschirm existiert, und der
CAN-Link läuft auf Hardware. Die Ausgangstreiber sind gebaut, und eine
scharfgeschaltete Bank treibt jeden gebundenen Pin auf der Ruhelage seiner
Rolle, ob etwas ihn kommandiert oder nicht; ein Motor und ein Servo sind je
vom Panel aus auf einem Aufbau-Prüfstand gelaufen. Nicht gemessen ist das
Timing an einem Messgerät -- keine Bitbreite, keine Rahmenperiode und keine
Antwortverzögerung in diesem Baum ist auf einem Oszilloskop oder einem
Logikanalysator gesehen worden. Die übrigen Messungen warten auf Bauteile, die
nicht bestückt sind. Bildschirme mit simulierten Werten sind mit SIMULATION
markiert.

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
über den Link. Die Leitung zwischen J8 des Panels und GP3 des
Koprozessors ist auf dem Aufbau-Prüfstand bestückt, dort funktioniert das
Scharfschalten. Das retriggerbare Monoflop des Heartbeats ist auf keiner
Platine vorhanden, die Ausgänge hängen also allein an der Firmware an beiden
Enden, ohne Hardware-Rückfallebene dahinter. Das STOP-Kommando über den Link
ist geschrieben und nicht auf Hardware gelaufen.
[Sicherheit](https://github.com/subtilitas/rcbench/wiki/Safety-de) spezifiziert
alle drei.

Nichts hiervon hat eine Sicherheitszertifizierung durchlaufen. Das „without
warranty of any kind" der MIT-Lizenz gilt.

## Funktionen

| Bildschirm | Funktion | Stand |
| --- | --- | --- |
| Motor & ESC | Spannung, Strom, Verbrauch, Drehzahl und Temperaturen live geplottet | Bildschirm gebaut; Drehzahl, Spannung, Strom und ESC-Temperatur aus der bidirektionalen DShot-Telemetrie eines ESC, gegen einen AM32-ESC gelaufen; simulierte Werte, solange kein Coprozessor antwortet |
| Servo | befohlene und gemessene Stellung; Suche nach der eingebauten Endlage; Abgleich zweier Servos; Servotypen mit ihren Bildwiederholraten, eine Warnung vor denen, die ein Servo zerstören können; ein Sweep durch Rechteck, Sinus oder Dreieck, vom Koprozessor gefahren; die Leistung des Netzteils live; ein automatischer Test, der die Spannung des Netzteils stuft, mit CSV und Textbericht | Bildschirm gebaut und steuert über den Link; treibt die als Ruderflächen gebundenen Pins, auf einem Aufbau-Prüfstand gelaufen, Timing ungemessen |
| Netzteil | der PD mini, ein USB-PD-Netzteil (USB Power Delivery): Spannung und Strombegrenzung einstellen, Ausgang schalten, V, A und W plotten und loggen | Bildschirm gebaut; der Koprozessor steuert den PD mini über einen PIO-UART (Programmable Input/Output, Universal Asynchronous Receiver-Transmitter), wenn SETUP ihn einschaltet, sonst rechnet das Panel ein Modell; mit 0.10.0 und 0.10.1 gegen ein Modul gelaufen, nichts nach 0.10.1 |
| Analyser | sechzehn Empfängerkanäle mit Verlauf, die Digitalkanäle, LIVE / FRAME LOST / FAILSAFE / SILENT | S.BUS-Decoder gebaut; PIO-Empfänger (Programmable Input/Output) nicht geschrieben |
| Programmierer | Parametertabellen für BLHeli_S, AM32, ESCape32, VESC und Hitec; [ESC-Programmierprofile](docs/EscProfiles-de.md) für Gasknüppel-Menüs: 72 Familien, 451 Modelle | Bildschirm gebaut; kein Protokoll auf einer Leitung. Profile eingebaut und aus `/ESC/` auf der Karte gelesen; keines an einem ESC gelaufen. Die [Stick-Programmierung](docs/StickProgramming-de.md) führt 24 davon vom Bildschirm PROGRAMMER aus und fragt dabei zur rechten Zeit nach Jumper oder Taster eines Kontronik-ESC, gegen den simulierten ESC des Panels; nie gegen einen ESC |
| Auswuchten | Blattzahl, Korrekturmasse und -winkel, Anleitungen zur Sensorplatzierung | Bildschirm gebaut; Sensoren nicht bestückt |
| Akku | Zellenspreizung und Bewertung | Bildschirm gebaut; Zellenmonitor nicht bestückt |
| Logs | CSV (Comma-Separated Values) von der Karte durchsehen, importieren und plotten; Läufe werden im scharfen Zustand oder bei eingeschaltetem Netzteil-Ausgang aufgezeichnet | gebaut |
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
| Bauanleitung, PDF: [Deutsch](https://github.com/subtilitas/rcbench/releases/latest/download/rcbench-build-guide-de.pdf) · [English](https://github.com/subtilitas/rcbench/releases/latest/download/rcbench-build-guide-en.pdf) | die Teile des Beta-Prüfstands (Display, RP2350-CAN, PD mini, INA228) mit Shop-Links, Verkabelung und Flashen; 4 Seiten, an jedem Release |
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

<!-- coverage:start -->
Zeilenabdeckung von `shared/` durch die Host-Suite: **97,4 %**, 26 654 von 27 378 Zeilen in 106 Dateien. CI schlägt unter 94 % gesamt oder unter 85 % in einer Datei fehl; ausgenommen von der Grenze je Datei: `stub_screen.c`. Die Tabelle je Datei steht in [STATUS.md](STATUS.md#tests-and-ci).
<!-- coverage:end -->

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
