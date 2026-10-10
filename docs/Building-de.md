# Bauen

<sub>[English](Building.md) · **Deutsch**</sub>

Drei Builds lesen einen Quellbaum: die Host-Testsuite, die Panel-Firmware
(ESP-IDF, Espressif Internet-of-Things Development Framework) und die
Koprozessor-Firmware (pico-sdk).

## Der Baum

```
rcbench/
  docs/                   Wiki-Quelle, Englisch und Deutsch
  tools/                  die Prüfungen, Generatoren und Messungen unten
  test/tools/             pytest-Fälle für die Werkzeuge
  shared/                 reines C: kein ESP-IDF, kein pico-sdk, keine FreeRTOS-Typen
    gfx/                  Rasterizer und drei Fonts
    touch/                Koordinaten- und Event-Mapping
    ui/                   Theme · Widgets · Icons · Router · Bildschirme
    settings/             typisiertes Schema und Werte
    logfile/              Zahlen- und CSV-Parsing
    link/                 Page-Protokoll · CAN-Framing · Watchdogs · Diagnose
    bench/                bench_state · Telemetriesimulator · Log-Writer · Drehknopf-Decoder
    outputs/              Kanäle · Treibertabelle · Arming, Slew und Staleness
    safety/               Heartbeat-Generator (Panel) und -Monitor (Koprozessor)
    servo/                Endlagen- und Abgleichsuche · Servomodell
    can/                  Bit Timing für beide Controller · MCP2515-Register · Echo-Selbsttest
    sbus/                 S.BUS-Decoder
    openyge/              OpenYGE-Framing, Status und Parameter-Cache
    esc/                  ESC-Programmierprofile, ihr JSON-Leser und die Registry
    sense/                Treiber für die Strommonitore INA228 und INA3221
  firmware/
    panel/                ESP-IDF-Projekt (ESP32-S3)
    iomcu/                pico-sdk-Projekt (RP2350)
  test/host/              die Host-Suite, eine Binary je Modul oder Bildschirm
  hardware/               Platinen-Designprotokoll; keine Platine existiert
```

Module unter `shared/` enthalten die Logik und haben keine
Hardwareabhängigkeit. Alles, was Hardware anfasst, liegt unter `firmware/`.

### Ein Verzeichnis, drei Builds

Jedes Modul unter `shared/` bringt eine `CMakeLists.txt` mit, die unter
`ESP_PLATFORM` eine IDF-Komponente registriert und sonst eine statische
Bibliothek:

```cmake
if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_gfx STATIC ${SRCS})
    target_include_directories(rcbench_gfx PUBLIC include)
endif()
```

Das Panel setzt `EXTRA_COMPONENT_DIRS` auf `shared/`; Koprozessor und
Host-Suite holen die Module, die sie brauchen, per `add_subdirectory()`.
Includes sind flach: `#include "gfx.h"`.

| Modul | panel | iomcu | host |
| --- | :-: | :-: | :-: |
| `gfx` · `touch` · `ui` · `settings` · `logfile` · `sbus` | ✔ | | ✔ |
| `link` · `bench` · `outputs` · `servo` · `safety` · `can` | ✔ | ✔ | ✔ |
| `artwork` · `esc` | ✔ | | ✔ |
| `openyge` · `dshot` · `ppm` | | ✔ | ✔ |
| `sense` | ✔ | ✔ | ✔ |

## Toolchains

| | Version | Hinweise |
| --- | --- | --- |
| ESP-IDF | v5.4 oder neuer | das Event `on_frame_buf_complete` des RGB-Panels (RGB: paralleles Rot-Grün-Blau-Interface), auf das der Framebuffer-Wechsel wartet, existiert ab v5.4. CI (Continuous Integration) baut v5.4 und v5.5 |
| pico-sdk | 2.0 oder neuer | RP2350-Unterstützung; CI baut 2.3.0 |
| ARM GNU Toolchain | 14.2 | jede `arm-none-eabi`-Version für Cortex-M33 |

