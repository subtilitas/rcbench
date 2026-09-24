# IO board component research, round 1

The plan for the multi-agent research that selects the IO board's integrated
circuits (ICs). **It has not run.** It starts when the owner accepts this page
and has answered the [blocking questions](#blocking-answered-before-p2-starts).
The [decisions that wait on the research](#decided-on-the-researchs-output)
stay open while it runs.

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
| Sensor inputs | load cells for thrust and torque, a phase-wire rpm (revolutions per minute) clip, motor temperature, a magnetic rpm pickup, and the encoder (quadrature A and B plus an index pulse, ABI), which is the encoder in the pin budget of `firmware/iomcu/CMakeLists.txt` |

The stock figures in [Power](Power.md) are dated 2026-09-01 and state that they
are not valid after it. Round 1 takes them again.

### Consequence of the RP2354B's 2 MB flash

Two places in the coprocessor build assume 4 MB of flash, and both move to
2 MB before the first IO board build:

| Place | Assumes | With 2 MB |
| --- | --- | --- |
| `firmware/iomcu/src/out_store.c` | the output binding in the last two sectors of the first 4 MB; asserts `STORE_SIZE_BYTES <= PICO_FLASH_SIZE_BYTES` | fails to compile, which is the safe direction |
| `firmware/iomcu/CMakeLists.txt` | the post-link check `image_fits.cmake` with `-DLIMIT=4186112`, 4 MB less 8 kB | passes an image that reaches into the store's two sectors, and a later save erases firmware. The limit becomes 2,088,960 bytes, 2 MB less 8 kB |
 The coprocessor image and the board picture share the same 2 MB; the
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
| R3 | Safety gate, to the monostable specification in pull request #167 (`hardware/docs/Monostable.md`, not merged): a dual retriggerable monostable whose clear release does not trigger (the '423 behaviour), the OR gate, the output buffer with its enable, the power-on reset or supervisor, and the high-side switches on the servo rail and on the ESC (electronic speed controller) pack, open within 5 ms of the enable falling. Every logic input driven while its own supply is absent needs I_off (a specified input leakage limit at V_CC = 0 V) | 74HC423, 74HCT423; 74LVC1G32; 74LVC8T245, 74LVC245A; TPS3839; TPS48110, LTC7001, 2ED4820 for the ESC pack's MOSFETs (metal-oxide-semiconductor field-effect transistors) |
| R4 | Output and input buffering: 3.3 V to servo and ESC signal levels; bidirectional lines for bidirectional DShot; the programming connector, which carries the one-wire bootloader at 19,200 baud (BLHeli_S, AM32), the ESCape32 text CLI (command-line interface), VESC's framed packets at 115,200 baud and the Hitec D-series servo protocol (`shared/ui/programmer_screen.c`); receiver inputs | LSF0108, 74LVC1T45, SN74LXC1T45; series resistance and clamps |
| R5 | Board power input and protection: reverse polarity, overvoltage, inrush, automatic selection between the 12 to 24 V DC input and the 2S pack, logic buck, 5 V rail, the display's supply on the link cable | LM74700, LM66200, TPS2663, TPS25947; LMR36015, TPS62933, TPS563300 |
| R6 | Servo supply: the TPS55288 re-checked from both inputs (12 to 24 V and 6.0 to 8.4 V), its inductor and sense resistor, per-socket supply switches, the per-socket voltage ceiling set in hardware | TPS55288; TPS22990, TPS22918, TPS2595 |
| R7 | Pack charger for the 2S pack, with balancing, charging from the source question F8 names | BQ25887, BQ25798 with BQ76907 or BQ29209, MP2672A |
| R8 | Current and voltage monitors: the motor monitor with the onboard 150 A shunt and the external-shunt input, the temperature sensor at the onboard shunt, one monitor per servo socket | INA238, INA228, INA236, INA3221, INA745A; TMP117, TMP1075 |
| R9 | Cell monitor on the balance lead, 1 to 14 cells (the range `SET_PACK_CELLS` and the battery screen take) | BQ76952, BQ76942, ADBMS6948, LTC6813 |
| R10 | ADC and reference (question Q4): the RP2354B's own ADC with an external reference, or an external converter | REF3033, LM4040; ADS131M04, ADS1115, ADS112C04 |
| R11 | Rotation and vibration front ends: accelerometer, optical index pulse, magnetic pickup, phase-wire rpm clip rated for the pack voltage, the encoder input (ABI) | ADXL1002, ADXL1005, IIS3DWB; TLV3201, TLV7011; AM26LV32, SN65LBC175 |
| R12 | Motor temperature (question F5), external I²C (Inter-Integrated Circuit) ports and the non-volatile store (questions Q7, Q8) | MCP9600, MAX31856, MLX90614; TCA9548A, PCA9615, TCA9617A; FM24CL16B, MB85RC256V, 24LC256 |
| R13 | Load cells for thrust and torque: bridge ADC and excitation (question F3) | ADS1232, ADS1234, ADS124S08, AD7124-4 |

## Agent layout

Every agent's output is checked by a critic that did not produce it and is
told to refute it. Nothing reaches a page on one agent's word. Each agent
returns a fixed schema, so a critic compares like with like.

```text
P0  reachability (1)
P1  requirement critic (1 per category)       -- the owner reviews what it flags
P2  discover and qualify (1 per category)
      +-- P3 search critic (1 per category)    pipelined per category,
      +-- P4 verify (2 per shortlisted part)   no wait between categories
P5  cross-category checks (1) and its critic (1)   waits for every category
P6  completeness critic (1)                   gaps go back to P2, at most 2 extra passes
P7  write the pages (1) and a page critic (1)
```

P0 and P1 run first, as a workflow of their own. The owner reads what P1
flags; P2 onward runs after that.

| Phase | Agents | Does | Returns |
| --- | --- | --- | --- |
| P0 | 1 | fetches one known page from every host in [Prerequisites](#prerequisites) | reachable or not, per host; the run stops if JLCPCB's API or the jlcparts database is unreachable |
| P1 | 1 per category | reads the category's lines in [the specification](IOBoard.md) and every source they cite, and tries to refute each value: does it follow from its source, is the unit right, is it an owner decision or an assumption | each value marked sourced, owner decision or assumption; every assumption is a question to the owner |
| P2 | 1 per category | lists candidates from allowlisted manufacturers in jlcparts; drops those that miss a requirement value; for up to five survivors records part number, manufacturer, LCSC number, package, every requirement value against the datasheet's, JLCPCB stock, presale, library type and price, second-vendor stock, incoming quantity and lead time, the lifecycle fields above, pin-compatible alternates, and whether a driver exists under `shared/` | a ranked shortlist with the reason for each rank, and every candidate dropped with the reason |
| P3 | 1 per category | searches for part families P2 did not consider, from the same allowlist, and re-reads each reason P2 gave for dropping a candidate | missed candidates, which go through P2's qualification once; exclusions that do not hold |
| P4 | 2 per verified part | two independent verifiers, each told to refute. One re-reads stock and lifecycle at the primary sources. One re-reads every requirement value in the datasheet. The first-ranked part of each function is verified; the second-ranked too when question S8 takes the larger run. Any other candidate is verified only when every part ranked above it is refuted, one at a time. A part stays when neither verifier refutes it | confirmed or refuted, with the evidence |
| P5 | 1, and 1 critic | the resource budget of the whole board against the RP2354B: 48 GPIO; the rule that a PIO (programmable input/output) block sees GPIO 0 to 31 or 16 to 47 only; 12 PIO state machines in 3 blocks, where plain DShot takes one, bidirectional DShot two adjacent ones in one block (`firmware/iomcu/src/out_dshot.h`), and each receiver bus and the programmer one; the instruction memory of each block; DMA (direct memory access) channels, two per PPM output (`firmware/iomcu/src/out_ppm.c`); 12 PWM slices, where two pins on one slice and channel conflict (`shared/outputs/include/out_pwm_map.h`). Then the I²C address map, current per rail, and the allowlist over the whole list. Where the budget cannot hold every output as bidirectional DShot beside the receivers and the programmer, the specification states which combinations the board supports. The critic re-derives each conflict and looks for ones the first agent missed | conflicts, each naming the parts involved; the supported output combinations |
| P6 | 1 | every specification line has a part or is marked "not round 1"; every figure has a date and a source; every assumption P1 flagged is answered | gaps, fed back to P2 |
| P7 | 1, and 1 critic | writes the outputs below. The critic checks every figure on the pages against the evidence P2 to P4 returned, and every sentence against the writing rules in [CONTRIBUTING.md](../../CONTRIBUTING.md#writing) | the pages, and a list of corrections applied |

Size, for 13 categories and about 20 parts across them (R5, R8 and R11 need
several each): P0 1, P1 13, P2 13, P3 13, P4 40 with one verified part per
function or 80 with the alternate verified too, P5 2, P6 1, P7 2. About 85 or
125 agents (question S8), before any extra pass P6 sends back.

## Outputs

| File | Content |
| --- | --- |
| `hardware/docs/IOBoard.md` | the specification, with the chosen part on each line |
| `hardware/docs/Parts.md` | one row per part: function, part number, manufacturer, LCSC number, package, JLCPCB stock, presale and library type with the date, the quantity held in the owner's personal library with the date it was stated, second source, lifecycle status, longevity, alternate |
| `hardware/docs/Power.md` and one page per category group in its form | the choice, the alternatives, the stock, the reason |
| `hardware/STATUS.md` | the decided and open tables |
| `tools/jlc_stock.py` | re-queries JLCPCB's API for every LCSC number in `Parts.md`; `--check` exits 1 when a part is missing, under the stock threshold, or oversold. A row marked as held in the owner's personal library is not held to the public count: it carries the quantity the owner stated and the date, the tool checks that quantity against the build quantity, and prints those rows apart from the rest. Run by hand before an order, not in CI (continuous integration): a stock count moving is not a defect in the tree |

## Prerequisites

1. **Where it runs.** On the owner's server: 48 CPUs, 128 GB RAM. A workflow
   runs min(16, CPUs − 2) agents at once, so 16 there. This session's
   container has 4 CPUs and runs 2.
2. **Network access from that server.** P0 checks each host and stops the run
   when one it cannot do without is unreachable. At minimum: `jlcpcb.com`,
   `www.lcsc.com`, `yaqwsx.github.io`, `datasheets.raspberrypi.com`,
   `www.raspberrypi.com`, the second vendor's site (question S3), and each
   allowlisted manufacturer's site. For the record: on 2026-09-24 this
   session's cloud container was refused (HTTP 403 at its egress proxy) for
   `jlcpcb.com`, `www.lcsc.com`, `yaqwsx.github.io`, `www.ti.com` and
   `www.raspberrypi.com`. Web search was reachable, and
   [Sourcing](Sourcing.md) does not accept a search summary as a stock
   source.
3. **The repository** checked out on the server at the branch that carries
   this page, so every agent reads the same specification.
4. **The owner's answers** to the questions below.
5. **The RP2354B quantity** in the owner's personal library.

## Questions for the owner

Answered on 2026-09-24: power, motor current, servo current and the sensor
inputs, recorded under [Scope](#scope). The rest are open.

### Sourcing

Every sourcing question blocks P2.

| ID | Question | Proposed answer |
| --- | --- | --- |
| S1 | Which manufacturers are allowed? | ICs: Analog Devices (with Maxim and Linear), Infineon (with Cypress), Microchip, Nexperia, NXP, onsemi, Raspberry Pi, Renesas, ROHM, STMicroelectronics, Texas Instruments, Toshiba, Diodes Incorporated, Vishay. Undecided: Monolithic Power Systems, Richtek, Silergy, SG Micro, 3PEAK, Nisshinbo, Torex, Allegro, Melexis, Bosch Sensortec, ams OSRAM |
| S2 | Does S1 apply to the passives in round 2 as well? | owner to state before round 2; round 1 selects no passives beyond the inductors, crystal and shunts in [Scope](#scope) |
| S3 | Is a second vendor still required, as [the hardware README](../README.md) states, or is JLCPCB alone the source? | keep the rule: a pin-compatible alternate at JLCPCB, or the same part at Digi-Key |
| S4 | What is the stock threshold? | 10 times the parts needed for the first build, and never under 100 |
| S5 | Which lifecycle states pass? | active only; preview fails; a longevity commitment is recorded and not required |
| S6 | Are extended-library parts acceptable? Each unique one adds a loading fee per order. | yes; basic preferred where two parts are otherwise equal |
| S7 | How many boards in the first build, and are the parts bought into the personal library ahead of the order? A part in the personal library is not substituted between quote and build. | owner to state |
| S8 | How many agents may the run use? | about 85: one verified part per function, alternates recorded unverified |

### Specification

#### Blocking: answered before P2 starts

| ID | Question | Proposed answer |
| --- | --- | --- |
| F1 | One INA238 switched between the onboard shunt and the external-shunt input, or one INA238 for each? | one for each: no switch sits in a sense path, and the firmware reads whichever is wired. The DEVICE_ID check in [Sourcing](Sourcing.md) already tells the two part types apart; the two positions differ by I²C address |
| F2 | Is 150 A on the onboard shunt continuous or a peak, and for how long? Which connector takes it? | owner to state. At 150 A a 100 µΩ shunt dissipates 2.25 W and a 200 µΩ shunt 4.5 W |
| F3 | How many load-cell channels (thrust, torque on one or two cells), and what excitation voltage? | owner to state |
| F4 | The encoder: single-ended at 3.3 V or 5 V, or differential RS-422? Its supply voltage, the highest count rate, and what it measures (servo output shaft, motor shaft)? | owner to state |
| F5 | Motor temperature: thermocouple, NTC (negative temperature coefficient thermistor) or infrared, and how many channels? | owner to state |
| F6 | The display's supply on the link cable: which voltage, and which connector for the cable? The display's current draw is not measured. | 5 V; the current is measured on the bring-up bench before R5 sizes the rail |
| F7 | The monostable specification requires a high-side switch on the ESC pack. On the onboard 150 A path it is a MOSFET array and a driver IC on the IO board. What switches the external 300 A path: a contactor or MOSFET module driven from the IO board, or the signal gate alone? | a driven external module; the IO board carries its driver output |
| F8 | Which source charges the 2S pack: the 12 to 24 V DC input, USB-C, or both? The BQ25887 takes 3.9 to 6.2 V only ([Power](Power.md)), so a DC-input charger is a different part. | the DC input; USB-C not required |
| Q7 | How many external I²C ports, at which voltage? | owner to state |

#### Decided on the research's output

These stay open while the research runs. The research reports the
alternatives with their figures; the owner decides before P7 writes the pages.

| ID | Decision | Reported by |
| --- | --- | --- |
| Q4 | The RP2354B's ADC with a reference, or an external ADC for the accelerometer | R10: ENOB (effective number of bits) and sampling rate of each against the balance measurement |
| Q8 | The output binding in the RP2354B's flash, or in an I²C FRAM (ferroelectric RAM). A flash erase and program measured 19 ms on the bring-up module and loses CAN frames ([STATUS.md](../../STATUS.md#open-items)) | R2: how many received frames each CAN controller holds against a 19 ms stall at 1 Mbit/s. R12: FRAM and EEPROM candidates |
