# Building

<sub>**English** · [Deutsch](Building-de.md)</sub>

Three builds read one source tree: the host test suite, the panel firmware
(ESP-IDF (Espressif Internet-of-Things Development Framework)) and the
coprocessor firmware (pico-sdk).

## The tree

```
rcbench/
  docs/                   wiki source, English and German
  tools/                  the checks, generators and measurements below
  test/tools/             pytest cases for the tools
  shared/                 pure C: no ESP-IDF, no pico-sdk, no FreeRTOS types
    gfx/                  rasteriser and three fonts
    touch/                coordinate and event mapping
    ui/                   theme · widgets · icons · router · screens
    settings/             typed schema and values
    logfile/              number and CSV parsing
    link/                 page protocol · CAN framing · watchdogs · diagnosis
    artwork/              the panel's cache of board photographs
    bench/                bench_state · telemetry simulator · log writer · rotary knob decode
    outputs/              channels · driver table · arming, slew and staleness
    safety/               heartbeat generator (panel) and monitor (coprocessor)
    servo/                limit and synchronisation searches · servo model
    can/                  bit timing for both controllers · MCP2515 registers · echo self-test
    esc/                  ESC programming profiles, their JSON reader and registry
    sense/                INA228 and INA3221 current monitor drivers
  protocols/              one module per external interface: a pure C core, its RP2350 pin driver in rp2350/, a README.md
    dshot/                DShot frames · GCR decode · eRPM · PIO driver, plain and bidirectional
    ppm/                  PPM frame layout · PIO and DMA driver
    sbus/                 S.BUS decoder; no pin driver
    openyge/              OpenYGE framing, CRC, status and parameter cache; no pin driver
    pdmini/               PD mini driver · PIO UART
    phase_tap/            ESC tone detector · edge ring reader · PIO capture
    kst/                  KST servo programming: frames, registers, limits, write plans, session; no pin driver
  firmware/
    panel/                ESP-IDF project (ESP32-S3)
    iomcu/                pico-sdk project (RP2350)
  test/host/              the host suite, one binary per module or screen
  hardware/               board design record; no board exists
```

