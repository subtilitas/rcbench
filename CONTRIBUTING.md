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

python3 tools/coverage.py --check     # coverage floors, and the STATUS.md table
python3 tools/check_docs.py           # links, translations, SPDX, the suite list
python3 tools/render_ui.py --check    # the committed screenshots match
python3 tools/frame_cost.py --check-doc
python3 tools/gen_font.py --check

cppcheck --error-exitcode=1 --std=c11 --enable=warning,style,performance,portability \
         --inline-suppr --suppressions-list=.cppcheck-suppress --check-level=exhaustive \
         $(git ls-files 'shared/**/include' | sed 's|^|-I|' | sort -u) \
         $(git ls-files 'shared/**/*.c' | grep -v gfx_font)
cmake -S test/host -B test/host/build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p test/host/build $(git ls-files 'shared/**/*.c' | grep -v gfx_font)
ruff check tools/
```

A change fails if:

- coverage drops below 94% overall, or any single file below 85%
  (`stub_screen.c` is exempt by name);
- a screen's render changes and the committed images in `docs/img/` were not
  regenerated with `tools/render_ui.py`. Review the new images before
  committing them;
- drawing code exceeds its cache-line ceiling
  ([Performance](docs/Performance.md));
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

Formatting is not enforced. Match the file you are in.

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

`docs/` is published to the wiki on every push to `main`, in English and
German. Every page needs both, linked by the switch at the top; the check
enforces it. Write German as German rather than translating sentence by
sentence, and keep technical vocabulary in English: protocol names, mode names,
register and field names.

The wiki is the manual. `STATUS.md` is the record of what is built, what is
open and what is settled. `CHANGELOG.md` is the version history.

## Pull requests

One change per pull request, with CI green. State the change and its effect.
