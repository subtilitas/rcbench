# rcbench hardware

The boards and the parts on them. No board exists; the firmware runs on modules
and development boards. This directory records the part decisions the firmware
depends on.

| Page | Content |
| --- | --- |
| [IO (input/output) board](docs/IOBoard.md) | the specification of the coprocessor board: what it does, with the numbers the tree fixes (draft) |
| [Research](docs/Research.md) | the plan for round 1 of the component research, its sourcing rules and the open questions |
| [Power](docs/Power.md) | four ICs (integrated circuits) of the power path, with their alternatives and the stock at two vendors on 2026-09-01. The TPS55285 and the INA238 are decided; the BQ25887 and the INA745A are candidates for round 1 of [the research](docs/Research.md) |
| [Sourcing](docs/Sourcing.md) | how to obtain a stock figure from a vendor's own API (application programming interface), and which sources are not reliable |
| [Where things stand](STATUS.md) | decided, open, not planned, and the order of work |

## Rules

- A part is not chosen until it is available at a vendor. Availability is
  checked at the vendor's own API or page, not at a mirror.
- A footprint is not committed on one vendor's stock. Either the part is
  available at both vendors or the board tolerates the alternative.

## Layout

`hardware/` is laid out as a repository of its own (README, STATUS, `docs/`) so
it can be split out later with a `git mv`. While it lives here:

- `tools/check_docs.py` checks its links and anchors, and nothing else.
- The pages are English only. The wiki is bilingual because it is a manual;
  this is a design record and is translated when the boards exist.

## Licence

MIT (Massachusetts Institute of Technology), as the rest of the tree:
[LICENSE](../LICENSE).
