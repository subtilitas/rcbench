# escwire

Reading and writing the settings of an ESC (electronic speed controller) over
its signal wire, for ESCs with BLHeli_S, Bluejay and AM32 firmware. The core
holds the transaction, the frames of the binary bootloader protocol (protocol
A), the guard every outgoing frame passes, identification, the field maps and
the session. A driver with 5 functions (`ew_driver_t`) connects the core to a
pin; `rp2350/ew_line.c` is that driver on one RP2350 pin. Nothing in a released
image calls the module yet.

This module was written from a behavioural specification, without access to
the firmware sources. The specification states what an ESC and a host exchange
on the wire; its values are not measured on hardware.

| Part | Files | Depends on |
|---|---|---|
| Core | `ew_wire.c`, `ew_pio.c`, `ew_a.c`, `ew_guard.c`, `ew_id.c`, `ew_map.c`, `ew_map_s.c`, `ew_map_bj.c`, `ew_map_am.c`, `ew_session.c`, 7 headers under `include/` | C11 standard headers |
| Pin driver | `rp2350/ew_line.pio`, `rp2350/ew_line.c`, `rp2350/ew_line.h` | pico-sdk 2.3.0 |

## Constraints

- Plain C11. Every header has `extern "C"` guards and compiles by itself as
  C11 and as C++17. `test/host/escwire_headers.cpp` calls the module from
  C++11 and links against the C library.
- Includes `<stdint.h>`, `<stdbool.h>`, `<stddef.h>` and `<string.h>` and no
  other file outside this folder.
- No heap, no floating point, no global or static mutable state. The field
  tables are `const`.
- Non-blocking: `ew_session_step()` reads the clock, polls the driver and
  returns. Call it every 1ms or more often. There is no delay function.
- A session (`ew_session_t`) holds 3 pages of 256B, a send buffer of 264B and
  a receive buffer of 259B. It is not moved while it runs: the guard and the
  transaction point into it.
- No function makes a throttle signal. `ew_guard_frame()` passes the commands
  of the list below and nothing else, and the caller holds the bench SAFE.

## Wire

Half duplex on the signal wire, idle high, 8N1, least significant bit first,
52us a bit. Frames carry a CRC-16 (cyclic redundancy check): polynomial 0xA001
in reflected form, initial value 0, no final XOR, low byte first.

| Command | Frame before the CRC | Answer | Wait, tries |
|---|---|---|---|
| first contact | 17 bytes, no frame | 9 bytes starting `471` | 100ms, 3 |
| address | `FF 00 high low` | `30` | 4ms, 3 |
| read | `03` and the count, 0 for 256 | the bytes, CRC, `30` | 80ms for the first byte, 2ms between bytes, 3 |
| announce and data | `FE 00 high low`, 10 idle bit times (520us), the data and their CRC | `30` | 80ms, 3 |
| program | `01 01` | `30` | 500ms, 1 |
| erase | `02 01` | `30` | 3000ms, 1 |
| leave | `00 01` (EFM8), `00 00` (AM32) | none | |
| keep alive | `FD 00` | `C1` | the guard allows it; a session sends none, since no bootloader leaves on idle |

Every firmware gets the same 520us between an announcement and its data. AM32
ends a message on more than 234us of idle line, so data less than that behind
their announcement are lost and no answer comes; an EFM8 takes any gap. The
bytes of one message go with no gap. AM32 answers about 0.3ms to 0.35ms after a
host's last byte. For the same reason a leave frame is followed by 500us of idle
line before the line goes low.

An erase and a program are not repeated on a failed answer: the read that
follows says what they did. A read goes with a new address on every try.

## Firmwares

| Firmware | Processor | Bytes read and written | Written | Shown only | Editable fields |
|---|---|---|---|---|---|
| BLHeli_S | EFM8 | 112 | layout 33 | layout 32 | 12 |
| Bluejay | EFM8BB2, EFM8BB5x | 255 | layout 208 | layouts 33, 200, 201, 203, 204, 206, 207 | 15 |
| AM32 | ARM parts | 192 | revisions 3 and 4 | revisions below 3, above 4 with no field names | 39 on revision 3, 40 on revision 4 |

- `ew_id.h` holds identification from the first contact's answer and the first
  reads, with a reason for every read-only or refused result (`ew_why_t`).
- A page whose tags are 0xFF, which an ESC writes for itself after losing its
  page, is shown and not written.
- An AM32 whose byte 0 reads 0xFF reports its settings erased
  (`id.erased`). Only `ew_session_restore()` writes to it: a backup of 192B
  with the same pin code, flash code, bootloader protocol and name, onto a page
  that reads 0xFF throughout. A restore left half written takes the same backup
  again, after a read, and no other.
