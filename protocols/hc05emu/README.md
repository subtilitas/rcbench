# hc05emu

This module answers like an HC-05 Bluetooth module in the SPP (serial port
profile) master role, seen from its UART (universal asynchronous
receiver-transmitter). It is a state machine supplied with the UART's
received bytes, the KEY pin's level, the radio's results and a
millisecond clock. It hands its caller the answer lines, the UART's baud
and the radio procedures to run: inquiry, remote name read, pairing,
connect and stop.

In this repository it is the core of `firmware/btbridge`, an ESP32 that
stands in for an HC-05 between the panel and a WitMotion BWT901CL angle
sensor. The panel's own machine (`protocols/hc05`) drives it unchanged.
The module has no pin driver and no radio code: the caller owns the UART,
the KEY input, the LED and the Bluetooth stack.

| Part | Files | Depends on |
|---|---|---|
| Core | `hc05emu.c`, `hc05emu_at.c`, `hc05emu_radio.c`, `hc05emu_int.h`, `include/hc05emu.h` | C11 standard headers |

## Constraints

- Plain C11. The header has `extern "C"` guards and compiles by itself as
  C11 and as C++17 (`tools/check_protocols.py`).
- The core includes `<stdbool.h>`, `<stddef.h>`, `<stdint.h>` and
  `<string.h>` and no file outside this folder.
- No heap, no floating point, no global or static mutable state, no clock
  read and no I/O (input/output).
- Times are unsigned differences of a free-running 32-bit millisecond
  clock the caller passes in, so a wait holds across the clock's wrap at
  2^32ms.
- Every function of one machine is called from one context at a time.
- A command line is held to 79 bytes (`HC05EMU_LINE_MAX` - 1); a longer
  one answers `ERROR:(0)`. An answer line is at most 64 bytes with its
  CR LF (`HC05EMU_OUT_MAX`). 12 actions wait for the caller at most
  (`HC05EMU_Q_MAX`); one more is dropped and counted in `q_lost`.
- An inquiry keeps 16 devices (`HC05EMU_FOUND_MAX`).

## Modes

| KEY | Mode | UART |
|---|---|---|
| high at the start (`hc05emu_init()`, `AT+RESET`) | full AT mode | 38400 baud 8N1 (8 data bits, no parity, 1 stop bit) |
| raised after the start | AT mode | the data baud, as stored |
| low | data mode | the data baud; bytes go over the link while `hc05emu_forwarding()` holds |

- A rise from data mode keeps an open link and holds its bytes back. A
  connect or search under way stops: AT mode runs none.
- A fall to data mode ends a command that waits for the radio, without an
  answer, and asks for the link. Leaving full AT mode sets the UART to the
  data baud.
- `AT+RESET` answers OK, closes the link and restarts 300ms later
  (`HC05EMU_RESTART_MS`) in the mode KEY selects then. Nothing is read in
  those 300ms.

## Commands

A line ends at LF; a CR is dropped wherever it stands. `AT` and the name
after `AT+` are read in either letter case, the arguments as given. Every
set that changes a stored value queues `HC05EMU_ACT_SAVE` before its OK.

| Command | Answer |
|---|---|
| `AT` | `OK` |
| `AT+VERSION?` | `+VERSION:<text>`, `OK`; the text is the caller's, at most 23 bytes |
| `AT+ROLE?`, `AT+ROLE=<0..2>` | `+ROLE:<n>`, `OK`; `OK` |
| `AT+CMODE?`, `AT+CMODE=<0..2>` | `+CMODE:<n>`, `OK`; `OK` |
| `AT+PSWD?`, `AT+PSWD=<pin>`, `AT+PSWD="<pin>"` | `+PSWD:<pin>`, `OK`; `OK`. A PIN is 1 to 16 printable bytes without `"` and `,` |
| `AT+UART?`, `AT+UART=<baud>,<stop>,<parity>` | `+UART:<baud>,<stop>,<parity>`, `OK`; `OK`. Taken at the next start. Bauds 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1382400 |
| `AT+INQM?`, `AT+INQM=<mode>,<max>,<units>` | `+INQM:<mode>,<max>,<units>`, `OK`; `OK`. Mode 0 to 2, max 1 to 255, units of 1.28s 1 to 48 |
| `AT+BIND?`, `AT+BIND=<addr>` | `+BIND:<addr>` (`0:0:0` with none), `OK`; `OK` |
| `AT+INIT` | `OK` once; `ERROR:(17)` after it, and after data mode ran |
| `AT+INQ` | one `+INQ:<addr>,<class>,<rssi>` line per new address, then `OK` when the radio ends the inquiry, at the end of the inquiry time (`units` x 1280ms) or at the `max`-th device, whichever comes first; mode 0 leaves out the RSSI |
| `AT+RNAME?<addr>` | `+RNAME:<name>`, `OK`; `FAIL` for a failed read and after 2500ms (`HC05EMU_NAME_MS`) without one. A name the inquiry gave answers at once |
| `AT+STATE?` | `+STATE:CONNECTED` with the link open, `+STATE:PAIRABLE` after `AT+INIT` or data mode, `+STATE:INITIALIZED` before |
| `AT+PAIR=<addr>,<s>` | the link closed, then `OK` or `FAIL` from the radio; `FAIL` after `<s>` seconds, 1 to 255 |
| `AT+RESET` | `OK`, then the restart |
| `AT+ORGL` | `OK`; the settings of a fresh bridge, the bound address forgotten |

