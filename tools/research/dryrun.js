#!/usr/bin/env node
// Runs tools/research/round1.js for every task with mock agents that return
// objects shaped by tools/research/schemas.json, and checks the rules the
// script enforces: the counts and the cap, both verifiers on every part, an
// adjudicator for every refutation and refuted figure, the next-ranked part
// after a refutation that stands, restarts from the free agents, questions
// that reach the owner only when checked, and P0's checks.
//
//   node tools/research/dryrun.js      exit 1 on the first failed check

'use strict'
const fs = require('fs')
const path = require('path')

const dir = __dirname
const src = fs.readFileSync(path.join(dir, 'round1.js'), 'utf8').replace('export const meta', 'const meta')
const cats = JSON.parse(fs.readFileSync(path.join(dir, 'categories.json'), 'utf8'))
const raw = JSON.parse(fs.readFileSync(path.join(dir, 'schemas.json'), 'utf8'))

function resolve(node) {
  if (Array.isArray(node)) return node.map(resolve)
  if (node && typeof node === 'object') {
    if (node.$ref) return resolve(raw.defs[node.$ref])
    const out = {}
    for (const [k, v] of Object.entries(node)) out[k] = resolve(v)
    return out
  }
  return node
}
const schemas = {}
for (const [role, s] of Object.entries(raw.roles)) schemas[role] = resolve(s)
const roleOf = schema => Object.keys(schemas).find(r => schemas[r] === schema)

function fake(schema, i = 0) {
  if (schema.enum) return schema.enum[0]
  switch (schema.type) {
    case 'object': {
      const o = {}
      for (const k of schema.required || []) o[k] = k === 'read_at' ? '2026-09-28T10:00:00Z' : fake(schema.properties[k], i)
      return o
    }
    case 'array': return [0, 1].map(j => fake(schema.items, j))
    case 'integer': return i
    case 'boolean': return false
    default: return `x${i}`
  }
}

let failures = 0
function check(cond, msg) {
  if (!cond) { failures++; console.log(`FAIL ${msg}`) }
}

const T6OUT = ['hardware/docs/IOBoard.md', 'hardware/docs/Parts.md', 'hardware/docs/Power.md', 'hardware/docs/Research.md', 'hardware/STATUS.md', 'hardware/README.md', 'tools/jlc_stock.py']
const JL = { sha256: 'sha', rows: 7, lcsc: [], manifest: 'm', manifest_created: '2026-09-14T09:56:01Z' }
const CLIENTS = JSON.parse(fs.readFileSync(path.join(__dirname, 'hosts.json'), 'utf8')).clients
// The endpoint a correct P0 row records for a host.
const endpoint = h => h.probe == null ? `https://${h.host}/products/x` : /-api$/.test(h.client || '') ? CLIENTS[h.client].replace(/\b(LCSC|MPN)\b/, h.probe) : h.probe

