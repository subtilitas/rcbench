# Bauen

<sub>[English](Building.md) · **Deutsch**</sub>

Wie man beide Platinen baut und flasht, und wofür jedes Werkzeug in `tools/`
da ist. Drei Build-Systeme lesen denselben Quellbaum: die Host-Suite für die
Entwicklung, dazu je ein Firmware-Build pro Prozessor.

## Der Baum

```
rcbench/
  docs/                   these pages
  tools/                  render_ui · coverage · check_docs · frame_cost
                          gen_font · wiki_links
  shared/                 pure C — no ESP-IDF, no pico-sdk, no FreeRTOS types
    gfx/                  rasteriser and three fonts
    touch/                coordinate and event mapping
    ui/                   theme · widgets · icons
    settings/             typed schema and values
    logfile/              number and CSV parsing
    link/                 framing · CRC-16 · the wire budget
    bench/                bench_state · the simulator
    outputs/              channels · driver table · arming, slew and staleness
    safety/               the heartbeat: the panel generates, the iomcu judges
    servo/                the limit and sync searches, and servos to run them against
    can/                  bit timing for both controllers, and the MCP2515 registers
    sbus/                 sixteen channels in twenty-five bytes, framed on the gap
    openyge/              the ESC protocol: framing, status, parameters
  firmware/
    panel/                ESP-IDF project
    iomcu/                pico-sdk project — the coprocessor
  test/host/              one suite over shared/ and its fakes
```

Alles, was etwas entscheidet, ist reines C ohne Hersteller-SDK; alles, was
Hardware anfasst, ist es nicht.

### Wie ein Verzeichnis drei Builds bedient

Jedes Modul unter `shared/` bringt eine zehnzeilige `CMakeLists.txt` mit, die
sich nach dem richtet, wer gerade baut:

```cmake
if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_gfx STATIC ${SRCS})
    target_include_directories(rcbench_gfx PUBLIC include)
endif()
```

Das Panel setzt `EXTRA_COMPONENT_DIRS` und bekommt jedes Modul als
vollwertige IDF-Komponente; Koprozessor und Host-Suite holen sich per
`add_subdirectory()` die Module, die sie brauchen. Keine Wrapper-Komponenten,
und keine Quellliste steht doppelt da.

Damit steht allerdings ein `if(ESP_PLATFORM)` in einem Verzeichnis, das von
sich behauptet, ohne Hersteller-SDK auszukommen. Die Behauptung meint den
C-Code — und die Alternative wären einundzwanzig Dateien Umleitung, nur um
einer einzigen Bedingung auszuweichen.

Includes sind flach — `#include "gfx.h"`, nicht `"rcbench/gfx.h"`.

## Die drei Toolchains

| | Version | Warum diese |
| --- | --- | --- |
| ESP-IDF | **v5.4 oder neuer** | das Event `on_frame_buf_complete` des RGB-Panels, auf das der Framebuffer-Swap wartet, kam in v5.4 |
| pico-sdk | **2.0 oder neuer** | RP2350-Support kam in 2.0; gebaut wird gegen 2.3.0 |
| ARM GNU | 14.2 | irgendein aktuelles `arm-none-eabi` für Cortex-M33 |

`firmware/panel/sdkconfig.defaults` setzt die Optionen, die das Panel braucht
— Octal-PSRAM, 64-Byte-Cache-Lines, die IRAM-sichere LCD-ISR — und legt die
Konsole auf die eingebaute USB-Serial-JTAG-Bridge, weshalb GPIO43 und 44 frei
bleiben. Von dieser Datei ausgehen, nicht von einem leeren `menuconfig`.

## Befehle

