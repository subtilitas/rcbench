# rcbench hardware

The boards, and the parts that go on them.

Nothing here is built yet. This half of the project exists on paper while the
firmware runs on modules and dev boards, and the paper is worth keeping because
**the parts decide what the firmware can honestly display.** A screen that
plots battery current is a screen that invents numbers until a sense resistor
and a monitor IC are fitted, and [rcbench's running
record](../STATUS.md) marks every one of those screens as inventing.

> The rule that settles arguments here: **a part is not chosen until it is
> chosen at a vendor.** A part right on merit and unbuyable is not a choice, it
> is a wish — see [Sourcing](docs/Sourcing.md) for what that cost the INA228.

## What is in here

| | |
| --- | --- |
| [Power](docs/Power.md) | the four ICs the power path needs, why each, and what each vendor had |
| [Sourcing](docs/Sourcing.md) | how to get a truthful stock figure, and the two ways of getting a false one |
| [Where things stand](STATUS.md) | what is decided, what is open, what is deliberately not going to be |

## This is destined to leave

`hardware/` is expected to become its own repository. It is laid out that way
already — its own README, its own running record, its own `docs/` — so that the
split is a `git mv` and a remote rather than a reorganisation.

Two consequences while it still lives here:

- **It is not wired into rcbench's CI.** The wiki workflow mirrors the
  top-level `docs/` only, and `tools/check_docs.py` audits that same directory
  plus the two root pages. Pages under `hardware/docs/` are checked by nobody,
  so their links and their numbers are on the author.
- **It is not bilingual yet.** The wiki is English and German because it is the
  manual and a manual has readers. This is a design record with one reader, and
  translating it now would double the cost of every edit during the period it
  changes most. When the boards exist and this becomes a manual, it gets the
  same treatment as `docs/`.

## Licence

MIT, the same as the rest of the tree — see [LICENSE](../LICENSE). Whatever
this becomes when it is split out inherits that, hardware designs included.
