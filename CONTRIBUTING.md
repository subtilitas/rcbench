# Contributing to rcbench

The rules below are enforced by CI (continuous integration) where a machine can
enforce them. Read this before a large change.

## Licensing rule

**Protocols are implemented from published specifications or from captures of
the wire, never from another implementation.**

Nearly every open implementation of the protocols this bench speaks (ESC
(electronic speed controller) serial, receiver buses, telemetry) is GPL (GNU
General Public License) or AGPL (GNU Affero General Public License), and this
repository is MIT (Massachusetts Institute of Technology). A decoder written by
reading GPL source cannot be relicensed by rewording it.

- Work from the specification, a datasheet, or a capture of the wire.
- If you have read a GPL implementation of the thing you are writing, say so in
  the pull request; somebody else writes it.
- Permissive references may be read for confirmation: PX4's decoders (BSD,
  Berkeley Software Distribution licence) and the MIT reference code for
  SRXL2, JETI EX Bus, DShot and DroneCAN. The code here is written
  independently of them.

A protocol with no published description and no capture of its wire is not
implemented. The programming protocol of KST and Chaservo servos has no
published description and is written from captures made on hardware the
project owns. The parameters of ESCs with BLHeli_32 firmware and remote
configuration over JETI EX Bus are recorded as not planned for this reason.

## Safety-relevant code

Anything that can make an output move is held to these rules; the reasoning is
in [Safety](docs/Safety.md).

- **Fail safe by absence.** When a signal stops, stop driving. Never require a
  message to arrive in order to be safe.
- **Refuse and clamp are different answers.** A configuration mistake (an
  endpoint no servo can take) is refused, atomically, leaving the previous
  value. A command outside its range is clamped.
- **A stop latches.** Nothing switches back to ARMED on its own, including the link coming
  back.
- **The IOmcu does not ask permission** to protect hardware. It acts and
  reports afterwards.

## What CI checks

All of it runs locally:

```bash
cmake -S test/host -B test/host/build -DCMAKE_BUILD_TYPE=Debug
cmake --build test/host/build && ctest --test-dir test/host/build --output-on-failure

cmake -S test/host -B test/host/build-san -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build test/host/build-san && ctest --test-dir test/host/build-san --output-on-failure

python3 tools/coverage.py --check     # coverage floors, the STATUS.md table, the README figure
python3 tools/check_docs.py           # links, translations, SPDX, the suite list, numbers against constants
python3 tools/render_ui.py --check    # the committed screenshots match
python3 tools/gen_font.py --check
python3 tools/gen_board_drawing.py --check
python3 tools/gen_esc_profiles.py --check
python3 tools/check_sizes.py --check  # every file over 1,000 code lines is in tools/check_sizes.txt
python3 tools/check_sanitizers.py --check  # the sanitizer build's flags, on every file
python3 tools/check_protocols.py --check   # every module under protocols/ stands alone
python3 tools/security_parsers.py --check  # the parser table in SECURITY.md
python3 tools/check_formats.py        # every translated format against its call
python3 tools/check_units.py --check  # a number and its unit without a space
python3 tools/check_prose.py --check  # the writing rules, exceptions in check_prose_allow.txt
python3 tools/render_ui.py --fit      # every string fits where it is drawn
python3 -m pytest test/tools          # the tools' own tests
python3 tools/doc_check.py --check --base origin/main  # Doxygen comments, see Comments
python3 tools/mutate.py               # what the suite misses in the changed lines

# After a panel build on ESP-IDF v5.5.5 for the ESP32-P4: the image against
# the smaller of its two update slots.
python3 tools/image_slot.py firmware/panel/build
# After an IOmcu build: the stacks of its two cores, and core 0's row
# in docs/Performance.md.
python3 tools/stack_check.py --iomcu firmware/iomcu/build --check-doc
# With a pico-sdk checkout: the IO board's pin map.
python3 tools/pinmap_check.py hardware/docs/ioboard-pinmap-draft.json --sdk "$PICO_SDK_PATH"

# cppcheck 2.17.1, as in CI: build the official source with
#   tools/install_cppcheck.sh 2.17.1 <SHA-256 from ci.yml> ~/cppcheck-2.17.1
# and put ~/cppcheck-2.17.1/bin on PATH, or use a distribution package of 2.17.1.
mkdir -p /tmp/cppcheck-build
cppcheck -j "$(nproc)" --error-exitcode=1 --std=c11 \
         --cppcheck-build-dir=/tmp/cppcheck-build \
         --enable=warning,style,performance,portability \
         --inline-suppr --suppressions-list=.cppcheck-suppress --check-level=exhaustive \
         -Itest/host -Ifirmware/iomcu/include \
         $(git ls-files 'shared/**/include/*' 'protocols/**/include/*' \
             | sed 's|/include/.*|/include|; s|^|-I|' | sort -u) \
         $(git ls-files 'shared/**/*.c' 'protocols/**/*.c' | grep -v -e gfx_font -e /rp2350/) \
         $(git ls-files 'test/host/*.c')
cmake -S test/host -B test/host/build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
git ls-files 'shared/**/*.c' 'protocols/**/*.c' 'test/host/*.c' \
    | grep -v -e gfx_font -e /rp2350/ -e test/host/render_screen.c -e test/host/bench_frame.c \
    | xargs -P "$(nproc)" -n 4 clang-tidy -p test/host/build --quiet
ruff check tools/ test/tools/ test/host/   # with the docstring (D) rules
```

