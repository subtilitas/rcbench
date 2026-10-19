# Hitec harness

The Hitec harness runs `protocols/hitec` on one pin of an RP2350 board and
takes its commands as text lines over USB (Universal Serial Bus) serial. It
puts the module's transactions on a servo's signal wire without the panel
or the IOmcu firmware: a classic command, a D series read or write, raw
bytes, a listen with an optional answer, a held level. Every transaction
reports the raw capture (bytes, start-bit times, flags) and the decoded
result, so a script on the bench Raspberry Pi can run it beside the logic
analyser.

It is a test-bench tool. No release carries its UF2 (USB Flashing Format)
image, and it is not part of the IOmcu image.

Hitec is a name or trademark of its owner. It is used here only to say
which servos the harness works with.

| Part | Files |
| --- | --- |
| Command lines and reports, pure C | `core/hitec_harness.c`, `core/include/hitec_harness.h` |
| The RP2350 glue: USB serial, the line, the marker pin | `rp2350/main.c` |
| The module | `protocols/hitec` (core and `rp2350/` pin driver) |
| Host tests of the core | `test/host/test_hitec_harness.c` |
| What to measure with it | [TESTPLAN.md](TESTPLAN.md) |

## Build

Needs pico-sdk 2.3.0, an `arm-none-eabi` GCC (tested with Arm GNU Toolchain
14.2.rel1), CMake 3.13 or later and Ninja. It builds from this directory and
uses `protocols/hitec` from the same checkout.

```bash
cd testbench/hitec-harness
export PICO_SDK_PATH=~/pico-sdk-2.3.0
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2
cmake --build build
```

Output: `build/rcbench-hitec-harness.uf2`, 91 136 bytes (45 236 bytes of
image) with pico-sdk 2.3.0 and Arm GNU Toolchain 14.2.rel1.

`PICO_BOARD` only sets the flash size and the boot stage. The line pin is
a run-time argument, so `pico2` runs on any RP2350A board with 4 MB of flash
or more. Give the board's own name (`waveshare_rp2350_zero`,
`pimoroni_pico_plus2_rp2350`, ...) when the SDK has one. On the Pi, the
`PICOTOOL_DIR` line of `testbench/touch-injector/README.md` applies here
too.

## Flash

Hold BOOT, plug the board in, release BOOT, then copy the UF2 to the
`RP2350` drive, or:

```bash
picotool load -f -x build/rcbench-hitec-harness.uf2
```

`-f` reboots a board that runs a pico-sdk image with USB serial into
BOOTSEL first; `-x` starts the image after loading. The port appears as:

```text
/dev/serial/by-id/usb-Raspberry_Pi_rcbench_hitec_<serial>-if00
```

**Never let a board boot an image that drives pins while it is wired to a
servo or a bus** (`testbench/WIRING.md`). Flash the harness with the servo
lead off. The harness itself touches no pin until `open`.

## Pin states

| When | Line pin | Marker pin |
| --- | --- | --- |
| After a reset, before `open` | as the boot ROM leaves it: an input | not touched |
| Open, no transaction | classic: an input, pull-down on; D series: an input, pull-up on, input inverted | output, low |
| In a transaction | as `protocols/hitec/README.md`, "Line levels", says | high from `start()` to the poll that returns the capture |
| After `close`, a closed port, a watchdog reset | an input with the family's pull, no override | an input |

The internal pulls are 50 to 80 kOhm. The watchdog resets the chip when the
main loop stalls for 1 s. Closing the serial port (DTR, data terminal
ready, dropped) aborts a running transaction and closes the line.

## Serial protocol

One command a line, at most 127 characters, LF or CRLF. Words are separated
by spaces. A number is decimal or `0x` hex. Options are `key=value` words
after the positional arguments, in any order. Protocol version 1.

| Command | Does | Reply |
| --- | --- | --- |
| `hello` | | `ok hello rcbench-hitec-harness proto=1 sys_hz=150000000` |
| `help` | | the command list as `#` lines, then `ok help` |
| `status` | | `ok status open= busy= family= pin= drive= mark=` |
| `open <classic\|d> <pin> [od\|pp] [mark=<pin>]` | `hitec_line_open()`; closes an open line first | `ok open family= pin= drive= bit_ns= div256= err_ppm= sys_hz= mark=` or `err open code=ARG\|CLOCK\|PIO\|DMA` |
| `close` | aborts, closes, releases the marker | `ok close` |
| `c <cmd> <p1> <p2>` | classic: `hitec_c_xfer()`, decoded with the command's reply shape | a transaction report |
| `dread <addr> [id=<id>]` | D series: `hitec_d_read_xfer()`, id 0 by default, decoded | a transaction report |
| `dwrite <addr> <value> [id=<id>]` | D series: `hitec_d_write_xfer()`, no receive by default | a transaction report |
| `send <b0> [b1 .. b7]` | a raw SEND of 1 to 8 bytes on the line's family | a transaction report |
| `listen <window_us> <rx> [answer=<b>] [want=<b>]` | a LISTEN; `answer=` answers the first byte; `want=` decodes with `hitec_listen_decode()` | a transaction report |
| `level <0\|1> <us>` | holds the line low, or high on a push-pull line, then releases it | a transaction report |
| `abort` | `hitec_line_abort()` on the running transaction | `ok abort`, then its report with `aborted=1` |

