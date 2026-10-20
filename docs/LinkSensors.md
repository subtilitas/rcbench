# The INA3221 map and windows

<sub>**English** · [Deutsch](LinkSensors-de.md)</sub>

This page is a section of [The link](Link.md), which holds the wiring, the frame
format, the page map and the other pages. I2C is Inter-Integrated Circuit.

SENSE_MAP (0x35), SENSE_WIN_A (0x37) and SENSE_WIN_B (0x38) are new in 4.25,
SENSE_LIMIT (0x36) in 4.27.
SENSE_MAP sets up to 4 TI INA3221 current monitors on the sensor bus SENSE
opens, at 0x40 to 0x43: which parts are fitted, a shunt for each of their 12
channels, what each channel measures, and the FAST channel. The two window
pages carry every channel's last 50 ms window. SENSE_LIMIT holds a current
limit for each channel and flags the windows above it.
`shared/outputs/sense_map.c` and `sense_limit.c` hold the rules;
`test_sense_map` and `test_sense_limit` test them on the host.

Channel c is part a's channel ch for c = 3 a + ch - 1, so 0 to 11: channel 0
is CH1 of the part at 0x40, channel 11 is CH3 of the part at 0x43.

## SENSE_MAP

| Register | Name | Meaning |
| ---: | --- | --- |
| 0 | `ENABLE` | bit a: the part at 0x40 + a is fitted. 0 to 15 |
| 1 | `FAST` | the channel read every 1 ms, 0 to 11, or 0xFF for none. SERVO_WIN carries its last 4 windows and SERVO_SENSE times a move on it |
| 2 to 13 | `SHUNT[c]` | channel c's shunt in 0.1 mΩ, 50 to 10000 (5 mΩ to 1 Ω); 0 switches the channel off: it is never read |
| 14 to 25 | `WATCH[c]` | what channel c measures: 0 nothing named, 0x01 to 0x08 output channel 1 to 8, 0x10 to 0x13 programming port 1 to 4, 0x20 the supply. The IOmcu keeps it and acts on nothing in it |
| 26 to 29 | `ID[a]` | read only: the die ID part a answered at its last probe, 0 before one |
| 30 | `FLAGS` | read only: bit a part a online; bit 4 + a part a answered with another identity; bit 8 the map has been written and is kept (`IN_FORCE`); bit 9 it has never been written and is SENSE's one part (`DERIVED`) |
| 31 | reserved | reads 0 |

Registers 0 to 25 are kept in the IOmcu's flash. A write lies within them:
one that reaches register 26 is refused with READ_ONLY. The registers written
are merged into the map and the whole map is judged; a map refused leaves
every register as it was. Refused with BAD_VALUE:

- `ENABLE` above 15;
- `FAST` not 0xFF and past 11, on a part not fitted, or on a channel whose
  shunt is 0;
- a shunt not 0 outside 50 to 10000;
- a `WATCH` value outside the list above;
- a fitted part with all three shunts 0;
- a fitted part on the INA228's address while SENSE enables the INA228;
- a map whose mirrored SENSE set-up (below) SENSE itself refuses: the bus's
  pins while the map opens the bus;
- any change while the bank drives an output.

A part not fitted keeps the values written for it, judged by range alone. A
change of `ENABLE`, `FAST` or a shunt is a new set-up: core 1 sets the bus up
again, and every reading and window of the set-up before goes. A change of
`WATCH` alone is not a new set-up. A write of the map in force changes
nothing and is taken in any bank state, and so is a first write of the
derived map as it reads.

## SENSE and the map

Until SENSE_MAP is first written the map is derived from SENSE's one INA3221:
`ENABLE` bit a for the part at SENSE's register 8 while SENSE's ENABLE bit 1
is set, SENSE's shunt on each channel in SENSE's register 10, `FAST` that
part's CH1 while CH1 is read, and nothing watched. `FLAGS` bit 9 says so. A
bench whose panel never writes SENSE_MAP runs as it did before 4.25.

