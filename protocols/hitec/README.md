# hitec

The signal-wire programming protocols of Hitec servos, wire layer only. The
core holds the frames of both wire families, the transactions a driver
runs, the reply decoders and the arithmetic of the PIO (programmable
input/output) pin driver. The pin driver puts the wire on one RP2350 pin.
There is no session, no register model and no caller in this repository
yet.

| Part | Files | Depends on |
|---|---|---|
| Core | `hitec_wire.c`, `hitec_pio.c`, 2 headers under `include/` | C11 standard headers |
| RP2350 pin driver | `rp2350/hitec_line.pio`, `rp2350/hitec_line.c`, `rp2350/hitec_line.h` | the core; pico-sdk `hardware_pio`, `hardware_dma`, `hardware_gpio`, `hardware_clocks`, `hardware_timer` |

The wire timings below are measured on an HS-5645MG, an HS-7955TG and a
D485HW with the HPP-21 Plus and HFP-30 programmers, at a 1.4 V threshold.
No byte of this code has been on a wire.

## Constraints

- Plain C11. Every header has `extern "C"` guards and compiles by itself as
  C11 and as C++17 (`tools/check_protocols.py`).
  `test/host/hitec_headers.cpp` calls both parts from C++11 and links
  against the C library.
- Includes `<stdint.h>`, `<stdbool.h>`, `<stddef.h>` and `<string.h>` and
  no other file outside this folder.
- No heap, no floating point, no global or static mutable state. Two lines
  on two drivers run side by side.
- A transaction (`hitec_xfer_t`) is 28 bytes, a capture
  (`hitec_capture_t`) 140 bytes, a clock 16 bytes and a run 20 bytes, with
  arm-none-eabi-gcc 14.2 (`-mcpu=cortex-m33 -mthumb -Os`) and with gcc on
  x86-64. A line (`hitec_line_t`) is 172 bytes with arm-none-eabi-gcc 14.2.
- Non-blocking: `start()` and `poll()` return at once. Nothing waits.

## Wire

| | Classic: HS-5xxx, HS-7xxx | D series |
|---|---|---|
| Bit rate | 19 200 bit/s, 52.08 us a bit | 115 200 bit/s, 8.68 us a bit |
| Format | 8N1, least significant bit first | 8N1, least significant bit first |
| Polarity | normal: the line idles high, a start bit is low | inverted: inside a frame the line rests low, a start bit is high |
| High level | made by the servo; with no servo the line is low | made by a pull-up; the servo has a 3 kOhm pull-down (reported with another programmer, not measured here) |
| Master frame | 4 bytes `cmd p1 p2 sum`; the 4 bytes sum to 0 modulo 256 | write 7 bytes `96 ID addr 02 lo hi sum`, read 5 bytes `96 ID addr 00 sum`; `sum` is the low byte of the sum of every byte after the header |
| Reply | 2 bytes: value and `+`, `+ +`, or position high and low | 7 bytes `69 ID addr 02 lo hi sum`; a write is not answered |
| Reply start | 508 to 517 us after the start of the master's last byte after reads, up to 614 us after writes and after the first read of a session: inside the master's stop bit | 5.9 to 6.5 ms after the end of the request |
| Reply bytes | 516 to 528 us start to start (10 bits are 520.8 us) | back to back, 86 to 87 us start to start (86.8 us) |
| Master bytes | 572 us start to start (HPP-21 Plus), 521 us (HFP-30) | back to back |

Classic command bytes: `0x61` read EEPROM (address; value, `+`), `0x62`
write EEPROM (address, value; `+ +`), `0x64` write data memory (`+ +`),
`0x65` read position (high, low), `0x66` move (target high, low; `+ +`),
`0x67` the first command of a session (value, `+`). `0x63`, read data
memory, and the reply `-` (0x2D) to a bad sum are described for the
HS-5xxx and not seen on the wire.

On the D485HW the line also shows:

- After a request the line is high for 0.02 to 0.49 ms, then low until the
  answer starts.
