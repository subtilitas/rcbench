# Where the hardware stands

The running record for `hardware/`. Stock figures carry the date they were
taken and are not valid after it.

**Status: nothing exists.** No schematic, no layout, no board. The RP2354B is
held in the owner's JLCPCB personal parts library, in a quantity not stated
here; no other part is bought. The microcontroller is fixed by the owner. Two
ICs (integrated circuits), the TPS55288 and the INA238, are decided with the
reason for each; the charger and the servo socket monitors are open. [The IO board specification](docs/IOBoard.md) is a draft;
[round 1 of the component research](docs/Research.md) is planned and has not
run.

## Decided

| | Part | Reason |
| --- | --- | --- |
| Microcontroller | RP2354B | the owner's choice, held in the owner's JLCPCB personal parts library: the RP2350B die and 2 MB of flash in one QFN-80 (quad flat no-lead) package, 48 GPIO (general-purpose input/output). The 2 MB moves the coprocessor's output store and its image-size guard. [IO board](docs/IOBoard.md) |
| Board power | 12 to 24 V DC (direct current) input and the 2S (two cells in series) pack | the DC input runs the IO board and the display when present, the pack otherwise, selected automatically; the display is powered through the link cable; the pack is disconnected in hardware below a discharge floor that is not chosen. Owner decision, 2026-09-24 |
| Motor current path | INA238 on the IO board, two shunt positions | an onboard shunt for up to 150 A with a temperature sensor beside it, and an input for an external shunt for 300 A and above, connected by its sense leads. Owner decision, 2026-09-24 |
| Servo current | one monitor per socket, 8 sockets | beside each socket's supply switch; the limit search needs one sensor per servo. Owner decision, 2026-09-24 |
| Servo supply | TPS55288 | its I²C (Inter-Integrated Circuit) current limit sets a sense voltage across an external shunt, so the ceiling scales with the resistor, and its inductor current limit is 16 A. The TPS55289 limits the same way and is rated 8 A; the TPS55285 senses internally and stops at 6.35 A. [Power](docs/Power.md) |
| Motor monitor | INA238 | 85 V, 16 bit, the same VSSOP-10 and pin order as the INA228, which cannot be bought. Its step at 300 A with a 50 µΩ shunt is 25 mA. [Power](docs/Power.md) |
| Monitor footprint | one for both | INA228 and INA238 are pin-identical and distinguishable at run time by DEVICE_ID (0x3F: 0x2281 or 0x2381) |
| Servo rail | two output settings | up to 5.5 V for LV (low-voltage) servos and up to 8.4 V for HV (high-voltage) servos, 4 to 8 A. The converter runs from the 12 to 24 V DC input or from the 2S pack (owner, 2026-09-24); 8 A at 8.4 V from the pack is not checked, and research category R6 checks it. [Power](docs/Power.md) |
| Stock source | the vendor's own API (application programming interface) | a mirror reported 1046 of a part the vendor reported 29 of, oversold. [Sourcing](docs/Sourcing.md) |

## Open

| Item | State | Needs |
| --- | --- | --- |
| Pack charger | the BQ25887 (2 A, 400 mA per-cell balancing, [Power](docs/Power.md)) takes 3.9 to 6.2 V only and does not charge from the 12 to 24 V DC input. | which source charges the pack and at what current, question F8 in [Research](docs/Research.md); then research category R7 |
| Servo socket monitors | one per socket, 8 sockets. INA745A (integrated shunt, no Kelvin layout) and INA238 (one driver for every monitor, an external shunt each) are the candidates recorded in [Power](docs/Power.md). | research category R8 |
| Onboard shunt, 150 A | on the IO board, with a temperature sensor beside it. At 150 A a 100 µΩ shunt dissipates 2.25 W and a 200 µΩ shunt 4.5 W. | whether 150 A is continuous or a peak (question F2); part number, footprint |
| External shunt, 300 A and above | off the board, sense leads to the IO board. Class settled: busbar type, 50 to 100 µΩ, 4.5 to 9 W at 300 A. The largest four-terminal SMD (surface-mount device) parts stop at 0.2 mΩ and would dissipate 18 W. | part number, the sense-lead connector, and what switches the external path (question F7) |
| Converter input | 12 to 24 V DC or the 2S pack, decided (owner, 2026-09-24). | input connector, heatsink, and the current drawn from the pack (research categories R5 and R6) |
| Layout | Isolation, a 150 A path and a 3.3 V I²C bus on one board, connectors, thermal. | the onboard shunt first |

## Not planned

- **A hall-effect sensor for the 300 A path.** It avoids the shunt's 9 W and
  its layout, at the cost of gain and offset drift on the bench's primary
  measurement. The shunt is the measurement.
- **A 20-bit motor monitor.** The INA228 cannot be bought. The INA238's
  25 mA step with a 50 µΩ shunt is 0.008 % of 300 A; the noise
  a running ESC (electronic speed controller) puts on the wire is not
  measured.
- **Bilingual pages** while no board exists. See [the README](README.md).

## Order of work

1. The owner accepts [the research plan](docs/Research.md) and answers its
   blocking questions.
2. Round 1 of the component research: the ICs, the onboard shunt and the
   external shunt.
3. Choose the converter's input connector and heatsink for 12 to 24 V.
4. Schematic.
5. Buy the long-lead parts early. TI quoted a 16-week manufacturer lead time on
   every part on the list on 2026-09-01, and 26 weeks on the INA745x and
   INA260. The parts are the critical path once the schematic exists.
