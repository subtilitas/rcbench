# Building

<sub>**English** · [Deutsch](Building-de.md)</sub>

Three builds read one source tree: the host test suite, the panel firmware
(ESP-IDF (Espressif Internet-of-Things Development Framework)) and the
IOmcu firmware (pico-sdk).

## The tree

```
rcbench/
  docs/                   wiki source, English and German
  tools/                  the checks, generators and measurements below
  test/tools/             pytest cases for the tools
  shared/                 pure C: no ESP-IDF, no pico-sdk, no FreeRTOS types
    gfx/                  rasteriser and seven fonts
    touch/                coordinate and event mapping
    ui/                   theme · widgets · icons · router · screens
    settings/             typed schema and values
    logfile/              number and CSV parsing
    files/                file manager and editor core: text buffer · highlighting · names and paths · card table · panes · rules · jobs
    link/                 page protocol · CAN framing · watchdogs · diagnosis
    artwork/              the panel's cache of board drawings
    bench/                bench_state · telemetry simulator · log writer · rotary knob decode
    outputs/              channels · driver table · the ARMED state, slew and staleness
    safety/               heartbeat generator (panel) and monitor (IOmcu)
    servo/                limit and synchronisation searches · servo model
    can/                  bit timing for both controllers · MCP2515 registers · echo self-test
    esc/                  ESC programming profiles, their JSON reader and registry
    sense/                INA228 and INA3221 current monitor drivers
    fwupdate/             panel firmware image check · upload staging · installer
    webaccess/            Wi-Fi session · card listing and ZIP stream · the page · mDNS
    text/                 the interface's words, every language in one table (header only)
  protocols/              one module per external interface: a pure C core, its RP2350 pin driver in rp2350/, a README.md
    dshot/                DShot frames · GCR decode · eRPM · PIO driver, plain and bidirectional
    ppm/                  PPM frame layout · PIO and DMA driver
    sbus/                 S.BUS decoder; no pin driver
    openyge/              OpenYGE framing, CRC, status and parameter cache; no pin driver
    pdmini/               PD mini driver · PIO UART
    phase_tap/            ESC tone detector · edge ring reader · PIO capture
    kst/                  programming of KST and Chaservo servos: frames, registers, limits, write plans, session · rp2350: kst_line.pio · kst_line
    hitec/                Hitec servo wire, both families: frames, transactions, reply decoders · rp2350: hitec_line.pio · hitec_line
  firmware/
    panel/                ESP-IDF project (ESP32-P4)
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
`protocols/` it names; the IOmcu and the host suite use
`add_subdirectory()` for the modules they need. A module with an `rp2350/`
folder adds its pin driver as the INTERFACE library `rcbench_<module>_rp2350`
in a pico-sdk build only. Includes are flat: `#include "gfx.h"`.

| Module | panel | iomcu | host |
| --- | :-: | :-: | :-: |
| `gfx` · `touch` · `ui` · `settings` · `logfile` · `files` · `sbus` | ✔ | | ✔ |
| `link` · `bench` · `outputs` · `servo` · `safety` · `can` · `text` | ✔ | ✔ | ✔ |
| `artwork` · `esc` · `fwupdate` · `webaccess` | ✔ | | ✔ |
| `openyge` · `ppm` | | ✔ | ✔ |
| `files` | | | ✔ |
| `sense` · `pdmini` · `phase_tap` · `kst` · `hitec` · `dshot` | ✔ | ✔ | ✔ |

## Toolchains

| | Version | Notes |
| --- | --- | --- |
| ESP-IDF | v5.5.5 or newer on the v5.5 line | the panel's ESP32-P4 is chip revision 3.2, which Espressif's advisory AR2026-004 pairs with v5.5.5 or later; the build sets `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=n` and minimum revision 3.1. Neither v5.5.0 nor v5.5.1 compiles: their TWAI (two-wire automotive interface) driver sends a CAN frame with DLC (data length code) 0 as DLC 8, and the IOmcu refuses every link read sent that way. CI (continuous integration) builds v5.5.5 |
| pico-sdk | 2.0 or newer | RP2350 support; CI builds 2.3.0 |
| ARM GNU toolchain | 14.2 | any `arm-none-eabi` release targeting Cortex-M33 |