- In 37 of 37 answers checked in 3 captures a byte 0x00 follows the 7
  bytes of the answer back to back. The line then stays low for 110 to
  190 us and goes high.
- From a high line the HFP-30 drives the line low 88 us before the first
  start bit of a request.

## Transactions

A driver runs one `hitec_xfer_t` at a time:

| Kind | What the driver does |
|---|---|
| `HITEC_XFER_SEND` | the line at its idle level for `lead_us`, 1 to 8 bytes with `gap_bits` (0 to 6) idle bit times between two of them, then the receiver |
| `HITEC_XFER_LISTEN` | the receiver at once; with `answer` set, `answer_byte` goes out at the end of the stop bit of the first byte received |
| `HITEC_XFER_LEVEL` | the line held low, or high on a push-pull line, for `level_us`, then released |

The receiver runs until `window_us` of waiting for a start bit have passed
or `max_rx` bytes have come. Time inside bytes and in the answer does not
count against the window. Windows, leads and levels are 4 s or shorter.

| Builder | Bytes | Gap | Lead | Window | Bytes received |
|---|---|---|---|---|---|
| `hitec_c_xfer()` | 4 | 1 bit time: 572.9 us start to start | none | 1.5 ms | 2 |
| `hitec_d_read_xfer()` | 5 | 0 | 88 us driven low | 25 ms | 8: the answer and the byte 0x00 |
| `hitec_d_write_xfer()` | 7 | 0 | 88 us driven low | none | none |
| `hitec_listen_xfer()` | none | | | as given | as given |
| `hitec_level_xfer()` | none | | | | |

Where the receiver starts, which is time 0 of a capture:

- Classic SEND: the start of the last byte's stop bit, 9 bit times
  (468.75 us) after the start of that byte. The servo's first start bit
  falls inside this stop bit, so the master does not send it.
- D series SEND: the last stop bit is sent, then one released bit time
  (`HITEC_D_SETTLE_BITS`), then the receiver starts.
- LISTEN: the start of the transaction.

A capture holds up to 16 bytes, each with its start bit's time in ns from
time 0 and a flag for a stop bit sampled at the start level. It also says
whether the count was reached, the answer went out, a stamp was not
consistent or the driver ended a stuck receiver (`overflow`), and whether
the transaction was cut (`aborted`).

## Decoders

`hitec_c_decode()` takes a classic reply with one of 3 expectations
(`hitec_c_expect_of()` gives the one of a command byte),
`hitec_d_decode()` a D series answer to a read of one address,
`hitec_listen_decode()` a listen for one byte, such as the `+` of the
classic entry. Every failed check has its own code:

| Code | Name | Condition |
|---|---|---|
| 1 | `HITEC_RX_ERR_ARG` | NULL, a capture of another family, an unknown expectation |
| 2 | `HITEC_RX_ERR_ABORTED` | the transaction was cut |
| 3 | `HITEC_RX_ERR_OVERFLOW` | the capture is not whole |
| 4 | `HITEC_RX_ERR_SILENT` | no byte |
| 5 | `HITEC_RX_ERR_EARLY` | first byte before the accept window |
| 6 | `HITEC_RX_ERR_LATE` | first byte after it |
| 7 | `HITEC_RX_ERR_NAK` | classic: `-` and nothing after it |
| 8 | `HITEC_RX_ERR_LENGTH` | fewer bytes than the reply has; classic: also more |
| 9 | `HITEC_RX_ERR_FRAMING` | a byte of the reply with a bad stop bit |
| 10 | `HITEC_RX_ERR_GAP` | a byte of the reply less than 9.5 or more than 15 bit times after the start of the one before |
| 11 | `HITEC_RX_ERR_ACK` | classic: no `+` where the reply has one; listen: another byte |
| 12 | `HITEC_RX_ERR_HEADER` | D: not 0x69, or a length byte other than 2 |
| 13 | `HITEC_RX_ERR_ADDRESS` | D: the answer of another register |
| 14 | `HITEC_RX_ERR_SUM` | D: the sum does not fit |

