# rcbench hardware

The boards and the parts on them. No board exists; the firmware runs on modules
and development boards. This directory records the part decisions the firmware
depends on.

| Page | Content |
| --- | --- |
| [IO (input/output) board](docs/IOBoard.md) | the specification of the coprocessor board: what it does, with the numbers the tree fixes (draft) |
| [Research](docs/Research.md) | the plan for round 1 of the component research, its sourcing rules, the owner's answers, and the dates and commits of its run (2026-09-28 to 2026-09-30) |
| [Parts](docs/Parts.md) | one row per part round 1 selected, 49 parts: its function, maker, LCSC number, package, JLCPCB and Digi-Key stock of 2026-09-29, placements, second source, lifecycle and alternate. `tools/jlc_stock.py` reads the stock again |
| [Control](docs/Control.md) | research group A, R1 to R4: the RP2354B's support, CAN (Controller Area Network), the safety gate and the output buffers. The choice, the alternatives, the stock, the reason, the budget of the RP2354B from the research's cross-category check, and what is not known |
| [Supply](docs/Supply.md) | research group B, R5 to R8: the power input and rails, the servo supply and port switches, the charger and bench pack, the monitors and shunts. The choice, the alternatives, the stock, the reason, the power budget from the research's cross-category check, and what is not known |
| [Sensing](docs/Sensing.md) | research group C, R9 to R13: the cell monitor, the accelerometer converter, the rotation and vibration front ends, temperature, the external I²C (Inter-Integrated Circuit) ports, the store and the load cells. The choice, the alternatives, the stock, the reason, and what is not known |
| [Power](docs/Power.md) | four ICs (integrated circuits) of the power path, with their alternatives and the stock at two vendors on 2026-09-01, and round 1's result for each: the TPS55285 refuted at 4.0 A continuous and kept by the owner, four a board with the adjustable supply, each with a temperature shutdown, the INA238 short of stock and the INA228 fixed by the owner in its place, the BQ25713 selected as the charger and the INA3221 kept for the port monitors |
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
