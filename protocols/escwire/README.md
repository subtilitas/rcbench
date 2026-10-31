# escwire

Reading and writing the settings of an ESC (electronic speed controller) over
its signal wire, for ESCs with BLHeli_S, Bluejay and AM32 firmware. The core
holds the transaction, the frames of the binary bootloader protocol (protocol
A), the guard every outgoing frame passes, identification, the field maps and
the session. A driver with 5 functions (`ew_driver_t`) connects the core to a
pin. The folder holds no pin driver yet, and nothing in this repository calls
the module yet.

This module was written from a behavioural specification, without access to
the firmware sources. The specification states what an ESC and a host exchange
on the wire; its values are not measured on hardware.

| Part | Files | Depends on |
|---|---|---|
| Core | `ew_wire.c`, `ew_a.c`, `ew_guard.c`, `ew_id.c`, `ew_map.c`, `ew_map_s.c`, `ew_map_bj.c`, `ew_map_am.c`, `ew_session.c`, 6 headers under `include/` | C11 standard headers |

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
| read | `03 N`, 0 for 256 | N bytes, CRC, `30` | 80ms for the first byte, 2ms between bytes, 3 |
| announce and data | `FE 00 high low`, 2 idle bit times (104us), the data and their CRC | `30` | 80ms, 3 |
| program | `01 01` | `30` | 500ms, 1 |
| erase | `02 01` | `30` | 3000ms, 1 |
| leave | `00 01` (EFM8), `00 00` (AM32) | none | |
| keep alive | `FD 00` | `C1` | the guard allows it; a session sends none, since no bootloader leaves on idle |

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
  that reads 0xFF throughout.
- An AM32 of bootloader protocol 3 with the firmware start 0x4000 in its
  device-info record, a build for CAN (Controller Area Network) boards, is
  shown and not written.
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
drives it low for 300ms and parks it, and a stop, an abort or a driver that fails
parks it: an input with its pull-down. `EW_LINE_UP` is an input with its pull-up,
the rest between exchanges.

A driver that does not end a transaction 5ms after its longest time
(`ew_xfer_span_us()`) is aborted, with `EW_SES_ERR_DRIVER`.

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
- the 104us between an announcement and its data, which the specification does
  not bound.

## Trademarks

BLHeli_S, Bluejay, AM32 and SimonK are names of firmwares; they are used here
only to say which ESCs this module works with. The project is not affiliated
with their authors. The full notice is in the repository's
[README](../../README.md#legal-notice-and-disclaimer).

## Tests

`test/host/test_ew_a.c`, `test_ew_id.c`, `test_ew_map.c`, `test_ew_guard.c`,
`test_ew_session.c` and `test_ew_session_write.c` run the core; the last two
against the simulated ESC of `test/host/ew_sim.h`. That simulation is written
from the same specification and shares its misreadings until a real ESC answers.

```sh
cmake -S test/host -B test/host/build
cmake --build test/host/build
ctest --test-dir test/host/build -R 'ew_|escwire'
```
