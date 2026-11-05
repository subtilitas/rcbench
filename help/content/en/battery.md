---
screen: BATTERY
title: Battery
order: 70
---

# Battery

BATTERY shows each cell's voltage difference from the pack average and gives a verdict on the pack. Open it from the BATTERY tile on the [menu](overview.md). The tile carries the SIMULATED mark in this release.

## On the bench: no pack

![BATTERY with nothing feeding it](view:battery-none)

Neither board reads a balance lead in this release, so the firmware supplies no measurements to this screen. It shows NO PACK, `No battery connected to the balance lead.`, and `No monitor on the balance lead.` in place of the plot. The rows read `---`.

## What a fed screen shows

![Cell divergence in the host renderer](view:battery)

The picture comes from the host renderer, which feeds the screen with cell voltages.

- CELL DIFFERENCE plots each cell's voltage difference from the pack average. At a spread of 30mV or more, the weakest cell appears in red.
- The scale adjusts to the pack, with a minimum of 12mV, and is printed beside the plot.
- Each bar has the cell number and voltage beneath it.

| Verdict | Line | Spread |
|---|---|---|
| HEALTHY | `Cell readings stay even under load.` | under 30mV |
| WATCH | `The difference between cells is growing.` | from 30mV |
| REPLACE | `One cell differs from the others.` | from 60mV |
| NO PACK | `No battery connected to the balance lead.` | no cells |

The spread is the widest gap between any two cells.

| Row | Shows |
|---|---|
| CELLS | the cell count, `6S` |
| PACK | the pack voltage |
| AVERAGE | the mean cell voltage |
| SPREAD | the spread, in the verdict's colour |
| PACK iR | the pack's internal resistance |
| USED | the charge used, of the capacity |

Measure under load. A weak cell may appear normal at rest.

## Related pages

- [Menu](overview.md): the SIMULATED mark.
