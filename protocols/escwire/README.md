# escwire

Reading and writing the settings of an ESC (electronic speed controller) over
its signal wire, for ESCs with BLHeli_S, Bluejay, AM32 and ESCape32 firmware.
The core holds the transaction, the frames of the binary bootloader protocol
(protocol A), the lines of the text interface, the guard every outgoing frame
and line passes, identification, the field maps, the session and the settings
file. A driver with 5 functions (`ew_driver_t`) connects the core to a pin;
`rp2350/ew_line.c` is that driver on one RP2350 pin. The IOmcu's `ESC_WIRE` page
calls it; MOTOR & ESC > PROGRAM's signal-wire row reaches it.

This module was written from a behavioural specification, without access to
the firmware sources. The specification states what an ESC and a host exchange
on the wire; its values are not measured on hardware.

| Part | Files | Depends on |
|---|---|---|
| Core | `ew_wire.c`, `ew_pio.c`, `ew_a.c`, `ew_text.c`, `ew_guard.c`, `ew_id.c`, `ew_map.c`, `ew_map_s.c`, `ew_map_bj.c`, `ew_map_am.c`, `ew_map_e32.c`, `ew_session.c`, `ew_session_text.c`, `ew_file.c`, `ew_file_read.c`, `ew_file_load.c`, 9 headers under `include/`, `ew_session_int.h`, `ew_file_int.h` | C11 standard headers |
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
- A session (`ew_session_t`) holds 3 pages of 256B, a send buffer of 264B, a
  receive buffer of 259B, a text reply buffer of 1536B (`EW_RX_TEXT_MAX`) and
  `music` of 256B. It is not moved while it runs: the guard and the
  transaction point into it.
- No function makes a throttle signal. `ew_guard_frame()` and `ew_guard_text()`
  pass the commands of the lists below and nothing else, and the caller holds
  the bench SAFE.

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
| ESCape32 | ARM parts | 92, 46 keys | revision 17 | other revisions | 46 |

- `ew_id.h` holds identification from the first contact's answer and the first
  reads, with a reason for every read-only or refused result (`ew_why_t`).
- A page whose tags are 0xFF, which an ESC writes for itself after losing its
  page, is shown and not written.
- An AM32 whose byte 0 reads 0xFF reports its settings erased
  (`id.erased`). Only `ew_session_restore()` writes to it: a backup of 192B
  with the same pin code, flash code, bootloader protocol and name, onto a page
  that reads 0xFF throughout. An AM32 with no name takes no backup: the guard
  refuses it with `EW_REF_RESTORE`. A restore left half written takes the same backup
  again, after a read, and no other.
- An AM32 of bootloader protocol 3 with the firmware start 0x4000 in its
  device-info record, a build for CAN (Controller Area Network) boards, is
  shown and not written, a backup included.
- SimonK is not supported. Its frames are refused as unknown commands.

Melody and tune areas, tags, markers and every byte no field names go back as
they were read. A stored value that is none of a field's own reads `raw 17`.

## Text interface

ESCape32 has no bootloader frames: a command line on the signal wire, 38400baud,
8N1, half duplex, no echo, no checksum, LF line ends. A reply ends with a line
`OK` or `ERROR`.

| Line | Answer | Wait for the first byte, ends after | Tries |
|---|---|---|---|
| `info` | `ESCape32 rev<N>.<M> [<target>]`, live values | 200ms, 20ms with no byte | 3 |
| `show` | 47 lines `key: value` | 200ms, 20ms with no byte | 3 |
| `set <key> <value>` | `key: value`, the value stored | 200ms, 20ms with no byte | 3 |
| `save` | `OK` or `ERROR` | 2000ms, 20ms with no byte | 1 |

A reply with no last line, a low stop bit, or a byte other than a printable
character or LF is a failed try. These times are a host's choice.

**Entry.** 3 silent tries of the first contact are followed by 2500ms with the
line driven high (`EW_SES_TEXT_HOLD_MS`), then `info`. The first contact is a
framing error to this ESC and resets it; it opens its command line after 2s with
the line high. Any byte on any of the 3 tries sends no line:
`EW_SES_ERR_CONTACT`. Worst case
without an answer: the hold, 300ms, 2500ms and 600ms.

