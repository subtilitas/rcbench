# Component research scripts

The scripts that run rounds 1 and 2 of the IO (input/output) board component
research, as [the plan](../../hardware/docs/Research.md) sets it out. The plan
is the agents' instructions; these files run it and hold its counts, schemas
and host table. Nothing here runs in CI (continuous integration) but
`session.py check`, which reads no network and starts no agent.

| File | Content |
| --- | --- |
| `round1.js` | the workflow script of both rounds, one run per task: `args.task` is T1 to T6, or FU for a follow-up task, and `args.research_round` the round, 1 when absent. `args.mode` `plan` returns the task's planned agent count and starts none |
| `schemas.json` | the return schema of each role: P0, P1, P1-critic, P1-recheck, P2, P3, rerank, P4 (both verifiers), adjudicator, P5, P5-critic, P6, P6-critic, P7, P7-critic |
| `categories.json` | R1 to R13, the three groups, the cap of 32, each task's planned agents, the "P1 asks" items of each row and its lines in IOBoard.md (`p1_asks`), and the fixed inputs of the Scope table, each with its category, function and part (`fixed_inputs`) |
| `hosts.json` | each host P0 probes: its known page, its client, the regular expression the lifecycle status matches, and the categories it holds when unreachable |
| `jlcparts.json` | the saved parts database of 2026-09-14: its path under the base directory, its SHA-256 (Secure Hash Algorithm, 256 bits), its row count and its manifest's created time |
| `vendors.py` | the readings agents take: a page (`fetch`), JLCPCB stock by exact LCSC number (`jlcpcb`), Digi-Key stock, lead time and status (`digikey`), each dated |
| `session.py` | the session's side: `check`, `prepare`, `record`, `raised`, `script` |
| `dryrun.js` | runs `round1.js` for every task with mock agents and checks the counting, and round 2's prompts and T6 rules |

## Limits

- At most 32 agents a task. The planned agents always run. A restart, an
  adjudicator and a new verifier pair each take a free agent; with none left
  the item is returned for a follow-up task.
- An agent that returns nothing counts as not checked, never as not refuted.
- A refutation holds only under the function it was made in. A part whose
  refutation stands, also on a part-level reading such as
  `lifecycle status` or `end-of-life notices`, is still kept under another
  function, in the same run or a later one, when both verifiers confirm it
  there.
- A refutation fails only the alternate relationship when each refuting
  verdict fails `pin-for-pin match` or `functional match` and no other
  check. One that also fails another check, or gives its reason in text
  with no failing check, gets one ruling: when it stands, the part is
  refuted in the function in every role, also when only its fit to the
  other part holds.
- The workflow script has no file or git access. Every return comes back in
  the task's output, and `session.py record` writes and commits it.
- `vendors.py fetch` needs curl_cffi for the chrome and safari clients.
  `vendors.py digikey` needs the owner's credentials: `DIGIKEY_CLIENT_ID` and
  `DIGIKEY_CLIENT_SECRET`, or a file named by `DIGIKEY_ENV_FILE`.
