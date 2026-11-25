# supvan

This module implements the print protocol of Supvan T50-series thermal
label printers, as sold under the Supvan and Katasymbol names. It builds
the commands, joins the printer's answers, reads the status and the roll's
material record, packs a 1-bit image into print buffers, compresses them
with a literal-only LZMA (Lempel-Ziv-Markov chain algorithm) encoder, wraps
the stream in data frames and runs a print job over one link, one write at
a time. The caller moves the bytes over the radio and draws the image.

| Part | Files | Depends on |
|---|---|---|
| Core | `supvan_frame.c`, `supvan_page.c`, `supvan_job.c`, `include/supvan.h` | C11 standard headers |
| Encoder | `lzlit.c`, `include/lzlit.h` | C11 standard headers |
| RP2350 pin driver | none | |

The module has no pin driver and no PIO program. The printer is reached
over Bluetooth LE (BLE): the caller owns the GATT (Generic Attribute
Profile) client, writes what `sv_job_tx()` hands out and feeds the
notifications to `sv_job_rx()`.

## Printer

The protocol is held to a T50M Pro with firmware "T50ProJ" 2.4 on the
bench's station, with a 40 x 30mm roll (gap 3mm, type 1).

- BLE service `0000e0ff-3c17-d293-8e48-14fe2e4da212`. FFE1 notify and
  write (value handle 0x000E, CCCD 0x000F), FFE9 write (0x0011), FFEA
  notify (0x0013). No pairing.
- Every frame goes to FFE9 as a write request (not a write command).
  Answers come as notifications on FFE1 in pieces of MTU - 3 bytes. Data
  frames get no answer over BLE.
- A data frame is 512 bytes, written as 4 writes of 128 bytes. A write of
  128 bytes needs an ATT MTU (attribute protocol maximum transmission unit)
  of 131 or more. The printer answered 240 to a request of 247.
- The printer beeps once on each connect.

## Constraints

### Frames

- A command is 16 bytes:
  `7E 5A 0C 00 10 01 AA CMD CHK_L CHK_H 00 01 P0_L P0_H P1_L P1_H`, CHK
  the 16-bit sum of bytes 10 to 15 (`sv_cmd_build()`).
- An answer is `7E 5A LEN_L LEN_H ...`, LEN + 4 bytes in all, with the
  command echoed at byte 7. Byte 4 is 0x55 over BLE and 0x10 or 0x39 over
  SPP (serial port profile); its meaning is unknown and the module does
  not read it.
- `sv_rx_feed()` joins answers of 8 to 128 bytes from pieces of any size,
  1 byte included. Bytes before `7E 5A` are dropped. A length field that
  gives a frame under 8 or over 128 bytes drops the `7E` and searches
  again from the next byte. The joiner holds 128 bytes and no more.
- Commands used: 0x12 CHECK_DEVICE, 0x11 INQUIRY_STA, 0x30 RETURN_MAT,
  0x13 START_PRINT, 0x5C NEXT_ZIPPEDBULK (P0 512, P1 the data frame count),
  0x10 BUF_FULL (P0 the stream length, P1 the speed byte), 0x14
  STOP_PRINT.

### Status (INQUIRY_STA answer, 20 bytes or more)

| Byte | Bit | Field | Ends a job with |
|---|---|---|---|
| 14 | 0 | buffer full | |
| 14 | 1 | label read or write error | `SV_ERR_LABEL` |
| 14 | 2 | label end | `SV_ERR_NO_LABEL` |
| 14 | 3 | label mode error | `SV_ERR_LABEL` |
| 14 | 6 | low battery | `SV_ERR_BATTERY` |
| 15 | 2 | busy | |
| 15 | 3 | head temperature high | `SV_ERR_HOT` |
| 16 | 3 | cover open | `SV_ERR_COVER` |
| 16 | 4 | USB power | |
| 16 | 6 | printing | |
| 17 | 0 | label not installed | `SV_ERR_NO_LABEL` |
| 18, 19 | | print count, little-endian | |

With several flags set, the first of cover, no label, label, battery,
head wins (`sv_status_error()`).

### Material record (RETURN_MAT answer, 47 bytes or more)

| Bytes | Field |
|---|---|
| 22 to 28 | the roll's RFID (radio-frequency identification) tag UID |
| 39 | label type |
| 40 | width, mm |
| 41 | height along the feed, mm |
| 42 | gap, mm |
| 43 to 46 | labels left, little-endian 32 bits |

`sv_roll_check()` takes a roll of 15 to 48mm wide and 20 to 100mm long. A
width or height of 0 is `SV_ERR_NO_LABEL`.

### Image and print buffers