cppcheck also runs over `firmware/` and the pin drivers in
`protocols/*/rp2350/`, with the warning, performance and portability
classes and without the style class; the two commands are in
`.github/workflows/ci.yml`. clang-tidy does not run over `firmware/` or a
pin driver: it needs the ESP-IDF and pico-sdk headers.

A change fails if:

- coverage drops below 94% overall, or any single file below 85%;
- a C file under `shared/`, or under `protocols/` outside a module's
  `rp2350/` folder, is in neither `TRACKED` nor `DATA_ONLY` in
  `tools/coverage.py`, is not compiled into the host suite, or is linked by
  no test, or the coverage figure in `README.md` or `README-de.md` differs
  from the measurement;
- a module under `protocols/` breaks a rule of
  [Where code goes](#where-code-goes): `tools/check_protocols.py` names the
  file and the include, the header that does not compile as C11 or C++17,
  or the part that is missing;
- a screen's render changes and the committed images in `docs/img/` were not
  regenerated with `tools/render_ui.py`. Review the new images before
  committing them;
- the panel image is larger than the smaller of its two update slots, or
  the panel build lacks the IOmcu image, the touch injector or exFAT, or
  the build without the injector holds its code;
- an IOmcu core's deepest chain, one interrupt and 256 bytes exceed
  its stack ([Performance](docs/Performance.md#stacks)), or a stack figure
  or a count of calls not followed that Performance or `STATUS.md` gives
  differs from the build;
- a file over 1,000 code lines (lines that are not blank and not
  comment-only) is not in `tools/check_sizes.txt` with the reason it stays
  whole, a listed file is at or under 1,000 code lines, or a listed
  line count is outside its tolerance;
- a string overflows where it is drawn, in English or in German, and is not
  one of the overflows `tools/render_ui.py` lists as known;
- a page states a protocol version, a timing, a pin, a coverage floor, a
  stack margin, a servo test constant or an ESP-IDF or pico-sdk tag that
  differs
  from the constant in the headers, the tools or `ci.yml`. A reworded
  sentence takes its entry in `FACTS` in `tools/check_docs.py` along;
- a compile command of the sanitizer build lacks one of
  `-fsanitize=address,undefined`, `-fno-sanitize-recover=all` and
  `-fno-omit-frame-pointer`;
- a case under `test/tools/` fails;
- a C file the pull request adds has a Doxygen warning, a line it changes
  carries one, a C file it changes has no `@file` block, the tree has a
  warning that `tools/doc_check_baseline.txt` does not list, or the
  baseline lists one that the tree does not produce ([Comments](#comments));
- a source file has no SPDX (Software Package Data Exchange) line, a wiki page
  has no German counterpart, or a link goes nowhere;
- the suite list or the module tree in `STATUS.md` or `docs/Building.md`
  disagrees with what CMake builds;
- a test fails to build under `-Wall -Wextra -Werror -Wshadow -Wconversion
  -Wno-sign-conversion`, the flags of the libraries;
- clang-tidy or cppcheck reports a finding in `shared/`, `protocols/` or
  `test/host/`, or ruff reports anything. clang-tidy treats every warning as an
  error (`WarningsAsErrors`) and cppcheck exits with 1 on any finding
  (`--error-exitcode=1`). The disabled checks are listed with their reasons in
  `.clang-tidy`, `test/host/.clang-tidy` (the tests only) and
  `.cppcheck-suppress`; suppress a false positive inline with the reason, not
  by widening the list. `test/host/render_screen.c` and `bench_frame.c` are
  not in the clang-tidy run; cppcheck reads them.

The suite also runs under AddressSanitizer and UBSan
(UndefinedBehaviorSanitizer). Run a parser change that way before pushing; the
parsers are fed malformed input by design.

The `esc_parity` test of the suite needs Python 3. It reads 3000 changed
profiles with `tools/gen_esc_profiles.py` and with the card reader and
fails on a file the two read differently. A change to a rule of one of the
two changes the other in the same pull request. A failure prints the seed,
the case number and the file:

```bash
python3 test/host/fuzz_parity.py test/host/build/esc_parse_dump --seed 1 --count 1500
```

Formatting is not enforced. Match the file you are in.

## Mutation check

`tools/mutate.py` changes one line of `shared/` or of a core under
`protocols/` at a time in a copy of the
tree, builds the host suite and runs it. The copy holds `shared/`,
`protocols/`, `test/host/`, `firmware/iomcu/` and
`tools/gen_esc_profiles.py`. It takes the lines the working tree
changes against the merge base with `origin/main` (`--base` for another
ref, `--files` for every line of the files named) and makes three kinds of
change: a comparison flipped to its neighbour, an integer or an upper-case
constant beside a comparison plus 1 and minus 1, and an assignment through
a pointer, to a member or to an element dropped. A change the suite passes
with is a survivor: no test tells that line from the changed one.

```bash
python3 tools/mutate.py                       # this change
python3 tools/mutate.py --list                # the mutants, without a run
python3 tools/mutate.py --files shared/safety/heartbeat.c --max-mutants 0
```

It runs at most 60 mutants and stops starting new ones after 1200s; a run
ends within 2700s. The working tree is not written. CI runs it on every
pull request and puts the survivors in the job summary; a survivor does not
fail the job. Answer each survivor of a change with a test, or say in the
pull request why the two lines behave the same.

`mutation.yml` runs every mutant of every line of `shared/safety` and
`shared/outputs` (3032 mutants) each Sunday at 03:17 UTC and on demand. It has
no mutant limit and runs in 6 shards of files (file i of the sorted list goes
to shard i mod 6). A shard starts no mutant after 18000s, stops one build or
suite run after 600s and ends within 21000s; the mutants left are reported as
not run. The share of mutants that run within the budget on a hosted runner is
not measured. A survivor does not fail the job. The result is the job summaries
and the artifacts `mutation-full-0` to `mutation-full-5`.

## Where code goes

- `shared/` is pure C with no vendor SDK (software development kit): no ESP-IDF
  (Espressif Internet-of-Things Development Framework), no pico-sdk, no
  FreeRTOS types. It compiles into the panel firmware, the IOmcu firmware
  and the host suite from one directory. Everything that decides something
  belongs there, so it is tested on the host.
- `protocols/` holds one module per interface to external hardware: a
  wire protocol, and the RP2350 PIO (programmable input/output) program
  that puts it on a pin. A module is built in other pico-sdk projects, C
  and C++, without the rest of this repository. `protocols/<name>/` holds
  three parts:
  - a core in C11 that includes C standard headers and its own headers
    only, declares its interface inside `extern "C"`, and is tested by the
    host suite;
  - the pin driver in `protocols/<name>/rp2350/`: the `.pio` program and
    the C around it. It includes the module's core and pico-sdk headers
    only. Its header includes no pico-sdk header. A driver on a
    fixed-function block of the chip, such as an I2C block, has no `.pio`
    program, and its `README.md` says "no PIO program";
  - a `README.md`: what the module does, its constraints with numbers, its
    interface, and the CMake lines that add it to another project.

  A module's `CMakeLists.txt` builds the library `rcbench_<name>` and, in a
  pico-sdk build, the INTERFACE library `rcbench_<name>_rp2350`. It names no
  other module. A limit a module shares with this project is the module's
  own constant, and the caller holds the two equal with a `_Static_assert`.
  A module without a pin driver says "no pin driver" in its `README.md`.
  `tools/check_protocols.py` checks, per module: the parts above; that no
  include names a path with a `..` component or an absolute path, and that
  `#import` and `%:include` are read like `#include`; an `#include_next`
  fails unless it names a C standard header or, in a C file under
  `rp2350/`, a pico-sdk header, because it reaches the next file of that
  name on the include path, outside the module; a macro include fails,
  because the tool does not expand macros, and so does a token after an
  include operand; the name `__has_include` or `__has_include_next`
  outside a literal fails, as a call and as the body of a macro, and passes
  as the operand of `#ifdef`, `#ifndef`, `#elifdef` and `#elifndef`, and as
  the operand of a `defined` term (`defined NAME`, `defined(NAME)`) on an
  `#if` or `#elif` line that holds nothing but such terms, white space,
  `(`, `)`, `!`, `&` and `|`, the line being the directive's line as GCC
  reads it, which a comment or raw string literal across a line break
  continues; with any other token on that line the name
  fails (`#if X || defined(__has_include)`), because a macro can take
  `defined` into an argument and paste it to another name, and it fails
  after a `defined` joined to the identifier before it (`$defined`); a
  line of only `#` or
  `%:`, any trigraph
  (`??` followed by one of `= / ' ( ) ! < > -`) anywhere in the C (in a
  `.pio` file, the C pioasm writes) and a NUL byte anywhere in the file
  fail, because they can hide an include (ISO C11 and C17 read `??/` as a
  backslash and `??'` as `^`, and GCC reads a NUL as a blank); a leading
  UTF-8 byte order mark, form feed and vertical tab are read, CRLF and a
  lone CR end a line, a backslash followed by blanks and a newline joins
  the lines, and a quote without its closing quote runs to the line end,
  a backslash before that line end escaping nothing, as GCC reads them;
  each file is
  read under the literal rules of ISO C11 and C17 (no raw strings, no digit
  separators, no delimited or named universal character names), C++11 (raw
  strings), gnu11, gnu17 and gnu++11 (raw strings and delimited and named
  universal character names),
  C23 (digit separators), C++14 to C++20 (raw strings and digit
  separators), and gnu23, C++23 and gnu++14 or later (all three), and an
  include found under any of them counts; a raw string literal is one token, with the line continuations
  inside it kept, and a number runs through digit separators (`1'000`,
  but not a `'` before a `.`, a `$`, a universal character name or a
  character outside ASCII), exponent signs (`1e-x`), `$`, universal
  character names (`\u00e9`, `\U000000E9`, and in gnu11, gnu17, gnu23,
  gnu++11 or later and C++23 also `\u{e9}` and
  `\N{LATIN SMALL LETTER E WITH ACUTE}`) and the characters outside
  ASCII that GCC 13 takes into an identifier, the ranges of ISO C11
  Annex D.1 (`1$'a` in C23), and starts at an ASCII digit (0 to 9) after
  any character but a letter, a digit, `_`, `$` and a character of these
  ranges, and in gnu11, gnu17, gnu23, gnu++11 or later and C++23 not after
  a delimited or named universal character name (`é1`, `€1`, `\u{e9}1`
  and `٠1` with the Arabic-Indic digit U+0660 are identifiers); a
  character outside ASCII and outside the ranges ends a number and a name,
  and a number or raw string literal starts after it, as in C (in C23
  `×1'a` is a number, and `ⸯR"(...)"` is a raw string in gnu11); the C of
  a `.pio` file is what pioasm 2.3.0 writes into the generated header, the `% c-sdk {` blocks after the first
  `.program`, found as pioasm lexes the file: a `;` or `//` comment runs to
  the line end, a `;` inside a `/* */` comment ends nothing, a block name
  is trimmed of spaces and tabs and an empty one is c-sdk, a lone CR is a
  character of its line, and a block ends only at `%}` in column 0 with
  spaces, tabs or CRs after it; that
  the `README.md` has a heading with "Interface", a heading with
  "Trademark" in it where it names a brand from the tool's `BRANDS` list,
  and the words "no pin driver" where the module has no `rp2350/` folder,
  and "no PIO program" where its `rp2350/` folder has no `.pio` file;
  the Interface heading, "no pin driver" and "no PIO program" count
  outside fenced code
  blocks and HTML comments only, while a brand is matched anywhere in the
  file, in any case, in paths, addresses, code blocks and comments
  included, also inside an identifier, file name or number such as
  `kst_session_t`, `www.futaba.com` or `2kst`, but not next to a letter:
  not inside a longer word such as `KSTART` and not in a CamelCase
  identifier such as `KstSession`; and that every `_Static_assert` in the C files under
  `shared/` and `firmware/` on a module constant is listed in the tool's
  `ASSERTED` table and every listed one exists. Assertions under `test/`
  are not checked. The CMake include directories and link targets of a
  module are not checked: only `REQUIRES` and `rcbench_` library names are
  read. A `__has_include` that a macro pastes together with `##`
  (`CAT(__has_, include)`) passes, and GCC 13 evaluates it as the call.
  In C++ an identifier right after a closing quote is a literal suffix, so
  `""R"(` and `''R"(` open no raw string there, while the GNU dialects of
  C read one; the tool reads them as C does, so an include that such a
  sequence hides in C and that C++11 or later reads is not found
  (`R"(a" /* ")"`, then `""R"(" '/*)" /*'`, hides the include on the next
  line from the tool, and GCC 13 with `-std=c++17` reads it).
  The SPDX line of the module's `.c` and `.h` files is checked by
  `tools/check_docs.py`.
- Everything specific to this project stays outside `protocols/` and calls
  the module: link pages, the binding of an output to a pin, switching to ARMED, STOP,
  the stores and the screens.
- `firmware/` is hardware access and wiring. A rule in a firmware file belongs
  in `shared/` or in a protocol core, with a test.

## The active output

A screen, tab, overlay, prompt or automatic run that drives an output names
that output while it is shown: the pin, the port with its pin, or the supply
with its voltage. A screen that talks to a module over a serial link names
the link's pins as well. Examples of the form, not texts of any screen: `GPIO 4`,
`PROG PORT GPIO 13`, `PD MINI 7.40V`. Nothing bound, a binding not yet read and
a target out of reach show as a warning. The label reads the state the drive
path reads, not the setting that asks for it (`prog_link_target()`, not
`SET_SERVO_TARGET`). A screen added or changed meets the rule and comes
with a host test that holds the label and the drive path's state
equal. A user who cannot tell from the screen what it drives meets a defect
of the screen. The screens that do not meet it are listed in
[STATUS.md](STATUS.md#open-items).

## Style

- Four spaces, no tabs. Match the file you are in.
- Test names are sentences: `a_refused_write_changes_nothing`.
- Comments state the constraint or decision the code depends on, in present
  tense. No history of how the code came to be.

## Comments

C files under `shared/`, `protocols/`, `firmware/panel/`, `firmware/iomcu/`
and `testbench/touch-injector/core/` carry Doxygen comments. The generated
tables (`gfx_font*`, `*_gen.c`, `board_drawing_*`) do not.

- Each file opens with a `@file` block that has a `@brief`.
- Each public function, type and macro group has a `/** */` block with
  `@brief`, a `@param` for every parameter and a `@return` for a non-void
  result.
- Each file-scope variable, `static` ones too, has a block with a
  `@brief`; a struct member has one or a trailing `/**< */`.
- A `static` helper in a `.c` file has at least a `@brief`.
- Every `/**` or `/*!` block has a `@brief`, except a member's trailing
  `/**< */`, a group block (`@{`, `@}`, `@name`) and the `@file` block.
- A brief is an explicit `@brief`. The first sentence of a block is not
  one (`JAVADOC_AUTOBRIEF = NO`).

```c
/**
 * @file
 * @brief Clamp a servo pulse to the endpoints of its channel.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @brief Clamp a pulse width to a channel's endpoints.
 * @param ch The channel, 0 to 15.
 * @param us The requested pulse width, in microseconds.
 * @return The pulse width the channel drives, in microseconds.
 */
uint16_t servo_clamp(uint8_t ch, uint16_t us);
```

Python modules, public functions, classes and methods carry a Google-style
docstring: a summary line, a blank line, then `Args:`, `Returns:` and
`Raises:` as they apply.

```python
def read_baseline(path):
    """Read a baseline file.

    Args:
        path: The file; a missing file is an empty baseline.

    Returns:
        Each line with the number of times it occurs.
    """
```

Two checks hold the rule. They apply to new and touched code and list
the gaps of the rest of the tree, so an old gap does not fail a pull
request and a new one does.

- `tools/doc_check.py --check` runs Doxygen 1.18.0 with the `Doxyfile`
  and compares its warnings with `tools/doc_check_baseline.txt`. A
  baseline line is a file and a message without the line number; code
  that moves inside a file changes no line. In a pull request CI passes
  `--base origin/<base branch>`, and these warnings fail, listed or not:
  - every warning in a C file the pull request adds;
  - in a C file it changes, every warning on a line it adds or rewrites
    (`git diff -U0`, new side). A warning stands for the line Doxygen
    reports and the comment block next to it, so a rewritten `@brief`
    or `@param` counts for the function below it;
  - a missing `@file` block or `@brief` in a C file it changes. Doxygen
    reports nothing inside a file without the block;
  - a `/**` or `/*!` block without `@brief` in a new file or on a changed
    line. Doxygen takes such a block as detailed text and warns about
    nothing.

  A renamed or moved file counts as new, so splitting a file documents
  every function that moves. An old gap on a line the pull request does
  not touch stays listed.
- ruff's D rules (pydocstyle, Google convention) in `ruff.toml`. A file
  not listed under `[lint.per-file-ignores]` passes every D rule; a listed
  file is excused the codes on its line.

`doc_check.py` and `tools/install_doxygen.sh` run on Linux on x86-64, as
in CI. On Windows (Git Bash), `doc_check.py` runs with a Doxygen 1.18.0
on PATH or named by `--doxygen`; `install_doxygen.sh` installs the Linux
binary only.

When a gap is fixed, its baseline line goes in the same pull request:

```bash
# Doxygen 1.18.0, as in CI (another release stops doc_check.py with exit 2)
tools/install_doxygen.sh 1.18.0 <DOXYGEN_SHA256 from ci.yml> ~/doxygen-1.18.0
python3 tools/doc_check.py --update --doxygen ~/doxygen-1.18.0/bin/doxygen
git diff tools/doc_check_baseline.txt    # removed lines only
```

For Python, delete the fixed codes from the file's line in `ruff.toml`,
and the line when no code is left, then run `ruff check .`.

`mkdir -p build/doxygen && doxygen` at the repository root writes the
HTML to `build/doxygen/html/`; GitHub Pages publishes it as the
[C API (application programming interface) reference](https://subtilitas.github.io/rcbench/api/).

## Writing

Applies to the wiki, the READMEs, `STATUS.md`, code comments, commit messages,
pull requests and issues.

- Describe the system as it is, in present tense. No development history, no
  "we", no "previously" or "now". Version history is in `CHANGELOG.md` and git.
- No self-assessment. A defect that affects a user is documented as a current
  limitation with the condition that triggers it.
- No marketing words (powerful, seamless, robust, comprehensive, simply, just,
  easy). No exclamation marks, no emoji.
- Facts carry numbers and units: pin numbers, voltage ranges, timings, buffer
  sizes, error codes. An adjective that could be a number is a missing number.
- Short sentences, one idea each. Expand every acronym on first use in each
  document.
- Lead with what a thing does and its constraints, then how to use it. A
  command or a code block beats a paragraph describing one.
- Unknowns are stated as unknown ("not measured", "untested above 40V"), never
  asserted and never omitted.
- Commit messages and pull request bodies: the change and its effect, in the
  imperative mood, without the debugging path.

## Documentation

`docs/` is published to the wiki after CI passes on a push to `main` that
touches it, in English and German. Every page needs both, linked by the switch at the top; the check
enforces it. Write German as German rather than translating sentence by
sentence, and keep technical vocabulary in English: protocol names, mode names,
register and field names.

The wiki is the manual. `STATUS.md` is the record of what is built, what is
open and what is settled. `CHANGELOG.md` is the version history;
`CHANGELOG-0.9.md` holds versions 0.9.1 and older.

## Pull requests

One change per pull request, with CI green. State the change and its effect.
