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

## The two ends, from the schematic

Read off the board schematic (ESP32-S3-Touch-LCD-7B, May 2025) rather than
inferred from documentation.

**The panel.** U6 is an **SP3485EN** — a 3.3 V transceiver, so no level
shifting and no 5 V anywhere near the module — on **GPIO16 (TX)** and
**GPIO15 (RX)**.

That direction is the opposite of the obvious reading, and the schematic's own
pin table does not settle it: the table calls GPIO15 `RS485_TX`, which names
the *transceiver's* data directions rather than the ESP32's. The connectivity
settles it twice. GPIO15 reaches U6 pin 1, `RO` — the receiver's output, which
the ESP32 cannot drive, so GPIO15 is an input. GPIO16 reaches pin 4, `DI`, and
also the input of the buffer that operates the direction line — and an
automatic-direction circuit only makes sense watching the line the ESP32
transmits on.

`RO` carries a 4.7 kΩ pull-up to the transceiver rail, so the panel's RX idles
high while the receiver is disabled and no spurious start bit appears during
its own transmission.

**The coprocessor** is a MAX485 breakout with an explicit DE/RE pin. That is
the easier half — the turnaround is under firmware control rather than a
property of an RC circuit — and its one trap is the familiar one: do not
release the driver until the last stop bit has left the shift register.

### The direction circuit sets a floor, not a ceiling

```
  TX ──▶ SN74LVC1G125 ──▶ ──[ R76 200k ]── ┬── gate ──▶ Q1 ──▶ DE + /RE
         (/OE to GND)      ◀──[ D7 ]──     │                    ▲
                                          C51 1nF              R79 1k
                                           │                    │
                                          GND              RS485_VCC
```

The buffer follows the transmit line. When it falls, the Schottky dumps the
gate charge at once, Q1 turns off, and R79 pulls DE and /RE high — the driver
is enabled on the first start bit. When the line goes high, C51 charges through
R76 and the driver releases only once the gate reaches the FET's threshold:

    t = −R76 · C51 · ln(1 − Vth / 3.3)

| Vth | hold |
| ---: | ---: |
| 1.0 V | 72 µs |
| 1.5 V | 121 µs |
| 2.0 V | 179 µs |

**The consequence nobody was looking for.** The longest run of high bits inside
an 8N1 frame is nine bit times — eight data bits and the stop bit, with the next
start bit low. If that run outlasts the hold, the gate crosses the threshold
mid-frame, the driver switches off, and the rest of the transmission never
reaches the bus.

Nine bit times must fit inside the worst-case 72 µs, which puts a floor at about
**125,000 baud**. 1.5 Mbaud clears it twelvefold and 256 kbaud twofold;
**128 kbaud clears it by two percent**, which is why the bring-up rate is 256 k
and why `link_wire.h` carries an `#error` for anything below the floor.

This was carried in the record for a while as an open question about a possible
baud *ceiling*. There is no ceiling.

**And the far end must not answer too early.** The hold runs from the last
*falling* edge rather than the end of the frame — a final byte of `0xFF` starts
its hold nine bit times early — so the coprocessor waits a conservative 200 µs
after the last received byte. At 1.5 Mbaud that is 37% on top of a whole-page
transaction; at 256 kbaud, 6%.

One number is not on the schematic: **Q1's threshold**. The parts around it are
named — R76, C51, D7, R79 — but the FET is not, so the floor above is quoted
from the pessimistic end of a plausible range. One measurement settles it:
scope DE against TX at 256 kbaud and read the release directly.

## Watchdogs

**Two, and the tighter one is on the coprocessor.** IOMCU's ratio is worth
copying: the coprocessor fills failsafe values after 200 ms of silence, on its
own authority, while the host escalates after a second — so the end holding the
outputs is always the more suspicious of the two.

**Traffic returning does not lift the failsafe.** A request arriving is proof
the host is alive and stops the silence counter, but the failsafe is latched
and leaving it takes a deliberate write of a known value to the control page.
Those are different facts, and conflating them is how a bench re-arms itself
while nobody is looking at it.

**Both are wrap-safe**, and that is tested at the boundary rather than asserted.
The failure mode is not the obvious one: `now >= then + timeout` is evaluated in
32-bit arithmetic, so the deadline wraps along with the clock and the two agree
perfectly *after* the turnover. They differ **before** it — while the clock is
still large and the deadline has already wrapped small, the naive form reports
the timeout expired the moment the deadline wrapped, firing up to a whole
interval early with the outputs live.

**The coprocessor protects hardware without asking** — overcurrent,
over-temperature, stall timeout, lost link — and reports what it did at the next
poll. It never waits for permission to fail safe. See [Safety](Safety.md).

## Moving to CAN

The board turned out to have a CAN path already — an FSUSB42UMX multiplexes it
against USB — and the coprocessor module arrived with an **XL2515** controller
(MCP2515-compatible, SPI) behind a **SIT65HVD230** transceiver. That changes
the transport, and it is worth being exact about what it buys and what it
costs.

**What it deletes.** CAN arbitrates rather than taking turns, so there is no
direction line. Everything downstream of that goes with it: the RC one-shot,
`LINK_TURNAROUND_US`, the ~125 kbaud floor, Q1's unmeasured threshold, and the
transceiver's 5 V power-up hazard. Three of the six faults the
[bring-up procedure](Bringup.md) was written to tell apart stop being possible
rather than becoming easier to diagnose. The controller also brings a 15-bit
CRC, an acknowledge slot, automatic retransmission and bus-off confinement,
none of which had to be written.

