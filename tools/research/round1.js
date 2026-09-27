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
    `Your instructions are hardware/docs/Research.md in the read-only checkout ${P.checkout} at commit ${A.commit}: ${rows}, the Sourcing rules, "Held parts" and "Lifecycle check". The requirement values are in hardware/docs/IOBoard.md in the same checkout and in the Answer columns of Research.md there, "Raised by P1" among them; a proposed answer is not an answer. Where this prompt and the page differ, the page holds.`,
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

Probe every host below with its client and record one row per host. A marker is a regular expression the lifecycle status matches; record what it matched. Where a row's probe is null, find a product page of one of that maker's seeds (the seeds are in the category rows and the row's note) and record its URL and whether its status is in the page body. The fields hold, hold_in_t1 and stop are for the script; report reachability only, and do not decide holds from them.
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

Read the ${cat} row on the page and every line of hardware/docs/IOBoard.md that belongs to it (search for "${cat}" and for the functions the row names), and every source they cite. Try to refute each value: does it follow from its source, is the unit right, is it an owner decision or an assumption. Mark each. Every value you mark assumption is also a question to the owner. List every requirement value P2 needs to qualify a part that neither page states, as a question to the owner; and every "P1 asks" in the row or in those lines. ${cat === 'R5' ? 'The supply currents of the parts T2 and T4 select are inputs R5 takes from those tasks, not questions. ' : ''}A question whose value only feeds Q4, Q8 or Q9 is blocks "decision-only". A question already under "Raised by P1" is not raised again.${items.length ? `\n\nThis is a follow-up task for these gaps: ${J(items)}` : ''}`
}

function criticPrompt(cat, p1) {
  return `${ctx('P1-critic', cat, `P1-critic-${cat}`)}

You are the critic of P1 for ${cat}. Re-derive each marking and each question below from the sources yourself. Rule on every marking (holds or wrong) and on every question by its 0-based index (confirmed or rejected); a question you give no verdict is sent to a follow-up task, not to the owner. A marking you correct to assumption is also added as a question. Then look for values P1 did not mark and for missing values P1 did not list, and add each as a question.

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

Find each value the owner marked "for research" under "Raised by P1" for ${cat} at its primary source, and record it as found. Find candidates as Sourcing rule 3 sets out and keep those from allowlisted makers (rule 2). Drop those that miss a requirement value. For up to five survivors per function, record every field of the P2 row. List in "report", one figure each, the figures the ${cat} row asks the category to report for a decision or for P5.${t3 ? ` ${cat === 'R5' ? 'R5 sizes the 3.3 V logic buck and the 5 V rail from the supply currents of the parts T2 and T4 selected, as P4 verified them, plus the display\'s draw.' : ''} The parts earlier tasks selected are in ${P.results}/hardware/research/round1/selection.json; the returns in each run's directory carry their figures.` : ''}${items.length ? `\n\nThis is a follow-up task. Its items for ${cat}: ${J(items)}` : ''}`
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

Search for part families P2 did not consider, from the same allowlist, and re-read each reason P2 gave for dropping a candidate. P2's shortlist and drops (its full records are with the session):
${J(p2View(p2))}`
}

function rerankPrompt(cat, p2, p3) {
  return `${ctx('rerank', cat, `rerank-${cat}`)}

You are the re-rank of the "Agent layout" table. Qualify each candidate P3 returned as P2 does, and give each qualified one a full record in new_candidates. Then rank each function's shortlist: every entry of ranking names a part of P2's shortlist or of new_candidates, by its exact part number. Rank every such part, or list it with its reason in dropped_from_shortlist (a P2 candidate) or dropped_from_p3 (a P3 candidate). In verify, name the parts P4 must verify: the first-ranked part, the sourcing-rule-5 alternate where the second source is an alternate, and in R10 and R12 the first-ranked part of each Q4 or Q8 alternative. List your own figures in report. The script builds the final shortlist from P2's records and yours.

P2's return:
${J(p2)}

P3's return:
${J(p3)}`
}

