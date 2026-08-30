# Receiver buses

<sub>**English** · [Deutsch](Receivers-de.md)</sub>

What comes out of an RC receiver, decoded. One protocol at a time — each is
about a day's work and each adds something the bench can do.

Built: **S.BUS**.

## S.BUS

Futaba's, and the most common. Sixteen proportional channels plus two digital
ones in twenty-five bytes, on an **inverted 8E2 UART at 100 kbaud** — the
inversion and the two stop bits are why this wants a PIO state machine rather
than a hardware UART.

| | |
| --- | --- |
| byte 0 | header, `0x0F` |
| bytes 1–22 | sixteen channels of eleven bits, packed end to end |
| byte 23 | flags |
| byte 24 | footer |

### It has no checksum, and that shapes everything

There is no CRC, no escaping, and `0x0F` — the header — is an ordinary value
inside channel data. So the header alone cannot say where a frame begins, and a
decoder that trusts it will lock onto the middle of one and report sixteen
plausible-looking channels that are all wrong. Nothing downstream can detect
that: the numbers are in range, they move when the sticks move, and they are
the wrong channels.

**What actually delimits a frame is the gap.** Frames arrive every 7 ms or
14 ms and take 3 ms to send, so there is at least 4 ms of silence between them
against 120 µs between bytes within one. The decoder is fed a timestamp and
uses that gap; the header and footer then confirm rather than carry the whole
job alone.

`SBUS_GAP_US` is 2 ms, which is the only interesting number here — it has to
sit above any plausible inter-byte time and below the smallest inter-frame gap,
and a test pins it between the two. Setting it below a byte time restarts the
frame on every byte, which is the same as having no framing at all, and ten of
the eleven cases fail when you try it.

### The channel packing is where a decoder goes quietly wrong

Sixteen values of eleven bits, packed little-endian end to end across
twenty-two bytes with no alignment to anything, so channel *n* starts at bit
11*n* and lands mid-byte for all but the first. Most implementations carry
sixteen hand-expanded expressions for this, which is sixteen chances to
transpose a shift — and each mistake produces a channel that looks entirely
reasonable and moves the wrong surface.

Here it is arithmetic, and the test walks **every bit of every channel**,
asserting it lands in that channel and nowhere else. That is 176 assertions
about bit positions rather than a handful of sample frames, because sample
frames agree with a decoder that has a systematic error in it.

### The flags matter more than the channels

| bit | |
| --- | --- |
| `0x01` | channel 17 (digital) |
| `0x02` | channel 18 (digital) |
| `0x04` | frame lost — *this* frame did not arrive intact |
| `0x08` | **failsafe** — the receiver has lost the transmitter |

A receiver in failsafe sends sixteen perfectly well-formed channels that mean
nothing. Anything on this bench that drives a servo or an ESC from S.BUS has to
treat that bit as *stop*, not as sixteen valid numbers — which is exactly what
they look like.

### Footers, and being generous where it is free

The specification says the footer is `0x00`. Receivers in the field also send
`0x04`, `0x14`, `0x24` and `0x34`, the low nibble carrying a frame counter on
some of them. Refusing those would refuse hardware that works, so the check is
on the bits that are always clear. A footer that is neither is counted apart
from a resync: a full-length frame with the wrong tail is a receiver speaking a
variant or a decoder framed one byte out, and neither is line noise.

## What is not built

The bytes. The decoder is fed them and does not care where from; the inverted
8E2 receiver is a PIO program on the coprocessor, and it is not written.

## Next

iBUS, SUMD, CRSF, SRXL2 and JETI EX Bus, in whatever order the hardware to test
them against turns up. Two rules carry over from S.BUS and are worth stating
once: **frame on something better than a magic byte** where the protocol gives
you one, and **decode the failsafe flag before the channels**, because a
protocol that can lie convincingly will.

The licensing constraint in [the record](../README.md) applies to every one of
them: written from the specification, not from somebody's line.
