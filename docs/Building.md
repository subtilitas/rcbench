# Building

Three build systems read one source tree. That is the whole trick, and it is
what makes the host suite, the coverage numbers and the golden images possible
across two processors.

## The tree

```
rcbench/
  docs/                   these pages
  tools/                  render_ui · coverage · check_docs · frame_cost · gen_font
  shared/                 pure C — no ESP-IDF, no pico-sdk, no FreeRTOS types
    gfx/                  rasteriser and three fonts
    touch/                coordinate and event mapping
    ui/                   theme · widgets · icons
    settings/             typed schema and values
    logfile/              number and CSV parsing
    link/                 framing · CRC-16 · the wire budget
  firmware/
    panel/                ESP-IDF project
    copro/                pico-sdk project
  test/host/              one suite over shared/ and its fakes
```

Everything that decides something is pure C with no vendor SDK; everything that
touches hardware is not.

### How one directory serves three builds

Each module under `shared/` carries a ten-line `CMakeLists.txt` that answers to
whichever build system is asking:

```cmake
if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_gfx STATIC ${SRCS})
    target_include_directories(rcbench_gfx PUBLIC include)
endif()
```

The panel sets `EXTRA_COMPONENT_DIRS` and gets every module as a first-class
IDF component; the coprocessor and the host suite `add_subdirectory()` the ones
they want. No wrapper components, and no source list written down twice.

It does put one `if(ESP_PLATFORM)` in a directory that claims no vendor SDK.
That claim is about the C, and the alternative is twenty-one files of
indirection to avoid one conditional.

Includes are flat — `#include "gfx.h"`, not `"rcbench/gfx.h"`.

## The three toolchains

| | Version | Why that one |
| --- | --- | --- |
| ESP-IDF | **v5.4 or newer** | the RGB panel's `on_frame_buf_complete` event, which the framebuffer swap waits on, landed in v5.4 |
| pico-sdk | **2.0 or newer** | RP2350 support arrived in 2.0; 2.3.0 is what this is built against |
| ARM GNU | 14.2 | any recent `arm-none-eabi` targeting Cortex-M33 |

`firmware/panel/sdkconfig.defaults` sets the octal-PSRAM, 64-byte-cache-line
and IRAM-safe-LCD-ISR options the panel needs, and puts the console on the
built-in USB-Serial-JTAG bridge — which is why GPIO43 and 44 are free. Start
from it rather than a bare `menuconfig`.

## Commands

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

# the coprocessor
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S firmware/copro -B firmware/copro/build
cmake --build firmware/copro/build
```

The coprocessor's `PICO_BOARD` defaults to `pimoroni_pico_plus2_rp2350` and is
a guess at which RP2350B module arrives; override it with `-DPICO_BOARD=` and
correct the default once that is settled. It must be an **RP2350B** — the pin
budget lands around 27 to 32 GPIO and a Pico 2 brings out 26.

## Tools

| | |
| --- | --- |
| `tools/coverage.py` | measures host-test line coverage and keeps the README table honest; `--check` fails on drift |
| `tools/check_docs.py` | holds these pages to the tree: links resolve, the sidebar is complete, the suite list is the list CMake builds, the tree above lists every `shared/` module |
| `tools/gen_font.py` | regenerates the three embedded fonts; `--check` fails if the committed C no longer matches its generator |
| `tools/render_ui.py` | renders every screen to a PNG; `--check` compares against the committed goldens |
| `tools/frame_cost.py` | measures cache-line traffic per frame under cachegrind, because frame rate on this panel is bandwidth-bound |

`gen_font.py` needs a DejaVu Sans Mono TTF. It looks at `RCBENCH_FONT_DIR`
first, then `~/.local/share/fonts`, then the system paths — so it runs on a
machine where you cannot write `/usr/share/fonts`, which a hardcoded list made
impossible.

`render_ui.py` and `frame_cost.py` have nothing to do yet: both need the
screens, which are being re-cut against the two-processor model. They return to
CI with them.

## What CI checks

| Workflow | Trigger | What it does |
| --- | --- | --- |
| `ci.yml` | push / PR / tag `v*` / manual | host suite, coverage `--check`, the font check, the docs check, the ESP-IDF matrix (v5.4, v5.5) building the panel, the pico-sdk build of the coprocessor, firmware artifacts |
| `docs.yml` | push to `main` touching `docs/` | publishes `docs/` to the GitHub wiki |
| `release.yml` | tag `v*` | builds both images, packages them, opens a release |

Coverage lands in the README and CI runs `--check`, which fails on drift rather
than committing a fixup — a file that rewrites itself is one nobody reads the
diff of.

**The wiki must exist before `docs.yml`'s first run.** Create one page by hand
in the repository's Wiki tab, otherwise the clone 404s.
