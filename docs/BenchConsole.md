# The bench console

<sub>**English** · [Deutsch](BenchConsole-de.md)</sub>

The bench console reads and writes the IOmcu's link pages from command
lines. It is available only in debug builds, not in released images. Build
the IOmcu with `IOMCU_CONSOLE` to use its USB (Universal Serial Bus) serial
port. A station connected to that port can access the KST, SUPPLY, PROG,
HITEC and other pages described in [The link](Link.md), including pages
without a panel screen.

Every request goes through the same page handlers as a link request. The
same refusals apply: an ARM write while the arm latch is set, a KST or
HITEC command without its write key or with an incorrect check, a SUPPLY
ON without a heartbeat, and a transaction after a stop. The console has
the same permissions as the panel.

A console request does not count as link traffic or as a heartbeat, so it
does not prevent the link's silence failsafe from triggering after 200ms.
Operations that require a trusted heartbeat and an active link also
require a running panel: SUPPLY's ON, a KST session, every HITEC command
except ABORT and CLOSE, and ARM. Without a panel, these operations are
refused or stopped, as they are when a bench's panel stops.

The console can write what the panel can write, CONTROL included. A write
of ARM from the console arms the bank when every rule of the safety gate
lets it ([Safety](Safety.md)). A CLEAR written from the console releases
the arm latch and the failsafe as the panel's does; the link goes silent
again 200ms after the panel's last request. Use the console on a bench
with no propeller and the outputs secured, as for any bench work.

