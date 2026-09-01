# The power path

Four integrated circuits stand between a bench socket and an honest number:
the supply that feeds a servo, the charger that keeps the bench's own pack, and
the two monitors that measure what the motor and the servo actually drew. This
page is which parts, and — more usefully — which datasheet footnote decided
each one.

**Availability was checked on 2026-09-01.** Stock figures rot; the reasoning
does not. Re-check before a layout commits, by the method in
[Sourcing](Sourcing.md), and treat every number below as a claim with a date on
it rather than as a fact.

## Summary

| Need | Part | Package | The reason, in one line |
| --- | --- | --- | --- |
| Buck-boost, I²C voltage and current | **TPS55288RPMR** | VQFN-26-HR 3.5×4 | the only candidate whose I²C current limit scales past 6.35 A |
| 2S charger with balancing | **BQ25887RGER** | QFN-24-EP 4×4 | the only single die that charges 2S and balances it; 2 A, not 3 A |
| Servo rail monitor, ≤15 V, 8 A | **INA745A** | VQFN-14 5×3 | the shunt is inside it, so there is no shunt to lay out badly |
| Motor monitor, 65 V, 300 A | **INA238AIDGSR** | VSSOP-10 | 85 V and sixteen bits, while the twenty-bit part is unbuyable |

Fitting **INA238 in both monitor positions** collapses two drivers into one and
two footprints into one, and costs a shunt on the servo rail. On a project with
a per-file coverage floor, one driver tested once is worth more than the
0.9 mA of resolution it gives up.

## The servo supply — TPS55288

A servo test bench has to be able to lie to a servo about its supply. Standard
servos want something at or under 5.5 V; HV servos want 7.4 V or 8.4 V; and the
interesting question a bench answers — *what does this servo draw when it
stalls at 6 V rather than at 8.4 V* — needs the rail to be set from software
and the current limit to be set from software with it.

Three parts from the same TI family look identical in the parametric table.
They are not.

| | TPS55288 | TPS55289 | TPS55285 |
| --- | --- | --- | --- |
| V_in | 2.7–36 V | 3.0–30 V | 2.4–22 V |
| V_out over I²C | 0.8–22 V, 20 mV step | 0.8–22 V, 10 mV step | 0.8–15 V, 10 mV step |
| Current limit | external shunt, 0–63.5 mV in 0.5 mV steps | external shunt, same | **internal sense, hard 6.35 A** |
| Rating | 16 A inductor limit, 100 W from 12 V | 8 A | 8 A, four 15 mΩ FETs |
| JLCPCB | C2864583 · 5854 · $1.79 | C5942077 · 2218 · $5.20 | C52160906 · 2984 · $2.11 |
| Digi-Key | 5808 · $4.28 | 0, 3000 due 2026-11-02 | 2349 · $2.76 |

**All three datasheets say "programmable output current limit up to 6.35 A",
and on two of them it is not true.** On the '288 and the '289 that register
does not set a current at all — it sets a *sense voltage*, 0 to 63.5 mV in
0.5 mV steps, across a shunt the designer picks between the ISP and ISN pins.
6.35 A is what you get with the 10 mΩ resistor the datasheet happens to use in
its example. Change the resistor and the ceiling moves:

| R_sense | Full-scale limit | Step | Dissipation at 8 A |
| --- | --- | --- | --- |
| 8 mΩ | 7.94 A | 62.5 mA | 0.51 W |
| **6.3 mΩ** | **10.1 A** | **79 mA** | **0.40 W** |

Take 6.3 mΩ in a 1 W part, for headroom over the 8 A the requirement asks for.
The average inductor current limit is a separate mechanism set by a resistor at
the ILIM pin, and goes to 16 A.

On the **'285 the sense is internal**, so 6.35 A is a real ceiling. It is the
newest, smallest and cheapest of the three, it will *deliver* 8 A, and it
cannot be *told* to allow 8 A. It is out on that one sentence.

### The input rail is the thing that will bite

8.4 V at 8 A is 67 W. Drawn from a 5 V input at 90 % efficiency that is close
to 15 A on the input side — at the '288's 16 A inductor limit, and a thermal
problem before it is an electrical one. **From a ≤5.5 V source, about 4 A at
8.4 V is the honest number.** Feed the converter from 12 V or more and the full
8 A is comfortable, which is the arrangement to design for.

Rejected: **MP4245** (36 V, 6 A peak, I²C) is marked *Not For New Designs* at
Digi-Key, which is a decision already made for us. **MP8859** stops at 3 A.

## The bench's own pack — BQ25887

**No single die charges a 2S pack above 2 A and balances it.** That is the
finding, and it is worth stating as a finding because the parametric search
looks like it should have one.

| | BQ25887 | MP2672A |
| --- | --- | --- |
| Charge current | 2 A | 2 A |
| Input | 3.9–6.2 V, 20 V abs max | 4–5.75 V |
| Balancing | 400 mA integrated FETs, automatic, inside one charge cycle | integrated, on a mismatch threshold |
| Power path | none | NVDC |
| ADC | 16-bit — bus V and I, each cell, charge current, NTC, die | present, unspecified |
| JLCPCB | C2761614 · 874 · $3.16 | C6674645 · 549 · $1.78 |
| Digi-Key | 10,980 · $3.60 | 3705 · $1.51 |

BQ25887 wins on the ADC, which is the part a bench cares about: per-cell voltage
over I²C is a battery screen that reads rather than estimates. MP2672A wins if
the bench must come alive from a flat pack — that is what the NVDC power path
buys — and it is cheaper.