`firmware/panel/sdkconfig.defaults` setzt Octal-PSRAM (PSRAM: Pseudo-Static
Random-Access Memory) mit 80 MHz, den 64-KB-Datencache mit 64-Byte-Lines, Code
und Konstanten im Flash statt im PSRAM, den IRAM-sicheren RGB-LCD-Interrupt
(IRAM: Instruction Random-Access Memory; LCD: Liquid-Crystal Display), `-O2`
und die Konsole auf UART0 (UART: Universal Asynchronous Receiver-Transmitter)
mit USB-Serial-JTAG (die eingebaute serielle und Debug-Bridge des ESP32-S3
über USB, Universal Serial Bus) als Zweitkonsole. Von dieser Datei ausgehen,
nicht von `menuconfig`.

ESP-IDF liest `sdkconfig.defaults` nur, wenn es `sdkconfig` erzeugt. Ein Baum,
der schon einmal gebaut wurde, hat bereits `firmware/panel/sdkconfig`, und
diese Datei gewinnt: nach einer Änderung der Defaults löschen, sonst wirkt die
Änderung nicht auf das Image.

## Verdrahtung des Drehknopfs

Der Drehknopf ist ein magnetischer Winkelsensor AS5600 am I2C-Anschluss
(I2C: Inter-Integrated Circuit) des Panels, der 4-poligen PH2.0-Buchse des
Waveshare ESP32-S3-Touch-LCD-7. Der Anschluss ist GPIO8 (SDA) und GPIO9 (SCL),
der Bus von Touch-Controller und CH422G-Expander, über den Pegelumsetzer der
Platine. Pinbelegung aus dem Text des Platinenschaltplans, nicht gemessen:
1 I2C_VCC, 2 GND, 3 SDA, 4 SCL. Vor dem Anschließen mit einem Multimeter
prüfen.

| AS5600-Pin | Anschluss an |
| --- | --- |
| VDD3V3 und VDD5V | 3,3 V von I2C_VCC, beide Pins verbunden, was die 3,3-V-Versorgung des Datenblatts ist. Die Lötbrücke der Platine speist I2C_VCC ab Werk aus 3V3; ein Breakout mit eigenem Regler nimmt seinen Versorgungspin von I2C_VCC mit 3,3 V |
| GND | GND |
| SDA, SCL | SDA, SCL |
| DIR | GND oder VCC, nie offen. GND: Der Winkel steigt, wenn der Magnet von der Chipoberseite gesehen im Uhrzeigersinn dreht (Datenblatt; hier nicht geprüft). VCC kehrt es um |
| PGO | GND |
| OUT | nicht angeschlossen; das Panel liest den Winkel über I2C |

