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

function p0Prompt() {
  const j = A.jlcparts || {}
  return `${ctx('P0', '', 'P0')}

Probe every host below with its client and record one row per host, copying the host name and the client name exactly. In url record the URL fetched; for an API client, the command run with its probe. A row with another client, or without the row's probe in url, counts as not probed. A marker is a regular expression the lifecycle status matches; record what it matched. Where a row's probe is null, find a product page of one of that maker's seeds (the seeds are in the category rows and the row's note) and record its URL and whether its status is in the page body. The fields hold, hold_in_t1 and stop are for the script; report reachability only, and do not decide holds from them.
${J(A.hosts)}

Check the parts database: the SHA-256 of ${P.db} is ${j.sha256}; jlc_components holds ${j.rows} rows; every one of these LCSC numbers is in it: ${(j.lcsc || []).join(', ')}. Record in snapshot the created time of ${j.manifest}; it should be ${j.manifest_created}.
Check that ${P.monostable}/hardware/docs/Monostable.md exists and that \`git -C ${P.checkout} cat-file -e 23c82ca6ca976d956098cfafd25dbefa11584f5d\` succeeds.
Record \`git -C ${P.checkout} rev-parse HEAD\` as checkout_head.
Record the versions of Claude Code, git, python3, ruff, 7-Zip and curl_cffi.

This is task ${TASK}. It ${readsStock ? 'reads stock' : 'reads no stock'}. Set stop and list held categories as the P0 row gives them for this task, with the reason.`
}

function p1Prompt(cat) {
  const items = TASK === 'FU' ? itemsFor(cat) : []
  return `${ctx('P1', cat, `P1-${cat}`)}

Read the ${cat} row on the page and every line of hardware/docs/IOBoard.md that belongs to it (search for "${cat}" and for the functions the row names), and every source they cite. Try to refute each value: does it follow from its source, is the unit right, is it an owner decision or an assumption. Mark each. Every value you mark assumption is also a question to the owner; give each value the index of the question that asks for it in "question" (-1 if none). List every requirement value P2 needs to qualify a part that neither page states, as a question to the owner; and every "P1 asks" in the row or in those lines. ${cat === 'R5' ? 'The supply currents of the parts T2 and T4 select are inputs R5 takes from those tasks, not questions. ' : ''}A question whose value only feeds Q4, Q8 or Q9 is blocks "decision-only". A question already under "Raised by P1" is not raised again.${items.length ? `\n\nThis is a follow-up task for these gaps: ${J(items)}` : ''}`
}

function criticPrompt(cat, p1) {
  return `${ctx('P1-critic', cat, `P1-critic-${cat}`)}

You are the critic of P1 for ${cat}. Re-derive each marking and each question below from the sources yourself. Rule on every marking once, copying its where and quantity exactly: holds with correct_marking unchanged, or wrong with the correct marking. Rule on every question once by its 0-based index (confirmed or rejected); a question you give no verdict is sent to a follow-up task, not to the owner. A marking you correct to assumption is also added as a question, with that value's where in for_where. Then look for values P1 did not mark and for missing values P1 did not list, and add each as a question.

P1's return:
${J(p1)}`
}

function recheckPrompt(added) {
  return `${ctx('P1-recheck', '', 'P1-recheck')}

The P1 critics added the questions below. P1 did not raise them. Re-derive each from the same sources and confirm or reject it, citing the evidence. Give a verdict on every one, by its category and its 0-based index in that category's list.
${J(added)}`
}

