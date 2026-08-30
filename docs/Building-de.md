# Bauen

<sub>[English](Building.md) · **Deutsch**</sub>

Wie man beide Platinen baut und flasht, und wofür jedes Werkzeug in `tools/`
da ist. Drei Build-Systeme lesen einen Quellbaum: die Host-Suite für die
Entwicklung, und je ein Firmware-Build pro Prozessor.

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
    safety/               the heartbeat: the panel generates, the copro judges
    servo/                the limit and sync searches, and servos to run them against
    can/                  bit timing for both controllers, and the MCP2515 registers
    sbus/                 sixteen channels in twenty-five bytes, framed on the gap
    openyge/              the ESC protocol: framing, status, parameters
  firmware/
    panel/                ESP-IDF project
    copro/                pico-sdk project
  test/host/              one suite over shared/ and its fakes
```

Alles, was etwas entscheidet, ist reines C ohne Hersteller-SDK; alles, was
Hardware anfasst, ist es nicht.

### Wie ein Verzeichnis drei Builds bedient

Jedes Modul unter `shared/` trägt eine zehnzeilige `CMakeLists.txt`, die dem
antwortet, der gerade fragt:

```cmake
if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_gfx STATIC ${SRCS})
    target_include_directories(rcbench_gfx PUBLIC include)
endif()
```

Das Bedienteil setzt `EXTRA_COMPONENT_DIRS` und bekommt jedes Modul als
vollwertige IDF-Komponente; der Koprozessor und die Host-Suite holen sich per
`add_subdirectory()` die, die sie wollen. Keine Wrapper-Komponenten, und keine
Quellenliste, die zweimal dasteht.

Es setzt allerdings ein `if(ESP_PLATFORM)` in ein Verzeichnis, das kein
Hersteller-SDK behauptet. Diese Behauptung gilt dem C, und die Alternative wären
einundzwanzig Dateien Indirektion, um einer Bedingung aus dem Weg zu gehen.

Includes sind flach — `#include "gfx.h"`, nicht `"rcbench/gfx.h"`.

## Die drei Toolchains

| | Version | Warum diese |
| --- | --- | --- |
| ESP-IDF | **v5.4 oder neuer** | das Event `on_frame_buf_complete` des RGB-Panels, auf das der Framebuffer-Swap wartet, kam in v5.4 |
| pico-sdk | **2.0 oder neuer** | RP2350-Support kam in 2.0; gebaut wird gegen 2.3.0 |
| ARM GNU | 14.2 | irgendein aktuelles `arm-none-eabi` für Cortex-M33 |

`firmware/panel/sdkconfig.defaults` setzt die Optionen für Octal-PSRAM,
64-Byte-Cache-Lines und die IRAM-sichere LCD-ISR, die das Bedienteil braucht,
und legt die Konsole auf die eingebaute USB-Serial-JTAG-Bridge — weshalb GPIO43
und 44 frei sind. Fang damit an und nicht mit einem nackten `menuconfig`.

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
cmake -S firmware/copro -B firmware/copro/build
cmake --build firmware/copro/build
```

Das `PICO_BOARD` des Koprozessors steht standardmäßig auf
`pimoroni_pico_plus2_rp2350` und ist eine Vermutung darüber, welches
RP2350B-Modul ankommt; mit `-DPICO_BOARD=` überschreiben und den Standard
korrigieren, sobald das feststeht. Es muss ein **RP2350B** sein — der Pinbedarf
liegt bei etwa 27 bis 32 GPIO, und ein Pico 2 führt 26 heraus.

## Werkzeuge

| | |
| --- | --- |
| `tools/coverage.py` | misst die Line Coverage der Host-Tests und hält die Tabelle im README ehrlich; `--check` schlägt bei Abweichung fehl |
| `tools/check_docs.py` | hält diese Seiten an den Baum: Links gehen irgendwohin, die Sidebar ist vollständig, die Suite-Liste ist die, die CMake baut, der Baum oben nennt jedes Modul unter `shared/` |
| `tools/wiki_links.py` | streicht das `.md` aus Seitenlinks auf dem Weg ins Wiki, wo eine Seite über ihren Titel adressiert wird und ein Link auf eine Datei sie herunterlädt. Das Repository will die Endung, das Wiki nicht — die Quelle behält sie, der Mirror-Schritt übersetzt |
| `tools/gen_font.py` | erzeugt die drei eingebetteten Fonts neu; `--check` schlägt fehl, wenn das eingecheckte C nicht mehr zu seinem Generator passt |
| `tools/render_ui.py` | rendert jeden Bildschirm in ein PNG; `--check` vergleicht gegen die eingecheckten Goldens |
| `tools/frame_cost.py` | misst den Cache-Line-Traffic je Frame unter cachegrind, weil die Frame Rate auf diesem Panel bandbreitenbegrenzt ist |

`gen_font.py` braucht eine TTF von DejaVu Sans Mono. Es schaut zuerst in
`RCBENCH_FONT_DIR`, dann in `~/.local/share/fonts`, dann in die Systempfade — es
läuft also auch auf einer Maschine, auf der man nicht nach `/usr/share/fonts`
schreiben darf, was eine fest verdrahtete Liste unmöglich gemacht hat.

`frame_cost.py` braucht `valgrind`; alles andere braucht nur einen C-Compiler
und Pillow.

## Was CI prüft

| Workflow | Auslöser | Was er tut |
| --- | --- | --- |
| `ci.yml` | Push / PR / Tag `v*` / manuell | Host-Suite, Coverage `--check`, der Font-Check, der Docs-Check, die ESP-IDF-Matrix (v5.4, v5.5) baut das Bedienteil, der pico-sdk-Build des Koprozessors, Firmware-Artefakte |
| `docs.yml` | Push auf `main`, der `docs/` berührt | veröffentlicht `docs/` ins GitHub-Wiki |
| `release.yml` | Tag `v*` | baut beide Images, packt sie, öffnet ein Release |

Die Coverage landet im README, und CI läuft mit `--check`, was bei Abweichung
fehlschlägt, statt einen Fixup zu committen — eine Datei, die sich selbst
umschreibt, ist eine, deren Diff niemand liest.

**Das Wiki muss vor dem ersten Lauf von `docs.yml` existieren.** Lege eine Seite
von Hand im Wiki-Tab des Repositories an, sonst läuft der Clone in einen 404.