`firmware/panel/sdkconfig.defaults` sets the ESP32-P4 at chip revision 3.1
or later and 400 MHz, 32 MB of flash, the in-package PSRAM (pseudo-static
random-access memory) at 200 MHz, code and constants in flash rather than
PSRAM, the zero-initialised data that `main/psram_bss.lf` names in PSRAM
([Internal RAM](Performance.md#internal-ram)), the DSI (Display Serial
Interface) host's interrupt running during a flash operation
(`CONFIG_LCD_DSI_ISR_CACHE_SAFE`), the partition table at 0xC000, which
leaves the bootloader 40,960 bytes from 0x2000 (it takes 0x6140 bytes with
rollback and its log at INFO), `-O2`, and the
console on UART0 with USB-Serial-JTAG (the chip's built-in USB (Universal
Serial Bus) serial and debug bridge) as secondary. Start from it rather
than from `menuconfig`.

ESP-IDF reads `sdkconfig.defaults` only when it generates `sdkconfig`. A tree
that has been built before already has `firmware/panel/sdkconfig`, and that
file wins: delete it after changing the defaults, or the change has no effect
on the image.

## Rotary knob wiring

The rotary knob is an MT6701 magnetic angle sensor on an I2C
(Inter-Integrated Circuit) bus of its own, the ESP32-P4's second I2C
controller, on two free header pins of the 7-inch ESP32-P4 display board:
`BOARD_KNOB_PIN_SDA` and `BOARD_KNOB_PIN_SCL` in
`firmware/panel/components/board/include/board_pins.h`: GPIO30 (SDA) and
GPIO31 (SCL), the pins IO30 and IO31 of the 12-pin header P3 (3V3, GND, IO2
to IO5, IO28 to IO31, IO34, IO36). The sensor takes 3.3 V and GND from the
same header. The panel switches on the ESP32-P4's internal pull-ups; whether
they alone hold 400 kHz on the knob's wires is not measured, so a sensor
without pull-ups of its own gets 4.7 kOhm ones to 3.3 V, as the datasheet's
I2C circuit has.

