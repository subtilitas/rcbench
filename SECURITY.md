# Security policy

## Scope

rcbench is a test bench: two microcontrollers, a touch panel and an SD (Secure Digital) card. It
has no accounts and no user data. It has two remote interfaces, both on the
panel's Wi-Fi radio, and the radio runs only with the bench SAFE, the supply
output off and no log run open.

The first is the panel's Wi-Fi page: an HTTP (hypertext transfer protocol)
server that lists and sends the card's files and takes a panel firmware
image. It runs only while the FILES & UPDATE screen is open, and goes off
after 10 min without a request. It runs as a WPA2 (Wi-Fi Protected Access 2)
access point for 1 client or joins a WPA2 or WPA3 home network. An
8-character access code, new at every start of the radio, guards the page
unless the operator switches it off on the panel; 16 wrong codes switch the
radio off. An uploaded image installs only after a 2 s hold on the panel.

The second is the panel's own requests to GitHub over HTTPS (HTTP over TLS,
transport layer security), on the home network the panel keeps; the panel
serves nothing meanwhile. With a home network kept and REMINDER AT START on
(SETUP > UPDATES), the panel asks once after each start; CHECK FOR UPDATES on
the same screen asks once more. Each check sends one
`GET https://github.com/subtilitas/rcbench/releases/latest` and reads the
status and the `Location` header of the redirect, not the body, within 30 s
for the join, the address and the request together; then the radio goes
off. A request carries the User-Agent `rcbench-panel` and nothing about the
bench; the DHCP (dynamic host configuration protocol) request of the join
carries the panel's name as host name, as a join from FILES & UPDATE does.
The version text is read strictly (`shared/fwupdate` below). A 2 s hold on
the panel, after a check found a newer release, downloads that release's
`rcbench-panel-vX.Y.Z.bin` and `SHA256SUMS` into PSRAM (pseudo-static
random-access memory), following at most 3 redirects and only to
`github.com`, `release-assets.githubusercontent.com` and
`objects.githubusercontent.com`. Every server's certificate chain is checked
against ESP-IDF's (Espressif Internet-of-Things Development Framework)
bundle of common certificate authorities. The image's SHA-256 (secure hash
algorithm, 256 bits) must equal its line in `SHA256SUMS`, and every image
check below runs, before the flash is written.

