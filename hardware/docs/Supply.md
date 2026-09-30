# Supply, pack and monitors

The parts of research group B: R5 (board power input and rails), R6 (the
servo supply and the port switches), R7 (the charger and the bench's own 2S
pack; 2S is two cells in series) and R8 (current and voltage monitors and the
motor shunts). This page records the choice, the alternatives, the stock and
the reason, from the returns of round 1 under `hardware/research/round1/`
and `hardware/research/round1/selection.json`. One row per part, with every
stock field, is in [Parts](Parts.md). [Power](Power.md) keeps the comparison
of 2026-09-01 that seeded R6 to R8.

**Availability was checked on 2026-09-29.** Stock figures are not valid after
that day. `python3 tools/jlc_stock.py --check 5` reads them again.

P2 to P6 are the phases of [Research](Research.md#agent-layout): P4 the
verifiers, P5 the cross-category check and P6 the completeness check. T1 to T6 are its tasks, and the runs named FU- its follow-up tasks. S, F, Q and V followed by a number are its questions to the owner, cited with the owner's answer. A figure no P4 verifier re-read is marked not verified. The alternatives below rank 1 are not verified by P4; their figures are the re-rank's.

## Summary

| Need | Part | Package | JLCPCB / Digi-Key stock, 2026-09-29 | Reason |
| --- | --- | --- | --- | --- |
| Source selection | LM74800QDRRRQ1 (Texas Instruments), 2 a board | WSON-12 | 3105 / 7634 | a two-input ideal-diode OR with the DC (direct current) path gated by undervoltage, one controller per input |
| 3.3 V logic buck | LMQ66430MC3RXBRQ1 (Texas Instruments) | VQFN-14, 2.5 × 2.5 mm | 671 / 13558 | rank 1 on output accuracy, stated across the input range and a junction temperature of −40 to 150 °C; 3 A against a load of 1.36 A |
| 5 V rail | TPS62933DRLR (Texas Instruments) | SOT-583 | 36974 / 63937 | 3 A against a load of 2.5 A; rank 1 on stock at both vendors |
| Display supply | TPS259474ARPWR (Texas Instruments) | VQFN-10 | 331 / 125 | true reverse blocking, and a circuit breaker with auto-retry that turns a cable short off |
| Encoder 12 V supply | TPS55288RPMR (Texas Instruments) | VQFN-26 | 5183 / 5262 | integrated switches and a stated output current limit; it needs the I²C (Inter-Integrated Circuit) address opposite the TPS55285's |
| Port supply switch | TPS259474LRPWR (Texas Instruments), 20 a board | VQFN-10 | 3795 / 40352 | a latch-off circuit breaker, 3.96 to 4.84 A, which matches "every stop latches; nothing re-arms on its own". Its VIN of 2.7 V at least misses the owner's 0.8 V lowest rail (V55, 2026-09-30) |
| I/O (input/output) expander | MCP23017T-E/SS (Microchip), 2 a board | SSOP-28 | 6606 / 17187 | eight addresses, 0x20 to 0x27, and a hardware RESET pin |
| Pack charger and power path | BQ25713RSNR (Texas Instruments) | QFN-32 | 301 / 2103 | meets every charger value; its NVDC (narrow voltage direct current) path runs the board from USB-C (Universal Serial Bus Type-C) with the pack flat |
| Cell balancing, pack overcurrent protection and bench pack balance lead | MAX17320G22+ (Analog Devices) | QFN-24 | 578 / 355 | balances on its own from its non-volatile memory at the required 50 mA average |
| Charger input limit | FUSB303BTMX (onsemi) | X2-QFN-12 | 5194 / 4224 | a dead-battery Rd, the pull-down resistor a USB-C sink presents with no power; runs from VBUS (the USB supply line) down to 2.85 V, and its GPIO (general-purpose input/output) outputs set the charger's limit with no firmware |
| Onboard shunt | BVR-Z-R0002-1.0 (Isabellenhütte) | 4026 | 359 / 11190 | rank 1 on accuracy: its path error is 0.50 A of 1.5 A at 150 A; four-terminal Kelvin |
| Onboard shunt temperature sensor | MCP9808T-E/MS (Microchip) | MSOP-8 | 764 / 21589 | ±0.5 °C at most from −20 to 100 °C; its addresses, 0x18 to 0x1F, sit outside the monitor block 0x40 to 0x4F |
| External shunt, off the board | WSBE8518L1000JKA2 (Vishay) | 8518 busbar | none / 418 | rank 1 on accuracy: its path error is 0.73 A of 3 A at 300 A. No second source: see [Not known](#not-known) |
| Port current monitor | INA3221AIRGVR (Texas Instruments), 7 a board | QFN-16 | 8704 / 67241 | the owner's seed, which passes with 7 parts for 20 ports |
| Port shunt | ERJ-6CWDR010V (Panasonic), 20 a board | 0805 | 4062 / 12302 | passes with the INA3221; a ±0.5 % part, rank 1 on stock at both vendors |

The three fixed or held inputs of group B do not stand as selected parts:
the servo supply TPS55285VALR is refuted on its ambient range, the motor
monitor INA238AIDGSR fails the stock gate at both vendors, and no 18650 cell
is selected. Each is under [Not known](#not-known). The owner keeps the
TPS55285, fixes the INA228AIDGSR in place of the INA238, and names an SLS
XTRON 3000 mAh 2S1P pack for the bench (2026-09-30).

## R5: board power input and rails

| Function | Rank 1 | Alternatives |
| --- | --- | --- |
| Reverse-polarity protection | LM74800QDRRRQ1, not requalified by FU-B3 | LM74810QDRRRQ1, LM74800MDRRR; not verified |
| Overvoltage protection | no part in selection.json; FU-B3's re-rank ranks LM74800QDRRRQ1 first | LM74810QDRRRQ1, LM74800MDRRR; not verified |
| Inrush limiting | LM74800QDRRRQ1, not requalified by FU-B3 | LM74810QDRRRQ1, LM74800MDRRR; not verified |
| Source selection | LM74800QDRRRQ1 | LM74810QDRRRQ1, LM74800MDRRR; not verified |
| Pack disconnect | no part | none |
| 3.3 V logic buck | LMQ66430MC3RXBRQ1 | LM61460AFSQRJRRQ1, LMR43620MSC3RPERQ1; not verified |
| 5 V rail | TPS62933DRLR | TPS563300DRLR, LMR51430YFDDCR; not verified |
| Display supply | TPS259474ARPWR | TPS259470ARPWR, TPS259474LRPWR; not verified |
| Receiver supply | no part | none |
| Encoder 12 V supply | TPS55288RPMR | LM5175PWPR, LM5176PWPR; not verified |
| 2S pack connector | no part | a connector, round 2 |
| Logic supply from the RP2354B's USB port | no part | none |

The same LM74800QDRRRQ1 on each input serves the reverse-polarity
protection, the overvoltage cut-off, the inrush limit and the source
selection, so those functions add no part.

## R6: servo supply and port switches

| Function | Rank 1 | Alternatives |
| --- | --- | --- |
| Servo supply | TPS55285VALR, the held fixed input, refuted on its ambient range; no part selected | none |
| Servo supply inductor | no part in selection.json; FU-B3's verifiers confirm SRP1265A-4R7M | ETQP5M4R7YFC, ASPIAIG-Q1010-4R7M-T; not verified |
| Port supply switch | TPS259474LRPWR | TPS259470LRPWR, TPS259470ARPWR; not verified |
| I/O expander | MCP23017T-E/SS | TCA6416APWR, TCA6424ARGJR; not verified |
| Port voltage ceiling | no part | none |

The MCP23017 drives 26 outputs: 20 port-switch enables, 5 bias selectors and
1 converter enable (V119), 16 lines a part.

## R7: charger and bench pack

| Function | Rank 1 | Alternatives |
| --- | --- | --- |
| Pack charger | BQ25713RSNR | BQ25703ARSNR, BQ25723RSNR; not verified |
| Cell balancing | MAX17320G22+ | BQ29209DRBR, BQ40Z50RSMR-R2; not verified |
| Power path | BQ25713RSNR | BQ25703ARSNR, BQ25723RSNR; not verified |
| Charger input limit | FUSB303BTMX | TUSB321AIRWBR, TUSB321RWBR; not verified |
| Pack cells | the owner names an SLS XTRON 3000 mAh 2S1P pack from Stefansliposhop (2026-09-30); round 1's INR-18650-P30B (Molicel) passes every check and is not used | none |
| Pack overcurrent protection | MAX17320G22+ | BQ40Z50RSMR-R2, BQ28Z610DRZR; not verified |
| Bench pack balance lead | MAX17320G22+ | BQ40Z50RSMR-R2, BQ28Z610DRZR; not verified |

## R8: monitors and shunts

| Function | Rank 1 | Alternatives |
| --- | --- | --- |
| Motor monitor | INA238AIDGSR, the fixed input, refuted on stock; the owner fixes the INA228AIDGSR in its place (2026-09-30), stock not read | INA238AQDGSRQ1, the same die: Digi-Key 5024, JLCPCB 1 with a presale count of −115; not verified |
| Onboard shunt | BVR-Z-R0002-1.0 | BVE-M-R0002-1.0, WSLP2726L2000FEA; not verified |
| Onboard shunt temperature sensor | MCP9808T-E/MS | STTS22HTR, TMP116AIDRVR and 2 others; not verified |
| External shunt | WSBE8518L1000JKA2 | BAS-M-R0001-R-5.0, WSBS8536L1000JK60, neither a second source; not verified |
| External shunt sense input | no part; the owner sets 1 kΩ in series with each input and a low-capacitance TVS (transient-voltage-suppression) diode behind them (2026-09-30) | INA238AIDGSR refuted; the INA228's rating against V179 not read |
| Port current monitor | INA3221AIRGVR | INA226AIDGSR, INA237AIDGSR; not verified |
| Port shunt | ERJ-6CWDR010V | PE1206DRM470R01L, PA1206DRE470R01Z; not verified |
| Servo rail monitor | no part | none |

## Power budget

P5's budget in T5, as its critic upheld it
(`hardware/research/round1/T5/002-P5.json`,
`hardware/research/round1/T5/004-P5-critic.json`). Over their limit: the
servo rail, the cell rating, the pack protection, the motor overcurrent path
and the shared-part stock of INA238AIDGSR. The owner's SLS XTRON pack passes
the cell rating on its vendor-stated 90 A (2026-09-30).

| Budget | Value | Within its limit |
| --- | --- | --- |
| Current, 3.3 V logic rail | LMQ66430MC3RXBRQ1 rated 3 A against 1.36 A | yes |
| Current, 5 V rail | TPS62933DRLR rated 3 A against 2.5 A: the display's 1.5 A continuous and 2 A peak, and the load-cell excitation's 51 mA, with a margin | yes |
| Current, servo rail | TPS55285VALR fails its ambient range at the pack floor: 6.0 V in, 8.4 V and 4.0 A out, 50 °C ambient. Its junction reaches an estimated 89 to 179 °C against a 125 °C recommended maximum (150 °C absolute maximum); the datasheet gives no loss figure for the IC (integrated circuit) at that point | no |
| Current, display supply | the TPS259474ARPWR breaker trips from 2.25 to 2.75 A, above the display's 2 A peak | yes |
| Current, encoder 12 V supply | TPS55288RPMR against an assumed 100 mA at 12 V; the encoder's own current (V102) is not found | yes |
| Pack current | 12.23 A at the 6.0 V floor: 10.02 A for the servo supply (53.3 W through 88.7 %), 1.30 A for the display through the 5 V rail (7.5 W through 96.0 %), 0.91 A for the 3.3 V rail, the encoder supply and the load-cell excitation. The owner lets every converter run to its limit from the pack (2026-09-30): at the 6.0 V floor the four converters and the other loads draw about 49.5 A at an assumed 90 %, nominal, against the pack's 90 A; R5 sized the pack path and R7 the pack protection for 12.23 A | yes in T5, at 12.23 A |
| Cell rating | INR-18650-P30B: 30 A continuous a cell against 12.23 A, 2900 mAh at least; no cell is selected in T5, because it is sold only at Liion Wholesale and not at Digi-Key. The owner's SLS XTRON pack: 30C, a C rate of 30 times its 3.0 Ah capacity, 90 A continuous, against 12.23 A (vendor page, 2026-09-30), and against about 49.5 A with every converter at its limit (owner, 2026-09-30). The 49.5 A is nominal, at an assumed 90 %: no efficiency is guaranteed and the converters' inductor clamps have no maximum, so the draw has no upper bound until the pack protection's overcurrent threshold is set at or below 90 A (a follow-up of R7) | no in T5; for the owner's pack, yes against 12.23 A, not shown against the draw with every converter at its limit |
| Pack protection | MAX17320G22+'s thresholds are set against 12.23 A and the cell's rating (V150). The owner's pack is rated 90 A continuous. Its vendor page states no cell or temperature limit; the owner sets 3.0 V a cell (F9) and 0 °C to 55 °C for it (2026-09-30), and the thresholds against them are round 2's | no |
| ESC (electronic speed controller) pack node clamp | no clamp part: every TVS diode read clamps at 113 to 126 V at 12 to 13 A, above the 85 V ceiling. The pack switch's controlled turn-off holds the node instead: 11 to 45 mJ on the pack leads and 5.6 to 22.5 mJ on the switch-to-ESC leads (V45, V54) | yes |
| Alert pins | 0 GPIO: no monitor's ALERT output is wired, and the firmware checks every sample. The ALERT outputs of the servo supply temperature sensors, an MCP9808T-E/MS beside each of the 4 TPS55285 the owner added on 2026-09-30, are wired to their converters' switch-off through a latch the operator clears and take no GPIO either ([IOBoard](IOBoard.md)) | yes |
| Motor overcurrent path | at most 20 ms from detection to the firmware's disarm (V158, V159). No path from the detection to the ESC pack switch: the switch opens only when the heartbeat stops, 155 to 185 ms after the last edge with a 200 ms deadline, or at the operator's latch clear (V160) | no |
| Shared-part stock | BQ25713RSNR (charger and power path) and MAX17320G22+ (three functions) at 1 placement, gate 50: each passes. INA238AIDGSR at 2 placements, need 10, gate 50: JLCPCB 0 with a presale count of −99, Digi-Key 0 | no for INA238AIDGSR |

## Not known

Each item is reported to the owner for round 2, with its evidence.

- R8, INA238AIDGSR, a shared-part stock shortfall: not known how it is sourced. The fixed-input motor monitor serves the onboard-shunt path and the external-shunt sense path, 10 units for 5 boards (2 placements). It fails sourcing rule 4 at both vendors: JLCPCB stockCount 0 and canPresaleNumber −99, Digi-Key 0, with no rule-5 route, read again at 2026-09-29T17:18:33Z (`hardware/research/round1/FU-B3/017-P4-stock-R8.json`, `FU-B3/023-adjudicator-R8.json`). The INA228 alternate footprint failed rule 5 on the figures of 2026-09-01. selection.json keeps no part for either function. The owner fixes the INA228AIDGSR (2026-09-30), which returns to stock soon, as the owner states; its stock is not read.
- R8, external shunt sense input, a second failure of INA238AIDGSR: not known. Beyond the stock, its differential input is rated −40 V to +40 V only, and its filter guidance allows too little series resistance on a 5 mA pin to survive the required 85 V fault of a mis-plugged or shorted sense lead (V179) (`FU-B3/021-P4-datasheet-R8.json`, `FU-B3/024-adjudicator-R8-2.json`). The sense leads need another clamp or isolation approach. The owner fixes the INA228 without further research on the monitor, and protects the sense leads with 1 kΩ in series with each input and a low-capacitance TVS behind them (2026-09-30). The INA228's rating against the 85 V fault, the error 1 kΩ adds to its reading and the protection's parts are not known; the INA238's datasheet allows at most 100 Ω of filter resistance (SLYS025B 7.1.4).
- R6, TPS55285VALR, a rail conflict on temperature: not known whether it holds. At the pack-floor corner (6.0 V in, 8.4 V and 4.0 A out, 50 °C ambient) its estimated junction temperature spans 89 to 179 °C against the 125 °C recommended maximum, because its datasheet gives no IC-loss curve there. The refutation stands (`FU-B1/030-adjudicator-R6.json`, carried unchanged in `FU-B3/011-rerank-R6.json`). The part is a fixed input and is reported, not replaced; selection.json keeps no part for Servo supply and carries no note that sets it apart from an ordinary open function. The owner keeps the part and uses three, one for each servo rail (2026-09-30): 4 to 6.35 A is a short peak for one servo, a heatsink cools each, and an MCP9808T-E/MS beside each switches its rail off when it runs hot, which bounds a sustained current from several servos ([IOBoard](IOBoard.md)). Round 2 finds the continuous current one converter holds at this corner.
- R7, pack cells, a sourcing-rule conflict that the owner resolves by naming the pack: an SLS XTRON 3000 mAh 2S1P pack from Stefansliposhop, which sourcing rule 1 names (owner, 2026-09-30). INR-18650-P30B (Molicel, S9) passes every technical, stock and lifecycle check, with 6529 at Liion Wholesale (2026-09-29T17:21:44Z), but sourcing rule 1 buys a part off the board at Digi-Key, and Digi-Key's API (application programming interface) carries no 18650 cell from an S9 maker (V151) (`FU-B3/016-P4-stock-R7.json`, `FU-B3/004-P2-R7.json`). The pack-protection thresholds follow the cell (V150): the owner sets 3.0 V a cell and 0 °C to 55 °C for the pack, read from an NTC taped at the center of the cells (2026-09-30), and the thresholds against them are round 2's.
- R7, pack cells, the selection record: selection.json keeps no part for Pack cells although both FU-B3 verifiers confirm INR-18650-P30B with no adjudicator against it (`FU-B3/016-P4-stock-R7.json`, `FU-B3/019-P4-datasheet-R7.json`). Not known whether the omission is the vendor question above or how selection.json was built.
- R8, the motor overcurrent path to the ESC pack switch: no hardware actuation path exists, so its time is not known. The coprocessor can only disarm the outputs, at most 20 ms after the averaged sample (V158, V159); the pack switch opens only when the heartbeat stops (155 to 185 ms, 200 ms deadline) or the operator clears the latch, and the overcurrent detection drives neither (V160, owner, 2026-09-28; `FU-B3/005-P2-R8.json`).
- R6 and R7, FUSB303BTMX and MCP23017T-E/SS, an I²C address omission: not known whether they collide. The FUSB303's 7-bit address is 0x21 or 0x31, set by its ADDR/ORIENT pin, and 0x21 is inside the 0x20 to 0x27 block P5 reserves for the 2 MCP23017 expanders. No return checks the pair on one bus (onsemi FUSB303 datasheet, Table 4, read by the P5 critic at 2026-09-29T17:58:39Z; `FU-B3/019-P4-datasheet-R7.json` records no FUSB303 address).
- R8, INA3221AIRGVR, the servo current sample period: not known which period the budget uses. P5 assumed 20 ms, the motor monitor's reading window of V158. The tree gives 5 ms a channel for the port current (V173: `docs/Servo.md:35`, `test/host/test_servo_limit.c:19`). Against 5 ms, 4 INA3221 on one bus take 4.84 ms of conversion, a margin of 0.16 ms, and the servo current abort's worst-case latency is understated (`FU-B3/005-P2-R8.json`, `FU-B3/021-P4-datasheet-R8.json`). The specification states that the sample rate each port monitor needs is not stated ([IO board](IOBoard.md#power)), and V173 gives 5 ms; which holds is not known.
- R5, pack disconnect: not known; no part qualifies. No candidate for the 2S pack's hardware disconnect at the 3.0 V a cell floor of F9 passes every value: the R5460 family fails an open balance lead (V107), the reconnect condition (V93, V94) or the per-cell floor (V95), and MAX17320G22+ states no open-cell-tap detection (`FU-B3/002-P2-R5.json`).
- R6, port voltage ceiling: not known; no part qualifies. No candidate meets the HV (high-voltage) trip band (off by 8.70 V, allowed from 8.53 V, inside ±0.99 %, V128) together with the latch that stays off until an operator or firmware action (V130). TPS3701 and TLV6710 release on their own; TLV6700, TPS3700 and the TPS3703 family have a trip band of about 1.0 to 1.2 % over temperature (`FU-B3/003-P2-R6.json`).
- R8, INA238AIDGSR as a hard stock blocker: the motor monitor has no part while the fixed input fails rule 4 live on 2026-09-29, JLCPCB stockCount 0 and canPresaleNumber −99 against a gate of 50, Digi-Key 0 on every packaging. Not known when stock returns. The owner fixes the INA228AIDGSR in its place (2026-09-30).
- R6, Servo supply, the standing refutation on its ambient range: at the worst-case point (4.0 A at 8.4 V from 6.0 V, 50 °C) the datasheet gives no IC loss and no guaranteed efficiency, so a junction at or under 125 °C is not shown; the adjudicator's bound spans 89 to 179 °C. Not known; reported as an open thermal-margin question.
- R5, overvoltage protection, the selection record: selection.json keeps no part and no not-requalified note, although LM74800QDRRRQ1 was confirmed by both P4 verifiers in T3 (`T3/031-P4-stock-R5.json`, `T3/032-P4-datasheet-R5.json`) and FU-B3's re-rank states that the same part serves reverse polarity, inrush and source selection (`FU-B3/010-rerank-R5.json`). Not known whether the omission is deliberate.
- R6, servo supply inductor, the selection record: selection.json keeps no part, although both FU-B3 verifiers confirm SRP1265A-4R7M (4.7 µH, 28 A saturation, 8.4 mΩ DC resistance at most, −55 to 150 °C) with no adjudicator against it (`FU-B3/015-P4-stock-R6.json`, `FU-B3/020-P4-datasheet-R6.json`). Not known why the confirmed part is not carried.

## Not verified

Each function below has no part selected by a verified run, or its part is
not requalified. The owner accepted each as open (owner, 2026-09-29).

- R5: reverse-polarity protection (LM74800QDRRRQ1 not requalified by FU-B3). Not verified.
- R5: overvoltage protection. No part in selection.json; not verified.
- R5: inrush limiting (LM74800QDRRRQ1 not requalified by FU-B3). Not verified.
- R5: pack disconnect. No part; not verified.
- R5: receiver supply. No part; not verified.
- R5: 2S pack connector. A connector, round 2; not verified.
- R5: logic supply from the RP2354B's USB port (IOBoard.md:384, marked R5). No part; not verified.
- R6: Servo supply. The held TPS55285, refuted on its ambient range; not verified.
- R6: Servo supply inductor. No part in selection.json; not verified.
- R6: Port voltage ceiling. No part; not verified.
- R7: Pack cells. No part selected; not verified.
- R8: Motor monitor. The held INA238, refuted; not verified.
- R8: External shunt sense input. No part; not verified.
- R8: Servo rail monitor. No part; not verified.
- R8: External shunt (no second source). WSBE8518L1000JKA2 has no alternate, so rule 5 has no route; not verified.

Figures of group B that no P4 verifier confirmed; each is not verified:

- R6: figure found V129, not confirmed in FU-B3; not verified.
- R8: figure P2 report: BVR market introduction, not confirmed in FU-B3; not verified.
