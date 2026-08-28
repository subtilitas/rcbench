# Bringing the link up on silicon

Both ends are written and tested against each other on the host. What is left
is running them against each other on a wire, which is the one part no test
can stand in for.

This page exists because the panel's first hardware boot cost three symptoms
and two root causes, worked out from a description rather than from the board.
The link has more ways to be half-broken than the display does, and most of
them present as "it does not work". So the firmware now says which one.

## Start here: does anything cross the bus?

Before the page protocol means anything, one question has to be answered on
its own: **do frames cross this bus intact?** Answering it together with "does
the link work" is how a bring-up turns into an afternoon — a bit timing that is
slightly wrong, a missing terminator, a transceiver in the wrong mode and a
dispatcher bug all present as "the panel shows no numbers".

So there is an echo test that answers only that. The panel sends a frame, the
coprocessor sends it straight back, the panel checks it came back byte for
byte. No page map, no registers, no state machine. **If this passes and the
link still does not work, the fault is above the wire** — which is worth
knowing before anybody unplugs anything.

### Running it

The coprocessor needs nothing: it tries CAN at boot, says whether the
controller answered, and echoes probes from then on. That costs one register
read per loop and answers only a page the map does not use, so it is left in
permanently — the bring-up tool that is already flashed is the one that gets
used.

The panel side is opt-in, because starting CAN takes native USB away:

```bash
cd firmware/panel
idf.py -DRCBENCH_CAN_SELFTEST=1 build flash
```

**Watch the UART socket, not the USB one.** GPIO19 and GPIO20 carry both the
native USB and the CAN transceiver, and the multiplexer has to choose — see
[the link](Link.md). The console is on UART0 with USB-Serial-JTAG as a
secondary precisely so this session has somewhere to talk.

It runs for five seconds at boot and prints:

    can: 1000000 bit/s: brp 2, tseg1 5, tseg2 2, sjw 2, sample point 75.0%
    panel: CAN self-test: every probe came back intact
      sent 96 echoed 96 corrupt 0 lost 0 stale 0
      round trip min 210 max 480 us
      controller tx_err 0 rx_err 0 bus_err 0

| What it says | What it means | Where to look |
| --- | --- | --- |
| `no probe came back` | Nothing crosses at all | CANH/CANL swapped? Far end powered? Both at the same bit rate? Terminators at **both** ends? |
| `probes come back altered` | Frames cross and arrive wrong | Sample point or bit timing; a missing terminator reflects |
| `probes cross, and not all of them` | Marginal | Timing, one terminator, or a bus longer than the rate |
| `every probe came back intact` | The wire is fine | Anything still wrong is above it |

Corruption outranks loss in that table, and the ordering is deliberate: a
marginal bus does both, loss is the louder number, and it points at cable
length while the corruption says the bit timing is wrong.

The payloads are not arbitrary either. CAN stuffs a complementary bit after
five of the same polarity, so the patterns that stress a marginal bus are long
runs of one level — the probes cycle through all-dominant, all-recessive and
both alternating patterns, and a test sending only counting integers would pass
on a bus that drops real traffic.

If the coprocessor prints `CAN DID NOT ANSWER` at boot, the fault is on **SPI
and not on CAN**: the datasheet guarantees the controller wakes in
configuration mode, so a CANSTAT that says otherwise means nothing is listening
on the SPI bus the driver thinks it has. That is worth telling apart before a
scope comes out.

---

## The RS485 transport, superseded

> The link is moving to CAN — see [the link](Link.md) — and three of the six
> faults below are properties of the direction circuit and stop existing with
> it: the return path that will not release, the turnaround that is too short,
> and corruption from running under the transceiver's baud floor. The
> diagnosis module itself is transport-independent and stays; the wiring
> checks and the direction-circuit measurement below apply only while RS485 is
> what is fitted.

## Before power