Der Magnet ist eine diametral magnetisierte Scheibe, mittig über dem Chip. Der
AS5600 meldet ein zu schwaches oder zu starkes Feld im STATUS-Register, und
das Panel behandelt beides als keine Antwort. Der Sensor liest unter 0x36, was
fest ist. Auf dem Waveshare ESP32-S3 Touch LCD 7 antwortet der
I/O-Expander CH422G an den I2C-Adressen 0x20 bis 0x27 und 0x30 bis 0x3F,
darunter 0x36; ein AS5600 lässt sich an diesem Bus daher nicht lesen. Der Knopf ist aus, bis SETUP, ANWENDUNG, `Drehknopf` eingeschaltet
wird. [Bildschirme](Screens-de.md#anwendung-der-drehknopf) beschreibt, was er
tut.

## Befehle

```bash
# host suite
cmake -S test/host -B test/host/build -DCMAKE_BUILD_TYPE=Debug
cmake --build test/host/build
ctest --test-dir test/host/build --output-on-failure

# panel
. $IDF_PATH/export.sh
idf.py -C firmware/panel set-target esp32s3
idf.py -C firmware/panel build
idf.py -C firmware/panel -p /dev/ttyACM0 flash monitor    # COMx on Windows

# panel, as one image at offset 0
idf.py -C firmware/panel merge-bin -o rcbench-panel-merged.bin
esptool.py -p /dev/ttyACM0 write_flash 0x0 \
    firmware/panel/build/rcbench-panel-merged.bin

# coprocessor
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S firmware/iomcu -B firmware/iomcu/build
cmake --build firmware/iomcu/build

# coprocessor, debug build that prints INA3221 CH1's 1 ms samples
cmake -S firmware/iomcu -B firmware/iomcu/build-trace -DSENSE_TRACE=ON
cmake --build firmware/iomcu/build-trace
```

Der Koprozessor-Build erzeugt `rcbench-iomcu.uf2`. Die Datei auf das
Massenspeicherlaufwerk des Moduls kopieren, während das Modul in BOOTSEL
gehalten wird.

`PICO_BOARD` steht standardmäßig auf `pimoroni_pico_plus2_rp2350`. Das für die
Inbetriebnahme verwendete Modul ist ein Waveshare RP2350-CAN (RP2350A, 4 MB
Flash), für das das SDK (Software Development Kit) keine Board-Datei hat; der
Standardwert baut und läuft darauf. Überschreiben mit `-DPICO_BOARD=`. Die
endgültige Platine braucht einen RP2350B: der geplante Pinbedarf liegt bei 27
bis 32 GPIO (General-Purpose Input/Output).

## Werkzeuge

| Werkzeug | Zweck |
| --- | --- |
| `tools/coverage.py` | misst die Line Coverage der Host-Suite, erzwingt die Untergrenzen (94 % gesamt, 85 % je Datei) und schreibt die Tabelle in `STATUS.md` und den Wert in `README.md` und `README-de.md`; `--check` schlägt bei Abweichung fehl, ebenso bei einer C-Datei unter `shared/`, die nicht in der Messung steht oder keine Zähler hat |
| `tools/mutate.py` | ändert in einer Kopie des Baums jeweils eine Zeile von `shared/` (ein Vergleich umgedreht, eine Grenze plus oder minus 1, eine gespeicherte Zuweisung entfernt), baut die Host-Suite, führt sie aus und meldet die Änderungen, mit denen die Suite besteht; standardmäßig die seit `origin/main` geänderten Zeilen, höchstens 60 Mutanten und 2700 s; CI führt es bei Pull Requests aus und schlägt bei einem Überlebenden nicht fehl |
| `tools/check_sanitizers.py` | konfiguriert den Sanitizer-Build und schlägt fehl, wenn nicht jeder Compile-Befehl unter `shared/` und `test/host/` `-fsanitize=address,undefined`, `-fno-sanitize-recover=all` und `-fno-omit-frame-pointer` trägt und kein anderes Flag dieser Familie |
| `tools/check_docs.py` | hält die Seiten am Quellbaum: Links und Anker führen irgendwohin, jedes Bild wird benutzt, die Sidebar ist vollständig, jede Seite hat ein deutsches Gegenstück, die Suite-Liste in `STATUS.md` stimmt mit CMake überein, die Zahlen der Screenshots in `STATUS.md` stimmen mit `docs/img` überein, die Tabelle der roten Leuchte auf den Seiten zur Stick-Programmierung stimmt mit `esc_stick_reason_is_fault()` überein, der Baum oben nennt jedes Modul unter `shared/`, jede Quelldatei trägt eine SPDX-Zeile (SPDX: Software Package Data Exchange), die Protokollversion, die Zeiten von Heartbeat und Link, die Pins von Heartbeat und CAN, die Coverage-Untergrenzen und die Stack-Marge, die eine Seite nennt, sind die Konstanten in den Headern und Werkzeugen (`FACTS` im Werkzeug führt jeden Satz), eine Tabellenzeile, die eine C-Konstante nennt, gibt deren Wert an, die Tabelle der Obergrenzen in [Performance](Performance-de.md) stimmt mit den `--max-lines`-Argumenten in `ci.yml` überein, die Pinzahlen in `hardware/docs/Pins.md` stimmen mit `pinmap.json` überein, eine deutsche Seite zitiert in Backticks das Deutsch, das der Bildschirm zeigt: Texte und Formate der Oberfläche, Namen, Hilfetexte, Optionen und Kategorien der Einstellungen und die Wörter des Servotests |
| `tools/wiki_links.py` | schreibt `Page.md`-Links zu `Page` um, für das Wiki, das Seiten über ihren Titel adressiert |
| `tools/check_formats.py` | kompiliert `shared/` mit jedem Aufruf von `TR()` und jedem Wort des Berichts durch sein englisches Literal ersetzt, unter `-Wformat=2 -Wformat-nonliteral -Wformat-signedness`, und schlägt bei jeder Warnung fehl: jedes englische Format gegen die Argumente seines Aufrufs ([Sprache](Language-de.md)) |
| `tools/gen_font.py` | erzeugt die drei eingebetteten Fonts aus DejaVu Sans Mono neu, die beiden Text-Fonts mit den deutschen Buchstaben; `--check` schlägt fehl, wenn die eingecheckten Tabellen abweichen |
| `tools/render_ui.py` | rendert jeden Bildschirm mit dem Code, den das Panel ausführt, als PNG (Portable Network Graphics), auf Englisch nach `docs/img/` und auf Deutsch nach `docs/img/de/`; `--check` vergleicht mit den eingecheckten Bildern; `--fit` schlägt fehl, wenn ein Text in einer der beiden Sprachen dort überläuft, wo er gezeichnet wird, außer den englischen Überläufen, die das Werkzeug als bekannt führt ([Sprache](Language-de.md)) |
| `tools/frame_cost.py` | misst Cache-Line-Fills je Frame unter cachegrind; `--check-doc` hält die Tabelle in [Performance](Performance-de.md) |
| `tools/stack_check.py` | liest die tiefste Aufrufkette jeder Panel-Task aus der gebauten ELF-Datei (Executable and Linkable Format) und schlägt fehl, wenn eine ihren Stack abzüglich 1024 Bytes überschreitet; nimmt das Build-Verzeichnis, Standard `firmware/panel/build`; `-v` gibt jede tiefste Kette und jeden Aufruf aus, dem es nicht folgen kann; `--check-doc` hält die Task-Tabelle in [Performance](Performance-de.md#stacks) am Build; `--iomcu` liest stattdessen das Koprozessor-Image und schlägt fehl, wenn die tiefste Kette eines Kerns, ein Interrupt und 256 Bytes seinen Stack überschreiten; mit `--check-doc` hält es die Zeile von Kern 0 der Koprozessor-Tabelle |
| `tools/sense_trace.py` | liest die mitgeschnittene Konsole eines Koprozessors, der mit `-DSENSE_TRACE=ON` gebaut ist: prüft jeden Trace der 1-ms-Samples von INA3221 CH1 gegen seine Endzeile, schreibt je Trace eine CSV-Datei (Comma-Separated Values) und spielt jede Bewegung durch `shared/servo/servo_move.c`, mit dem Filter bei 1, 4 und 8 Samples und dem Band bei 0,02, 0,05 und 0,10 A; `--servo-csv` ergänzt die Ankunft abzüglich der Stellzeit des AS5600 ([Erster Lauf](FirstRun-de.md#89-die-1-ms-samples-von-ch1-auf-der-konsole)) |
| `tools/pinmap_check.py` | prüft die vorläufige Pinbelegung des IO-Boards, `hardware/docs/pinmap.json`, gegen die Pinfunktionen in `io_bank0.h` des pico-sdk; braucht einen Checkout des pico-sdk |
| `tools/gen_board_art.py`, `tools/gen_esc_profiles.py` | erzeugen die eingecheckten Tabellen der Board-Grafik und der ESC-Profile aus ihren PNG- und JSON-Quellen; `--check` schlägt fehl, wenn die eingecheckten Tabellen abweichen |
| `tools/ci_gate.py` | wartet auf den CI-Lauf eines Commits und schlägt fehl, wenn keiner bestanden hat; der erste Schritt von `docs.yml` und `release.yml` |
| `test/tools/` | pytest-Fälle für die Werkzeuge oben: `python3 -m pytest test/tools` |
| `.clang-tidy`, `.cppcheck-suppress`, `ruff.toml` | Konfiguration für statische Analyse und Lint; jeder Befund ist ein Fehler |

`gen_font.py` sucht den Font in `RCBENCH_FONT_DIR`, dann in
`~/.local/share/fonts`, dann in den Systemfontverzeichnissen. `frame_cost.py`
braucht `valgrind`; die übrigen Werkzeuge brauchen einen C-Compiler und
Pillow. `mutate.py` braucht außerdem git und CMake, `ci_gate.py` das
Kommando `gh`. `sense_trace.py` braucht CMake und einen C-Compiler: es baut
`test/host/sense_trace_replay.c` nach `build-trace/`, wenn `--replay` kein
gebautes Programm nennt.

## CI

| Workflow | Auslöser | Jobs |
| --- | --- | --- |
| `ci.yml` | Push, Pull Request, Tag `v*`, manuell | Host-Suite; dieselbe Suite unter AddressSanitizer und UBSan (UndefinedBehaviorSanitizer); Coverage-Untergrenzen und Codecov-Upload, dessen Fehlschlag den Job fehlschlagen lässt; Font-, Docs-, Wiki-Link-, Frame-Cost-, Screenshot- und Research-Skript-Prüfungen; clang-tidy und cppcheck über `shared/`, die Klassen warning, performance und portability von cppcheck über `firmware/`, und ruff; die pytest-Fälle der Werkzeuge; bei einem Pull Request die Mutationsprüfung der geänderten Zeilen, die meldet und bei einem Überlebenden nicht fehlschlägt; Panel-Build mit ESP-IDF v5.4 und v5.5, jeweils mit der Prüfung der Task-Stacks, v5.4 mit der Stack-Tabelle dieses Wikis; Koprozessor-Build mit pico-sdk 2.3.0 mit der Prüfung der Pinbelegung und der Stacks seiner zwei Kerne, und ein zweiter mit `-DSENSE_TRACE=ON` und derselben Stack-Prüfung, der fehlschlägt, wenn das Standard-Image ein Trace-Symbol enthält; Firmware-Artefakte einschließlich eines zusammengeführten Panel-Images für Offset 0 |
| `docs.yml` | Push auf `main`, der `docs/` berührt, manuell | wartet auf den CI-Lauf desselben Commits und spiegelt, wenn er bestanden hat, `docs/` ins GitHub-Wiki |
| `release.yml` | Tag `v*`, manuell für einen Tag | wartet auf den CI-Lauf des getaggten Commits und baut, wenn er bestanden hat, beide Images, packt sie mit Prüfsummen, erstellt ein Release und übernimmt die PDFs der Bauanleitung vom letzten Release |

clang-tidy läuft nicht über `firmware/`: es kompiliert jede Datei und braucht
die Header von ESP-IDF und pico-sdk, die der Analyse-Job nicht hat. Die
Klasse style von cppcheck läuft ebenfalls nicht über `firmware/`.

Jede Prüfung läuft lokal;
[CONTRIBUTING.md](https://github.com/subtilitas/rcbench/blob/main/CONTRIBUTING.md)
listet die Befehle.

Das Wiki muss vor dem ersten Lauf von `docs.yml` mindestens eine Seite
enthalten; sonst existiert das Wiki-Repository nicht und der Clone schlägt
fehl.
