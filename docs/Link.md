# The link

Half-duplex RS485 between the panel and the coprocessor, 8N1. Differential
signalling beside 100–300 A of switching current is what one would choose
anyway; it is also, conveniently, what the panel already brings out.

The protocol copies ArduPilot's **IOMCU**, which has flown this exact problem
for a decade — a small processor owning RC input and PWM output while the main
one does everything else.

## The frame

```
  0      1      2     3      4       5       6 .. 6+2n      last two
  +------+------+-----+------+-------+-------+-------------+---------+
  | SYNC | LEN  | OP  | PAGE | OFFS  | COUNT | payload ... |  CRC16  |
  +------+------+-----+------+-------+-------+-------------+---------+
```

`LEN` counts every byte after itself, CRC included, so a frame is `LEN + 2`
bytes and a receiver knows how much to read from byte one. Registers are
little-endian and so is the CRC — one convention, not two.

`SYNC` is `0xA5`, chosen because it is neither `0x00` nor `0xFF`: an idle line
and a stuck driver both present as one of those, and a sync byte a fault can
manufacture is not a sync byte.

The CRC covers `SYNC` and `LEN` as well as the payload. A corrupted `LEN` would
be caught by the arithmetic anyway — the CRC would be read from the wrong
offset — but covering it costs nothing and makes the check whole rather than
nearly whole.

| Op | | |
| --- | --- | --- |
| `READ` | 0x01 | host asks for COUNT registers from PAGE:OFFS |
| `WRITE` | 0x02 | host sets them |
| `DATA` | 0x03 | coprocessor answers a read |
| `ACK` | 0x04 | coprocessor accepted a write |
| `NACK` | 0x05 | coprocessor refused it; `regs[0]` says why |

A `READ` carries no payload however large its count: the count is the question,
not the answer.

## Two properties the rest depends on

**The coprocessor never speaks unsolicited.** There is no event op and there is
not going to be one. Nothing arbitrates outbound priority because nothing
competes for it — which is how a stop command is kept from queueing behind a
telemetry burst *by construction* rather than by scheduling. High-rate streams
are served by polling a batch page.

**A corrupt frame is never accepted.** CCITT-FALSE was chosen over the other
CRC-16 variants for one reason: it has an unambiguous published check value
(`0x29B1` over the ASCII string `123456789`), so a second implementation —
written by somebody else, in another language, years from now — can be verified
against one number before it is trusted.

## The decoder resynchronises inside its own buffer

Every sync byte in the buffer is a candidate, and they are examined in order
rather than one at a time.

The reason is the case that breaks the obvious implementation: noise containing
a byte that looks like a sync, followed by a length byte that happens to be
plausible, in front of a genuine frame. Commit to that first candidate and the
decoder sits waiting for bytes that will never make sense while the real frame
— already whole, already in the buffer — goes unreported. That is not a
hypothetical: it was written as a test before the decoder existed, and the
first decoder failed it.

Preferring the earliest *complete* candidate over an earlier incomplete one is
safe. For a later candidate to verify inside a genuine frame, its own CRC would
have to hold by accident.

Waiting for an idle gap instead would be worse: on a polled link the frame
after the corrupt one is the reply.

## What the wire costs

The framing is rate-agnostic. The schedule that uses it is not, and the bench
and the finished board are not the same wire — the module build runs a breakout
at **128 or 256 kbaud** while the finished board runs the panel's own interface
at **1.5 Mbaud**. 8N1 is ten bits per byte:

| | 128 kbaud | 256 kbaud | 1.5 Mbaud |
| --- | ---: | ---: | ---: |
| Throughput | 12.8 kB/s | 25.6 kB/s | 187.5 kB/s |
| A whole-page poll (8 + 72 bytes) | 6.25 ms | 3.13 ms | 0.53 ms |
| Whole-page polls per second | **160** | 320 | 1875 |

A page is 32 registers, so a whole-page transfer is 72 bytes. The research
behind this link put the cap at 64; it follows the page size here instead,
because a bigger frame amortises a turnaround that two smaller ones pay twice.

**This is what makes "nothing raw crosses" load-bearing rather than tidy.** Four
channels of current and voltage streamed raw at 1 kHz is 4,000 registers a
second — about 125 whole pages, **78% of the wire at 128 kbaud**, leaving
nothing for telemetry. At 1.5 Mbaud the same stream is 7% and nobody would
notice. Firmware written against the comfortable case would need redesigning at
exactly the wrong moment.

So it is not sent raw at either rate. The coprocessor accumulates minimum,
maximum and mean per batch and reads charge and energy out of the INA228's
hardware accumulators, and reports those with the rest of the bench state for
almost no extra bytes. Raw samples cross only during a **bounded capture** the
panel asks for by name: one second of four channels at 1 kHz is 8 kB of
samples, 10 kB framed — 0.78 s of wire at 128 kbaud, 67 ms at 1.5 Mbaud. Every
measurement that wants raw samples is a burst of exactly that shape.

The arithmetic is in `shared/link/include/link_wire.h`, and the schedule
derives its rates from the configured baud rather than hardcoding them, so
moving between the two wires is a constant and not a rewrite. An `#error` there
fires if a future page grows the frame past what the bring-up link can afford.

## The two ends are not symmetric

The panel uses **its own on-board RS485 interface**, which Waveshare document as
having automatic transmit/receive control, with A and B leaving on a PH2.0
terminal. It drives no direction line and has no 5 V logic near it.

The coprocessor end is a **MAX485 breakout with an explicit DE/RE pin**. That is
the easier half — the turnaround is under firmware control rather than a
property of an RC circuit — and its one trap is the familiar one: do not release
the driver until the last stop bit has left the shift register.

Two things about that arrangement are not settled and are in the README's open
list. An automatic-direction circuit holds its driver enabled for a fixed time
after the last edge, which puts a floor under bus turnaround that has to be
measured rather than assumed. And the breakout's 5 V receiver output is safe on
the RP2350's Bank 0 only *while the 3.3 V rail is up*, which on a module build
is not the order the rails come up in.

## Watchdogs

**Two, and the tighter one is on the coprocessor.** IOMCU's ratio is worth
copying: the coprocessor fills failsafe values after 200 ms of silence, on its
own authority, while the host escalates after a second.

**The coprocessor protects hardware without asking** — overcurrent,
over-temperature, stall timeout, lost link — and reports what it did at the next
poll. It never waits for permission to fail safe. See [Safety](Safety.md).

## What is tested, and where

The codec is pure C with the transport injected, so all of this runs on a
laptop with no wire: the CRC against its published check value and against every
single-bit flip in a 64-byte frame; the frame against every single-bit flip and
every truncation of itself; a genuine frame behind a burst of noise containing
false syncs; a good frame following a corrupt one; frames back to back with no
gap; 200,000 bytes of deterministic noise that must never manufacture a frame;
and a frame that verifies but claims an impossible shape, which is a version
mismatch rather than line noise and is refused just the same.
