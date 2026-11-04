# hc05

The AT command set-up of an HC-05 Bluetooth module as an SPP (serial port
profile) master. The core is a state machine fed the module's bytes and a
millisecond clock. It tells its caller what to do next: drive the KEY pin,
set the UART (universal asynchronous receiver-transmitter) to a baud, or
send a command. It finds the module's baud, reads its firmware version,
lists the devices in range with their names, and binds the module to one
of them or sets it to connect to any slave. The module has no pin driver:
the caller owns the UART and the KEY pin.

In this repository it sets up the HC-05 that connects the panel to a
WitMotion BWT901CL angle sensor, whose radio is an HC-06 slave.

| Part | Files | Depends on |
|---|---|---|
| Core | `hc05.c`, `hc05_line.c`, `hc05_int.h`, `include/hc05.h` | C11 standard headers |

## Constraints

- Plain C11. The header has `extern "C"` guards and compiles by itself as
  C11 and as C++17 (`tools/check_protocols.py`).
- The core includes `<stdbool.h>`, `<stddef.h>`, `<stdint.h>` and
  `<string.h>` and no file outside this folder.
- No heap, no floating point, no global or static mutable state, no clock
  read and no I/O (input/output). A machine (`hc05_t`) is 696 bytes on
  x86-64; `hc05.c` and `hc05_line.c` are 3622 and 1709 bytes of code with
  gcc `-Os` there.
- Times are unsigned differences of a free-running 32-bit millisecond
  clock the caller passes in, so a wait holds across the clock's wrap at
  2^32ms.
- Every function of one machine is called from one context at a time.
- One operation runs at a time. A start while one runs returns false and
  changes nothing.
- A command is at most 64 bytes (`HC05_OUT_MAX`); an answer line is held
  to 95 bytes (`HC05_LINE_MAX` - 1), and the rest of a longer line is
  dropped. A scan keeps 8 devices (`HC05_FOUND_MAX`).

## Operations

Every operation starts with KEY high. The first byte goes out 100ms
later (`HC05_KEY_SETTLE_MS`). KEY stays high until the operation ends:
the HC-05 answers AT+INQ and AT+RNAME? only while it is high. After the
end the next `hc05_step()` asks KEY low.

The handshake sends `AT` and waits 1000ms (`HC05_AT_MS`) for OK at each
baud: 115200, 38400, then 9600. PROBE starts at 115200. SCAN, BIND and
ANY start at the baud the module last answered at, then try the other two
in that order. No OK at any baud ends the operation `HC05_ERR_NO_AT`. An
ERROR to `AT` sends `AT` once more at the same baud: bytes sent at a wrong
baud can stay in the module's line buffer and spoil the first command.

| Operation | Commands after the handshake | Ends |
|---|---|---|
| PROBE | `AT+VERSION?` | DONE, `version` holds the text after `+VERSION:` |
| SCAN | `AT+INIT`, `AT+INQM=1,9,9`, `AT+INQ`, `AT+RNAME?<addr>` per device | DONE, `found[0..found_n-1]` |
| BIND | `AT+ROLE=1`, `AT+PSWD=<pin>`, `AT+PAIR=<addr>,20`, `AT+BIND=<addr>`, `AT+CMODE=0`, `AT+UART=115200,0,0`, `AT+RESET` | DONE, the module set to connect to `<addr>` at 115200 baud |
| ANY | `AT+ROLE=1`, `AT+PSWD=<pin>`, `AT+CMODE=1`, `AT+UART=115200,0,0`, `AT+RESET` | DONE, the module set to connect to any slave that takes the PIN |

| Command | Waits for | Timeout |
|---|---|---|
| each command not named below | OK | 1000ms (`HC05_AT_MS`) |
| `AT+INQ` | `+INQ:` lines, then OK | 13520ms (`HC05_INQ_MS(9)`: 9 units of 1.28s plus 2000ms) |
| `AT+RNAME?<addr>` | `+RNAME:<name>`, then OK | 3000ms (`HC05_RNAME_MS`) |
| `AT+STATE?`, after a name that did not come | `+STATE:<state>`, then OK | 3000ms (`HC05_RNAME_MS`) |
| `AT+PAIR=<addr>,20` | OK | 22000ms (`HC05_PAIR_MS`: the 20s pairing time plus 2000ms) |
| `AT+RESET` | OK | 1500ms (`HC05_RESET_MS`) |
| the restart after `AT+RESET`'s OK | nothing: KEY low | 1500ms (`HC05_RESET_MS`) |

