# Bringing the link up on silicon

Both ends are written and tested against each other on the host. What is left
is running them against each other on a wire, which is the one part no test
can stand in for.

This page exists because the panel's first hardware boot cost three symptoms
and two root causes, worked out from a description rather than from the board.
The link has more ways to be half-broken than the display does, and most of
them present as "it does not work". So the firmware now says which one.

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
