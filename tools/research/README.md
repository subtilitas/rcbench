# Round 1 research scripts

The scripts that run round 1 of the IO (input/output) board component research,
as [the plan](../../hardware/docs/Research.md) sets it out. The plan is the
agents' instructions; these files run it and hold its counts, schemas and host
table. Nothing here runs in CI (continuous integration) but `session.py check`,
which reads no network and starts no agent.

| File | Content |
| --- | --- |
| `round1.js` | the workflow script, one run per task: `args.task` is T1 to T6, or FU for a follow-up task. `args.mode` `plan` returns the task's planned agent count and starts none |
| `schemas.json` | the return schema of each role: P0, P1, P1-critic, P1-recheck, P2, P3, rerank, P4 (both verifiers), adjudicator, P5, P5-critic, P6, P6-critic, P7, P7-critic |
| `categories.json` | R1 to R13, the three groups, the cap of 32 and each task's planned agents |
| `hosts.json` | each host P0 probes: its known page, its client, the regular expression the lifecycle status matches, and the categories it holds when unreachable |
| `jlcparts.json` | the saved parts database of 2026-09-14: its path under the base directory, its SHA-256 (Secure Hash Algorithm, 256 bits) and its row count |
| `vendors.py` | the readings agents take: a page (`fetch`), JLCPCB stock by exact LCSC number (`jlcpcb`), Digi-Key stock, lead time and status (`digikey`), each dated |
| `session.py` | the session's side: `check`, `prepare`, `record`, `raised` |
| `dryrun.js` | runs `round1.js` for every task with mock agents and checks the counting |

## Limits

- At most 32 agents a task. The planned agents always run. A restart, an
  adjudicator and a new verifier pair each take a free agent; with none left
  the item is returned for a follow-up task.
- An agent that returns nothing counts as not checked, never as not refuted.
- The workflow script has no file or git access. Every return comes back in
  the task's output, and `session.py record` writes and commits it.
- `vendors.py fetch` needs curl_cffi for the chrome and safari clients.
  `vendors.py digikey` needs the owner's credentials: `DIGIKEY_CLIENT_ID` and
  `DIGIKEY_CLIENT_SECRET`, or a file named by `DIGIKEY_ENV_FILE`.

## Running a task

On the research server, from a clone on `main`, with `research/round1` and
`research/round1-results` on the remote and the parts database at
`~/rcbench-research/jlcparts-2026-09-14/` (`--db` names another path):

```bash
B=~/rcbench-research/round1
O="--base $B --digikey-env PATH_TO_CREDENTIALS --model MODEL_ID --effort EFFORT"
python3 tools/research/session.py check
python3 tools/research/session.py prepare T1 $O
# Workflow tool: scriptPath tools/research/round1.js, args = $B/args-T1.json
# (a follow-up's arguments are $B/args-FU-N.json)
python3 tools/research/session.py record T1 TASK_OUTPUT_FILE --base $B
git -C $B/results push origin research/round1-results
python3 tools/research/session.py raised --base $B
git -C $B/plan push origin research/round1
```

`prepare` fetches origin and refuses a run out of turn: T2 and T4 before T1 is
recorded, T3 before T2 and T4, T5 before T3, T6 before T5, a P1 follow-up
before T1, a P2-P4 follow-up before the tasks that own its categories, and a
P5-P6 follow-up before T5. It refuses a P1 run while another P1 run is
prepared and not recorded, so question IDs do not repeat, and numbers new
questions after the last one on the page and in the committed runs.

Before T2 to T4, a P2-P4 follow-up and a P1 follow-up it refuses while a
question a committed P1 run raised for their categories is not yet under
"Raised by P1" on `research/round1`, or is there without an answer. Before T5,
a P5-P6 follow-up and T6 the question check covers every category. A question
that only feeds Q4, Q8 or Q9 (`blocks` is `decision-only`) needs no answer.
Before T2 to T4 and a P2-P4 follow-up it also refuses while the latest P1 run
covering a category left P1 items there for follow-up.

Before T3, for the categories of T2 and T4, and before T5, a P5-P6 follow-up
and T6, for every category, it refuses while a function has no verified part
or a missing or unverified second source; while an R10 or R12 function has a
Q4 or Q8 alternative not verified with its own second source; while the last
run of a category left a figure the datasheet verifier did not confirm; while
the latest P1 run left P1 items in a category; and while a category's parts
were selected before a P1 run raised questions for it, or before its answers
under "Raised by P1" changed. Before T6 it refuses while the owner's decisions
on Q4, Q8 and Q9 are not written; while a part changed after the last P5/P6
check; while that check ran without P5 or P6, left combinations or budgets its
critic did not rule on, or lists conflicts or gaps with no round 2 run; and
while the output paths have changes. Then it merges `research/round1` into the
results tree.

`--accept-open REASON` passes the open items, not the refusals for turn,
questions or decisions; the reason and the items go into the arguments. The
gates read committed run records and the plan on `research/round1`. The
arguments hold the results tree's head, the Claude Code version, the model,
the effort, the CPU count and the workflow concurrency.

A category whose P3 returned nothing keeps no part: its selection names the
part in `without_p3` for the follow-up task. A function P3 lists as named by
the row and not returned by P2 is selected with no part. A ranking with a
repeated position or one below 1 ranks nothing for that function.

`prepare` gives each run an identity, `run_id`, which the workflow returns.
`record` refuses an output whose run or `run_id` differs from the prepared
arguments, an output already recorded, a plan or a refusal, a return that does
not match its schema, and a results tree whose head moved since `prepare`. A
task P0 stopped is recorded as `TASK-stopped-N` and does not count as
recorded. After a run that selects parts it rewrites
`hardware/research/round1/selection.json`, the part each function keeps: a
later run's verified part overrides an earlier one, and a run that verified
none leaves the earlier part. A follow-up that refutes the part in force and
verifies none clears it from `selection.json`. `record` commits only the run's
directory and that file.

After a T6 that was not stopped `record` also commits the pages P7 wrote. It
refuses a changed file outside the files P7 declared and its critic reviewed
within the plan's Outputs (`hardware/docs/`, `hardware/STATUS.md`,
`hardware/README.md`, `tools/jlc_stock.py`), a file P7 declared that did not
change, and a group page outside `hardware/docs/`. A T6 is recorded as stopped
unless P7 and its critic both return, the critic's three checks pass, it
checked at least one figure and every figure agrees with its return, no
writing issue is left, the three group pages are three files in
`hardware/docs/`, and every output of the plan and each group page was written
and reviewed. A stopped T6 leaves the output paths as they were. `raised`
reads the committed run record only and refuses a plan tree with uncommitted
changes.

A follow-up task takes `--followup FILE`, a JSON object with `phases` (`P1`,
`P2-P4` or `P5-P6`), `round` (1 or 2), `categories` and `items`, and
`--name N` (letters and digits, without `stopped`). Each item names one of the
follow-up's categories; a P5-P6 follow-up takes none, as it checks the whole
board. It is recorded with `record FU OUTPUT --name N` and raised with
`raised --run FU-N`. Each task's `followUps` list is the source of the next
follow-up files.