- SCAN: an ERROR:(17) to `AT+INIT` (the SPP library is up already) is no
  error. `AT+INQM=1,9,9` asks for RSSI (received signal strength
  indication) mode, at most 9 devices and 9 units of 1.28s: 11.52s. The
  first report of each address is kept, up to 8. An inquiry that reports
  no device ends `HC05_ERR_NONE_FOUND`. A FAIL or ERROR to `AT+RNAME?`
  leaves that device's name "" and the scan goes on with the next one. A
  name that does not come within 3000ms leaves it "" too, and `AT+STATE?`
  goes out before the next device's `AT+RNAME?`: every line up to its
  `+STATE:` line and OK is dropped, so a late `+RNAME:` is never taken for
  the next device. No answer to `AT+STATE?` within 3000ms ends the scan
  DONE with the names left "".
- BIND and ANY: an ERROR to `AT+PSWD=<pin>` sends `AT+PSWD="<pin>"` once
  (`pswd_quoted`), the form firmware 3.0 takes. A FAIL, an ERROR or no
  OK to `AT+PAIR` ends the bind: `HC05_ERR_PAIR` for FAIL and ERROR,
  `HC05_ERR_TIMEOUT` for no answer. After the OK of `AT+RESET` the machine
  asks KEY low, so the module restarts in data mode, holds it low and
  stays busy for 1500ms, then asks the UART to 115200 baud and ends DONE.
  An operation started at once after DONE finds the module restarted.
- A PIN (personal identification number) is 1 to 16 decimal digits. The
  BWT901CL's is 1234.

| Error | Condition | `err_code` |
|---|---|---|
| `HC05_ERR_NO_AT` | no OK to `AT` at any of the 3 bauds | 0 |
| `HC05_ERR_TIMEOUT` | a command's answer did not come within its timeout | 0 |
| `HC05_ERR_REFUSED` | ERROR:(hh) or FAIL to a command | hh; 0 for FAIL |
| `HC05_ERR_INQ_REFUSED` | ERROR:(1F) to `AT+INQ`: offer ANY instead | 0x1F |
| `HC05_ERR_PAIR` | FAIL or ERROR:(hh) to `AT+PAIR` | hh; 0 for FAIL |
| `HC05_ERR_NONE_FOUND` | the inquiry ended with no device | 0 |
| `HC05_ERR_ARG` | `hc05_bind()` or `hc05_any()` with a bad address or PIN; nothing is sent | 0 |

`hc05_abort()` ends a running operation FAILED with `HC05_ERR_NONE`. The
next `hc05_step()` asks KEY low when KEY was high. Lines that come after
it change nothing. After `AT+RESET` has gone out the module restarts
whatever comes: an abort then asks KEY low, keeps the machine busy for
the 1500ms hold, and ends FAILED with `HC05_ERR_NONE` at its end, without
the data baud action.

## Answers and addresses

Lines end in CR LF (carriage return, line feed). A CR is dropped, an LF
ends the line, a NUL byte inside a line makes it match no answer.
`hc05_parse_line()` knows:

| Line | Kind | Value or code |
|---|---|---|
| `OK` | `HC05_LINE_OK` | |
| `FAIL` | `HC05_LINE_FAIL` | |
| `ERROR:(hh)`, `ERROR(hh)`, `ERROR` | `HC05_LINE_ERROR` | hh, 1 or 2 hex digits; 0 for a bare `ERROR` |
| `+INQ:<addr>,<class>,<rssi>` | `HC05_LINE_INQ` | the text after the colon |
| `+RNAME:<name>` | `HC05_LINE_RNAME` | the name |
| `+VERSION:<text>` | `HC05_LINE_VERSION` | the version, `2.0-20100601` or `3.0-20170601` |
| `+STATE:<text>` | `HC05_LINE_STATE` | the state word |
| `+BIND:<addr>` | `HC05_LINE_BIND` | the address |

Any other line, `ERROR:(100)` and `ERROR:(1F) ` among them, is
`HC05_LINE_OTHER`. Prefixes are case-sensitive.

An address is NAP:UAP:LAP (the non-significant, upper and lower address
parts) in hex: 1 to 4, 1 to 2 and 1 to 6 digits. `+INQ:` and `+BIND?`
print it with colons and leading zeros dropped, `3014:10:171179`; a set
command takes it with commas, `3014,10,171179`. `hc05_addr_cmd()` writes
the comma form, `hc05_addr_show()` the colon form; each reads either
form, one separator kind in one address, and keeps the digits and their
case as given. The longest address is 14 bytes plus the NUL.

