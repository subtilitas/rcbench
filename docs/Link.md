# The link

**CAN at 1 Mbit/s** between the panel and the coprocessor. Differential
signalling beside 100–300 A of switching current is what one would choose
anyway; arbitration and hardware error handling are what made it the right
choice over the RS485 this started as.

The protocol copies ArduPilot's **IOMCU**, which has flown this exact problem
for a decade — a small processor owning RC input and PWM output while the main
one does everything else.

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

## The wire

**CAN at 1 Mbit/s.** The panel's TWAI controller against an **XL2515**
(MCP2515-compatible, over SPI) behind a **SIT65HVD230** transceiver on the
coprocessor.

It began as half-duplex RS485, because that is what the panel brought out. The
board turned out to have a CAN path already — an FSUSB42UMX multiplexes it
against USB — and the coprocessor module arrived with a controller on it. The
byte transport has since been **deleted**: its framing, its decoder, both UART
drivers, the direction handling and their suites are gone. What follows is
what that change bought and what it cost.

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

Against the 12–30 kB/s that actually crosses,
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

### Bit timing, and the number that decides it

Both controllers divide a clock into time quanta and split each bit into a
fixed sync quantum plus two programmable segments. Getting that wrong does not
fail cleanly: a node a fraction of a percent off works on a short bench cable
with one other node and starts logging errors once the bus is longer, colder or
busier. So the arithmetic is in `shared/can/can_timing.c` with tests, rather
than in a table of register values copied from an application note.

It insists the bit rate come out **exact** — no approximation — and that rule
is what made the crystal worth checking before anything was wired. The XL2515
divides its crystal by two before the prescaler starts and a bit needs at least
eight quanta, so the crystal sets a hard ceiling that no register value moves:

| crystal | ceiling | payload at that rate | against the 12–30 kB/s that crosses |
| --- | ---: | ---: | --- |
| **16 MHz — what the module has** | **1 Mbit/s** | **51.6 kB/s** | comfortable |
| 8 MHz | 500 kbit/s | 25.8 kB/s | would have been short at the top |

**The module is 16 MHz**, so the budget stands and 1 Mbit/s is reachable
exactly, at the smallest divisor and the fewest quanta the part allows.

That was not taken on trust. The vendor's driver carries CNF triples for ten
standard bit rates; decoding each back into a divisor and a quanta count gives
the advertised rate **at 16 MHz and at no other crystal** — ten independent
confirmations of one number. `test_can_timing` pins it, so a future module with
a different can fails a test rather than a bus.

Two worked examples are checked by hand against the datasheet: 500 kbit/s gives
sixteen quanta, a sample point at 87.5% and CNF1/2/3 = `0x40`, `0xB5`, `0x01`;
1 Mbit/s gives eight quanta and a sample point at 75%. Seventy-five, not the
87.5 asked for, because eight quanta is the fewest a bit may have and one
quantum is therefore an eighth of the bit — nothing lands closer. The vendor's
own table puts that bit at 62.5%, a whole quantum earlier than it needs to be.

### The pins

From the vendor's own driver rather than guessed, recorded in `copro_pins.h`:
**spi1, SCK on GP10, MOSI GP11, MISO GP12, CS GP9, INT GP8**, with the bus
clocked at 10 MHz. The panel's TWAI is on **GPIO19/20** — the native USB pins,
which is why USB and CAN are mutually exclusive and why the console is on UART.

### Reassembly, and where it is not

A CAN frame carries four registers, so a reply to a thirteen-register read
arrives as four messages. There is no reassembly *in the transport*: each frame
says which registers it holds, so a frame is a complete message on its own.

The joining-up happens in the poller, which is the only place that knows what
was asked for. It keeps a bit per register of the window it requested and
answers the caller when the window is full. A part that lands without
completing it is not a fault and is not counted as one; a part that falls
outside the window is, because that is a reply to a question nobody is still
asking. `test_link_loopback` runs the real host against the real dispatcher
over a bus that drops, delays and reverses, including a reply delivered back to
front — order is not information when every frame carries its own offset.

### Keeping a console while CAN is selected

Native USB is the wrong place for it, which is the opposite of the obvious
answer. USB-Serial-JTAG and USB-OTG both live on **GPIO19 and GPIO20** —
dedicated analog pins that cannot be routed anywhere else — and those are the
pair the FSUSB42UMX switches against CAN. Selecting CAN therefore costs the
native console whichever way round the multiplexer is wired, and that is
exactly the session that wants one.

The board's second USB-C socket sits behind a USB-UART bridge and shares
nothing with CAN, so the console is **UART0 primary with USB-Serial-JTAG as a
secondary**. Output goes to both; whichever socket is plugged in shows it. The
secondary costs nothing while USB is selected and means the arrangement fails
soft if the bridged socket turns out to be wired somewhere other than UART0's
default pins.

## The two ends

Both drivers are deliberately dumb: they move frames and nothing else. The page
semantics, the identifier layout, the bit timing and both watchdogs live in
`shared/` and are tested on a laptop — so every decision a driver could make is
one that could only be tested on hardware.

**The panel** (`firmware/panel/components/can_twai`) configures the TWAI
peripheral from the timing solver and switches the board's multiplexer, which
is the one thing it does that is not a wrapper: selecting CAN costs native USB,
so it is a deliberate call rather than something `board_init` does quietly.

**The coprocessor** (`firmware/copro/src/xl2515.c`) is chip-select edges and a
handful of register writes in the order the datasheet gives them. It checks
that the controller wakes in configuration mode, which the datasheet
guarantees — so a failure there is the **SPI** wiring and not the CAN wiring,
and those are opposite ends of the board.

Its transmit buffer holds one frame, so a multi-frame answer waits for each to
win arbitration; the wait is bounded, because a bus that has stopped accepting
must not stall the failsafe.

## What is tested, and where

All of it runs on a laptop with no wire.

The identifier layout is checked bit by bit against the datasheet rather than
by round-tripping values — a round trip agrees with itself even when both
halves share a mistake — and the whole 29-bit space is swept. The bit timing is
solved for exactness rather than approximation, with one worked example pinned
by hand against the datasheet, and a test that both ends land on the *same*
sample point at the link's bit rate: they are not free to decide that
separately, and once did.

`test_link_loopback` runs the two ends against each other — the real host
poller and the real device dispatcher — over a bus that drops, delays and
reverses. No mock of either end, because a mock agrees with whatever the test
expects. It covers a page wider than a frame coming back whole, the pieces
arriving back to front, a window that starts inside a page keeping its offset,
a write acknowledged with what was *stored* rather than what was sent, a
refusal travelling as an answer, a lost piece leaving the request unanswered
rather than half-answered, a reply to an abandoned question being refused, and
the device's own watchdog still firing on a bus that has gone quiet.