// opts: refute ['label:part'], refuteFigure ['label'], stands, omit ['label'],
// nulls {label: count}, throws [label], critic 'none' | 'all', p0 {...}, hosts,
// edit {figure: {...}} for every P4 check, editPart {'label:part': {figure:
// {...}}} for one verifier's checks of one part
async function runTask(task, opts = {}) {
  const calls = []
  const prompts = []
  const nulls = new Map(Object.entries(opts.nulls || {}))
  async function agent(prompt, o) {
    const base = o.label.replace(/:restart$/, '')
    calls.push(o.label)
    prompts.push({ label: o.label, prompt })
    if ((opts.throws || []).includes(base)) throw new Error('mock throw')
    const left = nulls.get(base)
    if (left) { nulls.set(base, left - 1); return null }
    const role = roleOf(o.schema)
    const data = fake(o.schema)
    // A category-scoped agent names the category its label carries.
    if ('category' in data && /R\d+/.test(o.label) && !opts.wrongCategory) data.category = o.label.match(/R\d+/)[0]
    const base0 = fake(schemas.P2.properties.functions.items.properties.shortlist.items)
    base0.requirements = base0.requirements.map(r => ({ ...r, pass: true }))
    base0.placements = opts.placements === undefined ? 1 : opts.placements
    base0.lcsc = opts.lcsc === undefined ? 'C1000' : opts.lcsc
    const cand = rank => ({ ...base0, rank, part: `part${rank}`,
      second_source_route: rank === 2 && opts.replacementAlt ? 'alternate' : (rank === 1 && opts.emptyAlt ? 'alternate' : 'second-vendor'), second_source_part: rank === 2 && opts.replacementAlt ? 'altB' : '' })
    if (role === 'P0') {
      Object.assign(data, { stop: false, held: [], checkout_head: 'deadbeef', snapshot: '2026-09-14T09:56:01+00:00',
        jlcparts: { path: 'db', sha256: JL.sha256, sha256_ok: true, rows: JL.rows, missing_lcsc: [] },
        monostable: { commit: 'c', path: 'p', fetched: true },
        hosts: (opts.hosts || []).map(h => ({ host: h.host, url: opts.rowUrl || endpoint(h), client: opts.rowClient || h.client, http_status: 200, bytes: 1, status_marker: opts.markerText || (opts.markerless ? '' : 'Status - Active'), reachable: !(opts.down || []).includes(h.host), note: '' })) }, opts.p0 || {})
    }
    if (role === 'P2') {
      data.functions = [{ ...data.functions[0], function: 'f1', shortlist: [1, 2, 3].map(cand) }]
      if (opts.replacementAlt) data.functions[0].shortlist.push({ ...cand(9), part: 'altB', rank: 9 })
      if (opts.fnReq) data.functions[0].requirements = [...data.functions[0].requirements, { name: opts.fnReq, value: 'v', source: 's' }]
      if (opts.noReqs) { data.functions[0].requirements = []; data.functions[0].shortlist.forEach(c => { c.requirements = [] }) }
      if (opts.failedReq) data.functions[0].shortlist[0].requirements = [{ name: 'x0', required: 'x0', datasheet: '40 V', pass: false, source: 's' }]
      if (opts.selfAlt) Object.assign(data.functions[0].shortlist[0], { second_source_route: 'alternate', second_source_part: 'part1' })
      if (opts.backAlt) Object.assign(data.functions[0].shortlist[1], { second_source_route: 'alternate', second_source_part: 'part1' })
      if (opts.qAltBack) Object.assign(data.functions[0].shortlist[2], { second_source_route: 'alternate', second_source_part: 'part1' })
      if (opts.p2DupPart) data.functions[0].shortlist.push({ ...cand(1), requirements: [{ name: 'x0', required: 'x0', datasheet: '40 V', pass: false, source: 's' }] })
      if (opts.fnNoReq) data.functions[0].requirements = []
      if (opts.dupReq) data.functions[0].requirements.push({ ...data.functions[0].requirements[0], value: '<= 60 V' })
      if (opts.firstRecord) Object.assign(data.functions[0].shortlist[0], opts.firstRecord)
      if (opts.reportPerPart) data.report = [{ figure: 'frames held: MCP2518FD', value: '2', source: 's' }]
      if (opts.strictReq) data.functions[0].shortlist[0].requirements = [{ name: 'x0', required: '>= 99 V', datasheet: '70 V', pass: false, source: 's' }, { name: 'x1', required: 'x1', datasheet: 'd', pass: true, source: 's' }]
      if (opts.reportParts) data.report = opts.reportParts.map(figure => ({ figure, value: 'not applicable: transceiver', source: 's' }))
      if (opts.twoDeciders) data.functions.push({ ...data.functions[0], function: 'fX' })
      if (opts.p2DropY) data.functions[0].dropped = [{ part: 'partY', maker: 'm', reason: 'r' }]
      if (opts.foundNotRead) data.found_values = [{ question_id: 'V9', value: 'not read: HTTP 403', source: 's', read_at: 't' }]
      if (opts.foundValue) data.found_values = [{ question_id: 'V9', value: opts.foundValue, source: 's', read_at: '2026-09-28T10:00:00Z' }]
      if (opts.reports) data.report = opts.reports.map(([figure, value]) => ({ figure, value, source: 's', read_at: '2026-09-28T10:00:00Z' }))
      if (opts.onBoardAltOfOff) { Object.assign(data.functions[0].shortlist[0], { lcsc: 'none', second_source_route: 'alternate', second_source_part: 'altOn' }); data.functions[0].shortlist.push({ ...cand(8), part: 'altOn', lcsc: 'C2' }) }
      if (opts.weakReq) { data.functions[0].requirements = [{ name: 'x0', value: '>= 67.2 V', source: 's' }]; data.functions[0].shortlist.forEach(c => { c.requirements = [{ name: 'x0', required: '>= 40 V', datasheet: '45 V', pass: true, source: 's' }] }) }
      if (opts.offBoardAlt) { Object.assign(data.functions[0].shortlist[0], { lcsc: 'C1', second_source_route: 'alternate', second_source_part: 'altOff' }); data.functions[0].shortlist.push({ ...cand(8), part: 'altOff', lcsc: 'none' }) }
      if (opts.held !== undefined) data.functions[0].shortlist.forEach(c => { c.held = opts.held; c.lcsc = 'C9' })
      if (opts.altName) Object.assign(data.functions[0].shortlist[0], { second_source_route: 'alternate', second_source_part: opts.altName })
      if (opts.altFewer) {
        Object.assign(data.functions[0].shortlist[0], { placements: 20, second_source_route: 'alternate', second_source_part: 'altF' })
        data.functions[0].shortlist.push({ ...cand(9), part: 'altF', rank: 9, placements: 5 })
      }
      // alts {PART: ALTERNATE}: rule-5 alternates, each with a record.
      for (const [part, alt] of Object.entries(opts.alts || {})) {
        Object.assign(data.functions[0].shortlist.find(c => c.part === part), { second_source_route: 'alternate', second_source_part: alt })
        if (!data.functions[0].shortlist.some(c => c.part === alt)) data.functions[0].shortlist.push({ ...cand(9), part: alt, rank: 9 })
      }
      if (opts.twoQShare) {
        Object.assign(data.functions[0].shortlist[1], { second_source_route: 'alternate', second_source_part: 'altS2' })
        Object.assign(data.functions[0].shortlist[2], { second_source_route: 'alternate', second_source_part: 'altS2' })
        data.functions[0].shortlist.push({ ...cand(9), part: 'altS2', rank: 9 })
      }
      if (opts.sharedAlt) {
        Object.assign(data.functions[0].shortlist[0], { second_source_route: 'alternate', second_source_part: 'altS' })
        Object.assign(data.functions[0].shortlist[2], { second_source_route: 'alternate', second_source_part: 'altS' })
        data.functions[0].shortlist.push({ ...cand(9), part: 'altS', rank: 9 })
      }
      if (opts.qAlt) {
        Object.assign(data.functions[0].shortlist[2], { second_source_route: 'alternate', second_source_part: 'altQ' })
        data.functions[0].shortlist.push({ ...cand(9), part: 'altQ', rank: 9 })
      }
      if (opts.altFails) {
        Object.assign(data.functions[0].shortlist[0], { second_source_route: 'alternate', second_source_part: 'altA' })
        data.functions[0].shortlist.push({ ...cand(8), part: 'altA', requirements: [{ name: 'x0', required: 'x0', datasheet: '40 V', pass: false, source: 's' }] })
      }
      if (opts.offBoard) Object.assign(data.functions[0].shortlist[0], { lcsc: 'none', second_source_route: 'second-vendor' })
      if (opts.noSecond) Object.assign(data.functions[0].shortlist[0], { second_source_route: 'none' })
    }
    if (role === 'rerank') {
      const rcat = (o.label.match(/R\d+/) || [''])[0]
      data.functions = [{ function: 'f1', decision: opts.qDecision || ({ R10: 'Q4', R12: 'Q8' }[rcat] || 'none'), kept_option: opts.keptOption || '', ranking: (opts.ranking || (opts.ranked || [1, 2, 3]).map(rank => ({ rank, part: `part${rank}`, reason: 'r' }))),
        new_candidates: opts.p3dropped ? [{ ...cand(7), part: 'partN' }] : [], dropped_from_p3: opts.p3dropped ? [{ part: 'partN', maker: 'm', reason: 'r' }] : [],
        dropped_from_shortlist: opts.rankDropped ? [{ part: 'part1', maker: 'm', reason: 'r' }] : (opts.dropShort || []).map(part => ({ part, maker: 'm', reason: 'r' })), verify: opts.qDup ? [{ part: 'part3', kind: 'q-alternative', option: 'reference' }, { part: 'part3', kind: 'q-alternative', option: 'external ADC' }] : opts.twoQShare ? [{ part: 'part2', kind: 'q-alternative' }, { part: 'part3', kind: 'q-alternative' }] : opts.qAlt || opts.sharedAlt ? [{ part: 'part3', kind: 'q-alternative', option: opts.qOption || '' }] : (opts.verify || []).map(part => ({ part, kind: 'alternate' })) }]
      if (opts.extraFn) data.functions.push({ ...data.functions[0], function: 'fX', ...(opts.rrHandleYin || opts.rrHandleXin ? { dropped_from_shortlist: [{ part: opts.rrHandleXin ? 'partX' : 'partY', maker: 'm', reason: 'r' }] } : {}) })
      if (opts.p3dropShort) data.functions[0].dropped_from_shortlist = [{ part: 'partX', maker: 'm', reason: 'fails vmax' }]
      if (opts.rrDup) data.functions.push({ ...data.functions[0], ranking: [], dropped_from_shortlist: [{ part: 'part1', maker: 'm', reason: 'fails vmax at 85 C' }] })
      if (opts.noRerankFn) data.functions = []
      if (opts.emptyRanking) data.functions[0].ranking = []
      if (opts.qAltBack) data.functions[0].verify = [{ part: 'part3', kind: 'q-alternative' }]
      if (opts.qSelf) data.functions[0].verify = [{ part: 'part1', kind: 'q-alternative' }]
      if (opts.verifyFirst) data.functions[0].verify = opts.verifyFirst.map(part => ({ part, kind: 'first', option: '' }))
      if (opts.verifyList) data.functions[0].verify = opts.verifyList.map(v => ({ option: '', ...v }))
    }
    if (role === 'P5' && opts.p5Empty) Object.assign(data, { combinations: [], budgets: [] })
    if (role === 'P5' && opts.unsourcedCombos) data.combinations = data.combinations.map(x => ({ ...x, source: '' }))
    if (role === 'P5' && opts.p5Items) data.budgets = opts.p5Items.map(item => ({ item, value: (opts.budgetValues || {})[item] || 'v', source: opts.blankSource ? '   ' : 's', read_at: opts.budgetUndated ? '' : opts.budgetTime || '2026-09-28T10:00:00Z' }))
    if (role === 'P5') data.combinations = data.combinations.map(x => ({ ...x, fits: !opts.noFit }))
    if (role === 'P5') data.budgets = data.budgets.map(x => ({ ...x, within: !(opts.overBudget || []).includes(x.item) }))
    if (role === 'P5' && opts.p5Assumed) data.assumptions = opts.p5Assumed.map(([item, value]) => ({ item, value, why: 'w', source: 's', read_at: '2026-09-28T10:00:00Z' }))
    if (role === 'P5' && opts.unsourcedAssumption) data.assumptions = data.assumptions.map(x => ({ ...x, source: '' }))
    if (role === 'P5' && opts.blankCombos) data.combinations = data.combinations.map(x => ({ ...x, outputs: '', bind_order: '', resources: '' }))
    if (role === 'P5' && opts.assumptionValue) data.assumptions = data.assumptions.map(x => ({ ...x, value: opts.assumptionValue }))
    if (role === 'P5-critic') {
      // The critic rules on every item P5 returned.
      const p5ret = JSON.parse(prompt.slice(prompt.lastIndexOf('\n') + 1))
      for (const [k, list] of [['combination_verdicts', 'combinations'], ['budget_verdicts', 'budgets'], ['assumption_verdicts', 'assumptions']]) {
        data[k] = p5ret[list].map((_, index) => ({ index, reason: opts.blankVerdictReason ? ' ' : 'r', holds: !(opts.rejectBudget && k === 'budget_verdicts' && index === 0) && !(opts.rejectAssumption && k === 'assumption_verdicts' && index === 0) }))
      }
      if (opts.blankVerdictReason) data.verdicts = data.verdicts.map(v => ({ ...v, reason: '' }))
      data.budgets_missing = opts.criticMissing || []
    }
    if (role === 'P6-critic' && opts.blankVerdictReason) data.verdicts = data.verdicts.map(v => ({ ...v, reason: '' }))
    if (role === 'P1' && opts.qCategory) data.questions = data.questions.map(q => ({ ...q, category: 'R99', source: 'mock' }))
    if (role === 'P1' && opts.decisionNone) data.questions = data.questions.map(q => ({ ...q, blocks: 'decision-only', decision: 'none' }))
    if (role === 'P1' && opts.contradict) data.values = data.values.map(v => ({ ...v, question: -1 }))
    if (role === 'P1' && opts.noValues) data.values = []
    if (role === 'P1' && opts.noFunctions) data.functions = []
    if (role === 'P1' && opts.decisionQ9) data.questions = data.questions.map(q => ({ ...q, blocks: 'decision-only', decision: 'Q9' }))
    if (role === 'P1' && opts.fuQuestion) data.questions[0].question = opts.fuQuestion
    if (role === 'P1' && opts.asksQ) data.questions[0].asks = opts.asksQ
    if (role === 'P1' && opts.sameQuantity) data.values = ['2.7 V', '3.6 V'].map(value => ({ where: 'IOBoard.md:40', quantity: 'supply voltage', value, marking: 'sourced', source: 's', refutation_tried: 'r', question: -1 }))
    if (role === 'P1' && opts.sameLine) {
      data.values = [['A', -1], ['B', 0]].map(([quantity, question]) => ({ where: 'IOBoard.md:1', quantity, value: '1 V', marking: 'assumption', source: 's', refutation_tried: 'r', question }))
      data.questions = [{ function: 'rail', question: 'State A', why: 'w', blocks: 'p2', decision: 'none', for_where: 'IOBoard.md:1', for_quantity: 'A' }]
    }
    if (role === 'P1' && opts.assumption) data.values = [{ where: 'IOBoard.md:1', quantity: 'ripple', value: '10 mV', marking: 'assumption', source: 's', refutation_tried: 'r', question: -1 }]
    if (role === 'P1' && (opts.twoAssumptions || opts.sharedQ)) {
      data.values = [1, 2].map(n => ({ where: `IOBoard.md:${n}`, quantity: 'voltage', value: `${n} V`, marking: 'assumption', source: 's', refutation_tried: 'r', question: n === 1 || opts.sharedQ ? 0 : -1 }))
      data.questions = [{ function: 'rail', question: 'State the voltage of rail 1', why: 'w', blocks: 'p2', decision: 'none', for_where: opts.wrongWhere ? 'IOBoard.md:9' : 'IOBoard.md:1', for_quantity: 'voltage' }]
    }
    if (role === 'P3') Object.assign(data, { missed: [], exclusions_not_holding: [], missed_functions: opts.missedFn ? [{ function: 'f2', why: 'the row names it' }] : [] })
    if (role === 'P3' && opts.p3missed) data.missed = [{ function: opts.p3missedFn || 'f1', part: 'partX', maker: 'm', why: 'w' }]
    if (role === 'P3' && opts.p3overturned) data.exclusions_not_holding = [{ part: 'partY', reason_given: 'r', why_it_fails: 'w' }]
    if (role === 'P7-critic' || role === 'P7') {
      for (const k of Object.keys(data.checks)) data.checks[k] = { passed: !(opts.failCheck === k && role === 'P7-critic'), output: 'o' }
      const outs = [...T6OUT, 'hardware/docs/GroupA.md', 'hardware/docs/GroupB.md', 'hardware/docs/GroupC.md'].filter(f => !(role === 'P7' && f === opts.unwritten))
      if (opts.pageOutside) outs.push('tools/research/README.md')
      if (role === 'P7') { data.files = outs; data.group_pages = opts.pageOutside ? { A: 'hardware/docs/GroupA.md', B: 'tools/research/README.md', C: 'hardware/docs/GroupC.md' } : opts.samePages ? { A: 'hardware/docs/Power.md', B: 'hardware/docs/Power.md', C: 'hardware/docs/Power.md' } : (opts.pagePower ? { A: 'hardware/docs/GroupA.md', B: 'hardware/docs/Power.md', C: 'hardware/docs/GroupC.md' } : { A: 'hardware/docs/GroupA.md', B: 'hardware/docs/GroupB.md', C: 'hardware/docs/GroupC.md' }) }
      else {
        data.reviewed = outs
        const figFiles = opts.oneFigure ? ['hardware/docs/Parts.md'] : outs.filter(f => f.startsWith('hardware/docs/'))
        data.figure_checks = opts.noFigures ? [] : figFiles.map(file => ({ file, line: 1, figure: opts.blankFigure ? '' : 'stock', return_file: opts.blankFigure ? '' : opts.returnFile || 'hardware/research/round1/T2/012-P4-stock-R1.json', agrees: !opts.criticDisagrees }))
        data.sentence_issues = opts.sentenceIssue ? [{ file: 'f', line: 1, issue: 'i' }] : []
        data.marked = opts.marked || []
        data.part_rows = opts.partRows || []
        data.jlc_stock_review = opts.jlcReview || []
      }
    }
    if (role === 'P1-recheck' && opts.recheckRejects) {
      const added = JSON.parse(/confirm or reject it[^\n]*\n([\s\S]*)$/.exec(prompt).pop().split('\n').pop())
      data.verdicts = added.map(q => ({ category: q.category, index: q.index, verdict: 'rejected', evidence: opts.recheckEvidence || 'e' }))
      return data
    }
    if (role === 'P1-recheck') {
      const added = JSON.parse(/confirm or reject it[^\n]*\n([\s\S]*)$/.exec(prompt).pop().split('\n').pop())
      data.verdicts = added.map(q => ({ category: q.category, index: q.index, verdict: opts.recheckRejectSynthetic && q.synthetic ? 'rejected' : 'confirmed', evidence: 'e' }))
    }
    if (role === 'P1-critic') {
      if (opts.critic === 'none') data.question_verdicts = []
      else data.question_verdicts = (opts.twoAssumptions ? [0] : opts.dupVerdicts ? [0, 0, 1] : [0, 1]).map(index => ({ index, verdict: 'confirmed', reason: 'r' }))
      data.added = []
      data.marking_verdicts = opts.markings === 'none' ? [] : (opts.assumption
        ? [{ index: 0, where: 'IOBoard.md:1', quantity: 'ripple', verdict: 'holds', correct_marking: 'unchanged', reason: 'r' }]
        : opts.twoAssumptions ? [1, 2].map(n => ({ index: n - 1, where: `IOBoard.md:${n}`, quantity: 'voltage', verdict: 'holds', correct_marking: 'unchanged', reason: 'r' }))
        : opts.contradict ? [{ index: 0, where: 'x0', quantity: 'x0', verdict: 'wrong', correct_marking: 'unchanged', reason: 'r' }, { index: 1, where: 'x1', quantity: 'x1', verdict: 'holds', correct_marking: 'unchanged', reason: 'r' }]
        : [0, 1].map(i => ({ index: i, where: `x${i}`, quantity: `x${i}`, verdict: 'holds', correct_marking: 'unchanged', reason: 'r' })))
      // Verdicts on P1's own values, by index, where and quantity.
      const p1ret = JSON.parse(prompt.slice(prompt.lastIndexOf('\n') + 1))
      if (opts.sameQuantity || opts.sameLine) data.marking_verdicts = p1ret.values.map((v, index) => ({ index, where: v.where, quantity: v.quantity, verdict: 'holds', correct_marking: 'unchanged', reason: 'r' }))
      if (opts.restate) data.marking_verdicts = data.marking_verdicts.map(m => ({ ...m, correct_marking: 'sourced' }))
      if (opts.blankReason) {
        data.marking_verdicts = data.marking_verdicts.map(m => ({ ...m, verdict: 'wrong', correct_marking: 'owner', reason: '' }))
        data.question_verdicts = data.question_verdicts.map(v => ({ ...v, verdict: 'rejected', reason: ' ' }))
      }
      if (opts.criticAdds) data.added = [opts.criticAdds]
      if (opts.rejectQ0) data.question_verdicts[0] = { ...data.question_verdicts[0], verdict: 'rejected', reason: 'the row states it' }
      if (opts.noFunctions) data.functions_missing = []
    }
    if (role === 'P4') {
      const only = /list only those: (\{.*\})/.exec(prompt)
      const part = only ? JSON.parse(only[1]).part : 'part1'
      const kind = o.label.includes('stock') ? 'stock' : 'datasheet'
      const refute = (opts.refute || []).includes(`${base}:${part}`)
      data.verifier = kind
      // The checks a correct verifier returns, by the names round1.js requires.
      const bundle2 = JSON.parse(prompt.slice(prompt.lastIndexOf('\n') + 1))
      const f1 = bundle2.functions[0]
      const cand = pt => [...f1.shortlist, ...(f1.alternateRecords || [])].find(c => c.part === pt) || { requirements: [] }
      const checksFor = (pt, pk) => (opts.emptyChecks || []).includes(base) ? []
        : (kind === 'stock' ? [...(opts.heldChecks ? ['held quantity', ...(opts.heldAndLive ? ['stock'] : [])] : ['stock', 'presale']), ...(opts.addStock || []), 'lifecycle status', 'end-of-life notices', ...(opts.noPlacementsCheck ? [] : ['placements']), ...(opts.noBoardCheck ? [] : ['board placement']), ...(opts.noLifecycleReadings ? [] : ['longevity commitment', 'market introduction', 'distributor status', 'lead time']), ...(!opts.noIdentity && /^C\d+$/.test(cand(pt).lcsc || '') ? ['LCSC identity', ...(opts.noLibType ? [] : ['library type'])] : []), ...(cand(pt).second_source_route === 'second-vendor' && !opts.noSecondVendor ? ['second-vendor stock'] : [])]
          : [...cand(pt).requirements.map(r => r.name).filter(n => n !== opts.skipReq), ...(opts.noMakerCheck ? [] : ['manufacturer allowlist']), ...(pk === 'alternate' && !opts.noCompat ? ['pin-for-pin match', 'functional match'] : [])])
          .map((figure, k) => ({ figure, stated: 's', read: opts.readNone && kind === 'datasheet' ? 'not read: API timed out' : 'r', source: opts.noSource && kind === 'stock' ? '' : 'src', read_at: opts.undated && kind === 'stock' ? '' : '2026-09-28T10:00:00Z', agrees: !((opts.disagree || []).includes(base) && k === 0), passes: !((opts.failPass || []).includes(base) && k === 0) }))
          .map(c => opts.heldAndLive && c.figure === 'stock' ? { ...c, passes: false } : c)
          .map(c => ({ ...c, ...((opts.edit || {})[c.figure] || {}), ...(kind === 'stock' ? opts.stockPatch || {} : {}) }))
          .map(c => ({ ...c, ...(((opts.editPart || {})[`${base}:${pt}`] || {})[c.figure] || {}) }))
      data.parts = (opts.omit || []).includes(base) || (opts.omitPart || []).includes(`${base}:${part}`) ? [] : [{ function: 'f1', part, kind: 'first', verdict: refute ? 'refuted' : 'confirmed', checks: checksFor(part, opts.bothRoles && !only ? 'alternate' : undefined), refutation: refute ? 'mock' : '' }]
      if (opts.note && opts.note[0] === base && data.parts.length) data.parts[0].refutation = opts.note[1]
      if ((opts.dupRow || []).includes(base)) data.parts.push({ ...data.parts[0], verdict: 'refuted', refutation: 'second row' })
      const oc = only ? JSON.parse(only[1]) : null
      if (oc && oc.second_source_route === 'alternate' && !opts.dropReplacementAlt) data.parts.push({ function: 'f1', part: oc.second_source_part, kind: 'alternate', verdict: 'confirmed', checks: checksFor(oc.second_source_part, 'alternate'), refutation: '' })
      const names = only ? [] : JSON.parse(/"figures_to_check":(\[[^\]]*\])/.exec(prompt)[1])
      data.figures = kind === 'datasheet' ? names.filter(n => !(opts.omitFigure && base === opts.omitFigure[0] && n === opts.omitFigure[1]))
        .map((figure, k) => ({ figure, verdict: (opts.refuteFigure || []).includes(base) && k === 0 ? 'refuted' : 'confirmed', evidence: opts.unreadFigure && k === 0 ? 'not read' : 'e', source: opts.unsourcedFigure && k === 0 ? '' : 'datasheet table 5', read_at: opts.figureReadAt && k === 0 ? opts.figureReadAt : '2026-09-28T10:00:00Z' }))
        : opts.stockFigures ? names.map(figure => ({ figure, verdict: 'refuted', evidence: 'stock page lists 12 frames', source: 'jlcpcb C1000', read_at: '2026-09-28T10:00:00Z' })) : []
      // A replacement pair's datasheet verifier that refutes a figure.
      if (only && kind === 'datasheet' && opts.replacementFigure) data.figures = [{ figure: opts.replacementFigure, verdict: 'refuted', evidence: 'table 4-1: 2 FIFOs of 8 frames', source: 'datasheet table 4-1', read_at: '2026-09-28T10:00:00Z' }]
      const extraParts = opts.p4parts || opts.verify
      if (extraParts && !(opts.omit || []).includes(base) && !opts.dropVerify && !only) data.parts.push(...extraParts.map(pt => ({ function: 'f1', part: pt, kind: opts.reportFirst ? 'first' : 'alternate', verdict: (opts.refute || []).includes(`${base}:${pt}`) ? 'refuted' : 'confirmed', checks: checksFor(pt, opts.reportFirst ? 'first' : 'alternate'), refutation: '' })))
    }
    if (role === 'adjudicator') {
      data.stands = opts.stands !== false
      const m = /refuted (.+?) for the function "(.+?)"/.exec(prompt)
      const g = /refuted the figure "(.+?)"/.exec(prompt)
      if (m) { data.part = m[1]; data.function = m[2] } else if (g) { data.part = ''; data.function = g[1] }
      if (opts.wrongRuling) data.part = 'another'
      if (opts.unreadRuling) data.evidence = 'not read'
      if (opts.unsourcedRuling) data.source = ''
      if (opts.rulingReadAt) data.read_at = opts.rulingReadAt
    }
    return data
  }
  const parallel = async thunks => Promise.all(thunks.map(t => t().catch(() => null)))
  // As the Workflow tool: a stage that throws drops the item to null.
  async function pipeline(items, ...stages) {
    return Promise.all(items.map(async (item, idx) => {
      try {
        let v = item
        for (let s = 0; s < stages.length; s++) v = await stages[s](s ? v : item, item, idx)
        return v
      } catch (e) { return null }
    }))
  }
  const args = { task, cap: opts.cap || cats.cap, categories: cats.categories, tasks: cats.tasks, schemas,
    commit: 'deadbeef', date: '2026-09-27', paths: {}, hosts: opts.hosts || [], clients: CLIENTS, inventory: opts.inventory || {}, jlcparts: JL, p5_budgets: opts.p5Budgets || [], p5_conditional: opts.p5Conditional || [], q_options: opts.qOptions || {},
    followup: opts.followup, first_v: 5, decision_categories: { Q4: ['R10'], Q8: ['R2', 'R12'], Q9: ['R3'] }, t6_outputs: T6OUT, for_research: opts.forResearch || [], raised: opts.raised || [], p1_asks: opts.p1Asks || {}, fixed_inputs: opts.fixedInputs || {}, required_reports: opts.requiredReports || {}, per_part_reports: opts.perPartReports || {},
    p5_assumptions: opts.p5Assumptions || [], decisions: opts.decisions || {}, accept_open: opts.acceptOpen || null, left_open: opts.leftOpen || [], p5_assumed: opts.p5AssumedT6 || [],
    last_p56: opts.lastP56 || '', p56_runs: opts.p56Runs || [], selection: opts.selection || {}, jlc_stock_row: opts.jlcRow || [] }
  const fn = new Function('args', 'agent', 'parallel', 'pipeline', 'phase', 'log',
    `return (async () => {${src}})()`)
  const result = await fn(args, agent, parallel, pipeline, () => {}, () => {})
  return { result, calls, prompts }
}

const r1 = r => r.result.summary.results.find(x => x.category === 'R1')

