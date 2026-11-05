# Reference captures

This folder stores small captures used as decoder fixtures. Committing
them keeps decoder checks reproducible without the physical bench.

Two kinds belong here and nothing else:

- **Round-trip fixtures** — a waveform this tree built from a known value, so
  a decoder that reads it back proves it transcribes correctly.
- **Independent captures** — traffic from another implementation that
  checks protocol conventions independently of this tree's frame builder.
  See the section on trusting a decoder in `../../README.md`.

Each file is named for what it holds and what took it, and says which of the
two it is. A capture whose origin is not recorded is not evidence.