**The page.** `ew_map_e32` holds 46 numeric keys in the order of the wire's key
numbers. The page in `orig`, `page` and a draft is 92B: each key a 16-bit integer,
low byte first, at its place in that table (`ew_e32_get()`, `ew_e32_put()`), so
`ew_session_read()` and `ew_session_write()` take it as they take any other page.
A key a read did not bring holds `EW_KEY_NONE`. `music` is text in `s.music`, up
to 255 characters; no write changes it. `s.info` holds the live values as the ESC
spelled them.

**The write.**

1. The guard judges the draft: every changed key holds one of its values;
   `freq_max` is not below `freq_min`, `duty_max` not below `duty_min`.
2. `info`: the revision is still 17. `show`: the keys are still as read.
   Otherwise `EW_SES_ERR_CHANGED`.
3. `set` for each key that differs, in table order; the reply is the value
   stored.
4. `save`, then `show`, compared with the draft.

A failed `set` is repeated 3 times, then a `show`, then the procedure from step 3,
3 times at most. A `save` with no answer, or with any line before its `OK`, is
not repeated on its own: a `show` follows, then the procedure. Only a `save`
answered `OK` alone, followed by a `show` equal to the draft, ends a write with
`EW_SES_OK`. `save_open` stands from the first `save` until then, and RAM equal
to the draft or to the page read does not clear it. An `ERROR` to a `save` clears
it only when no earlier `save` left it standing.

| Result | Flash | RAM | What goes next |
|---|---|---|---|
| `EW_SES_OK` | the draft | the draft | anything |
| `EW_SES_ERR_READ`, `EW_SES_ERR_CHANGED` | as read | as read | anything |
| `EW_SES_ERR_CLIPPED` (`clipped`, `bad_off`): the ESC stored another value, no `save` follows | as read, unless `save_open` | some keys set | a read, then the draft or the page read; a leave unless `save_open` |
| `EW_SES_ERR_ESC_ERROR` (`bad_off` for a `set`): a `set` or `save` answered `ERROR` | as read, unless `save_open` | some keys set | as above |
| `EW_SES_ERR_WRITE_FAILED` | as read | as read, or not known with `dirty` | as above |
| `EW_SES_ERR_TORN` | as read, unless `save_open` | any | a read, then the draft or the page read; no leave with `save_open` |
| `EW_SES_ERR_LOST` | not known with `save_open`, else as read | not known | a read, or a power cycle; no leave with `save_open` |

`show` reads RAM, so a read-back alone does not prove the flash. The `OK` of a
`save` says the flash holds the values; nothing here reads the flash after a
power cycle. `ew_session_writing()` is true from the `save` on, and `ew_session_stop()`
before a `save` cuts at once, as the flash is untouched.

A `show` that lacks a key, repeats one or holds another is a failed try. On a
read's third try it is taken: the values it holds are shown, `id.writable` is
false and `id.why` is `EW_WHY_SHOW_KEYS`, until a read that is whole. A revision
other than 17, a revision above 255 or one that does not read as `rev<N>.<M>` is
read-only (`EW_WHY_TEXT_REVISION`), and its `show` is taken on the first try.

**Leaving.** The interface has no command to leave. `ew_session_leave()` sends
nothing, parks the line at once and returns `EW_SES_OK`; the caller switches the
supply off. It is refused while `save_open` stands. On an ESC on its own
supply a person disconnects it.

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
- On the text interface, `ew_guard_line()` allows `info`, `show`, `save` and
  `help` alone, `get` with a key of the map or `music`, and `set` with a key of
  the map and one of its values, one blank between words, at most 1023 characters
  and a line feed. `reset`, `throt`, `play`, `beep`, `set music`, a line starting
  0x00 0xFF, a control character, DEL and any other word are refused.
- `ew_guard_text()` adds the session: before an ESC answered only `info`, on
  protocol A no line, a `set` only of a value in the judged draft, a `save` only
  with a judged draft. `ew_guard_frame()` refuses every frame on the text
  interface.

## The settings file

`ew_file.h` writes a page as text (`ew_file.c`), reads a text back (`ew_file_read.c`) and holds it against an
ESC (`ew_file_load.c`). It has no file access and no heap: the caller holds a buffer of
`EW_FILE_MAX` (5280B), and `ew_file_t` takes 618B.

