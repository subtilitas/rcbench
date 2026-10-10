# Contributing to rcbench

The rules below are enforced by CI (continuous integration) where a machine can
enforce them. Read this before a large change.

## Licensing rule

**Protocols are implemented from published specifications, never from another
implementation.**

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

A protocol with no published description is not implemented. BLHeli_32's
parameters and JETI's remote configuration are recorded as not planned for this
reason.

## Safety-relevant code

Anything that can make an output move is held to these rules; the reasoning is
in [Safety](docs/Safety.md).

- **Fail safe by absence.** When a signal stops, stop driving. Never require a
  message to arrive in order to be safe.
- **Refuse and clamp are different answers.** A configuration mistake (an
  endpoint no servo can take) is refused, atomically, leaving the previous
  value. A command outside its range is clamped.
- **A stop latches.** Nothing re-arms on its own, including the link coming
  back.
- **The coprocessor does not ask permission** to protect hardware. It acts and
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
python3 tools/frame_cost.py --check-doc
python3 tools/gen_font.py --check
python3 tools/gen_board_art.py --check
python3 tools/gen_esc_profiles.py --check
python3 tools/check_sanitizers.py --check  # the sanitizer build's flags, on every file
python3 tools/check_formats.py        # every translated format against its call
python3 tools/render_ui.py --fit      # every string fits where it is drawn
python3 tools/research/session.py check  # the research scripts against their plan
python3 -m pytest test/tools          # the tools' own tests
python3 tools/mutate.py               # what the suite misses in the changed lines

# After a panel build, on ESP-IDF v5.4 for --check-doc: task stacks, and the
# table in docs/Performance.md.
python3 tools/stack_check.py firmware/panel/build --check-doc
# After a coprocessor build: the stacks of its two cores, and core 0's row
# in docs/Performance.md.
python3 tools/stack_check.py --iomcu firmware/iomcu/build --check-doc
# With a pico-sdk checkout: the IO board's pin map.
python3 tools/pinmap_check.py hardware/docs/pinmap.json --sdk "$PICO_SDK_PATH"

cppcheck --error-exitcode=1 --std=c11 --enable=warning,style,performance,portability \
         --inline-suppr --suppressions-list=.cppcheck-suppress --check-level=exhaustive \
         $(git ls-files 'shared/**/include' | sed 's|^|-I|' | sort -u) \
         $(git ls-files 'shared/**/*.c' | grep -v gfx_font)
cmake -S test/host -B test/host/build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p test/host/build $(git ls-files 'shared/**/*.c' | grep -v gfx_font)
ruff check tools/ test/tools/ test/host/
```

cppcheck also runs over `firmware/`, with the warning, performance and
portability classes and without the style class; the two commands are in
`.github/workflows/ci.yml`. clang-tidy does not run over `firmware/`: it
needs the ESP-IDF and pico-sdk headers.

A change fails if:

- coverage drops below 94% overall, or any single file below 85%
  (`stub_screen.c` is exempt by name);
- a C file under `shared/` is in neither `TRACKED` nor `DATA_ONLY` in
  `tools/coverage.py`, is not compiled into the host suite, or is linked by
  no test, or the coverage figure in `README.md` or `README-de.md` differs
  from the measurement;
- a screen's render changes and the committed images in `docs/img/` were not
  regenerated with `tools/render_ui.py`. Review the new images before
  committing them;
- drawing code exceeds its cache-line ceiling
  ([Performance](docs/Performance.md));
- a panel task's deepest call chain exceeds its stack less 1024 bytes, or
  a coprocessor core's deepest chain, one interrupt and 256 bytes exceed
  its stack ([Performance](docs/Performance.md#stacks));
- a string overflows where it is drawn, in English or in German, and is not
  one of the overflows `tools/render_ui.py` lists as known;
- a page states a protocol version, a timing, a pin, a coverage floor, a
  stack margin, a servo test constant or a frame-cost ceiling that differs
  from the constant in the headers, the tools or `ci.yml`. A reworded
  sentence takes its entry in `FACTS` in `tools/check_docs.py` along;
- a compile command of the sanitizer build lacks one of
  `-fsanitize=address,undefined`, `-fno-sanitize-recover=all` and
  `-fno-omit-frame-pointer`;
- a case under `test/tools/` fails;
- a source file has no SPDX (Software Package Data Exchange) line, a wiki page
  has no German counterpart, or a link goes nowhere;
- the suite list or the module tree in `STATUS.md` or `docs/Building.md`
  disagrees with what CMake builds;
- clang-tidy, cppcheck or ruff report anything. Every finding is an error. The
  disabled checks are listed with their reasons in `.clang-tidy` and
  `.cppcheck-suppress`; suppress a false positive inline with the reason, not
  by widening the list.

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

`tools/mutate.py` changes one line of `shared/` at a time in a copy of the
tree, builds the host suite and runs it. The copy holds `shared/`,
`test/host/`, `firmware/iomcu/` and `tools/gen_esc_profiles.py`. It takes the lines the working tree
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

It runs at most 60 mutants and stops starting new ones after 1200 s; a run
ends within 2700 s. The working tree is not written. CI runs it on every
pull request and puts the survivors in the job summary; a survivor does not
fail the job. Answer each survivor of a change with a test, or say in the
pull request why the two lines behave the same.

## Where code goes

- `shared/` is pure C with no vendor SDK (software development kit): no ESP-IDF
  (Espressif Internet-of-Things Development Framework), no pico-sdk, no
  FreeRTOS types. It compiles into the panel firmware, the coprocessor firmware
  and the host suite from one directory. Everything that decides something
  belongs there, so it is tested on the host.
- `firmware/` is hardware access and wiring. A rule in a firmware file belongs
  in `shared/` with a test.

## Style

- Four spaces, no tabs. Match the file you are in.
- Test names are sentences: `a_refused_write_changes_nothing`.
- Comments state the constraint or decision the code depends on, in present
  tense. No history of how the code came to be.

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
- Unknowns are stated as unknown ("not measured", "untested above 40 V"), never
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
open and what is settled. `CHANGELOG.md` is the version history.

## Pull requests

One change per pull request, with CI green. State the change and its effect.