Current limitations: the page is plain HTTP, so on a home network the code
and the files cross the network unencrypted; the home network's password is
kept unencrypted in NVS (non-volatile storage); an image uploaded over the
page is checked against its own SHA-256, not against a signature, so any
image built as `rcbench-panel` for the ESP32-S3 installs, and on a home
network a device between the browser and the panel can replace the image on
its way; a downloaded image is bound to the release by the certificate
chain and `SHA256SUMS`, not by a signature, so whoever can publish a release
on the repository, or a certificate authority in the bundle that issues a
false certificate, can deliver an image that installs; no certificate's
validity dates are checked, as the panel has no clock; esp_http_client keeps
every response header value in memory without a bound of its own, which only
a server whose certificate chain checks reaches; the coprocessor image a
panel image carries comes with it and is offered to the coprocessor, after
its own 2 s hold, at the next link-up. Two classes of report apply.

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
| `protocols/kst` | servo configuration text files (line length, field values, the CRC-32 (32-bit cyclic redundancy check) checksum line, type and limits against the connected servo) and servo replies (edge timestamps checked against timing windows; the wire has no parity, checksum or length field) |
| `protocols/feetech` | Feetech SCS and STS status frames from the half-duplex servo bus (`feetech_parse()`): two 0xFF header bytes, ID (identifier), a LEN of 2 to 18, the error byte, at most 16 parameter bytes and a checksum; a header with LEN out of range is no frame, a wrong checksum drops the header byte and the search goes on, a good frame from another ID or with another parameter count is reported and not used, and the echo of the request is skipped only when it matches the sent bytes |
| `protocols/sbus`, and the receiver decoders that follow it | frames from a receiver |
| `protocols/openyge` | ESC telemetry and parameter frames |
| `protocols/dshot` | the ESC telemetry reply: 21 line bits, GCR (group-coded recording) decode, 4-bit checksum, extended telemetry frame types |
| `protocols/pdmini` | replies of the PD (Power Delivery) Mini module on its UART (universal asynchronous receiver-transmitter): command byte, length (fixed per command, or a length byte for text) and CRC8 (cyclic redundancy check) |
| `protocols/phase_tap` | words from the DMA (direct memory access) ring of the motor-phase capture: 31-bit counts extended to 64 bits, overrun detection |
| `shared/link` | CAN (Controller Area Network) frames between the two boards, and the board drawing the coprocessor serves (`link_drawing_parse()`: the length is exactly the header, the pads and the shapes; every count, kind and value is in range, and every pad ring and shape lies inside the outline) |
| `shared/can` | CAN controller frames and the CAN self-test status frame |
| `shared/bench` | coprocessor page registers (BENCH, BUS, BUS_IDS, the capability bits of IDENTITY, KST, programming port, SENSE, SERVO_SENSE, SERVO_WIN, SUPPLY, TONE), AS5600 knob register bytes, SD card servo-file listings, `kst_file_parse` results, and the KST journal file from the SD card: at most 399 bytes, an `RCBENCH KST 1` or `RCBENCH KST 2` header, then either nothing or two images of 64 hexadecimal digits with a port line (a port number of 4 or more refuses the text) and, in version 2, a pin line, and a last line that must match the CRC-16 (cyclic redundancy check) of the text before it; a text that fails any check changes nothing |
| `shared/gfx` | UTF-8 text from the SD card (file names, CSV column names and cells, servo file names, ESC profile names and manual text) and the device-under-test name from NVS (non-volatile storage), decoded for drawing |
| `shared/outputs` | link page writes from the panel (offset, count and values for the OUTPUTS, CHAN_CFG, CHANNELS, SERVO, SERVO_SENSE, SUPPLY, SENSE, TONE, KST, BUS, programming-port, BIND_CFG and BIND_OUT pages, and the BIND commit, which applies the staged pages only when its CRC-16 matches them), the output set-up read back from flash, and, on the panel, the OUTPUTS and CHAN_CFG pages and the board number from the IDENTITY page read back from the coprocessor; a binding is read from them only when every slot names a known driver, rate and header pin and the OUTPUTS page renders back unchanged; from CHAN_CFG only the role of each slot's first channel is read |
| `shared/sense` | INA228, INA3221 and AS5600 register bytes (fixed length per register) and the SENSE set-up from the link |
| `shared/ui` | SD card directory listings, CSV column names and cells, and ESC profile names and manual text as drawn, cut and wrapped by character cells; touch coordinates turned into row and key indexes |
| `shared/artwork` | board drawing blocks fetched over the link and drawing records read back from flash (payload checked against a 16-bit CRC and a 32-bit FNV-1a hash before use) |
| `shared/safety` | CONTROL page writes from the link (offset, count and values), which arm and clear the output stage |
| `shared/settings` | setting values and texts loaded from NVS at start-up. Each value is brought to its schema: a switch to 0 or 1, an option out of range and a Sensor SDA (serial data) pin the bus cannot use to the default, a number snapped to its step, where it has one, and clamped to its range. Sensor SCL (serial clock) is derived from Sensor SDA, not read. The text (the device-under-test name) is cut to 23 characters and terminated. Not checked here: the characters of the text (`shared/gfx` decodes them for drawing), and whether a stored value inside today's range is the one that was saved |
| `shared/touch` | the GT911 touch controller's point list as the panel driver reads it: count, track IDs (identifiers), x and y. The panel driver drops a frame whose count is above 5 (`TOUCH_MAX_POINTS`), and `shared/touch` clamps the count to 5 as well; x and y are clamped to the resolution the controller reports at start-up (the LCD (liquid crystal display) size when an axis reads 0 or the read fails), before rotation and mirroring; a track ID repeated in one frame is kept once; a contact that moves more than 120 px (`TOUCH_JUMP_PX`, x plus y distance) in one frame becomes a release and a new press. Not checked here: a reported resolution larger than the screen, which passes coordinates beyond the screen to `shared/ui`; a reported resolution of 32,768 or more on an axis, which the driver casts to a negative `int16_t`, so `touch_map_apply` takes a size of 1 and maps every coordinate on that axis to 1 value; and the range of a track ID |
| `shared/fwupdate` | the answers to the panel's requests to GitHub: the newest release's tag from a `Location` header (`fw_release_parse_tag()`: printable ASCII without space, at most 69 bytes, `https://github.com/subtilitas/rcbench/releases/tag/v` and then `fw_release_parse_version()`: MAJOR.MINOR.PATCH, digits only, each 0 to 65535 in at most 5 digits without a leading zero, nothing after it); a redirect's `Location` (at most 2048 bytes of printable ASCII, `https://` and one of the 3 listed hosts exactly, at most 3 redirects); the HTTP status (200, or 301, 302, 303, 307 and 308 as redirects; anything else refused); the `Content-Length` (required, above 0, not with chunked transfer, at most the slot or 4096 bytes for `SHA256SUMS`); and `SHA256SUMS` (`fw_release_parse_sums()`: at most 4096 bytes ending in a line feed, every line 64 lowercase hexadecimal digits, a space, a space or `*`, and a name of letters, digits, `.`, `-` and `_`, optionally after `./`; exactly one line for the image's name). A panel firmware image uploaded over Wi-Fi or downloaded from GitHub: refused before it is stored when it is larger than the slot (4,063,232 bytes); then the ESP image header (magic byte, chip ID of the ESP32-S3, a segment count from 1 to 16, every segment's length within the file), the application description (magic word, project name `rcbench-panel` with its terminator, a version of the form MAJOR.MINOR.PATCH), the hash flag, the appended SHA-256 over the whole image and no bytes after it. An image that fails any check is not offered for install; a downloaded one also when its SHA-256 differs from its `SHA256SUMS` line or it names another version than its release |
| `shared/webaccess` | requests to the Wi-Fi page: the URI (uniform resource identifier) path of a card file (percent decoding; `..`, an absolute path, a backslash, an encoded `.` or `/` that forms a traversal, NUL, a name over 255 characters and a path over 384 bytes are refused, and the path stays under the card's mount point), the access code from the form and the cookie (8 characters, compared in full), the upload's declared length, and mDNS (multicast domain name system) queries: at most 512 bytes, every label and compression pointer checked against the packet's end, at most 16 pointer hops |

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