```bash
# the parts that are pure C, on any laptop
cmake -S test/host -B test/host/build -DCMAKE_BUILD_TYPE=Debug
cmake --build test/host/build
ctest --test-dir test/host/build --output-on-failure

# the panel
. $IDF_PATH/export.sh
idf.py -C firmware/panel set-target esp32s3
idf.py -C firmware/panel build
idf.py -C firmware/panel -p /dev/ttyACM0 flash monitor    # COMx on Windows

# or one image, to one offset
idf.py -C firmware/panel merge-bin -o rcbench-panel-merged.bin
esptool.py -p /dev/ttyACM0 write_flash 0x0 \
    firmware/panel/build/rcbench-panel-merged.bin

# the coprocessor
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S firmware/iomcu -B firmware/iomcu/build
cmake --build firmware/iomcu/build
```

Das `PICO_BOARD` des Koprozessors steht standardmäßig auf
`pimoroni_pico_plus2_rp2350` — eine Annahme darüber, welches RP2350B-Modul
geliefert wird. Mit `-DPICO_BOARD=` lässt es sich überschreiben; sobald das
Modul feststeht, gehört der Standardwert korrigiert. Ein **RP2350B** muss es
sein: der Pinbedarf liegt bei 27 bis 32 GPIO, ein Pico 2 führt nur 26 heraus.

## Werkzeuge

| | |
| --- | --- |
| `tools/coverage.py` | misst die Line Coverage der Host-Tests und hält die Tabelle im README aktuell; `--check` schlägt bei Abweichung fehl |
| `tools/check_docs.py` | prüft diese Seiten gegen den Quellbaum: jeder Link führt irgendwohin, die Sidebar ist vollständig, die Suite-Liste ist die, die CMake baut, der Baum oben nennt jedes Modul unter `shared/`, und jede Quelldatei trägt eine SPDX-Lizenzzeile |
| `tools/wiki_links.py` | entfernt das `.md` aus Seitenlinks auf dem Weg ins Wiki, denn dort wird eine Seite über ihren Titel angesprochen und ein Link auf die Datei lädt sie nur herunter. Im Repository braucht es die Endung, im Wiki nicht — die Quelle behält sie, der Kopierschritt übersetzt |
| `tools/gen_font.py` | erzeugt die drei eingebetteten Fonts neu; `--check` schlägt fehl, wenn der eingecheckte C-Code nicht mehr zu seinem Generator passt |
| `tools/render_ui.py` | rendert jeden Bildschirm in ein PNG; `--check` vergleicht gegen die eingecheckten Goldens |
| `tools/frame_cost.py` | misst den Cache-Line-Traffic je Frame unter cachegrind, weil die Frame Rate auf diesem Panel bandbreitenbegrenzt ist |

`gen_font.py` braucht DejaVu Sans Mono als TTF. Gesucht wird zuerst in
`RCBENCH_FONT_DIR`, dann in `~/.local/share/fonts`, dann in den Systempfaden —
so läuft es auch auf Rechnern ohne Schreibrecht auf `/usr/share/fonts`, woran
die frühere fest verdrahtete Pfadliste gescheitert war.

`frame_cost.py` braucht `valgrind`; alles andere braucht nur einen C-Compiler
und Pillow.

## Was CI prüft

| Workflow | Auslöser | Was er tut |
| --- | --- | --- |
| `ci.yml` | Push / PR / Tag `v*` / manuell | Host-Suite, Coverage `--check`, der Font-Check, der Docs-Check, die ESP-IDF-Matrix (v5.4, v5.5) baut das Panel, der pico-sdk-Build des Koprozessors, Firmware-Artefakte |
| `docs.yml` | Push auf `main`, der `docs/` berührt | veröffentlicht `docs/` ins GitHub-Wiki |
| `release.yml` | Tag `v*` | baut beide Images, packt sie, öffnet ein Release |

Die Coverage steht im README, und CI prüft sie mit `--check`: bei Abweichung
schlägt der Lauf fehl, statt dass ein Bot die Datei nachzieht — denn eine
Datei, die sich selbst umschreibt, ist eine, deren Diff niemand mehr liest.

**Das Wiki muss vor dem ersten Lauf von `docs.yml` existieren.** Einmal von
Hand eine Seite im Wiki-Tab des Repositories anlegen, sonst endet der Clone in
einem 404.
