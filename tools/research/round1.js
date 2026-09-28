export const meta = {
  name: 'rcbench-round1',
  description: 'One task of round 1 of the IO board component research (hardware/docs/Research.md)',
  whenToUse: 'Run by the research session with the arguments tools/research/session.py prepare writes; args.task is T1 to T6 or FU, args.mode plan counts agents only',
  phases: [
    { title: 'P0', detail: 'reachability, the parts database, the checkout' },
    { title: 'P1', detail: 'requirement critic, its critic, the re-check' },
    { title: 'P2-P4', detail: 'discover, search critic, re-rank, two verifiers' },
    { title: 'Refutations', detail: 'adjudicator, then a new verifier pair' },
    { title: 'P5-P6', detail: 'cross-category checks and completeness' },
    { title: 'P7', detail: 'the pages and their critic' },
  ],
}

// This script is the orchestration only. Each agent's instructions are the
// page hardware/docs/Research.md at the commit the session passes; the
// prompts name the row to follow. A workflow script has no file or git
// access: every return comes back in the result, and the session writes and
// commits it (tools/research/session.py record).

const A = args || {}
const TASK = A.task
const P = A.paths || {}
const S = A.schemas || {}
const CAP = A.cap || 32
const CATS = A.categories || {}
const ORDER = Object.keys(CATS)
const FU = A.followup || {}

function groupOf(task) {
  const t = (A.tasks || {})[task]
  return t ? t.categories : []
}

function fuPlan() {
  const n = (FU.categories || []).length
  if (FU.phases === 'P1') return 1 + 2 * n + 1
  if (FU.phases === 'P2-P4') return 1 + 5 * n
  if (FU.phases === 'P5-P6') return 1 + 4
  return 0
}

function plannedFor(task) {
  if (task === 'T1') return 1 + 2 * ORDER.length + 1
  if (task === 'T2' || task === 'T3' || task === 'T4') return 1 + 5 * groupOf(task).length
  if (task === 'T5') return 4
  if (task === 'T6') return 2
  if (task === 'FU') return fuPlan()
  return 0
}

const PLANNED = plannedFor(TASK)
if (!PLANNED) throw new Error(`unknown task ${TASK}`)
if (A.mode === 'plan' || PLANNED > CAP) {
  return { task: TASK, mode: 'plan', planned: PLANNED, cap: CAP, free: CAP - PLANNED, refused: PLANNED > CAP }
}
for (const k of ['commit', 'date']) if (!A[k]) throw new Error(`args.${k} missing`)

const readsStock = TASK === 'T2' || TASK === 'T3' || TASK === 'T4' || (TASK === 'FU' && FU.phases === 'P2-P4')
const isP1Task = TASK === 'T1' || (TASK === 'FU' && FU.phases === 'P1')

// ---------------------------------------------------------------- counting

const FREE = CAP - PLANNED
let extra = 0
let started = 0
const labels = new Map()
const returns = []
const followUps = []
const missing = []
const skipped = []

function takeExtra(n) {
  if (extra + n > FREE) return false
  extra += n
  return true
}

function unique(label) {
  const n = (labels.get(label) || 0) + 1
  labels.set(label, n)
  return n === 1 ? label : `${label}-${n}`
}

// Starts one agent. A planned agent always runs; an agent paid for from the
// free agents is started with paid = true. A restart takes one free agent.
// A null return is recorded as not checked, never as not refuted.
async function run(role, cat, base, phase, prompt) {
  const label = unique(base)
  for (let attempt = 0; attempt < 2; attempt++) {
    if (attempt > 0 && !takeExtra(1)) {
      followUps.push({ role, category: cat, label, reason: 'returned nothing; no free agent for a restart' })
      return null
    }
    started++
    let data = null
    try {
      data = await agent(prompt, { schema: S[role], label: attempt ? `${label}:restart` : label, phase })
    } catch (err) {
      missing.push({ role, category: cat || '', label, attempt, error: String(err) })
      continue
    }
    // A return for another category is not this category's return.
    if (data && cat && typeof data.category === 'string' && data.category !== cat) {
      missing.push({ role, category: cat, label, attempt, error: `returned for category ${data.category}` })
      continue
    }
    if (data) {
      returns.push({ role, category: cat || '', label, attempt, data })
      return data
    }
    missing.push({ role, category: cat || '', label, attempt })
  }
  followUps.push({ role, category: cat, label, reason: 'returned nothing twice' })
  return null
}

// ---------------------------------------------------------------- prompts

// The page's row for each role. Critics follow their phase's row.
const ROW = {
  'P1-critic': 'P1', 'P1-recheck': 'P1 re-check', rerank: 'Re-rank', adjudicator: 'P4',
  'P5-critic': 'P5', 'P6-critic': 'P6', 'P7-critic': 'P7',
}

function ctx(role, cat, label) {
  const row = ROW[role] || role
  const catLine = cat ? ` for category ${cat} (${CATS[cat]})` : ''
  const rows = cat
    ? `the row ${row} of the table under "Agent layout" and the row ${cat} under "Research categories"`
    : `the row ${row} of the table under "Agent layout"`
  return [
    `You are ${role}${catLine} in task ${TASK} of round 1 of the rcbench IO board component research.`,
    `Your instructions are hardware/docs/Research.md in the read-only checkout ${P.checkout} at commit ${A.commit}: ${rows}, the Sourcing rules, "Held parts" and "Lifecycle check". The requirement values are in hardware/docs/IOBoard.md in the same checkout and in the Answer columns of Research.md there, "Raised by P1" among them, and the owner's decisions in its Decision column; a proposed answer is not an answer. Where this prompt and the page differ, the page holds.`,
    `Today is ${A.date}. Every figure you report carries the time it was read and the URL or API call it came from. A value you could not read is written as not read, with the reason; never estimated. A search engine's summary is not a source.`,
    `Paths: the monostable page and the pages it links, at commit 23c82ca: ${P.monostable}. The parts database, sqlite, opened read-only only (sqlite3.connect('file:${P.db}?mode=ro', uri=True)): ${P.db}. Returns of earlier tasks: ${P.results}/hardware/research/round1/<run>/. The part each function keeps after refutations and follow-ups is in ${P.results}/hardware/research/round1/selection.json; it counts over any re-rank's rank 1 and over a superseded selection. Your scratch directory: ${P.scratch}/${A.run || TASK}/${label}/.`,
    `Clients: pages with \`python3 ${P.checkout}/tools/research/vendors.py fetch URL\` (--client safari for www.analog.com); JLCPCB stock with \`vendors.py jlcpcb C<digits>\` (the exact LCSC match); Digi-Key with \`vendors.py digikey MPN\` or \`vendors.py digikey-search KEYWORDS\`, with DIGIKEY_ENV_FILE=${P.digikey_env} in that command's environment. Never print a credential.`,
    `Write, commit and push nothing inside ${P.checkout} or ${P.results}. Return exactly the schema.`,
  ].join('\n')
}

const J = x => JSON.stringify(x)

function taskCats() {
  if (TASK === 'T1') return ORDER
  if (TASK === 'FU') return FU.categories || []
  return groupOf(TASK)
}

function itemsFor(cat) {
  return (FU.items || []).filter(i => !i.category || i.category === cat)
}

// Text compared by its words, whatever the spacing.
const words = t => String(t || '').split(/\s+/).filter(Boolean).join(' ')

// The questions committed P1 runs raised, all under "Raised by P1" before a
// P1 follow-up runs: a run does not raise one again.
function raisedFor(cat) {
  return (A.raised || []).filter(q => q.category === cat)
}

// The "P1 asks" items of the category's row and its lines that no question
// under "Raised by P1" names.
function asksFor(cat) {
  return ((A.p1_asks || {})[cat] || []).filter(n => !raisedFor(cat).some(q => words(q.asks) === words(n)))
}

// The fixed inputs of the Scope table in the category: the function each
// serves, by the table's input name, and its part. A candidate is the input
// when its part number starts with the table's (INA238AIDGSR for INA238).
const fixedFor = cat => (A.fixed_inputs || {})[cat] || []
const isFixed = (fixed, part) => !!fixed && String(part || '').toUpperCase().startsWith(String(fixed).toUpperCase())
const fixedNames = cat => fixedFor(cat).map(x => `${x.function} for the ${x.part}`).join('; ')

function p0Prompt() {
  const j = A.jlcparts || {}
  return `${ctx('P0', '', 'P0')}

Probe every host below with its client and record one row per host, copying the host name and the client name exactly. In url record the URL fetched, which is the row's probe where it has one, or a page on the host itself where it has none; for an API client, the command of the clients table run with the probe (${JSON.stringify(A.clients || {})}). A row with another client, URL or command counts as not probed. A marker is a regular expression the lifecycle status matches; record what it matched. Where a row's probe is null, find a product page of one of that maker's seeds (the seeds are in the category rows and the row's note) and record its URL, and in status_marker the lifecycle status as the page body states it, or none where the body states none. The fields hold, hold_in_t1 and stop are for the script; report reachability only, and do not decide holds from them.
${J(A.hosts)}

Check the parts database: the SHA-256 of ${P.db} is ${j.sha256}; jlc_components holds ${j.rows} rows; every one of these LCSC numbers is in it: ${(j.lcsc || []).join(', ')}. Record in snapshot the created time of ${j.manifest}; it should be ${j.manifest_created}.
Check that ${P.monostable}/hardware/docs/Monostable.md exists and that \`git -C ${P.checkout} cat-file -e 23c82ca6ca976d956098cfafd25dbefa11584f5d\` succeeds.
Record \`git -C ${P.checkout} rev-parse HEAD\` as checkout_head.
Record the versions of Claude Code, git, python3, ruff, 7-Zip and curl_cffi.

This is task ${TASK}. It ${readsStock ? 'reads stock' : 'reads no stock'}. Set stop and list held categories as the P0 row gives them for this task, with the reason.`
}

