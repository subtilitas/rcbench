# Where the hardware stands

The running record for `hardware/`. Stock figures carry the date they were
taken and are not valid after it.

**Status: no board exists.** No schematic and no layout exist. Round 1 of
[the component research](docs/Research.md) ran from 2026-09-28 to 2026-09-30
and selected 49 parts for the IO (input/output) board, each with its stock in
[Parts](docs/Parts.md) and its reason on the page of its group:
[Control](docs/Control.md) (R1 to R4), [Supply](docs/Supply.md) (R5 to R8) and
[Sensing](docs/Sensing.md) (R9 to R13). The owner decided Q4, Q8 and Q9 on
2026-09-29. Three held or fixed inputs do not stand: the TPS55285 is refuted
on its ambient range, the INA238 fails the stock gate at both vendors, and no
18650 cell is selected. The RP2354B is held in the owner's JLCPCB personal
parts library, 20 of them, and 12 TPS55285, beside the other parts listed
under [Held parts](docs/Research.md#held-parts) (export of 2026-09-25).
[The IO board specification](docs/IOBoard.md) is a draft with the chosen part
on each line. The workflow scripts are in `tools/research/` (prerequisite 6),
and the returns of every task are under `hardware/research/round1/` on the
branch `research/round1-results`.

## Decided

| | Part | Reason |
| --- | --- | --- |
| Microcontroller | RP2354B | the owner's choice, held in the owner's JLCPCB personal parts library: the RP2350B die and 2 MB of flash in one QFN-80 (quad flat no-lead) package, 48 GPIO (general-purpose input/output), stepping A4 (owner, 2026-09-27), taken from the marking RP2354B0A4 in JLCPCB's product photograph of C39843328; the marking on the held parts is not read. The 2 MB changes three places in the coprocessor build. The IO board needs a board file that states the B package, 2 MB of flash and no PSRAM (pseudo-static random-access memory). On the IO board the output store sits in the last two sectors of the 2 MB, and the image limit is 2,088,960 bytes. The tree places the store at 4 MB less 8 kB and holds the limit at 4,186,112 bytes. [IO board](docs/IOBoard.md#microcontroller-flash-and-debug), [Research](docs/Research.md#consequence-of-the-rp2354bs-2-mb-flash) |
| Board power | 12 to 20 V DC (direct current) input and the 2S (two cells in series) pack | the DC input, when present, runs the IO board and the display. The pack runs both otherwise. The selection is automatic. The display is powered through the link cable. The pack holds 2 lithium-ion 18650 cells. It is disconnected in hardware when any cell falls to 3.0 V, read per cell from its balance lead (question F9 in [Research](docs/Research.md#blocking-answered-before-the-research-tasks), owner, 2026-09-25). At that floor the pack is 6.0 V at the lowest (2 × 3.0 V); full charge is 8.4 V (2 × 4.2 V). The cells come from Samsung SDI, LG Energy Solution, Molicel, EVE, Murata or Panasonic (question S9, owner, 2026-09-27). Owner decision, 2026-09-24; 12 to 20 V, 2026-09-25 |
| Motor current path | INA238 on the IO board, two shunt positions | an onboard shunt for 150 A continuous (question F2) with a temperature sensor beside it, and an input for an external shunt for currents above 150 A, to 300 A and more (owner, 2026-09-25), connected by its sense leads. The two paths replace each other: a test is cabled through one, never both (owner, 2026-09-25). One INA238 for each path (question F1). The ESC (electronic speed controller) pack has up to 16 cells, 67.2 V at 4.2 V a cell (question F15), under the INA238's 85 V. Owner decisions, 2026-09-24 and 2026-09-25 |
| Output ports | 20 typed ports | 16 PWM (pulse-width modulation) ports with enable, pulse range and a frame rate per pair, one of them switchable to PPM (pulse-position modulation), and 4 multiprotocol ports (servo PWM, DShot, bidirectional DShot, ESC telemetry, servo configuration, ESC bootloader); a one-pin UART (universal asynchronous receiver-transmitter) socket; an external CAN (Controller Area Network) port on its own controller. Each port has a supply switch, a current monitor channel and an LV (low-voltage) or HV (high-voltage) jumper that cuts the port off above 6.0 V or 8.7 V (question F16). Owner decision, 2026-09-25 |
| Sensor inputs | load cells, a phase-wire clip, a magnetic pickup, motor temperature sensors, an encoder | thrust and torque come from load cells. Rotation speed in rpm (revolutions per minute) comes from a phase-wire clip and a magnetic pickup. The motor's temperature is measured. The encoder is ABI (quadrature A and B plus an index pulse) and is counted in the pin budget. Load cells on 3 channels, 1 for thrust and 2 for torque, at 5 V excitation (question F3). The encoder takes 5 V single-ended up to 1 MHz or RS-422 (Recommended Standard 422) differential up to 5 MHz, and a 5 V or 12 V supply, each selected by a jumper (question F4). Motor temperature on 2 channels, 1 infrared and 1 thermocouple (question F5). 2 external I²C (Inter-Integrated Circuit) ports at 3.3 V, Qwiic (question Q7). Owner decisions, 2026-09-24 and 2026-09-25. [IO board](docs/IOBoard.md#measurement) |
| Safety gate | a heartbeat monostable, specified in pull request #167 at commit 23c82ca, not merged | the single-channel OR design (question F10); a hardware latch cleared only by an operator control (F11); a reset supervisor as the clear network (F12); the 20 output ports and the UART socket behind the gated buffer (F13); a pull-up on a bidirectional DShot line and a pull-down on every other (F14); on the ESC pack a MOSFET (metal-oxide-semiconductor field-effect transistor) array and driver on the board for the onboard path, and a driven external module above 150 A (F7). Owner decisions, 2026-09-25. The parts are in research categories R3 and R4 |
| Display supply | 5 V through the link cable | 2 A peak, 1.5 A continuous (question F6 and prerequisite 7 in [Research](docs/Research.md#prerequisites)). Owner decisions, 2026-09-25 |
| First build | 5 boards | the parts are bought into the personal library before the order (question S7). A part's stock gate is 5 times the build's need, never under 50 (S4). Owner decisions, 2026-09-25 |
| Coprocessor watchdog | the RP2350's internal watchdog | a frozen coprocessor resets, and every gate it drives opens while its pins are at their reset state. Owner decision, 2026-09-25. The timeout is not stated, and the coprocessor image enables no watchdog |
| Output combinations | the combinations P5 returns | P5 is the cross-category check of [the research](docs/Research.md#agent-layout). Bidirectional DShot takes 2 PIO (programmable input/output) state machines per output; the 4 multiprotocol ports take 8 of the 12. The board supports the combinations P5 returns; the coprocessor refuses a bind outside them and the OUTPUTS page shows the refusal. Owner decision, 2026-09-25 |
| Servo supply | TPS55285 | 12 held in the owner's personal library (owner, 2026-09-25). It senses current internally and its I²C limit stops at 6.35 A, so the rail runs to 6.35 A. Its input takes 2.4 to 22 V, so the DC input is 12 to 20 V. The TPS55288, which limits across an external shunt to 10.1 A, is not used. Round 1 refutes it on its ambient range at the pack floor, and no servo supply is selected: see the Servo supply row under [Open](#open). [Power](docs/Power.md) |
| Motor monitor | INA238 | 85 V, 16 bit. Its step at 300 A with a 50 µΩ shunt is 25 mA. It has the same 10-pin VSSOP (very thin shrink small-outline package) and the same pin order as the INA228. Neither vendor could supply the INA228 on 2026-09-01: 29 at JLCPCB with a presale count of −477, and 0 at Digi-Key. On the figures of 2026-09-01 the INA238 has no second source that passes [the README's rule](README.md#rules): 0 at Digi-Key with 2500 due 2026-10-27, and the INA228 as above. Digi-Key's API (application programming interface) on 2026-09-27: 0 in stock in every packaging, manufacturer lead time 16 weeks, status Active. Round 1 read it again on 2026-09-29: JLCPCB 0 with a presale count of −99, Digi-Key 0, so no motor monitor is selected: see the Motor monitor row under [Open](#open). [Power](docs/Power.md#motor-monitor-ina238), [Research](docs/Research.md#sourcing-rules) |
| Monitor footprint | one for both | INA228 and INA238 are pin-identical and distinguishable at run time by DEVICE_ID (0x3F: 0x2281 or 0x2381). On the figures of 2026-09-01 the shared footprint gives the INA238 no second source, because the INA228 could not be bought (Motor monitor row) |
| Servo rail | two output settings | up to 5.5 V for LV (low-voltage) servos and up to 8.4 V for HV (high-voltage) servos, 4 to 6.35 A. The converter runs from the 12 to 20 V DC input or from the 2S pack (owner, 2026-09-24). 6.35 A at 8.4 V from the pack is not checked. Research category R6 checks 8.4 V and 4.0 A from the 6.0 V pack floor at 50 °C ambient, and the TPS55285 fails its ambient range there: see the Servo supply row under [Open](#open). [Power](docs/Power.md) |
| Pack charger | BQ25713RSNR, with MAX17320G22+ and FUSB303BTMX, [Supply](docs/Supply.md) | charged from USB-C (Universal Serial Bus Type-C) only, drawing up to 3 A at 5 V, and not from the DC input (question F8 in [Research](docs/Research.md), owner, 2026-09-25). Research round 1 selects the BQ25713RSNR as the charger and power path, the MAX17320G22+ for balancing at 50 mA average and for pack protection, and the FUSB303BTMX for the USB-C input limit (R7). The BQ25887 (2 A, 400 mA per-cell balancing, [Power](docs/Power.md)) takes 3.9 to 6.2 V and is not selected. No 18650 cell is selected: see the Pack cells row under [Open](#open) |
| Port monitors | INA3221AIRGVR, 7 parts, with an ERJ-6CWDR010V shunt on each port, [Supply](docs/Supply.md) | one channel per port, 20 ports: the limit search needs one sensor per servo. The owner's choice is the INA3221, 7 parts (21 channels), a 10 mΩ shunt each: 4 mA resolution, 16.4 A full scale, two I²C buses (owner, 2026-09-25). Research round 1 keeps it (R8) |
| Onboard shunt, 150 A | BVR-Z-R0002-1.0, [Supply](docs/Supply.md) | on the IO board, with a temperature sensor beside it, the MCP9808T-E/MS (R8). 150 A is continuous (question F2, owner, 2026-09-25). At 150 A a 100 µΩ shunt drops 150 A × 100 µΩ = 15 mV and dissipates (150 A)² × 100 µΩ = 2.25 W continuously. A 200 µΩ shunt drops 30 mV and dissipates 4.5 W. Both drops are inside the INA238's ±40.96 mV range |
| External shunt, 300 A and above | WSBE8518L1000JKA2, off the board, [Supply](docs/Supply.md) | sense leads to the IO board. Class: busbar type, 50 to 100 µΩ, 4.5 to 9 W at 300 A. The four-terminal SMD (surface-mount device) parts recorded in [Power](docs/Power.md#motor-monitor-ina238) stop at 0.2 mΩ and would dissipate 18 W at 300 A. The WSBE8518L1000JKA2 is 100 µΩ, bought at Digi-Key, the second vendor of question S3, which held 418 on 2026-09-29. No alternate is found, so it has no second source (sourcing rule 5), and the owner has the report. The sense-lead connector is round 2 |
| Parts of group A | 21 parts, [Control](docs/Control.md) | ABM8-272-T3 and AOTA-B201610S3R3-101-T (R1); MCP2518FDT-E/SL and TCAN3413DR, 2 of each (R2); 74HC423BQ,115, 74AUP1G32GW,125, TPS3703A7330DSERQ1, 74LVC1G175GW,125, 434153017835, SN74AUP1G14DBVR, SN74AUP1G17DBVR, BTS7004-1EPP, TPSI3052DWZR, IPT015N10N5ATMA1, CSD19537Q3T, 2N7002BK,215 and SN74LVC1G04DBVR (R3); SN74LVC244APWR with its alternate 74LVC244APW,118, SN74LVC1G126DBVR, SN74CBTLV3126PWR and SN74LVC1T45DBVR (R4). Research round 1, verified by its two verifiers of phase P4, which re-read stock, lifecycle and every datasheet value |
| Parts of group B | 15 parts, [Supply](docs/Supply.md) | LM74800QDRRRQ1, LMQ66430MC3RXBRQ1, TPS62933DRLR, TPS259474ARPWR and TPS55288RPMR (R5); TPS259474LRPWR and MCP23017T-E/SS (R6); BQ25713RSNR, MAX17320G22+ and FUSB303BTMX (R7); BVR-Z-R0002-1.0, MCP9808T-E/MS, WSBE8518L1000JKA2, INA3221AIRGVR and ERJ-6CWDR010V (R8). Research round 1, verified by the P4 verifiers |
| Parts of group C | 13 parts, [Sensing](docs/Sensing.md) | ADBMS1818ASWAZ-RL (R9); ADCS7476AIMFX/NOPB (R10); ADXL316WBCSZ, DRV5015A1QDBZR, TLV3201AIDBVR with its alternate TLV3601DBVR, AM26LV32EIDR and the RP2354B's PIO (R11); MAX31856MUD+T, MLX90614ESF-BCC-000-TU, TCA9548APWR with its alternate PCA9548APWR and MB85RC256VPNF-G-AMERE2 (R12); ADS1235IRHBR and BD450M2FP3-CE2 (R13). Research round 1, verified by the P4 verifiers |
| Accelerometer converter (Q4) | ADCS7476AIMFX/NOPB, an external ADC (analogue-to-digital converter) | the part R10 kept (owner, 2026-09-29). The RP2354B's ADC with the LM4040AIM3-3.0/NOPB reference is the verified option not chosen |
| Output binding store (Q8) | MB85RC256VPNF-G-AMERE2, an I²C FRAM (ferroelectric random-access memory) | the part R12 kept (owner, 2026-09-29). The EEPROM (electrically erasable programmable read-only memory) M24256E-FMN6TP is the verified option not chosen |
| Monostable isolation (Q9) | 74HC423BQ,115 with SN74AUP1G17DBVR in front of its trigger inputs | the 74HC423 keeps the '423 behaviour, and the buffer supplies I_off, a limit on the input current of an unpowered part, as R3 selected (owner, 2026-09-29). No part is added or changed |
| Stock exception | ADS1235IRHBR | Digi-Key, the second vendor, read stock 0 against the gate of 50 on 2026-09-29; the owner keeps the part and notes the exception (owner, 2026-09-29) |
| Stock source | the vendor's own API (application programming interface) | on 2026-09-01 the mirror `jlcsearch.tscircuit.com` reported 1046 INA228AIDGSR. JLCPCB's own API reported 29, with a presale count of −477: oversold. [Sourcing](docs/Sourcing.md#observed-divergence-2026-09-01) |

## Open

The first nine rows come from round 1 and are reported to the owner for
round 2. The group pages state each with its evidence, as not known or not
verified.

| Item | State | Needs |
| --- | --- | --- |
| Servo supply | the held TPS55285VALR is refuted on its ambient range: at 6.0 V in, 8.4 V and 4.0 A out and 50 °C ambient its estimated junction temperature spans 89 to 179 °C against 125 °C, and its datasheet gives no loss of the IC (integrated circuit) there. No servo supply is selected. Its inductor SRP1265A-4R7M is confirmed by both verifiers of the follow-up task FU-B3 and is not carried in `selection.json`. Not known ([Supply](docs/Supply.md#not-known)) | the owner's ruling on the fixed input |
| Motor monitor | the INA238AIDGSR fails the stock gate at both vendors on 2026-09-29: JLCPCB 0 with a presale count of −99, Digi-Key 0, 10 needed for 5 boards. On the external shunt's sense input it is also refuted on its ±40 V differential rating against the 85 V fault. Not known ([Supply](docs/Supply.md#not-known)) | a source for the part, and a protection for the sense leads |
| Pack cells | INR-18650-P30B (Molicel) passes every check and is sold only at Liion Wholesale, not at Digi-Key, which sourcing rule 1 names for a part off the board. No cell is selected, so the cell rating and the pack protection have no confirmed cell. Not known ([Supply](docs/Supply.md#not-known)) | the owner's ruling on the vendor |
| ESC pack switch | the onboard MOSFETs (IPT015N10N5ATMA1) and the external module (VS-FC420SA10, VS-FC270SA20) are refuted on safe operating area during the controlled turn-off; no clamp part holds the pack node under 85 V at up to 600 A. Not known ([Control](docs/Control.md#not-known)) | a follow-up search or a trade-off by the owner |
| USB protection | no candidate covers the 5 lines of the RP2354B's USB (Universal Serial Bus) port at the 3.63 V limit of D+ and D−. Not known ([Control](docs/Control.md#not-known)) | a search in round 2, or a relaxed requirement |
| Watchdog | the stop deadline (V1) is not found, and the longest main-loop stall (V2) is not bounded while a slow USB host can hold a console write. Not known ([Control](docs/Control.md#not-known)) | the owner's deadline, and a cap on the console write in firmware |
| Pack disconnect and port voltage ceiling | no candidate passes: the disconnect fails an open balance lead, the reconnect condition or the per-cell floor; the ceiling fails the ±0.99 % HV trip band or the latch. Not known ([Supply](docs/Supply.md#not-known)) | a follow-up search |
| Optical index | no reflective sensor meets the 1.244 µs delay-variation budget. Not known ([Sensing](docs/Sensing.md#not-known)) | a follow-up search |
| Board budget | about 51 GPIO (general-purpose input/output) signals against 48; the FUSB303 address 0x21 inside the expanders' block; the servo current sample period, 5 ms a channel against the 20 ms P5 assumed; VESC's lines outside the gate. Not known ([Control](docs/Control.md#not-known), [Supply](docs/Supply.md#not-known)) | the pin map, the I²C address map and a P5 check after it |
| Converter input connector and heatsink | the converter runs from the 12 to 20 V DC input or the 2S pack (owner, 2026-09-24). | the input connector (round 2) and the heatsink (round 3) |
| Layout | Isolation, a 150 A path and a 3.3 V I²C bus on one board, connectors, thermal. | the schematic first |

## Not planned

- **A hall-effect sensor for the 300 A path.** It avoids the shunt's 9 W and
  its layout, at the cost of gain and offset drift on the bench's primary
  measurement. The shunt is the measurement.
- **A 20-bit motor monitor.** The INA228 could not be bought from either
  vendor on 2026-09-01: 29 at JLCPCB with a presale count of −477, and 0 at
  Digi-Key. The INA238's 25 mA step with a 50 µΩ shunt is 0.008 % of 300 A.
  The noise a running ESC puts on the wire is not measured.
- **Bilingual pages** while no board exists. See [the README](README.md).

## Order of work

1. The owner's rulings on the open items of round 1 ([Open](#open)): the
   TPS55285, the INA238, the pack cells' vendor, the ESC pack switch and
   the other items without a part.
2. The follow-ups those rulings call for. Round 1 of the component
   research ran from 2026-09-28 to 2026-09-30, as in
   [its scope](docs/Research.md#scope): the ICs, the parts that fix an IC's
   surroundings, both motor shunts, the switch of the ESC pack with its input
   protection, and the 18650 cells of the bench's own pack, in six tasks
   (question S8) and 9 follow-up tasks, each of at most 32 agents (owner,
   2026-09-24). The commit each task read is in the status line of
   [Research](docs/Research.md).
3. Round 2 selects the passives and the connectors, the DC input connector
   among them. It also selects the protection on the signal, sensor,
   balance-lead, link and heartbeat connectors. The power input's protection
   (R5) and the ESC pack path's (R3) are in round 1. Round 3 sets the mechanical, thermal and layout constraints,
   the converter's heatsink among them ([Research](docs/Research.md#scope)).
4. Schematic.
5. Order each part with a manufacturer lead time of 16 weeks or more as soon
   as the schematic fixes it. On 2026-09-01 Texas Instruments quoted 16 weeks
   on every power-path part checked, and 26 weeks on the INA745x and the
   INA260 ([Sourcing](docs/Sourcing.md#the-2026-09-01-sweep)). Once the
   schematic exists, the build waits up to 26 weeks for those parts.
