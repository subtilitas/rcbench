# openyge

This module implements framing, CRC (cyclic redundancy check), telemetry,
status, the parameter table, the programming session and the settings file
of the OpenYGE ESC (electronic speed controller) protocol.

| Part | Files | Depends on |
|---|---|---|
| Core | `openyge_frame.c`, `openyge_status.c`, `openyge_params.c`, `include/openyge.h` | C11 standard headers |
| Field map | `openyge_map.c`, `include/openyge_map.h` | C11 standard headers |
| Guard | `openyge_guard.c`, `include/openyge_guard.h` | the core, the map |
| Session | `openyge_session.c`, `include/openyge_session.h` | the core, the guard |
| Settings file | `openyge_file.c`, `include/openyge_file.h` | the core, the map, the guard |
| PIO arithmetic | `openyge_pio.c`, `include/openyge_pio.h` | the core |
| RP2350 pin driver | `rp2350/oy_line.pio`, `rp2350/oy_line.c`, `rp2350/oy_line.h` | the PIO arithmetic, the session's `oy_driver_t`, pico-sdk 2.3.0 |

The wire is a half-duplex UART (universal asynchronous
receiver-transmitter) at 115200 baud, 8N1. The session reaches it through
`oy_driver_t`: `tx()` sends one frame, `rx()` hands over the received bytes
with the echo removed, `now_us()` is a free-running clock that may wrap.
`rp2350/oy_line.c` implements that driver on one RP2350 pin with one PIO
(programmable input/output) state machine and two DMA (direct memory
access) channels; the IOmcu links it, and only the console build's test
`oyline` calls it. The specification is `docs/OpenYGE.md` in the rcbench
repository.

## Constraints

- A frame starts with the sync byte 0xA5 and is 6 to 140 bytes long. The
  header is 6 bytes in version 3 and 4 bytes before it.
- The CRC is CRC-16/XMODEM: polynomial 0x1021, seed 0x0000, no reflection,
  no final XOR. It covers the frame without its last 2 bytes and is stored
  there little-endian. The check value over `"123456789"` is 0x31C3
  (`OPENYGE_CRC_CHECK`).
- A telemetry payload is 26 bytes, a control payload 4 bytes.
- Every field is assembled byte by byte. No struct is cast over a buffer.
- The parameter table holds 64 indices (`OPENYGE_MAX_PARAMS`). A table is
  readable only when every index below its count has been seen.
- Every frame the session sends passes `oy_guard_frame()` right before
  `tx()`. Only `TELE_REQ` with a zero payload and `WRITE_PARAM_REQ` of an
  approved draft go out. A write needs a complete table read in the same
  session, a count of 32, a v3 header and firmware V1.03547 or later; the
  draft carries the read number and CRC of the read it was made from.
- Writable are the indices the map marks CHECK or edit, within the
  writable values of docs/OpenYGE.md section 5.3: 1, 2, 3, 5 to 10, 17,
  18, 21 to 24, 27, and bits 0 and 1 of 26. Index 0, 11 to 15 and 25 are
  never written; raw indices are written back as read.
- Session timing, docs/OpenYGE.md section 10: boot wait 5000ms (1000 to
  20000ms), listen 3000ms, a request 40ms after each reply, read timeout
  200ms, write timeout 400ms, 3 tries, table read cap 10000ms. Each try
  carries a new sequence number.
- A write reads the table again first and ends `OY_CHANGED` with no frame
  sent when it differs. After the writes it reads the table again and ends
  `OY_OK`, `OY_NOT_TAKEN` or `OY_PARTIAL`.
- Settings file, format 1 (docs/OpenYGE.md section 9.1): a header, the
  type code, firmware, serial and count, one line `<ii> <KEY> = <value>`
  per index, and a CRC-32 checksum line. At most 4620B, 64 characters a
  line. Only a file whose checksum fits loads; an `edited` file is refused
  with its own code. A load takes the CHECK and edit indices, of index 26
  bits 0 and 1 only, refuses another type code or count, and passes the
  draft through `oy_guard_approve()` on a copy of the guard.
- Pin driver: 16 state machine cycles a bit, divider 81 + 97/256 at
  150MHz, 8680ns a bit, 16ppm off. The program is 25 words. A frame of 1
  to 140 bytes goes out push-pull while the line is high between bytes;
  the pad is released 16 cycles after the last stop bit's edge, at the end
  of the stop bit, and the receiver runs from the next cycle. Its own bytes
  are not received. The receiver samples 22 + 16 k cycles after the sample
  that saw a start bit and holds a sender up to 4% off with no gaps. The
  receive buffer is 256 entries of 2B; the bytes after a full buffer are
  dropped and counted. Line states: input with pull-down before the supply
  reads on and at once on a fault, STOP or link loss (`oy_line_park()`),
  input with pull-up while listening (`oy_line_listen()`), push-pull while
  sending.
- No heap. Telemetry values use `float`; the map, guard, session, file
  and PIO arithmetic do not.
