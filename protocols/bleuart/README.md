# bleuart

This module is a BLE (Bluetooth Low Energy) central for a sensor behind a
transparent-UART (universal asynchronous receiver-transmitter) service:
the ISSC/Microchip Transparent UART service. It scans and lists the
sensors in range, keeps one bound address, connects to it, finds the
service's characteristics, subscribes to the sensor's byte stream, sends
commands to the sensor and reconnects after a drop. The core is a state
machine fed events and a millisecond clock. It hands its caller actions
for the host stack and the sensor's stream bytes for a parser.

The module has no pin driver and no radio code: the caller runs the BLE
host stack (NimBLE on the panel) and turns its callbacks into events and
the actions into its calls.

In this repository it reads the WitMotion BWT901CL angle sensor, whose
module advertises as `HC-06` with the 16-bit service UUID (universally
unique identifier) 0x18F0. The stream bytes go to `protocols/witmotion`.

| Part | Files | Depends on |
|---|---|---|
| Core | `bleuart.c`, `bleuart_adv.c`, `include/bleuart.h` | C11 standard headers |

## Constraints

- Plain C11. The header has `extern "C"` guards and compiles by itself as
  C11 and as C++17 (`tools/check_protocols.py`).
- The core includes `<stdbool.h>`, `<stddef.h>`, `<stdint.h>` and
  `<string.h>` and no file outside this folder.
- No heap, no floating point, no mutable global or static state, no clock
  read and no I/O (input/output). A machine (`bleu_t`) is 912 bytes on
  x86-64, an event (`bleu_evt_t`) 64 bytes, an action (`bleu_act_t`) 40
  bytes. `bleuart.c` and `bleuart_adv.c` are 4405 and 2423 bytes of code
  and constant data with gcc `-Os` there.
- Times are unsigned differences of a free-running 32-bit millisecond
  clock the caller passes in, so a wait holds across the clock's wrap at
  2^32ms.
- Every function of one machine is called from one context at a time.
- One link, one scan and one command write at a time.
- No pairing, no bond and no security: the sensor takes a connection
  without them, and so does any central in range.

## The service

Service 49535343-FE7D-4AE5-8FA9-9FAFD205E455, characteristics by their
second UUID group:

| Characteristic | Role | Used |
|---|---|---|
| 49535343-1E4D-4BD9-BA61-23C647249616 | Transfer TX: the sensor's UART bytes to the central, NOTIFY | subscribed (0x0001 to its CCCD, the client characteristic configuration descriptor); every notification is stream bytes |
| 49535343-8841-43F4-A8D4-ECBE34729BB3 | Transfer RX: bytes to the sensor's UART, WRITE or WRITE NO RESPONSE | the commands, up to 20 bytes each |
| 49535343-ACA3-481C-91EC-D85E28A60318 | Air Patch: the module's own commands | never written, never subscribed |
| 49535343-6DAA-4D02-ABF6-19569ACA69FE | Connection Parameter | read once after each subscribe when it has READ; never written |

The core writes only two handles: 1E4D's CCCD and 8841's value. The value
of 6DAA is kept in `param[0..param_len-1]`; its layout is not known.

## Scan

`bleu_scan()` starts an active scan of 10000ms (`BLEU_SCAN_MS`) with
duplicates filtered, and clears the list. A report is listed when it
carries 0x18F0 in a 16-bit UUID list (AD types 0x02 and 0x03) or the
service UUID in a 128-bit list (0x06 and 0x07). The listing is a hint:
0x18F0 is no Bluetooth SIG assignment and other modules may use it. The
connect checks the service.

| Item | Rule |
|---|---|
| AD (advertising data) structure | a length byte, a type byte, length - 1 bytes; a length 0 ends the data, a length past the end drops the rest (`cut`) |
| name | AD 0x09 (complete), else 0x08 (shortened), else ""; ends at its first NUL (the BWT901CL sends `HC-06` in a field of 22 bytes padded with NULs); cut to 23 bytes, each other byte outside 0x20 to 0x7E shown as `?`; a complete name is not replaced by a shortened one |
| row | address and type, RSSI (received signal strength indication) of the last report in dBm, name, flags `BLEU_DEV_18F0`, `BLEU_DEV_ISSC`, `BLEU_DEV_NAME_FULL` |
| reports of one address | merged into one row: an advertisement and its scan response are one device |
| order | strongest RSSI first, an equal RSSI keeps the older row first, an RSSI of 127 (none) last |
| rows | 8 (`BLEU_FOUND_MAX`); a ninth matching device takes the weakest row when it is stronger, else it is left out |
| other devices | counted once per address in `hidden` |