**What it gains.** Arbitration is by identifier, lowest wins — so priority is a
property of the address rather than of a scheduler. A write to the control page
outranks every telemetry read *on the wire*, against traffic already in flight,
with no software involved at either end. RS485 cannot make that promise at any
baud rate, and it is the strongest argument here.

**What it costs.** The ESP32-S3's TWAI is classic CAN only — no FD, 1 Mbit/s
ceiling — and eight data bytes a frame:

| | payload |
| --- | ---: |
| Classic CAN, 1 Mbit/s, 29-bit IDs, worst-case stuffing | **52 kB/s** |
| The same with 11-bit IDs | 62 kB/s |
| RS485 at the 1.5 Mbaud target | 133 kB/s |

Against the 12–30 kB/s of section [what the wire costs](#what-the-wire-costs),
the margin falls from five-to-twelve times to about two. That is thinner and it
is still enough: a `bench_state` poll is thirteen registers, five frames and
**1.55% of the bus at 20 Hz**, and a 60 kB coprocessor image takes 1.2 s.

### The mapping

`link_msg_t` never knew what carried it, so the dispatcher does not change.
What changes is the framing, and the 29-bit identifier turns out to hold
exactly the fields the message already had:

| bits | field | |
| --- | --- | --- |
| 28..26 | priority | 3 bits, lower wins |
| 25..22 | op | a `link_op_t` |
| 21..14 | page | the page map, unchanged |
| 13..6 | offset | first register this frame carries |
| 5..0 | count | registers this frame is about |

Three consequences fall out of that, and each is worth more than it looks:

**A read has no payload.** The whole question is its address, so polling costs
one zero-byte frame. The identifier is arbitrated whether or not it carries
anything.

**Nothing is reassembled.** Each frame carries its own offset and its own
count, so a thirteen-register reply is four independent messages rather than a
sequence. A dropped frame costs one register range instead of a whole
transfer, there is no timer waiting for a continuation that is not coming, and
order does not matter — which `test_link_can` checks by decoding a split reply
backwards.

**The frame CRC is gone.** CAN has one in silicon, with an acknowledge slot and
retransmission behind it. Carrying another two bytes would spend a quarter of
an eight-byte payload duplicating that. End-to-end integrity over something
larger than a page — a firmware image — belongs to that transfer rather than to
the transport.

### What is not built yet

The mapping and its tests. **Not** the drivers: the ESP32-S3 TWAI side, the
XL2515 over SPI, or the bit timing at either end. The firmware still speaks
RS485, and both transports are in the tree while that is true.

And one problem to solve before the first CAN bring-up: the panel's console is
`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` with no secondary, while USB and CAN share
the FSUSB42UMX. **Selecting CAN takes the console away**, which is exactly when
the bring-up report is wanted. Moving the link off RS485 frees GPIO16 and
GPIO15, and a UART console there is the obvious answer.

## The two transports

Both are deliberately dumb: they move bytes and nothing else. Framing, CRC,
page semantics and both watchdogs live in `shared/link` and are tested on a
laptop, so every decision a transport could make is one that could then only be
tested on hardware.

**The panel** (`firmware/panel/components/link_uart`) is an ordinary
full-duplex port that never asserts RTS, because the board switches its own
transceiver. Its pins arrive as arguments rather than being looked up: a
transport that reached into the application for a pin map would be a component
depending on `main`, and would be specific to one board for no gain.

**The coprocessor** (`firmware/copro/src/link_uart.c`) drives DE and /RE, and
does two things the panel's does not:

It waits for `uart_tx_wait_blocking` before releasing the driver. That call
spins on the UART's BUSY flag, which clears only once the *shift register* has
emptied — not merely the FIFO. Releasing on an empty FIFO cuts the final byte
off every frame; the far end correctly refuses the truncated result, so the
link simply never works and nothing says why.

And it waits out the panel's turnaround before answering, measured from the
last byte received — which is later than the last falling edge, and therefore
covers it.

Both refuse a baud rate below the floor rather than running one.

## What is tested, and where

The codec is pure C with the transport injected, so all of this runs on a
laptop with no wire: the CRC against its published check value and against every
single-bit flip in a 64-byte frame; the frame against every single-bit flip and
every truncation of itself; a genuine frame behind a burst of noise containing
false syncs; a good frame following a corrupt one; frames back to back with no
gap; 200,000 bytes of deterministic noise that must never manufacture a frame;
and a frame that verifies but claims an impossible shape, which is a version
mismatch rather than line noise and is refused just the same.

Above that, `test_link_loopback` runs the two ends against each other — the
real host state machine and the real device dispatcher, over a wire that can
corrupt a chosen byte, truncate a frame, or go deaf. No mock of either end,
because a mock agrees with whatever the test expects. It covers a thousand
polls leaving nothing in either decoder, a corrupted request that is never
answered, a corrupted reply that times out and recovers on the next poll, a
refusal travelling as an answer, and the whole failure end to end: the far end
goes silent, both watchdogs fire in the right order, the wire comes back, and
the bench stays disarmed until a deliberate write says otherwise.
