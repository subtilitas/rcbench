# prnengine

This module drives a Bluetooth LE (BLE) label printer from a description
file: a text file of data that the engine interprets. It reads the file
into a fixed-size spec, builds the image stream (head lines, pages, chunk
or row frames), renders commands from the file's templates, joins the
printer's answers, reads their fields and runs a QUERY or a PRINT job over
one link, one write at a time. The file format is
[protocols/printers/README.md](../printers/README.md); the files of record
live in that folder.

| Part | Files | Depends on |
|---|---|---|
| Reader | `prn_parse.c`, `prn_parse_img.c`, `prn_parse_ext.c`, `prn_parse_int.h`, `prn_check.c`, `include/prn_spec.h` | C11 standard headers |
| Engine | `prn_tmpl.c`, `prn_image.c`, `prn_job.c`, `include/prn_job.h` | C11 standard headers, `prn_spec.h` |
| RP2350 pin driver | none | |

The module has no pin driver and no PIO (programmable input/output)
program. The caller owns the GATT (Generic Attribute Profile) client,
writes what `prn_job_tx()` hands out and feeds the notifications to
`prn_job_rx()`. No firmware links the module yet: the panel's
`print_ctl` runs `protocols/supvan`'s `sv_job`.

## Interface

```c
prn_spec_t spec;          /* 6,080B on the host, at most 6,144B */
prn_perr_t err;
if (!prn_parse(text, len, "supvan-t50m-pro.prn", &spec, &err)) {
    /* err.code 1 to 30 (E01 to E30), err.line from 1, 0 for the file */
}
prn_job_t job;            /* at most 2,048B */
prn_stream_t z = { stream, stream_len, pages, lines, roll_w_mm, roll_h_mm };
prn_job_start(&job, &spec, PRN_JOB_PRINT, &z, now_ms);
/* every poll: */
const uint8_t *data; uint16_t n; prn_write_t kind;
if (prn_job_tx(&job, now_ms, &data, &n, &kind)) { /* write it */ }
prn_job_written(&job, ok, now_ms);      /* the write's answer */
prn_job_rx(&job, notification, len, now_ms);
prn_job_link(&job, up, now_ms);
prn_job_abort(&job, now_ms);
prn_job_tick(&job, now_ms);
if (prn_job_ended(&job)) { /* job.err: prn_err_t */ }
```

- `prn_job_written()` is the link's answer to a write request, or the host
  stack taking a write without response.
- Compression is the caller's. With `compress lzma_lit` the caller runs
  `protocols/supvan`'s `lzlit` over the pages from `prn_page_fill()`,
  writes the file's dictionary into header bytes 1 to 4 and hands the job
  the stream.
- `prn_match()` scores an advertisement against a spec: oui 4, name or
  iname 2, svc 1, 0 for no match.
- `prn_err_t` holds the 15 values of `sv_err_t` at the same numbers, then
  `PRN_ENGINE`, `PRN_MEMORY` and `PRN_SENT`.

## Constraints

### Memory and time

| Item | Bytes or time |
|---|---|
| `prn_spec_t` | 6,080B on x86-64; `_Static_assert` at 6,144B |
| `prn_job_t` | 2,048B on x86-64 (frame buffer 1,184B, command 160B, joiner 128B); `_Static_assert` at 2,048B |
| stack, ESP32-P4 build (GCC 14.2 `-Os`, `-fstack-usage`) | `prn_parse()` 432B frame (the reader state with the 121B line), 672B deepest chain; `prn_job_tx()` 304B, `prn_job_rx()` 272B, `prn_page_fill()` 256B deepest chain; `sv_job_tx()` 80B, `sv_job_rx()` 144B |
| heap | none |
| a job | ends within `PRN_JOB_MS`, 120s, as `PRN_ENGINE`; inside the `abort` range it then has 2 x `answer_ms` + the data gap more to send the abort template |

### Limits a file from the card cannot pass

- A file is at most 8,192B, a line 120B, a file 256 lines; any byte
  outside 0x20 to 0x7E, TAB, and CR before LF is E02.
