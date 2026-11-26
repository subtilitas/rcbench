# Supvan label printer fixtures

Files the host suites of `protocols/supvan` compare with. Two kinds:
captures from the hardware station, and streams that
`tools/gen_supvan_fixtures.py` writes.

## Captures (station step H0)

Captured on 2026-11-26 on the hardware station: Raspberry Pi 5, BlueZ
5.82, a Supvan T50M Pro at address A4:93:40:30:FA:71 (public), not paired
or bonded, RSSI -54 to -56dBm. Roll 40 x 30mm, gap 3mm, type 1. One label
printed ("H0 FIXTURE" / "2026-11-26"); the label counter went from 294 to
293. ATT MTU (attribute protocol maximum transmission unit): a request of
247 answered 240. Every write to FFE9 (handle 0x0011) was a Write Request.

| File | Bytes | sha256 | Content |
|---|---|---|---|
| `label-bufs.bin` | 12,288 | `804e06bb01e675b9f958edb4f9fd8c9703233b147410e264d7ea5f522e9eeacc` | the 3 print buffers of 4,096 bytes: 84, 84 and 56 columns, density 4, margins 8 and 8 dots |
| `label.lz` | 813 | `18fb972dd2590f29ebb05c6ec79a13021f53c153472d79023316c5ce5958ac44` | the LZMA-alone stream sent: props 0x5D, dictionary 8,192, length 12,288 in the header, full LZMA (Python `lzma`, match finder BT4); it decodes to `label-bufs.bin` |
| `label-frames.bin` | 1,418 | `ca1c9445cf56d2d95674b1b40c55c208cc306a3472392074a63497c85a4e5e32` | every write to FFE9 in order as `<u16 LE length><bytes>`: 29 records, 7 commands of 16 bytes, 8 fragments of 128 bytes, 14 commands of 16 bytes; the CCCD write is not in it |
| `label-dataframes.bin` | 1,024 | `d82564210af6eac061849ba800749705693b0fa6551055ef2b1bab0f64736474` | the 2 data frames of 512 bytes, the 128-byte records of `label-frames.bin` joined |
| `label-cmds.tsv` | 2,379 | `f653583e848f7f9b2b683b93ed07299284f15509d6403f7bd9ecc877d05fd68c` | every command with its joined answer and the capture's Unix time |

The answers in `label-cmds.tsv`:

- RETURN_MAT (0x30): 61 bytes, LEN 57, bytes 4 to 6 `55 03 55`, bytes 8
  and 9 the 16-bit sum of bytes 10 to 60. Before the print 294 labels
  left at bytes 43 to 46, after it 293; bytes 40 and 41 are 40 and 30.
  `test_supvan_frame.c` and `test/host/sv_sim.h` carry both answers.
- INQUIRY_STA (0x11): 26 bytes. Idle: byte 16 0x10. While printing:
  byte 15 0x04 (busy), byte 16 0x50 (printing). The last poll read idle
  1.9s after BUF_FULL. The data transfer took 0.54s.
- CHECK_DEVICE, START_PRINT, NEXT_ZIPPEDBULK and BUF_FULL: 23 bytes.

Not captured: long writes, Write Commands, notifications without the CCCD
write, traffic on FFEA, a second roll size, the battery state.

## Generated

`tools/gen_supvan_fixtures.py` writes these; `--check` fails when one
differs.

| File | Content |
|---|---|
| `lzlit-*.bin` | `tools/lzlit.py` streams of 0, 1, 12,288 bytes, a pattern and 5,000 seeded random bytes |
| `label-lit.lz` | the `tools/lzlit.py` stream of `label-bufs.bin` |