| Accept window of the first byte | From | Accepted | Measured |
|---|---|---|---|
| Classic | the start of the master's last byte | 494.8 us (the middle of the master's stop bit) to 1 ms | 508 to 614 us |
| D series | the end of the request's last stop bit | 2 ms to 20 ms | 5.9 to 6.5 ms; 15.2 ms is reported with another programmer |

The D decoder checks the first 7 bytes and no byte after them: the byte
0x00 the D485HW sends after its answer, and the release of the line,
which reads as a byte with a bad stop bit. It reports the ID byte and does
not check it: it reads 0x00 in every answer on the bench, and 0xFF and
0xFE are reported from a servo with ID 0.

## Driver interface

```c
typedef struct {
    void *ctx;
    bool (*start)(void *ctx, const hitec_xfer_t *xfer);
    bool (*poll)(void *ctx, hitec_capture_t *out);
    uint32_t (*now_us)(void *ctx);
} hitec_driver_t;
```

- `start()` begins the transaction and returns at once; `false` when the
  line is not open, a transaction runs, the transaction is not valid
  (`hitec_xfer_valid()`), it is of another family, or it asks for a level
  the drive does not make.
- `poll()` returns `false` while the transaction runs. After it ends it
  fills the capture and returns `true` once.
- `now_us()` is a free-running microsecond clock. It may wrap;
  `hitec_elapsed_us()` and `hitec_due()` compare times across the wrap.

There is no delay function.

## PIO program

`hitec_pio.h` computes the divider for a system clock, lays a transaction
out as the words `rp2350/hitec_line.pio` reads and turns the words it
pushes into a capture, without hardware access.

One program runs both families. The clock divider makes a bit 16 state
machine cycles. For the D series the pad's input override inverts the pin,
so the program reads the idle level as 1 in both families. On the output
side the program sends each bit time as 2 bits of its stream, the pad's
direction and its level: an open-drain line keeps the level low and
switches the direction, a push-pull line keeps the direction and switches
the level. One output override cannot give both, which is why the
direction and the level come from the stream and the output overrides are
not used.

| Quantity | Value |
|---|---|
| Bit | 16 cycles |
| Receiver tick, the resolution of the window | 2 cycles, 1/8 bit |
| Start bit time in a capture | within 1 cycle (1/16 bit) of the edge, and the input synchroniser's 2 system clock cycles |
| Data bit samples | 23 to 25 cycles after the start bit's edge plus 16 per bit: 1.44 to 1.56 bit times into each bit |
| Stop bit sample | 151 to 153 cycles after the edge, 9.44 to 9.56 bit times |
| Answer | an open-drain line changes the direction for its start bit 160 cycles after the sample that saw the first byte's start bit: 10.0 to 10.13 bit times after that edge, 520.8 to 527.3 us at 19 200 bit/s. The programmers send theirs 0.52 to 0.55 ms after it. A push-pull line changes the level 1 cycle later |
| Lead before the first start bit | the transaction's lead, rounded up to 2 cycles, or 8 cycles plus the bit times that pad the front of the stream to a word, whichever is longer: a D read is driven low 117.2 us, a D write 89.0 us |
| Window end to idle | 5 cycles |
| Push-pull | the level moves 1 cycle after the direction would: the bits on the wire sit 1 cycle late, and the release cuts the last stop bit to 15 cycles |

A receiver that samples once in the middle of each bit takes a sender
whose bit time is 4.4 % shorter to 4.9 % longer than its own: the stop bit
sample at 9.44 to 9.56 bit times has to fall inside the sender's tenth bit.

## RP2350 pin driver

`rp2350/hitec_line.c` implements `hitec_driver_t` on one pin. Its header
includes no pico-sdk header and has `extern "C"` guards.
`hitec_line_open(line, pin, family, drive)` takes the family and the drive:
classic is open drain only; the D series is open drain, as Hitec's
reference circuit, or push-pull.