Both are **boost chargers off a USB input**, which is a constraint worth seeing
early: neither will take 12 V.

If 3 A turns out to be firm, the function splits and balancing gets an order of
magnitude slower:

| Part | Role | Balance current | JLCPCB | Digi-Key |
| --- | --- | --- | --- | --- |
| BQ25798RQMR | 1–4S buck-boost charger, 5 A, 3.6–24 V in, I²C | — | 366 · $1.81 | 10,954 · $3.51 |
| + BQ76907RGRR | 2–7S AFE, I²C, 16-bit ADC, full protection suite | 0–50 mA per cell, host-driven | 2646 · $1.15 | — |
| + BQ29209DRBR | autonomous 2S balancer, no host at all | up to 15 mA, resistor-set | 707 · $0.58 | — |

400 mA against 50 mA is the difference between balancing within a charge and
balancing overnight. BQ25798 does buy a wide input in exchange, which BQ25887
does not have — worth it if the bench ends up running from 12 V rather than USB.

## The servo rail monitor — INA745A

40 V, ±35 A continuous, 16-bit, **and the 800 µΩ shunt is inside the package.**
At 8 A it drops 6.4 mV and burns 51 mW, and the LSB is about 1.2 mA. There is
no shunt to lay out, no Kelvin connection to get wrong, and no four-terminal
footprint to source.

| | INA745A/B | INA238 | INA239 | INA226 |
| --- | --- | --- | --- | --- |
| Bus | 40 V | 85 V | 85 V | 36 V |
| ADC | 16-bit | 16-bit | 16-bit | 16-bit |
| Shunt | integrated 800 µΩ | external | external | external |
| Interface | I²C | I²C | **SPI** | I²C |
| Energy and charge accumulators | no | yes | yes | no |
| JLCPCB | B: 13,351 · $1.05 — A: 50 | 2246 · $4.35 | 108 · $2.86 | 74,914 · $0.52 |
| Digi-Key | A: 6611 · $1.67 — B: 9330 · $1.27 | 0, 2500 due 2026-10-27 | 255 · $2.34 | 95,920 · $1.72 |

**Take the A grade.** Offset is ±6.25 mA against the B grade's ±62.5 mA, which
on a 0–8 A rail is 0.08 % of full scale against 0.8 %. Digi-Key stocks A well
and JLCPCB effectively stocks only B, which is a reason to buy this one part
from Digi-Key even on a JLCPCB assembly.

**INA238 instead** if the single-driver argument wins: 5 mΩ here gives 40 mV at
8 A, a 0.25 mA LSB and 0.32 W to get rid of. **INA239** is the same part with
SPI, for the day the coprocessor's I²C is full — but stock is thin at both
vendors, which is a poor reason to design around a bus. **INA226** is a fifth
of the price and in enormous stock, and has no energy or charge accumulator,
so consumption would have to be integrated in firmware against a timer the
firmware does not currently keep.

## The motor monitor — INA238

This is the measurement the bench is for, and it is the one the parts market
made hardest.

| | INA238 | INA228 | INA229 |
| --- | --- | --- | --- |
| Bus | 85 V | 85 V | 85 V |
| ADC | 16-bit | 20-bit | 20-bit |
| Interface | I²C | I²C | SPI |
| Shunt full scale | ±163.84 mV / ±40.96 mV | same | same |
| LSB | 5 µV / 1.25 µV | 312.5 nV / 78.125 nV | as INA228 |
| JLCPCB | 2246 · $4.35 | 29, pre-sale −477 | 0 to 3 |
| Digi-Key | 0, 2500 due 2026-10-27 | 0, 666 due 2026-11-03 | 0, 2500 due 2026-12-23 |

**INA228 is unbuyable at both vendors, and the automotive INA228AQDGSRQ1 is no
better** — JLCPCB has none, Digi-Key has 416 at $3.81. The project's running
record already said so and was right.

What that record gets wrong is the other half of the sentence: *"the obvious
substitute stops at 36 V, which is under 8S."* That describes **INA226**.
**INA238 is 85 V** — the same bus range as the INA228, the same two shunt
ranges, the same energy and charge accumulated in hardware, in the same
VSSOP-10. It costs sixteen bits against twenty and nothing else.

Sixteen bits is not close to being the limit here:

| R_shunt | Drop at 300 A | Of ±40.96 mV full scale | INA238 LSB | Dissipation at 300 A |
| --- | --- | --- | --- | --- |
| 100 µΩ | 30 mV | 73 % | 12.5 mA | 9 W |
| 50 µΩ | 15 mV | 37 % | 25 mA | 4.5 W |

25 mA of resolution on a 300 A measurement is far below the noise a running
ESC puts on the same wire. **The shunt is the hard part, not the IC.** 300 A
needs a busbar-style resistor — Isabellenhütte's BV series, Vishay's WSBS8518
— because the largest four-terminal SMD parts (Bourns CSS2H-2512, 15 W) stop
at 0.2 mΩ and would burn 18 W at 300 A. Kelvin-sense it, and put an RC filter
on IN+ and IN− against the switching edges, which arrive here by design.

## What this page does not answer

- **Which of the two readings of the servo supply is the real one** — whether
  ≤5.5 V and ≤8.4 V are two output settings, or an input and an output. The
  part survives either, the input rail does not; see above.
- **Whether 2 A of charging is enough**, which decides between one chip and
  three.
- **The shunt part numbers themselves.** Only the class is settled here.
- **Anything about layout**, isolation, or how a 300 A path and a 3.3 V I²C bus
  share a board.