function p2Prompt(cat) {
  const t3 = ['R5', 'R6', 'R7', 'R8'].includes(cat)
  const items = TASK === 'FU' ? itemsFor(cat) : []
  return `${ctx('P2', cat, `P2-${cat}`)}

Find each value the owner marked "for research" under "Raised by P1" for ${cat} at its primary source, and record it as found. Find candidates as Sourcing rule 3 sets out and keep those from allowlisted makers (rule 2). Drop those that miss a requirement value. Return an entry for every function the ${cat} row and its lines in hardware/docs/IOBoard.md name. For up to five survivors per function, record every field of the P2 row, and the rule-5 route in second_source_route and second_source_part; a part whose route is alternate needs a full record for that alternate among the survivors or in the re-rank. List in "report", one figure each, the figures the ${cat} row asks the category to report for a decision or for P5.${t3 ? ` ${cat === 'R5' ? 'R5 sizes the 3.3 V logic buck and the 5 V rail from the supply currents of the parts T2 and T4 selected, as P4 verified them, plus the display\'s draw.' : ''} The parts earlier tasks selected are in ${P.results}/hardware/research/round1/selection.json; the returns in each run's directory carry their figures.` : ''}${cat === 'R3' && ((A.decisions || {}).Q9 || '').trim() ? `\n\nThe owner decided Q9: ${A.decisions.Q9}. A part it adds or changes is a function of R3 in this run, found and qualified as the others.` : ''}${items.length ? `\n\nThis is a follow-up task. Its items for ${cat}: ${J(items)}` : ''}${TASK === 'FU' ? ` Name each function exactly as ${P.results}/hardware/research/round1/selection.json names it for ${cat}.` : ''}`
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

You are the re-rank of the "Agent layout" table. Qualify each candidate P3 returned as P2 does, and give each qualified one a full record in new_candidates. Then rank each function's shortlist: every entry of ranking names a part of P2's shortlist or of new_candidates, by its exact part number. Rank every such part, or list it with its reason in dropped_from_shortlist (a P2 candidate) or dropped_from_p3 (a P3 candidate). In verify, name the parts P4 must verify: the first-ranked part, the sourcing-rule-5 alternate where the second source is an alternate, and in R10 and R12 the first-ranked part of each Q4 or Q8 alternative. List your own figures in report. A rule-5 alternate that is not on P2's shortlist needs a full record in new_candidates; it is kept as the alternate's record, not ranked. The script builds the final shortlist from P2's records and yours.

P2's return:
${J(p2)}

P3's return:
${J(p3)}`
}

function p4Prompt(cat, kind, bundle, only) {
  const what = only
    ? `Verify only this candidate, which replaces a refuted one, and, when its second_source_route is alternate, the part named in its second_source_part (kind alternate); list only those: ${J(only)}`
    : 'Verify, for each function, the first-ranked part, its rule-5 alternate when its second_source_route is alternate (the part in its second_source_part, kind alternate), every part in its verify list, and the rule-5 alternate of each part of kind q-alternative in that list, in the same way (kind alternate). A first-ranked part that is also such an alternate is listed once, with the checks of an alternate as well. List every part you verify in parts, once; a part you leave out counts as not verified.'
  const how = kind === 'stock'
    ? 'You are the stock and lifecycle verifier. Re-read stock and lifecycle at the primary sources, with the clients above, and try to refute each figure.'
    : `You are the datasheet and pin verifier. Re-read every requirement value in the datasheet; for an alternate, the pin-for-pin match and the functional match to the part it stands in for.${only ? '' : ' Re-read each item of figures_to_check below (the reported figures, the values found for research, and the reasons the re-rank gave for dropping a P3 candidate) and give each a verdict in figures under its exact name; an item without a verdict counts as not verified.'} Try to refute each.`
  return `${ctx('P4', cat, `P4-${kind}-${cat}`)}

${how} ${what} Copy each function and part name exactly as the shortlist below writes it. Name your checks exactly: the stock verifier gives "stock" (the gate's reading, at JLCPCB for a part on the board, at Digi-Key for a part off it), "presale" (JLCPCB, for a part with an LCSC number), "lifecycle status", and "second-vendor stock" (Digi-Key, against the rule-4 gate) for a part on the board whose second_source_route is second-vendor; the datasheet verifier gives one check per entry of the candidate's requirements, named as that entry is, and for a part of kind alternate also "pin-for-pin match" and "functional match". Each check has agrees (the value read matches the value stated) and passes (the value read meets its requirement, or the rule-4, rule-5 or lifecycle gate passes); the stock verifier also gives "end-of-life notices" (passes when no end-of-life or last-time-buy notice exists). A confirmation without its required checks counts as not verified, and a check with agrees or passes false counts as a refutation.

The shortlist, the values found for research and the reports:
${J(bundle)}`
}

function adjudicatorPrompt(cat, fn, part, evidence) {
  return `${ctx('adjudicator', cat, `adjudicator-${cat}`)}

You are the adjudicating critic of the P4 row. A verifier refuted ${part ? `${part} for the function "${fn}"` : `the figure "${fn}"`}. Re-read the evidence yourself at its sources and rule on every refutation below: stands is true when any of them holds. Copy the function "${fn}" and the part "${part}" exactly into your ruling.
${J(evidence)}`
}

function p5Prompt() {
  return `${ctx('P5', '', 'P5')}

Read every return under ${P.results}/hardware/research/round1/ (T2, T3, T4 and every FU-* directory) and run the checks of the P5 row over the whole board. The parts to check are those in ${P.results}/hardware/research/round1/selection.json, each with its alternate, and both alternatives of Q4 and Q8.`
}
function p5CriticPrompt(p5) {
  return `${ctx('P5-critic', '', 'P5-critic')}

You are the critic of P5. Re-derive each conflict, each combination and each budget below by its 0-based index and give every one a verdict (conflict_verdicts as verdicts, combination_verdicts, budget_verdicts); add conflicts P5 missed. P5's return:
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
function readsNone(text) {
  return !text || /^(none|n\/a|-)$/i.test(text.trim()) || /^not read\b/i.test(text.trim())
}

// P0's reading of one host: reachable only when its HTTP status agrees, and
// its lifecycle status read only when the text matches the host's marker. A
// row taken with another client, or not at the host's fixed probe, is not a
// reading of that host.
function probedAsTold(h, row) {
  return row.client === h.client && (h.probe == null || (row.url || '').includes(h.probe))
}

function hostReading(h, row) {
  if (!row) return { up: undefined, status: false }
  if (!probedAsTold(h, row)) return { up: undefined, status: false, why: `not probed with ${h.client}${h.probe == null ? '' : ` at ${h.probe}`}` }
  const ok = row.reachable === true && row.http_status >= 200 && row.http_status < 300
  let status = !readsNone((row.status_marker || '').trim())
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
    if (!c.p1 || !c.critic) continue
    const mv = uniqueVerdicts(c.critic.marking_verdicts, x => `${x.where}\u0000${x.quantity}`, { role: 'P1-critic', category: c.cat })
    c.ruledQ = uniqueVerdicts(c.critic.question_verdicts, x => x.index, { role: 'P1-critic', category: c.cat })
    c.assumptions = []
    c.extra = []
    // A P1 question asks for one value, and a critic addition covers one
    // value at its where: an assumption that shares either is not asked.
    const claimed = new Set()
    const criticAt = new Map()
    for (const q of c.critic.added || []) if (q.for_where) criticAt.set(q.for_where, (criticAt.get(q.for_where) || 0) + 1)
    for (const v of c.p1.values || []) {
      const m = mv.get(`${v.where}\u0000${v.quantity}`)
      let marking = v.marking
      if (!m) followUps.push({ role: 'P1-critic', category: c.cat, value: v, reason: 'marking not ruled on' })
      else if (m.verdict === 'holds' && m.correct_marking !== 'unchanged') { marking = 'assumption'; followUps.push({ role: 'P1-critic', category: c.cat, value: v, reason: 'marking holds with a new marking; taken as an assumption', notice: true }) }
      else if (m.verdict === 'wrong' && (m.correct_marking === 'unchanged' || m.correct_marking === v.marking)) { marking = 'assumption'; followUps.push({ role: 'P1-critic', category: c.cat, value: v, reason: 'marking wrong with no new marking; taken as an assumption', notice: true }) }
      else if (m.verdict === 'wrong') marking = m.correct_marking
      if (marking !== 'assumption') continue
      const ownQ = v.question >= 0 && v.question < (c.p1.questions || []).length && (c.ruledQ.get(v.question) || {}).verdict !== 'rejected' && !claimed.has(v.question)
      if (ownQ) { claimed.add(v.question); c.assumptions.push({ ...v, asked: 'own' }); continue }
      c.assumptions.push({ ...v, asked: 'addition' })
      if ((criticAt.get(v.where) || 0) > 0) { criticAt.set(v.where, criticAt.get(v.where) - 1); continue }
      c.extra.push({ function: v.quantity, question: `State ${v.quantity}. P1 read "${v.value}" at ${v.where} and it is marked an assumption.`, why: 'every assumption is a question to the owner (P1 row)', blocks: 'p2', decision: 'none', for_where: v.where, synthetic: true })
    }
  }
  const added = []
  for (const c of byCat.values()) {
    if (c.critic) [...(c.critic.added || []), ...(c.extra || [])].forEach((q, index) => added.push({ ...q, category: c.cat, index }))
  }
  let recheck = { verdicts: [] }
  if (added.length) recheck = await run('P1-recheck', '', 'P1-recheck', 'P1', recheckPrompt(added))
  else skipped.push({ role: 'P1-recheck', reason: 'no question was added, so none needed a re-check' })
  const rv = recheck ? uniqueVerdicts(recheck.verdicts, x => `${x.category}\u0000${x.index}`, { role: 'P1-recheck' }) : new Map()
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
      } else if (!v) followUps.push({ role: 'P1-critic', category: cat, index: i, question: q.question, reason: 'P1 question not ruled on' })
    })
    ;[...(c.critic.added || []), ...(c.extra || [])].forEach((q, i) => {
      if (!recheck) { followUps.push({ role: 'P1-recheck', category: cat, index: i, question: q.question, reason: 'critic addition not checked' }); return }
      const v = rv.get(`${cat}\u0000${i}`)
      if (!v) followUps.push({ role: 'P1-recheck', category: cat, index: i, question: q.question, reason: 'critic addition not ruled on' })
      else if (v.verdict === 'confirmed') {
        questions.push({ ...q, category: cat, source: 'P1 critic, re-checked' })
        if (q.for_where) confirmedAt.set(q.for_where, (confirmedAt.get(q.for_where) || 0) + 1)
      }
    })
    // Every assumption ends with a confirmed question of its own, or is
    // listed: its own P1 question, or one confirmed addition at its where.
    for (const a of c.assumptions) {
      if (a.asked === 'own' && confirmedIdx.has(a.question)) continue
      if ((confirmedAt.get(a.where) || 0) > 0) { confirmedAt.set(a.where, confirmedAt.get(a.where) - 1); continue }
      const { asked, ...value } = a
      followUps.push({ role: 'P1', category: cat, value, reason: 'assumption without a confirmed question' })
    }
  }
  // A question feeds a decision only when it names Q4, Q8 or Q9; one that
  // names none blocks P2 until answered.
  for (const q of questions) if (q.blocks === 'decision-only' && !['Q4', 'Q8', 'Q9'].includes(q.decision)) {
    followUps.push({ role: 'P1', category: q.category, question: q.question, reason: 'decision-only question names no decision; published as blocking P2', notice: true })
    q.blocks = 'p2'
  }
  questions.forEach((q, i) => { q.id = `V${(A.first_v || 1) + i}` })
  return { questions }
}