Modules under `shared/` contain the logic and have no hardware dependency.
A module under `protocols/` is one interface to external hardware, built
to be copied into another pico-sdk project: its core includes C standard
headers only, and its `rp2350/` folder includes the core and the pico-sdk
only. Everything else that touches hardware is under `firmware/`.
`tools/check_protocols.py` holds each module to that
([CONTRIBUTING](https://github.com/subtilitas/rcbench/blob/main/CONTRIBUTING.md#where-code-goes)).

### Two directories, three builds

Each module under `shared/` and `protocols/` carries a `CMakeLists.txt`
that registers an IDF component under `ESP_PLATFORM` and a plain static
library otherwise:

```cmake
if(ESP_PLATFORM)
    idf_component_register(SRCS ${SRCS} INCLUDE_DIRS include)
else()
    add_library(rcbench_gfx STATIC ${SRCS})
    target_include_directories(rcbench_gfx PUBLIC include)
endif()
```

The panel sets `EXTRA_COMPONENT_DIRS` to `shared/` and to each module under
`protocols/` it names; the coprocessor and the host suite use
`add_subdirectory()` for the modules they need. A module with an `rp2350/`
folder adds its pin driver as the INTERFACE library `rcbench_<module>_rp2350`
in a pico-sdk build only. Includes are flat: `#include "gfx.h"`.

| Module | panel | iomcu | host |
| --- | :-: | :-: | :-: |
| `gfx` · `touch` · `ui` · `settings` · `logfile` · `sbus` | ✔ | | ✔ |
| `link` · `bench` · `outputs` · `servo` · `safety` · `can` | ✔ | ✔ | ✔ |
| `artwork` · `esc` | ✔ | | ✔ |
| `openyge` · `dshot` · `ppm` | | ✔ | ✔ |
| `sense` · `pdmini` · `phase_tap` | ✔ | ✔ | ✔ |
| `kst` | | | ✔ |

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

## Rotary knob wiring

The rotary knob is an AS5600 magnetic angle sensor on the panel's I2C
(Inter-Integrated Circuit) terminal, the 4-pin PH2.0 connector on the
Waveshare ESP32-S3-Touch-LCD-7. The terminal is GPIO8 (SDA) and GPIO9 (SCL),
the bus the touch controller and the CH422G expander use, through the
board's level translator. Pin order from the board schematic's text, not
measured: 1 I2C_VCC, 2 GND, 3 SDA, 4 SCL. Check it with a meter before
connecting.

| AS5600 pin | Connect to |
| --- | --- |
| VDD3V3 and VDD5V | 3.3 V from I2C_VCC, both pins tied together, which is the datasheet's 3.3 V supply. The board's solder jumper feeds I2C_VCC from 3V3 as shipped; a breakout with its own regulator takes its supply pin from I2C_VCC at 3.3 V |
| GND | GND |
| SDA, SCL | SDA, SCL |
| DIR | GND or VCC, never open. GND: the angle rises when the magnet turns clockwise seen from the chip's top (datasheet; not checked here). VCC reverses it |
| PGO | GND |
| OUT | not connected; the panel reads the angle over I2C |

The magnet is a diametrically magnetised disc centred over the chip. The
AS5600 reports a field too weak or too strong in its STATUS register, and the
panel treats either as no answer. The sensor reads at 0x36, which is fixed.
On the Waveshare ESP32-S3 Touch LCD 7 the CH422G I/O expander answers at I2C
addresses 0x20 to 0x27 and 0x30 to 0x3F, which includes 0x36, so an AS5600
cannot be read on that bus. The knob is off until SETUP, APPLICATION, Rotary
knob is switched on. [Screens](Screens.md#application-the-rotary-knob)
describes what it does.

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

# coprocessor, debug build that prints INA3221 CH1's 1 ms samples
cmake -S firmware/iomcu -B firmware/iomcu/build-trace -DSENSE_TRACE=ON
cmake --build firmware/iomcu/build-trace
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
| `tools/coverage.py` | measures host-suite line coverage, enforces the floors (94% total, 85% per file) and writes the table in `STATUS.md` and the figure in `README.md` and `README-de.md`; `--check` fails on drift, and on a C file under `shared/` or a core file under `protocols/` that is not in the measurement or has no counters |
| `tools/mutate.py` | changes one line of `shared/` or of a core under `protocols/` at a time in a copy of the tree (a comparison flipped, a bound plus or minus 1, a stored assignment dropped), builds and runs the host suite and reports the changes the suite passes with; by default the lines changed since `origin/main`, at most 60 mutants and 2700 s; CI runs it on pull requests and does not fail on a survivor |
| `tools/check_sanitizers.py` | configures the sanitizer build and fails unless every compile command under `shared/`, `protocols/` and `test/host/` carries `-fsanitize=address,undefined`, `-fno-sanitize-recover=all` and `-fno-omit-frame-pointer`, and no other flag of that family |
| `tools/check_protocols.py` | holds every module under `protocols/` to the rule that lets it be built in another pico-sdk project: a core file includes C standard headers and the core's own headers only, a file under `rp2350/` also the module's driver headers, its generated `.pio.h` and pico-sdk headers, a driver's header includes no pico-sdk header, every public header carries `extern "C"` and compiles by itself as C11 and as C++17 with `-Wall -Wextra -Werror`, the module has a `README.md`, and its CMake files name no other module; `--check` prints the broken rules only |
| `tools/check_docs.py` | holds the pages to the tree: links and anchors resolve, every image is used, the sidebar is complete, every page has a German counterpart, the suite list in `STATUS.md` matches CMake, the screenshot counts in `STATUS.md` match `docs/img`, the stick programming pages' red light table matches `esc_stick_reason_is_fault()`, the tree above lists every module under `shared/` and `protocols/`, every source file carries an SPDX (Software Package Data Exchange) line, the protocol version, the heartbeat and link timings, the heartbeat and CAN pins, the coverage floors and the stack margin a page states are the constants in the headers and tools (`FACTS` in the tool lists each sentence), a table row that names a C constant gives its value, the ceiling table in [Performance](Performance.md) matches the `--max-lines` arguments in `ci.yml`, the pin counts in `hardware/docs/Pins.md` match `pinmap.json`, a German page quotes in backticks the German the screen shows: interface strings and formats, setting labels, help, options and categories, and the servo test's words |
| `tools/wiki_links.py` | rewrites `Page.md` links to `Page` for the wiki, where pages are addressed by title |
| `tools/check_formats.py` | compiles `shared/` with every `TR()` lookup and report word replaced by its English literal, under `-Wformat=2 -Wformat-nonliteral -Wformat-signedness`, and fails on any warning: each English format against the arguments of its call ([Language](Language.md)) |
| `tools/gen_font.py` | regenerates the three embedded fonts from DejaVu Sans Mono, the two text faces with the German letters; `--check` fails if the committed tables differ |
| `tools/render_ui.py` | renders every screen to PNG (Portable Network Graphics) with the code the panel runs, in English into `docs/img/` and in German into `docs/img/de/`; `--check` compares with the committed images; `--fit` fails when a string overflows where it is drawn, in either language, except the English overflows the tool lists as known ([Language](Language.md)) |
| `tools/frame_cost.py` | measures cache-line fills per frame under cachegrind; `--check-doc` holds the table in [Performance](Performance.md) |
| `tools/stack_check.py` | reads every panel task's deepest call chain out of the built ELF (Executable and Linkable Format) file and fails when one exceeds its stack less 1024 bytes; takes the build directory, default `firmware/panel/build`; `-v` prints each deepest chain and every call it cannot follow; `--check-doc` holds the task table in [Performance](Performance.md#stacks) to the build; `--iomcu` reads the coprocessor image instead and fails when a core's deepest chain, one interrupt and 256 bytes exceed its stack; with `--check-doc` it holds core 0's row of the coprocessor table |
| `tools/sense_trace.py` | reads a captured console of a coprocessor built with `-DSENSE_TRACE=ON`: reads each line against the line format and each trace of INA3221 CH1's 1 ms samples against its end line, writes one CSV (comma-separated values) file per trace, and replays every move through `shared/servo/servo_move.c` with the filter at 1, 4 and 8 samples and the band at 0.02, 0.05 and 0.10 A; `--servo-csv` adds the arrival less the AS5600's travel time ([First run](FirstRun.md#89-ch1s-1-ms-samples-on-the-console)) |
| `tools/pinmap_check.py` | checks the IO board's draft pin map, `hardware/docs/pinmap.json`, against the pin functions in the pico-sdk's `io_bank0.h`; needs a pico-sdk checkout |
| `tools/gen_board_art.py`, `tools/gen_esc_profiles.py` | generate the checked-in board artwork and ESC profile tables from their PNG and JSON sources; `--check` fails if the committed tables differ |
| `tools/ci_gate.py` | waits for the CI run on a commit and fails unless one passed; the first step of `docs.yml` and `release.yml` |
| `test/tools/` | pytest cases for the tools above: `python3 -m pytest test/tools` |
| `.clang-tidy`, `.cppcheck-suppress`, `ruff.toml` | static analysis and lint configuration; every finding is an error |

`gen_font.py` looks for the font in `RCBENCH_FONT_DIR`, then
`~/.local/share/fonts`, then the system font directories. `frame_cost.py` needs
`valgrind`; the other tools need a C compiler and Pillow. `mutate.py` needs
git and CMake as well, and `ci_gate.py` the `gh` command. `sense_trace.py`
needs CMake and a C compiler: it builds `test/host/sense_trace_replay.c`
into `build-trace/` unless `--replay` names a built one.

## CI

| Workflow | Trigger | Jobs |
| --- | --- | --- |
| `ci.yml` | push, pull request, tag `v*`, manual | host suite; the same suite under AddressSanitizer and UBSan (UndefinedBehaviorSanitizer); coverage floors and Codecov upload, which fails the job when it fails; font, docs, wiki-link, frame-cost, screenshot and research-script checks; the protocol module check; clang-tidy and cppcheck over `shared/` and the cores under `protocols/`, cppcheck's warning, performance and portability classes over `firmware/` and the pin drivers under `protocols/`, and ruff; the tools' pytest cases; on a pull request the mutation check of the changed lines, which reports and does not fail on a survivor; panel build on ESP-IDF v5.4 and v5.5, each with the task stack check, v5.4 with the stack table of this wiki; coprocessor build on pico-sdk 2.3.0 with the pin map check and the stack check of its two cores, and once more with `-DSENSE_TRACE=ON`, with the same stack check, failing when the default image holds a trace symbol; firmware artifacts including a merged panel image for offset 0 |
| `docs.yml` | push to `main` touching `docs/`, manual | waits for the CI run on the same commit and, when it passed, mirrors `docs/` to the GitHub wiki |
| `release.yml` | tag `v*`, manual for a tag | waits for the CI run on the tagged commit and, when it passed, builds both images, packages them with checksums, creates a release, and carries the build guide PDFs over from the latest release |

clang-tidy does not run over `firmware/`: it compiles each file and needs
the ESP-IDF and pico-sdk headers, which the analysis job does not have.
cppcheck's style class does not run over `firmware/` either.

Every check runs locally;
[CONTRIBUTING.md](https://github.com/subtilitas/rcbench/blob/main/CONTRIBUTING.md)
lists the commands.

The wiki must contain at least one page before the first `docs.yml` run;
otherwise the wiki repository does not exist and the clone fails.
