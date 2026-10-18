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
    artwork/              the panel's cache of board drawings
    bench/                bench_state · telemetry simulator · log writer · rotary knob decode
    outputs/              channels · driver table · the ARMED state, slew and staleness
    safety/               heartbeat generator (panel) and monitor (coprocessor)
    servo/                limit and synchronisation searches · servo model
    can/                  bit timing for both controllers · MCP2515 registers · echo self-test
    esc/                  ESC programming profiles, their JSON reader and registry
    sense/                INA228 and INA3221 current monitor drivers
    fwupdate/             panel firmware image check · upload staging · installer
    webaccess/            Wi-Fi session · card listing and ZIP stream · the page · mDNS
  protocols/              one module per external interface: a pure C core, its RP2350 pin driver in rp2350/, a README.md
    dshot/                DShot frames · GCR decode · eRPM · PIO driver, plain and bidirectional
    ppm/                  PPM frame layout · PIO and DMA driver
    sbus/                 S.BUS decoder; no pin driver
    openyge/              OpenYGE framing, CRC, status and parameter cache; no pin driver
    pdmini/               PD mini driver · PIO UART
    phase_tap/            ESC tone detector · edge ring reader · PIO capture
    kst/                  programming of KST and Chaservo servos: frames, registers, limits, write plans, session · rp2350: kst_line.pio · kst_line
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
| `artwork` · `esc` · `fwupdate` · `webaccess` | ✔ | | ✔ |
| `openyge` · `ppm` | | ✔ | ✔ |
| `sense` · `pdmini` · `phase_tap` · `kst` · `dshot` | ✔ | ✔ | ✔ |

## Toolchains

| | Version | Notes |
| --- | --- | --- |
| ESP-IDF | v5.4 or newer | the RGB (parallel red-green-blue) panel's `on_frame_buf_complete` event, which the framebuffer swap waits on, exists from v5.4. Neither v5.5.0 nor v5.5.1 compiles: their TWAI (two-wire automotive interface) driver sends a CAN frame with DLC (data length code) 0 as DLC 8, and the coprocessor refuses every link read sent that way. CI (continuous integration) builds v5.4 and v5.5.3 |
| pico-sdk | 2.0 or newer | RP2350 support; CI builds 2.3.0 |
| ARM GNU toolchain | 14.2 | any `arm-none-eabi` release targeting Cortex-M33 |