| | |
| --- | --- |
| **Flash both ends from the same tree** | A protocol skew produces corruption and staleness as side effects, and both are red herrings. The panel refuses to arm on a major mismatch and says so. |
| **TX to RX, both ways** | Panel **GPIO16 is TX**, **GPIO15 is RX**. Swapped is the commonest wiring fault and presents as total silence. |
| **A common ground** | Two separately-powered boards with only a data pair between them is not a link. |
| **DE and /RE tied** | The board's auto-direction circuit drives them together. See [safety](Safety.md) for why this line is not the heartbeat. |

Start at **256 kbaud** (`LINK_BAUD_BRINGUP`), not the 1.5 Mbaud target. The
floor is about 125 kbaud, set by the direction circuit's RC one-shot; 256 k is
comfortably clear of it and slow enough that a marginal edge is still a good
byte.

## What the console says

The panel prints one block every five seconds while the link is unhealthy and
once a minute when it is:

    LINK requests land, answers do not
      check: direction line not releasing, or /RE still disabled
      panel  polls 412 replies 9 timeouts 403 stale 0 nack 0 crc 0 resync 0
      copro  frames 410 crc 0 resync 0
      round trip min 210 avg 1840 max 4980 us (turnaround allowance 200 us)

The first line is the diagnosis and the second is where to look. The counters
below are both ends' own view, and **the comparison between them is the whole
point**: 410 frames decoded at the far end against 9 replies heard at the near
one is a return-path fault and cannot be anything else. No amount of staring at
the panel's numbers alone would say so.

| What it says | What it means | Where to look |
| --- | --- | --- |
| `no reply to any poll` | Nothing came back at all | Coprocessor powered? TX/RX swapped? Both ends at the same baud? DE stuck high? |
| `answering, wrong protocol` | It talks, but not this protocol | Flash both ends from the same tree |
| `requests land, answers do not` | The far end decoded them; the near end heard nothing | The direction line is not releasing, or /RE is still disabled |
| `frames arrive corrupt` | CRC failures at one end or both | Baud mismatch, or below the transceiver's floor |
| `answers arrive too late` | Replies to questions already abandoned | Turnaround shorter than the transceiver holds the bus |
| `works, and not every time` | Both directions working, sometimes | Marginal baud, or a poll period tighter than the round trip |

Silence is reported as silence rather than as four hundred timeouts, which is
the same fault counted downstream. The order the checks run in *is* the
diagnosis, and `test_link_bringup` constructs each fault rather than waiting to
meet one.

## The three measurements worth taking while it is on the bench

**1. The round trip, at 256 kbaud.** The report gives min, average and max.
The minimum is the interesting one: a request and a reply are about 1.5 ms of
wire between them at this rate, so anything much above that is the far end
waiting out the direction circuit rather than talking. Compare against the
`turnaround allowance` the same line prints.

**2. The direction circuit's release, directly.** Scope DE against TX at
256 kbaud and read how long DE stays asserted after the last falling edge.
This is the measurement that settles [the open question about Q1's threshold
voltage](../README.md#what-is-still-unsettled): the baud floor is currently
quoted from the pessimistic end of a plausible range (1.0 V → 72 µs →
125 kbaud) because the schematic names R76, C51, D7 and R79 but not the FET.
One capture replaces the estimate with a number.

**3. Where the baud actually stops working.** Raise it in steps toward the
1.5 Mbaud target and watch `crc` and `resync` climb. The last rate with a
clean report over a few thousand polls is the real ceiling; take a step back
from it for the value that ships. Record what it was — the wire budget in the
record assumes 1.5 Mbaud and would want rewriting if the board cannot hold it.

## What to write back

The three numbers above, into the record: the measured round trip, the DE
release time, and the highest clean baud. Two entries in
[what is still unsettled](../README.md#what-is-still-unsettled) close on the
second of those, and the first tells the coprocessor's `LINK_TURNAROUND_US`
whether it is generous or optimistic.

Also worth noting is what the *heartbeat* did while all this was happening. Its
monostable is not fitted, so the edges reach a header pin and a scope and
nothing else — but this is the first session where both boards are powered at
once, and it is the cheapest opportunity to confirm the line is edging at the
rate [safety](Safety.md) says it should.
