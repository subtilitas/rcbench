# hitec

The signal-wire programming protocols of Hitec servos, wire layer only. The
core holds the frames of both wire families, the transactions a driver
runs, the reply decoders and the arithmetic of the PIO (programmable
input/output) pin driver. The pin driver puts the wire on one RP2350 pin.
There is no session and no register model. In this repository the IOmcu's
HITEC link page (`shared/outputs/hitec_port.c`,
[The programming port for Hitec servos](../../docs/LinkHitec.md)) runs its
transactions on a programming port.

| Part | Files | Depends on |
|---|---|---|
| Core | `hitec_wire.c`, `hitec_pio.c`, `hitec_pre.c`, 3 headers under `include/` | C11 standard headers |
| RP2350 pin driver | `rp2350/hitec_line.pio`, `rp2350/hitec_line.c`, `rp2350/hitec_line.h` | the core; pico-sdk `hardware_pio`, `hardware_dma`, `hardware_gpio`, `hardware_clocks`, `hardware_timer` |

The wire timings below are measured on an HS-5645MG, an HS-7955TG and a
D485HW with the HPP-21 Plus and HFP-30 programmers, at a 1.4 V threshold.
On a wire this code has run D series reads on a D485HW, answered with the
pull-up under [Line levels](#line-levels); nothing else of it has.

## Constraints

- Plain C11. Every header has `extern "C"` guards and compiles by itself as
  C11 and as C++17 (`tools/check_protocols.py`).
  `test/host/hitec_headers.cpp` calls both parts, the precharge receiver
  included, from C++11 and links
  against the C library.
- Includes `<stdint.h>`, `<stdbool.h>`, `<stddef.h>` and `<string.h>` and
  no other file outside this folder.
- No heap, no floating point, no global or static mutable state. Two lines
  on two drivers run side by side.
- A transaction (`hitec_xfer_t`) is 28 bytes, a capture
  (`hitec_capture_t`) 140 bytes, a clock 16 bytes and a run 20 bytes, with
  arm-none-eabi-gcc 14.2 (`-mcpu=cortex-m33 -mthumb -Os`) and with gcc on
  x86-64. A line (`hitec_line_t`) is 4,100 bytes with arm-none-eabi-gcc
  14.2, 3,712 of them the precharge sampler's sample buffer.
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
| `hitec_d_read_xfer()` | 5 | 0 | 88 us driven low, at least (RP2350: 117.2 us) | 25 ms | 8: the answer and the byte 0x00 |
| `hitec_d_write_xfer()` | 7 | 0 | 88 us driven low, at least (RP2350: 89.0 us) | none | none |
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
| 8 | `HITEC_RX_ERR_LENGTH` | fewer bytes than the reply has; classic and listen: also more |
| 9 | `HITEC_RX_ERR_FRAMING` | a byte of the reply with a bad stop bit |
| 10 | `HITEC_RX_ERR_GAP` | a byte of the reply less than 9.5 or more than 15 bit times after the start of the one before |
| 11 | `HITEC_RX_ERR_ACK` | classic: no `+` where the reply has one; listen: a first byte other than the one asked for |
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
| Window end to idle | 7 cycles: the drain empties the output shift register, which drops an answer no byte called for |
| Push-pull | the level moves 1 cycle after the direction would: the bits on the wire sit 1 cycle late, and the release cuts the last stop bit to 15 cycles |

A receiver that samples once in the middle of each bit takes a sender
whose bit time is 4.4 % shorter to 4.9 % longer than its own: the stop bit
sample at 9.44 to 9.56 bit times has to fall inside the sender's tenth bit.

## Receive by precharge and sample

`hitec_pre.h` receives a D series answer with no pull-up on the wire
(receive mode PRECHARGE). The D series servo pulls the line low and leaves
it released for a high level, so a released line with no pull-up reads
low throughout. The master makes the high level itself, one sample at a
time: a second state machine drives the pad high (the precharge),
releases it, waits and samples it. A servo that pulls low has pulled the
line down when the sample is taken; one that leaves it released leaves it
high. The samples go to memory 32 to a word; the decoder reads them as
inverted 8N1 into the `hitec_capture_t` the PULL-UP receiver gives, so
`hitec_d_decode()` takes it unchanged.

Measured on one D485HW, IOmcu at 150 MHz, no pull-up on the signal wire,
servo supply 5.0 V from the PD mini, register 0x32 read 5 times per
setting:

| Sample after the release | Precharge | Reads that pass |
| --- | --- | --- |
| 40 to 187 ns (6 to 28 cycles) | 6.7 to 200 ns (1 to 30 cycles) | 5 of 5 at each setting |
| 213 ns (32 cycles) | 13.3 ns or longer | 5 of 5 |
| 213 ns (32 cycles) | 6.7 ns (1 cycle) | fail |
| 267 ns and later | any | 0 of 5 |
| any | none | 0 of 5, every sample low |

With a 20 ns precharge and the sample 80 ns after the release, 133 of 133
reads of 19 registers passed with a 10 ms window and 30 of 30 with a
25 ms window, equal to the HFP-30's reads of the same registers. The
answer started 5.2 to 5.9 ms into the window.

The module takes the middle of that window:

| Quantity | At 150 MHz | Rule |
| --- | --- | --- |
| Sample period | 163 cycles, 1086.7 ns, 7.99 samples a bit | the system clock cycles nearest to 1/8 of 8.68 us |
| Precharge | 15 cycles, 100.0 ns | 100 ns rounded to cycles |
| Sample after the release | 17 cycles, 113.3 ns | 113 ns rounded to cycles |
| Program | 7 words: `set pindirs, 1 [14]`, `set pindirs, 0 [16]`, `in pins, 1 [31]`, 4 `nop` | `hitec_pre_program()` |
| Window | up to 30,000 us | `HITEC_PRE_WINDOW_MAX_US` |
| Buffer | 928 words, 3,712 bytes: 30 ms and 16 bytes at every clock taken | `HITEC_PRE_WORDS` |

`hitec_pre_timing()` computes the counts from the system clock at the open.
It takes 48 to 250 MHz and refuses a clock whose rounded precharge or wait
falls outside the measured window; only 150 MHz is measured.

The decoder:

- waits for the rest level (low), then for a start bit (the first high
  sample after a low one; a stop bit read low counts as that low sample);
- samples the middle of each bit, floor((j + 1/2) x samples a bit) samples
  after the start bit's first high sample, 3 to 75 at 150 MHz; a start bit
  whose middle reads low is a glitch, and the wait goes on;
- counts every waiting sample against the window and none inside a byte,
  as the PULL-UP receiver does;
- ends at the bytes asked for, at the window's end, or at the end of the
  samples; a byte whose samples are not all in waits for the next call;
- decodes the servo's bit time 4 % short to 4 % long (host-tested; the
  servo's own bit time is not measured);
- gives a start bit's time as the middle between its first high sample
  and the one before, within half a sample, 543 ns.

Integer arithmetic only. On the host (x86-64, gcc -Os) one call over a
whole 30 ms window with 16 bytes takes 3.4 us; with every sample a start
candidate (a line toggling every sample), 155 us. The RP2350 pin driver
decodes in each poll only the words written since the last one, about 29
words a millisecond. The time on the RP2350 is not measured.

`hitec_pre_begin()` plans a transaction: a SEND with a window goes out on
the line's own state machine with no receiver (`hitec_pre_tx_xfer()`),
then the sampler takes the pad; a LISTEN samples from its start; a SEND
with no window (a write) and a LEVEL run as with PULL-UP; a LISTEN that
answers, a window above 30,000 us and a classic transaction are refused.
`hitec_pre_abort()` keeps the whole bytes in any phase.

Classic servos need neither a pull-up nor a precharge: an HS-7955TG, an
HS-5955TG and an HS-5645MG enter serial mode and answer every read on an
open-drain port with neither, at a 5.0 V supply. The servo drives the high
level itself, through about 1 kOhm, and its answer starts inside the
master's stop bit. The line voltages are not measured. The pin driver
refuses PRECHARGE for classic.

Not measured: the current through the pad during a precharge while the
servo pulls the line low (2 mA drive, 100 ns of every 1086.7 ns); a long
run at the values this module chose (100 ns and 113.3 ns lie inside the
passing window; the 133 reads ran at 20 ns and 80 ns); reads with the bank
armed (the HITEC page refuses OPEN
while it is); writes through a PRECHARGE line (a write has no window and
takes no sampler); windows of 30 ms; any servo other than the one
D485HW; any clock but 150 MHz.

## RP2350 pin driver

`rp2350/hitec_line.c` implements `hitec_driver_t` on one pin. Its header
includes no pico-sdk header and has `extern "C"` guards.
`hitec_line_open(line, pin, family, drive)` takes the family and the drive:
classic is open drain only; the D series is open drain, as Hitec's
reference circuit, or push-pull.

| Resource | Use |
|---|---|
| PIO state machines | 1 per open line, in a PIO block that reaches the pin; PRECHARGE 1 more, the sampler |
| PIO program space | 29 of 32 words per open line: every line loads its own copy, so a block holds 1 line and the RP2350's 3 blocks hold up to 3. PRECHARGE: the sampler's 7 words (at 150 MHz) in a block with room for them, another block than the line's |
| DMA (direct memory access) channels | 2 per open line: the words of a transaction and the received bytes; PRECHARGE 1 more, the samples |
| Interrupts, PIO interrupt flags | none |
| Memory | 1 `hitec_line_t` per line, held by the caller; no static state |

`hitec_line_open()` claims the resources and `hitec_line_close()` gives all
of them back. `hitec_line_open_rx()` takes the receive mode as well;
`hitec_line_open()` is PULL-UP. With PRECHARGE the open claims the
sampler's state machine, program and DMA channel after the line's own, and
on a failure gives back everything it claimed before touching the pin.

PRECHARGE on the pin: the first `poll()` that finds a SEND's request
ended (the state machine idle with no word left) keeps the pin's IO_BANK0
control register and its pad register, puts the pin in the sampler's PIO
block with no input override, 2 mA drive and slow slew, and starts the
sampler and its DMA channel; the pad's pull-up stays as it was. Each
`poll()` reads the channel's write address and decodes the words written
since the last one (`hitec_pre_step()`). At the decoder's end, at the last
word, at `hitec_line_abort()` and at `hitec_line_close()` the sampler
stops, its pad direction released, and the two registers are written back.
A sampler whose receive FIFO ran full while its DMA channel still had
words to take, or one that runs 1 ms past its last word, ends with the
capture marked overflowed; an abort that finds it so marks the capture
aborted and overflowed. The FIFO running full after the last word, when
a poll comes more than 278 us late, loses no sample. An open fails with its own code for a pin the chip does not
have, an unknown family or drive, push-pull for classic, a clock the open
refuses, no free state machine or program space, and fewer than 2 free DMA
channels; a failed open claims nothing and leaves the pin as it was.

The IOmcu image of this repository compiles the driver under its `-Werror`
and holds `hitec_line.pio` to `test/host/fixtures/hitec_line_words.txt`.
The HITEC page and this module take 12 452 bytes of `rcbench-iomcu.bin`
(203 580 bytes against 191 128 without them, arm-none-eabi-gcc 14.2,
pico-sdk 2.3.0).

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
| PRECHARGE receive | | driven high 100.0 ns of every 1086.7 ns at 2 mA, slow slew, else released |
| After close | an input, pull-down on, no override | an input, pull-up on, no override |

The internal pulls are 50 to 80 kOhm. The D series needs a pull-up on the
board. Tested with a D485HW: with the pad's pull-up alone the line stays
below the 1.4 V threshold and the servo answers no read; with 2.2 kOhm
from the signal wire to the servo supply it answers every read. The line
voltages are not measured. The HFP-30 holds the line low after a write until its next request
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
  receiver takes further bytes and drops them. With an answer and a
  `max_rx` of 1 the end waits until the answer is out
  (`hitec_pio_answer_due()`). An abort or a restart past the
  transaction's time that comes while the answer goes out cuts it; the
  capture's `answered` is then 0.
- The open, a restart and an abort run the drain's 2 instructions on the
  stopped state machine and return with it stalled at idle: the output
  shift register is empty and `start()` takes a transaction at once.
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
  pin is not established.
- D series, tested with a D485HW: 2.2 kOhm from the signal wire to the
  servo supply, and every read is answered. The line voltages are not
  measured. Against the 3 kOhm pull-down reported for the servo the
  divider puts 58 % of the servo supply on the line (computed): whether
  that stays within the pin's rating in the table above, also with the
  IOmcu unpowered, is not known. Push-pull drives the high level of a sent bit to
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
  527.3 us, a D read's lead of 117.2 us on a servo other than the D485HW,
  the classic pacing of 572.9 us, the push-pull stop bit of 15/16 of a bit.
- Bit time tolerances of the servos.
- `0x63`, the `-` reply and a D series ID other than 0.
- What sets the D series line level between transactions: the bench shows
  it high in some 20 s windows and low in others.
- The electrical items above.
- On a chip: that autopull refills the output shift register before the
  `jmp !osre` after the last count, that `mov osr, null` and
  `out null, 32` leave it empty with autopull on and the FIFO empty, and
  the DMA handshakes. The host suite models them.

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

`test/host/test_hitec_pre.c` (18 cases) runs the precharge receiver's
core: the counts at 150 MHz and at every clock from 48 to 250 MHz, the
sampler's program stepped instruction by instruction, a 7-byte answer at
7.99 samples a bit and the servo 4 % fast and slow, a start bit on the
first sample and on the window's last, a byte across the window's end, a
framing error, no answer, a line stuck high, a glitch, the 30 ms window, a
sampler that stops short, words fed one at a time, and an abort in every
phase. `test/host/test_hitec_wire.c` (11 cases) runs the core: the sums, the
transactions, every refusal of each decoder with its limits at both
edges, and the clock helpers across the wrap, with frames and reply times
read off bench captures. `test/host/test_hitec_pio.c` (22 cases) assembles
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