- Digi-Key's API allows 1,000 calls a day and resets at 00:00 UTC; a call
  past it answers HTTP 429 and its check reads as not read. T2, T4 and the
  first R1 to R4 follow-up of 2026-09-28 made 367, 325 and 273 (by the API's
  count; the follow-up ran out). `DIR/cache/digikey/` (`DIGIKEY_CACHE_DIR` in
  the agents' commands) holds one folder per UTC day, where
  `vendors.py digikey` and `digikey-search` keep every answer of HTTP 200 or
  404: an agent of any run that asks again that day for the same part or
  keywords gets the kept answer, marked `"cached": true`, with the time and
  API call of the reading. A lock per part or keywords makes agents that ask
  at once wait for one call. A keyword search that returned every product it
  matched also keeps each part number it lists as that part's reading, unless
  one is kept already; on 2026-09-28 the
  search and details records of 26 parts read both ways agreed in every
  field. A part number several Digi-Key products carry answers 404
  "Duplicate Products found" (54 parts on 2026-09-28): `vendors.py digikey`
  then reads it with one keyword search and lists the matches with their
  makers, with `"complete": false` when the search matched more products than
  it returned. A refusal such as 429 is not kept. P2 and P3 read Digi-Key only
  for candidates that pass every requirement value, and P2 records up to
  three survivors per function.

## Rounds

`prepare`, `record`, `raised` and `script` take `--round N`, 1 unless given.
Each round has its own branches, records and script name:

| | Round 1 | Round 2 |
| --- | --- | --- |
| Plan branch, prerequisite 4 | `research/round1` | `research/round2` |
| Results branch, prerequisite 5 | `research/round1-results` | `research/round2-results` |
| Base directory (`--base`) | `~/rcbench-research/round1` | `~/rcbench-research/round2` |
| Records | `hardware/research/round1/RUN/` | `hardware/research/round2/RUN/` |
| Script `script` writes | `round1-RUN.js` | `round2-RUN.js` |

The runs of every round up to N count as earlier runs: the gates, the
selection, the inventories, the committed questions and the next question
number read `hardware/research/round1/` and, in round 2,
`hardware/research/round2/`, in the order the runs were recorded. A run's
sequence number counts the runs of every round, so round 2's first record is
18. `record` writes the round into each `task.json` as `research_round`;
round 1's records, which lack it, are read as round 1. The round's own
`selection.json` holds the part each function keeps over every round once a
run of the round is recorded; until then a run reads the previous round's.
`check` runs the round rules on a throwaway git repository, and fails when
the plan has no section "Round 2".

A follow-up file's `round` is the pass of follow-up tasks within the research
round, 1 or 2: the plan's "Follow-up tasks run in at most 2 rounds". It is not
the research round.

`round1.js` runs both rounds. `prepare` gives it the round
(`research_round`), the plan branch (`branch`), the round's records directory
(`runs_dir`), every round's (`runs_dirs`) and the `selection.json` the run
reads (`selection_file`); without them it uses round 1's. In round 2 every
prompt names round 2, the records of both rounds and the selection file, and
tells the agent that the plan's section "Round 2" holds where it and a
category row or a phase row differ. P6 may mark a specification line
"stage 2 of round 2".

Round 2's T6 updates the group pages round 1's T6 wrote: `prepare` reads
them from round 1's P7 return (`Control.md`, `Supply.md`, `Sensing.md`) and
passes them as `group_pages`. P7 must name exactly those pages, or T6 is
stopped. It must write `hardware/docs/IOBoard.md`, `hardware/docs/Research.md`
and `hardware/STATUS.md` (`t6_outputs`), and writes `Parts.md`, `Power.md`,
`hardware/README.md` and `tools/jlc_stock.py` (`t6_may_write`) only where
round 2's returns change them, listing a file only when it changed it. The
P7 critic corrects only the files P7 listed. A stock exception's mark counts
on its `Parts.md` row whether or not P7 wrote `Parts.md`.

## Running a task

On the research server, from a clone on `main`, with `research/round1` and
`research/round1-results` on the remote and the parts database at
`~/rcbench-research/jlcparts-2026-09-14/` (`--db` names another path):

```bash
B=~/rcbench-research/round1
O="--base $B --digikey-env PATH_TO_CREDENTIALS --model MODEL_ID --effort EFFORT"
O="$O --agent-model sonnet --oversight-model opus"
python3 tools/research/session.py check
python3 tools/research/session.py prepare T1 $O
python3 tools/research/session.py script T1 --base $B --out SCRATCH
# Workflow tool: scriptPath SCRATCH/round1-T1.js, no args
# (a follow-up: script FU-N, SCRATCH/round1-FU-N.js)
python3 tools/research/session.py record T1 TASK_OUTPUT_FILE --base $B
git -C $B/results push origin research/round1-results
python3 tools/research/session.py raised --base $B
git -C $B/plan push origin research/round1
```

A round 2 run needs `research/round2` and `research/round2-results` on the
remote, both cut from the same `main` commit, and takes `--round 2` on each
command and its own base directory. Its follow-ups are named `FU-2` and a suffix: `--name 2P1` gives `FU-2P1`.
A run that reads stock takes `--digikey-min 250`, as the plan's Round 2
section sets out:

```bash
B=~/rcbench-research/round2
O="--base $B --digikey-env PATH_TO_CREDENTIALS --model MODEL_ID --effort EFFORT"
O="$O --agent-model sonnet --oversight-model opus --round 2"
python3 tools/research/session.py prepare FU $O --followup FU-2P1.json --name 2P1
python3 tools/research/session.py script FU-2P1 --base $B --round 2 --out SCRATCH
# Workflow tool: scriptPath SCRATCH/round2-FU-2P1.js, no args
python3 tools/research/session.py record FU TASK_OUTPUT_FILE --base $B --round 2 --name 2P1
git -C $B/results push origin research/round2-results
python3 tools/research/session.py raised --base $B --round 2 --run FU-2P1
git -C $B/plan push origin research/round2
```

`script` writes `round1.js` with the prepared arguments in place of the
Workflow tool's `args`, into the base directory or the one `--out` names. The
Workflow tool reads a script only from the session's working directory or
its scratchpad directory, so `--out` names one of those: once P1 has raised questions they pass 100 KB (T2's
were 132 KB), too long to copy into a tool call by hand. Only the line
`const A = args || {}` changes, and the script is read back against both
files. `record` takes the Workflow tool's task output file as it is, or its
`result`. `record` and `script` refuse arguments `prepare` wrote for another
round.

The agents run `DIR/tools/RUN_ID/vendors.py`, a copy `prepare` makes of the
`vendors.py` beside `session.py`; its SHA-256 goes into `run_info` as
`vendors_sha256`. The read-only checkout of the plan branch carries the
tools of the day the branch was cut, so a change to `vendors.py` on `main`
reaches the agents only through this copy. FU-A2 of 2026-09-29 ran the
checkout's copy, without the Digi-Key cache of #184 and #185: 253 calls.

`--effort` is the effort every agent of the run gets: `low`, `medium`, `high`,
`xhigh` or `max`. The workflow passes it to each agent. `--model` is the
session's model, which runs the workflow and records its returns.
`--oversight-model` is the model of P2, which finds and records the
candidates, of the adjudicator, the role that rules on a standing refutation,
and of P7 and its critic, which write and check the pages; `--agent-model` is
every other role's: P0, P1 and its critic and re-check, P3, the re-rank, both
P4 verifiers, P5, P6 and their critics.
Each is `sonnet`, `opus`, `haiku` or `fable`; one not given leaves those
agents on the session's model. Both go into `run_info`. From 2026-09-29 the
owner runs round 1 with `--agent-model sonnet --oversight-model opus` at
`--effort high`. The first such run, with P2 on Sonnet, returned 2 of R5's 12
functions from P2.

Each P4 verifier is given, by function and part, the names of the checks
its verdict owes (`manufacturer allowlist` among the datasheet verifier's): a
verdict that lacks one counts as not verified. In FU-B3 of round 1 the
datasheet verifiers on Sonnet left `manufacturer allowlist` out for 18 of 26
parts; on Opus no run had left it out.

A return names its category by ID: the schemas hold the field to `R1` to
`R13`, so the Workflow tool has the agent correct a name. The script reads
text that gives the ID with the name (`R6 (Servo supply)`) or the category's
name alone as that ID, and text naming another ID as another category's
return.

`prepare` fetches origin and refuses a run out of turn: T2 and T4 before T1 is
recorded, T3 before T2 and T4, T5 before T3, T6 before T5, a P1 follow-up
before T1, a P2-P4 follow-up before the tasks that own its categories, a P5-P6
follow-up before T5, and a pass 2 follow-up before a pass 1 follow-up of its
research round is recorded. In round 1 it refuses any task once T6 is
recorded. In round 2 it refuses:

- T1 to T5, which run in round 1 only;
- any run before round 1's T6 is recorded in the results tree, which also
  refuses a results branch cut from a commit without round 1's records;
- a run name any round recorded, T6 aside: T6 runs once in each round;
- a P2-P4 follow-up before P1 follow-ups of round 2 that cover each of its
  categories are recorded;
- a P2-P4 follow-up before P2-P4 follow-ups of round 2 are recorded in the
  categories its task builds on, as far as round 2's P1 follow-ups cover
  them: an R5 to R8 follow-up after R1 to R3 and R9 to R11, as T3 after T2
  and T4;
- a P5-P6 follow-up before P2-P4 follow-ups of round 2 are recorded in every
  category its P1 follow-ups cover;
- T6 before a P5-P6 follow-up of round 2 is recorded;
- any run once round 2's T6 is recorded;
- any run while the plan at the commit it reads has no section "Round 2".

It fast-forwards the results tree to the round's results branch at origin,
and refuses while that tree holds records origin does not (push after each
`record`), any other commit than the merge before T6, which changes no record,
or has diverged. It refuses any run while another run is prepared on the
results head it reads and neither recorded nor recorded as stopped: of two
runs prepared on one head, the second to record is refused, and preparing it
again gives a `run_id` its output does not carry. A run prepared on an
earlier head no longer counts; an abandoned one prepared on the head now
counts until its `args-RUN.json` leaves the base directory. A run is settled
by its `run_id`, so round 1's T6 does not settle round 2's. Two prepares in
one base directory do not overlap: each holds a lock on `.prepare.lock` there
from these checks to the args file, which it writes whole, and the second is
refused while the first holds it. It refuses a P1
run while another P1 run is prepared and neither recorded nor recorded as
stopped, so question IDs do not repeat, and numbers new questions after the
last one on the page and in the committed runs.

It refuses these without exception:

- A task that reads stock (T2 to T5 and P2-P4 and P5-P6 follow-ups) while
  fewer than `--digikey-min` (600 unless given; 0 skips the check) of the
  day's Digi-Key calls are left. `prepare` makes one call to read the count
  (`vendors.py digikey-quota` does the same), names the reset time when it
  refuses, and records the count in `run_info` as `digikey_calls_left`.
- Any task while its parts database (`--db`, or the path in `jlcparts.json`)
  differs from `jlcparts.json` in its SHA-256, its `jlc_components` row count
  or the created time of the `manifest.json` beside it. `prepare` reads all
  three itself; P0 checks them again. On the research server hashing and
  counting the saved copy of 5,940,703,232 bytes take 7 s.
- T1 and P1 follow-ups while S1, S3 or S8 has no answer; T2 to T4 and P2-P4
  follow-ups while S2, S4 to S7, S9 (for R7) or a Blocking row naming one of
  their categories has no answer.
- T2 to T4, P2-P4 and P1 follow-ups while a question a committed P1 run raised
  for their categories is not yet under "Raised by P1" on the round's plan branch,
  or is there without an answer or under another category or question. T5,
  P5-P6 follow-ups and T6 check every category. A question that only feeds Q4,
  Q8 or Q9 (`blocks` is `decision-only` and `decision` names one of them)
  needs no answer. `round1.js` publishes a question as blocking P2 instead,
  with a notice, unless its category is one that decision's "Reported by" cell
  on the page names. P6 and its critic are told that such a question, and a
  specification line whose hardware is selected and whose link is firmware
  work (Scope: "hardware selected, link open"), is not a gap.
- T6 while any of the rows Q4, Q8 and Q9 is missing or has no decision, and
  while the output paths have changes.

`--stock-exception PART=REASON`, for T6 only and repeatable, records a part
whose stock gate failure the owner accepts. T6's stock check then counts as
passed when every line of its report that starts `[FAIL]` is a shortfall of
an excepted part in the format P7 is given: `[FAIL] PART: stock N, gate G`
(or `PART (second vendor)`) with N below G, or `[FAIL] PART: presale N` with
N below zero, PART exactly the excepted part number, and the report ends with
`N problem(s)`, N the count of `[FAIL]` lines, with no traceback. For a fixed
input the owner keeps that no run verified, off the board and not listed at
Digi-Key, `[FAIL] PART: not checked, no Digi-Key product carries PART` is
excepted too, both PARTs the excepted part number. A line in
any other wording, a failed lookup among them, is not excepted, and an
exception's mark counts only on its part's own `Parts.md` row, the line the
critic gives for the part in `part_rows`. With exceptions
given, the report's `[FAIL]` lines are read whether or not the critic marks
the check passed, and `prepare` refuses an exception for a part no function
keeps (kept part, alternate, Q alternative or its alternate). The pages state each
exception with its reason in the part's `Parts.md` row, and only a mark
there counts. `prepare` reads the exceptions before it merges anything. `record` refuses an output whose exceptions differ from the
prepared ones. P7 and its critic run the stock check once each, after their
last edit: each run reads Digi-Key. The critic corrects every sentence that
breaks the writing rules in the files P7 listed, older sentences among them,
before its stock check, and edits nothing after it. It rephrases a
comparison that no verified figure backs to state the rank alone, lists no
sentence of `Research.md` below its status line, and states a contradiction
between the specification and a return as not known instead of listing it
as a writing issue; only a line that starts
with `[FAIL]` is a problem line of the report.

It refuses these unless `--accept-open REASON` records the owner's reason and
the items in the arguments:

- T2 to T4 and P2-P4 follow-ups while a P1 run covering a category left P1
  items there that no later P1 follow-up listed among its items; each listed
  item is cleared, and what that follow-up leaves open takes its place.
  Notices, such as a marking taken as an assumption, do not count.
- T3 and P2-P4 follow-ups on its categories, for the categories of T2 and T4,
  and T5, P5-P6 follow-ups and T6, for every category, while:
  - a function has no verified part, or keeps a part a run at or after its own
    refuted for itself, or a missing or unverified second source; a fixed
    input the owner keeps (below) counts as neither, and `prepare` prints
    each such part once;
  - R10 or R12 has not exactly one function marked with its decision, or no Q4
    or Q8 alternative, other than the kept part, on that function, lacks a
    verified part, of its own and with its class confirmed by the datasheet
    verifier, for one of the decision's option classes (`categories.json`
    `q_options`), or has an alternative not verified with its own second
    source;
  - a run that decides one of the category's functions left a figure the
    datasheet verifier did not confirm, or a value for research or required
    report figure P2 did not return or wrote as not read, not found, not
    stated, not known or unknown (`none` is a value): `categories.json`
    `reports` once per category (as `NAME` or `NAME: PART`),
    `reports_per_part` once for every part the category verifies or selects,
    replacements included (as `NAME: PART`, from P2 or the re-rank);
  - the latest P1 run left P1 items in the category;
  - a run that decides one of its functions predates a later P1 run covering
    the category, read other answers under "Raised by P1" for it (answers to
    questions that only feed Q4, Q8 or Q9 aside), read another specification
    (the files under `hardware/docs/`, Research.md without the rows under
    "Raised by P1" and with the Decision cells of Q4, Q8 and Q9 blank), or,
    for R5 to R8, ran on other upstream entries of T2 and T4 than
    `selection.json` keeps now: a function bound to another part, alternate or
    Q alternative, or decided again by a later run.
- T6 while a function of a category is bound to another part, alternate or Q
  alternative than at the last P5/P6 check, or was decided again by a later
  run; while that check ran without P5, P6 or either critic, returned no
  upheld combination that fits, with its outputs, bind order, resources,
  source and time, has combinations, budgets or assumptions without source and
  time or assumptions without a value, lacks an upheld budget with a value
  read (`shared-part stock` for each part `selection.json` keeps in more than
  one function, in any role, not applicable only when there is none) (not
  unknown, not known or not stated), its source and its reading time for an
  item of `categories.json` `p5_budgets` (named as listed or `NAME: DETAIL`,
  every row of the item counting; the Q4 and Q8 alternatives once for each
  option class of `q_options`, as `Q4 alternatives: CLASS`; from round 2 each
  item of `p5_chips` once for each chip, as `GPIO: main` and
  `GPIO: measurement`; only an item of
  `p5_conditional` may be `not applicable: REASON`, `N/A: REASON` or
  `does not apply: REASON`) or for a rail, I²C bus or other instance its
  critic lists as not budgeted, has an upheld budget not within its limit,
  lacks an upheld assumption with a value, its source and its time for an item
  of `p5_assumptions` (the encoder's state machines are R11's
  `encoder decoding` report), or left a conflict, gap, combination, budget or
  assumption its critic did not rule on (a verdict whose reason reads as none
  rules on nothing), or a combination, budget or assumption it rejected; while
  it lists a conflict or gap that no round-2 P2-P4 follow-up after the check
  before it covered, or an earlier check lists one that no P2-P4 follow-up of
  either round after that check covered: each category a gap's category names
  by its ID or in a range such as `R5 to R8`, and each a conflict's categories
  name and every function any run selected one of its parts for (as the kept
  part, its alternate, a Q alternative or that one's alternate; a retired
  function aside), each researched to a verified part (never the first check's
  items, a conflict naming a part no run selected and the owner does not
  keep, which `prepare` names, or a conflict naming a part the owner keeps,
  which no run verifies, or
  one only retired functions kept, or a category holding a word other than
  IDs, ranges, `and` and list separators); and while a run that decides an R3
  function was not given the Q9 decision now in force.

Then, before T6, it merges the round's plan branch into the results tree. The
gates read committed run records and the plan on the round's plan branch. The arguments
hold the results tree's head, the owner's decisions, the questions committed
P1 runs raised (`raised`), the Claude Code version, the model, the effort, the
CPU count and the workflow concurrency. For T6 they also hold the P5/P6
checks, the conflicts and gaps they leave (each the last check lists, and each
of an earlier check no follow-up after it covered), the assumptions of the
last check's P5 its critic upheld, the parts `selection.json` keeps, and the
sentences of the Outputs row of `tools/jlc_stock.py`. Every run's arguments
hold the fixed inputs the owner keeps that no run verified (`owner_fixed`),
which every prompt names. `session.py check`
confirms the Blocking and Sourcing tables still read as the gates expect, that
the Outputs table has a row for `tools/jlc_stock.py`, and that `p1_asks` names
exactly the rows that carry "P1 asks", each with distinct names, and that
`fixed_inputs` lists the fixed inputs of the Scope table by input name and
part, each `orderable` number, where given, starting with its part.

`round1.js` keeps a function open, with no part, when:

- the category's P3 returned nothing (the part is named in `without_p3`);
- P3 lists it as named by the row, or it is in the category's P1 inventory
  (the functions P1 and its critic read from the row, passed as `inventory`)
  or serves a fixed input, and P2 did not return it;
- the re-rank did not rank it, ranked no part, gave positions other than 1 to
  n, ranked a part with no record, ranked a part twice or also dropped it, or
  either P2 or the re-rank returned it twice;
- P3 found a candidate for it, or overturned its P2 drop of a part, and the
  re-rank neither qualified nor dropped that part under it. P3 names the
  functions whose drop it overturns. Where P2 dropped the part under none of
  them, or P3 names none, the drop is overturned under every function that
  dropped the part. A find P3 files under no function of P2's, and a drop it
  overturns that no function made, hold every function open until the re-rank
  qualifies or drops the part under one of P2's functions. The re-rank is told
  to list each entry of P3's under the functions these rules hold open for it,
  also where P3 says the drop stands for another reason: a qualified record by
  its exact part number, with the entry's text copied exactly in `p3_part`, or
  a drop with the entry's text copied exactly, one drop for each entry. A part
  discussed only in the re-rank's report is neither qualified nor dropped. A
  record in `new_candidates` handles the entry whose text its `p3_part`
  copies, runs of spaces and case aside, and no other entry that shares a part
  number with it. A find and the re-rank's part are the same part when they
  share a part number or an LCSC number, a record's `lcsc` field included:
  `TCAN3413DR (C22433320)`, `A (C1); B (C2)` and a part with its function
  appended all match the bare part number. Part numbers are the pieces between
  semicolons, commas followed by a space, slashes between spaces and ` and `,
  without parenthesized text, case and spaces; a function name is none.
  `2N7002BK,215` and `MCP2542FD-E/SN` are one part number each. A `#` after 4
  characters or more, a letter and a digit among them, starts an ordering
  option: Analog Devices' packing and RoHS (Restriction of Hazardous
  Substances) option in `LTC4020EUHF#PBF` and `LTC4020EUHF#TRPBF`, or an
  automotive flow (`#W`, `#3ZZ`). For a drop P3 overturned, the number before
  the `#` matches any option of it. The re-rank's drop of another option of
  that part is re-read as any P3 drop. A P3 find matches only its own option,
  as the find's option may be the one with stock. Other makers' packing
  suffixes, as Texas Instruments' reel letter (`TPS62933DRLR`, `TPS62933DRLT`)
  or Maxim's `+T`, make another part number: a current limitation. A find that
  names only a family, as `MLX90393 rows`, is also handled by a part of that
  family the re-rank qualified (`new_candidates`) or dropped as a P3 candidate
  (`dropped_from_p3`), not by a part P2 shortlisted. Its stem is a word
  outside parentheses of 6 or more letters, digits and hyphens with a letter
  and a digit, starting with a letter or digit, followed by `rows`, `family`,
  `series`, `variants`, `parts` or `devices`; a part belongs to the family
  when its number starts with the stem. A find with an LCSC number or another
  word of 4 or more characters with a letter and a digit names a part and is
  handled only by that part. A stem also matches a number that continues its
  digits (`TMP107 family` and TMP1075DGKR, a different part): a current
  limitation. The drop of such a find is re-read as the drop of any P3 find;
