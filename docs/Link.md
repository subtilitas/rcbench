# The link

<sub>**English** · [Deutsch](Link-de.md)</sub>

The two boards talk over **CAN at 1 Mbit/s**. The wiring you need is first;
the protocol reference for anyone changing the firmware follows.

## Wiring

| | |
| --- | --- |
| Bus | classic CAN (no FD), **1 Mbit/s** |
| Panel side | the TWAI controller on **GPIO19/20** |
| Coprocessor side | **XL2515** (MCP2515-compatible) on **spi1**: SCK GP10, MOSI GP11, MISO GP12, CS GP9, INT GP8 — SPI clocked at 10 MHz |
| Transceiver | SIT65HVD230 |
| Termination | at **both** ends |
| Crystal | the XL2515 module must carry **16 MHz** — an 8 MHz module caps the bus at 500 kbit/s and cannot join this link |

**Selecting CAN takes the panel's native USB away.** GPIO19/20 carry both the
native USB and the CAN transceiver, and the board's multiplexer has to choose.
The console is therefore on **UART0** — the board's second USB-C socket,
behind its USB-UART bridge — with USB-Serial-JTAG as a secondary. When CAN is
up, watch the UART socket.

If frames do not cross, work through [Bringing up the link](Bringup.md) — it
tells the failure modes apart before anything gets unplugged.

## What happens when the link fails

The coprocessor fills failsafe values after **200 ms** of silence, on its own
authority; the panel escalates after a second. The end holding the outputs is
always the more suspicious of the two.

The failsafe **latches**. Traffic returning proves the link is alive and stops
the silence counter, but it does not lift the failsafe — leaving it takes a
deliberate act from the panel. A bench must not re-arm itself while nobody is
looking at it.

---

## Protocol reference

Everything below is for changing the firmware, not for using the bench. The
model: up to 32 sixteen-bit registers per **page**, read and written in
windows; adding a capability adds a page, never a message type.

### The identifier carries the whole address

A 29-bit CAN identifier holds every field the message has — so a read is a
zero-payload frame, and priority is a property of the address:

| bits | field | |
| --- | --- | --- |
| 28..26 | priority | 3 bits, lower wins arbitration |
| 25..22 | op | read / write / data / ack / nack |
| 21..14 | page | the page map |
| 13..6 | offset | first register this frame carries |
| 5..0 | count | registers this frame is about |

A write to the control page outranks every telemetry read **on the wire**,
against traffic already in flight, with no software involved at either end.

### Nothing is reassembled

A frame carries up to four registers, each frame its own offset and count — so
a thirteen-register reply is four independent messages, order irrelevant, and
a dropped frame costs one register range rather than a whole transfer. The
joining-up happens in the poller, which tracks the window it asked for and
answers when it is full.

There is no CRC in the payload: CAN carries a 15-bit CRC, an acknowledge slot
and retransmission in silicon. End-to-end integrity over something larger than
a page — a firmware image — belongs to that transfer, not to the transport.

### Bit timing

Solved in `shared/can/can_timing.c` rather than copied from a table, and it
insists the bit rate come out exact. At 1 Mbit/s both ends land on **eight
time quanta with the sample point at 75%** — and a test pins that they land on
the *same* point, because they are not free to decide it separately. The
XL2515 divides its crystal by two before the prescaler, which is why the
16 MHz crystal is a hard requirement and not a preference.

### Budget

A full `bench_state` poll is thirteen registers — five frames, **1.55% of the
bus at 20 Hz**. Worst-case classic-CAN payload at this rate is about 52 kB/s
against the 12–30 kB/s that actually crosses.

### How it is tested

All of it on a laptop, with no wire. The identifier layout is checked bit by
bit against the datasheet across the whole 29-bit space; the timing solver is
pinned to worked examples checked by hand. `test_link_loopback` runs the real
host poller against the real device dispatcher over a bus that drops, delays
and reverses — covering split replies arriving backwards, refused writes
travelling as answers, lost pieces leaving a request unanswered rather than
half-answered, and the device watchdog still firing on a quiet bus.