- 8 dots a mm. The head is 384 dots (48mm) across. A column is 48 bytes,
  least significant bit first, 1 black: dot (col, row) is bit `row % 8` of
  byte `col * 48 + row / 8`.
- A print buffer is 4,096 bytes and holds up to 84 columns (4,074 data
  bytes from byte 14). A 40 x 30mm label is 224 columns: 3 buffers of 84,
  84 and 56.

| Bytes | Content |
|---|---|
| 0, 1 | checksum |
| 2 | page bits: 0x02 page start (first buffer), 0x04 page end and 0x08 print end (last buffer) |
| 3 | density << 2 \| 1 << 6: 0x50 at density 4 |
| 4, 5 | columns in this buffer |
| 6 | bytes a column, 48 |
| 8, 9 | top margin, 8 dots |
| 10, 11 | bottom margin, 8 dots |
| 12 | density, 4 |
| 14 on | image data, the rest 0 |

- Checksum: the 16-bit sum of bytes 2 to 13 plus byte `i * 256 - 1` for
  each whole 256-byte block that ends before the data's end.

### LZMA stream

- The printer prints an LZMA "alone" stream and nothing else: raw buffers
  are acknowledged, run to "done" with no error flag, and print nothing.
- `lzlit` codes every byte as a literal: properties 0x5D (lc 3, lp 0,
  pb 2), dictionary 8,192 bytes in the header, the exact length in 64
  bits, no end marker. A larger dictionary fails on the printer's RAM.
  With no match the decoder never reads the dictionary.
- Its output is byte-identical to `tools/lzlit.py`, the encoder whose
  stream the station printed.
- Size: 12,288 bytes of a 40 x 30mm label give 2,107 bytes on the
  station's label; the worst case is `LZLIT_BOUND(n)` = 13 + n + n / 8 +
  16 bytes.
- State: `lzlit_t`, 12,336 bytes on a 32-bit target. No dictionary, no
  match finder, no heap. The input may arrive in pieces of any size, so a
  caller encodes one print buffer at a time.

### Data frames

- The stream is cut into 500-byte chunks, the last one padded with 0. A
  chunk becomes a 506-byte packet `AA BB CHK_L CHK_H IDX TOTAL` + 500
  bytes, CHK the 16-bit sum of packet bytes 4 to 505, in a 512-byte frame
  `7E 5A FC 01 10 02` + packet (`sv_chunk_build()`).
- Index and count are single bytes: 255 frames at most. BUF_FULL carries
  the length in 16 bits: a stream is 65,535 bytes at most
  (`SV_STREAM_MAX`, 132 frames).
- Speed byte from the mean stream bytes a print buffer (`sv_speed()`):

| Mean above | 3,000 | 2,800 | 2,500 | 2,000 | 1,500 | 1,000 | 500 | else |
|---|---|---|---|---|---|---|---|---|
| Speed | 10 | 15 | 20 | 25 | 40 | 45 | 55 | 60 |

### Job

`sv_job_t` runs a QUERY (steps 1 to 3) or a PRINT (steps 1 to 12).

| Step | Sends | Goes on when | Gives up after |
|---|---|---|---|
| 1 CHECK | CHECK_DEVICE | answered | 1s, 1 retry: `SV_ERR_SILENT` |
| 2 MATERIAL | RETURN_MAT | the roll and the counter c0 read | 1s, 1 retry: `SV_ERR_SILENT` |
| 3 READY | INQUIRY_STA every 100ms; for a PRINT then RETURN_MAT | neither busy nor printing; the counter c0 read again, same roll | 5s: `SV_ERR_BUSY` |
| 4 START | START_PRINT | answered | 1s |
| 5 PRINTING | INQUIRY_STA every 100ms | printing set | 3s |
| 6 BUFFER | INQUIRY_STA every 100ms | buffer full clear | 3s |
| 7 BULK | NEXT_ZIPPEDBULK | answered | 1s |
| 8 DATA | each frame as 4 writes of 128 bytes, 10ms after each write's answer | every write answered | 1s a write |
| 9 PAUSE | nothing for 20ms | | |
| 10 FULL | BUF_FULL | answered | 1s |
| 11 DONE | INQUIRY_STA every 100ms | neither printing nor busy | 15s |
| 12 COUNTER | RETURN_MAT, up to 3 reads 300ms apart | c0 - 1: `SV_OK`; c0 three times: `SV_ERR_NOT_PRINTED` | 1s a read |

- A job that the printer cannot decode still ends with no error flag, so
  only the label counter proves a print.
- An error flag in step 3 or later ends the job with its error.
- c0 is read again at the end of step 3, once the printer is idle: an
  earlier print that ends during the wait uses a label before START, not
  after it. A roll other than step 2's at that read is `SV_ERR_ROLL`.
