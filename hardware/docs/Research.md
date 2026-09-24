# IO board component research, round 1

The plan for the multi-agent research that selects the IO board's integrated
circuits (ICs). **It has not run.** It starts when the owner accepts this page
and has answered the [questions](#questions-for-the-owner).

The IO board is the coprocessor board: the RP2354B, the CAN (Controller Area
Network) link to the display, the outputs, the receiver inputs, the sensor
front ends and the power path. [The specification](IOBoard.md) lists what it
has to do; this page lists how its parts are found.

## Scope

Round 1 selects the ICs, plus the few parts that fix an IC's surroundings:
the RP2354B's crystal and regulator inductor, the servo supply's inductor and
sense resistor, and the motor shunt.

| Round | Selects |
| --- | --- |
| 1 | ICs: microcontroller support, CAN, safety gate, output and input buffers, power conversion and protection, current and voltage monitors, cell monitor, ADC (analogue-to-digital converter) and reference, sensor front ends, non-volatile store |
| 2 | passives, ESD (electrostatic discharge) and overvoltage protection, connectors, crystals and inductors not fixed in round 1 |
| 3 | mechanical, thermal, layout constraints, the 300 A path |

Fixed inputs. Round 1 checks them for stock and lifecycle and does not
re-select them unless one fails:

| Input | Value | Source |
| --- | --- | --- |
| Microcontroller | RP2354B: the RP2350B die and a Winbond W25Q16JV 2 MB QSPI (quad serial peripheral interface) NOR flash stacked in one QFN-80 (quad flat no-lead, 80 pads) 10 × 10 mm package; 48 GPIO (general-purpose input/output), 8 ADC inputs, 520 kB SRAM (static RAM) on the die, no PSRAM (pseudo-static RAM) in the package | owner; stock in the owner's JLCPCB personal parts library |
| Servo supply | TPS55288 | [Power](Power.md) |
| Motor monitor | INA238 | [Power](Power.md) |
| Pack charger | BQ25887, re-opened by the power decision below | [Power](Power.md) |

Owner decisions of 2026-09-24:

| Decision | Value |
| --- | --- |
| Power | a 12 to 24 V DC input runs the IO board and the display when present; the bench's own 2S pack runs both otherwise; the selection is automatic. The display is powered through the link cable |
| Motor current | the INA238 is on the IO board. An onboard shunt carries up to 150 A, with a temperature sensor beside it. An external shunt, for 300 A and above, connects to the IO board by its sense leads |
| Servo current | one current monitor per socket, 8 sockets, beside the per-socket supply switch |
| Sensor inputs | load cells for thrust and torque, a phase-wire rpm (revolutions per minute) clip, motor temperature, a magnetic rpm pickup, and an ABI encoder (quadrature A and B, plus an index pulse) |

The stock figures in [Power](Power.md) are dated 2026-09-01 and state that they
are not valid after it. Round 1 takes them again.

### Consequence of the RP2354B's 2 MB flash

`firmware/iomcu/src/out_store.c` places the output binding in the last two
sectors of the first 4 MB and asserts `STORE_SIZE_BYTES <=
PICO_FLASH_SIZE_BYTES`. A board file with 2 MB fails that assertion at
compile time. The store moves to the top of 2 MB before the first IO board
build. The coprocessor image and the board picture share the same 2 MB; the
picture of the bring-up module is 143,762 bytes.

## Sourcing rules

Every agent applies these. They extend [Sourcing](Sourcing.md) and do not
replace it.

1. **JLCPCB's parts library only.** A part has an LCSC number (`C` followed by
   digits) and JLCPCB assembly places it.
2. **Allowlisted manufacturers only.** The list is question S1. A part from a
   manufacturer not on it is not a candidate, whatever its stock.
3. **Find in jlcparts, count at JLCPCB.** The `yaqwsx/jlcparts` database is a
   periodic snapshot: used to find candidates, never to count them. Stock comes
   from JLCPCB's own API (application programming interface), as in
   [Sourcing](Sourcing.md): `stockCount`, `canPresaleNumber`,
   `componentLibraryType` and the price breaks.