function p4Prompt(cat, kind, bundle, only) {
  const what = only
    ? `Verify only this candidate, which replaces a refuted one; list only it: ${J(only)}`
    : 'Verify, for each function, the first-ranked part and every part in its verify list. List every part you verify in parts; a part you leave out counts as not verified.'
  const how = kind === 'stock'
    ? 'You are the stock and lifecycle verifier. Re-read stock and lifecycle at the primary sources, with the clients above, and try to refute each figure.'
    : `You are the datasheet and pin verifier. Re-read every requirement value in the datasheet; for an alternate, the pin-for-pin match and the functional match to the part it stands in for.${only ? '' : ' Re-read each item of figures_to_check below (the reported figures, the values found for research, and the reasons the re-rank gave for dropping a P3 candidate) and give each a verdict in figures under its exact name; an item without a verdict counts as not verified.'} Try to refute each.`
  return `${ctx('P4', cat, `P4-${kind}-${cat}`)}

${how} ${what} Copy each function and part name exactly as the shortlist below writes it. Name your checks exactly: the stock verifier gives "stock" (the gate's reading, at JLCPCB for a part on the board, at Digi-Key for a part off it), "presale" (JLCPCB, for a part with an LCSC number) and "lifecycle status"; the datasheet verifier gives one check per entry of the candidate's requirements, named as that entry is. A confirmation without its required checks counts as not verified, and a check with agrees false counts as a refutation.

The shortlist, the values found for research and the reports:
${J(bundle)}`
}

function adjudicatorPrompt(cat, fn, part, evidence) {
  return `${ctx('adjudicator', cat, `adjudicator-${cat}`)}

You are the adjudicating critic of the P4 row. A verifier refuted ${part ? `${part} for the function "${fn}"` : `the figure "${fn}"`}. Re-read the evidence yourself at its sources and rule whether the refutation stands.
${J(evidence)}`
}

function p5Prompt() {
  return `${ctx('P5', '', 'P5')}

Read every return under ${P.results}/hardware/research/round1/ (T2, T3, T4 and every FU-* directory) and run the checks of the P5 row over the whole board. The parts to check are those in ${P.results}/hardware/research/round1/selection.json, each with its alternate, and both alternatives of Q4 and Q8.`
}
function p5CriticPrompt(p5) {
  return `${ctx('P5-critic', '', 'P5-critic')}

You are the critic of P5. Re-derive each conflict below by its 0-based index and give every one a verdict; add conflicts P5 missed. P5's return:
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

Exception to the rule above: write the outputs listed under "Outputs" of Research.md into the working tree ${P.results}, which the session has merged with research/round1 before this task. Write them from the returns under ${P.results}/hardware/research/round1/ only, the parts in hardware/research/round1/selection.json, P5's budget and combinations, and the owner's decisions in the "Decision (owner, date)" column for Q4, Q8 and Q9. Do not commit. Write tools/jlc_stock.py to read DIGIKEY_ENV_FILE as well as the two variables, as tools/research/vendors.py does. Run \`python3 tools/check_docs.py\`, \`ruff check tools/\` and \`DIGIKEY_ENV_FILE=${P.digikey_env} python3 tools/jlc_stock.py --check 5\` in ${P.results} and report each result.`
}
function p7CriticPrompt(p7) {
  return `${ctx('P7-critic', '', 'P7-critic')}

You are the critic of P7. Exception to the rule above: you may correct the pages in ${P.results}; do not commit. Check every figure and every stated combination on the pages P7 wrote against the returns under ${P.results}/hardware/research/round1/, and every sentence against the writing rules in CONTRIBUTING.md. Apply the corrections, then run the three checks P7 ran and report them. P7's return:
${J(p7)}`
}

// ---------------------------------------------------------------- P0