| Resource | Use |
|---|---|
| PIO state machines | 1 per open line, in a PIO block that reaches the pin |
| PIO program space | 27 of 32 words; lines in one block share the program |
| DMA (direct memory access) channels | 2 per open line: the words of a transaction and the received bytes |
| Interrupts, PIO interrupt flags | none |
| Memory | 1 `hitec_line_t` per line, held by the caller; no static state |

`hitec_line_open()` claims the resources and `hitec_line_close()` gives all
of them back. An open fails with its own code for a pin the chip does not
have, an unknown family or drive, push-pull for classic, a clock the open
refuses, no free state machine or program space, and fewer than 2 free DMA
channels; a failed open claims nothing and leaves the pin as it was.

The IOmcu image of this repository compiles the driver under its `-Werror`
and holds `hitec_line.pio` to `test/host/fixtures/hitec_line_words.txt`.
Nothing in the image calls the driver yet, so the linker drops it: the
image is 190 200 bytes with and without it.

### Clock

| Family | Divider at 150 MHz | Bit time | Lowest system clock |
|---|---|---|---|
| Classic | 488 + 72/256, exact | 52.083 us | 1.2282 MHz |
| D series | 81 + 97/256, 0.002 % short | 8.680 us | 7.3692 MHz |

The lowest clock is 64 bit rates less half a step of the divider. The
open refuses a system clock that gives a divider below 4, where the
input synchroniser's 2 system clock cycles would be more than half a state
machine cycle, or a bit time more than 0.05 % from the family's. The 8
fractional bits of the divider keep every clock from the lowest one up
within 0.049 %, so the divider limit is the one that refuses. The limit is
the same for both families, for the same reason: the servo's bit time is
not measured and takes the receive margin of 4.4 % and 4.9 %.

| Family | What the bench shows of the servo's bit time |
|---|---|
| Classic | reply bytes 516 to 528 us start to start against 520.8 us for 10 bits: -0.9 % to +1.4 % per byte, stop bit included |
| D series | answer bytes 86 to 87 us start to start against 86.8 us, at 1 us resolution |

How far the servo's own receiver accepts the master's bit time is not
measured.

### Line levels

| State | Classic | D series |
|---|---|---|
| Open, no transaction | an input, pull-down on | an input, pull-up on, input inverted |
| Lead and idle bits | released | driven low |
| A bit 0 | driven low | released (open drain), driven high (push-pull) |
| A bit 1 | released | driven low |
| Receive, end of the window, abort, restart | released | released |
| After close | an input, pull-down on, no override | an input, pull-up on, no override |

The internal pulls are 50 to 80 kOhm. The D series needs a pull-up on the
board. The HFP-30 holds the line low after a write until its next request
and releases it after a read; this driver releases it after every
transaction.

Limits:

- `hitec_line_poll()` ends a transaction whose state machine still runs
  1 ms (`HITEC_LINE_GRACE_US`) after the longest time the transaction can
  take: the state machine is restarted with the pad released and the
  capture is marked as overflowed. A line that keeps sending bytes back to
  back keeps the receiver from its window; a receive with a byte count ends
  at the count.
- A receive that reaches `max_rx` ends at the next `poll()`; until then the
  receiver takes further bytes and drops them.
- The answer follows the first byte received, also one with a bad stop bit
  or a byte made by a glitch.
- The system clock must not change while a line is open.
- Every function of one line is called from one context.

```c
static hitec_line_t line;
hitec_xfer_t x;
hitec_capture_t cap;
hitec_reply_t r;

hitec_line_init(&line);
if (hitec_line_open(&line, 14, HITEC_D, HITEC_DRIVE_OPEN_DRAIN)
    == HITEC_LINE_OK) {                       /* a released input */
    const hitec_driver_t d = hitec_line_driver(&line);

    hitec_d_read_xfer(0x00, 0x0C, &x);        /* position */
    if (d.start(d.ctx, &x)) {
        while (!d.poll(d.ctx, &cap)) { /* call every 1 ms */ }
        if (hitec_d_decode(&cap, 0x0C, &r) == HITEC_RX_OK) {
            /* r.value */
        }
    }
}
hitec_line_close(&line);
```