- Every table has a fixed size, checked before the write: 14 match lines,
  16 templates of at most 40 tokens (a 120B line holds at most 37: `tmpl`, a
  one-letter name and 2-character tokens) and 384 tokens in all, 8 answers, 24 fields,
  12 error lines, 24 steps, 4 checks and 4 saves a step.
- The reader runs one pass with no recursion; its work is linear in the
  file's size.
- A template's checksum and length ranges are checked against the shortest
  and the longest frame the template can make before the file is taken
  (E12); at run time a range outside its frame, or a value too large for
  its encoding, ends the job with `PRN_ENGINE` before the write.
- A field is read only from an answer at least its key's `min` long, and
  the reader holds every field inside that `min` (E13).
- Every step has a deadline. A poll step ends at `within`, every write
  answer at `answer_ms` or the data step's `within`.
- The engine calls `memcpy()`, `memset()`, `memcmp()` and `strlen()` and
  no other function outside its own objects; the ctest
  `prnengine_symbols` (`test/host/check_prn_symbols.py`) holds that with
  `nm`.
- Nothing in a file is a function pointer, a path or a format string;
  every kind a file names is a `switch` case whose default refuses.

### Behaviour a file cannot change

- One write is out at a time. Its answer from the link is booked against
  that write, so the printer's answer may come before or after it.
- The joiner is emptied when a command is handed out.
- An end inside the `abort` range, after a write of that range went out,
  sends the abort template once, after the write in flight is answered.
- A link drop in a step with `relink`, after a write of such a step went
  out, waits `relink_ms` and goes on at the target. A second drop ends the
  job with `PRN_LINK` and no verdict.
- In the verdict, a drop relinks as the job's last step does.

### Known difference from sv_job

The T50M Pro file splits `sv_job`'s step 3 into `ready` and `rebase`.
`sv_job` holds the counter's re-read in step 3 to that step's 5s deadline
(`PRN_BUSY`); the file's `rebase` step has its own 1s (`PRN_SILENT`). The
two differ when step 3 has waited over 4s and the re-read then gets no
answer. No scenario of `test_supvan_job` or `test_supvan_job_ends` reaches
that case.

## Tests

| Suite | What it holds |
|---|---|
| `test_prn_job_vs_sv` | byte identity, layer 3: `sv_job` and the engine with the T50M Pro file against `test/host/sv_sim.h` through every scenario of `test_supvan_job` and `test_supvan_job_ends`, plus a roll changed while step 3 waits; each write's time, bytes, length and kind, and each state, compared |
| `test_prn_image` | layers 1 and 2: pages equal `label-bufs.bin`, their literal stream `label-lit.lz`, chunk frames of `label.lz` equal `label-dataframes.bin`, commands equal the 21 requests of `label-cmds.tsv`, each of the 29 writes of `label-frames.bin` is a write the engine makes; pages for 1 to 300 lines against `sv_buf_fill()`, chunks of 1 to 2,000B against `sv_chunk_build()`, the speed table against `sv_speed()` |
| `test_prn_parse` | E01 to E30 with their lines, CRLF, the limits on both sides, damaged specs, the match score |
| `test_prn_tmpl` | every checksum kind against its reference value over "123456789", ranges at both ends, encodings at their top and one past |
| `test_prn_job` | rows, writes without response, wait, delay, polls on two conditions, a length joiner with a tail and a check, relinks, the verdicts `done` and `result`, start refusals, `PRN_ENGINE`, the 120s limit, the limit inside the T50M Pro's abort range with the tick or the write first and with a write in flight |
| `test_prn_fuzz` | the reader on every cut and every line removed, doubled and swapped, and on 3 seeds x 6,000 changed files; the job on 3 seeds x 500 random printers |
| `prn_parity` | `test/host/prn_parity.py`: the C reader and `tools/printer_files.py` on 2 seeds x 1,500 changed files: the same verdict, code, line and dump |

The fixture printers `rows-test.prn` and `magic-test.prn` in
`test/host/fixtures/printers/` exist for these suites only.

## Trademarks

Supvan and Katasymbol are names of their owners. They are used only to
say which printer a file describes.
