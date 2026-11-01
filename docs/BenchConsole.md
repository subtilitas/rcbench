# The bench console

<sub>**English** · [Deutsch](BenchConsole-de.md)</sub>

The bench console is a bench tool. It is not in any released image. An IOmcu
built with the option `IOMCU_CONSOLE` reads command lines on its USB
(Universal Serial Bus) serial port and reads and writes the link pages of
[The link](Link.md) from them. A station on that port drives the IOmcu's
pages without a panel screen for them: the KST page, SUPPLY, PROG, HITEC
and every other page.

Every request goes through the page handlers the link uses. A page refuses
to the console what it refuses to the panel: an ARM write while the arm
latch is set, a KST or HITEC command without its write key or with a wrong
check, a SUPPLY ON without a heartbeat, a transaction after a stop. The
console has no rights the panel does not have.

A request from the console is not link traffic. It does not hold the link's
silence failsafe off (200ms), and it is not a heartbeat. Every page that
needs a trusted heartbeat and a link that is not silent needs a running
panel beside the console: SUPPLY's ON, a KST session, every HITEC command
but ABORT and CLOSE, and an ARM. Without a panel those are refused or
stopped, as they are on a bench whose panel has stopped.

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

Built with ARM GNU 14.2 and pico-sdk 2.3.0, the console image is 17,784
bytes larger than the default image and holds 9,216 bytes more RAM
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

The output goes out in whole lines through a buffer of 2,048 bytes, as fast
as the USB host takes it. What does not fit is dropped, and a line
`err output full, N bytes dropped` follows.

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

The programming ports and a KST session the same way. CMD 0x0041 is ENTER
(1) on port 0 with bit 6 set; `SEQ` is the page's plus 1:

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

A command that starts something prints `hitec <op> port=<n> started`, and
the report follows when the port has finished it. A command that leaves the
port free or open in its own frame, such as CLOSE or CLEAR, prints the
report at once. The report's lines, the last one for each byte received:

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

`flags=PRECHARGE` and `rx=` say two things. `PRECHARGE` in `flags` is
register 23, the receive mode the next OPEN of a D series line takes.
`rx=` on the `hitec line` line is the mode the last OPEN took: the open
line's while a line is open, and kept after CLOSE. They differ after
register 23 is written while a line is open: the open line keeps its mode
until CLOSE. The panel writes register 23 at every link-up
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
is off. A SEND whose frame writes is held without `more` too, and for
150ms after it; an OFF during a read or a LISTEN is not held.

```text
hitec c 0 0x62 0x10 0x55 more
hitec c 0 0x62 0x1C 0xAA
```

An option of `send`, `listen` or `power` left out is 0: no gap, no
window, no byte received, no decoder. `answer` is the exception: left out,
nothing is answered; `answer=0` answers 0x00. A `lead=` left out still
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

`pre` and `pu` write the same register the panel writes from SETUP Hitec D receive (the Hitec tab's MORE list). The panel writes it again at its next edit
of that row and at every link-up.

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

`esc` drives [the ESC_WIRE page](LinkEsc.md) in the frames a panel sends: the same
checks, the same refusals. It needs a running panel beside the console, as every page
that needs a heartbeat does. No command of it writes to an ESC. It is not in a released
image.

```
esc
esc open slot <n>
esc open port <n>
esc enter [mv=<mV>] [ma=<mA>] [hold=<ms>] [off=<ms>] [hand]
esc done|read|leave|stop|abort|clear|close
```

`esc enter` defaults to 7400mV, 1000mA, 3000ms and 3000ms, SWITCHED. Each command
answers one line:

```
esc state=2 op=1 result=2 ref=0 flags=0x0008 line=0 gpio=5 ses=0 fw=0 layout=0 why=0 t=52ms
```

`state`, `result`, `ref`, `flags` and `line` are the registers of
[its page](LinkEsc.md#registers); `ses` is the session's last result.

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
