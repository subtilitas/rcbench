# dialknob

This module is the protocol between the panel and an M5Stack Dial v1.1
used as a wireless knob over BLE (Bluetooth Low Energy). It holds the
packets in both directions with the bounds of every field, the panel's
rules for taking a turn (`dk_track_t`), the link's DEGRADED and LOST
timing (`dk_link_t`) and the Dial's model of what its face shows
(`dk_face_t`).

The module has no pin driver and no radio code: the panel's BLE central
and the Dial's NimBLE peripheral run the host stacks and hand the module
the bytes of each characteristic.

| Part | Files | Depends on |
|---|---|---|
| Codec | `dialknob.c`, `include/dialknob.h` | C11 standard headers |
| Panel side: tracker, link state | `dk_track.c` | the codec |
| Dial side: face model | `dk_face.c` | the codec |

## Constraints

- Plain C11. The header has `extern "C"` guards and compiles by itself as
  C11 and as C++17 (`tools/check_protocols.py`).
- The sources include `<stdbool.h>`, `<stddef.h>`, `<stdint.h>` and
  `<string.h>` and no file outside this folder.
- No heap, no floating point, no mutable global or static state, no clock
  read and no I/O (input/output).
- Times are unsigned differences of a free-running 32-bit millisecond
  clock the caller passes in, so every rule holds across the clock's wrap
  at 2^32ms.
- Every packet is 20 bytes or less (`DK_PKT_MAX`), so each fits one ATT
  (attribute protocol) payload at the default MTU (maximum transmission
  unit) of 23. All fields are little-endian.
- The link is relative. An EVENT carries the Dial's absolute detent
  counter, never a value; the panel moves its value by the difference to
  the last counter it took. No packet in either direction sets a throttle
  or a position.
- No pairing and no security in protocol version 1: any device in range
  that serves the service under the bound address is taken.

## The service

UUIDs (universally unique identifiers), 128-bit, base
`8F3C9D10-2A7B-4D6E-9B1A-5243424E00xx`. `DK_UUID_LE(xx)` gives the 16
bytes least significant first, as on the air and as NimBLE's
`BLE_UUID128_INIT()` takes them.

| xx | Characteristic | Properties | Direction |
|---|---|---|---|
| 00 | the service | in the Dial's advertising data | |
| 01 | EVENT | NOTIFY | Dial to panel |
| 02 | STATUS | NOTIFY, READ | Dial to panel |
| 03 | SCREEN | WRITE NO RESPONSE | panel to Dial, every panel packet |
| 04 | INFO | READ | Dial to panel |

The Dial advertises as `DIAL-xxxx`, the last 4 hex digits of its address.
The service UUID is in the advertising data and the name in the scan
response. `dk_adv_has_service()` finds the UUID in a 128-bit UUID list (AD
types 0x06 and 0x07); a structure whose length runs past the data ends
the walk.

## Packets, Dial to panel

Byte 0 is the kind. A decoder refuses a wrong length, another kind, a field
out of its range and a reserved bit or byte that is not 0. Each encoder
runs its own decoder on the bytes it wrote and returns 0, with the bytes
cleared, when the decoder refuses them; a decoded packet encodes to the
same bytes.

EVENT, kind 0x01, 12 bytes (`DK_LEN_EVENT`). The Dial sends one at once on
a change and every 200ms while idle (`DK_KEEPALIVE_MS`).

| Byte | Field | Range |
|---|---|---|
| 0 | kind | 0x01 |
| 1 | seq | 0 to 255, wrapping; one count for EVENT |
| 2 to 3 | counter | int16, detents since power-up, wrapping, clockwise positive; 16 a turn (`DK_DETENTS_TURN`), 4 quadrature counts a detent (`DK_COUNTS_DETENT`) |
| 4 | buttons | bit0 screen button (GPIO42), bit1 touch on the glass, bit2 Stamp button (GPIO0); bits 3 to 7 are 0 |
| 5 | gesture | 0 none, 1 PRESS (released before 1,000ms), 2 HOLD (1,000ms, `DK_HOLD_MS`, sent once), 3 TAP on the glass |
| 6 | touch x | 0 to 239 with bit1 or a TAP, else 0 |
| 7 | touch y | as touch x |
| 8 to 9 | ms | the Dial's ms clock, low 16 bits |
| 10 to 11 | echo | the low 16 bits of the last STATE's panel ms |