## Building it

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S firmware/iomcu -B firmware/iomcu/build-console -DIOMCU_CONSOLE=ON
cmake --build firmware/iomcu/build-console
```

The option is off by default. CI (continuous integration) builds the image
with it, checks the stacks of both cores, and fails when the default image
holds a console symbol. `IOMCU_CONSOLE` and `SENSE_TRACE` both read the USB
console; CMake refuses a build with both.

Built with ARM GNU 14.2 and pico-sdk 2.3.0, the console image is 17,444
bytes larger than the default image and holds 9,664 bytes more RAM
(random-access memory): 2,048 of them the output buffer, 960 the USB
transmit buffer of 1,024 bytes in place of the SDK's 64, 3,488 the signal
wire's pin driver and 1,536 its test's reply. Core 0's deepest
call chain is the default image's: the console keeps its work space in its
own state, not on the stack.

## Lines

A line ends with CR or LF and holds at most 160 characters; a longer line is
refused whole. Tokens are separated by spaces. Numbers are decimal or `0x`
hexadecimal. The console takes up to 64 characters a pass of the IOmcu's
main loop and prints no prompt and no echo.

Every reply is one line or more, each starting with one word:

| Word | Means |
| --- | --- |
| `data` | a read's registers, in hexadecimal |
| `ack` | a write taken; the registers as the page stored them |
| `nack` | a write or read refused, with the link's reason: `BAD_PAGE` (1), `BAD_RANGE` (2), `READ_ONLY` (3), `BAD_VALUE` (4), `NOT_ARMED` (5), `BUSY` (6) |
| `err` | the console did not understand the line, or the page did not answer |
| `page` | one line of `pages` |
| `hitec` | a hitec command and its report |

Output passes through a 2,048-byte buffer in whole lines, at the rate the
USB host or the panel over the link can receive it. Lines that do not fit
are dropped, followed by `err output full, N bytes dropped`.

## Raw pages

| Command | Does |
| --- | --- |
| `help` | the commands |
| `pages` | each page: number, registers, `rw` or `ro` |
| `r <page> <off> [n]` | read n registers, 1 to 32, 1 when left out |
| `w <page> <off> <v> [v ...]` | write 1 to 32 registers of 16 bits |

```text
> r 0x00 0 2
data page=0x00 off=0 n=2: 0x0004 0x001B
> w 0x2A 4 1 5000 1000
nack page=0x2A off=4: NOT_ARMED (5)
```

Lines typed are shown here after `> `; the console prints no prompt. The
first line reads the protocol version, 4.27. The second is SUPPLY's ON at
5000mV and 1000mA, refused because no panel sends a heartbeat. With a
panel running, the same line is taken:

```text
> w 0x2A 4 1 5000 1000
ack page=0x2A off=4 n=3: 0x0001 0x1388 0x03E8
```

The same commands can access the programming ports and start a KST
session. CMD 0x0041 is ENTER (1) on port 0 with bit 6 set; use the page's
current `SEQ` plus 1:

```text
> r 0x33 0 8
> w 0x32 26 5000 1000
> r 0x32 1 1
data page=0x32 off=1 n=1: 0x0000
> w 0x32 0 0x0041 1 0 0
> r 0x32 0 4
```

[The programming port for KST and Chaservo servos](LinkKST.md) has the
registers, the key and the check.

## Hitec

The `hitec` commands write the HITEC page
([The programming port for Hitec servos](LinkHitec.md)). Each stages the
page's registers 4 to 21, computes their check, reads `SEQ` and sends the
command with `SEQ` plus 1. It puts the write key 0x48A5 in every SEND and in
a LISTEN or POWER that answers. A command the page refuses is said at once:

```text
hitec SEND port=0 refused STOPPED
```

A command that starts an operation prints `hitec <op> port=<n> started`.
The report follows when the port finishes the operation. Commands that
leave the port free or open within their own frame, such as CLOSE or
CLEAR, print the report immediately. The report contains these lines,
with the last repeated for each received byte:

```text
hitec done state=<state> port=<n> flags=<flags> seq=<n> op=<op> count=<n> result=<result> refusal=<refusal>
hitec line pin=<pin> line=<line> rx=<pu|pre> bit_ns=<ns> lead_ns=<ns> span_us=<us>
hitec cap n=<n> full=<0|1> answered=<0|1> overflow=<0|1> aborted=<0|1> dec=<dec> rx=<rx> id=<id> value=<value> delay_ns=<ns> t_us=<us> window_us=<us>
hitec byte <k> <value> at_ns=<ns> framing=<0|1>
```

The names are the page's: the states, operations, results and refusals of
[The programming port for Hitec servos](LinkHitec.md#registers), and the
decoder results of `hitec_rx_t`. `hitec cap` and `hitec byte` come only
while the page holds a capture.

`hitec` and `hitec state` print the same report with `now` in place of
`done`:

```text
hitec now state=OPEN port=0 flags=RAIL ...
```

`flags=PRECHARGE` and `rx=` describe different states. `PRECHARGE` in
`flags` reflects register 23: the receive mode for the next OPEN of a
D series line. `rx=` on the `hitec line` line shows the mode selected by
the last OPEN. It remains available after CLOSE. Writing register 23
while a line is open can make the two differ, because the open line
keeps its mode until CLOSE. The panel writes register 23 at every link-up
and at every edit of Hitec D receive on the Hitec tab's MORE list:
every value set on SETUP is in force at once.

`lead_ns` runs from the lead's first edge to the release of the line: the
lead and every bit time sent. The lead is the one asked, or longer: the PIO
(programmable input/output) program sends whole words of 16 bit times, and
the bit times that pad the front of the stream to a word count as lead. A D
series read gives `lead_ns=559886`, at a bit time of 8.68us:

| Part | Bit times | Time |
| --- | ---: | ---: |
| Lead: 8 cycles and the 13 bit times of padding (88us asked) | 13.5 | 117.2us |
| 5 bytes of 10 bits | 50 | 434.0us |
| 1 released bit time before the receiver starts | 1 | 8.7us |
| `lead_ns` | 64.5 | 559.9us |

A D series write gives `lead_ns=705283`: 89.0us of lead, 70 bit times of
bytes and 1 released bit time.

| Command | Does |
| --- | --- |
| `hitec [state]` | the page, decoded, with the capture |
| `hitec open <port> c\|d [od\|pp] [pu\|pre]` | OPEN on port 0 to 3 (PROG PORT 1 to 4; 0 alone before protocol 4.26): classic or D series, open drain (the default) or push-pull. For the D series `pu` or `pre` first writes the receive mode, PULL-UP or PRECHARGE (register 23, [Receive mode](LinkHitec.md#receive-mode)); without either the OPEN takes the mode the page holds. Classic refuses both: a classic servo needs neither |
| `hitec close <port>` | CLOSE |
| `hitec abort <port>` | ABORT |
| `hitec clear <port>` | CLEAR, after a stop |
| `hitec send <port> [lead=<us>] [gap=<bits>] [window=<us>] [rx=<n>] [dec=<dec>] <byte>...` | SEND of 1 to 8 bytes |
| `hitec listen <port> window=<us> rx=<n> [answer=<byte>] [dec=<dec>]` | LISTEN |
| `hitec level <port> 0\|1 <us>` | LEVEL |
| `hitec power <port> mv=<mV> ma=<mA> off=<ms> window=<us> rx=<n> [answer=<byte>] [dec=<dec>]` | POWER: the supply off, held off, on at 4800 to 6000mV, and the LISTEN from the ON |
| `hitec c <port> <cmd> <p1> <p2>` | a classic command with its sum byte, 1 idle bit between bytes, a window of 1500us for 2 bytes, and the classic decoder when the command is one the module knows |
| `hitec d <port> read <id> <addr>` | a D series read: 88us of lead asked, driven low for 117.2us, 5 bytes, a window of 25,000us for 8 bytes, and the D series decoder |
| `hitec d <port> write <id> <addr> <value>` | a D series write: 88us of lead asked, driven low for 89.0us, 7 bytes, nothing received |

`<dec>` is `none`, `c:<cmd>`, `d:<addr>` or `l:<byte>`.

`more` as the last word of `send`, `listen`, `level`, `power`, `c` or `d`
sets bit 4 of the command (`MORE`, protocol 4.31): more steps of this write
follow. Put it on every step of a write to the servo's memory but the last,
for example a classic cell write before its balance, or a D series write
and its save before the reboot. While it stands, `flags=MORE`, an OFF the
panel writes to SUPPLY waits for the write's last step, 10,000ms after the
OFF at most ([An OFF during a write](LinkHitec.md#an-off-during-a-write)).
A step typed 10,000ms or more after the OFF goes to a servo whose supply
is off. OFF is also deferred during a SEND whose frame writes, and for 150ms
after it, even without `more`. OFF is not deferred during a read or a
LISTEN.

```text
hitec c 0 0x62 0x10 0x55 more
hitec c 0 0x62 0x1C 0xAA
```

Omitted options of `send`, `listen` and `power` default to 0: no gap,
no window, no received byte and no decoder. The exception is `answer`: if
omitted, no answer is sent; `answer=0` sends 0x00. A `lead=` left out still
leaves the front padding of the PIO words: a 5-byte D series SEND is
driven low for 117.2us with or without `lead=88`. `send` adds no sum
byte. The raw form of `hitec d 0 read 0 0x32`:

```text
> hitec send 0 lead=88 window=25000 rx=8 dec=d:0x32 0x96 0x00 0x32 0x00 0x32
```

The last byte is the low byte of the sum of every byte after the header
0x96: 0x00 + 0x32 + 0x00 = 0x32. The same frame with the sum byte 0xC8,
which counts the header, got no answer from a D485HW.

A D series line needs a pull-up on the board. Tested with a D485HW:
2.2kΩ from the signal wire to the servo supply, and every read is
answered; with the pad's pull-up alone the servo answers none
(`rx=SILENT`). [The programming port for Hitec servos](LinkHitec.md#the-way-in-and-out)
has the unknowns.

`hitec c` needs an open classic line and `hitec d` an open D series line.
On a line of the other family the console writes nothing and says so:

```text
err the line is classic; this command needs a D series line
```

A session by hand, classic, port 0:

```text
> hitec open 0 c
hitec OPEN port=0 started
hitec done state=OPEN port=0 ...
> hitec c 0 0x67 0 0
> hitec c 0 0x61 0x12 0
> hitec power 0 mv=5000 ma=1000 off=1000 window=2000000 rx=1 dec=l:0x2B
> hitec close 0
```

A D series read on port 0 with no pull-up on the signal wire:

```text
> hitec open 0 d pp pre
hitec OPEN port=0 started
hitec done state=OPEN port=0 flags=RAIL,PRECHARGE ...
hitec line pin=13 line=0x11 rx=pre ...
> hitec d 0 read 0 0x32
```

`pre` and `pu` write the register controlled by SETUP Hitec D receive
(the Hitec tab's MORE list). The panel writes it again whenever that row
is edited and whenever the link comes up.

A raw `w` of the HITEC page gets no report and no key; the
[worked example](LinkHitec.md#a-transaction-by-hand) writes one transaction
that way.

## The signal wire's pin driver

`ewline <gpio> tx|abort|rx` runs the pin driver of the ESC (electronic speed
controller) signal wire (`protocols/escwire/rp2350/ew_line.h`) on a free pin,
for a logic analyser with no ESC attached. It is not a page and has no key: it
sends fixed bytes, none of them a throttle signal, an erase or a program. It
refuses a pin the outputs, ports, supply, sensor bus or tone capture hold, a pin
that drives, and any pin while the bank is ARMED. A programming port that names
the pin holds it, so that port's PIN goes to 0xFFFF first: `w 0x33 4 0xFFFF` for
port 0, `w 0x33 4 13` to give GPIO 13 back. During a test the pin
is reserved: the pages refuse it. An ARM during a test closes the line at once
and answers `ewline armed`.

| Test | On the pin, in order |
|---|---|
| `tx` | parked 20ms; high 50ms; the first contact (17B at 52us a bit) 3 times, each followed by its 100ms wait; pull-up 20ms; 12B with 10 idle bit times, 520us, after the sixth; pull-up 20ms; `info` and LF at 38400baud and a 200ms wait; pull-up 20ms; 0x55 at 52us a bit into a pull-down; pull-up 20ms; 0x55 at 38400baud into a pull-down; parked 20ms; low 300ms; parked 20ms |
| `abort` | high 5ms; the first contact, cut 3ms after its start; parked 20ms; high 5ms; 0x55 into a pull-down; parked 20ms |
| `rx` | high 5ms; 3 times 0x55 and a 9B answer driven on the pad by the pin's overrides: at 52us a bit, with a low stop bit on the third byte and 2 idle bit times after it, at 38400baud; each followed by 5ms |

0x55 into a pull-down shows the release: the stop bit's rising edge, then the
pad falls 1 bit time later, when it stops driving. The `rx` test blocks the main
loop for its 9B, 4.7ms at 52us a bit.

```
ewline open gpio=13 bit_a=52000ns bit_text=26042ns
ewline capture n=9 bad_stop=0 aborted=0 overflow=0 match=1
ewline done gpio=13 start_max=216us poll_max=96us level_max=30us abort=53us
```

The last line gives the longest call of each kind in microseconds, here the
longest seen on the station at commit aeea917d
([what was seen](../protocols/escwire/README.md#on-a-chip-no-esc-attached)). A
load on the line, such as a servo's signal wire, reads low on the pull-up alone,
and a high pulse of 6.5us, 3.26us at 38400baud, shows before each frame's first
start bit. A test on such a line can end in `n=` values that do not match.

## The ESC settings page

`esc` accesses [the ESC_WIRE page](LinkEsc.md) with the same frames as the
panel. The same checks and refusals apply. Like other pages that require
a heartbeat, it requires a running panel alongside the console. None of
its commands writes to an ESC. It is absent from released images.

```
esc
esc open slot <n>
esc open port <n>
esc enter mv=<mV> ma=<mA> [hold=<ms>] [off=<ms>]
esc enter hand [hold=<ms>]
esc done|read|leave|stop|abort|clear|close
```

SWITCHED takes `mv` and `ma` with no default; `hold` and `off` default to 3000ms. Each command
answers one line:

```
esc state=2 op=1 result=2 ref=0 flags=0x0008 line=0 gpio=5 ses=0 fw=0 layout=0 why=0 t=52ms
```

`state`, `result`, `ref`, `flags` and `line` are the registers of
[its page](LinkEsc.md#registers); `ses` is the session's last result.

## Over the link, with no USB on the IOmcu

Debug builds only. No released image of either end holds any of this.

A panel built with `RCBENCH_PANEL_CONSOLE` relays its UART0 and
USB-Serial-JTAG serial ports to an IOmcu built with `IOMCU_CONSOLE`. It uses
the link's CONSOLE page (0xE0). Commands run on the IOmcu with the same
behaviour and refusals as on USB, including `ewline` and other commands
that drive lines. The IOmcu's USB connection remains usable.

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S firmware/iomcu -B firmware/iomcu/build-console -DIOMCU_CONSOLE=ON
cmake --build firmware/iomcu/build-console
cp firmware/iomcu/build-console/rcbench-iomcu.bin firmware/panel/iomcu-console.bin
idf.py -C firmware/panel -B firmware/panel/build-console \
    -D RCBENCH_PANEL_CONSOLE=ON -D RCBENCH_IOMCU_BIN=iomcu-console.bin build
```