The list remembers 32 addresses it does not list (`BLEU_SEEN_MAX`). Past
them, each report of another device counts in `hidden`, so a device with
a scan response can count twice; a device left out of a full list can
then count as hidden. A device whose scan response comes before its
advertisement loses the response's name.

## Connect

`bleu_bind()` keeps the address; `bleu_enable()` switches the central on
and off. While on and bound, the central connects, and after any failure
or drop it tries again after 1000ms (`BLEU_RETRY_MS`); a connect the host
stack ends with a failure, HCI 0x3E (failed to be established) among
them, is tried again after 7000ms (`BLEU_RETRY_CONNECT_MS`).

| Step | Action | Timeout | Fails as |
|---|---|---|---|
| connect | `BLEU_ACT_CONNECT`, interval 30 to 50ms (`BLEU_ITVL_MIN` 24, `BLEU_ITVL_MAX` 40 x 1.25ms), latency 0, supervision timeout 4000ms (`BLEU_SUPERVISION` 400 x 10ms), connect scan 30ms in every 60ms (`BLEU_CONN_SCAN_WINDOW` 0x30, `BLEU_CONN_SCAN_ITVL` 0x60 x 0.625ms); chosen as a phone's central asks them, not measured | 5000ms (`BLEU_CONNECT_MS`), then `BLEU_ACT_CANCEL` | `BLEU_ERR_CONNECT`, retry |
| the service | `BLEU_ACT_DISC_SVC` | 5000ms (`BLEU_GATT_MS`) | none found: `BLEU_ERR_NOT_WIT`, disconnect, no retry |
| its characteristics | `BLEU_ACT_DISC_CHRS` in the service's range | 5000ms | 1E4D without NOTIFY or 8841 without WRITE or WRITE NO RESPONSE: `BLEU_ERR_NOT_WIT` |
| 1E4D's CCCD | `BLEU_ACT_DISC_DSCS` from 1E4D's value handle to the next declaration or the service's end | 5000ms | no 0x2902 there: `BLEU_ERR_NOT_WIT` |
| MTU (maximum transmission unit) | `BLEU_ACT_MTU`, 247 asked (`BLEU_MTU`) | 5000ms | an error leaves `mtu` 0 and goes on |
| subscribe | `BLEU_ACT_SUBSCRIBE`, 0x01 0x00 to the CCCD | 5000ms | `BLEU_ERR_GATT`, retry |
| link up | `BLEU_ST_UP`: notifications on 1E4D are stream bytes | | |
| 6DAA | `BLEU_ACT_READ` | 5000ms | an error is kept in `param_status` and the link stays |

A step that times out, and a discovery step that ends with an error,
disconnects with `BLEU_ERR_GATT` and retries. `BLEU_ERR_NOT_WIT` holds
until the address is bound again or the central is switched off and on.
A drop ends the link with `BLEU_ERR_LOST` and the reason in `reason`.
When the host stack ends a pending step because of the drop before it
reports the disconnect, as NimBLE does, the error is `BLEU_ERR_GATT` with
that step's status. The handles are discovered again on each connect.

A scan while a connect is pending cancels the connect; the connect
resumes when the scan ends. A scan beside a link keeps the link. A
cancelled connect and a disconnect the central asked for settle before
the next connect or scan: until their event comes, or for 2000ms
(`BLEU_SETTLE_MS`). A connect that comes through after it was cancelled or
timed out is disconnected.

## Writes

