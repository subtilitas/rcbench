# Round 1 research scripts

The scripts that run round 1 of the IO (input/output) board component
research, as [the plan](../../hardware/docs/Research.md) sets it out. The plan
is the agents' instructions; these files run it and hold its counts, schemas
and host table. Nothing here runs in CI (continuous integration) but
`session.py check`, which reads no network and starts no agent.

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
before T1, a P2-P4 follow-up before the tasks that own its categories, a P5-P6
follow-up before T5, and a round 2 follow-up before a round 1 follow-up is
recorded. It fast-forwards the results tree to
`origin/research/round1-results`, and refuses while that tree holds records
origin does not (push after each `record`), any other commit than the merge
before T6, or has diverged. It refuses a P1 run while another P1 run is
prepared and not recorded, so question IDs do not repeat, and numbers new
questions after the last one on the page and in the committed runs.

It refuses these without exception:

- T1 and P1 follow-ups while S1, S3 or S8 has no answer; T2 to T4 and P2-P4
  follow-ups while S2, S4 to S7, S9 (for R7) or a Blocking row naming one of
  their categories has no answer.
- T2 to T4, P2-P4 and P1 follow-ups while a question a committed P1 run raised
  for their categories is not yet under "Raised by P1" on `research/round1`,
  or is there without an answer or under another category or question. T5,
  P5-P6 follow-ups and T6 check every category. A question that only feeds Q4,
  Q8 or Q9 (`blocks` is `decision-only` and `decision` names one of them)
  needs no answer.
- T6 while any of the rows Q4, Q8 and Q9 is missing or has no decision, and
  while the output paths have changes.

It refuses these unless `--accept-open REASON` records the owner's reason and
the items in the arguments:

- T2 to T4 and P2-P4 follow-ups while a P1 run covering a category left P1
  items there that no later P1 follow-up listed among its items and cleared.
  Notices, such as a marking taken as an assumption, do not count.
- T3 and P2-P4 follow-ups on its categories, for the categories of T2 and T4,
  and T5, P5-P6 follow-ups and T6, for every category, while:
  - a function has no verified part, or a missing or unverified second source;
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
    the category, read other answers under "Raised by P1" for it, read another
    specification (Research.md outside its Raised by P1 and Decided tables,
    and IOBoard.md), or, for R5 to R8, predates a change to a part of T2 or
    T4.
