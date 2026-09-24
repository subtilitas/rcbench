# IO board component research, round 1

The plan for the multi-agent research that selects the IO board's integrated
circuits (ICs). **It has not run.** It runs as six tasks, each a workflow of
at most 32 agents (owner, 2026-09-24). The first starts when the owner accepts
this page and has answered questions S1, S3 and S8. A research task starts
when every [blocking question](#blocking-answered-before-the-research-tasks)
its categories depend on is answered. The
[decisions that wait on the research](#decided-on-the-researchs-output) are
taken before the last task.

The IO board is the coprocessor board: the RP2354B, the CAN (Controller Area
Network) link to the display, the outputs, the receiver inputs, the sensor
front ends and the power path. [The specification](IOBoard.md) lists what it
has to do; this page lists how its parts are found.

## Scope

| Round | Selects |
| --- | --- |
| 1 | ICs: microcontroller support, CAN, safety gate, output and input buffers, power conversion and protection, current and voltage monitors, cell monitor, ADC (analogue-to-digital converter) and reference, sensor front ends, non-volatile store. Also the parts that fix an IC's surroundings: the RP2354B's crystal and regulator inductor, the servo supply's inductor and sense resistor, the onboard and external motor shunts, and the switch of the ESC (electronic speed controller) pack |
| 2 | passives, ESD (electrostatic discharge) and overvoltage protection, connectors, crystals and inductors not fixed in round 1 |
| 3 | mechanical, thermal, layout constraints; the busbar, mounting and cabling of the external 300 A path, whose shunt is in round 1 |

Fixed inputs. Round 1 checks them for stock, lifecycle and a second source.
A fixed input that fails a check is reported to the owner, not re-selected:

| Input | Value | Source |
| --- | --- | --- |
| Microcontroller | RP2354B: the RP2350B die and a 2 MB Winbond QSPI (quad serial peripheral interface) NOR flash stacked in one QFN-80 (quad flat no-lead, 80 pads) 10 × 10 mm package; 48 GPIO (general-purpose input/output), 8 ADC inputs, 520 kB SRAM (static random-access memory) on the die, no PSRAM (pseudo-static random-access memory) in the package. The flash is a W25Q16JVWI according to section 14.3 of the RP2350 datasheet, read from a copy; R1 reads it from Raspberry Pi's own | owner; held in the owner's JLCPCB personal parts library |
| Servo supply | TPS55288 | [Power](Power.md) |
| Motor monitor | INA238 | [Power](Power.md) |

The BQ25887 in [Power](Power.md) takes 3.9 to 6.2 V only and does not charge
from the 12 to 24 V DC (direct current) input. It is a seed of R7, not a fixed
input.

Owner decisions of 2026-09-24:

| Decision | Value |
| --- | --- |
| Power | a 12 to 24 V DC input runs the IO board and the display when present; the bench's own 2S (two cells in series) pack runs both otherwise; the selection is automatic. The display is powered through the link cable. The pack is disconnected in hardware below a discharge floor (question F9) |
| Motor current | the INA238 is on the IO board. An onboard shunt carries up to 150 A, with a temperature sensor beside it. An external shunt, for 300 A and above, connects to the IO board by its sense leads |
| Servo current | one current monitor per socket, 8 sockets, beside the per-socket supply switch |
| Sensor inputs | load cells for thrust and torque, a phase-wire rpm (revolutions per minute) clip, motor temperature, a magnetic rpm pickup, and the encoder (quadrature A and B plus an index pulse, ABI), which is the encoder in the pin budget of `firmware/iomcu/CMakeLists.txt` |
| Where the research runs | the owner's server, 48 CPUs (central processing units) |
| Checking | every finding is countered by a critic |
| Size of a task | at most 32 agents per task |

The stock figures in [Power](Power.md) are dated 2026-09-01 and state that they
are not valid after it. Round 1 takes them again.

### Consequence of the RP2354B's 2 MB flash

Three places in the coprocessor build assume more than 2 MB of flash. All
three change before the first IO board build:

| Place | Assumes | With the RP2354B |
| --- | --- | --- |
| `PICO_BOARD` in `firmware/iomcu/CMakeLists.txt`, default `pimoroni_pico_plus2_rp2350` | 16 MB of flash and 8 MB of PSRAM | a board file for the IO board with `PICO_FLASH_SIZE_BYTES` at 2 MB and no PSRAM. The tree has none ([STATUS.md](../../STATUS.md#open-items), Coprocessor board file) |
| `firmware/iomcu/src/out_store.c` | the output binding in the last two sectors of the first 4 MB; asserts `STORE_SIZE_BYTES <= PICO_FLASH_SIZE_BYTES` | fails to compile once the board file states 2 MB, which is the safe direction. With the default board file it compiles and places the two sectors at 4 MB less 8 kB, past the end of the 2 MB part |
| `-DLIMIT=4186112` in `firmware/iomcu/CMakeLists.txt`, the post-link check `image_fits.cmake` | 4 MB less 8 kB | passes an image that reaches into the store's two sectors, and a later save erases firmware. The limit becomes 2,088,960 bytes, 2 MB less 8 kB |

The coprocessor image carries the board picture as a constant array of
206,000 bytes: 500 × 206 pixels in RGB565, 16-bit red-green-blue
(`firmware/iomcu/src/art_rp2350_can.c`). The image-size check counts it with
the code. Its source PNG (Portable Network Graphics) file is 143,762 bytes.

The in-package flash sets how long the core stops for a save. The 19 ms
erase-and-program window in [STATUS.md](../../STATUS.md#open-items) was
measured on the bring-up module's flash, a different part. The W25Q16JV
datasheet (revision D, 2016-08-12, read from a copy) gives a 4 kB sector erase
of 45 ms typical and 400 ms maximum, and a 256-byte page program of 0.4 ms
typical and 3 ms maximum. R1 reads the figures from Winbond's own datasheet.
Question Q8 decides between those stalls and a store off the flash.

## Sourcing rules

Every agent applies these. They extend [Sourcing](Sourcing.md) and do not
replace it.

1. **JLCPCB's parts library for the board.** A part placed on the IO board has
   an LCSC number (`C` followed by digits), and JLCPCB assembly places it. A
   part off the board, such as the external shunt or an external switch
   module (question F7), is bought at the second vendor of question S3 and
   passes rules 2, 4, 5 and 7 there, with the stock read at that vendor's own
   page.
2. **Allowlisted manufacturers only.** The list is question S1 for ICs and S2
   for the rest. A part from a manufacturer not on the list is not a
   candidate, whatever its stock. A seed from such a manufacturer is dropped.
3. **Find in jlcparts, count at JLCPCB.** The `yaqwsx/jlcparts` database is a
   periodic snapshot: used to find candidates, never to count them. Stock comes
   from JLCPCB's own API (application programming interface), as in
   [Sourcing](Sourcing.md): `stockCount`, `canPresaleNumber`,
   `componentLibraryType` and the price breaks. The reading is the result
   whose LCSC number equals the part's, not the first result of the keyword
   search.
4. **Stock gate.** `stockCount` at or above question S4's rule applied to the
   part's need, and `canPresaleNumber` zero or above. A negative presale count
   fails whatever the stock says. The need is boards (question S7) × placements
   per board. Where the specification fixes the placements (8 for each
   per-socket part), that number is used. Where it does not, for example the
   MOSFETs (metal-oxide-semiconductor field-effect transistors) of a pack
   switch, P2 states a range and its basis, and the gate applies at the top of
   the range.
5. **A second source.** Either a pin-compatible alternate that also passes
   rules 1 to 4, or the same part at the second vendor of question S3 with
   stock at or above the gate of rule 4 on that vendor's own page, read as
   [Sourcing](Sourcing.md) describes. A part bought at the second vendor
   reaches JLCPCB assembly through the personal library (rule 6). The INA238
   and INA228 share one footprint ([Sourcing](Sourcing.md)). On the figures of
   2026-09-01 neither route passes for the INA238: the INA228 had 29 at JLCPCB
   with a presale count of −477, and the INA238 had 0 at Digi-Key with 2500 due
   2026-10-27. Round 1 reports that to the owner.
6. **Personal library.** Agents cannot read the owner's JLCPCB personal library;
   it needs a login. A part held there passes rule 4 when the quantity the
   owner states, with its date, is at or above boards × placements per board.
   Question S4's threshold does not apply to it, because a held part is not
   substituted between quote and build.
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

One research agent per row. The requirement values each agent checks against
are in [the specification](IOBoard.md). The seeds are starting points for the
search, not choices. None has been checked for stock, lifecycle or fit. The
concerns column holds what the plan's critics found against a seed. It rests
on search-engine summaries, which count as weak evidence, and on the tree.

| ID | Category | Seeds | Concerns found |
| --- | --- | --- | --- |
| R1 | RP2354B and its support: the part itself, 12 MHz crystal, core regulator inductor, 3.3 V supply, USB (Universal Serial Bus) protection. R1 records the silicon stepping of the RP2354B held in the personal library, and lists each erratum of that stepping in the RP2350 datasheet that constrains a pin, a pull resistor, an input front end or a peripheral; the tree cites RP2350-E5 (`firmware/iomcu/src/out_ppm.c`). It confirms the pin ratings the specification states | RP2354B; the inductor and crystal the RP2350 hardware design guide names | none |
| R2 | CAN controller and transceiver. Every output that reaches an RP2354B pin is at 3.3 V logic: a 3.3 V part, or a transceiver whose logic-supply (VIO) pin is at 3.3 V. R2 reports, for each controller, how many received frames it holds, against the stall of the RP2354B's own flash above and the number of frames the panel sends in that time, which the tree does not state and P1 asks | MCP2515, MCP2518FD, MCP251863 (controller and transceiver in one package); TCAN1042V, TCAN334, SN65HVD230, TJA1051T/3, TJA1462 | the bring-up module's XL2515 holds 2 received frames ([STATUS.md](../../STATUS.md#open-items)) |
| R3 | Safety gate, to the monostable specification: `hardware/docs/Monostable.md` at commit 23c82ca of pull request #167, not merged. A dual retriggerable monostable whose clear release does not trigger (the '423 behaviour), run from the 3.3 V rail; the OR gate, or the AND design of question F10; the clear network of F12; the latch of F11; the high-side switch on the servo rail; the switch on the ESC pack of F7, open within 5 ms of the enable falling and rated for the pack voltage of F15 with margin. Where F7 names a MOSFET array, the MOSFETs are qualified for that voltage, the current of F2, safe operating area during turn-off, the 5 ms budget, and current sharing and heat across the array. Every logic input driven while its own supply is absent has I_off (a datasheet limit on the input current with the input above the device's supply and the supply at 0 V), or a series resistor of 4.7 kΩ or more where the specification's isolation table allows one; the monostable's trigger inputs take both. Whether any part has both the '423 behaviour and I_off is not known; R3 reports that first. P1 checks the specification's statement that the RP2350 does not tolerate an input above an absent IOVDD (its I/O supply) against the pin ratings R1 confirms | 74HC423, SN74LV123A; 74LVC1G32; TPS3839; TPS48110, LTC7001, 2ED4820 as MOSFET drivers; 80 V and 100 V N-channel MOSFETs from Infineon, onsemi, Nexperia, Vishay and Texas Instruments | 74HC423: its inputs clamp to its supply, so I_off is not shown. SN74LV123A: has I_off, and its clear release triggers ('123 behaviour), which the specification excludes. 74HCT423: specified at 4.5 to 5.5 V only, not a candidate on 3.3 V |
| R4 | Output and input buffering. The output stage is the gated buffer of the monostable specification: an enable driven from the enable node, high impedance when disabled, I_off on every input on the RP2354B side and on the enable, and the bidirectional DShot reply passed while enabled with the bias of F14. It carries 3.3 V to servo and ESC signal levels. Also: the programming connector, which carries the one-wire bootloader at 19,200 baud (BLHeli_S, AM32), the ESCape32 text CLI (command-line interface), VESC's framed packets at 115,200 baud and the Hitec D-series servo protocol (`shared/ui/programmer_screen.c`); the OpenYGE telemetry line, half duplex at 115,200 baud 8N1 ([OpenYGE](../../docs/OpenYGE.md)); the receiver inputs | LSF0108, 74LVC1T45, SN74LXC1T45, 74LVC8T245, 74LVC245A; series resistance and clamps | the ESCape32 CLI's baud rate and the Hitec protocol's levels and rate are not stated in the tree; P1 asks |
| R5 | Board power input and protection: reverse polarity, overvoltage, inrush, automatic selection between the 12 to 24 V DC input and the 2S pack, the hardware disconnect of the pack below the floor of F9 with hysteresis so the load returning does not reconnect it, logic buck, 5 V rail, the display's supply on the link cable (F6). The pack path carries the servo supply's input current: 8.4 V at 8 A is 67 W, and at the 90 % efficiency in [Power](Power.md) that is 8.9 A from a pack at 8.4 V and 11.3 A at 6.6 V, before the logic rails and the display | LM74700, TPS3700, TPS2663, TPS25947; LMR36015, TPS62933, TPS563300 | TPS25947: operates to 23 V, checked against the 24 V input with its tolerance. LM66200 (1.6 to 5.5 V) and BQ29700 (one cell) do not fit and are not seeds |
| R6 | Servo supply: the TPS55288 re-checked from both inputs, 12 to 24 V and the pack from the floor of F9 to its full charge, including its inductor current at the low end; its inductor and sense resistor; per-socket supply switches for up to 8.4 V; the per-socket voltage ceiling of F16 | TPS55288; TPS2595 | TPS22990 and TPS22918 are rated to 5.5 V and do not switch the 8.4 V setting; not seeds |
| R7 | Pack charger for the 2S pack, with balancing, charging from the source and at the current of F8 | BQ25887, BQ25798 with BQ76907 or BQ29209, MP2672A | MP2672A is from Monolithic Power Systems, undecided in S1 |
| R8 | Current and voltage monitors: the motor monitor or monitors (F1) with the onboard 150 A shunt and the external-shunt input, the temperature sensor at the onboard shunt, one monitor per servo socket | INA238, INA228, INA236, INA3221, INA745A; TMP117, TMP1075 | none |
| R9 | Cell monitor on the balance lead, 1 to 14 cells (the range `SET_PACK_CELLS` and the battery screen take) | BQ76952, BQ76942, ADBMS6948, LTC6813 | BQ76952 reads 3 to 16 cells and BQ76942 3 to 10, so neither reads 1 or 2 cells. ADBMS6948 (16 channels) and LTC6813 (18 cells): lowest cell count not found |
| R10 | ADC and reference (question Q4): the RP2354B's own ADC with an external reference, or an external converter | REF3033, LM4040; ADS131M04, ADS1115, ADS112C04 | none |
| R11 | Rotation and vibration front ends: accelerometer, optical index pulse, magnetic pickup, phase-wire rpm clip rated for the pack voltage of F15, the encoder input (F4). A front end that drives one of the 8 ADC inputs keeps it within its rating while the RP2354B's 3.3 V rail is down and the front end's supply is up | ADXL1002, ADXL1005; TLV3201, TLV7011; AM26LV32, SN65LBC175 | IIS3DWB is digital, on SPI (Serial Peripheral Interface); the specification asks for an analogue accelerometer, so it is a seed only if Q4 accepts a digital one. SN65LBC175 runs from 5 V; its outputs are translated to 3.3 V |
| R12 | Motor temperature (F5), external I²C (Inter-Integrated Circuit) ports (Q7) and the non-volatile store (Q8) | MCP9600, MAX31856, MLX90614; TCA9548A, PCA9615, TCA9617A; FM24CL16B, 24LC256 | MLX90614 is from Melexis, undecided in S1. MB85RC256V (Fujitsu) is a seed only if S1 adds Fujitsu |
| R13 | Load cells for thrust and torque: bridge ADC and excitation (F3) | ADS1232, ADS1234, ADS124S08, AD7124-4 | none |

## Agent layout

Every finding is checked by a critic that did not produce it and is told to
refute it. Nothing reaches a page on one agent's word. P0 returns
reachability, not a finding, and has no critic. P1's findings go to the
owner, who answers them. Each agent returns a fixed schema, so a critic
compares like with like.

A task is one workflow run of at most 32 agents. The workflow script counts
the agents a task plans before it starts, refuses a task over 32, and moves
work that does not fit into a follow-up task, which it logs.

```text
T1  P0 reachability (1), P1 requirement critic (1 per category)      14
                                 -- the owner answers what P1 returns
T2  group A: R1 R2 R3 R4        P0 (1), then per category:           21
T3  group B: R5 R6 R7 R8          P2 discover and qualify (1)        21
T4  group C: R9 R10 R11 R12 R13   P3 search critic (1)               26
                                  P2 re-rank with P3's finds (1)
                                  P4 stock and lifecycle verifier (1)
                                  P4 datasheet and pin verifier (1)
T5  P5 cross-category checks (1) and its critic (1),                  4
    P6 completeness (1) and its critic (1)
                                 -- gaps become follow-up research tasks
                                 -- the owner decides Q4 and Q8
T6  P7 write the pages (1) and a page critic (1)                      2
```

T2, T3 and T4 are independent of each other and run in any order. Within a
task, the categories run side by side, and each category runs P2, P3, the
re-rank and P4 in that order.

| Phase | Agents | Does | Returns |
| --- | --- | --- | --- |
| P0 | 1 | fetches one known page from every host in [Prerequisites](#prerequisites), and downloads one copy of the jlcparts database with its snapshot date. Runs at the start of T1 and of each research task | reachable or not, per host. The task stops if JLCPCB's API, the jlcparts database or the second vendor of S3 is unreachable. A category is held while a manufacturer whose parts it seeds has an unreachable site, because the lifecycle gate cannot be read |
| P1 | 1 per category | reads the category's lines in [the specification](IOBoard.md) and every source they cite, and tries to refute each value: does it follow from its source, is the unit right, is it an owner decision or an assumption. Lists, for each function, the requirement values P2 needs to qualify a part (voltage, current, range, resolution, rate, accuracy) that the specification does not state | each value marked sourced, owner decision or assumption, and each missing value. Every assumption and every missing value is a question to the owner; P2 does not start on a function with one unanswered |
| P2 | 1 per category | lists candidates from allowlisted manufacturers in the jlcparts copy; drops those that miss a requirement value; for up to five survivors per function records part number, manufacturer, LCSC number, package, every requirement value against the datasheet's, placements per board, JLCPCB stock, presale, library type and price, second-vendor stock, incoming quantity and lead time, the lifecycle fields above, pin-compatible alternates, and whether a driver exists under `shared/` | a ranked shortlist per function with the reason for each rank, and every candidate dropped with the reason |
| P3 | 1 per category | searches for part families P2 did not consider, from the same allowlist, and re-reads each reason P2 gave for dropping a candidate | missed candidates and exclusions that do not hold |
| Re-rank | 1 per category | qualifies what P3 returned as P2 does, and re-ranks each function's shortlist | the final ranking per function |
| P4 | 2 per category | two independent verifiers, each told to refute, each covering every verified part of the category. One re-reads stock and lifecycle at the primary sources. One re-reads every requirement value in the datasheet, and for an alternate, the pin-for-pin match to the part it stands in for. Verified: the first-ranked part of each function, and the alternate that satisfies sourcing rule 5 for it when the second source is an alternate rather than a second vendor. A part stays when neither verifier refutes it. A refuted part sends the next-ranked candidate to a new pair of verifiers, inside the task while it stays at or under 32 agents, otherwise in a follow-up task | confirmed or refuted, with the evidence, per part |
| P5 | 1, and 1 critic | the resource budget of the whole board against the RP2354B, from the RP2350 datasheet and R1's errata: 48 GPIO; the rule that a PIO (programmable input/output) block sees GPIO 0 to 31 or 16 to 47 only; 12 PIO state machines in 3 blocks and each block's instruction memory; DMA (direct memory access) channels, two per PPM (pulse-position modulation) output (`firmware/iomcu/src/out_ppm.c`); 12 PWM (pulse-width modulation) slices, where two pins on one slice and channel conflict (`shared/outputs/include/out_pwm_map.h`); the 8 ADC inputs; the SPI, I²C and UART (universal asynchronous receiver-transmitter) controllers with their pin options. Plain DShot takes one state machine and bidirectional DShot two adjacent ones in one block (`firmware/iomcu/src/out_dshot.h`). The receiver buses and the programmer take a number of state machines the tree does not state: no receiver PIO program is written ([Receivers](../../docs/Receivers.md)) and no programmer protocol has run on a wire; P5 states the count it assumes for each, and whether a bus uses a hardware UART instead. 8 bidirectional DShot outputs alone take 16 state machines against 12, so P5 returns the output combinations the board supports. P5 budgets both alternatives of Q4 and Q8, checks the I²C address map and the current per rail, checks that R4's buffer enable and R3's enable node agree in polarity, and applies the allowlist over the whole list. The critic re-derives each conflict and looks for ones the first agent missed | conflicts, each naming the parts involved; the supported output combinations |
| P6 | 1, and 1 critic | every specification line has a part or is marked "not round 1"; every figure has a date and a source; every assumption P1 flagged is answered. The critic re-reads the specification line by line against the evidence and looks for gaps P6 did not report | gaps. Each gap becomes a follow-up research task of at most 32 agents; at most 2 rounds of them |
| P7 | 1, and 1 critic | writes the outputs below from the evidence the research tasks returned, the budget and combinations P5 returned, and the owner's decisions on Q4 and Q8. The critic checks every figure and every stated combination on the pages against that evidence and every sentence against the writing rules in [CONTRIBUTING.md](../../CONTRIBUTING.md#writing), runs `python3 tools/check_docs.py` and `ruff check tools/`, and runs `tools/jlc_stock.py --check` once against the new `Parts.md` | the pages, a list of corrections applied, and the three check results |

Size. T1 14, T2 21, T3 21, T4 26, T5 4, T6 2: 88 agents in six tasks, the
largest 26. The 6 to 11 agents each research task keeps free take P3's finds
and the verifiers a refutation calls for. Follow-up tasks add to the total and
stay at 32 each.

## Outputs

| File | Content |
| --- | --- |
| `hardware/docs/IOBoard.md` | the specification, with the chosen part on each line |
| `hardware/docs/Parts.md` | one row per part: function, part number, manufacturer, LCSC number, package, JLCPCB stock, presale and library type with the date, placements per board, the quantity held in the owner's personal library with the date it was stated, second source, lifecycle status, longevity, alternate |
| `hardware/docs/Power.md` and one page per category group in its form | the choice, the alternatives, the stock, the reason |
| `hardware/STATUS.md` | the decided and open tables |
| `hardware/README.md` | one row per new page in the page table |
| `hardware/docs/Research.md` | the status line: the date round 1 ran and the commit of its output |
| `tools/jlc_stock.py` | re-queries JLCPCB's API for every LCSC number in `Parts.md` and for each row's alternate, taking the build quantity as an argument. It reads the result whose LCSC number equals the row's. A part's gate is rule 4 of the sourcing rules. `--check` exits 1 when a part is missing, oversold (`canPresaleNumber` below zero) or under its gate while its alternate is too, and prints a part under its gate with a passing alternate as a warning. A personal-library row is checked by rule 6 and printed apart, with the date its quantity was stated. The second vendor is not queried. Run by hand before an order, not in CI (continuous integration): a stock count moving is not a defect in the tree |

## Prerequisites

1. **Where it runs.** On the owner's server, 48 CPUs (owner, 2026-09-24). Its
   memory is not stated. The workflow tool sets how many agents run at once;
   the run records that number.
2. **Network access from that server.** P0 checks every host. At minimum:
   `jlcpcb.com`, `www.lcsc.com`, `yaqwsx.github.io`,
   `datasheets.raspberrypi.com`, `www.raspberrypi.com`, `github.com` and
   `raw.githubusercontent.com` (Raspberry Pi's `pico-sdk` and `documentation`
   repositories), the second vendor's site (question S3), and each
   allowlisted manufacturer's site. For the record: on 2026-09-24 a cloud
   container was refused (HTTP (Hypertext Transfer Protocol) 403 at its egress
   proxy) for `jlcpcb.com`, `www.lcsc.com`, `yaqwsx.github.io`, `www.ti.com`,
   `www.raspberrypi.com`, `datasheets.raspberrypi.com`, `pip.raspberrypi.com`,
   `www.nexperia.com`, `www.analog.com`, `www.microchip.com` and
   `www.digikey.com`, and reached `github.com` and
   `raw.githubusercontent.com`.
3. **A fetcher for Digi-Key**, if S3 names it. Its pages sit behind
   Cloudflare, and a plain request gets HTTP 403 ([Sourcing](Sourcing.md)). P2
   and P4 use a fetcher that executes the challenge, or Digi-Key's own API with
   the owner's credentials. P0 checks that fetcher, not a plain request.
4. **The repository** checked out on the server at branch
   `claude/rcbench-io-board-spec-0japaa`, at the commit that carries the
   owner's answers, so every agent reads the same specification. R3 reads
   `hardware/docs/Monostable.md` and the pages it links at commit 23c82ca, the
   head of pull request #167 (branch `monostable-specification`, open, not
   merged). P0 fetches that commit into a read-only directory beside the
   checkout. The description of pull request #167 is not a source: it gives a
   155 to 200 ms window, and the page at 23c82ca gives 155 to 185 ms with a
   200 ms deadline. The branch and pull request #167 both change `STATUS.md`
   and `hardware/STATUS.md`, so the later of the two to merge resolves a
   conflict by hand.
5. **Access on the server.** Model access for the workflow's agents, read
   access to `github.com/subtilitas/rcbench` including branch
   `monostable-specification`, and push access to a results branch. P7
   commits to that branch. Nothing is pushed to `main`, and a pull request is
   opened only when the owner asks for one.
6. **The workflow script and its schemas.** Not written. They are written from
   this page, reviewed by the owner, and committed before T1 runs.
7. **The display's current draw** on the link cable at the voltage of F6, peak
   and steady, measured on the bring-up bench before R5 runs. Not measured.
8. **The owner's answers** to the questions below.
9. **The RP2354B quantity** in the owner's personal library.

## Questions for the owner

Answered on 2026-09-24: power, motor current, servo current, the sensor
inputs, where the research runs, the checking rule and the size of a task,
recorded under
[Scope](#scope). A proposed answer is not an answer: the tasks read the
Answer column only.

### Sourcing

S1, S3 and S8 block T1. The other sourcing questions block the research
tasks.

| ID | Question | Proposed answer | Answer (owner, date) |
| --- | --- | --- | --- |
| S1 | Which manufacturers are allowed for ICs? | Analog Devices (with Maxim and Linear), Infineon (with Cypress), Microchip, Nexperia, NXP, onsemi, Raspberry Pi, Renesas, ROHM, STMicroelectronics, Texas Instruments, Toshiba, Diodes Incorporated, Vishay. Undecided: Monolithic Power Systems, Richtek, Silergy, SG Micro, 3PEAK, Nisshinbo, Torex, Allegro, Melexis, Bosch Sensortec, ams OSRAM, Fujitsu | |
| S2 | Which makers are allowed for the parts that are not ICs? Round 1 selects the RP2354B's crystal and regulator inductor, the servo supply's inductor and sense resistor, both shunts, and the ESC pack switch; round 2 selects every other passive. | round 1 and round 2 alike: the S1 list, plus Murata, TDK, Würth Elektronik, Coilcraft, Bourns, Isabellenhütte, KEMET, Panasonic, Yageo, Samsung Electro-Mechanics, Abracon, Epson and NDK | |
| S3 | Is a second vendor still required, as [the hardware README](../README.md) states, or is JLCPCB alone the source? Parts off the board are bought there too (rule 1). | keep the rule, with Digi-Key as the second vendor | |
| S4 | What is the stock gate? | 10 times the need of the first build, and never under 100 | |
| S5 | Which lifecycle states pass? | active only; preview fails; a longevity commitment is recorded and not required | |
| S6 | Are extended-library parts acceptable? Each unique one adds a loading fee per order. | yes; basic preferred where two parts are otherwise equal | |
| S7 | How many boards in the first build, and are the parts bought into the personal library ahead of the order? A part in the personal library is not substituted between quote and build. | owner to state | |
| S8 | May the run use six tasks of at most 32 agents, 88 agents in all, plus the follow-up tasks P6 and refutations call for? | yes | |

### Specification

#### Blocking: answered before the research tasks

Each question blocks the research task whose categories depend on it, not the whole run.

| ID | Question | Proposed answer | Answer (owner, date) |
| --- | --- | --- | --- |
| F1 | One INA238 switched between the onboard shunt and the external-shunt input, or one INA238 for each? | one for each: no switch sits in a sense path, and the firmware reads whichever is wired. The two positions differ by I²C address | |
| F2 | Is 150 A on the onboard shunt continuous or a peak, and for how long? The connector for it is a round 2 question. | owner to state. At 150 A a 100 µΩ shunt dissipates 2.25 W and a 200 µΩ shunt 4.5 W | |
| F3 | How many load-cell channels (thrust, torque on one or two cells), and what excitation voltage? | owner to state | |
| F4 | The encoder: single-ended at 3.3 V or 5 V, or differential RS-422? Its supply voltage, the highest count rate, and what it measures (servo output shaft, motor shaft)? | owner to state | |
| F5 | Motor temperature: thermocouple, NTC (negative temperature coefficient thermistor) or infrared, and how many channels? | owner to state | |
| F6 | The display's supply on the link cable: which voltage? Its current is measured first (prerequisite 7). | 5 V | |
| F7 | The switch on the ESC pack. The monostable specification requires one and leaves its technology open. On the onboard 150 A path: a MOSFET array with a driver IC, or a normally-open relay or contactor that opens within 5 ms of the enable falling? On the external 300 A path: a contactor or MOSFET module driven from the IO board, or the signal gate alone? | onboard: a MOSFET array and its driver on the IO board. External: a driven external module, whose driver output is on the IO board | |
| F8 | Which source charges the 2S pack (the 12 to 24 V DC input, USB-C, or both), and at what current? 2 A is the BQ25887's charge current for a USB input ([Power](Power.md)); the tree states no requirement. | the DC input at 2 A; USB-C not required | |
| F9 | The 2S pack's cell chemistry and its discharge floor. A pack-voltage floor does not bound one cell: a pack at 6.6 V can hold cells at 3.0 V and 3.6 V. A per-cell floor reads the pack's balance lead. | owner to state. 3.3 V a cell (6.6 V for the pack) is a candidate for 4.2 V lithium-ion cells and has no source in the tree. Reconnection only when the DC input or the charger is present | |
| F10 | The monostable's long timing-element failures: the single-channel OR design, whose fault model excludes an R or C risen in value, or the two-channel AND design, which adds two both-edge detectors and an AND gate? | owner to state | |
| F11 | A hardware latch set by the monostable's first expiry and cleared only by an operator action? It adds a clear input and a control to R3. | owner to state | |
| F12 | The monostable's clear network: the RC (resistor-capacitor) network, which covers a fast power-up ramp only, or a reset supervisor with a stated threshold and delay? | a reset supervisor | |
| F13 | Which IO board pins pass through the gated buffer? | every pin that reaches an ESC or a servo: the 8 output sockets and the programming connector | |
| F14 | The bias of a bidirectional DShot line through the gated buffer: selected per protocol (a pull-up for bidirectional DShot, a pull-down otherwise), or an open-drain stage with a pull-up that idles high for every protocol? | owner to state | |
| F15 | The highest ESC pack voltage the bench takes. `SET_PACK_CELLS` takes 1 to 14 cells; at 4.2 V a cell that is 58.8 V. The 65 V in [Power](Power.md) has no source in the tree. | owner to state | |
| F16 | A per-socket voltage ceiling set in hardware: is it required, at which value, and does it follow the rail setting (5.5 V or 8.4 V) or the socket? No source in the tree gives it. | owner to state | |
| Q7 | How many external I²C ports, at which voltage? | owner to state | |

#### Decided on the research's output

These stay open while the research runs. The research tasks report the
alternatives with their figures, and P5 budgets both. The owner decides before
T6.

| ID | Decision | Reported by |
| --- | --- | --- |
| Q4 | The RP2354B's ADC with a reference, or an external ADC, for the accelerometer | R10: ENOB (effective number of bits) and sampling rate of each against the balance measurement |
| Q8 | The output binding in the RP2354B's flash, or in an I²C FRAM (ferroelectric random-access memory) or EEPROM (electrically erasable programmable read-only memory) | R2: how many received frames each CAN controller holds against the stall of the W25Q16JV above and the frame count P1's question returns. R12: FRAM and EEPROM candidates |
