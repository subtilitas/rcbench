# Where the hardware stands

The running record for `hardware/`. Stock figures carry the date they were
taken and are not valid after it.

**Status: nothing exists.** No schematic, no layout, no board, no part bought.
Four ICs (integrated circuits) are decided, with the reason for each.

## Decided

| | Part | Reason |
| --- | --- | --- |
| Servo supply | TPS55288 | its I²C (Inter-Integrated Circuit) current limit sets a sense voltage across an external shunt, so the ceiling scales with the resistor; the alternatives stop at 6.35 A in silicon. [Power](docs/Power.md) |
| Motor monitor | INA238 | 85 V, 16 bit, the same VSSOP-10 and pin order as the INA228, which cannot be bought. 20 bits are not needed at 300 A. [Power](docs/Power.md) |
| Monitor footprint | one for both | INA228 and INA238 are pin-identical and distinguishable at run time by DEVICE_ID (0x3F: 0x2281 or 0x2381) |
| Servo rail | two output settings | up to 5.5 V for LV (low-voltage) servos and up to 8.4 V for HV (high-voltage) servos, 4 to 8 A. The converter input is a design choice, not part of the requirement: 12 V or more delivers the full 8 A at 8.4 V; a 5 V input delivers about 4 A. [Power](docs/Power.md) |
| Charge current | 2 A, one BQ25887 | 3 A is the top of the requirement's range, not a requirement; 2 A with 400 mA per-cell balancing stands. [Power](docs/Power.md) |
| Stock source | the vendor's own API (application programming interface) | a mirror reported 1046 of a part the vendor reported 29 of, oversold. [Sourcing](docs/Sourcing.md) |

## Open

| Item | State | Needs |
| --- | --- | --- |
| Servo rail monitor | INA745A (integrated shunt, no Kelvin layout) against a second INA238 (one driver, one footprint for both monitors, plus an external shunt). | a firmware-surface decision, not a parts one |
| 300 A shunt | Class settled: busbar type, 50 to 100 µΩ, 4.5 to 9 W at 300 A. The largest four-terminal SMD (surface-mount device) parts stop at 0.2 mΩ and would dissipate 18 W. | part number, footprint, supplier |
| Converter input | 12 V or more for 8 A at 8.4 V. Connector and heatsink follow from it. | input connector, heatsink |
| Layout | Isolation, a 300 A path and a 3.3 V I²C bus on one board, connectors, thermal. | the shunt first |

## Not planned

- **A hall-effect sensor for the 300 A path.** It avoids the shunt's 9 W and
  its layout, at the cost of gain and offset drift on the bench's primary
  measurement. The shunt is the measurement.
- **A 20-bit motor monitor.** The INA228 cannot be bought, and 25 mA of
  resolution on 300 A is below the noise on the wire.
- **Bilingual pages** while no board exists. See [the README](README.md).

## Order of work

1. Pick the 300 A shunt: the only component with no candidate.
2. Choose the converter's input connector and heatsink for 12 V or more.
3. Schematic.
4. Buy the long-lead parts early. TI quoted a 16-week manufacturer lead time on
   every part on the list on 2026-09-01, and 26 weeks on the INA745x and
   INA260. The parts are the critical path once the schematic exists.
