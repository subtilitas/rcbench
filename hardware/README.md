# rcbench hardware

The boards and the parts on them. No board exists; the firmware runs on modules
and development boards. This directory records the part decisions the firmware
depends on.

| Page | Content |
| --- | --- |
| [Power](docs/Power.md) | the four ICs (integrated circuits) of the power path, with the alternatives and the stock at two vendors |
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
