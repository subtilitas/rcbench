# exbus

The master side of EX Bus, the serial bus of JETI receivers and devices.
The bench sends channel values to one device and reads the device's EX
telemetry. It never answers as a device. The core holds the packet
encoder, the reply check, the EX telemetry decoder, the cycle scheduler and
the arithmetic of the pin driver. The pin driver puts the bus on one RP2350
pin.

| Part | Files | Depends on |
|---|---|---|
| Core | `exbus_wire.c`, `exbus_telem.c`, `exbus_sched.c`, `exbus_pio.c`, 4 headers under `include/` | C11 standard headers |
| RP2350 pin driver | `rp2350/exbus_line.pio`, `rp2350/exbus_line.c`, `rp2350/exbus_line.h` | the core; pico-sdk `hardware_pio`, `hardware_dma`, `hardware_gpio`, `hardware_clocks`, `hardware_timer` |

A driver with 3 functions (`exbus_driver_t`) connects the scheduler to a
pin; `rp2350/` holds the one for the RP2350. The module is beta.

Not part of the module: the JETIBOX menu and the configuration of a device
over the bus. The encoder knows the JETIBOX data identifier only to name
it; the scheduler sends no such request.

## Constraints

- Plain C11. Every header has `extern "C"` guards and compiles by itself as
  C11 and as C++17 (`tools/check_protocols.py`). `test/host/exbus_headers.cpp`
  calls each of the 4 core parts from C++ and links against the C library.
- Includes `<stdint.h>`, `<stdbool.h>`, `<stddef.h>` and `<string.h>` and
  no other file outside this folder.
- No heap, no floating point, no global or static mutable state. Two
  schedulers on two drivers run side by side.
- Sizes with arm-none-eabi-gcc 14.2 (`-mcpu=cortex-m33 -mthumb -Os`), and
  with gcc on x86-64 where it differs:

  | Object | Bytes |
  | --- | --- |
  | `exbus_sched_t`, a scheduler with its capture and last reply | 500 (520) |
  | `exbus_capture_t` | 196 |
  | `exbus_reply_t` | 32 |
  | `exbus_tm_packet_t`, one decoded block | 288 |
  | `exbus_tm_table_t`, the sensors of one device | 2492 |
  | `exbus_line_t`, one RP2350 line | 520 (528) |

- Code with the same compiler and options: `exbus_wire.c` 566 bytes,
  `exbus_telem.c` 1635, `exbus_sched.c` 1178, `exbus_pio.c` 750. The
  largest stack frame is `exbus_sched_step()` with 120 bytes
  (`-fstack-usage`); no function recurses. The stack of a whole call chain
  in the firmware image is not measured: the image does not call the module
  yet.
- Non-blocking: `exbus_sched_step()` polls the driver, starts the next
  cycle when it is due and returns. It never waits.

## Modules

| Header | Contents |
| --- | --- |
| `exbus_wire.h` | the channel packet (1 to 24 channels, 1/8 us a count), the telemetry request, CRC-16, the reply check with 11 result codes, the capture a driver fills |
| `exbus_telem.h` | the EX telemetry block: CRC8, text, data and message packets, data types 0 to 15 with sign and decimal point, a table of one device's sensors, a value as text |
| `exbus_sched.h` | the master's cycle: configuration and its check, a channel packet every period, a request every nth cycle, the reply check, statistics, silence on stop |
| `exbus_pio.h` | the arithmetic of the pin driver, without hardware access: the clock divider, a transmission as the words the PIO (programmable input/output) program reads, the program's words as a capture |

## Wire

From S1 unless marked otherwise.

| Quantity | Value |
| --- | --- |
| Line | one signal wire, half duplex, UART (universal asynchronous receiver-transmitter) levels, idle high |
| Rate | 125 kbaud or 250 kbaud; the master keeps one rate, the device finds it |
| Frame | 8N1: start bit, 8 data bits least significant first, stop bit; 80 us a byte at 125 kbaud, 40 us at 250 kbaud |
| Levels | logic 0 at most 1.5 V, logic 1 at least 3.0 V |
| Packet | header 2 bytes, LEN (whole packet, CRC included), Packet_ID, DATA_ID, SUB_LEN, data, CRC-16 low byte first |
| CRC-16 | CRC-16/KERMIT: polynomial 0x1021 reflected, initial value 0, no final XOR, from the first header byte |
| Channel value | pulse width in us times 8, 2 bytes little-endian |
| Release | after a packet whose second header byte is 0x01 the master releases the line for at least 4 ms; 0x03 means no release, and a packet with 0x01 follows |
| Reply | header 0x3B 0x01, the request's Packet_ID, DATA_ID 0x3A, an EX telemetry block of at most 28 bytes (S2); 8 to 36 bytes |

The bench sends one of two cycles per period:

| Cycle | Bytes on the wire | Then |
| --- | --- | --- |
| Channels | `3E 01 LEN ID 31 SUB_LEN` + 2 bytes a channel + CRC | release |
| Channels and request | `3E 03 LEN ID 31 SUB_LEN` + channels + CRC, then `3D 01 08 ID 3A 00` + CRC, same Packet_ID | release, the reply in it |

The channel packet carries no failsafe flag. The master signals a loss of
channel values by sending nothing. The scheduler therefore sends only while
it runs and holds channel values younger than `hold_us` (100 ms by
default); `exbus_sched_stop()` cuts a transaction on the line and sends
nothing more.

Chosen by this module, not stated in S1:

| Quantity | Value | Reason |
| --- | --- | --- |
| Channel count | 1 to 24, 16 by default | S1's example carries 16; a JETI receiver manual (S4) names 24 |
| Channel range | 400 us to 2500 us (3200 to 20000 counts), clamped | the range of the bench's own pulse outputs |
| Period | 10 ms by default, 5 ms to 30 ms | 10 ms (100 Hz) is the rate S7 relays from JETI; S1 states none |
| Window | 4 ms by default, 4 ms to 20 ms | S1's minimum release |
| Quiet time | 20 bits after a received byte ends the release | S1 states no gap between the bytes of a reply |
| Packet_ID | counts up by 1 a cycle, wraps from 255 to 0 | S1 says the device copies it; it does not say how the master picks it |

A cycle has to fit its period with 500 us to spare: 24 channels and a
request at 125 kbaud are 64 bytes, 5120 us, and with the 4 ms window 9120
us. The configuration check refuses a cycle that does not fit
(`EXBUS_CFG_ERR_FIT`).

## Driver interface

```c
typedef struct {
    void *ctx;
    bool (*start)(void *ctx, const uint8_t *tx, unsigned n,
                  uint32_t window_us);
    bool (*poll)(void *ctx, exbus_capture_t *out);
    void (*abort)(void *ctx);
} exbus_driver_t;
```

- Outside a transaction the driver drives the line high.
- `start()` sends `n` bytes as 8N1, releases the line right after the last
  stop bit (an input with a pull-up) and receives bytes until the line has
  been high for `window_us` before the first byte, or for 20 bits after a
  byte. Then it drives the line high again. It returns at once, `false`
  when it cannot start.
- `poll()` returns `false` while the transaction runs and `true` once after
  it, with the capture filled: up to 48 bytes, each with its stop bit and
  the time of its start bit in us after the release, the release's length
  and an overflow mark.
- `abort()` ends a transaction at once and leaves the line driven high.
- The scheduler cuts a transaction the driver has not ended 5 ms after the
  longest it can take (`EXBUS_EV_CUT`): the transmission, the window, and
  49 bytes each followed by a gap of 20 bits.

## Use

```c
exbus_cfg_t cfg;
exbus_sched_t s;
exbus_tm_table_t table;
uint16_t ch[16];

exbus_cfg_default(&cfg);              /* 125 kbaud, 10 ms, 16 channels */
exbus_sched_init(&s, &driver, &cfg);
exbus_tm_table_init(&table);

for (unsigned i = 0; i < 16; ++i) {
    ch[i] = exbus_us_to_counts(1500);
}
exbus_sched_set_channels(&s, ch, 16, now_us());
exbus_sched_start(&s, now_us());      /* only while the bench is ARMED */

for (;;) {                            /* at least once a millisecond */
    const unsigned ev = exbus_sched_step(&s, now_us());

    if (ev & EXBUS_EV_REPLY) {
        const exbus_reply_t *r = exbus_sched_reply(&s);
        exbus_tm_packet_t p;

        if (r->block_len != 0
            && exbus_tm_parse(r->block, r->block_len, &p) == EXBUS_TM_OK) {
            exbus_tm_table_update(&table, &p, now_us());
        }
    }
}

exbus_sched_stop(&s);                 /* on STOP and on every switch to SAFE */
```

`exbus_sched_step()` returns event bits: a cycle sent, a reply that passed,
no reply, a refused reply (`exbus_sched_last_rx()` says why), bytes after a
cycle without a request, stale channel values (the stream stops), a cut
transaction, a refused start. `exbus_stats_t` counts each of them, the
refused replies per reason.

The reply check runs in this order: header 0x3B 0x01 (bytes before it are
skipped and counted), LEN against SUB_LEN and the 36-byte limit, CRC-16,
DATA_ID, Packet_ID. The scheduler adds 2 codes from the capture: a reply
whose first byte starts after the window is late (`EXBUS_RX_LATE`), and a
refused reply with a low stop bit in it is a framing error
(`EXBUS_RX_FRAMING`).

## EX telemetry