// P0 has no critic, so the script applies the stop and hold rules itself
// from P0's check results and the host table, beside P0's own reading.
function applyP0(p0) {
  const j = A.jlcparts || {}
  const reasons = []
  if (!p0.jlcparts || !p0.jlcparts.sha256_ok || p0.jlcparts.sha256 !== j.sha256) reasons.push('the parts database fails its SHA-256 check')
  if (!p0.jlcparts || p0.jlcparts.rows !== j.rows) reasons.push('the parts database row count differs')
  if (!p0.jlcparts || (p0.jlcparts.missing_lcsc || []).length) reasons.push('LCSC numbers the page names are missing from the parts database')
  if (!(Date.parse(p0.snapshot) === Date.parse(j.manifest_created))) reasons.push(`the parts database's manifest is ${p0.snapshot}, not ${j.manifest_created}`)
  if (p0.checkout_head !== A.commit) reasons.push(`the checkout is at ${p0.checkout_head}, not ${A.commit}`)
  if (TASK === 'T1' && !(p0.monostable && p0.monostable.fetched)) reasons.push('commit 23c82ca is not in place')
  const reach = new Map((p0.hosts || []).map(h => [h.host, h.reachable]))
  const held = new Map()
  for (const h of A.hosts || []) {
    const up = reach.get(h.host)
    if (up === true) continue
    const why = up === false ? 'unreachable' : 'not probed'
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
  // the re-check checks it with the critic's additions.
  for (const c of byCat.values()) {
    if (!c.p1 || !c.critic) continue
    const verdicts = c.critic.marking_verdicts || []
    const asked = [...(c.p1.questions || []), ...(c.critic.added || [])].map(q => `${q.function} ${q.question}`.toLowerCase())
    c.extra = []
    for (const v of c.p1.values || []) {
      const mv = verdicts.find(x => x.where === v.where && x.quantity === v.quantity)
      if (!mv) followUps.push({ role: 'P1-critic', category: c.cat, value: v, reason: 'marking not ruled on' })
      const marking = mv && mv.verdict === 'wrong' && mv.correct_marking !== 'unchanged' ? mv.correct_marking : v.marking
      if (marking !== 'assumption' || asked.some(a => a.includes(String(v.quantity).toLowerCase()))) continue
      c.extra.push({ function: v.quantity, question: `State ${v.quantity}. P1 read "${v.value}" at ${v.where} and it is marked an assumption.`, why: 'every assumption is a question to the owner (P1 row)', blocks: 'p2', decision: 'none', synthetic: true })
    }
  }
  const added = []
  for (const c of byCat.values()) {
    if (c.critic) [...(c.critic.added || []), ...(c.extra || [])].forEach((q, index) => added.push({ category: c.cat, index, ...q }))
  }
  let recheck = { verdicts: [] }
  if (added.length) recheck = await run('P1-recheck', '', 'P1-recheck', 'P1', recheckPrompt(added))
  else skipped.push({ role: 'P1-recheck', reason: 'no question was added, so none needed a re-check' })
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
    const ruled = new Map((c.critic.question_verdicts || []).map(v => [v.index, v.verdict]))
    ;(c.p1.questions || []).forEach((q, i) => {
      if (ruled.get(i) === 'confirmed') questions.push({ category: cat, source: 'P1', ...q })
      else if (!ruled.has(i)) followUps.push({ role: 'P1-critic', category: cat, index: i, question: q.question, reason: 'P1 question not ruled on' })
    })
    ;[...(c.critic.added || []), ...(c.extra || [])].forEach((q, i) => {
      if (!recheck) { followUps.push({ role: 'P1-recheck', category: cat, index: i, question: q.question, reason: 'critic addition not checked' }); return }
      const v = (recheck.verdicts || []).find(x => x.category === cat && x.index === i)
      if (!v) followUps.push({ role: 'P1-recheck', category: cat, index: i, question: q.question, reason: 'critic addition not ruled on' })
      else if (v.verdict === 'confirmed') questions.push({ category: cat, source: 'P1 critic, re-checked', ...q })
    })
  }
  questions.forEach((q, i) => { q.id = `V${(A.first_v || 1) + i}` })
  return { questions }
}

// ---------------------------------------------------------------- P2 to P4