`firmware/panel/sdkconfig.defaults` sets octal PSRAM (pseudo-static
random-access memory) at 80 MHz, the 64 KB data cache with 64-byte lines, code
and constants in flash rather than PSRAM, the zero-initialised data that
`main/psram_bss.lf` names in PSRAM ([Internal RAM](Performance.md#internal-ram)),
the RGB LCD (liquid-crystal display) interrupt not IRAM-safe (IRAM:
instruction random-access memory), so ESP-IDF masks it during a flash
operation, `-O2`, and the console on UART0 with USB-Serial-JTAG (the ESP32-S3's built-in USB (Universal Serial Bus)
serial and debug bridge) as secondary. Start from it rather than from
`menuconfig`.

ESP-IDF reads `sdkconfig.defaults` only when it generates `sdkconfig`. A tree
that has been built before already has `firmware/panel/sdkconfig`, and that
file wins: delete it after changing the defaults, or the change has no effect
on the image.

## Rotary knob wiring

The rotary knob is an MT6701 magnetic angle sensor on the panel's I2C
(Inter-Integrated Circuit) terminal, the 4-pin PH2.0 connector on the
Waveshare ESP32-S3-Touch-LCD-7. The terminal is GPIO8 (SDA) and GPIO9 (SCL),
the bus the touch controller and the CH422G expander use, through the
board's level translator. Pin order from the board schematic's text, not
measured: 1 I2C_VCC, 2 GND, 3 SDA, 4 SCL. Check it with a meter before
connecting.

The sensor has to be in its I2C/SSI (synchronous serial interface) mode,
MODE high. MODE low selects the ABZ encoder outputs, and the sensor then does
not answer on I2C. No EEPROM (electrically erasable programmable read-only
memory) setting is needed. Pins of the SOP-8 MT6701CT, from the
[MagnTek MT6701 datasheet, version 1.9](https://atta.szlcsc.com/upload/public/pdf/source/20260616/FEB700C1922508565884FF0A90656B50.pdf)
(2024-05), section 1 and figure 17:

| MT6701 pin | Connect to |
| --- | --- |
| 1 VDD | 3.3 V from I2C_VCC. The datasheet's supply is 3.3 to 5.0 V. The board's solder jumper feeds I2C_VCC from 3V3 as shipped |
| 2 MODE | VDD. An internal 200 kOhm pull-up holds an open pin high; figure 17 ties it to VDD. Never GND |
| 3 OUT | not connected; the panel reads the angle over I2C |
| 4 GND | GND |
| 5 PUSH | not connected |
| 6 A/SDA | SDA |
| 7 B/SCL | SCL |
| 8 Z/CSN | VDD, as figure 17 |

A breakout board brings out fewer pins. On one, check that MODE is at VDD
(a pin, a jumper or a pull-up) and that its supply pin takes 3.3 V. The
datasheet's I2C circuit has 4.7 kOhm pull-ups on SDA and SCL to VDD; a
breakout's own pull-ups go to its VDD, here 3.3 V.

- Address: 0x06, the datasheet's default. The panel reads 0x06 only. EEPROM
  programming can move the address to 0x46 (version 1.9; version 0.1 of the
  datasheet gives 0x26); a sensor moved there is not read.
- The board answers at 0x14, 0x20 to 0x27, 0x30 to 0x3F and 0x5D on this bus
  (the CH422G and the GT911). The panel refuses to put any other device at
  those addresses, or at 0x00 to 0x03 and 0x78 to 0x7F, which the I2C
  specification reserves (`shared/bench/include/panel_i2c.h`). 0x06 is
  outside them. It is one of the specification's Hs-mode (high-speed mode)
  master codes, 0x04 to 0x07, which no device on this bus uses. The board's
  one bus scan covered 0x08 to 0x77, so that nothing else answers at 0x06
  is not measured.
- Bus clock: 400 kHz. The datasheet allows an SCL (serial clock) period down
  to 1 us and a rise and fall time of 150 ns at most. The rise time on the
  terminal is not measured.
- The magnet is a diametrically magnetised disc centred over the chip.
- The I2C registers carry no field status: the magnet-too-weak and
  too-strong bits are in the SSI frame only. A sensor with no magnet over it
  still answers with an angle, and the panel takes it as a reading.
- Each poll reads 0x03, 0x04 and 0x03 again and drops a poll whose two 0x03
  bytes differ. A high byte that changes and changes back between them is
  accepted, so one step of up to 64 counts (0.52 % of the span at 270 deg)
  at a slider end stop remains possible; how often is not measured.
- Direction: on the MT6701CT-STD with DIR (register 0x29 bit 1, in EEPROM)
  at its default 0, the angle rises counter-clockwise seen from the chip's
  top. The panel counts the other way, so with the magnet above the chip's
  top face a clockwise turn raises the throttle. An MT6701CT-STV, whose
  angle rises clockwise, a part with DIR = 1, or a sensor mounted to be seen
  from below turns the knob the other way.

The knob is off until SETUP, APPLICATION, Rotary knob is switched on.
[Screens](Screens.md#application-the-rotary-knob) describes what it does.

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

# panel carrying the coprocessor image it offers over CAN; build the
# coprocessor first
idf.py -C firmware/panel -D RCBENCH_IOMCU_BIN=$PWD/firmware/iomcu/build/rcbench-iomcu.bin build

# panel for the measurement bench: also reads a touch injector at 0x14
# (testbench/touch-injector); never a release image
idf.py -C firmware/panel -B firmware/panel/build-inject -D RCBENCH_TOUCH_INJECTOR=ON build

# panel, as one image at offset 0; it erases the settings in nvs
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

The coprocessor build produces three UF2 (USB Flashing Format) files:

| File | Holds | For |
| --- | --- | --- |
| `rcbench-iomcu-install.uf2` | the partition table at 0x0 and the image in slot A | a module without the table: the one USB load of [First run](FirstRun.md#the-coprocessors-partition-table) |
| `rcbench-iomcu.uf2` | the image alone | a module with the table; the boot ROM writes it into the slot that is not running |
| `rcbench-iomcu-pt.uf2` | the partition table alone | `picotool` |

Copy one to the module's mass-storage drive while the module is held in
BOOTSEL. `rcbench-iomcu.bin` is the image the panel carries: the panel build
option `RCBENCH_IOMCU_BIN` takes its path, and the panel then offers it over
CAN ([Link](Link.md#firmware-update-over-the-link)). A panel built without
the option offers no coprocessor update.

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
| `tools/check_protocols.py` | holds every module under `protocols/` to the rule that lets it be built in another pico-sdk project: a core file includes C standard headers and the core's own headers only, a file under `rp2350/` also the module's driver headers, its generated `.pio.h` and pico-sdk headers, a driver's header includes no pico-sdk header, every public header carries `extern "C"` and compiles by itself as C11 and as C++17 with `-Wall -Wextra -Werror`, no include of a module names a path with `..` or an absolute path or a macro, or has a token after its operand, no `#include_next` names a header other than a C standard header or, in a C file under `rp2350/`, a pico-sdk header, the name `__has_include` stands outside a literal only as the operand of `#ifdef`, `#ifndef`, `#elifdef` or `#elifndef`, or of a `defined` term on an `#if` or `#elif` line that holds nothing but such terms, white space, `(`, `)`, `!`, `&` and `|`, no line holds only `#`, read under the literal rules of ISO C, GNU C, C23 and C++ and, in a `.pio` file, in the C that pioasm writes into the generated header, no C file, header or C of a `.pio` file holds a trigraph (`??` followed by one of `= / ' ( ) ! < > -`), no file holds a NUL byte, the module has a `README.md` with an "Interface" heading, a heading with "Trademark" in it where it names a brand (matched anywhere in the file, in any case, also inside an identifier, file name, address or number, not next to a letter, so not in `KSTART` or `KstSession`), and the words "no pin driver" where it has no `rp2350/` folder, its CMake files name no other module, and every `_Static_assert` outside `protocols/` on a module's constants is listed by file and condition in the tool and still present (CMake include directories and link targets are not checked, and a `__has_include` pasted together with `##` passes, and `""R"(` and `''R"(` are read as C reads them, a literal and a raw string, so an include that such a sequence hides in C and that C++11 or later reads, with `R` as a literal suffix, is not found); `--check` prints the broken rules only |
| `tools/security_parsers.py` | lists the modules under `shared/` and `protocols/` whose public headers declare a `parse`, `decode`, `unpack`, `_rx`, `_feed`, `_write`, `_read`, `_done` or `_utf8_next` function, plus the forced modules in the tool, which take outside input under other names, each with a reason (`shared/settings`, `shared/touch`); `--check` fails when the parser table in `SECURITY.md` and that list differ, or when a forced entry names a module that does not exist or that the name rule finds, a module being excused only by an entry with a reason in the tool |
| `tools/check_docs.py` | holds the pages to the tree: links and anchors resolve, every image is used, the sidebar is complete, every page has a German counterpart, the suite list in `STATUS.md` matches CMake, the screenshot counts in `STATUS.md` match `docs/img`, the stick programming pages' red light table matches `esc_stick_reason_is_fault()`, the tree above lists every module under `shared/` and `protocols/`, every source file carries an SPDX (Software Package Data Exchange) line, the protocol version, the heartbeat and link timings, the heartbeat and CAN pins, the coverage floors and the stack margin a page states are the constants in the headers and tools (`FACTS` in the tool lists each sentence), a table row that names a C constant gives its value, the ceiling table in [Performance](Performance.md) matches the `--max-lines` arguments in `ci.yml`, the pin counts in `hardware/docs/Pins.md` match `pinmap.json`, a German page quotes in backticks the German the screen shows: interface strings and formats, setting labels, help, options and categories, and the servo test's words |
| `tools/wiki_links.py` | rewrites the links of a copy of `docs/` for the wiki, where pages are addressed by title: `Page.md` becomes `Page`, a target above `docs/` a repository URL (`blob`, `tree`, `raw` for an image); compares each page with its rewrite through markdown-it-py 3.0.0 and exits 1 on any other difference, with page and line; `--check` changes nothing; `--verify` fails on a converted page whose links a wiki cannot open |
| `tools/check_formats.py` | compiles `shared/` with every `TR()` lookup and report word replaced by its English literal, under `-Wformat=2 -Wformat-nonliteral -Wformat-signedness`, and fails on any warning: each English format against the arguments of its call ([Language](Language.md)) |
| `tools/gen_font.py` | regenerates the three embedded fonts from DejaVu Sans Mono, the two text faces with the German letters; `--check` fails if the committed tables differ |
| `tools/render_ui.py` | renders every screen to PNG (Portable Network Graphics) with the code the panel runs, in English into `docs/img/` and in German into `docs/img/de/`; `--check` compares with the committed images; `--fit` fails when a string overflows where it is drawn, in either language, except the English overflows the tool lists as known ([Language](Language.md)) |
| `tools/frame_cost.py` | measures cache-line fills per frame under cachegrind; `--check-doc` holds the table in [Performance](Performance.md) |
| `tools/stack_check.py` | reads every panel task's deepest call chain out of the built ELF (Executable and Linkable Format) file and fails when one exceeds its stack less 1024 bytes; takes the build directory, default `firmware/panel/build`; `-v` prints each deepest chain and every call it cannot follow; `--check-doc` holds the task table in [Performance](Performance.md#stacks) to the build; `--iomcu` reads the coprocessor image instead and fails when a core's deepest chain, one interrupt and 256 bytes exceed its stack; with `--check-doc` it holds core 0's row of the coprocessor table; the ELF reader and the call graph are in `tools/stack_elf.py`, the coprocessor's half in `tools/stack_iomcu.py` |
| `tools/ram_budget.py` | reads the internal heap a panel build leaves, subtracts every task stack, queue and buffer the panel allocates at run time and what each Wi-Fi mode allocates on top, and fails when the largest mode leaves under 32,768 bytes; takes the build directory, default `firmware/panel/build`; `-v` lists every allocation; `--check-doc` holds the internal RAM (random-access memory) table in [Performance](Performance.md#internal-ram) to the build |
| `tools/sense_trace.py` | reads a captured console of a coprocessor built with `-DSENSE_TRACE=ON`: reads each line against the line format and each trace of INA3221 CH1's 1 ms samples against its end line, writes one CSV (comma-separated values) file per trace, and replays every move through `shared/servo/servo_move.c` with the filter at 1, 4 and 8 samples and the band at 0.02, 0.05 and 0.10 A; `--servo-csv` adds the arrival less the AS5600's travel time, `--loaded-channel` names the output channels with a servo connected and leaves the commands of the others out ([First run](FirstRun.md#89-ch1s-1-ms-samples-on-the-console)) |
| `tools/pinmap_check.py` | checks the IO board's draft pin map, `hardware/docs/ioboard-pinmap-draft.json`, against the pin functions in the pico-sdk's `io_bank0.h`; needs a pico-sdk checkout |
| `tools/gen_board_drawing.py`, `tools/gen_esc_profiles.py` | generate the checked-in board drawings and ESC profile tables from their JSON sources (the board drawing from the configurator's `configurator/drawings/panel.json`); `--check` fails if the committed tables differ |
| `tools/ci_gate.py` | waits for the CI run on a commit and fails unless one passed; the first step of `docs.yml` and `release.yml` |
| `tools/patch_rgb_driver.py` | runs in the panel's configure step: writes a copy of ESP-IDF's RGB panel driver (`components/esp_lcd/rgb/esp_lcd_panel_rgb.c`) into the build directory in which every restart of the scan is the clean start of boot, and fails the build when one of its 3 patches does not match the driver exactly once ([Constraints](../STATUS.md#constraints)) |
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
| `ci.yml` | push, pull request, tag `v*`, manual | host suite; the same suite under AddressSanitizer and UBSan (UndefinedBehaviorSanitizer); coverage floors and Codecov upload, which fails the job when it fails; font, docs, web help, wiki-link, frame-cost, screenshot and research-script checks; the protocol module check; clang-tidy and cppcheck over `shared/`, the cores under `protocols/` and `test/host/`, cppcheck's warning, performance and portability classes over `firmware/` and the pin drivers under `protocols/`, and ruff; the tools' pytest cases; on a pull request the mutation check of the changed lines, which reports and does not fail on a survivor; panel build on ESP-IDF v5.4 and v5.5.3, each with the task stack check and the internal RAM budget, v5.4 with the stack table and the internal RAM table of this wiki; coprocessor build on pico-sdk 2.3.0 with the pin map check and the stack check of its two cores, and once more with `-DSENSE_TRACE=ON`, with the same stack check, failing when the default image holds a trace symbol; on v5.4 a panel build with `RCBENCH_TOUCH_INJECTOR=ON`, with the stack check and the internal RAM budget, failing when the default image holds an injector symbol; the touch injector build on pico-sdk 2.3.0 for `waveshare_rp2350_zero`, not uploaded; firmware artifacts including a merged panel image for offset 0 |
| `docs.yml` | push to `main` touching `docs/`, manual | waits for the CI run on the same commit and, when it passed, mirrors `docs/` to the GitHub wiki |
| `mutation.yml` | Sunday 03:17 UTC, manual | every mutant of the lines of `shared/safety` and `shared/outputs` (3032 mutants) in 6 parallel shards of files, no mutant limit, no mutant started after 18000 s in a shard; the mutants left then are reported as not run; does not fail on a survivor; the result is the artifacts `mutation-full-0` to `mutation-full-5` and the job summaries |
| `release.yml` | tag `v*`, manual for a tag | waits for the CI run on the tagged commit and, when it passed, builds both images, packages them with checksums and creates a release; beside that, without holding the release back, runs the configurator data check (`configurator/core/tools/check-annotate.sh --warn-only`) and shows each warning, such as a release tag newer than the configurator data, as an annotation and in the run summary |
| `pages.yml` | pull request and push to `main` touching `configurator/`, `help/`, `docs/img/` or the help generator | type check, tests, data check, size budget and browser tests of the configurator; the web help build of `tools/help_site.py`; on a push to `main` it publishes the built page to GitHub Pages, with the web help under `help/` |

clang-tidy does not run over `firmware/`: it compiles each file and needs
the ESP-IDF and pico-sdk headers, which the analysis job does not have.
cppcheck's style class does not run over `firmware/` either. The host tests
are built with the library flags `-Wall -Wextra -Werror -Wshadow -Wconversion
-Wno-sign-conversion`; `test/host/.clang-tidy` lists the checks that are off
for them, each with its reason.

CI installs pillow 12.3.0, gcovr 8.6, ruff 0.16.5 and pytest 9.1.1 at these
exact versions, and cppcheck 2.17.1 built from the official source archive of
the tag (`tools/install_cppcheck.sh` checks its SHA-256 against the value in
`ci.yml` and fails on a mismatch; the build is cached on version and hash). The
distribution's cppcheck is older (2.13 on Ubuntu 24.04) and reports differently;
install 2.17.1 locally with `tools/install_cppcheck.sh 2.17.1 <SHA-256 from
ci.yml> ~/cppcheck-2.17.1` or from a distribution package of 2.17.1. Every job has a `timeout-minutes` limit: 100 min for the jobs that wait for CI with `tools/ci_gate.py` (up to 5400 s, 90 min), 10 to 60 min for the others. ctest stops a
test after 120 s. With the environment variable `CI` set, a missing C++
compiler fails the CMake configure step of the host suite, and a missing `git`, `cmake` or `cc`
fails the end-to-end cases of `tools/mutate.py`. `tools/check_docs.py` holds
the pico-sdk and ESP-IDF tags of this page to those in `ci.yml` and
`release.yml`, and fails when a build page has no ESP-IDF tag statement.

Every check runs locally;
[CONTRIBUTING.md](https://github.com/subtilitas/rcbench/blob/main/CONTRIBUTING.md)
lists the commands.

The wiki must contain at least one page before the first `docs.yml` run;
otherwise the wiki repository does not exist and the clone fails.