- `openyge_motor_rpm()` and `openyge_head_rpm()` read index 20 as poles.
  On an ESC whose index 20 holds pole pairs they return twice the RPM
  (revolutions per minute) (docs/OpenYGE.md section 4).

## Interface

`openyge.h`:

```c
uint16_t openyge_crc(uint16_t crc, const void *data, size_t len);
void     openyge_decoder_reset(openyge_decoder_t *d);
bool     openyge_decode_byte(openyge_decoder_t *d, uint8_t byte,
                             openyge_frame_t *out);
bool     openyge_telemetry_parse(const openyge_frame_t *f,
                                 openyge_telemetry_t *out);
size_t   openyge_encode(uint8_t *out, size_t cap, uint8_t type,
                        uint8_t device, uint8_t seq, const void *payload,
                        size_t payload_len);
size_t   openyge_build_telemetry_request(uint8_t *out, size_t cap,
                                         uint8_t device, uint8_t seq);
void     openyge_status_decode(uint8_t status1, openyge_status_t *out);
void     openyge_params_observe(openyge_params_t *p, uint16_t index,
                                uint16_t value);
bool     openyge_motor_rpm(const openyge_params_t *p, uint32_t erpm,
                           float *motor_rpm);
```

`openyge_decode_byte()` returns true when `out` contains a frame with a
valid CRC. The decoder counts frames, CRC errors, resynchronisations and
frames rejected for invalid contents.

`openyge_session.h`:

```c
oy_session_init(&s, &driver);
oy_session_enter(&s, OY_BOOT_MS);               // WAIT, LISTEN, TAKE
while (oy_session_step(&s) == OY_BUSY) { }      // call every 1ms
oy_session_read(&s);                            // READ
while (oy_session_step(&s) == OY_BUSY) { }
memcpy(draft, s.orig, s.count * sizeof(draft[0]));
draft[21] = 13u;                                // pinion teeth
oy_session_write(&s, draft, s.count, s.guard.read_no, s.guard.read_crc);
while (oy_session_step(&s) == OY_BUSY) { }      // RECHECK, WRITE, VERIFY
```

Results: `OY_OK`, `OY_SILENT`, `OY_NO_TABLE`, `OY_NOT_KNOWN`, `OY_CHANGED`,
`OY_NOT_TAKEN`, `OY_PARTIAL`, `OY_GUARD`, `OY_ABORTED`. A result other than
`OY_OK` that an operation reaches ends the session; the caller switches
every output off. `oy_session_abort()` cuts at once on STOP, a fault or a
link loss, and `oy_session_cut_write()` says whether a write was cut after
its first frame.

`openyge_file.h`:

```c
n = oy_file_format(s.orig, s.count, text, OY_FILE_MAX);   // backup text
oy_file_name(name, sizeof(name), 1, s.orig[11]);          // YGE001-5712.TXT
if (oy_file_parse(text, n, &f, &line) == OY_FILE_OK
    && oy_file_load(&f, &s.guard, &ld) == OY_LOAD_OK) {
    oy_session_write(&s, ld.draft, ld.count, s.guard.read_no,
                     s.guard.read_crc);
}
```

Parser refusals: `OY_FILE_ERR_ARG` (1), `_OVERSIZED` (2), `_NOT_YGE` (3),
`_VERSION` (4), `_NEWER` (5), `_TRUNCATED` (6), `_LINE_END` (7),
`_MALFORMED` (8), `_COUNT` (9), `_CHECKSUM` (10), `_TRAILING` (11),
`_MISMATCH` (12), `_EDITED` (13). Load results: `OY_LOAD_OK`, `_ARG`,
`_TYPE`, `_COUNT`, `_GUARD` with the guard's reason and index.

`oy_line.h`, the RP2350 driver behind `oy_driver_t`:

```c
oy_line_init(&line);
oy_line_open(&line, 13);                  // parked: input, pull-down
oy_driver_t d = oy_line_driver(&line);
oy_session_init(&s, &d);
// the caller switches the ESC's supply on
oy_line_listen(&line);                    // input, pull-up, receiving
oy_session_enter(&s, OY_BOOT_MS);
// on a fault, STOP or link loss:
oy_line_park(&line);                      // input, pull-down at once
oy_line_close(&line);
```

`line.rx.framing`, `line.rx.full` and `line.tx_dropped` count bytes with a
low stop bit, buffer fills and streams dropped after a line held low.

## Use in another pico-sdk project

```cmake
add_subdirectory(protocols/openyge)
target_link_libraries(my_target PRIVATE rcbench_openyge
                                        rcbench_openyge_rp2350)
```

`rcbench_openyge_rp2350` is added only where `pico_generate_pio_header`
exists; it assembles `rp2350/oy_line.pio` into the build tree.

The headers compile from C++.

## Trademarks

YGE and OpenYGE are names or trademarks of their respective owners; they
are used here only to say which ESCs this module works with. The project is
not affiliated with them. The module is written from the specification in
`docs/OpenYGE.md`; no vendor code is included.
The full notice is in the repository's
[README](../../README.md#legal-notice-and-disclaimer) and in the section
"Trademarks" of [NOTICE](../../NOTICE).
