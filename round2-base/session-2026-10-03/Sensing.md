# Sensing

The parts of research group C: R9 (the cell monitor on the ESC pack's
balance lead; ESC is electronic speed controller), R10 (the converter for the
accelerometer, question Q4), R11 (rotation and vibration front ends), R12
(motor temperature, the external I²C ports and the non-volatile store,
question Q8; I²C is Inter-Integrated Circuit) and R13 (the load cells). This
page records the choice, the alternatives, the stock and the reason, from the
returns of round 1 under `hardware/research/round1/` and
`hardware/research/round1/selection.json`. One row per part, with every stock
field, is in [Parts](Parts.md).

**Availability was checked on 2026-09-29.** Stock figures are not valid after
that day. `python3 tools/jlc_stock.py --check 5` reads them again.

P2 to P6 are the phases of [Research](Research.md#agent-layout): P4 the
verifiers, P5 the cross-category check and P6 the completeness check. T1 to T6 are its tasks, and the runs named FU- its follow-up tasks. S, F, Q and V followed by a number are its questions to the owner, cited with the owner's answer. A figure no P4 verifier re-read is marked not verified. The alternatives below rank 1 are not verified by P4; their figures are the re-rank's.

## Summary

ADC is an analogue-to-digital converter; rpm is revolutions per minute;
BENCH is the link page that carries the bench readings; EEPROM is
electrically erasable programmable read-only memory.

| Need | Part | Package | JLCPCB / Digi-Key stock, 2026-09-29 | Reason |
| --- | --- | --- | --- | --- |
| Cell monitor, 1 to 16 cells | ADBMS1818ASWAZ-RL (Analog Devices) | LQFP-64 | 12500 / 564 | total measurement error ±4.8 mV at 4.2 V over temperature, inside the ±6 mV check; rank 1 on JLCPCB stock and price of the parts that pass |
| Converter for the accelerometer (Q4) | ADCS7476AIMFX/NOPB (Texas Instruments), 2 a board | SOT-23-6 | 2,312 / 88 | the owner's Q4 decision. ENOB (effective number of bits) 11.34 at least over −40 to 125 °C; aperture jitter 30 ps; a conversion starts on the chip-select edge the coprocessor drives |
| Accelerometer | ADXL316WBCSZ (Analog Devices), on the sensor board | LFCSP-12 | 104 / 387 | meets every value with 100 pF on XOUT: 1.000 µs of delay variation at 1,092 Hz against a 1.244 µs share; 210 µg/√Hz. The accelerometer's requirement figures are not verified (below) |
| Magnetic pickup | DRV5015A1QDBZR (Texas Instruments) | SOT-23 | 339 / 2831 | 0.7 mT typical operate point, the lowest found; 20 kHz guaranteed bandwidth |
| Phase-wire clip | TLV3201AIDBVR (Texas Instruments), 2 a board | SOT-23-5 | 41679 / 0 | 55 ns maximum delay over temperature and 40 µA supply current. Its second source is the alternate TLV3601DBVR (JLCPCB 1623) |
| Encoder input | AM26LV32EIDR (Texas Instruments) | SOIC-16 | 48987 / 13484 | one 3.3 V quad receiver takes A, B and I; 26 ns maximum delay and 6 ns maximum output skew |
| Encoder decode, rotation from the ESC, rotation inputs, BENCH rpm source, servo measured position | RP2354B (Raspberry Pi), no added part | QFN-80 | held 20 | two PIO (programmable input/output) state machines decode the encoder; firmware on the RP2354B does the rest. No program is in the tree, and the decoding figures are not verified (below) |
| Motor temperature, thermocouple | MAX31856MUD+T (Analog Devices) | TSSOP-14 | 11,459 / 12,023 | a budget of ±1.51 °C against ±2 °C with no firmware calibration; open-thermocouple detection on chip; inputs rated ±45 V |
| Motor temperature, infrared | MLX90614ESF-BCC-000-TU (Melexis), on the sensor board | TO-39 | 311 / 933 | 35° view, ±3 °C at most over 0 to 200 °C at 3.3 V |
| External I²C ports | TCA9548APWR (Texas Instruments) | TSSOP-24 | 39,690 / 0 | rank 1 on JLCPCB stock. Its second source is the alternate PCA9548APWR (JLCPCB 5,100) |
| Non-volatile store (Q8) | MB85RC256VPNF-G-AMERE2 (RAMXEED, which S1's Fujitsu covers) | SOP-8 | 6,523 / 484 | the owner's Q8 decision. 32,768 bytes against 792 needed, 10^12 cycles a byte, no write wait |
| Load-cell bridge ADC | ADS1235IRHBR (Texas Instruments) | VQFN-32 | 1340 / 0 | 0.13 µV RMS (root mean square) at 1200 samples a second and gain 128, against a limit of 0.170 µV; rank 1 on price of the three. Digi-Key holds 0: the owner's stock exception, below |
| Load-cell excitation | BD450M2FP3-CE2 (ROHM) | SOT-223-4 | 3165 / 3618 | rank 1: its output band covers the full load of 0 to 60 mA with no added part |

The owner's decisions (owner, 2026-09-29): Q4, an external ADC,
ADCS7476AIMFX/NOPB, the part R10 kept. Q8, an I²C FRAM (ferroelectric
random-access memory), MB85RC256VPNF-G-AMERE2, the part R12 kept.

ADS1235IRHBR carries the owner's stock exception: Digi-Key, the second
vendor, read stock 0 against the gate of 50 on 2026-09-29; the owner keeps the
part and notes the exception (owner, 2026-09-29). Digi-Key holds 401 of
ADS1235IRHBT, the same device on a 250-piece reel. The exception is in the
part's row in [Parts](Parts.md).

## R9: cell monitor

| Function | Rank 1 | Alternatives |
| --- | --- | --- |
| Cell monitor | ADBMS1818ASWAZ-RL | ADBMS1818ASWAZ-R7 (the same part on a 300-piece reel), ADBMS1818ASWZ-RL (another land pattern), LTC6813HLWE-1#3ZZPBF and LTC6813HLWE-1#3ZZTRPBF; not verified |

It needs an auxiliary V+ of at least 16 V for packs under 16 V, and round 2's
input network for a reversed lead.

## R10: converter for the accelerometer

| Option | Rank 1 | Alternatives |
| --- | --- | --- |
| External ADC, chosen (Q4) | ADCS7476AIMFX/NOPB | ADS7886SBDBVR (the same pinout), ADC128S052CIMTX/NOPB; not verified |
| The RP2354B's ADC with a reference, verified, not chosen | LM4040AIM3-3.0/NOPB (JLCPCB 2,969, Digi-Key 26,705) | REF3330AIDBZR, REF35300QDBVR, LM4040AIM3-3.0+T, LM4040BIM3-3.0/NOPB; not verified |

## R11: rotation and vibration

| Function | Rank 1 | Alternatives |
| --- | --- | --- |
| Accelerometer | ADXL316WBCSZ | ADXL326BCPZ-RL7 (no second source); not verified |
| Optical index | no part | see [Not known](#not-known) |
| Magnetic pickup | DRV5015A1QDBZR | DRV5015A1EDBZRQ1, AH1711-SA-7 and 4 others; not verified |
| Phase-wire clip | TLV3201AIDBVR | TLV3601DBVR (verified, the rule-5 alternate), TLV3501AIDBVR, MAX999EUK+T; the last two not verified |
| Encoder input | AM26LV32EIDR | DS34LV86TMX/NOPB, ISL32273EIBZ-T and 2 others; not verified |
| Encoder decode | RP2354B, 2 PIO state machines | none: no external decoder from an allowed maker is in the parts database |
| Rotation from the ESC, rotation inputs, BENCH rpm source, servo measured position | RP2354B | none |

The encoder decode is not run on hardware and its program is not in the tree.

## R12: temperature, external ports and store

| Function | Rank 1 | Alternatives |
| --- | --- | --- |
| Motor temperature, thermocouple | MAX31856MUD+T | ADS1220IPWR, ADS122C04IPWR; not verified |
| Motor temperature, infrared | MLX90614ESF-BCC-000-TU | MLX90614ESF-DCC-000-TU; not verified |
| External I²C ports | TCA9548APWR | PCA9548APWR (verified, the rule-5 alternate), PCA9548APW,118; the second not verified |
| Non-volatile store, FRAM, chosen (Q8) | MB85RC256VPNF-G-AMERE2 | FM24CL64B-GTR; not verified |
| Non-volatile store, EEPROM, verified, not chosen | M24256E-FMN6TP (JLCPCB 125,256, Digi-Key 185) | none |

Digi-Key lists PCA9548APWR as Not For New Designs; Texas Instruments' page,
the status of record, reads ACTIVE.

## R13: load cells

| Function | Rank 1 | Alternatives |
| --- | --- | --- |
| Load-cell bridge ADC | ADS1235IRHBR | ADS1262IPWR, ADS1261IRHBR; not verified |
| Load-cell excitation | BD450M2FP3-CE2 | TLE42754D, TPS7B8250QDGNRQ1, TPS7B8150DGNR, each needing a 1 mA preload; not verified |

## Budget of the Q4 and Q8 options

P5's budget in T5, as its critic upheld it
(`hardware/research/round1/T5/002-P5.json`,
`hardware/research/round1/T5/004-P5-critic.json`).

| Budget | Value | Within its limit |
| --- | --- | --- |
| Q4, external ADC (chosen) | ADCS7476AIMFX/NOPB takes 1 of the 2 SPI (Serial Peripheral Interface) controllers, shared with the CAN (Controller Area Network) controllers and the load-cell ADC, and 0 of the RP2354B's 8 ADC inputs. Its conversion trigger on the index pulse's timebase is not confirmed in the returns | yes |
| Q4, reference | LM4040AIM3-3.0/NOPB: up to 4 of the RP2354B's 8 ADC inputs and no SPI or I²C controller. Triggering on the index pulse's timebase is not confirmed in the returns | yes |
| Q8, FRAM (chosen) | MB85RC256VPNF-G-AMERE2 answers at 0x50 to 0x57, outside the monitor block 0x40 to 0x4F; it adds no GPIO (general-purpose input/output) beyond its bus | yes |
| Q8, EEPROM | M24256E-FMN6TP, an I²C part; its address block is not read in the returns, so its clearance from 0x40 to 0x4F is not confirmed | yes |

## Not known

Each item is reported to the owner, with its evidence; a part an item needs comes from a follow-up of its research category, or from round 2 for a passive, a connector, or the protection on the signal, sensor, balance-lead, link and heartbeat connectors ([Research](Research.md#scope)).

- R11, optical index: not known; no part qualifies. No optical reflective sensor searched meets the delay-variation budget of V225, at most 1.244 µs at 1,092 Hz (65,535 rpm): VCNT2030, VCNT2020, VCNT2025X01, QRD1114, QRE1113GR, RPR-220, TCND5000, TCRT5000 and TCRT5000L state response times of 10 to 70 µs (`hardware/research/round1/FU-C2/002-P2-R11.json`). The magnetic pickup and the phase-wire clip of the same category are selected.

The other conflicts and gaps up to T5 are on [Control](Control.md#not-known)
and [Supply](Supply.md#not-known).

## Not verified

The owner accepted each as open (owner, 2026-09-29).

- R11: optical index. No part; not verified.

Figures of group C that no P4 verifier confirmed; each is not verified:

- R9: figure found V186, not confirmed in FU-C1; not verified.
- R9: figure found V187, not confirmed in FU-C1; not verified.
- R9: figure P2 report: skew limit on the external path, corrected, not confirmed in FU-C1; not verified.
- R10: figure P2 report: Q4 reference supply headroom, not confirmed in FU-C1; not verified.
- R11: figure P2 report: encoder decoding, not confirmed in FU-C2; not verified.
- R11: figure re-rank report: separate receivers and the decoder, not confirmed in FU-C2; not verified.
- R11: figure re-rank report: comparator input bias, not confirmed in FU-C2; not verified.
- R11: figure function requirements: accelerometer, not confirmed in FU-C2; not verified.
- R12: figure P2 report: Q8 store candidate: M24256E-FMN6TP, not confirmed in FU-C1; not verified.
- R12: figure function requirements: external I2C ports, not confirmed in FU-C1; not verified.