async function main() {
  for (const t of ['T1', 'T2', 'T3', 'T4', 'T5', 'T6']) {
    const { result, calls } = await runTask(t)
    check(result.planned === cats.planned[t], `${t}: planned ${result.planned}, the plan says ${cats.planned[t]}`)
    check(result.started === calls.length, `${t}: ${calls.length} calls against ${result.started} counted`)
    check(result.started <= cats.cap, `${t}: started ${result.started} over the cap`)
    check(result.returns.length === result.started, `${t}: ${result.returns.length} returns against ${result.started} started`)
  }

  // Both verifiers confirm: the first-ranked part is kept.
  let r = await runTask('T2')
  check(r1(r).selection[0].part === 'part1', 'T2: part1 selected')

  // A refutation that stands: one adjudicator, then a new pair on part2.
  r = await runTask('T2', { refute: ['P4-stock-R1:part1'] })
  check(r.calls.filter(c => c.startsWith('adjudicator-R1')).length === 1, 'refutation: one adjudicator')
  check(r.result.started === 24 && r.result.extra_used === 3, `refutation: started ${r.result.started}, extra ${r.result.extra_used}; expected 24 and 3`)
  check(r1(r).selection[0].part === 'part2', `refutation: selection ${r1(r).selection[0].part}, expected part2`)

  // A refutation that does not stand: the adjudicator only.
  r = await runTask('T2', { refute: ['P4-stock-R1:part1'], stands: false })
  check(r.result.started === 22 && r1(r).selection[0].part === 'part1', 'refutation not standing: 22 started, part1 kept')

  // One verifier leaves the part out: not verified, listed.
  r = await runTask('T2', { omit: ['P4-datasheet-R1'] })
  check(r1(r).ledger.some(l => l.part === 'part1' && l.status === 'not verified'), 'omitted part: not verified')
  check(r.result.followUps.some(f => f.role === 'P4' && f.category === 'R1'), 'omitted part: listed for follow-up')
  check(r1(r).selection[0].part === null, 'omitted part: nothing selected')

  // A refuted figure is adjudicated; a figure without a verdict is listed.
  r = await runTask('T2', { refuteFigure: ['P4-datasheet-R2'] })
  check(r.calls.some(c => c.startsWith('adjudicator-R2')), 'refuted figure: adjudicated')
  check(r.result.followUps.some(f => f.reason === 'figure refuted'), 'refuted figure that stands: listed')
  r = await runTask('T2')
  check(!r.result.followUps.some(f => f.reason === 'figure not verified'), 'figures: all verified when every one has a verdict')
  check(r.result.summary.figures_open.R1.length === 0, 'figures: none open when every one is confirmed')
  r = await runTask('T2', { omitFigure: ['P4-datasheet-R1', 'P2 report: x1'] })
  check(r.result.followUps.some(f => f.figure === 'P2 report: x1' && f.reason === 'figure not verified'), 'figure without a verdict: listed')
  check(r.result.summary.figures_open.R1.includes('P2 report: x1'), 'figure without a verdict: open on the category')
  r = await runTask('T2', { refuteFigure: ['P4-datasheet-R2'] })
  check(r.result.summary.figures_open.R2.length === 1, 'refuted figure that stands: open on the category')

  // A part on the verify list that both verifiers leave out is not verified.
  r = await runTask('T2', { verify: ['part2'] })
  check(r1(r).ledger.some(l => l.part === 'part2' && l.status === 'verified'), 'verify list: part2 verified when covered')
  r = await runTask('T2', { verify: ['part2'], dropVerify: true })
  check(r1(r).ledger.some(l => l.part === 'part2' && l.status === 'not verified'), 'verify list: omitted part2 not verified')

  // A re-rank that ranks only part1 keeps part2 and part3, listed.
  r = await runTask('T2', { ranked: [1] })
  check(r.result.followUps.filter(f => f.category === 'R1' && f.reason === 'candidate neither ranked nor dropped').length === 2, 're-rank omission: two candidates listed')
  r = await runTask('T2', { ranked: [1], refute: ['P4-stock-R1:part1'] })
  check(r1(r).selection[0].part === 'part2', 're-rank omission: part2 still available after a refutation')

  // Refutations beyond T4's 6 free agents go to a follow-up task.
  r = await runTask('T4', { refute: ['P4-stock-R9:part1', 'P4-stock-R9:part2', 'P4-stock-R9:part3',
    'P4-stock-R10:part1', 'P4-stock-R11:part1', 'P4-stock-R12:part1'] })
  check(r.result.started <= cats.cap && r.result.extra_used <= 6, `T4 refutations: started ${r.result.started}, extra ${r.result.extra_used}`)
  check(r.result.followUps.length > 0, 'T4 refutations: work beyond the free agents is listed')

  // An agent that returns nothing is restarted once, then listed.
  r = await runTask('T2', { nulls: { 'P3-R2': 2 } })
  check(r.calls.includes('P3-R2:restart'), 'null: P3 restarted')
  check(r.result.missing.length === 2 && r.result.followUps.some(f => f.role === 'P3'), 'null: two missing, P3 listed')

  // An agent that throws counts as missing; its category is not lost.
  r = await runTask('T2', { throws: ['P2-R3'] })
  check(r.result.summary.results.some(x => x.category === 'R3'), 'throw: R3 still reported')
  check(r.result.followUps.some(f => f.category === 'R3'), 'throw: R3 listed')

  // Questions reach the owner only when checked, numbered from first_v.
  r = await runTask('T1')
  check(r.result.summary.questions.length === 26 && r.result.summary.questions[0].id === 'V5', 'T1: 26 questions from V5')
  r = await runTask('T1', { critic: 'none' })
  check(r.result.summary.questions.length === 0, 'T1 unruled: no question reaches the owner')
  check(r.result.followUps.filter(f => f.reason === 'P1 question not ruled on').length === 26, 'T1 unruled: 26 listed')
  r = await runTask('T1', { nulls: { 'P1-critic-R4': 2 } })
  check(!r.result.summary.questions.some(q => q.category === 'R4'), 'T1 null critic: no R4 question')
  r = await runTask('T1')
  check(r.result.skipped.some(k => k.role === 'P1-recheck') && r.result.started + r.result.skipped.length === 28, 'T1: a skipped re-check is reported')
  r = await runTask('T1', { markings: 'none' })
  check(r.result.followUps.filter(f => f.reason === 'marking not ruled on').length === 26, 'T1: markings without a verdict are listed')
  r = await runTask('T1', { assumption: true })
  check(r.result.summary.questions.filter(q => q.synthetic).length === 13 && r.calls.includes('P1-recheck'), 'T1: an unasked assumption becomes a re-checked question')

  // P0: the script applies the stop and hold rules itself.
  r = await runTask('T2', { p0: { jlcparts: { path: 'db', sha256: 'other', sha256_ok: true, rows: 7, missing_lcsc: [] } } })
  check(r.result.summary.stopped === true, 'P0: a wrong database SHA-256 stops the task')
  r = await runTask('T2', { p0: { snapshot: '2020-01-01T00:00:00Z' } })
  check(r.result.summary.stopped === true, 'P0: a wrong manifest time stops the task')
  r = await runTask('T2', { p0: { checkout_head: 'cafe' } })
  check(r.result.summary.stopped === true, 'P0: a wrong checkout stops the task')
  r = await runTask('T2', { hosts: [{ host: 'jlcpcb.com', stop: 'stock-tasks', hold: [] }], down: ['jlcpcb.com'] })
  check(r.result.summary.stopped === true, 'P0: JLCPCB down stops a stock task')
  r = await runTask('T1', { hosts: [{ host: 'jlcpcb.com', stop: 'stock-tasks', hold: [] }], down: ['jlcpcb.com'] })
  check(!r.result.summary.stopped, 'P0: JLCPCB down does not stop T1')
  const winbond = [{ host: 'www.winbond.com', hold: ['R1'], hold_in_t1: ['R1'] }, { host: 'www.ti.com', hold: ['R2', 'R3'] }]
  r = await runTask('T1', { hosts: winbond, down: ['www.winbond.com', 'www.ti.com'] })
  check(!r.calls.includes('P1-R1') && r.calls.includes('P1-R2'), 'P0: T1 holds by hold_in_t1 only')
  r = await runTask('T2', { hosts: winbond, down: ['www.ti.com'] })
  check(!r.calls.includes('P2-R2') && r.calls.includes('P2-R1'), 'P0: T2 holds by hold')
  const jlc = [{ host: 'jlcpcb.com', client: 'jlcpcb-api', probe: 'C39843328', stop: 'stock-tasks', hold: [] }]
  r = await runTask('T2', { hosts: jlc })
  check(!r.result.summary.stopped, 'P0: JLCPCB read with its client at its probe')
  r = await runTask('T2', { hosts: jlc, rowClient: 'chrome' })
  check(r.result.summary.stopped === true, 'P0: JLCPCB read with another client stops a stock task')
  r = await runTask('T2', { hosts: jlc, rowUrl: 'python3 /srv/checkout/tools/research/vendors.py jlcpcb C39843328' })
  check(!r.result.summary.stopped, 'P0: the API command through an absolute path counts')
  r = await runTask('T2', { hosts: jlc, rowUrl: 'echo C39843328' })
  check(r.result.summary.stopped === true, 'P0: JLCPCB probe text without its command stops a stock task')
  r = await runTask('T2', { hosts: [{ host: 'www.onsemi.com', client: 'chrome', probe: null, hold: ['R3'] }], rowUrl: 'https://example.com/onsemi' })
  check(!r.calls.includes('P2-R3'), 'P0: a null-probe page on another host holds its categories')
  r = await runTask('T2', { hosts: jlc, rowUrl: 'https://jlcpcb.com/' })
  check(r.result.summary.stopped === true, 'P0: JLCPCB read without its probe stops a stock task')
  // The Digi-Key command as run: its environment and interpreter path aside.
  const dk = [{ host: 'api.digikey.com', client: 'digikey-api', probe: 'INA238AIDGSR', stop: 'stock-tasks', hold: [] }]
  for (const u of ['DIGIKEY_ENV_FILE=/srv/dk.env python3 /srv/base/checkout-abc/tools/research/vendors.py digikey INA238AIDGSR',
    'env DIGIKEY_ENV_FILE=/srv/dk.env python3 tools/research/vendors.py digikey INA238AIDGSR',
    '/usr/bin/python3 /srv/checkout/tools/research/vendors.py digikey INA238AIDGSR']) {
    r = await runTask('T2', { hosts: dk, rowUrl: u })
    check(!r.result.summary.stopped, `P0: the Digi-Key command "${u}" counts`)
  }
  r = await runTask('T2', { hosts: dk, rowUrl: 'DIGIKEY_ENV_FILE=/srv/dk.env python3 tools/research/vendors.py digikey INA3221AIRGVR' })
  check(r.result.summary.stopped === true, 'P0: the Digi-Key command at another part stops a stock task')

  // T6 without both P7 returns is stopped.
  r = await runTask('T6', { nulls: { 'P7-critic': 2 } })
  check(r.result.summary.stopped === true, 'T6: stopped without the critic')

  // A confirmation without its required checks is not verified.
  r = await runTask('T2', { emptyChecks: ['P4-stock-R1'] })
  check(r1(r).ledger.some(l => l.part === 'part1' && l.status === 'not verified') && r1(r).selection[0].part === null, 'evidence: empty checks do not verify')
  // A disagreeing check is a refutation, and is adjudicated.
  r = await runTask('T2', { disagree: ['P4-datasheet-R1'] })
  check(r.calls.some(c => c.startsWith('adjudicator-R1')), 'evidence: a disagreeing check is adjudicated')
  // One verifier returns nothing: the other's refutation is still adjudicated.
  r = await runTask('T2', { nulls: { 'P4-datasheet-R1': 2 }, refute: ['P4-stock-R1:part1'] })
  check(r.calls.some(c => c.startsWith('adjudicator-R1')) && r1(r).selection[0].part === 'part2', 'missing peer: refutation adjudicated, part2 selected')
  // A later refutation overrides an earlier verification of the same part.
  r = await runTask('T2', { verify: ['part2'], refute: ['P4-stock-R1:part1', 'P4-stock-R1-2:part2'] })
  check(r1(r).selection[0].part === 'part3', `final status: selection ${r1(r).selection[0].part}, expected part3`)
  check(r1(r).selection[0].refuted.includes('part2'), 'final status: part2 listed as refuted')
  // An alternate needs its compatibility checks.
  r = await runTask('T2', { verify: ['part2'], noCompat: true })
  check(r1(r).ledger.some(l => l.part === 'part2' && l.status === 'not verified'), 'alternate: no compatibility checks, not verified')
  // An overturned refutation without its required checks does not verify.
  r = await runTask('T2', { refute: ['P4-stock-R1:part1'], stands: false, emptyChecks: ['P4-stock-R1'] })
  check(r1(r).selection[0].part === null, 'overturned refutation without checks: not verified')
  // A candidate P3 found that the re-rank leaves out is listed.
  r = await runTask('T2', { p3missed: true })
  check(r.result.followUps.some(f => f.part === 'partX'), 'P3 candidate left out by the re-rank: listed')
  // Two assumptions of one quantity: only the one without its own question
  // gets a synthetic question.
  r = await runTask('T1', { twoAssumptions: true })
  const syn = r.result.summary.questions.filter(q => q.synthetic)
  check(syn.length === 13 && syn.every(q => q.for_where === 'IOBoard.md:2'), `two assumptions: ${syn.length} synthetic, expected 13 for IOBoard.md:2`)
  // Two assumptions naming one question: the second is not asked by it.
  r = await runTask('T1', { sharedQ: true })
  const syn2 = r.result.summary.questions.filter(q => q.synthetic)
  check(syn2.length === 13 && syn2.every(q => q.for_where === 'IOBoard.md:2'), `shared question: ${syn2.length} synthetic, expected 13 for IOBoard.md:2`)
  r = await runTask('T1', { sharedQ: true, recheckRejects: true })
  check(r.result.followUps.filter(f => f.reason === 'assumption without a confirmed question').length === 13, 'shared question, synthetic rejected: the second assumption is listed')
  // A question linked from another location does not ask for the value.
  r = await runTask('T1', { twoAssumptions: true, wrongWhere: true })
  check(r.result.summary.questions.filter(q => q.synthetic).length === 26, 'assumption linked to a question for another location: asked anew')
  // A null-marker maker page written as not read has no status read.
  r = await runTask('T2', { hosts: [{ host: 'www.onsemi.com', probe: null, hold: ['R2'] }], markerText: 'not read: the page body carries no lifecycle status' })
  check(r.result.followUps.some(f => f.host === 'www.onsemi.com' && f.reason === 'lifecycle status not read from the page'), 'not read status: listed')
  // Two rows for one host are no reading.
  r = await runTask('T2', { hosts: [{ host: 'jlcpcb.com', client: 'jlcpcb-api', probe: 'C39843328', stop: 'stock-tasks', hold: [] }],
    p0: { hosts: [{ host: 'jlcpcb.com', url: 'C39843328', client: 'jlcpcb-api', http_status: 200, bytes: 1, status_marker: '', reachable: true, note: '' }, { host: 'jlcpcb.com', url: 'C39843328', client: 'jlcpcb-api', http_status: 503, bytes: 1, status_marker: '', reachable: false, note: '' }] } })
  check(r.result.summary.stopped === true, 'P0: two rows for JLCPCB stop a stock task')
  // A reachable maker page without its lifecycle status is listed.
  r = await runTask('T2', { hosts: [{ host: 'www.nxp.com', marker: 'Status', hold: ['R2'] }], p0: {} , markerless: true })
  check(r.result.followUps.some(f => f.host === 'www.nxp.com' && f.reason === 'lifecycle status not read from the page'), 'marker absent: listed')

  // A part to verify with no shortlist record is still verified.
  // An alternate with no record cannot show its gate evidence.
  r = await runTask('T2', { verify: ['partZ'], altName: 'partZ' })
  check(r1(r).ledger.some(l => l.part === 'partZ' && l.status === 'not verified'), 'alternate without record: not verified')
  check(r1(r).selection[0].alternate_unverified.includes('partZ'), 'alternate without record: recorded on the selection')
  // A replacement's rule-5 alternate is verified with it.
  r = await runTask('T2', { replacementAlt: true, refute: ['P4-stock-R1:part1'] })
  check(r1(r).selection[0].part === 'part2' && r1(r).selection[0].alternate_unverified.length === 0, 'replacement alternate: verified with the replacement')
  r = await runTask('T2', { replacementAlt: true, refute: ['P4-stock-R1:part1'], dropReplacementAlt: true })
  check(r1(r).selection[0].alternate_unverified.includes('altB'), 'replacement alternate: left out, recorded as unverified')
  check(r1(r).ledger.some(l => l.part === 'altB' && l.status === 'not verified'), 'replacement alternate: left out, marked not verified')
  // A P3 candidate the re-rank dropped stays dropped.
  r = await runTask('T2', { p3dropped: true })
  check(!r.result.followUps.some(f => f.part === 'partN'), 'explicit P3 drop: not put back')
  // T6 needs every output written and reviewed.
  r = await runTask('T6', { unwritten: 'hardware/README.md' })
  check(r.result.summary.stopped === true, 'T6: an unwritten output stops it')
  // The kind comes from the plan: an alternate reported as first still needs its compatibility checks.
  r = await runTask('T2', { verify: ['part2'], reportFirst: true, altName: 'part2' })
  check(r1(r).ledger.some(l => l.part === 'part2' && l.status === 'not verified'), 'canonical kind: alternate reported as first not verified')
  check(r1(r).selection[0].alternate_unverified.includes('part2'), 'canonical kind: unverified alternate recorded on the selection')
  // An overturned P2 exclusion the re-rank leaves out is listed.
  r = await runTask('T2', { p3overturned: true })
  check(r.result.followUps.some(f => f.part === 'partY'), 'overturned exclusion left out: listed')
  // A function requirement the candidate omits is added and must be checked.
  r = await runTask('T2', { fnReq: 'vmax' })
  check(r.result.followUps.some(f => /lacks the function's requirements: vmax/.test(f.reason)) && r1(r).selection[0].part === 'part1', 'function requirement: added, and verified when checked')
  r = await runTask('T2', { fnReq: 'vmax', skipReq: 'vmax' })
  check(r1(r).selection[0].part === null, 'function requirement: not verified when unchecked')
  // T6 stops when the critic's checks do not all pass.
  r = await runTask('T6', { failCheck: 'jlc_stock' })
  check(r.result.summary.stopped === true, 'T6: a failed check stops it')
  r = await runTask('T6')
  check(!r.result.summary.stopped, 'T6: passing checks finish it')

  // A second-vendor route needs the second vendor's stock re-read.
  r = await runTask('T2', { noSecondVendor: true })
  check(r1(r).selection[0].part === null, 'second-vendor route: not verified without the second-vendor stock check')

  // An alternate route that names no part is a missing second source.
  r = await runTask('T2', { emptyAlt: true })
  check(r1(r).selection[0].second_source_missing === true, 'alternate route without a part: second source missing')
  // Three group pages must be three files.
  r = await runTask('T6', { samePages: true })
  check(r.result.summary.stopped === true, 'T6: one file for three group pages stops it')
  // P5 and P6 items the critic did not check stay in the summary, unchecked.
  r = await runTask('T5', { nulls: { 'P5-critic': 2 } })
  check(r.result.summary.conflicts.length > 0 && r.result.summary.conflicts.every(c => c.unchecked), 'T5: unchecked conflicts stay in the summary')

  // An unverified first-ranked part is not passed over.
  r = await runTask('T2', { verify: ['part2'], omitPart: ['P4-datasheet-R1:part1'] })
  check(r1(r).selection[0].part === null, 'no promotion past an unverified first-ranked part')
  // Duplicated and contradictory rankings.
  r = await runTask('T2', { ranking: [{ rank: 1, part: 'part1', reason: 'r' }, { rank: 2, part: 'part1', reason: 'r' }, { rank: 3, part: 'part2', reason: 'r' }], refute: ['P4-stock-R1:part1'] })
  check(r.result.followUps.some(f => f.reason === 'ranked twice') && r1(r).selection[0].part === null, 'ranked twice: listed, function not ranked')
  r = await runTask('T2', { rankDropped: true })
  check(r.result.followUps.some(f => f.reason === 'ranked and dropped by the re-rank') && r1(r).selection[0].part === null, 'ranked and dropped: function not ranked')
  r = await runTask('T2', { ranking: [{ rank: 1, part: 'partTypo', reason: 'r' }, { rank: 2, part: 'part2', reason: 'r' }] })
  check(r.result.followUps.some(f => f.reason === 'ranked part has no record') && r1(r).selection[0].part === null, 'ranked part without a record: function not ranked')
  // A failed requirement and a failing check.
  r = await runTask('T2', { failedReq: true })
  check(r.result.followUps.some(f => /failed requirement/.test(f.reason)) && r1(r).selection[0].part !== 'part1', 'failed requirement: off the shortlist')
  r = await runTask('T2', { failPass: ['P4-datasheet-R1'] })
  check(r.calls.some(c => c.startsWith('adjudicator-R1')), 'a check that fails its requirement is a refutation')
  // Second sources that are not one.
  r = await runTask('T2', { selfAlt: true })
  check(r1(r).selection[0].second_source_missing === true, 'self-named alternate: second source missing')
  r = await runTask('T2', { offBoard: true })
  check(r1(r).selection[0].second_source_missing === true, 'part off the board by the second vendor: second source missing')
  // Ranks repeated or below 1 rank nothing.
  r = await runTask('T2', { ranking: [{ rank: 1, part: 'part1', reason: 'r' }, { rank: 1, part: 'part2', reason: 'r' }] })
  check(r1(r).selection[0].part === null && r.result.followUps.some(f => /not the ranks 1 to n/.test(f.reason)), 'repeated rank: nothing selected, listed')
  r = await runTask('T2', { ranking: [{ rank: 0, part: 'part1', reason: 'r' }, { rank: 1, part: 'part2', reason: 'r' }] })
  check(r1(r).selection[0].part === null, 'rank 0: nothing selected')
  // A rule-5 alternate that fails a requirement is no alternate.
  r = await runTask('T2', { altFails: true, verify: ['altA'] })
  check(r.result.followUps.some(f => /alternate with a failed requirement/.test(f.reason)) && r1(r).selection[0].alternate_unverified.includes('altA'), 'alternate failing a requirement: dropped, alternate not verified')
  // A function P2 did not return stays open; a category without P3 keeps no part.
  r = await runTask('T2', { missedFn: true })
  check(r1(r).selection.some(e => e.function === 'f2' && e.part === null) && r.result.followUps.some(f => f.function === 'f2'), 'function P2 did not return: open, listed')
  r = await runTask('T2', { nulls: { 'P3-R1': 2 } })
  check(r1(r).status === 'done without P3' && r1(r).selection[0].part === null && r1(r).selection[0].without_p3 === 'part1', 'no P3: no part kept')
  r = await runTask('T2', { noSecond: true })
  check(r1(r).selection[0].part === 'part1' && r1(r).selection[0].second_source_missing === true, 'no second source route: second source missing')
  r = await runTask('T2')
  check(r1(r).selection[0].second_source_missing === false, 'second-vendor route with stock checked: second source present')
  // A function with no requirements, and one only the re-rank names.
  r = await runTask('T2', { noReqs: true })
  check(r1(r).selection[0].part === null && r.result.followUps.some(f => f.reason === 'function lists no requirement'), 'no requirements: not verified, listed')
  r = await runTask('T2', { extraFn: true })
  check(r.result.followUps.some(f => f.reason === 'function P2 did not return') && r1(r).selection.length === 1, 're-rank-only function: listed, not verified')
  // R10 and R12 name a Q4 or Q8 alternative.
  r = await runTask('T4')
  check(r.result.followUps.some(f => f.category === 'R10' && /no Q4 function with an alternative/.test(f.reason)), 'R10 without a Q4 alternative: listed')
  // A Q4 or Q8 alternative is verified with its own rule-5 alternate.
  const r10 = x => x.result.summary.results.find(c => c.category === 'R10')
  r = await runTask('T4', { qAlt: true, p4parts: ['part3', 'altQ'] })
  let q = r10(r).selection[0].q_alternatives[0]
  check(q && q.status === 'verified' && q.alternate === 'altQ' && q.alternate_status === 'verified' && !q.second_source_missing, 'Q alternative and its alternate: both verified')
  r = await runTask('T4', { qAlt: true, p4parts: ['part3'] })
  q = r10(r).selection[0].q_alternatives[0]
  check(q && q.status === 'verified' && q.alternate_status === 'not verified', 'Q alternative without its alternate checked: alternate not verified')
  // Only the kept part's alternate gates it; a refuted part's does not.
  r = await runTask('T2', { altName: 'partZ', verify: ['partZ'], refute: ['P4-stock-R1:part1'] })
  check(r1(r).selection[0].part === 'part2' && r1(r).selection[0].alternate_unverified.length === 0, 'refuted part\'s alternate: not on the kept part')
  // A part the pair was not asked to verify is ignored.
  r = await runTask('T2', { p4parts: ['part3'] })
  check(!r1(r).ledger.some(l => l.part === 'part3') && r.result.followUps.some(f => f.part === 'part3' && /not asked to verify/.test(f.reason)), 'unsolicited part: ignored, listed')
  // R10 and R12 without any Q alternative stay open.
  r = await runTask('T4')
  check(r.result.summary.q_missing.includes('R10') && r.result.summary.q_missing.includes('R12') && !r.result.summary.q_missing.includes('R9'), 'R10 and R12 without a Q alternative: open')
  r = await runTask('T4', { qAlt: true, p4parts: ['part3', 'altQ'] })
  check(r.result.summary.q_missing.length === 0, 'R10 and R12 with a Q alternative: not open for it')
  // A value for research P2 did not return stays open, whatever P4 says.
  r = await runTask('T2', { forResearch: [{ id: 'V9', category: 'R1' }] })
  check(r.result.summary.figures_open.R1.includes('found V9'), 'value for research not returned: open')
  // A question's own category or source key does not move it.
  r = await runTask('T1', { qCategory: true })
  check(r.result.summary.questions.length > 0 && r.result.summary.questions.every(q => q.category !== 'R99' && q.source !== 'mock'), 'question keys from the agent do not override the category')
  // A decision-only question that names no decision blocks P2.
  r = await runTask('T1', { decisionNone: true })
  check(r.result.summary.questions.length > 0 && r.result.summary.questions.every(q => q.blocks === 'p2' || ['Q4', 'Q8', 'Q9'].includes(q.decision)) && r.result.followUps.some(f => /names no decision/.test(f.reason)), 'decision-only without a decision: blocks P2')
  // A part refuted in the first pair does not return as an alternate.
  r = await runTask('T2', { backAlt: true, refute: ['P4-stock-R1:part1'] })
  check(r1(r).selection[0].part === 'part2' && r1(r).selection[0].refuted.includes('part1') && r1(r).selection[0].alternate_unverified.includes('part1'), 'refuted part as a later alternate: stays refuted')
  // A P3 find dropped from the shortlist is re-read.
  r = await runTask('T2', { p3missed: true, p3dropShort: true, omitFigure: ['P4-datasheet-R1', 're-rank drop: f1: partX'] })
  check(r.result.followUps.some(f => f.figure === 're-rank drop: f1: partX' && f.reason === 'figure not verified'), 'P3 find dropped from the shortlist: re-read')
  // Functions and parts given twice.
  r = await runTask('T2', { rrDup: true })
  check(r1(r).selection[0].part === null && r.result.followUps.some(f => /function returned twice/.test(f.reason)), 'function ranked twice: not ranked')
  r = await runTask('T2', { p2DupPart: true })
  check(r1(r).selection[0].part !== 'part1' && r.result.followUps.some(f => /more than one record/.test(f.reason)), 'part with two records: dropped')
  // Values and checks written as not read.
  r = await runTask('T2', { forResearch: [{ id: 'V9', category: 'R1' }], foundNotRead: true })
  check(r.result.summary.figures_open.R1.includes('found V9'), 'value found as not read: open')
  r = await runTask('T2', { readNone: true })
  check(r1(r).selection[0].part === null, 'checks read as not read: not verified')
  // No ranking, or no requirement, leaves the function open.
  r = await runTask('T2', { noRerankFn: true })
  check(r1(r).selection[0].part === null, 'function the re-rank did not rank: open')
  r = await runTask('T2', { emptyRanking: true })
  check(r1(r).selection[0].part === null, 'empty ranking: open')
  r = await runTask('T2', { fnNoReq: true })
  check(r1(r).selection[0].part === null, 'function with no requirement: open')
  // A requirement named twice has no single value to check against.
  r = await runTask('T2', { dupReq: true })
  check(r1(r).selection[0].part === null && r.result.followUps.some(f => /names a requirement twice/.test(f.reason)), 'requirement named twice: function open, listed')
  check(r.prompts.find(x => x.label === 'P2-R1').prompt.includes('Give each requirement of a function a name of its own.'), 'P2 is told to name each requirement once')
  // A Q alternative whose alternate is the kept part: the kept part needs the alternate's checks.
  r = await runTask('T4', { qAltBack: true, p4parts: ['part3'] })
  check(r.result.summary.results.find(c => c.category === 'R10').ledger.some(l => l.part === 'part1' && l.as === 'alternate' && l.status === 'not verified'), 'kept part that is a Q alternative\'s alternate: compatibility checks required for that role')
  // A Q alternative outside the decision's function is not one.
  r = await runTask('T2', { qAlt: true, p4parts: ['part3', 'altQ'] })
  check(r1(r).selection[0].q_alternatives.length === 0 && r.result.followUps.some(f => /implements no decision/.test(f.reason)), 'Q alternative on a function with no decision: not verified as one')
  r = await runTask('T4', { qAlt: true, p4parts: ['part3', 'altQ'], qDecision: 'none' })
  check(r.result.summary.q_missing.includes('R10'), 'R10 with its alternative on no Q4 function: open')
  // A Q alternative that is the kept part is none.
  r = await runTask('T4', { qSelf: true })
  check(r.result.summary.q_missing.includes('R10') && r.result.summary.results.find(x => x.category === 'R10').selection[0].q_alternatives.length === 0, 'kept part named as the Q alternative: none')
  // A figure confirmed without evidence read is open.
  r = await runTask('T2', { unreadFigure: true })
  check(r.result.summary.figures_open.R1.length === 1 && r.result.followUps.some(f => f.reason === 'figure not verified'), 'figure confirmed with no evidence read: open')
  // A check whose critic did not return is missing.
  r = await runTask('T5', { nulls: { 'P6-critic': 2 } })
  check(r.result.summary.missing_checks.includes('P6-critic'), 'T5: a P6 critic that returned nothing is a missing check')
  r = await runTask('T5', { nulls: { 'P5-critic': 2 } })
  check(r.result.summary.missing_checks.includes('P5-critic'), 'T5: a P5 critic that returned nothing is a missing check')
  r = await runTask('T5')
  check(r.result.summary.missing_checks.length === 0, 'T5: no missing check when every agent returns')
  // Ranks start at 1.
  r = await runTask('T2', { ranking: [{ rank: 2, part: 'part1', reason: 'r' }, { rank: 3, part: 'part2', reason: 'r' }] })
  check(r1(r).selection[0].part === null, 'ranking without rank 1: function not ranked')
  // A check without its source shows nothing.
  r = await runTask('T2', { noSource: true })
  check(r1(r).selection[0].part === null, 'checks without a source: not verified')
  // A part on the board needs an alternate on the board.
  r = await runTask('T2', { offBoardAlt: true, verify: ['altOff'] })
  check(r1(r).selection[0].part === 'part1' && r1(r).selection[0].second_source_missing === true, 'off-board alternate of a part on the board: second source missing')
  // A part off the board needs an alternate off the board.
  r = await runTask('T2', { onBoardAltOfOff: true, verify: ['altOn'] })
  check(r1(r).selection[0].second_source_missing === true, 'on-board alternate of a part off the board: second source missing')
  // A candidate's weaker requirement value is replaced by the function's.
  r = await runTask('T2', { weakReq: true })
  const p4ds = r.prompts.find(x => x.label === 'P4-datasheet-R1').prompt
  check(r.result.followUps.some(f => /states other values than the function/.test(f.reason)) && p4ds.includes('"required":">= 67.2 V"') && !p4ds.includes('"required":">= 40 V"'), 'candidate restating a requirement: checked against the function value')
  // A figure confirmed without its source is open.
  r = await runTask('T2', { unsourcedFigure: true })
  check(r.result.summary.figures_open.R1.length === 1, 'figure confirmed without a source: open')
  // P2's own drop of a shortlisted candidate is re-read by the datasheet verifier.
  r = await runTask('T2', { failedReq: true })
  check(r.prompts.find(x => x.label === 'P4-datasheet-R1').prompt.includes('failed requirement: f1: part1 (x0)'), 'shortlisted candidate failing its own requirement: re-read by P4')
  // An alternate shared by the kept part and a Q alternative is not verified for the latter.
  r = await runTask('T4', { sharedAlt: true, p4parts: ['part3', 'altS'] })
  const qs = r.result.summary.results.find(c => c.category === 'R10').selection[0].q_alternatives[0]
  check(qs && /shared with part1/.test(qs.alternate_status), 'alternate shared with the kept part: not verified for the Q alternative')
  // Two Q alternatives naming one alternate: verified for the first only.
  r = await runTask('T4', { twoQShare: true, p4parts: ['part2', 'part3', 'altS2'] })
  const q2 = r.result.summary.results.find(c => c.category === 'R10').selection[0].q_alternatives
  check(q2.length === 2 && q2[0].alternate_status === 'verified' && /shared with part2/.test(q2[1].alternate_status), 'alternate shared by two Q alternatives: verified for the first only')
  // A replacement part owes its per-part figures.
  r = await runTask('T2', { perPartReports: { R2: ['clock tolerance'] }, reportParts: ['clock tolerance: part1'], refute: ['P4-stock-R2:part1'] })
  check(r.result.summary.results.find(c => c.category === 'R2').selection[0].part === 'part2' && r.result.summary.figures_open.R2.includes('report: clock tolerance: part2'), 'replacement part without its per-part figure: open')
  // Placements, maker and reading time are checked for every part.
  r = await runTask('T2', { noPlacementsCheck: true })
  check(r1(r).selection[0].part === null, 'no placements check: not verified')
  r = await runTask('T2', { noMakerCheck: true })
  check(r1(r).selection[0].part === null, 'no manufacturer allowlist check: not verified')
  r = await runTask('T2', { undated: true })
  check(r1(r).selection[0].part === null, 'undated checks: not verified')
  // A function's requirement list is re-read against the specification.
  r = await runTask('T2', { omitFigure: ['P4-datasheet-R1', 'function requirements: f1'] })
  check(r.result.summary.figures_open.R1.includes('function requirements: f1'), 'function requirement list not confirmed: open')
  // An alternate with fewer placements than its primary is no second source.
  r = await runTask('T2', { altFewer: true, verify: ['altF'] })
  check(r1(r).selection[0].part === 'part1' && r1(r).selection[0].second_source_missing === true, 'alternate with fewer placements: second source missing')
  // A first-ranked part that is also a Q alternative's alternate keeps its primary result.
  r = await runTask('T4', { qAltBack: true, p4parts: ['part3'] })
  const r10b = r.result.summary.results.find(c => c.category === 'R10')
  check(r10b.selection[0].part === 'part1' && r10b.selection[0].q_alternatives[0].alternate_status === 'not verified', 'part in both roles: selected as primary, alternate role not verified without its compatibility checks')
  // A part refuted as an alternate on its fit alone keeps its own place on
  // the shortlist; on any other check it is refuted in the function.
  r = await runTask('T2', { altName: 'part2', verify: ['part2'], refute: ['P4-stock-R1:part1', 'P4-datasheet-R1:part2'], editPart: { 'P4-datasheet-R1:part2': { 'pin-for-pin match': { agrees: false, passes: false } } } })
  check(r1(r).selection[0].part === 'part2', 'part refuted as an alternate on its fit only: still a candidate in its own right')
  r = await runTask('T2', { altName: 'part2', verify: ['part2'], refute: ['P4-stock-R1:part1'], editPart: { 'P4-stock-R1:part2': { 'end-of-life notices': { read: 'last-time-buy notice 2026-08-01', agrees: false, passes: false } } } })
  check(r1(r).selection[0].part === 'part3' && r1(r).selection[0].refuted.includes('part2') && !r1(r).ledger.some(l => l.part === 'part2' && l.status === 'verified'), 'part refuted as an alternate on end-of-life notices: not kept in its own right, and no pair for it')
  r = await runTask('T2', { alts: { part1: 'part3' }, p4parts: ['part3'], refute: ['P4-stock-R1:part1', 'P4-stock-R1-2:part2'], editPart: { 'P4-stock-R1:part3': { 'end-of-life notices': { read: 'last-time-buy notice 2026-08-01', agrees: false, passes: false } } } })
  check(!r.calls.includes('P4-stock-R1-3') && r.result.followUps.some(f => f.part === 'part2' && f.reason === 'refuted, and no next-ranked candidate'), 'part refuted as an alternate on end-of-life notices: not the next part after a refutation')
  // A rule-5 alternate the re-rank drops as a candidate keeps its record.
  r = await runTask('T2', { altName: 'part2', verify: ['part2'], ranking: [{ rank: 1, part: 'part1', reason: 'r' }, { rank: 2, part: 'part3', reason: 'r' }], dropShort: ['part2'] })
  check(r1(r).selection[0].part === 'part1' && r1(r).selection[0].alternate_unverified.length === 0, 'alternate dropped as a candidate: verified as the alternate')
  // The alternate of a dropped part is a candidate the re-rank left out.
  r = await runTask('T2', { altName: 'part3', ranking: [{ rank: 1, part: 'part2', reason: 'r' }], dropShort: ['part1'], p4parts: ['part2'], refute: ['P4-stock-R1:part2'] })
  check(r1(r).selection[0].part === 'part3' && r.result.followUps.some(f => f.part === 'part3' && f.reason === 'candidate neither ranked nor dropped'), 'alternate of a dropped part: kept and listed, next after a refutation')
  // The LCSC row must be the candidate.
  r = await runTask('T2', { noIdentity: true })
  check(r1(r).selection[0].part === null, 'no LCSC identity check: not verified')
  // Whether a part is on the board is re-derived, not taken from its LCSC number.
  r = await runTask('T2', { lcsc: 'none' })
  check(r1(r).selection[0].part === 'part1', 'part off the board with its board placement checked: verified')
  r = await runTask('T2', { lcsc: 'none', noBoardCheck: true })
  check(r1(r).selection[0].part === null, 'part off the board without a board placement check: not verified')
  // Every option class of Q4 has a part.
  r = await runTask('T4', { qAlt: true, p4parts: ['part3', 'altQ'], qOptions: { Q4: ['reference', 'external ADC'] }, qOption: 'external ADC', keptOption: 'reference' })
  check(!r.result.summary.q_missing.includes('R10'), 'Q4 with a reference kept and an external ADC alternative: covered')
  r = await runTask('T4', { qAlt: true, p4parts: ['part3', 'altQ'], qOptions: { Q4: ['reference', 'external ADC'] }, qOption: 'external ADC' })
  check(r.result.summary.q_missing.includes('R10') && r.result.summary.results.find(c => c.category === 'R10').selection[0].q_options_missing.includes('reference'), 'Q4 without a reference option: open')
  // One part cannot stand for two option classes.
  r = await runTask('T4', { qDup: true, p4parts: ['part3'], qOptions: { Q4: ['reference', 'external ADC'] } })
  check(r.result.summary.q_missing.includes('R10') && r.result.followUps.some(f => /listed more than once/.test(f.reason)), 'one part listed for two Q classes: not verified as either')
  // A class the datasheet verifier did not confirm does not count.
  r = await runTask('T4', { qAlt: true, p4parts: ['part3', 'altQ'], qOptions: { Q4: ['reference', 'external ADC'] }, qOption: 'external ADC', keptOption: 'reference', omitFigure: ['P4-datasheet-R10', 'Q option: f1: part3: external ADC'] })
  check(r.result.summary.results.find(c => c.category === 'R10').selection[0].q_options_missing.includes('external ADC'), 'Q class not confirmed: missing')
  // A ruling without its source is no ruling.
  r = await runTask('T2', { refute: ['P4-stock-R1:part1'], stands: false, unsourcedRuling: true })
  check(r1(r).selection[0].part === null, 'ruling without a source: no ruling')
  // A function of the P1 inventory that P2 did not return stays open.
  r = await runTask('T2', { inventory: { R1: ['f1', 'latch'] } })
  check(r1(r).selection.some(e => e.function === 'latch' && e.part === null) && r.result.followUps.some(f => f.function === 'latch' && /P1 inventory/.test(f.reason)), 'inventory function P2 did not return: open')
  check(r.prompts.find(x => x.label === 'P2-R1').prompt.includes('latch'), 'P2 is told the inventory')
  // T1 returns each category's inventory from P1 and its critic.
  r = await runTask('T1')
  check(r.result.summary.inventory && Array.isArray(r.result.summary.inventory.R1) && r.result.summary.inventory.R1.length > 0, 'T1: inventory per category')
  // P5's assumptions are sourced and ruled on.
  r = await runTask('T5', { unsourcedAssumption: true })
  check(r.result.summary.unsourced_items > 0, 'T5: an unsourced assumption is counted')
  r = await runTask('T5', { rejectAssumption: true })
  check(r.result.summary.rejected_items === 1, 'T5: a rejected assumption is counted')
  // A combination that does not fit supports nothing.
  r = await runTask('T5', { noFit: true })
  check(r.result.summary.missing_checks.includes('P5 combinations'), 'T5: no fitting combination is a missing check')
  // Combinations need their source and time.
  r = await runTask('T5', { unsourcedCombos: true })
  check(r.result.summary.missing_checks.includes('P5 combinations') && r.result.summary.unsourced_items > 0, 'T5: unsourced combinations are no combination')
  // Two functions marked with one decision: none counts.
  r = await runTask('T4', { qAlt: true, p4parts: ['part3', 'altQ'], extraFn: true, twoDeciders: true })
  check(r.result.summary.q_missing.includes('R10') && r.result.followUps.some(f => f.category === 'R10' && /functions marked Q4; none counts/.test(f.reason)), 'two Q4 functions: neither counts')
  // The library type of the JLCPCB row is checked.
  r = await runTask('T2', { noLibType: true })
  check(r1(r).selection[0].part === null, 'no library type check: not verified')
  // A reading time must be a date.
  r = await runTask('T2', { undated: true })
  check(r1(r).selection[0].part === null, 'blank reading time: not verified')
  r = await runTask('T5', { p5Budgets: ['GPIO'], p5Items: ['GPIO'], budgetTime: 'unknown' })
  check(r.result.summary.budgets_missing.includes('GPIO'), 'T5: a reading time that is no date is missing')
  // A malformed LCSC number drops the candidate.
  r = await runTask('T2', { lcsc: 'C123oops' })
  check(r1(r).selection[0].part === null && r.result.followUps.some(f => /LCSC number "C123oops"; dropped/.test(f.reason)), 'malformed LCSC number: dropped')
  // A ranked part whose record is dropped is a ranked part with no record:
  // the function is not ranked, and rank 2 does not take it over.
  for (const rec of [{ lcsc: 'C 2868250' }, { placements: 0 }]) {
    r = await runTask('T2', { firstRecord: rec, p4parts: ['part2'] })
    check(r1(r).selection[0].part === null && r.result.followUps.some(f => f.part === 'part1' && f.reason === 'ranked part has no record'), `first-ranked part with ${JSON.stringify(rec)}: function not ranked`)
  }
  // A failure against a restated, stricter value is not a failure.
  r = await runTask('T2', { strictReq: true })
  check(r1(r).selection[0].part === 'part1', 'failure against a restated value: checked against the function value, kept')
  // Placements below 1 drop the candidate.
  r = await runTask('T2', { placements: 0 })
  check(r1(r).selection[0].part === null && r.result.followUps.some(f => /placements 0; dropped/.test(f.reason)), 'placements 0: dropped')
  // Per-part report figures are owed for every part the category verifies.
  r = await runTask('T2', { perPartReports: { R2: ['clock tolerance'] } })
  check(r.result.summary.figures_open.R2.includes('report: clock tolerance: part1'), 'per-part figure not reported: open')
  r = await runTask('T2', { perPartReports: { R2: ['clock tolerance'] }, reportParts: ['clock tolerance: part1'] })
  check(!r.result.summary.figures_open.R2.some(f => f.startsWith('report: clock tolerance')), 'per-part figure reported for the verified part: returned')
  // The lifecycle table's readings are required.
  r = await runTask('T2', { noLifecycleReadings: true })
  check(r1(r).selection[0].part === null, 'no lifecycle readings: not verified')
  // A budget without its reading time is missing.
  r = await runTask('T5', { p5Budgets: ['GPIO'], p5Items: ['GPIO'], budgetUndated: true })
  check(r.result.summary.budgets_missing.includes('GPIO'), 'T5: an undated budget is missing')
  // Budgets need a value; not applicable only where the check is conditional.
  r = await runTask('T5', { p5Budgets: ['GPIO', 'absent IOVDD'], p5Conditional: ['absent IOVDD'], p5Items: ['GPIO', 'absent IOVDD'], budgetValues: { GPIO: 'not read: timeout', 'absent IOVDD': 'not applicable: ratings as cited' } })
  check(r.result.summary.budgets_missing.length === 1 && r.result.summary.budgets_missing[0] === 'GPIO', 'T5: an unread budget value is missing; a conditional not applicable is not')
  r = await runTask('T5', { p5Budgets: ['GPIO'], p5Items: ['GPIO'], budgetValues: { GPIO: 'not applicable: x' } })
  check(r.result.summary.budgets_missing.includes('GPIO'), 'T5: not applicable for an unconditional budget is missing')
  r = await runTask('T5', { p5Budgets: ['GPIO'], p5Items: ['GPIO'], blankSource: true })
  check(r.result.summary.budgets_missing.includes('GPIO'), 'T5: a whitespace source is none')
  // A held part's failing live stock is superseded by its held quantity.
  r = await runTask('T2', { held: 500, heldChecks: true, heldAndLive: true })
  check(r1(r).selection[0].part === 'part1' && !r.calls.some(c => c.startsWith('adjudicator-R1')), 'held part with failing live stock: verified on its held quantity')
  // The P5 row's budgets are each returned and upheld.
  r = await runTask('T5', { p5Budgets: ['GPIO', 'DMA channels'], p5Items: ['GPIO', 'DMA channels: PPM'] })
  check(r.result.summary.budgets_missing.length === 0, 'T5: every named budget returned and upheld')
  r = await runTask('T5', { p5Budgets: ['GPIO', 'DMA channels'], p5Items: ['GPIO'] })
  check(r.result.summary.budgets_missing.includes('DMA channels'), 'T5: a named budget not returned is missing')
  r = await runTask('T5', { p5Budgets: ['GPIO'], p5Items: ['GPIO'], rejectBudget: true })
  check(r.result.summary.budgets_missing.includes('GPIO'), 'T5: a named budget the critic rejected is missing')
  // Rule 6: a held part passes on its held quantity.
  r = await runTask('T2', { held: 500, heldChecks: true })
  check(r1(r).selection[0].part === 'part1', 'held part with its held quantity checked: verified')
  r = await runTask('T2', { held: 0, heldChecks: true })
  check(r1(r).selection[0].part === null, 'part not held, checked on a held quantity: not verified')
  // A required figure reported per part counts as returned.
  r = await runTask('T2', { requiredReports: { R2: ['frames held'] }, reportPerPart: true })
  check(!r.result.summary.figures_open.R2.includes('P2 report: frames held') && !r.result.followUps.some(f => f.figure === 'P2 report: frames held'), 'required figure reported per part: returned')
  check(r.prompts.find(x => x.label === 'P2-R2').prompt.includes('frames held'), 'P2 prompt names the required figures')
  // A ruling with no evidence read is no ruling.
  r = await runTask('T2', { refute: ['P4-stock-R1:part1'], stands: false, unreadRuling: true })
  check(r1(r).selection[0].part === null && r.result.followUps.some(f => f.reason === 'the ruling gives no evidence, source and time read'), 'ruling without evidence read: no ruling')
  // An overturned exclusion is handled by the function that dropped it.
  r = await runTask('T2', { p3overturned: true, p2DropY: true, extraFn: true, rrHandleYin: 'fX' })
  check(r1(r).selection.find(e => e.function === 'f1').part === null, 'overturned exclusion handled by another function: owner stays open')
  // A P3 find the re-rank did not handle keeps its function open.
  r = await runTask('T2', { p3missed: true })
  check(r1(r).selection[0].part === null, 'unhandled P3 find: function open')
  // A P3 find or overturned exclusion under no function of P2's has no owner:
  // one of P2's functions handles it, or every function stays open.
  r = await runTask('T2', { p3missed: true, p3missedFn: 'F1 buck' })
  check(r1(r).selection[0].part === null, 'P3 find under a function P2 did not return, unhandled: every function open')
  r = await runTask('T2', { p3missed: true, p3missedFn: 'F1 buck', p3dropShort: true })
  check(r1(r).selection[0].part === 'part1', 'P3 find under a function P2 did not return, dropped under f1: handled')
  r = await runTask('T2', { p3missed: true, p3missedFn: 'fX', extraFn: true, rrHandleXin: true })
  check(r1(r).selection[0].part === null, 'P3 find handled only under a function P2 did not return: open')
  r = await runTask('T2', { p3overturned: true, extraFn: true, rrHandleYin: true })
  check(r1(r).selection[0].part === null, 'overturned exclusion without an owner, handled only under a function P2 did not return: open')
  // A fixed input of the Scope table is reported to the owner, not
  // re-selected: ranked below another part, refuted, or passed over for a
  // verified part of the verify list.
  const fixedPart1 = { R1: [{ function: 'f1', part: 'PART1' }] }
  r = await runTask('T2', { fixedInputs: fixedPart1 })
  check(r1(r).selection[0].part === 'part1' && !r.result.followUps.some(f => f.role === 'owner'), 'fixed input ranked first and verified: kept')
  check(['P2-R1', 'rerank-R1'].every(l => r.prompts.find(x => x.label === l).prompt.includes('f1 for the PART1')), 'fixed input: P2 and the re-rank are told')
  r = await runTask('T2', { fixedInputs: { R1: [{ function: 'f1', part: 'INA238' }] }, firstRecord: { part: 'INA238AIDGSR' }, p4parts: ['INA238AIDGSR'],
    ranking: ['INA238AIDGSR', 'part2', 'part3'].map((part, i) => ({ rank: i + 1, part, reason: 'r' })) })
  check(r1(r).selection[0].part === 'INA238AIDGSR', 'fixed input named by its part number\'s start: kept')
  r = await runTask('T2', { fixedInputs: { R1: [{ function: 'f1', part: 'part2' }] } })
  check(r1(r).selection[0].part === null && r.result.followUps.some(f => f.role === 'owner' && f.part === 'part1' && /not ranked first/.test(f.reason)), 'fixed input ranked below another part: function open, reported to the owner')
  r = await runTask('T2', { fixedInputs: fixedPart1, refute: ['P4-stock-R1:part1'] })
  check(r1(r).selection[0].part === null && !r.calls.includes('P4-stock-R1-2') && r.result.followUps.some(f => f.role === 'owner' && f.part === 'part1' && /refuted/.test(f.reason)), 'fixed input refuted: no next-ranked part, reported to the owner')
  r = await runTask('T2', { fixedInputs: fixedPart1, verifyFirst: ['part2'], p4parts: ['part2'], refute: ['P4-stock-R1:part1'] })
  check(r1(r).ledger.some(l => l.part === 'part2' && l.status === 'verified') && r1(r).selection[0].part === null, 'fixed input refuted, another part verified: not kept')
  r = await runTask('T1', { fixedInputs: { R1: [{ function: 'Microcontroller', part: 'part1' }] } })
  check(r.prompts.find(x => x.label === 'P1-R1').prompt.includes('Microcontroller for the part1'), 'fixed input: P1 names its function')
  r = await runTask('T2', { fixedInputs: { R1: [{ function: 'Microcontroller', part: 'part1' }] } })
  check(r1(r).selection.some(e => e.function === 'Microcontroller' && e.part === null) && r.result.followUps.some(f => f.function === 'Microcontroller' && /fixed input that P2 did not return/.test(f.reason)), 'fixed input function P2 did not return: open')
  // A part one verifier lists twice has no verdict from it.
  r = await runTask('T2', { dupRow: ['P4-stock-R1'] })
  check(r1(r).selection[0].part === null && r.result.followUps.some(f => /listed the part twice; no verdict/.test(f.reason)), 'part listed twice by one verifier: no verdict')
  // A return for another category is not the category's return.
  r = await runTask('T2', { wrongCategory: true })
  check(!('R1' in r.result.summary.selection) && r.result.missing.some(m => /returned for category/.test(m.error || '')), 'return for another category: not returned')
  // An empty P5 result, and unruled gaps, keep T6 open.
  r = await runTask('T5', { p5Empty: true })
  check(r.result.summary.missing_checks.includes('P5 combinations') && r.result.summary.missing_checks.includes('P5 budgets'), 'T5: empty P5 budgets and combinations are missing checks')
  r = await runTask('T5', { nulls: { 'P6-critic': 2 } })
  check(r.result.summary.unchecked_items === r.result.summary.gaps.length && r.result.summary.gaps.length > 0, 'T5: unruled gaps are unchecked items')
  // A ruling about another item does not count.
  r = await runTask('T2', { refute: ['P4-stock-R1:part1'], wrongRuling: true })
  check(r.result.followUps.some(f => f.reason === 'the ruling names another item') && r1(r).selection[0].part === null, 'ruling about another item: no ruling')
  // Values for research and required report figures are checked whether or not P2 returned them.
  r = await runTask('T2', { forResearch: [{ id: 'V9', category: 'R1' }], requiredReports: { R2: ['frames held'] } })
  check(r.result.followUps.some(f => f.question === 'V9') && r.result.followUps.some(f => f.figure === 'frames held' && /required report/.test(f.reason)), 'for-research value and required figure: listed when missing')
  // P5 that returns nothing, and combinations the critic did not rule on.
  r = await runTask('T5', { nulls: { P5: 2 } })
  check(r.result.summary.missing_checks.includes('P5'), 'T5: a missing P5 is recorded')
  r = await runTask('T5', { nulls: { 'P5-critic': 2 } })
  check(r.result.summary.unchecked_items > 0, 'T5: unchecked combinations and budgets are counted')
  // The T6 critic's findings.
  r = await runTask('T6', { criticDisagrees: true })
  check(r.result.summary.stopped === true, 'T6: a figure the critic finds wrong stops it')
  r = await runTask('T6', { sentenceIssue: true })
  check(r.result.summary.stopped === true, 'T6: a writing issue left stops it')
  r = await runTask('T6', { oneFigure: true })
  check(r.result.summary.stopped === true, 'T6: a critic that checked one page stops it')
  r = await runTask('T6', { pagePower: true })
  check(r.result.summary.stopped === true, 'T6: a fixed output used as a group page stops it')
  r = await runTask('T5')
  check(r.result.summary.rejected_items === 0 && r.result.summary.unchecked_items === 0, 'T5: every item upheld')
  r = await runTask('T5', { rejectBudget: true })
  check(r.result.summary.rejected_items === 1 && r.result.followUps.some(f => /critic rejected/.test(f.reason)), 'T5: a rejected budget is counted and listed')
  r = await runTask('T5', { throws: ['P5'] })
  check(r.result.summary.missing_checks.includes('P5'), 'T5: a P5 chain that fails is a missing check')
  // A category whose chain failed is not a selection run.
  r = await runTask('T2', { nulls: { 'P2-R1': 2 } })
  check(!('R1' in r.result.summary.selection) && r.result.summary.chain_failed.includes('R1') && 'R2' in r.result.summary.selection, 'failed chain: category left out of the selection')
  r = await runTask('T6', { pageOutside: true })
  check(r.result.summary.stopped === true, 'T6: a group page outside hardware/docs/ stops it')
  r = await runTask('T6', { noFigures: true })
  check(r.result.summary.stopped === true, 'T6: a critic that checked no figure stops it')

  // P1: contradictory marking rulings, duplicate verdicts, and an assumption
  // whose question the re-check rejects.
  r = await runTask('T1', { contradict: true })
  check(r.result.followUps.some(f => /taken as an assumption/.test(f.reason)) && r.result.summary.questions.some(q => q.synthetic), 'P1: a contradictory marking ruling becomes a question')
  r = await runTask('T1', { dupVerdicts: true })
  check(r.result.followUps.some(f => /more than one verdict/.test(f.reason)) && r.result.followUps.some(f => f.reason === 'P1 question not ruled on'), 'P1: duplicate verdicts count as not ruled')
  r = await runTask('T1', { assumption: true, recheckRejects: true })
  check(r.result.followUps.some(f => f.reason === 'assumption without a confirmed question'), 'P1: an assumption without a confirmed question is listed')
  // A critic addition for another value on the assumption's line does not
  // ask for the assumption; one for its quantity does.
  const addAt = forQuantity => ({ function: 'display', question: `State the ${forQuantity}, in V`, why: 'w', blocks: 'p2', decision: 'none', for_where: 'IOBoard.md:1', for_quantity: forQuantity })
  r = await runTask('T1', { assumption: true, criticAdds: addAt('cable drop') })
  check(r.result.summary.questions.filter(q => q.synthetic).length === 13, 'P1: an addition for another quantity on the line leaves the assumption asked anew')
  r = await runTask('T1', { assumption: true, criticAdds: addAt('cable drop'), recheckRejectSynthetic: true })
  check(r.result.followUps.filter(f => f.reason === 'assumption without a confirmed question').length === 13, 'P1: a confirmed addition for another quantity on the line does not ask the assumption')
  r = await runTask('T1', { assumption: true, criticAdds: addAt('ripple') })
  check(!r.result.summary.questions.some(q => q.synthetic) && !r.result.followUps.some(f => f.reason === 'assumption without a confirmed question'), 'P1: an addition for the assumption\'s quantity asks it')
  // Two assumptions on one line: a question for the first does not ask the
  // second.
  r = await runTask('T1', { sameLine: true })
  check(r.result.summary.questions.filter(q => q.synthetic && q.for_quantity === 'B').length === 13, 'P1: a question for another quantity on the line does not ask the assumption')
  // Two values of one line and quantity are ruled by index.
  r = await runTask('T1', { sameQuantity: true })
  check(!r.result.followUps.some(f => /more than one verdict|marking not ruled on/.test(f.reason)), 'P1: two values of one quantity on one line are each ruled')
  // A verdict that restates the marking upholds it.
  r = await runTask('T1', { restate: true })
  check(!r.result.followUps.some(f => /taken as an assumption/.test(f.reason)) && !r.result.summary.questions.some(q => q.synthetic), 'P1: holds with the same marking keeps the marking')
  // A verdict without its reason or evidence rules on nothing.
  r = await runTask('T1', { blankReason: true })
  check(r.result.followUps.filter(f => f.reason === 'marking not ruled on').length === 26 && r.result.followUps.filter(f => f.reason === 'P1 question not ruled on').length === 26, 'P1: critic verdicts without a reason are not ruled')
  r = await runTask('T1', { criticAdds: { ...addAt('frames'), for_where: '', for_quantity: '' }, recheckRejects: true, recheckEvidence: 'not read' })
  check(r.result.followUps.filter(f => f.reason === 'critic addition not ruled on').length === 13, 'P1: a re-check rejection without evidence is not ruled')
  // A P1 with no value, or no function inventory, leaves a P1 item.
  r = await runTask('T1', { noValues: true })
  check(r.result.followUps.filter(f => f.role === 'P1' && !f.notice && /no value/.test(f.reason)).length === 13, 'P1: a return with no value is listed')
  r = await runTask('T1', { noFunctions: true })
  check(r.result.followUps.filter(f => f.role === 'P1' && !f.notice && /no function inventory/.test(f.reason)).length === 13, 'P1: an empty function inventory is listed')
  // A decision-only question feeds only a decision its category reports.
  r = await runTask('T1', { decisionQ9: true })
  const q9 = r.result.summary.questions
  check(q9.filter(q => q.category === 'R3').every(q => q.blocks === 'decision-only') && q9.filter(q => q.category !== 'R3').every(q => q.blocks === 'p2')
    && r.result.followUps.some(f => f.category === 'R8' && /names no decision R8 reports/.test(f.reason)), 'P1: a Q9-only question outside R3 blocks P2')
  // A P1 follow-up lists again each item its run did not deal with.
  const overcurrent = 'What is the motor overcurrent threshold, in A?'
  const fuItems = [{ role: 'P1-critic', category: 'R8', index: 1, question: overcurrent, reason: 'P1 question not ruled on' },
    { role: 'P1', category: 'R8', value: { where: 'IOBoard.md:88', quantity: 'shunt' }, reason: 'assumption without a confirmed question' },
    { role: 'P1', category: 'R8', reason: 'the P1 chain failed' }]
  fuItems.push({ role: 'P1', category: 'R8', item: fuItems[0], reason: 'follow-up item this run did not deal with' })
  r = await runTask('FU', { followup: { phases: 'P1', round: 1, categories: ['R8'], items: fuItems } })
  const again = r.result.followUps.filter(f => f.reason === 'follow-up item this run did not deal with')
  check(again.length === 3 && again.every(f => f.category === 'R8' && f.item.reason !== 'the P1 chain failed'), `P1 follow-up: ${again.length} items listed again, expected the question, the value and the question listed again`)
  check(r.prompts.find(x => x.label === 'P1-critic-R8').prompt.includes(overcurrent), 'P1 follow-up: the critic is given the items')
  r = await runTask('FU', { followup: { phases: 'P1', round: 1, categories: ['R8'], items: [fuItems[0], fuItems[3]] }, fuQuestion: overcurrent })
  check(!r.result.followUps.some(f => f.reason === 'follow-up item this run did not deal with'), 'P1 follow-up: a question raised again is dealt with')
  // A question already under "Raised by P1" is not raised again: an item
  // that carries it is dealt with, and it asks for its assumption.
  const published = { id: 'V1', category: 'R8', question: overcurrent, for_where: '', for_quantity: '', asks: '' }
  const relabel = { role: 'P1', category: 'R8', question: overcurrent, reason: 'decision-only question names no decision R8 reports; published as blocking P2', notice: true }
  r = await runTask('FU', { followup: { phases: 'P1', round: 1, categories: ['R8'], items: [fuItems[0], fuItems[3], relabel] }, raised: [published] })
  check(!r.result.followUps.some(f => f.reason === 'follow-up item this run did not deal with'), 'P1 follow-up: a question under Raised by P1 is dealt with')
  r = await runTask('FU', { followup: { phases: 'P1', round: 1, categories: ['R8'], items: [] }, assumption: true, raised: [{ ...published, question: 'State the ripple', for_where: 'IOBoard.md:1', for_quantity: 'ripple' }] })
  check(!r.result.summary.questions.some(q => q.synthetic) && !r.result.followUps.some(f => f.reason === 'assumption without a confirmed question'), 'P1 follow-up: a question under Raised by P1 asks for its assumption')
  // A question that raises an item copies its words.
  r = await runTask('FU', { followup: { phases: 'P1', round: 1, categories: ['R8'], items: [fuItems[0]] } })
  check(['P1-R8', 'P1-critic-R8'].every(l => /copies its question word for word|copying its question word for word/.test(r.prompts.find(x => x.label === l).prompt)), 'P1 follow-up: P1 and the critic copy an item\'s question')
  // Each "P1 asks" item ends with a confirmed question that names it.
  const extCurrent = 'external path highest current'
  r = await runTask('T1', { p1Asks: { R3: [extCurrent] } })
  check(r.result.followUps.some(f => f.role === 'P1' && f.category === 'R3' && f.asks === extCurrent && !f.notice), 'P1 asks: an item no question names is listed')
  check(['P1-R3', 'P1-critic-R3'].every(l => r.prompts.find(x => x.label === l).prompt.includes(extCurrent)), 'P1 asks: P1 and the critic are given the items')
  r = await runTask('T1', { p1Asks: { R3: [extCurrent] }, asksQ: extCurrent })
  check(!r.result.followUps.some(f => f.asks) && r.result.summary.questions.some(q => q.category === 'R3' && q.asks === extCurrent), 'P1 asks: a confirmed question that names the item asks it')
  r = await runTask('T1', { p1Asks: { R3: [extCurrent] }, asksQ: extCurrent, rejectQ0: true })
  check(r.result.followUps.some(f => f.category === 'R3' && f.asks === extCurrent && !f.notice), 'P1 asks: a question the critic rejects is listed')
  r = await runTask('T1', { p1Asks: { R3: [extCurrent] }, criticAdds: { ...addAt('external current'), for_where: '', for_quantity: '', asks: extCurrent } })
  check(!r.result.followUps.some(f => f.asks), 'P1 asks: a re-checked critic addition that names the item asks it')
  r = await runTask('FU', { followup: { phases: 'P1', round: 1, categories: ['R3'], items: [] }, p1Asks: { R3: [extCurrent] }, raised: [{ ...published, category: 'R3', asks: extCurrent }] })
  check(!r.result.followUps.some(f => f.asks) && !r.prompts.find(x => x.label === 'P1-R3').prompt.includes(extCurrent), 'P1 asks: an item under Raised by P1 is not asked again')

  // P0's own stop and held fields do not decide the outcome.
  r = await runTask('T2', { p0: { stop: true, stop_reasons: ['mock'], held: [{ category: 'R1', host: 'h', reason: 'mock' }] } })
  check(!r.result.summary.stopped && r.calls.includes('P2-R1'), 'P0: its own stop and held are not applied')

  // Reading times: a check, figure or ruling read before the task's date,
  // after the day that follows it, or on a day the calendar lacks, is not
  // this task's reading.
  r = await runTask('T2', { stockPatch: { read_at: '2026-09-14T09:56:01Z' } })
  check(r1(r).selection[0].part === null, 'stock checks read before the task: not verified')
  r = await runTask('T2', { stockPatch: { read_at: '2026-09-31T10:00:00Z' } })
  check(r1(r).selection[0].part === null, 'stock checks read on 31 September: not verified')
  for (const t of ['2026-09-29T00:00:00Z', '2027-09-28T10:00:00Z', '2099-01-01']) {
    r = await runTask('T2', { stockPatch: { read_at: t } })
    check(r1(r).selection[0].part === null, `stock checks read at ${t}, after the task: not verified`)
  }
  r = await runTask('T2', { stockPatch: { read_at: '2026-09-28T23:59:59Z' } })
  check(r1(r).selection[0].part === 'part1', 'stock checks read the day after prepare: verified')
  r = await runTask('T2', { figureReadAt: '2026-09-20T10:00:00Z' })
  check(r.result.summary.figures_open.R1.length === 1, 'figure read before the task: open')
  r = await runTask('T2', { figureReadAt: '2027-09-28T10:00:00Z' })
  check(r.result.summary.figures_open.R1.length === 1, 'figure read after the task: open')
  r = await runTask('T2', { refute: ['P4-stock-R1:part1'], stands: false, rulingReadAt: '2026-09-20T10:00:00Z' })
  check(r1(r).selection[0].part === null && r.result.followUps.some(f => f.reason === 'the ruling gives no evidence, source and time read'), 'ruling read before the task: no ruling')
  r = await runTask('T2', { refute: ['P4-stock-R1:part1'], stands: false, rulingReadAt: '2099-01-01' })
  check(r1(r).selection[0].part === null && r.result.followUps.some(f => f.reason === 'the ruling gives no evidence, source and time read'), 'ruling read after the task: no ruling')
  // "none" is a reading of a notice or commitment that does not exist, and
  // no reading of a status or identity; "not found" is none.
  r = await runTask('T2', { edit: { 'end-of-life notices': { read: 'none' }, 'longevity commitment': { read: 'none' } } })
  check(r1(r).selection[0].part === 'part1', 'checks read as none: verified')
  for (const name of ['lifecycle status', 'LCSC identity']) {
    r = await runTask('T2', { edit: { [name]: { read: 'none' } } })
    check(r1(r).selection[0].part === null, `${name} read as none: not verified`)
  }
  r = await runTask('T2', { requiredReports: { R1: ['BOOT resistor'] }, reports: [['BOOT resistor', 'none']] })
  check(!r.result.summary.figures_open.R1.includes('P2 report: BOOT resistor'), 'report read as none: returned')
  r = await runTask('T2', { forResearch: [{ id: 'V9', category: 'R1' }], foundValue: 'not found: the datasheet does not state it' })
  check(r.result.summary.figures_open.R1.includes('found V9'), 'value found as not found: open')
  r = await runTask('T5', { p5Budgets: ['GPIO', 'absent IOVDD'], p5Conditional: ['absent IOVDD'], p5Items: ['GPIO', 'absent IOVDD'], budgetValues: { GPIO: 'N/A: the trip filter is unanswered', 'absent IOVDD': 'N/A: ratings as cited' } })
  check(r.result.summary.budgets_missing.join() === 'GPIO', `T5: budgets missing ${r.result.summary.budgets_missing}, expected GPIO only (N/A, unconditional)`)
  r = await runTask('T5', { p5Budgets: ['GPIO'], p5Items: ['GPIO'], budgetValues: { GPIO: 'unknown: sample period not stated' } })
  check(r.result.summary.budgets_missing.includes('GPIO'), 'T5: a budget value written as unknown is missing')
  // A null-marker page whose status P0 answers with no or absent is not read.
  for (const [markerText, listed] of [['no', true], ['absent: the page body carries no status', true], ['not in the page body', true], ['No longer manufactured', false]]) {
    r = await runTask('T2', { hosts: [{ host: 'www.onsemi.com', probe: null, hold: ['R2'] }], markerText })
    check(r.result.followUps.some(f => f.host === 'www.onsemi.com' && f.reason === 'lifecycle status not read from the page') === listed, `status "${markerText}": ${listed ? '' : 'not '}listed as not read`)
  }
  // Rule 6: an unread held quantity waives nothing; a failing one does not
  // refute a part that passes on live stock.
  r = await runTask('T2', { held: 50, heldChecks: true, heldAndLive: true, addStock: ['presale'], edit: { 'held quantity': { read: 'not read', source: '' }, presale: { passes: false } } })
  check(r1(r).selection[0].part !== 'part1' && r.calls.some(c => c.startsWith('adjudicator-R1')), 'unread held quantity with failing live stock: refuted')
  r = await runTask('T2', { held: 20, addStock: ['held quantity'], edit: { 'held quantity': { passes: false } } })
  check(r1(r).selection[0].part === 'part1' && !r.calls.some(c => c.startsWith('adjudicator-R1')), 'failing held quantity, passing live stock: verified')
  // The second vendor's stock bears on the second-vendor route only, and not
  // on an alternate (rule 5 asks rules 1 to 4).
  r = await runTask('T2', { altName: 'part2', verify: ['part2'], addStock: ['second-vendor stock'], edit: { 'second-vendor stock': { passes: false } } })
  check(r1(r).selection[0].part === 'part1' && r1(r).selection[0].alternate_unverified.length === 0 && !r.calls.some(c => c.startsWith('adjudicator-R1')), 'failing second-vendor stock off its route: refutes nothing')
  r = await runTask('T2', { altName: 'part2', verify: ['part2'], noSecondVendor: true })
  check(r1(r).selection[0].part === 'part1' && r1(r).selection[0].alternate_unverified.length === 0, 'alternate without second-vendor stock: verified')
  // A value that differs from P2's refutes only when it fails: a reading that
  // moves, and a requirement P2 gave no value for.
  r = await runTask('T2', { edit: { stock: { stated: '41200', read: '40870', agrees: false }, 'lead time': { agrees: false } } })
  check(r1(r).selection[0].part === 'part1' && !r.calls.some(c => c.startsWith('adjudicator-R1')), 'stock moved since P2, still passing: no refutation')
  r = await runTask('T2', { fnReq: 'vmax', edit: { vmax: { agrees: false } } })
  check(r1(r).selection[0].part === 'part1' && !r.calls.some(c => c.startsWith('adjudicator-R1')), 'requirement P2 gave no value for, passing: no refutation')
  // A confirmation that states a refutation is adjudicated; a refuted
  // verdict names its failing checks to the adjudicator.
  r = await runTask('T2', { note: ['P4-datasheet-R1', 'maker page lists the part as NRND since 2026-06'] })
  check(r.calls.some(c => c.startsWith('adjudicator-R1')) && r1(r).selection[0].part === 'part2', 'confirmation stating a refutation: adjudicated')
  for (const note of ['none', 'No refutation found', 'No refutations found.', 'Not refuted.']) {
    r = await runTask('T2', { note: ['P4-datasheet-R1', note] })
    check(!r.calls.some(c => c.startsWith('adjudicator-R1')) && r1(r).selection[0].part === 'part1', `confirmation with refutation "${note}": confirmed`)
  }
  r = await runTask('T2', { refute: ['P4-stock-R1:part1'], failPass: ['P4-stock-R1'], stands: false })
  const adj = r.prompts.find(x => x.label === 'adjudicator-R1')
  check(adj && adj.prompt.includes('"refutation":"mock; checks disagree or fail: stock"'), 'refuted verdict with a failing check: the check is named to the adjudicator')
  // Figure checks name the figure and the committed return.
  r = await runTask('T6', { blankFigure: true })
  check(r.result.summary.stopped === true, 'T6: figure checks without a figure or return file stop it')
  // The return file is a path ending in .json, as session.py record reads it.
  r = await runTask('T6', { returnFile: 'hardware/research/round1/T2' })
  check(r.result.summary.stopped === true, 'T6: figure checks naming a run directory, not a file, stop it')
  for (const rf of ['hardware/research/round1/T2/012-P4-stock-R1.json:34', '/srv/results/hardware/research/round1/T2/012-P4-stock-R1.json, parts[0]']) {
    r = await runTask('T6', { returnFile: rf })
    check(!r.result.summary.stopped, `T6: return file "${rf}" names a file`)
  }
  // A combination states its outputs, bind order and resources; an
  // assumption its value.
  r = await runTask('T5', { blankCombos: true })
  check(r.result.summary.missing_checks.includes('P5 combinations'), 'T5: a combination without outputs, bind order and resources is none')
  r = await runTask('T5', { assumptionValue: 'not read' })
  check(r.result.summary.unsourced_items === 2, `T5: ${r.result.summary.unsourced_items} items without a value, source and time, expected the 2 assumptions`)

  // A refutation no adjudicator ruled on is not cleared by a later pair's
  // verification; a standing one still moves on.
  r = await runTask('T2', { verifyFirst: ['part2'], p4parts: ['part2'], refute: ['P4-stock-R1:part1', 'P4-stock-R1:part2'], nulls: { 'adjudicator-R1-2': 2 } })
  check(r1(r).ledger.some(l => l.part === 'part2' && l.status === 'refuted, no ruling') && r1(r).ledger.some(l => l.part === 'part2' && l.status === 'verified') && r1(r).selection[0].part === null, 'refutation without a ruling, then verified by a pair: not kept')
  r = await runTask('T2', { verifyFirst: ['part2'], p4parts: ['part2'], refute: ['P4-stock-R1:part1', 'P4-stock-R1:part2', 'P4-stock-R1-2:part2'], nulls: { 'adjudicator-R1-2': 2 } })
  check(r1(r).selection[0].part === 'part3' && r1(r).selection[0].refuted.includes('part2'), 'refutation without a ruling, then one that stands: next part kept')
  // A refutation of the alternate as a primary with no ruling keeps it from
  // sourcing the kept part.
  r = await runTask('T2', { alts: { part1: 'part3' }, verifyFirst: ['part3'], p4parts: ['part3'], refute: ['P4-stock-R1:part3'], nulls: { 'adjudicator-R1': 2 } })
  check(r1(r).selection[0].part === 'part1' && r1(r).selection[0].alternate_unverified.includes('part3'), 'alternate refuted as a primary without a ruling: not verified as the alternate')
  // An alternate counts for the part it was checked against only.
  r = await runTask('T4', { sharedAlt: true, p4parts: ['part3', 'altS'], refute: ['P4-stock-R10:part1'] })
  const qsr = r.result.summary.results.find(c => c.category === 'R10').selection[0]
  check(qsr.part === 'part2' && /shared with part1/.test(qsr.q_alternatives[0].alternate_status), 'alternate shared with a refuted first-ranked part: not verified for the Q alternative')
  r = await runTask('T2', { cap: 23, alts: { part1: 'altP', part2: 'altP' }, verifyFirst: ['part2'], p4parts: ['part2', 'altP'], refute: ['P4-stock-R1:part1'] })
  check(r1(r).selection[0].part === 'part2' && r1(r).selection[0].alternate_unverified.includes('altP'), 'shared alternate checked against a refuted part, no pair left: not verified for the kept part')
  r = await runTask('T2', { alts: { part1: 'altP', part2: 'altP' }, p4parts: ['altP'], refute: ['P4-stock-R1:part1', 'P4-datasheet-R1:altP'], editPart: { 'P4-datasheet-R1:altP': { 'pin-for-pin match': { agrees: false, passes: false } } } })
  check(r1(r).selection[0].part === 'part2' && r1(r).selection[0].alternate_unverified.length === 0, 'alternate refuted on its fit for a refuted part, verified for the replacement: verified')
  r = await runTask('T2', { alts: { part1: 'altP', part2: 'altP' }, p4parts: ['altP'], refute: ['P4-stock-R1:part1'], editPart: { 'P4-stock-R1:altP': { 'end-of-life notices': { read: 'last-time-buy notice 2026-08-01', agrees: false, passes: false } } } })
  check(r1(r).selection[0].part === 'part2' && r1(r).selection[0].alternate_unverified.includes('altP'), 'alternate refuted on end-of-life notices for a refuted part, verified for the replacement: not verified')
  // A part verified in its own right keeps that role when it is also an
  // alternate: the first-ranked part listed as one, a Q alternative named
  // by another, and the kept part's alternate listed as a Q alternative.
  for (const [what, o, ok] of [
    ['first-ranked part also listed as an alternate', { alts: { part3: 'part1' }, verifyList: [{ part: 'part3', kind: 'q-alternative' }, { part: 'part1', kind: 'alternate' }], p4parts: ['part3'] }, s => s.part === 'part1'],
    ['Q alternative that is another\'s alternate', { alts: { part3: 'part2' }, verifyList: [{ part: 'part2', kind: 'q-alternative' }, { part: 'part3', kind: 'q-alternative' }], p4parts: ['part2', 'part3'] }, s => s.q_alternatives[0].status === 'verified' && s.q_alternatives[1].alternate_status === 'verified'],
    ['kept part\'s alternate listed as a Q alternative', { alts: { part1: 'part3' }, verifyList: [{ part: 'part3', kind: 'q-alternative' }], p4parts: ['part3'] }, s => s.part === 'part1' && s.alternate_unverified.length === 0 && s.q_alternatives[0].status === 'verified'],
  ]) {
    r = await runTask('T4', o)
    check(ok(r10(r).selection[0]), `${what}: verified in both roles`)
  }
  // A part in both roles whose refutation did not stand holds the alternate
  // role on its compatibility checks.
  r = await runTask('T4', { qAltBack: true, bothRoles: true, p4parts: ['part3'], refute: ['P4-datasheet-R10:part1'], stands: false })
  check(r10(r).selection[0].part === 'part1' && r10(r).selection[0].q_alternatives[0].alternate_status === 'verified', 'part in both roles, refutation did not stand: alternate role verified')
  r = await runTask('T4', { qAltBack: true, bothRoles: true, p4parts: ['part3'], refute: ['P4-datasheet-R10:part1'], edit: { 'pin-for-pin match': { passes: false } }, stands: false })
  check(r10(r).selection[0].part === 'part1' && r10(r).selection[0].q_alternatives[0].alternate_status === 'not verified', 'part in both roles, refutation did not stand, compatibility failing: alternate role not verified')
  // A part in both roles refuted on its fit alone is refuted as the
  // alternate only, and stays the kept part.
  r = await runTask('T4', { qAltBack: true, bothRoles: true, p4parts: ['part3'], refute: ['P4-datasheet-R10:part1'], edit: { 'pin-for-pin match': { agrees: false, passes: false } } })
  check(r10(r).selection[0].part === 'part1' && r10(r).selection[0].refuted.length === 0 && r10(r).selection[0].q_alternatives[0].alternate_status === 'refuted' && r10(r).ledger.filter(l => l.part === 'part1' && l.as === 'alternate').length === 1, 'part in both roles refuted on its fit, standing: kept in its own right, alternate role refuted')

  // A standing refutation moves its function on once the pass's rulings are
  // in: one pair, for the part the function then stands on, and none after
  // a refuted part ranked below the part kept.
  const r12 = x => x.result.summary.results.find(c => c.category === 'R12')
  r = await runTask('T4', { verifyList: [{ part: 'part2', kind: 'q-alternative', option: 'EEPROM' }], p4parts: ['part2'], refute: ['P4-stock-R12:part2'] })
  check(r12(r).selection[0].part === 'part1' && !r.calls.includes('P4-stock-R12-2') && r.result.extra_used === 1 && r.result.followUps.some(f => f.part === 'part2' && /^Q alternative refuted/.test(f.reason)), 'Q alternative refuted, first-ranked part verified: no pair, listed')
  r = await runTask('T4', { verifyList: [{ part: 'part2', kind: 'q-alternative', option: 'EEPROM' }], p4parts: ['part2'], refute: ['P4-stock-R12:part1', 'P4-stock-R12:part2'] })
  check(r12(r).selection[0].part === 'part3' && r.calls.filter(c => c.startsWith('P4-stock-R12')).length === 2 && r.result.extra_used === 4, `first-ranked part and Q alternative refuted in one pass: one pair, on part3 (extra ${r.result.extra_used}, expected 4)`)
  // A kept Q alternative, or a kept part ranked first after the drops,
  // counts in the class the datasheet verifier confirmed for it.
  const q4 = { Q4: ['reference', 'external ADC'] }
  r = await runTask('T4', { verifyList: [{ part: 'part2', kind: 'q-alternative', option: 'external ADC' }, { part: 'part3', kind: 'q-alternative', option: 'reference' }], p4parts: ['part2', 'part3'], qOptions: q4, keptOption: 'reference', refute: ['P4-stock-R10:part1'] })
  check(r10(r).selection[0].part === 'part2' && r10(r).selection[0].q_options_missing.length === 0 && !r.result.summary.q_missing.includes('R10') && !r.calls.includes('P4-stock-R10-2'), 'Q alternative verified in the pass and kept after a refutation: no pair, its class counts')
  r = await runTask('T4', { failedReq: true, qAlt: true, qOption: 'external ADC', keptOption: 'reference', qOptions: q4, p4parts: ['part2', 'part3', 'altQ'] })
  check(r10(r).selection[0].part === 'part2' && r10(r).selection[0].q_options_missing.length === 0, 'first-ranked part dropped for a failed requirement: the next part\'s confirmed class counts')
  // A kept part classed twice, as the first-ranked part and as a Q
  // alternative, counts for one class when the two agree and for neither
  // when they differ.
  const extAdc = part => ({ part, kind: 'q-alternative', option: 'external ADC' })
  r = await runTask('T4', { failedReq: true, verifyList: [extAdc('part2'), extAdc('part3')], p4parts: ['part2', 'part3'], qOptions: q4, keptOption: 'reference' })
  check(r10(r).selection[0].part === 'part2' && r10(r).selection[0].q_options_missing.join() === 'reference' && r.result.summary.q_missing.includes('R10'), 'part ranked first after a drop, classed reference and as an external ADC alternative: counts for neither')
  r = await runTask('T4', { verifyList: [extAdc('part1'), extAdc('part3')], p4parts: ['part3'], qOptions: q4, keptOption: 'reference' })
  check(r10(r).selection[0].part === 'part1' && r10(r).selection[0].q_options_missing.join() === 'reference', 'first-ranked part classed reference and as an external ADC alternative: counts for neither')
  r = await runTask('T4', { verifyList: [{ part: 'part1', kind: 'q-alternative', option: 'reference' }, extAdc('part3')], p4parts: ['part3'], qOptions: q4, keptOption: 'reference' })
  check(r10(r).selection[0].part === 'part1' && r10(r).selection[0].q_options_missing.length === 0, 'first-ranked part classed reference in both roles: counts once')
  // Figure refutations: the datasheet verifier's only, a replacement pair's
  // included, each ruled on against the claim it refutes.
  r = await runTask('T2', { refute: ['P4-stock-R1:part1'], replacementFigure: 'P2 report: x0' })
  check(r1(r).selection[0].part === 'part2' && r.calls.includes('adjudicator-R1-2') && r.result.summary.figures_open.R1.includes('P2 report: x0'), 'figure refuted by the replacement pair: adjudicated, open')
  r = await runTask('T2', { stockFigures: true })
  check(!r.calls.some(c => c.startsWith('adjudicator-R1')) && r.result.summary.figures_open.R1.length === 0 && r.result.extra_used === 0, 'figures refuted by the stock verifier: not adjudicated, not open')
  check(r.prompts.find(x => x.label === 'P4-stock-R1').prompt.includes('Return figures empty'), 'stock verifier is told to return no figure')
  r = await runTask('T2', { foundValue: '31 frames', refuteFigure: ['P4-datasheet-R1'] })
  const figAdj = r.prompts.find(x => x.label === 'adjudicator-R1')
  check(figAdj && figAdj.prompt.includes('"claim":{"question_id":"V9","value":"31 frames"'), 'refuted figure: the adjudicator is given the value it rules on')
  // Each verifier lists a part once for each function it verifies it for.
  r = await runTask('T2')
  check(['P4-stock-R1', 'P4-datasheet-R1'].every(l => r.prompts.find(x => x.label === l).prompt.includes('once for each function you verify it for')), 'P4 is told to list a part once for each function')
  // The re-rank reports the per-part figures of every part it ranks or
  // records, a rule-5 alternate's record among them.
  r = await runTask('T2', { perPartReports: { R2: ['clock tolerance'] } })
  check(r.prompts.find(x => x.label === 'rerank-R2').prompt.includes('for each part you rank, name in verify or give a record in new_candidates'), 're-rank is told to report per-part figures for its records')
  // R1's crystal tolerance is owed for the crystal kept, a replacement too.
  r = await runTask('T2', { requiredReports: cats.reports, perPartReports: cats.reports_per_part, reportParts: ['crystal tolerance: part1'], refute: ['P4-stock-R1:part1'] })
  check(r1(r).selection[0].part === 'part2' && r.result.summary.figures_open.R1.includes('report: crystal tolerance: part2'), 'crystal tolerance of a replacement part: owed')
  // R1 reports the pin ratings, the pad figures and the in-package flash.
  r = await runTask('T2', { requiredReports: cats.reports, reports: ['stepping-A4 errata', 'BOOT resistor', 'OpenOCD', 'watchdog'].map(f => [f, 'v']) })
  check(['pin ratings', 'pad figures', 'in-package flash'].every(f => r.result.summary.figures_open.R1.includes(`P2 report: ${f}`)), 'R1 without its pin ratings, pad figures and in-package flash: open')
  // A value found for research is a requirement of the function its row
  // names, and the datasheet verifier checks the list holds it.
  r = await runTask('T2')
  check(r.prompts.find(x => x.label === 'P2-R1').prompt.includes('apply it as a requirement of the function its row names') && r.prompts.find(x => x.label === 'P4-datasheet-R1').prompt.includes('each value found for research whose row under "Raised by P1" names the function'), 'found values: applied to their function and checked in its requirement list')

  // Every row of a P5 budget item counts, not the first that passes.
  r = await runTask('T5', { p5Budgets: ['current per rail', 'GPIO'], p5Items: ['current per rail: 3.3 V', 'current per rail: 12 V', 'GPIO: bank 0', 'GPIO: bank 1'], budgetValues: { 'current per rail: 12 V': 'not read: datasheet returned 403', 'GPIO: bank 1': 'not applicable: none' } })
  check(r.result.summary.budgets_missing.join() === 'current per rail,GPIO', `T5: budgets missing ${r.result.summary.budgets_missing}, expected an unread rail and an unconditional not applicable beside a row that counts`)
  // The Q4 and Q8 alternatives are budgeted for each option class.
  r = await runTask('T5', { p5Budgets: ['Q4 alternatives'], qOptions: q4, p5Items: ['Q4 alternatives: reference'] })
  check(r.result.summary.budgets_missing.join() === 'Q4 alternatives: external ADC' && r.prompts.find(x => x.label === 'P5').prompt.includes('Q4 alternatives: reference and Q4 alternatives: external ADC'), 'T5: a Q4 option class without a budget is missing')
  r = await runTask('T5', { p5Budgets: ['Q4 alternatives'], qOptions: q4, p5Items: ['Q4 alternatives: reference', 'Q4 alternatives: external ADC'] })
  check(r.result.summary.budgets_missing.length === 0, 'T5: each Q4 option class budgeted')
  // A rail or bus the critic finds unbudgeted is missing.
  r = await runTask('T5', { p5Budgets: ['current per rail'], p5Items: ['current per rail: 3.3 V'], criticMissing: ['current per rail: 5 V', 'none'] })
  check(r.result.summary.budgets_missing.join() === 'current per rail: 5 V' && r.prompts.find(x => x.label === 'P5-critic').prompt.includes('List in budgets_missing'), 'T5: an instance the critic lists as unbudgeted is missing')
  // The pack current, cell and protection recheck of the R7 row is owed.
  r = await runTask('T5', { p5Budgets: cats.p5_budgets, p5Items: cats.p5_budgets.filter(n => !['pack current', 'cell rating', 'pack protection'].includes(n)) })
  check(r.result.summary.budgets_missing.join() === 'pack current,cell rating,pack protection' && r.prompts.find(x => x.label === 'P5').prompt.includes('the pack current recomputed from the converter efficiencies R5 and R6 verified'), 'T5: the R7 row\'s pack recheck is owed')
  // A budget upheld over its limit is counted and listed.
  r = await runTask('T5', { p5Budgets: ['PIO state machines'], p5Items: ['PIO state machines'], budgetValues: { 'PIO state machines': '13 of 12 used' }, overBudget: ['PIO state machines'] })
  check(r.result.summary.budgets_over === 1 && r.result.followUps.some(f => f.reason === 'budget over its limit'), 'T5: a budget over its limit is counted')
  r = await runTask('T5')
  check(r.result.summary.budgets_over === 0, 'T5: no budget over its limit')
  // The assumptions the P5 row requires are each stated and upheld.
  r = await runTask('T5', { p5Assumptions: cats.p5_assumptions })
  check(r.result.summary.assumptions_missing.join() === cats.p5_assumptions.join() && r.prompts.find(x => x.label === 'P5').prompt.includes('State in assumptions'), 'T5: required assumptions not stated are missing')
  r = await runTask('T5', { p5Assumptions: cats.p5_assumptions, p5Assumed: [...cats.p5_assumptions.map(n => [n, '2']), ['programmer state machines: SWD', 'not read']] })
  check(r.result.summary.assumptions_missing.join() === 'programmer state machines', `T5: assumptions missing ${r.result.summary.assumptions_missing}, expected the one with an unread row`)
  // A critic verdict without a reason rules on nothing.
  r = await runTask('T5', { blankVerdictReason: true })
  const s5 = r.result.summary
  const own = [...s5.combinations, ...s5.budgets, ...s5.assumptions, ...s5.conflicts.filter(c => c.source === 'P5'), ...s5.gaps.filter(g => g.source === 'P6')]
  check(s5.rejected_items === 0 && own.length > 6 && own.every(x => x.unchecked) && s5.unchecked_items === own.length, 'T5: verdicts without a reason leave every item unchecked')
  check(['P5-critic', 'P6-critic'].every(l => r.prompts.find(x => x.label === l).prompt.includes('a verdict without one is no verdict')), 'T5: the critics are told a verdict needs its reason')
  // P6 and its critic apply the Scope section's firmware lines and the
  // decision-only questions.
  check(['P6', 'P6-critic'].every(l => r.prompts.find(x => x.label === l).prompt.includes('"hardware selected, link open" and is not a gap') && r.prompts.find(x => x.label === l).prompt.includes('"(feeds Q9 only)" feeds that decision alone and needs no answer')), 'T5: P6 and its critic are given the link-open and decision-only rules')

  // T6: what the pages state as open, each confirmed by the critic.
  const leftOpen = [{ check: 'FU-d', conflict: { parts: ['LMR36015'], categories: ['R5'], kind: 'rail', description: '5 V rail over its rating', evidence: 'e' } }]
  const openT6 = { leftOpen, acceptOpen: { reason: 'owner', functions: ['R3: f1 has no verified part'] }, p5AssumedT6: [{ item: 'servo current sample period', value: '1 ms' }] }
  r = await runTask('T6', openT6)
  check(r.result.summary.stopped === true && /item 2 not stated on the pages as assumed/.test(r.result.summary.reasons[0]) && ['P7', 'P7-critic'].every(l => r.prompts.find(x => x.label === l).prompt.includes('R3: f1 has no verified part') && r.prompts.find(x => x.label === l).prompt.includes('5 V rail over its rating') && r.prompts.find(x => x.label === l).prompt.includes('servo current sample period')), 'T6: open items not stated on the pages stop it')
  r = await runTask('T6', { ...openT6, marked: [0, 1, 2].map(index => ({ index, file: 'hardware/docs/Power.md', line: 3 })) })
  check(!r.result.summary.stopped, 'T6: open items stated on the pages finish it')
  r = await runTask('T6', { ...openT6, marked: [0, 1, 2].map(index => ({ index, file: 'notes.md', line: 3 })) })
  check(r.result.summary.stopped === true, 'T6: open items stated outside the outputs stop it')
  // A budget or combination comes from the last P5/P6 check.
  r = await runTask('T6', { p56Runs: ['T5', 'FU-b'], lastP56: 'FU-b', returnFile: 'hardware/research/round1/T5/003-P5.json' })
  check(r.result.summary.stopped === true && r.prompts.find(x => x.label === 'P7-critic').prompt.includes('against the last P5/P6 check, FU-b, only'), 'T6: a figure checked against a superseded P5/P6 check stops it')
  r = await runTask('T6', { p56Runs: ['T5', 'FU-b'], lastP56: 'FU-b', returnFile: 'hardware/research/round1/FU-b/003-P5.json', decisions: { Q4: 'external ADC (owner, 2026-10-01)' } })
  check(!r.result.summary.stopped && r.prompts.find(x => x.label === 'P7-critic').prompt.includes('"Q4":"external ADC (owner, 2026-10-01)"'), 'T6: figures from the last P5/P6 check finish it; the critic is given the decisions')
  // Both T6 prompts state the rules the gate reads: the group pages, a
  // figure on each page by its repository-relative path, the three commands.
  r = await runTask('T6')
  const p7p = r.prompts.find(x => x.label === 'P7').prompt
  const p7c = r.prompts.find(x => x.label === 'P7-critic').prompt
  check(p7p.includes('three new files hardware/docs/NAME.md') && p7c.includes('Check at least one figure on each of these pages: hardware/docs/IOBoard.md, hardware/docs/Parts.md, hardware/docs/Power.md, hardware/docs/Research.md, hardware/docs/GroupA.md') && p7c.includes('its file and return_file repository-relative'), 'T6: the path rules and the pages to check are in the prompts')
  check(p7c.includes('DIGIKEY_ENV_FILE=') && p7c.includes('python3 tools/jlc_stock.py --check 5'), 'T6: the critic is given the stock check with its credentials')
  // Every part selection.json keeps has its Parts.md row and group page, and
  // tools/jlc_stock.py does what each sentence of its Outputs row states.
  const selT6 = { selection: { R1: { f1: { part: 'part1', alternate: 'altA', q_alternatives: [] } } }, jlcRow: ['It reads the result whose LCSC number equals the row\'s.'] }
  const rows = ['part1', 'altA'].map(part => ({ function: 'f1', part, parts_line: 4, group_page: 'hardware/docs/GroupA.md' }))
  r = await runTask('T6', { ...selT6, partRows: rows.slice(0, 1), jlcReview: [{ index: 0, holds: true, line: 12, reason: 'r' }] })
  check(r.result.summary.stopped === true && /no Parts.md row or group page for altA/.test(r.result.summary.reasons[0]), 'T6: a selected part without its Parts.md row stops it')
  r = await runTask('T6', { ...selT6, partRows: rows, jlcReview: [{ index: 0, holds: false, line: 0, reason: 'reads the first result' }] })
  check(r.result.summary.stopped === true && /tools\/jlc_stock.py not confirmed/.test(r.result.summary.reasons[0]), 'T6: a stock tool that does not do its Outputs row stops it')
  r = await runTask('T6', { ...selT6, partRows: rows, jlcReview: [{ index: 0, holds: true, line: 12, reason: 'r' }] })
  check(!r.result.summary.stopped, 'T6: every part in Parts.md and the stock tool confirmed finish it')

  // Follow-up plans.
  for (const [phases, n, want] of [['P1', 2, 6], ['P2-P4', 3, 16], ['P5-P6', 0, 5]]) {
    const { result } = await runTask('FU', { followup: { phases, round: 1, categories: ['R1', 'R2', 'R3'].slice(0, n), items: [] } })
    check(result.planned === want, `FU ${phases}: planned ${result.planned}, expected ${want}`)
  }

  console.log(failures ? `${failures} checks failed` : 'dry run: every check passed')
  process.exit(failures ? 1 : 0)
}

main()