From S2, section 6 ("Protocol EX"). A block is the EX mark (low nibble
0xF), a byte with the packet kind in 2 bits and the length in 6, the serial
number (manufacturer and device, 2 bytes each, little-endian), a reserved
byte, the items and a CRC8 (polynomial 0x07, initial value 0, from the
kind and length byte).

| Kind | Content | Decoded to |
| --- | --- | --- |
| 0 text | identifier, label length (5 bits) and unit length (3 bits), label, unit | `label`, `unit`; identifier 0 names the device |
| 1 data | items of identifier (4 bits, or a whole byte after 0) and data type (4 bits), then the value | up to 10 values |
| 2 message | message type, class (3 bits) and length (5 bits), the text | `message`, `msg_class` 0 to 7 |

| Data type | Bytes | Value |
| --- | --- | --- |
| 0, 1, 4, 8 | 1, 2, 3, 4 | number: sign bit and 2 bits of decimal point (0 to 3) in the top of the last byte, the magnitude below |
| 5 | 3 | time (hours, minutes, seconds) or date (year, month, day), by the lowest decimal-point bit |
| 9 | 4 | coordinate, latitude or longitude, with its hemisphere; the 29 value bits are kept as sent, S2 does not give their encoding |
| 2, 3 | 2 | reserved: kept as raw bytes, the parser reads on |
| 6, 7 | 3 | reserved, the same |
| 10, 11 | 4 | reserved, the same |
| 12 to 15 | 5 | reserved, the same |

`exbus_tm_table_t` keeps one device: up to 32 identifiers with label, unit
and last value, and the last message. Labels arrive over several replies,
so an identifier may have a value before it has a label. A block from
another serial number clears the table. `exbus_tm_format()` writes a value
as text: `-12.34`, `12:05:09`, `2026-10-15`, `N raw 1234`, `raw 0A 1B`.

## Electrical

S1 joins the master's transmit and receive pins with 2.4 kOhm on one wire.
The RP2350 driver does both on one pin, so the resistor has no place. The
pin drives the line at 3.3 V, above S1's 3.0 V minimum for logic 1.

| Master | Rating of its pin | On the wire |
| --- | --- | --- |
| RP2350 GPIO0 to 39, IOVDD (input/output supply) at 3.3 V | 5.5 V | no level shifter, no series resistor |
| The same pin, IOVDD at 0 V | 3.63 V | a device that drives the line at 5 V exceeds the rating |

The device's output stage (push-pull or open drain), its high level and
what it drives while the bench is unpowered are not stated in S1 and not
measured.

## RP2350 pin driver

`rp2350/exbus_line.c` implements `exbus_driver_t` on one pin. Its header
includes no pico-sdk header and has `extern "C"` guards.

| Resource | Use |
| --- | --- |
| PIO state machines | 1 per open line, in a PIO block that reaches the pin |
| PIO program space | 23 of 32 words; lines in one block share the program |
| DMA (direct memory access) channels | 2 per open line: the words of a transmission and the bytes of its release |
| Interrupts, PIO interrupt flags | none |
| Memory | 1 `exbus_line_t` per line, held by the caller; no static state |

`exbus_line_open()` claims the resources and `exbus_line_close()` gives all
of them back. An open fails with its own code for a pin the chip does not
have, a system clock that gives no divider from 1 to 65535, no free state
machine or program space, and fewer than 2 free DMA channels; a failed
open claims nothing and leaves the pin as it was.

| Quantity | 125 kbaud | 250 kbaud |
| --- | --- | --- |
| Clock divider at 150 MHz | 150, exact | 75, exact |
| State machine cycle | 1 us | 0.5 us |
| Bit | 8 cycles, 8 us | 8 cycles, 4 us |
| Resolution of a byte's time | 2 cycles, 2 us | 2 cycles, 1 us |
| Release without a byte, 4 ms window | 4001 us | 4001 us |
| Release after the last byte received, device at the nominal rate | 161 to 162 us after its stop bit | 80.5 to 81 us after its stop bit |
| Device rate the receiver takes | 2.5 % fast to 3 % slow | the same |

The rate is the system clock over 8 times the rate, rounded to 1/256 of the
divider: at most 0.2 % off at any system clock. The receiver resynchronises
on each start bit. A byte takes the program 78 cycles from the sample that
saw its start bit back to the watch loop, against 80 cycles on the wire: a
device up to 2.5 % fast loses no time over a reply. A device 3 % slow still
has every sample inside its bit. The host suite holds both bounds at 16
phases of the device's clock.

While a line is open and no transaction runs, the pad drives high. The
pull-up is on from the open on and holds the line while the pad is an
input. The internal pull-up is 50 to 80 kOhm; its rise time against the
device's input capacitance is not measured. The close leaves the pad an
input with its pull-down on.

Limits:

- After 20 bits of high line following a byte, the pad drives high again. A
  device that pauses 20 bits or longer inside a reply and then sends on
  drives against the pad, and the reply is cut at the pause. S1 states no
  gap; a device's gap is not measured.
- A reply that runs past the period delays the next cycle. The scheduler
  counts a cycle that starts a period or more late and restarts the
  schedule from it.
- The system clock must not change while a line is open.
- Every function of one line is called from one context.

```c
static exbus_line_t line;
exbus_sched_t s;

exbus_line_init(&line);
if (exbus_line_open(&line, 13, EXBUS_BAUD_LS) == EXBUS_LINE_OK) {
    const exbus_driver_t driver = exbus_line_driver(&line);  /* pad high */

    exbus_sched_init(&s, &driver, &cfg);
}
exbus_line_close(&line);                  /* an input, pulled down */
```

Overlap with other programs of this repository: the half-duplex pattern
(send, release, receive, drive again) is the one of
`protocols/kst/rp2350/kst_line.pio`, and the receive loop of 8 cycles a bit
(`in pins, 1` and a `jmp` with 6 cycles of delay) is the pattern of the
pico-examples' UART receiver that `protocols/pdmini/rp2350/pd_uart.pio`
carries under BSD-3-Clause. `exbus_line.pio` is written for this module
and is MIT like the rest of it. No half-duplex UART program is shared
between the bus modules.

The host suite assembles `exbus_line.pio` with its own assembler for the
instructions the program uses, runs the words in a model of the state
machine and holds them to `test/host/fixtures/exbus_line_words.txt`
(`test_exbus_pio`). The firmware build of this repository compares the
words `pioasm` produced with that file and fails on a difference.

## Use in another pico-sdk project

```cmake
add_subdirectory(protocols/exbus)
target_link_libraries(my_target PRIVATE rcbench_exbus rcbench_exbus_rp2350)
```

`rcbench_exbus` is the core. `rcbench_exbus_rp2350` exists when the
pico-sdk is initialised before the `add_subdirectory()`; it is an INTERFACE
library, so `exbus_line.c` compiles in the target that links it. A project
on another chip links `rcbench_exbus` and supplies its own
`exbus_driver_t`. Nothing in the folder names a file outside it.

The caller's part: stop every other use of the pin before
`exbus_line_open()`, run the stream only while the bench is ARMED, and call
`exbus_sched_stop()` on STOP and on every switch to SAFE.

## Sources

| | Document | Used for |
| --- | --- | --- |
| S1 | "EX Bus communication protocol", JETI model, version 1.21 of 2013-12-18 | the line, the rates, the release, the packets, the CRC-16, the examples on pages 6 to 7 |
| S2 | "JETI Telemetry communication protocol", JETI model, version 1.07 of 2015-01-14, section 6 "Protocol EX" | the EX telemetry block, the data types, text and message packets, the CRC8 |
| S4 | Duplex REX Assist receivers manual, JETI model, 2023-02 | 24 channels on EX Bus |
| S7 | Spirit System forum, thread on the EX Bus frame rate, 2015-09 | 100 Hz as the transmitter's setting |

The facts are written here in this module's own words; no table and no code
of S1 or S2 is reproduced. The test vectors are the example packets of S1
and S2 and packets computed with an independent script. No GPL (GNU
General Public License) source was read for this module.

## What is measured and what is not

No byte of this module has been on a wire to a device. The tests run
against the packets of S1 and S2 and against the state machine model in
`test/host/test_exbus_pio.c`. Not measured:

- The delay from the request to a device's reply, the reply's length, the
  gaps between its bytes and whether it echoes the Packet_ID.
- Whether a device acts on channel packets without a request, and on
  every Packet_ID sequence.
- The channel count and range a device reads, and what it does with a
  value outside it.
- A device's reaction to silence: the time to its failsafe and what it
  holds.
- The line at each direction change: the glitch, the pull-up's rise time,
  and a device that drives while the pad drives.
- Real EX telemetry blocks against S2: text, data and message packets of a
  device.

## Trademarks

JETI and EX Bus are names or trademarks of their respective owner; they are
used here only to say which receivers and devices this module works with.
The project is not affiliated with JETI model s.r.o. The module is written
from the published specifications S1 and S2; no vendor code is included.
The full notice is in the repository's
[README](../../README.md#legal-notice-and-disclaimer) and in the section
"Trademarks" of [NOTICE](../../NOTICE).

## Tests

`test/host/test_exbus_wire.c`, `test_exbus_telem.c`, `test_exbus_sched.c`
and `test_exbus_pio.c`, 75 cases. The table in `STATUS.md` has the line
coverage per file. The pin driver's C file is not in the host build.

```sh
cmake -S test/host -B test/host/build
cmake --build test/host/build
ctest --test-dir test/host/build -R exbus
```