- An AM32 of bootloader protocol 3 with the firmware start 0x4000 in its
  device-info record, a build for CAN (Controller Area Network) boards, is
  shown and not written, a backup included.
- SimonK is not supported. Its frames are refused as unknown commands.

Melody and tune areas, tags, markers and every byte no field names go back as
they were read. A stored value that is none of a field's own reads `raw 17`.

## Use

```c
ew_session_t s;

ew_session_init(&s, &driver);
ew_session_hold(&s);                 /* the line driven high; supply off */
/* the caller switches the supply on */
ew_session_enter(&s, 3000u);
while (ew_session_step(&s) == EW_SES_BUSY) { /* call every 1ms */ }

ew_session_read(&s);
while (ew_session_step(&s) == EW_SES_BUSY) { }

memcpy(draft, s.orig, s.id.len);
draft[0x15] = 2u;
if (ew_session_write(&s, draft, s.id.len) == EW_SES_BUSY) {
    while (ew_session_step(&s) == EW_SES_BUSY) { }
}
ew_session_leave(&s);
while (ew_session_step(&s) == EW_SES_BUSY) { }
```

The hold before the first contact is 500 to 120000ms. 3000ms covers an ESC with
the default melody, about 1.1s, and not one with a custom melody, which can
last far longer.

## The write

1. The guard judges the draft (`ew_guard_approve()`): only editable fields
   differ, and only to their values. Otherwise the call returns
   `EW_SES_ERR_REFUSED` with the byte in `bad_off`. A draft equal to the page
   read ends with no frame.
2. A read and a compare with the page read. A difference ends with
   `EW_SES_ERR_CHANGED`. On AM32 byte 2 is excepted, and the new one is taken.
3. EFM8: address, erase, address, announce and data, program. AM32: address,
   announce and data, program; the ESC erases its page itself.
4. A read and a compare with the draft; on AM32 byte 2 is excepted, since the
   bootloader sets it.

A step that fails, or a read-back that differs, runs the procedure again from
step 3, 3 times at most.

| Result | Page | What goes next |
|---|---|---|
| `EW_SES_OK` | the draft | anything |
| `EW_SES_ERR_READ`, `EW_SES_ERR_CHANGED` | not touched | anything |
| `EW_SES_ERR_WRITE_FAILED` | as read | anything |
| `EW_SES_ERR_TORN` | neither | the draft or the page read; no leave |
| `EW_SES_ERR_LOST` | not known | a read, then as `EW_SES_ERR_TORN` |

`ew_session_stop()` lets a write finish from its first erase or program on,
without starting another procedure, and parks the line after it. Outside that
stretch it cuts at once, as `ew_session_abort()` always does. The caller keeps
the supply on while `ew_session_writing()` is true, unless the heartbeat or the
link is lost. Power lost between an erase and a program leaves the page empty.

## What the guard refuses

`ew_guard.h` has one code per refusal (`ew_refuse_t`).

- Before a first contact, everything but the first contact.
- Any command outside the table above; a frame of the wrong length, with wrong
  fixed bytes, with a CRC that does not fit.
- An erase and a program anywhere but the settings start, with their own codes
  for the first flash page and the bootloader area; on AM32 any erase, and any
  address but the special address 0x0020.
- An erase or data with no page judged, data of another length or other than
  that page, a program with no data.
- A leave while a judged page is being written.
- On AM32 a page whose byte 0 is not 0x01, whose revision is not 3 or 4 or whose
  length is not 192B.
- On an EFM8 a page of a layout not written, or with a changed tag, marker or
  unused byte.
- On the text interface, `ew_guard_line()` allows `info`, `show`, `get`, `set`
  other than `music`, `save` and `help`, at most 1023 characters and a line
  feed. `reset`, `throt`, `play`, `beep` and a line starting 0x00 0xFF are
  refused.

## Driver interface

```c
typedef struct {
    void *ctx;
    bool (*start)(void *ctx, const ew_xfer_t *x);
    bool (*poll)(void *ctx, ew_capture_t *out);
    void (*abort)(void *ctx);
    void (*line)(void *ctx, ew_line_t level);
    uint32_t (*now_us)(void *ctx);
} ew_driver_t;
```

`start()` sends the bytes and releases the pad within one state machine cycle of
the last stop bit. `ew_session_hold()` drives the line high, the end of a leave
leaves it idle for 500us, drives it low for 300ms and parks it, and a stop, an abort or a driver that fails
parks it: an input with its pull-down. `EW_LINE_UP` is an input with its pull-up,
the rest between exchanges.

A driver that does not end a transaction 5ms after its longest time
(`ew_xfer_span_us()`) is aborted, with `EW_SES_ERR_DRIVER`.

## Pin driver

