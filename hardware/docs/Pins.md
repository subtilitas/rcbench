# Pin map

**Status: draft, 2026-09-30.** The GPIO (general-purpose input/output) of the
IO board's two RP2354B, the main and the measurement coprocessor
([IOBoard](IOBoard.md#two-coprocessors)). [pinmap.json](pinmap.json) holds the
same map for tools; the tables below follow it. The schematic's routing can move
a signal to another pin with the same function.

`tools/pinmap_check.py` checks the map against the pin functions in
pico-sdk 2.3.0's `io_bank0.h`: each function exists on its pin, each SPI (Serial
Peripheral Interface), UART (universal asynchronous receiver-transmitter) and
I²C (Inter-Integrated Circuit) bus stays on one instance, each PIO
(programmable input/output) block's pins fit one 32-pin window (GPIO 0 to 31 or
16 to 47), the ADC (analogue-to-digital converter) is on GPIO 40 to 47, and a
signal that reaches a connector is on GPIO 0 to 39, the fault-tolerant pins.
Every PIO pin names its protocol, every serial pin its bus, and an I²C bus
has 2 pins.

```sh
python3 tools/pinmap_check.py hardware/docs/pinmap.json
```

| Chip | GPIO used | Free |
| --- | --- | --- |
| Main coprocessor | 44 of 48 | 4: GPIO 15, 35, 39, 47 |
| Measurement coprocessor | 32 of 48 | 16 |

## Owner decisions

- The rotation inputs (encoder A, B and index, the phase-wire clip, the
  magnetic pickup) and the receiver input sit on the measurement coprocessor.
  The main coprocessor's motor stall action gets its rpm over the inter-chip
  link (owner, 2026-09-30).
- The external CAN (Controller Area Network) controller stays on the main
  coprocessor. It shares the display link controller's SPI bus, with its own
  chip select and interrupt (owner, 2026-09-30).
- The inter-chip link is a UART, 2 pins a side (owner, 2026-09-30). With the
  link and VESC on the main coprocessor's two hardware UARTs, the UART socket
  runs as a PIO UART on 1 pin.
- VESC's TX and RX pass 2 of the 3 spare channels of the two SN74CBTLV3126PWR,
  whose enable is the heartbeat enable node, so they stop with the other
  outputs (owner, 2026-09-30; Control.md, R4).

## Main coprocessor

| GPIO | Function | Signal | Connector |
| --- | --- | --- | --- |
| 0 | UART0_TX | inter-chip link TX |  |
| 1 | UART0_RX | inter-chip link RX |  |
| 2 | I2C1_SDA | main I2C SDA: 3 MCP23017, FRAM, TPS55288 (provisional) |  |
| 3 | I2C1_SCL | main I2C SCL |  |
| 4 | UART1_TX | VESC TX | yes |
| 5 | UART1_RX | VESC RX | yes |
| 6 | SIO | heartbeat input | yes |
| 7 | SIO | optical index, main copy (rpm) | yes |
| 8 | SIO | INA3221 critical-alert shared line |  |
| 9 | SIO | INA3221 warning-alert shared line |  |
| 10 | SIO | INA228 ALERT shared line |  |
| 11 | SIO | alert shift register load |  |
| 12 | SIO | alert shift register clock |  |
| 13 | SIO | alert shift register data |  |
| 14 | SIO | I/O expander reset (the 3 MCP23017), pulled down |  |
| 16 | PWM_A_0 | PWM port 1 | yes |
| 17 | PWM_B_0 | PWM port 2 | yes |
| 18 | PWM_A_1 | PWM port 3 | yes |
| 19 | PWM_B_1 | PWM port 4 | yes |
| 20 | PWM_A_2 | PWM port 5 | yes |
| 21 | PWM_B_2 | PWM port 6 | yes |
| 22 | PWM_A_3 | PWM port 7 | yes |
| 23 | PWM_B_3 | PWM port 8 | yes |
| 24 | PWM_A_4 | PWM port 9 | yes |
| 25 | PWM_B_4 | PWM port 10 | yes |
| 26 | PWM_A_5 | PWM port 11 | yes |
| 27 | PWM_B_5 | PWM port 12 | yes |
| 28 | PWM_A_6 | PWM port 13 | yes |
| 29 | PWM_B_6 | PWM port 14 | yes |
| 30 | PWM_A_7 | PWM port 15 | yes |
| 31 | PWM_B_7 | PWM port 16 | yes |
| 32 | PIO0 | multiprotocol port 1 | yes |
| 33 | PIO2 | UART socket (OpenYGE, one pin) | yes |
| 34 | PIO0 | multiprotocol port 2 | yes |
| 36 | PIO1 | multiprotocol port 3 | yes |
| 37 | USB_MUXING_VBUS_DETECT | USB VBUS detect, through a divider | yes |
| 38 | PIO1 | multiprotocol port 4 | yes |
| 40 | SPI1_RX | CAN SPI RX (MISO), link + external controller |  |
| 41 | SIO | link CAN controller CS, SIO, pulled up |  |
| 42 | SPI1_SCLK | CAN SPI SCLK |  |
| 43 | SPI1_TX | CAN SPI TX (MOSI) |  |
| 44 | SIO | link CAN controller INT, active low, pulled up |  |
| 45 | SIO | external CAN controller CS, SIO, pulled up |  |
| 46 | SIO | external CAN controller INT, active low, pulled up |  |

