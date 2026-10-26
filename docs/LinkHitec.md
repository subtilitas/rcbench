# The programming port for Hitec servos

<sub>**English** · [Deutsch](LinkHitec-de.md)</sub>

This page is a section of [The link](Link.md), which holds the wiring, the frame
format, the page map and the other pages. Pins are named by GPIO (general-purpose
input/output) number. PWM is pulse-width modulation.

Hitec is a name or trademark of its owner. It is used here only to say which
servos the page works with.

The Hitec page (`HITEC`, 0x34) is new in 4.23; write register 23, the
receive mode of a D series line, is new in 4.24. From 4.29 a held port
holds the SUPPLY page from OPEN to CLOSE
([The supply while a port is held](#the-supply-while-a-port-is-held)).
The page runs raw transactions of
the Hitec servo wire of [`protocols/hitec`](../protocols/hitec/README.md) on
one port of [PROG](Link.md#the-programming-ports): classic (HS-5xxx,
HS-7xxx, 19,200bit/s) or D series (115,200bit/s). The page holds no Hitec
session. The writer stages one transaction, the page runs it, and the
capture and its decoding stay on the page until the next one. Which bytes a
servo takes, and in which order, is the writer's business. The session core
([`hitec_session.h`](../protocols/hitec/README.md#session)) and the panel's
link half ([The panel's link half](#the-panels-link-half)) are built;
nothing on the panel calls them yet. `shared/outputs/hitec_port.c` holds
the rules.

A port is free, held by this page or held by the KST page
([The programming port for KST and Chaservo servos](LinkKST.md)), never two
of them. The page holds one port at a time; the KST page may hold another.
Nothing is kept in flash: after a restart of the IOmcu the port is
free.

Any of the build's ports may be opened, 4 from protocol 4.26: the port's pin
is handed over whatever its FUNCTION, a NONE port's parked pin as a SERVO
PWM port's, and given back to that function at CLOSE. The PIO and DMA use is
the line's, whichever port it is on: one line, so at most 2 state machines
and 3 DMA channels with PRECHARGE.

The panel reads `STATE` with the PROG page, every 500ms, from 4.23 on. While
it names a port held (STOPPING to POWER_OFF), SERVO > PROGRAM dims a row on
that port with `PORT IN USE`
([Screens](Screens.md#programmer)).

## The way in and out

OPEN takes the port:

1. STOPPING, 50ms: the port renders no pulse, so a pulse that runs ends.
2. The PWM generator gives up the pin. The wire driver opens the line on it
   as a released input: classic with the pad's pull-down, the D series with
   its pull-up and the input inverted. The internal pulls are 50 to 80kΩ;
   the D series receives with a pull-up on the board, or by PRECHARGE
   ([Receive mode](#receive-mode)). Tested with a D485HW: with the pad's
   pull-up alone the line stays below 1.4V and the servo answers no read;
   with 2.2kΩ from the signal wire to the servo supply it answers every
   read. The line voltages, and whether the servo's high level stays within
   the pin's rating, are not measured
   ([Electrical](../protocols/hitec/README.md#electrical)).
3. OPEN. A driver that gets no PIO (programmable input/output) state machine,
   no 30 words of program space or no 2 DMA (direct memory access) channels
   leaves the port free with result 5 (`NO_LINE`). PRECHARGE needs a second
   state machine, 7 words of program space in another PIO block at 150MHz,
   and a third DMA channel; without them the port stays free with
   `NO_LINE` as well.

In OPEN a command starts one transaction (BUSY). The port is in OPEN again
when the driver hands back the capture. The driver ends a transaction itself
1ms after the longest it can take: the capture keeps every byte received
before that end and is marked overflow (`CAP` bit 10). A transaction that
still runs after 10,000ms is aborted by the page as well.

CLOSE gives the port back from any state but free: a running transaction is
cut, the line closed and the pin the generator's. The supply is left as it
is. ABORT cuts what runs and keeps the port.

## The supply while a port is held

From 4.29 the page holds the SUPPLY page while it holds a port: from the
frame of the OPEN to the frame of the CLOSE, in STOPPING, OPEN, BUSY and
POWER_OFF, a stopped port included. The KST page holds it the same way
while its port is out of PWM
([The programming port for KST and Chaservo servos](LinkKST.md)). While it
is held:

- bit 11 of SUPPLY's flags is set;
- SUPPLY refuses with BAD_VALUE every write of its registers 4 to 6 that is
  not an OFF: an ON of an output that is off, and any change of the voltage
  or of the current limit;
- a write with OUTPUT 0 is taken as an OFF, whatever set points come with
  it, and the page keeps the set points it had;
- a write that repeats an output that is on with the set points in force is
  taken;
- POWER switches the supply as staged: the hold holds the panel's writes,
  not the port's own.

Between two POWERs the set points are the last POWER's, or the ones in
force at the OPEN. The hold ends with the port: at CLOSE, at an ABORT or a
stop while the port is in STOPPING, at an OPEN that ends with result 5
(`NO_LINE`), and at a restart of the IOmcu. A stop, an unsafe bench, an
ABORT in OPEN, BUSY or POWER_OFF, and the abort after 10,000ms keep the port
and the hold. A lost heartbeat or a silent link switches the supply off all
the same: SUPPLY does that itself, held or not ([SUPPLY](Link.md)).

An IOmcu of 4.23 to 4.28 holds SUPPLY only from the OFF of a POWER to the
end of its LISTEN, and takes the panel's set points between two POWERs.
The panel reads bit 11 from either.

## Receive mode

Write register 23 (`RX_MODE`) says how a D series line receives the
servo's answer, from the next OPEN on. The OPEN takes the value the
register holds when the OPEN is written; a change while a line is open
waits for the next OPEN. An OPEN of a classic line ignores the register:
a classic servo drives its high level itself, through about 1kΩ, and
answers with neither a pull-up nor a precharge on an open-drain port
(measured on an HS-7955TG, an HS-5955TG and an HS-5645MG at a 5.0V
supply; the line voltages are not measured).

| Value | Name | How the answer is received |
| ---: | --- | --- |
| 0 | `PULL-UP` | the line released; a 2.2kΩ pull-up from the signal wire to the servo supply lifts it. The default, and the value after an IOmcu restart |
| 1 | `PRECHARGE` | no pull-up. Every 1086.7ns the IOmcu drives the pad high for 100.0ns, releases it, and samples it 113.3ns after the release |

On a PRECHARGE line:

- A SEND with a receive window goes out with no receiver. The first pass
  of the IOmcu's main loop that finds the request ended hands the pad to a
  second state machine: 2mA drive, slow slew, the input not inverted, the
  pad's pull-up as it was. A DMA channel writes the samples to memory, and
  each pass decodes the samples written since the last one as inverted
  8N1 (`protocols/hitec/include/hitec_pre.h`).
- A LISTEN, and the LISTEN of a POWER, sample from their start.
- The capture is the one a PULL-UP line gives: the same registers, the
  same decoders, the same result codes. A byte's time carries the
  sampler's start, read off the IOmcu's microsecond timer, so it holds
  to 1us; a start bit's edge lies within half a sample, 543ns, of the
  time given.
- Refused with `RANGE`: a LISTEN or a POWER that answers, and a receive
  window above 30,000us.
- A SEND with no window (a D series write) and a LEVEL run as on a
  PULL-UP line: no sampler.
- At the end of the receive, an ABORT, a CLOSE and a stop, the sampler
  stops with the pad released, and the pin's control and pad registers
  are written back as they were.
- A receive started with the supply asked on is cut (result 6, `CUT`) at
  the pass that finds the supply asked off, no PD mini answering, or its
  output read off after it read on (the PD mini's protection leaves
  OUTPUT asked on). A SEND or a LISTEN counts as read on from its start.
  POWER's LISTEN starts in the pass of the ON, while the output still
  reads off; it counts as read on from the first pass that does not read
  the output off. One whose output reads off for the whole window,
  30,000us at most, runs to its end with result 2 (`DONE`). A receive
  started with the supply off, or with no PD mini, runs to its end.

`STATE` bit 7 reads the register; register 25 of `VIEW` 2, bit 8, says the
open line receives by PRECHARGE. An IOmcu speaking 4.23 refuses a write
that reaches register 23 with READ_ONLY and receives with the pull-up; the
panel then marks the setting on SETUP
([INTERFACES: the programming port](Screens.md#interfaces-the-programming-port)).
What is measured and what is not:
[`protocols/hitec`](../protocols/hitec/README.md#receive-by-precharge-and-sample).

## Transactions

| Operation | What goes on the wire | Key |
| --- | --- | --- |
| SEND | the lead at the idle level (D series: driven low), 1 to 8 bytes with 0 to 6 idle bit times between two, then the receiver for the window | always |
| LISTEN | the receiver for the window; with an answer, one byte at the end of the stop bit of the first byte received, and the receiver again from the end of the answer's stop bit | with an answer |
| LEVEL | the line held at a level for 1us to 4s, then released. Open drain drives low only | never |
| POWER | the supply cycle below, then a LISTEN from the ON | with an answer |

The answer is 10 bit times: start bit, 8 data bits, stop bit. Its start
bit begins 10.0 to 10.13 bit times after the start bit of the first byte
received. The receiver waits for the next start bit from the end of the
answer's stop bit, so a byte the servo starts right after the answer is
received.

Leads, windows and levels run to 4,000,000us. A capture holds up to 16
bytes. The window counts the time spent waiting for a start bit only. Each
received byte has its start bit's time in ns from the receiver's start, and
its stop bit as sampled.

A SEND, a LISTEN or a POWER names a decoder for the capture:

| Decoder | Argument | Decodes |
| ---: | --- | --- |
| 0 | none | nothing; `DEC` reads 0, `VALUE` 0 |
| 1 | the classic command byte | the reply that command has (`hitec_c_decode()`); classic only |
| 2 | the D series register read | the answer of that register (`hitec_d_decode()`); D series only |
| 3 | the byte wanted | one byte and nothing after it (`hitec_listen_decode()`) |

The decoders' result codes are `hitec_rx_t`: 0 OK, 1 ARG, 2 ABORTED,
3 OVERFLOW, 4 SILENT, 5 EARLY, 6 LATE, 7 NAK, 8 LENGTH, 9 FRAMING, 10 GAP,
11 ACK, 12 HEADER, 13 ADDRESS, 14 SUM.

## POWER

POWER needs a PD mini that answers ([SUPPLY](Link.md)). It:

1. asks the supply off;
2. waits until it reads off, 3000ms at most, else result 3 (`SUPPLY`);
3. keeps it off for the staged 0 to 10,000ms;
4. asks it on at the staged voltage and current limit;
5. starts the staged LISTEN in the same pass.

The voltage is 4800 to 6000mV until a session knows the servo model. The
current limit is 50 to 3000mA. The voltage plus 700mV has to be under the
PD mini's input when the input is known. A set point outside these is
refused with `SUPPLY_RANGE`. A supply that refuses the ON gives result 3; a
LISTEN the driver refuses gives result 4 (`START`) with the supply on. The
SUPPLY page is held throughout and after it, until CLOSE
([The supply while a port is held](#the-supply-while-a-port-is-held)).

An OFF of the SUPPLY page waits for a byte that may be on the wire, 12ms
at most: a SEND's lead and bytes, and the whole of a LISTEN or a POWER's
LISTEN that answers, since the answer goes out when the first byte has
come.

## STOP, a switch to SAFE, heartbeat loss and link loss

A stop is the heartbeat lost, the link silent, or the bank switched to SAFE.
On a stop:

- a running transaction is aborted: the pad is released at once and the
  capture holds the bytes so far, marked aborted (result 6, `CUT`);
- a POWER in its off time ends there, and the supply stays off; a POWER
  whose listen runs is cut as a transaction, and the supply is as the
  SUPPLY page has it: off after a lost heartbeat or a silent link, on after
  a switch to SAFE alone;
- the port stays held with the pin a released input, as a KST port stays in
  PROGRAMMING, and so does the hold of the SUPPLY page;
- a port still in STOPPING is free again;
- bit 3 of `STATE` (`STOPPED`) is set.

While `STOPPED` is set, SEND, LISTEN, LEVEL and POWER are refused with
`STOPPED`. CLEAR clears it while the heartbeat is trusted and the link is
not silent; CLOSE and a new OPEN clear it too. While the heartbeat is not
trusted or the link is silent, every pass counts as a stop, and every
command but ABORT and CLOSE is refused with `UNSAFE`. The bank does not
switch to ARMED while the page holds a port, and OPEN is refused while it
is ARMED.

## Registers

A read gives these registers:

| Register | Name | Reads |
| ---: | --- | --- |
| 0 | `STATE` | bits 0 to 2 the state: 0 FREE, 1 STOPPING, 2 OPEN, 3 BUSY, 4 POWER_OFF. Bit 3 `STOPPED`. Bit 4 a PD mini answers. Bit 5 the page holds a capture. Bit 6 the last transaction put a byte on the wire. Bit 7 `RX_MODE` is PRECHARGE (4.24). Bits 12 to 15 the port |
| 1 | `SEQ` | the `SEQ` of the last command taken; 0 at start |
| 2 | `OP` | bits 12 to 15 the last operation started, bits 0 to 11 the transactions started since OPEN, held at 4095 |
| 3 | `RESULT` | low byte the result of that operation: 0 none, 1 running, 2 done, 3 `SUPPLY`, 4 `START`, 5 `NO_LINE`, 6 `CUT`. High byte why the last command taken started nothing, 0 when it started |
| 4 | `CAP` | bits 0 to 4 the bytes held. Bit 8 the receive ended at its count, bit 9 the answer went out, bit 10 overflow, bit 11 aborted. Bits 12 and 13 the decoder |
| 5 | `DEC` | low byte the decoder's result, high byte the D series answer's ID |
| 6 | `VALUE` | the decoded value |
| 7 | `VIEW` | what registers 16 to 31 show |
| 8 to 15 | `BYTES` | byte 2 k in the low byte of register 8 + k, 2 k + 1 in the high byte; 0 past the bytes held |
| 16 to 31 | `DATA` | `VIEW` 0: the start bits of bytes 0 to 7, ns, 32 bits each, low half first. `VIEW` 1: bytes 8 to 15. `VIEW` 2: the run, below |

`VIEW` 2:

| Register | Reads |
| ---: | --- |
| 16, 17 | the decoder's delay, ns |
| 18, 19 | from the lead's first edge to the release of the line, ns: the lead as the PIO program ran it and every bit time sent; a D series read 559,886 |
| 20, 21 | from the start to the capture, us |
| 22, 23 | the longest the transaction could take, us |
| 24 | the bit time of the open line, ns; 0 while none is open |
| 25 | the line: bit 0 the family (0 classic, 1 D series), bit 4 the drive (0 open drain, 1 push-pull), bit 8 it receives by PRECHARGE (4.24) |
| 26 | the port's pin; 0xFFFF while the driver holds none |
| 27, 28 | the window the capture asked, us |
| 29 | bit k: byte k's stop bit was sampled at the start level |

A write sees other registers:

| Register | Name | Takes |
| ---: | --- | --- |
| 0 | `CMD` | bits 0 to 3 the operation: 1 OPEN, 2 CLOSE, 3 SEND, 4 LISTEN, 5 LEVEL, 6 ABORT, 7 POWER, 8 CLEAR. Bits 8 to 11 the port. Every other bit 0 |
| 1 | `SEQ` | the page's `SEQ` plus 1, modulo 65536 |
| 2 | `KEY` | 0x48A5 for an operation that puts a byte on the wire, else 0 |
| 3 | `CRC` | the check over registers 4 to 21, for every operation but ABORT, CLOSE and CLEAR |
| 4 | `LINE` | OPEN: bit 0 the family, bit 4 the drive. Classic is open drain only |
| 5 | `NTX` | SEND: low byte the bytes to send, 1 to 8; high byte the idle bit times between two, 0 to 6 |
| 6 to 9 | `TX` | byte 2 k in the low byte of register 6 + k, 2 k + 1 in the high byte |
| 10, 11 | `LEAD` | SEND: the lead, us, low half first |
| 12, 13 | `WINDOW` | SEND, LISTEN, POWER: the window, us |
| 14 | `RX` | low byte the bytes to receive, 0 to 16, 0 only with no window; bit 8 answer the first byte (LISTEN, POWER) |
| 15 | `ANSWER` | low byte the answer; bit 8 the level of a LEVEL |
| 16, 17 | `LEVEL` | LEVEL: how long, us |
| 18 | `DECODE` | bits 0 and 1 the decoder, bits 8 to 15 its argument; bits 2 to 7 0 |
| 19 | `SUPPLY_MV` | POWER: 4800 to 6000mV |
| 20 | `SUPPLY_MA` | POWER: 50 to 3000mA |
| 21 | `OFF_MS` | POWER: 0 to 10,000ms |
| 22 | `VIEW` | 0 to 2. In force when written, not covered by the check |
| 23 | `RX_MODE` | 0 PULL-UP, 1 PRECHARGE: how a D series line receives, from the next OPEN on (4.24). Not covered by the check; 0 after a restart |

The check is the link's CRC-16 (cyclic redundancy check), seeded 0xFFFF,
over each register's low byte and then its high byte (`link_kst_crc()`), as
on the KST page.

Registers 0 to 3 are the command: one frame, written as exactly those 4
registers and taken whole or not at all. Registers 4 to 23 are written in
any windows before the command. A write that reaches register 24 or higher
is refused with READ_ONLY, a `VIEW` above 2 or an `RX_MODE` above 1 with
BAD_VALUE, and the window changes nothing.

A frame with a `SEQ` other than the page's plus 1, an operation 0 or above
8, a bit set outside the operation and the port, or a `KEY` that is neither
0 nor 0x48A5 is refused with BAD_VALUE and changes nothing. The frame just
taken, sent again, is acknowledged and does nothing. Every other frame is
acknowledged; what became of it is in `RESULT`.

## Refusals

`RESULT`'s high byte. A port the build does not have is refused with
`PORT` before every other check, ABORT excepted. Then, in this order for a
transaction:

| Code | Name | Why |
| ---: | --- | --- |
| 7 | `NO_KEY` | a SEND without the key, before every other check; a LISTEN or POWER with an answer, after the range check |
| 6 | `NOT_OPEN` | the port is free |
| 1 | `BUSY` | a transaction or a POWER runs, the port is in STOPPING, or OPEN on a held port |
| 2 | `PORT` | no such programming port, or not the one held |
| 9 | `STOPPED` | a stop since; CLEAR first |
| 5 | `UNSAFE` | heartbeat not trusted or link silent |
| 8 | `STAGED` | registers 4 to 21 do not have the check `CRC` names |
| 11 | `RANGE` | the staged transaction is not one the line runs, the decoder does not fit the family, or a reserved bit is set; on a PRECHARGE line also a LISTEN or POWER that answers, or a receive window above 30,000us |
| 13 | `NO_SUPPLY` | POWER without a PD mini that answers |
| 12 | `SUPPLY_RANGE` | POWER: a set point or the off time out of range |
| 3 | `HELD` | OPEN: the KST page holds the port |
| 4 | `ARMED` | OPEN: the bank is ARMED |
| 10 | `NO_PIN` | OPEN: the port has no pin, or its FUNC is DShot (since 4.28): an ESC is on that lead |

The KST page refuses ENTER and a REBOOT from PWM with its `BUSY` (1) while
this page holds the port.

## A transaction by hand

A classic read of EEPROM address 0x12 on port 0, written raw, as the bench
console ([The bench console](BenchConsole.md)) takes it. The command is
0x61 0x12 0x00 and its sum byte 0x8D; the classic decoder (1) is named for
command 0x61. The page is at `SEQ` 0.

```text
w 0x34 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
w 0x34 0 0x0001 1 0 0x8135
r 0x34 0 4
w 0x34 4 0 0x0104 0x1261 0x8D00 0 0 0 0 0x05DC 0 2 0 0 0 0x6101 0 0 0
w 0x34 0 0x0003 2 0x48A5 0xE56A
r 0x34 0 16
```

The first two lines open the line, classic and open drain; the third reads
`STATE` 2 (OPEN) after 50ms. The fourth stages 4 bytes with 1 idle bit
between two, a window of 1500us for 2 bytes and the decoder; the fifth
sends it with the key. 0x8135 and 0xE56A are the checks of the two staged
blocks. The console's `hitec open 0 c` and `hitec c 0 0x61 0x12 0` do the
same and compute `SEQ` and the checks.

## The panel's link half

`shared/bench/hitec_link.c` turns one step of a session into frames of this
page, and the page's answer back into the step's result
(`hitec_ses_result_t`). It takes one request at a time: OPEN, a step (SEND,
LISTEN or LEVEL, or POWER at the caller's set points), CLEAR, or CLOSE of
any port the page holds, a port another writer opened included. The caller
makes each exchange; one pass makes 4 at most.

A request goes out in this order:

1. OPEN and a step: registers 4 to 22 in 5 frames of 4, 4, 4, 4 and 3
   registers. `VIEW` (22) is always 2.
2. The command, registers 0 to 3: `SEQ` one above the page's, the check of
   registers 4 to 21, and the key with every SEND and with a LISTEN or POWER
   that answers. A command without an answer goes again unchanged, 3 tries;
   the page takes it once.
3. Registers 0 to 15: the command started the operation, or `RESULT`'s
   high byte says why not. Then every 5ms until the port is out of
   STOPPING, BUSY and POWER_OFF. The last read holds the capture.
4. Registers 0 to 31, once. Registers 16 to 31 count as the run when the
   same read shows `VIEW` 2 and the `SEQ`, `OP` and `CAP` of step 3's
   last read: `VIEW` is written without a command, so another writer can
   change it unseen by `SEQ`. Otherwise the outcome has no run, and the
   pin and line read before stay.

| The page | The step's result |
| --- | --- |
| `RESULT` 2, `DONE` | RAN, with `DEC`'s low byte and `VALUE` |
| `RESULT` 6, `CUT` | CUT |
| a refusal, a refused frame, `SUPPLY`, `START`, no answer, a restart | FAILED |

| Case | Exchanges | Passes of 5ms |
| --- | ---: | ---: |
| a classic read | 9 | 3 |
| a D series read | 10 | 4 |
| a full D series read, 21 registers, 20ms apart | - | 840ms in all |

The figures come from the host model (`test_hitec_link`,
`test_hitec_link_run`); on the bench they are not measured.

When the link does not answer:

- 3 exchanges in a row without an answer give the request up.
- A command whose 3 tries go unanswered is not sent again. The next read of
  registers 0 to 15 decides: a page at the command's `SEQ` in a state the
  command leaves took it, a page at the `SEQ` before it did not. The state
  that fits: a refusal; for CLOSE the port free or another port held; for
  CLEAR the port held with `STOPPED` clear; for the others the operation
  in `OP` on the request's port, with `OP`'s count, the state or the
  result changed against the read before. The command's `SEQ` in a state
  that does not fit is another writer's command at that `SEQ`. A page at
  `SEQ` 0 with operation NONE and no refusal started again, also when the
  command's `SEQ` is 0, the one after 65535.
- Another `SEQ`, or `OP` back at NONE, is an IOmcu that started again or
  another writer's command. A request on its way ends there, FAILED.
- A request still running 25,000ms after its command was taken is aborted
  from the panel. The page ends every transaction within 10,000ms and a
  POWER within 23,000ms, so this limit is for a page that does not.

ABORT drops a request whose command has not gone. A command sent without an
answer is not sent again; the `SEQ` decides as above. The ABORT command
goes as soon as the page is known; one refused with BAD_VALUE goes again
after a read, with the page's `SEQ`. A request dropped while one of its
exchanges is out holds the next request off until that exchange's answer
is in.

Between requests registers 0 to 15 are read every 100ms while the page
holds a port and every 500ms while it holds none. An IOmcu below 4.23 is
sent nothing. The screen's copy names the state, the port, the flags
(`STOPPED`, a PD mini answering, `PRECHARGE`), the pin and the line, and
whether this module opened the held port since the link came up. A port
held over a link loss, over a restart of the panel or after another
writer's command is not the module's. CLOSE releases it, from any state.
CLEAR is refused with `UNSAFE` while the IOmcu does not trust the
heartbeat. The SUPPLY hold of 4.29 is SUPPLY's FLAGS bit 11, which the
panel's SUPPLY module reads (`shared/bench/supply_link.c`).
