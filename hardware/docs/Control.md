# Control, link and gate

The parts of research group A: R1 (the RP2354B and its support), R2 (CAN,
Controller Area Network), R3 (the safety gate and the ESC pack switch; ESC is
electronic speed controller) and R4 (output and input buffering). This page
records the choice, the alternatives, the stock and the reason, from the
returns of round 1 under `hardware/research/round1/` and
`hardware/research/round1/selection.json`. One row per part, with every stock
field, is in [Parts](Parts.md).

**Availability was checked on 2026-09-29.** Stock figures are not valid after
that day. `python3 tools/jlc_stock.py --check 5` reads them again.

P2 to P6 are the phases of [Research](Research.md#agent-layout): P4 the
verifiers, P5 the cross-category check and P6 the completeness check. T1 to T6 are its tasks, and the runs named FU- its follow-up tasks. S, F, Q and V followed by a number are its questions to the owner, cited with the owner's answer. A figure no P4 verifier re-read is marked not verified. The alternatives below rank 1 are not verified by P4; their figures are the re-rank's.

## Summary

| Need | Part | Package | JLCPCB / Digi-Key stock, 2026-09-29 | Reason |
| --- | --- | --- | --- | --- |
| 12 MHz crystal | ABM8-272-T3 (Abracon) | SMD3225-4P | 15120 / 28505 | rank 1; meets every value of V5, V6, V7 and V15 |
| Core regulator inductor | AOTA-B201610S3R3-101-T (Abracon) | 0806 | 4566 / 28115 | the inductor section 6.3.8.2 of the RP2350 datasheet recommends, in the 2.0 × 1.6 mm case of its layout |
| Link and external CAN controller | MCP2518FDT-E/SL (Microchip), 2 a board | SOIC-14 | 28,884 / 43,486 | one receive FIFO (first in, first out buffer) holds 32 frames against the 9 of V16; one part number serves both ports. Its register map is not the MCP2515's, so it needs a new driver |
| Link and external CAN transceiver | TCAN3413DR (Texas Instruments), 2 a board | SOIC-8 | 14,544 / 4,455 | runs from the 3.3 V rail alone; loop delay 180 ns maximum against 350 ns; one part number serves both ports |
| Monostable | 74HC423BQ,115 (Nexperia) | DHVQFN-16 | 233 / 2148 | the '423 behaviour, stated in section 1 and Table 3 of its datasheet |
| OR gate | 74AUP1G32GW,125 (Nexperia) | TSSOP-5 | 952 / 14211 | rank 1 of the OR gates read, on I_off (the input current of an unpowered part) |
| Reset supervisor | TPS3703A7330DSERQ1 (Texas Instruments) | WSON-6 | 307 / 280 | the narrowest unspecified band read, 155 mV |
| Latch | 74LVC1G175GW,125 (Nexperia) | TSSOP-6 | 1973 / 36048 | 24 mA output for the gate inputs it drives |
| Latch clear control | 434153017835 (Würth Elektronik) | SMD (surface-mount device), 2.9 × 3.5 mm | 5229 / 27089 | stated bounce of 10 ms; maker status read |
| Trigger-path inverter | SN74AUP1G14DBVR (Texas Instruments) | SOT-23-5 | 5252 / 28635 | rank 1 of the inverters read, on I_off; placed only with a monostable whose trigger inputs have one polarity, so none with the 74HC423 |
| Trigger input I_off buffer (Q9) | SN74AUP1G17DBVR (Texas Instruments), 2 a board | SOT-23-5 | 8928 / 88895 | the owner's Q9 decision; it is also the Schmitt buffer ahead of the latch clock |
| Servo rail gate | BTS7004-1EPP (Infineon) | TSDSO-14 | 20982 / 34830 | states its off-state leakage and a turn-on voltage maximum |
| Servo rail gate driver | 2N7002BK,215 (Nexperia) | SOT-23 | 172635 / 135525 | placed only with a discrete P-channel rail switch, so 0 with the BTS7004-1EPP |
| Onboard pack switch driver and external module driver output | TPSI3052DWZR (Texas Instruments), 4 a board | SOIC-8 | 1126 / 3184 | holds the arrays on from the 3.3 V primary at any pack voltage |
| Pack reverse-polarity protection | IPT015N10N5ATMA1 (Infineon), 10 a board | HSOF-8 | 28320 / 40281 | an anti-series array of the switch MOSFET (metal-oxide-semiconductor field-effect transistor): 100 V blocks −67.2 V |
| Pack precharge | CSD19537Q3T (Texas Instruments), 2 a board | VSONP-8 | 630 / 3267 | 100 V in 3.3 × 3.3 mm, stock at both vendors |
| Inverter on an active-low output enable, and R4's enable inverter | SN74LVC1G04DBVR (Texas Instruments) | SOT-23-5 | 53165 / 248297 | rank 1 of the inverters read, on propagation delay and stock |
| Gated output buffer | SN74LVC244APWR (Texas Instruments), 2 a board | TSSOP-20 | 94504 / 0 | meets every value with a 142 Ω series resistor, a figure from V75, which is not verified; its second source is the alternate 74LVC244APW,118 (JLCPCB 27928) |
| Bias selector | SN74LVC1G126DBVR (Texas Instruments), 5 a board | SOT-23-5 | 73877 / 19254 | off is high impedance, so reset and unpowered give the pull-down |
| Multiprotocol port path, UART (universal asynchronous receiver-transmitter) socket path and reply path | SN74CBTLV3126PWR (Texas Instruments), 2 a board | TSSOP-14 | 401 / 3756 | active-high enable from the enable node, one switch a line and no direction pin, so the reply passes while enabled |
| Receiver inputs | SN74LVC1T45DBVR (Texas Instruments) | SOT-23-6 | 392691 / 90872 | inputs recommended to 5.5 V |

I_off is a datasheet
limit on the input current of a part whose supply is at 0 V.

The owner's Q9 decision (owner, 2026-09-29): the 74HC423 keeps the '423
behaviour, and an SN74AUP1G17DBVR with I_off goes in front of its trigger
inputs, as R3 selected. It adds no part and changes none.

## R1: the RP2354B and its support

The RP2354B is the fixed input (owner, 2026-09-27) and is held:
20 in the personal library, stated 2026-09-25, against a need of 5. FU-A4 did not
requalify it for R1; FU-C2 verified it as the part of the R11 functions that
run on its PIO (programmable input/output) and firmware. JLCPCB C39843328
read 2982 on 2026-09-29T07:37:59Z. Raspberry Pi states that the RP2350 stays
in production until at least January 2045.

| Function | Rank 1 | Alternatives |
| --- | --- | --- |
| 12 MHz crystal | ABM8-272-T3 | none qualified |
| Core regulator inductor | AOTA-B201610S3R3-101-T | VLS4015CX-3R3M-H (TDK), 74438357033 (Würth Elektronik); not verified |
| USB (Universal Serial Bus) protection | no part; relaxed to match standard parts (owner, 2026-09-30) | see [Not known](#not-known) |

The crystal's maker status is not read: Abracon's pages answered HTTP 404 (the
Hypertext Transfer Protocol's not-found status) or load the part by script. Digi-Key reads it Active. This is reported to the
owner.

## R2: CAN

| Function | Rank 1 | Alternatives |
| --- | --- | --- |
| Link CAN controller | MCP2518FDT-E/SL | MCP251863T-E/SS, MCP2515T-I/ST (holds 2 received frames); not verified |
| Link CAN transceiver | TCAN3413DR | SN65HVD233DR, TCAN1044VDRQ1; not verified |
| External CAN controller | MCP2518FDT-E/SL | MCP251863T-E/SS; not verified |
| External CAN transceiver | TCAN3413DR | TCAN1044VDRQ1, TCAN1042HGVDRQ1, TCAN1472VDRQ1; not verified |

The MCP2518FD's receive FIFO holds 32 frames. The panel sends at most 9
frames to the IO (input/output) board in one 400 ms flash stall, the
packaged sector-erase maximum, and at most 8 in a 3 ms one (V16, found by
R2). The MCP2515 holds 2 received frames.

## R3: safety gate and ESC pack switch

| Function | Rank 1 | Alternatives |
| --- | --- | --- |
| Monostable | 74HC423BQ,115 | 74HC4538PW,118; not verified |
| OR gate | 74AUP1G32GW,125 | none recorded |
| Reset supervisor | TPS3703A7330DSERQ1 | TPS3852H33DRBR (no second source); not verified |
| Latch | 74LVC1G175GW,125 | none recorded |
| Latch clear control | 434153017835 | EVPAA202K, EVPAA602W; not verified |
| Trigger-path inverter | SN74AUP1G14DBVR | 74AUP1G14GVH; not verified |
| Trigger input I_off buffer (Q9) | SN74AUP1G17DBVR | none recorded |
| Servo rail gate | BTS7004-1EPP | BTS7008-1EPP; not verified |
| Servo rail gate driver | 2N7002BK,215 | none recorded |
| Onboard pack switch MOSFETs | no part | IPT015N10N5ATMA1 refuted: see [Not known](#not-known) |
| Onboard pack switch driver | TPSI3052DWZR | none recorded |
| External pack switch module | no part | VS-FC420SA10 and VS-FC270SA20 refuted: see [Not known](#not-known) |
| External module driver output | TPSI3052DWZR | LTC7001IMSE#PBF (no second source); not verified |
| Pack reverse-polarity protection | IPT015N10N5ATMA1 | none recorded |
| Pack overvoltage and transient clamp | no part | see [Not known](#not-known) |
| Pack precharge | CSD19537Q3T | none recorded |
| Inverter on an active-low output enable | SN74LVC1G04DBVR | none recorded |

The 74HC423's '423 behaviour rests on a sentence in section 1 and on Table 3
of its datasheet, and its Fig. 9 contradicts them. The FU-A2 adjudicator let
the part stand; test step 5 of pull request #167 settles it on the bench.

## R4: output and input buffering

| Function | Rank 1 | Alternatives |
| --- | --- | --- |
| Gated output buffer | SN74LVC244APWR | 74LVC244APW,118 (verified, the rule-5 alternate), SN74LV244APWR and 74LV244APWJ; the last two not verified |
| Enable inverter | SN74LVC1G04DBVR | SN74AUP1G04DBVR and 5 others; not verified |
| Bias selector | SN74LVC1G126DBVR | SN74AUP1G126DBVR and 5 others; not verified |
| Multiprotocol port path | SN74CBTLV3126PWR | none recorded |
| UART socket path | SN74CBTLV3126PWR, a spare channel of the same 2 parts | none recorded |
| Receiver inputs | SN74LVC1T45DBVR | none recorded |
| Reply path through the gate | SN74CBTLV3126PWR | none recorded |

## Board budget against the RP2354B

P5's budget in T5, as its critic upheld it
(`hardware/research/round1/T5/002-P5.json`,
`hardware/research/round1/T5/004-P5-critic.json`). Over its limit: the GPIO
(general-purpose input/output) count.

| Budget | Value | Within its limit |
| --- | --- | --- |
| GPIO | about 51 committed signals against 48 GPIO, before the accelerometer path, the converter and port-ceiling status lines and the latch read-back. No pin map exists | no |
| PIO state machines | 10 of 12: 8 for bidirectional DShot on the 4 multiprotocol ports, 1 for PPM (pulse-position modulation) on one PWM (pulse-width modulation) port, 1 for the receiver input. Assumed, by P5 and upheld by its critic: 1 PIO state machine serves the board's one receiver input, whichever bus is selected; the programmer modes take 0 state machines beyond the 8 booked for bidirectional DShot, because a multiprotocol port runs one protocol at a time | yes |
| PIO instruction memory | 68 of 96 words: 64 for bidirectional DShot in 2 whole blocks, 4 for PPM; 28 words free in the third block for the receiver and programmer programs, which are not written | yes |
| DMA (direct memory access) channels | 2 of 16, for PPM | yes |
| PWM slices | 16 of 24 channels for the 16 PWM ports, if the pin map avoids the shared compare registers | yes |
| ADC (analogue-to-digital converter) inputs | 0 of 8 with the external ADC of Q4; up to 4 with the reference option | yes |
| SPI (Serial Peripheral Interface) controllers | up to 5 devices on 2 controllers: the 2 CAN controllers, the load-cell ADC, the thermocouple converter and the external ADC. The timing between the CAN controllers' polling at 10 MHz and the other transactions is not checked | yes |
| UART controllers | 2 of 2: the OpenYGE socket and VESC's separate lines at 115,200 baud. Assumed, as in the PIO state machine row: the receiver input is decoded by 1 PIO state machine, not a hardware UART | yes |
| In-package flash | image limit 2,088,960 bytes against a build of 272,040 bytes plus the IO board's picture, whose size is not known (the module's is 206,000 bytes) | yes |
| Enable polarity | R4's buffer enable and R3's enable node are both high for enabled | yes |
| Power-up sequence | the 3.3 V ramp time is not stated, so the reset supervisor's delay margin is not computed | yes |
| Allowlist | every kept part is from a maker on S1, S2 or S9 | yes |
| Absent IOVDD | R1 confirms 3.63 V at an IOVDD (input/output supply) of 0 V on GPIO0 to 39, so the statement of pull request #167 stands | yes |
| Shared-part stock | MCP2518FDT-E/SL and TCAN3413DR at 2 placements, TPSI3052DWZR at 4 (gate 100), SN74CBTLV3126PWR at 2: each passes. SN74LVC1G04DBVR at 1, 2 or 3 placements: 53,165 at JLCPCB clears the largest gate, 75 | yes |

The combination P5 returns and its critic upheld, for the stated bind order:
the 4 multiprotocol ports bound first with bidirectional DShot, then the one
PPM-capable PWM port, then the receiver input. It takes
10 of 12 state machines, 68 of 96 instruction words, 2 of 16 DMA channels
and 16 of 24 PWM channels, with the other 15 PWM ports on hardware PWM
slices. It rests on the
receiver assumption above: 1 PIO state machine for the one receiver input. A
second combination, a fifth or sixth bidirectional DShot user bound ahead of
the receiver and PPM, is not evaluated: nothing in the specification names
one.

## Not known

Each item is reported to the owner for round 2, with its evidence.

- R1, USB protection: no qualifying part is known. No candidate covers the 5 lines (VBUS, the USB supply; D+ and D−; CC1 and CC2, the configuration channel pins) while holding D+ and D− at the 3.63 V steady limit and meeting IEC (International Electrotechnical Commission) 61000-4-2 level 4 (V9, V12, V13). The OVP (overvoltage protection) switches read trip at up to 5.2 V, and the Type-C protectors with configuration-channel and VBUS coverage carry no D+ and D− path. Three passes (FU-A1, FU-A2, FU-A4) found no survivor (`hardware/research/round1/FU-A4/006-P4-datasheet-R1.json`, `FU-A4/004-rerank-R1.json`). The owner relaxes the requirement to match standard parts (2026-09-30): where a standard part misses the 3.63 V steady limit or another figure of V9 and V12, its datasheet figure stands. Round 2 selects the parts.
- R1, the stop deadline of V1: not known. No primary source in the tree gives the time from the coprocessor ceasing to feed the watchdog to every gate at its reset state; the 200 ms figures in the tree count other intervals (`FU-A4/002-P2-R1.json`). It goes to the owner as an unknown, and the owner sets it at 500 ms (2026-09-30).
- R1, the longest legitimate stall of V2: not known, and not bounded as the firmware is written. pico-sdk 2.3.1's `stdio_usb_out_chars()` restarts its 500 ms timeout on every byte the USB host accepts, so a slow host can hold the main loop without limit, and no watchdog timeout exceeds that (`FU-A4/002-P2-R1.json`). The firmware needs a cap on a console write to a slow host. The watchdog timeout bounds a whole pass between two feeds, and with the reset stays within the 500 ms stop deadline (owner, 2026-09-30). One pass can erase a sector, up to 400 ms, and then write to the console (`firmware/iomcu/src/main.c:1049-1067`), so the console write, the rest of the pass and the reset share 100 ms while the output store stays in flash; Q8's FRAM takes the erase out of the loop.
- R3, onboard pack switch MOSFETs: not known. IPT015N10N5ATMA1 is refuted on safe operating area during the controlled 300 A turn-off of V48: its VGS(th) (gate threshold voltage) spread of 2.2 to 3.8 V and its RDS(on) (on-resistance), which rises with temperature, mean the 8-device array does not share the current equally through the 16.9 µs turn-off, so one device can carry a multiple of the 37.5 A average. The adjudicators of FU-A3 let the refutation stand, and no replacement is searched (`FU-A3/015-P4-datasheet-R3.json`, `FU-A3/016-adjudicator-R3.json`).
- R3, external pack switch module: not known. VS-FC420SA10 and its alternate VS-FC270SA20 are refuted on safe operating area and temperature rise during the 600 A controlled turn-off (V48, V50): their Fig. 14 single-pulse curves (about 136 to 169 A at 85 V a module on the 100 µs line) do not support 600 A shared equally over 5 modules through the 34 µs turn-off (`FU-A3/017-adjudicator-R3-2.json`, `FU-A3/018-adjudicator-R3-3.json`).
- R3, pack overvoltage and transient clamp: not known. No candidate clamps under 85 V at up to 600 A (V45, V48, V54): SMCJ70A clamps to 113 V at 13.3 A, and SMCJ64A, SMCJ75A and SMCJ78A to 121 to 126 V at 12 A. A TVS (transient-voltage-suppression) or Schottky diode cannot limit the switched node by construction (`FU-A3/003-P2-R3.json`). P5's budget holds the node under the clamp level by the switch's controlled turn-off instead: see [Supply](Supply.md#power-budget).
- R3 and R4, SN74LVC1G04DBVR placements: not known. R3 books 1 placement for its inverter on an active-low output enable and calls it the one R4 shares; R4 books 2 for its enable inverter (one for the 4 OE (output enable) pins, or one per 244). The two counts are not cross-checked. JLCPCB C7827 holds over 53,000, so the stock clears any of the counts (`FU-A3/009-P4-stock-R3.json`, `FU-A3/008-P4-stock-R4.json`).
- R4, gate coverage of VESC's lines: not known. V67 moves VESC's 115,200 baud link onto separate transmit and receive lines. F13 puts the 20 output ports and the UART socket behind the gate; VESC's lines are neither, and selection.json has no VESC path. If they do not pass the gate they stay driven when the heartbeat stops (V67 and F13 in [Research](Research.md), `hardware/research/round1/selection.json` R4; found by the P5 critic in T5).
- R3, the latch's place: not known which source holds. The specification states that the latch's place relative to the enable node and the gates is not stated ([IO board](IOBoard.md#link-and-safety), Latch). FU-A3 found V30: the latch sits between the enable node and the gates, so the enable node keeps the monostable's own timing for test steps 7 and 10 of pull request #167 (`hardware/research/round1/FU-A3/003-P2-R3.json`, confirmed in `FU-A3/015-P4-datasheet-R3.json`).
- R4, the number of receiver inputs: not known which source holds. The specification states that the number of receiver pins the board brings out is not stated ([IO board](IOBoard.md#summary), Receiver inputs). FU-A3 found V68: 1 receiver input, from `docs/Receivers.md:10-16`, which gives each bus one signal wire and no count for the board (`hardware/research/round1/FU-A3/010-P4-datasheet-R4.json`). P5 sizes its receiver assumption on that 1 input.
- The GPIO budget of the whole board (R1, R2, R4, R8, R11, R13): not known whether it fits. P5 counts about 51 signals against 48 GPIO, on sharing assumptions no pin map confirms: a separate SPI bus per CAN controller, one shared bus for the load-cell ADC and the thermocouple converter. It is likely tight or over, for the pin-mapping work to settle (`hardware/research/round1/T5/002-P5.json`).
- The 25 conflicts and gaps up to T5 that no follow-up after their check covered: each is on this page, on [Supply](Supply.md#not-known) or on [Sensing](Sensing.md#not-known), stated as not known, and reported to the owner for round 2.
- The conflicts up to T5 name parts no run selected: INA238AIDGSR and TPS55285VALR, both held or fixed inputs, and both refuted. Their conflicts are on [Supply](Supply.md#not-known). The owner keeps the TPS55285, three a board, each with a heatsink and a temperature shutdown (2026-09-30); the owner fixes the INA228 in place of the INA238 (2026-09-30).
- T5 has no budget, upheld by its critic, for PIO GPIO windows, I²C (Inter-Integrated Circuit) controllers, the I²C address map, I²C bus timing, the servo current abort, the shared-part stock of the RP2354B, the current of the receiver supply and the current of the logic supply from the RP2354B's USB port. These budgets are not known.
- T5 has 7 combinations and budgets its critic rejected: the PIO GPIO windows, the I²C controllers, the I²C address map, I²C bus timing on the bus carrying 4 port monitors, I²C bus timing on the bus carrying the other monitors, the servo current abort, and the assumed servo current sample period. None of them is on the pages; each is not known.
- T5 has 6 budgets over their limit: the GPIO count (this page), the servo rail current, the cell rating, the pack protection, the motor overcurrent path and the shared-part stock of INA238AIDGSR, in whose place the owner fixes the INA228 (2026-09-30) ([Supply](Supply.md#power-budget)). Each is a conflict of the board; how it is resolved is not known.
- T5 states no assumption, upheld by its critic, for the servo current sample period. The period is not known: P5 assumed 20 ms, and its critic found the tree gives 5 ms a channel (V173).

## Not verified

Each function below has no part selected by a verified run, or its part is
not requalified. The owner accepted each as open (owner, 2026-09-29).

- R1: Microcontroller (RP2354B not requalified by FU-A4). The held fixed input; not verified for R1 in FU-A4.
- R1: USB protection. No part; not verified.
- R2: Link terminator (the 120 Ohm at the IO board's end: fitted, switchable or external). A round-2 passive; not verified.
- R2: CAN controller clock (the crystal, resonator or oscillator each controller takes). A round-2 part; not verified.
- R3: onboard pack switch MOSFETs. No part; not verified.
- R3: external pack switch module. No part; not verified.
- R3: pack overvoltage and transient clamp. No part; not verified.
- R3: timing network (C and R of the monostable). Round-2 passives, set on test; not verified.
- R3: heartbeat-node series resistors, links and fail-safe pull-downs. Round-2 passives; not verified.
- R4: level translation. No part of its own: none is needed at the 3.3 V connector level (V63); not verified.
- R4: series resistance and clamps. Round-2 passives; not verified.
- R4: reset idle level. Round-2 passives; not verified.
- R4: programming supply switching. No part; not verified.
- R3: FU-A3 selected its parts without the Q9 decision that is in force. A choice that adds or changes a part needs an R3 follow-up and a P5 and P6 check after it. The owner's Q9 decision keeps the parts R3 selected (74HC423BQ,115 with SN74AUP1G17DBVR in front of its trigger inputs), so no part is added or changed; the selection is not verified against Q9 by a run.

Figures of group A that no P4 verifier confirmed; each is not verified:

- R1: figure found V1, not confirmed in FU-A4; not verified.
- R1: figure P2 report: board header, not confirmed in FU-A4; not verified.
- R1: figure P2 report: USB protection: no survivor, not confirmed in FU-A4; not verified.
- R2: figure re-rank report: CAN controller clock (P3's missed function), not confirmed in FU-A2; not verified.
- R2: figure re-rank report: External CAN transceiver: P3 finds against the ranked parts, not confirmed in FU-A2; not verified.
- R2: figure function requirements: Link CAN transceiver, not confirmed in FU-A2; not verified.
- R2: figure function requirements: External CAN transceiver, not confirmed in FU-A2; not verified.
- R3: figure P2 report: onboard switch turn-off, not confirmed in FU-A3; not verified.
- R3: figure re-rank drop: reset supervisor: TPS3850H33DRCT, not confirmed in FU-A3; not verified.
- R4: figure found V75, not confirmed in FU-A3; not verified.
- R4: figure P2 report: series resistance range, not confirmed in FU-A3; not verified.
- R4: figure P2 report: clamp level against the switch alternates, not confirmed in FU-A3; not verified.
- R4: figure re-rank report: rerank R4: series resistance on the PWM lines, not confirmed in FU-A3; not verified.
