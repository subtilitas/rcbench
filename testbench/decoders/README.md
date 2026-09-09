# Decoders

sigrok decodes what it has a decoder for. It has one for UART (universal
asynchronous receiver-transmitter), which covers SBUS, OpenYGE and the
programmer's serial protocols once the framing is known. It has none for
DShot, and DShot is where most of this project's unverified numbers are.

So a DShot decoder belongs here, and it is written against evidence rather
than against the same specification the firmware was written from — otherwise
the two agree for the same wrong reason and the measurement proves nothing.

**How it is checked without hardware.** `shared/dshot/dshot_frame.c` builds a
frame from a throttle value and a cyclic redundancy check. The host suite can
emit that frame as a waveform, and the decoder must read back the value and
the check that went in. A decoder that passes that is worth pointing at the
coprocessor; one that has not is not.

Nothing is written here yet, and most of it does not wait on the bench.

- **The round trip** against this tree's own frame builder can be written now.
  It proves the decoder transcribes correctly.
- **Published vectors** can be written now too, and they are what proves the
  convention: a throttle value and the frame it must produce, taken from the
  protocol's documentation rather than from an implementation. This is the
  check that catches a bit order or a checksum this project has read the same
  wrong way twice.
- **A capture of another implementation** is the only part that waits on
  hardware, and it is the weakest of the three to be missing.

`../README.md` sets out why the round trip alone is not enough, under
*Trusting a decoder before trusting a measurement*.