- T6 while a part changed after the last P5/P6 check; while that check ran
  without P5, P6 or either critic, returned no upheld combination that fits,
  with its outputs, bind order, resources, source and time, has combinations,
  budgets or assumptions without source and time or assumptions without a
  value, lacks an upheld budget with a value read (not unknown, not known or
  not stated), its source and its reading time for an item of
  `categories.json` `p5_budgets` (named as listed or `NAME: DETAIL`; only an
  item of `p5_conditional` may be `not applicable: REASON`, `N/A: REASON` or
  `does not apply: REASON`), or left a conflict, gap, combination, budget or
  assumption its critic did not rule on, or a combination, budget or
  assumption it rejected; while it, or an earlier check, lists a conflict or
  gap that no round-2 P2-P4 follow-up covered after that check (for the last
  check, after the check before it): a gap's category, and a conflict's named
  categories and every function any run selected one of its parts for (as the
  kept part, its alternate, a Q alternative or that one's alternate), each
  researched to a verified part (the first check's items are never covered);
  and while a run that decides an R3 function was not given the Q9 decision
  now in force.

Then, before T6, it merges `research/round1` into the results tree. The gates
read committed run records and the plan on `research/round1`. The arguments
hold the results tree's head, the owner's decisions, the Claude Code version,
the model, the effort, the CPU count and the workflow concurrency. `session.py
check` confirms the Blocking and Sourcing tables still read as the gates
expect.

`round1.js` keeps a function open, with no part, when:

- the category's P3 returned nothing (the part is named in `without_p3`);
- P3 lists it as named by the row, or it is in the category's P1 inventory
  (the functions P1 and its critic read from the row, passed as `inventory`),
  and P2 did not return it;
- the re-rank did not rank it, ranked no part, gave positions other than 1 to
  n, ranked a part with no record, ranked a part twice or also dropped it, or
  either P2 or the re-rank returned it twice;
- P3 found a candidate for it, or overturned its P2 drop of a part, and the
  re-rank neither qualified nor dropped that part under it;
- it has no requirement.

A part with more than one record, with placements below 1, or with an LCSC
number that is neither `C` and digits nor `none`, is dropped. A shortlisted
candidate P2's own record fails is dropped and its failure re-read by the
datasheet verifier. A requirement a candidate states with another value than
its function is checked against the function's value, and its own pass is
dropped before failures are counted. An adjudicator's ruling without evidence,
source and a reading time of the task is no ruling. A standing refutation
stays final in the run, even if the part is verified later as another part's
alternate. A refutation as an alternate fails only that relationship: the part
keeps its own place on the shortlist. A part a verifier was not asked to
verify is ignored, a part it lists twice has no verdict from it, and a check
read as empty, blank or not read (`none` is a reading), figure evidence that
is none, or either without its source or a reading time of the task, shows
nothing. A reading time is a date the calendar has (2026-02-31 is not one);
for a P4 check, a figure verdict or a ruling it is also on or after the date
`prepare` gave the run, so a reading copied from an earlier return or the
parts database shows nothing. A check that disagrees or fails is a refutation,
named in the refutation the adjudicator rules on whatever the verdict, and a
confirmation that states a refutation is one. A reading that moves or always
passes (`stock`, `presale`, `second-vendor stock`, `lead time`,
`distributor status`, `market introduction`, `longevity commitment`,
`library type`), and a requirement added as not given, refute only when they
fail. A check for a route or role the part does not take refutes nothing:
compatibility for a part that is no alternate, and `second-vendor stock` off
the second-vendor route or for an alternate, which passes rules 1 to 4 only
(rule 5). Every verified part needs a `placements` check re-deriving its count
from the specification, the lifecycle table's readings
(`longevity commitment`, `market introduction`, `distributor status`,
`lead time`), for a part on the board `LCSC identity` and `library type`
checks, and a `manufacturer allowlist` check (rule 2), and an alternate needs
at least the placements of the part it stands in for. The datasheet verifier
re-reads each function's requirement list against IOBoard.md and the answers
(`function requirements: FUNCTION`). A part on the board that the owner holds
may pass the stock gate on a `held quantity` check in place of `stock` and
`presale` (rule 6); a passing held quantity that shows a reading supersedes
failing live readings, and passing live readings a failing held quantity. A
return whose `category` names another category counts as not returned. Only
the kept part's rule-5 alternate gates its selection, and the alternate of a
part on the board must be on the board; a first-ranked part that is also a Q
alternative's alternate keeps its own verification, and holds the alternate
role only with its compatibility checks, and an alternate several primaries
name is verified for the first only (the kept part, then the Q alternatives in
order). A category whose chain failed is left out of the run's selection, so
the gates read the run before it. Each assumption needs a confirmed question
of its own, whose `for_where` is the assumption's location. P0 counts a host
with two rows, or a status written as not read, none, no, false, absent or not
in the page body, as not read. A P0 row counts only at its host's endpoint: an
API client's command with the probe, a page client's probe URL, or with no
probe a page on the host itself. The P7 critic checks at least one figure on
each group page and each output under `hardware/docs/`, each check naming the
figure and the file under `hardware/research/round1/` it comes from.

`prepare` gives each run an identity, `run_id`, which the workflow returns.
`record` refuses an output whose run or `run_id` differs from the prepared
arguments, that lacks the result fields or the summary its task writes, or
that did not stop and lacks the returns its task cannot finish without (P0, or
P7 and its critic), an output already recorded, a plan or a refusal, a return
that does not match its schema, and a results tree whose head moved since
`prepare`. A task P0 stopped is recorded as `TASK-stopped-N` and does not
count as recorded. After a run that selects parts it rewrites
`hardware/research/round1/selection.json`, the part each function keeps: the
latest run that names a function decides it. A run that names it and verifies
no part leaves it open and records the earlier part in `not_requalified`.
`record` commits only the run's directory and that file.

After a T6 that was not stopped `record` also commits the pages P7 wrote. It
refuses a changed file outside the files P7 declared and its critic reviewed
within the plan's Outputs (`hardware/docs/`, `hardware/STATUS.md`,
`hardware/README.md`, `tools/jlc_stock.py`), an output or group page that is
not a file afterwards, a file P7 declared that did not change, a group page
outside `hardware/docs/` or among the fixed outputs, and a figure the critic
checked against a file under `hardware/research/round1/` that HEAD does not
hold. A renamed file counts as both its old and its new path. A T6 is recorded
as stopped unless P7 and its critic both return, the critic's three checks
pass, it checked at least one figure and every figure agrees with its return,
no writing issue is left, the three group pages are three files in
`hardware/docs/` other than the fixed outputs, and every output of the plan
and each group page was written and reviewed. A stopped T6 leaves the output
paths as they were. A T6 that `record` refuses, for any reason, an unreadable
output included, does too, and keeps what P7 changed in a stash named
`refused T6 RUN_ID` in the results tree. `raised` reads the committed run
record only and refuses a plan tree with uncommitted changes.

A follow-up task takes `--followup FILE`, a JSON object with `phases` (`P1`,
`P2-P4` or `P5-P6`), `round` (1 or 2), `categories` and `items`, and
`--name N` (letters and digits, without `stopped`). Each item names one of the
follow-up's categories; a P5-P6 follow-up takes none, as it checks the whole
board. It is recorded with `record FU OUTPUT --name N` and raised with
`raised --run FU-N`. Each task's `followUps` list is the source of the next
follow-up files.
