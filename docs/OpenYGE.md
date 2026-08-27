# The OpenYGE ESC telemetry and parameter protocol

The specification this project implements against. Everything here is a fact
about bytes on a line — offsets, scale factors, a polynomial, timeouts.

## 1. Where this comes from

**There is no published OpenYGE document.** The protocol's details were
supplied by YGE's own developer, in the form of working code, and this page is
that material written up as a specification. It is the documentation of record
here because at the time of writing there is no other.

That provenance is worth stating plainly for two reasons.

**It settles the licensing question rather than dodging it.** The material
arrived as a file carrying a GPL-3.0 header, and rcbench is MIT. Those files
stay outside this repository — they are not in the tree, not in the history,
and nothing here is copied, adapted or translated from them. What crossed is
what cannot be owned: a byte offset, a scale factor, a polynomial, a timeout.
That a voltage sits at offset 8 as a little-endian `uint16` in units of 10 mV
is a fact about a YGE ESC, not authorship. This page is that set of facts,
written from scratch, and it is what any code in this repository is written
from — which is the rule the record commits to and the thing that makes the
MIT licence here true rather than merely claimed.

**It also tells you how much to trust each number.** This came from the person
who wrote the protocol, which is as close to authoritative as it gets. But it
came as an implementation rather than as a document, and an implementation
answers "what does this code do" rather than "what does the wire guarantee" —
so where the code and its own comments disagree, as they do about the RPM
scale in section 5, only hardware settles it. Section 9 lists what to measure.
Each measurement turns a strong inference into a fact, and the list is short.

Section 8 records six defects in the material as supplied. They are recorded
here rather than fixed silently because they are worth feeding back to YGE,
and because an implementation transcribed from that code would inherit every
one of them.

---

## 2. Physical layer

| | |
| --- | --- |
| Signalling | UART, **115200 baud, 8N1** |
| Byte order | **Little-endian** throughout |
| Topology | Half duplex, one master and up to 127 addressable ESCs |
| Duty cycle | ~20 Hz telemetry |

The master hears its own transmission — the reference suppresses exactly as
many received bytes as it just sent — so the physical connection is a
**single wire** with TX and RX commoned, or a driver that echoes.

### What this means for rcbench specifically

**This line cannot go through the RS485 transceiver.** rcbench's own panel
link has a baud *floor* of about 125 kbaud, set by the RC one-shot in the
board's auto-direction circuit; below that the driver releases the bus
mid-frame — [the link](Link.md) has the arithmetic. OpenYGE runs at
**115200, which is under that floor**. It is not a
conflict — the ESC line is a different link — but it does settle where it
lands: a dedicated half-duplex UART on the coprocessor, never the RS485 path.

On the RP2350 that is either a hardware UART with TX and RX tied through a
resistor and the echo discarded, or a PIO half-duplex UART. The coprocessor
already plans PIO soft UARTs for the receiver-bus analyser, so the second
option costs one state machine out of twelve and gives cleaner control of the
turnaround.

Timing at 115200, 8N1 — 86.81 µs per byte:

| | |
| --- | ---: |
| 34-byte telemetry reply | 2.95 ms |
| 12-byte request | 1.04 ms |
| One poll, both directions | **3.99 ms** plus the ESC's turnaround |

Against a 50 ms cycle that is 8% of the line. There is room for a second ESC
on its own UART, or for parameter traffic interleaved with telemetry.

---

## 3. Frame structure

Every frame is `header · payload · CRC`. `frame_length` counts **the whole
frame including the header and the CRC**.

### 3.1 Header, protocol version 3 and later — 6 bytes

| Off | Size | Field | |
| ---: | ---: | --- | --- |
| 0 | 1 | `sync` | Always **0xA5** |
| 1 | 1 | `version` | 3 for the current protocol |
| 2 | 1 | `frame_type` | Section 4 |
| 3 | 1 | `frame_length` | Total frame length, header and CRC included |
| 4 | 1 | `seq` | Master increments per request; the ESC **echoes it back** |
| 5 | 1 | `device` | Address. Bit 7 (**0x80**) set = sender is the master. Bits 0–6 are the ESC address, **0x01–0x7F**; address **0x00 addresses all ESCs** |

### 3.2 Legacy header, version below 3 — 4 bytes

`sync`, `version`, `frame_type`, `frame_length` only. No sequence number and no
address, so **a pre-v3 ESC cannot be polled or addressed**: it broadcasts
unsolicited telemetry and that is all it does. Parameter access does not exist
below v3.