// The final shortlist: the re-rank's order over P2's records and the
// re-rank's records of P3's finds.
function merge(cat, p2, rr) {
  const functions = []
  const names = new Set([...(p2.functions || []).map(f => f.function), ...(rr.functions || []).map(f => f.function)])
  for (const name of names) {
    const f2 = (p2.functions || []).find(f => f.function === name) || {}
    const fr = (rr.functions || []).find(f => f.function === name)
    const pool = [...(f2.shortlist || []), ...((fr && fr.new_candidates) || [])]
    let shortlist
    let verify = []
    if (fr) {
      shortlist = []
      for (const r of [...(fr.ranking || [])].sort((a, b) => a.rank - b.rank)) {
        const rec = pool.find(c => c.part === r.part)
        if (!rec) { followUps.push({ role: 'rerank', category: cat, function: name, part: r.part, reason: 'ranked part has no record' }); continue }
        shortlist.push({ ...rec, rank: r.rank, reason: r.reason })
      }
      // A candidate the re-rank neither ranked nor dropped is kept, after the
      // ranked ones, and listed: an omission is not a drop.
      const dropped = new Set((fr.dropped_from_shortlist || []).map(d => d.part))
      let last = shortlist.length ? shortlist[shortlist.length - 1].rank : 0
      for (const c of pool) {
        if (shortlist.some(x => x.part === c.part) || dropped.has(c.part)) continue
        shortlist.push({ ...c, rank: ++last, reason: 'not ranked by the re-rank; kept in P2 order' })
        followUps.push({ role: 'rerank', category: cat, function: name, part: c.part, reason: 'candidate neither ranked nor dropped' })
      }
      verify = (fr.verify || []).filter(v => {
        if (shortlist.some(x => x.part === v.part)) return true
        followUps.push({ role: 'rerank', category: cat, function: name, part: v.part, reason: 'part to verify is not on the shortlist' })
        return false
      })
    } else {
      shortlist = [...(f2.shortlist || [])].sort((a, b) => a.rank - b.rank)
      followUps.push({ role: 'rerank', category: cat, function: name, reason: 'the re-rank did not rank this function' })
    }
    functions.push({ function: name, requirements: f2.requirements || [], shortlist, verify, dropped: f2.dropped || [], dropped_from_shortlist: (fr && fr.dropped_from_shortlist) || [], dropped_from_p3: (fr && fr.dropped_from_p3) || [] })
  }
  return functions
}

function candidateOf(functions, fn, part) {
  const f = functions.find(x => x.function === fn)
  return f ? f.shortlist.find(c => c.part === part) : null
}

// The checks a verifier returns by these exact names for a confirmation to
// count (the P4 prompt names them).
function requiredChecks(kind, cand) {
  if (kind === 'stock') return ['stock', 'lifecycle status', ...(cand && cand.lcsc && cand.lcsc !== 'none' ? ['presale'] : [])]
  return ((cand && cand.requirements) || []).map(r => r.name)
}