`c`, `dread`, `dwrite` and `send` take `lead=<us>`, `gap=<bits>`,
`window=<us>` and `rx=<n>`, which replace the builder's values:

| Builder | lead | gap | window | rx |
| --- | --- | --- | --- | --- |
| `c` | 0 | 1 bit time | 1500 us | 2 |
| `dread` | 88 us, driven low | 0 | 25 000 us | 8 |
| `dwrite` | 88 us, driven low | 0 | 0 | 0 |
| `send` | 0 | 0 | 0 | 0 |

Limits: lead, window and level times 0 to 4 000 000 us (a level 1 or more),
gap 0 to 6, rx 0 to 16, a window and rx both 0 or both not, pins 0 to 47.
The PIO program rounds a lead up and pads the front of the stream to whole
words: a D read's 88 us lead runs 117.2 us (`lead_ns` in the report).

### A transaction report

```text
ok start kind=send family=classic tx=61,05,00,9a lead_us=0 gap=1 window_us=1500 rx=2 answer=- level=0 level_us=0 lead_ns=2034 span_us=4410
cap n=2 full=1 answered=0 overflow=0 aborted=0 window_ns=1500000 t_us=3212
byte 0 at_ns=40350 value=0xc2 framing=0
byte 1 at_ns=561990 value=0x2b framing=0
dec kind=c rx=OK value=0x00c2 id=0x00 n=2 delay_ns=509100
done
```

The numbers above show the layout; they are not a measurement.

| Line | Field | Meaning |
| --- | --- | --- |
| `ok start` | `tx` | the bytes sent, hex, `-` for none |
| | `lead_ns` | from the lead's first change to the release, as the program runs it |
| | `span_us` | the longest the transaction can take; the driver ends a run 1 ms past it and marks `overflow=1` |
| `cap` | `n`, `full` | bytes held; 1 when `rx` bytes ended the receive |
| | `answered` | 1 when the answer went out whole |
| | `overflow`, `aborted` | the capture is not whole; the transaction was cut |
| | `t_us` | from `start()` to the poll that returned the capture, at 1 us resolution; includes up to one pass of the main loop |
| `byte k` | `at_ns` | the start bit's leading edge from time 0 of the capture: classic SEND the start of the last byte's stop bit, D series SEND one bit time after the end of the last stop bit, LISTEN its start; within 1/16 bit and the input synchroniser's 2 system clock cycles |
| | `framing` | 1: the stop bit was sampled at the start level |
| `dec` | `kind` | `c`, `d`, `listen` or `none` (`send`, `dwrite`, `level`, a `listen` without `want=`, a classic command with no reply shape) |
| | `rx` | the decoder's result: `OK`, `ARG`, `ABORTED`, `OVERFLOW`, `SILENT`, `EARLY`, `LATE`, `NAK`, `LENGTH`, `FRAMING`, `GAP`, `ACK`, `HEADER`, `ADDRESS`, `SUM` (`protocols/hitec/README.md`, "Decoders") |
| | `delay_ns` | the first byte from the master's last byte (classic), the end of the request (D series) or the start of the listen |

While a transaction runs, only `abort` and `status` are taken; every other
command gets `err busy`. Errors are one line and nothing runs:

| Reply | Cause |
| --- | --- |
| `err usage <hint>` | missing positional argument, or a verb that takes none given one |
| `err range <field>` | a number out of its limit, or not a number |
| `err option <word>` | an option the verb does not take |
| `err family <hint>` | `c` on a D line, `dread`/`dwrite` on a classic line, an unknown family |
| `err drive <hint>` | an unknown drive, or `pp` on a classic line |
| `err closed open a line first` | a transaction with no line open |
| `err unknown <verb>` | no such command |
| `err line ...` / `err line too long` | over 127 characters or 16 words |
| `err busy` | a transaction is running |
| `err idle` | `abort` with nothing running |
| `err start ...` | the driver refused the stream: `level 1` on an open-drain line |
| `err open code=...` | `hitec_line_open()` failed (`protocols/hitec/rp2350/hitec_line.h`) |

### Scripting it

```python
import serial  # pyserial

port = serial.Serial(
    "/dev/serial/by-id/usb-Raspberry_Pi_rcbench_hitec_<serial>-if00",
    timeout=10)

def run(cmd):
    """Send one command; return its reply lines up to done, ok or err."""
    port.write((cmd + "\n").encode())
    lines = []
    while True:
        line = port.readline().decode().strip()
        if not line:
            raise TimeoutError(cmd)
        lines.append(line)
        if line == "done" or line.startswith("err"):
            return lines
        if line.startswith("ok") and not line.startswith("ok start"):
            return lines

run("hello")
run("open classic 2 mark=3")
for addr in range(0, 8):
    print(run(f"c 0x61 {addr} 0"))
run("close")
```