An implementation must therefore read `version` before it can know where the
payload starts.

### 3.3 CRC — 2 bytes, little-endian, at the end

Computed over **every byte from `sync` up to but not including the CRC
itself**.

> **The algorithm is CRC-16/XMODEM, not CRC-16/CCITT-FALSE**, whatever it may
> be called in any given implementation. Same polynomial, different seed, and
> the difference is every byte of every frame:
>
> | | Poly | Init | Refl. | XorOut | Check over `"123456789"` |
> | --- | --- | --- | --- | --- | --- |
> | **OpenYGE** | 0x1021 | **0x0000** | none | none | **0x31C3** |
> | rcbench's own link | 0x1021 | 0xFFFF | none | none | 0x29B1 |
>
> Both check values were computed and confirmed while writing this.

rcbench's `link_crc()` — see [the link](Link.md) — already takes the seed as
its first argument, so the existing, tested routine serves both links
unchanged — pass `0x0000` here and
`LINK_CRC_INIT` for the panel link. Nothing new needs writing, and nothing new
needs testing beyond one vector.

---

## 4. Frame types

| Value | Name | Sent by | |
| ---: | --- | --- | --- |
| 0x00 | `TELE_AUTO` | ESC | Unsolicited telemetry. What a pre-v3 ESC does, and what a v3 ESC does before the master takes over |
| 0x02 | `TELE_RESP` | ESC | Telemetry, answering a request |
| 0x03 | `TELE_REQ` | Master | Ask for telemetry |
| 0x04 | `WRITE_PARAM_RESP` | ESC | Acknowledges a parameter write; carries telemetry as well |
| 0x05 | `WRITE_PARAM_REQ` | Master | Write one parameter |

Requests carry the 4-byte control payload of section 6.2; every ESC-to-master
frame carries the full telemetry payload of section 5, including the response
to a parameter write.

The material carries a claim that bit 7 of `frame_type` distinguishes a read
from a write — 0x81 for a parameter update, say. **No value in the table above
uses it, and nothing tests it.** Treat `frame_type` as a plain enumeration and
reject anything not listed, which is what the supplied code does in practice
whatever its comments describe. Worth asking YGE whether bit 7 is reserved for
something not yet in use, because a decoder that rejects it today will reject
it tomorrow too.

---

## 5. Telemetry payload — 26 bytes

Offsets are given both within the payload and absolute in a version 3 frame.
A version 3 telemetry frame is **34 bytes** total; a legacy one is 32.

| Payload | v3 abs | Size | Field | Unit | To SI |
| ---: | ---: | ---: | --- | --- | --- |
| 0 | 6 | 1 | `reserved` | | |
| 1 | 7 | u8 | `temperature` | °C **+ 40** | `°C = v − 40`, range −40…215 |
| 2 | 8 | u16 | `voltage` | 10 mV | `V = v / 100` |
| 4 | 10 | u16 | `current` | 10 mA | `A = v / 100` |
| 6 | 12 | u16 | `consumption` | mAh | as is |
| 8 | 14 | u16 | `rpm` | **10 eRPM** | `eRPM = v × 10` — see below |
| 10 | 16 | i8 | `pwm` | % | output duty, **signed** |
| 11 | 17 | i8 | `throttle` | % | input setpoint, **signed** |
| 12 | 18 | u16 | `bec_voltage` | mV | as is |
| 14 | 20 | u16 | `bec_current` | mA | as is |
| 16 | 22 | u8 | `bec_temp` | °C + 40 | `°C = v − 40` |
| 17 | 23 | u8 | `status1` | | Section 7 |
| 18 | 24 | u8 | `cap_temp` | °C + 40 | capacitor pack |
| 19 | 25 | u8 | `aux_temp` | °C + 40 | |
| 20 | 26 | u8 | `status2` | | undocumented; carry it, do not interpret it |
| 21 | 27 | u8 | `reserved1` | | possibly a consumption high byte above 65 Ah |
| 22 | 28 | u16 | `pidx` | | parameter index — section 6 |
| 24 | 30 | u16 | `pdata` | | parameter value — section 6 |

Every 16-bit field lands on an even offset in both header lengths, so a
compiler happens to lay this out with no padding on ordinary ABIs. **Do not
rely on that.** Parse byte by byte: casting a buffer to a struct assumes the
host is little-endian, assumes no padding, and performs unaligned 16-bit reads
that are undefined behaviour on strict-alignment targets. rcbench's existing
link decoder already parses byte-wise and this should match it.

