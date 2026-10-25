---
screen: BATTERY
title: Battery
order: 70
---

# Battery

BATTERY shows how far each cell of a pack departs from the pack's mean, and gives a verdict on the pack. It opens from the BATTERY tile on the [menu](overview.md). The tile carries the SIMULATED mark in this release.

## On the bench: no pack

![BATTERY with nothing feeding it](view:battery-none)

No firmware feeds this screen in this release: neither board reads a balance lead. The screen therefore shows NO PACK, `No battery connected to the balance lead.`, and `No monitor on the balance lead.` where the plot would be. The rows read `---`.

## What a fed screen shows

![Cell divergence in the host renderer](view:battery)

The picture comes from the host renderer, which feeds the screen with cell voltages.

- CELL DIFFERENCE draws each cell as its departure from the pack's mean. From a spread of 30mV the weakest cell is drawn red.
- The scale follows the pack down to a floor of 12mV and is printed beside the plot.
- Under each bar: the cell's number and its voltage.

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

Measure under load. At rest a weak cell reads like the others.

## Related pages

- [Menu](overview.md): the SIMULATED mark.
