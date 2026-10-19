# The programming port for Hitec servos

<sub>**English** · [Deutsch](LinkHitec-de.md)</sub>

This page is a section of [The link](Link.md), which holds the wiring, the frame
format, the page map and the other pages. Pins are named by GPIO (general-purpose
input/output) number. PWM is pulse-width modulation.

Hitec is a name or trademark of its owner. It is used here only to say which
servos the page works with.

The Hitec page (`HITEC`, 0x34) is new in 4.23. It runs raw transactions of
the Hitec servo wire of [`protocols/hitec`](../protocols/hitec/README.md) on
one port of [PROG](Link.md#the-programming-ports): classic (HS-5xxx,
HS-7xxx, 19,200 bit/s) or D series (115,200 bit/s). The page holds no Hitec
session. The writer stages one transaction, the page runs it, and the
capture and its decoding stay on the page until the next one. Which bytes a
servo takes, and in which order, is the writer's business. A session on the
panel is not built. `shared/outputs/hitec_port.c` holds the rules.

A port is free, held by this page or held by the KST page
([The programming port for KST and Chaservo servos](LinkKST.md)), never two
of them. Nothing is kept in flash: after a restart of the IOmcu the port is
free.

## The way in and out

OPEN takes the port:

1. STOPPING, 50 ms: the port renders no pulse, so a pulse that runs ends.
2. The PWM generator gives up the pin. The wire driver opens the line on it
   as a released input: classic with the pad's pull-down, the D series with
   its pull-up and the input inverted. The internal pulls are 50 to 80 kOhm;
   the D series needs a pull-up on the board.
3. OPEN. A driver that gets no PIO (programmable input/output) state machine,
   no 29 words of program space or no 2 DMA (direct memory access) channels
   leaves the port free with result 5 (`NO_LINE`).

In OPEN a command starts one transaction (BUSY). The port is in OPEN again
when the driver hands back the capture. The driver ends a transaction itself
1 ms after the longest it can take; a transaction that still runs after
10,000 ms is aborted by the page as well.

CLOSE gives the port back from any state but free: a running transaction is
cut, the line closed and the pin the generator's. The supply is left as it
is. ABORT cuts what runs and keeps the port.

## Transactions

| Operation | What goes on the wire | Key |
| --- | --- | --- |
| SEND | the lead at the idle level (D series: driven low), 1 to 8 bytes with 0 to 6 idle bit times between two, then the receiver for the window | always |
| LISTEN | the receiver for the window; with an answer, one byte at the end of the stop bit of the first byte received | with an answer |
| LEVEL | the line held at a level for 1 us to 4 s, then released. Open drain drives low only | never |
| POWER | the supply cycle below, then a LISTEN from the ON | with an answer |

Leads, windows and levels run to 4,000,000 us. A capture holds up to 16
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
2. waits until it reads off, 3000 ms at most, else result 3 (`SUPPLY`);
3. keeps it off for the staged 0 to 10,000 ms;
4. asks it on at the staged voltage and current limit;
5. starts the staged LISTEN in the same pass.

The voltage is 4800 to 6000 mV until a session knows the servo model. The
current limit is 50 to 3000 mA. The voltage plus 700 mV has to be under the
PD mini's input when the input is known. A set point outside these is
refused with `SUPPLY_RANGE`. A supply that refuses the ON gives result 3; a
LISTEN the driver refuses gives result 4 (`START`) with the supply on. From
the OFF to the end of the LISTEN the SUPPLY page is held (flag bit 11): the
panel can switch the supply off and cannot switch it on or move a set
point.

## STOP, a switch to SAFE, heartbeat loss and link loss

A stop is the heartbeat lost, the link silent, or the bank switched to SAFE.
On a stop:

- a running transaction is aborted: the pad is released at once and the
  capture holds the bytes so far, marked aborted (result 6, `CUT`);
- a POWER ends where it is, with the supply off;
- the port stays held with the pin a released input, as a KST port stays in
  PROGRAMMING;
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
| 0 | `STATE` | bits 0 to 2 the state: 0 FREE, 1 STOPPING, 2 OPEN, 3 BUSY, 4 POWER_OFF. Bit 3 `STOPPED`. Bit 4 a PD mini answers. Bit 5 the page holds a capture. Bit 6 the last transaction put a byte on the wire. Bits 12 to 15 the port |
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
| 18, 19 | the lead as the PIO program ran it, ns |
| 20, 21 | from the start to the capture, us |
| 22, 23 | the longest the transaction could take, us |
| 24 | the bit time of the open line, ns; 0 while none is open |
| 25 | the line: bit 0 the family (0 classic, 1 D series), bit 4 the drive (0 open drain, 1 push-pull) |
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
| 19 | `SUPPLY_MV` | POWER: 4800 to 6000 mV |
| 20 | `SUPPLY_MA` | POWER: 50 to 3000 mA |
| 21 | `OFF_MS` | POWER: 0 to 10,000 ms |
| 22 | `VIEW` | 0 to 2. In force when written, not covered by the check |

The check is the link's CRC-16 (cyclic redundancy check), seeded 0xFFFF,
over each register's low byte and then its high byte (`link_kst_crc()`), as
on the KST page.

Registers 0 to 3 are the command: one frame, written as exactly those 4
registers and taken whole or not at all. Registers 4 to 22 are written in
any windows before the command. A write that reaches register 23 or higher
is refused with READ_ONLY, a `VIEW` above 2 with BAD_VALUE.

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
| 11 | `RANGE` | the staged transaction is not one the line runs, the decoder does not fit the family, or a reserved bit is set |
| 13 | `NO_SUPPLY` | POWER without a PD mini that answers |
| 12 | `SUPPLY_RANGE` | POWER: a set point or the off time out of range |
| 3 | `HELD` | OPEN: the KST page holds the port |
| 4 | `ARMED` | OPEN: the bank is ARMED |
| 10 | `NO_PIN` | OPEN: the port has no pin |

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
`STATE` 2 (OPEN) after 50 ms. The fourth stages 4 bytes with 1 idle bit
between two, a window of 1500 us for 2 bytes and the decoder; the fifth
sends it with the key. 0x8135 and 0xE56A are the checks of the two staged
blocks. The console's `hitec open 0 c` and `hitec c 0 0x61 0x12 0` do the
same and compute `SEQ` and the checks.