function p1Prompt(cat) {
  const items = TASK === 'FU' ? itemsFor(cat) : []
  const asks = asksFor(cat)
  return `${ctx('P1', cat, `P1-${cat}`)}

Read the ${cat} row on the page and every line of hardware/docs/IOBoard.md that belongs to it (search for "${cat}" and for the functions the row names), and every source they cite. Try to refute each value: does it follow from its source, is the unit right, is it an owner decision or an assumption. Mark each. Every value you mark assumption is also a question to the owner; give each value the index of the question that asks for it in "question" (-1 if none), and give that question the value's where in for_where and its quantity in for_quantity. List in functions every function the row and those lines name, one short name each${fixedFor(cat).length ? `, the function of each fixed input of the Scope table by its exact name: ${fixedNames(cat)}` : ''}. List every requirement value P2 needs to qualify a part that neither page states, as a question to the owner; and every "P1 asks" in the row or in those lines${asks.length ? `, among them these, each question giving its item's name in asks: ${asks.join('; ')}` : ''}. ${cat === 'R5' ? 'The supply currents of the parts T2 and T4 select are inputs R5 takes from those tasks, not questions. ' : ''}A question whose value only feeds Q4, Q8 or Q9, in a category that decision's "Reported by" names, is blocks "decision-only" with that decision. A question already under "Raised by P1" is not raised again.${items.length ? `\n\nThis is a follow-up task for these gaps: ${J(items)}\nA question that raises one of them copies its question word for word.` : ''}`
}

function criticPrompt(cat, p1) {
  const items = TASK === 'FU' ? itemsFor(cat) : []
  const asks = asksFor(cat)
  return `${ctx('P1-critic', cat, `P1-critic-${cat}`)}

You are the critic of P1 for ${cat}. Re-derive each marking and each question below from the sources yourself. Rule on every marking once by its 0-based index in values, copying its where and quantity exactly: holds with correct_marking unchanged, or wrong with the correct marking. Rule on every question once by its 0-based index (confirmed or rejected); a question you give no verdict is sent to a follow-up task, not to the owner. Give every verdict its reason; a verdict without one is no verdict. A marking you correct to assumption is also added as a question, with that value's where in for_where and its quantity in for_quantity. Then look for values P1 did not mark and for missing values P1 did not list, and add each as a question.${asks.length ? ` The page makes each of these "P1 asks" items a question to the owner: ${asks.join('; ')}. Add as a question each one that no question of P1 names in asks, giving its name in asks; one left without a confirmed question is sent to a follow-up task.` : ''} List in functions_missing every function the row or its lines name that P1's functions lacks.${items.length ? `\n\nThis is a follow-up task for these gaps. Check that P1's return deals with each, and add as a question a value one of them asks for that P1 did not raise, copying its question word for word: ${J(items)}` : ''}

P1's return:
${J(p1)}`
}

function recheckPrompt(added) {
  return `${ctx('P1-recheck', '', 'P1-recheck')}

The P1 critics added the questions below. P1 did not raise them. Re-derive each from the same sources and confirm or reject it, citing the evidence; a verdict without evidence is no verdict. Give a verdict on every one, by its category and its 0-based index in that category's list.
${J(added)}`
}

function p2Prompt(cat) {
  const t3 = ['R5', 'R6', 'R7', 'R8'].includes(cat)
  const items = TASK === 'FU' ? itemsFor(cat) : []
  const named = [...new Set([...((A.inventory || {})[cat] || []), ...fixedFor(cat).map(x => x.function)])]
  return `${ctx('P2', cat, `P2-${cat}`)}

Find each value the owner marked "for research" under "Raised by P1" for ${cat} at its primary source, and record it as found. Find candidates as Sourcing rule 3 sets out and keep those from allowlisted makers (rule 2). Drop those that miss a requirement value. Return an entry for every function the ${cat} row and its lines in hardware/docs/IOBoard.md name${named.length ? `, among them these, by these exact names: ${named.join('; ')}` : ''}.${fixedFor(cat).length ? ` The Scope table fixes these inputs: ${fixedNames(cat)}. Shortlist each for its function with its full record, also where it misses a requirement or a gate; a fixed input that fails a check is reported to the owner, not replaced.` : ''} Give each requirement of a function a name of its own. For up to five survivors per function, record every field of the P2 row, and the rule-5 route in second_source_route and second_source_part; a part whose route is alternate needs a full record for that alternate among the survivors or in the re-rank. List in "report", one figure each, the figures the ${cat} row asks the category to report for a decision or for P5${((A.required_reports || {})[cat] || []).length ? `, among them these by these names: ${((A.required_reports || {})[cat]).join('; ')}` : ''}.${((A.per_part_reports || {})[cat] || []).length ? ` Report these once for each shortlisted part, named "NAME: PART", with "not applicable: REASON" as the value where one does not concern the part: ${((A.per_part_reports || {})[cat]).join('; ')}.` : ''}${t3 ? ` ${cat === 'R5' ? 'R5 sizes the 3.3 V logic buck and the 5 V rail from the supply currents of the parts T2 and T4 selected, as P4 verified them, plus the display\'s draw.' : ''} The parts earlier tasks selected are in ${P.results}/hardware/research/round1/selection.json; the returns in each run's directory carry their figures.` : ''}${cat === 'R3' && ((A.decisions || {}).Q9 || '').trim() ? `\n\nThe owner decided Q9: ${A.decisions.Q9}. A part it adds or changes is a function of R3 in this run, found and qualified as the others.` : ''}${items.length ? `\n\nThis is a follow-up task. Its items for ${cat}: ${J(items)}` : ''}${TASK === 'FU' ? ` Name each function exactly as ${P.results}/hardware/research/round1/selection.json names it for ${cat}.` : ''}`
}

function p2View(p2) {
  return {
    category: p2.category,
    functions: (p2.functions || []).map(f => ({
      function: f.function, requirements: f.requirements, dropped: f.dropped,
      shortlist: (f.shortlist || []).map(c => ({ rank: c.rank, part: c.part, maker: c.maker, lcsc: c.lcsc, reason: c.reason })),
    })),
  }
}

function p3Prompt(cat, p2) {
  return `${ctx('P3', cat, `P3-${cat}`)}

Search for part families P2 did not consider, from the same allowlist, and re-read each reason P2 gave for dropping a candidate. Name each function exactly as P2 does. List in missed_functions each function the ${cat} row or its lines in hardware/docs/IOBoard.md name that P2 returned no entry for. P2's shortlist and drops (its full records are with the session):
${J(p2View(p2))}`
}

function rerankPrompt(cat, p2, p3) {
  return `${ctx('rerank', cat, `rerank-${cat}`)}

You are the re-rank of the "Agent layout" table. Qualify each candidate P3 returned as P2 does, and give each qualified one a full record in new_candidates. Then rank each function's shortlist: every entry of ranking names a part of P2's shortlist or of new_candidates, by its exact part number. Rank every such part, or list it with its reason in dropped_from_shortlist (a P2 candidate) or dropped_from_p3 (a P3 candidate).${fixedFor(cat).length ? ` Rank each fixed input of the Scope table first for its function: ${fixedNames(cat)}; one that fails a check is reported to the owner, not replaced by another part.` : ''} In verify, name the parts P4 must verify: the first-ranked part, the sourcing-rule-5 alternate where the second source is an alternate, and in R10 and R12 the first-ranked part of each Q4 or Q8 alternative. List your own figures in report${((A.per_part_reports || {})[cat] || []).length ? `, and for each part you rank, name in verify or give a record in new_candidates, a rule-5 alternate's record among them, that P2's report lacks, these figures named "NAME: PART": ${((A.per_part_reports || {})[cat]).join('; ')}` : ''}. ${cat === 'R10' || cat === 'R12' ? `Set decision to ${cat === 'R10' ? 'Q4 on the function that implements Q4' : 'Q8 on the function that implements Q8, the non-volatile store,'} and none on the others; its alternatives are the q-alternatives in its verify. Give each q-alternative its option, and the function its kept_option for the first-ranked part, from these classes, one part at least for each: ${((A.q_options || {})[cat === 'R10' ? 'Q4' : 'Q8'] || []).join('; ')}.` : 'Set decision to none on every function.'} A rule-5 alternate that is not on P2's shortlist needs a full record in new_candidates; it is kept as the alternate's record, not ranked. The script builds the final shortlist from P2's records and yours.

P2's return:
${J(p2)}

P3's return:
${J(p3)}`
}

function p4Prompt(cat, kind, bundle, only) {
  const what = only
    ? `Verify only this candidate, which replaces a refuted one, and, when its second_source_route is alternate, the part named in its second_source_part (kind alternate); list only those: ${J(only)}`
    : 'Verify, for each function, the first-ranked part, its rule-5 alternate when its second_source_route is alternate (the part in its second_source_part, kind alternate), every part in its verify list, and the rule-5 alternate of each part of kind q-alternative in that list, in the same way (kind alternate). An alternate several parts name is checked against the first of them: the first-ranked part, then the q-alternatives in order. A part verified in its own right that is also such an alternate is listed once in that function, with the checks of an alternate as well. List every part you verify in parts, once for each function you verify it for, under that function\'s name: a part verified for two functions has a row under each. A part you leave out counts as not verified.'
  const how = kind === 'stock'
    ? 'You are the stock and lifecycle verifier. Re-read stock and lifecycle at the primary sources, with the clients above, and try to refute each reading. Return figures empty: the datasheet verifier rules on figures_to_check.'
    : `You are the datasheet and pin verifier. Re-read every requirement value in the datasheet; for an alternate, the pin-for-pin match and the functional match to the part it stands in for.${only ? '' : ' Re-read each item of figures_to_check below (the reported figures, the values found for research, the reasons the re-rank gave for dropping a P3 candidate, P2\'s own drops, for "Q option: FUNCTION: PART: CLASS" whether the part belongs to that option class of Q4 or Q8, and for "function requirements: FUNCTION" whether the function\'s requirement list names every requirement IOBoard.md and the answers set for it, with no value weaker than theirs) and give each a verdict in figures under its exact name; an item without a verdict counts as not verified.'} Try to refute each.`
  return `${ctx('P4', cat, `P4-${kind}-${cat}`)}

${how} ${what} Copy each function and part name exactly as the shortlist below writes it. Name your checks exactly: the stock verifier gives "stock" (the gate's reading, at JLCPCB for a part on the board, at Digi-Key for a part off it), "presale" (JLCPCB, for a part with an LCSC number), "lifecycle status", and "second-vendor stock" (Digi-Key, against the rule-4 gate) for a part on the board whose second_source_route is second-vendor, other than a part of kind alternate; the datasheet verifier gives one check per entry of the candidate's requirements, named as that entry is, and for a part of kind alternate also "pin-for-pin match" and "functional match". The stock verifier also gives the lifecycle table's readings: "longevity commitment" (the programme page's commitment, or that none is published; passes unless S5 makes it a gate it fails), "market introduction" (the first datasheet revision date; passes, and under 12 months is stated in read), "distributor status" (JLCPCB, LCSC and Digi-Key; passes, a disagreement with the maker stated in read) and "lead time" (the manufacturer's lead time in Digi-Key's API; passes). For a part on the board it gives "library type" (componentLibraryType of the exact JLCPCB row, basic or extended; passes, S6 allowing both) and "LCSC identity" (the exact JLCPCB result for the LCSC number names the candidate's part number and package; passes only then). It also gives "placements" (the placements per board as the specification fixes them, or the top of the range P2 states with its basis; passes when the candidate's count is at least that) and "board placement" (whether the specification places the part on the IO board or off it, rule 1; passes when its LCSC number says the same: C and digits on the board, none off it), and the datasheet verifier "manufacturer allowlist" (passes when the datasheet's manufacturer is allowed by S1, S2 or S9 for the part, rule 2). Each check has read_at, the time it was read, agrees (the value read matches the value stated) and passes (the value read meets its requirement, or the rule-4, rule-5 or lifecycle gate passes); the stock verifier also gives "end-of-life notices" (passes when no end-of-life or last-time-buy notice exists). For a part on the board whose held is above 0, "held quantity" (the quantity and date the owner states under Held parts, against boards × placements per board, rule 6) may stand in place of "stock" and "presale". A confirmation without its required checks counts as not verified, and one whose refutation is not empty counts as a refutation. A check with passes false counts as a refutation, and so does one with agrees false, except for a reading that moves or always passes (${[...MOVING].join(', ')}) and a requirement stated as not given.

The shortlist, the values found for research and the reports:
${J(bundle)}`
}

function adjudicatorPrompt(cat, fn, part, evidence) {
  return `${ctx('adjudicator', cat, `adjudicator-${cat}`)}

You are the adjudicating critic of the P4 row. A verifier refuted ${part ? `${part} for the function "${fn}"` : `the figure "${fn}", which states what its claim below holds`}. Re-read the evidence yourself at its sources and rule on every refutation below: stands is true when any of them holds. Copy the function "${fn}" and the part "${part}" exactly into your ruling.
${J(evidence)}`
}

function p5Prompt() {
  return `${ctx('P5', '', 'P5')}

Read every return under ${P.results}/hardware/research/round1/ (T2, T3, T4 and every FU-* directory) and run the checks of the P5 row over the whole board. The parts to check are those in ${P.results}/hardware/research/round1/selection.json, each with its alternate, and both alternatives of Q4 and Q8.${(A.p5_budgets || []).length ? ` Return one budget for each of these, its item named as listed or "NAME: DETAIL" where it has several, each with its source and reading time; a check the P5 row makes conditional (${(A.p5_conditional || []).join('; ')}) is returned with "not applicable: REASON" as its value when its condition does not hold: ${A.p5_budgets.join('; ')}.` : ''}`
}
function p5CriticPrompt(p5) {
  return `${ctx('P5-critic', '', 'P5-critic')}

You are the critic of P5. Re-derive each conflict, each combination, each budget and each assumption below by its 0-based index and give every one a verdict (conflict_verdicts as verdicts, combination_verdicts, budget_verdicts, assumption_verdicts); an assumption holds when the tree does not state the value and the value P5 assumed is sourced and reasonable. Add conflicts P5 missed. P5's return:
${J(p5)}`
}
function p6Prompt() {
  return `${ctx('P6', '', 'P6')}

Run the checks of the P6 row: every line of hardware/docs/IOBoard.md has a part or is marked "not round 1"; every figure in ${P.results}/hardware/research/round1/ has a date and a source; every question under "Raised by P1" is answered; every value marked for research is found.`
}
function p6CriticPrompt(p6) {
  return `${ctx('P6-critic', '', 'P6-critic')}

You are the critic of P6. Re-read the specification line by line against the evidence and give every gap below a verdict by its 0-based index; add the gaps P6 did not report. P6's return:
${J(p6)}`
}
function p7Prompt() {
  return `${ctx('P7', '', 'P7')}

Exception to the rule above: write the outputs listed under "Outputs" of Research.md into the working tree ${P.results}, which the session has merged with research/round1 before this task. List every file you write in files, repository-relative, and the page of each category group (A, B, C) in group_pages. Required among them: ${(A.t6_outputs || []).join(', ')}. Write them from the returns under ${P.results}/hardware/research/round1/ only, the parts in hardware/research/round1/selection.json, P5's budget and combinations, and the owner's decisions in the "Decision (owner, date)" column for Q4, Q8 and Q9. P5's budget and combinations are those of the last P5/P6 check, ${A.last_p56 || 'T5'}, as its critic upheld them. Do not commit. Write tools/jlc_stock.py to read DIGIKEY_ENV_FILE as well as the two variables, as tools/research/vendors.py does. Run \`python3 tools/check_docs.py\`, \`ruff check tools/\` and \`DIGIKEY_ENV_FILE=${P.digikey_env} python3 tools/jlc_stock.py --check 5\` in ${P.results} and report each as passed or not with its last lines.`
}
function p7CriticPrompt(p7) {
  return `${ctx('P7-critic', '', 'P7-critic')}

You are the critic of P7. Exception to the rule above: you may correct the pages in ${P.results}; do not commit. figure_checks and sentence_issues describe the pages as they stand after your corrections; list every figure you checked. List in reviewed every file you checked, repository-relative; every file P7 listed is checked. Check every figure and every stated combination on the pages P7 wrote against the returns under ${P.results}/hardware/research/round1/, and every sentence against the writing rules in CONTRIBUTING.md. Apply the corrections, then run the three checks P7 ran on the tree you leave and report each as passed or not with its last lines. P7's return:
${J(p7)}`
}

// ---------------------------------------------------------------- P0

// P0 has no critic, so the script applies the stop and hold rules itself
// from P0's check results and the host table, beside P0's own reading.
// A reading time is a date the parser reads, not any nonblank text, on a day
// the calendar has: the parser turns 2026-02-31 into March 3.
function isTime(text) {
  const t = String(text || '').trim()
  const day = t.slice(0, 10)
  return /^\d{4}-\d{2}-\d{2}/.test(t) && Number.isFinite(Date.parse(t)) && new Date(Date.parse(day)).toISOString().slice(0, 10) === day
}

// A reading of this task: taken on or after the day the session prepared it,
// not copied from an earlier return or the parts database, and no later than
// the next day, for a run that passes midnight. The script has no clock.
function readInRun(text) {
  const t = Date.parse(String(text).trim())
  return isTime(text) && t >= Date.parse(A.date) && t < Date.parse(A.date) + 2 * 864e5
}

// A value that holds no reading: blank, a bare placeholder or written as not
// read. "none" is a reading: no notice, no commitment, no resistor.
function unread(text) {
  const t = String(text || '').trim()
  return !t || /^(n\/a|-)$/i.test(t) || /^not read\b/i.test(t)
}

// A source, evidence or refutation written as none holds none.
function readsNone(text) {
  return unread(text) || /^none$/i.test(String(text || '').trim())
}

// A refutation that says there is none: the schema leaves it empty.
function noRefutation(text) {
  return readsNone(text) || /^(no refutations?|not refuted|(none|nothing) found)\b/i.test(String(text || '').trim())
}

// A value found, reported or budgeted that says it was not found holds none.
function notFound(text) {
  return unread(text) || /^(not (found|stated|known)|unknown)\b/i.test(String(text || '').trim())
}

// P0's reading of one host: reachable only when its HTTP status agrees, and
// its lifecycle status read only when the text matches the host's marker. A
// row taken with another client, or not at the host's fixed probe, is not a
// reading of that host.
function probedAsTold(h, row) {
  if (row.client !== h.client) return false
  const url = String(row.url || '').trim()
  // The command compares without its environment (env, NAME=value), its
  // interpreter's path and the script's path.
  const words = t => String(t).split(/\s+/).filter(Boolean).join(' ').replace(/^(env )?([A-Za-z_]\w*=\S* )*(\S*\/)?python3 /, '').replace(/^\S*vendors\.py/, 'vendors.py')
  // An API client runs its command with the probe; a page client fetches
  // the probe URL, or, with no probe, a page on the host itself.
  const command = (A.clients || {})[h.client]
  if (h.probe != null && /-api$/.test(h.client) && command) return words(url) === words(command.replace(/\b(LCSC|MPN)\b/, h.probe))
  if (h.probe != null) return url.replace(/\/$/, '') === String(h.probe).replace(/\/$/, '')
  const m = /^https?:\/\/([^/:?#]+)/i.exec(url)
  const bare = String(h.host).replace(/^www\./, '')
  return !!m && (m[1] === h.host || m[1] === bare || m[1].endsWith(`.${bare}`))
}

function hostReading(h, row) {
  if (!row) return { up: undefined, status: false }
  if (!probedAsTold(h, row)) return { up: undefined, status: false, why: `not probed with ${h.client}${h.probe == null ? '' : ` at ${h.probe}`}` }
  const ok = row.reachable === true && row.http_status >= 200 && row.http_status < 300
  // A yes/no or absence answer states no status.
  const text = String(row.status_marker || '').trim()
  let status = !readsNone(text) && !/^(no|false)$/i.test(text) && !/^(absent|none found|not (in|found|present))\b|\bnot in (the )?page body\b/i.test(text)
  if (status && h.marker) {
    try { status = new RegExp(h.marker).test(row.status_marker) } catch (e) { status = false }
  }
  return { up: row.reachable === true && !ok ? false : row.reachable, status: ok && status }
}

function applyP0(p0) {
  const j = A.jlcparts || {}
  const reasons = []
  if (!p0.jlcparts || !p0.jlcparts.sha256_ok || p0.jlcparts.sha256 !== j.sha256) reasons.push('the parts database fails its SHA-256 check')
  if (!p0.jlcparts || p0.jlcparts.rows !== j.rows) reasons.push('the parts database row count differs')
  if (!p0.jlcparts || (p0.jlcparts.missing_lcsc || []).length) reasons.push('LCSC numbers the page names are missing from the parts database')
  if (!(Date.parse(p0.snapshot) === Date.parse(j.manifest_created))) reasons.push(`the parts database's manifest is ${p0.snapshot}, not ${j.manifest_created}`)
  if (p0.checkout_head !== A.commit) reasons.push(`the checkout is at ${p0.checkout_head}, not ${A.commit}`)
  if (TASK === 'T1' && !(p0.monostable && p0.monostable.fetched)) reasons.push('commit 23c82ca is not in place')
  // A host given more than one row has no single reading: not probed.
  const rows = new Map()
  const twice = new Set()
  for (const r of p0.hosts || []) { if (rows.has(r.host)) twice.add(r.host); else rows.set(r.host, r) }
  for (const h of twice) { rows.delete(h); followUps.push({ role: 'P0', host: h, reason: 'more than one row for the host; counted as not probed' }) }
  const held = new Map()
  for (const h of A.hosts || []) {
    const { up, status, why: off } = hostReading(h, rows.get(h.host))
    // A maker page that answers without its lifecycle status is reachable
    // with the status not read: listed for the categories it serves.
    if (up === true && (h.marker || h.probe === null) && (h.hold || []).length) {
      if (!status) followUps.push({ role: 'P0', host: h.host, categories: h.hold, reason: 'lifecycle status not read from the page' })
      continue
    }
    if (up === true) continue
    const why = up === false ? 'unreachable' : (off || 'not probed')
    if (h.stop === 'stock-tasks' && readsStock) reasons.push(`${h.host} is ${why}`)
    for (const c of (isP1Task ? h.hold_in_t1 : h.hold) || []) if (!held.has(c)) held.set(c, { category: c, host: h.host, reason: why })
  }
  // P0's own stop and held fields are its reading, kept in its return; the
  // outcome comes from the check results above only.
  return { stop: reasons.length > 0, reasons, held: [...held.values()] }
}

async function phaseP0() {
  phase('P0')
  const p0 = await run('P0', '', 'P0', 'P0', p0Prompt())
  if (!p0) return { stop: true, reasons: ['P0 returned nothing'], held: [] }
  return applyP0(p0)
}

// ---------------------------------------------------------------- P1

// A verdict list keyed by `key`: a key given more than once counts as not
// ruled, and is listed.
function uniqueVerdicts(list, key, where) {
  const count = new Map()
  for (const v of list || []) count.set(key(v), (count.get(key(v)) || 0) + 1)
  const out = new Map()
  for (const v of list || []) {
    if (count.get(key(v)) > 1) continue
    out.set(key(v), v)
  }
  for (const [k, n] of count) if (n > 1) followUps.push({ ...where, key: k, reason: 'more than one verdict on one item; counted as not ruled' })
  return out
}

// A verdict whose reason or evidence reads as none rules on nothing.
const reasoned = (list, field) => (list || []).filter(v => !readsNone(v[field]))

// A value, and the question that asks for it, by its where and quantity.
const at = (where, quantity) => `${where}\u0000${quantity}`

async function phaseP1(cats) {
  phase('P1')
  const chains = await pipeline(
    cats,
    cat => run('P1', cat, `P1-${cat}`, 'P1', p1Prompt(cat)),
    (p1, cat) => p1
      ? run('P1-critic', cat, `P1-critic-${cat}`, 'P1', criticPrompt(cat, p1)).then(critic => ({ cat, p1, critic }))
      : { cat, p1: null, critic: null },
  )
  const byCat = new Map()
  cats.forEach((cat, i) => {
    const c = chains[i]
    if (!c) followUps.push({ role: 'P1', category: cat, reason: 'the P1 chain failed' })
    else byCat.set(cat, c)
  })
  // Every marking needs the critic's verdict. An assumption is a question to
  // the owner: one that neither P1 nor the critic asked is added here, and
  // the re-check checks it with the critic's additions. A marking ruled on
  // in contradiction (holds with a new marking, or wrong with none) counts as
  // an assumption.
  for (const c of byCat.values()) {
    // Every row states values: a P1 that returns none marked and checked
    // nothing.
    if (c.p1 && !(c.p1.values || []).length) followUps.push({ role: 'P1', category: c.cat, reason: 'P1 returned no value; nothing was marked' })
    if (!c.p1 || !c.critic) continue
    // A marking verdict names its value by index, where and quantity: two
    // values of one line may share a quantity.
    const mv = uniqueVerdicts(reasoned(c.critic.marking_verdicts, 'reason'), x => `${x.index}\u0000${at(x.where, x.quantity)}`, { role: 'P1-critic', category: c.cat })
    c.ruledQ = uniqueVerdicts(reasoned(c.critic.question_verdicts, 'reason'), x => x.index, { role: 'P1-critic', category: c.cat })
    c.assumptions = []
    c.extra = []
    // A P1 question asks for one value, and a critic addition covers one
    // value by its where and quantity: an assumption that shares either is
    // not asked. One under "Raised by P1" for its where and quantity has
    // asked for it already, and is not raised again.
    const claimed = new Set()
    const criticAt = new Map()
    const raisedAt = new Map()
    for (const q of c.critic.added || []) if (q.for_where) criticAt.set(at(q.for_where, q.for_quantity), (criticAt.get(at(q.for_where, q.for_quantity)) || 0) + 1)
    for (const q of raisedFor(c.cat)) if (q.for_where) raisedAt.set(at(q.for_where, q.for_quantity), (raisedAt.get(at(q.for_where, q.for_quantity)) || 0) + 1)
    for (const [i, v] of (c.p1.values || []).entries()) {
      const m = mv.get(`${i}\u0000${at(v.where, v.quantity)}`)
      let marking = v.marking
      if (!m) followUps.push({ role: 'P1-critic', category: c.cat, value: v, reason: 'marking not ruled on' })
      else if (m.verdict === 'holds' && m.correct_marking !== 'unchanged' && m.correct_marking !== v.marking) { marking = 'assumption'; followUps.push({ role: 'P1-critic', category: c.cat, value: v, reason: 'marking holds with a new marking; taken as an assumption', notice: true }) }
      else if (m.verdict === 'wrong' && (m.correct_marking === 'unchanged' || m.correct_marking === v.marking)) { marking = 'assumption'; followUps.push({ role: 'P1-critic', category: c.cat, value: v, reason: 'marking wrong with no new marking; taken as an assumption', notice: true }) }
      else if (m.verdict === 'wrong') marking = m.correct_marking
      if (marking !== 'assumption') continue
      const k = at(v.where, v.quantity)
      if ((raisedAt.get(k) || 0) > 0) { raisedAt.set(k, raisedAt.get(k) - 1); continue }
      const q = v.question >= 0 ? (c.p1.questions || [])[v.question] : null
      const ownQ = !!q && (c.ruledQ.get(v.question) || {}).verdict !== 'rejected' && !claimed.has(v.question) && at(q.for_where, q.for_quantity) === k
      if (ownQ) { claimed.add(v.question); c.assumptions.push({ ...v, asked: 'own' }); continue }
      c.assumptions.push({ ...v, asked: 'addition' })
      if ((criticAt.get(k) || 0) > 0) { criticAt.set(k, criticAt.get(k) - 1); continue }
      c.extra.push({ function: v.quantity, question: `State ${v.quantity}. P1 read "${v.value}" at ${v.where} and it is marked an assumption.`, why: 'every assumption is a question to the owner (P1 row)', blocks: 'p2', decision: 'none', for_where: v.where, for_quantity: v.quantity, asks: '', synthetic: true })
    }
  }
  const added = []
  for (const c of byCat.values()) {
    if (c.critic) [...(c.critic.added || []), ...(c.extra || [])].forEach((q, index) => added.push({ ...q, category: c.cat, index }))
  }
  let recheck = { verdicts: [] }
  if (added.length) recheck = await run('P1-recheck', '', 'P1-recheck', 'P1', recheckPrompt(added))
  else skipped.push({ role: 'P1-recheck', reason: 'no question was added, so none needed a re-check' })
  const rv = recheck ? uniqueVerdicts(reasoned(recheck.verdicts, 'evidence'), x => `${x.category}\u0000${x.index}`, { role: 'P1-recheck' }) : new Map()
  // Questions reach the owner only when checked: P1's by its critic, a
  // critic's addition by the re-check. Unchecked ones go to a follow-up.
  const questions = []
  for (const cat of cats) {
    const c = byCat.get(cat)
    if (!c || !c.p1) continue
    if (!c.critic) {
      if ((c.p1.questions || []).length) followUps.push({ role: 'P1-critic', category: cat, reason: 'P1 questions not checked' })
      continue
    }
    const confirmedIdx = new Set()
    const confirmedAt = new Map()
    ;(c.p1.questions || []).forEach((q, i) => {
      const v = c.ruledQ.get(i)
      if (v && v.verdict === 'confirmed') {
        questions.push({ ...q, category: cat, source: 'P1' })
        confirmedIdx.add(i)
      } else if (!v) followUps.push({ role: 'P1-critic', category: cat, index: i, question: q.question, for_where: q.for_where, for_quantity: q.for_quantity, reason: 'P1 question not ruled on' })
    })
    ;[...(c.critic.added || []), ...(c.extra || [])].forEach((q, i) => {
      if (!recheck) { followUps.push({ role: 'P1-recheck', category: cat, index: i, question: q.question, for_where: q.for_where, for_quantity: q.for_quantity, reason: 'critic addition not checked' }); return }
      const v = rv.get(`${cat}\u0000${i}`)
      if (!v) followUps.push({ role: 'P1-recheck', category: cat, index: i, question: q.question, for_where: q.for_where, for_quantity: q.for_quantity, reason: 'critic addition not ruled on' })
      else if (v.verdict === 'confirmed') {
        questions.push({ ...q, category: cat, source: 'P1 critic, re-checked' })
        if (q.for_where) confirmedAt.set(at(q.for_where, q.for_quantity), (confirmedAt.get(at(q.for_where, q.for_quantity)) || 0) + 1)
      }
    })
    // Every assumption ends with a confirmed question of its own, or is
    // listed: its own P1 question, or one confirmed addition for its where
    // and quantity.
    for (const a of c.assumptions) {
      if (a.asked === 'own' && confirmedIdx.has(a.question)) continue
      const k = at(a.where, a.quantity)
      if ((confirmedAt.get(k) || 0) > 0) { confirmedAt.set(k, confirmedAt.get(k) - 1); continue }
      const { asked, ...value } = a
      followUps.push({ role: 'P1', category: cat, value, reason: 'assumption without a confirmed question' })
    }
    // Each "P1 asks" item not yet under "Raised by P1" ends with a confirmed
    // question that names it, or is listed: a rejected one as well.
    for (const name of asksFor(cat)) {
      if (!questions.some(q => q.category === cat && words(q.asks) === words(name))) followUps.push({ role: 'P1', category: cat, asks: name, reason: '"P1 asks" item without a confirmed question' })
    }
  }
  // A P1 follow-up deals with each of its items, or lists it again: a value
  // item needs the value in this run's P1 return, a question item the
  // question in P1's questions, the critic's additions or under "Raised by
  // P1" (by its words, or by its where and quantity), any other item P1 and
  // its critic returning. What the run then leaves open it lists as its own;
  // an item listed again is judged by the item it carries.
  const inner = x => x.item ? inner(x.item) : x
  if (TASK === 'FU') for (const cat of cats) {
    const c = byCat.get(cat) || {}
    const raisedQ = [...((c.p1 || {}).questions || []), ...((c.critic || {}).added || []), ...(c.extra || []), ...raisedFor(cat)]
    for (const item of itemsFor(cat)) {
      const it = inner(item)
      const done = it.value ? ((c.p1 || {}).values || []).some(v => at(v.where, v.quantity) === at(it.value.where, it.value.quantity))
        : it.question ? raisedQ.some(q => words(q.question) === words(it.question) || (!!it.for_where && at(q.for_where, q.for_quantity) === at(it.for_where, it.for_quantity)))
          : !!(c.p1 && c.critic)
      if (!done) followUps.push({ role: 'P1', category: cat, item, reason: 'follow-up item this run did not deal with' })
    }
  }
  // A question feeds a decision only when it names Q4, Q8 or Q9 and its
  // category is one the decision's Reported by names (Decided table); any
  // other blocks P2 until answered.
  for (const q of questions) if (q.blocks === 'decision-only' && !((A.decision_categories || {})[q.decision] || []).includes(q.category)) {
    followUps.push({ role: 'P1', category: q.category, question: q.question, reason: `decision-only question names no decision ${q.category} reports; published as blocking P2`, notice: true })
    q.blocks = 'p2'
  }
  questions.forEach((q, i) => { q.id = `V${(A.first_v || 1) + i}` })
  // The functions each category names, as P1 and its critic read them:
  // the inventory P2 answers for in later runs.
  const inventory = {}
  for (const cat of cats) {
    const c = byCat.get(cat)
    if (!c || !c.p1 || !c.critic) continue
    inventory[cat] = [...new Set([...(c.p1.functions || []), ...(c.critic.functions_missing || [])].map(n => String(n).trim()).filter(Boolean))]
    // Every row names functions: an empty inventory is a check that did
    // not run, and would leave P2's returns unchecked against it.
    if (!inventory[cat].length) followUps.push({ role: 'P1', category: cat, reason: 'no function inventory from P1 and its critic' })
  }
  return { questions, inventory }
}

// ---------------------------------------------------------------- P2 to P4

// The final shortlist: the re-rank's order over P2's records and the
// re-rank's records of P3's finds.
// Ranks are positions: the whole numbers 1 to n, each given once.
function distinctRanks(list) {
  const ranks = list.map(r => r.rank).sort((a, b) => a - b)
  return ranks.every((r, i) => r === i + 1)
}

function merge(cat, p2, rr, p3) {
  // Every candidate P3 found, and every P2 exclusion P3 overturned, is
  // qualified, ranked or dropped by the re-rank.
  const handled = (fr, part) => fr && ([...(fr.new_candidates || []), ...(fr.dropped_from_p3 || []), ...(fr.dropped_from_shortlist || [])].some(c => c.part === part) || (fr.ranking || []).some(r => r.part === part))
  // The functions are P2's. A find with no owner is handled by any of them.
  const names = (p2.functions || []).map(f => f.function)
  const handledAny = part => (rr.functions || []).some(fr => names.includes(fr.function) && handled(fr, part))
  // A P3 find the re-rank neither qualified nor dropped leaves its function
  // open: the function P3 names, the function whose P2 drop P3 overturned,
  // or every function when it names or dropped under none of P2's.
  const unhandled = new Set()
  for (const m of (p3 && p3.missed) || []) {
    const owned = names.includes(m.function)
    if (owned ? handled((rr.functions || []).find(f => f.function === m.function), m.part) : handledAny(m.part)) continue
    followUps.push({ role: 'rerank', category: cat, function: m.function, part: m.part, reason: 'P3 candidate neither qualified nor dropped by the re-rank' })
    for (const n of owned ? [m.function] : names) unhandled.add(n)
  }
  for (const x of (p3 && p3.exclusions_not_holding) || []) {
    // Each function whose P2 drop P3 overturned handles the part itself;
    // with no owner, any function's handling counts.
    const owners = (p2.functions || []).filter(f => (f.dropped || []).some(d => d.part === x.part)).map(f => f.function)
    const open = owners.length
      ? owners.filter(n => !handled((rr.functions || []).find(fr => fr.function === n), x.part))
      : (handledAny(x.part) ? [] : names)
    if (!open.length) continue
    followUps.push({ role: 'rerank', category: cat, part: x.part, functions: open, reason: 'P2 exclusion P3 overturned, neither qualified nor dropped by the re-rank' })
    for (const n of open) unhandled.add(n)
  }
  // A function only the re-rank names is listed. A function either returns
  // twice is not ranked.
  const twice = list => new Set((list || []).map(f => f.function).filter((n, i, all) => all.indexOf(n) !== i))
  const p2Twice = twice(p2.functions)
  const rrTwice = twice(rr.functions)
  for (const n of p2Twice) followUps.push({ role: 'P2', category: cat, function: n, reason: 'function returned twice; not ranked' })
  for (const n of rrTwice) followUps.push({ role: 'rerank', category: cat, function: n, reason: 'function returned twice; not ranked' })
  for (const fr of rr.functions || []) if (!names.includes(fr.function)) followUps.push({ role: 'rerank', category: cat, function: fr.function, reason: 'function P2 did not return' })
  const functions = []
  for (const name of [...new Set(names)]) {
    const f2 = (p2.functions || []).find(f => f.function === name) || {}
    const fr = (rr.functions || []).find(f => f.function === name)
    const fixed = (fixedFor(cat).find(x => x.function === name) || {}).part || ''
    const need = (f2.requirements || []).map(r => r.name)
    if (!need.length) followUps.push({ role: 'P2', category: cat, function: name, reason: 'function lists no requirement' })
    // A requirement named twice has no single value to check a part against.
    const needTwice = new Set(need).size !== need.length
    if (needTwice) followUps.push({ role: 'P2', category: cat, function: name, reason: 'function names a requirement twice; not ranked' })
    // A part with more than one record has no single record, and one with an
    // LCSC number that is neither C and digits nor none (for a part off the
    // board), or with placements below 1 (rule 4's need is boards x
    // placements), has none: dropped. A ranking that names it names a part
    // with no record.
    const listed = [...(f2.shortlist || []), ...((fr && fr.new_candidates) || [])]
    const malformed = c => !/^(C\d+|none)$/.test(c.lcsc || '') ? `LCSC number ${JSON.stringify(c.lcsc)}` : !(Number.isInteger(c.placements) && c.placements >= 1) ? `placements ${c.placements}` : ''
    for (const c of listed) if (malformed(c)) followUps.push({ role: 'P2', category: cat, function: name, part: c.part, reason: `record with ${malformed(c)}; dropped` })
    const pool = listed.filter(c => listed.filter(x => x.part === c.part).length === 1 && !malformed(c))
    for (const part of new Set(listed.filter(c => listed.filter(x => x.part === c.part).length > 1).map(c => c.part))) followUps.push({ role: 'P2', category: cat, function: name, part, reason: 'part given more than one record; dropped' })
    let shortlist = []
    let verify = []
    let alternateRecords = []
    // Only the re-rank's ranking orders the shortlist. A function it did not
    // rank, ranked with no part, or ranked with repeated or non-positive
    // positions, and a function with no requirement or one named twice, is
    // left open for a follow-up task.
    if (!fr) followUps.push({ role: 'rerank', category: cat, function: name, reason: 'the re-rank did not rank this function' })
    else if (!(fr.ranking || []).length) followUps.push({ role: 'rerank', category: cat, function: name, reason: 'the re-rank ranked no part' })
    const unranked = !fr || !(fr.ranking || []).length || p2Twice.has(name) || rrTwice.has(name) || unhandled.has(name)
    const badRanks = !unranked && !distinctRanks(fr.ranking)
    if (badRanks) followUps.push({ role: 'rerank', category: cat, function: name, reason: "the re-rank's positions are not the ranks 1 to n; the function is not ranked" })
    if (unranked || badRanks || !need.length || needTwice) {
      // nothing to verify
    } else {
      const dropped = new Set([...(fr.dropped_from_shortlist || []), ...(fr.dropped_from_p3 || [])].map(d => d.part))
      let contradicted = false
      for (const r of [...(fr.ranking || [])].sort((a, b) => a.rank - b.rank)) {
        const rec = pool.find(c => c.part === r.part)
        const why = !rec ? 'ranked part has no record' : shortlist.some(x => x.part === r.part) ? 'ranked twice' : dropped.has(r.part) ? 'ranked and dropped by the re-rank' : ''
        if (why) { followUps.push({ role: 'rerank', category: cat, function: name, part: r.part, reason: why }); contradicted = true; continue }
        shortlist.push({ ...rec, rank: r.rank, reason: r.reason })
      }
      // A ranking that names a part without a record, twice, or also as
      // dropped did not establish its order: the function is not ranked.
      if (contradicted) {
        followUps.push({ role: 'rerank', category: cat, function: name, reason: 'the ranking contradicts itself or its records; the function is not ranked' })
        shortlist = []
      }
      // A candidate the re-rank neither ranked nor dropped is kept, after the
      // ranked ones, and listed: an omission is not a drop. A record kept as
      // the rule-5 alternate of a part the re-rank did not drop stays a
      // record, not a candidate, also when the re-rank dropped it as one.
      const altNames = new Set([...(fr.verify || []).filter(v => v.kind === 'alternate').map(v => v.part),
        ...pool.filter(c => c.second_source_route === 'alternate' && !dropped.has(c.part)).map(c => c.second_source_part)])
      let last = shortlist.length ? shortlist[shortlist.length - 1].rank : 0
      for (const c of contradicted ? [] : pool) {
        if (shortlist.some(x => x.part === c.part)) continue
        if (altNames.has(c.part)) { alternateRecords.push(c); continue }
        if (dropped.has(c.part)) continue
        shortlist.push({ ...c, rank: ++last, reason: 'not ranked by the re-rank; kept in P2 order' })
        followUps.push({ role: 'rerank', category: cat, function: name, part: c.part, reason: 'candidate neither ranked nor dropped' })
      }
      // A part to verify without a shortlist record, such as an alternate
      // named only in a candidate's alternates, is still verified.
      verify = contradicted ? [] : (fr.verify || []).map(v => {
        if (shortlist.some(x => x.part === v.part) || alternateRecords.some(x => x.part === v.part)) return v
        followUps.push({ role: 'rerank', category: cat, function: name, part: v.part, reason: 'part to verify has no record' })
        return { ...v, norecord: true }
      })
    }
    // A candidate or a rule-5 alternate is checked against every requirement
    // of its function; one it does not list is added as unmet, so the
    // verifier must check it. A record that fails a requirement is dropped.
    const failed = []
    const fnValue = new Map((f2.requirements || []).map(r => [r.name, r.value]))
    const norm = t => String(t || '').replace(/\s+/g, ' ').trim()
    const qualify = (list, what) => list.map(c => {
      // The function's value is the requirement; a candidate that states
      // another is checked against the function's, its own pass unknown.
      const restated = (c.requirements || []).filter(r => fnValue.has(r.name) && norm(r.required) !== norm(fnValue.get(r.name)))
      if (!restated.length) return c
      followUps.push({ role: 'P2', category: cat, function: name, part: c.part, reason: `${what} states other values than the function for: ${restated.map(r => r.name).join(', ')}; checked against the function's`, notice: true })
      return { ...c, requirements: c.requirements.map(r => restated.includes(r) ? { ...r, required: fnValue.get(r.name), pass: null, restated: r.required } : r) }
    }).filter(c => {
      const failing = (c.requirements || []).filter(r => r.pass === false)
      if (!failing.length) return true
      followUps.push({ role: 'P2', category: cat, function: name, part: c.part, reason: `${what} with a failed requirement: ${failing.map(r => r.name).join(', ')}` })
      // The drop is P2's word only: the datasheet verifier re-reads it.
      failed.push({ part: c.part, names: failing.map(r => r.name), requirements: failing })
      return false
    }).map(c => {
      const have = new Set((c.requirements || []).map(r => r.name))
      const lack = need.filter(n => !have.has(n))
      if (!lack.length) return c
      followUps.push({ role: 'P2', category: cat, function: name, part: c.part, reason: `${what} lacks the function's requirements: ${lack.join(', ')}` })
      return { ...c, requirements: [...(c.requirements || []), ...lack.map(n => ({ name: n, required: 'see the function', datasheet: 'not given', pass: false, source: '', added: true }))] }
    })
    shortlist = qualify(shortlist, 'shortlisted')
    alternateRecords = qualify(alternateRecords, 'alternate')
    // A fixed input is not re-selected (Scope): a function whose first-ranked
    // part after the drops is another part is not ranked, and the owner is
    // told.
    if (fixed && shortlist.length && !isFixed(fixed, shortlist[0].part)) {
      followUps.push({ role: 'owner', category: cat, function: name, part: shortlist[0].part, reason: `fixed input ${fixed} not ranked first; reported to the owner, not re-selected` })
      shortlist = []
      verify = []
    }
    // Only R10's Q4 function and R12's Q8 function carry the owner's
    // alternatives; q-alternatives elsewhere are not verified as such.
    const owned = { R10: 'Q4', R12: 'Q8' }[cat]
    const decision = owned && fr && fr.decision === owned ? owned : 'none'
    if (fr && fr.decision && fr.decision !== 'none' && decision === 'none') followUps.push({ role: 'rerank', category: cat, function: name, reason: `decision ${fr.decision} does not belong to ${cat}` })
    // A part listed more than once as a Q alternative has no single class.
    const qTwice = new Set(verify.filter(v => v.kind === 'q-alternative').map(v => v.part).filter((part, i, all) => all.indexOf(part) !== i))
    if (qTwice.size) {
      for (const part of qTwice) followUps.push({ role: 'rerank', category: cat, function: name, part, reason: 'q-alternative listed more than once; not verified as one' })
      verify = verify.filter(v => !(v.kind === 'q-alternative' && qTwice.has(v.part)))
    }
    if (decision === 'none' && verify.some(v => v.kind === 'q-alternative')) {
      followUps.push({ role: 'rerank', category: cat, function: name, reason: 'q-alternatives on a function that implements no decision; not verified' })
      verify = verify.filter(v => v.kind !== 'q-alternative')
    }
    functions.push({ function: name, decision, kept_option: decision !== 'none' ? String((fr && fr.kept_option) || '') : '', failed, requirements: f2.requirements || [], shortlist, verify, alternateRecords, dropped: f2.dropped || [], dropped_from_shortlist: (fr && fr.dropped_from_shortlist) || [], dropped_from_p3: (fr && fr.dropped_from_p3) || [], fixed_input: fixed })
  }
  // One function implements the decision; with several, none does.
  const deciding = functions.filter(f => f.decision !== 'none')
  if (deciding.length > 1) {
    followUps.push({ role: 'rerank', category: cat, functions: deciding.map(f => f.function), reason: `${deciding.length} functions marked ${deciding[0].decision}; none counts` })
    for (const f of deciding) { f.decision = 'none'; f.verify = f.verify.filter(v => v.kind !== 'q-alternative') }
  }
  // R10 and R12 carry the owner's choice for Q4 and Q8: one of their
  // functions names an alternative to verify, or the category stays open.
  if (['R10', 'R12'].includes(cat) && !functions.some(f => f.decision !== 'none' && f.verify.some(v => v.kind === 'q-alternative'))) {
    followUps.push({ role: 'rerank', category: cat, reason: `names no ${cat === 'R10' ? 'Q4' : 'Q8'} function with an alternative to verify` })
  }
  // A function of the P1 inventory or of a fixed input that P2 returned no
  // entry for stays open.
  const inventory = (A.inventory || {})[cat] || []
  for (const inv of new Set([...inventory, ...fixedFor(cat).map(x => x.function)])) {
    if (functions.some(f => f.function === inv)) continue
    followUps.push({ role: 'P2', category: cat, function: inv, reason: `${inventory.includes(inv) ? 'function of the P1 inventory' : 'function of a fixed input'} that P2 did not return` })
    functions.push({ function: inv, decision: 'none', failed: [], requirements: [], shortlist: [], verify: [], alternateRecords: [], dropped: [], dropped_from_shortlist: [], dropped_from_p3: [], not_returned: true })
  }
  // A function the row names that P2 returned no entry for stays open, with
  // no part, for a follow-up task.
  for (const m of (p3 && p3.missed_functions) || []) {
    if (functions.some(f => f.function === m.function)) continue
    followUps.push({ role: 'P3', category: cat, function: m.function, reason: `function the row names that P2 did not return: ${m.why}` })
    functions.push({ function: m.function, decision: 'none', failed: [], requirements: [], shortlist: [], verify: [], alternateRecords: [], dropped: [], dropped_from_shortlist: [], dropped_from_p3: [], not_returned: true })
  }
  return functions
}

function candidateOf(functions, fn, part) {
  const f = functions.find(x => x.function === fn)
  return f ? (f.shortlist.find(c => c.part === part) || (f.alternateRecords || []).find(c => c.part === part) || null) : null
}

// A part's rule-5 alternate, when its route names one other than itself.
function altOf(c) {
  return c && c.second_source_route === 'alternate' && c.second_source_part && c.second_source_part !== c.part ? c.second_source_part : ''
}

// One P4 row checks an alternate against one part, the first to name it:
// the first-ranked part, then the Q alternatives in order.
function altOwners(functions, f) {
  const owners = new Map()
  for (const primary of [f.shortlist[0], ...f.verify.filter(v => v.kind === 'q-alternative').map(v => candidateOf(functions, f.function, v.part))]) {
    const a = altOf(primary)
    if (a && !owners.has(a)) owners.set(a, primary.part)
  }
  return owners
}

// The checks a verifier returns by these exact names for a confirmation to
// count (the P4 prompt names them).
// The stock verifier re-derives the placements per board from the
// specification, and whether the part is on the board, which its LCSC number
// claims (rule 1); the datasheet verifier confirms the maker is allowed
// (rule 2). An alternate passes rules 1 to 4 only (rule 5): it owes no
// second-vendor stock.
function requiredChecks(kind, cand, partKind) {
  if (kind === 'stock') {
    return ['stock', 'lifecycle status', 'end-of-life notices', 'placements', 'board placement', 'longevity commitment', 'market introduction', 'distributor status', 'lead time', ...(onBoard(cand) ? ['presale', 'LCSC identity', 'library type'] : []),
      ...(onBoard(cand) && cand.second_source_route === 'second-vendor' && partKind !== 'alternate' ? ['second-vendor stock'] : [])]
  }
  return [...((cand && cand.requirements) || []).map(r => r.name), 'manufacturer allowlist', ...(partKind === 'alternate' ? ['pin-for-pin match', 'functional match'] : [])]
}

// Readings that move between P2's reading and P4's, or that pass whatever
// they read: a value that differs from P2's refutes only when it fails.
const MOVING = new Set(['stock', 'presale', 'second-vendor stock', 'lead time', 'distributor status', 'market introduction', 'longevity commitment', 'library type'])

// Checks whose reading may be that there is none: no notice, no published
// commitment.
const MAY_READ_NONE = new Set(['end-of-life notices', 'longevity commitment'])

// The checks of an alternate's fit to the part it stands in for. Every
// other check reads the part against its function, whatever its role.
const FIT = new Set(['pin-for-pin match', 'functional match'])

// A check written as not read, as none where a value exists to be read, or
// without its source or a reading time of this task, shows nothing.
function shown(c) {
  return !(MAY_READ_NONE.has(c.figure) ? unread(c.read) : readsNone(c.read)) && !readsNone(c.source) && readInRun(c.read_at)
}

function covered(v, cand) {
  // A part with no record cannot show its gate evidence, and a datasheet
  // confirmation with nothing to check shows none.
  if (!cand) return false
  const req = requiredChecks(v.verifier, cand, v.kind)
  if (v.verifier === 'datasheet' && !req.length) return false
  const have = new Set((v.checks || []).filter(shown).map(c => c.figure))
  // Rule 6: a part the owner holds passes rule 4 on the held quantity, in
  // place of the live stock and presale.
  const held = v.verifier === 'stock' && onBoard(cand) && Number(cand.held) > 0 && have.has('held quantity')
  return req.filter(n => !(held && (n === 'stock' || n === 'presale'))).every(n => have.has(n))
}

// A part's status after a later ledger entry: a standing refutation is
// final, and one no adjudicator ruled on gives way to a standing one only.
function laterStatus(prev, next) {
  const weight = s => s === 'refuted' ? 2 : String(s || '').startsWith('refuted') ? 1 : 0
  return weight(next) >= weight(prev) ? next : prev
}

// Whether a ledger row gives the part's own status in its function: a row
// as a primary does, and so does a refutation as an alternate unless it
// rests on the fit alone, which fails only that relationship.
const ofPart = l => l.as !== 'alternate' || (String(l.status).startsWith('refuted') && !l.fit_only)

// A part's own status in its function, from the ledger.
function partStatus(ledger, fn, part) {
  let status = ''
  for (const l of ledger) if (l.part === part && l.function === fn && ofPart(l)) status = laterStatus(status, l.status)
  return status
}

// The part a function stands on: the first in rank order whose refutation
// has not stood.
function standingPart(f, ledger) {
  return f.shortlist.find(c => partStatus(ledger, f.function, c.part) !== 'refuted') || null
}

// The adjudicator's ruling counts only for the item it was asked about.
function boundRuling(ruling, fn, part, cat) {
  if (!ruling) return null
  if (readsNone(ruling.evidence) || readsNone(ruling.source) || !readInRun(ruling.read_at)) {
    followUps.push({ role: 'adjudicator', category: cat, function: fn, part, reason: 'the ruling gives no evidence, source and time read' })
    return null
  }
  if (ruling.function === fn && (ruling.part || '') === (part || '')) return ruling
  followUps.push({ role: 'adjudicator', category: cat, function: fn, part, reason: 'the ruling names another item' })
  return null
}

async function verifyCategory(cat, functions, bundle, claims) {
  const ledger = []
  const queue = [{ only: null }]
  while (queue.length) {
    const { only } = queue.shift()
    // The parts whose refutation stood in this pass, by function.
    const passedOver = new Map()
    const phaseName = only ? 'Refutations' : 'P2-P4'
    const [st, ds] = await parallel([
      () => run('P4', cat, `P4-stock-${cat}`, phaseName, p4Prompt(cat, 'stock', bundle, only)),
      () => run('P4', cat, `P4-datasheet-${cat}`, phaseName, p4Prompt(cat, 'datasheet', bundle, only)),
    ])
    // Each part to verify, with its kind from the verification plan, and
    // the rule-5 alternate of the part that would be selected.
    // An alternate's row names the part it was checked against (for).
    const want = []
    if (only) {
      want.push({ function: only.function, part: only.part, kind: 'first' })
      if (altOf(only)) want.push({ function: only.function, part: altOf(only), kind: 'alternate', for: only.part })
    } else {
      for (const f of functions) {
        // A part verified in its own right keeps that kind; an alternate
        // role it also has is checked beside it.
        const kinds = new Map()
        const plan = (part, kind) => { if (!kinds.has(part) || (kinds.get(part) === 'alternate' && kind !== 'alternate')) kinds.set(part, kind) }
        if (f.shortlist.length) {
          plan(f.shortlist[0].part, 'first')
          if (altOf(f.shortlist[0])) plan(altOf(f.shortlist[0]), 'alternate')
        }
        for (const v of f.verify) plan(v.part, v.kind)
        // A Q4 or Q8 alternative needs its own second source, as the part
        // kept does.
        for (const v of f.verify.filter(x => x.kind === 'q-alternative')) {
          const a = altOf(candidateOf(functions, f.function, v.part))
          if (a) plan(a, 'alternate')
        }
        const owners = altOwners(functions, f)
        for (const [part, kind] of kinds) want.push({ function: f.function, part, kind, for: owners.get(part) || '', alsoAlternate: kind !== 'alternate' && owners.has(part) })
      }
    }
    const got = [['stock', st], ['datasheet', ds]].filter(([, v]) => v)
    if (got.length < 2) followUps.push({ role: 'P4', category: cat, part: only ? only.part : '', reason: 'a verifier returned nothing; what the other returned is still adjudicated' })
    const byPart = new Map()
    const key = (f, p) => `${f}\u0000${p}`
    for (const w of want) byPart.set(key(w.function, w.part), { ...w, verdicts: [] })
    for (const [kind, v] of got) {
      // A part the verifier lists twice has no single verdict from it.
      const count = new Map()
      for (const p of v.parts || []) count.set(key(p.function, p.part), (count.get(key(p.function, p.part)) || 0) + 1)
      for (const [k, n] of count) if (n > 1) { const [fn, part] = k.split('\u0000'); followUps.push({ role: 'P4', category: cat, function: fn, part, reason: `${kind} verifier listed the part twice; no verdict from it` }) }
      for (const p of v.parts || []) {
        if (only && (p.function !== only.function || (p.part !== only.part && p.part !== altOf(only)))) continue
        const k = key(p.function, p.part)
        if (count.get(k) > 1) continue
        if (!byPart.has(k)) { followUps.push({ role: 'P4', category: cat, function: p.function, part: p.part, reason: `${kind} verifier listed a part it was not asked to verify; ignored` }); continue }
        const e = byPart.get(k)
        e.verdicts.push({ verifier: kind, kind: e.kind, reported_kind: p.kind, verdict: p.verdict, refutation: p.refutation, checks: p.checks })
        byPart.set(k, e)
      }
    }
    for (const e of byPart.values()) {
      // A confirmation counts only with its required checks, each reading
      // the stated figure and passing its requirement or gate. A check that
      // disagrees or fails is a refutation, and is named in the refutation
      // the adjudicator rules on whatever the verdict. A confirmation that
      // states a refutation is one.
      const cand = candidateOf(functions, e.function, e.part)
      const added = new Set(((cand && cand.requirements) || []).filter(r => r.added).map(r => r.name))
      for (const v of e.verdicts) {
        if (v.verdict === 'confirmed' && !noRefutation(v.refutation)) v.verdict = 'refuted'
        const passing = name => (v.checks || []).some(c => c.figure === name && shown(c) && c.passes === true)
        // Rule 6: a passing held quantity supersedes the live stock and
        // presale readings of a held part, and passing live readings a
        // failing held quantity.
        const heldRoute = v.verifier === 'stock' && onBoard(cand) && Number(cand && cand.held) > 0 && (v.checks || []).some(c => c.figure === 'held quantity' && shown(c) && c.agrees && c.passes === true)
        const liveRoute = v.verifier === 'stock' && passing('stock') && passing('presale')
        // Compatibility checks bear on a part's alternate role only, and the
        // second vendor's stock on a route that needs it.
        const compat = c => FIT.has(c.figure)
        const offRoute = c => (heldRoute && (c.figure === 'stock' || c.figure === 'presale')) || (liveRoute && c.figure === 'held quantity')
          || (e.kind !== 'alternate' && compat(c)) || (c.figure === 'second-vendor stock' && !requiredChecks('stock', cand, e.kind).includes(c.figure))
        // A reading that moves or always passes, and a requirement P2 gave no
        // value for, differs from the value stated without refuting.
        const passesOnly = c => MOVING.has(c.figure) || (v.verifier === 'datasheet' && added.has(c.figure))
        const fails = c => (!c.agrees && !passesOnly(c)) || c.passes !== true
        const bad = (v.checks || []).filter(c => !offRoute(c) && fails(c))
        // The checks the verdict fails, the fit of a part that is also an
        // alternate among them.
        v.failed = (v.checks || []).filter(c => fails(c) && (!offRoute(c) || (e.alsoAlternate && compat(c)))).map(c => c.figure)
        if (bad.length) {
          v.refutation = [v.verdict === 'refuted' ? v.refutation : '', `checks disagree or fail: ${bad.map(c => c.figure).join(', ')}`].filter(t => !readsNone(t)).join('; ')
          v.verdict = 'refuted'
        }
        if (v.verdict === 'confirmed' && !covered(v, cand)) { v.verdict = 'incomplete'; v.missing = requiredChecks(v.verifier, cand, v.kind).filter(n => !(v.checks || []).some(c => c.figure === n)) }
      }
      // Coverage needs the required checks whatever the verdict, so an
      // overturned refutation without them does not count as confirmation.
      const complete = kind => e.verdicts.some(v => v.verifier === kind && v.verdict !== 'incomplete' && covered(v, cand))
      const refuted = e.verdicts.filter(v => v.verdict === 'refuted')
      // A refutation whose every refuting verdict fails the fit and nothing
      // else fails only the alternate relationship. A part in both roles
      // refuted so keeps its own verification on its other checks, and the
      // ruling is on the relationship.
      const fitOnly = refuted.length > 0 && refuted.every(v => v.failed.length && v.failed.every(n => FIT.has(n)))
      const relOnly = e.alsoAlternate && fitOnly
      if (relOnly) {
        const both = complete('stock') && complete('datasheet')
        ledger.push({ function: e.function, part: e.part, as: 'primary', status: both ? 'verified' : 'not verified', reason: both ? '' : 'not covered by both verifiers' })
        if (!both) followUps.push({ role: 'P4', category: cat, function: e.function, part: e.part, reason: 'not covered by both verifiers' })
      }
      const as = e.kind === 'alternate' || relOnly ? { as: 'alternate', for: e.for, ...(fitOnly ? { fit_only: true } : {}) } : { as: 'primary' }
      if (!refuted.length) {
        if (complete('stock') && complete('datasheet')) { ledger.push({ function: e.function, part: e.part, ...as, status: 'verified' }); continue }
        const reason = e.verdicts.some(v => v.verdict === 'incomplete') ? 'a verifier gave no required check for some figures' : 'not covered by both verifiers'
        ledger.push({ function: e.function, part: e.part, ...as, status: 'not verified', reason })
        followUps.push({ role: 'P4', category: cat, function: e.function, part: e.part, reason })
        continue
      }
      if (!takeExtra(1)) {
        ledger.push({ function: e.function, part: e.part, ...as, status: 'refuted, not adjudicated' })
        followUps.push({ role: 'adjudicator', category: cat, function: e.function, part: e.part, reason: 'no free agent' })
        continue
      }
      const ruling = boundRuling(await run('adjudicator', cat, `adjudicator-${cat}`, 'Refutations', adjudicatorPrompt(cat, e.function, e.part, e.verdicts)), e.function, e.part, cat)
      if (!ruling) {
        ledger.push({ function: e.function, part: e.part, ...as, status: 'refuted, no ruling' })
        followUps.push({ role: 'adjudicator', category: cat, function: e.function, part: e.part, reason: 'refutation without a ruling' })
        continue
      }
      if (!ruling.stands) {
        // Its fit then holds on its compatibility checks (below).
        if (relOnly) continue
        const both = complete('stock') && complete('datasheet')
        ledger.push({ function: e.function, part: e.part, ...as, status: both ? 'verified; refutation did not stand' : 'not verified', reason: both ? '' : 'refutation did not stand, but one verifier did not cover it' })
        if (!both) followUps.push({ role: 'P4', category: cat, function: e.function, part: e.part, reason: 'not covered by both verifiers' })
        continue
      }
      ledger.push({ function: e.function, part: e.part, ...as, status: 'refuted' })
      // A refuted alternate leaves the primary without a second source by
      // it. One that does not rest on the fit alone also holds for the part
      // in its function (ofPart), but sends no next part to a pair.
      if (as.as === 'alternate') { followUps.push({ role: 'P4', category: cat, function: e.function, part: e.part, reason: 'alternate refuted; the primary has no second source by it' }); continue }
      // A fixed input that fails a check is reported to the owner, not
      // re-selected (Scope): its function takes no next-ranked part.
      const fixed = (functions.find(x => x.function === e.function) || {}).fixed_input
      if (fixed) {
        if (isFixed(fixed, e.part)) followUps.push({ role: 'owner', category: cat, function: e.function, part: e.part, reason: `fixed input ${fixed} refuted; reported to the owner, not re-selected` })
        continue
      }
      // No pair verifies another Q alternative in a refuted one's place.
      if (e.kind === 'q-alternative') followUps.push({ role: 'rerank', category: cat, function: e.function, part: e.part, reason: 'Q alternative refuted; no other alternative verified in its place' })
      passedOver.set(e.function, [...(passedOver.get(e.function) || []), e.part])
    }
    // A part verified in its own right that is also another part's
    // alternate: its primary result stands as it is; the alternate
    // relationship holds when that result is a verification and its
    // compatibility checks pass. A refutation that did not stand was ruled
    // on as a whole, its compatibility included.
    for (const e of byPart.values()) {
      if (!e.alsoAlternate) continue
      // A relationship refuted on its fit keeps that status.
      if (ledger.some(l => l.as === 'alternate' && l.function === e.function && l.part === e.part && l.for === e.for)) continue
      const primary = [...ledger].reverse().find(l => l.function === e.function && l.part === e.part && l.as === 'primary')
      const fits = e.verdicts.some(v => v.verifier === 'datasheet' && [...FIT].every(n => (v.checks || []).some(c => c.figure === n && c.agrees && c.passes === true && shown(c))))
      const status = primary && primary.status === 'refuted' ? 'refuted' : primary && primary.status.startsWith('verified') && fits ? 'verified' : 'not verified'
      ledger.push({ function: e.function, part: e.part, as: 'alternate', for: e.for, status })
    }
    // A standing refutation moves its function on once every ruling of the
    // pass is in: one pair a function, for the part it now stands on. A
    // refuted part ranked below that part, such as a Q alternative, moves
    // nothing, and a part already verified, with its rule-5 alternate
    // checked against it, takes no pair.
    for (const [fn, parts] of passedOver) {
      const f = functions.find(x => x.function === fn)
      const next = standingPart(f, ledger)
      const at = part => f.shortlist.findIndex(c => c.part === part)
      const over = parts.filter(p => at(p) >= 0 && (!next || at(p) < at(next.part))).sort((a, b) => at(a) - at(b))
      if (!over.length) continue
      if (!next) { followUps.push({ role: 'P2', category: cat, function: fn, part: over[over.length - 1], reason: 'refuted, and no next-ranked candidate' }); continue }
      const alt = altOf(next)
      if (partStatus(ledger, fn, next.part).startsWith('verified') && (!alt || ledger.some(l => l.function === fn && l.part === alt && l.as === 'alternate' && l.for === next.part))) continue
      // The pair is paid for here from the free agents; run() starts it.
      if (!takeExtra(2)) { followUps.push({ role: 'P4', category: cat, function: fn, part: next.part, reason: 'no free agents for the next pair' }); continue }
      queue.push({ only: { ...next, function: fn } })
    }
    // Figures are confirmed once, by the first pair: each name in
    // figures_to_check needs one verdict from the datasheet verifier, with
    // its evidence, source and time read. Its refutation so read is
    // adjudicated in every pass, a replacement pair's included; the stock
    // verifier re-reads no figure.
    const figs = uniqueVerdicts((ds && ds.figures) || [], f => f.figure, { role: 'P4', category: cat })
    const shows = f => !readsNone(f.evidence) && !readsNone(f.source) && readInRun(f.read_at)
    for (const name of only ? [] : bundle.figures_to_check) {
      const f = figs.get(name)
      if (f && shows(f)) continue
      ledger.push({ figure: name, status: 'not verified', reason: f ? `${f.verdict} without evidence, source and time read` : 'the datasheet verifier gave no single verdict' })
      followUps.push({ role: 'P4', category: cat, figure: name, reason: 'figure not verified' })
    }
    for (const name of bundle.figures_to_check) {
      const f = figs.get(name)
      // A figure already open takes no other ruling.
      if (!f || f.verdict !== 'refuted' || !shows(f) || ledger.some(l => l.figure === name && !l.status.startsWith('confirmed'))) continue
      if (!takeExtra(1)) {
        ledger.push({ figure: name, status: 'refuted, not adjudicated' })
        followUps.push({ role: 'adjudicator', category: cat, figure: name, reason: 'no free agent' })
        continue
      }
      // The ruling weighs the refutation against the claim the figure states.
      const ruling = boundRuling(await run('adjudicator', cat, `adjudicator-${cat}`, 'Refutations', adjudicatorPrompt(cat, name, '', [{ verifier: 'datasheet', ...f, claim: claims.get(name) }])), name, '', cat)
      const status = !ruling ? 'refuted, no ruling' : ruling.stands ? 'refuted' : 'confirmed; refutation did not stand'
      ledger.push({ figure: name, status })
      if (!ruling || ruling.stands) followUps.push({ role: 'P2', category: cat, figure: name, reason: ruling ? 'figure refuted' : 'figure refuted without a ruling' })
    }
  }
  return ledger
}

function onBoard(c) {
  return !!(c && /^C\d+$/.test(c.lcsc || ''))
}

// A part whose rule-5 route names no second source it can be built with.
function noSecondSource(c) {
  return c.second_source_route === 'none'
    || (c.second_source_route === 'alternate' && !altOf(c))
    || (c.second_source_route === 'second-vendor' && (!c.lcsc || c.lcsc === 'none'))
}

// The part each function keeps: the first part in rank order that is not
// refuted, when both verifiers confirmed it. A part not verified keeps the
// function open; only a refutation that stands moves on to the next part.
function selection(functions, ledger) {
  // A part's status is its last ledger entry: a later refutation overrides
  // an earlier verification, and a later verification no refutation.
  // Primary and alternate verifications are separate relationships: a part
  // refuted as another part's alternate on its fit alone keeps its own
  // shortlist place, and an alternate counts for the part it was checked
  // against only.
  const last = new Map()
  const altLast = new Map()
  for (const l of ledger) {
    if (!l.part) continue
    const own = `${l.function}\u0000${l.part}`
    const rel = `${l.function}\u0000${l.for}\u0000${l.part}`
    if (l.as === 'alternate') altLast.set(rel, laterStatus(altLast.get(rel), l.status))
    if (ofPart(l)) last.set(own, laterStatus(last.get(own), l.status))
  }
  return functions.map(f => {
    const st = c => last.get(`${f.function}\u0000${c.part}`) || ''
    // A part refuted in the function, standing or not ruled on, fails as an
    // alternate too; a refutation on the fit alone is the relationship's.
    const altSt = (primary, alt) => st({ part: alt }).startsWith('refuted') ? st({ part: alt }) : (altLast.get(`${f.function}\u0000${primary}\u0000${alt}`) || '')
    const standing = f.shortlist.find(c => st(c) !== 'refuted')
    // A fixed input's function keeps that input or nothing.
    const kept = standing && st(standing).startsWith('verified') && (!f.fixed_input || isFixed(f.fixed_input, standing.part)) ? standing : null
    const refuted = f.shortlist.filter(c => st(c) === 'refuted').map(c => c.part)
    // Only the kept part's rule-5 alternate sources it; the alternate of a
    // refuted part does not.
    const alternateUnverified = altOf(kept) && !altSt(kept.part, altOf(kept)).startsWith('verified') ? [altOf(kept)] : []
    const recOf = part => f.shortlist.find(c => c.part === part) || (f.alternateRecords || []).find(c => c.part === part) || null
    const altOwner = altOwners(functions, f)
    // Rule 5's alternate passes rule 1 as the part does: a part and its
    // alternate are both on the board or both off it.
    const offBoardAlt = c => !!(altOf(c) && recOf(altOf(c)) && (onBoard(c) !== onBoard(recOf(altOf(c))) || recOf(altOf(c)).placements < c.placements))
    const qAlternatives = f.verify.filter(v => v.kind === 'q-alternative' && !(kept && v.part === kept.part)).map(v => ({ ...qAlt(v), option: String(v.option || '') }))
    // Each option class of the decision (categories.json q_options) needs a
    // part: the kept part in the class its figure was asked for, as the
    // first-ranked part after the drops (kept_option) or as a Q alternative,
    // or another Q alternative.
    const needed = f.decision !== 'none' ? ((A.q_options || {})[f.decision] || []) : []
    // A class counts for a verified part whose class the datasheet verifier
    // confirmed ("Q option: FUNCTION: PART: CLASS").
    const classed = (part, option) => !!option && !ledger.some(l => l.figure === `Q option: ${f.function}: ${part}: ${option}` && !String(l.status).startsWith('confirmed'))
    const keptClasses = !kept ? [] : [...(kept === f.shortlist[0] ? [f.kept_option] : []),
      ...f.verify.filter(v => v.kind === 'q-alternative' && v.part === kept.part).map(v => String(v.option || ''))]
    const have = new Set([...keptClasses.filter(o => classed(kept.part, o)),
      ...qAlternatives.filter(q => String(q.status).startsWith('verified') && classed(q.part, q.option)).map(q => q.option)])
    const qOptionsMissing = needed.filter(o => !have.has(o))
    function qAlt(v) {
      const rec = recOf(v.part)
      const alt = altOf(rec)
      const shared = alt && altOwner.get(alt) !== v.part
      return { part: v.part, status: st(v) || 'not verified', alternate: alt,
        alternate_status: !alt ? '' : altSt(v.part, alt) || (shared ? `not verified: shared with ${altOwner.get(alt)}` : 'not verified'), second_source_missing: !rec || noSecondSource(rec) || offBoardAlt(rec) }
    }
    const missing = !!(kept && (noSecondSource(kept) || offBoardAlt(kept)))
    return { function: f.function, decision: f.decision || 'none', part: kept ? kept.part : null, rank: kept ? kept.rank : null, alternate: altOf(kept), refuted,
      alternate_unverified: alternateUnverified, second_source_missing: missing, q_alternatives: qAlternatives, q_options_missing: qOptionsMissing }
  })
}

// The per-part figures (categories.json reports_per_part) P2 and the re-rank
// did not return for these parts, as "report: NAME: PART".
function perPartMissing(cat, p2, rr, parts) {
  const reported = [...(p2.report || []), ...(rr.report || [])].filter(f => !notFound(f.value)).map(f => f.figure)
  const out = []
  for (const need of (A.per_part_reports || {})[cat] || []) {
    for (const part of parts) {
      const n = `${need}: ${part}`
      if (reported.includes(n)) continue
      out.push(`report: ${n}`)
      followUps.push({ role: 'P2', category: cat, figure: n, reason: 'required per-part report figure not returned' })
    }
  }
  return out
}

// The figures the datasheet verifier rules on: each reported figure and each
// value found for research under a unique name, and the figures the category
// must report and the values the owner marked for research, whether or not
// P2 returned them. Each carries its claim, the value or record it states,
// for the adjudicator of its refutation.
function figuresToCheck(cat, p2, rr, functions, p3) {
  const names = []
  const missing = []
  const claims = new Map()
  const add = (n, claim) => {
    const name = names.includes(n) ? `${n} (${names.filter(x => x.startsWith(n)).length + 1})` : n
    names.push(name)
    claims.set(name, claim)
  }
  const read = (p2.found_values || []).filter(v => !notFound(v.value))
  const reported = (p2.report || []).filter(f => !notFound(f.value))
  for (const v of read) add(`found ${v.question_id}`, v)
  for (const q of (A.for_research || []).filter(q => q.category === cat)) if (!read.some(v => v.question_id === q.id)) {
    missing.push(`found ${q.id}`)
    followUps.push({ role: 'P2', category: cat, question: q.id, reason: 'value marked for research not found' })
  }
  for (const f of reported) add(`P2 report: ${f.figure}`, f)
  // A required figure is returned under its name, or per part as "NAME: PART".
  for (const need of (A.required_reports || {})[cat] || []) if (!reported.some(f => f.figure === need || f.figure.startsWith(`${need}: `))) {
    missing.push(`P2 report: ${need}`)
    followUps.push({ role: 'P2', category: cat, figure: need, reason: 'required report figure not returned' })
  }
  const rrReported = (rr.report || []).filter(f => !notFound(f.value))
  for (const f of rrReported) add(`re-rank report: ${f.figure}`, f)
  // A figure the plan asks per part is owed for every part the category
  // verifies, by P2 or the re-rank, as "NAME: PART" ("not applicable:
  // REASON" where it does not concern the part).
  const verified = new Set()
  for (const f of functions) {
    const first = f.shortlist[0]
    for (const part of [first && first.part, altOf(first), ...f.verify.map(v => v.part),
      ...f.verify.filter(v => v.kind === 'q-alternative').map(v => altOf(candidateOf(functions, f.function, v.part)))]) if (part) verified.add(part)
  }
  missing.push(...perPartMissing(cat, p2, rr, verified))
  // Each option class the re-rank gives a Q4 or Q8 part is confirmed.
  for (const f of functions) {
    if (f.decision === 'none') continue
    if (f.shortlist[0] && f.kept_option) add(`Q option: ${f.function}: ${f.shortlist[0].part}: ${f.kept_option}`, { function: f.function, part: f.shortlist[0].part, option: f.kept_option })
    for (const v of f.verify) if (v.kind === 'q-alternative' && v.option) add(`Q option: ${f.function}: ${v.part}: ${v.option}`, { function: f.function, part: v.part, option: v.option })
  }
  // Each function's requirement list is P2's reading of the specification:
  // the datasheet verifier re-derives it from IOBoard.md and the answers.
  for (const f of functions) if (f.shortlist.length) add(`function requirements: ${f.function}`, f.requirements)
  // Every candidate P2 shortlisted and its own record fails.
  for (const f of functions) for (const d of f.failed || []) add(`failed requirement: ${f.function}: ${d.part} (${d.names.join(', ')})`, d)
  // Every drop of a part P3 found or reopened, whichever list the re-rank
  // put it in.
  const fromP3 = new Set([...((p3 && p3.missed) || []), ...((p3 && p3.exclusions_not_holding) || [])].map(x => x.part))
  // A part dropped under several functions is re-read under each.
  const drops = new Set()
  for (const f of functions) {
    for (const d of [...f.dropped_from_p3, ...f.dropped_from_shortlist]) {
      const k = `${f.function}\u0000${d.part}`
      if (d.part && (fromP3.has(d.part) || f.dropped_from_p3.includes(d)) && !drops.has(k)) { drops.add(k); add(`re-rank drop: ${f.function}: ${d.part}`, d) }
    }
  }
  return { names, missing, parts: verified, claims }
}

async function categoryChain(cat) {
  const p2 = await run('P2', cat, `P2-${cat}`, 'P2-P4', p2Prompt(cat))
  if (!p2) return { category: cat, status: 'P2 returned nothing' }
  const p3 = await run('P3', cat, `P3-${cat}`, 'P2-P4', p3Prompt(cat, p2))
  const rr = await run('rerank', cat, `rerank-${cat}`, 'P2-P4', rerankPrompt(cat, p2, p3 || { category: cat, missed: [], exclusions_not_holding: [], note: 'P3 returned nothing twice; its search is a follow-up item' }))
  if (!rr) return { category: cat, status: 're-rank returned nothing' }
  const functions = merge(cat, p2, rr, p3)
  const { names: figures_to_check, missing: not_returned, parts: checkedParts, claims } = figuresToCheck(cat, p2, rr, functions, p3)
  const bundle = { functions, found_values: p2.found_values || [], report_p2: p2.report || [], report_rerank: rr.report || [], figures_to_check }
  const ledger = await verifyCategory(cat, functions, bundle, claims)
  // Without P3's search the category is not complete: no part is kept, and
  // the part that would have been is named for the follow-up task.
  const sel = selection(functions, ledger)
  // A figure is confirmed unless the ledger holds another status for it.
  // What P2 did not return has nothing to verify and stays open.
  // A replacement part reached through a refutation owes its per-part
  // figures too; the first pair re-read the ones P2 and the re-rank gave.
  const selected = new Set()
  for (const e of sel) {
    const rec = e.part ? candidateOf(functions, e.function, e.part) : null
    for (const part of [e.part, altOf(rec), ...(e.q_alternatives || []).flatMap(q => [q.part, q.alternate])]) if (part) selected.add(part)
  }
  const owedLater = perPartMissing(cat, p2, rr, [...selected].filter(part => !checkedParts.has(part)))
  const figures_open = [...new Set([...not_returned, ...owedLater, ...ledger.filter(l => l.figure && !l.status.startsWith('confirmed')).map(l => l.figure)])]
  const q_missing = ['R10', 'R12'].includes(cat) && !sel.some(e => e.decision !== 'none' && (e.q_alternatives || []).length && !(e.q_options_missing || []).length)
  return { category: cat, status: p3 ? 'done' : 'done without P3', ledger, figures_open, q_missing,
    selection: p3 ? sel : sel.map(e => ({ ...e, part: null, rank: null, without_p3: e.part })) }
}

async function phaseP2P4(cats) {
  phase('P2-P4')
  const out = await parallel(cats.map(cat => () => categoryChain(cat)))
  return cats.map((cat, i) => {
    if (out[i]) return out[i]
    followUps.push({ role: 'P2', category: cat, reason: 'the category chain failed' })
    return { category: cat, status: 'failed' }
  })
}

// ---------------------------------------------------------------- P5, P6, P7

// Items the critic upholds, and items it did not check, which stay in the
// summary as unchecked so the T6 gate sees them.
function critiqued(kind, items, critic) {
  if (!critic) {
    items.forEach((it, index) => followUps.push({ role: `${kind}-critic`, index, item: it, reason: `${kind} item not checked` }))
    return items.map(it => ({ ...it, source: kind, unchecked: true }))
  }
  const held = []
  const vs = uniqueVerdicts(critic.verdicts, x => x.index, { role: `${kind}-critic` })
  items.forEach((it, index) => {
    const v = vs.get(index)
    if (!v) { followUps.push({ role: `${kind}-critic`, index, item: it, reason: `${kind} item not ruled on` }); held.push({ ...it, source: kind, unchecked: true }) }
    else if (v.holds) held.push({ ...it, source: kind, unchecked: false })
  })
  for (const it of critic.added || []) held.push({ ...it, source: `${kind} critic`, unchecked: false })
  return held
}

// Combinations and budgets reach the pages only when the critic upheld them;
// the rest stay in the summary marked unchecked or not upheld.
function ruled(items, verdicts, critic, kind) {
  const v = uniqueVerdicts(verdicts, x => x.index, { role: `${kind}-critic` })
  return items.map((it, index) => {
    if (!critic || !v.has(index)) return { ...it, unchecked: true, upheld: undefined }
    return { ...it, unchecked: false, upheld: !!v.get(index).holds, reason: v.get(index).reason }
  })
}

async function p5Chain() {
  const p5 = await run('P5', '', 'P5', 'P5-P6', p5Prompt())
  if (!p5) return { conflicts: [], missing: 'P5' }
  const critic = await run('P5-critic', '', 'P5-critic', 'P5-P6', p5CriticPrompt(p5))
  return {
    critic_missing: critic ? '' : 'P5-critic',
    conflicts: critiqued('P5', p5.conflicts || [], critic),
    combinations: ruled(p5.combinations || [], critic && critic.combination_verdicts, critic, 'P5'),
    budgets: ruled(p5.budgets || [], critic && critic.budget_verdicts, critic, 'P5'),
    assumptions: ruled(p5.assumptions || [], critic && critic.assumption_verdicts, critic, 'P5'),
  }
}
async function p6Chain() {
  const p6 = await run('P6', '', 'P6', 'P5-P6', p6Prompt())
  if (!p6) return { gaps: [], missing: 'P6' }
  const critic = await run('P6-critic', '', 'P6-critic', 'P5-P6', p6CriticPrompt(p6))
  return { gaps: critiqued('P6', p6.gaps || [], critic), critic_missing: critic ? '' : 'P6-critic' }
}

// T5 runs the two checks side by side. A follow-up task runs P5 and its
// critic, then P6 and its critic, as the page orders it. Each conflict and
// gap that holds becomes a follow-up item.
async function phaseP5P6() {
  phase('P5-P6')
  let a
  let b
  if (TASK === 'FU') { a = await p5Chain(); b = await p6Chain() } else { [a, b] = await parallel([p5Chain, p6Chain]) }
  a = a || { conflicts: [], missing: 'P5' }
  b = b || { gaps: [], missing: 'P6' }
  for (const c of a.conflicts) followUps.push({ role: 'P5', item: c, reason: 'conflict' })
  for (const g of b.gaps) followUps.push({ role: 'P6', item: g, reason: 'gap' })
  const items = [...(a.combinations || []), ...(a.budgets || []), ...(a.assumptions || [])]
  const unchecked = [...items, ...a.conflicts, ...b.gaps].filter(x => x.unchecked).length
  // A combination or budget the critic rejected is not on the pages, so it
  // is unresolved until a later check replaces it.
  const rejected = items.filter(x => x.upheld === false)
  for (const x of rejected) followUps.push({ role: 'P5', item: x, reason: 'combination or budget the critic rejected' })
  // P5 owes the board's budget and its output combinations; an empty list
  // of either is a check that did not run.
  // A check whose critic did not return is not an independent check.
  const missingChecks = [a.missing, b.missing, a.critic_missing, b.critic_missing].filter(Boolean)
  // Every budget the P5 row names, upheld by the critic.
  // A budget counts with a value read, its source and its time; "not
  // applicable" (or N/A, does not apply) only for a check the P5 row makes
  // conditional.
  const conditional = n => (A.p5_conditional || []).some(c => n === c || n.startsWith(`${c}: `))
  const upheld = (a.budgets || []).filter(x => x.upheld === true && !readsNone(x.source) && !!isTime(x.read_at) && !notFound(x.value)
    && (!/^(not applicable|n\/a|does not apply)\b/i.test(String(x.value).trim()) || conditional(String(x.item || '')))).map(x => String(x.item || ''))
  const budgetsMissing = a.missing ? [] : (A.p5_budgets || []).filter(n => !upheld.some(i => i === n || i.startsWith(`${n}: `)))
  for (const n of budgetsMissing) followUps.push({ role: 'P5', item: n, reason: 'budget the P5 row names not returned and upheld' })
  // A combination counts with its outputs, bind order, resources, source
  // and time, and upheld.
  if (!a.missing && !(a.combinations || []).some(x => x.upheld === true && x.fits === true && !readsNone(x.outputs) && !readsNone(x.bind_order) && !readsNone(x.resources) && !readsNone(x.source) && !!isTime(x.read_at))) missingChecks.push('P5 combinations')
  // An assumption without a value assumed nothing.
  const unsourced = [...(a.combinations || []), ...(a.budgets || [])].filter(x => readsNone(x.source) || !isTime(x.read_at)).length
    + (a.assumptions || []).filter(x => readsNone(x.source) || !isTime(x.read_at) || notFound(x.value)).length
  if (!a.missing && !(a.budgets || []).length) missingChecks.push('P5 budgets')
  for (const m of missingChecks) followUps.push({ role: m, reason: `${m} returned nothing; the check did not run` })
  return { conflicts: a.conflicts, combinations: a.combinations || [], budgets: a.budgets || [], assumptions: a.assumptions || [], gaps: b.gaps,
    missing_checks: missingChecks, unchecked_items: unchecked, rejected_items: rejected.length, budgets_missing: budgetsMissing, unsourced_items: unsourced }
}

// ---------------------------------------------------------------- the task

// A committed file under hardware/research/round1/ that a figure comes from:
// plain path segments ending in .json, wherever the path stands in the text.
// session.py cited_returns() reads the same pattern.
const RETURN_FILE = /(?:^|[^A-Za-z0-9_.-])hardware\/research\/round1\/(?:[A-Za-z0-9_-][A-Za-z0-9_.-]*\/)*[A-Za-z0-9_-][A-Za-z0-9_.-]*\.json(?![A-Za-z0-9_./-])/

let summary = {}
if (TASK === 'T6') {
  phase('P7')
  const p7 = await run('P7', '', 'P7', 'P7', p7Prompt())
  const critic = p7 ? await run('P7-critic', '', 'P7-critic', 'P7', p7CriticPrompt(p7)) : null
  summary = { p7: p7 ? p7.checks : null, critic: critic ? critic.checks : null }
  // The pages are final only with both returns and the critic's three
  // checks passing on the tree it leaves; otherwise T6 is stopped.
  const failed = critic ? Object.entries(critic.checks || {}).filter(([, r]) => !(r && r.passed)).map(([k]) => k) : []
  // The critic's own findings, as they stand after its corrections.
  if (critic) {
    // A figure check names its figure and the committed file it comes from,
    // a path ending in .json; session.py record reads the same path.
    const named = (critic.figure_checks || []).filter(x => !readsNone(x.figure) && RETURN_FILE.test(String(x.return_file || '')))
    if (!named.length) failed.push('the critic checked no figure')
    // Each group page and each output under hardware/docs/ carries figures
    // from the returns; each needs at least one figure checked.
    const figPages = [...(A.t6_outputs || []).filter(f => f.startsWith('hardware/docs/')), ...Object.values((p7 && p7.group_pages) || {})]
    for (const f of new Set(figPages)) if (!named.some(x => x.file === f)) failed.push(`no figure checked on ${f}`)
    const wrong = (critic.figure_checks || []).filter(f => !f.agrees)
    if (wrong.length) failed.push(`${wrong.length} figures disagree with the returns`)
    if ((critic.sentence_issues || []).length) failed.push(`${critic.sentence_issues.length} writing issues left`)
  }
  // Every output of the plan is written by P7 and reviewed by its critic.
  const groupPages = Object.values((p7 && p7.group_pages) || {})
  if (p7 && new Set(groupPages).size !== groupPages.length) failed.push('the three group pages are not three files')
  for (const g of groupPages) if (!/^hardware\/docs\/[A-Za-z0-9_-]+\.md$/.test(g)) failed.push(`group page outside hardware/docs/: ${g}`)
  for (const g of groupPages) if ((A.t6_outputs || []).includes(g)) failed.push(`group page is a fixed output: ${g}`)
  const required = [...(A.t6_outputs || []), ...groupPages]
  const unwritten = p7 ? required.filter(f => !(p7.files || []).includes(f)) : []
  const unreviewed = critic ? required.filter(f => !(critic.reviewed || []).includes(f)) : []
  if (p7 && critic && (unwritten.length || unreviewed.length)) failed.push(...unwritten.map(f => `not written: ${f}`), ...unreviewed.map(f => `not reviewed: ${f}`))
  if (!p7 || !critic) summary = { ...summary, stopped: true, reasons: [!p7 ? 'P7 returned nothing' : 'the P7 critic returned nothing'] }
  else if (failed.length) summary = { ...summary, stopped: true, reasons: [`checks failed: ${failed.join(', ')}`] }
} else if (TASK === 'T5') {
  summary = await phaseP5P6()
} else {
  const p0 = await phaseP0()
  if (p0.stop) {
    summary = { stopped: true, reasons: p0.reasons, held: p0.held }
  } else {
    const heldCats = new Set(p0.held.map(h => h.category))
    const cats = taskCats().filter(c => !heldCats.has(c))
    for (const h of p0.held) if (taskCats().includes(h.category)) followUps.push({ role: 'category', category: h.category, reason: `held: ${h.reason} (${h.host})` })
    if (heldCats.size) log(`held by P0: ${[...heldCats].join(', ')}`)
    if (isP1Task) {
      summary = { categories: cats, held: p0.held, ...(await phaseP1(cats)) }
    } else if (TASK === 'FU' && FU.phases === 'P5-P6') {
      summary = await phaseP5P6()
    } else {
      const results = await phaseP2P4(cats)
      // A category whose chain failed selected nothing and verified no
      // figure: it is left out, so the gates read the run before it.
      const done = results.filter(r => r.selection)
      summary = { categories: cats, held: p0.held, results, selection: Object.fromEntries(done.map(r => [r.category, r.selection])),
        figures_open: Object.fromEntries(done.map(r => [r.category, r.figures_open || []])),
        q_missing: done.filter(r => r.q_missing).map(r => r.category),
        chain_failed: results.filter(r => !r.selection).map(r => r.category) }
    }
  }
}

log(`${TASK}: ${started} agents started (${PLANNED} planned, ${extra} of ${FREE} free used); ${followUps.length} items for follow-up`)
return {
  task: TASK, run: A.run || TASK, run_id: A.run_id || '', commit: A.commit, date: A.date, run_info: A.run_info || {}, followup: TASK === 'FU' ? FU : null,
  accept_open: A.accept_open || null, decisions: A.decisions || {},
  planned: PLANNED, started, extra_used: extra, free: FREE, skipped, summary, followUps, missing, returns,
}
