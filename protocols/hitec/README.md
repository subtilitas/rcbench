# hitec

The signal-wire programming protocols of Hitec servos. The core holds the
frames of both wire families, the transactions a driver runs, the reply
decoders, the arithmetic of the PIO (programmable input/output) pin driver
and the register models: the parameters, their encodings and the writes a
change takes ([Register model](#register-model)). The pin driver puts the
wire on one RP2350 pin. There is no session. In this repository the IOmcu's
HITEC link page (`shared/outputs/hitec_port.c`,
[The programming port for Hitec servos](../../docs/LinkHitec.md)) runs its
transactions on a programming port.

| Part | Files | Depends on |
|---|---|---|
| Core | `hitec_wire.c`, `hitec_pio.c`, `hitec_pre.c`, `hitec_reg.c`, 4 headers under `include/` | C11 standard headers |
| RP2350 pin driver | `rp2350/hitec_line.pio`, `rp2350/hitec_line.c`, `rp2350/hitec_line.h` | the core; pico-sdk `hardware_pio`, `hardware_dma`, `hardware_gpio`, `hardware_clocks`, `hardware_timer` |

The wire timings below are measured on an HS-5645MG, an HS-7955TG and a
D485HW with the HPP-21 Plus and HFP-30 programmers, at a 1.4V threshold.
On a wire this code has run D series reads on a D485HW, answered with the
pull-up under [Line levels](#line-levels); nothing else of it has.

## Constraints

- Plain C11. Every header has `extern "C"` guards and compiles by itself as
  C11 and as C++17 (`tools/check_protocols.py`).
  `test/host/hitec_headers.cpp` calls both parts, the precharge receiver
  and the register model included, from C++11 and links
  against the C library.
- Includes `<stdint.h>`, `<stdbool.h>`, `<stddef.h>` and `<string.h>` and
  no other file outside this folder.
- No heap, no floating point, no global or static mutable state. Two lines
  on two drivers run side by side.
- A transaction (`hitec_xfer_t`) is 28 bytes, a capture
  (`hitec_capture_t`) 140 bytes, a clock 16 bytes and a run 20 bytes, with
  arm-none-eabi-gcc 14.2 (`-mcpu=cortex-m33 -mthumb -Os`) and with gcc on
  x86-64. A line (`hitec_line_t`) is 4,100 bytes with arm-none-eabi-gcc
  14.2, 3,712 of them the precharge sampler's sample buffer. An image
  (`hitec_image_t`) is 291 bytes and a plan (`hitec_plan_t`) 338 bytes on
  both; `hitec_param_set()` holds one image on its stack.
- Non-blocking: `start()` and `poll()` return at once. Nothing waits.

## Wire

| | Classic: HS-5xxx, HS-7xxx | D series |
|---|---|---|
| Bit rate | 19 200bit/s, 52.08us a bit | 115 200bit/s, 8.68us a bit |
| Format | 8N1, least significant bit first | 8N1, least significant bit first |
| Polarity | normal: the line idles high, a start bit is low | inverted: inside a frame the line rests low, a start bit is high |
| High level | made by the servo; with no servo the line is low | made by a pull-up; the servo has a 3kΩ pull-down (reported with another programmer, not measured here) |
| Master frame | 4 bytes `cmd p1 p2 sum`; the 4 bytes sum to 0 modulo 256 | write 7 bytes `96 ID addr 02 lo hi sum`, read 5 bytes `96 ID addr 00 sum`; `sum` is the low byte of the sum of every byte after the header |
| Reply | 2 bytes: value and `+`, `+ +`, or position high and low | 7 bytes `69 ID addr 02 lo hi sum`; a write is not answered |
| Reply start | 508 to 517us after the start of the master's last byte after reads, up to 614us after writes and after the first read of a session: inside the master's stop bit | 5.9 to 6.5ms after the end of the request (HFP-30); 5.93 to 6.52ms over 153 reads by PRECHARGE (D485HW, 2026-10-20) |
| Reply bytes | 516 to 528us start to start (10 bits are 520.8us) | back to back, 86 to 87us start to start (86.8us) |
| Master bytes | 572us start to start (HPP-21 Plus), 521us (HFP-30) | back to back |

Classic command bytes: `0x61` read EEPROM (address; value, `+`), `0x62`
write EEPROM (address, value; `+ +`), `0x64` write data memory (`+ +`),
`0x65` read position (high, low), `0x66` move (target high, low; `+ +`),
`0x67` the first command of a session (value, `+`). `0x63`, read data
memory, and the reply `-` (0x2D) to a bad sum are described for the
HS-5xxx and not seen on the wire.

On the D485HW the line also shows:

- After a request the line is high for 0.02 to 0.49ms, then low until the
  answer starts.
- In 37 of 37 answers checked in 3 captures a byte 0x00 follows the 7
  bytes of the answer back to back. The line then stays low for 110 to
  190us and goes high.
- From a high line the HFP-30 drives the line low 88us before the first
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
count against the window. Windows, leads and levels are 4s or shorter.

| Builder | Bytes | Gap | Lead | Window | Bytes received |
|---|---|---|---|---|---|
| `hitec_c_xfer()` | 4 | 1 bit time: 572.9us start to start | none | 1.5ms | 2 |
| `hitec_d_read_xfer()` | 5 | 0 | 88us driven low, at least (RP2350: 117.2us) | 25ms | 8: the answer and the byte 0x00 |
| `hitec_d_write_xfer()` | 7 | 0 | 88us driven low, at least (RP2350: 89.0us) | none | none |
| `hitec_listen_xfer()` | none | | | as given | as given |
| `hitec_level_xfer()` | none | | | | |

Where the receiver starts, which is time 0 of a capture:

- Classic SEND: the start of the last byte's stop bit, 9 bit times
  (468.75us) after the start of that byte. The servo's first start bit
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
| Classic | the start of the master's last byte | 494.8us (the middle of the master's stop bit) to 1ms | 508 to 614us |
| D series | the end of the request's last stop bit | 2ms to 20ms | 5.9 to 6.5ms (HFP-30); 5.93 to 6.52ms over 153 reads by PRECHARGE (D485HW, 2026-10-20); 15.2ms is reported with another programmer |

The D decoder checks the first 7 bytes and no byte after them: the byte
0x00 the D485HW sends after its answer, and the release of the line,
which reads as a byte with a bad stop bit. It reports the ID byte and does
not check it: it reads 0x00 in every answer on the bench, and 0xFF and
0xFE are reported from a servo with ID 0.

## Register model

`hitec_reg.h` holds both register models as data: the image of a servo, the
parameter table, the encodings, model identification and the write plans.
It runs no transaction; a session reads into an image, edits a copy of it
(the draft) and sends the writes of a plan.

| | Classic: HS-5xxx, HS-7xxx | D series |
|---|---|---|
| Memory | 256 EEPROM cells of 8 bits; a 2-cell value high byte first | 16-bit registers at even addresses 0x00 to 0xFE, low byte first |
| Checksum | cells 0x00 to 0x1C sum to 0 modulo 256; 0x1C is the balance | none in memory |
| Full read | 100 cells: 0x00 to 0x23, the factory block 0x30 to 0x53, the name 0x60 to 0x6F, the date 0x70 to 0x73, the serial 0x80 to 0x87 (`hitec_c_read_ranges()`) | 21 registers (`hitec_d_read_list()`) |
| A write takes effect | at once; the servo answers `+ +` | in RAM; save `0x70 = 0xFFFF`, then reboot `0x46 = 1` |

### Models

`hitec_identify()` reads register 0x00 (D series) or the name cells 0x60
to 0x6F up to the first 0x00 (classic). A model not in the table takes its
series' layout.

| Model | Identified by | Layout | Source |
|---|---|---|---|
| D485HW | 0x00 = 34485 | D | read and written through the IOmcu; every HFP-30 setting captured |
| D645MW | 0x00 = 34645 | D | third-party observation only |
| other D series | any other number | D | documentation only |
| HS-5645MG | `HS5645MG` | HS-5xxx | every HPP-21 Plus setting captured |
| HS-7955TG | `HS7955` | HS-7xxx | every HPP-21 Plus setting captured |
| HS-5955TG | `HS5955TG` | HS-5xxx | all 256 cells read through the IOmcu; no write recorded |
| other `HS5...`, other `HS7...` | the name's first 3 characters | HS-5xxx, HS-7xxx | documentation only |
| any other name | | HS-5xxx | documentation only |

### Parameters

Every row of the table names its registers or cells, its encoding, its
limits, its layouts and its source (`hitec_src_t`): RCBENCH, written
through the IOmcu and read back; CAPTURED, a vendor programmer's writes
captured on the wire; READ, read from a servo with no write recorded; DOC,
documentation only. A mark refers to the measured servo of the layout: the
D485HW, the HS-5645MG, the HS-7955TG. Every model is written; the mark is
for the interface to show.

D series, encodings of the HFP-30:

| Parameter | Register | Encoding | Edit | Source |
|---|---|---|---|---|
| model | 0x00 | number | read only | READ |
| position | 0x0C | 0 to 16383, 8192 the centre | read only | READ |
| ID | 0x32 | 0 to 255 | 0 to 255 | RCBENCH |
| direction | 0x5E | 0 CW, 1 CCW | mirrors 0xC2, 0xB2, 0xB0 | CAPTURED |
| neutral, left end, right end | 0xC2, 0xB2, 0xB0 | position | 1 to 16383, left < neutral < right | CAPTURED |
| dead band | 0x4E | steps 1 to 5: 1, 4, 8, 12, 16 | 1 to 5 | CAPTURED (1 and 16; 4, 8, 12 third-party) |
| speed | 0x54 | 10% to 100% in 10% steps: 2 to 20; the factory 4095 shows 100% | 10% to 100% | CAPTURED |
| fail-safe | 0x4C | 0 limp, 1 off, 2 to 5000 a position in us | limp, off, 900 to 2100us | CAPTURED |
| soft start | 0x60 | 20% 1, 40% 3, 60% 6, 80% 8, 100% 100 | the 5 steps | CAPTURED |
| overload protection | 0x9C | off 100, 10% to 50% as 10 to 50 | off, 10% to 50% | CAPTURED |
| smart sensor | 0x6C, 0x44 | on 0 and 0; off the values of 0x8A and 0x8C | on, off | RCBENCH |
| sensitivity | 0x64 | number | read only | READ |
| 0x04, 0x22, 0x8A, 0x8C, 0xD4, 0xD6 | | number, meaning not known | read only | READ |

Classic, encodings of the HPP-21 Plus:

| Parameter | Cells | Encoding | Edit | Source |
|---|---|---|---|---|
| version | the reply to 0x67 | number | read only | READ |
| date | 0x70 to 0x73 | day, month, year high byte first | read only | READ |
| serial | 0x80 to 0x86 | 7 ASCII digits | read only | READ |
| checksum | 0x00 to 0x1C | OK, BAD | read only | CAPTURED |
| dead band | 0x02 | steps 1 to 5: 01, 04, 08, 0C, 10 | 1 to 5 | CAPTURED |
| speed | 0x06 | 10% to 100%: 01, 07, 0E, 15, 1C, 23, 2A, 31, 38, 40 | 10% to 100% | CAPTURED |
| centre | 0x07, 0x08 | 0.25us: HS-5xxx stored + 2052, HS-7xxx stored + 2048 | 3600 to 8400 (900 to 2100us); sets 0x13 = 0x14 = 0x10 | CAPTURED |
| left end, right end | 0x13, 0x14 | number, unit not known; CW: right 0x13, left 0x14; CCW: left 0x13, right 0x14 | 0 to 255 | CAPTURED |
| direction | 0x1B bit 0 | 1 CW | swaps 0x13 and 0x14, 0x19 and 0x1A from 0x49 and 0x4A | CAPTURED |
| fail-safe | 0x1B bit 1 | on, off | on, off | CAPTURED |
| fail-safe position, HS-5xxx | 0x19, 0x1A | 0.25us: stored + 4 | 3600 to 8400; sets 0x1B bit 1 | CAPTURED |
| fail-safe position, HS-7xxx | 0x19, 0x1A | number, mapping not known | read only | READ |
| overload protection | HS-5xxx 0x20 with 0x21 = FF; HS-7xxx 0x21 with 0x20 = 00 | off 64, 10% to 50% as 0A to 32 | off, 10% to 50% | CAPTURED |
| resolution, HS-7xxx | 0x22 | 00 Normal; 01 and the factory 0B High | read only | CAPTURED |

A stored value that fits no encoding decodes as `HITEC_GET_RAW` with the
raw value, and the interface shows no decoded value beside it. Every
decode returns the stored value beside the decoded one. An edit of such a
parameter takes any value of the table, with its side effects; the raw
value cannot be entered by an edit, only written back by a restore. On the
bench reads this holds for the HS-5645MG's and the HS-5955TG's dead band
(02) and overload protection (0x20 = FF): an edit to dead band 2 writes
0x02 = 04 and the balance, an edit to overload protection 10% writes
0x20 = 0A and 0x21 = FF with no balance.

`hitec_param_clamp()` gives the nearest value an edit takes, a tie going
up; `hitec_param_ok()` tests one. An edit of a value equal to the decoded
one changes nothing, so a D series speed of 4095 stays 4095 when 100% is
entered.

### Write plans

`hitec_plan(from, to)` compares the writable addresses (`hitec_writable()`)
of the read image and the draft and lists the writes in order. A
difference anywhere else is not written: the position, the factory block,
the name.

- Classic: the HPP-21 Plus's settings, in its order: 0x00 to 0x06, the
  pairs 0x07/08 to 0x11/12, 0x13, 0x14, the pairs 0x15/16 to 0x19/1A,
  0x1B, 0x1D, the pairs 0x1E/1F and 0x20/21, 0x22, 0x23. A setting with a
  changed cell writes all its cells. After a setting of cells up to 0x1B
  the balance goes to 0x1C (`HITEC_STEP_C_SUM`); cells above 0x1C get none,
  as the HS-7955TG's overload protection in 0x20/0x21 shows. A read image
  with a bad checksum and no such setting gets one balance write. A
  restore of every cell is 35 cell writes and 19 balance writes.
- D series: per setting the registers the HFP-30 writes for it: direction
  0xC2, 0xB2, 0xB0, 0x4C (unchanged), 0x5E; overload protection 0x9C,
  0x9A = 3, 0x98 = 200; smart sensor 0x72 = 0x4E54, 0x6C, 0x44; every
  other setting its own register. A register goes out once. A plan with a
  write ends with the save and the reboot. `hitec_d_factory_plan()` is
  0x6E = 0x0F0F, save, reboot.
- `hitec_c_factory()` copies the factory block into 0x00 to 0x23 of a
  draft; the plan then writes the cells that differ, where the HPP-21 Plus
  writes all 24 settings.

| Wait after a write | Value | Source |
|---|---|---|
| D series register write | 20ms | the HFP-30's spacing |
| D series save to reboot | 100ms | the HFP-30's |
| D series reboot | 1000ms | third-party observation; the HFP-30 reads again 834.6ms after it |
| Classic write | 0ms | not measured; the HPP-21 Plus spaces two settings 121 to 128ms, with a read of 0x00 to 0x23 between them |

Not measured:

- No plan of this module has run against a servo. The classic writes and
  the D series writes but ID and smart sensor are the vendor programmers'.
- The HFP-30 writes a neutral or an end point after its adjust loop, with
  0x54 and 0x56 beside it; a plan writes the register alone.
- Whether a D series speed of 20 and the factory 4095 behave the same.
- The classic centre's offset rests on one capture per layout; the end
  point unit; the HS-7xxx fail-safe position mapping; what a resolution
  change writes to the centre.
- The fail-safe edit range of 900 to 2100us is Hitec's PWM range; the D
  series manual allows 2 to 5000us.
- ID 0 reaches every servo on the line: two D series servos on one port
  both take a plan.

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
| Answer | an open-drain line changes the direction for its start bit 160 cycles after the sample that saw the first byte's start bit: 10.0 to 10.13 bit times after that edge, 520.8 to 527.3us at 19 200bit/s. The programmers send theirs 0.52 to 0.55ms after it. A push-pull line changes the level 1 cycle later. The answer is 10 bit times; the receiver waits for the idle level again 1 cycle after its stop bit ends |
| Lead before the first start bit | the transaction's lead, rounded up to 2 cycles, or 8 cycles plus the bit times that pad the front of the stream to a word, whichever is longer: a D read is driven low 117.2us, a D write 89.0us |
| Window end to idle | 7 cycles: the drain empties the output shift register, which drops an answer no byte called for |
| Push-pull | the level moves 1 cycle after the direction would: the bits on the wire sit 1 cycle late, and the release cuts the last stop bit to 15 cycles |

A receiver that samples once in the middle of each bit takes a sender
whose bit time is 4.4% shorter to 4.9% longer than its own: the stop bit
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

Measured on one D485HW, IOmcu at 150MHz, no pull-up on the signal wire,
servo supply 5.0V from the PD mini, register 0x32 read 5 times per
setting:

| Sample after the release | Precharge | Reads that pass |
| --- | --- | --- |
| 40 to 187ns (6 to 28 cycles) | 6.7 to 200ns (1 to 30 cycles) | 5 of 5 at each setting |
| 213ns (32 cycles) | 13.3ns or longer | 5 of 5 |
| 213ns (32 cycles) | 6.7ns (1 cycle) | fail |
| 267ns and later | any | 0 of 5 |
| any | none | 0 of 5, every sample low |

With a 20ns precharge and the sample 80ns after the release, 133 of 133
reads of 19 registers passed with a 10ms window and 30 of 30 with a
25ms window, equal to the HFP-30's reads of the same registers. The
answer started 5.2 to 5.9ms into the bench reader's window.

With this module's values on GPIO 13 and no pull-up (D485HW, 2026-10-20),
the D decoder's delay, from the end of the request's last stop bit, was
5.93 to 6.52ms over 153 reads.

The module takes the middle of that window:

| Quantity | At 150MHz | Rule |
| --- | --- | --- |
| Sample period | 163 cycles, 1086.7ns, 7.99 samples a bit | the system clock cycles nearest to 1/8 of 8.68us |
| Precharge | 15 cycles, 100.0ns | 100ns rounded to cycles |
| Sample after the release | 17 cycles, 113.3ns | 113ns rounded to cycles |
| Program | 7 words: `set pindirs, 1 [14]`, `set pindirs, 0 [16]`, `in pins, 1 [31]`, 4 `nop` | `hitec_pre_program()` |
| Window | up to 30,000us | `HITEC_PRE_WINDOW_MAX_US` |
| Buffer | 928 words, 3,712 bytes: 30ms and 16 bytes at every clock taken | `HITEC_PRE_WORDS` |

`hitec_pre_timing()` computes the counts from the system clock at the open.
It takes 48 to 250MHz and refuses a clock whose rounded precharge or wait
falls outside the measured window; only 150MHz is measured.

The decoder:

- waits for the rest level (low), then for a start bit (the first high
  sample after a low one; a stop bit read low counts as that low sample);
- samples the middle of each bit, floor((j + 1/2) x samples a bit) samples
  after the start bit's first high sample, 3 to 75 at 150MHz; a start bit
  whose middle reads low is a glitch, and the wait goes on;
- counts every waiting sample against the window and none inside a byte,
  as the PULL-UP receiver does;
- ends at the bytes asked for, at the window's end, or at the end of the
  samples; a byte whose samples are not all in waits for the next call;
- decodes the servo's bit time 4% short to 4% long (host-tested; the
  servo's own bit time is not measured);
- gives a start bit's time as the middle between its first high sample
  and the one before, within half a sample, 543ns.

Integer arithmetic only. On the host (x86-64, gcc -Os) one call over a
whole 30ms window with 16 bytes takes 3.4us; with every sample a start
candidate (a line toggling every sample), 155us. The RP2350 pin driver
decodes in each poll only the words written since the last one, about 29
words a millisecond. The time on the RP2350 is not measured.

`hitec_pre_begin()` plans a transaction: a SEND with a window goes out on
the line's own state machine with no receiver (`hitec_pre_tx_xfer()`),
then the sampler takes the pad; a LISTEN samples from its start; a SEND
with no window (a write) and a LEVEL run as with PULL-UP; a LISTEN that
answers, a window above 30,000us and a classic transaction are refused.
`hitec_pre_abort()` keeps the whole bytes in any phase.

Classic servos need neither a pull-up nor a precharge: an HS-7955TG, an
HS-5955TG and an HS-5645MG enter serial mode and answer every read on an
open-drain port with neither, at a 5.0V supply. The servo drives the high
level itself, through about 1kΩ, and its answer starts inside the
master's stop bit. The line voltages are not measured. The pin driver
refuses PRECHARGE for classic.

Not measured: the current through the pad during a precharge while the
servo pulls the line low (2mA drive, 100ns of every 1086.7ns); a long
run at the values this module chose (100ns and 113.3ns lie inside the
passing window; the 133 reads ran at 20ns and 80ns); reads with the bank
armed (the HITEC page refuses OPEN
while it is); writes through a PRECHARGE line (a write has no window and
takes no sampler); windows of 30ms; any servo other than the one
D485HW; any clock but 150MHz.

## RP2350 pin driver

`rp2350/hitec_line.c` implements `hitec_driver_t` on one pin. Its header
includes no pico-sdk header and has `extern "C"` guards.
`hitec_line_open(line, pin, family, drive)` takes the family and the drive:
classic is open drain only; the D series is open drain, as Hitec's
reference circuit, or push-pull.

| Resource | Use |
|---|---|
| PIO state machines | 1 per open line, in a PIO block that reaches the pin; PRECHARGE 1 more, the sampler |
| PIO program space | 30 of 32 words per open line: every line loads its own copy, so a block holds 1 line and the RP2350's 3 blocks hold up to 3. PRECHARGE: the sampler's 7 words (at 150MHz) in a block with room for them, another block than the line's |
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
block with no input override, 2mA drive and slow slew, and starts the
sampler and its DMA channel; the pad's pull-up stays as it was. Each
`poll()` reads the channel's write address and decodes the words written
since the last one (`hitec_pre_step()`). At the decoder's end, at the last
word, at `hitec_line_abort()` and at `hitec_line_close()` the sampler
stops, its pad direction released, and the two registers are written back.
A sampler whose receive FIFO ran full while its DMA channel still had
words to take, or one that runs 1ms past its last word, ends with the
capture marked overflowed; an abort that finds it so marks the capture
aborted and overflowed. The FIFO running full after the last word, when
a poll comes more than 278us late, loses no sample. An open fails with its own code for a pin the chip does not
have, an unknown family or drive, push-pull for classic, a clock the open
refuses, no free state machine or program space, and fewer than 2 free DMA
channels; a failed open claims nothing and leaves the pin as it was.

The IOmcu image of this repository compiles the driver under its `-Werror`
and holds `hitec_line.pio` to `test/host/fixtures/hitec_line_words.txt`.
The HITEC page and this module take 12 452 bytes of `rcbench-iomcu.bin`
(203 580 bytes against 191 128 without them, arm-none-eabi-gcc 14.2,
pico-sdk 2.3.0).

### Clock

| Family | Divider at 150MHz | Bit time | Lowest system clock |
|---|---|---|---|
| Classic | 488 + 72/256, exact | 52.083us | 1.2282MHz |
| D series | 81 + 97/256, 0.002% short | 8.680us | 7.3692MHz |

The lowest clock is 64 bit rates less half a step of the divider. The
open refuses a system clock that gives a divider below 4, where the
input synchroniser's 2 system clock cycles would be more than half a state
machine cycle, or a bit time more than 0.05% from the family's. The 8
fractional bits of the divider keep every clock from the lowest one up
within 0.049%, so the divider limit is the one that refuses. The limit is
the same for both families, for the same reason: the servo's bit time is
not measured and takes the receive margin of 4.4% and 4.9%.

| Family | What the bench shows of the servo's bit time |
|---|---|
| Classic | reply bytes 516 to 528us start to start against 520.8us for 10 bits: -0.9% to +1.4% per byte, stop bit included |
| D series | answer bytes 86 to 87us start to start against 86.8us, at 1us resolution |

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
| PRECHARGE receive | | driven high 100.0ns of every 1086.7ns at 2mA, slow slew, else released |
| After close | an input, pull-down on, no override | an input, pull-up on, no override |

The internal pulls are 50 to 80kΩ. The D series needs a pull-up on the
board. Tested with a D485HW: with the pad's pull-up alone the line stays
below the 1.4V threshold and the servo answers no read; with 2.2kΩ
from the signal wire to the servo supply it answers every read. The line
voltages are not measured. The HFP-30 holds the line low after a write until its next request
and releases it after a read; this driver releases it after every
transaction.

Limits:

- `hitec_line_poll()` ends a transaction whose state machine still runs
  1ms (`HITEC_LINE_GRACE_US`) after the longest time the transaction can
  take: the state machine is stopped, the receive channel takes the words
  still in the receive FIFO, the bytes are counted, and only then is the
  machine restarted with the pad released. The capture holds every byte
  pushed before the stop and is marked as overflowed. A line that keeps sending bytes back to
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
        while (!d.poll(d.ctx, &cap)) { /* call every 1ms */ }
        if (hitec_d_decode(&cap, 0x0C, &r) == HITEC_RX_OK) {
            /* r.value */
        }
    }
}
hitec_line_close(&line);
```

## Electrical

Not measured: the line voltages, the edges and the servo's current; only
the 1.4V crossing is known.

| Master | Rating of its pin | Against |
|---|---|---|
| RP2350 GPIO, IOVDD (input/output supply) at 3.3V | 5.5V | a servo whose high level is not measured; no level shifter, no clamp |
| The same pin, IOVDD at 0V | 3.63V | a classic servo makes the high level itself and may drive the line while the master is off |

- Classic: whether the internal pull-down holds the line low with no servo
  is not measured.
- D series: a pull-up of R to 3.3V against the servo's 3kΩ pull-down
  gives 3.3V x 3 / (3 + R) at the line. With the 3.3kΩ of Hitec's
  reference circuit that is 1.57V, below the 2.0V an RP2350 input needs
  for a high at IOVDD 3.3V. 1.95kΩ or less gives 2.0V; whether the
  servo still pulls such a line low is not known. The pull-up for a 3.3V
  pin is not established.
- D series, tested with a D485HW: 2.2kΩ from the signal wire to the
  servo supply, and every read is answered. The line voltages are not
  measured. Against the 3kΩ pull-down reported for the servo the
  divider puts 58% of the servo supply on the line (computed): whether
  that stays within the pin's rating in the table above, also with the
  IOmcu unpowered, is not known. Push-pull drives the high level of a sent bit to
  3.3V, 1.1mA into 3kΩ; the servo's high bits still come from the
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
  527.3us, a D read's lead of 117.2us on a servo other than the D485HW,
  the classic pacing of 572.9us, the push-pull stop bit of 15/16 of a bit.
- Bit time tolerances of the servos.
- `0x63`, the `-` reply and a D series ID other than 0.
- What sets the D series line level between transactions: the bench shows
  it high in some 20s windows and low in others.
- The electrical items above.
- On a chip: that autopull refills the output shift register before the
  `jmp !osre` after the last count, that `mov osr, null` and
  `out null, 32` leave it empty with autopull on and the FIFO empty, that
  `out null, 12` drops the front of the answer word, and the DMA
  handshakes. The host suite models them.
- On a chip: that a byte pushed just before the restart of a transaction
  past its time reaches the capture. The pin driver is not in the host
  build.
- The register model's open items, listed under
  [Write plans](#write-plans).

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

`test/host/test_hitec_reg.c` (14 cases) and
`test/host/test_hitec_reg_plan.c` (9 cases) run the register models on the
bench reads of the D485HW, the HS-5645MG, the HS-7955TG and the HS-5955TG
(`test/host/hitec_reg_fix.h`). The first: every encoding at both ends of
its range and one step past, the clamp, the raw fallback, every refusal of
an edit, the checksum across its wrap modulo 256 and the identification of
every model. The second: the edits' side effects, the balance after every
classic setting, the plan of every setting of both series against the
vendor programmers' captured writes, and an edit from a raw value to a
table value on the HS-5645MG and the HS-5955TG.
`test/host/test_hitec_pre.c` (18 cases) runs the precharge receiver's
core: the counts at 150MHz and at every clock from 48 to 250MHz, the
sampler's program stepped instruction by instruction, a 7-byte answer at
7.99 samples a bit and the servo 4% fast and slow, a start bit on the
first sample and on the window's last, a byte across the window's end, a
framing error, no answer, a line stuck high, a glitch, the 30ms window, a
sampler that stops short, words fed one at a time, and an abort in every
phase. `test/host/test_hitec_wire.c` (11 cases) runs the core: the sums, the
transactions, every refusal of each decoder with its limits at both
edges, and the clock helpers across the wrap, with frames and reply times
read off bench captures. `test/host/test_hitec_pio.c` (23 cases) assembles
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
