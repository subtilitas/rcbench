# Building

<sub>**English** · [Deutsch](Building-de.md)</sub>

Three builds read one source tree: the host test suite, the panel firmware
(ESP-IDF (Espressif Internet-of-Things Development Framework)) and the
coprocessor firmware (pico-sdk).

## The tree

```
rcbench/
  docs/                   wiki source, English and German
  tools/                  render_ui · coverage · check_docs · frame_cost
                          gen_font · wiki_links
  shared/                 pure C: no ESP-IDF, no pico-sdk, no FreeRTOS types
    gfx/                  rasteriser and three fonts
    touch/                coordinate and event mapping
    ui/                   theme · widgets · icons · router · screens
    settings/             typed schema and values
    logfile/              number and CSV parsing
    link/                 page protocol · CAN framing · watchdogs · diagnosis
    bench/                bench_state · telemetry simulator · log writer
    outputs/              channels · driver table · arming, slew and staleness
    safety/               heartbeat generator (panel) and monitor (coprocessor)
    servo/                limit and synchronisation searches · servo model
    can/                  bit timing for both controllers · MCP2515 registers · echo self-test
    sbus/                 S.BUS decoder
    openyge/              OpenYGE framing, status and parameter cache
  firmware/
    panel/                ESP-IDF project (ESP32-S3)
    iomcu/                pico-sdk project (RP2350)
  test/host/              the host suite, one binary per module or screen
  hardware/               board design record; no board exists
```

Modules under `shared/` contain the logic and have no hardware dependency.
Everything that touches hardware is under `firmware/`.

### One directory, three builds

Each module under `shared/` carries a `CMakeLists.txt` that registers an IDF
component under `ESP_PLATFORM` and a plain static library otherwise:

```cmake
if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_gfx STATIC ${SRCS})
    target_include_directories(rcbench_gfx PUBLIC include)
endif()
```

The panel sets `EXTRA_COMPONENT_DIRS` to `shared/`; the coprocessor and the
host suite use `add_subdirectory()` for the modules they need. Includes are
flat: `#include "gfx.h"`.

| Module | panel | iomcu | host |
| --- | :-: | :-: | :-: |
| `gfx` · `touch` · `ui` · `settings` · `logfile` · `sbus` | ✔ | | ✔ |
| `link` · `bench` · `outputs` · `safety` · `can` | ✔ | ✔ | ✔ |
| `servo` · `openyge` | | ✔ | ✔ |

## Toolchains

| | Version | Notes |
| --- | --- | --- |
| ESP-IDF | v5.4 or newer | the RGB (parallel red-green-blue) panel's `on_frame_buf_complete` event, which the framebuffer swap waits on, exists from v5.4. CI (continuous integration) builds v5.4 and v5.5 |
| pico-sdk | 2.0 or newer | RP2350 support; CI builds 2.3.0 |
| ARM GNU toolchain | 14.2 | any `arm-none-eabi` release targeting Cortex-M33 |

`firmware/panel/sdkconfig.defaults` sets octal PSRAM (pseudo-static
random-access memory) at 80 MHz, the 64 KB data cache with 64-byte lines, code
and constants in flash rather than PSRAM, the IRAM-safe RGB LCD
(liquid-crystal display) interrupt, `-O2`, and the console on UART0 with USB-Serial-JTAG (the ESP32-S3's built-in USB (Universal Serial Bus)
serial and debug bridge) as secondary. Start from it rather than from
`menuconfig`.

ESP-IDF reads `sdkconfig.defaults` only when it generates `sdkconfig`. A tree
that has been built before already has `firmware/panel/sdkconfig`, and that
file wins: delete it after changing the defaults, or the change has no effect
on the image.

## Commands

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

The coprocessor build produces `rcbench-iomcu.uf2`. Copy it to the module's
mass-storage drive while the module is held in BOOTSEL.

`PICO_BOARD` defaults to `pimoroni_pico_plus2_rp2350`. The module used for
bring-up is a Waveshare RP2350-CAN (RP2350A, 4 MB flash), which has no board
file in the SDK (software development kit); the default builds and runs on it.
Override with `-DPICO_BOARD=`. The final board needs an RP2350B: the planned
pin budget is 27 to 32 GPIO (general-purpose input/output).

## Tools

| Tool | Purpose |
| --- | --- |
| `tools/coverage.py` | measures host-suite line coverage, enforces the floors (94% total, 85% per file) and writes the table in `STATUS.md`; `--check` fails on drift |
| `tools/check_docs.py` | holds the pages to the tree: links and anchors resolve, every image is used, the sidebar is complete, every page has a German counterpart, the suite list in `STATUS.md` matches CMake, the tree above lists every `shared/` module, every source file carries an SPDX (Software Package Data Exchange) line |
| `tools/wiki_links.py` | rewrites `Page.md` links to `Page` for the wiki, where pages are addressed by title |
| `tools/gen_font.py` | regenerates the three embedded fonts from DejaVu Sans Mono; `--check` fails if the committed tables differ |
| `tools/render_ui.py` | renders every screen to PNG (Portable Network Graphics) with the code the panel runs; `--check` compares with the committed images in `docs/img/` |
| `tools/frame_cost.py` | measures cache-line fills per frame under cachegrind; `--check-doc` holds the table in [Performance](Performance.md) |
| `.clang-tidy`, `.cppcheck-suppress`, `ruff.toml` | static analysis and lint configuration; every finding is an error |

`gen_font.py` looks for the font in `RCBENCH_FONT_DIR`, then
`~/.local/share/fonts`, then the system font directories. `frame_cost.py` needs
`valgrind`; the other tools need a C compiler and Pillow.

## CI

| Workflow | Trigger | Jobs |
| --- | --- | --- |
| `ci.yml` | push, pull request, tag `v*`, manual | host suite; the same suite under AddressSanitizer and UBSan (UndefinedBehaviorSanitizer); coverage floors and Codecov upload; font, docs, wiki-link, frame-cost and screenshot checks; clang-tidy, cppcheck and ruff; panel build on ESP-IDF v5.4 and v5.5; coprocessor build on pico-sdk 2.3.0; firmware artifacts including a merged panel image for offset 0 |
| `docs.yml` | push to `main` touching `docs/` | mirrors `docs/` to the GitHub wiki |
| `release.yml` | tag `v*` | builds both images, packages them with checksums, creates a release |

Every check runs locally;
[CONTRIBUTING.md](https://github.com/subtilitas/rcbench/blob/main/CONTRIBUTING.md)
lists the commands.

The wiki must contain at least one page before the first `docs.yml` run;
otherwise the wiki repository does not exist and the clone fails.
