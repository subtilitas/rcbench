# Security policy

## What this project is

rcbench is a hobby test bench: two microcontrollers on a workbench, a touch
panel and an SD card. It has no network stack, no accounts, no user data and no
remote interface. That rules out most of what a security policy usually covers,
and leaves two things that genuinely matter.

## The class of bug that matters most here

**Anything that could make the bench drive an output when it should not, or
defeat one of the stops.** This is a machine that spins propellers. A fault
that causes an unexpected output — or that lets a stop be bypassed, latched
off, or silently re-armed — is the most serious kind of bug this project can
have, whether or not an attacker is involved.

If you find one, please report it the same way as a security issue, and say so
plainly in the title. It will be treated with more urgency than anything else
on this list.

## The realistic attack surface

Everything that parses input somebody else produced:

| | |
| --- | --- |
| `shared/logfile` | CSV from an SD card — the file may come from anywhere |
| `shared/sbus`, and the receiver decoders that follow it | frames from a receiver, on a wire that can be noisy or hostile |
| `shared/openyge` | ESC telemetry and parameter frames |
| `shared/link` | CAN frames between the two boards |

These are all fed malformed input deliberately by the test suite, which runs
under AddressSanitizer and UBSan in CI. A crash, a read past the end of a
buffer, or an infinite loop reachable from any of these inputs is a real bug
and worth reporting.

## Reporting

Use **GitHub's private vulnerability reporting** on this repository
(*Security → Report a vulnerability*), which keeps the report private until
there is a fix. If that is unavailable to you, open a normal issue that says
only that you have something to report and asks for a contact route — do not
put the details in a public issue.

Please include what you can: the input that triggers it, the file or function
you believe is at fault, and what you expected instead.

## What to expect

This is a hobby project maintained by one person in spare time. There is no
service level, no bounty, and no guaranteed response time. What is promised is
straightforward:

- a report will be read and answered;
- a confirmed bug will be fixed in the open, with a test that pins it, and the
  reasoning recorded in the commit;
- credit in the commit and the release notes if you want it, and none if you
  do not.

## Supported versions

`main` only. This is pre-release software under active development; there are
no maintained release branches, and fixes land at the head.

## Out of scope

- **Physical safety of a bench you built.** The design, the parts and the
  wiring are yours; see the safety notice in the README and
  [the Safety page](docs/Safety.md) for what the design assumes you have built,
  particularly the monostable that gates the outputs.
- **Requests to circumvent another product's protection.** This project does
  not accept code or keys extracted from closed firmware or binaries, for any
  device, however convenient. See
  [BLHeli_32](docs/BLHeli32.md) for the one time this came up and how it was
  answered.
