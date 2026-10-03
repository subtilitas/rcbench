# Round 2 research state

A copy of the small local files of the component research's round 2, stored for a pause on 2026-10-03. The working folder is `~/rcbench-research/round2` on the owner's server. It stays there unchanged and is the source; this branch is a copy of its small files.

Start with `round2-base/HANDOVER.md`, section 0.

| Path | Content |
| --- | --- |
| `round2-base/HANDOVER.md` | state, branch heads, commands, accept-open reasons, next steps |
| `round2-base/args-*.json`, `followup-*.json` | each run's prepared arguments and follow-up file |
| `round2-base/FU-2*-output.json`, `T6-output.json` | each run's workflow output as `record` read it |
| `round2-base/scratch/build-check/` | accept-open reasons and the gate's open lists |
| `round2-base/scratch/T6-stopped-85f3ba0e92b88896/` | the stopped second T6: P7's return and the page diff it left |
| `round2-base/session-2026-10-03/` | the session's briefs, PR bodies and helper scripts |

Not copied: each run's agent scratch (`scratch/FU-*`, about 7.9 GB), the Digi-Key cache and the git worktrees. The run records are on `research/round2-results`, the plan on `research/round2`. This branch is never merged.
