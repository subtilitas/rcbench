# bleuart

This module is a BLE (Bluetooth Low Energy) central for a peer with a
byte stream behind a GATT (generic attribute profile) service. Its default
profile is the ISSC/Microchip Transparent UART service, a transparent UART
(universal asynchronous receiver-transmitter). It scans and lists the
sensors in range, keeps one bound address, connects to it, finds the
service's characteristics, subscribes to the peer's byte stream, sends
commands to the peer and reconnects after a drop. The core is a state
machine fed events and a millisecond clock. It hands its caller actions
for the host stack and the peer's stream bytes for a parser. A profile
(`bleu_profile_t`) names another service, its characteristics by role and
the connection parameters. A hub (`bleuart_hub.h`) runs up to 5 machines
on one host stack.

The module has no pin driver and no radio code: the caller runs the BLE
host stack (NimBLE on the panel) and turns its callbacks into events and
the actions into its calls.

In this repository it reads the WitMotion BWT901CL angle sensor, whose
module advertises as `HC-06` with the 16-bit service UUID (universally
unique identifier) 0x18F0. The stream bytes go to `protocols/witmotion`.

| Part | Files | Depends on |
|---|---|---|
| Core | `bleuart.c`, `bleuart_adv.c`, `include/bleuart.h` | C11 standard headers |
| Hub | `bleuart_hub.c`, `include/bleuart_hub.h` | the core |

## Constraints

- Plain C11. The header has `extern "C"` guards and compiles by itself as
  C11 and as C++17 (`tools/check_protocols.py`).
- The core includes `<stdbool.h>`, `<stddef.h>`, `<stdint.h>` and
  `<string.h>` and no file outside this folder.
- No heap, no floating point, no mutable global or static state, no clock
  read and no I/O (input/output). A machine (`bleu_t`) is 1016 bytes on
  x86-64, an event (`bleu_evt_t`) 64 bytes, an action (`bleu_act_t`) 52
  bytes, a profile (`bleu_profile_t`) 104 bytes, a hub (`bleu_hub_t`) 1120
  bytes. `bleuart.c`, `bleuart_adv.c` and `bleuart_hub.c` are 5468, 2569
  and 2991 bytes of code and constant data with gcc 13 `-Os` there.
- Times are unsigned differences of a free-running 32-bit millisecond
  clock the caller passes in, so a wait holds across the clock's wrap at
  2^32ms.
- Every function of one machine, and of one hub, is called from one
  context at a time.
- One link, one scan and one command write at a time per machine. On one
  host stack, one scan or one connect at a time across machines (the hub).
- No pairing, no bond and no security: the sensor takes a connection
  without them, and so does any central in range.

## The profile

A profile names the service by its 128-bit UUID (universally unique
identifier), up to 4 characteristics (`BLEU_CHR_MAX`) with a role each,
and the connect's parameters (`bleu_conn_params_t`). A characteristic is
named by its 128-bit UUID (`uuid`), its 16-bit UUID (`uuid16`), or both.
A discovered characteristic matches when it equals `uuid`, or when it is
`uuid16` in 16 bits or in the 128 bits of the Bluetooth base
0000xxxx-0000-1000-8000-00805F9B34FB (`bleu_uuid_base`). A peer that
declares a 16-bit value in its own vendor base matches only through
`uuid`, so a profile for such a peer names both:

| Role | The peer's characteristic must have | Used |
|---|---|---|
| `BLEU_ROLE_STREAM`, exactly one | NOTIFY and a CCCD (client characteristic configuration descriptor) | subscribed; every notification is stream bytes |
| `BLEU_ROLE_CMD`, at most one | WRITE or WRITE NO RESPONSE; with `BLEU_CHR_OPTIONAL` it may be missing, and `bleu_send()` then refuses | `bleu_send()` |
| `BLEU_ROLE_PARAM`, at most one | nothing: without READ it is not read | read once after each subscribe; its first 20 bytes (`BLEU_PARAM_MAX`) kept |
| `BLEU_ROLE_NONE`, any | nothing | never written, read or subscribed |