`bleu_send()` takes 1 to 20 bytes (`BLEU_WRITE_MAX`) while the link is up,
no write is in flight and the 6DAA read is done (`bleu_can_send()`). With
WRITE in 8841's properties it writes with response (`BLEU_ACT_WRITE`),
with WRITE NO RESPONSE only without (`BLEU_ACT_WRITE_NR`); the caller then
answers with `BLEU_EV_WRITE_DONE` and its call's own status. `wr` ends
`BLEU_WR_OK` or `BLEU_WR_FAILED` with `wr_status`: the host stack's
status, `BLEU_STATUS_TIMEOUT` after 1000ms (`BLEU_WRITE_MS`) or
`BLEU_STATUS_LOST` for a drop. A write that times out leaves the link up.
No flow control exists on this service.

## Interface

`bleuart.h`:

```c
void   bleu_init(bleu_t *b);
void   bleu_enable(bleu_t *b, bool on, uint32_t now_ms);
void   bleu_bind(bleu_t *b, const bleu_addr_t *addr, uint32_t now_ms);
bool   bleu_scan(bleu_t *b, uint32_t now_ms);
void   bleu_scan_stop(bleu_t *b, uint32_t now_ms);
bool   bleu_send(bleu_t *b, const uint8_t *data, size_t len, uint32_t now_ms);
bool   bleu_event(bleu_t *b, const bleu_evt_t *e, uint32_t now_ms);
bool   bleu_step(bleu_t *b, uint32_t now_ms, bleu_act_t *out);
bool   bleu_link_up(const bleu_t *b);
bool   bleu_can_send(const bleu_t *b);
bool   bleu_adv_parse(const uint8_t *ad, size_t len, bleu_adv_t *out);
void   bleu_list_clear(bleu_list_t *l);
void   bleu_list_report(bleu_list_t *l, const bleu_addr_t *addr, int8_t rssi,
                        const uint8_t *ad, size_t len);
size_t bleu_addr_format(const bleu_addr_t *a, bool with_type, char *out,
                        size_t cap);
bool   bleu_addr_parse(const char *s, bleu_addr_t *out);
bool   bleu_addr_eq(const bleu_addr_t *a, const bleu_addr_t *b);
```

Every action with a token is answered by events with that token: the
caller passes it to the host stack as the callback's argument. An event
whose token is not the step's under way is counted in `stray` and changes
nothing, so a late answer after a timeout, an abort or a drop reaches no
later step. Status 0 is success; a nonzero status is the host stack's own
code and the core only tells it from 0. A discovery's end is its `_DONE`
event with status 0, also when the host stack reports it as an end code.

| Action | NimBLE call | Answered by |
|---|---|---|
| `BLEU_ACT_SCAN` | `ble_gap_disc()`, active, duplicates filtered, `ms` long | `BLEU_EV_ADV` per report; `BLEU_EV_SCAN_END` when it ends early or does not start |
| `BLEU_ACT_SCAN_STOP` | `ble_gap_disc_cancel()` | nothing |
| `BLEU_ACT_CONNECT` | `ble_gap_connect()` to `addr` with the `BLEU_ITVL_*` set | `BLEU_EV_CONNECT`, or `BLEU_EV_DISCONNECT` with the connect's token for a connection that failed to be established |
| `BLEU_ACT_CANCEL` | `ble_gap_conn_cancel()` | the connect's `BLEU_EV_CONNECT` |
| `BLEU_ACT_DISCONNECT` | `ble_gap_terminate()` of `conn` | `BLEU_EV_DISCONNECT` |
| `BLEU_ACT_DISC_SVC` | `ble_gattc_disc_svc_by_uuid()` | `BLEU_EV_SVC`, `BLEU_EV_SVC_DONE` |
| `BLEU_ACT_DISC_CHRS` | `ble_gattc_disc_all_chrs()` in `h1..h2` | `BLEU_EV_CHR` each, `BLEU_EV_CHR_DONE` |
| `BLEU_ACT_DISC_DSCS` | `ble_gattc_disc_all_dscs()` after value handle `h1` to `h2` | `BLEU_EV_DSC` each, `BLEU_EV_DSC_DONE` |
| `BLEU_ACT_MTU` | `ble_gattc_exchange_mtu()`, 247 as the preferred MTU | `BLEU_EV_MTU` |
| `BLEU_ACT_SUBSCRIBE`, `BLEU_ACT_WRITE` | `ble_gattc_write_flat()` of `data` to `h1` | `BLEU_EV_WRITE_DONE` |
| `BLEU_ACT_WRITE_NR` | `ble_gattc_write_no_rsp_flat()` | `BLEU_EV_WRITE_DONE` with the call's status |
| `BLEU_ACT_READ` | `ble_gattc_read()` of `h1` | `BLEU_EV_READ` |

