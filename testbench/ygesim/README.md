# ygesim: a YGE ESC on the station Pi

`ygesim.py` plays a YGE ESC (electronic speed controller) on the Pi's UART
(universal asynchronous receiver-transmitter). It speaks OpenYGE as
[docs/OpenYGE.md](../../docs/OpenYGE.md) describes it, so the IOmcu's pin
driver and session can run against it with no ESC on the bench. Python 3
with pyserial 3.5; the frame and CRC (cyclic redundancy check) code is its
own.

| | |
| --- | --- |
| Line | 115200 baud 8N1, half duplex, one wire |
| Address | 1 (`--address`); a request to address 1 or to 0 (broadcast) is answered, every other one is ignored |
| Answers | `TELE_REQ` with `TELE_RESP`, `WRITE_PARAM_REQ` with `WRITE_PARAM_RESP`, both 34B |
| Turnaround | 3ms from the request's last byte as the Pi reads it (`--turnaround-ms`); the Linux serial driver adds its own latency, not measured |
| Parameters | 32, the table of `test/host/oy_sim.h`: count 32, type 5712 (165 HVT), firmware V1.03555, serial 4660 |
| Delivery | one (index, value) pair per reply, the index cycling 0 to count − 1 |
| Writes | stored at once for an index below the count; an index at or above it changes nothing and is answered |
| Telemetry | 25°C, 12.00V, BEC (battery eliminator circuit) voltage = index 2 × 100mV, motor state `DISARMED` |
| Echo | the Pi hears its own bytes; they are taken off the front of what it receives before the decoder sees them |

## Modes

One mode per run, `--mode`:

| Mode | The ESC | What the session reports (docs/OpenYGE.md §10) |
| --- | --- | --- |
| `normal` | answers every request after the turnaround | full read, writes stored |
| `silent` | sends nothing; writes are still stored | `SILENT` after 3 sends of 200ms each |
| `late` | every reply 250ms after its request | each reply past the 200ms read timeout: `SILENT` |
| `crc` | 10% of the replies with a broken CRC, drawn from `--seed` (default 1) | retries; the read completes unless all 3 tries of one request fail, 0.1% a request |
| `seq` | every reply carries the request's sequence number plus 1 (255 gives 0) | every reply dropped: `SILENT` |
| `legacy` | version 2 `TELE_AUTO` frames of 32B every 50ms, no request answered | read-only, no write |
| `count33` | index 0 reads 33, the table has 33 entries | read-only, no write |

`--auto` adds version 3 `TELE_AUTO` frames every 50ms from the start until
the first request it answers. Whether a real ESC sends them is U10 of
docs/OpenYGE.md §8.

A write to an index the write guard never sends (0, 11 to 15, 25) is
stored, logged as `GUARDED` and counted in `guarded`.

## Run

```
python3 testbench/ygesim/ygesim.py --port /dev/serial0 --mode normal
python3 testbench/ygesim/ygesim.py --mode crc --seed 3 --auto
python3 testbench/ygesim/ygesim.py --set 21=40 --set 22=200 --seconds 60
```

`--set INDEX=VALUE` sets a start value, `--seconds` ends the run (0, the
default, runs until Ctrl-C), `--quiet` drops the frame lines. A run ends
with one line of counters and the table, one index a line:

```
0.512 rx TELE_REQ seq=7 dev=0x81 idx=0 val=0
0.515 tx TELE_RESP seq=7 pidx=7 pdata=1
1.034 rx WRITE_PARAM_REQ seq=19 dev=0x81 idx=21 val=13
1.034 write idx=21 12->13
1.037 tx WRITE_PARAM_RESP seq=19 pidx=19 pdata=20
requests=40 writes=1 sent=41 auto=0 crc_broken=0 guarded=0 ignored=0 crc_errors_in=0 resyncs=0 echo_missing=0 echo_mismatch=0
00 = 32
...
```

| Counter | Counts |
| --- | --- |
| `requests`, `writes` | master frames taken |
| `sent` | frames sent, auto frames included |
| `crc_broken` | replies sent with a broken CRC (`crc` mode) |
| `ignored` | frames with a valid CRC that are not a request to this ESC |
| `crc_errors_in` | received frames whose CRC failed |
| `resyncs` | incomplete frame starts dropped because a complete valid frame followed them |
| `echo_missing` | echoes that did not come back within 50ms: GPIO 14 and 15 are not joined |
| `echo_mismatch` | echoes that differed from the bytes sent: two senders at once |

`--port` takes a device or a pyserial URL; `loop://` runs the simulator
against its own echo.

## Wiring

For station step H2, wired by hand:

- Pi GPIO 14 (TXD) through 1kΩ to the wire, Pi GPIO 15 (RXD) on the wire:
  the Pi hears its own bytes.
- The wire to the IOmcu's GPIO 13 (port 0, CH11 on the logic analyser).
- Common ground.

With a second 1kΩ in series at GPIO 13, a low from the IOmcu meets the
Pi's idle high through 2 × 1kΩ and the wire sits at 1.65V, between the
Pi's input thresholds. GPIO 13 straight on the wire keeps that low under
0.2V; the 1kΩ at GPIO 14 limits the current to 3.3mA while both drive at
once.

The Pi's UART on GPIO 14/15 needs `enable_uart=1` in
`/boot/firmware/config.txt` and no serial console on it
(`/dev/serial0` free).

## Station step H2

Wiring as above, IOmcu built with `IOMCU_CONSOLE=ON`, the logic analyser
on CH11 at 1.65V.

| Step | Do | Pass |
| --- | --- | --- |
| 1 | `ygesim.py --mode normal` on the Pi; `oyline 13 tx` on the IOmcu console | ygesim logs 3 `TELE_REQ`, `seq=1` to `3`, `dev=0x81`, and answers each; `oyline done` reports `rx=102` (3 replies of 34B); `echo_missing=0` |
| 2 | the session against `normal`: full read | the table equals ygesim's summary |
| 3 | a write of 3 fields, then verify | 3 `write` lines in the log, result `OK`, `guarded=0` |
| 4 | the session against `silent`, `late`, `crc`, `seq`, `legacy`, `count33` | each result as in the mode table above |
| 5 | STOP during a write | no `rx` line in ygesim's log after the STOP; CH11 stays high, because the Pi's TX holds the wire idle-high through 1kΩ against the IOmcu's pull-down |
| 6 | after step 5: end ygesim, note `pinctrl get 14`, then `pinctrl set 14 ip pn` to release the Pi's TX | CH11 falls low and stays low: GPIO 13 is an input on its pull-down, not driving. A pad still driving would hold the wire at its last level, high between bytes. Then restore GPIO 14 with the function `pinctrl get 14` showed |

Steps 2 to 6 need the IOmcu page of slice 6.

## Tests

`test/tools/test_ygesim.py` runs the simulator on an in-memory wire that
echoes like the station's: frames, the CRC-16/XMODEM check values, each
mode, writes, the address and the echo filter. One case opens pyserial's
`loop://`.

```
python3 -m pytest test/tools/test_ygesim.py -q
```
