---
screen: ANALYSER
title: Receiver
order: 40
---

# Receiver

RECEIVER shows what a receiver sends on an S.BUS line: sixteen channels with their history, the two digital channels, the frame rate and the decoder's counters. It opens from the RECEIVER tile on the [menu](overview.md). The tile carries the SIMULATED mark in this release.

## On the bench: NO SIGNAL

![The receiver screen with nothing on the wire](view:analyser-silent)

No firmware image feeds this screen in this release. The S.BUS decoder is built and tested, but neither board has a byte source for it. The screen therefore shows NO SIGNAL and `No signal on the wire.`. The lanes are empty, the values read `---`, and RAW DATA reads `No data decoded yet.`.

## What a fed screen shows

![The receiver screen fed with S.BUS frames in the host renderer](view:analyser)

The pictures in this section come from the host renderer, which feeds the screen with frames. They show what the screen draws once a byte source exists.

| Area | Shows |
|---|---|
| CH01 to CH16 | one lane per channel: about 2.8s of history at 71 frames a second, 200 samples, a bar for the current value and the value in us |
| State block | NO SIGNAL, FRAME LOST, FAILSAFE or LIVE, with one line under it |
| BUS | `S.BUS` |
| RATE | frames a second, in Hz |
| FRAMES, RESYNC, BAD TAIL | frames decoded, re-synchronisations, frames with a footer the decoder refuses |
| DIGITAL | CH17 and CH18 as lamps: the dot lights while the channel is set; they take no tap |

A glitch is a spike in one lane. A dropout is a notch across all sixteen lanes at the same instant.

| State | Line | Meaning |
|---|---|---|
| LIVE | `Signal received from the transmitter.` | frames arrive; the values are valid |
| FRAME LOST | `A damaged frame was received.` | this frame did not arrive intact; frequent on the bench points to wiring |
| FAILSAFE | `These are fallback values.` | the receiver lost the transmitter and sends its failsafe values |
| NO SIGNAL | `No signal on the wire.` | no frame; check receiver power, wiring and pin |

![A receiver in failsafe: every trace red](view:analyser-failsafe)

In FAILSAFE every trace is drawn red. The receiver sends sixteen well-formed values it made up itself. Treat FAILSAFE as a stop, not as sixteen valid channels.

## RAW DATA

RAW DATA shows the last frame as hexadecimal bytes, 8 to a row. The header and footer bytes are drawn in the accent colour. Under them: the byte count and the gap the decoder frames on, `25 bytes; a 2000us gap separates frames`, then the resync and bad-tail counts. CHANNELS and RAW DATA switch the left pane.

## Related pages

- [Menu](overview.md): the SIMULATED mark.
- [Receiver buses](https://github.com/subtilitas/rcbench/blob/main/docs/Receivers.md): the S.BUS frame and the decoder rules.
