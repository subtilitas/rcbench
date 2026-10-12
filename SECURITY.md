# Security policy

## Scope

rcbench is a test bench: two microcontrollers, a touch panel and an SD card. It
has no network stack, no accounts, no user data and no remote interface. Two
classes of report apply.

## Unexpected output

Anything that could make the bench drive an output when it should not, or
defeat one of the stops: a fault that causes an unexpected output, or that lets
a stop be bypassed, latched off or silently re-armed. Report it the same way as
a security issue and say so in the title; it is handled with priority.

## Input parsing

Everything that parses input somebody else produced:

| Module | Input |
| --- | --- |
| `shared/logfile` | CSV (comma-separated values) from an SD card |
| `shared/esc` | ESC (electronic speed controller) profile JSON (JavaScript Object Notation) from an SD card; files over 65536 bytes, nesting deeper than 16 levels, objects of more than 64 members and duplicate keys are refused |
| `protocols/kst` | servo configuration text files (line length, field values, the CRC-32 checksum line, type and limits against the connected servo) and servo replies (edge timestamps checked against timing windows; the wire has no parity, checksum or length field) |
| `protocols/sbus`, and the receiver decoders that follow it | frames from a receiver |
| `protocols/openyge` | ESC telemetry and parameter frames |
| `protocols/dshot` | the ESC telemetry reply: 21 line bits, GCR (group-coded recording) decode, 4-bit checksum, extended telemetry frame types |
| `protocols/pdmini` | replies of the PD (Power Delivery) Mini module on its UART (universal asynchronous receiver-transmitter): command byte, length (fixed per command, or a length byte for text) and CRC8 (cyclic redundancy check) |
| `protocols/phase_tap` | words from the DMA (direct memory access) ring of the motor-phase capture: 31-bit counts extended to 64 bits, overrun detection |
| `shared/link` | CAN (Controller Area Network) frames between the two boards |
| `shared/can` | CAN controller frames and the CAN self-test status frame |

`tools/security_parsers.py --check` derives this list from the public headers
under `shared/` and `protocols/` and fails when the table and the headers
disagree. CI (continuous integration) runs it.

The test suite runs under AddressSanitizer and UBSan
(UndefinedBehaviorSanitizer) in CI. A crash, a read past the end of a buffer or an infinite loop
reachable from any of these inputs is a bug worth reporting.

## Reporting

Use GitHub's private vulnerability reporting on this repository (Security →
Report a vulnerability). If that is unavailable, open an issue that says only
that you have something to report and asks for a contact route; do not put the
details in a public issue.

Include what you can: the input that triggers it, the file or function you
believe is at fault, and what you expected instead.

## What to expect

This is a hobby project maintained by one person. There is no service level, no
bounty and no guaranteed response time.

- A report is read and answered.
- A confirmed bug is fixed in the open, with a test that pins it.
- Credit in the commit and the release notes if you want it.

## Supported versions

`main` only. There are no maintained release branches.

## Out of scope

- Physical safety of a bench you built. The design, the parts and the wiring
  are yours; [Safety](docs/Safety.md) states what the design assumes,
  particularly the monostable that gates the outputs.
- Another vendor's device firmware. Bugs in an ESC, a servo or a receiver
  belong to whoever made it. How this bench behaves when one of them sends
  something unexpected is in scope.