- it has no requirement, or names one requirement twice;
- it serves a fixed input of the Scope table (`categories.json`
  `fixed_inputs`: the function by the table's input name, the input by a part
  number that starts with the table's, INA238AIDGSR for INA238), and its
  first-ranked part after the drops is another part, or the input's refutation
  stands. The input is reported to the owner (role `owner`), not re-selected:
  no replacement pair runs, and the function keeps no other part, also none
  verified from the verify list. P1, P2 and the re-rank are given the inputs.
  A function that serves the input under another name is not bound to it. A
  fixed input that fails a requirement in P2's record is dropped as any part
  is, and the datasheet verifier re-reads that drop; the function keeps no
  other part, and the owner is told. A packaging suffix of a table entry that
  is already an orderable number counts as the input too (ADXL316WBCSZ-RL7 for
  ADXL316WBCSZ), also where its stock differs: a current limitation. The
  effective selection then keeps the input as the owner's (see `record`).

A part with more than one record, with placements below 1, or with an LCSC
number that is neither `C` and digits nor `none`, has no record: it is
dropped, and a ranking that names it leaves the function open. A shortlisted
candidate P2's own record fails is dropped and its failure re-read by the
datasheet verifier. A requirement a candidate states with another value than
its function is checked against the function's value, and its own pass is
dropped before failures are counted. An adjudicator's ruling without evidence,
source and a reading time of the task is no ruling. A refutation the
adjudicator rules is about the part itself (its `scope` is `part`: lifecycle,
end-of-life, maker, identity) holds in every function of the category, and
`prepare` holds any category that keeps the part after that run. A standing
refutation stays final in the run, even if the part is verified later as
another part's alternate. Once every ruling of a pass is in, a standing
refutation sends the part its function now stands on, the first in rank order
whose refutation has not stood, to one new pair. A refuted part ranked below
that part, such as a Q alternative beside a verified first-ranked part, sends
none; a refuted Q alternative is listed for a follow-up task. A part already
verified, with its rule-5 alternate checked against it, is kept without a
pair. A refutation no adjudicator ruled on, for want of a free agent or of a
ruling, keeps the part open in the run: a later verification does not clear
it, only a standing refutation replaces it, and the part holds no alternate
role. A refutation of a part as an alternate on its fit alone, each refuting
verdict failing `pin-for-pin match` or `functional match` and no other check,
fails only that relationship: the part keeps its own place on the shortlist,
and a part in both roles keeps its own verification on its other checks while
the adjudicator rules on the relationship. Any other refutation as an
alternate holds for the part in the function, in every role, as a refutation
as a primary does: every other check reads the part against the function,
whatever its role. A part a verifier was not asked to verify is ignored, a
part it lists twice has no verdict from it, and a check read as empty, blank
or not read, or as `none` other than `end-of-life notices` and
`longevity commitment`, figure evidence that is none, or either without its
source or a reading time of the task, shows nothing. `longevity commitment`
and `market introduction`, which the lifecycle table records without a gate
(S5 records a commitment and does not require one), may read
`not read: REASON` in the stock verifier's return: that is recorded, neither
passing nor failing, and listed for the owner as an unread manufacturer status
is. A stock verifier's `lifecycle status` written `not read: REASON` (a part
whose maker status cannot be read and that Digi-Key does not list under its
maker, or a Digi-Key value the Lifecycle check does not grade), with passes
true, neither passes nor fails either, whatever agrees says, but shows no
check, so the part stays not verified; with passes false it fails as any
check. Either way it is listed for the owner. A datasheet requirement of the
same name is a requirement. A placeholder reason (`none`, `N/A`, `-`,
`unknown`) is none, and a refuted verdict that states no refutation and whose
only failing checks are such readings is a confirmation; one that states a
refutation is ruled on. A reading time is an ISO (International Organization
for Standardization) 8601 date or date-time at the start of the text, on a
date the calendar has (2026-02-31 is not one); a note, a second reading's time
or a range's end may follow it, as in `2026-09-28T13:51Z to 13:56Z (JLCPCB)`.
The time ends at the end of the text, at whitespace, at `,`, `;`, `(` or `)`,
or at a hyphen before a range's end time, so `2026-09-28T10` and
`2026-09-28T10:00+99` are no times. A date-time without a zone is UTC
(Coordinated Universal Time). For a P4 check, a figure verdict or a ruling,
the time may follow one word that names the source, as in
`JLCPCB 2026-10-02T15:17:10Z; Digi-Key 2026-10-02T15:17:30Z` or
`ADXL316: 2026-10-02T11:27:32Z`. The word is a letter, then letters, digits,
dots and hyphens, with a colon or none. `not`, `no`, `none`, `nothing`, `nil`,
`never`, `unknown`, `unread`, `unavailable`, `missing`, `pending`, `failed`,
`na`, `n.a` and `n.a.`, in any case and also before a hyphen or dot
(`No-data`, `None.`), name no source. Two words before the time are no
reading. For a P4 check, a figure verdict or a ruling, every date the text
gives is also the date `prepare` gave the run or the next day, for a run that
passes midnight, so a reading copied from an earlier return or the parts
database, or dated later, shows nothing. The workflow script has no clock: a
run that goes on past the day after `prepare` counts none of the readings it
takes then. A check that disagrees or fails is a refutation, named in the
refutation the adjudicator rules on whatever the verdict, and a confirmation
whose refutation is not empty is one; a refutation that is `none`, or starts
with `no refutation`, `not refuted`, `none found` or `nothing found`, counts
as empty. A reading that moves or always passes (`stock`, `presale`,
`second-vendor stock`, `lead time`, `distributor status`,
`market introduction`, `longevity commitment`, `library type`), and a
requirement added as not given, refute only when they fail. A check for a
route or role the part does not take refutes nothing: compatibility for a part
that is no alternate, and `second-vendor stock` off the second-vendor route or
for an alternate, which passes rules 1 to 4 only (rule 5). Every verified part
needs a `placements` check re-deriving its count from the specification, a
`board placement` check re-deriving from it whether the part is on the board,
which passes only when its LCSC number agrees (rule 1), the lifecycle table's
readings (`longevity commitment`, `market introduction`, `distributor status`,
`lead time`), for a part on the board `LCSC identity` and `library type`
checks, and a `manufacturer allowlist` check (rule 2), and an alternate needs
at least the placements of the part it stands in for. The datasheet verifier
re-reads each function's requirement list against IOBoard.md, the answers and
each value found for research, which P2 applies to the function its row under
"Raised by P1" names (`function requirements: FUNCTION`). Figures count on its
verdicts only; the stock verifier returns none. A figure it refutes with
evidence, source and a reading time of the task goes to an adjudicator with
the claim the figure states: the value reported or found, the class, the
requirement list, the failed requirements or the drop. A replacement pair
confirms no figure, but its datasheet verifier's refutation of one is
adjudicated in the same way. A part on the board that the owner holds may pass
the stock gate on a `held quantity` check in place of `stock` and `presale`
(rule 6; a check named `held quantity, or both stock and presale`, the owed
list's wording up to FU-2C of round 2, is the same check); a passing held
quantity that shows a reading supersedes failing live readings, and passing
live readings a failing held quantity. A return whose `category` names another
category counts as not returned. Only the kept part's rule-5 alternate gates
its selection, and the alternate of a part on the board must be on the board.
The kept part counts for a Q4 or Q8 option class in which the datasheet
verifier confirmed it: `kept_option` when it is the first-ranked part after
the drops, the option the re-rank gives it in the ranking, or its option when
it is a Q alternative. A kept part given two different classes in these two
roles counts for neither. An alternate counts only for the part it was checked
against: an alternate several parts name is checked against the first of them
(the first-ranked part, then the Q alternatives in order), and a replacement's
alternate against the replacement. A part verified in its own right, the
first-ranked part or a Q alternative, that is also another part's alternate
keeps that verification. It holds the alternate role while verified, also
after a refutation that did not stand, with its compatibility checks. A
category whose chain failed is left out of the run's selection, so the gates
read the run before it. Each assumption needs a confirmed question of its own,
whose `for_where` and `for_quantity` are the assumption's location and
quantity. The P1 critic rules on a marking by the value's index, location and
quantity, and a marking it upholds with the same marking is unchanged. A P1
critic or re-check verdict whose reason or evidence reads as none is no
verdict. A P1 return with no value, and a category whose P1 and critic name no
function, leave a P1 item. Each "P1 asks" item of the category that no
question under "Raised by P1" names in `asks` needs a confirmed question that
names it. Without one it leaves a P1 item, also when the critic or the
re-check rejected the question for it. A question under "Raised by P1" is not
raised again: it asks for an assumption at its `for_where` and `for_quantity`.
A P1 follow-up gives its items to P1 and the critic, and lists again each item
its run did not deal with: a value its P1 did not return, a question neither
its P1 nor its critic raised and not under "Raised by P1" (the same words, or
the same `for_where` and `for_quantity`), and any other item of a category
whose P1 or critic returned nothing; an item listed again stands for the item
it carries. P1 and the critic are told to copy an item's question word for
word; an item without a `for_where` whose question they raise in other words
is listed again. P1 and the critic of a follow-up are given the names the
category's inventory and `selection.json` give its functions, and keep them.
P0 counts a host with two rows, or a status written as not read, none, no,
false, absent or not in the page body, as not read. A P0 row counts only at
its host's endpoint: an API client's command with the probe, whatever
environment assignments (`DIGIKEY_ENV_FILE=...`, `env`) and interpreter path
precede it, a page client's probe URL, or with no probe a page on the host
itself. The P7 critic checks at least one figure on each group page and each
output under `hardware/docs/` T6 must write, each check naming the figure and
the file under a round's records directory it comes from, a path ending in
`.json`; text after the path, such as `:34` or `#L34`, is not part of it, and
`record` reads the same path. Each check gives its kind: budget, combination
or other. A budget or combination checked against any file but the last P5/P6
check's is superseded, and so is any figure checked against an earlier check's
P5 return (a restarted one included), whatever its kind; every figure P7 lists
as written needs a check of its own, a figure written on n lines of a file
checks on n lines of it (a check of text the critic corrected names P7's
figure text in `p7_figure` and stands for it); another figure, such as a run's
status line, may cite an earlier check's other files. P7 and its critic are
given the three commands, the pages that need a figure checked, and the path
rules for group pages and figure checks that `round1.js` and `record` apply.
The schemas ask P5, P6 and their critics to write a conflict's parts as
`selection.json` writes them and each category by its ID alone.

`prepare` gives each run an identity, `run_id`, which the workflow returns.
`record` refuses an output whose run, `run_id`, commit, date, follow-up,
decisions or accepted open items differ from the prepared arguments, that
lacks the result fields or the summary its task writes, or that did not stop
and lacks the returns its task cannot finish without (P0, or P7 and its
critic), an output already recorded in any round, a plan or a refusal, a return that does
not match its schema, and a results tree whose head moved since `prepare`. A
task P0 stopped is recorded as `TASK-stopped-N` and does not count as
recorded. After any run that is not stopped it rewrites the round's
`selection.json` where it changes, the part each function keeps over every
round: the latest run that names a function decides it. A run that names it
and verifies no part leaves it open and records the earlier part in
`not_requalified`. A function an earlier P1 inventory of its category lists
and the latest does not is retired: the next run that selects parts in the
category without naming it removes it. A fixed input of the Scope table
(`categories.json` `fixed_inputs`) is its function's part whatever the runs
return: where no run keeps a part that starts with the input's number, the
function keeps the input's `orderable` number, or the input itself, with run
`owner`. Its `owner_fixed` names the run that left the function open, the
part it replaces and each ledger reading that did not verify it. `record`
commits only the run's directory and that file.

After a T6 that was not stopped `record` also commits the pages P7 wrote. It
refuses a changed file outside the files P7 declared and its critic reviewed
within the plan's Outputs (`hardware/docs/`, `hardware/STATUS.md`,
`hardware/README.md`, `tools/jlc_stock.py`), an output or group page that is
not a file afterwards, a file P7 declared that did not change, a group page
outside `hardware/docs/` or among the fixed outputs, a group page HEAD already
holds other than its group's page the previous round's T6 wrote, a
`Research.md` whose rows under "Raised by P1", Blocking and Sourcing
answers or decisions differ from HEAD's, a figure the critic checked
against a file under a round's records directory that HEAD does not hold, and
a figure check whose line of its page, after T6, does not state the figure in
the check's text as a value of its own (`5 V` is not read in `15 V`, `0.5 V`,
`-5 V` or `5 VA`; runs of whitespace compare as one space). A
renamed file counts as both its old and its new path. A T6 is recorded as
stopped unless P7 and its critic both return, the critic's three checks pass,
it checked at least one figure and every figure agrees with its return, no
budget or combination is superseded, no writing issue is left, P7 names three
group pages, three files in `hardware/docs/` other than the fixed outputs (in
round 2 the pages round 1 wrote), and every output T6 must write and each
group page was written and reviewed: in round 1 every output of the plan, in
round 2 `IOBoard.md`, `Research.md` and `hardware/STATUS.md`. The
critic also gives, in an output or group page, the line that states each item
as the arguments say: each conflict and gap the checks leave as not known,
each item accepted open as not verified or not known, and each upheld
assumption as assumed; the `Parts.md` line and the page of its category's
group for each verified part `selection.json` keeps (the kept part, its
alternate, each Q alternative and its alternate) and each part the owner
keeps, a part not verified being an item accepted open; the line that states
each fixed input the owner keeps as the owner's, with each check no run
confirmed as not known; and a verdict with its reason that `tools/jlc_stock.py`
does what each sentence of its Outputs row states. A stopped T6 leaves the
output paths as they were. A T6 that `record` refuses, for any reason, an
unreadable output included, does too, and keeps what P7 changed in a stash
named `refused T6 RUN_ID` in the results tree. `raised` reads the committed
run record only and refuses a plan tree with uncommitted changes. It writes
each question and function on one line: every run of whitespace, a line break
among them, becomes one space. A question that only feeds a decision ends in
` (feeds Q4 only)`, with that decision's ID; the gates accept that suffix on
that question alone.

A follow-up task takes `--followup FILE`, a JSON object with `phases` (`P1`,
`P2-P4` or `P5-P6`), `round` (1 or 2), `categories` and `items`, and
`--name N` (letters and digits, without `stopped`). Each item names one of the
follow-up's categories; a P5-P6 follow-up takes none, as it checks the whole
board. It is recorded with `record FU OUTPUT --name N` and raised with
`raised --run FU-N`. Each task's `followUps` list is the source of the next
follow-up files.