A fresh bridge is a master (`ROLE 1`), `CMODE 0` without an address, PIN
1234, 115200 baud 8N1 and `INQM 1,1,48`. A real HC-05 leaves the factory a
slave at 9600 or 38400 baud.

An address is NAP:UAP:LAP (the non-significant, upper and lower address
parts) in hex, 1 to 4, 1 to 2 and 1 to 6 digits. A command takes it with
commas or with colons, one kind in one address. `+INQ:` and `+BIND:` print
it in upper case with colons and without leading zeros,
`98D3:31:FD5C81`. A `+INQ:` line prints the class of device in hex and
the RSSI (received signal strength indication) as a 16-bit two's
complement word, `FFC0` for -64dBm, `7FFF` when the radio gave none. A
name is cut to 31 bytes, and each byte outside printable ASCII (0x20 to
0x7E) is `?`.

| Error | Condition |
|---|---|
| `ERROR:(0)` | a command or a form not in the table, a line over 79 bytes |
| `ERROR:(5)`, `(6)`, `(7)` | the address's NAP, UAP or LAP has too many digits |
| `ERROR:(F)`, `(10)` | the PIN is empty, or longer than 16 bytes |
| `ERROR:(11)` | a role above 2; `AT+INQ` in a role other than 1 |
| `ERROR:(12)`, `(13)`, `(14)` | a baud not in the list; a stop setting above 1; a parity above 2 |
| `ERROR:(16)` | `AT+INQ` before `AT+INIT` |
| `ERROR:(17)` | `AT+INIT` again |
| `ERROR:(18)`, `(19)` | an inquiry mode above 2; an inquiry time of 0 or above 48 units |
| `ERROR:(1A)` | no address |

The codes are the HC-05 datasheet's. `ERROR:(11)` for `AT+INQ` as a slave
is this module's choice; what a real module answers there is not measured.

A command that comes while `AT+INQ`, `AT+RNAME?` or `AT+PAIR` waits for
the radio is held and answered after that command's end. A second one in
that time is dropped. `protocols/hc05`'s fence (`AT+STATE?` after an abort)
is answered so.

## Data mode

| Setting | The link asked for |
|---|---|
| `ROLE 1`, `CMODE 0`, an address bound | `HC05EMU_ACT_CONNECT` to that address with the PIN |
| `ROLE 1`, `CMODE 1` | an inquiry of 8 units of 1.28s (10.24s) for 9 devices; then a connect to each device found, strongest RSSI first, an unknown RSSI last, until one opens |
| any other | none: this module takes no connection as a slave |

A connect that does not end within 15000ms (`HC05EMU_CONNECT_MS`) is
stopped. A failed connect, a lost link and the end of `CMODE 1`'s list
start the next try 1000ms later (`HC05EMU_RETRY_MS`). A real HC-05 is
reported to give up after 30min; this module tries without end. A link
lost in AT mode is asked for again at KEY's fall. A link that opens when
none is asked for, or to another address, is stopped.

The status LED (`hc05emu_led()`): off in the restart; 1000ms on and
1000ms off in AT mode; on with the link open in data mode; 100ms on and
100ms off in data mode without it. `hc05emu_connected()` is the HC-05's
STATE pin: high with the link open, in either mode.

## Interface

`hc05emu.h`:

```c
void hc05emu_cfg_default(hc05emu_cfg_t *c);
bool hc05emu_cfg_valid(const hc05emu_cfg_t *c);
void hc05emu_init(hc05emu_t *e, const hc05emu_cfg_t *stored,
                  const char *version, bool key, uint32_t now_ms);
void hc05emu_key(hc05emu_t *e, bool high, uint32_t now_ms);
void hc05emu_feed(hc05emu_t *e, const uint8_t *b, size_t n, uint32_t now_ms);
void hc05emu_found(hc05emu_t *e, const uint8_t *addr, uint32_t cod,
                   int16_t rssi, const char *name, uint32_t now_ms);
void hc05emu_inq_done(hc05emu_t *e, uint32_t now_ms);
void hc05emu_name_done(hc05emu_t *e, const uint8_t *addr, const char *name,
                       uint32_t now_ms);
void hc05emu_pair_done(hc05emu_t *e, const uint8_t *addr, bool ok,
                       uint32_t now_ms);
void hc05emu_link_event(hc05emu_t *e, const uint8_t *addr, bool up,
                        uint32_t now_ms);
bool hc05emu_step(hc05emu_t *e, uint32_t now_ms, hc05emu_act_t *out);
bool hc05emu_at_mode(const hc05emu_t *e);
bool hc05emu_forwarding(const hc05emu_t *e);
bool hc05emu_connected(const hc05emu_t *e);
bool hc05emu_led(const hc05emu_t *e, uint32_t now_ms);
```

