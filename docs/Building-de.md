# Bauen

<sub>[English](Building.md) · **Deutsch**</sub>

Drei Builds lesen einen Quellbaum: die Host-Testsuite, die Panel-Firmware
(ESP-IDF, Espressif Internet-of-Things Development Framework) und die
IOmcu-Firmware (pico-sdk).

## Der Baum

```
rcbench/
  docs/                   Wiki-Quelle, Englisch und Deutsch
  tools/                  die Prüfungen, Generatoren und Messungen unten
  test/tools/             pytest-Fälle für die Werkzeuge
  shared/                 reines C: kein ESP-IDF, kein pico-sdk, keine FreeRTOS-Typen
    gfx/                  Rasterizer und sieben Fonts
    touch/                Koordinaten- und Event-Mapping
    ui/                   Theme · Widgets · Icons · Router · Bildschirme
    settings/             typisiertes Schema und Werte
    logfile/              Zahlen- und CSV-Parsing
    link/                 Page-Protokoll · CAN-Framing · Watchdogs · Diagnose
    artwork/              Cache des Panels für Platinenzeichnungen
    bench/                bench_state · Telemetriesimulator · Log-Writer · Drehknopf-Decoder
    outputs/              Kanäle · Treibertabelle · Arming, Slew und Staleness
    safety/               Heartbeat-Generator (Panel) und -Monitor (IOmcu)
    servo/                Endlagen- und Abgleichsuche · Servomodell
    can/                  Bit Timing für beide Controller · MCP2515-Register · Echo-Selbsttest
    esc/                  ESC-Programmierprofile, ihr JSON-Leser und die Registry
    sense/                Treiber für die Strommonitore INA228 und INA3221
    fwupdate/             Prüfung des Panel-Firmware-Images · Upload-Puffer · Installer
    webaccess/            WLAN-Sitzung · Kartenliste und ZIP-Strom · die Seite · mDNS
    text/                 die Wörter der Oberfläche, alle Sprachen in einer Tabelle (nur Header)
  protocols/              ein Modul je externem Interface: ein Kern in reinem C, sein RP2350-Pin-Treiber in rp2350/, eine README.md
    dshot/                DShot-Frames · GCR-Dekodierung · eRPM · PIO-Treiber, einfach und bidirektional
    ppm/                  PPM-Frame-Layout · PIO- und DMA-Treiber
    sbus/                 S.BUS-Decoder; kein Pin-Treiber
    openyge/              OpenYGE-Framing, CRC, Status und Parameter-Cache; kein Pin-Treiber
    pdmini/               PD-mini-Treiber · PIO-UART
    phase_tap/            ESC-Ton-Detektor · Leser des Flankenrings · PIO-Capture
    kst/                  Programmierung von KST- und Chaservo-Servos: Frames, Register, Grenzen, Schreibpläne, Session · rp2350: kst_line.pio · kst_line
    hitec/                Leitung der Hitec-Servos, beide Familien: Frames, Transaktionen, Decoder der Antworten · rp2350: hitec_line.pio · hitec_line
  firmware/
    panel/                ESP-IDF-Projekt (ESP32-P4)
    iomcu/                pico-sdk-Projekt (RP2350)
  test/host/              die Host-Suite, eine Binary je Modul oder Bildschirm
  hardware/               Platinen-Designprotokoll; keine Platine existiert
```