STATUS, kind 0x02, 12 bytes (`DK_LEN_STATUS`), every 1,000ms
(`DK_STATUS_MS`) and on a change.

| Byte | Field | Range |
|---|---|---|
| 1 | seq | 0 to 255, a count of its own |
| 2 to 3 | battery | mV, 2,000 to 5,000; 0 with flag bit2 only |
| 4 | flags | bit0 a USB host reads the Dial's port, bit1 charging, bit2 battery unknown, bit3 no panel packet for 500ms; bits 4 to 7 are 0 |
| 5 to 7 | firmware | major, minor, patch |
| 8 to 9 | uptime | s, wrapping |
| 10 | RSSI | int8, dBm, -127 to 20; 127 not known (`DK_RSSI_NONE`) |
| 11 | refused | panel packets the Dial's decoder refused, wrapping |

INFO, kind 0x03, 20 bytes (`DK_LEN_INFO`), the value of the INFO
characteristic.

| Byte | Field | Range |
|---|---|---|
| 1 | protocol | `DK_PROTOCOL`, 1 to 255 |
| 2 to 4 | firmware | major, minor, patch |
| 5 | flags | bit0 a bound panel is held, bit1 a test build whose console injects input; bits 2 to 7 are 0 |
| 6 | hardware | 1, the M5Stack Dial v1.1 (`DK_HW_DIAL11`) |
| 7 | reserved | 0 |
| 8 to 19 | name | 12 bytes printable ASCII, NUL-padded |

## Packets, panel to Dial

Every packet: byte 0 the kind, byte 1 the panel's packet count (seq), one
count for all kinds, wrapping at 256. The panel writes them to SCREEN
without response: SCREEN on every screen change and every 5,000ms
(`DK_SCREEN_MS`), VALUE in the frame the knob's value changed, LIVE at
20Hz on a bench screen, STATE when nothing else went for 200ms
(`DK_STREAM_MS`).

| Kind | Bytes | Sent |
|---|---:|---|
| 0x10 SCREEN | 20 | on a screen change and every 5,000ms |
| 0x11 NAV | 20 | three a move: the previous, the focused and the next row |
| 0x12 VALUE_META | 20 | on entry to a value |
| 0x13 VALUE | 11 | in the frame the value changed |
| 0x14 LIVE_MOTOR | 18 | at 20Hz on MOTOR & ESC |
| 0x15 LIVE_SERVO | 19 | at 20Hz on SERVO |
| 0x16 STATE | 9 | when nothing else went for 200ms |

SCREEN, after kind and seq:

| Byte | Field | Range |
|---|---|---|
| 2 | face | 1 NAV, 2 VALUE, 3 SERVO, 4 MOTOR, 5 OTHER, 6 HOME |
| 3 | language | 0 EN, 1 DE |
| 4 | flags | bit0 the knob navigates, bit1 the Dial clicks per detent |
| 5 to 19 | title | 15 bytes of text |

NAV:

| Byte | Field | Range |
|---|---|---|
| 2 | index | below count; 0 for an empty list |
| 3 | count | 0 to 255 |
| 4 | slot | 0 previous, 1 focused, 2 next |
| 5 to 19 | item | 15 bytes of text, "" past an end of the list |

VALUE_META and VALUE:

| Byte | Field | Range |
|---|---|---|
| META 2 to 5 | unit | 4 bytes of text, "" none |
| META 6 | decimals | 0 to 4 |
| META 7 | reserved | 0 |
| META 8 to 11 | min | int32 |
| META 12 to 15 | max | int32, at least min |
| META 16 to 19 | step | int32, at least 1 |
| VALUE 2 to 5 | value | int32 |
| VALUE 6 to 9 | was | int32, the value on entry |
| VALUE 10 | flags | bit0 SET mode, bit1 the last turn was refused at a limit |

LIVE_MOTOR:

| Byte | Field | Unit and range |
|---|---|---|
| 2 | flags | bit0 ARMED, bit1 driving, bit2 fault |
| 3 | valid | bit0 speed, bit1 voltage, bit2 current, bit3 power, bit4 ESC temperature, bit5 motor temperature |
| 4 to 5 | throttle | 0.01%, 0 to 10,000 |
| 6 to 7 | speed | rpm, 0 to 65,535 |
| 8 to 9 | voltage | mV, 0 to 65,535 |
| 10 to 11 | current | 10mA, int16 |
| 12 to 13 | power | 0.1W, 0 to 65,535 |
| 14 | ESC temperature | °C, int8, -40 to 125 |
| 15 | motor temperature | °C, int8, -40 to 125 |
| 16 to 17 | run clock | s, 0 to 65,535 |

LIVE_SERVO:

| Byte | Field | Unit and range |
|---|---|---|
| 2 | flags | bit0 ARMED, bit1 driving, bit2 measured angle valid, bit3 feedback valid, bit4 rail valid |
| 3 to 4 | commanded pulse | 0 none, or 400 to 2,500us |
| 5 to 6 | commanded angle | 0.1°, -1,800 to 1,800 |
| 7 to 8 | measured angle | 0.01°, -18,000 to 18,000 |
| 9 to 10 | rail voltage | mV |
| 11 to 12 | rail current | 10mA, int16 |
| 13 to 14 | rail power | 0.1W |
| 15 to 16 | feedback position | 400 to 2,500us |
| 17 to 18 | feedback current | 10mA, int16 |

STATE:

| Byte | Field | Unit and range |
|---|---|---|
| 2 to 5 | panel clock | ms, uint32 |
| 6 | Dial step, throttle | 0.1%, 1 to 100 |
| 7 | Dial step, servo | 0.1°, 1 to 100 |
| 8 | flags | bit0 ARMED, bit1 the panel marks the link DEGRADED |

A reading without its valid bit or flag is 0 on the wire, so each packet
has one form. Values in VALUE_META and VALUE are integers scaled by
10^decimals; `dk_value_text()` writes one with its unit and no space
(`12.5%`). The face kind, not the panel's screen id, crosses the link, so
the Dial never depends on the panel's screen table.

Text fields (title, item, unit, name) are UTF-8, NUL-padded, and hold no
terminator when full. A decoder refuses a malformed sequence, an overlong
form, a surrogate, a code point above U+10FFFF, a control character (below
U+0020, U+007F, U+0080 to U+009F) and a byte other than NUL after the first
NUL. An encoder cuts a field filled to its last byte at the last whole
character; `dk_text_copy()` fills a field from a longer text the same way.

## Taking a turn

`dk_track_feed()` takes one decoded EVENT with its arrival time and
returns the detents to apply. The rules, in order:

| Rule | Condition | Result |
|---|---|---|
| reference | the first packet after `dk_track_reset()`, which the panel calls at every connect and disconnect, whatever its seq | 0, `refs` |
| repeat | seq equal to the last packet's | dropped, `repeats`; nothing changes, the last packet's time counts on |
| stale | arrives more than 300ms (`DK_STALE_MS`) after the packet before; 300ms is taken | 0, a new reference, `stale` |
| glitch | the counter's difference, wrapped to int16, is over 16 detents (`DK_MAX_STEP`); 16 is taken | 0, a new reference, `glitches` |
| turn | otherwise | the difference, -16 to 16 |
| skip | seq numbers between the last and this one | counted in `skipped`; no motion is lost or added, the counter is absolute |

