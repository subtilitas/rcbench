# Decoders

sigrok decodes what it has a decoder for. It has one for UART (universal
asynchronous receiver-transmitter), which covers SBUS, OpenYGE and the
programmer's serial protocols once the framing is known. It has none for
DShot, and DShot is where most of this project's unverified numbers are.

A DShot decoder belongs in this folder. Validate it with independent
evidence as well as the firmware's frame builder: two implementations
derived from the same specification can share the same mistake.

**How it is checked without hardware.** `protocols/dshot/dshot_frame.c` builds a
frame from a throttle value and a cyclic redundancy check. The host suite can
emit that frame as a waveform, and the decoder must read back the value and
the check that went in. A decoder that passes that is worth pointing at the
IOmcu; one that has not is not.

The decoder is not implemented. Its round-trip and published-vector
checks can be developed without the physical bench.

- **The round trip** against this tree's own frame builder can be written.
  It proves the decoder transcribes correctly.
- **Published vectors** can be written too, and they are what proves the
  convention: a throttle value and the frame it must produce, taken from the
  protocol's documentation rather than from an implementation. This is the
  check that catches a bit order or a checksum this project has read the same
  wrong way twice.
- **A capture of another implementation** requires hardware and provides
  independent evidence for the wire format.

`../README.md` sets out why the round trip alone is not enough, under
*Trusting a decoder before trusting a measurement*.