// ---------------------------------------------------------------- P2 to P4

// The final shortlist: the re-rank's order over P2's records and the
// re-rank's records of P3's finds.
// Ranks are positions: whole numbers from 1, each given once.
function distinctRanks(list) {
  return list.every(r => Number.isInteger(r.rank) && r.rank >= 1) && new Set(list.map(r => r.rank)).size === list.length
}

function merge(cat, p2, rr, p3) {
  // Every candidate P3 found, and every P2 exclusion P3 overturned, is
  // qualified, ranked or dropped by the re-rank.
  const handled = (fr, part) => fr && ([...(fr.new_candidates || []), ...(fr.dropped_from_p3 || []), ...(fr.dropped_from_shortlist || [])].some(c => c.part === part) || (fr.ranking || []).some(r => r.part === part))
  for (const m of (p3 && p3.missed) || []) {
    const fr = (rr.functions || []).find(f => f.function === m.function)
    if (!handled(fr, m.part)) followUps.push({ role: 'rerank', category: cat, function: m.function, part: m.part, reason: 'P3 candidate neither qualified nor dropped by the re-rank' })
  }
  for (const x of (p3 && p3.exclusions_not_holding) || []) {
    if (!(rr.functions || []).some(fr => handled(fr, x.part))) followUps.push({ role: 'rerank', category: cat, part: x.part, reason: 'P2 exclusion P3 overturned, neither qualified nor dropped by the re-rank' })
  }
  // The functions are P2's; a function only the re-rank names is listed. A
  // function either returns twice is not ranked.
  const names = (p2.functions || []).map(f => f.function)
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
    const need = (f2.requirements || []).map(r => r.name)
    if (!need.length) followUps.push({ role: 'P2', category: cat, function: name, reason: 'function lists no requirement' })
    // A part with more than one record has no single record: dropped.
    const listed = [...(f2.shortlist || []), ...((fr && fr.new_candidates) || [])]
    const pool = listed.filter(c => listed.filter(x => x.part === c.part).length === 1)
    for (const part of new Set(listed.filter(c => !pool.includes(c)).map(c => c.part))) followUps.push({ role: 'P2', category: cat, function: name, part, reason: 'part given more than one record; dropped' })
    let shortlist = []
    let verify = []
    let alternateRecords = []
    // Only the re-rank's ranking orders the shortlist. A function it did not
    // rank, ranked with no part, or ranked with repeated or non-positive
    // positions, and a function with no requirement, is left open for a
    // follow-up task.
    if (!fr) followUps.push({ role: 'rerank', category: cat, function: name, reason: 'the re-rank did not rank this function' })
    else if (!(fr.ranking || []).length) followUps.push({ role: 'rerank', category: cat, function: name, reason: 'the re-rank ranked no part' })
    const unranked = !fr || !(fr.ranking || []).length || p2Twice.has(name) || rrTwice.has(name)
    const badRanks = !unranked && !distinctRanks(fr.ranking)
    if (badRanks) followUps.push({ role: 'rerank', category: cat, function: name, reason: "the re-rank's positions are not distinct ranks from 1; the function is not ranked" })
    if (unranked || badRanks || !need.length) {
      // nothing to verify
    } else {
      const dropped = new Set([...(fr.dropped_from_shortlist || []), ...(fr.dropped_from_p3 || [])].map(d => d.part))
      for (const r of [...(fr.ranking || [])].sort((a, b) => a.rank - b.rank)) {
        const rec = pool.find(c => c.part === r.part)
        if (!rec) { followUps.push({ role: 'rerank', category: cat, function: name, part: r.part, reason: 'ranked part has no record' }); continue }
        if (shortlist.some(x => x.part === r.part)) { followUps.push({ role: 'rerank', category: cat, function: name, part: r.part, reason: 'ranked twice' }); continue }
        if (dropped.has(r.part)) { followUps.push({ role: 'rerank', category: cat, function: name, part: r.part, reason: 'ranked and dropped by the re-rank' }); continue }
        shortlist.push({ ...rec, rank: r.rank, reason: r.reason })
      }
      // A candidate the re-rank neither ranked nor dropped is kept, after the
      // ranked ones, and listed: an omission is not a drop. A record kept as
      // another part's rule-5 alternate stays a record, not a candidate.
      const altNames = new Set([...(fr.verify || []).filter(v => v.kind === 'alternate').map(v => v.part),
        ...pool.filter(c => c.second_source_route === 'alternate').map(c => c.second_source_part)])
      let last = shortlist.length ? shortlist[shortlist.length - 1].rank : 0
      for (const c of pool) {
        if (shortlist.some(x => x.part === c.part) || dropped.has(c.part)) continue
        if (altNames.has(c.part)) { alternateRecords.push(c); continue }
        shortlist.push({ ...c, rank: ++last, reason: 'not ranked by the re-rank; kept in P2 order' })
        followUps.push({ role: 'rerank', category: cat, function: name, part: c.part, reason: 'candidate neither ranked nor dropped' })
      }
      // A part to verify without a shortlist record, such as an alternate
      // named only in a candidate's alternates, is still verified.
      verify = (fr.verify || []).map(v => {
        if (shortlist.some(x => x.part === v.part) || alternateRecords.some(x => x.part === v.part)) return v
        followUps.push({ role: 'rerank', category: cat, function: name, part: v.part, reason: 'part to verify has no record' })
        return { ...v, norecord: true }
      })
    }
    // A candidate or a rule-5 alternate is checked against every requirement
    // of its function; one it does not list is added as unmet, so the
    // verifier must check it. A record that fails a requirement is dropped.
    const qualify = (list, what) => list.filter(c => {
      const failing = (c.requirements || []).filter(r => r.pass === false)
      if (!failing.length) return true
      followUps.push({ role: 'P2', category: cat, function: name, part: c.part, reason: `${what} with a failed requirement: ${failing.map(r => r.name).join(', ')}` })
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
    functions.push({ function: name, requirements: f2.requirements || [], shortlist, verify, alternateRecords, dropped: f2.dropped || [], dropped_from_shortlist: (fr && fr.dropped_from_shortlist) || [], dropped_from_p3: (fr && fr.dropped_from_p3) || [] })
  }
  // R10 and R12 carry the owner's choice for Q4 and Q8: one of their
  // functions names an alternative to verify, or the category stays open.
  if (['R10', 'R12'].includes(cat) && !functions.some(f => f.verify.some(v => v.kind === 'q-alternative'))) {
    followUps.push({ role: 'rerank', category: cat, reason: `names no ${cat === 'R10' ? 'Q4' : 'Q8'} alternative to verify` })
  }
  // A function the row names that P2 returned no entry for stays open, with
  // no part, for a follow-up task.
  for (const m of (p3 && p3.missed_functions) || []) {
    if (functions.some(f => f.function === m.function)) continue
    followUps.push({ role: 'P3', category: cat, function: m.function, reason: `function the row names that P2 did not return: ${m.why}` })
    functions.push({ function: m.function, requirements: [], shortlist: [], verify: [], alternateRecords: [], dropped: [], dropped_from_shortlist: [], dropped_from_p3: [], not_returned: true })
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

// The checks a verifier returns by these exact names for a confirmation to
// count (the P4 prompt names them).
function requiredChecks(kind, cand, partKind) {
  if (kind === 'stock') {
    const onBoard = cand && cand.lcsc && cand.lcsc !== 'none'
    return ['stock', 'lifecycle status', 'end-of-life notices', ...(onBoard ? ['presale'] : []),
      ...(onBoard && cand.second_source_route === 'second-vendor' ? ['second-vendor stock'] : [])]
  }
  return [...((cand && cand.requirements) || []).map(r => r.name), ...(partKind === 'alternate' ? ['pin-for-pin match', 'functional match'] : [])]
}

function covered(v, cand) {
  // A part with no record cannot show its gate evidence, and a datasheet
  // confirmation with nothing to check shows none.
  if (!cand) return false
  const req = requiredChecks(v.verifier, cand, v.kind)
  if (v.verifier === 'datasheet' && !req.length) return false
  // A check written as not read shows nothing.
  const have = new Set((v.checks || []).filter(c => !readsNone(c.read)).map(c => c.figure))
  return req.every(n => have.has(n))
}

// The next part in rank order that is not refuted.
function nextCandidate(functions, fn, part, ledger) {
  const f = functions.find(x => x.function === fn)
  if (!f) return null
  const status = new Map()
  for (const l of ledger) if (l.part && l.function === fn) status.set(l.part, l.status)
  const i = f.shortlist.findIndex(c => c.part === part)
  if (i < 0) return null
  return f.shortlist.slice(i + 1).find(c => status.get(c.part) !== 'refuted') || null
}

// The adjudicator's ruling counts only for the item it was asked about.
function boundRuling(ruling, fn, part, cat) {
  if (!ruling) return null
  if (ruling.function === fn && (ruling.part || '') === (part || '')) return ruling
  followUps.push({ role: 'adjudicator', category: cat, function: fn, part, reason: 'the ruling names another item' })
  return null
}

async function verifyCategory(cat, functions, bundle) {
  const ledger = []
  const queue = [{ only: null }]
  while (queue.length) {
    const { only } = queue.shift()
    const phaseName = only ? 'Refutations' : 'P2-P4'
    const [st, ds] = await parallel([
      () => run('P4', cat, `P4-stock-${cat}`, phaseName, p4Prompt(cat, 'stock', bundle, only)),
      () => run('P4', cat, `P4-datasheet-${cat}`, phaseName, p4Prompt(cat, 'datasheet', bundle, only)),
    ])
    // Each part to verify, with its kind from the verification plan, and
    // the rule-5 alternate of the part that would be selected.
    const want = []
    if (only) {
      want.push({ function: only.function, part: only.part, kind: 'first' })
      if (altOf(only)) want.push({ function: only.function, part: altOf(only), kind: 'alternate' })
    } else {
      for (const f of functions) {
        const kinds = new Map()
        if (f.shortlist.length) {
          kinds.set(f.shortlist[0].part, 'first')
          if (altOf(f.shortlist[0])) kinds.set(altOf(f.shortlist[0]), 'alternate')
        }
        for (const v of f.verify) if (!kinds.has(v.part) || v.kind === 'alternate') kinds.set(v.part, v.kind)
        // A Q4 or Q8 alternative needs its own second source, as the part
        // kept does.
        const alsoAlternate = new Set()
        for (const v of f.verify.filter(x => x.kind === 'q-alternative')) {
          const a = altOf(candidateOf(functions, f.function, v.part))
          if (a && kinds.get(a) === 'first') alsoAlternate.add(a)
          else if (a) kinds.set(a, 'alternate')
        }
        // A first-ranked part that is also an alternate is checked as one.
        for (const [part, kind] of kinds) want.push({ function: f.function, part, kind: alsoAlternate.has(part) ? 'alternate' : kind })
      }
    }
    const got = [['stock', st], ['datasheet', ds]].filter(([, v]) => v)
    if (got.length < 2) followUps.push({ role: 'P4', category: cat, part: only ? only.part : '', reason: 'a verifier returned nothing; what the other returned is still adjudicated' })
    const byPart = new Map()
    const key = (f, p) => `${f}\u0000${p}`
    for (const w of want) byPart.set(key(w.function, w.part), { ...w, verdicts: [] })
    for (const [kind, v] of got) {
      const seenHere = new Set()
      for (const p of v.parts || []) {
        if (only && (p.function !== only.function || (p.part !== only.part && p.part !== altOf(only)))) continue
        const k = key(p.function, p.part)
        if (seenHere.has(k)) { followUps.push({ role: 'P4', category: cat, function: p.function, part: p.part, reason: `${kind} verifier listed the part twice` }); continue }
        seenHere.add(k)
        if (!byPart.has(k)) { followUps.push({ role: 'P4', category: cat, function: p.function, part: p.part, reason: `${kind} verifier listed a part it was not asked to verify; ignored` }); continue }
        const e = byPart.get(k)
        e.verdicts.push({ verifier: kind, kind: e.kind, reported_kind: p.kind, verdict: p.verdict, refutation: p.refutation, checks: p.checks })
        byPart.set(k, e)
      }
    }
    for (const e of byPart.values()) {
      // A confirmation counts only with its required checks, each reading
      // the stated figure and passing its requirement or gate. A check that
      // disagrees or fails is a refutation.
      const cand = candidateOf(functions, e.function, e.part)
      for (const v of e.verdicts) {
        if (v.verdict !== 'confirmed') continue
        const bad = (v.checks || []).filter(c => !c.agrees || c.passes !== true)
        if (bad.length) { v.verdict = 'refuted'; v.refutation = `checks disagree or fail: ${bad.map(c => c.figure).join(', ')}`; continue }
        if (!covered(v, cand)) { v.verdict = 'incomplete'; v.missing = requiredChecks(v.verifier, cand, v.kind).filter(n => !(v.checks || []).some(c => c.figure === n)) }
      }
      // Coverage needs the required checks whatever the verdict, so an
      // overturned refutation without them does not count as confirmation.
      const complete = kind => e.verdicts.some(v => v.verifier === kind && v.verdict !== 'incomplete' && covered(v, cand))
      const refuted = e.verdicts.filter(v => v.verdict === 'refuted')
      if (!refuted.length) {
        if (complete('stock') && complete('datasheet')) { ledger.push({ function: e.function, part: e.part, status: 'verified' }); continue }
        const reason = e.verdicts.some(v => v.verdict === 'incomplete') ? 'a verifier gave no required check for some figures' : 'not covered by both verifiers'
        ledger.push({ function: e.function, part: e.part, status: 'not verified', reason })
        followUps.push({ role: 'P4', category: cat, function: e.function, part: e.part, reason })
        continue
      }
      if (!takeExtra(1)) {
        ledger.push({ function: e.function, part: e.part, status: 'refuted, not adjudicated' })
        followUps.push({ role: 'adjudicator', category: cat, function: e.function, part: e.part, reason: 'no free agent' })
        continue
      }
      const ruling = boundRuling(await run('adjudicator', cat, `adjudicator-${cat}`, 'Refutations', adjudicatorPrompt(cat, e.function, e.part, e.verdicts)), e.function, e.part, cat)
      if (!ruling) {
        ledger.push({ function: e.function, part: e.part, status: 'refuted, no ruling' })
        followUps.push({ role: 'adjudicator', category: cat, function: e.function, part: e.part, reason: 'refutation without a ruling' })
        continue
      }
      if (!ruling.stands) {
        const both = complete('stock') && complete('datasheet')
        ledger.push({ function: e.function, part: e.part, status: both ? 'verified; refutation did not stand' : 'not verified', reason: both ? '' : 'refutation did not stand, but one verifier did not cover it' })
        if (!both) followUps.push({ role: 'P4', category: cat, function: e.function, part: e.part, reason: 'not covered by both verifiers' })
        continue
      }
      ledger.push({ function: e.function, part: e.part, status: 'refuted' })
      const next = nextCandidate(functions, e.function, e.part, ledger)
      if (!next) { followUps.push({ role: 'P2', category: cat, function: e.function, part: e.part, reason: 'refuted, and no next-ranked candidate' }); continue }
      // The pair is paid for here from the free agents; run() starts it.
      if (!takeExtra(2)) { followUps.push({ role: 'P4', category: cat, function: e.function, part: next.part, reason: 'no free agents for the next pair' }); continue }
      queue.push({ only: { ...next, function: e.function } })
    }
    if (only) continue
    // Figures are verified once, by the first pair. Each name in
    // figures_to_check needs one verdict from the datasheet verifier.
    const figs = uniqueVerdicts((ds && ds.figures) || [], f => f.figure, { role: 'P4', category: cat })
    for (const name of bundle.figures_to_check) {
      if (figs.has(name)) continue
      ledger.push({ figure: name, status: 'not verified', reason: 'the datasheet verifier gave no single verdict' })
      followUps.push({ role: 'P4', category: cat, figure: name, reason: 'figure not verified' })
    }
    for (const [kind, v] of got) {
      for (const f of v.figures || []) {
        if (f.verdict !== 'refuted') continue
        if (!takeExtra(1)) {
          ledger.push({ figure: f.figure, status: 'refuted, not adjudicated' })
          followUps.push({ role: 'adjudicator', category: cat, figure: f.figure, reason: 'no free agent' })
          continue
        }
        const ruling = boundRuling(await run('adjudicator', cat, `adjudicator-${cat}`, 'Refutations', adjudicatorPrompt(cat, f.figure, '', [{ verifier: kind, ...f }])), f.figure, '', cat)
        const status = !ruling ? 'refuted, no ruling' : ruling.stands ? 'refuted' : 'confirmed; refutation did not stand'
        ledger.push({ figure: f.figure, status })
        if (!ruling || ruling.stands) followUps.push({ role: 'P2', category: cat, figure: f.figure, reason: ruling ? 'figure refuted' : 'figure refuted without a ruling' })
      }
    }
  }
  return ledger
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
  // an earlier verification.
  const last = new Map()
  for (const l of ledger) if (l.part && last.get(`${l.function}\u0000${l.part}`) !== 'refuted') last.set(`${l.function}\u0000${l.part}`, l.status)
  return functions.map(f => {
    const st = c => last.get(`${f.function}\u0000${c.part}`) || ''
    const standing = f.shortlist.find(c => st(c) !== 'refuted')
    const kept = standing && st(standing).startsWith('verified') ? standing : null
    const refuted = f.shortlist.filter(c => st(c) === 'refuted').map(c => c.part)
    // Only the kept part's rule-5 alternate sources it; the alternate of a
    // refuted part does not.
    const alternateUnverified = altOf(kept) && !st({ part: altOf(kept) }).startsWith('verified') ? [altOf(kept)] : []
    const recOf = part => f.shortlist.find(c => c.part === part) || (f.alternateRecords || []).find(c => c.part === part) || null
    const qAlternatives = f.verify.filter(v => v.kind === 'q-alternative' && !(kept && v.part === kept.part)).map(v => {
      const rec = recOf(v.part)
      const alt = altOf(rec)
      return { part: v.part, status: st(v) || 'not verified', alternate: alt,
        alternate_status: alt ? (st({ part: alt }) || 'not verified') : '', second_source_missing: !rec || noSecondSource(rec) }
    })
    const missing = !!(kept && noSecondSource(kept))
    return { function: f.function, part: kept ? kept.part : null, rank: kept ? kept.rank : null, refuted,
      alternate_unverified: alternateUnverified, second_source_missing: missing, q_alternatives: qAlternatives }
  })
}

// The figures the datasheet verifier rules on: each reported figure and each
// value found for research under a unique name, and the figures the category
// must report and the values the owner marked for research, whether or not
// P2 returned them.
function figuresToCheck(cat, p2, rr, functions, p3) {
  const names = []
  const missing = []
  const add = n => { names.push(names.includes(n) ? `${n} (${names.filter(x => x.startsWith(n)).length + 1})` : n) }
  const read = (p2.found_values || []).filter(v => !readsNone(v.value))
  const reported = (p2.report || []).filter(f => !readsNone(f.value))
  for (const v of read) add(`found ${v.question_id}`)
  for (const q of (A.for_research || []).filter(q => q.category === cat)) if (!read.some(v => v.question_id === q.id)) {
    missing.push(`found ${q.id}`)
    followUps.push({ role: 'P2', category: cat, question: q.id, reason: 'value marked for research not found' })
  }
  for (const f of reported) add(`P2 report: ${f.figure}`)
  for (const need of (A.required_reports || {})[cat] || []) if (!reported.some(f => f.figure === need)) {
    missing.push(`P2 report: ${need}`)
    followUps.push({ role: 'P2', category: cat, figure: need, reason: 'required report figure not returned' })
  }
  for (const f of rr.report || []) add(`re-rank report: ${f.figure}`)
  // Every drop of a part P3 found or reopened, whichever list the re-rank
  // put it in.
  const fromP3 = new Set([...((p3 && p3.missed) || []), ...((p3 && p3.exclusions_not_holding) || [])].map(x => x.part))
  const drops = new Set()
  for (const f of functions) for (const d of [...f.dropped_from_p3, ...f.dropped_from_shortlist]) if (d.part && (fromP3.has(d.part) || f.dropped_from_p3.includes(d)) && !drops.has(d.part)) { drops.add(d.part); add(`re-rank drop: ${d.part}`) }
  return { names, missing }
}

async function categoryChain(cat) {
  const p2 = await run('P2', cat, `P2-${cat}`, 'P2-P4', p2Prompt(cat))
  if (!p2) return { category: cat, status: 'P2 returned nothing' }
  const p3 = await run('P3', cat, `P3-${cat}`, 'P2-P4', p3Prompt(cat, p2))
  const rr = await run('rerank', cat, `rerank-${cat}`, 'P2-P4', rerankPrompt(cat, p2, p3 || { category: cat, missed: [], exclusions_not_holding: [], note: 'P3 returned nothing twice; its search is a follow-up item' }))
  if (!rr) return { category: cat, status: 're-rank returned nothing' }
  const functions = merge(cat, p2, rr, p3)
  const { names: figures_to_check, missing: not_returned } = figuresToCheck(cat, p2, rr, functions, p3)
  const bundle = { functions, found_values: p2.found_values || [], report_p2: p2.report || [], report_rerank: rr.report || [], figures_to_check }
  const ledger = await verifyCategory(cat, functions, bundle)
  // Without P3's search the category is not complete: no part is kept, and
  // the part that would have been is named for the follow-up task.
  const sel = selection(functions, ledger)
  // A figure is confirmed unless the ledger holds another status for it.
  // What P2 did not return has nothing to verify and stays open.
  const figures_open = [...new Set([...not_returned, ...ledger.filter(l => l.figure && !l.status.startsWith('confirmed')).map(l => l.figure)])]
  const q_missing = ['R10', 'R12'].includes(cat) && !sel.some(e => (e.q_alternatives || []).length)
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
    conflicts: critiqued('P5', p5.conflicts || [], critic),
    combinations: ruled(p5.combinations || [], critic && critic.combination_verdicts, critic, 'P5'),
    budgets: ruled(p5.budgets || [], critic && critic.budget_verdicts, critic, 'P5'),
  }
}
async function p6Chain() {
  const p6 = await run('P6', '', 'P6', 'P5-P6', p6Prompt())
  if (!p6) return { gaps: [], missing: 'P6' }
  const critic = await run('P6-critic', '', 'P6-critic', 'P5-P6', p6CriticPrompt(p6))
  return { gaps: critiqued('P6', p6.gaps || [], critic) }
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
  const items = [...(a.combinations || []), ...(a.budgets || [])]
  const unchecked = items.filter(x => x.unchecked).length
  // A combination or budget the critic rejected is not on the pages, so it
  // is unresolved until a later check replaces it.
  const rejected = items.filter(x => x.upheld === false)
  for (const x of rejected) followUps.push({ role: 'P5', item: x, reason: 'combination or budget the critic rejected' })
  const missingChecks = [a.missing, b.missing].filter(Boolean)
  for (const m of missingChecks) followUps.push({ role: m, reason: `${m} returned nothing; the check did not run` })
  return { conflicts: a.conflicts, combinations: a.combinations || [], budgets: a.budgets || [], gaps: b.gaps,
    missing_checks: missingChecks, unchecked_items: unchecked, rejected_items: rejected.length }
}

// ---------------------------------------------------------------- the task

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
    if (!(critic.figure_checks || []).length) failed.push('the critic checked no figure')
    // Each group page and each output under hardware/docs/ carries figures
    // from the returns; each needs at least one figure checked.
    const figPages = [...(A.t6_outputs || []).filter(f => f.startsWith('hardware/docs/')), ...Object.values((p7 && p7.group_pages) || {})]
    for (const f of new Set(figPages)) if (!(critic.figure_checks || []).some(x => x.file === f)) failed.push(`no figure checked on ${f}`)
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