- A write's answer is booked against the write it answers (a command or a
  data fragment), not against the step. The printer's notification may
  come before or after the link's answer to the same write.
- A roll that differs from the one the stream was laid out for
  (`sv_stream_t.w_mm`, `h_mm`, each checked on its own; 0 any) ends a
  PRINT in step 2 with `SV_ERR_ROLL`. A tag UID that changes between step 2 and step 12, or a
  counter that moves by other than 1, gives no verdict: `SV_ERR_ROLL`.
- An end in steps 4 to 11 (an error, a timeout, a refused write or
  `sv_job_abort()`) sends STOP_PRINT once, after the write in flight is
  answered. Its answer, its refusal, its timeout of 1s or a link drop ends
  the job with the first error. An end before START was written, or in
  step 12, sends nothing.
- A link drop in steps 1 to 3 ends the job with `SV_ERR_LINK`. A drop
  after START waits 5s for the link. Back in time after BUF_FULL was
  written, step 11 runs again first: INQUIRY_STA every 100ms until neither
  printing nor busy, 15s at most (`SV_ERR_SILENT` and STOP_PRINT after
  that), because the counter moves only once the label is out. Then, or
  at once when BUF_FULL was not written, the counter decides: c0 - 1 is
  `SV_OK` with `relinked` set, c0 three times is `SV_ERR_LINK`. Not back
  in time: `SV_ERR_LINK`. The job is never sent a second time.
- Times are unsigned differences of a 32-bit ms clock the caller passes,
  so a job runs across the clock's wrap.
- `sv_job_t` is 752 bytes on a 32-bit target. No function allocates,
  blocks, reads a clock or keeps a global.

## Interface

`lzlit.h`:

```c
void   lzlit_begin(lzlit_t *z, uint8_t *out, size_t cap, uint32_t raw_len);
void   lzlit_put(lzlit_t *z, const uint8_t *data, size_t n);
size_t lzlit_end(lzlit_t *z);   /* the length; 0 when out was too small */
```

`supvan.h`:

```c
void     sv_cmd_build(uint8_t out[16], uint8_t cmd, uint16_t p0, uint16_t p1);
void     sv_rx_reset(sv_rx_t *rx);
size_t   sv_rx_feed(sv_rx_t *rx, const uint8_t *in, size_t n,
                    const uint8_t **frame, uint16_t *len);
bool     sv_status_parse(const uint8_t *f, size_t n, sv_status_t *st);
sv_err_t sv_status_error(const sv_status_t *st);
bool     sv_material_parse(const uint8_t *f, size_t n, sv_material_t *m);
sv_err_t sv_roll_check(const sv_material_t *m);
uint16_t sv_buf_count(uint16_t cols);
bool     sv_buf_fill(uint8_t out[4096], const uint8_t *bits, uint16_t cols,
                     uint16_t index);
uint16_t sv_chunk_count(uint32_t len);
bool     sv_chunk_build(uint8_t out[512], const uint8_t *stream, uint32_t len,
                        uint8_t index);
uint8_t  sv_speed(uint32_t mean);
bool     sv_job_start(sv_job_t *j, sv_job_kind_t kind, const sv_stream_t *z,
                      uint32_t now_ms);
void     sv_job_rx(sv_job_t *j, const uint8_t *data, size_t n, uint32_t now_ms);
void     sv_job_written(sv_job_t *j, bool ok, uint32_t now_ms);
void     sv_job_link(sv_job_t *j, bool up, uint32_t now_ms);
void     sv_job_abort(sv_job_t *j, uint32_t now_ms);
bool     sv_job_tx(sv_job_t *j, uint32_t now_ms, const uint8_t **data,
                   uint16_t *n);
void     sv_job_tick(sv_job_t *j, uint32_t now_ms);
bool     sv_job_ended(const sv_job_t *j);
```

A print, in the order the caller runs it:

```c
lzlit_begin(&z, stream, cap, buffers * SV_BUF_SIZE);
for (uint16_t i = 0; i < buffers; ++i) {
    sv_buf_fill(buf, bits, cols, i);
    lzlit_put(&z, buf, SV_BUF_SIZE);
}
sv_stream_t s = { stream, lzlit_end(&z), buffers, w_mm, h_mm };
sv_job_start(&job, SV_JOB_PRINT, &s, now);
/* each poll: */
if (sv_job_tx(&job, now, &data, &n)) { /* write request to FFE9 */ }
/* on its answer: sv_job_written(); on a notification: sv_job_rx();
 * on the link's change: sv_job_link(); every poll: sv_job_tick() */
```