4. **Stock gate.** `stockCount` at or above the threshold of question S4, and
   `canPresaleNumber` zero or above. A negative presale count fails whatever
   the stock says.
5. **A second source.** Either a pin-compatible alternate that also passes
   rules 1 to 4, or the same part at a second vendor (question S3). The INA238
   and INA228 footprint in [Sourcing](Sourcing.md) is the pattern.
6. **Personal library.** Agents cannot read the owner's JLCPCB personal library;
   it needs a login. A part held there counts as stock only for the quantity the
   owner states.
7. **Every figure is dated and sourced.** A stock count carries the date and the
   URL or API call it came from. A search engine's summary of a vendor page is
   not a source.

## Lifecycle check

Recorded for every candidate that reaches the shortlist.

| Field | Source | Gate |
| --- | --- | --- |
| Manufacturer status | the manufacturer's product page | pass: active or in production. Fail: NRND (not recommended for new designs), last-time buy, obsolete, discontinued. Preview or sampling: fails unless question S5 accepts it |
| Longevity commitment | the manufacturer's longevity or product-lifecycle programme page, where it publishes one | recorded, and a gate only if question S5 makes it one. No published commitment is recorded as that |
| Market introduction | first datasheet revision date | recorded; under 12 months on the market is flagged |
| Change and discontinuation notices | the manufacturer's PCN (product change notice) listing, where public | any end-of-life notice fails |
| Distributor status | JLCPCB and LCSC part page; the second vendor's lifecycle field | recorded. Where it disagrees with the manufacturer, the manufacturer's page is the status and the disagreement is written down |

## Research categories

One research agent per row. The seeds are starting points for the search, not
choices, and none of them has been checked for stock, lifecycle or fit. The
requirement values each agent checks against are in
[the specification](IOBoard.md).

| ID | Category | Seeds |
| --- | --- | --- |
| R1 | RP2354B and its support: the part itself, 12 MHz crystal, core regulator inductor, 3.3 V supply, USB (Universal Serial Bus) protection | RP2354B; the inductor and crystal the RP2350 hardware design guide names |
| R2 | CAN controller and transceiver | MCP2515, MCP2518FD, MCP251863 (controller and transceiver in one package); TCAN1042V, TCAN334, SN65HVD230, TJA1051T/3, TJA1462 |
| R3 | Safety gate: retriggerable monostable, output-enable buffers, the switch that removes servo and ESC (electronic speed controller) power | 74LVC1G123; 74LVC8T245, 74LVC245A, 74AHCT125; load switches and eFuses |
| R4 | Output and input buffering: 3.3 V to servo and ESC signal levels, bidirectional lines for bidirectional DShot and one-wire programming, receiver inputs | LSF0108, 74LVC1T45, SN74LXC1T45; series resistance and clamps |
| R5 | Board power input and protection: reverse polarity, overvoltage, inrush, automatic selection between the 12 to 24 V DC input and the 2S pack, logic buck, 5 V rail, the display's supply on the link cable | LM74700, LM66200, TPS2663, TPS25947; LMR36015, TPS62933, TPS563300 |
| R6 | Servo supply: the TPS55288 re-checked from both inputs (12 to 24 V and 6.0 to 8.4 V), its inductor and sense resistor, per-socket supply switches, the per-socket voltage ceiling set in hardware | TPS55288; TPS22990, TPS22918, TPS2595 |
| R7 | Pack charger: charging the 2S pack from the DC input, from USB-C, or both, with balancing | BQ25887, BQ25798 with BQ76907 or BQ29209, MP2672A |
| R8 | Current and voltage monitors: the motor monitor with the onboard 150 A shunt and the external-shunt input, the temperature sensor at the onboard shunt, one monitor per servo socket | INA238, INA228, INA236, INA3221, INA745A; TMP117, TMP1075 |
| R9 | Cell monitor on the balance lead, 1 to 14 cells (the range `SET_PACK_CELLS` and the battery screen take) | BQ76952, BQ76942, ADBMS6948, LTC6813 |
| R10 | ADC and reference (question Q4): the RP2354B's own ADC with an external reference, or an external converter | REF3033, LM4040; ADS131M04, ADS1115, ADS112C04 |
| R11 | Rotation and vibration front ends: accelerometer, optical index pulse, magnetic pickup, phase-wire rpm clip rated for the pack voltage, ABI encoder input | ADXL1002, ADXL1005, IIS3DWB; TLV3201, TLV7011; AM26LV32, SN65LBC175 |
| R12 | Motor temperature (question F5), external I²C (Inter-Integrated Circuit) ports and the non-volatile store (questions Q7, Q8) | MCP9600, MAX31856, MLX90614; TCA9548A, PCA9615, TCA9617A; FM24CL16B, MB85RC256V, 24LC256 |
| R13 | Load cells for thrust and torque: bridge ADC and excitation (question F3) | ADS1232, ADS1234, ADS124S08, AD7124-4 |