The first write puts the map in force, and it stays in force: no write
returns it to the derived map. From then on SENSE's ENABLE bit 1 and
registers 8 to 10 mirror the map:

| SENSE | With the map in force |
| --- | --- |
| ENABLE bit 1 | set while a part is fitted |
| 8 `I3221_ADDR` | the mirror part: `FAST`'s part, or the lowest fitted part while `FAST` is 0xFF |
| 9 `I3221_SHUNT_DMOHM` | the mirror part's CH1 shunt, or its first shunt that is not 0 |
| 10 `I3221_CHANNELS` | the mirror part's channels with a shunt |

A SENSE write that would change ENABLE bit 1 or registers 8 to 10 is then
refused with BAD_VALUE; a SENSE write that carries them as they read is taken.
SENSE's flag bits 4 to 6 and its INA3221 die ID are the mirror part's.
SERVO_SENSE's registers 0 to 11 are the mirror part's three channels.
SERVO_WIN is `FAST`'s ring, and a capture's channel 1 on SERVO_SENSE is
`FAST`: an arm while `FAST` is 0xFF is refused with BAD_VALUE.

IDENTITY's capability bit 3 (`SERVO_SENSE`, current per output) is SENSE's
INA3221 bit while the map is derived. With the map in force it is set while a
channel the map reads -- a fitted part, a shunt not 0 -- watches an output
channel or a programming port. It changes only with a SENSE or SENSE_MAP
write taken.

## SENSE_WIN_A and SENSE_WIN_B

Every channel's last complete 50 ms window: channels 0 to 5 on SENSE_WIN_A,
6 to 11 on SENSE_WIN_B. Every write is refused with READ_ONLY. Nothing is
kept.

| Register | Name | Meaning |
| ---: | --- | --- |
| 0 | `WINDOW` | the window's number modulo 65536, SERVO_SENSE's register 12 |
| 1 | `FLAGS` | bit a part a online, on both pages; bit 8 a window has closed under the set-up in force (`HAVE`) |
| 2 + 5 k | `MEAN_MA` | entry k, the page's channel k: mean current in mA, signed |
| 3 + 5 k | `MAX_MA` | highest current in mA, signed |
| 4 + 5 k | `MEAN_MV` | mean bus voltage in mV, at the load side of the shunt |
| 5 + 5 k | `MIN_MV` | lowest bus voltage in mV |
| 6 + 5 k | `E_FLAGS` | bit 0 current samples, bit 1 voltage samples, bit 2 a current sample at the top of the range, bit 3 one at the bottom |

Entry k of SENSE_WIN_B is channel 6 + k. A clipped sample is left out of the
figures, as on SERVO_SENSE. A channel that is off, or of a part not online,
reads 0 throughout.

The rates, per 1 ms tick at 400 kHz: `FAST` every 1 ms; every other channel
with a shunt in turn, one a tick, 1000 / n Hz for n of them (500 Hz each for
CH2 and CH3 of one part, 90.9 Hz each with 11); each bus voltage every 24 ms
(41.7 Hz); each part's Mask/Enable and Configuration read back at 25 Hz. The
longest tick holds 742.5 µs of bus time. 4 parts with 12 channels, the INA228
and the AS5600 output encoder take 60.8 % of the bus; one part with 3
channels and the INA228 take 43.4 %. At most one part is probed a tick.

## SENSE_LIMIT

A current limit for each channel, and the channels whose window passed it.
An alert only: a limit passed stops nothing, switches nothing to SAFE and
drives nothing.

| Register | Name | Meaning |
| ---: | --- | --- |
| 0 to 11 | `LIMIT_MA[c]` | channel c's limit in mA, 1 to 32767; 0 = none |
| 12 | `OVER` | read only: bit c, a window of channel c was over its limit since the last read of this register |
| 13 | `OVER_COUNT` | read only: channel windows over, modulo 65536, since the IOmcu started |
| 14, 15 | reserved | read 0 |

