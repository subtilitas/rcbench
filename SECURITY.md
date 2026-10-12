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
| `shared/esc` | ESC (electronic speed controller) profile JSON (JavaScript Object Notation) from an SD card |
| `protocols/kst` | KST servo programmer files from an SD card, and servo replies on the programming wire |
| `protocols/sbus`, and the receiver decoders that follow it | frames from a receiver |
| `protocols/openyge` | ESC telemetry and parameter frames |
| `protocols/dshot` | DShot telemetry bursts from an ESC |
| `protocols/pdmini` | UART (universal asynchronous receiver-transmitter) bytes from the PD (Power Delivery) mini module |
| `protocols/phase_tap` | edges of an ESC motor phase |
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
