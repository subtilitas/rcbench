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
silence failsafe off (200 ms), and it is not a heartbeat. Every page that
needs a trusted heartbeat and a link that is not silent needs a running
panel beside the console: SUPPLY's ON, a KST session, every HITEC command
but ABORT and CLOSE, and an ARM. Without a panel those are refused or
stopped, as they are on a bench whose panel has stopped.

The console can write what the panel can write, CONTROL included. A write
of ARM from the console arms the bank when every rule of the safety gate
lets it ([Safety](Safety.md)). A CLEAR written from the console releases
the arm latch and the failsafe as the panel's does; the link goes silent
again 200 ms after the panel's last request. Use the console on a bench
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

Built with ARM GNU 14.2 and pico-sdk 2.3.0, the console image is 10,860
bytes larger than the default image and holds 4,124 bytes more RAM
(random-access memory): 2,048 of them the output buffer, 960 the USB
transmit buffer of 1,024 bytes in place of the SDK's 64. Core 0's deepest
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
data page=0x00 off=0 n=2: 0x0004 0x0019
> w 0x2A 4 1 5000 1000
nack page=0x2A off=4: NOT_ARMED (5)
```

Lines typed are shown here after `> `; the console prints no prompt. The
first line reads the protocol version, 4.25. The second is SUPPLY's ON at
5000 mV and 1000 mA, refused because no panel sends a heartbeat. With a
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

`lead_ns` runs from the lead's first edge to the release of the line: the
lead and every bit time sent. The lead is the one asked, or longer: the PIO
(programmable input/output) program sends whole words of 16 bit times, and
the bit times that pad the front of the stream to a word count as lead. A D
series read gives `lead_ns=559886`, at a bit time of 8.68 us:

| Part | Bit times | Time |
| --- | ---: | ---: |
| Lead: 8 cycles and the 13 bit times of padding (88 us asked) | 13.5 | 117.2 us |
| 5 bytes of 10 bits | 50 | 434.0 us |
| 1 released bit time before the receiver starts | 1 | 8.7 us |
| `lead_ns` | 64.5 | 559.9 us |

A D series write gives `lead_ns=705283`: 89.0 us of lead, 70 bit times of
bytes and 1 released bit time.

| Command | Does |
| --- | --- |
| `hitec [state]` | the page, decoded, with the capture |
| `hitec open <port> c\|d [od\|pp] [pu\|pre]` | OPEN: classic or D series, open drain (the default) or push-pull. For the D series `pu` or `pre` first writes the receive mode, PULL-UP or PRECHARGE (register 23, [Receive mode](LinkHitec.md#receive-mode)); without either the OPEN takes the mode the page holds. Classic refuses both: a classic servo needs neither |
| `hitec close <port>` | CLOSE |
| `hitec abort <port>` | ABORT |
| `hitec clear <port>` | CLEAR, after a stop |
| `hitec send <port> [lead=<us>] [gap=<bits>] [window=<us>] [rx=<n>] [dec=<dec>] <byte>...` | SEND of 1 to 8 bytes |
| `hitec listen <port> window=<us> rx=<n> [answer=<byte>] [dec=<dec>]` | LISTEN |
| `hitec level <port> 0\|1 <us>` | LEVEL |
| `hitec power <port> mv=<mV> ma=<mA> off=<ms> window=<us> rx=<n> [answer=<byte>] [dec=<dec>]` | POWER: the supply off, held off, on at 4800 to 6000 mV, and the LISTEN from the ON |
| `hitec c <port> <cmd> <p1> <p2>` | a classic command with its sum byte, 1 idle bit between bytes, a window of 1500 us for 2 bytes, and the classic decoder when the command is one the module knows |
| `hitec d <port> read <id> <addr>` | a D series read: 88 us of lead asked, driven low for 117.2 us, 5 bytes, a window of 25,000 us for 8 bytes, and the D series decoder |
| `hitec d <port> write <id> <addr> <value>` | a D series write: 88 us of lead asked, driven low for 89.0 us, 7 bytes, nothing received |

`<dec>` is `none`, `c:<cmd>`, `d:<addr>` or `l:<byte>`.

A D series line needs a pull-up on the board. Tested with a D485HW:
2.2 kOhm from the signal wire to the servo supply, and every read is
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

`pre` and `pu` write the same register the panel writes from SETUP >
INTERFACES > Hitec D receive. The panel writes it again at its next edit
of that row and at every link-up.

A raw `w` of the HITEC page gets no report and no key; the
[worked example](LinkHitec.md#a-transaction-by-hand) writes one transaction
that way.

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
- The IOmcu's own lines, `rcbench-iomcu: ...` every 3 s and after a flash
  write, go through the same output in this build: they come after the
  line before them, never inside it, and end with LF alone.
- `shared/outputs/page_console.c` holds the rules and
  `test/host/test_page_console.c` tests them against a model of the HITEC
  page's driver.