The panel carries that image. An IOmcu running another build answers the build check as different, so UPDATES offers the image, with the version number the same: `Same version number, but a different build.` After the update the check finds the same build and offers nothing. A panel that carries a released image offers that one back to a console IOmcu in the same way. An rbmod carrier needs its own console image, built with `-DIOMCU_BOARD=rbmod`, in `RCBENCH_IOMCU_RBMOD_BIN`. A panel built with the option and no image offers nothing.

| Line | Means |
| --- | --- |
| `console: link up t_ms=<ms>` | the link came up, in the panel's time from its start |
| `console: link down t_ms=<ms>` | the link went |
| `console: the IOmcu has no console. Take the update UPDATES offers, or flash a build with IOMCU_CONSOLE=ON` | the page answered `BAD_PAGE`; said once, and the panel looks again every 5000ms |
| `console: the IOmcu's output restarted at <n>, expected <m>` | the output's place moved: the IOmcu started again |
| `console: no pass from t_ms=<a> to t_ms=<b>: the link was not followed` | no control pass reached the relay for 1500ms or more, as during an IOmcu update: a link down or up between the two times has no line |
| `err no link: line dropped` | a line with the link down |
| `err line lost: the link went down inside it` | the link went while the line was going over |
| `err line lost: the IOmcu restarted inside it` | the IOmcu started again while the line was going over |
| `err no answer: line lost` | a chunk of the line had no answer |
| `err the IOmcu has no console` | a line, and no page to send it to |
| `err line longer than 160 characters` | the line is refused whole |
| `err line dropped: 8 lines wait` | a line while 8 wait for the link |
| `err panel buffer full, <n> bytes dropped` | the panel's own 4,096-byte buffer had no room, as when a serial host stops reading and the IOmcu starts again |

