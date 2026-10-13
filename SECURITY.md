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
| `shared/bench` | coprocessor page registers (BENCH, KST, programming port, SENSE and SERVO windows, SUPPLY, TONE), AS5600 knob register bytes, SD card servo-file listings, `kst_file_parse` results, and the KST journal file from the SD card: at most 399 bytes, an `RCBENCH KST 1` or `RCBENCH KST 2` header, then either nothing or two images of 64 hexadecimal digits with a port line (a port number of 4 or more refuses the text) and, in version 2, a pin line, and a last line that must match the CRC-16 (cyclic redundancy check) of the text before it; a text that fails any check changes nothing |
| `shared/gfx` | UTF-8 text from the SD card (file names, CSV column names and cells, servo file names, ESC profile names and manual text) decoded for drawing |
| `shared/outputs` | link page writes from the panel (offset, count and values for the OUTPUTS, CHAN_CFG, CHANNELS, SERVO, SUPPLY, SENSE, TONE, KST, programming-port, BIND_CFG and BIND_OUT pages, and the BIND commit, which applies the staged pages only when its CRC-16 matches them), the output set-up read back from flash, and, on the panel, the OUTPUTS and CHAN_CFG pages and the board number from the IDENTITY page read back from the coprocessor; a binding is read from them only when every slot names a known driver, rate and header pin and the pages render back unchanged |
| `shared/sense` | INA228, INA3221 and AS5600 register bytes (fixed length per register) and the SENSE set-up from the link |
| `shared/ui` | SD card directory listings, CSV column names and cells, and ESC profile names and manual text as drawn, cut and wrapped by character cells; touch coordinates turned into row and key indexes |
| `shared/artwork` | board picture chunks fetched over the link and picture records read back from flash (payload checked against a 16-bit CRC before use) |
| `shared/safety` | CONTROL page writes from the link (offset, count and values), which arm and clear the output stage |
| `shared/settings` | setting values and texts loaded from NVS (non-volatile storage) at start-up. Each value is brought to its schema: a switch to 0 or 1, an option out of range and a Sensor SDA pin the bus cannot use to the default, a number snapped to its step, where it has one, and clamped to its range. Sensor SCL is derived from Sensor SDA, not read. The text (the device-under-test name) is cut to 23 characters and terminated. Not checked here: the characters of the text (`shared/gfx` decodes them for drawing), and whether a stored value inside today's range is the one that was saved |
| `shared/touch` | the GT911 touch controller's point list as the panel driver reads it: count, track IDs, x and y. The count is clamped to 5 (`TOUCH_MAX_POINTS`); x and y are clamped to the resolution the controller reports at start-up (the LCD size when it reports 0), before rotation and mirroring; a track ID repeated in one frame is kept once; a contact that moves more than 120 px (`TOUCH_JUMP_PX`, x plus y distance) in one frame becomes a release and a new press. Not checked here: a reported resolution larger than the screen, which passes coordinates beyond the screen to `shared/ui`, and the range of a track ID |

The table holds two kinds of module. The first kind is derived from the
function names in the public headers under `shared/` and `protocols/`. The
second kind is the forced set in `tools/security_parsers.py`: modules with
outside input under names that rule cannot see, `shared/settings` and
`shared/touch`. `tools/security_parsers.py --check` fails when the table and
the two kinds disagree. CI (continuous integration) runs it.

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
