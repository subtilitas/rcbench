# Hash corpus

Links to the configurator, each with the setup the page makes of it. The
corpus is the compatibility contract of the URL hash: a link that is in the
corpus keeps its meaning.

| File | Holds |
|---|---|
| `hashes.json` | One entry per line: `name`, `hash`, `why`, `expect`. 164 entries at the start. |
| `wires.json` | The wire numbers of each entry under the catalogue of this commit. |
| `corpus.ts` | Types, readers and `outcome(hash)`: what the page does with a hash. |
| `tool.ts` | Appends an entry and rewrites `wires.json`. |
| `corpus.test.ts` | Holds the hash codec and the resolver to each entry, under `npm test`. |
| `../../e2e/matrix/corpus.spec.ts` | Holds the built page to each entry, under `npm run e2e:matrix`. |

## What an entry stores

`expect` holds: ticked functions, ticked parts, options, language, kinds of the
notices, the ids a notice names, the hash the page writes on the first change,
the parts of the setup and the functions the setup enables. An entry whose
hash names a row token under `h` also holds `have`: the "have it" ticks of the
parts list that the page keeps. The language is
the one for a browser that asks for English.

Entries by name:

| Prefix | Count | Input |
|---|---:|---|
| `page-` | 61 | Hashes the page writes. 12 of them are added after the start: 2 for the option token `servo5v`, 3 for the function `servoprog`, 1 for the part `dcsup`, 3 for the key `h`, 3 for the key `m` of the motor simulator. |
| `hand-` | 11 | Well-formed hashes in another spelling: other key order, repeated ids, default tokens. 1 of them is added after the start: the default values of the key `m`. |
| `bad-empty`, `bad-number-sign`, `bad-two-number-signs` | 3 | No hash, the number sign alone, two number signs. |
| `bad-cut-` | 14 | A link cut short. |
| `bad-version-` | 19 | Another version, no version, the version twice. 1 of them, the key `h` without a version, is added after the start. |
| `bad-id-` | 31 | Unknown ids, ids of the wrong kind, ids in upper case. 8 of them are added after the start: 6 for the key `h` (an unknown row, a row of another variant, of a part outside the setup, of an owned part, of a part the visitor brings, and malformed tokens), 2 for the key `m` (values the page does not offer, a value twice). |
| `bad-limit-` | 7 | 64 and 65 ids, 2000 and 2001 characters, 3 hashes of 64 kB. |
| `bad-percent-` | 20 | Percent-encoding: encoded separators, broken escapes, a byte that is no UTF-8, a plus sign. |
| `bad-shape-` | 20 | Input that is no key-value list: script text, repeated separators, other separators. |

Every malformed entry gives a setup without an exception. An entry whose hash
carries no readable selection gives the base setup.

A hash of 64 kB is stored as a recipe `{ prefix, unit, count, suffix }`.

## Rules

1. An entry is never edited, moved or removed. `corpus.test.ts` holds a digest
   of the first 164 entries and fails on a change.
2. A change of the page that gives an entry another `expect` breaks links
   that are in use. The change needs a new hash version, a decoder for
   version 1 and a corpus of its own.
3. A function or a part added to the catalogue changes no entry. The tests
   compare each entry in the ids the corpus starts with (`ids` in
   `hashes.json`). An entry whose hash names an id that the catalogue adds
   later is held to the rules of a well-formed result only.
4. A function or a part of `ids` stays in the catalogue.
5. A test object (`servo`, `esc`) is no part to tick. 8 entries name one
   under `p=`. The page reads it as the tick of the function that drives the
   test object alone: `p=servo` as `f=servo`, `p=esc` as `f=esc`. The tests
   hold such an entry to the stored parts, enabled functions, options,
   language and notices. The hash the page writes names the function in place
   of the part and gives the same parts and wires.
6. An option key added to the catalogue changes no entry. The tests compare
   the options in the keys the entry stores. A key the entry does not store
   has its default value.

## Add an entry

```bash
cd configurator
npm run corpus -- add <name> "<why>" '<hash>'
npm test
```

`<name>` is lower case letters, digits and hyphens. `<why>` is one sentence:
what the entry is in the corpus for. The tool computes `expect` from the code
of the commit, appends the entry and rewrites `wires.json`. It refuses a name
or a hash that the corpus holds. Read the new `expect` before the commit: the
tool stores what the page does, right or wrong.

Add an entry for each new function, each new part, each new option token and
each new key of the hash.

## When a wire of the catalogue changes

`wires.json` is not part of the contract. A wire that is added, removed or
renumbered in `core/catalogue.ts` changes it. Rewrite it and read the
difference:

```bash
npm run corpus -- wires
git diff test/corpus/wires.json
```

The difference shows which stored links get another wiring.