function nextCandidate(functions, fn, part) {
  const f = functions.find(x => x.function === fn)
  if (!f) return null
  const i = f.shortlist.findIndex(c => c.part === part)
  return i >= 0 && i + 1 < f.shortlist.length ? f.shortlist[i + 1] : null
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
    const want = []
    if (only) want.push({ function: only.function, part: only.part })
    else {
      for (const f of functions) {
        const parts = new Set([...(f.shortlist.length ? [f.shortlist[0].part] : []), ...f.verify.map(v => v.part)])
        for (const part of parts) want.push({ function: f.function, part })
      }
    }
    const got = [['stock', st], ['datasheet', ds]].filter(([, v]) => v)
    if (got.length < 2) followUps.push({ role: 'P4', category: cat, part: only ? only.part : '', reason: 'a verifier returned nothing; what the other returned is still adjudicated' })
    const byPart = new Map()
    const key = (f, p) => `${f}\u0000${p}`
    for (const w of want) byPart.set(key(w.function, w.part), { ...w, verdicts: [] })
    for (const [kind, v] of got) {
      for (const p of v.parts || []) {
        if (only && (p.function !== only.function || p.part !== only.part)) continue
        const k = key(p.function, p.part)
        const e = byPart.get(k) || { function: p.function, part: p.part, verdicts: [] }
        e.verdicts.push({ verifier: kind, verdict: p.verdict, refutation: p.refutation, checks: p.checks })
        byPart.set(k, e)
      }
    }
    for (const e of byPart.values()) {
      // A confirmation counts only with its required checks, each agreeing.
      // A check that disagrees is a refutation.
      const cand = candidateOf(functions, e.function, e.part)
      for (const v of e.verdicts) {
        if (v.verdict !== 'confirmed') continue
        const bad = (v.checks || []).filter(c => !c.agrees)
        if (bad.length) { v.verdict = 'refuted'; v.refutation = `checks disagree: ${bad.map(c => c.figure).join(', ')}`; continue }
        const have = new Set((v.checks || []).map(c => c.figure))
        const lack = requiredChecks(v.verifier, cand).filter(n => !have.has(n))
        if (lack.length) { v.verdict = 'incomplete'; v.missing = lack }
      }
      const complete = kind => e.verdicts.some(v => v.verifier === kind && v.verdict !== 'incomplete')
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
      const ruling = await run('adjudicator', cat, `adjudicator-${cat}`, 'Refutations', adjudicatorPrompt(cat, e.function, e.part, e.verdicts))
      if (!ruling) {
        ledger.push({ function: e.function, part: e.part, status: 'refuted, adjudicator returned nothing' })
        continue
      }
      if (!ruling.stands) {
        const both = complete('stock') && complete('datasheet')
        ledger.push({ function: e.function, part: e.part, status: both ? 'verified; refutation did not stand' : 'not verified', reason: both ? '' : 'refutation did not stand, but one verifier did not cover it' })
        if (!both) followUps.push({ role: 'P4', category: cat, function: e.function, part: e.part, reason: 'not covered by both verifiers' })
        continue
      }
      ledger.push({ function: e.function, part: e.part, status: 'refuted' })
      const next = nextCandidate(functions, e.function, e.part)
      if (!next) { followUps.push({ role: 'P2', category: cat, function: e.function, part: e.part, reason: 'refuted, and no next-ranked candidate under that name' }); continue }
      // The pair is paid for here from the free agents; run() starts it.
      if (!takeExtra(2)) { followUps.push({ role: 'P4', category: cat, function: e.function, part: next.part, reason: 'no free agents for the next pair' }); continue }
      queue.push({ only: { ...next, function: e.function } })
    }
    if (only) continue
    // Figures are verified once, by the first pair. Each name in
    // figures_to_check needs the datasheet verifier's verdict.
    const seen = new Set(((ds && ds.figures) || []).map(f => f.figure))
    for (const name of bundle.figures_to_check) {
      if (seen.has(name)) continue
      ledger.push({ figure: name, status: 'not verified', reason: 'the datasheet verifier gave no verdict' })
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
        const ruling = await run('adjudicator', cat, `adjudicator-${cat}`, 'Refutations', adjudicatorPrompt(cat, f.figure, '', [{ verifier: kind, ...f }]))
        const status = !ruling ? 'refuted, adjudicator returned nothing' : ruling.stands ? 'refuted' : 'confirmed; refutation did not stand'
        ledger.push({ figure: f.figure, status })
        if (ruling && ruling.stands) followUps.push({ role: 'P2', category: cat, figure: f.figure, reason: 'figure refuted' })
      }
    }
  }
  return ledger
}

// The part each function keeps: the first in rank order that both verifiers
// confirmed, or none.
function selection(functions, ledger) {
  // A part's status is its last ledger entry: a later refutation overrides
  // an earlier verification.
  const last = new Map()
  for (const l of ledger) if (l.part) last.set(`${l.function}\u0000${l.part}`, l.status)
  return functions.map(f => {
    const st = c => last.get(`${f.function}\u0000${c.part}`) || ''
    const kept = f.shortlist.find(c => st(c).startsWith('verified'))
    const refuted = f.shortlist.filter(c => st(c) === 'refuted').map(c => c.part)
    return { function: f.function, part: kept ? kept.part : null, rank: kept ? kept.rank : null, refuted }
  })
}