## Agent layout

A workflow of five phases. Each research agent returns a fixed schema, so
the phases after it compare like with like.

```text
P0 reachability (1)
P1 discover and qualify (1 per category) --+-- P2 verify (2 per shortlisted part), pipelined per category
                                           |
P3 cross-category checks (1) <-------------+ waits for every category
P4 completeness critic (1) -- sends gaps back to P1, at most 2 extra passes
P5 write the pages (1)
```

| Phase | Agents | Does | Returns |
| --- | --- | --- | --- |
| P0 | 1 | fetches one known page from every host in [Prerequisites](#prerequisites) | reachable or not, per host; the run stops if JLCPCB's API or the jlcparts database is unreachable |
| P1 | 1 per category | reads its lines of the specification; lists candidates from allowlisted manufacturers in jlcparts; drops those that miss a requirement value; for up to five survivors records part number, manufacturer, LCSC number, package, every requirement value against the datasheet's, JLCPCB stock, presale, library type and price, second-vendor stock, incoming quantity and lead time, the lifecycle fields above, pin-compatible alternates, and whether a driver exists under `shared/` | a ranked shortlist with the reason for each rank |
| P2 | 2 per shortlisted part | two independent verifiers, each told to refute. One re-reads stock and lifecycle at the primary sources. One re-reads every requirement value in the datasheet. A part stays when neither refutes it; a refuted first choice sends the runner-up to verification | confirmed or refuted, with the evidence |
| P3 | 1 | pin budget against the RP2354B's 48 GPIO and the rule that a PIO (programmable input/output) block sees GPIO 0 to 31 or 16 to 47 only; the I²C address map; current per rail; the allowlist over the whole list | conflicts, each naming the parts involved |
| P4 | 1 | every specification line has a part or is marked "not round 1"; every figure has a date and a source | gaps, fed back to P1 |
| P5 | 1 | writes the outputs below | the pages |

Size: 13 categories give 1 + 13 + 26 to 52 + 3 agents, about 43 with one
verified part per category and about 69 with the alternate verified too
(question S8). A category that needs several parts (R5, R8, R11) verifies each
of them, so the real count is higher by a few. This container runs two agents at a time, so the run time grows
with the count. Not measured.

## Outputs

| File | Content |
| --- | --- |
| `hardware/docs/IOBoard.md` | the specification, with the chosen part on each line |
| `hardware/docs/Parts.md` | one row per part: function, part number, manufacturer, LCSC number, package, JLCPCB stock, presale and library type with the date, second source, lifecycle status, longevity, alternate |
| `hardware/docs/Power.md` and one page per category group in its form | the choice, the alternatives, the stock, the reason |
| `hardware/STATUS.md` | the decided and open tables |
| `tools/jlc_stock.py` | re-queries JLCPCB's API for every LCSC number in `Parts.md`; `--check` exits 1 when a part is missing, under the stock threshold, or oversold. Run by hand before an order, not in CI (continuous integration): a stock count moving is not a defect in the tree |

## Prerequisites

1. **Network access.** On 2026-09-24 this session's container was refused
   (HTTP 403 at the egress proxy) for `jlcpcb.com`, `www.lcsc.com`,
   `yaqwsx.github.io`, `www.ti.com` and `www.raspberrypi.com`. Web search was
   reachable, and [Sourcing](Sourcing.md) does not accept a search summary as a
   stock source. The research session needs, at minimum: `jlcpcb.com`,
   `www.lcsc.com`, `yaqwsx.github.io`, `datasheets.raspberrypi.com`,
   `www.raspberrypi.com`, the second vendor's site (question S3), and each
   allowlisted manufacturer's site. The alternative is the environment's full
   network access level.
2. **The owner's answers** to the questions below.
3. **The RP2354B quantity** in the owner's personal library.

## Questions for the owner

Answered on 2026-09-24: power, motor current, servo current and the sensor
inputs, recorded under [Scope](#scope). The rest are open.

### Sourcing

| ID | Question | Proposed answer |
| --- | --- | --- |
| S1 | Which manufacturers are allowed? | ICs: Analog Devices (with Maxim and Linear), Infineon (with Cypress), Microchip, Nexperia, NXP, onsemi, Raspberry Pi, Renesas, ROHM, STMicroelectronics, Texas Instruments, Toshiba, Diodes Incorporated, Vishay. Undecided: Monolithic Power Systems, Richtek, Silergy, SG Micro, 3PEAK, Nisshinbo, Torex, Allegro, Melexis, Bosch Sensortec, ams OSRAM |
| S2 | Does S1 apply to the passives in round 2 as well? | owner to state before round 2; round 1 selects no passives beyond the inductors, crystal and shunts in [Scope](#scope) |
| S3 | Is a second vendor still required, as [the hardware README](../README.md) states, or is JLCPCB alone the source? | keep the rule: a pin-compatible alternate at JLCPCB, or the same part at Digi-Key |
| S4 | What is the stock threshold? | 10 times the parts needed for the first build, and never under 100 |
| S5 | Which lifecycle states pass? | active only; preview fails; a longevity commitment is recorded and not required |
| S6 | Are extended-library parts acceptable? Each unique one adds a loading fee per order. | yes; basic preferred where two parts are otherwise equal |
| S7 | How many boards in the first build, and are the parts bought into the personal library ahead of the order? A part in the personal library is not substituted between quote and build. | owner to state |
| S8 | How many agents may the run use? | about 43: one verified part per category, alternates recorded unverified |

### Specification

| ID | Question | Proposed answer |
| --- | --- | --- |
| F1 | One INA238 switched between the onboard shunt and the external-shunt input, or one INA238 for each? | one for each: no switch sits in a sense path, and the firmware reads whichever is wired. The DEVICE_ID check in [Sourcing](Sourcing.md) already tells the two part types apart; the two positions differ by I²C address |
| F2 | Is 150 A on the onboard shunt continuous or a peak, and for how long? Which connector takes it? | owner to state. At 150 A a 100 µΩ shunt dissipates 2.25 W and a 200 µΩ shunt 4.5 W |
| F3 | How many load-cell channels (thrust, torque on one or two cells), and what excitation voltage? | owner to state |
| F4 | ABI encoder: single-ended at 3.3 V or 5 V, or differential RS-422? Its supply voltage, the highest count rate, and what it measures (servo output shaft, motor shaft)? | owner to state |
| F5 | Motor temperature: thermocouple, NTC (negative temperature coefficient thermistor) or infrared, and how many channels? | owner to state |
| F6 | The display's supply on the link cable: which voltage, and which connector for the cable? The display's current draw is not measured. | 5 V; the current is measured on the bring-up bench before R5 sizes the rail |
| Q4 | The RP2354B's ADC with a reference, or an external ADC for the accelerometer? | the owner decides after R10 reports ENOB (effective number of bits) against the balance measurement |
| Q7 | How many external I²C ports, at which voltage? | owner to state |
| Q8 | The output binding in the RP2354B's flash, or in an I²C FRAM (ferroelectric RAM)? A flash erase and program measured 19 ms on the bring-up module and loses CAN frames ([STATUS.md](../../STATUS.md#open-items)). | FRAM, or a CAN controller with a deeper receive FIFO (first-in, first-out buffer); R2 and R12 report both |