## Use in another project

```cmake
add_subdirectory(protocols/supvan)
target_link_libraries(my_target PRIVATE rcbench_supvan)
```

Under ESP-IDF the same CMakeLists.txt registers a component. The headers
compile from C++. Code for the ESP32-P4 at `-Os`
(`riscv32-esp-elf-gcc` 15.2.0): `lzlit.c` 710 bytes, `supvan_frame.c` 891,
`supvan_page.c` 591, `supvan_job.c` 2,230.

## Source and credit

The protocol was reverse-engineered by the
[heeen/supvan-cups](https://github.com/heeen/supvan-cups) project (MIT
licence) from the vendor's Android app, and is described in its
`docs/PROTOCOL.md`. This module is written from that description and from
the bench station's captures of a T50M Pro; it contains no code of that
project and no vendor code. The material record offsets follow its
`crates/supvan-proto/src/status.rs`, the buffer checksum its
`buffer.rs`. The literal-only encoder and the counter check are this
project's: the station found that the printer prints a literal-only
stream and that a completed job does not prove a print.

## Test fixtures

`test/host/fixtures/supvan/` holds the streams and buffers the C suites
compare with byte for byte. `tools/gen_supvan_fixtures.py` writes them from
rules written a second time in Python, apart from the C;
`--check` fails when a file differs, and `test/tools/test_lzlit.py` runs
that check.

| File | Content | From |
|---|---|---|
| `lzlit-*.bin` | streams of 0, 1, 12,288 bytes, a pattern and 5,000 seeded random bytes | `tools/lzlit.py` |
| `label-bufs.bin` | 3 print buffers of a 224-column test bitmap | the report's buffer rules |
| `label.lz` | their stream | `tools/lzlit.py` |
| `label-frames.bin` | its data frames | the report's frame rules |

The command bytes, the 26-byte answer to command 0xF0 and the status and
material offsets in `test_supvan_frame.c` are copied from the station's
report; those cases say "report" beside each.

**Station captures (H0).** The label files stand in for the station's
captures of one label printed with `bleprint.py --lit`: its 3 raw buffers,
its stream and its data frames, plus the raw RETURN_MAT answer of the
40 x 30mm roll and the advertising bytes. When they arrive they replace
`label-bufs.bin`, `label.lz` and `label-frames.bin`, and `label_bits()` in
`tools/gen_supvan_fixtures.py` and `test/host/test_supvan_page.c` then
draws the captured bitmap; the raw RETURN_MAT answer becomes the record in
`test_supvan_frame.c`'s `material()`. The C tests stay as they are.

## Trademarks

Supvan and Katasymbol are names or trademarks of their respective owners;
they are used here only to say which label printers this module talks to.
The project is not affiliated with them and is neither supported nor
sponsored by them. The full notice is in the repository's
[README](../../README.md#legal-notice-and-disclaimer) and in the section
"Trademarks" of [NOTICE](../../NOTICE).

## Tests

| Suite | Holds |
|---|---|
| `test_lzlit` | the golden streams of `tools/lzlit.py`, fed whole and in pieces; the header; the overflow at one byte short and at the exact size; the bound up to 65,536 bytes; the carry path |
| `test_supvan_frame` | the command against the report's bytes; an answer split 20 + 20 + 7 and fed 1 byte at a time; noise, a length out of range, two answers in one piece, the 0xF0 answer; each status bit; the material offsets; the roll limits at both ends |
| `test_supvan_page` | 224 columns as 84 / 84 / 56 against the fixture; 1, 84 and 85 columns; the checksum at byte 255; the data frames against the fixture; frame counts at 1, 500, 501 bytes and 255 frames; the speed byte at each threshold |
| `test_supvan_job` | the whole PRINT in order with its parameters and spacing; notifications before and after their writes' answers; a label used by an earlier print during step 3; a reconnect while the label prints; each roll dimension on its own; QUERY alone; the counter 295 to 294, 295 three times, 294 on the second read, a changed UID, a jump; busy for 4.9s and 5.1s; the clock's wrap |
| `test_supvan_job_ends` | the 7 error flags before START and in step 11; no answer at each step; a write refused and lost; an abort at each of steps 4 to 11 sends STOP_PRINT once; the link down at each step, back and not back inside 5s |
| `test/tools/test_lzlit.py` | `lzlit.c` through `test/host/lzlit_cli.c` on 9 inputs up to 65,536 bytes: equal to `tools/lzlit.py`, decoded by liblzma |

The simulated printer is `test/host/sv_sim.h`. Its answer lengths (status
24 bytes, material 47) are the simulator's; H0's captures give the
printer's own.
