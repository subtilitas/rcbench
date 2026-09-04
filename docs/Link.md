# The link

<sub>**English** · [Deutsch](Link-de.md)</sub>

The two boards communicate over CAN (Controller Area Network) at 1 Mbit/s. Wiring first, then the
protocol reference.

## Wiring

| | |
| --- | --- |
| Bus | classic CAN without FD (flexible data rate), 1 Mbit/s, 29-bit identifiers only |
| Panel | TWAI controller on GPIO19 (RX) and GPIO20 (TX), through the board's USB (Universal Serial Bus)/CAN multiplexer |
| Coprocessor | XL2515 (MCP2515-compatible) on `spi1`: SCK GP10, MOSI GP11, MISO GP12, CS GP9, INT GP8; SPI (Serial Peripheral Interface) clock 10 MHz |
| Transceiver | SIT65HVD230 (3.3 V) on the coprocessor module |
| Termination | 120 Ω at both ends |
| Crystal | the XL2515 must have a 16 MHz crystal; 8 MHz limits the controller to 500 kbit/s |

Selecting CAN removes the panel's native USB. GPIO19 and GPIO20 carry both, and
the FSUSB42UMX multiplexer (CH422G EXIO5: 0 = USB, 1 = CAN) selects one. The
console is therefore on UART0, the board's second USB-C socket behind the
USB-UART bridge, with USB-Serial-JTAG (the ESP32-S3's built-in USB serial and
debug bridge) as secondary.

If frames do not cross: [Bringing up the link](Bringup.md).

## Failure behaviour

The coprocessor fills failsafe values after 200 ms without a request; the panel
escalates after 1 s without an answer. The failsafe latches. Traffic returning
stops the silence counter but does not lift the failsafe; leaving it takes a
write of 0x5AFE to the CLEAR register of the control page.

## Protocol

Pages of up to 32 sixteen-bit registers, read and written in windows. The
coprocessor transmits only in answer to a request. Protocol version 2.3. The
major version is register 0 of page 0; the panel refuses to arm when it differs
from its own.

### Identifier

A 29-bit extended identifier carries the whole address, so a read is a frame
with no payload.

| Bits | Field | Width | Values |
| --- | --- | ---: | --- |
| 28..26 | priority | 3 | 0 control, 1 normal, 2 bulk (reserved); lower wins arbitration |
| 25..22 | op | 4 | 1 READ, 2 WRITE, 3 DATA, 4 ACK (acknowledge), 5 NACK (negative acknowledge) |
| 21..14 | page | 8 | page map below |
| 13..6 | offset | 8 | first register in this frame |
| 5..0 | count | 6 | registers in this frame, 0..32 |

Priority is derived from the page: the control, limits and failsafe pages and
their acknowledgements are class 0; everything else is class 1.

### Frames

A frame carries up to four registers (8 bytes, little-endian). Each frame
carries its own offset and count, so a reply wider than four registers is
several independent frames in any order, and a dropped frame costs one register
range. The host poller tracks the window it asked for and completes when every
register has arrived; the transport does no reassembly. There is no CRC (cyclic
redundancy check) in the payload; CAN's 15-bit CRC, acknowledge slot and
retransmission apply.

A NACK carries its reason in register 0:

| Value | Reason |
| ---: | --- |
| 1 | BAD_PAGE |
| 2 | BAD_RANGE: offset + count past the end of the page |
| 3 | READ_ONLY |
| 4 | BAD_VALUE |
| 5 | NOT_ARMED |

### Page map