### `rpm` — the one genuine ambiguity

The field is described in the reference as "0.1 erpm" while the code multiplies
by ten. Those cannot both be true; multiplying by ten is right and the wording
is loose — the wire value is eRPM divided by ten, which is what most ESC
telemetry protocols send, and 65535 × 10 = 655,350 eRPM is a plausible ceiling
where 6,553 eRPM would not be.

**Confirm it on hardware anyway**, with a motor of known pole count at a known
mechanical RPM. It is one measurement and it is the difference between a
tachometer and a decoration.

Mechanical RPM needs the pole count, which is parameter 20 (section 6.3):

    RPM = eRPM / (poles / 2)

---

## 6. Parameters

The most valuable half of this protocol, and the part that makes it more than
a telemetry feed: **it reads and writes the ESC's configuration**. That is ESC
programming over a documented, non-proprietary interface.

### 6.1 How they arrive: drip fed, one per frame

Every telemetry frame carries **one** `(pidx, pdata)` pair. The ESC walks its
own table; the master collects.

A master therefore builds the table by:

1. Caching each `(pidx, pdata)` as it arrives.
2. Reading **parameter 0, which is the parameter count**.
3. Considering the table complete only when every index `0 … count−1` has been
   seen at least once.

At 20 Hz and 32 parameters this is about **1.6 seconds** to a full table. Show
that as progress; do not present a half-read table as the ESC's settings.

Two rules the reference enforces and any implementation should:

- **Stop accepting cached values while writes are outstanding.** Otherwise a
  frame in flight from before the write lands on top of the new value and the
  ESC appears to have ignored it.
- **Withdraw the whole table when a write is scheduled**, and republish it only
  once every index has been re-read. A table that is partly old and partly new
  is the one thing worse than no table.

The cache is capped at **64 parameters** by a 64-bit bitmap; the buffer in the
reference holds 48. Treat 32 as the current reality, 64 as the ceiling.

### 6.2 Control payload — 4 bytes

Carried by `TELE_REQ` and `WRITE_PARAM_REQ`:

| Off | Size | Field |
| ---: | ---: | --- |
| 0 | u16 | `index` — parameter index |
| 2 | u16 | `param` — value to write |

A plain telemetry request sends both as zero. A request frame is **12 bytes**
(6 header + 4 payload + 2 CRC).

Writes are **one parameter per frame**, and each is a separate request with its
own sequence number. There is no page write; a "save" is a queue of single
writes.

### 6.3 The parameter table

Indices as observed. There is no document to check them against (section 1),
so **confirm them with YGE before writing any of them to a real ESC** — that
channel is the one that produced this table in the first place, and writing a
wrong index into an ESC's configuration is not a recoverable experiment. The
supplied comments are misnumbered around indices 26–28, where three
consecutive entries are all labelled 26, so at least one of those three is
wrong on the page even if the values are right.

Reading the whole table is safe and answers most of it: read every index,
change one setting in YGE's own tool, read again, and the index that moved is
the index that means it. That is slower than being told and it needs no
trust.

| # | Meaning | Notes |
| ---: | --- | --- |
| 0 | **Parameter count** | 32 from firmware v1.03503 |
| 1 | Device mode | |
| 2 | BEC voltage | 0.1 V |
| 3 | Motor timing | 0 = auto, 1 = 0°, 6 = 30° |
| 4 | Initial torque | |
| 5 | Governor P gain | 0–9 |
| 6 | Governor I gain | 0–9 |
| 7 | Throttle response | slow / medium / high / custom |
| 8 | Cut-off type | 0 = none, 1 = slow down, 2 = cut off |
| 9 | Cut-off voltage per cell | 0 = 2.9 V |
| 10 | Freewheel demand | |
| 11 | ESC type | low word |
| 12–13 | Firmware version | low, high word |
| 14–15 | Serial number | low, high word |
| 16 | mAh alarm limit | |
| 17 | `STK_ZERO` | input pulse calibration |
| 18 | `STK_RANGE` | |
| 19 | `STK_PERIOD` | |
| 20 | **Motor pole count** | needed for mechanical RPM |
| 21 | Pinion teeth | |
| 22 | Main gear teeth | |
| 23 | Minimum start power | |
| 24 | Maximum start power | |
| 25 | Telemetry type | |
| 26 | Flags | *numbering uncertain from 26 to 28* |
| 27 | Current limit | |
| 28 | Soft start | |
| 29 | Soft run | |
| 30 | Soft blend | |
| 31 | RPM setpoint | |