## Electrical

Not measured: the line voltages, the edges and the servo's current; only
the 1.4 V crossing is known.

| Master | Rating of its pin | Against |
|---|---|---|
| RP2350 GPIO, IOVDD (input/output supply) at 3.3 V | 5.5 V | a servo whose high level is not measured; no level shifter, no clamp |
| The same pin, IOVDD at 0 V | 3.63 V | a classic servo makes the high level itself and may drive the line while the master is off |

- Classic: whether the internal pull-down holds the line low with no servo
  is not measured.
- D series: a pull-up of R to 3.3 V against the servo's 3 kOhm pull-down
  gives 3.3 V x 3 / (3 + R) at the line. With the 3.3 kOhm of Hitec's
  reference circuit that is 1.57 V, below the 2.0 V an RP2350 input needs
  for a high at IOVDD 3.3 V. 1.95 kOhm or less gives 2.0 V; whether the
  servo still pulls such a line low is not known. The pull-up for a 3.3 V
  pin is not established. Push-pull drives the high level of a sent bit to
  3.3 V, 1.1 mA into 3 kOhm; the servo's high bits still come from the
  pull-up.
- Which side drives each level of a D series answer is not measured.

## Use in another pico-sdk project

```cmake
add_subdirectory(protocols/hitec)
target_link_libraries(my_target PRIVATE rcbench_hitec rcbench_hitec_rp2350)
```

`rcbench_hitec` is the core. `rcbench_hitec_rp2350` exists when the
pico-sdk is initialised before the `add_subdirectory()`; it is an INTERFACE
library, so `hitec_line.c` compiles in the target that links it. A project
on another chip links `rcbench_hitec` and supplies its own
`hitec_driver_t`. Nothing in the folder names a file outside it.

## What is measured and what is not

Measured, on an HS-5645MG, an HS-7955TG and a D485HW: the character format
and polarity of both families, the frames, the command bytes above, the
reply times and the byte spacing in the tables, the HFP-30's lead and the
D485HW's trailing byte.

Not measured:

- Whether a servo accepts this driver's timing: the answer at 520.8 to
  527.3 us, a D read's lead of 117.2 us, the classic pacing of 572.9 us,
  the push-pull stop bit of 15/16 of a bit.
- Bit time tolerances of the servos.
- `0x63`, the `-` reply and a D series ID other than 0.
- What sets the D series line level between transactions: the bench shows
  it high in some 20 s windows and low in others.
- The electrical items above.
- On a chip: that autopull refills the output shift register before the
  `jmp !osre` after the last count, that a restarted state machine starts
  with that register empty, and the DMA handshakes. The host suite models
  them.

## Trademarks

Hitec is a name or trademark of its owner; it is used here only to say
which servos this module works with. The project is not affiliated with
it. The protocols are implemented from wire captures of hardware the
project owns, the servos and programmers named above, and from Hitec's
published D series protocol manual; no vendor code and no vendor document
is included. The full
notice is in the repository's
[README](../../README.md#legal-notice-and-disclaimer).

## Tests

`test/host/test_hitec_wire.c` (11 cases) runs the core: the sums, the
transactions, every refusal of each decoder with its limits at both
edges, and the clock helpers across the wrap, with frames and reply times
read off bench captures. `test/host/test_hitec_pio.c` (20 cases) assembles
`hitec_line.pio` with its own assembler for the instructions the program
uses, runs the words in a model of the state machine
(`test/host/hitec_pio_model.h`), holds them to
`test/host/fixtures/hitec_line_words.txt` and runs both families through
it. The pin driver's C file is not in the host build.

```sh
cmake -S test/host -B test/host/build
cmake --build test/host/build
ctest --test-dir test/host/build -R hitec
```