Registers 0 to 11 are kept in the IOmcu's flash. A write lies within them:
one that reaches register 12 is refused with READ_ONLY, a value above 32767
with BAD_VALUE, and a refused write stores none of its registers. A write is
taken in any bank state.

Each complete 50 ms window core 0 takes from core 1 is judged once, channel
by channel. A window is over when its highest current, in mA as SENSE_WIN
shows it, is above the limit: with a limit of 100 mA, 100 mA is not over
and 101 mA is. A window is also over when one of its samples read the top
of the range and that top is above the limit: 1638 mA on 0.1 Ω, 163.8 mV
across the shunt. A negative current is never over. A channel that is off,
or of a part not online, has no samples and is never over. Core 0 takes
core 1's snapshot at least every 20 ms (the 50 Hz BENCH sample), so it sees
each 50 ms window while its main loop runs; a window it misses is not
judged.

`OVER` holds its bits until a read covers register 12: that read returns
them and clears them. A reply lost on the link loses those bits;
`OVER_COUNT` still counts them. A new set-up keeps `OVER` and
`OVER_COUNT`. The console line reads both without clearing.

## Kept in flash

The IOmcu keeps SENSE_MAP's registers 0 to 25, whether the map is in force,
and SENSE_LIMIT's registers 0 to 11 in its store record, version 8. A version 8 record is 308 bytes and fills a slot of
two 256-byte flash pages: 8 slots a 4096-byte sector, 16 in the store. A save
programs both pages in one window. A program cut off in either page reads as
no record: the record's count of 0 bits and its CRC-16 cover both pages.

At boot the limits come back through SENSE_LIMIT's write checks; a kept
value above 32767 leaves every limit 0. The map comes back after SENSE's
set-up, through the write's own
checks, and only when SENSE's set-up was taken. A kept map that is refused
there is not restored, and the map reads derived.

A store written by an earlier build holds records of version 3 to 7 in
one-page slots. This build reads the newest of them while the store holds no
version 8 record: the map then reads derived. The sector that holds that
record is not erased until the first version 8 record is written, so a power
cut before then loses nothing. An IOmcu image older than 4.25 reads no
version 8 record and starts as from an empty store.

## Older peers

| Panel | IOmcu | SENSE_MAP, SENSE_WIN_A, SENSE_WIN_B and SENSE_LIMIT |
| --- | --- | --- |
| 4.25 or later | 4.27 or later | served. This build's panel reads and writes none of the four pages and sets SENSE's one INA3221, so the map stays derived and no limit is set |
| 4.25 or later | 4.25 or 4.26 | the map and window pages are served; the IOmcu answers SENSE_LIMIT with BAD_PAGE and judges no limit |
| 4.25 or later | 4.24 or older | the IOmcu answers each of the four pages with BAD_PAGE |
| 4.24 or older | 4.25 or later, map derived | the panel never reads or writes the four pages. SENSE runs as before. Kept limits are judged and `OVER` holds its bits until something reads it |
| 4.24 or older | 4.25 or later, map in force | SENSE's ENABLE bit 1 and registers 8 to 10 read the mirror part. A SENSE write that changes them is refused with BAD_VALUE, so that panel cannot change the INA3221 set-up. The INA228, the encoder and the pins stay its own |

## On the console

An image built with `IOMCU_CONSOLE=ON` prints the map every 3 s while a part
on SENSE is enabled, after the two `sense` lines
([First run on hardware](FirstRun.md#8-current-monitors-ina228-ina3221)):

```
rcbench-iomcu: sense map fit 0x3 fast 0 flags 0x0103 | mA 120 4 0 310 0 0 0 0 0 0 0 0 | over 0x008 count 3
```

`fit` is `ENABLE`, `fast` is `FAST` (255 for none), `flags` is `FLAGS`, the
12 figures are each channel's `MEAN_MA` from channel 0 on, and `over` and
`count` are SENSE_LIMIT's `OVER` and `OVER_COUNT`.