Parameters 20–22 give the whole gear train, so a bench that reads them can
report **head speed** as well as motor RPM without being told anything.

---

## 7. `status1`

One byte carrying two unrelated things.

### Low nibble — motor state

| | | |
| ---: | --- | --- |
| 0x0 | `DISARMED` | stopped |
| 0x1 | `POWER_CUT` | power cut; see the warning bits for why |
| 0x2 | `FAST_START` | "bailout" |
| 0x4 | `ALIGN_FOR_POS` | positioning |
| 0x6 | `BRAKING_NORM_FINI` | |
| 0x7 | `BRAKING_SYNC_FINI` | |
| 0x8 | `STARTING` | |
| 0x9 | `BRAKING_NORM` | |
| 0xA | `BRAKING_SYNC` | |
| 0xC | `WINDMILLING` | turning, not driven — idle |
| 0xE | `RUNNING_NORM` | running normally |

0x3, 0x5, 0xB, 0xD and 0xF are reserved. Display an unknown state as its
number; do not map it onto a neighbour.

### High nibble — warnings, and the trap in it

| Mask | |
| --- | --- |
| 0x10 | Undervoltage |
| 0x20 | Over-temperature |
| 0x40 | Over-current |
| 0x80 | **These warnings refer to the BEC**, not the ESC |

**The encoding is overloaded and the overload is not optional to understand.**
`0x80 | 0x40` — "BEC over-current" — cannot occur, so that exact combination is
reused to mean **setpoint noise**: the ESC is telling you its *input signal* is
dirty. Decode in this order:

1. If the high nibble is exactly `0xC0` → **setpoint noise**. Stop.
2. Otherwise bit 0x80 selects the subject: set → BEC, clear → ESC.
3. Bits 0x10 / 0x20 / 0x40 are then that subject's warnings.

Get step 1 wrong and a noisy servo lead reads as a BEC over-current fault,
which sends somebody looking for a short that is not there.

### The warnings are qualified by the state

A warning bit alone is not a fault. It becomes one in combination:

| | Is a fault when |
| --- | --- |
| No warning bits set | state is `POWER_CUT` → **over-voltage** |
| Undervoltage | state is **below** `STARTING` (< 0x08) |
| Over-temperature | state is `POWER_CUT` |
| Over-current | state is `POWER_CUT` |

So the same bit is a caution while running and a fault once the ESC has cut
power, and over-voltage has no bit of its own at all — it is *the absence* of
warnings while the power is cut. A decoder that reports bits without the state
will both cry wolf and miss the one condition that has no flag.

---

## 8. Defects in the material as supplied

The protocol details arrived as working code rather than as a document, and
that code has at least six faults. They are recorded here for three reasons:
they are worth reporting back to YGE; anything transcribed from that code
would inherit them; and each one is a place where "what the code does" and
"what the protocol means" have to be told apart.

The code itself is not in this repository — see section 1.

| | |
| --- | --- |
| **1. The init entry point returns nothing** | Declared as returning a success flag, then falls off the end without a `return`. Undefined behaviour, and the caller reads whatever is in the return register — so initialisation reports success or failure at random. |
| **2. Oversized request corrupts the send** | The request builder sets the length field, *then* returns early if that length exceeds the buffer — leaving a length that says more than was written. The next transmission sends stale bytes past the end of the built frame. |
| **3. Timing arguments are ignored** | The builder takes a frame period and a timeout and assigns neither; both assignments are commented out. In the extract supplied, the period and timeout are never set from a request at all — possibly an artefact of pulling one protocol out of a driver that handled a dozen. |
| **4. Sequence number read from the send buffer** | It increments the previous request's byte in place rather than keeping a counter, so a builder that returns early (defect 2) leaves the sequence advanced without a frame going out. |
| **5. Struct casting over the wire buffer** | Assumes little-endian, assumes no padding, and performs unaligned 16-bit reads. It happens to work on the ABIs it has been run on. |
| **6. Parameter comments misnumbered** | Three consecutive entries labelled 26. Whether the *values* are off by one or only the comments is exactly the kind of question that must be answered before writing to an ESC. |