## Measurement coprocessor

| GPIO | Function | Signal | Connector |
| --- | --- | --- | --- |
| 0 | UART0_TX | inter-chip link TX |  |
| 1 | UART0_RX | inter-chip link RX |  |
| 2 | I2C1_SDA | sensor I2C bus B SDA: monitors, MCP9808, TPS55285 | yes |
| 3 | I2C1_SCL | sensor I2C bus B SCL | yes |
| 4 | SPI0_RX | accelerometer ADC data, both ADCS7476 (SDATA) |  |
| 5 | SIO | accelerometer ADC CS, first axis (ADCS7476), SIO, pulled up |  |
| 6 | SPI0_SCLK | accelerometer ADC SCLK, both ADCS7476 |  |
| 7 | PIO2 | optical index, measurement copy, sampled by PIO on the ADC timebase | yes |
| 8 | SPI1_RX | measurement SPI RX (MISO); reaches the cell monitor only across the barrier |  |
| 9 | SIO | load-cell ADC CS (ADS1235), SIO, pulled up |  |
| 10 | SPI1_SCLK | measurement SPI SCLK; reaches the cell monitor only across the barrier |  |
| 11 | SPI1_TX | measurement SPI TX (MOSI); reaches the cell monitor only across the barrier |  |
| 12 | SIO | load-cell ADC DRDY (ADS1235) |  |
| 13 | SIO | thermocouple converter CS (MAX31856), SIO, pulled up |  |
| 14 | SIO | thermocouple converter FAULT (MAX31856) |  |
| 15 | SIO | cell monitor CS, to the host side of the isoSPI or digital-isolator barrier (V189), SIO, pulled up |  |
| 16 | I2C0_SDA | sensor I2C bus A SDA: monitors, MCP9808, TPS55285 | yes |
| 17 | I2C0_SCL | sensor I2C bus A SCL | yes |
| 18 | PIO2 | Qwiic and infrared I2C bus SDA: TCA9548A and MLX90614, PIO | yes |
| 19 | PIO2 | Qwiic and infrared I2C bus SCL | yes |
| 20 | PIO0 | encoder A | yes |
| 21 | PIO0 | encoder B | yes |
| 22 | PIO0 | encoder index | yes |
| 23 | PIO1 | receiver input (S.BUS, iBUS, SUMD, CRSF, SRXL2, EX Bus) | yes |
| 24 | SIO | accelerometer ADC CS, second axis (ADCS7476), SIO, pulled up |  |
| 25 | SIO | phase-wire clip comparator (TLV3201) | yes |
| 26 | SIO | TCA9548A RESET, active low, pulled up: clears a Qwiic channel held low |  |
| 27 | SIO | magnetic pickup (DRV5015A1) | yes |
| 28 | SIO | thermocouple converter DRDY (MAX31856), active low |  |
| 29 | SIO | load-cell ADC START (ADS1235) |  |
| 30 | SIO | load-cell ADC RESET, active low (ADS1235) |  |
| 31 | SIO | load-cell ADC PWDN, active low (ADS1235) |  |

## I/O expanders

