# The ESC settings pages

<sub>**English** · [Deutsch](LinkEsc-de.md)</sub>

This page is a section of [The link](Link.md), which holds the wiring, the frame
format, the page map and the other pages. ESC is electronic speed controller, PD
power delivery, GPIO general-purpose input/output, CRC cyclic redundancy check.

`ESC_WIRE` (0x3A) and `ESC_DATA` (0x3B), new in 4.34, read and write an ESC's
settings over its signal wire. They hold one session of
[`protocols/escwire`](../protocols/escwire/README.md) on the pin of a throttle slot
or a DShot programming port. `shared/outputs/esc_port.c` holds the rules. An IOmcu
that serves them reports `LINK_CAP_PROGRAM` (bit 8).

There is no raw transaction. The IOmcu holds the read copy and judges every frame
([the guard](../protocols/escwire/README.md)). A panel names an operation and hands
over a draft.

Nothing here has been to an ESC. On the station with no ESC, see
[What is not measured](#what-is-not-measured).

## Which pin

| Target | Taken when |
|---|---|
| Output slot 0 to 7 | its driver is set, it is bound, and its first channel has the throttle role (DShot, bidirectional DShot, PPM or servo PWM) |
| Programming port 0 to 3 | its FUNC is DShot and the IOmcu holds its pin |

Any other slot or port is refused with `NO_PIN`. Only this pin moves: other throttle
pins stay bound and at their idle.

## The way in and back

| Step | What happens |
|---|---|
| OPEN | Refused while armed or asked to arm, while KST, HITEC or a firmware update has the bench, or with the heartbeat not trusted or the link silent. From this frame on the bank does not arm, SUPPLY is held, PROG, OUTPUTS, CHAN_CFG and BIND are refused with `BUSY`, and KST and HITEC refuse every port. |
| `STOPPING`, 50ms | The output renders nothing. An ABORT, a stop or a lost link here frees the pin with nothing touched. |
| `OPEN` | The output lets the pin go, the pin is reserved, and the line opens parked. No state machine, program space or 2 DMA (direct memory access) channels: `NO_LINE`, the pin goes back, the page is free. |
| CLOSE | Refused with `WRITING` while a write runs. The line closes and the pin goes back as its output had it: the same slots bound as at the take, the same PROG registers. A slot or port was not written, so its rate, driver and role are what they were. |

## Entry

`ENTER` stages the power (`SWITCHED` or `BY HAND`), the voltage, the limit, the hold
and the time off.

| `SWITCHED`, on the PD mini | Line | Supply |
|---|---|---|
| `POWER_OFF`: 250ms at least, read off within 3000ms, then the time off (0 to 10000ms) | parked | off |
| `LEAD`, 50ms | driven high | off |
| `RUN`: the hold (500 to 120000ms), then first contact or `info` | driven high, then the wire | on, at 3000 to 20000mV and 50 to 3000mA, at least 700mV under the input |

| `BY HAND`, on the ESC's own supply | Line | Supply |
|---|---|---|
| `HAND_WAIT`, up to 60000ms | driven high, into an ESC with no power if it is connected; the current is not measured | not touched |
| DONE, then `RUN` as above | | not touched |

An ESC session uses the PD mini on every board, also where PROG_SUPPLY exists, and
never PROG_SUPPLY. `READY` follows an ESC that answered.

## What each event does

| Event | Line | Supply | After |
|---|---|---|---|
| ARM asked, page free | the output's | as it is | a later OPEN is refused while it stands |
| ARM asked, page held | no change | no change | the bank stays SAFE: the page's bit in the quiet mask holds `safety_gate_arm_now()` down; a slot's is bit 4 |
| STOP (a stop edge or the STOP operation), no write | parked at once | off | held, `STOPPED` |
| STOP, in a write | as the write needs, to its read-back, then parked | the panel's OFF waits, 10000ms at most, then off | held, `STOPPED`; the result says what the write did |
| ABORT | parked at once, also in a write | off | held |
| Heartbeat lost or link silent 200ms | parked at once, also in a write | off at once | held, `STOPPED` |
| Error | parked at once | off | held, `FAULT` and `STOPPED` |
| IOmcu restart | the chip's reset state | off at boot | free; the slot binds from flash |
| The supply off, silent, or read off after it read on, in `RUN` or `READY` | parked at once | off | held, `FAULT`, result `SUPPLY` |

A write that a lost link cuts between its erase and its program leaves the page
erased. See [the session's table](../protocols/escwire/README.md).

Parked is an input with its pull-down and its buffer off. No state above sends a
throttle signal: while the page holds the pin the output has released it, an apply
leaves that slot unbound (`esc_port_may_bind()`), and the module has no function
that makes a pulse pattern.

**What counts as an error.** Any operation of the session that ends other than
`EW_SES_OK`, among them `EW_SES_ERR_SILENT`, `NOT_KNOWN`, `READ`, `CHANGED`,
`CLIPPED`, `TORN` and `LOST`; a guard refusal of a draft; a supply that fails; a
`BY HAND` wait that runs out. A command the session refuses before any frame
(`EW_SES_ERR_NO_READ`, `MUST_READ`, `DIRTY`, `ERASED`, `BACKUP`, `NOT_WRITABLE`,
`STATE`) is a refusal of that command and changes nothing.

**After `TORN` and `LOST`.** The session itself keeps an ESC in its mode after these
two, for a repair in the same session. The page does not: it aborts the session,
which parks the line, and switches the supply off, so the ESC loses its power. A
repair is CLEAR, ENTER and READ. What a power cycle does to a half-written page is
in the specification's terms:
a family S or Bluejay ESC rewrites defaults and its 48 tag bytes read 0xFF, which
makes it read-only here; an AM32 with erased settings stays in its bootloader, and
only RESTORE writes it. Not measured on an ESC. On `BY HAND` the bench cannot cut the ESC's own
supply: its line is parked and a person disconnects it.

**Who switches the rest.** The bank is SAFE for as long as the page is held, so the
page's line and the supply are all the IOmcu has on. `LINK_EW_F_FAULT` tells the
panel, which follows it to the bench's SAFE state.

## Current limitations

- **A bidirectional DShot pin rests high on SAFE.** Condition: a slot or port bound
  as bidirectional DShot, with the bank SAFE and the ESC powered. The pin rests on
  its pull-up (`out_dshot_stop()`), and a line high after signal loss is the entry
  condition of the family S, Bluejay and AM32 bootloaders. Such an ESC may sit in its
  bootloader after a SAFE, with or without this page. What the next ARM does then is
  not measured. Plain DShot and PPM rest low. The page does not change this.
- **A line with a load.** The pull-up is the pad's, 50 to 80kOhm. A line a load holds
  down, such as a servo's signal wire, stays low on it, and a first contact fails.
  Whether it raises an open-drain answer at 52us a bit is not measured.
- **A firmware update** that starts while the page is held counts as a stop.

## Registers

`ESC_WIRE` reads:

| Reg | Name | Value |
|---|---|---|
| 0 | `STATE` | 0 `FREE`, 1 `STOPPING`, 2 `OPEN`, 3 `POWER_OFF`, 4 `LEAD`, 5 `HAND_WAIT`, 6 `RUN`, 7 `READY` |
| 1 | `SEQ` | the last SEQ taken |
| 2 | `OP` | the last operation |
| 3 | `RESULT` | low byte the result, high byte the last command's refusal |
| 4 | `FLAGS` | bit 0 `STOPPED`, 1 `FAULT`, 2 this session switched the supply on, 3 a PD mini answers, 4 writable, 5 AM32 settings erased, 6 AM32 built for CAN (Controller Area Network) boards, 7 in the bootloader, 8 SUPPLY holds the panel's OFF back, 9 a page half written, 10 a whole page read, 11 a text `save` not proven |
| 5 | `LINE` | low byte 0 parked, 1 driven high, 2 pull-up, 3 driven low, 4 a transaction; high byte the GPIO, 0xFF for none. A screen shows the active output from this register. |
| 6 | `TARGET` | as staged; 0 when free |
| 7 | `SES` | low byte the session's last result, high byte the guard's last refusal |
| 8 | `ID` | low byte the firmware, high byte the layout or revision |
| 9 | `WHY` | low byte why it is not written or refused, high byte the processor |
| 10 | `SIGNATURE` | an EFM8's |
| 11 | `LEN` | bytes of the page: 112, 192, 255 or 92 |
| 12, 13 | `BAD_OFF`, `CLIPPED` | the byte the guard refused; what a clipping `set` stored |
| 14 | `TRIES` | low byte write procedures started, high byte the last answer judged |
| 15, 16 | `READS`, `READ_CRC` | reads this session; `link_crc()` of the read copy's `LEN` bytes |
| 17 | `T_MS` | ms in this state, stops at 65535 |
| 18, 19 | `CONTACT`, `PROTOCOL` | AM32: pin code and flash code; bootloader protocol |
| 20 | `BLOCK` | the block `ESC_DATA` shows |

`ESC_WIRE` writes:

| Reg | Name | Value |
|---|---|---|
| 0 | `CMD` | 1 OPEN, 2 CLOSE, 3 ABORT, 4 STOP, 5 CLEAR, 6 ENTER, 7 DONE, 8 READ, 9 WRITE, 10 RESTORE, 11 LEAVE |
| 1 | `SEQ` | `SEQ` + 1 modulo 65536 |
| 2 | `KEY` | 0x45C3 for WRITE and RESTORE, else 0 |
| 3 | `CRC` | `link_kst_crc()` of registers 4 to 13, for OPEN, ENTER, WRITE and RESTORE |
| 4 | `TARGET` | a slot, or 0x0100 and a port |
| 5 | `POWER` | 0 `SWITCHED`, 1 `BY HAND` |
| 6, 7 | `MV`, `MA` | the supply's set points |
| 8, 9 | `HOLD_LO`, `HOLD_HI` | the hold, ms |
| 10 | `OFF_MS` | the time off, ms |
| 11, 12 | `READS`, `READ_CRC` | the read copy the draft was made from |
| 13 | `DRAFT_CRC` | `link_crc()` of the draft's `LEN` bytes |
| 14 | `BLOCK` | not staged; 0 to 19 |

Registers 0 to 3 are one frame. A SEQ other than `SEQ` + 1 is `BAD_VALUE`; the frame
just taken, sent again, is acknowledged and does nothing. A command that is taken is
acknowledged, and what became of it is on the page: `RESULT`'s high byte says why it
started nothing, or 0. Refusals: 1 `BUSY`, 2 `TARGET`, 3 `HELD`, 4 `ARMED`, 5
`UNSAFE`, 6 `NOT_OPEN`, 7 `NO_KEY`, 8 `STAGED`, 9 `STOPPED`, 10 `NO_PIN`, 11 `RANGE`,
12 `NO_SUPPLY`, 13 `STALE`, 14 `DRAFT`, 15 `WRITING`, 16 `SESSION`. Results: 1
running, 2 done, 3 `NO_LINE`, 4 `SUPPLY`, 5 `TIMEOUT`, 6 `CUT`, 7 `SESSION`.

`ESC_DATA` is 32 registers, 64B, low byte first, of the block `BLOCK` names:

| Block | Holds | Access |
|---|---|---|
| 0 to 3 | the page as read | read |
| 4 to 7 | the draft | read, write; `BUSY` while an operation runs |
| 8 to 11 | the page as last read, also after a failed write | read |
| 12 | the name or the text target, ends in 0 | read |
| 13, 14 | `info`, 106B: a byte that says the revision read, the revision, 16 bits of sub revision, the target (30B), then 12B each of temperature, second temperature, voltage, current, charge and eRPM, each ending in 0 | read |
| 15 to 18 | `music`, ends in 0 | read |
| 19 | RESTORE's identity: pin code, flash code, protocol, 0, the name ending in 0 | read, write |

An ESCape32 page is its 46 values as 16-bit integers in map order.

WRITE needs the key, the CRC, the draft's CRC and the `READS` and `READ_CRC` of the
read it was made from (else `STALE`), and the session's own rules. RESTORE needs the
same but `READS` and `READ_CRC`.

## Older and newer

| Panel | IOmcu | What happens |
|---|---|---|
| any | 4.33 or older | both pages `BAD_PAGE`; `LINK_CAP_PROGRAM` is not reported |
| 4.33 or older | 4.34 | it never touches the pages; nothing is held |

The panel does not use the pages yet: no screen drives them in this image.

## What is not measured

No frame of this page has been to an ESC. On the bench console, `esc` drives it
([The bench console](BenchConsole.md#the-esc-settings-page)). Its times come from the
specification or are this module's choice ([`protocols/escwire`](../protocols/escwire/README.md)):
the entry hold, the time off, 50ms before ON and 60000ms `BY HAND` are chosen. The
current a line driven high feeds into an unpowered ESC is not measured, nor the power
stage's state in its bootloader.

Written from a behavioural specification, without access to the firmware sources.