One further difference, not a defect but worth knowing: that code
resynchronises by discarding its buffer a byte at a time. **rcbench's own frame
decoder already scans every sync candidate in its buffer and prefers the
earliest complete frame**, which recovers a good frame arriving behind noise
that happens to contain a plausible sync — a case the byte-at-a-time approach
drops. See [the link](Link.md); reuse that decoder's shape.

---

## 9. What must be measured before this is trusted

Cheap, and each one converts a reading of somebody's code into evidence about
the wire.

1. **`rpm` scale.** Known motor, known pole count, known mechanical RPM.
   Settles ×10 versus ÷10 (section 5).
2. **CRC seed.** Capture one frame; confirm the CRC verifies with seed
   0x0000 and fails with 0xFFFF.
3. **Frame length.** Confirm a v3 telemetry frame is 34 bytes on the wire and
   that `frame_length` counts the CRC.
4. **Legacy header.** Confirm a pre-v3 ESC really omits `seq` and `device`,
   and that the payload therefore starts at offset 4.
5. **Turnaround.** Measure the gap between the last byte of a request and the
   first byte of the reply. This sets the coprocessor's timing the way
   `LINK_TURNAROUND_US` does for the panel link.
6. **Parameter indices**, confirmed with YGE or by read-modify-read against
   their own tool — **before any write**. Section 6.3.
7. **`status2` and `reserved1`.** Log them across a session. If `reserved1` is
   a consumption high byte it will move on a long run.

---

## 10. Where this lands in rcbench

### It belongs on the coprocessor

Everything with a deadline is the coprocessor's. This is a 20 Hz request /
response loop with a turnaround measured in microseconds and a timeout that
must fire on time whether or not bytes arrive — the panel has no business
anywhere near it.

The panel sees the result the way it sees everything else: as a `bench_state`
read through the existing page protocol. **No OpenYGE frame ever crosses the
panel link**, which is the rule [the whole two-processor split](Link.md) rests
on.

### What it changes

This is step 3 of the order of work — *"make the numbers real; the bench stops
simulating"* — and it arrives without the current sensor that step assumed.

A YGE ESC reports **voltage, current, consumption, eRPM, and four
temperatures** on its own. Every field of `bench_state` that is modelled today
has a real source in section 5, so the SIMULATION watermark comes off the
screen for anyone running a supported ESC, and it does so **before** the INA228
that is back-ordered into January 2027 arrives.

It does not replace that sensor. The ESC reports its own input current, which
is what the ESC believes about itself; an independent shunt is what the bench
knows. Both, eventually, and the disagreement between them is itself a
measurement.

It does not change the safety arrangement either. A throttle command still
travels as a register write over the panel link and still lives behind the
heartbeat and the coprocessor's failsafe — [safety](Safety.md) is unaffected by
where the numbers come from.

### What it adds that was not planned

- **ESC programming**, over a documented interface, with no reverse
  engineering and nobody's permission — against BLHeli_S and AM32, which the
  record already names as the first programming targets *because* they need
  nobody's permission. This is a third and it is arguably easier than either.
- **Head speed**, free, from parameters 20–22.
- **A fault display with real content**: section 7 is a genuine diagnostic,
  not a status LED.

### Suggested shape

    shared/openyge/          pure C, host-tested, no SDK
      openyge_frame.c        encode and decode, reusing link_crc with seed 0
      openyge_status.c       status1 -> state, subject, warnings, faults
      openyge_params.c       the drip-fed cache and its completeness rule
      openyge_session.c      the request/response state machine, fed bytes
                             and a clock, exactly like
                             [the servo searches](Servo.md)

Every one of those is testable on the host against captured and synthesised
frames, including the ones this spec says to be suspicious of: a truncated
frame, a frame with a good CRC and an impossible length, a pre-v3 ESC that
never answers a request, a parameter table that never completes, and `0xC0`
in the warning nibble.

The reference's own error counters — bytes, frames, timeouts, CRC errors, sync
errors — are worth keeping. rcbench's link decoder already counts the same
four, and the analyser screen already promises *"a raw view: bytes, gaps,
errors, framing"*.

### One thing to decide first

**Which ESCs are being supported.** OpenYGE is YGE's protocol. The reference
sits in a switch beside Hobbywing, Kontronik, Scorpion, OMP, ZTW, APD, FLY,
Graupner and XDFly, several of which share its state machine and none of which
share its frame format. Building the session layer so a second protocol can be
added is nearly free now and expensive later — but building *two* protocols
before either has been tested against hardware would be inventing requirements.

One protocol, proven on a real ESC, then the second.