What was turned during a stall is dropped, not applied late. A repeat's
gesture is not handed out twice; the gesture of every other packet is the
caller's to act on, a stale or glitch packet's too.

## The link's state

`dk_link_t` holds the panel's view of the link:

| State | When |
|---|---|
| DOWN | no Dial in use: after `dk_link_init()` or `dk_link_off()` |
| CONNECTING | at the connect, until the first EVENT; no loss clock runs, so a GATT discovery of any length is no loss, and a discovery that never ends is the central's own timeout |
| UP | at each packet |
| DEGRADED | 500ms (`DK_DEGRADED_MS`) or more since the last packet: a notice |
| LOST | at the disconnect, or 1,000ms (`DK_LOST_MS`) or more since the last packet |

`dk_link_step()` moves the state by the clock. A move from UP or DEGRADED
to LOST is a loss. `dk_link_loss_error()` takes each loss once: it returns
true when the bench is ARMED at that moment, and the panel then switches
SAFE as at any error. A loss taken while SAFE is no error and is not kept
for a later ARM. A packet on the same connection after a LOST returns the
link to UP; the tracker's stale rule makes that packet a reference.

The Dial's side shows LINK LOST when no panel packet came for 500ms
(`DK_DIAL_LOST_MS`, `dk_face_lost()`). When LINK LOST has held for 500ms
more, 1,000ms after the last panel packet on the connection
(`DK_DIAL_DROP_MS`, `dk_face_drop()`), the Dial ends the link and sends
nothing until it is connected again. The loss is then the same on both
sides: the panel sees the disconnect, LOST, and switches an ARMED bench
SAFE. A connection that has had no panel packet yet is not ended.

## The encoder's detent

`dk_detent_follow()` turns the encoder's accumulated quadrature count into
the detent EVENT carries. The detent moves only when the count is 3 counts
(`DK_DETENT_HYST`) or more from the current detent's center (4 counts a
detent), so a detent counts at three quarters of its travel, and an
encoder that rests between two counts never clicks. A jump of many counts
moves by as many detents in one call.

## The face model

`dk_face_feed()` decodes each panel packet into `dk_face_t`: the last
SCREEN, the three NAV rows by slot, VALUE_META, VALUE, LIVE_MOTOR,
LIVE_SERVO and STATE, each with a `have_` flag, and `changed` bits for the
caller's redraw. A SCREEN with another face or title empties the NAV rows,
VALUE_META and VALUE. A NAV row with another index or count empties the
other slots. A refused packet counts in `bad`, a repeated seq in
`repeats`, and neither changes the model. `dk_face_link_reset()` empties
the model at a connect and learns the seq again.

## Connection parameters

| Step | Values |
|---|---|
| connect | the central's set |
| after the panel subscribes, asked by the Dial | interval 11.25 to 15ms (`DK_ITVL_MIN` 9, `DK_ITVL_MAX` 12 x 1.25ms), latency 0, supervision timeout 1,000ms (`DK_SUPERVISION` 100 x 10ms) |
| 10,000ms without a turn (`DK_IDLE_MS`), asked by the Dial | latency 12 (`DK_LATENCY_IDLE`), the same interval; the next turn asks for latency 0 again |
| MTU | 247 asked (`DK_MTU`); every packet fits 23 |
| advertising | every 100ms while unbound (`DK_ADV_UNBOUND_MS`), every 500ms while bound and not connected (`DK_ADV_BOUND_MS`) |

## Interface

`dialknob.h`:

```c
size_t  dk_evt_encode(const dk_evt_t *e, uint8_t *out);
bool    dk_evt_decode(const uint8_t *buf, size_t len, dk_evt_t *out);
/* and the same pair for status, info, screen, nav, value_meta, value,
 * live_motor, live_servo and state */
bool    dk_panel_decode(const uint8_t *buf, size_t len, dk_panel_pkt_t *out);
bool    dk_adv_has_service(const uint8_t *ad, size_t len);
size_t  dk_text_copy(char *dst, size_t cap, const char *src);
size_t  dk_value_text(int32_t value, uint8_t decimals, const char *unit,
                      char *out, size_t cap);

void    dk_track_init(dk_track_t *t);
void    dk_track_reset(dk_track_t *t);
int32_t dk_track_feed(dk_track_t *t, const dk_evt_t *e, uint32_t now_ms,
                      dk_take_t *why);

void    dk_link_init(dk_link_t *l);
void    dk_link_off(dk_link_t *l);
void    dk_link_connect(dk_link_t *l, uint32_t now_ms);
void    dk_link_rx(dk_link_t *l, uint32_t now_ms);
void    dk_link_disconnect(dk_link_t *l);
uint8_t dk_link_step(dk_link_t *l, uint32_t now_ms);
bool    dk_link_loss_error(dk_link_t *l, bool armed);

void    dk_face_init(dk_face_t *f);
void    dk_face_link_reset(dk_face_t *f);
bool    dk_face_feed(dk_face_t *f, const uint8_t *buf, size_t len,
                     uint32_t now_ms);
bool    dk_face_lost(const dk_face_t *f, uint32_t now_ms);
bool    dk_face_drop(const dk_face_t *f, uint32_t now_ms);
int32_t dk_detent_follow(int32_t detent, int32_t count);
```

The panel's side, once a packet arrives on EVENT:

```c
static dk_track_t track;
static dk_link_t link;
dk_evt_t e;
dk_take_t why;

if (dk_evt_decode(data, len, &e)) {
    dk_link_rx(&link, now);
    int32_t detents = dk_track_feed(&track, &e, now, &why);
    if (why != DK_TAKE_REPEAT) {
        knob_add(detents);          /* moves nothing while SAFE */
        gesture_take(e.gesture);
    }
}
dk_link_step(&link, now);
if (dk_link_loss_error(&link, bench_armed())) {
    bench_error_safe();             /* every output off */
}
```

## Use in another project

```cmake
add_subdirectory(protocols/dialknob)
target_link_libraries(my_target PRIVATE rcbench_dialknob)
```

Under ESP-IDF (Espressif Internet-of-Things Development Framework) the
folder is a component: add it to `EXTRA_COMPONENT_DIRS` and
`REQUIRES dialknob`.

## Measured and not measured

The host suite runs the codec, the tracker, the link state and the face
model. The Dial's firmware (`firmware/dial`) builds the module; the panel
does not yet.

Not measured: any of it over the air. The round trip of a STATE and its
echo, the share of packets a connection event carries, the rate of
repeated or skipped seq numbers on a real link, and whether 11.25 to 15ms
with latency 0 is granted by the panel's central are all unknown until the
Dial is at the station (PLAN-DIAL slice 4).

## Trademarks

M5Stack is the name of its owner, and Dial, Stamp-S3A and Bluetooth are
names of their owners' products and trademarks. They are used here only to
say which device this code talks to. The project is not affiliated with
the owners. The full notice is in the repository's
[README](../../README.md#legal-notice-and-disclaimer).

## Tests

`test/host/test_dialknob.c` runs every packet at the ends of each field
and one past them, the wrong lengths and kinds, the text rules, the
advertising data walk, the value text, and 1,000,000 random packets, each
in a heap block of its own length so that a read past it fails under
AddressSanitizer, of which every decoded one must encode to its own bytes.
`test_dialknob_track.c` runs each tracker and link rule at its limit, one
past it and across the clock's and the counter's wrap;
`test_dialknob_face.c` the face model.

```sh
cmake -S test/host -B test/host/build
cmake --build test/host/build
ctest --test-dir test/host/build -R dialknob
```