## Wiring

The board for the harness is not chosen yet (see "Open questions" in
TESTPLAN.md). The pins below are examples for a board where GP2 and GP3
are free; any GPIO works, given to `open`.

| Harness pin | Connects to | Notes |
| --- | --- | --- |
| line pin, e.g. GP2 | the servo's signal wire, through the series resistor below | the only pin that meets the servo |
| marker pin, e.g. GP3 | one analyser channel | high for each transaction: the capture trigger; drives 3.3 V push-pull, connect nothing else to it |
| GND | the bench's star point, short lead | `testbench/WIRING.md`, section 1 |

The servo has its own supply, returned to the star point
(`testbench/WIRING.md`, section 1), and never runs from the board's rail.

### Classic (HS-5645MG, HS-7955TG)

```text
 harness GPx ──[ R_s 1 kOhm ]──┬── servo signal
                               │
                       analyser channel (threshold 1.4 V)
 harness GND ─── star point ─── servo supply return ─── analyser GND
```

- The servo makes the line's high level itself. The harness only pulls low
  (open drain) and releases. No pull-up on the board.
- `R_s` sits between the pin and the servo. It limits the current into the
  pin while the servo drives the line above the pin's rating, and while
  both drive at once in a fault. The 1 kOhm start value is not derived from
  a measurement: with the servo's pull-up not known, how low the line goes
  through 1 kOhm is not known either. Read the low level on the analyser
  (or a scope) before trusting a capture; lower `R_s` only when the low
  level is too high.
- The analyser probe goes on the servo side of `R_s`: that is the level the
  servo sees.

### D series (D485HW)

```text
              3.3 V (the harness board's 3V3 pin)
                │
              [ R_p ]
                │
 harness GPx ──[ R_s ]──┬── servo signal
                        │
                analyser channel
 harness GND ─── star point ─── servo supply return ─── analyser GND
```

- The line needs a pull-up on the board, `R_p`, to 3.3 V. The internal
  50 to 80 kOhm pull-up does not lift the line against the servo's 3 kOhm
  pull-down (reported with another programmer, not measured here).
- `R_p` against the servo's 3 kOhm gives 3.3 V x 3 / (3 + R_p) at the line.
  Hitec's reference circuit has 3.3 kOhm: 1.57 V, below the 2.0 V an RP2350
  input needs for a high at 3.3 V. 1.95 kOhm or less gives 2.0 V or more;
  1.8 kOhm gives 2.06 V. Whether the servo still pulls a 1.8 kOhm line low
  enough is not known.
- `R_s`: 100 Ohm to start, or none. In open drain the harness only pulls
  low against `R_p`; in push-pull (`open d <pin> pp`) it also drives the
  sent bits high, 1.1 mA into 3 kOhm.
- Which side drives each level of a D series answer is not measured.

## Electrical risks

Nothing here is measured. The station asks Julian before connecting a
servo to the harness, before changing a resistor, and before `pp`.

| Risk | Condition | What limits it |
| --- | --- | --- |
| The servo's high level is above the pin's rating | the classic servo makes the high level itself; its voltage is not measured. An RP2350 GPIO is rated 5.5 V with IOVDD at 3.3 V, 3.63 V with IOVDD at 0 V (board unpowered) | measure the servo's idle signal voltage first, servo powered and the harness not connected; `R_s`; power the harness board before the servo and take the servo supply off before the harness |
| Two drivers on the line | the servo replies inside the master's stop bit (classic); a D line in push-pull meets a servo that drives | open drain for classic (the only drive the harness takes for it); `R_s`; `pp` only after the open-drain captures |
| The D line never reads high | `R_p` above 1.95 kOhm | `R_p` 1.8 kOhm, with the open question above |
| A second ground path | the harness on a USB data cable to the Pi while the analyser captures: the USB shell grounds the board a second time (`testbench/WIRING.md`, section 1 forbids readings in that state) | an isolated USB adapter on the harness cable, or Julian's decision for this tool |
| The servo moves | `c 0x66` (move), `dwrite` to a position register, a power-up | the servo horn free, nothing in reach; the test plan reads before it writes |
| A setting written into the servo | `c 0x62`, `c 0x64`, `dwrite` | the test plan writes only the value just read back, and only after the reads pass |

## Tests

`test/host/test_hitec_harness.c` (11 cases) parses every verb, its options
and their limits, every refusal, and checks the lines a transaction
reports. The RP2350 glue is not in the host build; the CI workflow builds
it for `pico2`.

```sh
cmake -S test/host -B test/host/build
cmake --build test/host/build
ctest --test-dir test/host/build -R hitec_harness
```
