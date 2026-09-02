# The power path

Four integrated circuits: the supply that feeds a servo, the charger for the
bench's own pack, and the two monitors that measure what the motor and the
servo draw. This page records the choice, the alternatives and the stock.

**Availability was checked on 2026-09-01.** Stock figures are not valid after
that date. Re-check before a layout commits, by the method in
[Sourcing](Sourcing.md).

## Summary

| Need | Part | Package | Reason |
| --- | --- | --- | --- |
| Buck-boost, I²C (Inter-Integrated Circuit) voltage and current | TPS55288RPMR | VQFN-26-HR 3.5×4 | the only candidate whose I²C current limit scales past 6.35 A |
| 2S charger with balancing | BQ25887RGER | QFN-24-EP 4×4 | the only single die that charges 2S and balances it; 2 A, not 3 A |
| Servo rail monitor, ≤15 V, 8 A | INA745A | VQFN-14 5×3 | integrated shunt, so no shunt layout |
| Motor monitor, 65 V, 300 A | INA238AIDGSR | VSSOP-10 | 85 V and 16 bits; the 20-bit part is unbuyable |

Fitting an INA238 in both monitor positions gives one driver and one footprint
for both, at the cost of an external shunt on the servo rail and 0.9 mA of
resolution.

## Servo supply: TPS55288

The servo rail has two output settings: up to 5.5 V for LV (low-voltage)
servos and up to 8.4 V for HV (high-voltage) servos, at 4 to 8 A. Both are
set from software, and the current limit follows the voltage setting.

Three parts of the same TI family:

| | TPS55288 | TPS55289 | TPS55285 |
| --- | --- | --- | --- |
| V_in | 2.7–36 V | 3.0–30 V | 2.4–22 V |
| V_out over I²C | 0.8–22 V, 20 mV step | 0.8–22 V, 10 mV step | 0.8–15 V, 10 mV step |
| Current limit | external shunt, 0–63.5 mV in 0.5 mV steps | external shunt, same | internal sense, fixed 6.35 A |
| Rating | 16 A inductor limit, 100 W from 12 V | 8 A | 8 A, four 15 mΩ FETs (field-effect transistors) |
| JLCPCB | C2864583 · 5854 · $1.79 | C5942077 · 2218 · $5.20 | C52160906 · 2984 · $2.11 |
| Digi-Key | 5808 · $4.28 | 0, 3000 due 2026-11-02 | 2349 · $2.76 |

All three datasheets state "programmable output current limit up to 6.35 A". On
the '288 and the '289 the register sets a sense voltage, 0 to 63.5 mV in 0.5 mV
steps, across a shunt between ISP and ISN; 6.35 A is the value with the
datasheet's 10 mΩ example. The ceiling moves with the resistor:

| R_sense | Full-scale limit | Step | Dissipation at 8 A |
| --- | --- | --- | --- |
| 8 mΩ | 7.94 A | 62.5 mA | 0.51 W |
| 6.3 mΩ | 10.1 A | 79 mA | 0.40 W |

Choice: 6.3 mΩ in a 1 W part, for headroom over the 8 A the requirement asks
for. The average inductor current limit is a separate mechanism set by a
resistor at the ILIM pin and goes to 16 A.

On the '285 the sense is internal, so 6.35 A is a hard ceiling. It delivers 8 A
but cannot be set to allow 8 A.

Rejected: MP4245 (36 V, 6 A peak, I²C) is marked Not For New Designs at
Digi-Key. MP8859 stops at 3 A.

### Input rail

8.4 V at 8 A is 67 W. From a 5 V input at 90% efficiency that is close to 15 A
on the input side, at the '288's 16 A inductor limit. A 5 V input delivers about 4 A at 8.4 V. The converter input is not part
of the requirement and is designed for 12 V or more, which delivers the
full 8 A.

## Pack charger: BQ25887

No single die charges a 2S pack above 2 A and balances it.

| | BQ25887 | MP2672A |
| --- | --- | --- |
| Charge current | 2 A | 2 A |
| Input | 3.9–6.2 V, 20 V abs max | 4–5.75 V |
| Balancing | 400 mA integrated FETs, automatic, within one charge cycle | integrated, on a mismatch threshold |
| Power path | none | NVDC (narrow voltage direct current) |
| ADC (analogue-to-digital converter) | 16 bit: bus V and I, each cell, charge current, NTC (negative temperature coefficient thermistor), die | present, unspecified |
| JLCPCB | C2761614 · 874 · $3.16 | C6674645 · 549 · $1.78 |
| Digi-Key | 10,980 · $3.60 | 3705 · $1.51 |

