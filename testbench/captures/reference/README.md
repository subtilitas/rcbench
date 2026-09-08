# Reference captures

Small captures the decoders are tested against, committed because a decoder
with no fixture is a decoder nobody can check after the bench is unplugged.

Two kinds belong here and nothing else:

- **Round-trip fixtures** — a waveform this tree built from a known value, so
  a decoder that reads it back proves it transcribes correctly.
- **Independent captures** — traffic from an implementation that is not ours,
  which is the only thing that can show the convention is right as well. See
  the section on trusting a decoder in `../../README.md`.

Each file is named for what it holds and what took it, and says which of the
two it is. A capture whose origin is not recorded is not evidence.