async function categoryChain(cat) {
  const p2 = await run('P2', cat, `P2-${cat}`, 'P2-P4', p2Prompt(cat))
  if (!p2) return { category: cat, status: 'P2 returned nothing' }
  const p3 = await run('P3', cat, `P3-${cat}`, 'P2-P4', p3Prompt(cat, p2))
  const rr = await run('rerank', cat, `rerank-${cat}`, 'P2-P4', rerankPrompt(cat, p2, p3 || { category: cat, missed: [], exclusions_not_holding: [], note: 'P3 returned nothing twice; its search is a follow-up item' }))
  if (!rr) return { category: cat, status: 're-rank returned nothing' }
  const functions = merge(cat, p2, rr)
  const figures_to_check = [
    ...(p2.found_values || []).map(v => `found ${v.question_id}`),
    ...(p2.report || []).map(f => `P2 report: ${f.figure}`),
    ...(rr.report || []).map(f => `re-rank report: ${f.figure}`),
    ...functions.flatMap(f => f.dropped_from_p3.map(d => `re-rank drop: ${d.part}`)),
  ]
  const bundle = { functions, found_values: p2.found_values || [], report_p2: p2.report || [], report_rerank: rr.report || [], figures_to_check }
  const ledger = await verifyCategory(cat, functions, bundle)
  return { category: cat, status: p3 ? 'done' : 'done without P3', ledger, selection: selection(functions, ledger) }
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

function critiqued(kind, items, critic) {
  if (!critic) {
    items.forEach((it, index) => followUps.push({ role: `${kind}-critic`, index, item: it, reason: `${kind} item not checked` }))
    return []
  }
  const held = []
  items.forEach((it, index) => {
    const v = (critic.verdicts || []).find(x => x.index === index)
    if (!v) followUps.push({ role: `${kind}-critic`, index, item: it, reason: `${kind} item not ruled on` })
    else if (v.holds) held.push({ source: kind, ...it })
  })
  for (const it of critic.added || []) held.push({ source: `${kind} critic`, ...it })
  return held
}

async function p5Chain() {
  const p5 = await run('P5', '', 'P5', 'P5-P6', p5Prompt())
  if (!p5) return { conflicts: [] }
  const critic = await run('P5-critic', '', 'P5-critic', 'P5-P6', p5CriticPrompt(p5))
  return { conflicts: critiqued('P5', p5.conflicts || [], critic), combinations: p5.combinations || [] }
}
async function p6Chain() {
  const p6 = await run('P6', '', 'P6', 'P5-P6', p6Prompt())
  if (!p6) return { gaps: [] }
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
  a = a || { conflicts: [] }
  b = b || { gaps: [] }
  for (const c of a.conflicts) followUps.push({ role: 'P5', item: c, reason: 'conflict' })
  for (const g of b.gaps) followUps.push({ role: 'P6', item: g, reason: 'gap' })
  return { conflicts: a.conflicts, combinations: a.combinations || [], gaps: b.gaps }
}

// ---------------------------------------------------------------- the task

let summary = {}
if (TASK === 'T6') {
  phase('P7')
  const p7 = await run('P7', '', 'P7', 'P7', p7Prompt())
  const critic = p7 ? await run('P7-critic', '', 'P7-critic', 'P7', p7CriticPrompt(p7)) : null
  summary = { p7: p7 ? p7.checks : null, critic: critic ? critic.checks : null }
  // The pages are final only with both returns; otherwise T6 is recorded as
  // stopped and its pages are not committed.
  if (!p7 || !critic) summary = { ...summary, stopped: true, reasons: [!p7 ? 'P7 returned nothing' : 'the P7 critic returned nothing'] }
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
      summary = { categories: cats, held: p0.held, results, selection: Object.fromEntries(results.map(r => [r.category, r.selection || []])) }
    }
  }
}

log(`${TASK}: ${started} agents started (${PLANNED} planned, ${extra} of ${FREE} free used); ${followUps.length} items for follow-up`)
return {
  task: TASK, run: A.run || TASK, run_id: A.run_id || '', commit: A.commit, date: A.date, run_info: A.run_info || {}, followup: TASK === 'FU' ? FU : null,
  planned: PLANNED, started, extra_used: extra, free: FREE, skipped, summary, followUps, missing, returns,
}
