# Bauen

<sub>[English](Building.md) · **Deutsch**</sub>

Drei Builds lesen einen Quellbaum: die Host-Testsuite, die Panel-Firmware
(ESP-IDF, Espressif Internet-of-Things Development Framework) und die
Koprozessor-Firmware (pico-sdk).

## Der Baum

```
rcbench/
  docs/                   Wiki-Quelle, Englisch und Deutsch
  tools/                  render_ui · coverage · check_docs · frame_cost
                          gen_font · wiki_links
  shared/                 reines C: kein ESP-IDF, kein pico-sdk, keine FreeRTOS-Typen
    gfx/                  Rasterizer und drei Fonts
    touch/                Koordinaten- und Event-Mapping
    ui/                   Theme · Widgets · Icons · Router · Bildschirme
    settings/             typisiertes Schema und Werte
    logfile/              Zahlen- und CSV-Parsing
    link/                 Page-Protokoll · CAN-Framing · Watchdogs · Diagnose
    bench/                bench_state · Telemetriesimulator · Log-Writer
    outputs/              Kanäle · Treibertabelle · Arming, Slew und Staleness
    safety/               Heartbeat-Generator (Panel) und -Monitor (Koprozessor)
    servo/                Endlagen- und Abgleichsuche · Servomodell
    can/                  Bit Timing für beide Controller · MCP2515-Register · Echo-Selbsttest
    sbus/                 S.BUS-Decoder
    openyge/              OpenYGE-Framing, Status und Parameter-Cache
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
| `link` · `bench` · `outputs` · `safety` · `can` | ✔ | ✔ | ✔ |
| `servo` · `openyge` | | ✔ | ✔ |

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
| `tools/coverage.py` | misst die Line Coverage der Host-Suite, erzwingt die Untergrenzen (94 % gesamt, 85 % je Datei) und schreibt die Tabelle in `STATUS.md`; `--check` schlägt bei Abweichung fehl |
| `tools/check_docs.py` | hält die Seiten am Quellbaum: Links und Anker führen irgendwohin, jedes Bild wird benutzt, die Sidebar ist vollständig, jede Seite hat ein deutsches Gegenstück, die Suite-Liste in `STATUS.md` stimmt mit CMake überein, der Baum oben nennt jedes Modul unter `shared/`, jede Quelldatei trägt eine SPDX-Zeile (SPDX: Software Package Data Exchange) |
| `tools/wiki_links.py` | schreibt `Page.md`-Links zu `Page` um, für das Wiki, das Seiten über ihren Titel adressiert |
| `tools/gen_font.py` | erzeugt die drei eingebetteten Fonts aus DejaVu Sans Mono neu; `--check` schlägt fehl, wenn die eingecheckten Tabellen abweichen |
| `tools/render_ui.py` | rendert jeden Bildschirm mit dem Code, den das Panel ausführt, als PNG (Portable Network Graphics); `--check` vergleicht mit den eingecheckten Bildern in `docs/img/` |
| `tools/frame_cost.py` | misst Cache-Line-Fills je Frame unter cachegrind; `--check-doc` hält die Tabelle in [Performance](Performance-de.md) |
| `.clang-tidy`, `.cppcheck-suppress`, `ruff.toml` | Konfiguration für statische Analyse und Lint; jeder Befund ist ein Fehler |

`gen_font.py` sucht den Font in `RCBENCH_FONT_DIR`, dann in
`~/.local/share/fonts`, dann in den Systemfontverzeichnissen. `frame_cost.py`
braucht `valgrind`; die übrigen Werkzeuge brauchen einen C-Compiler und
Pillow.

## CI

| Workflow | Auslöser | Jobs |
| --- | --- | --- |
| `ci.yml` | Push, Pull Request, Tag `v*`, manuell | Host-Suite; dieselbe Suite unter AddressSanitizer und UBSan (UndefinedBehaviorSanitizer); Coverage-Untergrenzen und Codecov-Upload; Font-, Docs-, Wiki-Link-, Frame-Cost- und Screenshot-Prüfungen; clang-tidy, cppcheck und ruff; Panel-Build mit ESP-IDF v5.4 und v5.5; Koprozessor-Build mit pico-sdk 2.3.0; Firmware-Artefakte einschließlich eines zusammengeführten Panel-Images für Offset 0 |
| `docs.yml` | Push auf `main`, der `docs/` berührt | spiegelt `docs/` ins GitHub-Wiki |
| `release.yml` | Tag `v*` | baut beide Images, packt sie mit Prüfsummen, erstellt ein Release |

Jede Prüfung läuft lokal;
[CONTRIBUTING.md](https://github.com/subtilitas/rcbench/blob/main/CONTRIBUTING.md)
listet die Befehle.

Das Wiki muss vor dem ersten Lauf von `docs.yml` mindestens eine Seite
enthalten; sonst existiert das Wiki-Repository nicht und der Clone schlägt
fehl.
