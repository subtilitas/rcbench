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
python3 tools/research/session.py record T1 TASK_OUTPUT_FILE --base $B
git -C $B/results push origin research/round1-results
python3 tools/research/session.py raised --base $B
git -C $B/plan push origin research/round1
```

`prepare` fetches origin and refuses a task out of turn: T2 and T4 before T1
is recorded, T3 before T2 and T4, T5 before T3, T6 before T5. It refuses T2 to
T4 and a P2-P4 follow-up while a question of their categories under "Raised by
P1" has no answer, unless it only feeds Q4, Q8 or Q9. It numbers new
questions after the last one on the page. Before T6 it refuses until the
owner's decisions on Q4, Q8 and Q9 are written, then merges `research/round1`
into the results tree. It records the Claude Code version, the model, the
effort, the CPU count and the workflow concurrency in the arguments.

`record` refuses a run already recorded, a plan or a refusal, and a return
that does not match its schema. A task P0 stopped is recorded as
`TASK-stopped-N` and does not count as recorded. It commits only the run's
directory; after T6 it also commits the pages P7 wrote.

A follow-up task takes `--followup FILE`, a JSON object with `phases` (`P1`,
`P2-P4` or `P5-P6`), `round` (1 or 2), `categories` and `items`, and
`--name N`; it is recorded with `record FU OUTPUT --name N` and raised with
`raised --run FU-N`. Each task's `followUps` list is the source of the next
follow-up files.