Module unter `shared/` enthalten die Logik und haben keine
Hardwareabhängigkeit. Ein Modul unter `protocols/` ist ein Interface zu
externer Hardware und so gebaut, dass es in ein anderes pico-sdk-Projekt
kopiert wird: sein Kern bindet nur C-Standard-Header ein, sein Ordner
`rp2350/` nur den Kern und das pico-sdk. Alles andere, was Hardware anfasst,
liegt unter `firmware/`. `tools/check_protocols.py` hält jedes Modul daran
([CONTRIBUTING](https://github.com/subtilitas/rcbench/blob/main/CONTRIBUTING.md#where-code-goes)).

### Zwei Verzeichnisse, drei Builds

Jedes Modul unter `shared/` und `protocols/` bringt eine `CMakeLists.txt`
mit, die unter `ESP_PLATFORM` eine IDF-Komponente registriert und sonst eine
statische Bibliothek:

```cmake
if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_gfx STATIC ${SRCS})
    target_include_directories(rcbench_gfx PUBLIC include)
endif()
```

Das Panel setzt `EXTRA_COMPONENT_DIRS` auf `shared/` und auf jedes Modul
unter `protocols/`, das es nennt; IOmcu und Host-Suite holen die
Module, die sie brauchen, per `add_subdirectory()`. Ein Modul mit einem
Ordner `rp2350/` fügt seinen Pin-Treiber nur in einem pico-sdk-Build hinzu,
als INTERFACE-Bibliothek `rcbench_<modul>_rp2350`. Includes sind flach:
`#include "gfx.h"`.

| Modul | panel | iomcu | host |
| --- | :-: | :-: | :-: |
| `gfx` · `touch` · `ui` · `settings` · `logfile` · `sbus` | ✔ | | ✔ |
| `link` · `bench` · `outputs` · `servo` · `safety` · `can` · `text` | ✔ | ✔ | ✔ |
| `artwork` · `esc` · `fwupdate` · `webaccess` | ✔ | | ✔ |
| `openyge` · `ppm` | | ✔ | ✔ |
| `sense` · `pdmini` · `phase_tap` · `kst` · `hitec` · `dshot` | ✔ | ✔ | ✔ |

## Toolchains

| | Version | Hinweise |
| --- | --- | --- |
| ESP-IDF | v5.5.5 oder neuer auf der Linie v5.5 | der ESP32-P4 des Panels ist Chiprevision 3.2, die Espressifs Hinweis AR2026-004 mit v5.5.5 oder neuer paart; der Build setzt `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=n` und Mindestrevision 3.1. Weder v5.5.0 noch v5.5.1 lässt sich kompilieren: ihr TWAI-Treiber (Two-Wire Automotive Interface) sendet einen CAN-Frame mit DLC (Data Length Code) 0 als DLC 8, und der IOmcu lehnt jeden so gesendeten Link-Read ab. CI (Continuous Integration) baut v5.5.5 |
| pico-sdk | 2.0 oder neuer | RP2350-Unterstützung; CI baut 2.3.0 |
| ARM GNU Toolchain | 14.2 | jede `arm-none-eabi`-Version für Cortex-M33 |

`firmware/panel/sdkconfig.defaults` setzt den ESP32-P4 auf Chiprevision 3.1
oder neuer und 400 MHz, 32 MB Flash, das PSRAM im Gehäuse (PSRAM:
Pseudo-Static Random-Access Memory) auf 200 MHz, Code und Konstanten im
Flash statt im PSRAM, die nullinitialisierten Daten, die
`main/psram_bss.lf` nennt, im PSRAM ([Interner RAM](Performance-de.md#interner-ram)),
den Interrupt des DSI-Hosts (DSI: Display Serial Interface), der auch
während einer Flash-Operation läuft (`CONFIG_LCD_DSI_ISR_CACHE_SAFE`), die
Partitionstabelle bei 0xC000, die dem Bootloader ab 0x2000 40.960 Bytes
lässt (er belegt mit Rollback und Log auf INFO 0x6140 Bytes), `-O2` und
die Konsole auf UART0 (UART:
Universal Asynchronous Receiver-Transmitter) mit USB-Serial-JTAG (die
eingebaute serielle und Debug-Bridge des Chips über USB, Universal Serial
Bus) als Zweitkonsole. Von dieser Datei ausgehen, nicht von `menuconfig`.

ESP-IDF liest `sdkconfig.defaults` nur, wenn es `sdkconfig` erzeugt. Ein Baum,
der schon einmal gebaut wurde, hat bereits `firmware/panel/sdkconfig`, und
diese Datei gewinnt: nach einer Änderung der Defaults löschen, sonst wirkt die
Änderung nicht auf das Image.

## Verdrahtung des Drehknopfs

Der Drehknopf ist ein magnetischer Winkelsensor MT6701 an einem eigenen
I2C-Bus (I2C: Inter-Integrated Circuit), dem zweiten I2C-Controller des
ESP32-P4, auf zwei freien Header-Pins der 7-Zoll-ESP32-P4-Displayplatine:
`BOARD_KNOB_PIN_SDA` und `BOARD_KNOB_PIN_SCL` in
`firmware/panel/components/board/include/board_pins.h`. GPIO30 (SDA) und
GPIO31 (SCL) sind dort Platzhalter; die Header-Pins sind noch nicht
gewählt. Der Sensor nimmt 3,3 V und GND vom selben Header. Das Panel
schaltet die internen Pull-ups des ESP32-P4 ein; ob sie allein 400 kHz auf
den Leitungen des Knopfs halten, ist nicht gemessen, ein Sensor ohne eigene
Pull-ups bekommt daher welche von 4,7 kOhm nach 3,3 V, wie die
I2C-Schaltung des Datenblatts.

Der Sensor muss in seinem I2C/SSI-Modus sein (SSI: Synchronous Serial
Interface), MODE high. MODE low wählt die ABZ-Encoderausgänge, und der Sensor
antwortet dann nicht über I2C. Eine EEPROM-Einstellung (EEPROM: Electrically
Erasable Programmable Read-Only Memory) ist nicht nötig. Pins des MT6701CT im
SOP-8-Gehäuse, aus dem
[Datenblatt MagnTek MT6701, Version 1.9](https://atta.szlcsc.com/upload/public/pdf/source/20260616/FEB700C1922508565884FF0A90656B50.pdf)
(2024-05), Abschnitt 1 und Abbildung 17:

| MT6701-Pin | Anschluss an |
| --- | --- |
| 1 VDD | 3,3 V vom Header. Die Versorgung des Datenblatts ist 3,3 bis 5,0 V |
| 2 MODE | VDD. Ein interner Pull-up von 200 kOhm hält einen offenen Pin high; Abbildung 17 legt ihn an VDD. Nie GND |
| 3 OUT | nicht angeschlossen; das Panel liest den Winkel über I2C |
| 4 GND | GND |
| 5 PUSH | nicht angeschlossen |
| 6 A/SDA | SDA |
| 7 B/SCL | SCL |
| 8 Z/CSN | VDD, wie Abbildung 17 |

Ein Breakout führt weniger Pins heraus. Dort prüfen, dass MODE an VDD liegt
(ein Pin, eine Brücke oder ein Pull-up) und dass sein Versorgungspin 3,3 V
nimmt. Die I2C-Schaltung des Datenblatts hat Pull-ups von 4,7 kOhm an SDA und
SCL nach VDD; die eigenen Pull-ups eines Breakouts gehen an dessen VDD, hier
3,3 V.

- Adresse: 0x06, die Voreinstellung des Datenblatts. Das Panel liest nur
  0x06. Eine EEPROM-Programmierung kann die Adresse auf 0x46 legen (Version
  1.9; Version 0.1 des Datenblatts nennt 0x26); ein so umgestellter Sensor
  wird nicht gelesen.
- Nichts von der Platine liegt an diesem Bus. Das Panel lehnt eine Adresse
  aus der Tabelle in `shared/bench/include/panel_i2c.h` ab: 0x00 bis 0x03
  und 0x78 bis 0x7F, die die I2C-Spezifikation reserviert, und die Adressen
  von CH422G und GT911 des ESP32-S3-Panels. 0x06 liegt außerhalb. Es ist
  einer der Hs-Mode-Mastercodes (Hs: High-Speed) der Spezifikation, 0x04
  bis 0x07, die kein Gerät an diesem Bus benutzt.
- Bustakt: 400 kHz. Das Datenblatt erlaubt eine SCL-Periode (SCL: Serial
  Clock) ab 1 us und Anstiegs- und Abfallzeiten von höchstens 150 ns. Die
  Anstiegszeit am Anschluss ist nicht gemessen.
- Der Magnet ist eine diametral magnetisierte Scheibe, mittig über dem Chip.
- Die I2C-Register tragen keinen Feldstatus: Die Bits für zu schwaches und
  zu starkes Feld stehen nur im SSI-Rahmen. Ein Sensor ohne Magnet darüber
  antwortet trotzdem mit einem Winkel, und das Panel nimmt ihn als Messwert.
- Jede Abfrage liest 0x03, 0x04 und noch einmal 0x03 und verwirft eine
  Abfrage, deren zwei 0x03-Bytes sich unterscheiden. Ein High-Byte, das sich
  dazwischen ändert und wieder zurückändert, wird angenommen; ein Schritt von
  bis zu 64 Zählern (0,52 % des Wegs bei 270 deg) an einem Reglerende bleibt
  also möglich; wie oft, ist nicht gemessen.
- Drehrichtung: Beim MT6701CT-STD mit DIR (Register 0x29 Bit 1, im EEPROM)
  auf seinem Standard 0 steigt der Winkel gegen den Uhrzeigersinn, von der
  Chipoberseite gesehen. Das Panel zählt umgekehrt, also erhöht mit dem
  Magneten über der Chipoberseite eine Drehung im Uhrzeigersinn den
  Throttle. Ein MT6701CT-STV, dessen Winkel im Uhrzeigersinn steigt, ein
  Teil mit DIR = 1 oder ein von unten gesehen montierter Sensor dreht den
  Knopf andersherum.

Der Knopf ist aus, bis SETUP, PANEL, `Drehknopf` eingeschaltet wird.
[Bildschirme](Screens-de.md#panel-der-drehknopf) beschreibt, was er tut.

## Befehle

```bash
# host suite
cmake -S test/host -B test/host/build -DCMAKE_BUILD_TYPE=Debug
cmake --build test/host/build
ctest --test-dir test/host/build --output-on-failure

# panel
. $IDF_PATH/export.sh
idf.py -C firmware/panel set-target esp32p4
idf.py -C firmware/panel build
idf.py -C firmware/panel -p /dev/ttyACM0 flash monitor    # COMx on Windows

# Panel mit den Farbbalken des DSI-Hosts für 3 s beim Start, für einen
# ersten Blick auf eine Platine
idf.py -C firmware/panel -B firmware/panel/build-bars -D RCBENCH_DISPLAY_BARS=ON build

# panel carrying the IOmcu image it offers over CAN; build the
# IOmcu first
idf.py -C firmware/panel -D RCBENCH_IOMCU_BIN=$PWD/firmware/iomcu/build/rcbench-iomcu.bin build

# Panel ohne Touch-Injector: jeder Panel-Build, auch der eines Releases,
# sucht beim Start einmal einen Touch-Injector an 0x14 neben dem Glas
# (testbench/touch-injector); RCBENCH_TOUCH_INJECTOR=OFF lässt diesen Code
# weg. Die Option steht standardmäßig auf ON
idf.py -C firmware/panel -B firmware/panel/build-noinject -D RCBENCH_TOUCH_INJECTOR=OFF build

# panel, as one image at offset 0 (the ESP32-P4's bootloader sits at
# 0x2000 inside it); it erases the settings in nvs
idf.py -C firmware/panel merge-bin -o rcbench-panel-merged.bin
esptool.py -p /dev/ttyACM0 write_flash 0x0 \
    firmware/panel/build/rcbench-panel-merged.bin

# IOmcu
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S firmware/iomcu -B firmware/iomcu/build
cmake --build firmware/iomcu/build

# IOmcu, debug build that prints INA3221 CH1's 1 ms samples
cmake -S firmware/iomcu -B firmware/iomcu/build-trace -DSENSE_TRACE=ON
cmake --build firmware/iomcu/build-trace

# IOmcu, bench build that reads and writes link pages from the USB console
# (Die Prüfstandskonsole, BenchConsole-de.md); not with SENSE_TRACE
cmake -S firmware/iomcu -B firmware/iomcu/build-console -DIOMCU_CONSOLE=ON
cmake --build firmware/iomcu/build-console
```

Der IOmcu-Build erzeugt drei UF2-Dateien (USB Flashing Format):

| Datei | Enthält | Für |
| --- | --- | --- |
| `rcbench-iomcu-install.uf2` | die Partitionstabelle bei 0x0 und das Image in Slot A | ein Modul ohne die Tabelle: das eine USB-Laden aus [Erster Lauf](FirstRun-de.md#die-partitionstabelle-des-iomcu) |
| `rcbench-iomcu.uf2` | nur das Image | ein Modul mit der Tabelle; das Boot-ROM schreibt es in den Slot, der nicht läuft |
| `rcbench-iomcu-pt.uf2` | nur die Partitionstabelle | `picotool` |

Eine davon auf das Massenspeicherlaufwerk des Moduls kopieren, während das
Modul in BOOTSEL gehalten wird. `rcbench-iomcu.bin` ist das Image, das das
Panel mitbringt: die Build-Option `RCBENCH_IOMCU_BIN` des Panels nimmt seinen
Pfad, und das Panel bietet es dann über CAN an
([Link](Link-de.md#firmware-update-über-den-link)). Ein Panel ohne die Option
bietet kein IOmcu-Update an.

`PICO_BOARD` steht standardmäßig auf `pimoroni_pico_plus2_rp2350`. Das für die
Inbetriebnahme verwendete Modul ist ein Waveshare RP2350-CAN (RP2350A, 4 MB
Flash), für das das SDK (Software Development Kit) keine Board-Datei hat; der
Standardwert baut und läuft darauf. Überschreiben mit `-DPICO_BOARD=`. Die
endgültige Platine braucht einen RP2350B: der geplante Pinbedarf liegt bei 27
bis 32 GPIO (General-Purpose Input/Output).

## Werkzeuge

| Werkzeug | Zweck |
| --- | --- |
| `tools/coverage.py` | misst die Line Coverage der Host-Suite, erzwingt die Untergrenzen (94 % gesamt, 85 % je Datei) und schreibt die Tabelle in `STATUS.md` und den Wert in `README.md` und `README-de.md`; `--check` schlägt bei Abweichung fehl, ebenso bei einer C-Datei unter `shared/` oder einer Kern-Datei unter `protocols/`, die nicht in der Messung steht oder keine Zähler hat |
| `tools/mutate.py` | ändert in einer Kopie des Baums jeweils eine Zeile von `shared/` oder eines Kerns unter `protocols/` (ein Vergleich umgedreht, eine Grenze plus oder minus 1, eine gespeicherte Zuweisung entfernt), baut die Host-Suite, führt sie aus und meldet die Änderungen, mit denen die Suite besteht; standardmäßig die seit `origin/main` geänderten Zeilen, höchstens 60 Mutanten und 2700 s; CI führt es bei Pull Requests aus und schlägt bei einem Überlebenden nicht fehl |
| `tools/check_sanitizers.py` | konfiguriert den Sanitizer-Build und schlägt fehl, wenn nicht jeder Compile-Befehl unter `shared/`, `protocols/` und `test/host/` `-fsanitize=address,undefined`, `-fno-sanitize-recover=all` und `-fno-omit-frame-pointer` trägt und kein anderes Flag dieser Familie |
| `tools/check_protocols.py` | hält jedes Modul unter `protocols/` an der Regel, nach der es in einem anderen pico-sdk-Projekt gebaut wird: eine Kern-Datei bindet nur C-Standard-Header und die eigenen Header des Kerns ein, eine Datei unter `rp2350/` außerdem die Treiber-Header des Moduls, sein erzeugtes `.pio.h` und pico-sdk-Header, der Header eines Treibers bindet keinen pico-sdk-Header ein, jeder öffentliche Header trägt `extern "C"` und kompiliert für sich als C11 und als C++17 mit `-Wall -Wextra -Werror`, kein Include eines Moduls nennt einen Pfad mit `..`, einen absoluten Pfad oder ein Makro oder hat ein Token nach seinem Operanden, kein `#include_next` nennt einen anderen Header als einen C-Standard-Header oder, in einer C-Datei unter `rp2350/`, einen pico-sdk-Header, der Name `__has_include` steht außerhalb eines Literals nur als Operand von `#ifdef`, `#ifndef`, `#elifdef` oder `#elifndef` oder eines `defined`-Terms in einer `#if`- oder `#elif`-Zeile, die nur solche Terme, Leerraum, `(`, `)`, `!`, `&` und `|` enthält, keine Zeile besteht nur aus `#`, gelesen nach den Literal-Regeln von ISO C, GNU C, C23 und C++ und in einer `.pio`-Datei in dem C, das pioasm in den erzeugten Header schreibt, keine C-Datei, kein Header und kein C einer `.pio`-Datei enthält einen Trigraph (`??` gefolgt von einem aus `= / ' ( ) ! < > -`), keine Datei enthält ein NUL-Byte, das Modul hat eine `README.md` mit einer Überschrift „Interface“, einer Überschrift mit „Trademark“ darin, wo sie eine Marke nennt (die Marke gilt an jeder Stelle der Datei, in jeder Schreibweise, auch innerhalb eines Bezeichners, Dateinamens, einer Adresse oder Zahl, nicht neben einem Buchstaben, also nicht in `KSTART` oder `KstSession`), und den Worten „no pin driver“, wo es keinen Ordner `rp2350/` hat, seine CMake-Dateien nennen kein anderes Modul, und jedes `_Static_assert` außerhalb von `protocols/` auf Konstanten eines Moduls steht nach Datei und Bedingung im Werkzeug und ist noch vorhanden (CMake-Include-Verzeichnisse und Link-Ziele werden nicht geprüft, und ein mit `##` zusammengesetztes `__has_include` besteht die Prüfung, und `""R"(` und `''R"(` werden wie in C gelesen, als Literal und Raw-String, sodass ein Include nicht gefunden wird, das eine solche Folge in C verbirgt und C++11 oder später liest, wo `R` ein Literal-Suffix ist); `--check` gibt nur die gebrochenen Regeln aus |
| `tools/security_parsers.py` | listet die Module unter `shared/` und `protocols/`, deren öffentliche Header eine Funktion `parse`, `decode`, `unpack`, `_rx`, `_feed`, `_write`, `_read`, `_done` oder `_utf8_next` deklarieren, dazu die im Werkzeug erzwungenen Module, die Eingaben von außen unter anderen Namen annehmen, je mit Begründung (`shared/settings`, `shared/touch`); `--check` schlägt fehl, wenn die Parser-Tabelle in `SECURITY.md` und diese Liste abweichen oder ein erzwungener Eintrag ein Modul nennt, das es nicht gibt oder das die Namensregel schon findet; ein Modul ausgenommen wird nur durch einen Eintrag mit Begründung im Werkzeug |
| `tools/check_docs.py` | hält die Seiten am Quellbaum: Links und Anker führen irgendwohin, jedes Bild wird benutzt, die Sidebar ist vollständig, jede Seite hat ein deutsches Gegenstück, die Suite-Liste in `STATUS.md` stimmt mit CMake überein, die Zahlen der Screenshots in `STATUS.md` stimmen mit `docs/img` überein, die Tabelle der roten Leuchte auf den Seiten zur Stick-Programmierung stimmt mit `esc_stick_reason_is_fault()` überein, der Baum oben nennt jedes Modul unter `shared/` und `protocols/`, jede Quelldatei trägt eine SPDX-Zeile (SPDX: Software Package Data Exchange), die Protokollversion, die Zeiten von Heartbeat und Link, die Pins von Heartbeat und CAN, die Coverage-Untergrenzen und die Stack-Marge, die eine Seite nennt, sind die Konstanten in den Headern und Werkzeugen (`FACTS` im Werkzeug führt jeden Satz), eine Tabellenzeile, die eine C-Konstante nennt, gibt deren Wert an, [Performance](Performance-de.md) hat keine Tabelle der Obergrenzen, solange `ci.yml` kein `--max-lines`-Argument hat, die Pinzahlen in `hardware/docs/Pins.md` stimmen mit `pinmap.json` überein, eine deutsche Seite zitiert in Backticks das Deutsch, das der Bildschirm zeigt: Texte und Formate der Oberfläche, Namen, Hilfetexte, Optionen und Kategorien der Einstellungen und die Wörter des Servotests |
| `tools/wiki_links.py` | schreibt die Links einer Kopie von `docs/` für das Wiki um, das Seiten über ihren Titel adressiert: `Page.md` wird zu `Page`, ein Ziel oberhalb von `docs/` zu einer Repository-URL (`blob`, `tree`, `raw` für ein Bild); vergleicht jede Seite über markdown-it-py 3.0.0 mit ihrer Umschreibung und endet bei jeder anderen Abweichung mit 1, mit Seite und Zeile; `--check` ändert nichts; `--verify` schlägt bei einer umgeschriebenen Seite fehl, deren Links ein Wiki nicht öffnen kann |
| `tools/check_formats.py` | kompiliert `shared/` mit jedem Aufruf von `TR()` und jedem Wort des Berichts durch sein englisches Literal ersetzt, unter `-Wformat=2 -Wformat-nonliteral -Wformat-signedness`, und schlägt bei jeder Warnung fehl: jedes englische Format gegen die Argumente seines Aufrufs ([Sprache](Language-de.md)) |
| `tools/merge_texts.py`, `tools/old_texts.py`, `tools/c_tables.py` | bringen die Wörter der Oberfläche aus der Form in mehreren Dateien (Englisch in `ui_text.def` und im Schema, Deutsch in `ui_text_de.c`) in die eine Tabelle, `shared/text/include/ui_text.def`; `merge BASE THEIRS` übernimmt beim Merge die Textänderungen eines Branches, der noch die alte Form hat, in die Tabelle ([Sprache der Oberfläche](Language-de.md#einen-text-hinzufügen)) |
| `tools/gen_font.py` | erzeugt die sieben eingebetteten Fonts aus DejaVu Sans Mono neu, die fünf Text-Fonts mit den deutschen Buchstaben; `--check` schlägt fehl, wenn die eingecheckten Tabellen abweichen |
| `tools/render_ui.py` | rendert jeden Bildschirm mit dem Code, den das Panel ausführt, als PNG (Portable Network Graphics), auf Englisch nach `docs/img/` und auf Deutsch nach `docs/img/de/`; `--check` vergleicht mit den eingecheckten Bildern; `--fit` schlägt fehl, wenn ein Text in einer der beiden Sprachen dort überläuft, wo er gezeichnet wird, außer den englischen Überläufen, die das Werkzeug als bekannt führt ([Sprache](Language-de.md)) |
| `tools/frame_cost.py` | misst Cache-Line-Fills je Frame unter cachegrind mit dem Cache des ESP32-S3 und 800 x 480 px; `--check-doc` hält die Tabelle in [Performance](Performance-de.md) an einem lokalen Build; CI führt es nicht aus |
| `tools/stack_check.py` | liest die tiefste Aufrufkette jeder Panel-Task aus der gebauten ELF-Datei (Executable and Linkable Format) und schlägt fehl, wenn eine ihren Stack abzüglich 1024 Bytes überschreitet; dekodiert nur Xtensa-Code und hält beim RISC-V-Image des ESP32-P4 an, sodass CI es auf dem Panel nicht ausführt; nimmt das Build-Verzeichnis, Standard `firmware/panel/build`; `-v` gibt jede tiefste Kette und jeden Aufruf aus, dem es nicht folgen kann; `--check-doc` hält die Task-Tabelle in [Performance](Performance-de.md#stacks) am Build; `--iomcu` liest stattdessen das IOmcu-Image und schlägt fehl, wenn die tiefste Kette eines Kerns, ein Interrupt und 256 Bytes seinen Stack überschreiten; mit `--check-doc` hält es die Zeile von Kern 0 der IOmcu-Tabelle, die Zahl der von `main` aus erreichbaren Aufrufe über ein Register in beiden Performance-Seiten und den Satz zu Kern 0 in `STATUS.md`; der ELF-Leser und der Aufrufgraph stehen in `tools/stack_elf.py`, die IOmcu-Hälfte in `tools/stack_iomcu.py` |
| `tools/image_slot.py` | liest die Partitionstabelle eines Panel-Builds und schlägt fehl, wenn das Image größer ist als der kleinere der zwei OTA-Anwendungsslots (Over-the-Air), wenn die Tabelle weniger als zwei solche Slots hat oder wenn sie sich in der Größe unterscheiden; gibt den verbleibenden Platz aus; nimmt das Build-Verzeichnis, Standard `firmware/panel/build` |
| `tools/check_sizes.py` | zählt die Zeilen jeder Datei, die git verfolgt, und hält die Dateien über 1 000 Zeilen an `tools/check_sizes.txt`, das für jede den Grund nennt, warum sie ganz bleibt, und für einige eine Zeilenzahl mit Toleranz; `--check` schlägt fehl bei einer Datei über 1 000, die die Liste nicht nennt, einer gelisteten Datei mit 1 000 Zeilen oder weniger, einer Zahl außerhalb ihrer Toleranz und einem Ausschluss, der nichts trifft; eine Datei, deren Kopf das `tools/gen_*.py` nennt, das sie erzeugt, steht nicht in der Liste, wenn CI diesen Generator mit `--check` ausführt |
| `tools/sense_trace.py` | liest die mitgeschnittene Konsole eines IOmcu, der mit `-DSENSE_TRACE=ON` gebaut ist: liest jede Zeile gegen das Zeilenformat und jeden Trace der 1-ms-Samples von INA3221 CH1 gegen seine Endzeile, schreibt je Trace eine CSV-Datei (Comma-Separated Values) und spielt jede Bewegung durch `shared/servo/servo_move.c`, mit dem Filter bei 1, 4 und 8 Samples und dem Band bei 0,02, 0,05 und 0,10 A; `--servo-csv` ergänzt die Ankunft abzüglich der Stellzeit des AS5600, `--loaded-channel` nennt die Ausgangskanäle mit angeschlossenem Servo und lässt die Kommandos der anderen aus ([Erster Lauf](FirstRun-de.md#89-die-1-ms-samples-von-ch1-auf-der-konsole)) |
| `tools/pinmap_check.py` | prüft die vorläufige Pinbelegung des IO-Boards, `hardware/docs/ioboard-pinmap-draft.json`, gegen die Pinfunktionen in `io_bank0.h` des pico-sdk; braucht einen Checkout des pico-sdk |
| `tools/gen_board_drawing.py`, `tools/gen_esc_profiles.py` | erzeugen die eingecheckten Tabellen der Platinenzeichnungen und der ESC-Profile aus ihren JSON-Quellen (die Platinenzeichnung aus `configurator/drawings/panel.json` des Konfigurators); `--check` schlägt fehl, wenn die eingecheckten Tabellen abweichen |
| `tools/ci_gate.py` | wartet auf den CI-Lauf eines Commits und schlägt fehl, wenn keiner bestanden hat; der erste Schritt von `docs.yml` und `release.yml` |
| `tools/release_kind.py` | liest einen Release-Tag und gibt seine Art aus, `testing` für eine ungerade Minor-Nummer und `stable` für eine gerade, mit den Flags `prerelease` und `latest`, nach denen `release.yml` veröffentlicht; mit den Tags der vollen Releases auf GitHub dahinter ist `latest` `false` für einen stabilen Tag, der älter als einer davon ist; Exit-Code 1 bei einem Tag außerhalb der Form ([Releases](#releases)) |
| `test/tools/` | pytest-Fälle für die Werkzeuge oben: `python3 -m pytest test/tools` |
| `.clang-tidy`, `.cppcheck-suppress`, `ruff.toml` | Konfiguration für statische Analyse und Lint; jeder Befund ist ein Fehler |

`gen_font.py` sucht den Font in `RCBENCH_FONT_DIR`, dann in
`~/.local/share/fonts`, dann in den Systemfontverzeichnissen. `frame_cost.py`
braucht `valgrind`; die übrigen Werkzeuge brauchen einen C-Compiler und
Pillow. `mutate.py` braucht außerdem git und CMake, `ci_gate.py` das
Kommando `gh`. `sense_trace.py` braucht CMake und einen C-Compiler: es baut
`test/host/sense_trace_replay.c` nach `build-trace/`, wenn `--replay` kein
gebautes Programm nennt.

## Wi-Fi über den ESP32-C6

Der ESP32-P4 hat kein Funkteil. Das 7-Zoll-Display-Board von Waveshare trägt
ein ESP32-C6-MINI-1-Modul, das als SDIO-Gerät (Secure Digital input/output)
am SDMMC-Host (SD/MMC host) des P4 auf Slot 1 hängt; die SD-Karte liegt auf
Slot 0. Der Wi-Fi-Code des Panels (`firmware/panel/components/wifi_access/`)
ruft die `esp_wifi`-API auf; zwei Komponenten aus der Espressif component
registry tragen jeden Aufruf und jeden Netzwerk-Frame zum C6. lwIP
(lightweight IP stack), der Server der Seite und der TLS-Client (transport
layer security) laufen auf dem P4; der C6 betreibt das Funkteil.

| Signal | P4 | C6 |
| --- | --- | --- |
| CLK | GPIO18 | IO19 |
| CMD | GPIO19 | IO18 |
| D0 bis D3 | GPIO14 bis GPIO17 | IO20 bis IO23 |
| EN (chip enable), Low setzt zurück | GPIO54 | EN |

SDIO läuft mit 4 Bit Breite und 40 MHz.
`components/board/include/board_pins.h` nennt die Pins (`BOARD_C6_*`),
`firmware/panel/sdkconfig.defaults` gibt sie an esp_hosted, und
`wifi_radio.c` bricht den Build ab, wenn beide abweichen.

### Die managed components

| Komponente | Version | Lizenz | Aufgabe |
| --- | --- | --- | --- |
| `espressif/esp_wifi_remote` | 1.6.5 | Apache-2.0 | die `esp_wifi`-API auf einem Chip ohne Funkteil |
| `espressif/esp_hosted` | 2.13.0 | Apache-2.0 | der SDIO-Transport und die Aufrufe zum C6 |
| `espressif/wifi_remote_over_eppp` | 0.3.3 | Apache-2.0 | von `esp_wifi_remote` verlangt; kompiliert, nicht gelinkt |
| `espressif/eppp_link` | 1.1.6 | Apache-2.0 | von der Zeile darüber verlangt; kompiliert, nicht gelinkt |
| `espressif/esp_serial_slave_link` | 1.1.2 | Apache-2.0 | von der Zeile darüber verlangt; kompiliert, nicht gelinkt |

esp_hosted 2.x: jede Datei, die das Panel aus der Registry kompiliert, steht
unter Apache-2.0, außer der protobuf-c-Laufzeit in esp_hosted, BSD-2-Clause.
Der aus der `.proto`-Datei erzeugte Nachrichtencode von esp_hosted gehört
Espressif, Apache-2.0; `dummy_src.c` von `esp_wifi_remote` ist eine leere
Datei ohne Lizenzzeile.
[NOTICE](https://github.com/subtilitas/rcbench/blob/main/NOTICE) enthält
Copyright und Lizenz von protobuf-c, die bei der Weitergabe eines
Panel-Images mitgegeben werden müssen.

`components/wifi_access/idf_component.yml` nennt die ersten beiden mit
exakter Version. `firmware/panel/dependencies.lock` hält alle fünf mit ihren
Hashes und ist eingecheckt. Der erste Build lädt sie von
`components.espressif.com` nach `firmware/panel/managed_components/`, das git
ignoriert, und prüft jede gegen ihren Hash. Ein späterer Build stellt keine
Anfrage: mit gefülltem Ordner läuft ein Build ohne Netzwerk durch. Für eine
andere Version `idf_component.yml` ändern, `idf.py update-dependencies`
ausführen und die neue Lock-Datei einchecken.

### Die Firmware des C6

Der C6 betreibt Espressifs esp_hosted-Co-Prozessor-Firmware. Mit welcher
Version das Board ausgeliefert wird, ist nicht veröffentlicht und hier nicht
bekannt. Beim ersten Wi-Fi-Start nach dem Einschalten setzt das Panel den C6
über GPIO54 zurück, wartet bis zu 1500 ms auf ihn und schreibt seine Version
auf die Konsole:

```
wifi: C6: esp_hosted firmware 2.13.0, host 2.13.0
```

Eine C6-Firmware ohne die Versionsabfrage antwortet nach bis zu 5 s mit
`wifi: C6: firmware version not reported, host 2.13.0`. esp_hosted meldet
außerdem `Version mismatch: Host [2.13.0] > Co-proc [x.y.z]`, wenn Haupt-
oder Nebenversion abweichen. Laut Espressif läuft ein Host, der neuer ist als
sein Co-Prozessor, und kann Zeitüberschreitungen bei Anfragen sehen; das
Verhalten des Panels mit einer älteren C6-Firmware ist nicht gemessen. Das
Panel hat keinen Weg, den C6 zu aktualisieren.

### Fehler

- Ein C6, der nicht antwortet: der Start scheitert nach 100 Versuchen von
  esp_hosted im Abstand von 200 ms, nach höchstens 20 s, und WI-FI zeigt den
  gescheiterten Start. Spätere Starts scheitern sofort. Prüfstand und Anzeige
  laufen derweil weiter; der Start des Funkteils läuft in einem eigenen Task.
- Eine Verbindung, die während einer Sitzung ausfällt: esp_hosted meldet es,
  die Sitzung endet als gescheiterter Start, und Wi-Fi bleibt bis zum
  nächsten Neustart des Panels aus. Das Panel startet dafür nicht neu
  (`CONFIG_ESP_HOSTED_TRANSPORT_RESTART_ON_FAILURE=n`): ein Neustart würde
  den Link zum IOmcu und den Heartbeat unterbrechen.

Bekannte Einschränkungen, beide in esp_hosted:

- Meldet der Co-Prozessor in seinem Start-Event einen anderen Chip als den
  ESP32-C6, hält esp_hosted das Panel mit einer Assertion an. Das
  Start-Event kommt beim ersten Wi-Fi-Start nach dem Einschalten.
- Lassen sich der SDMMC-Controller oder sein Slot 1 nicht einrichten, oder
  fehlt der Speicher für die Queues, den Pufferpool oder die Tasks von
  esp_hosted, hält esp_hosted das Panel beim Start mit einer Assertion an,
  vor `app_main()` und bevor die Anzeige etwas zeigt. Es richtet diese bei
  jedem Start ein, auch mit Wi-Fi aus.

Die SD-Karte auf Slot 0 und der C6 auf Slot 1 teilen sich einen
SDMMC-Controller:

- ESP-IDFs SDMMC-Treiber hält die Einrichtung eines Slots nicht gegen eine
  Übertragung auf dem anderen Slot zurück. Wird die Karte eingebunden,
  während eine Wi-Fi-Sitzung Daten mit dem C6 austauscht, kann die laufende
  Übertragung scheitern; esp_hosted meldet dann eine ausgefallene
  Verbindung, und Wi-Fi bleibt bis zum nächsten Neustart des Panels aus.
  Nicht gemessen.
- Kann ESP-IDFs Einbinden Slot 0 nicht starten, nachdem der Controller
  läuft, hält es den Controller an und damit die Verbindung zum C6; Wi-Fi
  bleibt dann bis zum nächsten Neustart des Panels aus. Die eigene Prüfung
  des Panels für eine Karte, die sich nicht einbinden ließ (`storage.c`),
  lässt den Controller laufen.

### Kerne und Speicher

Auf dem ESP32-P4 läuft ein Task, der ohne Kern angelegt wird, auf Kern 0,
und Kern 1 gehört dem Control-Task und den dort festgelegten Tasks. Dafür
linkt das Panel mit `--wrap=xTaskCreatePinnedToCore`
(`components/wifi_access/wifi_radio.c`): esp_hosted legt 6 Tasks mit
Priorität 23 an, ohne einen Kern zu nennen. Die Konsole listet beim Start
jeden Task, `task <Name> core <0|1> priority <n>`.

Der statische interne RAM von esp_hosted beträgt 1 637 Byte (esp_hosted
1 576, esp_wifi_remote 61); seine Task-Stacks belegen 20 480 Byte
(4 x 3 072 und 2 x 4 096); seine Puffer sind nicht gemessen. Die
Konsolenzeile `wifi: radio up` nennt den freien internen RAM mit laufendem
Funkteil.

## CI

| Workflow | Auslöser | Jobs |
| --- | --- | --- |
| `ci.yml` | Push, Pull Request, Tag `v*`, manuell | Host-Suite; dieselbe Suite unter AddressSanitizer und UBSan (UndefinedBehaviorSanitizer); Coverage-Untergrenzen und Codecov-Upload, dessen Fehlschlag den Job fehlschlagen lässt; Font-, Docs-, Webhilfe-, Wiki-Link-, Screenshot- und Research-Skript-Prüfungen; die Prüfung der Protokollmodule; clang-tidy und cppcheck über `shared/`, die Kerne unter `protocols/` und `test/host/`, die Klassen warning, performance und portability von cppcheck über `firmware/` und die Pin-Treiber unter `protocols/`, und ruff; die pytest-Fälle der Werkzeuge; bei einem Pull Request die Mutationsprüfung der geänderten Zeilen, die meldet und bei einem Überlebenden nicht fehlschlägt; Panel-Build mit ESP-IDF v5.5.5 für den ESP32-P4 mit der Prüfung des Images gegen seinen Update-Slot, der Prüfung, dass das Image das IOmcu-Image trägt und die Symbole des Touch-Injectors und exFAT enthält, und einem zweiten Build mit `RCBENCH_TOUCH_INJECTOR=OFF`, der fehlschlägt, wenn er ein Injector-Symbol enthält; IOmcu-Build mit pico-sdk 2.3.0 mit der Prüfung der Pinbelegung und der Stacks seiner zwei Kerne, ein zweiter mit `-DSENSE_TRACE=ON` und derselben Stack-Prüfung, der fehlschlägt, wenn das Standard-Image ein Trace-Symbol enthält, und ein dritter mit `-DIOMCU_CONSOLE=ON` und derselben Stack-Prüfung, der fehlschlägt, wenn das Standard-Image ein Symbol der Konsole enthält; der Build des Touch-Injectors mit pico-sdk 2.3.0 für `waveshare_rp2350_zero`, nicht hochgeladen; Firmware-Artefakte einschließlich eines zusammengeführten Panel-Images für Offset 0 |
| `docs.yml` | Push auf `main`, der `docs/` berührt, manuell | wartet auf den CI-Lauf desselben Commits und spiegelt, wenn er bestanden hat, `docs/` ins GitHub-Wiki |
| `mutation.yml` | sonntags 03:17 UTC, manuell | jede Mutante der Zeilen von `shared/safety` und `shared/outputs` (3032 Mutanten) in 6 parallelen Shards von Dateien, ohne Obergrenze der Mutantenzahl, nach 18000 s startet in einem Shard keine mehr, die übrigen werden als nicht gelaufen gemeldet; schlägt bei einer überlebenden Mutante nicht fehl; das Ergebnis sind die Artifacts `mutation-full-0` bis `mutation-full-5` und die Job-Zusammenfassungen |
| `release.yml` | Tag `v*`, manuell für einen Tag | wartet auf den CI-Lauf des getaggten Commits und baut, wenn er bestanden hat, beide Images, packt sie mit Prüfsummen und erstellt ein Release, ein Pre-Release bei ungerader Minor-Nummer ([Releases](#releases)); daneben, ohne das Release aufzuhalten, läuft die Prüfung der Konfigurator-Daten (`configurator/core/tools/check-annotate.sh --warn-only`) und zeigt jede Warnung, etwa einen Release-Tag neuer als die Daten des Konfigurators, als Annotation und in der Zusammenfassung des Laufs |
| `pages.yml` | Pull Request und Push auf `main`, der `configurator/`, `help/`, `docs/img/` oder den Hilfe-Generator berührt | Typprüfung, Tests, Datenprüfung, Größenbudget und Browser-Tests des Configurators; der Bau der Webhilfe mit `tools/help_site.py`; bei einem Push auf `main` veröffentlicht es die gebaute Seite auf GitHub Pages, mit der Webhilfe unter `help/` |

clang-tidy läuft nicht über `firmware/`: es kompiliert jede Datei und braucht
die Header von ESP-IDF und pico-sdk, die der Analyse-Job nicht hat. Die
Klasse style von cppcheck läuft ebenfalls nicht über `firmware/`. Die
Host-Tests werden mit den Flags der Bibliotheken gebaut: `-Wall -Wextra
-Werror -Wshadow -Wconversion -Wno-sign-conversion`; `test/host/.clang-tidy`
listet die für sie abgeschalteten Checks, jeweils mit Grund.

CI installiert pillow 12.3.0, gcovr 8.6, ruff 0.16.5 und pytest 9.1.1 in
genau diesen Versionen und cppcheck 2.17.1, gebaut aus dem offiziellen
Quellarchiv des Tags (`tools/install_cppcheck.sh` prüft dessen SHA-256 gegen
den Wert in `ci.yml` und bricht bei Abweichung ab; der Build wird nach Version
und Hash gecacht). Das cppcheck der Distribution ist älter (2.13 unter Ubuntu
24.04) und meldet anders; lokal 2.17.1 mit `tools/install_cppcheck.sh 2.17.1
<SHA-256 aus ci.yml> ~/cppcheck-2.17.1` oder aus einem Distributionspaket von
2.17.1 installieren. Jeder Job hat ein `timeout-minutes`-Limit: 100 min für die Jobs, die mit `tools/ci_gate.py` auf CI warten (bis zu 5400 s, 90 min), 10 bis 60 min für die übrigen. ctest
bricht einen Test nach 120 s ab. Ist die Umgebungsvariable `CI` gesetzt,
lässt ein fehlender C++-Compiler den CMake-Configure-Schritt der Host-Suite
fehlschlagen, und ein fehlendes `git`, `cmake` oder `cc` lässt die
End-to-End-Fälle von `tools/mutate.py` fehlschlagen. `tools/check_docs.py`
hält die Tags von pico-sdk und ESP-IDF dieser Seite an die in `ci.yml` und
`release.yml`.

Jede Prüfung läuft lokal;
[CONTRIBUTING.md](https://github.com/subtilitas/rcbench/blob/main/CONTRIBUTING.md)
listet die Befehle.

Das Wiki muss vor dem ersten Lauf von `docs.yml` mindestens eine Seite
enthalten; sonst existiert das Wiki-Repository nicht und der Clone schlägt
fehl.

## Releases

Ein Release ist ein Tag `vMAJOR.MINOR.PATCH` auf `main`. Seine
Minor-Nummer nennt seine Art:

| Minor-Nummer | Art | Auf GitHub | Beispiele |
| --- | --- | --- | --- |
| ungerade | Testing-Release | Pre-Release, nie als Latest markiert | 0.21.0, 0.21.3, 0.23.0, 1.3.0 |
| gerade | Stable-Release | volles Release, als Latest markiert, außer ein neueres volles Release existiert | 0.22.0, 0.22.3, 1.4.0 |

Die Patch-Nummer trägt Korrekturen in beiden Arten: 0.21.1 korrigiert das
Testing-Release 0.21.0, 0.22.1 das Stable-Release 0.22.0.

`release.yml` liest die Art mit `tools/release_kind.py`, bevor es auf CI
(Continuous Integration) wartet. Ein Tag außerhalb der Form lässt den Job
fehlschlagen, und nichts wird gebaut. Beim Veröffentlichen läuft das
Werkzeug noch einmal, mit den Tags der vollen Releases, die GitHub dann
hält; über alle Tags läuft ein Veröffentlichen zur Zeit. Ein Stable-
Release, das älter als ein veröffentlichtes ist, wird nicht als Latest
markiert:

```bash
python3 tools/release_kind.py v0.21.0   # kind=testing prerelease=true latest=false
python3 tools/release_kind.py v0.22.0   # kind=stable prerelease=false latest=true
python3 tools/release_kind.py v0.22.0 v0.20.0 v0.24.0   # kind=stable prerelease=false latest=false
```

`https://github.com/subtilitas/rcbench/releases/latest` nennt das neueste
Stable-Release. Das Panel bietet ein Testing-Release nur an, wenn RELEASES
auf SETUP > UPDATES auf TESTING steht; mit STABLE, dem Standard, bietet es
Stable-Releases an ([Bildschirme: Updates](Screens-de.md#updates)). In
beiden Stellungen bietet es nur eine Version an, die neuer ist als die, die
es hat; ein Panel mit 0.21.0 bekommt 0.20.0 nicht angeboten. Ein
Testing-Release lässt sich auch von Hand laden: seine
`rcbench-panel-vX.Y.Z.bin` über die Browser-Seite des Panels, oder seine
Archive über USB (Universal Serial Bus), wie in
[Erster Lauf, §1](FirstRun-de.md#1-beide-platinen-flashen). Das
`rcbench-iomcu-vX.Y.Z.zip` des Releases und das Image, das das Panel
trägt, stammen aus demselben Commit.