The sensor has to be in its I2C/SSI (synchronous serial interface) mode,
MODE high. MODE low selects the ABZ encoder outputs, and the sensor then does
not answer on I2C. No EEPROM (electrically erasable programmable read-only
memory) setting is needed. Pins of the SOP-8 MT6701CT, from the
[MagnTek MT6701 datasheet, version 1.9](https://atta.szlcsc.com/upload/public/pdf/source/20260616/FEB700C1922508565884FF0A90656B50.pdf)
(2024-05), section 1 and figure 17:

| MT6701 pin | Connect to |
| --- | --- |
| 1 VDD | 3.3 V from the header. The datasheet's supply is 3.3 to 5.0 V |
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
- Nothing of the board is on this bus. The panel refuses an address in the
  table of `shared/bench/include/panel_i2c.h`, which holds 0x00 to 0x03 and
  0x78 to 0x7F, which the I2C specification reserves, and the addresses of
  the ESP32-S3 panel's CH422G and GT911. 0x06 is outside it. It is one of
  the specification's Hs-mode (high-speed mode) master codes, 0x04 to 0x07,
  which no device on this bus uses.
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

The knob is off until SETUP, PANEL, Rotary knob is switched on.
[Screens](Screens.md#panel-the-rotary-knob) describes what it does.

## Commands

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

# panel showing the DSI host's colour bars for 3 s at start-up, for a first
# look at a board
idf.py -C firmware/panel -B firmware/panel/build-bars -D RCBENCH_DISPLAY_BARS=ON build

# panel carrying the IOmcu image it offers over CAN; build the
# IOmcu first
idf.py -C firmware/panel -D RCBENCH_IOMCU_BIN=$PWD/firmware/iomcu/build/rcbench-iomcu.bin build

# panel without the touch injector: every panel build, a release's too,
# looks once at start-up for one at 0x14 beside the glass
# (testbench/touch-injector), and RCBENCH_TOUCH_INJECTOR=OFF leaves that
# code out; the option defaults to ON
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
# (The bench console, BenchConsole.md); not with SENSE_TRACE
cmake -S firmware/iomcu -B firmware/iomcu/build-console -DIOMCU_CONSOLE=ON
cmake --build firmware/iomcu/build-console
```

The IOmcu build produces three UF2 (USB Flashing Format) files:

| File | Holds | For |
| --- | --- | --- |
| `rcbench-iomcu-install.uf2` | the partition table at 0x0 and the image in slot A | a module without the table: the one USB load of [First run](FirstRun.md#the-iomcus-partition-table) |
| `rcbench-iomcu.uf2` | the image alone | a module with the table; the boot ROM writes it into the slot that is not running |
| `rcbench-iomcu-pt.uf2` | the partition table alone | `picotool` |

Copy one to the module's mass-storage drive while the module is held in
BOOTSEL. `rcbench-iomcu.bin` is the image the panel carries: the panel build
option `RCBENCH_IOMCU_BIN` takes its path, and the panel then offers it over
CAN ([Link](Link.md#firmware-update-over-the-link)). A panel built without
the option offers no IOmcu update.

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
| `tools/check_docs.py` | holds the pages to the tree: links and anchors resolve, every image is used, the sidebar is complete, every page has a German counterpart, the suite list in `STATUS.md` matches CMake, the screenshot counts in `STATUS.md` match `docs/img`, the stick programming pages' red light table matches `esc_stick_reason_is_fault()`, the tree above lists every module under `shared/` and `protocols/`, every source file carries an SPDX (Software Package Data Exchange) line, the protocol version, the heartbeat and link timings, the heartbeat and CAN pins, the coverage floors and the stack margin a page states are the constants in the headers and tools (`FACTS` in the tool lists each sentence), a table row that names a C constant gives its value, [Performance](Performance.md) has no ceiling table while `ci.yml` has no `--max-lines` argument, the pin counts in `hardware/docs/Pins.md` match `pinmap.json`, a German page quotes in backticks the German the screen shows: interface strings and formats, setting labels, help, options and categories, and the servo test's words |
| `tools/wiki_links.py` | rewrites the links of a copy of `docs/` for the wiki, where pages are addressed by title: `Page.md` becomes `Page`, a target above `docs/` a repository URL (`blob`, `tree`, `raw` for an image); compares each page with its rewrite through markdown-it-py 3.0.0 and exits 1 on any other difference, with page and line; `--check` changes nothing; `--verify` fails on a converted page whose links a wiki cannot open |
| `tools/check_formats.py` | compiles `shared/` with every `TR()` lookup and report word replaced by its English literal, under `-Wformat=2 -Wformat-nonliteral -Wformat-signedness`, and fails on any warning: each English format against the arguments of its call ([Language](Language.md)) |
| `tools/merge_texts.py`, `tools/old_texts.py`, `tools/c_tables.py` | move the interface's words from the per-file form (English in `ui_text.def` and the schema, German in `ui_text_de.c`) into the one table, `shared/text/include/ui_text.def`; `merge BASE THEIRS` carries the text changes of a branch still in the per-file form into the table during a merge ([Interface language](Language.md#adding-a-text)) |
| `tools/gen_font.py` | regenerates the seven embedded fonts from DejaVu Sans Mono, the five text faces with the German letters; `--check` fails if the committed tables differ |
| `tools/render_ui.py` | renders every screen to PNG (Portable Network Graphics) with the code the panel runs, in English into `docs/img/` and in German into `docs/img/de/`; `--check` compares with the committed images; `--fit` fails when a string overflows where it is drawn, in either language, except the English overflows the tool lists as known ([Language](Language.md)) |
| `tools/frame_cost.py` | measures cache-line fills per frame under cachegrind with the ESP32-S3's cache at the renderer's 1024 x 600 px; `--check-doc` holds the table in [Performance](Performance.md) to a local build while both are 800 x 480 px, and reports it as not checked otherwise; CI does not run it |
| `tools/stack_check.py` | reads every panel task's deepest call chain out of the built ELF (Executable and Linkable Format) file and fails when one exceeds its stack less 1024 bytes; decodes Xtensa code only and stops on the ESP32-P4's RISC-V image, so CI does not run it on the panel; takes the build directory, default `firmware/panel/build`; `-v` prints each deepest chain and every call it cannot follow; `--check-doc` holds the task table in [Performance](Performance.md#stacks) to the build; `--iomcu` reads the IOmcu image instead and fails when a core's deepest chain, one interrupt and 256 bytes exceed its stack; with `--check-doc` it holds core 0's row of the IOmcu table, the count of calls through a register reachable from `main` in both Performance pages and core 0's sentence in `STATUS.md`; the ELF reader and the call graph are in `tools/stack_elf.py`, the IOmcu's half in `tools/stack_iomcu.py` |
| `tools/image_slot.py` | reads the partition table of a panel build and fails when the image is larger than the smaller of the two OTA (over-the-air) application slots, when the table has fewer than two such slots, or when they differ in size; prints the room left; takes the build directory, default `firmware/panel/build` |
| `tools/psram_bss.py` | reads the linker map of a panel build and fails when a line in the entries of a mapping in `firmware/panel/main/psram_bss.lf` is not `<object> (rcbench_extram_bss)`, when an object that the fragment maps to PSRAM (pseudo-static random-access memory) holds no .bss in `.ext_ram.bss` or keeps any .bss in internal RAM; prints the bytes moved; `--list N` also lists every object with more than N bytes of .bss in internal RAM; takes the build directory, default `firmware/panel/build` |
| `tools/check_sizes.py` | counts the code lines of every file git tracks (a code line is not blank and not comment-only: comments and Doxygen blocks in C, C++, headers, TypeScript and JavaScript, and `#` comments and docstrings in Python, are left out; other file kinds count their non-blank lines) and holds the files over 1,000 code lines to `tools/check_sizes.txt`, which gives each one's reason to stay whole and, for some, a code-line count with a tolerance; `--check` fails on a file over 1,000 code lines that the list does not name, a listed file at or under 1,000 code lines, a count outside its tolerance and an exclusion that covers nothing; a file whose head names the `tools/gen_*.py` that generates it, when CI runs that generator with `--check`, is not listed |
| `tools/sense_trace.py` | reads a captured console of an IOmcu built with `-DSENSE_TRACE=ON`: reads each line against the line format and each trace of INA3221 CH1's 1 ms samples against its end line, writes one CSV (comma-separated values) file per trace, and replays every move through `shared/servo/servo_move.c` with the filter at 1, 4 and 8 samples and the band at 0.02, 0.05 and 0.10 A; `--servo-csv` adds the arrival less the AS5600's travel time, `--loaded-channel` names the output channels with a servo connected and leaves the commands of the others out ([First run](FirstRun.md#89-ch1s-1-ms-samples-on-the-console)) |
| `tools/pinmap_check.py` | checks the IO board's draft pin map, `hardware/docs/ioboard-pinmap-draft.json`, against the pin functions in the pico-sdk's `io_bank0.h`; needs a pico-sdk checkout |
| `tools/gen_board_drawing.py`, `tools/gen_esc_profiles.py` | generate the checked-in board drawings and ESC profile tables from their JSON sources (the board drawing from the configurator's `configurator/drawings/panel.json`); `--check` fails if the committed tables differ |
| `tools/ci_gate.py` | waits for the CI run on a commit and fails unless one passed; the first step of `docs.yml` and `release.yml` |
| `tools/release_kind.py` | reads a release tag and prints its kind, `testing` for an odd minor number and `stable` for an even one, with the `prerelease` and `latest` flags `release.yml` publishes by; with the tags of the full releases on GitHub after it, `latest` is `false` for a stable tag older than one of them; exits 1 on a tag out of form ([Releases](#releases)) |
| `tools/doc_check.py`, `Doxyfile` | runs Doxygen 1.18.0 with the `Doxyfile` over the C of `shared/`, `protocols/`, `firmware/panel/`, `firmware/iomcu/` and `testbench/touch-injector/core/`, without the generated tables, and compares its warnings, as file and message without the line, with `tools/doc_check_baseline.txt`; adds a warning for a C file without a `@file` block or without its `@brief`; `--check` fails on a warning not in the baseline and on a baseline line that no longer occurs, and with `--base REF` on any warning in a C file added since the merge base with `REF`, on a changed line or its comment block, for a missing `@file` block in a changed file, and on a comment block without `@brief` that the change adds or edits; a renamed file counts as new; `--update` rewrites the baseline. The HTML in `build/doxygen/html/` is the [C API (application programming interface) reference](https://subtilitas.github.io/rcbench/api/) |
| `tools/pages_changes.py` | decides whether a run of `pages.yml` is a full one, with the configurator's tests and browser tests, or builds the configurator page, the help and the API reference only: full when a changed path is under `configurator/`, `help/` or `docs/img/`, or is `pages.yml`, `rcbench_version.h`, `ui_screen.h`, `tools/help_site.py` or `tools/render_ui.py`; a push counts its changes from the newest successful push run of `pages.yml` on `main`, and is full when there is none |
| `test/tools/` | pytest cases for the tools above: `python3 -m pytest test/tools` |
| `.clang-tidy`, `.cppcheck-suppress`, `ruff.toml` | static analysis and lint configuration; every finding is an error. `ruff.toml` lists the files excused some docstring (D) codes; each listed file is excused the codes on its line; delete a code when its file is fixed |

`doc_check.py` needs Doxygen 1.18.0 and stops with exit code 2 on another
release: `tools/install_doxygen.sh 1.18.0 <SHA-256 from ci.yml> PREFIX`
installs it as `PREFIX/bin/doxygen`.

`gen_font.py` looks for the font in `RCBENCH_FONT_DIR`, then
`~/.local/share/fonts`, then the system font directories. `frame_cost.py` needs
`valgrind`; the other tools need a C compiler and Pillow. `mutate.py` needs
git and CMake as well, and `ci_gate.py` the `gh` command. `sense_trace.py`
needs CMake and a C compiler: it builds `test/host/sense_trace_replay.c`
into `build-trace/` unless `--replay` names a built one.

## Wi-Fi through the ESP32-C6

The ESP32-P4 has no radio. The 7-inch display board from Waveshare carries an
ESP32-C6-MINI-1 module wired to the P4's SDMMC (SD/MMC host) slot 1 as an
SDIO (Secure Digital input/output) device; the SD card is on slot 0. The
panel's Wi-Fi code (`firmware/panel/components/wifi_access/`) calls the
`esp_wifi` API; two components from the Espressif component registry carry
each call and every network frame to the C6. lwIP (lightweight IP stack),
the page's server and the TLS (transport layer security) client run on the
P4; the C6 runs the radio.

| Signal | P4 | C6 |
| --- | --- | --- |
| CLK | GPIO18 | IO19 |
| CMD | GPIO19 | IO18 |
| D0 to D3 | GPIO14 to GPIO17 | IO20 to IO23 |
| EN (chip enable), low resets | GPIO54 | EN |

SDIO runs 4 bits wide at 40 MHz. `components/board/include/board_pins.h`
names the pins (`BOARD_C6_*`), `firmware/panel/sdkconfig.defaults` hands
them to esp_hosted, and `wifi_radio.c` fails the build when the two differ.

### The managed components

| Component | Version | Licence | Role |
| --- | --- | --- | --- |
| `espressif/esp_wifi_remote` | 1.6.5 | Apache-2.0 | the `esp_wifi` API on a chip without a radio |
| `espressif/esp_hosted` | 2.13.0 | Apache-2.0 | the SDIO transport and the calls to the C6 |
| `espressif/wifi_remote_over_eppp` | 0.3.3 | Apache-2.0 | required by `esp_wifi_remote`; compiled, not linked |
| `espressif/eppp_link` | 1.1.6 | Apache-2.0 | required by the one above; compiled, not linked |
| `espressif/esp_serial_slave_link` | 1.1.2 | Apache-2.0 | required by the one above; compiled, not linked |

esp_hosted 2.x: every file the panel compiles from the registry is
Apache-2.0, except the protobuf-c runtime inside esp_hosted, BSD-2-Clause.
esp_hosted's message code generated from its `.proto` file is Espressif's,
Apache-2.0; `esp_wifi_remote`'s `dummy_src.c` is an empty file without a
licence line. [NOTICE](https://github.com/subtilitas/rcbench/blob/main/NOTICE)
carries the protobuf-c copyright and licence, which a panel image's
distribution has to reproduce.

`components/wifi_access/idf_component.yml` names the first two at exact
versions. `firmware/panel/dependencies.lock` records all five with their
hashes and is committed. The first build downloads them from
`components.espressif.com` into `firmware/panel/managed_components/`, which
git ignores, and checks each against its hash. A later build makes no
request: with the folder filled, a build without network access completes.
To move a version, change `idf_component.yml`, run
`idf.py update-dependencies` and commit the new lock file.

### The C6's firmware

The C6 runs Espressif's esp_hosted co-processor firmware. The version the
board ships with is not published and not known here. At the first Wi-Fi
start after power-up esp_hosted resets the C6: GPIO54 (EN) goes low for
10 ms, then esp_hosted waits a fixed 1,500 ms
(`CONFIG_ESP_HOSTED_SDIO_RESET_DELAY_MS`), starts the C6 as an SDIO card
and polls for its start event every 200 ms. The panel then writes the C6's
version to the console:

```
wifi: C6: esp_hosted firmware 2.13.0, host 2.13.0
```

A C6 firmware without the version request answers with
`wifi: C6: firmware version not reported, host 2.13.0`, after up to 5 s.
esp_hosted also logs `Version mismatch: Host [2.13.0] > Co-proc [x.y.z]`
when the major or minor numbers differ. Espressif states that a host newer
than its co-processor runs and may see request time-outs; the panel's
behaviour with an older C6 firmware is not measured. The panel has no path
that updates the C6. A C6 that answers is reset once per start of the
panel; later Wi-Fi starts find the link up and reset nothing.

### Failures

`esp_hosted_connect_to_slave()` returns `ESP_OK` whether or not the C6
came up (`esp_hosted_api.c` 177-192). The panel takes esp_hosted's
`ESP_HOSTED_EVENT_TRANSPORT_UP` event as the answer, and waits for it at
most 1,000 ms after that call has returned (`wifi_radio.c`).

- A C6 that does not start as an SDIO card: esp_hosted gives up after the
  card's start-up, at least 1,500 ms after the 1,520 ms reset; 1 reset.
- A C6 that starts as a card and sends no start event: esp_hosted polls 100
  times 200 ms apart and resets the C6 again after 50 and after 100 polls
  (`transport_drv.c` 227-252); 3 resets, about 25 s.
- Either way the start fails, WI-FI shows the failed start, and the panel
  holds the C6 in reset (GPIO54 low) until it restarts. Later starts fail
  at once, with no reset. The bench and the display run on meanwhile; the
  radio's start runs on its own task.
- A link that fails during a session: esp_hosted posts
  `ESP_HOSTED_EVENT_TRANSPORT_FAILURE`. The panel logs
  `wifi: C6 link failed: C6 held in reset, Wi-Fi is off until the panel
  restarts` once, holds the C6 in reset, and the session ends as a failed
  start. With the C6 in reset its SDIO lines are held high by their
  51 kOhm pull-ups, so esp_hosted's read task, priority 23 on core 0, waits
  instead of failing in a loop; this is not measured on a board. The panel
  does not restart for it
  (`CONFIG_ESP_HOSTED_TRANSPORT_RESTART_ON_FAILURE=n`): a restart would drop
  the link to the IOmcu and the heartbeat.
- From the failure to Wi-Fi off: the event loop delivers the event (not
  measured), the radio's task sees it within 100 ms, and takes the radio
  down without the two requests to the C6, `esp_wifi_stop()` and
  `esp_wifi_deinit()`, which would each end at esp_hosted's 5 s response
  time-out. Taking it down waits up to 1,500 ms for a GitHub fetch under way
  and up to 20,000 ms for the fetch task to end. The time on a board is not
  measured.

Current limitations, all inside esp_hosted. Each stops the panel with an
abort, which restarts it:

- When the co-processor reports a chip other than the ESP32-C6 in its start
  event (`transport_drv.c` 697-702), or an init event longer than 64 bytes
  (`transport_drv.c` 875-881), or a streaming SDIO mode the host does not
  run (`transport_drv.c` 944-947). The start event arrives at the first
  Wi-Fi start after power-up.
- When a CMD52 (single register) access to the C6 fails while esp_hosted
  starts the card: `ESP_ERROR_CHECK` on the interrupt enable, the bus width
  and the block size registers (`port_esp_hosted_host_sdio.c` 131-134,
  195-217). A C6 that half answers during a Wi-Fi start can trigger it.
- When the SDMMC controller or its slot 1 cannot be set up, or the memory
  for esp_hosted's queues, buffer pools or tasks cannot be had, esp_hosted
  stops the panel with an assertion during start-up, before `app_main()`
  and before the display shows anything. It sets these up at every start,
  with Wi-Fi off.

The start-up release check starts the radio on its own: with SETUP >
UPDATES' reminder on (`SET_UPD_REMIND`, on by default) and a kept home
network, the panel checks GitHub for a release after each power-up. A
persistent fault of the kinds above then restarts the panel at each
power-up, until the reminder is switched off or the home network is
forgotten.

The SD card on slot 0 and the C6 on slot 1 share one SDMMC controller:

- ESP-IDF's SDMMC driver does not hold a slot's set-up against a transfer
  on the other slot. Starting a slot and starting a card set the
  controller's clock divider, which both slots share, and send its clock
  update commands outside the driver's request mutex
  (`sdmmc_host_set_card_clk()`, `sdmmc_host.c` 301-351). When the card is
  mounted while a Wi-Fi session moves data to or from the C6, the transfer
  under way can fail; esp_hosted then reports a failed link and Wi-Fi stays
  off until the panel restarts. The other way round, esp_hosted's start of
  slot 1 at the first Wi-Fi start, and at each C6 reset it makes, can make a
  card read or write under way fail. Not measured.
- ESP-IDF's mount stops slot 0 on every failure after the controller's
  init (`vfs_fat_sdmmc.c` 310-312), and stops the whole controller when
  that leaves no slot counted (`sdmmc_host.c` 906-923). `storage.c` starts
  slot 0 itself before the mount and gives up with no stop when it does not
  start, so the count never drops to 0 under the C6's link.

### Cores and memory

On the ESP32-P4 a task created through `xTaskCreate()` or
`xTaskCreatePinnedToCore()` without a core runs on core 0, and core 1
belongs to the control task and the tasks pinned there. The panel links
with `--wrap=xTaskCreatePinnedToCore` (`components/wifi_access/wifi_radio.c`)
for it: esp_hosted creates 6 tasks at priority 23 without naming a core.
`xTaskCreateStatic()` and the `xTaskCreate*WithCaps()` calls are not
wrapped; esp_hosted uses `xTaskCreateStatic()` only with
`CONFIG_ESP_HOSTED_DFLT_TASK_FROM_SPIRAM`, which fails the build. The
console lists every task at start-up, `task <name> core <0|1|any> priority
<n>`.

The SD card and the C6 share ESP-IDF's SDMMC request mutex, which passes
on priority. While the run log task on core 1 holds it for a card write and
an esp_hosted task waits for it, the run log task runs at priority 23 on
core 1, above the control task. The control task's longest period under
that load is not measured.

esp_hosted's memory, all of it internal RAM:

| Part | Bytes | Source |
| --- | ---: | --- |
| static data | 1,637 | esp_hosted 1,576, esp_wifi_remote 61 (map file) |
| task stacks | 20,480 | 4 x 3,072 and 2 x 4,096 |
| SDIO buffer pool | 47,680 | 31 blocks of 1,536 bytes, 64-byte aligned (`sdio_drv.c` 246-262) |
| common buffer pool | 38,464 | 25 blocks of 1,536 bytes, 64-byte aligned (`transport_drv.c` 298-316) |

The two pools come from DMA-capable (direct memory access) internal RAM
(`port_esp_hosted_host_os.c` 130-147) and are made before `app_main()`,
with Wi-Fi off. `CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM` would move them
to PSRAM; it is off, and the free internal RAM with it on is not measured.
Other allocations of esp_hosted are not counted here. The console line
`wifi: radio up` names the internal RAM free with the radio up.

## CI

| Workflow | Trigger | Jobs |
| --- | --- | --- |
| `ci.yml` | push, pull request, tag `v*`, manual | host suite; the same suite under AddressSanitizer and UBSan (UndefinedBehaviorSanitizer); coverage floors and Codecov upload, which fails the job when it fails; font, docs, web help, wiki-link, screenshot and research-script checks; the protocol module check; clang-tidy and cppcheck over `shared/`, the cores under `protocols/` and `test/host/`, cppcheck's warning, performance and portability classes over `firmware/` and the pin drivers under `protocols/`, and ruff; the tools' pytest cases; the Doxygen comment check of `tools/doc_check.py`, which in a pull request also fails on any warning in a C file the pull request adds or on a line it changes; on a pull request the mutation check of the changed lines, which reports and does not fail on a survivor; panel build on ESP-IDF v5.5.5 for the ESP32-P4 with the check of the image against its update slot, a check that the image carries the IOmcu image and holds the touch injector's symbols and exFAT, and a second build with `RCBENCH_TOUCH_INJECTOR=OFF` that fails when it holds an injector symbol; IOmcu build on pico-sdk 2.3.0 with the pin map check and the stack check of its two cores, once more with `-DSENSE_TRACE=ON`, with the same stack check, failing when the default image holds a trace symbol, and once more with `-DIOMCU_CONSOLE=ON`, with the same stack check, failing when the default image holds a console symbol; the touch injector build on pico-sdk 2.3.0 for `waveshare_rp2350_zero`, not uploaded; firmware artifacts including a merged panel image for offset 0 |
| `docs.yml` | push to `main` touching `docs/`, manual | waits for the CI run on the same commit and, when it passed, mirrors `docs/` to the GitHub wiki |
| `mutation.yml` | Sunday 03:17 UTC, manual | every mutant of the lines of `shared/safety` and `shared/outputs` (3032 mutants) in 6 parallel shards of files, no mutant limit, no mutant started after 18000 s in a shard; the mutants left then are reported as not run; does not fail on a survivor; the result is the artifacts `mutation-full-0` to `mutation-full-5` and the job summaries |
| `release.yml` | tag `v*`, manual for a tag | waits for the CI run on the tagged commit and, when it passed, builds both images, packages them with checksums and creates a release, a pre-release for an odd minor number ([Releases](#releases)); beside that, without holding the release back, runs the configurator data check (`configurator/core/tools/check-annotate.sh --warn-only`) and shows each warning, such as a release tag newer than the configurator data, as an annotation and in the run summary |
| `pages.yml` | pull request and push to `main` touching `configurator/`, `help/`, `docs/img/`, the help generator, the `Doxyfile` or `tools/install_doxygen.sh`; a push to `main` touching the C sources as well | type check, tests, data check, size budget and browser tests of the configurator; the web help build of `tools/help_site.py`; the C API reference built by Doxygen 1.18.0; on a push to `main` it publishes the built page to GitHub Pages, with the web help under `help/` and the API reference under `api/`. A run that changes only the C sources, the `Doxyfile` or `tools/install_doxygen.sh` builds the configurator page without its checks and runs no browser test |

clang-tidy does not run over `firmware/`: it compiles each file and needs
the ESP-IDF and pico-sdk headers, which the analysis job does not have.
cppcheck's style class does not run over `firmware/` either. The host tests
are built with the library flags `-Wall -Wextra -Werror -Wshadow -Wconversion
-Wno-sign-conversion`; `test/host/.clang-tidy` lists the checks that are off
for them, each with its reason.

CI installs pillow 12.3.0, gcovr 8.6, ruff 0.16.5 and pytest 9.1.1 at these
exact versions, Doxygen 1.18.0 from the official Linux release archive
(`tools/install_doxygen.sh` checks its SHA-256 against the value in
`ci.yml`), and cppcheck 2.17.1 built from the official source archive of
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

## Releases

A release is a tag `vMAJOR.MINOR.PATCH` on `main`. Its minor number names
its kind:

| Minor number | Kind | On GitHub | Examples |
| --- | --- | --- | --- |
| odd | testing release | pre-release, never marked latest | 0.21.0, 0.21.3, 0.23.0, 1.3.0 |
| even | stable release | full release, marked latest unless a newer full release exists | 0.22.0, 0.22.3, 1.4.0 |

The patch number carries fixes in either kind: 0.21.1 is a fix of the
testing release 0.21.0, 0.22.1 a fix of the stable release 0.22.0.

`release.yml` reads the kind with `tools/release_kind.py` before it waits
for CI (continuous integration). A tag out of form fails the job, and
nothing is built. At the publish it runs the tool again with the tags of
the full releases GitHub holds then; one publish runs at a time across
tags. A stable release older than a published one is not marked latest:

```bash
python3 tools/release_kind.py v0.21.0   # kind=testing prerelease=true latest=false
python3 tools/release_kind.py v0.22.0   # kind=stable prerelease=false latest=true
python3 tools/release_kind.py v0.22.0 v0.20.0 v0.24.0   # kind=stable prerelease=false latest=false
```

`https://github.com/subtilitas/rcbench/releases/latest` names the newest
stable release. 0.22 is the last release line for the ESP32-S3 panel, and
later releases are for the 7-inch ESP32-P4 display board from Waveshare.
The panel's check reads GitHub's latest release with RELEASES set to
STABLE on SETUP > UPDATES, the default, and the 10 newest releases with
TESTING; STABLE reads those 10 as well when the latest release is past the
line and newer than the running build. On both settings it offers only a 0.22.x patch release newer than
the version it runs ([Screens: The release line](Screens.md#the-release-line)).
A release also loads by hand: its
`rcbench-panel-vX.Y.Z.bin` through the panel's browser page, or its
archives over USB (Universal Serial Bus), as in
[First run, §1](FirstRun.md#1-flash-both-boards). The release's
`rcbench-iomcu-vX.Y.Z.zip` and the image the panel carries are of the same
commit.