| Page | Name | Access | Registers |
| ---: | --- | --- | --- |
| 0x00 | IDENTITY | read | protocol major, protocol minor, firmware major, minor, patch, hardware revision, capabilities bitmap |
| 0x01 | STATUS | read | state (0 idle, 1 armed, 2 failsafe), faults bitmap, uptime in ms (two registers), requests accepted (two registers), XL2515 receive error counter, XL2515 transmit error counter |
| 0x10 | CONTROL | read, write | ARM (non-zero arms), THROTTLE (0..10000, hundredths of a percent), CLEAR (write 0x5AFE to leave failsafe), MOTOR_POLES |
| 0x11 | LIMITS | | declared, not served |
| 0x12 | FAILSAFE | | declared, not served |
| 0x13 | CHANNELS | read, write | one command per output channel, 0..1000 of the channel's travel; eight channels |
| 0x20 | BENCH | read | voltage (10 mV), current (10 mA), power (W), rpm (revolutions per minute), ESC (electronic speed controller) temperature and motor temperature (0.1 °C, signed), charge (mAh), energy (0.1 Wh), minimum voltage, maximum current, maximum power, maximum rpm, flags |
| 0x21 | reserved | | not assigned; not to be reused |
| 0x22 | OUTPUTS | read, write | per slot: driver (0 none, 1 PWM (pulse-width modulation), 2 PPM (pulse-position modulation), 3 DShot, 4 bidirectional DShot), pin, first channel and channel count in one register, rate in Hz (kbit/s for both DShot drivers); eight slots of four registers |
| 0x23 | CHAN_CFG | read, write | per channel: role (0 throttle, 1 surface), slew (span per second, 0 = immediate), minimum and maximum pulse in µs; eight channels of four registers |
| 0x24 | CATALOGUE | read | the board's own pins, one register each: GPIO (general-purpose input/output) number in 6 bits, the pad number printed beside it in 6, what holds it in 4 (0 free, 1 heartbeat, 2 CAN, 3 flash, 4 debug, 5 sensor, 15 other); 32 slots, and a pad number of 0 means there is no pin in that slot |
| 0x25 | SHAPE | read | where those pads are: outline width and height in 0.01 mm, the corner pad 1 sits at and the pads in one row packed as (corner << 8) \| per side, the pitch in 0.01 mm, and the distance from the edge to the centre of a pad row. Two rows on one pitch, numbered away from pad 1 along its edge and back along the opposite one. All zero when the coprocessor has no shape for its board, which means it is listed and not drawn |

Faults bitmap: bit 0 link silent, bit 1 overcurrent, bit 2 over-temperature,
bit 3 stall, bit 4 heartbeat stopped, bit 5 protocol version mismatch. Faults
are sticky until read and cleared.

Capabilities bitmap: bit 0 ESC drive, bit 1 ESC telemetry, bit 2 servo PWM, bit
3 servo current sense, bit 4 pack sense, bit 5 receiver bus, bit 6 vibration
sensor and index pulse, bit 7 cell monitor, bit 8 programming. The panel
derives the menu marks from it.

BENCH flags: bit 0 voltage valid, bit 1 current valid, bit 2 rpm valid, bit 3
temperature valid, bit 7 simulated. A coprocessor without a measurement front
end sets bit 7, and the panel draws SIMULATION across the screen.

MOTOR_POLES is the magnet count of the motor under test, even and between 2 and
42, or zero meaning nobody has said. A bidirectional DShot ESC reports
electrical periods and has no idea what it is bolted to, so this is the one
number the wire has to carry for the coprocessor to report a mechanical speed;
at zero it reports no speed rather than one derived from a guess. The panel
sends it from the `Motor poles` setting when the coprocessor answers.

The coprocessor refuses a pin it must not drive -- the safety line, the CAN
controller's pins, and any number above the last GPIO the part has -- and a
slot it refuses is left unbound while the page still reads back what was asked
for. [DShot and the output drivers](DShot.md) has the rest.

CHAN_CFG and OUTPUTS entries are written whole, four registers at a time. A
channel's pulse range defaults to 1000..2000 µs; endpoints outside 500..2500 µs
are refused with BAD_VALUE. A command outside its range is clamped. Two slots
on one pin, or two slots rendering the same channel, are refused. Arming is
decided by the coprocessor: a write of ARM is refused with NOT_ARMED while the
link is in failsafe or the heartbeat is not trusted.

### Bit timing

`shared/can/can_timing.c` solves the segmentation for each controller's clock
and requires the bit rate to come out exact. Both ends sample at 75% of the
bit:

| Controller | Clock | Prescaler | Quanta per bit | tseg1 | tseg2 | sjw |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| TWAI (ESP32-S3) | 80 MHz APB (advanced peripheral bus) | 4 | 20 | 14 | 5 | 4 |
| XL2515 | 16 MHz crystal, halved internally | 1 | 8 | 5 | 2 | 2 |

Eight quanta is the minimum a bit may have. That is why the XL2515 needs a 16
MHz crystal for 1 Mbit/s and why its sample point is fixed at 75%; the TWAI
timing is chosen to match. `test_can_timing` pins both.

### Budget

A bench-page poll is one request frame and four data frames (13 registers). At
20 Hz that is 1.55% of the bus. Worst-case classic CAN payload at 1 Mbit/s with
29-bit identifiers and full bit stuffing is about 52 kB/s; the expected traffic
is 12 to 30 kB/s.

### Tests

The identifier layout is checked bit by bit across the 29-bit space; the timing
solver is pinned to hand-checked examples; `test_link_loopback` runs the host
poller against the device dispatcher over a bus that drops, delays and reorders
frames: split replies arriving in reverse, refused writes, lost pieces leaving
a request unanswered rather than half-answered, and the device watchdog firing
on a quiet bus.