Events without an action: `BLEU_EV_DISCONNECT` of a drop,
`BLEU_EV_CONN_UPDATE` with the new interval, latency and supervision
timeout (`itvl`, `latency`, `supervision`), and `BLEU_EV_NOTIFY`. A
notification longer than 40 bytes (`BLEU_EVT_DATA`) comes as several
events in order. `bleu_event()` returns true for a notification on
1E4D's handle of the link: `data[0..len-1]` are stream bytes. Any
chunking is taken; the module cuts its notifications at its own UART
chunks, not at the sensor's frames.

A port that queues the events decides with `bleu_evt_admit(kind, free)`
whether one may take a slot: a notification or an advertising report only
while more than 8 slots are free (`BLEU_EVQ_RESERVE`), every other kind
while one is. A full stream then never crowds out a DISCONNECT or an
action's answer, which the link's state hangs on.

Call `bleu_step()` until it returns false after each event and at least
every 10ms. A wait counts from the call that queued its action. The queue
holds 6 actions (`BLEU_ACT_QUEUE`); a full queue drops the action and
counts it in `act_drops`.

```c
static bleu_t ble;
bleu_evt_t ev;
bleu_act_t act;
wit_frame_t f;

bleu_init(&ble);
bleu_bind(&ble, &bound, ms());
bleu_enable(&ble, true, ms());
for (;;) {
    while (queue_take(&ev)) {          /* events from the host stack */
        if (bleu_event(&ble, &ev, ms())) {
            for (unsigned i = 0; i < ev.len; ++i) {
                if (wit_parser_feed(&parser, ev.data[i], &f)) { /* ... */ }
            }
        }
    }
    while (bleu_step(&ble, ms(), &act)) {
        host_stack_do(&act);           /* the table above */
    }
    sleep_ms(5);
}
```

The address text is `00:0C:BF:08:19:29` (most significant byte first,
upper-case hex), with ` P` (public) or ` R` (random) for the stored form;
`bleu_addr_parse()` reads hex in either case and the type letter in upper
case only. Types other than public and random have no letter.

## Use in another project

```cmake
add_subdirectory(protocols/bleuart)
target_link_libraries(my_target PRIVATE rcbench_bleuart)
```

Under ESP-IDF the folder is a component: add it to
`EXTRA_COMPONENT_DIRS` and `REQUIRES bleuart`.

## Measured and not measured

The host suite runs the machine against the scripted peer in
`test/host/ble_sim.h` and the owner's capture of the sensor's
notifications (`test/host/ble_capture.h`, nRF Connect, 2026-11-05). This
code has not run against NimBLE or the sensor.

Not measured: the sensor's advertising interval and its connect and
discovery times; whether the module sends more than one notification per
connection event; whether writes on 8841 reach the sensor; whether the
module answers an MTU above 23; the layout of 6DAA's value; whether a
NimBLE cancel always reports the connect's end; the time a disconnect
takes to report.

## Trademarks

Bluetooth is a trademark of its owner, and Microchip, ISSC, WitMotion,
BWT901CL and HC-06 are names of their makers or their products. They are
used here only to say which service and sensor this code talks to. The
project is not affiliated with the owners. The full notice is in the
repository's [README](../../README.md#legal-notice-and-disclaimer).

## Tests

`test/host/test_bleuart.c` runs the connect sequence, the capture as
notifications and every timeout at its limit, 1ms before it and across the
clock's wrap; `test_bleuart_link.c` the write path, the refused tables,
the drops, the scans beside a connect, OFF in every state and late
answers; `test_bleuart_adv.c` the advertising data, the list and the
address text. The table in `STATUS.md` has the line coverage per file.

```sh
cmake -S test/host -B test/host/build
cmake --build test/host/build
ctest --test-dir test/host/build -R bleuart
```