The relay's own messages never interrupt an IOmcu line. A window may end
within a line, so the panel buffers the IOmcu line until its LF, up to
256 bytes. The relay's message is sent first. A longer IOmcu line is sent
in parts; a relay message after a split starts a new line. If the IOmcu
restarts with a line incomplete, the panel adds an LF to that line before
sending the `restarted` message.

The panel detects the page by reading it. There is no capability bit or
protocol minor version for this page. An IOmcu without it answers
`BAD_PAGE`, as it does for any unsupported page.

### Bluetooth LE: `ble scan`

A line that starts with the word `ble` stays on the panel and does not go
to the IOmcu. The panel holds each typed line until its end for this.
`ble scan` starts Bluetooth LE when it has not started ([Building,
Bluetooth LE through the ESP32-C6](Building.md#bluetooth-le-through-the-esp32-c6)),
waits for the host's sync, and runs one active scan of 10,000ms with the
controller's duplicate filter on: a device that advertises every 256ms
appears once in the scan. Another `ble` line prints the commands.

| Line | Means |
| --- | --- |
| `ble: starting Bluetooth LE; the scan follows the sync` | the first `ble scan` since the panel started |
| `ble: scan for 10000ms` | the scan started |
| `ble: adv <address> type <n> rssi <n>dBm evtype <n> data <hex>` | one device: its address with the most significant byte first, its address type (0 public, 1 random), the signal strength, the report's event type (0 connectable advertising, 4 scan response) and the advertising data in hexadecimal |
| `ble: scan done: <n> reports, reason <n>, <n> events dropped since start-up` | the scan ended; reason 0 is its end after 10,000ms |
| `ble: no scan: no link to the C6` | the C6's link failed |
| `ble: no scan: no sync` | the host had no sync within 3,000ms; the host keeps trying, and a later `ble scan` scans once it has synced |
| `ble: no scan: <error>` | NimBLE refused the scan |
| `ble: a scan is under way` | a `ble scan` while one waits or runs |
| `ble: commands: ble scan` | any other line that starts with `ble` |

### Rates

The relay performs at most one exchange per control pass, after the
panel's own polling. A pass takes 5ms when no other work delays it. The
relay performs no exchanges while an IOmcu update holds the link. The
table is calculated from the frames with a pass of exactly 5ms.

| Traffic | Frames | Rate |
| --- | --- | --- |
| A look at rest | 2 every 50ms | 40 frames/s |
| A window of output, 28 bytes in 14 registers | 5, a request and 4 replies of 4 registers | one pass |
| Its acknowledgement | 2 | one pass |
| Output, sustained | 7 for each 28 bytes, 2 passes | 2,800 bytes/s, 700 frames/s |
| Input, 6 bytes a chunk | 2 for each chunk, one pass | 1,200 bytes/s, 135ms for a line of 160 characters |

A window is one read of 16 registers, the most a read asks for: 4 reply frames fit the panel's TWAI receive FIFO ([The link](Link.md)). The page stays 32 registers wide, and the registers past the window read 0, so a panel built for 60-byte windows still reads it. This panel takes a window of up to 60 bytes from an IOmcu console build older than protocol 4.35 and reads it as 32 registers in one exchange.

The station measured output of 2,856 to 3,510 bytes/s with a 60-byte
window, which this build does not use. With `help` sent at 6/s to 30/s,
each window took 17ms to 21ms, compared with the calculated 10ms. The
control task waits 5ms after each pass's work (`vTaskDelay`), so a pass
takes 5ms plus the time for its exchanges: the panel's polls and the
window's frames. Their individual contributions are not measured. At the
same 17ms to 21ms per window, a 28-byte window would give about 1,330 to
1,650 bytes/s; this is not measured. A window and its acknowledgement
require 2 passes. After the last window of a burst, the next poll can be
up to 50ms later.

At 1Mbit/s a frame of 8 bytes takes about 150us, so sustained output at the calculated rate takes about 11% of the bus. At 1,330 to 1,650 bytes/s the 2,048-byte buffer empties in about 1.2s to 1.5s. Output faster than that for longer than that buffer lasts loses whole lines, the IOmcu's own 3s lines included; the next line says `err output full, N bytes dropped` as it does on USB. Output is also held back when the panel's own buffer of 4,096 bytes has under 412 bytes free, as when a serial host stops reading.

The IOmcu retains a window until it is acknowledged. A lost reply
therefore requires another read but loses no bytes. An unanswered exchange
blocks the control pass for up to 1000ms, as other panel exchanges do; the
heartbeat continues.

### What it does not change

The commands reach the pages through `link_dev_apply()`. A request from the console is not link traffic: it does not hold the silence failsafe off. The frames of the CONSOLE page are requests of the panel, like its polls, and stop when the panel stops. The panel's own modules keep writing: the supply link, `hitec_link`, `kst_link`, `esc_link` and the 50ms write of ARM and THROTTLE can overwrite what a console `w` wrote to the same registers. A console write of ARM reaches the page, and the panel's next 50ms write overwrites it with the panel's own arming state.

At IOmcu start-up, output waits in the 2,048-byte buffer for a USB host or
a panel, up to the buffer's capacity. The first line,
`rcbench-iomcu: reset ...`, gives the cause of the start and repeats every
3s. A line from the panel is processed as a whole without changing a
partially typed USB line. When both a USB host and a panel are connected,
output follows the slower reader. A USB host that leaves the port open
but stops reading also holds back output.

## Limits

- The console reads nothing while the IOmcu writes flash: a line typed then
  waits in the USB buffer.
- The output buffer holds 2,048 bytes. A report of 16 received bytes takes
  about 1,100.
- A pass hands the USB port as many bytes as its transmit buffer has free,
  at most 400, and never waits for the host. A line longer than that leaves
  in pieces over several passes; its rest goes first. Any line up to the
  longest, 200 bytes with its LF, leaves with the SDK's 64-byte buffer as
  well.
- The IOmcu's own lines, `rcbench-iomcu: ...` every 3s and after a flash
  write, go through the same output in this build: they come after the
  line before them, never inside it, and end with LF alone.
- `shared/outputs/page_console.c` holds the rules and
  `test/host/test_page_console.c` tests them against a model of the HITEC
  page's driver.