`rp2350/ew_line.c` takes one PIO (programmable input/output) state machine, a
program of 25 words and 2 DMA (direct memory access) channels at
`ew_line_open()`; a refused open claims nothing and leaves the pin alone. No
interrupt is used. The object is 3,488B.

| Rate | Bit time | Divider at 150MHz | Tick of a wait |
|---|---|---|---|
| protocol A | 52us | 487 + 128/256 | 6.5us |
| text line | 26.04us, 38400baud | 244 + 36/256 | 3.26us |

The bit time is exact at 150MHz. An open is refused when a divider is below 4 or
a bit time more than 0.05% off.

| Moment | Pad |
|---|---|
| open, `EW_LINE_PARK`, an abort, a stop, a capture marked aborted or overflow | input, pull-down, input buffer off |
| `EW_LINE_HIGH`, before the first bit | push-pull high, 2mA |
| the bytes | push-pull, both levels, 2mA; no gap between bytes |
| 1 bit time after the last stop bit's edge | released: a reply and the rest between exchanges, input, pull-up |
| `EW_LINE_LOW` | push-pull low, 2mA, 300ms in a leave |
| close | input, pull-down, the drive and slew as found |

The input buffer is off when parked, since erratum RP2350-E9 holds an A2 chip's
input pad at around 2.2V, leaking typically 120uA, against its pull-down while
the buffer is on. An A2 pad can sit there between a close and the next driver. A chip that restarts leaves the pad at
its reset state. Nothing in the driver makes a throttle signal.

Reception:

- A low has to be low still half a bit on to count as a start bit. A low of a
  quarter bit is no byte.
- Bits are sampled mid-bit. Computed: a sender up to 4% off, with no gap between
  bytes, keeps every sample inside its bit for any number of bytes.
- A low stop bit marks the capture (`bad_stop`), and the receiver waits for the
  idle level again, so a line held low is one byte. A line low at the release is
  no byte.
- The receiver starts at the release: a transaction does not receive its own
  bytes. A device repeating them after the release is received; `ew_a` refuses
  that answer.
- The first wait runs from the release, a later wait from the end of a stop bit.
  Each lasts up to 2 ticks more than asked.

A transaction ends at `max_rx`, at a wait's end, or, with the capture marked
aborted and the pad parked at once, 1ms past `ew_xfer_span_us()`.

Each call returns without waiting for the wire. Longest time at 150MHz, as an
estimate, neither computed nor measured: `ew_line_start()` 100us,
`ew_line_poll()` 100us at the end of a capture of 1536B and 2us otherwise, `ew_line_abort()` and `ew_line_level()` 5us,
`ew_line_open()` and `ew_line_close()` 50us. The console test prints measured
values.

The bench console's `ewline <gpio> tx|abort|rx` runs the driver for a logic
analyser ([The bench console](../../docs/BenchConsole.md#the-signal-wires-pin-driver)).

## Not measured

No frame of this code has been sent to an ESC, and every time and value in
this file comes from the specification or is a host's choice:

- the receive tolerance of an ESC and its transmit bit time;
- the time from power-on to bootloader entry; a first contact whose low bits
  fall into a check of the line starts the application;
- pull-ups and logic levels of real boards; the pad's pull-up alone may not
  raise an open-drain answer at 52us a bit;
- the state of the power stage in the bootloaders; the effect of a missing
  motor on entry;
- the 520us between an announcement and its data, and the 234us it clears;
- what an AM32 does after it answered 0xC2 to a cut data block: the specification
  does not state it. Such an answer behind an announcement lies under the host's
  own data and is not read, so the session sees no answer on that try. The tests
  hold the session for an ESC that says nothing more and for one that listens on.

## Trademarks

BLHeli_S, Bluejay, AM32 and SimonK are names of firmwares; they are used here
only to say which ESCs this module works with. The project is not affiliated
with their authors. The full notice is in the repository's
[README](../../README.md#legal-notice-and-disclaimer).

## Tests

`test/host/test_ew_a.c`, `test_ew_id.c`, `test_ew_map.c`, `test_ew_guard.c`,
`test_ew_session.c` and `test_ew_session_write.c` run the core; the last two
against the simulated ESC of `test/host/ew_sim.h`. `test_ew_pio.c` assembles
`rp2350/ew_line.pio` and runs it in a model of the state machine; the IOmcu build
fails when pioasm's words differ from `test/host/fixtures/ew_line_words.txt`.
`test_ew_line.c` runs the driver on a fake of the pico-sdk calls it makes: the pad
at each moment, every error path, the resources. That simulation is written
from the same specification and shares its misreadings until a real ESC answers.

```sh
cmake -S test/host -B test/host/build
cmake --build test/host/build
ctest --test-dir test/host/build -R 'ew_|escwire'
```
