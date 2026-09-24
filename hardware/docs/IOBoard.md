# IO board specification

What the IO board has to do, with the numbers the tree already fixes. The IO
board is the coprocessor board. It carries everything with a deadline:
outputs, receiver inputs, sensor front ends, the power path and the CAN
(Controller Area Network) link to the display. The display is the ESP32-S3
panel in `firmware/panel/`.

**Status: draft.** No schematic and no part list exist. Parts are selected
by [the round 1 research](Research.md), which has not run. A line marked
_open_ waits on a question in that page.

State of each line:

| State | Meaning |
| --- | --- |
| built | runs on the bring-up module (Waveshare RP2350-CAN) and moves to the IO board unchanged |
| decided | a part or a value is chosen and recorded |
| required | the tree needs it; no part chosen |
| open | a question to the owner decides it |

## Summary

| Function | Value | State |
| --- | --- | --- |
| Microcontroller | RP2354B: RP2350B die and 2 MB QSPI (quad serial peripheral interface) NOR flash in one QFN-80 (quad flat no-lead) 10 × 10 mm package, 48 GPIO (general-purpose input/output), 8 ADC (analogue-to-digital converter) inputs, 520 kB SRAM (static RAM) | decided |
| Link to the display | classic CAN at 1 Mbit/s, 29-bit identifiers, protocol 4.0. Every signal that reaches an RP2354B pin is at 3.3 V logic; a 5 V output on bank 0 takes a 2.2 kΩ series resistor, because the 5 V rail can come up before 3.3 V ([STATUS.md](../../STATUS.md#constraints)) | built |
| Display supply | through the link cable, voltage and current open (F6) | open |
| Safety | heartbeat input, retriggerable monostable with a 150 ms window gating the outputs and the servo and ESC (electronic speed controller) power | required |
| Outputs: PWM (pulse-width modulation) and DShot | 8 slots, 8 channels. A servo has swung and a motor has run from the panel on the bring-up bench; no pulse width, frame period or bit time has been seen on an instrument | built |
| Outputs: PPM (pulse-position modulation) and bidirectional DShot | written and host-tested; neither has driven anything on hardware ([STATUS.md](../../STATUS.md#state)) | required |
| Programming connector | one connector for every programmer: the one-wire bootloader at 19,200 baud half duplex (BLHeli_S, AM32), the ESCape32 text CLI (command-line interface), VESC's framed packets at 115,200 baud, and the Hitec D-series servo protocol | required |
| Receiver inputs | S.BUS, iBUS, SUMD, CRSF, SRXL2, JETI EX Bus, one pin each | required |
| Servo supply | two settings, up to 5.5 V and up to 8.4 V, 4 to 8 A, TPS55288 | decided |
| Servo sockets | 8, each with a supply switch, a current monitor and a voltage ceiling set in hardware | required |
| Motor current and voltage | INA238, 85 V bus; onboard shunt up to 150 A with a temperature sensor beside it; external-shunt input for 300 A and above | decided |
| Cell monitor | 1 to 14 cells on the balance lead | required |
| Board power | 12 to 24 V DC input or the bench's own 2S pack, selected automatically. The pack is disconnected in hardware below a discharge floor (F9) | decided; floor open |
| Pack charger | 2S, 2 A, balancing | open (R7) |
| Vibration | analogue accelerometer and a once-per-revolution index pulse on one timebase; the sensor and converter resolve the 167 Hz fundamental at 10,000 rpm (revolutions per minute), and a fused IMU (inertial measurement unit) streaming at 100 Hz does not ([Balancing](../../docs/Balance.md)) | required |
| Rotation | optical index, magnetic pickup, phase-wire clip, ESC telemetry, the encoder (quadrature A and B plus index, ABI) | required |
| Temperature | ESC (from its telemetry), motor (sensor open, F5), onboard shunt | required |
| Thrust and torque | load cells, channel count open (F3) | required |
| External sensors | I²C (Inter-Integrated Circuit) ports, count open (Q7) | required |
| Non-volatile store | output binding: 32 record slots in 2 flash sectors, or an FRAM (ferroelectric RAM) (Q8) | open |
| Debug | SWD (Serial Wire Debug), USB (Universal Serial Bus) boot, UART (universal asynchronous receiver-transmitter) console | required |