An `+INQ:` line carries the address, the class of device (1 to 6 hex
digits; `dev_class` keeps its low 16 bits, the major and minor device
class, not the service bits 16 to 23) and the RSSI (1 to 4 hex digits, a
16-bit two's complement word: `FFBC` is -68). A line without an RSSI
gives 32767, as the module's `7FFF` for an unknown one. Fields after the
third are not read. A line of another form is dropped. A name is cut to
31 bytes, and each byte outside printable ASCII (0x20 to 0x7E) is shown
as `?`.

## Firmware 2.0 and 3.0

| | 2.0 (`2.0-20100601`) | 3.0 (`3.0-20170601`) |
|---|---|---|
| `AT+PSWD` | `AT+PSWD=1234` | reported to take `AT+PSWD="1234"` only |
| `AT+INQ` | lists devices | reported to answer ERROR:(1F) |
| UART transmit | sends at once | reported to buffer up to 230 bytes |

Both forms of the PIN are tried. ERROR:(1F) ends a scan with
`HC05_ERR_INQ_REFUSED`, and ANY (`AT+CMODE=1`) connects to any slave that
takes the PIN, without a scan: with one sensor in range that is the
sensor. The 3.0 behaviour comes from reports; no 3.0 module has been
measured.

## Interface

`hc05.h`:

```c
void        hc05_init(hc05_t *h);
bool        hc05_probe(hc05_t *h, uint32_t now_ms);
bool        hc05_scan(hc05_t *h, uint32_t now_ms);
bool        hc05_bind(hc05_t *h, const char *addr, const char *pin,
                      uint32_t now_ms);
bool        hc05_any(hc05_t *h, const char *pin, uint32_t now_ms);
void        hc05_abort(hc05_t *h);
void        hc05_feed(hc05_t *h, const uint8_t *b, size_t n);
bool        hc05_step(hc05_t *h, uint32_t now_ms, hc05_act_t *out);
bool        hc05_busy(const hc05_t *h);
bool        hc05_addr_cmd(const char *addr, char *out, size_t cap);
bool        hc05_addr_show(const char *addr, char *out, size_t cap);
hc05_line_t hc05_parse_line(const char *line, char *value, size_t cap,
                            uint8_t *code);
```

`hc05_step()` hands out one action a call:

| Action | The caller |
|---|---|
| `HC05_ACT_KEY` | drives KEY to `key`; the machine itself holds the next byte 100ms after KEY rises |
| `HC05_ACT_BAUD` | sets the UART to `baud` |
| `HC05_ACT_SEND` | sends `data[0..len-1]`, a command with its CR LF |
| `HC05_ACT_NONE` | nothing: the machine waits for bytes or time |

Call `hc05_step()` until it returns false, at least every 10ms, and feed
every byte the UART receives. A wait is counted from the `hc05_step()`
that handed out the command, so a late call lengthens no timeout and a
late feed can make one run out. `out` NULL takes no action.

A scan with a driver that has `uart_set_baud()`, `uart_write()`,
`uart_read()`, `key_set()` and `ms()`:

```c
static hc05_t bt;
hc05_act_t act;
uint8_t rx[64];

hc05_init(&bt);
hc05_scan(&bt, ms());
while (hc05_busy(&bt) || bt.key) {
    size_t n = uart_read(rx, sizeof rx);
    hc05_feed(&bt, rx, n);
    while (hc05_step(&bt, ms(), &act)) {
        if (act.kind == HC05_ACT_KEY)  key_set(act.key);
        if (act.kind == HC05_ACT_BAUD) uart_set_baud(act.baud);
        if (act.kind == HC05_ACT_SEND) uart_write(act.data, act.len);
    }
}
/* bt.state == HC05_ST_DONE: bt.found[0..bt.found_n-1] */
```

## Use in another project

```cmake
add_subdirectory(protocols/hc05)
target_link_libraries(my_target PRIVATE rcbench_hc05)
```

Under ESP-IDF the folder is a component: add it to
`EXTRA_COMPONENT_DIRS` and `REQUIRES hc05`.

## Measured and not measured

The host suite runs the machine against the scripted module of
`test/host/hc05_sim.h`: answer tables of firmware 2.0 and 3.0, the
inquiry's timing, the quoted PIN, a restart in data mode or in AT mode by
the level of KEY. No HC-05 has been run with this code.

Not measured: whether every module answers `AT+INQ` with KEY raised after
power-up rather than held high at power-up; whether a module in the slave
role (the factory setting, `AT+ROLE=0`) runs `AT+INQ`, since SCAN sends no
`AT+ROLE=1`; the time a module takes to restart after `AT+RESET`; firmware
3.0's error codes and the form of its `+INQ:` lines.

## Trademarks

Bluetooth is a trademark of its owner, and HC-05, HC-06, WitMotion and
BWT901CL are names of their makers' products. They are used here only to
say which modules this code talks to. The project is not affiliated with
the owners. The full notice is in the repository's
[README](../../README.md#legal-notice-and-disclaimer).

## Tests

`test/host/test_hc05.c` runs the operations against the scripted module,
`test/host/test_hc05_lines.c` the lines, the addresses and the `+INQ:`
fields. The table in `STATUS.md` has the line coverage per file.

```sh
cmake -S test/host -B test/host/build
cmake --build test/host/build
ctest --test-dir test/host/build -R hc05
```
