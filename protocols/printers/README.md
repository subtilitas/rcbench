# Label printer files

A printer file describes one Bluetooth LE (BLE) label printer to the engine
in [protocols/prnengine](../prnengine/README.md): its GATT (Generic
Attribute Profile) layout, head, image stream, command templates, answers
and job steps. A file holds data only. Nothing in it is executed, looked
up as a symbol or used as a path. CI reads every file here with
`tools/printer_files.py --check`.

| File | Printer | Version | Licence | Sources |
|---|---|---|---|---|
| `supvan-t50m-pro.prn` | Supvan T50M Pro | 1 | MIT | [heeen/supvan-cups](https://github.com/heeen/supvan-cups) (MIT); the station's capture H0 (`test/host/fixtures/supvan/README.md`) |

A file of record carries `licence MIT`, the line
`# SPDX-License-Identifier: MIT`, at least one `source`, and its id as its
name. Supvan and Katasymbol are names of their owners, used only to say
which printer a file describes.

## Lines

- Name `<id>.prn`. At most 8,192B, 256 lines, 120B a line. Bytes 0x20 to
  0x7E, TAB, LF, CR before LF.
- The first line that is not blank or a comment is
  `rcbench printer, format 1`; the last is `end`.
- `#` starts a comment. A line is a key and its values, split by spaces or
  tabs. A line that starts with two spaces or a tab belongs to the `step`
  above it.
- Numbers: decimal or `0x` hexadecimal, at most 10 characters. Bytes and
  keys of answers: two hexadecimal digits (`7E`).
- A key that another key depends on stands above it: `head_dots` above
  `line`, `label align <dot>` and `roll`; `mtu_min` above `write data`;
  `page size` and `line` above `page lines`, `page lines` above
  `page head`; `chunk size` and `chunk pad` above `chunk frame`; `line`
  above `rows`; `rx join` above `rx check` and `rx key`; `rx key` above
  `answer`; `answer` above `field`; `field` above `error` and the steps;
  `tmpl` above its use; the steps above `job`; `job print` above `abort`.
  Only `relink` names a step further down.

## Keys

| Key | Values | Meaning |
|---|---|---|
| `id` | `a-z 0-9 -`, 1 to 23 | the file's name without `.prn` |
| `name` | 1 to 23 printable, rest of line | shown on the panel |
| `version` | 1 to 65,535 | a card file of the same id with a higher version wins (slice F4) |
| `licence`, `source` | SPDX id; URL, up to 3 lines | |
| `match oui`, `name`, `iname`, `svc` | `AA:BB:CC`; name prefix; same, any case; UUID | up to 4 each, `svc` up to 2; every kind used must hit; score oui 4, name 2, svc 1 |
| `service` | UUID, 16-bit or 128-bit | |
| `chr` | `cmd`/`data`/`rx`/`other`, UUID, `write` `write_nr` `notify` | `cmd` required, each role once |
| `cccd` | `next` or `discover` | `next`: the CCCD is the handle after `rx`'s value |
| `mtu_min` | 23 to 247 | |
| `conn` | itvl_min itvl_max latency supervision | BLE units; supervision x 4 > (1 + latency) x itvl_max |
| `head_dots`, `dots_mm` | 8 to 1,024; 8 | |
| `line` | bytes `lsb`/`msb` `1`/`0` `no`/`yes` | bytes a head line (at least head_dots / 8, at most 128), bit order, the value of black, byte order reversed |
| `label feed`, `label dot0` | `top` `bottom` `left` `right` | the label edge that leaves first; the edge at head dot 0, at right angles (E21) |
| `label align`, `label margin` | `center` or a dot; lead trail 0 to 64 | |
| `roll source` | `record` or `fixed` | `record` needs `roll_w` and `roll_h` fields, `fixed` a `roll fixed` line (E27) |
| `roll fixed` | w h gap, mm | up to 8 |
| `roll limits` | wmin wmax hmin hmax, mm | outside: `PRN_SMALL`, `PRN_LARGE` in a PRINT |
| `density` | 0 to 255 | `{density}` |
| `page size`, `lines`, `head`, `flags` | 64 to 8,192B; 1 to 1,024; template of up to 32 tokens; first last | all four or none; the head and the lines fit the page |
| `compress` | `none` or `lzma_lit` 4,096 to 65,536 | the caller compresses; not with `rows` |
| `chunk size`, `pad`, `max`, `frame` | 1 to 1,024B; byte or `no`; 1 to 255; template with `{data}` once | all four, or `rows` |
| `rows` | template with `{data}` once | one frame a head line |
| `write cmd`, `write data` | `req`/`nr`; `req`/`nr` frag gap | frag 16 to 244B and at most `mtu_min` - 3 (E18); gap 0 to 500ms after each write's answer |
| `table speed` | `mean_page`/`stream_len`, up to 12 `above:value`, `else` value | `{speed}`; `above` strictly falling (E19) |
| `tmpl` | name, up to 40 tokens | up to 16 |
| `rx join` | `magic_len` magic off enc add min max, or `notify` min max | the `sv_rx_feed()` rule: bytes before the magic dropped, a length outside min to max drops one byte |
| `rx tail`, `rx check`, `rx key` | 1 to 4 bytes; one `{ck:...}` token; offset 0 to 15 | a frame without the tail or with another checksum is dropped; the checksum sits after its range |
| `answer` | key min | up to 8; a shorter frame of that key is no answer |
| `field` | key name off kind | kind `bit n`, `u8`, `le16`, `be16`, `le32`, `be32`, `bytes n` (1 to 8, little-endian); inside the answer's min (E13) |
| `error` | bit field, code | the first set one in file order ends the job |
| `answer_ms` | 100 to 60,000 | a write's link answer and a poll's answer; 1,000 when absent |
| `relink_ms` | 1,000 to 30,000 | required with any `relink` |
| `job query`, `job print` | step names, up to 24 | the print job holds one `data` step |
| `abort` | template, arguments, `from` step `to` step | sent once on an end inside the range, after a write of the range |
| `verdict` | `counter` field reads gap template args; `result` field values; `done`; `none` | see below |

Fields: `busy`, `printing`, `buf_full`, `cover`, `no_label`, `label_end`,
`label_err`, `label_mode`, `paper_out`, `battery_low`, `head_hot`,
`roll_w`, `roll_h`, `roll_gap`, `roll_type`, `left`, `uid`, `count`,
`page_index`, `result`, `battery`, `x0` to `x3`. Codes a file names:
`COVER`, `NO_LABEL`, `LABEL`, `BATTERY`, `HOT`, `BUSY`, `SILENT`,
`NOT_PRINTED`, `ROLL`.

## Templates

| Token | Bytes | Meaning |
|---|---|---|
| `7E` | 1 | the byte |
| `{a:u8}`, `{p0:le16}` ... | 1 to 4 | an argument of the step's `send`: `a`, `p0`, `p1`, `p2`; named templates only |
| `{<var>:<enc>}` | 1 to 4 | a variable; encodings `u8` `le16` `be16` `le32` `be32` |
| `{len:<enc>:<from>:<to>}` | 1 to 4 | bytes from offset `from` to `to` of the finished frame; -1 is the last byte |
| `{ck:<kind>:<from>:<to>}` | 1, 2, 4 | a checksum, written after the lengths |
| `{ck:svpage:le16}` | 2 | page head only: the Supvan buffer sum (`sv_buf_fill()`) |
| `{data}` | n | the chunk, or the head line |

Variables: `stream_len`, `pages`, `frames`, `lines`, `line_bytes`,
`head_dots`, `density`, `speed`, `mlead`, `mtrail`, `roll_w`, `roll_h`,
`roll_gap`, `lay_w`, `lay_h`, the saved `c0`, `c1`, `uid0`, `w0`, `h0`,
`v0` to `v3`; `pflags` in the page head, `idx` and `total` in the chunk
frame, `line_no` in rows. In the page head `lines` is the page's lines.

Checksums: `sum8`, `sum16le`, `sum16be`, `xor8`, `crc8` (polynomial 0x07,
init 0), `crc16ccitt_le` and `_be` (0x1021, init 0xFFFF), `crc32le` (IEEE
802.3).

## Steps

```
step <name>                  1 to 11 characters a-z 0-9 _; up to 24
  send <tmpl> [a=x] [p0=x] [p1=x] [p2=x]   x a number or a variable
  data                       the stream's frames
  wait <ms>                  1 to 5,000: a step that sends nothing
  expect <key>               the answer's key
  delay <ms>                 1 to 5,000: the first send waits, not after a relink
  poll <ms>                  20 to 5,000 after each answer, until ...
  until <cond> [and <cond>]
  within <ms>                100 to 60,000: a try's answer; with poll the step's
                             deadline; data: a write's answer
  tries <n>                  1 to 5 sends, not with poll
  else <CODE>                the end at the deadline; SILENT when absent
  flags on                   a set error field of the answer ends the job
  roll                       roll_w or roll_h 0: NO_LABEL; in a PRINT outside
                             roll limits: SMALL or LARGE
  check <cond> else <CODE>   up to 4
  save <var> <field>         up to 4: c0 c1 uid0 w0 h0 v0 to v3
  relink <step> | verdict    a drop here waits relink_ms, then goes on there
```

Conditions: `<field> set`, `clear`, `= n`, `!= n`, `= var`, `= var-1`,
`!= var`. `lay_w` and `lay_h` of 0 match any roll. A `send` step without
`expect` is done when its write is taken. A step holds one of `send`,
`data` and `wait`, and `send` and `data` hold `within` (E24).

Verdicts after the print job's last step: `counter` sends its template up
to `reads` times, `gap` ms apart, and the field equal to `c0` - 1 with
`uid` equal to `uid0` is PRINTED, `c0` every time is NOT_PRINTED (LINK
after a relink), any other value ROLL; `result` is PRINTED when the field
of the last answer is one of up to 4 values; `done` is PRINTED; `none` is
SENT.

## Error codes

| Code | Cause | Code | Cause |
|---|---|---|---|
| E01 | empty, or over 8,192B | E16 | no `end`, or a line after it |
| E02 | a byte outside the set | E17 | id is not the file's name |
| E03 | a line over 120B | E18 | frag over `mtu_min` - 3 |
| E04 | over 256 lines | E19 | a speed table out of order |
| E05 | no `rcbench printer, format 1` | E20 | no `match` line |
| E06 | unknown key or sub-key | E21 | `dot0` not at right angles to `feed` |
| E07 | a value out of its range or form | E22 | `{data}` missing, twice, or outside a frame |
| E08 | a key twice | E23 | `rows` with `chunk` or `compress lzma_lit` |
| E09 | a required key missing, or not above its user | E24 | a step whose lines do not fit together |
| E10 | over 16 templates, or a template over its tokens | E25 | `relink` to a step not in the print job |
| E11 | an unknown placeholder or encoding | E26 | `abort` range not in the print job's order |
| E12 | a range that cannot lie inside its frame | E27 | `roll source` without its rolls or fields |
| E13 | a field past its answer's min | E28 | over 8 answers, 24 fields, 12 errors or 24 steps |
| E14 | an unknown template, field or variable | E29 | a name with a character outside its set |
| E15 | a job's step unknown, twice, or the data step misplaced | E30 | a table over its capacity |