A profile with `cccd_next` skips the descriptor discovery: the STREAM's
CCCD is the handle after its value, inside the characteristic. The CCCD
write is the subscribe's only check; the value is never read back. The
Supvan T50M Pro needs it: it answers Find Information with an ATT error
and reads its CCCD back as 00 00 after 01 00 was written (station step
H0, `test/host/fixtures/supvan/README.md`).

`bleu_profile_valid()` holds a profile to these counts, each entry to a
`uuid` or a `uuid16`, and the parameters
to their ranges: interval 6 to 3200 (7.5ms to 4s), minimum up to maximum,
supervision timeout 10 to 3200 (100ms to 32s) and longer than twice
(1 + latency) x the longest interval, connect scan interval 4 to 0x4000
and window 4 up to the interval. `bleu_init()` gives a machine the ISSC
profile, `bleu_profile_issc`, and owner 0; `bleu_setup()` gives it another
profile and an owner, 0 to 7. The profile is kept by pointer.

The ISSC profile, service 49535343-FE7D-4AE5-8FA9-9FAFD205E455,
characteristics by their second UUID group:

| Characteristic | Role | Used |
|---|---|---|
| 49535343-1E4D-4BD9-BA61-23C647249616 | STREAM. Transfer TX: the sensor's UART bytes to the central, NOTIFY | subscribed (0x0001 to its CCCD); every notification is stream bytes |
| 49535343-8841-43F4-A8D4-ECBE34729BB3 | CMD. Transfer RX: bytes to the sensor's UART, WRITE or WRITE NO RESPONSE | the commands, up to 20 bytes each |
| 49535343-ACA3-481C-91EC-D85E28A60318 | NONE. Air Patch: the module's own commands | never written, never subscribed |
| 49535343-6DAA-4D02-ABF6-19569ACA69FE | PARAM. Connection Parameter | read once after each subscribe when it has READ; never written |

The core writes only two handles: 1E4D's CCCD and 8841's value. The value
of 6DAA is kept in `param[0..param_len-1]`; its layout is not known.

## Scan

`bleu_scan()` starts an active scan of 10000ms (`BLEU_SCAN_MS`) with
duplicates filtered, and clears the list. A report is listed when it
carries the machine's profile's advertised service: its 16-bit UUID
(`adv_uuid16`, 0 for none) in a 16-bit UUID list (AD types 0x02 and 0x03)
or its 128-bit UUID (`adv_svc`, NULL for the service UUID `svc`) in a
128-bit list (0x06 and 0x07). A profile with `adv_oui` (3 bytes in
reading order, NULL for none) lists only a device with a public address
that starts with them, whatever it advertises (`BLEU_DEV_OUI`); a random
address with the same bytes, and a device that only advertises the
service, are not listed by it. The ISSC profile advertises 0x18F0 and the
ISSC service UUID. The listing is a hint: 0x18F0 is no Bluetooth SIG
assignment and other modules may use it. The connect checks the service.
`bleu_adv_parse_for()` and `bleu_list_report_for()` take the profile;
`bleu_adv_parse()` and `bleu_list_report()` are the ISSC profile's.

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
and off. While on and bound, the central connects, and after a failed
step or a drop it tries again after 1000ms (`BLEU_RETRY_MS`). A connect
that goes unanswered backs off: one that times out waits 1000ms the first
time, one the host stack ends with a failure, HCI 0x3E (failed to be
established) among them, 7000ms (`BLEU_RETRY_CONNECT_MS`), and each
further one twice the gap before, up to 30000ms (`BLEU_BACKOFF_MAX_MS`).
A scan, a bind, switching on and a connect that comes up start the gaps
over at 1000ms. `bleu_hurry()` after `bleu_bind()` marks the bound
address's next connects as asked for by the operator: they carry
`BLEU_ACT_F_HURRY` in `flags` until one is answered by the host stack or
times out, or the machine is bound again or switched off, and the hub
puts them ahead of other machines' connects that wait. A bound peripheral that is away so holds the host stack's
one connect for 5000ms in 6000ms at first and for 5000ms in 35000ms
after six tries, which leaves the slot to the hub's other clients.

