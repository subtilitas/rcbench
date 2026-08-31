# Receiver buses

<sub>**English** · [Deutsch](Receivers-de.md)</sub>

Connect a receiver to the bench and see what it is really sending — including
the things a servo hides from you.

## What is supported

| Bus | Wire | State |
| --- | --- | --- |
| **S.BUS** | inverted 8E2 UART, 100 kbaud, one pin | decoder built and tested; the PIO program that turns the inverted line into bytes is not written yet |
| iBUS, SUMD, CRSF, SRXL2, JETI EX Bus | ordinary or inverted UART, one pin each | planned — each is about a day of decoder work once hardware to test against is on the desk |

Every bus is one signal wire plus ground into a free coprocessor pin. Nothing
needs extra parts.

## What the analyser will show you

Sixteen channels with their movement, the two digital channels, the frame
rate, and the counters — frames, resyncs, bad tails. See
[Screens](Screens.md) for how to read the display itself. What matters here is
what the states mean:

| State | Meaning | What to do |
| --- | --- | --- |
| **LIVE** | frames arriving, transmitter heard | trust the numbers |
| **FRAME LOST** | this frame did not arrive intact | occasional ones are normal at range; frequent ones on the bench are a wiring or decoder problem |
| **FAILSAFE** | the receiver has **lost the transmitter** and is inventing all sixteen values | treat as *stop* — the numbers are perfectly formed and mean nothing |
| **SILENT** | nothing on the wire | receiver power, wiring, or the wrong pin |

The failsafe state is the reason this screen exists. Sixteen plausible,
smoothly-moving channel values are exactly what a receiver in failsafe sends,
and nothing downstream can tell. Use the bench to check what **your**
receiver actually does in failsafe — switch the transmitter off and watch —
because that behaviour is what your model will do at the worst possible
moment, and it is configurable on most receivers.

## S.BUS notes worth knowing

- The protocol has **no checksum**. The decoder frames on the silent gap
  between frames rather than trusting the header byte, so it cannot lock onto
  the middle of a frame and report sixteen plausible-but-wrong channels.
- Futaba's specification says the footer is `0x00`, but receivers in the
  field also send `0x04`, `0x14`, `0x24` and `0x34`. The bench accepts those —
  refusing them would refuse hardware that works.
- Channels are sixteen 11-bit values packed end to end; the two digital
  channels and the failsafe/frame-lost flags ride in the flags byte.

## For implementers

Two rules carried from S.BUS into every decoder that follows: **frame on
something better than a magic byte** where the protocol gives you one, and
**decode the failsafe flag before the channels**, because a protocol that can
lie convincingly will. Every decoder is written from its specification, not
from somebody else's code — the licence reasoning is in
[the record](https://github.com/subtilitas/rcbench/blob/main/STATUS.md).
