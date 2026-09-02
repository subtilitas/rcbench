# Receiver buses

<sub>**English** · [Deutsch](Receivers-de.md)</sub>

Connecting a receiver to the bench and reading its output on the analyser
screen.

## Support

| Bus | Wire | State |
| --- | --- | --- |
| S.BUS | inverted 8E2 UART (universal asynchronous receiver-transmitter), 100 kbaud, one pin | decoder built and tested; the PIO (programmable input/output) program that converts the inverted line into bytes is not written |
| iBUS, SUMD, CRSF, SRXL2, JETI EX Bus | UART, normal or inverted, one pin each | planned |

Every bus is one signal wire plus ground into a coprocessor pin. No external
parts are needed.

## States

The analyser shows sixteen channels with history, the two digital channels, the
frame rate and the counters (frames, resyncs, bad footers). The state block
shows one of:

| State | Meaning | Action |
| --- | --- | --- |
| LIVE | frames arriving, transmitter received | values are valid |
| FRAME LOST | this frame did not arrive intact | occasional at range; frequent on the bench indicates wiring or decoder |
| FAILSAFE | the receiver has lost the transmitter and is sending its failsafe values | treat as stop; the values are well-formed and meaningless |
| SILENT | nothing on the wire | receiver power, wiring, pin |

A receiver in failsafe sends sixteen plausible, smoothly moving channel values.
To check a receiver's failsafe behaviour, switch the transmitter off and watch
the state block. Most receivers make the failsafe behaviour configurable.

## S.BUS

- No checksum. The decoder frames on the inter-frame gap, not on the header
  byte, which is also a valid channel value.
- Frame: 25 bytes. Header 0x0F, sixteen 11-bit channels packed end to end, a
  flags byte carrying the two digital channels, frame-lost and failsafe, and a
  footer.
- Footer: the specification says 0x00; receivers also send 0x04, 0x14, 0x24 and
  0x34. The decoder accepts a low nibble of 0x0 or 0x4.

## Decoder rules

For every decoder: frame on something stronger than a header byte where the
protocol offers it, and decode the failsafe flag before the channels. Every
decoder is written from its specification; the licensing rule is in
[STATUS.md](https://github.com/subtilitas/rcbench/blob/main/STATUS.md).