`hc05emu_step()` returns one action per call:

| Action | The caller |
|---|---|
| `HC05EMU_ACT_SEND` | writes `data[0..len-1]`, one answer line with CR LF |
| `HC05EMU_ACT_BAUD` | sets the UART to `baud`, `stop`, `parity` |
| `HC05EMU_ACT_INQ` | starts an inquiry of `units` x 1.28s for `max` devices; reports each with `hc05emu_found()` and its end with `hc05emu_inq_done()` |
| `HC05EMU_ACT_NAME` | reads the remote name of `addr`; `hc05emu_name_done()` |
| `HC05EMU_ACT_PAIR` | pairs with `addr` using `pin`; `hc05emu_pair_done()` |
| `HC05EMU_ACT_CONNECT` | opens an SPP link to `addr` using `pin`; `hc05emu_link_event()` when it opens, fails or closes |
| `HC05EMU_ACT_STOP` | ends the inquiry, the connect or the pairing under way and closes the link, without reporting any of them |
| `HC05EMU_ACT_SAVE` | stores `cfg` |

Call `hc05emu_step()` until it returns false, at least every 10ms, and
after every result handed in. A wait is counted from the call that began
it. A result for another address than the one asked for, or one that
comes after its command ended, changes nothing.

`hc05emu_cfg_t` holds its flags as bytes. `hc05emu_init()` takes a stored
record only when `hc05emu_cfg_valid()` holds for it, and the defaults
otherwise.

A bridge loop with a driver that has `uart_read()`, `uart_write()`,
`uart_set()`, `key_read()`, `led_set()` and `ms()`, and a radio that calls
the result functions:

```c
static hc05emu_t emu;
hc05emu_act_t act;
uint8_t rx[64];

hc05emu_init(&emu, stored_or_null, "btbridge", key_read(), ms());
for (;;) {
    hc05emu_key(&emu, key_read(), ms());
    size_t n = uart_read(rx, sizeof rx);
    if (hc05emu_at_mode(&emu)) {
        hc05emu_feed(&emu, rx, n, ms());
    } else if (hc05emu_forwarding(&emu)) {
        radio_write(rx, n);
    }
    while (hc05emu_step(&emu, ms(), &act)) {
        perform(&act);   /* SEND, BAUD, INQ, NAME, PAIR, CONNECT, STOP, SAVE */
    }
    led_set(hc05emu_led(&emu, ms()));
}
```

## Use in another project

```cmake
add_subdirectory(protocols/hc05emu)
target_link_libraries(my_target PRIVATE rcbench_hc05emu)
```

Under ESP-IDF the folder is a component: add it to
`EXTRA_COMPONENT_DIRS` and `REQUIRES hc05emu`.

## Not covered

- The slave role: `ROLE 0` and `ROLE 2` are stored and answered, and data
  mode then asks for no link.
- `CMODE 2` (slave-loop): stored, no link.
- Commands outside the table, among them `AT+NAME`, `AT+ADDR?`,
  `AT+CLASS`, `AT+IAC`, `AT+SNIFF`, `AT+SENM`, `AT+PMSAD`, `AT+RMAAD`,
  `AT+FSAD`, `AT+ADCN?`, `AT+MRAD?`, `AT+INQC`, `AT+LINK`, `AT+DISC`,
  `AT+IPSCAN`, `AT+MPIO` and `AT+POLAR`: `ERROR:(0)`.
- Firmware 3.0's forms: `+PIN:` for `AT+PSWD?` and `ERROR:(1F)` for
  `AT+INQ`. The quoted PIN is taken.
- A UART break, a parity error and a byte at a wrong baud are bytes like
  any other: junk before a command makes it `ERROR:(0)`.

## Trademarks

Bluetooth is a trademark of its owner, and HC-05, WitMotion and BWT901CL
are names of their makers' products. They are used here only to say which
module this code answers like and which sensor it connects to. The project
is not affiliated with the owners. The full notice is in the repository's
[README](../../README.md#legal-notice-and-disclaimer).

## Tests

`test/host/test_hc05emu.c` runs the commands and lines,
`test_hc05emu_radio.c` the radio side, the timeouts at their limits and
across the clock's wrap, and the data-mode link, against the scripted radio
in `test/host/hc05emu_rig.h`. `test_hc05emu_panel.c` runs
`protocols/hc05`'s PROBE, SCAN, BIND and ANY against this module. The table
in `STATUS.md` has the line coverage per file.

```sh
cmake -S test/host -B test/host/build
cmake --build test/host/build
ctest --test-dir test/host/build -R hc05emu
```
