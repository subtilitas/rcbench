# Where the hardware stands

The running record for `hardware/`. Same job as [the project's
STATUS.md](../STATUS.md) and the same rule: **what it asserts is a claim to
verify, not a fact to inherit.** Stock figures in particular have a date on
them and rot from it.

**Status — nothing exists.** No schematic, no layout, no board, no part bought.
What exists is a settled opinion about four ICs and a documented reason for
each, which is enough to stop the firmware from being designed against parts
that cannot be had.

## What is decided

| | |
| --- | --- |
| The servo supply is a **TPS55288** | Its I²C current limit sets a sense voltage across an external shunt rather than a current, so the ceiling moves with the resistor. The alternatives cap at 6.35 A in silicon. [Power](docs/Power.md) |
| The motor monitor is an **INA238** | 85 V and sixteen bits, in the same VSSOP-10 and the same pin order as the INA228 that cannot be bought. Twenty bits was never needed at 300 A. [Power](docs/Power.md) |
| **One footprint takes both** monitors | INA228 and INA238 are pin-identical and distinguishable at run time by DEVICE_ID, so the board survives either being the one in stock. [Sourcing](docs/Sourcing.md) |
| Stock comes from **the vendor's own API** | A mirror said 1046 of a part the vendor said 29 of, oversold. [Sourcing](docs/Sourcing.md) |

## What is open

| | Where it stands |
| --- | --- |
| **Which supply feeds the servo rail** | The requirement reads "up to 5.5 V and up to 8.4 V, 4–8 A", and there are two readings: two output settings, or an input and an output. The TPS55288 survives both. The *bench* does not — 8.4 V at 8 A is 67 W, which off a 5 V input is nearly 15 A on the primary side, at the part's inductor limit and thermally unpleasant. If the input really is ≤5.5 V then the deliverable is about 4 A, and the requirement wants restating rather than the part wants changing. **This is the one that should be settled first**, because it decides an input connector and a heatsink, not a part number. |
| **Whether 2 A of charging is enough** | No single die charges 2S above 2 A and balances it. At 2 A it is one BQ25887. At 3 A it is a BQ25798 plus a balancer, three parts instead of one, and balancing drops from 400 mA to 50 mA per cell — from inside one charge to overnight. Nobody has said whether 3 A was a requirement or a range. |
| **INA745A against a second INA238** | The INA745A has its shunt inside it, which removes the whole class of Kelvin-sense mistakes from the servo rail. A second INA238 instead would mean one driver and one footprint across both monitors, which on a tree with a per-file coverage floor is worth more than it looks. Not a parts question — a firmware-surface question. |
| **The shunt for 300 A** | Only the class is settled: busbar-style, 50–100 µΩ, 4.5–9 W at 300 A. The largest four-terminal SMD parts stop at 0.2 mΩ and would burn 18 W. No part number, no footprint, no supplier. |
| **Everything about layout** | Isolation, how a 300 A path and a 3.3 V I²C bus share a board, connector choice, thermal. Not started, and not startable before the two questions above. |

## What is deliberately not going to be

- **A hall-effect sensor for the 300 A path.** It would avoid a 9 W shunt and
  the layout problem that comes with it, and it would put a gain and an offset
  drift between the bench and its headline number. The bench's whole claim is
  that its numbers are measured rather than estimated; a shunt is the honest
  instrument and the dissipation is the price.
- **A twenty-bit motor monitor.** Not from principle — the INA228 is the better
  part and would be the choice if it could be bought. But 25 mA of resolution
  on a 300 A measurement is already far under the noise a running ESC puts on
  the wire, so the four extra bits buy precision the measurement cannot use.
- **Bilingual pages, for now.** See [the README](README.md).

## The order of work

1. **Settle the servo rail's input.** One sentence from the person who wrote
   the requirement, and it decides a connector, a heatsink and 4 A against 8 A.
2. **Settle 2 A against 3 A** on the pack, which decides one part against three.
3. **Pick the 300 A shunt**, which is the only remaining component with no
   candidate at all.
4. **Schematic**, once nothing above is a guess.
5. **Buy the long-lead parts early.** TI was at a 16-week manufacturer lead time
   on every part on the list, and 26 weeks on two of them. That is longer than
   this project's remaining firmware work, so the parts are the critical path
   the moment the schematic is real.
