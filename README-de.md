# rcbench

<sub>[English](README.md) · **Deutsch**</sub>

[![CI](https://github.com/subtilitas/rcbench/actions/workflows/ci.yml/badge.svg)](https://github.com/subtilitas/rcbench/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/subtilitas/rcbench/branch/main/graph/badge.svg)](https://codecov.io/gh/subtilitas/rcbench)

Ein Motor-, ESC- (Electronic Speed Controller, Motorregler) und Servoprüfstand
auf zwei Prozessoren: ein ESP32-S3-Touchpanel (Bedienoberfläche, Einstellungen,
SD-Karte) und ein RP2350-Koprozessor (Messung, Ausgänge und jedes Protokoll mit
Zeitanforderungen), verbunden über CAN (Controller Area Network) mit 1 Mbit/s.

**Stand: im Aufbau.** Das Projekt ist in Entwicklung, und seine Funktionen
sind nicht auf Hardware verifiziert. Das Panel bootet, und jeder Bildschirm
existiert. Die Ausgangstreiber sind gebaut. Ein ARM nennt die Rollen, die es
treibt: die Throttle-Ausgänge von MOTOR & ESC aus, die Surfaces von SERVO
aus. Eine Rolle in ARMED treibt jeden an sie gebundenen Pin auf ihrer
Ruhelage, ob etwas ihn kommandiert oder nicht. Nicht gemessen ist das
Timing an einem Messgerät -- keine Bitbreite, keine Rahmenperiode und keine
Antwortverzögerung in diesem Baum ist auf einem Oszilloskop oder einem
Logikanalysator gesehen worden. Die übrigen Messungen warten auf Bauteile, die
nicht bestückt sind. Bildschirme mit simulierten Werten sind mit SIMULATION
markiert.

[STATUS.md](STATUS.md) (englisch) hält fest, was gebaut ist, was offen ist und
was nicht geplant ist.

## Sicherheit

Dieser Prüfstand treibt Motoren und Servos. Solange der Prüfstand ARMED ist,
außerhalb der Propellerebene bleiben.

Das Schalten auf ARMED ist bewusst: ein zwei Sekunden langes Halten auf ARM, wobei
das Kommando abgeht, wenn das Halten durchgelaufen ist, und nicht wenn der
Finger abhebt. Das Schalten auf SAFE und STOP sind ein einzelner Druck.

Drei Stoppmechanismen sind vorgesehen: ein Heartbeat, dessen Ausbleiben die
Ausgänge abschaltet, der Link-Watchdog des Koprozessors und ein STOP-Kommando
über den Link. Die Leitung zwischen J8 des Panels und GP3 des
Koprozessors ist auf dem Aufbau-Prüfstand bestückt. Das retriggerbare Monoflop des Heartbeats ist auf keiner
Platine vorhanden, die Ausgänge hängen also allein an der Firmware an beiden
Enden, ohne Hardware-Rückfallebene dahinter.
[Sicherheit](https://github.com/subtilitas/rcbench/wiki/Safety-de) spezifiziert
alle drei.

Nichts hiervon hat eine Sicherheitszertifizierung durchlaufen. Das „without
warranty of any kind" der MIT-Lizenz gilt.

## Funktionen

| Bildschirm | Funktion | Stand |
| --- | --- | --- |
| Motor & ESC | Spannung, Strom, Verbrauch, Drehzahl und Temperaturen live geplottet | Bildschirm gebaut; Drehzahl, Spannung, Strom und ESC-Temperatur aus der bidirektionalen DShot-Telemetrie eines ESC; simulierte Werte, solange kein Coprozessor antwortet |
| Servo | befohlene und gemessene Stellung; Suche nach der eingebauten Endlage; Abgleich zweier Servos; Servotypen mit ihren Bildwiederholraten, eine Warnung vor denen, die ein Servo zerstören können; ein Sweep durch Rechteck, Sinus oder Dreieck, vom Koprozessor gefahren; die Leistung des Netzteils live; ein automatischer Test, der die Spannung des Netzteils stuft, mit CSV und Textbericht; ein [Programmierer für KST- und Chaservo-Servos](docs/ServoProgrammer-de.md) mit Backup, Limitprüfung und Schreibschalter | Bildschirm gebaut und steuert über den Link; treibt die als Ruderflächen gebundenen Pins, Timing ungemessen; die Endlagensuche und der Abgleich sind auf dem Host gegen ein Servomodell getestet und laufen auf keiner Platine; der Programmierer ist auf dem Host getestet, kein Frame an ein Servo gesendet |
| Netzteil | der PD mini, ein USB-PD-Netzteil (USB Power Delivery): Spannung und Strombegrenzung einstellen, Ausgang schalten, V, A und W plotten und loggen | Bildschirm gebaut; der Koprozessor steuert den PD mini über einen PIO-UART (Programmable Input/Output, Universal Asynchronous Receiver-Transmitter), wenn SETUP ihn einschaltet, sonst rechnet das Panel ein Modell |
| Analyser | sechzehn Empfängerkanäle mit Verlauf, die Digitalkanäle, LIVE / FRAME LOST / FAILSAFE / SILENT | S.BUS-Decoder gebaut; PIO-Empfänger (Programmable Input/Output) nicht geschrieben |
| Programmierer | Parametertabellen für ESCs mit BLHeli_S-, AM32-, ESCape32- und VESC-Firmware und für Hitec-Servos; [ESC-Programmierprofile](docs/EscProfiles-de.md) für Gasknüppel-Menüs: 72 Familien, 451 Modelle | Bildschirm gebaut; kein Protokoll auf einer Leitung. Profile eingebaut und aus `/ESC/` auf der Karte gelesen. Die [Stick-Programmierung](docs/StickProgramming-de.md) führt 24 davon vom Bildschirm PROGRAMMER aus und fragt dabei zur rechten Zeit nach Jumper oder Taster eines Kontronik-ESC, gegen den simulierten ESC des Panels |
| Auswuchten | Blattzahl, Korrekturmasse und -winkel, Anleitungen zur Sensorplatzierung | Bildschirm gebaut; Sensoren nicht bestückt |
| Akku | Zellenspreizung und Bewertung | Bildschirm gebaut; Zellenmonitor nicht bestückt |
| Logs | CSV (Comma-Separated Values) von der Karte durchsehen, importieren und plotten; Läufe werden in ARMED oder bei eingeschaltetem Netzteil-Ausgang aufgezeichnet | gebaut |
| Setup | Einstellungen in beiden Themes, gespeichert im NVS | gebaut |

## Bauen

Die Host-Suite braucht einen C-Compiler, CMake und Python 3:

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
| [Worum es geht](https://github.com/subtilitas/rcbench/wiki/Manifest-de) | Anforderungen und ihre Umsetzung |
| [Bench configurator](https://subtilitas.github.io/rcbench/) | Webseite, Deutsch und Englisch: Funktionen oder Teile auswählen, ergibt die Teileliste, eine Einkaufsliste mit Herstellerseiten und Links zur Websuche, die Verdrahtungszeichnung und die Pin-zu-Pin-Tabelle dieses Aufbaus |
| [Bauen](https://github.com/subtilitas/rcbench/wiki/Building-de) | Toolchains, Befehle, CI |
| [Den Link in Betrieb nehmen](https://github.com/subtilitas/rcbench/wiki/Bringup-de) | die beiden Platinen verkabeln und den Bus prüfen |
| [Bildschirme](https://github.com/subtilitas/rcbench/wiki/Screens-de) | den Prüfstand bedienen |
| [Erster Lauf auf der Hardware](https://github.com/subtilitas/rcbench/wiki/FirstRun-de) | die Prüfungen beim ersten Einschalten beider Platinen |
| [Sprache der Oberfläche](https://github.com/subtilitas/rcbench/wiki/Language-de) | Englisch oder Deutsch auf dem Panel |
| [Auswuchten](https://github.com/subtilitas/rcbench/wiki/Balance-de) · [Servoverfahren](https://github.com/subtilitas/rcbench/wiki/Servo-de) · [Empfängerbusse](https://github.com/subtilitas/rcbench/wiki/Receivers-de) | die Messungen |
| [Sicherheit](https://github.com/subtilitas/rcbench/wiki/Safety-de) | Stoppmechanismen und die nötige externe Schaltung |
| [Der Link](https://github.com/subtilitas/rcbench/wiki/Link-de) · [OpenYGE](https://github.com/subtilitas/rcbench/wiki/OpenYGE-de) · [Performance](https://github.com/subtilitas/rcbench/wiki/Performance-de) · [DShot und die Output-Treiber](https://github.com/subtilitas/rcbench/wiki/DShot-de) · [ESCs mit BLHeli_32-Firmware](https://github.com/subtilitas/rcbench/wiki/BLHeli32-de) | Firmware-Referenz |

## Mitarbeit

[CONTRIBUTING.md](CONTRIBUTING.md) (englisch) enthält die Regeln und die
Prüfungen, die CI ausführt. Zwei davon lassen sich nachträglich nicht
korrigieren: Protokolle werden aus Spezifikationen implementiert, nicht aus
anderen Implementierungen (eine Lizenzregel), und die Coverage hat neben der
Gesamtuntergrenze eine Untergrenze je Datei.

<!-- coverage:start -->
Zeilenabdeckung von `shared/` und `protocols/` durch die Host-Suite: **97,9 %**, 36 731 von 37 509 Zeilen in 128 Dateien. CI schlägt unter 94 % gesamt oder unter 85 % in einer Datei fehl. Die Tabelle je Datei steht in [STATUS.md](STATUS.md#tests-and-ci).
<!-- coverage:end -->

Sicherheitsmeldungen: [SECURITY.md](SECURITY.md).

## Lizenzen und Quellen

rcbench steht unter der MIT-Lizenz (Massachusetts Institute of Technology):
[LICENSE](LICENSE).

- **DejaVu Sans Mono.** Die drei Fonttabellen unter `shared/gfx/` sind
  Glyphen-Bitmaps, die `tools/gen_font.py` aus DejaVu Sans Mono erzeugt, unter
  dem Bitstream Vera Fonts Copyright ([NOTICE](NOTICE)). Es wird keine
  Fontdatei weitergegeben.
- **Protokolle** werden aus veröffentlichten Spezifikationen implementiert;
  das Programmierprotokoll von KST- und Chaservo-Servos, für das es keine
  gibt, aus Mitschnitten der Signalleitung an eigener Hardware des Projekts.
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

## Rechtliche Hinweise und Haftungsausschluss

### Markenhinweis

KST, CHASERVO und Hitec sowie deren Logos sind Marken oder eingetragene Marken
der jeweiligen Hersteller. Die Namen werden in diesem Projekt ausschließlich
beschreibend verwendet, um die Kompatibilität mit den jeweiligen Servos zu
dokumentieren. Dieses Projekt steht in keiner Verbindung zu den genannten
Herstellern und wird von ihnen weder unterstützt noch gesponsert.

Jeder andere Name eines Herstellers, eines Produkts, eines Protokolls oder
einer Firmware in diesem Repository ist ein Name oder eine Marke seines
Inhabers. Jeder steht nur dort, um Kompatibilität zu beschreiben, ein Teil
zu bezeichnen, die Quelle einer Schrift anzugeben oder eine Design- oder
Protokoll-Referenz zu nennen. Das gilt für:

- die Servo-Hersteller und -Marken Futaba, Feetech und Dynamixel;
- die ESC-Hersteller (Electronic Speed Controller) YGE, Kontronik, Hobbywing,
  Hacker, Jeti, Castle Creations, Graupner, Multiplex und Spektrum;
- die Protokoll- und Firmware-Namen OpenYGE, S.BUS, S.BUS2, JETI EX Bus,
  SRXL2, DShot, KISS, BLHeli_S, BLHeli_32, AM32, ESCape32 und VESC;
- die Hersteller von Boards, Modulen und Bauteilen Waveshare, Raspberry Pi,
  Espressif, WeAct, Osoyoo, Matek, DAOKAI und Texas Instruments;
- die Distributoren Digi-Key, LCSC und JLCPCB;
- jeden anderen Hersteller-, Produkt-, Protokoll- und Firmware-Namen in
  diesem Repository. Der Abschnitt „Trademarks“ in [NOTICE](NOTICE) enthält
  die Liste.

Dieses Projekt steht mit keinem der Inhaber in Verbindung und wird von keinem
von ihnen unterstützt oder gesponsert.

### Haftungsausschluss

rcbench ist Open-Source-Software und -Hardware zum Selbstbau. Nutzung, Nachbau
und das Programmieren von Servos erfolgen auf eigene Gefahr. Soweit gesetzlich
zulässig, übernimmt der Autor keine Haftung für:

- Schäden an Servos, zum Beispiel durch falsche Spannung, Timing-Fehler oder
  beschädigte Einstellungsdaten im Servo,
- Schäden an Computern, Mikrocontrollern oder anderer angeschlossener
  Hardware,
- Verletzungen oder Schäden, die durch den Betrieb der Hardware entstehen.

Die Lizenzbedingungen stehen in [LICENSE](LICENSE).