Three MCP23017 on the main coprocessor's I²C bus, at 0x20, 0x21 and 0x22, reset
together from GPIO 14. They carry 33 lines: the 20 port switch enables, the 5
bias selectors of the multiprotocol ports and the UART socket, the 5 converter
enables (4 TPS55285 and the TPS55288), the hardware latch read-back, and two
inputs for the DC input's source status and its brownout, until a decision
lets one signal serve both (IOBoard.md, DC input brownout detection). On the MCP23017, GPA7 and GPB7 are
outputs only and come out of reset as inputs, so they carry no port switch,
converter enable or input; the other 42 lines are free for them, 33 of them used. The third
expander follows from that; it takes no GPIO.

## Rules the map follows

- The 20 output ports sit on GPIO 16 to 38, and all three main PIO blocks are
  based at GPIO 16, so any block reaches any port. The 16 PWM (pulse-width
  modulation) ports are on GPIO 16 to 31, one pair to a slice, slices 0 to 7,
  so the frame rate is set per pair. The 4 multiprotocol ports' servo mode uses
  slices 8 to 11. Bidirectional DShot takes 2 state machines and 16 of the 32
  instruction words of a block, so ports 1 and 2 are on PIO0 and ports 3 and 4
  on PIO1; PIO2 carries PPM and the UART socket.
- Every SPI chip select is a plain output (SIO), with an external pull-up of
  about 10 kΩ. In SPI mode 0 the RP2350's SPI block raises its own chip select
  between frames, which breaks an MCP2518FD instruction, and RP2350 pads come
  out of reset pulled down, 32 to 86 kΩ, which would select every chip while
  its coprocessor resets.
- The measurement coprocessor's SPI1 carries parts in different SPI modes: the
  ADS1235 in mode 1, the MAX31856 in mode 1 and the ADBMS1818 in mode 3 at
  1 MHz at most. The firmware switches the format between transactions with
  every chip select high.
- The MCP2518FD limits its SPI clock to 0.85 × SYSCLK / 2, so the 10 MHz the
  link uses needs its 40 MHz clock.
- The MLX90614, an SMBus part at 100 kHz at most, and the TCA9548A of the Qwiic
  ports sit on a third I²C bus that the measurement coprocessor makes with PIO.
  Sensor buses A and B then run faster, and a Qwiic device cannot collide with
  the internal addresses 0x18 to 0x1F, 0x40 to 0x4F and 0x74 to 0x75.
- The encoder, at up to 5 MHz (F4), runs on R11's two-state-machine decoder:
  a pico-examples quadrature decoder reads at most one step every 10 system
  clocks.
- The measurement coprocessor samples its copy of the optical index with PIO,
  on the timebase of the two accelerometer ADCs (ADCS7476, one an axis), which share SPI0's data and clock with a chip select each.
- USB VBUS reaches GPIO 37 through a divider, so the USB pull-up follows the
  cable.
- The cell monitor reaches measurement SPI1 only across an isoSPI or
  digital-isolator barrier rated 85 V DC working or more, because it sits on
  the ESC pack's negative (V189, owner, 2026-09-28). GPIO 15 and the SPI1
  lines drive the barrier's host side.
- The ADS1235's START, RESET and PWDN each take a GPIO, beside its CS and
  DRDY: the interface's upper bound of 8 lines (R13).
- No signal from a connector sits on GPIO 40 to 47.

The SPI modes, the MCP2518FD clock limit, the MLX90614's bus speed, the
MCP23017's output-only pins and the pad reset state come from the reviews of
the drafts, which read the datasheets. Round 1's records do not verify them.

## Not known

- The bus of the charger group, BQ25713RSNR, MAX17320G22+ and FUSB303BTMX, and
  their interrupt and status outputs. On the main bus the FUSB303 takes 0x31,
  clear of the expanders.
- The fault outputs of the 4 TPS55285 and the TPS55288, and the port ceiling
  read-backs (V135, up to 40 lines): not pinned.
- A disable-only input on the pack switch drivers (V53, V160): the map gives the
  pack switches no pin; one line would take a free main pin.
- The receiver input is receive-only in the map. EX Bus telemetry or an SRXL2
  handshake needs the SN74LVC1T45DBVR's direction pin.
- The cell monitor's barrier part, an isoSPI transceiver pair or a digital
  isolator: not selected (R9).
- The TPS55288's bus: the map puts it on the main I²C bus, provisionally; the
  IO board specification leaves the main bus or a third measurement bus open.
- The MCP2518FD clock source, the inter-chip UART's rate and silence limit, and
  the measurement coprocessor's load and debug route.