```text
ESC settings backup, format 1
Firmware: AM32
Layout: 4
Processor: ARM
Pin code: 14
Flash code: 35
Bootloader protocol: 2
Bootloader version: 2
Name: BOARD_F421
START MARKER = 1 [not loaded]
PWM FREQUENCY = 24kHz
bytes 192
00: 01 04 02 02 15 01 ...
checksum 1A2B3C4D
```

An EFM8's file has `Signature: E8B2` in place of the 5 lines from the pin code
to the name. A page of an ESC no firmware was found for goes out as
`Firmware: none`, `Layout: 0`, with its bytes and no field lines. A backup holds
the page as read, whole, melody and tune included. Lines are ASCII (American
Standard Code for Information Interchange), at most 64 characters. The checksum
is CRC-32 (IEEE 802.3) over the lines before it, a line end counted as one LF
(line feed), so a transfer that turns every LF into CR LF (carriage return, line
feed) keeps it valid.

The last line that is not blank says what the text is:

| Last line | Kind | Read as |
|---|---|---|
| `checksum xxxxxxxx` | `EW_FILE_VERIFIED` | the bytes are the page; every line is as written, or the file is refused |
| `edited` | `EW_FILE_EDITED` | a person's input: `<name> = <value>` lines, `#` comments; a field with no line keeps the ESC's value; the bytes give no value |
| anything else | refused | `EW_FILE_ERR_TRUNCATED` |

To change a file by hand, change its field lines and put `edited` in place of
the checksum line. A changed line under a kept checksum line is
`EW_FILE_ERR_CHECKSUM`. A file cut at any byte has neither last line and loads
nothing. A value is the text the file shows, its unit optional, also the unit of
a label when the number starts one label only (`TEMPERATURE = 80` reads `80C`),
or `raw <n>` for the byte itself. Where several bytes read as one text, the lowest is taken, and
a line of such a text that reads as the ESC's value changes nothing.

`ew_file_load()` builds the draft for `ew_session_write()`.

| Result | Condition |
|---|---|
| `EW_LOAD_OK` | `target` is the ESC's page with the file's editable fields; `changed`, `named` of `fields`, and `skipped`, the bytes the file holds otherwise and the load keeps |
| `EW_LOAD_FIRMWARE` | a file of another firmware |
| `EW_LOAD_LAYOUT` | a file of another layout or revision |
| `EW_LOAD_ERASED` | AM32 settings erased |
| `EW_LOAD_READ_ONLY` | a page that is not written |
| `EW_LOAD_VALUE` | a field would take no value of its own: `bad_off`; nothing is loaded |

`ew_file_restore_check()` judges a file for `ew_session_restore()`, which is the
one write to an AM32 whose settings read erased:

| Check | Result on failure |
|---|---|
| kind verified: the CRC-32 holds | `EW_RESTORE_NOT_VERIFIED` |
| file and ESC are AM32 | `EW_RESTORE_FIRMWARE` |
| the ESC's 192B all read 0xFF | `EW_RESTORE_NOT_ERASED` |
| no build for CAN boards | `EW_RESTORE_CAN_BUILD` |
| the file's byte 0 is 0x01, its revision 3 or 4 | `EW_RESTORE_BAD_PAGE` |
| same pin code | `EW_RESTORE_PIN_CODE` |
| same flash code | `EW_RESTORE_FLASH_CODE` |
| same bootloader protocol | `EW_RESTORE_PROTOCOL` |
| the file has a name: an AM32 with no name restores nothing here | `EW_RESTORE_NO_NAME` |
| same name, up to 29 characters | `EW_RESTORE_NAME` |

The bootloader version, byte 2 of a page, reads 0xFF on an erased ESC, so the
comparison uses the bootloader protocol, byte 7 of the first contact. The file
states the version as `Bootloader version`, and the bootloader overwrites byte 2
on every write.

On the card a file goes through `shared/logfile/servo_store.h` with the stem
`EW_FILE_STEM`: `BENCH/ESCW/CONF/ESCW001.TXT` to `ESCW999.TXT`, a base name of
at most 32 characters, `ESCW.TXT` being the record of a write.

ESCape32's file comes from `ew_file_capture_text()`. It has `Processor: TEXT` and
`Target: <name>`, and in place of the field lines and the bytes one line per key
of `ew_map_e32` in its order, then `music`:

```text
Firmware: ESCape32
Layout: 17
Processor: TEXT
Target: BOARD_F051
arm: 1
timing: 16
music: dfa#
checksum 1A2B3C4D
```

A value is a decimal number as the wire spells it, or `none` for a key a read did
not bring. The `music` line holds up to 262 characters
(`EW_FILE_MUSIC_LINE_MAX`). A load takes the keys, also from an edited file as
`<key>: <value>` or `<key> = <value>`, each through the key's values and floor;
in an edited file `none` counts as no line.
`music` stays in the file: no load writes it, and an edited file's `music` line
is read and not used. A backup of another revision is written and not loaded.
ESCape32 has no restore.

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

Each call returns without waiting for the wire. Longest time at 150MHz, from the
console test: `ew_line_start()` 216us, `ew_line_poll()` 96us, `ew_line_level()`
30us, `ew_line_abort()` 53us. Not measured: `ew_line_open()`, `ew_line_close()`, a
stream of 264B, a capture of 1536B.

The pad is driven high for 2 state machine cycles before a frame's first start
bit: 6.5us at 52us a bit, 3.26us at 38400baud. It shows as a high pulse only on a
line that rests low. A pad at rest up, or driven high, does not dip before a
frame.

A line that falls when the sender lets go, because a load holds it down, reads as
one more byte 0x00 with a low stop bit, if the transaction still expects bytes.
The capture marks it and `ew_a` refuses the answer. There is no guard against
this: it is a wiring fault, and the refusal is the outcome wanted. A transaction
that ends at its count does not see it.

### On a chip, no ESC attached

Station, GP13 and GP14, 150MHz. Analyser on commit aeea917d, console only on
a27d3e3f. Waveforms of later commits are not taken: `pad_park()`'s order, the
absence of the dip before a mark and the `rx` test's start are host-tested only.

| Check | Seen |
|---|---|
| bit time, 52us | 51.997us, -0.006%; edges within 0.03us of the grid |
| bit time, 38400baud | 26.040us, -0.007% |
| bytes, sigrok's UART decoder | right, but for the 0x55 marks on aeea917d (dip below) |
| abort | 21 to 53us for the call; 13 of 13 no edge 20.6 to 23ms after |
| ARMED | refused, 0 edges in 4s |
| 16-bit read of the FIFO word | `n=9 bad_stop=3 match=1` 12 of 12 on GP13, a27d3e3f |
| on aeea917d, on GP14: cut in a low bit | a high pulse of 1.35 to 3.70us, once 9.15us, 11 of 12, with the pull-up still on at the release; the order is changed, the likely cause is not separately tested |
| on aeea917d, on GP14: before a 0x55 mark | a low of 27.75us, 25.10us at 38400baud, from the pull-down set ahead of the frame; changed |

GP13 carried a KST servo's signal wire, unpowered: on the pull-up alone it read
low. A line loaded this way does not reach idle high on the pad's pull-up. Volts
not measured. A session on such a line gets no answer to its first contact.

The bench console's `ewline <gpio> tx|abort|rx` runs the driver for a logic
analyser ([The bench console](../../docs/BenchConsole.md#the-signal-wires-pin-driver)).

## Not measured

No frame or line of this code has been sent to an ESC, and every time and value in
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
  hold the session for an ESC that says nothing more and for one that listens on;
- on the text interface: the reply latency, the time a `save` takes, the pull on
  the signal pin, whether the startup music overlaps the entry, a parameter page
  of another revision.

## Trademarks

BLHeli_S, Bluejay, AM32, ESCape32 and SimonK are names of firmwares; they are used here
only to say which ESCs this module works with. The project is not affiliated
with their authors. The full notice is in the repository's
[README](../../README.md#legal-notice-and-disclaimer).

## Tests

`test/host/test_ew_a.c`, `test_ew_id.c`, `test_ew_map.c`, `test_ew_guard.c`,
`test_ew_text.c`, `test_ew_session.c`, `test_ew_session_write.c`,
`test_ew_session_text.c`, `test_ew_file.c`, `test_ew_file_load.c` and
`test_ew_file_e32.c` run the
core; the session and file suites against the simulated
ESC of `test/host/ew_sim.h` and `ew_sim_text.h`. `test_ew_pio.c` assembles
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
