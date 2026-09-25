# Where the hardware stands

The running record for `hardware/`. Stock figures carry the date they were
taken and are not valid after it.

**Status: no board exists.** No schematic and no layout exist. The RP2354B is
held in the owner's JLCPCB personal parts library. It holds 20
(owner, 2026-09-25), and 12 TPS55285. No other part is bought. Four ICs
(integrated circuits) are decided: the RP2354B and the INA3221 by the owner,
and the TPS55285 and the INA238, each with its reason. The pack charger and
both motor shunts are open, and the INA3221's stock is not checked. [The IO (input/output) board specification](docs/IOBoard.md) is a
draft. [Round 1 of the component research](docs/Research.md) is planned and
has not run.

## Decided

| | Part | Reason |
| --- | --- | --- |
| Microcontroller | RP2354B | the owner's choice, held in the owner's JLCPCB personal parts library: the RP2350B die and 2 MB of flash in one QFN-80 (quad flat no-lead) package, 48 GPIO (general-purpose input/output). The 2 MB changes three places in the coprocessor build. The IO board needs a board file that states the B package, 2 MB of flash and no PSRAM (pseudo-static random-access memory). On the IO board the output store sits in the last two sectors of the 2 MB, and the image limit is 2,088,960 bytes. The tree places the store at 4 MB less 8 kB and holds the limit at 4,186,112 bytes. [IO board](docs/IOBoard.md#microcontroller-flash-and-debug), [Research](docs/Research.md#consequence-of-the-rp2354bs-2-mb-flash) |
| Board power | 12 to 20 V DC (direct current) input and the 2S (two cells in series) pack | the DC input, when present, runs the IO board and the display. The pack runs both otherwise. The selection is automatic. The display is powered through the link cable. The pack holds 2 lithium-ion 18650 cells. It is disconnected in hardware when any cell falls to 3.0 V, read per cell from its balance lead (question F9 in [Research](docs/Research.md#blocking-answered-before-the-research-tasks), owner, 2026-09-25). At that floor the pack is 6.0 V at the lowest (2 × 3.0 V); full charge is 8.4 V (2 × 4.2 V). Owner decision, 2026-09-24 |
| Motor current path | INA238 on the IO board, two shunt positions | an onboard shunt for 150 A continuous (question F2) with a temperature sensor beside it, and an input for an external shunt for currents above 150 A, to 300 A and more (owner, 2026-09-25), connected by its sense leads. The two paths replace each other: a test is cabled through one, never both (owner, 2026-09-25). One INA238 for each path (question F1). The ESC (electronic speed controller) pack has up to 16 cells, 67.2 V at 4.2 V a cell (question F15), under the INA238's 85 V. Owner decisions, 2026-09-24 and 2026-09-25 |
| Output ports | 20 typed ports | 16 PWM ports (enable, pulse range, frame rate per pair, one switchable to PPM) and 4 multiprotocol ports (servo PWM, DShot, bidirectional DShot, ESC telemetry, servo configuration, ESC bootloader); a one-pin UART socket; an external CAN port on its own controller. Each port has a supply switch, an LV/HV jumper and a current monitor channel. Owner decision, 2026-09-25 |
| Servo current | INA3221, one channel per port | 20 ports on 7 INA3221 (21 channels), a 10 mΩ shunt each: 4 mA resolution, 16.4 A full scale, two I²C buses. The limit search needs one sensor per servo. Owner decision, 2026-09-25; R8 checks stock and lifecycle |
| Sensor inputs | load cells, a phase-wire clip, a magnetic pickup, motor temperature sensors, an encoder | thrust and torque come from load cells. Rotation speed in rpm (revolutions per minute) comes from a phase-wire clip and a magnetic pickup. The motor's temperature is measured. The encoder is ABI (quadrature A and B plus an index pulse) and is counted in the pin budget. Load cells on 3 channels, 1 for thrust and 2 for torque, at 5 V excitation (question F3). The encoder takes 5 V single-ended up to 1 MHz or RS-422 (Recommended Standard 422) differential up to 5 MHz, and a 5 V or 12 V supply, each selected by a jumper (question F4). Motor temperature on 2 channels, 1 infrared and 1 thermocouple (question F5). 2 external I²C (Inter-Integrated Circuit) ports at 3.3 V, Qwiic (question Q7). Owner decisions, 2026-09-24 and 2026-09-25. [IO board](docs/IOBoard.md#measurement) |
| Coprocessor watchdog | the RP2350's internal watchdog | a frozen coprocessor resets, and every gate it drives opens while its pins are at their reset state. Owner decision, 2026-09-25. The timeout is not stated, and the coprocessor image enables no watchdog yet |
| Output combinations | the combinations P5 returns | bidirectional DShot takes 2 PIO state machines per output; the 4 multiprotocol ports take 8 of the 12. The board supports the combinations P5 returns; the coprocessor refuses a bind outside them and the OUTPUTS page shows the refusal. Owner decision, 2026-09-25 |
| Servo supply | TPS55285 | 12 held in the owner's personal library (owner, 2026-09-25). It senses current internally and its I²C limit stops at 6.35 A, so the rail runs to 6.35 A. Its input takes 2.4 to 22 V, so the DC input is 12 to 20 V. The TPS55288, which limits across an external shunt to 10.1 A, is not used. [Power](docs/Power.md) |
| Motor monitor | INA238 | 85 V, 16 bit. Its step at 300 A with a 50 µΩ shunt is 25 mA. It has the same 10-pin VSSOP (very thin shrink small-outline package) and the same pin order as the INA228. Neither vendor could supply the INA228 on 2026-09-01: 29 at JLCPCB with a presale count of −477, and 0 at Digi-Key. On the figures of 2026-09-01 the INA238 has no second source that passes [the README's rule](README.md#rules): 0 at Digi-Key with 2500 due 2026-10-27, and the INA228 as above. Round 1 reads the stock again and reports the result to the owner. [Power](docs/Power.md#motor-monitor-ina238), [Research](docs/Research.md#sourcing-rules) |
| Monitor footprint | one for both | INA228 and INA238 are pin-identical and distinguishable at run time by DEVICE_ID (0x3F: 0x2281 or 0x2381). On the figures of 2026-09-01 the shared footprint gives the INA238 no second source, because the INA228 could not be bought (Motor monitor row) |
| Servo rail | two output settings | up to 5.5 V for LV (low-voltage) servos and up to 8.4 V for HV (high-voltage) servos, 4 to 6.35 A. The converter runs from the 12 to 20 V DC input or from the 2S pack (owner, 2026-09-24). 6.35 A at 8.4 V from the pack is not checked. Research category R6 checks it. [Power](docs/Power.md) |
| Stock source | the vendor's own API (application programming interface) | on 2026-09-01 the mirror `jlcsearch.tscircuit.com` reported 1046 INA228AIDGSR. JLCPCB's own API reported 29, with a presale count of −477: oversold. [Sourcing](docs/Sourcing.md#observed-divergence-2026-09-01) |

## Open

| Item | State | Needs |
| --- | --- | --- |
| Pack charger | charged from USB-C (Universal Serial Bus Type-C) only, drawing up to 3 A at 5 V, and not from the DC input (question F8 in [Research](docs/Research.md), owner, 2026-09-25). The BQ25887 (2 A, 400 mA per-cell balancing, [Power](docs/Power.md)) takes 3.9 to 6.2 V, a USB-level input. | the part, selected in research category R7 |
| Port monitor stock | INA3221, chosen by the owner for the 20 ports; stock and lifecycle not checked. The INA745A and INA238 figures in [Power](docs/Power.md) remain as the alternatives. | research category R8 |
| Onboard shunt, 150 A | on the IO board, with a temperature sensor beside it. 150 A is continuous (question F2, owner, 2026-09-25). At 150 A a 100 µΩ shunt drops 150 A × 100 µΩ = 15 mV and dissipates (150 A)² × 100 µΩ = 2.25 W continuously. A 200 µΩ shunt drops 30 mV and dissipates 4.5 W. Both drops are inside the INA238's ±40.96 mV range. | the part and its footprint, selected in research category R8 |
| External shunt, 300 A and above | off the board, sense leads to the IO board. Class settled: busbar type, 50 to 100 µΩ, 4.5 to 9 W at 300 A. The four-terminal SMD (surface-mount device) parts recorded in [Power](docs/Power.md#motor-monitor-ina238) stop at 0.2 mΩ and would dissipate 18 W at 300 A. Whether an SMD part reaches 50 to 100 µΩ is checked in research category R8. | the part, selected in R8 and bought at Digi-Key, the second vendor of question S3; the sense-lead connector (round 2); the driven external module that switches the external path, whose driver output is on the IO board (question F7, owner, 2026-09-25), selected in research category R3 |
| Converter input connector and heatsink | the converter runs from the 12 to 20 V DC input or the 2S pack (owner, 2026-09-24). | the input connector (round 2), the heatsink (round 3), and the current drawn from the pack (research categories R5 and R6) |
| Layout | Isolation, a 150 A path and a 3.3 V I²C bus on one board, connectors, thermal. | the onboard shunt first |

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

1. The owner has accepted [the research plan](docs/Research.md) and answered
   questions S1, S3 and S8 ([Sourcing questions](docs/Research.md#sourcing))
   (owner, 2026-09-25).
2. Round 1 of the component research, as in
   [its scope](docs/Research.md#scope): the ICs, the parts that fix an IC's
   surroundings, both motor shunts and the switch of the ESC pack. It is
   planned as six tasks (question S8), each of at most 32 agents (owner,
   2026-09-24). Each
   research task starts when the owner has answered the questions its
   categories depend on ([Research](docs/Research.md#questions-for-the-owner)).
3. Round 2 selects the passives and the connectors, the DC input connector
   among them. It also selects the protection on the signal, sensor,
   balance-lead, link and heartbeat connectors. The power input's protection
   is in round 1. Round 3 sets the mechanical, thermal and layout constraints,
   the converter's heatsink among them ([Research](docs/Research.md#scope)).
4. Schematic.
5. Order each part with a manufacturer lead time of 16 weeks or more as soon
   as the schematic fixes it. On 2026-09-01 Texas Instruments quoted 16 weeks
   on every power-path part checked, and 26 weeks on the INA745x and the
   INA260 ([Sourcing](docs/Sourcing.md#the-2026-09-01-sweep)). Once the
   schematic exists, the build waits up to 26 weeks for those parts.