| Step | Action | Timeout | Fails as |
|---|---|---|---|
| connect | `BLEU_ACT_CONNECT` with the profile's parameters in `cp`; the ISSC profile's: interval 30 to 50ms (`BLEU_ITVL_MIN` 24, `BLEU_ITVL_MAX` 40 x 1.25ms), latency 0, supervision timeout 4000ms (`BLEU_SUPERVISION` 400 x 10ms), connect scan 30ms in every 60ms (`BLEU_CONN_SCAN_WINDOW` 0x30, `BLEU_CONN_SCAN_ITVL` 0x60 x 0.625ms); chosen as a phone's central asks them, not measured | 5000ms (`BLEU_CONNECT_MS`), then `BLEU_ACT_CANCEL` | `BLEU_ERR_CONNECT`, retry |
| the service | `BLEU_ACT_DISC_SVC` with the profile's service UUID in `data[0..15]` | 5000ms (`BLEU_GATT_MS`) | none found: `BLEU_ERR_NOT_PEER` (`BLEU_ERR_NOT_WIT` is the same value), disconnect, no retry |
| its characteristics | `BLEU_ACT_DISC_CHRS` in the service's range | 5000ms | the STREAM (1E4D) without NOTIFY, or a required CMD (8841) without WRITE or WRITE NO RESPONSE: `BLEU_ERR_NOT_PEER` |
| the STREAM's CCCD | `BLEU_ACT_DISC_DSCS` from the STREAM's value handle to the next declaration or the service's end | 5000ms | no 0x2902 there: `BLEU_ERR_NOT_PEER` |
| MTU (maximum transmission unit) | `BLEU_ACT_MTU`, 247 asked (`BLEU_MTU`) | 5000ms | an error leaves `mtu` 0 and goes on |
| subscribe | `BLEU_ACT_SUBSCRIBE`, 0x01 0x00 to the CCCD | 5000ms | `BLEU_ERR_GATT`, retry |
| link up | `BLEU_ST_UP`: notifications on the STREAM are stream bytes | | |
| the PARAM (6DAA) | `BLEU_ACT_READ` | 5000ms | an error is kept in `param_status` and the link stays |

A step that times out, and a discovery step that ends with an error,
disconnects with `BLEU_ERR_GATT` and retries. `BLEU_ERR_NOT_PEER` holds
in `BLEU_ST_NOT_PEER` (`BLEU_ST_NOT_WIT`) until the address is bound again
or the central is switched off and on. A connect answered with
`BLEU_STATUS_BUSY` (0xFFFD), which the hub gives while another machine
holds the host stack's one scan or connect, is tried again after
`BLEU_RETRY_MS` and keeps the error of the attempt before it. A connect
answered with `BLEU_STATUS_TURN` (0xFFFC), which the hub gives while it
keeps the next turn for it, is tried again after 20ms
(`BLEU_RETRY_TURN_MS`), the error kept the same way.
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
answers with `BLEU_EV_WRITE_DONE` and its call's own status, at once and
not through an event queue (`bleu_hub_written()` through the hub), since
nothing answers it on the air and a lost answer holds the next write for
`BLEU_WRITE_MS`. `wr` ends
`BLEU_WR_OK` or `BLEU_WR_FAILED` with `wr_status`: the host stack's
status, `BLEU_STATUS_TIMEOUT` after 1000ms (`BLEU_WRITE_MS`) or
`BLEU_STATUS_LOST` for a drop. A write that times out leaves the link up.
`bleu_fail_link()` ends a link whose peer answered with something the
client cannot use, as a failed GATT step (`BLEU_ERR_GATT`): disconnect,
and connect again after 1000ms (`BLEU_RETRY_MS`).
No flow control exists on this service.

`bleu_send_ext()` writes up to 244 bytes (`BLEU_WRITE_EXT_MAX`) with
response, for a peer whose frames are longer than a command write. Its
action, `BLEU_ACT_WRITE_EXT`, names the CMD's value handle (`h1`) and the
length (`h2`) and carries no bytes: the caller supplies them when it
performs the action. It takes the write slot of `bleu_send()`, with its
token, its 1000ms timeout and its `BLEU_EV_WRITE_DONE`, so a command write
and a long write never overlap. It is refused without `bleu_can_send()`,
when the CMD lacks WRITE, for a length of 0 or over 244, and for one over
the link's MTU less 3 (23 less 3 when the exchange failed).

## Interface

`bleuart.h`:

```c
void   bleu_init(bleu_t *b);
bool   bleu_profile_valid(const bleu_profile_t *p);
bool   bleu_setup(bleu_t *b, const bleu_profile_t *p, uint8_t owner);
uint8_t bleu_token_owner(uint16_t token);
void   bleu_enable(bleu_t *b, bool on, uint32_t now_ms);
void   bleu_bind(bleu_t *b, const bleu_addr_t *addr, uint32_t now_ms);
bool   bleu_scan(bleu_t *b, uint32_t now_ms);
void   bleu_scan_stop(bleu_t *b, uint32_t now_ms);
void   bleu_fail_link(bleu_t *b, uint16_t status, uint32_t now_ms);
bool   bleu_send(bleu_t *b, const uint8_t *data, size_t len, uint32_t now_ms);
bool   bleu_send_ext(bleu_t *b, uint16_t len, uint32_t now_ms);
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
caller passes it to the host stack as the callback's argument. A token is
the machine's owner in its top 3 bits (`BLEU_OWNER_SHIFT` 13) and a count
of 1 to 0x1FFF (`BLEU_TOKEN_SEQ`) in the rest; 0 is never one. Every
owner's count wraps after 8,191 tokens, owner 0's too: its tokens run 1
to 8,191 and start at 1 again. An event
whose token is not the step's under way is counted in `stray` and changes
nothing, so a late answer after a timeout, an abort or a drop reaches no
later step. Status 0 is success; a nonzero status is the host stack's own
code and the core only tells it from 0. A discovery's end is its `_DONE`
event with status 0, also when the host stack reports it as an end code.

| Action | NimBLE call | Answered by |
|---|---|---|
| `BLEU_ACT_SCAN` | `ble_gap_disc()`, active, duplicates filtered, `ms` long | `BLEU_EV_ADV` per report; `BLEU_EV_SCAN_END` when it ends early or does not start |
| `BLEU_ACT_SCAN_STOP` | `ble_gap_disc_cancel()` | nothing |
| `BLEU_ACT_CONNECT` | `ble_gap_connect()` to `addr` with the parameters in `cp` | `BLEU_EV_CONNECT`, or `BLEU_EV_DISCONNECT` with the connect's token for a connection that failed to be established |
| `BLEU_ACT_CANCEL` | `ble_gap_conn_cancel()` | the connect's `BLEU_EV_CONNECT` |
| `BLEU_ACT_DISCONNECT` | `ble_gap_terminate()` of `conn` | `BLEU_EV_DISCONNECT` |
| `BLEU_ACT_DISC_SVC` | `ble_gattc_disc_svc_by_uuid()` of the UUID in `data[0..15]` | `BLEU_EV_SVC`, `BLEU_EV_SVC_DONE` |
| `BLEU_ACT_DISC_CHRS` | `ble_gattc_disc_all_chrs()` in `h1..h2` | `BLEU_EV_CHR` each, `BLEU_EV_CHR_DONE` |
| `BLEU_ACT_DISC_DSCS` | `ble_gattc_disc_all_dscs()` after value handle `h1` to `h2` | `BLEU_EV_DSC` each, `BLEU_EV_DSC_DONE` |
| `BLEU_ACT_MTU` | `ble_gattc_exchange_mtu()`, 247 as the preferred MTU | `BLEU_EV_MTU` |
| `BLEU_ACT_SUBSCRIBE`, `BLEU_ACT_WRITE` | `ble_gattc_write_flat()` of `data` to `h1` | `BLEU_EV_WRITE_DONE` |
| `BLEU_ACT_WRITE_NR` | `ble_gattc_write_no_rsp_flat()` | `BLEU_EV_WRITE_DONE` with the call's status |
| `BLEU_ACT_WRITE_EXT` | `ble_gattc_write_flat()` of `h2` bytes the caller supplies, to `h1` | `BLEU_EV_WRITE_DONE` |
| `BLEU_ACT_READ` | `ble_gattc_read()` of `h1` | `BLEU_EV_READ` |

Events without an action: `BLEU_EV_DISCONNECT` of a drop,
`BLEU_EV_CONN_UPDATE` with the new interval, latency and supervision
timeout (`itvl`, `latency`, `supervision`), and `BLEU_EV_NOTIFY`. A
notification longer than 40 bytes (`BLEU_EVT_DATA`) comes as several
events in order. `bleu_event()` returns true for a notification on the
STREAM's handle of the link: `data[0..len-1]` are stream bytes. Any
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

## The hub

`bleuart_hub.h` runs up to 6 machines (`BLEU_HUB_CLIENTS`) on one host
stack. Each machine gets its owner, 0 to 5, with `bleu_setup()`, and the
hub a client of that owner with `bleu_hub_add()`. The caller hands each
event of the host stack to `bleu_hub_event()`, then each event
`bleu_hub_next_event()` gives to that owner's machine. It hands each
action of a machine to `bleu_hub_action()` with the owner, then performs
each action `bleu_hub_next_action()` gives, in order, and answers a
`BLEU_ACT_WRITE_NR` at once with the host call's status through
`bleu_hub_written()`. `bleu_hub_tick()` runs the hub's timers; call it at
least every 10ms.

| Event | Goes to |
|---|---|
| `BLEU_EV_ADV` | the owner of the scan under way, else the owner of the last one |
| `BLEU_EV_CONNECT` | the token's owner; status 0 makes the handle that owner's |
| `BLEU_EV_DISCONNECT` | the token's owner, else the owner of the handle |
| `BLEU_EV_CONN_UPDATE`, `BLEU_EV_NOTIFY` | the owner of the handle, else the token's owner |
| every other kind | the token's owner |

NimBLE gives every event of a connection the token its connect passed, so
a DISCONNECT that a port held back reaches its own machine after the
handle went to another. An event that no added client takes is counted
in `stray`. A connect that comes up on a handle another client holds ends
that client's link first, with a DISCONNECT of status `BLEU_STATUS_LOST`:
its own DISCONNECT was lost. An action on a handle another client holds
is refused and counted in `refused`; a GATT step is answered with
`BLEU_STATUS_LOST`.

The host stack runs one scan or one connect at a time (NimBLE answers a
second with `BLE_HS_EALREADY` or `BLE_HS_EBUSY`). The hub holds that slot:

| A client asks | While | The hub |
|---|---|---|
| `BLEU_ACT_SCAN` or `BLEU_ACT_CONNECT` | the slot is free or the client's own | passes it on and gives the slot to it |
| `BLEU_ACT_SCAN` | another client scans, or its scan waits | answers `BLEU_EV_SCAN_END` with `BLEU_STATUS_BUSY` |
| `BLEU_ACT_SCAN` | another client connects | cancels that connect and holds the scan until the connect's event, or for 2000ms (`BLEU_SETTLE_MS`); a failed event goes to its client with `BLEU_STATUS_BUSY` |
| `BLEU_ACT_CONNECT` | another client holds the slot, or the slot is free and the next turn is kept for another client | puts the client in the queue, keeps the next turn for the queue's first client when none is kept, and answers `BLEU_EV_CONNECT`: with `BLEU_STATUS_TURN` when the turn is the client's (the machine tries again after 20ms), else with `BLEU_STATUS_BUSY` (after 1000ms); the attempt under way runs to its end |
| `BLEU_ACT_CONNECT` | a scan holds the slot and the next turn is kept for the client, unhurried | answers `BLEU_EV_CONNECT` with `BLEU_STATUS_BUSY`: a scan holds the slot for up to 12000ms, so the machine comes back on its 1000ms retry |
| `BLEU_ACT_CONNECT` with `BLEU_ACT_F_HURRY` | the next turn is kept for an unhurried client | takes that turn; the other client goes back to the queue's front |
| `BLEU_ACT_CONNECT` | the slot is free and the next turn is the client's | passes it on, gives the slot to it and ends the kept turn |
| `BLEU_ACT_CANCEL`, `BLEU_ACT_SCAN_STOP` | another client holds the slot | drops it |

The slot ends on its answer (`BLEU_EV_SCAN_END`, `BLEU_EV_CONNECT`, or a
`BLEU_EV_DISCONNECT` with the connect's token), on its holder's
`BLEU_ACT_SCAN_STOP`, or 2000ms after the action's own duration (12000ms
for a scan, 7000ms for a connect). With one client every action and every
event passes as it would without the hub.

The queue (`q`, up to 6 clients) holds the clients whose connect was
answered busy or with the turn, first come first. A hurried client goes
behind the hurried clients that wait and ahead of every other. A client
whose connect a scan cut goes ahead of the unhurried clients that wait.
A client that waits keeps its place when it asks again; a turn kept for
a client that then asks hurried becomes a hurried turn, which no later
hurried client takes. One turn is kept
at a time, for the client the hub took from the queue's front. A kept
turn and the queue do not hold back a scan. A client that waits leaves
the queue 200ms (`BLEU_HUB_TURN_MS`) after the slot became free and its
retry was due (`due`: its last answer and 20ms or 1000ms) without its
connect, and so does a kept turn's client: a client that was switched
off or unbound meanwhile leaves the slot to the others.

A client in the queue takes the slot after at most one connect of each
other client: the attempt under way and one for each client ahead of
it. The turn's client hears `BLEU_STATUS_TURN` at its next retry once
its turn is kept, within 2000ms of an attempt's start, so its 20ms retry
takes the slot when an attempt of 5000ms ends. Beside N clients bound to
peripherals that are away, each attempt holding the slot for 5000ms and
its cancel's event 2ms, a connect so waits at most N x 5023ms, the 20ms
retry and a 1ms tick included: 10046ms for N = 2, 20092ms for N = 4.
With a 5ms poll, as `panel_blec.c` runs, add 5ms an attempt. A connect
the host stack never answers holds the slot until its limit, 7000ms.
A hurried connect waits for the end of the attempt under way, 5000ms at
most, and its 20ms retry, behind only the hurried clients that came
before it; with none under way it goes at once. Each hurried connect and
each scan that goes ahead adds one turn or one scan to the others' wait.
In `test_bleuart_fair.c`, over 225 runs of 2 to 4 absent peripherals, an
unhurried client waits at most 9780ms, 14800ms and 19820ms.

The hub has two out queues of 8 (`BLEU_HUB_OUT`); a full one drops and
counts in `drops`. One event or action puts at most 2 events and 2
actions into them, so a caller that empties them after each call never
fills them.

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
takes to report; the hub with more than one client on NimBLE.

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
address text; `test_bleuart_profile.c` the owner in the tokens, a profile
with other UUIDs, an optional command characteristic, the profile's
rules at their limits, the busy connect's retry and `cccd_next`
subscribing the handle after the STREAM's value with no descriptor
discovery and no read;
`test_bleuart_hub.c` one client through the hub against the same run
without it, three clients coming up one connect at a time, routing by
token and by handle, a scan during another client's connect, a second
scan, a lost and a held-back DISCONNECT, another client's cancel, stop
and handle, the slot's ends and its time limits, an absent peripheral's
gaps doubling to 30000ms beside a Dial that, dropped, is up again after
its 1000ms retry, and failed connects' gaps doubling from 7000ms;
`test_bleuart_hub.c` also a hurried connect keeping the next turn, other
clients answered busy on the free slot, a scan not held back, the turn
lapsing 200ms after the slot freed and not 199ms, and a hurried bind
beside two absent peripherals connecting within 20ms of the attempt
under way ending; `test_bleuart_fair.c` an unhurried connect beside two
absent peripherals getting the slot after their two attempts, a sweep of
2 to 4 absent peripherals at 75 phases each with every client's wait
within one attempt of each other client, the queue's order, a connect a
scan cut waiting in front, the turn answered busy during a scan, and a
client that does not come back leaving the queue after 200ms and not
199ms; `test_bleuart_ext.c` the long write answered, timed out at
1000ms and not 999ms, refused by the peer and lost with the link, never
beside a command write, its length at 0, 244, 245 and the MTU's edge, a
CMD without WRITE; a characteristic found by its 16-bit, its vendor
128-bit and its Bluetooth-base 128-bit UUID and not by another; a scan
listing by address prefix, by service and neither; a long write on
another client's handle answered lost; 6 clients in the hub with the
sixth waiting its turn behind a scan; and a sixth connection the host
refuses leaving 5 links up. The table in `STATUS.md` has the line coverage per file.

```sh
cmake -S test/host -B test/host/build
cmake --build test/host/build
ctest --test-dir test/host/build -R bleuart
```