BQ25887 has the ADC, which gives per-cell voltage over I²C. MP2672A has the
NVDC power path, which lets the bench start from a flat pack, and is cheaper.
Both are boost chargers off a USB (Universal Serial Bus) input; neither takes
12 V.

3 A is the top of the requirement's range, not a requirement, so 2 A with
one BQ25887 stands. At 3 A the function would split:

| Part | Role | Balance current | JLCPCB | Digi-Key |
| --- | --- | --- | --- | --- |
| BQ25798RQMR | 1–4S buck-boost charger, 5 A, 3.6–24 V in, I²C | none | 366 · $1.81 | 10,954 · $3.51 |
| + BQ76907RGRR | 2–7S AFE (analogue front end), I²C, 16-bit ADC, full protection | 0–50 mA per cell, host-driven | 2646 · $1.15 | none |
| + BQ29209DRBR | autonomous 2S balancer, no host | up to 15 mA, resistor-set | 707 · $0.58 | none |

400 mA against 50 mA is balancing within a charge against balancing overnight.
BQ25798 has a wide input, which BQ25887 does not.

## Servo rail monitor: INA745A

40 V, ±35 A continuous, 16 bit, with the 800 µΩ shunt inside the package. At 8
A: 6.4 mV drop, 51 mW, LSB (least significant bit) about 1.2 mA. No shunt to
lay out, no Kelvin connection, no four-terminal footprint.

| | INA745A/B | INA238 | INA239 | INA226 |
| --- | --- | --- | --- | --- |
| Bus | 40 V | 85 V | 85 V | 36 V |
| ADC | 16 bit | 16 bit | 16 bit | 16 bit |
| Shunt | integrated 800 µΩ | external | external | external |
| Interface | I²C | I²C | SPI (Serial Peripheral Interface) | I²C |
| Energy and charge accumulators | no | yes | yes | no |
| JLCPCB | B: 13,351 · $1.05; A: 50 | 2246 · $4.35 | 108 · $2.86 | 74,914 · $0.52 |
| Digi-Key | A: 6611 · $1.67; B: 9330 · $1.27 | 0, 2500 due 2026-10-27 | 255 · $2.34 | 95,920 · $1.72 |

Take the A grade: offset ±6.25 mA against the B grade's ±62.5 mA, which on a 0
to 8 A rail is 0.08% against 0.8% of full scale. Digi-Key stocks A; JLCPCB
stocks B.

Alternatives: INA238 with a 5 mΩ shunt gives 40 mV at 8 A, a 0.25 mA LSB and
0.32 W. INA239 is the SPI variant; stock is thin at both vendors. INA226 is a
fifth of the price and in large stock, but has no energy or charge accumulator,
so consumption would have to be integrated in firmware.

## Motor monitor: INA238

| | INA238 | INA228 | INA229 |
| --- | --- | --- | --- |
| Bus | 85 V | 85 V | 85 V |
| ADC | 16 bit | 20 bit | 20 bit |
| Interface | I²C | I²C | SPI |
| Shunt full scale | ±163.84 mV / ±40.96 mV | same | same |
| LSB | 5 µV / 1.25 µV | 312.5 nV / 78.125 nV | as INA228 |
| JLCPCB | 2246 · $4.35 | 29, pre-sale −477 | 0 to 3 |
| Digi-Key | 0, 2500 due 2026-10-27 | 0, 666 due 2026-11-03 | 0, 2500 due 2026-12-23 |

INA228 is unbuyable at both vendors; the automotive INA228AQDGSRQ1 is no better
(JLCPCB none, Digi-Key 416 at $3.81). INA238 has the same bus range, the same
two shunt ranges, the same energy and charge accumulators, the same VSSOP-10
and the same pin order, at 16 bits instead of 20.

| R_shunt | Drop at 300 A | Of ±40.96 mV full scale | INA238 LSB | Dissipation at 300 A |
| --- | --- | --- | --- | --- |
| 100 µΩ | 30 mV | 73% | 12.5 mA | 9 W |
| 50 µΩ | 15 mV | 37% | 25 mA | 4.5 W |

25 mA of resolution on 300 A is below the noise a running ESC (electronic speed
controller) puts on the wire. The shunt is the constraint: 300 A needs a
busbar-type resistor (Isabellenhütte BV series, Vishay WSBS8518); the largest
four-terminal SMD (surface-mount device) parts (Bourns CSS2H-2512, 15 W) stop
at 0.2 mΩ and would dissipate 18 W. Kelvin-sense it, with an RC (radio control)
filter on IN+ and IN− against switching edges.

## Not answered here

- The converter's input connector and heatsink.
- The shunt part numbers. Only the class is settled.
- Layout, isolation, and how a 300 A path and a 3.3 V I²C bus share a board.
