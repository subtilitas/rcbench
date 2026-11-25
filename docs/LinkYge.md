# Programming for YGE ESCs

<sub>**English** · [Deutsch](LinkYge-de.md)</sub>

This page describes the link pages of programming for YGE ESCs. [The
link](Link.md) covers the wiring, frame format, page map and other pages.
ESC means electronic speed controller; PD, power delivery; GPIO,
general-purpose input/output; and CRC, cyclic redundancy check.

From protocol 4.42, `YGE_PROG` (0x44) and `YGE_DATA` (0x45) read and write the
parameter table of a YGE ESC over its telemetry wire. They hold one session of
[`protocols/openyge`](../protocols/openyge/README.md) on the pin of a
programming port. `shared/outputs/yge_port.c` holds the rules. An IOmcu that
serves them reports `LINK_CAP_YGE` (bit 9). [The OpenYGE
specification](OpenYGE.md) holds the wire, the table and the session timing.

The pages do not expose raw transactions. The IOmcu keeps the read copy and
passes every frame through the guard
([OpenYGE, section 9](OpenYGE.md#9-status-in-rcbench)). The panel selects an
operation and supplies a draft.

YGE is a name or trademark of its owner. It is used here only to say which
ESCs this works with.

## Which pin

| Target | Taken when |
|---|---|
| Programming port 0 to 3 | the IOmcu holds its pin and its FUNC is not DShot, as for KST and HITEC (`prog_port_wire_pin()`) |

Any other port is refused with `NO_PIN`, a port number past the build's ports
with `TARGET`. The port's FUNC does not change. Only this pin moves.

Wiring: the ESC's telemetry signal to the port's signal pin, its ground to
the port's ground. The throttle lead's center wire (BEC) stays off the bench;
the throttle lead is unplugged. Whether a connected throttle lead changes
whether the ESC answers is not known (OpenYGE.md U9).

## The way in and back

| Step | What happens |
|---|---|
| OPEN | Refused while armed or asked to arm (`ARMED`); while KST, HITEC, ESC_WIRE or a firmware update has the bench, or a PD mini wiring is saved and not yet attached (`HELD`, every port); on board 3 while the rail refuses the port's CONNECT (`HELD`, that port); with the heartbeat not trusted or the link silent (`UNSAFE`). From this frame on the bank does not arm, CONTROL refuses a non-zero ARM, SUPPLY and PROG_SUPPLY are held, a change of SUPPLY's wiring (ENABLE to BAUD) is refused with `BUSY`, and KST, HITEC and ESC_WIRE refuse every port. |
| `STOPPING`, 50ms | The port renders nothing. An ABORT, a stop or a lost link here frees the pin with nothing touched. |
| `OPEN` | The port lets the pin go and the line opens parked. No state machine, program space or 2 DMA (direct memory access) channels: `NO_LINE`, the pin goes back, the page is free. |
| CLOSE | Refused with `WRITING` from a write's first frame to the end of its read after. The session ends, the line closes and the pin goes back to its port. A port that does not take the pin back ends `NOT_BACK`: the page stays held in state 8 with its quiet bit set, so ARM stays refused. A CLOSE tries again; an IOmcu restart clears it. |

## Entry

`ENTER` stages the power (`SWITCHED` or `BY HAND`), the voltage, the limit and
the boot wait. It needs the key `LINK_YGE_KEY` (0x59C7), which a panel sends at
the end of the operator's 2s hold and with no other command but WRITE.

| `SWITCHED`, on the PD mini (the default) | Line | Supply |
|---|---|---|
| `POWER_OFF`: 250ms at least, read off within 3000ms | parked | off |
| `POWER_ON`: until the PD mini reads on, 3000ms at most | parked | asked on, at 3000 to 20000mV (8400mV on board 3) and 50 to 3000mA, at least 700mV under the input |
| `RUN`: the boot wait (1000 to 20000ms, 5000ms by default) counted from it reading on, listening up to 3000ms, then requests until one is answered | listening | on |

| `BY HAND`, on the ESC's own battery | Line | Supply |
|---|---|---|
| `HAND_WAIT`, HAND_DONE taken up to 60000ms after ENTER; at 60001ms an error | parked: nothing feeds an ESC with no power | not touched |
| HAND_DONE, then `RUN` as above | listening | not touched |

YGE's manuals allow batteries only as the ESC's supply (OpenYGE.md K16); the
PD mini gives 3A at most. Whether a model starts on the PD mini, and at which
voltage, is not known (OpenYGE.md U11).

A session uses the PD mini to power the ESC on every board, including boards
with PROG_SUPPLY. It never switches PROG_SUPPLY on. From OPEN until the port
is released, a panel request to switch it on is refused with `BAD_VALUE`. An
error, STOP or ABORT switches it off along with the PD mini. CLOSE without one
of those events leaves it as set by the panel. The state becomes `READY` when
the ESC answers a request, or when a legacy ESC's first frame arrives.

In `READY` the session sends a telemetry request 40ms after each reply. A
miss of 3 requests ends it with `SILENT`: an error.

## What each event does

| Event | Line | Supply | After |
|---|---|---|---|
| ARM asked, page free | the port's | as it is | a later OPEN is refused while it stands |
| ARM asked, page held | no change | no change | a non-zero ARM is refused with `NOT_ARMED` from OPEN to the give, and the page's bit in the quiet mask holds `safety_gate_arm_now()` down as well. The give sets ARM and THROTTLE to 0 |
| STOP (a stop edge or the STOP operation), also in a write | parked at once | PD mini and PROG_SUPPLY off | held, `STOPPED` |
| ABORT, also in a write | parked at once | PD mini and PROG_SUPPLY off | held |
| Heartbeat lost or link silent 200ms, also in a write | parked at once | off at once | held, `STOPPED` |
| Error | parked at once | PD mini and PROG_SUPPLY off | held, `FAULT` and `STOPPED` |
| LEAVE | parked | PD mini off | held, `OPEN` |
| IOmcu restart | the chip's reset state | off at boot | free; the port binds as the PROG page has it |
| The supply off, silent, or read off after it read on, in `RUN` or `READY` | parked at once | off | held, `FAULT`, result `SUPPLY` |

A stop cuts a write at once; the panel's OFF on the SUPPLY page is not held
back for a write either, and is an error for the session. A write cut after
its first frame leaves the ESC's table not known: `LINK_YP_F_CUT_WRITE`.
Whether one parameter's write is stored whole when the power goes in it is
not known (OpenYGE.md U5). In `BY HAND` mode the bench cannot cut the ESC's
own supply: it parks the line, and the operator must disconnect the battery.

Parked is an input with its pull-down and its input buffer off. Listening is
an input with its pull-up; the line is driven only while a frame goes out.

**What counts as an error.** Any operation of the session that ends other than
`OY_OK`: `SILENT`, `NO_TABLE`, `CHANGED`, `NOT_TAKEN`, `PARTIAL`; a guard
refusal of a draft (`GUARD`); a supply that fails; a `BY HAND` wait that runs
out. A command the session refuses before any frame (`NO_TABLE` for a write
before a read, `NOT_KNOWN` for a read-only table, a state) is a refusal of that
command and changes nothing.

**Who switches the rest.** The bank is SAFE for as long as the page is held, so
the page's line, the PD mini and PROG_SUPPLY are all the IOmcu has on.
`LINK_YP_F_FAULT` tells the panel, which follows it to the bench's SAFE state.

## Current limitations

- **A link that came back keeps the hold.** Condition: the link went silent for
  over 200ms with no panel restart. The page stays `OPEN` and `STOPPED` with the
  quiet bit set, so the bank cannot arm. The way out is the bench console's
  `yge close`, or an IOmcu or panel restart.
- **A stop in a write.** Condition: a STOP, a stop edge, an error or a link loss
  between a write's first frame and the end of its read after. The table the
  ESC holds is not known until the next read.
- **No panel half.** The panel sends nothing to these pages yet; the bench
  console's `yge` drives them.
- **A firmware update** that starts while the page is held counts as a stop.

## Registers

`YGE_PROG` reads:

| Reg | Name | Value |
|---|---|---|
| 0 | `STATE` | 0 `FREE`, 1 `STOPPING`, 2 `OPEN`, 3 `POWER_OFF`, 4 `POWER_ON`, 5 `HAND_WAIT`, 6 `RUN`, 7 `READY`, 8 `NOT_BACK` |
| 1 | `SEQ` | the last SEQ taken |
| 2 | `OP` | the last operation |
| 3 | `RESULT` | low byte the result, high byte the last command's refusal |
| 4 | `FLAGS` | bit 0 `STOPPED`, 1 `FAULT`, 2 this session switched the supply on, 3 a PD mini answers, 4 writable, 5 legacy headers, 6 taken over, 7 writing, 8 a whole table read, 9 a write cut after its first frame, 10 `BY HAND` |
| 5 | `LINE` | low byte 0 parked, 1 listening; high byte the GPIO, 0xFF for none. A screen shows the active output from this register. |
| 6 | `PORT` | the port held, 0xFF when free |
| 7 | `SES` | low byte the session's last result (`OY_OK` 0, `BUSY` 1, `SILENT` 2, `NO_TABLE` 3, `NOT_KNOWN` 4, `CHANGED` 5, `NOT_TAKEN` 6, `PARTIAL` 7, `GUARD` 8, `ABORTED` 9), high byte the guard's last refusal |
| 8 | `BAD_INDEX` | the index the guard refused |
| 9 | `TYPE` | the read copy's type code, index 11 |
| 10, 11 | `FW_LO`, `FW_HI` | its firmware, indices 12 and 13: 103555 is V1.03555 |
| 12, 13 | `SERIAL_LO`, `SERIAL_HI` | its serial number, indices 14 and 15 |
| 14 | `ENTRIES` | entries of the read copy, 0 before a read |
| 15, 16 | `READS`, `READ_CRC` | complete reads, wrapping at 65535; `oy_guard_table_crc()` of the read copy |
| 17 | `PROGRESS` | low byte the indices of the table being read seen so far, high byte the index being written, 0xFF none |
| 18 | `T_MS` | ms in this state, stops at 65535 |
| 19 | `BLOCK` | the block `YGE_DATA` shows |
| 20 | `SES_STATE` | low byte the session's state, high byte why the read copy is read-only (the guard's refusal code) |
| 21, 22 | `MISSES`, `DROPPED` | requests and writes not answered in time; frames dropped. Both stop at 65535 |

`YGE_PROG` writes:

| Reg | Name | Value |
|---|---|---|
| 0 | `CMD` | 1 OPEN, 2 CLOSE, 3 ABORT, 4 STOP, 5 CLEAR, 6 ENTER, 7 HAND_DONE, 8 READ, 9 WRITE, 10 LEAVE |
| 1 | `SEQ` | `SEQ` + 1 modulo 65536 |
| 2 | `KEY` | 0x59C7 for ENTER and WRITE, else 0 |
| 3 | `CRC` | `link_kst_crc()` of registers 4 to 11, for OPEN, ENTER and WRITE |
| 4 | `PORT` | the programming port, 0 to 3 |
| 5 | `POWER` | 0 `SWITCHED`, 1 `BY HAND` |
| 6, 7 | `MV`, `MA` | the supply's set points |
| 8 | `BOOT_MS` | the boot wait, 1000 to 20000ms |
| 9, 10 | `READS`, `READ_CRC` | the read copy the draft was made from |
| 11 | `DRAFT_CRC` | `oy_guard_table_crc()` of the draft's `ENTRIES` values |
| 12 | `BLOCK` | not staged; 0 to 5 |

Registers 0 to 3 are one frame. A SEQ other than `SEQ` + 1 is `BAD_VALUE`; the
frame last taken, sent again, is acknowledged and does nothing. A command that
is taken is acknowledged, and what became of it is on the page: `RESULT`'s high
byte says why it started nothing, or 0. Refusals: 1 `BUSY`, 2 `TARGET`, 3
`HELD`, 4 `ARMED`, 5 `UNSAFE`, 6 `NOT_OPEN`, 7 `NO_KEY`, 8 `STAGED`, 9
`STOPPED`, 10 `NO_PIN`, 11 `RANGE`, 12 `NO_SUPPLY`, 13 `STALE`, 14 `DRAFT`, 15
`WRITING`, 16 `SESSION`, 17 `SUPPLY_OFF`. Results: 1 running, 2 done, 3
`NO_LINE`, 4 `SUPPLY`, 5 `TIMEOUT`, 6 `CUT`, 7 `SESSION`, 8 `NOT_BACK`.

`YGE_DATA` is 32 registers, one value of the table each, of the block `BLOCK`
names. The first block of each pair holds indices 0 to 31, the second 32 to 63.

| Block | Holds | Access |
|---|---|---|
| 0, 1 | the read copy | read |
| 2, 3 | the draft | read, write; `BUSY` while an operation runs |
| 4, 5 | the table as last received, also a read cut short | read |

WRITE requires the key, the CRC, the draft's CRC, and the `READS` and
`READ_CRC` values of the read on which the draft is based; a mismatch gives
`STALE`. The guard's rules also apply: a draft it refuses is an error. A write
reads the table again first and ends `CHANGED` with nothing sent when it
differs from the read copy; after the writes it reads the whole table again,
which becomes the read copy with a new `READS`, so a draft based on the earlier
table becomes stale.

## Older and newer

| Panel | IOmcu | What happens |
|---|---|---|
| any | 4.41 or older | both pages `BAD_PAGE`; `LINK_CAP_YGE` is not reported |
| 4.41 or older | 4.42 | it never touches the pages; nothing is held |

## What is not measured

The bench console provides access through `yge`
([The bench console](BenchConsole.md#the-yge-page)). The timings come from
[OpenYGE section 10](OpenYGE.md#10-session-timing) or are implementation
choices: 50ms in `STOPPING`, 250ms off before ON, 3000ms to read off and on,
and 60000ms for `BY HAND`. Station steps H1 and H2
([OpenYGE section 8](OpenYGE.md#8-open-measurements)) check the line states and
a whole session against the Pi's simulated ESC; H3 to H5 need a YGE ESC.
