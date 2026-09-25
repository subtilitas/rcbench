# Where the hardware stands

The running record for `hardware/`. Stock figures carry the date they were
taken and are not valid after it.

**Status: no board exists.** No schematic and no layout exist. The RP2354B is
held in the owner's JLCPCB personal parts library. It holds 20
(owner, 2026-09-25). No other part is bought. Three ICs (integrated circuits) are
decided: the RP2354B by the owner, and the TPS55288 and the INA238, each with
its reason. The pack charger, the servo socket monitors and both motor shunts
are open. [The IO (input/output) board specification](docs/IOBoard.md) is a
draft. [Round 1 of the component research](docs/Research.md) is planned and
has not run.

## Decided

| | Part | Reason |
| --- | --- | --- |
| Microcontroller | RP2354B | the owner's choice, held in the owner's JLCPCB personal parts library: the RP2350B die and 2 MB of flash in one QFN-80 (quad flat no-lead) package, 48 GPIO (general-purpose input/output). The 2 MB changes three places in the coprocessor build. The IO board needs a board file that states the B package, 2 MB of flash and no PSRAM (pseudo-static random-access memory). On the IO board the output store sits in the last two sectors of the 2 MB, and the image limit is 2,088,960 bytes. The tree places the store at 4 MB less 8 kB and holds the limit at 4,186,112 bytes. [IO board](docs/IOBoard.md#microcontroller-flash-and-debug), [Research](docs/Research.md#consequence-of-the-rp2354bs-2-mb-flash) |
| Board power | 12 to 24 V DC (direct current) input and the 2S (two cells in series) pack | the DC input, when present, runs the IO board and the display. The pack runs both otherwise. The selection is automatic. The display is powered through the link cable. The pack is disconnected in hardware below a discharge floor, which is not chosen (question F9 in [Research](docs/Research.md#blocking-answered-before-the-research-tasks)). Owner decision, 2026-09-24 |
| Motor current path | INA238 on the IO board, two shunt positions | an onboard shunt for up to 150 A with a temperature sensor beside it, and an input for an external shunt for 300 A and above, connected by its sense leads. Owner decision, 2026-09-24 |
| Servo current | one monitor per socket, 8 sockets | beside each socket's supply switch. The limit search needs one sensor per servo. Owner decision, 2026-09-24 |
| Sensor inputs | load cells, a phase-wire clip, a magnetic pickup, a motor temperature sensor, an encoder | thrust and torque come from load cells. Rotation speed in rpm (revolutions per minute) comes from a phase-wire clip and a magnetic pickup. The motor's temperature is measured. The encoder is ABI (quadrature A and B plus an index pulse) and is counted in the pin budget. The channel counts, the encoder's signal level and the temperature sensor's type are open (questions F3, F4 and F5). Owner decision, 2026-09-24. [IO board](docs/IOBoard.md#measurement) |
| Servo supply | TPS55288 | its I²C (Inter-Integrated Circuit) current limit sets a sense voltage across an external shunt, so the ceiling scales with the resistor. Its inductor current limit is 16 A. The TPS55289 limits the same way and is rated 8 A. The TPS55285 senses internally and stops at 6.35 A. [Power](docs/Power.md) |
| Motor monitor | INA238 | 85 V, 16 bit. Its step at 300 A with a 50 µΩ shunt is 25 mA. It has the same 10-pin VSSOP (very thin shrink small-outline package) and the same pin order as the INA228. Neither vendor could supply the INA228 on 2026-09-01: 29 at JLCPCB with a presale count of −477, and 0 at Digi-Key. On the figures of 2026-09-01 the INA238 has no second source that passes [the README's rule](README.md#rules): 0 at Digi-Key with 2500 due 2026-10-27, and the INA228 as above. Round 1 reads the stock again and reports the result to the owner. [Power](docs/Power.md#motor-monitor-ina238), [Research](docs/Research.md#sourcing-rules) |
| Monitor footprint | one for both | INA228 and INA238 are pin-identical and distinguishable at run time by DEVICE_ID (0x3F: 0x2281 or 0x2381). On the figures of 2026-09-01 the shared footprint gives the INA238 no second source, because the INA228 could not be bought (Motor monitor row) |
| Servo rail | two output settings | up to 5.5 V for LV (low-voltage) servos and up to 8.4 V for HV (high-voltage) servos, 4 to 8 A. The converter runs from the 12 to 24 V DC input or from the 2S pack (owner, 2026-09-24). 8 A at 8.4 V from the pack is not checked. Research category R6 checks it. [Power](docs/Power.md) |
| Stock source | the vendor's own API (application programming interface) | on 2026-09-01 the mirror `jlcsearch.tscircuit.com` reported 1046 INA228AIDGSR. JLCPCB's own API reported 29, with a presale count of −477: oversold. [Sourcing](docs/Sourcing.md#observed-divergence-2026-09-01) |

## Open

| Item | State | Needs |
| --- | --- | --- |
| Pack charger | the BQ25887 (2 A, 400 mA per-cell balancing, [Power](docs/Power.md)) takes 3.9 to 6.2 V only and does not charge from the 12 to 24 V DC input. | which source charges the pack and at what current, question F8 in [Research](docs/Research.md); then research category R7 |
| Servo socket monitors | one per socket, 8 sockets. INA745A (integrated shunt, no Kelvin layout) and INA238 (one driver for every monitor, an external shunt each) are the candidates recorded in [Power](docs/Power.md). | research category R8 |
| Onboard shunt, 150 A | on the IO board, with a temperature sensor beside it. At 150 A a 100 µΩ shunt drops 15 mV and dissipates 2.25 W. A 200 µΩ shunt drops 30 mV and dissipates 4.5 W. Both drops are inside the INA238's ±40.96 mV range. | whether 150 A is continuous or a peak (question F2); the part and its footprint, selected in research category R8 |
| External shunt, 300 A and above | off the board, sense leads to the IO board. Class settled: busbar type, 50 to 100 µΩ, 4.5 to 9 W at 300 A. The four-terminal SMD (surface-mount device) parts recorded in [Power](docs/Power.md#motor-monitor-ina238) stop at 0.2 mΩ and would dissipate 18 W at 300 A. Whether an SMD part reaches 50 to 100 µΩ is checked in research category R8. | the part, selected in R8 and bought at the second vendor of question S3; the sense-lead connector (round 2); what switches the external path (question F7, research category R3) |
| Converter input connector and heatsink | the converter runs from the 12 to 24 V DC input or the 2S pack (owner, 2026-09-24). | the input connector (round 2), the heatsink (round 3), and the current drawn from the pack (research categories R5 and R6) |
| Layout | Isolation, a 150 A path and a 3.3 V I²C bus on one board, connectors, thermal. | the onboard shunt first |

## Not planned

- **A hall-effect sensor for the 300 A path.** It avoids the shunt's 9 W and
  its layout, at the cost of gain and offset drift on the bench's primary
  measurement. The shunt is the measurement.
- **A 20-bit motor monitor.** The INA228 could not be bought from either
  vendor on 2026-09-01: 29 at JLCPCB with a presale count of −477, and 0 at
  Digi-Key. The INA238's 25 mA step with a 50 µΩ shunt is 0.008 % of 300 A.
  The noise a running ESC (electronic speed controller) puts on the wire is
  not measured.
- **Bilingual pages** while no board exists. See [the README](README.md).

## Order of work

1. The owner accepts [the research plan](docs/Research.md) and answers
   questions S1, S3 and S8 ([Sourcing questions](docs/Research.md#sourcing)).
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
