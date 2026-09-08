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

Nothing is written here yet, and what it waits on is a capture rather than a
decision. The round trip against this tree's own frame builder can be written
now and needs no hardware; the part that cannot be written yet is the check
that the convention is right, because that needs traffic from an
implementation which is not ours. `../README.md` says which sources of
independence count, under *Trusting a decoder before trusting a
measurement*.
