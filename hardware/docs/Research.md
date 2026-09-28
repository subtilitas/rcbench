# IO (input/output) board component research, round 1

The plan for the multi-agent research that selects the integrated circuits
(ICs) of the IO board. **It has not run.** The owner accepted it on
2026-09-25. It is planned as six
tasks (question S8), each a workflow of at most 32 agents (owner, 2026-09-24).
Prerequisites 1 to 3 are in place (2026-09-27). T1 starts on the owner's go
once prerequisites 4 to 6 are in place (owner, 2026-09-27;
[Prerequisites](#prerequisites)). A research task starts when every
[blocking question](#blocking-answered-before-the-research-tasks) and every
[question raised by P1](#raised-by-p1) that its categories depend on is
answered. A question whose value only feeds a decision under
[Decided on the research's output](#decided-on-the-researchs-output) holds
neither P2 nor the start of its task. The
[decisions that wait on the research](#decided-on-the-researchs-output) are
taken before the last task.

The IO board is the coprocessor board: the RP2354B, the CAN (Controller Area
Network) link to the display, the outputs, the receiver inputs, the sensor
front ends and the power path. [The specification](IOBoard.md) lists what it
has to do; this page lists how its parts are found.

## Scope

| Round | Selects |
| --- | --- |
| 1 | ICs: microcontroller support, CAN, safety gate, output and input buffers, power conversion and protection, current and voltage monitors, cell monitor, ADC (analogue-to-digital converter) and reference, sensor front ends, non-volatile store. Also the parts that fix an IC's surroundings: the RP2354B's crystal and regulator inductor, the servo supply's inductor, the onboard and external motor shunts, and the switch of the ESC (electronic speed controller) pack with its input protection (R3). R7 also selects the 18650 cells of the bench's own pack and the pack's protection |
| 2 | passives; ESD (electrostatic discharge) and overvoltage protection on the signal, sensor, balance-lead, link and heartbeat connectors (the power input's protection is R5 and the ESC pack path's is R3, both in round 1); connectors; crystals and inductors not fixed in round 1 |
| 3 | mechanical, thermal, layout constraints; the busbar, mounting and cabling of the external 300 A path, whose shunt is in round 1 |

Not in any round: the link pages and coprocessor code that carry a
measurement or a function from the IO board to the display. The specification
lists each as required: receiver data, port currents and the servo
procedures, cell readings, vibration, thrust and torque. They are firmware
work. P6 marks such a line "hardware selected, link open" and does not return
it as a gap. Two questions from the same lines do reach round 1, because they
choose hardware or fix a value: which sensor gives the servo's measured
position (R11), and which source fills the BENCH voltage and current registers
(R8) and the rpm register (R11) when more than one reports. P1 asks the owner
both.

Fixed inputs. Round 1 checks them for stock, lifecycle and a second source.
A fixed input that fails a check is reported to the owner, not re-selected:

| Input | Value | Source |
| --- | --- | --- |
| Microcontroller | RP2354B: the RP2350B die and a 2 MB Winbond QSPI (quad serial peripheral interface) NOR (not-or) flash stacked in one QFN-80 (quad flat no-lead, 80 pads) 10 × 10 mm package; 48 GPIO (general-purpose input/output), 8 ADC inputs, 520 kB SRAM (static random-access memory) on the die, no PSRAM (pseudo-static random-access memory) in the package. Stepping A4 (owner, 2026-09-27), from the marking RP2354B0A4 in JLCPCB's product photograph of C39843328. In JLCPCB's API (application programming interface), C39843328 carries the `erpComponentName` `SC1512(13)-A4` (2026-09-27). The marking on the held parts is not read. The flash is a W25Q16JVWI according to section 14.3 of the RP2350 datasheet (build 2024-08-08), read from a copy. R1 reads the part number from Raspberry Pi's own copy at `datasheets.raspberrypi.com`. Section 14.3 gives the flash 2.7 to 3.6 V. Table 1441 of build 2025-07-29 gives QSPI_IOVDD, the supply of the QSPI interface, on the RP2354 as 2.97 to 3.63 V, 3.3 V nominal, because the in-package flash is a 3.3 V part; R1 confirms both | owner; held in the owner's JLCPCB personal parts library |
| Servo supply | TPS55285: current limit set internally, up to 6.35 A; input 2.4 to 22 V; 12 held in the owner's personal library (owner, 2026-09-25) | [Power](Power.md) |
| Motor monitor | INA238 | [Power](Power.md) |

The BQ25887 in [Power](Power.md) takes 3.9 to 6.2 V only and does not charge
from the 12 to 20 V DC (direct current) input. It is a seed of R7, not a fixed
input. The INA3221 is the owner's choice for the 20 port monitors and a seed of
R8, which may replace it (owner, 2026-09-27); it is not a fixed input.

Owner decisions of 2026-09-24. A value the owner set on 2026-09-25 or later
carries its date:

| Decision | Value |
| --- | --- |
| Power | a DC input runs the IO board and the display when present, 12 to 20 V (owner, 2026-09-25); the bench's own 2S (two cells in series) pack runs both otherwise; the selection is automatic. The display is powered through the link cable. The pack is disconnected in hardware below a discharge floor (question F9) |
| Motor current | the INA238 is on the IO board. An onboard shunt carries up to 150 A, with a temperature sensor beside it. An external shunt, for currents above 150 A to 300 A and more (owner, 2026-09-25), connects to the IO board by its sense leads |
| Output ports | 20 typed ports (owner, 2026-09-25): 16 PWM ports (enable, pulse range, frame rate per pair, one switchable to PPM) and 4 multiprotocol ports (servo PWM, DShot, bidirectional DShot, ESC telemetry, servo configuration, ESC bootloader); a one-pin UART socket; an external CAN port on its own controller |
| Servo current | one current monitor channel per port, beside each port's supply switch (owner, 2026-09-25). The owner's choice is 7 INA3221 for the 20 ports (owner, 2026-09-25), a seed of R8, which may replace it (owner, 2026-09-27) |
| Sensor inputs | load cells for thrust and torque, a phase-wire rpm (revolutions per minute) clip, motor temperature, a magnetic rpm pickup, and the encoder (quadrature A and B plus an index pulse, ABI), which is the encoder in the pin budget of `firmware/iomcu/CMakeLists.txt` |
| Where the research runs | the owner's server, 48 CPUs (central processing units); 128 GB of memory (owner, 2026-09-25) |
| Checking | every finding is countered by a critic |
| Size of a task | at most 32 agents per task |

Owner decisions of 2026-09-27, each applied where the last column says:

| Decision | Value | Applied in |
| --- | --- | --- |
| Parts database | the last full copy of the jlcparts database, of 2026-09-14, with 7,161,863 rows. The published copy held 985,000 rows on 2026-09-27 | [Sourcing rule 3](#sourcing-rules), P0 |
| Returns and commits | the session that launches a task writes each agent's return and commits it; a workflow script has no file or git access | [Agent layout](#agent-layout), prerequisite 5 |
| Questions a P1 critic adds | one more agent re-checks them before they reach the owner. T1 has 28 agents and the run 102 | [Agent layout](#agent-layout), S8 |
| Lifecycle of Analog Devices, Melexis and Allegro parts | Digi-Key's product status, recorded as a distributor figure. On 2026-09-27 the Analog Devices product pages carried no lifecycle status in the page body, and the host refused every request for a time after about 15. The Melexis pages refused every client tried. Allegro's pages load the status by script | [Lifecycle check](#lifecycle-check) |
| RAMXEED | counts as Fujitsu | S1, R12 |
| INA3221 | a seed of R8, which may replace it | [Fixed inputs](#scope), the Servo current row above, R8 |
| Branches | `research/round1` and `research/round1-results` start from `main` after the pull requests that carry these decisions and the workflow scripts are merged | prerequisites 4 and 5 |
| Workflow scripts | committed to `main` under `tools/research/` through a pull request that the automated reviewers and the owner review, with their schemas and the tool that writes and commits each task's returns | prerequisite 6 |
| Start of T1 | on the owner's go, once prerequisites 4 to 6 are in place | status line |
| Agents | every agent runs on the session's model at the session's effort | prerequisite 1 |
| Task size | the sizes S8 approves: the largest task is planned at 28 agents, and each task stays at 32 or fewer with the agents it adds for refutations and restarts. The session's Dynamic workflow size setting is advisory and is not changed | prerequisite 1 |
| Values the specification does not state | T1 raises them under [Raised by P1](#raised-by-p1); none is asked before T1 | T1 |
| In-package flash figures | not known for the die. Q8 and P5 budget with the packaged parts' maximums from Winbond's datasheets, each marked as a packaged-part figure | [Consequence of the 2 MB flash](#consequence-of-the-rp2354bs-2-mb-flash), R1, P5 |
| Pack cell makers | S9 | R7 |
| RP2354B stepping | A4 | fixed inputs, R1 |
| Digi-Key figures | stock, the manufacturer's lead time and the product status, read from Digi-Key's API only. The API carries no dated incoming quantity, and none is recorded | [Sourcing rules 1, 5 and 7](#sourcing-rules), P2 |

The stock figures in [Power](Power.md) are dated 2026-09-01 and state that they
are not valid after it. Round 1 takes them again.

### Consequence of the RP2354B's 2 MB flash

Three places in the coprocessor build assume more than 2 MB of flash. All
three change before the first IO board build:

| Place | Assumes | With the RP2354B |
| --- | --- | --- |
| `PICO_BOARD` in `firmware/iomcu/CMakeLists.txt`, default `pimoroni_pico_plus2_rp2350` | 16 MB of flash and 8 MB of PSRAM | a board file for the IO board that states the B package (`PICO_RP2350A` at 0, 48 GPIO), `PICO_FLASH_SIZE_BYTES` at 2 MB and no PSRAM. The tree has none ([STATUS.md](../../STATUS.md#open-items), Coprocessor board file). pico-sdk's `pico2.h` sets `PICO_RP2350A` to 1, which gives `NUM_BANK0_GPIOS` 30: a header copied from it loses GPIO 30 to 47 |
| `firmware/iomcu/src/out_store.c` | the output binding in the last two sectors of the first 4 MB; asserts `STORE_SIZE_BYTES <= PICO_FLASH_SIZE_BYTES` | fails to compile once the board file states 2 MB, which is the safe direction. With the default board file it compiles and places the two sectors at 4 MB less 8 kB, past the end of the 2 MB part |
| `-DLIMIT=4186112` in `firmware/iomcu/CMakeLists.txt`, the post-link check `image_fits.cmake` | 4 MB less 8 kB | passes an image that reaches into the store's two sectors, and a later save erases firmware. The limit becomes 2,088,960 bytes, 2 MB less 8 kB |

The coprocessor image carries the board picture as a constant array of
206,000 bytes: 500 × 206 pixels in RGB565, 16-bit red-green-blue
(`firmware/iomcu/src/art_rp2350_can.c`). The image-size check counts it with
the code. Its source PNG (Portable Network Graphics) file is 143,762 bytes.

The in-package flash sets how long the core stops for a save. The 19 ms
erase-and-program window in [STATUS.md](../../STATUS.md#open-items) is a
measurement on the bring-up module's 4 MB flash
(`firmware/iomcu/CMakeLists.txt:5-7`), a different part from the 2 MB
W25Q16JV. The W25Q16JV datasheet (revision D, 2016-08-12, read from a copy)
gives a 4 kB sector erase of 45 ms typical and 400 ms maximum, a 256-byte page
program of 0.4 ms typical and 3 ms maximum, and at least 100,000
program-erase cycles a sector. Those figures are for the packaged parts in its
ordering list, which refers KGD (known good die) parts to Winbond. The W25Q16JV
datasheet `www.winbond.com` served without a login on 2026-09-27 is the
W25Q16JV-DTR datasheet (revision I, published 2026-05-28), whose ordering list names only
DTR (double transfer rate) parts. It gives the same erase and program times.
It gives at least 100,000 program-erase cycles without "per sector": its
revision history removes those words. Whether the RP2354B's die is a DTR part
is not known. No public Winbond document gives figures for the W25Q16JVWI die.
R1 records them as not known. Q8 and P5 budget with the packaged parts'
maximums, each marked as a packaged-part figure (owner, 2026-09-27). Question
Q8 decides between those stalls and a store off the flash.

## Sourcing rules

Every agent applies these. They extend [Sourcing](Sourcing.md) and do not
replace it.

1. **JLCPCB's parts library for the board.** A part placed on the IO board has
   an LCSC number (`C` followed by digits), and JLCPCB assembly places it. A
   part off the board, such as the external shunt or an external switch
   module (question F7), is bought at the second vendor of question S3 and
   passes rules 2, 4, 5 and 7 there, with the stock read from that vendor's
   API (prerequisite 3).
2. **Allowlisted manufacturers only.** The list is question S1 for ICs, S9
   for the 18650 cells of the bench's own pack, and S2 for the rest. A part
   from a manufacturer not on the list is not a
   candidate, whatever its stock. A seed from such a manufacturer is dropped.
   A maker's other names count as its listed name: Maxim and Linear as Analog
   Devices, Cypress as Infineon (S1), and RAMXEED as Fujitsu (owner,
   2026-09-27). JLCPCB labels some rows "JLCPCB Assembly"; that is not a maker,
   and such a row is not a candidate. Where JLCPCB's label contradicts the maker
   that the part number and the datasheet name, the datasheet counts: C2061051, an
   MB85RC256V, is labelled "Fuji Electric" (2026-09-27).
   Connectors are not in S2: round 1 selects none, and their makers, JST for
   the Qwiic ports of Q7 among them, are asked before round 2.
3. **Find in jlcparts, count at JLCPCB.** The `yaqwsx/jlcparts` database is a
   rolling cache: each scheduled run refreshes a limited number of parts, and
   each part carries its own `fetched_at`. It is used to find candidates,
   never to count them. On 2026-09-14 the copy published at
   `yaqwsx.github.io/jlcparts/data/` lost every part below LCSC number
   C6374508 (upstream issue #159). On 2026-09-27 it held 985,000 rows and none
   of this page's fixed inputs or held parts. The research searches the last
   full copy (owner, 2026-09-27):
   the `web_build` artifact 10342101918 of upstream run 34823400239, manifest
   created 2026-09-14T09:56:01Z, 7,161,863 rows in `jlc_components` fetched
   from 2026-05-23 to 2026-09-14. It is kept on the server in
   `~/rcbench-research/jlcparts-2026-09-14/`. The artifact's zip,
   `web_build-10342101918.zip`, has the SHA-256 (Secure Hash Algorithm,
   256 bits) `4e578005c4bcce8a7cc825ce3c7e06ddb197acdce228e223af57cc2631eda5f9`.
   The `cache.sqlite3` unpacked from it, the file agents open, has
   `48b41b803d7b6bbe0fa7e25b5db430a8421f8175ad559e2c4b4bfc3a73d28c26`. A part
   listed after 2026-09-14 is not in it. Every agent that searches jlcparts
   opens that file read-only. 25.1 % of its rows have an empty maker field.
   20.0 % have empty attributes. INA238AIDGSR (C2868250) has an empty maker
   field. TPS55285VALR (C52160906) has an empty maker field and empty
   attributes. An agent therefore searches by part number and description as
   well as by category. It never drops a row because its maker or attributes
   are blank. It takes the maker from JLCPCB's API (`componentBrandEn`) and
   every requirement value from the datasheet.
   Stock comes from JLCPCB's own API, as in [Sourcing](Sourcing.md): `stockCount`,
   `canPresaleNumber`, `componentLibraryType` and the price breaks. The
   reading is the result whose LCSC number equals the part's, not the first
   result of the keyword search. A part off the board (rule 1) is found and
   counted at the second vendor of question S3, Digi-Key, through its API
   (prerequisite 3). P2 uses that API's keyword search, and the part's LCSC
   number is none.
4. **Stock gate.** `stockCount` at or above question S4's rule applied to the
   part's need, and `canPresaleNumber` zero or above. A negative presale count
   fails whatever the stock says. The need is boards (question S7) × placements
   per board. Where the specification fixes the placements (20 for each
   per-port part, 7 for the INA3221), that number is used. Where it does not, for example the
   MOSFETs (metal-oxide-semiconductor field-effect transistors) of a pack
   switch, P2 states a range and its basis, and the gate applies at the top of
   the range.
5. **A second source.** Either a pin-compatible alternate that also passes
   rules 1 to 4 and works in the same circuit and firmware configuration
   (registers, I²C address, start-up, surrounding parts), or the same part at the second vendor of question S3 with
   stock at or above the gate of rule 4 in that vendor's API, read as
   [Sourcing](Sourcing.md) describes. A part off the board is bought at the
   second vendor already (rule 1), so its second source is an alternate that
   passes rules 1 to 4 and fits the same mounting and connections. A part
   bought at the second vendor reaches JLCPCB assembly through the personal
   library (rule 6). The INA238
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
   URL (uniform resource locator) or API call it came from. A search engine's
   summary of a vendor page is not a source. At Digi-Key the research reads
   the API only and records stock, the manufacturer's lead time and the
   product status. The API carries no dated incoming quantity, and none is
   recorded (owner, 2026-09-27).

### Held parts

The owner's JLCPCB personal library, exported on 2026-09-25
([inventory-2026-09-25.csv](inventory-2026-09-25.csv)). The quantity is the
JLCPCB column of the export; the global-sourcing and consigned columns are 0 for
every row. A held part passes rule 4 by rule 6 when its quantity covers 5 boards
(S7) × its placements per board. It is still a candidate only when its maker is
on the list of S1 or S2 (rule 2) and it meets the category's requirement values.
P2 lists a held part that passes beside the other candidates and marks it held.

| Part | Maker | LCSC | Package | What it is | Held | Research use |
| --- | --- | --- | --- | --- | --- | --- |
| RP2354B | Raspberry Pi | C39843328 | QFN-80 10 × 10 mm | the microcontroller, 2 MB flash stacked; stepping A4 (owner, 2026-09-27, [fixed inputs](#scope)) | 20 | fixed input, R1 |
| RP2350B | Raspberry Pi | C42415655 | QFN-80 10 × 10 mm | the same die without flash | 25 | none in round 1; the board takes the RP2354B |
| TPS55285VALR | Texas Instruments | C52160906 | WQFN-15 | servo buck-boost converter | 12 | fixed input, R6 |
| ACS70331EESATR-2P5U3 | Allegro | C459299 | QFN-12 3 × 3 mm | Hall current sensor, 2.5 A, 800 mV/A, 1 MHz | 20 | R8, beside the INA3221 |
| ACS70331EESATR-005B3 | Allegro | C2649488 | QFN-12 3 × 3 mm | Hall current sensor, ±5 A, 200 mV/A, 1 MHz | 10 | R8, beside the INA3221 |
| ACS711KLCTR-12AB-T | Allegro | C459305 | SOP-8 | Hall current sensor, 12.5 A, 110 mV/A, 100 kHz | 5 | R8 |
| STM32L431CCY6TR | STMicroelectronics | C2053538 | WLCSP-49 3.1 × 3.1 mm | Cortex-M4 microcontroller, 80 MHz | 4 | none in round 1 |
| EPC23102 | EPC | C5160232 | QFN 3.5 × 5 mm | GaN half bridge, 100 V, 35 A | 9 | none in round 1; EPC is not on the list of S1 |
| WS2812B-1313-V6 | Worldsemi | C52941388 | 1.3 × 1.3 mm, 4 pads | RGB LED with driver | 50 | round 2, the indicators; Worldsemi is not on the list of S1 or S2 |
| CL05B104KP5NNNC | Samsung Electro-Mechanics | C133086 | 0402 | 100 nF 10 V X7R | 945 | round 2 |
| CL05A105MQ5NNNC | Samsung Electro-Mechanics | C318573 | 0402 | 1 µF 6.3 V X5R | 1000 | round 2 |
| C1005X5R1E225KT000E | TDK | C76595 | 0402 | 2.2 µF 25 V X5R | 57 | round 2 |
| GRM21BR61H106KE43L | Murata | C440198 | 0805 | 10 µF 50 V X5R | 86 | round 2 |
| CL31A106KAHNNNE | Samsung Electro-Mechanics | C9807 | 1206 | 10 µF 25 V X5R | 100 | round 2 |
| MSAST31LAB7106KTNA01 | not stated in the export | C6105339 | 1206 | 10 µF 25 V X7R | 36 | round 2, once its maker is known |
| C3225X5R1H106KT000N | TDK | C342671 | 1210 | 10 µF 50 V X5R | 5 | round 2; input capacitance of R5 and R6 |
| TMK325ABJ476MM-P | Taiyo Yuden | C90142 | 1210 | 47 µF 25 V X5R | 10 | round 2; Taiyo Yuden is not on the list of S2 |

Listed in the library with 0 held: STM32L431KCU6 (C1341298), ACS711KEXLT-15AB-T
(C150824), TPSM84209RKHT (C2836944), ACS70331EESATR-005U3 (C459297),
TPS82740BSIPR (C5187175), TPS82740ASIPR (C544660) and GRM32ER61A107ME20L
(C84455). A part held at 0 counts as not held.

## Lifecycle check

Recorded for every candidate that reaches the shortlist.

| Field | Source | Gate |
| --- | --- | --- |
| Manufacturer status | the manufacturer's product page. For Analog Devices (with Maxim and Linear), Melexis and Allegro, Digi-Key's API (`ProductStatus`), recorded as a distributor figure (owner, 2026-09-27). On 2026-09-27 the Analog Devices product pages carried no lifecycle status in the page body (the ADXL1002 page has `"lifeCycle":null`), and the host refused every request for a time after about 15. The Melexis pages refused every client tried. Allegro's pages load the status by script. Another product page whose status is absent from the page body is recorded as not read and reported to the owner | pass: active or in production. Fail: NRND (not recommended for new designs), last-time buy, obsolete, discontinued. Preview or sampling: fails unless question S5 accepts it. Digi-Key's `ProductStatus`: Active passes; Not For New Designs, Last Time Buy and Obsolete fail; any other value is recorded and reported to the owner |
| Longevity commitment | the manufacturer's longevity or product-lifecycle programme page, where it publishes one | recorded, and a gate only if question S5 makes it one. No published commitment is recorded as that |
| Market introduction | first datasheet revision date | recorded; under 12 months on the market is flagged |
| Change and discontinuation notices | the manufacturer's PCN (product change notice) listing, where public | any end-of-life notice fails |
| Distributor status | JLCPCB and LCSC part page; `ProductStatus` in Digi-Key's API | recorded. Where it disagrees with the manufacturer, the manufacturer's page is the status and the disagreement is written down. For Analog Devices, Melexis and Allegro parts, Digi-Key's `ProductStatus` is the status, and a JLCPCB or LCSC status that disagrees is written down |

## Research categories

Each row has an ID (identifier), R1 to R13. Each row runs one P1 and one P1 critic
in T1, then one P2, one P3, one re-rank and two P4 agents in its group's task
([Agent layout](#agent-layout)). The requirement
values each row is checked against are in [the specification](IOBoard.md) and
in the row itself. P1 reads both; "P1 asks" in either is a question P1 raises. A
front end in any category that drives one of the RP2354B's 8 ADC inputs keeps
that input within its rating while the 3.3 V rail is down and the front end's
supply is up. The seeds are starting points for the search, not choices. None
has been checked for stock, lifecycle or fit. The concerns column holds what
the plan's critics found against a seed. It rests on search-engine summaries,
which count as weak evidence, and on the tree.

| ID | Category | Seeds | Concerns found |
| --- | --- | --- | --- |
| R1 | RP2354B and its support: the part itself, 12 MHz crystal, core regulator inductor, USB (Universal Serial Bus) protection. R1 takes the RP2354B as stepping A4 (owner, 2026-09-27; [fixed inputs](#scope)); the marking on the held parts is not read. R1 lists each erratum of stepping A4 in the RP2350 datasheet that constrains a pin, a pull resistor, an input front end or a peripheral; the tree cites RP2350-E5 (`firmware/iomcu/src/out_ppm.c`). It confirms the pin ratings and pad figures the specification cites from build 2024-08-08 of the RP2350 datasheet. Build 2025-07-29, the copy `datasheets.raspberrypi.com` serves, numbers each of those tables three higher. The fault tolerance is in Tables 1423 (Pin Types), 1424 (GPIO pins) and 1430 (Absolute maximum ratings), which are 1426, 1427 and 1433 in build 2025-07-29. The pull-up at reset on SWDIO, the SWD (Serial Wire Debug) data pin, is in Table 1427 (Miscellaneous pins), which is 1430. The pad pull-up of 32 to 86 kΩ and the input leakage are in Table 1433 (Digital IO), which is 1436. It confirms the flash's part number and supply range, and records its erase, program and endurance figures as not known for the die ([above](#consequence-of-the-rp2354bs-2-mb-flash)). It states the crystal's frequency tolerance, on which the bidirectional DShot decoder and the CAN bit rate depend. It reports whether the BOOT contact on QSPI_SS, which is also the in-package flash's chip select, takes a series resistor, and whether OpenOCD's `target/rp2350.cfg` reaches the RP2354B and programs its in-package flash. It checks the watchdog timeout against the stop deadline P1 asks for and against the longest legitimate stall, a sector erase of up to 400 ms. No pico-sdk board header names the RP2354; `olimex_rp2350_xl.h` (pico-sdk 2.3.0) states the RP2350B and 2 MB of flash and defines no PSRAM chip select, and R1 records the lines the IO board's header changes from it | RP2354B; the inductor and crystal the RP2350 hardware design guide names | none |
| R2 | CAN controllers and transceivers: one for the display link, and one for the external CAN port, separate from the link (owner, 2026-09-25). The external port's transceiver has a standby input, held in standby while the enable node is low. Every output that reaches an RP2354B pin is at 3.3 V logic: a 3.3 V part, or a transceiver whose logic-supply (VIO) pin is at 3.3 V. R2 reports, for each controller, how many received frames it holds, against the stall of the RP2354B's own flash above and the number of frames the panel sends in that time, which the tree does not state and P1 asks. R2 states each controller's clock tolerance, which the 1 Mbit/s bit rate depends on | MCP2515, MCP2518FD, MCP251863 (controller and transceiver in one package); TCAN1042V, TCAN334, SN65HVD230, TJA1051T/3, TJA1462 | the bring-up module's XL2515 holds 2 received frames ([STATUS.md](../../STATUS.md#open-items)) |
| R3 | Safety gate, to the monostable specification of pull request #167 at commit 23c82ca, not merged (prerequisite 4 fetches it). Parts: a dual retriggerable monostable whose clear release does not trigger (the '423 behaviour), run from the 3.3 V rail. The OR gate of F10. The reset supervisor of F12: its threshold, with its tolerance, sits above the minimum supply of the monostable and the OR gate, and R3 states the band between that threshold and 3.201 V (3.3 V − 3 %), where the window is not specified, or picks the threshold that closes it. The latch of F11: the page at 23c82ca specifies no latch, the enable returning on the first resumed edge, and its test steps 7 and 10 assume that: step 7 times ten expiries in a row, and step 10 passes on the enable returning at each resumed edge. P1 asks the latch's power-up state, its place relative to the enable node and the gates, what the operator clear control is and where it sits, and whether a clear driven by a relay or by firmware meets the page's rule that no processor, register or firmware sits in the path from GPIO6 to a gate. The latch's inputs follow the powered-off isolation rule below. The high-side switch on the servo rail, which is the servo rail's gate: the 20 port switches of R6 are driven through the I/O expander, and that rule keeps a gate off it. P1 asks the owner to confirm that the port switches are not the gate. The switch on the ESC pack of F7: open within 5 ms of the enable falling, and rated for the pack voltage of F15 plus a margin that P1 asks the owner for. The ESC pack's input protection sits with the switch: reverse polarity, overvoltage and transients, and inrush into the ESC's input capacitance (precharge), for the pack voltage of F15, with the clamp level under the absolute maximum of every part on the pack node, the INA238's 85 V bus input among them, on each path at that path's current: 150 A (F2) on the onboard path, above 150 A to 300 A or more on the external path. R3's gates follow the enable node alone, so while the panel beats they stay enabled through an RP2354B reset. The coprocessor enables the RP2350 watchdog (owner, 2026-09-25); R6 checks that the port switches it drives open through that reset, and R4 the connector's idle level. F7 names a MOSFET array on the onboard path; its MOSFETs are qualified for that voltage, the current of F2, safe operating area during turn-off, the 5 ms budget, and current sharing and heat across the array. The external module of F7 is a part off the board (rule 1) and is qualified for the external path, not for the 150 A of F2: the pack voltage of F15, every current above 150 A up to its highest, 300 A or more, safe operating area during turn-off, the 5 ms budget, and its temperature rise at that current. It is a high-side switch, open with its control input undriven or the 3.3 V supply absent, within the servo rail gate's off-state leakage limit. The external path's highest current is not stated, and P1 asks. IOVDD is the RP2350's input/output supply. P1 checks the statement in pull request #167 that the RP2350 "is not specified as tolerating an input above an absent IOVDD" against the pin ratings the specification cites (RP2350 datasheet, build 2024-08-08, Tables 1423, 1424 and 1430, read from a copy). R1 confirms those ratings in T2; where R1's figures differ, P5 checks the statement again. Every logic input driven while its own supply is absent has I_off (a datasheet limit on the input current with the input above the device's supply and the supply at 0 V), or a series resistor of 4.7 kΩ or more where the specification's isolation table allows one. The monostable's trigger inputs take both. Whether any part has both the '423 behaviour and I_off is not known; R3 reports that first. If no part has both, R3 also shortlists parts with I_off to go in front of the trigger inputs, and the owner decides Q9 before T6 | 74HC423, SN74LV123A; 74LVC1G32; TPS3839; TPS48110-Q1, LTC7001, 2ED4820 as MOSFET drivers; 80 V and 100 V N-channel MOSFETs from Infineon, onsemi, Nexperia, Vishay and Texas Instruments | 74HC423: its inputs clamp to its supply, so I_off is not shown. SN74LV123A: has I_off, and its clear release triggers ('123 behaviour), which the specification excludes. 74HCT423: specified at 4.5 to 5.5 V only, not a candidate on 3.3 V |
| R4 | Output and input buffering. The output stage is the gated buffer of the monostable specification (pull request #167): an enable driven from the enable node, high impedance when disabled, I_off on every input on the RP2354B side and on the enable, and the bidirectional DShot reply passed while enabled with the bias of F14. The element that selects the bias per port is part of R4. P1 asks what selects it, whether a selector driven by firmware or by the I/O expander meets the monostable page's rule that no processor, register or firmware sits in the path from GPIO6 to a gate, and which bias the UART socket and the ESC bootloader, ESC telemetry and servo configuration modes take, whose lines idle high. It carries 3.3 V to servo and ESC signal levels. Also: the 4 multiprotocol ports (owner, 2026-09-25), which carry the one-wire bootloader at 19,200 baud (BLHeli_S, AM32), the ESCape32 text CLI (command-line interface), VESC's framed packets at 115,200 baud and the Hitec D-series servo protocol (`shared/ui/programmer_screen.c`); the one-pin UART socket, which carries the OpenYGE telemetry line ([OpenYGE](../../docs/OpenYGE.md)), half duplex at 115,200 baud 8N1 (8 data bits, no parity, 1 stop bit); the receiver inputs. R4 reports whether a programming session switches the ESC's or the servo's supply to enter a bootloader. The buffer's enable follows the monostable, not the RP2354B, so it can stay enabled through a watchdog reset (owner, 2026-09-25). R4 checks that every connector output is at its idle level while the RP2354B is held in reset: the buffer's inputs see the RP2350's reset-state pad bias, or an added bias, and the level that reaches the connector is recorded | LSF0108, 74LVC1T45, SN74LXC1T45, 74LVC8T245, 74LVC245A; series resistance and clamps | the ESCape32 CLI's baud rate is stated only in the test bench's proposed analyser map, which puts ESCape32 on the one-wire line at 19,200 baud (`testbench/README.md`, channel 12). The Hitec protocol's levels and rate are not stated in the tree. P1 asks both |
| R5 | Board power input and protection: reverse polarity, overvoltage, inrush, automatic selection between the 12 to 20 V DC input and the 2S pack, and the hardware disconnect of the pack below the floor of F9, with hysteresis so the load returning does not reconnect it. The 3.3 V logic buck, the 5 V rail, the display's supply on the link cable (F6), the receiver's supply if the board powers one, and the encoder's 12 V supply of F4, made from both sources: stepped down from the 12 to 20 V DC input and boosted from the 6.0 to 8.4 V pack. The 12 V supply's input spans 6.0 to 20 V, below and above its output, so it is a buck-boost converter: a step-down stage is in dropout at the 12 V DC input. The display's supply is budgeted to the display's end of the link cable: R5 states the lowest voltage the display takes and the cable's allowed drop at 2 A, and round 2 selects the cable conductors and connector for that current. The lowest voltage at the display is not stated, and P1 asks. The display's supply blocks reverse current, so a display powered from its own USB-C at the same time feeds nothing back into the IO board. Whether the link supply then reaches the display's USB-C VBUS depends on the display's own power path, which is not in the tree; P1 asks the owner for it. If the display does not isolate its VBUS, R5 states it as a conflict. R5 sizes the 3.3 V logic buck and the 5 V rail from the supply currents of the parts T2 and T4 selected, as P4 verified them, plus the display's draw (prerequisite 7). The parts R6, R7 and R8 select in the same task are not in that sum. P5 checks each rail's current with them; a rail whose load exceeds the rating of the part R5 selected is a conflict that goes to a follow-up task, which selects that rail's part again. The pack path carries the servo supply's input current. 8.4 V at 6.35 A is 53.3 W. At 90 %, the efficiency [Power](Power.md) uses for the DC input, that is 7.1 A from a pack at 8.4 V and 9.9 A at 6.0 V, the floor of F9 (2 cells at 3.0 V). The efficiency from the pack is not stated. The logic rails and the display add to it. The 3.3 V rail stays within 3.3 V ±3 % at the monostable, including reference accuracy, feedback resistors, ripple, load regulation and drift over 0 to 50 °C, because R3's 155 to 185 ms window holds only across that range. | LM74700, TPS3700, TPS2663, TPS25947; LMR36015, TPS62933, TPS563300 | TPS25947: operates to 23 V, above the 20 V input, with 3 V of margin; checked against the input's tolerance. LM66200 (rated to 5.5 V at most) and BQ29700 (one cell) do not fit and are not seeds |
| R6 | Servo supply: the TPS55285 re-checked from both inputs, 12 to 20 V and the pack from the floor of F9 to its full charge, including its inductor current at the low end; its inductor; 20 per-port supply switches for up to 8.4 V and the I/O expander that drives them, reset with the RP2354B so a watchdog reset opens every port switch; the enable stays high through that reset while the panel beats, so an expander held off only by the enable does not open them; the per-port voltage ceiling of F16 | TPS55285; TPS2595 | TPS22990 and TPS22918 are rated to 5.5 V and do not switch the 8.4 V setting; not seeds |
| R7 | Pack charger for the 2S pack, with balancing, charging from the source and at the current of F8, with a power path or load sharing so the bench runs from the pack while it charges. The charger's input limit follows the USB-C source's current advertisement on its CC (configuration channel) pins: default USB current (500 mA for USB 2.0, 900 mA for USB 3.2 single-lane; USB Type-C specification, not in the tree), 1.5 A or 3 A. R7 selects a charger that reads it or a CC detector that sets the limit, so a source offering less than 3 A is not overloaded. Also the pack itself: the 18650 cell model and the pack's protection. The pack sources the servo supply's 9.9 A at the 6.0 V floor plus the display's 1.5 A continuous at 5 V (about 1.4 A from the pack at an assumed 90 %) and the logic rails; R7 checks the cell's continuous-discharge rating, its temperature limits and the pack's overcurrent protection against that. P5 recomputes the pack current from the converter efficiencies R5 and R6 verified, and checks the cell and the protection again; a shortfall is a rail conflict. The cell makers allowed are those of S9 | BQ25887, BQ25798 with BQ76907 or BQ29209 | MP2672A is from Monolithic Power Systems, which S1 does not allow; not a seed |
| R8 | Current and voltage monitors and the motor shunts: the motor monitor or monitors (F1); the onboard 150 A shunt resistor (F2); the external shunt, a part off the board (rule 1), and the IO board's input for its sense leads; the temperature sensor at the onboard shunt; the monitors of the 20 output ports and their shunts, to the figures of [the specification](IOBoard.md): the INA3221, 7 parts with a 10 mΩ shunt a port, is the owner's choice (2026-09-25) and a seed that R8 may replace (owner, 2026-09-27). P1 asks which source fills the BENCH voltage and current registers when the motor monitor and the ESC's telemetry both report. P1 asks the motor overcurrent and over-temperature thresholds, the temperature the over-temperature action watches, the trip filter and the longest time from detection to the ESC pack switch open, which P5's budget of the motor overcurrent path needs | INA3221 (owner, 2026-09-25); INA238, INA228, INA236, INA745A; TMP117, TMP1075; Isabellenhütte BV series, Vishay WSBS8518 ([Power](Power.md)) | none |
| R9 | Cell monitor on the ESC pack's balance lead, 1 to 16 cells, up to 67.2 V (F15), on an input separate from the 2S pack's lead of F9, because both packs are connected during a motor test: the pack of F15 has 16 cells, above the 1 to 14 that `SET_PACK_CELLS` and the battery screen take, which change with it | BQ76952, BQ76942, ADBMS6948, LTC6813 | BQ76952 reads 3 to 16 cells and BQ76942 3 to 10, so neither reads 1 or 2 cells. ADBMS6948 (16 channels) and LTC6813 (18 cells): lowest cell count not found |
| R10 | ADC and reference (question Q4): the RP2354B's own ADC with an external reference, or an external converter | REF3033, LM4040; ADS131M04, ADS1115, ADS112C04 | none |
| R11 | Rotation and vibration front ends: accelerometer, optical index pulse, magnetic pickup, phase-wire rpm clip rated for the pack voltage of F15, the encoder input (F4). R11 reports how the encoder is decoded at 5 MHz: PIO (programmable input/output) state machines, DMA (direct memory access) channels, a PWM slice as a counter, or an external decoder, and P5 reserves those resources. P1 asks which sensor gives the servo's measured position (the encoder of F4 can sit on the servo output shaft), and which source fills the BENCH rpm register when more than one rotation input is present | ADXL1002, ADXL1005; TLV3201, TLV7011; AM26LV32, SN65LBC175 | IIS3DWB is digital, on SPI (Serial Peripheral Interface); the specification asks for an analogue accelerometer, so it is a seed only if Q4 accepts a digital one. SN65LBC175 runs from 5 V; its outputs are translated to 3.3 V |
| R12 | Motor temperature (F5), external I²C (Inter-Integrated Circuit) ports (Q7) and the non-volatile store (Q8). P1 asks which of the 2 motor temperature channels fills the BENCH page's one motor temperature register, or whether a second register carries the other | MCP9600, MAX31856, MLX90614; TCA9548A, PCA9615, TCA9617A; FM24CL16B, MB85RC256V, 24LC256 | MLX90614: Melexis, allowed (S1); its product pages refused every client tried on 2026-09-27, so its status is read at Digi-Key ([Lifecycle check](#lifecycle-check)). MB85RC256V: sold as RAMXEED, which S1's Fujitsu covers (owner, 2026-09-27); JLCPCB labels the stocked part C45273903 "RAMXEED/FUJITSU"; other MB85RC256V rows carry "RAMXEED/FUJITSU", "FUJITSU", "Fuji Electric" or "JLCPCB Assembly", and the maker is read from the datasheet (rule 2) |
| R13 | Load cells for thrust and torque: bridge ADC and excitation (F3) | ADS1232, ADS1234, ADS124S08, AD7124-4 | none |

## Agent layout

Every finding is checked by a critic that did not produce it and is told to
refute it. Nothing reaches a page or the owner on one agent's word. P0 returns
reachability, not a finding, and has no critic. A critic of each P1 re-derives
its markings and questions before they reach the owner, and one agent re-checks
every question a critic added (owner, 2026-09-27). Each agent returns a
fixed schema, so a critic compares like with like. An agent that returns
nothing, because it failed or was skipped, has checked nothing: a missing
verifier or critic never counts as one that did not refute. The script starts
that role again from the task's free agents, and with none left the item goes
to a follow-up task.

A task is one workflow run of at most 32 agents. The workflow script counts
the agents a task plans before it starts and refuses a task over 32. It counts
each agent it starts, the adjudicators and verifier pairs among them, and moves
work that does not fit into a follow-up task, which it returns with its result.

A workflow script has no file or git access. The workflow returns each
agent's result in its fixed schema. The session that launched the task writes
each result as one JSON (JavaScript Object Notation) file under
`hardware/research/round1/<task>/` on the results branch of prerequisite 5. It
commits them before the next task starts (owner, 2026-09-27). The session
passes each task the commit of the branch of prerequisite 4 that it reads; P0
checks out that commit read-only, and the session records it beside the
returns. A later task reads what earlier tasks returned from those files only,
and the owner's answers from the commit of the branch of prerequisite 4 that it
records. P7
writes the pages from them, and its critic checks each figure against them.

```text
T1  P0 reachability (1)                                              28
    P1 requirement critic (1 per category)
    a critic of each P1 (1 per category)
    P1 re-check of the questions the critics added (1)
                                 -- the session writes P1's questions
                                    under "Raised by P1"; the owner
                                    answers them
T2  group A: R1 R2 R3 R4        P0 (1), then per category:           21
T4  group C: R9 R10 R11 R12 R13   P2 discover and qualify (1)        26
                                  P3 search critic (1)
                                  P2 re-rank with P3's finds (1)
                                  P4 stock and lifecycle verifier (1)
                                  P4 datasheet and pin verifier (1)
T3  group B: R5 R6 R7 R8        the same, after T2 and T4            21
T5  P5 cross-category checks (1) and its critic (1),                  4
    P6 completeness (1) and its critic (1)
                                 -- gaps become follow-up research tasks
                                 -- the owner decides Q4, Q8 and Q9
T6  P7 write the pages (1) and a page critic (1)                      2
```

After T1, T2 (group A) and T4 (group C) run in either order. T3 (group B, power)
runs after both, because R5 sizes the rails from the parts T2 and T4 select.
Within a task, the categories run side by side, and each category runs P2,
P3, the re-rank and P4 in that order.

| Phase | Agents | Does | Returns |
| --- | --- | --- | --- |
| P0 | 1 | fetches one known page from every host in [Prerequisites](#prerequisites), each with the client the agents use for that host, and records the client per host. For a manufacturer's host the known page is a seed's product page that carries its lifecycle status, not a home page, and P0 checks that the status is in the page body: a host can answer HTTP (Hypertext Transfer Protocol) 200 for a path that does not exist, as `yageogroup.com` did on 2026-09-27. A manufacturer's page that answers without the status is reachable, with the status not read ([Lifecycle check](#lifecycle-check)). For `jlcpcb.com` and `api.digikey.com` P0 makes one API call, and for every other host it fetches one named file. It checks the `cache.sqlite3` of [Sourcing rule 3](#sourcing-rules) against the SHA-256 recorded for it, that `jlc_components` holds 7,161,863 rows, and that every LCSC number this page names is in it. It records as the snapshot date the `created` time of the copy's `manifest.json`, 2026-09-14T09:56:01Z; the database also dates each part separately. In T1 only, it fetches commit 23c82ca6ca976d956098cfafd25dbefa11584f5d into the read-only directory of prerequisite 4. Every P2, P3 and re-rank agent in T2 to T4 and in follow-up tasks searches that copy. Runs at the start of T1 and of each research task | reachable or not, per host, with the client used, and the result of each check on the copy. The task stops if the copy fails a check, or, in T1, if that commit cannot be fetched. A task that reads stock, T2 to T4 and the follow-up tasks, also stops if JLCPCB's API or Digi-Key's API is unreachable. In T6, where only P7's `tools/jlc_stock.py --check 5` reads stock, an unreachable API is reported in that check's result. From T2 on, a category is held while a site it reads is unreachable: the site of a manufacturer whose parts it seeds, because the lifecycle gate cannot be read, and `www.winbond.com` for R1. Analog Devices, Melexis and Allegro parts are read at Digi-Key ([Lifecycle check](#lifecycle-check)), so an unreachable site of those makers holds no category. In T1, P0 holds a category only when a source its P1 reads is unreachable |
| P1 | 1 per category, and 1 critic per category | reads the category's row on this page and its lines in [the specification](IOBoard.md), and every source they cite, and tries to refute each value: does it follow from its source, is the unit right, is it an owner decision or an assumption. Lists, for each function, the requirement values P2 needs to qualify a part (voltage, current, range, resolution, rate, accuracy) that the specification does not state. For R5, the supply currents of the parts T2 and T4 select are inputs R5 takes from those tasks, not questions to the owner. The critic re-derives each marking and question, and looks for values P1 did not mark and missing values P1 did not list | each value marked sourced, owner decision or assumption, and each missing value. Every assumption and every missing value is a question to the owner, written under [Raised by P1](#raised-by-p1). P2 does not start on a function with one unanswered, unless the value only feeds a decision under [Decided on the research's output](#decided-on-the-researchs-output) |
| P1 re-check | 1 | re-derives, from the same sources, every question a P1 critic added and P1 did not raise (owner, 2026-09-27) | each added question confirmed or rejected, with the evidence. Only confirmed questions are written under [Raised by P1](#raised-by-p1) |
| P2 | 1 per category | finds each value the owner marked for research at its primary source, and records it as found, not given. Lists candidates in the jlcparts copy, searched by part number, description and category as rule 3 sets out, and for a part off the board in the keyword search of Digi-Key's API (rule 3), and keeps those from allowlisted manufacturers (rule 2). Drops those that miss a requirement value. For up to five survivors per function, records part number, manufacturer, LCSC number (none for a part off the board), package, every requirement value against the datasheet's, placements per board, JLCPCB stock, presale, library type and price, second-vendor stock and the manufacturer's lead time from Digi-Key's API (no incoming quantity, owner, 2026-09-27), the lifecycle fields above, pin-compatible alternates, and whether a driver exists under `shared/` | a ranked shortlist per function with the reason for each rank, and every candidate dropped with the reason |
| P3 | 1 per category | searches for part families P2 did not consider, from the same allowlist, and re-reads each reason P2 gave for dropping a candidate | missed candidates and exclusions that do not hold |
| Re-rank | 1 per category | qualifies what P3 returned as P2 does, and re-ranks each function's shortlist | the final ranking per function |
| P4 | 2 per category | two independent verifiers, each told to refute, each covering every verified part of the category. One re-reads stock and lifecycle at the primary sources. One re-reads every requirement value in the datasheet, and for an alternate, the pin-for-pin match and the functional match to the part it stands in for. Verified: the first-ranked part of each function, the first-ranked part of each alternative of Q4 and Q8 the owner can choose, and the alternate that satisfies sourcing rule 5 for each when the second source is an alternate rather than a second vendor. The datasheet verifier also re-reads every figure the category reports for a decision or for P5: R1's list of stepping-A4 errata, the frames each R2 controller holds, R3's report on the '423 behaviour with I_off, the effective number of bits, sampling rate, trigger and latency jitter of each Q4 alternative in R10, each Q8 store candidate in R12, R1's crystal tolerance, BOOT resistor, OpenOCD and watchdog findings, and each R2 controller's clock tolerance. It re-reads each value found for research, and each reason the re-rank gave for dropping a candidate P3 returned. A part stays when neither verifier refutes it. A refutation stands only after one adjudicating critic re-reads the evidence. A standing refutation sends the next-ranked candidate to a new pair of verifiers. The adjudicator and the new pair come from the task's free agents (Size, below) | confirmed or refuted, with the evidence, per part and per figure; the adjudicator's ruling on each refutation |
| P5 | 1, and 1 critic | the resource budget of the whole board against the RP2354B, from the RP2350 datasheet and R1's errata: 48 GPIO; the rule that a PIO (programmable input/output) block sees GPIO 0 to 31 or 16 to 47 only; 3 PIO blocks of 4 state machines, 12 in all, each block with 32 instructions of memory; 16 DMA (direct memory access) channels, two per PPM (pulse-position modulation) output (`firmware/iomcu/src/out_ppm.c`); 12 PWM (pulse-width modulation) slices, where two pins on one slice and channel conflict (`shared/outputs/include/out_pwm_map.h`); the 8 ADC inputs; 2 SPI, 2 I²C and 2 UART (universal asynchronous receiver-transmitter) controllers with their pin options. The PIO, DMA, PWM and serial-controller counts are from `platform_defs.h` for the RP2350 in `pico-sdk`. With `PICO_RP2350A` at 0 it gives 48 GPIO and a `NUM_ADC_CHANNELS` of 9: the 8 inputs and the temperature sensor. Plain DShot takes one state machine and bidirectional DShot two in one block (`firmware/iomcu/src/out_dshot.h`): the receiver takes state machine (transmitter + 1) modulo 4, so state machine 3 pairs with state machine 0 (`firmware/iomcu/src/out_dshot.c:79`). Each bound output loads its own copy of its PIO program (`firmware/iomcu/src/out_dshot.c:88`, `:155`; `firmware/iomcu/src/out_ppm.c:183`); pico-sdk 2.3.0 places each copy in free space and never reuses a loaded one (`src/rp2_common/hardware_pio/pio.c:66-81`). A bidirectional DShot port therefore takes 16 of its block's 32 instruction words, and a block holds at most 2 bidirectional DShot ports. Which combinations fit depends on the order the outputs are bound; P5 counts instruction words per bound output and states the bind order its combinations assume. The receiver buses, the programmer and the encoder of R11 take a number of state machines the tree does not state: no receiver PIO program is written ([Receivers](../../docs/Receivers.md)) and no programmer protocol has run on a wire; P5 states the count it assumes for each, and whether a bus uses a hardware UART instead. Bidirectional DShot on the 4 multiprotocol ports takes 8 of the 12, PPM on one PWM port 1 and 2 DMA channels; P5 returns the output combinations the board supports. P5 budgets both alternatives of Q4 and Q8, budgets the in-package flash with the packaged parts' maximums, each marked as a packaged-part figure (owner, 2026-09-27), checks the I²C address map and the current per rail, checks each part on the ESC pack node against R3's clamp level, runs its checks for each function's alternate as well as its first-ranked part, and budgets each I²C bus: the conversion time and the read of every device on it, the alert pins as GPIO, the motor overcurrent path, from the INA238's conversion and averaging through its ALERT pin or the poll and the coprocessor's action to the ESC pack switch's turn-off, and the worst-case time from a servo current sample to the abort of the 3.0 A ceiling, which `docs/Servo.md` checks at every sample, against the 80 ms and 100 ms averaging windows of the servo procedures. The sample period is not stated in the tree; P5 states the one it assumes, checks that R4's buffer enable and R3's enable node agree in polarity, checks the power-up sequence: the threshold and delay of R3's reset supervisor (F12) against the start-up ramp of the 3.3 V regulator R5 selects, so the clear releases only after the rail is inside its range, and applies the allowlist over the whole list. Where R1's pin ratings differ from those the specification cites, P5 checks the statement of pull request #167 on an absent IOVDD again (R3). The critic re-derives each conflict and looks for ones the first agent missed | conflicts, each naming the parts involved; the supported output combinations |
| P6 | 1, and 1 critic | every specification line has a part or is marked "not round 1"; every figure has a date and a source; every question under Raised by P1 is answered, and every value marked for research is found. The critic re-reads the specification line by line against the evidence and looks for gaps P6 did not report | gaps, each of which becomes a follow-up task (below) |
| P7 | 1, and 1 critic | writes the outputs below from the returns committed under `hardware/research/round1/`, P5's budget and combinations among them, and the owner's decisions on Q4, Q8 and Q9. The critic checks every figure and every stated combination on the pages against those files and every sentence against the writing rules in [CONTRIBUTING.md](../../CONTRIBUTING.md#writing), runs `python3 tools/check_docs.py` and `ruff check tools/`, and runs `tools/jlc_stock.py --check 5` once against the new `Parts.md`, 5 being the boards of S7 | the pages, a list of corrections applied, and the three check results |

Size. T1 28, T2 21, T4 26, T3 21, T5 4, T6 2: 102 agents in six tasks, the
largest 28 (S8). T1 keeps 4 agents free, T2 and T3 keep 11, T4 keeps 6, T5
keeps 28 and T6 keeps 30. The free agents take the adjudicators and the
verifier pairs that refutations call for, and the restarts of agents that
return nothing. A refutation costs 3 agents: 1 adjudicator and 2 verifiers for
the next-ranked part. With no restarts, T2 and T3 absorb 3 refutations each,
and T4 absorbs 2. More go to a follow-up task. A host that P0 reached and that
refuses an agent later in the task, as `www.analog.com` did after about 15
requests on 2026-09-27, is recorded with the time and the client; the figure
is recorded as not read, and the item goes to a follow-up task.

Follow-up tasks. A gap P6 returns, a conflict P5 returns, the owner's choice on
Q9 when it adds or changes a part, or a refutation a task cannot absorb,
becomes a follow-up task of at most 32 agents. It runs P0, then for each
category the phases of T2. For a gap that is a missing requirement value, P1,
its critic and the P1 re-check run first and raise the question under Raised
by P1, and P2 waits for the owner's answer. Follow-up tasks run in at most 2 rounds. After a
follow-up task that changes a part, P5 and its critic run again, then P6 and
its critic. A gap left after the second round is written on the pages as not
known and reported to the owner. Follow-up tasks add to the total of 102.

## Outputs

| File | Content |
| --- | --- |
| `hardware/docs/IOBoard.md` | the specification, with the chosen part on each line |
| `hardware/docs/Parts.md` | one row per part: function, part number, manufacturer, LCSC number (none for a part off the board, rule 1), package, JLCPCB stock, presale and library type with the date, placements per board, the quantity held in the owner's personal library with the date it was stated, second source, lifecycle status, longevity, alternate |
| `hardware/docs/Power.md` and one page per category group in its form | the choice, the alternatives, the stock, the reason. A figure no verifier re-read is marked not verified |
| `hardware/STATUS.md` | the decided and open tables |
| `hardware/README.md` | one row per new page in the page table |
| `hardware/docs/Research.md` | the status line: the dates round 1 ran and the commit of the branch of prerequisite 4 that each task read |
| `tools/jlc_stock.py` | re-queries JLCPCB's API for every LCSC number in `Parts.md` and for each row's alternate, taking the build quantity as an argument. It reads the result whose LCSC number equals the row's. A part's gate is rule 4 of the sourcing rules. `--check` exits 1 when an LCSC number returns no exact match. It also exits 1 when a part fails rule 4, by stock under its gate or `canPresaleNumber` below zero, and its alternate fails rule 4 too. A part that fails rule 4 while its alternate passes is printed as a warning. A row whose second source is Digi-Key, and a part off the board with its alternate, is queried at Digi-Key's API with the owner's credentials (`DIGIKEY_CLIENT_ID` and `DIGIKEY_CLIENT_SECRET` in the environment). `--check` exits 1 when a second-vendor stock is under the part's gate, and for a part off the board when the part or its alternate is under the gate, since the alternate is its only second source (rule 5). Without the credentials those rows are printed as not checked and `--check` exits 1. A personal-library row is checked by rule 6 and printed apart, with the date its quantity was stated. Run by hand before an order, not in CI (continuous integration): a stock count moving is not a defect in the tree |

## Prerequisites

1. **Where it runs.** On the owner's server, 48 CPUs (owner, 2026-09-24) and
   128 GB of memory (owner, 2026-09-25). In place on 2026-09-27: 48 CPUs,
   125 GiB of memory visible to the system, Claude Code 2.1.283, git 2.43.0,
   Python 3.12.3, ruff 0.16.5, 7-Zip 23.01 for the split jlcparts archive, and
   curl_cffi 0.16.3 (below). The workflow tool runs at most 16 agents at once
   there, min(16, CPUs − 2), with `CLAUDE_CODE_WORKFLOW_MAX_CONCURRENT_AGENTS`
   unset. The session's Dynamic workflow size setting is advisory; the tasks run at the
   sizes S8 approves (owner, 2026-09-27). Every agent runs on the session's
   model at the session's effort (owner, 2026-09-27). The run records these
   versions, the model, the effort and that number again.
2. **Network access from that server.** P0 checks every host. At minimum:
   `jlcpcb.com`, `www.lcsc.com`, `datasheets.raspberrypi.com` and
   `pip-assets.raspberrypi.com`, where it redirects, `www.raspberrypi.com`,
   `www.winbond.com` (R1 reads the W25Q16JV datasheet), `github.com` and
   `raw.githubusercontent.com` (Raspberry Pi's `pico-sdk` and `documentation`
   repositories), `api.digikey.com` (question S3), and each allowlisted
   manufacturer's site under its current host name: `www.ramxeed.com` for
   Fujitsu's FRAM (ferroelectric random-access memory), `yageogroup.com` for
   KEMET and Yageo, `www.infineon.com` for Cypress, `www.analog.com` for Maxim
   and Linear, `www.isabellenhuette.com` and `www.epsondevice.com`. Observed on
   2026-09-27 from the server, with no proxy: plain `curl` is refused, by HTTP
   403, HTTP 404 at a product page or no answer, at `www.raspberrypi.com`,
   `www.microchip.com`, `www.nxp.com`, `www.onsemi.com`, `www.rohm.com`,
   `www.st.com`, `www.samsungsem.com`, `toshiba.semicon-storage.com`,
   `www.diodes.com`, `www.melexis.com`, `www.torexsemi.com`,
   `product.tdk.com`, `www.coilcraft.com`, `www.bourns.com`, `abracon.com`,
   `www.analog.com` and `www.digikey.com`. curl_cffi, a Python
   client that presents Chrome's TLS (Transport Layer Security) handshake,
   reaches every one except `www.analog.com`, `www.melexis.com` and
   `www.digikey.com`. At `www.analog.com`, curl_cffi presenting Safari's
   handshake loads product pages, and datasheet PDFs (Portable Document
   Format files) load. A loaded product page carries no lifecycle status in
   its body. After about 15 requests the host refused every request from
   curl_cffi for a time whose length is not measured; the ADXL1002 page loaded
   again later on 2026-09-27. Observed on 2026-09-24 from a cloud
   container: its egress proxy answered HTTP 403
   for `jlcpcb.com`, `www.lcsc.com`, `yaqwsx.github.io`, `www.ti.com`,
   `www.raspberrypi.com`, `datasheets.raspberrypi.com`, `pip.raspberrypi.com`,
   `www.nexperia.com`, `www.analog.com`, `www.microchip.com`,
   `www.digikey.com` and `www.winbond.com`, and passed `github.com` and
   `raw.githubusercontent.com`.
3. **Digi-Key's API**, the second vendor of S3. In place on 2026-09-27:
   Product Information V4 with the owner's credentials, `DIGIKEY_CLIENT_ID`
   and `DIGIKEY_CLIENT_SECRET` in the environment ([Sourcing](Sourcing.md#digi-key)).
   P2 and P4 read stock, the manufacturer's lead time and the product status
   there, and P2 uses its keyword search. The `tools/jlc_stock.py` that P7
   writes reads stock there. Digi-Key's pages
   sit behind a Cloudflare challenge: plain `curl` and curl_cffi get HTTP 403.
   P0 checks the API, not a page.
4. **The repository** checked out on the server at branch `research/round1`,
   so every agent reads the same specification. The branch starts from `main`
   after the pull requests that carry the owner's decisions of 2026-09-27 and
   the workflow scripts of prerequisite 6 are merged, and before pull request
   #167 merges (owner, 2026-09-27): #167
   rewrites lines of `testbench/WIRING.md` and `docs/Safety.md` that the
   specification cites by number. The branch is kept until P7 finishes. Its starting commit is recorded in the status line. The owner's answers arrive on that branch in stages. Each
   task reads the branch's commit at its start and records it with its
   returns. P1, R3, R4 and P5 read `hardware/docs/Monostable.md` and the pages
   it links at commit 23c82ca6ca976d956098cfafd25dbefa11584f5d, the head of
   pull request #167 on 2026-09-24 (branch `monostable-specification`, open,
   not merged). P0 fetches that commit by its full name into a read-only
   directory beside the checkout. The description of pull request #167 is not
   a source: it gives a 155 to 200 ms window, and the page at that commit
   gives 155 to 185 ms with a 200 ms deadline. The pages the monostable page
   links are read at that commit too, and there `hardware/docs/Power.md`
   predates the owner's decisions: it gives the TPS55288 and a servo rail of
   4 to 8 A. Where the two differ, the owner's decisions on the branch hold
   ([the specification](IOBoard.md#conflicts-in-the-tree)). Pull request #167
   conflicts with `main` in `STATUS.md` and `hardware/STATUS.md`, and the
   conflict is resolved by hand when it merges; the research reads it by
   commit, so the conflict does not hold the run.
5. **Access on the server.** Model access for the workflow's agents, read
   access to `github.com/subtilitas/rcbench` including branch
   `monostable-specification`, push access to the branch of prerequisite 4
   for the questions P1 raises, and push access to the results branch,
   `research/round1-results` (owner, 2026-09-27). The token in use has push
   access (2026-09-27). The results branch starts from the same `main`
   commit as the branch of prerequisite 4. The session commits each task's
   returns to the results branch. Before T6 the session merges the branch of
   prerequisite 4 into it, so P7 works on a page that carries every answer.
   P7 and its critic write the pages there, and the session commits them after
   T6. While the tasks run, nothing is pushed to `main`, and a pull request of
   the results is opened only when the owner asks for one.
6. **The workflow scripts and their schemas**, in `tools/research/`
   ([its README](../../tools/research/README.md)): `round1.js`, one run per
   task; the return schema of each role; the host table; `vendors.py`, the
   readings agents take; and `session.py`, which prepares a task, records its
   returns and commits them. `session.py check` holds them to this page in CI
   (continuous integration), with a dry run of every task on mock agents. They
   reach `main` through a pull request that the automated reviewers and the
   owner review, before T1 runs (owner, 2026-09-27).
7. **The display's current draw** on the link cable at the voltage of F6, peak
   and steady: 2 A peak and 1.5 A continuous at 5 V (owner, 2026-09-25).
8. **The owner's answers** to the questions below.
9. **The held parts** in the owner's personal library, exported on 2026-09-25:
   [Held parts](#held-parts). The RP2354B is held at 20 against a need of 5 for
   the first build (S7).

## Questions for the owner

Answered on 2026-09-24 and 2026-09-25: power, motor current, servo current,
the sensor inputs, where the research runs, the checking rule and the size of
a task, recorded under [Scope](#scope) with the decisions of 2026-09-27. A
proposed answer is not an answer: the tasks read the Answer column only.

### Sourcing

S1, S3 and S8 block T1. S2 and S4 to S7 block T2, T3 and T4. S9 blocks R7
(T3).

| ID | Question | Proposed answer | Answer (owner, date) |
| --- | --- | --- | --- |
| S1 | Which manufacturers are allowed for ICs? | Analog Devices (with Maxim and Linear), Infineon (with Cypress), Microchip, Nexperia, NXP, onsemi, Raspberry Pi, Renesas, ROHM, STMicroelectronics, Texas Instruments, Toshiba, Diodes Incorporated, Vishay. Undecided: Monolithic Power Systems, Richtek, Silergy, SG Micro, 3PEAK, Nisshinbo, Torex, Allegro, Melexis, Bosch Sensortec, ams OSRAM, Fujitsu | the proposed list, plus Melexis, Allegro, ams OSRAM, Bosch Sensortec, Fujitsu, Nisshinbo and Torex. Not allowed: Monolithic Power Systems, Richtek, Silergy, SG Micro, 3PEAK (owner, 2026-09-25). Fujitsu covers RAMXEED, the name its FRAM parts are sold under (owner, 2026-09-27) |
| S2 | Which makers are allowed for the parts that are not ICs? Round 1 selects the RP2354B's crystal and regulator inductor, the servo supply's inductor, both shunts, and the ESC pack switch; round 2 selects every other passive. | round 1 and round 2 alike: the S1 list, plus Murata, TDK, Würth Elektronik, Coilcraft, Bourns, Isabellenhütte, KEMET, Panasonic, Yageo, Samsung Electro-Mechanics, Abracon, Epson and NDK | the proposed list (owner, 2026-09-25) |
| S3 | Is a second vendor required, as [the hardware README](../README.md) states, or is JLCPCB the only source? Parts off the board are bought at the second vendor (rule 1). An answer of JLCPCB alone also names where those parts are bought, and rule 5's second source is then the alternate only. | keep the rule, with Digi-Key as the second vendor | required; Digi-Key (owner, 2026-09-25) |
| S4 | What is the stock gate? | 10 times the need of the build being ordered, never under 100 | 5 times the need of the build being ordered, never under 50 (owner, 2026-09-25) |
| S5 | Which lifecycle states pass? | active only; preview fails; a longevity commitment is recorded and not required | active only; preview fails; a longevity commitment is recorded and not required (owner, 2026-09-25) |
| S6 | Are extended-library parts acceptable? Each unique one adds a loading fee per order. | yes; basic preferred where two parts are otherwise equal | yes, chosen on merit; no preference for basic (owner, 2026-09-25) |
| S7 | How many boards in the first build, and are the parts bought into the personal library ahead of the order? A part in the personal library is not substituted between quote and build. | owner to state | 5 boards; the parts are bought into the personal library before the order (owner, 2026-09-25) |
| S8 | May the run use six tasks, 101 agents in all, plus the follow-up tasks P6 and refutations call for and the P5 and P6 runs after them? Each task stays at 32 agents or fewer (owner, 2026-09-24). | yes | yes: six tasks, 101 agents (owner, 2026-09-25); 102 with the P1 re-check agent in T1 (owner, 2026-09-27) |
| S9 | Which makers are allowed for the 18650 cells of the 2S pack (R7)? S2 allows Murata and Panasonic; no other cell maker is on either list. | owner to state | Samsung SDI, LG Energy Solution, Molicel and EVE, beside Murata and Panasonic, which S2 allows (owner, 2026-09-27) |

### Specification

#### Blocking: answered before the research tasks

Each question blocks the categories and tasks in its Blocks column, not the
whole run.

| ID | Question | Blocks | Proposed answer | Answer (owner, date) |
| --- | --- | --- | --- | --- |
| F1 | One INA238 switched between the onboard shunt and the external-shunt input, or one INA238 for each? | R8 (T3) | one for each: no switch sits in a sense path, and the firmware reads whichever is wired. The two positions differ by I²C address | one INA238 for each path (owner, 2026-09-25) |
| F2 | Is 150 A on the onboard shunt continuous or a peak, and for how long? The connector for it is a round 2 question. | R3, R8 (T2, T3) | owner to state. At 150 A a 100 µΩ shunt drops 15 mV and dissipates 2.25 W, and a 200 µΩ shunt drops 30 mV and dissipates 4.5 W. Both drops are inside the INA238's ±40.96 mV range ([Power](Power.md)) | 150 A continuous (owner, 2026-09-25) |
| F3 | How many load-cell channels (thrust, torque on one or two cells), and what excitation voltage? | R13 (T4) | owner to state | 3 channels: thrust, and torque on 2 cells; 5 V excitation (owner, 2026-09-25) |
| F4 | The encoder: single-ended at 3.3 V or 5 V, or differential RS-422 (Recommended Standard 422)? Its supply voltage, the highest count rate, and what it measures (servo output shaft, motor shaft)? | R11 (T4); R5, the 12 V supply (T3) | owner to state | both, selected by a jumper: 5 V single-ended up to 1 MHz, and RS-422 differential up to 5 MHz. It measures either the servo output shaft or the motor shaft, wherever the operator mounts it. Its supply is 5 V or 12 V, selected by a jumper; 12 V needs a source while the board runs from the 2S pack (R5) (owner, 2026-09-25) |
| F5 | Motor temperature: thermocouple, NTC (negative temperature coefficient thermistor) or infrared, and how many channels? | R12 (T4) | owner to state | 2 channels: 1 infrared and 1 thermocouple (owner, 2026-09-25) |
| F6 | The display's supply on the link cable: which voltage? Its current is measured first (prerequisite 7). | R5 (T3) | 5 V | 5 V (owner, 2026-09-25) |
| F7 | The switch on the ESC pack. The monostable specification requires one and leaves its technology open. On the onboard 150 A path: a MOSFET array with a driver IC, or a normally-open relay or contactor that opens within 5 ms of the enable falling? On the external 300 A path: a contactor or MOSFET module driven from the IO board, or the signal gate alone? | R3 (T2) | onboard: a MOSFET array and its driver on the IO board. External: a driven external module, whose driver output is on the IO board | onboard: a MOSFET array and its driver on the IO board. External: a driven external module, whose driver output is on the IO board (owner, 2026-09-25) |
| F8 | Which source charges the 2S pack (the 12 to 20 V DC input, USB-C, or both), and at what current? 2 A is the BQ25887's charge current for a USB input ([Power](Power.md)); the tree states no requirement. | R7 (T3) | the DC input at 2 A; USB-C not required | USB-C only, drawing up to 3 A at 5 V (15 W); the pack does not charge from the DC input (owner, 2026-09-25) |
| F9 | The 2S pack's cell chemistry and its discharge floor. A pack-voltage floor does not bound one cell: a pack at 6.6 V can hold cells at 3.0 V and 3.6 V. A per-cell floor reads the pack's balance lead. | R5, R6, R7 (T3) | chemistry: owner to state. Floor: 3.3 V a cell (6.6 V for the pack), a candidate for 4.2 V lithium-ion cells with no source in the tree. Reconnection only when the DC input or the charger is present | lithium-ion (18650), floor 3.0 V a cell, read per cell from the pack's balance lead (owner, 2026-09-25) |
| F10 | The monostable's long timing-element failures: the single-channel OR design, whose fault model excludes an R or C risen in value, or the two-channel AND design, which adds two both-edge detectors and an AND gate? | R3 (T2) | owner to state | the single-channel OR design (owner, 2026-09-25) |
| F11 | A hardware latch set by the monostable's first expiry and cleared only by an operator action? It adds a clear input and a control to R3. | R3 (T2) | owner to state | yes: a hardware latch with an operator clear control (owner, 2026-09-25) |
| F12 | The monostable's clear network: the RC (resistor-capacitor) network, which pull request #167 qualifies for a fast power-up ramp only, with no ramp time stated, or a reset supervisor with a stated threshold and delay? | R3 (T2) | a reset supervisor | a reset supervisor (owner, 2026-09-25) |
| F13 | Which IO board pins pass through the gated buffer? | R4 (T2) | every pin that reaches an ESC or a servo: the 8 output sockets and the programming connector | the 20 output ports and the UART socket; the programming connector is replaced by the 4 multiprotocol ports (owner, 2026-09-25) |
| F14 | The bias of a bidirectional DShot line through the gated buffer: selected per protocol (a pull-up for bidirectional DShot, a pull-down otherwise), or an open-drain stage with a pull-up that idles high for every protocol? | R4 (T2) | owner to state | selected per protocol: a pull-up for bidirectional DShot, a pull-down otherwise (owner, 2026-09-25) |
| F15 | The highest ESC pack voltage the bench takes. `SET_PACK_CELLS` takes 1 to 14 cells; at 4.2 V a cell that is 58.8 V. | R3, R8, R9, R11 (T2, T3, T4) | owner to state | 16 cells, 67.2 V at 4.2 V a cell (owner, 2026-09-25) |
| F16 | A per-socket voltage ceiling set in hardware: is it required, at which value, and does it follow the rail setting (5.5 V or 8.4 V) or the socket? No source in the tree gives it. | R6 (T3) | owner to state | yes: a jumper on each of the 20 ports selects an LV or HV hardware ceiling, cutting the port off above 6.0 V or 8.7 V (owner, 2026-09-25) |
| Q7 | How many external I²C ports, at which voltage? | R12 (T4) | owner to state | 2 ports, 3.3 V, Qwiic (JST-SH 4-pin) (owner, 2026-09-25) |

#### Raised by P1

After T1, and after a follow-up task that raises a question, the session writes into this table each question P1's critic
confirmed and each question a critic added that the P1 re-check confirmed,
with IDs V1, V2 and so on, and the category each holds. It commits the page on
the branch of prerequisite 4. The owner answers each with a value, or marks it
"for research". P2 finds a value marked for research at its primary source and
records it as found, not given. P4's datasheet verifier re-reads it. A
question whose value only feeds a decision under
[Decided on the research's output](#decided-on-the-researchs-output) holds
neither P2 nor the start of its task.

| ID | Category and function | Question | Answer (owner, date) |
| --- | --- | --- | --- |
| V1 | R1, Microcontroller | What is the stop deadline, in ms: the longest time from the coprocessor ceasing to feed the RP2350 watchdog (frozen while the panel keeps beating) to every output and power gate the coprocessor drives being at its reset state? | for research (owner, 2026-09-28) |
| V2 | R1, Microcontroller | What is the longest legitimate stall of the coprocessor's main loop, in ms, that the watchdog timeout must exceed? Is it the 400 ms packaged-part maximum of one 4 kB sector erase alone, or longer, to cover a USB console write that blocks while the host does not read (up to 500 ms a write at pico-sdk 2.3.1's default) and a pass that holds both? | for research (owner, 2026-09-28) |
| V3 | R1, 12 MHz crystal | What clock error between an ESC and the bench, in percent, must the bidirectional DShot decoder accept? The tree states 'a percent or two' with no source, and the decoder is host-tested at ±5 % (4.75 and 5.25 samples a bit). | ±2 % (owner, 2026-09-28) |
| V4 | R1, Microcontroller | What watchdog timeout, in ms, does the coprocessor image set? | for research (owner, 2026-09-28) |
| V5 | R1, 12 MHz crystal | What total frequency error, in ppm, may the RP2354B's 12 MHz crystal have: tolerance at 25 °C plus stability over the operating temperature range, plus first-year aging if counted? | for research (owner, 2026-09-28) |
| V6 | R1, 12 MHz crystal | What load capacitance in pF, highest ESR in Ohm and lowest drive-level rating in uW must the 12 MHz crystal meet? | for research (owner, 2026-09-28) |
| V7 | R1, 12 MHz crystal | Over what ambient temperature range, in °C, must the R1 parts be rated and the crystal stay within its tolerance: the 0 to 50 °C over which pull request #167 specifies the monostable window, or another range? | 0 to 50 °C ambient, one range for the whole IO board (owner, 2026-09-28) |
| V8 | R1, Core regulator inductor | What inductance in uH (with its tolerance in %), lowest saturation current in A and highest DC resistance in mOhm must the RP2354B's core regulator inductor meet? | 3.3 µH ±20 %; saturation current at least 1.5 A; DC resistance at most 250 mΩ (owner, 2026-09-28) |
| V9 | R1, USB protection | What ESD immunity must the USB protection give the USB port's lines, as IEC 61000-4-2 contact and air discharge voltages in kV? | IEC 61000-4-2 level 4: ±8 kV contact, ±15 kV air, on every line the protection covers (owner, 2026-09-28) |
| V10 | R1, USB protection | What highest VBUS voltage in V and current in A must the protection on the RP2354B's USB port carry? Is it a data port whose VBUS is only sensed, or also the USB-C charger input of F8 at up to 3 A and 5 V on the same connector, with its CC1 and CC2 pins? | 5.5 V, 3 A: one USB-C connector for the charger input of F8 and the RP2354B's data lines, with CC1 and CC2 (owner, 2026-09-28) |
| V11 | R1, USB protection | What highest capacitance per line, in pF, may the USB protection add on D+ and D-? | for research (owner, 2026-09-28) |
| V12 | R1, USB protection | What highest voltage, in V, may the USB protection let reach USB_DP and USB_DM (QFN-80 pins 67 and 66)? Give the clamping voltage at the ESD level asked in the ESD question, and the highest steady voltage those pins may see. | Steady: 3.63 V. ESD: clamping voltage at most 36 V at 16 A TLP (IEC 61000-4-2 ±8 kV contact at 30 ns), ahead of the 27 Ω series resistor (owner, 2026-09-28) |
| V13 | R1, USB protection | Which connector type does the RP2354B's USB port use: USB Type-C or another type the owner names? How many lines must the protection cover: VBUS, D+ and D-, plus CC1 and CC2 on USB Type-C? | USB Type-C shared with the F8 charger input: 5 lines, VBUS rated 5.5 V at 3 A (owner, 2026-09-28) |
| V14 | R1, Core regulator inductor | Must the core regulator inductor be fully shielded and marked for polarity (yes or no for each), as section 6.3.8.2 of the RP2350 datasheet requires? | yes: fully shielded, and marked for polarity (owner, 2026-09-28) |
| V15 | R1, 12 MHz crystal | What highest shunt capacitance C0, in pF, and which cut and mode (fundamental AT-cut or another) must the 12 MHz crystal meet? | C0 at most 3.0 pF; fundamental-mode AT-cut (owner, 2026-09-28) |
| V16 | R2, Link CAN controller | At most how many CAN frames does the panel send to the IO board during one flash stall of the RP2354B, within 400 ms (the W25Q16JV packaged sector-erase maximum) and within 3 ms (the packaged page-program maximum), in frames? From the tree: one request is outstanding at a time (shared/link/include/link_host.h:5-7), with a 1000 ms timeout (link_host.h:29). A read request is 1 frame and a 32-register write is 8 frames (shared/link/include/link_can.h:69-72). Polls come every 50 ms in runs of back-to-back transactions (firmware/iomcu/src/out_store.h:103-104). No count per stall is stated. (feeds Q8 only) | for research (owner, 2026-09-28) |
| V17 | R2, External CAN controller | At most how many CAN frames arrive at the IO board on the external CAN port during one flash stall, within 400 ms and within 3 ms, in frames? (feeds Q8 only) | for research (owner, 2026-09-28) |
| V18 | R2, External CAN transceiver | Is the external CAN port's transceiver held in standby by the enable node while the enable node is low (yes or no)? The R2 row, IOBoard.md:57 and IOBoard.md:238 state it with no source. The owner decision of 2026-09-25 names the port and its own controller only. F13 puts only the 20 output ports and the UART socket behind the gate. | for research (owner, 2026-09-28) |
| V19 | R2, External CAN transceiver | Within what time after the enable node falls must the external CAN transceiver be in standby, in ms? The pages give the output buffer under 1 µs and a power switch 5 ms (IOBoard.md:187-188, Monostable.md:112-113 at 23c82ca). They give no time for the transceiver. | for research (owner, 2026-09-28) |
| V20 | R2, External CAN controller | Does the external CAN port carry CAN FD frames, or classic CAN only (CAN FD or classic)? | for research (owner, 2026-09-28) |
| V21 | R2, External CAN transceiver | What is the highest bit rate on the external CAN port, in kbit/s: the nominal rate, and for CAN FD the data-phase rate? | for research (owner, 2026-09-28) |
| V22 | R2, External CAN transceiver | What is the longest total bus length on the external CAN port, in m? | for research (owner, 2026-09-28) |
| V23 | R2, External CAN transceiver | Is the external CAN port galvanically isolated from the IO board's ground, and for what working voltage, in V (0 V if not isolated)? | for research (owner, 2026-09-28) |
| V24 | R2, External CAN transceiver | What DC voltage must the external CAN transceiver's CANH and CANL pins withstand without damage (bus-fault rating), in V, apart from the round 2 connector protection? The highest voltage on the bench is the ESC pack's 67.2 V (F15). | for research (owner, 2026-09-28) |
| V25 | R2, Link CAN transceiver | What DC voltage must the link CAN transceiver's CANH and CANL pins withstand without damage (bus-fault rating), in V? The link cable carries the display's 5 V supply beside CANH and CANL (F6, IOBoard.md:177). | for research (owner, 2026-09-28) |
| V26 | R2, Link CAN controller | What is the largest frequency error the link CAN controller's clock may have, including temperature and ageing, in ppm? The tree requires the bit rate to come out exact (shared/can/include/can_timing.h:17-21) and says two nodes that disagree by 1 % fail on long frames. It states no limit. | for research (owner, 2026-09-28) |
| V27 | R2, External CAN controller | What is the largest frequency error the external CAN controller's clock may have, including temperature and ageing, in ppm? | for research (owner, 2026-09-28) |
| V28 | R2, Link CAN controller | The IO board also powers the display through the link cable. Within what time after power is applied to the IO board must its link CAN controller acknowledge frames on the link, in ms? | for research (owner, 2026-09-28) |
| V29 | R3, latch | Does the hardware latch of F11 power up set, holding the gates disabled until the operator clears it, or clear, letting the gates follow the enable node once edges arrive? STATUS.md:340 holds that every stop latches and nothing re-arms on its own. A latch that powers up set also holds the gates disabled through a clear-release pulse of a '123-type monostable (Q9). | for research (owner, 2026-09-28) |
| V30 | R3, latch | Where does the latch of F11 sit: between the enable node and the gates, so the enable node keeps the monostable's own timing and test steps 7 and 10 of pull request #167 still read it, or on the enable node itself, so the node stays low after the first expiry? The gates are the output buffer's enable, the servo rail gate, both ESC pack switches and the external CAN transceiver's standby. | for research (owner, 2026-09-28) |
| V31 | R3, latch clear control | What is the operator clear control of the latch (F11), and where does it sit: a push button on the IO board, a control on the display panel, or a lead to a remote button? F11's answer names no part and no connector. | for research (owner, 2026-09-28) |
| V32 | R3, latch clear control | May a relay contact, like the bench's relays across BOOT and RESET, or firmware on either board drive the latch's clear? Pull request #167 puts no processor, register or firmware in the path from GPIO6 to either gate. A clear driven that way re-enables the gates with no operator at the bench. Answer for a relay and for firmware. | for research (owner, 2026-09-28) |
| V33 | R3, servo rail gate | Is the one high-side switch on the servo rail the servo rail's gate, and are the 20 port supply switches, which the I/O expander drives, not a gate? The Not defeatable rule of pull request #167 keeps a gate off the I/O expander. | for research (owner, 2026-09-28) |
| V34 | R3, onboard pack switch MOSFETs | What voltage margin, in V, above the 67.2 V of a 16-cell pack at 4.2 V a cell (F15) are the ESC pack switches rated for, on the onboard and the external path? An 80 V part leaves 12.8 V and a 100 V part 32.8 V. The INA238 on the pack node is rated to 85 V at most. | for research (owner, 2026-09-28) |
| V35 | R3, external pack switch module | What is the highest current, in A, on the external ESC pack path? The tree gives above 150 A to 300 A and more (owner, 2026-09-25). | for research (owner, 2026-09-28) |
| V36 | R3, monostable | What drift of the monostable's timing constant k, in %, over 3.3 V ±3 % and 0 to 50 °C does the window budget take? Pull request #167 assumes ±3 % (k = 0.45 at 3.3 V and 25 °C), and the window's margin of 2.7 ms each way rests on it. Give a figure, or mark it for research: read from the candidate's datasheet, where parts of this class graph k and state no limit. | for research (owner, 2026-09-28) |
| V37 | R3, monostable | What timing-pin leakage, in µA at 50 °C, does the window budget take? Pull request #167 assumes 0.5 µA, against the class's 1 µA limit at 85 °C, and books ±2 % of window drift for it. Give a figure, or mark it for research: the candidate's 25 °C figure, doubled every 10 °C. | for research (owner, 2026-09-28) |
| V38 | R3, monostable | What ambient temperature range, in °C, do the safety gate and the ESC pack switches work across? Pull request #167 assumes 0 to 50 °C for the bench, analyses the window only there, and leaves behaviour outside it open. | for research (owner, 2026-09-28) |
| V39 | R3, servo rail gate | What off-state leakage, in mA, may the open servo rail gate pass? Pull request #167 sets 5 mA per rail as a placeholder until a load's idle current is measured; a small servo idles at 5 to 10 mA, not measured. | for research (owner, 2026-09-28) |
| V40 | R3, onboard pack switch MOSFETs | What off-state leakage, in mA, may an open ESC pack switch pass, on the onboard and the external path, any precharge path around it included? The specification applies the servo rail's 5 mA placeholder. The ESC in pull request #167's example idles at 50 mA. | 5 mA on both paths, precharge path included; the precharge path opens with the switch (owner, 2026-09-28) |
| V41 | R3, pack overvoltage and transient clamp | What highest sustained input voltage, in V, does the ESC pack path's overvoltage protection survive while it keeps the pack node under 85 V, for example a pack of more than 16 cells connected by mistake? Answer none if only switching transients are clamped. Only the research plan states this protection; no owner answer does. | for research (owner, 2026-09-28) |
| V42 | R3, pack precharge | What largest ESC input capacitance, in µF, does the ESC pack switch precharge before it closes fully? Answer none if no precharge is required. Pull request #167 gives one example ESC with 470 µF. Only the research plan states the precharge; no owner answer does. | 4700 µF (owner, 2026-09-28) |
| V43 | R3, reset supervisor | What shortest delay, in ms, does the reset supervisor hold the clear after the 3.3 V rail passes its threshold? The 3.3 V ramp time is not stated, and R5 selects the regulator after T2. | for research (owner, 2026-09-28) |
| V44 | R3, servo rail gate | What highest voltage, in V, is the servo rail gate rated for? The rail is set to 8.4 V at most, and the TPS55285 can be programmed to 15 V over I²C. | for research (owner, 2026-09-28) |
| V45 | R3, pack overvoltage and transient clamp | What inductance, in µH, do the pack leads to the IO board and to the external module have, for the transient the clamp absorbs when a switch opens at full current (energy = 0.5 x L x I², at 150 A onboard)? A TVS diode rated 70 V working (Vishay SMBJ70A) breaks down at 77.8 to 86.0 V and clamps at 113 V at 5.3 A. | 1 µH on each path: 11 mJ at 150 A onboard, 45 mJ at 300 A external (owner, 2026-09-28) |
| V46 | R3, pack precharge | What longest time, in ms, may an ESC pack switch take to close, precharge included, from the enable rising or the latch clearing? | 500 ms (owner, 2026-09-28) |
| V47 | R3, onboard pack switch driver | Does the closed ESC pack switch carry current from the ESC back into the pack, as in regenerative braking, and up to what current in A? A reverse-polarity stage built as an ideal diode blocks it. The BENCH current register sends a negative current as 0 A. | for research (owner, 2026-09-28) |
| V48 | R3, onboard pack switch MOSFETs | What highest current, in A, does each ESC pack switch open without damage: the motor overcurrent trip level plus its rise during detection, or a short-circuit figure? The switch opens on the enable falling and on the coprocessor's overcurrent action. | 300 A onboard; 600 A external at a 300 A path (owner, 2026-09-28) |
| V49 | R3, onboard pack switch MOSFETs | What highest dissipation, in W, may the onboard ESC pack switch have at 150 A continuous? It sets how many MOSFETs share the current. The onboard shunt beside it dissipates 2.25 W at 100 µΩ and 4.5 W at 200 µΩ. | 7 W for the switch array at 150 A, RDS(on) taken at TJ 100 °C; a reverse-polarity stage in series has its own budget (owner, 2026-09-28) |
| V50 | R3, external pack switch module | Is the external path's highest current continuous, or a peak, and if a peak, for how long in s? | Peak: 300 A for 30 s at most (owner, 2026-09-28) |
| V51 | R3, onboard pack switch driver | What lowest ESC pack voltage, in V, does each ESC pack switch close and conduct at? SET_PACK_CELLS takes 1 cell (shared/settings/settings.c:39-41), and no per-cell floor is stated for the ESC pack. | for research (owner, 2026-09-28) |
| V52 | R3, latch | Does the latch of F11 set on the enable node's low level, or on its falling edge? With a level set, a clear given while the enable is low leaves the gates disabled, and so does a clear control held or stuck closed. With an edge set, a clear given while the panel is stopped lets the gates return when the edges resume, with no operator action at that moment. Answer level or falling edge, and say which input wins when set and clear are both active. | for research (owner, 2026-09-28) |
| V53 | R3, onboard pack switch driver | How does the coprocessor's overcurrent and over-temperature action open the ESC pack switches? The options are an RP2354B output on a second, disable-only input of each switch driver, or no switch action at all: the throttle stops, and the switches follow the enable node alone. IOBoard.md:475 and the R8 row time the path 'from detection to the ESC pack switch open'. The R3 row says R3's gates follow the enable node alone. IOBoard.md:197 puts no processor in the path from GPIO6 to a gate. | for research (owner, 2026-09-28) |
| V54 | R3, pack overvoltage and transient clamp | What inductance, in µH, do the leads between each ESC pack switch and the ESC have? When a high-side switch opens at full current, that inductance keeps the current flowing into the ESC's input capacitance. That drives the switched node negative until a clamp conducts. The energy is 0.5 x L x I²: 11 mJ at 1 µH and 150 A. The INA238 takes -0.3 V at least on VBUS, IN+ and IN- (SLYS025B section 5.1, https://www.ti.com/lit/ds/symlink/ina238.pdf, read 2026-09-28T10:40:16Z). | 0.5 µH on each path: 5.6 mJ at 150 A, 22.5 mJ at 300 A (owner, 2026-09-28) |
| V55 | R3, servo rail gate | What lowest servo rail voltage, in V, does the servo rail gate close and conduct at? The rail has two settings, up to 5.5 V and up to 8.4 V, set from software (hardware/STATUS.md:37). The TPS55285 programs 0.8 V to 15 V (SLVSI72A, https://www.ti.com/lit/ds/symlink/tps55285.pdf, read 2026-09-28T10:40:16Z). | for research (owner, 2026-09-28) |
| V56 | R4, bias selector | What selects the pull-up or pull-down of F14 on each of the 4 multiprotocol ports: a jumper per port, an RP2354B GPIO, an output of the I/O expander that drives the port switches, or a circuit with no control input that follows the port's signal? | for research (owner, 2026-09-28) |
| V57 | R4, bias selector | Does a bias selector driven by RP2354B firmware or by the I/O expander meet the rule that no processor, register or firmware sits in the path from GPIO6 to a gate (Monostable.md:449-455 at 23c82ca): yes or no? The selector is not in the enable path, but it sets the connector line's level while the gate is disabled. | Yes, if the selector's reset and unpowered state selects the pull-down (owner, 2026-09-28) |
| V58 | R4, bias selector | Which bias does each of these take, both with the gate disabled and with the pin released: the UART socket (OpenYGE, UART 8N1, idles high), and a multiprotocol port in ESC bootloader, ESC telemetry and servo configuration modes, whose lines idle high? For each, the choices are a pull-up (value in kΩ, to which rail in V), the 10 kΩ pull-down that F14's 'otherwise' reads as, or no bias of the board's own. | for research (owner, 2026-09-28) |
| V59 | R4, multiprotocol port path | At what baud rate does the ESCape32 text CLI run on a multiprotocol port? The tree's only figure is 19,200 baud, in the proposed analyser map (testbench/README.md:396). ESCape32's wiki (https://github.com/neoxic/ESCape32/wiki/Configuration, read 2026-09-28T10:29:53Z) gives 38,400 baud 8N1 on the signal line. | for research (owner, 2026-09-28) |
| V60 | R4, multiprotocol port path | What signal levels does the Hitec D-series protocol use on the servo's signal wire: the high level the servo drives, and the input-high threshold it needs, in V? | for research (owner, 2026-09-28) |
| V61 | R4, multiprotocol port path | What is the Hitec D-series protocol's electrical form on the port: one signal wire at half duplex or separate lines, UART normal or inverted, and the servo's output stage (push-pull, or open drain with a pull-up at its own supply)? | for research (owner, 2026-09-28) |
| V62 | R4, multiprotocol port path | At what baud rate does the Hitec D-series protocol run, in baud? | for research (owner, 2026-09-28) |
| V63 | R4, level translation | What high level must each port's signal line drive at the connector, in V: 3.3 V, 5 V, or the level of a supply sensed from the device, and then which rail? It applies to the 16 PWM ports, the 4 multiprotocol ports and the UART socket. | for research (owner, 2026-09-28) |
| V64 | R4, bias selector | What is the value of the pull-up F14 puts on a line that carries bidirectional DShot, in kΩ, and to which rail, in V? | for research (owner, 2026-09-28) |
| V65 | R4, gated output buffer | What is the highest DShot bit rate the gated output path must pass, in kbit/s: 600, the bench's bind list (shared/outputs/out_bind.c:566-569), or 1200, the driver's range (shared/outputs/outputs.c:29)? | 1200 kbit/s (owner, 2026-09-28) |
| V66 | R4, gated output buffer | What load must each output drive at that bit rate: the longest lead from the connector to the servo or ESC, in m, or the load capacitance, in pF? | 0.3 m lead; 50 pF at the connector (owner, 2026-09-28) |
| V67 | R4, multiprotocol port path | Which wire does VESC's 115,200-baud framed-packet link use on the IO board, and how many signal lines does it take: one half-duplex line on a multiprotocol port, the UART socket, or separate transmit and receive lines (2)? | VESC's UART port: separate transmit and receive lines (2), 115,200 baud 8N1 (owner, 2026-09-28) |
| V68 | R4, receiver inputs | How many receiver inputs does the IO board bring out (a count)? | for research (owner, 2026-09-28) |
| V69 | R4, receiver inputs | What is the highest voltage a receiver drives on its signal wire into the IO board, in V? | for research (owner, 2026-09-28) |
| V70 | R4, receiver inputs | What is the highest baud rate of any receiver bus on a receiver input, in baud? The buses are iBUS, SUMD, CRSF, SRXL2 and JETI EX Bus; S.BUS is 100 kbaud. | for research (owner, 2026-09-28) |
| V71 | R4, receiver inputs | Does the bench transmit on a receiver input, for JETI EX Bus telemetry (docs/Manifest.md:37-39) or SRXL2: yes or no, for each bus? | JETI EX Bus: yes. SRXL2: yes (owner, 2026-09-28) |
| V72 | R4, receiver inputs | Does a receiver input pass the output gate of F13: yes or no? F13's answer names only the 20 output ports and the UART socket. | for research (owner, 2026-09-28) |
| V73 | R4, reset idle level | The gate stays enabled while the RP2354B is held in reset or restarts through the watchdog, because the enable follows the monostable (owner, 2026-09-25). During that time, which level must these lines hold at the connector: a multiprotocol port bound to a mode whose line idles high (bidirectional DShot, ESC telemetry, servo configuration, ESC bootloader), and the UART socket? Is it that mode's idle, high, or low? | for research (owner, 2026-09-28) |
| V74 | R4, gated output buffer | Must the connector side of a bidirectional line also carry I_off or a series resistor of 4.7 kΩ or more: yes or no? The lines are the 4 multiprotocol ports, the UART socket and a receiver input. The case is a device on its own supply, such as an ESC's BEC or a receiver on another supply, driving the line while the IO board is unpowered. | for research (owner, 2026-09-28) |
| V75 | R4, series resistance and clamps | Which faults on a connector signal line must the buffer survive, and for how long, in s or continuous: a short to ground, and a short to the port's supply pin at up to 8.7 V (the HV ceiling of F16)? | for research (owner, 2026-09-28) |
| V76 | R4, UART socket path | Besides OpenYGE ESCs, which serial devices does the UART socket carry, and what is the highest baud rate among them, in baud? | for research (owner, 2026-09-28) |
| V77 | R4, multiprotocol port path | Which protocol does a multiprotocol port carry in its ESC telemetry mode: the ESC's separate serial telemetry wire (ESC to bench only), OpenYGE (half duplex), or another? In which direction, and at what highest baud rate, in baud? | for research (owner, 2026-09-28) |
| V78 | R4, programming supply switching | When an ESC or a servo has to start up with its signal line held high to enter its bootloader or CLI, what powers it up? The choices: the operator by hand; a STOP and the operator clear of the F11 latch, since the ESC pack switch follows the enable node alone; the port's supply switch under firmware, for a servo; or a separate switch, giving its voltage in V and current in A. | for research (owner, 2026-09-28) |
| V79 | R5, display supply | What is the lowest voltage, in V, the display takes at its end of the link cable while it draws 2 A? | for research (owner, 2026-09-28) |
| V80 | R5, display supply | Does the display's own power path isolate the VBUS of each of its USB-C sockets from the 5 V it takes from the link cable: yes or no? The sockets are the native USB socket and the USB-UART bridge's socket (docs/Link.md:19-23). If no, what joins them: a diode, a switch or a direct connection? | for research (owner, 2026-09-28) |
| V81 | R5, display supply | What is the highest voltage, in V, the display takes at its end of the link cable at no load? | for research (owner, 2026-09-28) |
| V82 | R5, display supply | At which current, in A, does the link cable's supply limit a short on the cable so that the IO board's own rails stay up? Answer 'none' if it has no limit. | for research (owner, 2026-09-28) |
| V83 | R5, overvoltage protection | What is the highest continuous voltage, in V, the DC input survives without damage? Examples: a 24 V supply, or a 6S pack at 25.2 V, connected by mistake. | for research (owner, 2026-09-28) |
| V84 | R5, overvoltage protection | At which DC input voltage, in V, does the overvoltage protection cut the input off? It lies above the 20 V maximum and at or below the TPS55285's 22 V input rating. Does 20 V include the DC source's own tolerance? | for research (owner, 2026-09-28) |
| V85 | R5, overvoltage protection | What transient does the DC input's protection absorb: its peak in V and its duration in µs? An example is the ringing of a lead hot-plugged onto ceramic input capacitors. | for research (owner, 2026-09-28) |
| V86 | R5, reverse-polarity protection | Do the reverse-polarity, overvoltage and inrush protection sit on the 2S pack input as well as on the 12 to 20 V DC input: yes or no? | for research (owner, 2026-09-28) |
| V87 | R5, inrush limiting | What is the highest inrush current, in A, the board may draw when the DC input or the 2S pack is connected? | for research (owner, 2026-09-28) |
| V88 | R5, source selection | Below which DC input voltage, in V, does the board treat the DC input as absent and run from the pack? Is 12 V the lowest the DC source delivers including its own tolerance? | for research (owner, 2026-09-28) |
| V89 | R5, source selection | What is the longest interruption, in ms, the 3.3 V rail and the 5 V rail with the display may see when the DC input is connected or removed while the board runs? Answer 0 for none. | for research (owner, 2026-09-28) |
| V90 | R5, source selection | Which pack path current, in A, does R5 size the source selection and the pack disconnect for? Option 1: 9.9 A for the servo supply at 6.0 V, on the 90 % efficiency Power.md assumes, plus the display and the logic rails. Option 2: the TPS55285's input current from its datasheet efficiency at 6.0 V in and 8.4 V, 6.35 A out ('for research'). | for research (owner, 2026-09-28) |
| V91 | R5, source selection | Does the RP2354B's USB port power the board's logic when neither the DC input nor the pack is present: yes or no? If yes, which rails: the 3.3 V rail only, or also the 5 V rail and the display? | for research (owner, 2026-09-28) |
| V92 | R5, pack disconnect | What is the highest 2S pack voltage, in V, that R5 designs the pack path for: 8.4 V (4.2 V a cell), or the charge voltage of the cells R7 selects? | for research (owner, 2026-09-28) |
| V93 | R5, pack disconnect | What reconnects the pack after a floor disconnect: the USB-C charger present, the DC input present, every cell above a stated voltage, or an operator action? Name one or more. | for research (owner, 2026-09-28) |
| V94 | R5, pack disconnect | If the pack reconnects on cell voltage, at which cell voltage, in V, does it reconnect? This is the hysteresis above the 3.0 V floor that keeps the returning load from reconnecting it. | for research (owner, 2026-09-28) |
| V95 | R5, pack disconnect | Is 3.0 V the lowest cell voltage at which the disconnect trips at every tolerance corner, or its nominal trip point? What tolerance, in mV, does the trip point hold over 0 to 50 °C? | for research (owner, 2026-09-28) |
| V96 | R5, pack disconnect | How long, in ms, must a cell stay at or below the floor before the pack disconnects? This is the filter that keeps a cell's sag under a load step from tripping it. | for research (owner, 2026-09-28) |
| V97 | R5, pack disconnect | What is the highest current, in µA, the board may draw from the pack while the pack is disconnected? | for research (owner, 2026-09-28) |
| V98 | R5, pack disconnect | Which of these may the pack's floor disconnect be: (a) a part of its own, with the 3.0 V threshold fixed by resistors or factory trim; (b) the pack protection R7 selects; (c) a protector whose threshold firmware writes over I²C at start-up? | for research (owner, 2026-09-28) |
| V99 | R5, 3.3 V logic buck | What current margin, in %, do the 3.3 V logic buck and the 5 V rail each hold above their load? The load is summed from the parts T2 and T4 select and the display. The margin covers the parts R6, R7 and R8 select in the same task. | for research (owner, 2026-09-28) |
| V100 | R5, 5 V rail | Which tolerance, in %, does the 5 V rail hold at the IO board's own 5 V loads: the load-cell excitation (F3) and the encoder's 5 V supply (F4)? | for research (owner, 2026-09-28) |
| V101 | R5, 5 V rail | What is the lowest bridge resistance, in Ω, of the load cells on the 3 channels (F3)? | for research (owner, 2026-09-28) |
| V102 | R5, encoder 12 V supply | What is the highest supply current, in mA, of the encoder at 12 V and at 5 V (F4)? | for research (owner, 2026-09-28) |
| V103 | R5, encoder 12 V supply | Which voltage range, in V, does the encoder's 12 V supply hold at the encoder's connector? | for research (owner, 2026-09-28) |
| V104 | R5, receiver supply | Does the IO board power the receiver: yes or no? If yes, from which rail, at which voltage in V, and at which highest current in mA? | for research (owner, 2026-09-28) |
| V105 | R5, reverse-polarity protection | Which DC input current, in A, does R5 size the DC input's protection and the source selection for? Option 1: 4.9 A for the servo supply at 12 V, on the 90 % efficiency Power.md:68 assumes, plus the display, the encoder's 12 V supply and the logic rails. Option 2: the TPS55285's input current from its datasheet efficiency at 12 V in and 8.4 V, 6.35 A out ('for research'), plus the same loads. | for research (owner, 2026-09-28) |
| V106 | R5, 3.3 V logic buck | In which order do the IO board's 5 V and 3.3 V rails come up at power-on and fall at power-off: 5 V first, 3.3 V first, or either order? | for research (owner, 2026-09-28) |
| V107 | R5, pack disconnect | With the 2S pack's power leads connected and its balance lead unplugged, or one balance wire open, does the disconnect hold the pack off: yes or no? | for research (owner, 2026-09-28) |
| V108 | R5, encoder 12 V supply | At which current, in mA, is the encoder's supply output (5 V or 12 V, F4) limited, so that a short on the encoder cable leaves the 5 V rail and the display's supply in regulation? Answer 'none' if it has no limit. | for research (owner, 2026-09-28) |
| V109 | R6, Servo supply | What output current must the servo rail deliver at the 5.5 V setting and at the 8.4 V setting, in A? What current limit applies at each setting, in A? | for research (owner, 2026-09-28) |
| V110 | R6, Servo supply | What current must the servo rail deliver at the 8.4 V setting while the converter runs from the pack, with 6.0 to 8.4 V at its input, in A? | for research (owner, 2026-09-28) |
| V111 | R6, Servo supply | What efficiency do the servo converter's pack figures use from 6.0 to 8.4 V in at 8.4 V out, in %: 90 %, or the figure R6 reads from the TPS55285 datasheet (for research)? | for research (owner, 2026-09-28) |
| V112 | R6, Servo supply | What is the highest voltage the DC input can present at the servo converter's VIN, in V, as a steady value and as a transient with its duration in ms? | for research (owner, 2026-09-28) |
| V113 | R6, Servo supply | What is the lowest voltage at the servo converter's VIN when it runs from the pack, in V? Is it 6.0 V, or 6.0 V less a stated drop across the pack path (R5's disconnect, source selection and protection) at the pack current? | for research (owner, 2026-09-28) |
| V114 | R6, Servo supply | What is the highest voltage allowed at a port's supply pin at each setting, in V? Are 5.5 V and 8.4 V upper limits that include setpoint tolerance, ripple and load-step overshoot, or nominal setpoints? If nominal, what tolerance applies, in V? | for research (owner, 2026-09-28) |
| V115 | R6, Servo supply | What drives the servo converter's EN/UVLO pin: an RP2354B GPIO, an I/O expander output, or a divider from its input with the output switched only over I2C? | for research (owner, 2026-09-28) |
| V116 | R6, Servo supply | What ambient temperature range must the IO board operate in, in C? | for research (owner, 2026-09-28) |
| V117 | R6, Servo supply inductor | At which switching frequency does the servo converter run: 400 kHz, 800 kHz, 1.6 MHz or 2.1 MHz? Or is it marked for research? | for research (owner, 2026-09-28) |
| V118 | R6, Servo supply inductor | What current must the inductor's saturation rating exceed, in A: the peak current at the highest operating point, or the TPS55285's cycle-by-cycle peak current limit (13 A typical)? | for research (owner, 2026-09-28) |
| V119 | R6, I/O expander | How many outputs must the I/O expander drive? Is it the 20 port supply switches only, or also the servo converter's enable and R4's bias selectors? | for research (owner, 2026-09-28) |
| V120 | R6, I/O expander | At what clock rate does the coprocessor's I2C bus that carries the I/O expander and the TPS55285 run, in kHz? | for research (owner, 2026-09-28) |
| V121 | R6, Port supply switch | Are the 20 port supply switches not the servo rail gate, which is then R3's single high-side switch on the rail: yes or no? | for research (owner, 2026-09-28) |
| V122 | R6, Port supply switch | What continuous current must each port's supply switch carry, in A? | for research (owner, 2026-09-28) |
| V123 | R6, Port supply switch | Must each port supply switch limit its own current? If so, at what value, in A? | for research (owner, 2026-09-28) |
| V124 | R6, Port supply switch | What voltage drop is allowed from the servo rail to a port's supply pin at the port's rated current, in mV? | for research (owner, 2026-09-28) |
| V125 | R6, Port supply switch | What off-state leakage may an open port supply switch pass at 8.4 V, in mA? | for research (owner, 2026-09-28) |
| V126 | R6, Port supply switch | Must a port supply switch block current flowing from the port's supply pin back into the servo rail? If so, up to what voltage on the pin, in V? | for research (owner, 2026-09-28) |
| V127 | R6, Port supply switch | What is the longest time allowed from an RP2354B reset (watchdog, RUN or power loss) until every port supply switch is open, in ms? | for research (owner, 2026-09-28) |
| V128 | R6, Port voltage ceiling | What trip band is allowed for each ceiling, in V? For the LV and HV positions, what is the lowest voltage at which each may cut a port off, and the highest at which each must? | for research (owner, 2026-09-28) |
| V129 | R6, Port voltage ceiling | Within what time must a port be cut off once its supply pin exceeds its ceiling, in microseconds? | for research (owner, 2026-09-28) |
| V130 | R6, Port voltage ceiling | After a ceiling cut-off, does the port reconnect by itself once the rail falls below the ceiling? If so, with what hysteresis, in V? Or does it stay off until an operator or firmware action? | for research (owner, 2026-09-28) |
| V131 | R6, Servo supply | What is the 2S pack's full-charge voltage, in V: 8.4 V (2 x 4.2 V), or the charge voltage of the cell and charger R7 selects? | for research (owner, 2026-09-28) |
| V132 | R6, Servo supply | What input current from the pack, in A, do the servo supply's inductor and the pack path take at the 6.0 V floor: 9.9 A (8.4 V x 6.35 A at an assumed 90 %), or the TPS55285's average inductor current limit, 7 A minimum and 8 A typical, with no maximum stated? | for research (owner, 2026-09-28) |
| V133 | R6, Port supply switch | What capacitance must a port supply switch charge at turn-on, in uF, and what peak inrush current may it draw from the servo rail while doing so, in A? | for research (owner, 2026-09-28) |
| V134 | R6, Port supply switch | What is the highest current the servo rail may deliver into a shorted port, in A: 6.35 A, with firmware keeping the TPS55285's output current limit enabled and its response at 128 us, or the average inductor current limit the converter falls back to otherwise, 7 A minimum and 8 A typical, with no maximum stated? | for research (owner, 2026-09-28) |
| V135 | R6, Port voltage ceiling | Must the RP2354B read, for each of the 20 ports, whether the ceiling has cut the port off and which ceiling its jumper selects? Yes or no for each. | for research (owner, 2026-09-28) |
| V136 | R6, Port voltage ceiling | What is the highest voltage the port supply switches and the ceiling parts must withstand at their input, in V: 15 V (the TPS55285's recommended maximum output), 17 V (its VOUT absolute maximum), 20 V (the DC input through a failed converter), or about 21.3 V (the highest target its registers can set)? | for research (owner, 2026-09-28) |
| V137 | R7, Pack cells | How many 18650 cells does the bench's 2S pack hold: 2 (2S1P, each cell carrying the whole pack current), or more in parallel, for example 4 (2S2P)? Answer with the number of cells. | for research (owner, 2026-09-28) |
| V138 | R7, Pack charger | What charge voltage per cell, in V, does the charger regulate the pack to: 4.2 V (8.4 V for the pack), or the charge voltage in the selected cell's datasheet? | for research (owner, 2026-09-28) |
| V139 | R7, Pack charger | What charger efficiency, in %, does R7 take from 5 V at 3 A into the pack between 6.0 V and 8.4 V? Or is it for research, from the selected charger's datasheet? | for research (owner, 2026-09-28) |
| V140 | R7, Pack cells | What efficiency, in %, does R7 take for the TPS55285 running from the pack at 6.0 V and delivering 8.4 V at 6.35 A? Or is it for research, from the TPS55285 datasheet? | for research (owner, 2026-09-28) |
| V141 | R7, Pack cells | What efficiency, in %, does R7 take for the 5 V rail that feeds the display's 7.5 W (5 V x 1.5 A continuous) from the pack at 6.0 V? Or is it for research? | for research (owner, 2026-09-28) |
| V142 | R7, Cell balancing | What balance current per cell, in mA, must the charger or a separate balancer provide? | for research (owner, 2026-09-28) |
| V143 | R7, Pack cells | What current, in A drawn from the pack at 6.0 V, does R7 budget for the loads other than the servo supply and the display? These are the 3.3 V logic rail, the load-cell excitation and 5 V encoder supply on the 5 V rail, and the encoder's 12 V supply boosted from the pack. | for research (owner, 2026-09-28) |
| V144 | R7, Pack charger | What is the lowest charge current into the pack, in A, that the charger must deliver from a USB-C source advertising 3.0 A? | for research (owner, 2026-09-28) |
| V145 | R7, Pack cells | What is the lowest capacity per cell, in mAh, that the pack's 18650 cells need? Or what run time, in minutes, from a full pack at a stated load? | for research (owner, 2026-09-28) |
| V146 | R7, Pack cells | Over which ambient temperature range, in °C, must the pack charge, and over which range must it discharge? | for research (owner, 2026-09-28) |
| V147 | R7, Pack charger | Does the 2S pack carry an NTC thermistor for the charger's temperature input? If so, what is its resistance at 25 °C in kΩ, and its B value in K? | for research (owner, 2026-09-28) |
| V148 | R7, Charger input limit | Is the charger's USB-C input the RP2354B's USB port, or a USB-C connector of its own? | for research (owner, 2026-09-28) |
| V149 | R7, Pack overcurrent protection | Does the pack's protection sit in the pack, as a part off the board bought at Digi-Key (rule 1), or on the IO board, as an LCSC part that JLCPCB places? | for research (owner, 2026-09-28) |
| V150 | R7, Pack overcurrent protection | Besides overcurrent, which protections must the pack's protection carry: per-cell overvoltage on charge (threshold in V), short circuit (threshold in A), over-temperature (threshold in °C), or none? | for research (owner, 2026-09-28) |
| V151 | R7, Pack cells | Rule 1 has the 18650 cells, a part off the board, bought at Digi-Key, and Digi-Key's API returned no cell from an S9 maker on 2026-09-28. Which vendor sells the cells and counts them for rules 4 and 5? | for research (owner, 2026-09-28) |
| V152 | R7, Power path | With USB-C present, no DC input, and the pack flat or opened by the 3.0 V floor disconnect, does the IO board run from USB-C? If yes, up to what current, in A at 5 V, does the power path deliver to the board? | for research (owner, 2026-09-28) |
| V153 | R7, Pack charger | Must the charger charge the pack while R5's 3.0 V floor disconnect is open, with the charger connected on the cells' side of the disconnect? Answer yes or no. | for research (owner, 2026-09-28) |
| V154 | R8, Motor monitor | The motor monitor (INA238) and the ESC's telemetry (extended DShot at 0.25 V and 1 A a count, or OpenYGE) can both report. Which source then fills the BENCH page's voltage and current registers (page 0x20, registers 0 and 1)? Answer with a rule, for example: the monitor whenever it answers, and the telemetry only when no monitor answers. | for research (owner, 2026-09-28) |
| V155 | R8, Motor monitor | At what motor current, in A, does the coprocessor's overcurrent action trip? Give a figure for the onboard path (150 A continuous, F2) and one for the external path. | for research (owner, 2026-09-28) |
| V156 | R8, Onboard shunt temperature sensor | At what temperature, in °C, does the over-temperature action trip? | for research (owner, 2026-09-28) |
| V157 | R8, Onboard shunt temperature sensor | Which temperature does the over-temperature action watch? The candidates are the sensor beside the onboard shunt, the motor's infrared or thermocouple channel (F5), the ESC's telemetry temperature, the INA238's die temperature, or more than one of them. | for research (owner, 2026-09-28) |
| V158 | R8, Motor monitor | How long, in ms, must the motor current stay above the overcurrent threshold before the action trips? Or is one conversion above it enough? | for research (owner, 2026-09-28) |
| V159 | R8, Motor monitor | What is the longest time, in ms, from overcurrent detection to the ESC pack switch open? | for research (owner, 2026-09-28) |
| V160 | R8, Motor monitor | What opens the ESC pack switch on a motor overcurrent? The options are the coprocessor's firmware, the INA238's ALERT output wired to the switch driver, or nothing (the action disarms the outputs and the switch follows the enable node only). The Not defeatable rule (IOBoard.md:197) allows no processor, register or firmware in the path from GPIO6 to either gate, and the coprocessor cannot remove the enable. | for research (owner, 2026-09-28) |
| V161 | R8, Motor monitor | The INA238 has no charge or energy register (datasheet SLYS025B, Table 6-3); the INA228 has both. Are the BENCH charge (mAh) and energy (0.1 Wh) integrated in firmware from the INA238's current and power readings, and if so at what sample period in ms? Or must the motor monitor accumulate them in hardware? | for research (owner, 2026-09-28) |
| V162 | R8, External shunt | What resistance range must the external shunt have, in µΩ? The pages give busbar type, 50 to 100 µΩ, from Power.md's table at 300 A, with no owner date. 100 µΩ reaches the INA238's ±40.96 mV at 409.6 A. | for research (owner, 2026-09-28) |
| V163 | R8, External shunt | What is the highest current, in A, the external shunt path must carry and measure? The owner's answer gives 'above 150 A to 300 A and more'. | for research (owner, 2026-09-28) |
| V164 | R8, External shunt | Is the external path's highest current continuous or a peak? If a peak, for how long, in s? | for research (owner, 2026-09-28) |
| V165 | R8, External shunt | What resolution must the motor current reading have on the external path, in mA? Power.md states that 25 mA (50 µΩ) is below the noise of a running ESC. That noise is not measured. | for research (owner, 2026-09-28) |
| V166 | R8, Motor monitor | What accuracy must the motor current reading hold, in % of reading plus an offset in A, on the onboard path and on the external path? | for research (owner, 2026-09-28) |
| V167 | R8, Motor monitor | What accuracy must the pack voltage reading hold up to 67.2 V, in mV or % of reading? | for research (owner, 2026-09-28) |
| V168 | R8, Onboard shunt | Over which ambient temperature range, in °C, must the current and voltage readings hold their accuracy? The monostable's window is specified over 0 to 50 °C (pull request #167). No range is stated for the measurement. | for research (owner, 2026-09-28) |
| V169 | R8, Onboard shunt | What is the highest power, in W, the onboard shunt may dissipate at 150 A continuous? A 100 µΩ shunt dissipates 2.25 W and a 200 µΩ shunt 4.5 W. | for research (owner, 2026-09-28) |
| V170 | R8, Onboard shunt temperature sensor | What range, in °C, and what accuracy, in °C, must the temperature sensor beside the onboard shunt have? | for research (owner, 2026-09-28) |
| V171 | R8, Port current monitor | What is the highest voltage, in V, a port current monitor must measure and withstand at its inputs? Power.md labels the port monitors '≤15 V' with no source. The ports cut off above 6.0 V or 8.7 V (F16). | for research (owner, 2026-09-28) |
| V172 | R8, Port current monitor | What is the highest current, in A, a port current monitor channel must read? The servo procedures act on 3.0 A a servo and 4.0 A a pair. The rail stops at 6.35 A. | for research (owner, 2026-09-28) |
| V173 | R8, Port current monitor | What is the longest time, in ms, from a port's current exceeding the 3.0 A ceiling to the abort? | for research (owner, 2026-09-28) |
| V174 | R8, Port current monitor | What accuracy and noise, in mA, must a port current reading hold? The host model assumes 0.02 A peak-to-peak noise, and the servo procedures act on differences of 0.08 A and 0.15 A. | for research (owner, 2026-09-28) |
| V175 | R8, Port current monitor | Must the 3.0 A servo abort be carried by a hardware alert output of the port monitor? Or is the firmware check at every sample enough? | for research (owner, 2026-09-28) |
| V176 | R8, Servo rail monitor | Does the servo rail keep a current monitor of its own beside the 20 port monitors? docs/Manifest.md lists the INA745A on the servo rail as selected. | for research (owner, 2026-09-28) |
| V177 | R8, External shunt sense input | Does each motor shunt sit in the ESC pack's positive lead (high side) or in its return (low side)? Answer for the onboard path and for the external path. On the high side the sense leads sit at up to 67.2 V to ground; on the low side they sit within the shunt drop of ground. | for research (owner, 2026-09-28) |
| V178 | R8, External shunt sense input | When a test runs through the external path, where does that path's INA238 take the pack voltage for its VBUS input? The options are a separate voltage-sense lead from the pack, the external shunt's pack-side sense lead, or no connection, with no voltage reading on the external path. | for research (owner, 2026-09-28) |
| V179 | R8, External shunt sense input | What is the highest voltage, in V, the external sense input must withstand without damage, between its two sense leads and from either lead to the board's ground? The fault cases include a sense lead on the wrong terminal, a lead off, and the load side shorted to ground. | for research (owner, 2026-09-28) |
| V180 | R8, Port current monitor | Must a port current monitor channel read current that flows back into the servo rail, from a back-driven or braking servo? If so, down to how many A in that direction? Or is a range from 0 A to the positive full scale enough? | for research (owner, 2026-09-28) |
| V181 | R9, Cell monitor | What is the lowest cell count, in cells, of an ESC pack that the cell monitor reads on its balance lead? The page gives 1 to 16 cells. F15 gives only the top, 16 cells. The 1 is the lower bound of SET_PACK_CELLS, which the settings table sets as a physical limit ('a pack cannot have zero cells'). | for research (owner, 2026-09-28) |
| V182 | R9, Cell monitor | What resolution, in mV, does each cell voltage reading need? The screen prints each cell to 0.01 V, the pack mean to 0.001 V and the spread in whole mV. The per-cell resistance under load divides a cell's voltage change by the current change, so this resolution also bounds the resistance. | for research (owner, 2026-09-28) |
| V183 | R9, Cell monitor | What largest error, in mV, may each cell voltage reading have under load, and over which ambient temperature range, in °C? The spread verdict switches at 30 mV and 60 mV between two cells (shared/ui/battery_screen.c:46-47). | for research (owner, 2026-09-28) |
| V184 | R9, Cell monitor | What is the lowest cell voltage, in V, that the cell monitor reads within that error? F15 sets the top at 4.2 V a cell. The ESC pack's chemistry and discharge floor are not stated. | for research (owner, 2026-09-28) |
| V185 | R9, Cell monitor | How often, in ms, does the cell monitor read every cell of a 16-cell pack? The coprocessor samples the bench numbers every 20 ms (IOBoard.md:431). No rate is stated for the cells. | for research (owner, 2026-09-28) |
| V186 | R9, Cell monitor | What largest time, in µs, may separate the first and the last cell sample of one reading of the pack? The spread is measured under load (IOBoard.md:437), where each cell's voltage follows the ESC's current. | for research (owner, 2026-09-28) |
| V187 | R9, Cell monitor | What largest time, in µs, may separate a cell voltage sample from the motor current sample it is paired with for the per-cell resistance under load? The current comes from the INA238 (R8). | for research (owner, 2026-09-28) |
| V188 | R9, Cell monitor | What voltage, in V, must the cell monitor's balance-lead input be rated for, above the 67.2 V of F15? Round 2 selects the overvoltage protection on the balance-lead connector (Research.md:28), which clamps under that rating. No line places the balance lead behind R3's input protection on the ESC pack's power path. | for research (owner, 2026-09-28) |
| V189 | R9, Cell monitor | What largest voltage, in V, may stand between the ESC pack's negative at the balance lead and the IO board's ground during a motor test? If the cell monitor is isolated from the RP2354B instead, what isolation voltage, in V? The ESC pack's return reaches the star point by its own lead (testbench/WIRING.md:37-39). Which side of the motor supply the shunts sit on is not stated (IOBoard.md:469). The isolation between the 150 A path and the 3.3 V I²C bus is not defined (IOBoard.md:497). | Isolated: the monitor sits on the pack's negative and reaches the RP2354B across a barrier (isoSPI or digital isolator) rated 85 V DC working or more; no offset limit below that (owner, 2026-09-28) |
| V190 | R9, Cell monitor | What largest current, in µA, may the cell monitor draw from any one pin of the ESC pack's balance lead, with the IO board powered and with it unpowered? An uneven draw across the pins discharges the cells unevenly and opens the spread the screen judges. | Each tap pin: 100 µA powered and 10 µA unpowered, averaged over 1 s; the monitor's supply from top to bottom not counted (owner, 2026-09-28) |
| V191 | R9, Cell monitor | What voltages, in V, must the balance-lead input survive from a mis-plugged lead: the largest reverse voltage on any pin against the lead's lowest pin, and the largest voltage between two adjacent pins? A lead plugged in reversed, or shifted by one pin, puts these voltages on the input. | for research (owner, 2026-09-28) |
| V192 | R9, Cell monitor | How many connectors, of how many cells each, carry the ESC pack's balance lead to the IO board? A 16-cell pack's balance lead can arrive as one lead or as several. A lead plugged in without the others leaves inputs open. | 1 connector of 16 cells (17 pins) on the IO board; each pack's own leads, one or several, reach it through an adapter lead (owner, 2026-09-28) |
| V193 | R9, Cell monitor | May a jumper or an adapter lead set the cell count at the balance-lead input? Answer yes or no, and if yes, the number of positions. With no setting, a pack of any count from the lowest to 16 cells plugs in with no change on the board. | No: a pack of any count from the lowest to 16 cells plugs in with no setting; the board ties unused inputs itself (owner, 2026-09-28) |
| V194 | R9, Cell monitor | What resolution, in mΩ, does each cell's resistance under load need, and over what smallest step of the motor current, in A, is it taken? The battery screen carries one resistance per cell in mΩ (shared/ui/include/battery_screen.h:16). It prints the pack's sum to 0.1 mΩ (shared/ui/battery_screen.c:295). No line states a figure for the resistance. | for research (owner, 2026-09-28) |
| V195 | R9, Cell monitor | Does the cell monitor find the plugged pack's cell count, and whether any pack is plugged in, from the balance lead itself, or does the count come from the Cells setting, SET_PACK_CELLS? Answer 'from the lead' or 'from the setting'. The battery screen shows NO PACK, 'nothing on the lead', when its state is not valid (shared/ui/battery_screen.c:233; shared/ui/include/battery_screen.h:27). Its state carries the cell count (battery_screen.h:14). No line states where the count comes from. | for research (owner, 2026-09-28) |
| V196 | R10, Converter for the accelerometer | What is the highest motor speed, in rpm, at which the vibration path must resolve the once-per-revolution fundamental? | for research (owner, 2026-09-28) |
| V197 | R10, Converter for the accelerometer | What sample rate, in samples per second, must the converter reach on each vibration channel? | for research (owner, 2026-09-28) |
| V198 | R10, Converter for the accelerometer | What is the smallest vibration amplitude at the fundamental, in mg (thousandths of standard gravity), that the balance measurement must resolve? | 1 mg at the fundamental, after one run's averaging (V208) (owner, 2026-09-28) |
| V199 | R10, Converter for the accelerometer | What is the largest vibration amplitude, in g, that the vibration path must take without clipping? | ±16 g (owner, 2026-09-28) |
| V200 | R10, Converter for the accelerometer | What input voltage span, in volts, must the converter take at its accelerometer input? | for research (owner, 2026-09-28) |
| V201 | R10, Converter for the accelerometer | How many accelerometer axes does the converter sample: 1, or the 2 axes in the firewall's plane? | for research (owner, 2026-09-28) |
| V202 | R10, Converter for the accelerometer | What phase error, in degrees at the highest motor speed, may the variation of the delay between a vibration sample and the index pulse add? | for research (owner, 2026-09-28) |
| V203 | R10, Converter for the accelerometer | What accuracy, in percent of reading, must the vibration amplitude reach? | No requirement: the correction is relative (owner, 2026-09-28) |
| V204 | R10, Converter for the accelerometer | Over what ambient temperature range, in °C, must the converter and its reference hold their figures? | for research (owner, 2026-09-28) |
| V205 | R10, Converter for the accelerometer | Does Q4 take a digital accelerometer on SPI (Serial Peripheral Interface), such as the IIS3DWB, as a third alternative beside the RP2354B's ADC with a reference and an external ADC: yes or no? (feeds Q4 only) | for research (owner, 2026-09-28) |
| V206 | R10, Converter for the accelerometer | Must the converter for the accelerometer also measure a control surface's static angle from gravity, with an accelerometer on the surface, and if so to what resolution, in degrees? | Yes, to 0.1°, 2 axes, zeroed on a reference surface (owner, 2026-09-28) |
| V207 | R10, Converter for the accelerometer | What is the longest lead, in metres, from the accelerometer on the rig arm or the firewall to the IO board? | 2 m (owner, 2026-09-28) |
| V208 | R10, Converter for the accelerometer | Over how many seconds at a steady speed may one balance run average the vibration signal? | 5 s at steady speed per run (owner, 2026-09-28) |
| V209 | R11, servo measured position | Which sensor gives the servo's measured position that the servo screen draws beside the commanded one? The choices are: the F4 encoder mounted on the servo output shaft; a sensor of its own that R11 selects (for example a magnetic angle sensor at the horn); or none in round 1. | for research (owner, 2026-09-28) |
| V210 | R11, servo measured position | If a sensor other than the F4 encoder gives the servo's measured position: what angular resolution must it have, in degrees, over what horn travel, in degrees? | for research (owner, 2026-09-28) |
| V211 | R11, BENCH rpm source | When more than one rotation input reports (optical index, magnetic pickup, phase-wire clip, encoder, ESC telemetry), which one fills the BENCH rpm register and feeds the motor stall action? The choices are: a fixed priority order (state the order), or the one input a panel setting picks. | for research (owner, 2026-09-28) |
| V212 | R11, rotation inputs | What is the highest shaft speed, in rpm, that the rotation inputs and the vibration path must follow? | for research (owner, 2026-09-28) |
| V213 | R11, rotation inputs | Down to which shaft speed, in rpm, must at least one rotation input keep reading, including the coast-down after a disarm? | for research (owner, 2026-09-28) |
| V214 | R11, phase-wire clip | What peak voltage, in V, must the phase-wire clip's input withstand: the 67.2 V of F15 plus what margin for switching overshoot and ringing on the motor phase? | for research (owner, 2026-09-28) |
| V215 | R11, phase-wire clip | How does the phase-wire clip couple to the motor phase: galvanically, clipped onto the bare conductor, or without contact, through the wire's insulation? | for research (owner, 2026-09-28) |
| V216 | R11, phase-wire clip | Is the phase-wire input galvanically isolated from the 3.3 V logic, and if so, to what isolation voltage, in V? Or is it referenced to the ground that the ESC pack's return shares at the star point? | for research (owner, 2026-09-28) |
| V217 | R11, magnetic pickup | Which magnetic pickup does the input take, and does R11 select it or does the operator bring it? The choices are: a Hall-effect sensor with a digital output (state its supply voltage, in V), or a variable-reluctance coil with an AC output (state its peak voltage, in V). | for research (owner, 2026-09-28) |
| V218 | R11, magnetic pickup | How many pulses per revolution does the magnetic pickup give: one per magnet added to the bell (state how many magnets), or one per rotor pole pair (MOTOR_POLES / 2, up to 21)? | for research (owner, 2026-09-28) |
| V219 | R11, optical index | Does R11 select the optical index sensor itself (a reflective sensor on a lead, aimed from below at the pen line on the motor bell), or only the IO board's front end for a sensor the operator brings? In the second case, at what supply voltage, in V, and with which output: open collector, push-pull or analogue? | for research (owner, 2026-09-28) |
| V220 | R11, optical index | If R11 selects the optical index sensor: at what distance, in mm, does it read the pen line on the motor bell? | for research (owner, 2026-09-28) |
| V221 | R11, accelerometer | The accelerometer sits on the rig arm or the firewall. How is it built and bought: on a sensor board of its own that JLCPCB assembles (parts with LCSC numbers, under the IO board's sourcing rules), or as a part or module off the board, bought at Digi-Key (sourcing rule 1)? | for research (owner, 2026-09-28) |
| V222 | R11, accelerometer | How long is the lead from the accelerometer on the rig arm or the firewall to the IO board, in m? | 2 m at most (owner, 2026-09-28) |
| V223 | R11, accelerometer | What full-scale acceleration, in g, must the vibration sensor measure without clipping? | for research (owner, 2026-09-28) |
| V224 | R11, accelerometer | What is the smallest vibration amplitude at the rotation fundamental, in mg, that the balance measurement must resolve? | 1 mg at the fundamental, after one run's averaging (V208) (owner, 2026-09-28) |
| V225 | R11, accelerometer | What phase error, in degrees at the highest rpm, may delay variation add across the whole vibration chain: the accelerometer, the index pulse's front end and the converter together? | 1° at the highest rpm, whole chain, V202's converter share inside it: 2.54 µs of delay variation at 65,535 rpm, 16.7 µs at 10,000 rpm (owner, 2026-09-28) |
| V226 | R11, accelerometer | May the vibration sensor be a digital accelerometer with raw, unfused output and a known sampling instant (the IIS3DWB class, on SPI), or must it be analogue? | for research (owner, 2026-09-28) |
| V227 | R11, encoder input | Are the encoder's 1 MHz (single-ended) and 5 MHz (RS-422) figures the count rate, in counts per second over A and B together, or the frequency of each of A and B, in Hz (a quarter of the count rate)? | for research (owner, 2026-09-28) |
| V228 | R11, encoder input | With the supply jumper at 12 V and the single-ended form selected, what is the highest signal voltage, in V, the single-ended input takes: 5 V, or the 12 V of an encoder whose push-pull outputs swing to its supply (HTL, high-threshold logic)? | for research (owner, 2026-09-28) |
| V229 | R11, phase-wire clip | What range of ESC PWM (pulse-width modulation) switching frequency, in kHz, must the phase-wire clip's front end reject while it passes the motor's electrical frequency? The programmer screen offers 24, 48 and 96 kHz (shared/ui/programmer_screen.c:115). Does that range apply, or a wider one? | for research (owner, 2026-09-28) |
| V230 | R11, accelerometer | What is the lowest shaft speed, in rpm, at which a balance run is measured? | 1,000 rpm: a 16.7 Hz fundamental (owner, 2026-09-28) |
| V231 | R11, accelerometer | Across what ambient temperature range, in °C, must the sensors mounted at the motor work? That is the accelerometer on the rig arm or firewall, and the optical and magnetic sensors if R11 selects them. | 0 to 85 °C at the sensor mount (owner, 2026-09-28) |
| V232 | R12, motor temperature thermocouple | Which motor temperature channel, infrared or thermocouple, fills the BENCH page's one motor temperature register (register 5, 0.1 °C signed, valid bit 4)? Or does a second register carry the other channel? | Register 5 and valid bit 4 carry the thermocouple; the infrared channel gets a new BENCH register 13 (0.1 °C, signed) and valid bit 5, a minor protocol change (owner, 2026-09-28) |
| V233 | R12, motor temperature thermocouple | Which thermocouple type (a letter, such as K, J or T) does the thermocouple channel take? | Type K (owner, 2026-09-28) |
| V234 | R12, motor temperature thermocouple | What motor temperature range (°C, lowest to highest) must the thermocouple channel measure? | for research (owner, 2026-09-28) |
| V235 | R12, motor temperature thermocouple | What accuracy (± °C) must the thermocouple channel give over that range, cold-junction compensation included? | ±2 °C over 0 to 200 °C at 0 to 50 °C board ambient: converter and cold junction together, probe tolerance excluded (owner, 2026-09-28) |
| V236 | R12, motor temperature thermocouple | How often (Hz) must the thermocouple channel deliver a new reading? | for research (owner, 2026-09-28) |
| V237 | R12, motor temperature thermocouple | What voltage (V) must the thermocouple input withstand, without damage, between either lead and the IO board's ground if the junction touches a motor phase or winding? | for research (owner, 2026-09-28) |
| V238 | R12, motor temperature infrared | How long (m) is the lead from the IO board's infrared connector to the infrared sensor at the motor? Answer 0 m if the sensor sits on the IO board. | 2 m (owner, 2026-09-28) |
| V239 | R12, motor temperature infrared | What object temperature range (°C, lowest to highest) must the infrared channel measure? | for research (owner, 2026-09-28) |
| V240 | R12, motor temperature infrared | What accuracy (± °C) must the infrared channel give over that range? | ±3 °C over 0 to 200 °C object at 0 to 85 °C sensor ambient, object filling the field of view; the motor surface's emissivity error excluded (owner, 2026-09-28) |
| V241 | R12, motor temperature infrared | What full field of view (degrees) must the infrared sensor have, given its distance to the motor and the surface it must see? | 35° full field of view at 25 mm from the motor: a spot of about 16 mm on the bell (owner, 2026-09-28) |
| V242 | R12, motor temperature infrared | How often (Hz) must the infrared channel deliver a new reading? | for research (owner, 2026-09-28) |
| V243 | R12, external I2C ports | At which clock rate (kHz) must the external I2C ports run? | for research (owner, 2026-09-28) |
| V244 | R12, external I2C ports | What is the longest cable (m) between an external I2C port and the device on it? | for research (owner, 2026-09-28) |
| V245 | R12, external I2C ports | What current (mA) must each external I2C port supply on its 3.3 V pin? | for research (owner, 2026-09-28) |
| V246 | R12, external I2C ports | Which 7-bit I2C addresses (hex) must a device on an external I2C port be free to use? | for research (owner, 2026-09-28) |
| V247 | R12, non-volatile store | What capacity (bytes) must an I2C FRAM or EEPROM store hold for the IO board's output binding? (feeds Q8 only) | for research (owner, 2026-09-28) |
| V248 | R12, non-volatile store | How many saves (count) of the output binding must the store survive over the board's life? (feeds Q8 only) | for research (owner, 2026-09-28) |
| V249 | R12, motor temperature infrared | Where does the infrared motor temperature sensor sit: off the IO board, on a lead to a connector on the IO board, as IOBoard.md:558 assumes, or on the IO board itself? | Off the IO board at the motor, on a small sensor board of its own that JLCPCB assembles (parts with LCSC numbers), on a lead to the IO board's infrared connector (owner, 2026-09-28) |
| V250 | R12, motor temperature thermocouple | Must the thermocouple channel detect an open or unplugged thermocouple, so that the motor temperature valid bit (BENCH flag bit 4) stays clear while none is fitted? Answer yes or no. | for research (owner, 2026-09-28) |
| V251 | R12, motor temperature thermocouple | Over what ambient temperature range (°C, lowest to highest) must the IO board operate, which sets the temperature of the thermocouple's cold junction at its connector? | for research (owner, 2026-09-28) |
| V252 | R12, motor temperature infrared | What ambient temperature range (°C, lowest to highest) does the infrared sensor itself see at its mounting position near the motor? | 0 to 85 °C (owner, 2026-09-28) |
| V253 | R12, external I2C ports | Must a device be plugged into or out of an external I2C port while the board is powered, with the bus and the other devices on it still running? Answer yes or no. | Yes: plugging in or out while powered leaves the other port and the coprocessor's internal buses running; a transfer on the plugged port may fail and is retried (owner, 2026-09-28) |
| V254 | R13, load-cell excitation | Which supply gives the 5 V load-cell excitation of F3: the IO board's 5 V rail, which also carries the display's 1.5 A continuous and 2 A peak, or a separate supply for the bridges? | for research (owner, 2026-09-28) |
| V255 | R13, load-cell excitation | What is the bridge resistance of each of the 3 load cells (the thrust cell and the 2 torque cells), in Ω? | for research (owner, 2026-09-28) |
| V256 | R13, load-cell bridge ADC | What is the rated output of each of the 3 load cells (the thrust cell and the 2 torque cells), in mV/V? | for research (owner, 2026-09-28) |
| V257 | R13, load-cell bridge ADC | What is the rated capacity of each of the 3 load cells (the thrust cell and the 2 torque cells), in N? | for research (owner, 2026-09-28) |
| V258 | R13, load-cell bridge ADC | What thrust range, in N, and what torque range, in N·m, must the bench measure, each from its largest negative to its largest positive value? | for research (owner, 2026-09-28) |
| V259 | R13, load-cell bridge ADC | What is the smallest load change, in N, that each load cell's channel must resolve (the thrust cell and each torque cell), at the output rate asked for the load-cell channels? | for research (owner, 2026-09-28) |
| V260 | R13, load-cell bridge ADC | What accuracy must each load-cell channel hold, in % of the cell's rated output, counting the bridge ADC's and the excitation's gain and offset errors and their drift with temperature? | for research (owner, 2026-09-28) |
| V261 | R13, load-cell bridge ADC | At what output rate must each of the 3 load-cell channels be read, in samples per second per channel? | for research (owner, 2026-09-28) |
| V262 | R13, load-cell bridge ADC | How far apart in time, in ms, may the readings of the 2 torque cells, and of the thrust cell, be taken? May one multiplexed converter read them in turn, or must they be converted at the same instant? | for research (owner, 2026-09-28) |
| V263 | R13, load-cell bridge ADC | Does each load cell connect with 4 wires (excitation and signal) or with 6 wires (excitation, signal and sense leads)? | for research (owner, 2026-09-28) |
| V264 | R13, load-cell bridge ADC | Over what ambient temperature range, in °C, must each load-cell channel hold the accuracy asked for it? | for research (owner, 2026-09-28) |
| V265 | R13, load-cell bridge ADC | What is the longest time, in ms, from a step change in thrust or torque to a fully settled reading on that channel? | for research (owner, 2026-09-28) |

#### Decided on the research's output

These stay open while the research runs. The research tasks report the
alternatives with their figures, and P5 budgets both alternatives of Q4 and
Q8. The owner decides each before T6 and writes the decision in the last
column; `tools/research/session.py prepare T6` refuses to start T6 while one is
empty.

| ID | To decide | Reported by | Decision (owner, date) |
| --- | --- | --- | --- |
| Q4 | The RP2354B's ADC with a reference, or an external ADC, for the accelerometer | R10: ENOB (effective number of bits) and sampling rate of each against the balance measurement, whether each conversion can be triggered or timestamped on the index pulse's timebase, and its worst-case latency jitter |  |
| Q8 | The output binding in the RP2354B's flash, or in an I²C FRAM (ferroelectric random-access memory) or EEPROM (electrically erasable programmable read-only memory) | R2: how many received frames each CAN controller holds against the stall of the W25Q16JV above and the frame count P1's question returns. R12: FRAM and EEPROM candidates |  |
| Q9 | The monostable, if no part has both the '423 behaviour and I_off: which requirement gives way, or which part goes in front of the trigger inputs | R3: the parts that meet each requirement alone, what each lacks, and the shortlisted parts with I_off for a place in front of the trigger inputs. The latch's power-up state bears on it: a latch that powers up set holds the enable low through a clear-release pulse (F11, R3) |  |
