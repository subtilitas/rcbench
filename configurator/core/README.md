# Configurator core

The core of the rcbench bench configurator: one data file, one resolver and the
checks on both. It has no runtime dependency and no build step. Node.js 24 runs
the TypeScript files directly.

The data describes release 0.16.0. Every record carries a source: a file and
line at the tag `v0.16.0`, a section of the build guide, or a photo taken for
the build guide. A fact that no source confirms is marked as unknown in the
data and is not asserted.

## Files

| File | Content |
| --- | --- |
| `../contract.ts` | The types shared by the core and the page. |
| `catalogue.ts` | The data: 19 parts, 40 pads, 53 wires, 11 functions, 5 rules, 18 check entries, 5 stages, 7 safety notes, 8 pin defaults. All text is in English and German. |
| `resolve.ts` | `resolve(catalogue, selection)` returns the setup: parts with the reason for each, functions, options, wires, check entries. |
| `cond.ts` | Evaluates the condition of a wire, a rule or a check entry. |
| `index.ts` | The entry point: `CATALOGUE`, `DEFAULT_OPTIONS`, `resolve` and the default export `core`. |
| `tools/check.ts` | The static checks on the data. |
| `tools/coverage.ts` | Measures line coverage and keeps the figure below current. |
| `test/` | The test suite for `node:test`. |

## Resolver

`resolve` is pure: no input or output, no clock, no random number. It returns a
setup for every input. Ids that the catalogue does not hold, parts that are not
selectable and option values outside the type go to `ignored`. Output arrays
are in catalogue order.

The order of work:

1. Locked parts.
2. Parts needed by the chosen functions.
3. Owned parts.
4. Options forced by a chosen function. Functions are read in catalogue order; the first one that forces an option names it.
5. Rules, once each, in array order.
6. Wires and check entries whose condition holds.
7. Functions: `enabled` is true when no needed part is missing and every forced option holds.

`conflicts` is always empty: the data holds no pair of parts that exclude each
other.

## Commands

Run from `configurator/core` with Node.js 24.

```sh
npm run check                 # static checks; the tree at v0.16.0 is read when git and the tag are present
npm run check -- --strict     # as above; fails when git or the tag is absent
npm run check -- --wires      # prints the wire table as Markdown
npm test                      # the test suite
npm run coverage              # runs the suite and writes the figure into this file
npm run coverage -- --check   # fails when the figure in this file differs; writes nothing
```

Exit codes of `check`: 0 no error, 1 at least one error, 2 unknown argument.
A warning does not change the exit code. Exit codes of `coverage`: 0 figure
current or written, 1 drift or a failing suite, 2 unknown argument.

`--strict` needs the tag `v0.16.0` in the clone. A shallow clone without tags
fails it.

The type check needs the TypeScript compiler and the Node.js type
declarations. Neither is a dependency of this folder:

```sh
npm install --no-save --no-package-lock --prefix "$TMP_TOOLS" typescript@7.0.2 @types/node@24.19.2
"$TMP_TOOLS/node_modules/.bin/tsc" --noEmit -p tsconfig.json \
  --typeRoots "$TMP_TOOLS/node_modules/@types" --types node
```

## Checks

`tools/check.ts` reports each finding with the id of the record.

| Check | Fails when |
| --- | --- |
| Ids | An id is used twice, or the wire numbers are not unique and ascending. |
| Wire ends | A wire end is not a pad or a declared pin of a part, or a power wire ends on a signal pad. |
| References | A function, rule, condition or stage names an id that does not exist. |
| Text | A string is empty in one language, starts or ends with white space, or holds an exclamation mark, a listed marketing word, a character outside the allowed set or a tag other than `<b>` in a safety note. |
| Sources | A record has no source, a file or line does not exist at the tag, the line is empty, or a build guide section or photo is not known. |
| Pins | The pin table breaks one of the 7 rules below, or a wire and the pin table disagree. |
| Routes | A slot order is used twice in a column of the drawing, or a lane number is outside its range (left 0 to 16, right 0 to 20, outer 0 to 7, channel 0 to 4). |
| Tree | The pads, the reserved pins, the heartbeat pin or a pin default differ from the firmware sources at the tag. |

A release tag newer than `v0.16.0` is a warning.

The 7 pin rules:

1. A signal is on a General Purpose Input/Output (GPIO) pin of its allowed set, and not on a reserved pad (GP8 to GP12).
2. A GPIO carries one signal.
3. The clock line of the sensor bus (SCL) is the data line (SDA) plus 1. SDA is one of GP0, GP4, GP6, GP14, GP16, GP18, GP20, GP26, or -1 for off.
4. Two outputs do not share a pulse-width modulation (PWM) slice.
5. An output does not share a compare register with another signal. The partner of GP0 to GP5 is the same pin plus 16.
6. The tap pin is not GP26, GP27 or GP28.
7. The heartbeat pin is GP3 and fixed.

## Tests

The suite runs 2 full sweeps and 1 seeded sample through the resolver:

- 2048 sets of owned parts times 8 option combinations: 16384 setups.
- 2048 sets of functions times 8 option combinations: 16384 setups.
- 2000 mixed selections from a fixed seed.

Each setup is checked for: the same result on a second call, no exception, no
pin of a part on two nets, no power wire on a signal pad, every wire end a
declared pin of a part that is present, catalogue order of every output array.
The sweep refuses a catalogue with more than 16 selectable parts or 16
functions.

Further tests cover the empty selection, the full selection, each part and
each function alone, unknown ids, a missing German string, a broken source
anchor and a data file that differs from the firmware tables.

## Coverage

Line coverage of the resolver, measured by `node --test --experimental-test-coverage`.
`tools/check.ts` and `catalogue.ts` are not part of the figure.

<!-- coverage:begin -->
| File | Lines run | Lines | Line coverage |
| --- | ---: | ---: | ---: |
| `resolve.ts` | 184 | 184 | 100.0 % |
| `cond.ts` | 51 | 51 | 100.0 % |
<!-- coverage:end -->

## Unknown in the data

- Pins marked unconfirmed: `CANH` and `CANL` of the panel, `LP` of the INA3221 board, `GPO` of the AS5600 board, `NC1` and `NC2` of the emergency stop switch, and the 4 pins of the USB breakout. No source at `v0.16.0` shows their position.
- Parts with the state "not run on hardware": INA3221 board, AS5600 board, pull-up resistors, INA228 board, emergency stop switch, stick simulator lead, tap lead, USB breakout.
- Functions with the state "not run on hardware": servo current, angle, ESC power, stick programming, tap, emergency stop.
- The pins for the servo signal (GP2) and the PD board serial lines (GP14, GP15) are proposals of this data file. The firmware default for both PD pins is -1 (off), and the repository names no servo pin.
- Check entries of kind `unknown` state each open point in the text of a setup.
