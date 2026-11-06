# rcbench

<sub>[English](README.md) · **Deutsch**</sub>

[![CI](https://github.com/subtilitas/rcbench/actions/workflows/ci.yml/badge.svg)](https://github.com/subtilitas/rcbench/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/subtilitas/rcbench/branch/main/graph/badge.svg)](https://codecov.io/gh/subtilitas/rcbench)

## Displaywechsel: ESP32-P4-Panel ab 0.23.0

Ab 0.23.0 verwendet das Panel die 7-Zoll-Displayplatine mit ESP32-P4 von
Waveshare (ESP32-P4-WIFI6-Touch-LCD-7B, 1024 x 600 Pixel, MIPI DSI (Mobile
Industry Processor Interface, Display Serial Interface), ESP32-C6 für WLAN).
Bis 0.22 verwendet es die 7-Zoll-Displayplatine mit ESP32-S3 von Waveshare
(ESP32-S3-Touch-LCD-7, 800 x 480 Pixel).

- 0.22 ist die letzte Release-Linie für das ESP32-S3-Panel; ihr neuestes
  Release ist 0.22.1. Einem ESP32-S3-Panel werden nur 0.22.x-Patch-Releases
  angeboten.
- 0.23.0 ist das erste Release für das ESP32-P4-Panel, ein Testing-Release.
  Seine Bedienoberfläche ist für 1024 x 600 neu aufgeteilt; sie ist nicht
  das hochskalierte 800-x-480-Layout.
- Die ESP32-P4-Platine braucht die Chip-Revision 3.2 (ESP-IDF v6.1).
- Beide Panels nutzen denselben IOmcu, dieselbe IO-Platine und dieselbe CAN-Verbindung.
- Auf der ESP32-P4-Platine liegt der Heartbeat an IO28. Der Drehknopf nutzt
  IO30 (SDA) und IO31 (SCL). Alle drei liegen auf der 12-poligen Stiftleiste P3.

## Was rcbench ist

rcbench ist ein Motor-, ESC- (Electronic Speed Controller, Motorregler) und
Servoprüfstand mit zwei Prozessoren. Das Panel ist ein Waveshare ESP32-P4 7B.
Es übernimmt die Bedienoberfläche auf einem 1024x600-Display an MIPI DSI,
die Einstellungen, die SD-Karte (Secure Digital) und WLAN über seinen
ESP32-C6. Der RP2350-IOmcu (Input/Output-Mikrocontroller) übernimmt Messungen,
Ausgänge und alle Protokolle mit Zeitanforderungen. CAN (Controller Area
Network) mit 1Mbit/s verbindet die beiden.

Releases bis 0.22.1 laufen auf dem ESP32-S3-Panel. Was die Update-Suche
jedes Panels anbietet, steht in
[Bildschirme: Die Release-Linie](docs/Screens-de.md#die-release-linie).

**Stand.** Das Panel und der IOmcu arbeiten über CAN mit 1Mbit/s auf einem
Prüfstand mit einem PD mini als Netzteil, Servos und einem ESC zusammen.
Ein Betatester setzt Release-Builds ein. Das Timing der Ausgänge (Bitbreiten,
Frame-Perioden, Antwortverzögerungen) wurde nicht mit einem Messgerät geprüft.
[STATUS.md](STATUS.md) (englisch) hält fest, was umgesetzt, offen oder nicht
geplant ist.

## Sicherheit

Dieser Prüfstand steuert Motoren und Servos an. Bleibe außerhalb der
Propellerebene, solange der Prüfstand ARMED ist.

Halte ARM zwei Sekunden lang, um auf ARMED zu schalten. Der Befehl wird
nach Ablauf der Haltezeit gesendet. DISARM und STOP wirken bei einem
einzelnen Druck sofort beim Aufsetzen des Fingers, nicht erst beim Abheben.

Drei Stoppmechanismen sind vorgesehen: ein Heartbeat, dessen Ausbleiben die
Ausgänge abschaltet, der Verbindungs-Watchdog des IOmcu und ein STOP-Befehl
über die Verbindung. Die Heartbeat-Leitung führt von IO28 des Panels
(`PANEL_HEARTBEAT_PIN`, an der 12-poligen Stiftleiste P3 der
7-Zoll-ESP32-P4-Platine) zu GPIO 3 des IOmcu. Auf keiner Platine ist das
retriggerbare Monoflop für den Heartbeat bestückt. Die Firmware an beiden
Enden gibt die Ausgänge frei oder sperrt sie. Eine Hardware-Rückfallebene
für diese Funktion fehlt.
[Sicherheit](https://github.com/subtilitas/rcbench/wiki/Safety-de)
spezifiziert alle drei.

Der Prüfstand ist nicht sicherheitszertifiziert. Der Ausschluss „without
warranty of any kind“ der MIT-Lizenz gilt.

## Funktionen

| Bildschirm | Funktion | Stand |
| --- | --- | --- |
| MOTOR & ESC | Spannung, Strom, Verbrauch, Drehzahl und Temperaturen als Live-Grafik | Bildschirm gebaut; Drehzahl, Spannung, Strom und ESC-Temperatur aus der bidirektionalen DShot-Telemetrie eines ESC; simulierte Werte, solange kein IOmcu antwortet |
| SERVO | Sollstellung und gemessene Stellung; Suche nach der mechanischen Endlage im eingebauten Zustand; Abgleich zweier Servos; Servotypen mit ihren Pulsraten und Warnung vor Raten, die ein Servo zerstören können; vom IOmcu ausgeführter Sweep mit Rechteck-, Sinus- oder Dreieckskurve; Live-Leistung des Netzteils; automatischer Test mit Spannungsstufen, CSV-Datei (Comma-Separated Values) und Textbericht; [Servoprogrammierung für KST- und Chaservo-Servos](docs/ServoProgrammer-de.md) mit Backup, Grenzprüfungen und Schreibfreigabe | Seite umgesetzt, Ansteuerung über die Verbindung; steuert die als Ruder zugeordneten Pins an, Timing nicht gemessen; Endlagensuche, Abgleich und Servoprogrammierung sind auf dem Host mit einem simulierten Servo getestet |
| SUPPLY | der PD mini, ein USB-PD-Netzteil (USB Power Delivery): Spannung und Strombegrenzung einstellen, Ausgang schalten, V, A und W als Grafik anzeigen und aufzeichnen | Seite umgesetzt; der IOmcu steuert den PD mini über einen PIO-UART (Programmable Input/Output, Universal Asynchronous Receiver-Transmitter), wenn er unter SETUP eingeschaltet ist; andernfalls simuliert das Panel ihn |
| RECEIVER | sechzehn Empfängerkanäle mit Verlauf, die Digitalkanäle, LIVE / FRAME LOST / FAILSAFE / KEIN SIGNAL | S.BUS-Decoder gebaut; PIO-Empfänger (Programmable Input/Output) nicht geschrieben |
| MOTOR & ESC > PROGRAMMIEREN | [ESC-Programmierprofile](docs/EscProfiles-de.md) für Gasknüppel-Menüs: 72 Familien, 451 Modelle; [die ESC-Signalleitung](docs/EscWire-de.md): AM32, Bluejay, BLHeli_S und ESCape32, aus einer Verhaltensspezifikation geschrieben, ohne Zugriff auf die Firmware-Quellen. SimonK wird nicht unterstützt | Funktion umgesetzt. Profile sind eingebaut und werden aus `/BENCH/ESC/CONF/` auf der Karte gelesen. Die [Stick-Programmierung](docs/StickProgramming-de.md) führt 24 davon unter ESC STICK mit dem simulierten ESC des Panels aus. Sie fordert bei Bedarf zum Betätigen des Jumpers oder Tasters eines Kontronik-ESC auf. Die Parametertabellen enthalten Daten; sie implementieren kein Leitungsprotokoll und erscheinen nicht als Zeilen in einer Liste unter PROGRAMMIEREN |
| BALANCE | Blattzahl, Korrekturmasse und -winkel, Anleitungen zur Sensorplatzierung | Bildschirm gebaut; Beschleunigungssensor und Indexsensor sind nicht bestückt, es wird also nichts gemessen |
| BATTERY | Zellenspreizung und Bewertung | Bildschirm gebaut; aus einem Modell gezeichnet |
| LOGS | CSV-Dateien auf der Karte durchsehen, importieren und als Grafik anzeigen; Läufe werden in ARMED oder bei eingeschaltetem Netzteilausgang unter `BENCH/LOGS` aufgezeichnet, Tests unter `USER/LOGS` (vorgegebene Ordner); beide Arten lassen sich unter SETUP > SD CARD ein- und ausschalten sowie konfigurieren | gebaut |
| FILES | SD-Karte (Secure Digital) in zwei Bereichen: Dateien kopieren, verschieben, umbenennen und löschen, Ordner und leere Textdateien anlegen, Dateiinformationen anzeigen; Schreibzugriffe nur bei SAFE und ohne geöffnete Aufzeichnungsdatei; die eigenen Ordner des Prüfstands und die vom Logger geschriebenen Dateien sind geschützt | Bildschirm gebaut und auf dem Host getestet; öffnet aus Logs und aus Setup |
| WI-FI | Funkmodul des Panels, nur bei geöffneter Seite und für Update-Prüfungen eingeschaltet: eigener WLAN-Hotspot (WPA2 (Wi-Fi Protected Access 2), 2,4GHz Kanal 6, 1 Client) oder gespeichertes Heimnetz (WPA2 oder WPA3 personal); die Browserseite lädt einzelne Dateien oder die gesamte Karte als ZIP-Datei herunter und lädt eine Panel-Firmware hoch; zur Installation die Taste am Panel 2s halten | gebaut und auf dem Host getestet; ein Download von 16MiB lief in einem Heimnetz mit 688-757kB/s; Reichweite nicht gemessen |
| UPDATES | sucht auf GitHub das neueste Panel-Release (STABLE oder TESTING), erinnert einmal je Start und lädt es über HTTPS herunter; Installation nach 2s Halten; installiert die im Panel enthaltene IOmcu-Firmware ebenfalls nach 2s Halten auf dem IOmcu | gebaut und auf dem Host getestet; die Dauer einer Prüfung und eines Downloads in einem echten Netz ist nicht gemessen |
| OUTPUTS | zeigt die den IOmcu-Pins zugeordneten Protokolle und die jeweils angesteuerten Pins; die maßstäbliche Platinenzeichnung unter OUTPUTS zeigt, wo die Leitungen anzuschließen sind | gebaut und auf dem Host getestet |
| SETUP | Einstellungen im dunklen oder hellen Design, gespeichert im NVS (Non-Volatile Storage) | gebaut |
| SETUP > PANEL | `Zeitzone` (10 Zonen, standardmäßig BERLIN) bestimmt die Zeitzone für gespeicherte und angezeigte Zeitstempel von Dateien. Die Uhr ist nach jedem Einschalten ungestellt. Bei jeder Verbindung mit dem Heimnetz wird sie einmal innerhalb von 30000ms per SNTP (Simple Network Time Protocol) von `pool.ntp.org` gestellt. `Bilddrehung` dreht Bild und Touch um 180° | gebaut und auf dem Host getestet; die Platine hat keinen RTC-Chip (Real-Time Clock); ein gedrehtes Bild auf SERVO LIVE im Sweep brauchte 3,8 bis 4,8ms (TURN) bei 30,2fps, ungedreht 39 bis 45fps; 180° im Stillstand ist nicht gemessen |

## Bauen

Die Host-Suite braucht einen C-Compiler, CMake und Python 3:

```bash
cmake -S test/host -B test/host/build -DCMAKE_BUILD_TYPE=Debug
cmake --build test/host/build
ctest --test-dir test/host/build --output-on-failure
```

Die Panel-Firmware braucht ESP-IDF (Espressif Internet-of-Things Development
Framework) v6.1, die IOmcu-Firmware pico-sdk 2.0 oder neuer
(SDK, Software Development Kit).
[Bauen](https://github.com/subtilitas/rcbench/wiki/Building-de) beschreibt
Toolchains, Befehle und die CI-Gates (Continuous Integration).

## Dokumentation

Das [Wiki](https://github.com/subtilitas/rcbench/wiki) wird auf Englisch und
Deutsch aus [`docs/`](docs) erzeugt. Bearbeite die Dateien im Repository;
die Wiki-Seiten werden beim Veröffentlichen überschrieben.

| Seite | Inhalt |
| --- | --- |
| [Worum es geht](https://github.com/subtilitas/rcbench/wiki/Manifest-de) | Anforderungen und ihre Umsetzung |
| [Bench configurator](https://subtilitas.github.io/rcbench/) | Webseite, Deutsch und Englisch: Wähle Funktionen oder Teile aus. Daraus entstehen Teileliste, Einkaufsliste mit Herstellerseiten und Links zur Websuche, Verdrahtungszeichnung und Pin-zu-Pin-Tabelle für diesen Aufbau |
| [Panel-Hilfe](https://subtilitas.github.io/rcbench/help/de/index.html) | Webseite, Deutsch und Englisch: jede Seite des Panels mit Screenshots ihrer Zustände, Anzeigen und Bedienmöglichkeiten |
| [C-API-Referenz](https://subtilitas.github.io/rcbench/api/) | Webseite, Englisch: die Programmierschnittstelle (API) von `shared/`, `protocols/`, beiden Firmwares und dem Kern des Touch-Injectors, von Doxygen aus den Kommentaren im Quelltext erzeugt |
| [Bauen](https://github.com/subtilitas/rcbench/wiki/Building-de) | Toolchains, Befehle, CI |
| [Den Link in Betrieb nehmen](https://github.com/subtilitas/rcbench/wiki/Bringup-de) | die beiden Platinen verkabeln und den Bus prüfen |
| [Bildschirme](https://github.com/subtilitas/rcbench/wiki/Screens-de) | den Prüfstand bedienen |
| [Erster Lauf auf der Hardware](https://github.com/subtilitas/rcbench/wiki/FirstRun-de) | die Prüfungen beim ersten Einschalten beider Platinen |
| [Sprache der Oberfläche](https://github.com/subtilitas/rcbench/wiki/Language-de) | Englisch oder Deutsch auf dem Panel |
| [Auswuchten](https://github.com/subtilitas/rcbench/wiki/Balance-de) · [Servoverfahren](https://github.com/subtilitas/rcbench/wiki/Servo-de) · [Empfängerbusse](https://github.com/subtilitas/rcbench/wiki/Receivers-de) | die Messungen |
| [Sicherheit](https://github.com/subtilitas/rcbench/wiki/Safety-de) | Stoppmechanismen und die nötige externe Schaltung |
| [Der Link](https://github.com/subtilitas/rcbench/wiki/Link-de) · [OpenYGE](https://github.com/subtilitas/rcbench/wiki/OpenYGE-de) · [Performance](https://github.com/subtilitas/rcbench/wiki/Performance-de) · [DShot und die Output-Treiber](https://github.com/subtilitas/rcbench/wiki/DShot-de) · [ESCs mit BLHeli_32-Firmware](https://github.com/subtilitas/rcbench/wiki/BLHeli32-de) | Firmware-Referenz |

## Mitarbeit

[CONTRIBUTING.md](CONTRIBUTING.md) (englisch) enthält die Regeln und
CI-Prüfungen. Protokolle müssen aus Spezifikationen implementiert werden,
nicht aus anderen Implementierungen; das ist eine Lizenzvorgabe. Die
Testabdeckung muss sowohl die Gesamtuntergrenze als auch die Untergrenze
für jede Datei erreichen.

<!-- coverage:start -->
Zeilenabdeckung von `shared/` und `protocols/` durch die Host-Suite: **98,1%**, 83 138 von 84 716 Zeilen in 366 Dateien. CI schlägt unter 94% gesamt oder unter 85% in einer Datei fehl. Die Tabelle je Datei steht in [STATUS.md](STATUS.md#tests-and-ci).
<!-- coverage:end -->

Sicherheitsmeldungen: [SECURITY.md](SECURITY.md).

## Lizenzen und Quellen

rcbench steht unter der MIT-Lizenz (Massachusetts Institute of Technology):
[LICENSE](LICENSE).

- **DejaVu Sans Mono.** Die sieben Fonttabellen unter `shared/gfx/` sind
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
