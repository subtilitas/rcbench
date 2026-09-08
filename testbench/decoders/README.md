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

Nothing is written here yet: it waits on the two questions at the end of
`../README.md`, because whether a capture can be taken at all decides whether
a decoder can be checked against one.
