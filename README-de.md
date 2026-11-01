# rcbench

<sub>[English](README.md) · **Deutsch**</sub>

[![CI](https://github.com/subtilitas/rcbench/actions/workflows/ci.yml/badge.svg)](https://github.com/subtilitas/rcbench/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/subtilitas/rcbench/branch/main/graph/badge.svg)](https://codecov.io/gh/subtilitas/rcbench)

## Displaywechsel: ESP32-P4-Panel ab 0.23.0

Ab 0.23.0 ist das Panel das 7-Zoll-ESP32-P4-Displayboard von Waveshare
(ESP32-P4-WIFI6-Touch-LCD-7B, 1024 x 600 Pixel, MIPI DSI (Mobile Industry
Processor Interface, Display Serial Interface), ESP32-C6 für Wi-Fi). Bis 0.22
ist es das 7-Zoll-ESP32-S3-Displayboard von Waveshare
(ESP32-S3-Touch-LCD-7, 800 x 480 Pixel).

- 0.22 ist die letzte Release-Linie für das ESP32-S3-Panel; ihr neuestes
  Release ist 0.22.1. Einem ESP32-S3-Panel werden nur 0.22.x-Patch-Releases
  angeboten.
- 0.23.0 ist das erste Release für das ESP32-P4-Panel, ein Testing-Release.
  Seine Bedienoberfläche ist für 1024 x 600 neu aufgeteilt; sie ist nicht
  das hochskalierte 800-x-480-Layout.
- Das ESP32-P4-Board braucht die Chip-Revision 3.2 (ESP-IDF v6.1).
- Beide Panels nutzen denselben IOmcu, dasselbe IO-Board und denselben CAN-Link.
- Auf dem ESP32-P4-Board ist der Heartbeat IO28 und der Drehknopf IO30
  (SDA) und IO31 (SCL), alle auf der 12-poligen Stiftleiste P3.

## Was rcbench ist

Ein Motor-, ESC- (Electronic Speed Controller, Motorregler) und Servoprüfstand
auf zwei Prozessoren. Das Panel ist ein ESP32-P4-Touchpanel, das Waveshare
ESP32-P4 7B: Bedienoberfläche auf einem 1024x600-Display an MIPI DSI,
Einstellungen, SD-Karte (Secure Digital) und Wi-Fi über seinen ESP32-C6. Der
IOmcu ist ein RP2350 (Input/Output-Mikrocontroller): Messung, Ausgänge und
jedes Protokoll mit Zeitanforderungen. CAN (Controller Area Network) mit
1Mbit/s verbindet die beiden.

Releases bis 0.22.1 laufen auf dem ESP32-S3-Panel. Was die Update-Suche
jedes Panels anbietet, steht in
[Bildschirme: Die Release-Linie](docs/Screens-de.md#die-release-linie).

**Stand.** Das Panel und der IOmcu laufen über CAN mit 1Mbit/s zusammen auf
einem Prüfstand mit einem PD mini als Netzteil, Servos und einem ESC, und ein
Betatester setzt Release-Builds ein. Das Timing der Ausgänge (Bitbreiten,
Rahmenperioden, Antwortverzögerungen) ist an keinem Messgerät gemessen.
[STATUS.md](STATUS.md) (englisch) hält fest, was gebaut ist, was offen ist und
was nicht geplant ist.

## Sicherheit

Dieser Prüfstand treibt Motoren und Servos. Solange der Prüfstand ARMED ist,
außerhalb der Propellerebene bleiben.

Das Schalten auf ARMED ist bewusst: ein zwei Sekunden langes Halten auf ARM, wobei
das Kommando abgeht, wenn das Halten durchgelaufen ist, und nicht wenn der
Finger abhebt. Das Schalten auf SAFE und STOP sind ein einzelner Druck und
wirken beim Aufsetzen des Fingers, nicht beim Abheben.

Drei Stoppmechanismen sind vorgesehen: ein Heartbeat, dessen Ausbleiben die
Ausgänge abschaltet, der Link-Watchdog des IOmcu und ein STOP-Kommando über
den Link. Die Heartbeat-Leitung läuft von IO28 des Panels (`PANEL_HEARTBEAT_PIN`,
am 12-poligen Header P3 des 7-Zoll-ESP32-P4-Boards) zu GPIO 3 des IOmcu. Keine
Platine trägt das retriggerbare Monoflop des Heartbeats. Die Firmware an
beiden Enden schaltet die Ausgänge, und dahinter sitzt keine
Hardware-Rückfallebene.
[Sicherheit](https://github.com/subtilitas/rcbench/wiki/Safety-de)
spezifiziert alle drei.

Nichts hiervon hat eine Sicherheitszertifizierung durchlaufen. Das „without
warranty of any kind" der MIT-Lizenz gilt.

## Funktionen

| Bildschirm | Funktion | Stand |
| --- | --- | --- |
| Motor & ESC | Spannung, Strom, Verbrauch, Drehzahl und Temperaturen live geplottet | Bildschirm gebaut; Drehzahl, Spannung, Strom und ESC-Temperatur aus der bidirektionalen DShot-Telemetrie eines ESC; simulierte Werte, solange kein IOmcu antwortet |
| Servo | befohlene und gemessene Stellung; Suche nach der eingebauten Endlage; Abgleich zweier Servos; Servotypen mit ihren Bildwiederholraten, eine Warnung vor denen, die ein Servo zerstören können; ein Sweep durch Rechteck, Sinus oder Dreieck, vom IOmcu gefahren; die Leistung des Netzteils live; ein automatischer Test, der die Spannung des Netzteils stuft, mit CSV-Datei (Comma-Separated Values) und Textbericht; ein [Programmierer für KST- und Chaservo-Servos](docs/ServoProgrammer-de.md) mit Backup, Limitprüfung und Schreibschalter | Bildschirm gebaut und steuert über den Link; treibt die als Ruderflächen gebundenen Pins, Timing ungemessen; die Endlagensuche, der Abgleich und der Programmierer sind auf dem Host gegen ein Servomodell getestet |
| Netzteil | der PD mini, ein USB-PD-Netzteil (USB Power Delivery): Spannung und Strombegrenzung einstellen, Ausgang schalten, V, A und W plotten und loggen | Bildschirm gebaut; der IOmcu steuert den PD mini über einen PIO-UART (Programmable Input/Output, Universal Asynchronous Receiver-Transmitter), wenn SETUP ihn einschaltet, sonst rechnet das Panel ein Modell |
| Receiver | sechzehn Empfängerkanäle mit Verlauf, die Digitalkanäle, LIVE / FRAME LOST / FAILSAFE / KEIN SIGNAL | S.BUS-Decoder gebaut; PIO-Empfänger (Programmable Input/Output) nicht geschrieben |
| Motor & ESC > Programmieren | [ESC-Programmierprofile](docs/EscProfiles-de.md) für Gasknüppel-Menüs: 72 Familien, 451 Modelle; [die ESC-Signalleitung](docs/EscWire-de.md): AM32, Bluejay, BLHeli_S und ESCape32, aus einer Verhaltensspezifikation geschrieben, ohne Zugriff auf die Firmware-Quellen. SimonK wird nicht unterstützt | Aufgabe gebaut. Profile eingebaut und aus `/BENCH/ESC/CONF/` auf der Karte gelesen. Die [Stick-Programmierung](docs/StickProgramming-de.md) führt 24 davon aus ESC STICK aus und fragt dabei zur rechten Zeit nach Jumper oder Taster eines Kontronik-ESC, gegen den simulierten ESC des Panels. Die Parametertabellen sind Daten: kein Protokoll auf einer Leitung, keine Zeile in einer Liste von PROGRAMMIEREN |
| Auswuchten | Blattzahl, Korrekturmasse und -winkel, Anleitungen zur Sensorplatzierung | Bildschirm gebaut; Beschleunigungssensor und Indexsensor sind nicht bestückt, es wird also nichts gemessen |
| Akku | Zellenspreizung und Bewertung | Bildschirm gebaut; aus einem Modell gezeichnet |
| Logs | CSV von der Karte durchsehen, importieren und plotten; Läufe werden in ARMED oder bei eingeschaltetem Netzteil-Ausgang nach `BENCH/LOGS` aufgezeichnet, Tests nach `USER/LOGS` (die vorgegebenen Ordner), beides abschaltbar und einstellbar unter SETUP > SD-KARTE | gebaut |
| Files | die SD-Karte (Secure Digital) in zwei Panes: Dateien kopieren, verschieben, umbenennen und löschen, Ordner und leere Textdateien anlegen, Dateiinformation; schreibt nur, solange der Prüfstand auf SAFE steht und keine Logdatei offen ist, und verweigert die eigenen Ordner des Prüfstands und die Dateien, die der Logger schreibt | Bildschirm gebaut und auf dem Host getestet; öffnet aus Logs und aus Setup |
| WLAN | das Funkmodul des Panels, nur an, solange der Bildschirm offen ist und für Update-Prüfungen: ein eigener Access Point (WPA2 (Wi-Fi Protected Access 2), 2,4GHz Kanal 6, 1 Client) oder ein gemerktes Heimnetz (WPA2 oder WPA3 personal); eine Browser-Seite lädt jede Datei der Karte oder die ganze Karte als eine ZIP-Datei herunter und lädt ein Panel-Firmware-Image hoch, das ein 2s-Hold am Panel installiert | gebaut und auf dem Host getestet; ein Download von 16MiB lief in einem Heimnetz mit 688-757kB/s; Reichweite nicht gemessen |
| Updates | fragt GitHub nach dem neuesten Panel-Release (STABLE oder TESTING), erinnert einmal je Start, lädt es über HTTPS herunter und installiert es auf einen 2s-Hold; installiert das IOmcu-Image, das das Panel trägt, auf einen 2s-Hold auf dem IOmcu | gebaut und auf dem Host getestet; die Dauer einer Prüfung und eines Downloads in einem echten Netz ist nicht gemessen |
| Outputs | die an Pins des IOmcu gebundenen Protokolle und die Pins, die jedes treibt, hinter der Kachel OUTPUTS, auf der maßstäblich gezeichneten IOmcu-Platine, die zeigt, wohin die Leitung eines Pins gehört | gebaut und auf dem Host getestet |
| Setup | Einstellungen im dunklen und im hellen Theme, gespeichert im NVS (Non-Volatile Storage) | gebaut |
| Setup > Panel | `Zeitzone` (10 Zonen, standardmäßig BERLIN) bestimmt die Zone, in der Dateidaten geschrieben und gezeigt werden. Die Uhr startet bei jedem Einschalten ungestellt und wird bei jedem Beitritt zum Heimnetz einmal binnen 30000ms per SNTP (Simple Network Time Protocol) von `pool.ntp.org` gestellt. `Bilddrehung` dreht Bild und Touch um 180° | gebaut und auf dem Host getestet; die Platine hat keinen RTC-Chip (Real-Time Clock); ein gedrehtes Bild auf SERVO LIVE im Sweep brauchte 3,8 bis 4,8ms (TURN) bei 30,2fps, ungedreht 39 bis 45fps; 180° im Stillstand ist nicht gemessen |

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

Das [Wiki](https://github.com/subtilitas/rcbench/wiki) wird aus [`docs/`](docs)
erzeugt, auf Englisch und Deutsch. Die Dateien bearbeiten, nicht das Wiki.

| Seite | Inhalt |
| --- | --- |
| [Worum es geht](https://github.com/subtilitas/rcbench/wiki/Manifest-de) | Anforderungen und ihre Umsetzung |
| [Bench configurator](https://subtilitas.github.io/rcbench/) | Webseite, Deutsch und Englisch: Funktionen oder Teile auswählen, ergibt die Teileliste, eine Einkaufsliste mit Herstellerseiten und Links zur Websuche, die Verdrahtungszeichnung und die Pin-zu-Pin-Tabelle dieses Aufbaus |
| [Panel-Hilfe](https://subtilitas.github.io/rcbench/help/de/index.html) | Webseite, Deutsch und Englisch: jeder Bildschirm des Panels mit Screenshots seiner Zustände, was er zeigt und was er bedienen lässt |
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

[CONTRIBUTING.md](CONTRIBUTING.md) (englisch) enthält die Regeln und die
Prüfungen, die CI ausführt. Zwei davon lassen sich nachträglich nicht
korrigieren: Protokolle werden aus Spezifikationen implementiert, nicht aus
anderen Implementierungen (eine Lizenzregel), und die Coverage hat neben der
Gesamtuntergrenze eine Untergrenze je Datei.

<!-- coverage:start -->
Zeilenabdeckung von `shared/` und `protocols/` durch die Host-Suite: **98,1%**, 75 212 von 76 657 Zeilen in 321 Dateien. CI schlägt unter 94% gesamt oder unter 85% in einer Datei fehl. Die Tabelle je Datei steht in [STATUS.md](STATUS.md#tests-and-ci).
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
