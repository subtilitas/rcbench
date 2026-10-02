#!/usr/bin/env node
// Runs tools/research/round1.js for every task with mock agents that return
// objects shaped by tools/research/schemas.json, and checks the rules the
// script enforces: the counts and the cap, both verifiers on every part, an
// adjudicator for every refutation and refuted figure, the next-ranked part
// after a refutation that stands, restarts from the free agents, questions
// that reach the owner only when checked, P0's checks, and round 2's prompts
// and T6 rules.
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
// {...}}} for one verifier's checks of one part; researchRound, runsDir,
// runsDirs, selectionFile, branch, groupPages, t6Outputs and t6MayWrite as
// prepare writes them, p7Pages the group pages the mock P7 returns
async function runTask(task, opts = {}) {
  const calls = []
  const prompts = []
  const nulls = new Map(Object.entries(opts.nulls || {}))
  const efforts = [], models = []
  async function agent(prompt, o) {
    const base = o.label.replace(/:restart$/, '')
    calls.push(o.label)
    efforts.push(o.effort)
    models.push([o.label, o.model])
    prompts.push({ label: o.label, prompt })
    if ((opts.throws || []).includes(base)) throw new Error('mock throw')
    const left = nulls.get(base)
    if (left) { nulls.set(base, left - 1); return null }
    const role = roleOf(o.schema)
    const data = fake(o.schema)
    // A category-scoped agent names the category its label carries.
    if ('category' in data && /R\d+/.test(o.label) && !opts.wrongCategory) data.category = opts.categoryText ? opts.categoryText(o.label.match(/R\d+/)[0]) : o.label.match(/R\d+/)[0]
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
      if (opts.failedReqAt !== undefined) data.functions[0].shortlist[opts.failedReqAt].requirements = [{ name: 'x0', required: 'x0', datasheet: '40 V', pass: false, source: 's' }]
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
      if (opts.p2DropY) data.functions[0].dropped = [{ part: opts.p2DropPart || 'partY', maker: 'm', reason: 'r' }]
      if (opts.p2DropYAll) data.functions.forEach(f => { f.dropped = [{ part: opts.p2DropPart || 'partY', maker: 'm', reason: 'r' }] })
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
      data.functions = [{ function: 'f1', decision: opts.qDecision || ({ R10: 'Q4', R12: 'Q8' }[rcat] || 'none'), kept_option: opts.keptOption || '', ranking: (opts.ranking || (opts.ranked || [1, 2, 3]).map(rank => ({ rank, part: `part${rank}`, reason: 'r', option: (opts.rankOptions || {})[`part${rank}`] || '' }))),
        new_candidates: opts.p3dropped ? [{ ...cand(7), part: 'partN' }] : (opts.newP3 || []).map((part, k) => ({ ...cand(7), part, lcsc: `C77${k}`, ...(opts.newP3For ? { p3_part: opts.newP3For } : {}) })), dropped_from_p3: opts.p3dropped ? [{ part: 'partN', maker: 'm', reason: 'r' }] : (opts.dropP3 || []).map(part => ({ part, maker: 'm', reason: 'r' })),
        dropped_from_shortlist: opts.rankDropped ? [{ part: 'part1', maker: 'm', reason: 'r' }] : (opts.dropShort || []).map(part => ({ part, maker: 'm', reason: 'r' })), verify: opts.qDup ? [{ part: 'part3', kind: 'q-alternative', option: 'reference' }, { part: 'part3', kind: 'q-alternative', option: 'external ADC' }] : opts.twoQShare ? [{ part: 'part2', kind: 'q-alternative' }, { part: 'part3', kind: 'q-alternative' }] : opts.qAlt || opts.sharedAlt ? [{ part: 'part3', kind: 'q-alternative', option: opts.qOption || '' }] : (opts.verify || []).map(part => ({ part, kind: 'alternate' })) }]
      if (opts.extraFn) data.functions.push({ ...data.functions[0], function: 'fX', ...(opts.rrHandleYin || opts.rrHandleXin ? { dropped_from_shortlist: [{ part: opts.rrHandleXin ? 'partX' : 'partY', maker: 'm', reason: 'r' }] } : {}) })
      if (opts.p3dropShort) data.functions[0].dropped_from_shortlist = [{ part: opts.dropShortPart || 'partX', maker: 'm', reason: 'fails vmax', ...(opts.dropShortLcsc ? { lcsc: opts.dropShortLcsc } : {}) }]
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
    if (role === 'P3' && opts.p3missed) data.missed = [{ function: opts.p3missedFn || 'f1', part: opts.p3missedPart || 'partX', maker: 'm', why: 'w' }]
    if (role === 'P3' && opts.p3overturned) data.exclusions_not_holding = [{ ...(opts.p3overFns ? { functions: opts.p3overFns } : {}), part: opts.p3overPart || 'partY', reason_given: 'r', why_it_fails: 'w' }]
    if (role === 'P7-critic' || role === 'P7') {
      for (const k of Object.keys(data.checks)) data.checks[k] = { passed: !(opts.failCheck === k && role === 'P7-critic'), output: opts.failCheck === k && opts.failOutput ? opts.failOutput : k === 'jlc_stock' && opts.passOutput ? opts.passOutput : 'o' }
      const pagesOut = opts.p7Pages || opts.groupPages
      const outs = [...(opts.t6Outputs || T6OUT), ...(pagesOut ? Object.values(pagesOut) : ['hardware/docs/GroupA.md', 'hardware/docs/GroupB.md', 'hardware/docs/GroupC.md'])].filter(f => !(role === 'P7' && f === opts.unwritten))
      if (opts.pageOutside) outs.push('tools/research/README.md')
      if (opts.fourthPage) outs.push('hardware/docs/GroupD.md')
      if (opts.p7Extra) outs.push(...opts.p7Extra)
      if (role === 'P7') data.figures = opts.p7Figures || outs.filter(f => f.startsWith('hardware/docs/')).map(file => ({ file, line: 1, figure: 'stock' }))
      if (role === 'P7') { data.files = outs; data.group_pages = opts.pageOutside ? { A: 'hardware/docs/GroupA.md', B: 'tools/research/README.md', C: 'hardware/docs/GroupC.md' } : opts.samePages ? { A: 'hardware/docs/Power.md', B: 'hardware/docs/Power.md', C: 'hardware/docs/Power.md' } : (opts.pagePower ? { A: 'hardware/docs/GroupA.md', B: 'hardware/docs/Power.md', C: 'hardware/docs/GroupC.md' } : pagesOut ? { ...pagesOut } : { A: 'hardware/docs/GroupA.md', B: 'hardware/docs/GroupB.md', C: 'hardware/docs/GroupC.md' }); if (opts.fourthPage) data.group_pages.D = 'hardware/docs/GroupD.md' }
      else {
        data.reviewed = outs
        const figFiles = opts.oneFigure ? ['hardware/docs/Parts.md'] : outs.filter(f => f.startsWith('hardware/docs/') && f !== opts.noFigureOn)
        data.figure_checks = opts.noFigures ? [] : figFiles.map(file => ({ ...(opts.p7FigureAlias ? { p7_figure: 'stock' } : {}), file, line: opts.checkLine === undefined ? 1 : opts.checkLine, figure: opts.blankFigure ? '' : opts.p7FigureAlias || 'stock', return_file: opts.blankFigure ? '' : opts.returnFile || 'hardware/research/round1/T2/012-P4-stock-R1.json', kind: opts.figureKind || 'other', agrees: !opts.criticDisagrees }))
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
          .map((figure, k) => ({ figure, stated: 's', read: opts.readNone && kind === 'datasheet' ? 'not read: API timed out' : 'r', source: opts.noSource && kind === 'stock' ? '' : 'src', read_at: opts.undated && kind === 'stock' ? '' : opts.readAt && kind === 'stock' ? opts.readAt : '2026-09-28T10:00:00Z', agrees: !((opts.disagree || []).includes(base) && k === 0), passes: !((opts.failPass || []).includes(base) && k === 0) }))
          .map(c => opts.heldAndLive && c.figure === 'stock' ? { ...c, passes: false } : c)
          .map(c => ({ ...c, ...((opts.edit || {})[c.figure] || {}), ...(kind === 'stock' ? opts.stockPatch || {} : {}) }))
          .map(c => ({ ...c, ...(((opts.editPart || {})[`${base}:${pt}`] || {})[c.figure] || {}) }))
      data.parts = (opts.omit || []).includes(base) || (opts.omitPart || []).includes(`${base}:${part}`) ? [] : [{ function: 'f1', part, kind: 'first', verdict: refute ? 'refuted' : 'confirmed', checks: checksFor(part, opts.bothRoles && !only ? 'alternate' : undefined), refutation: refute ? (opts.refutationText === undefined ? 'mock' : opts.refutationText) : '' }]
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
      data.scope = opts.partScope ? 'part' : 'function'
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
    commit: 'deadbeef', date: '2026-09-27', paths: {}, hosts: opts.hosts || [], clients: CLIENTS, inventory: opts.inventory || {}, jlcparts: JL, p5_budgets: opts.p5Budgets || [], p5_conditional: opts.p5Conditional || [], p5_chips: opts.p5Chips || {}, q_options: opts.qOptions || {},
    followup: opts.followup, first_v: 5, decision_categories: { Q4: ['R10'], Q8: ['R2', 'R12'], Q9: ['R3'] }, t6_outputs: opts.t6Outputs || T6OUT, for_research: opts.forResearch || [], raised: opts.raised || [], p1_asks: opts.p1Asks || {}, fixed_inputs: opts.fixedInputs || {}, required_reports: opts.requiredReports || {}, per_part_reports: opts.perPartReports || {},
    p5_assumptions: opts.p5Assumptions || [], decisions: opts.decisions || {}, accept_open: opts.acceptOpen || null, left_open: opts.leftOpen || [], p5_assumed: opts.p5AssumedT6 || [],
    last_p56: opts.lastP56 || '', p56_runs: opts.p56Runs || [], selection: opts.selection || {}, jlc_stock_row: opts.jlcRow || [], run_info: opts.runInfo || {}, stock_exceptions: opts.stockExceptions || [],
    research_round: opts.researchRound, runs_dir: opts.runsDir, runs_dirs: opts.runsDirs, selection_file: opts.selectionFile, branch: opts.branch, group_pages: opts.groupPages, t6_may_write: opts.t6MayWrite }
  const fn = new Function('args', 'agent', 'parallel', 'pipeline', 'phase', 'log',
    `return (async () => {${src}})()`)
  const result = await fn(args, agent, parallel, pipeline, () => {}, () => {})
  return { result, calls, prompts, efforts, models }
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

  // The verifiers are told the checks each part owes by name.
  let r = await runTask('T2')
  const dsPrompt = (r.prompts.find(p => p.label === 'P4-datasheet-R1') || {}).prompt || ''
  const owedBlock = dsPrompt.split('The checks each part owes')[1] || ''
  check(/"part1":\s*\[[^\]]*"manufacturer allowlist"/.test(owedBlock), 'P4 datasheet prompt lists manufacturer allowlist among part1\'s owed checks')
  const stPrompt = (r.prompts.find(p => p.label === 'P4-stock-R1') || {}).prompt || ''
  check(/"part1":\s*\[[^\]]*"lead time"/.test(stPrompt.split('The checks each part owes')[1] || ''), 'P4 stock prompt lists lead time among part1\'s owed checks')
  const owedOf = (res, label) => (((res.prompts.find(p => p.label === label) || {}).prompt || '').split('The checks each part owes')[1] || '').split('The shortlist,')[0]
  check(/"part1"/.test(owedOf(r, 'P4-stock-R1')) && !/"part2"|"part3"/.test(owedOf(r, 'P4-stock-R1')), 'P4 prompt lists owed checks for the planned parts only')
  check(stPrompt.includes('for another maker\'s part its product page, and where that page cannot be read or carries no status, Digi-Key\'s ProductStatus. A Digi-Key status is that of the product whose maker and manufacturer part number are the candidate\'s') && stPrompt.includes('Obsolete and Preliminary (a preview, S5) fail') && stPrompt.includes('Write any other Digi-Key value as "not read: Digi-Key ProductStatus VALUE", and a part whose maker status cannot be read (its page unreadable or without a status) and that Digi-Key does not list under its maker as "not read: REASON", each with agrees and passes true'), 'P4 stock prompt reads the lifecycle status at Digi-Key where the maker\'s page cannot be read, for the candidate\'s own maker and part number')
  r = await runTask('T2', { held: 500, heldChecks: true })
  check(/\["held quantity",/.test(owedOf(r, 'P4-stock-R1')) && !/"held quantity, or/.test(owedOf(r, 'P4-stock-R1')) && !/"stock",/.test(owedOf(r, 'P4-stock-R1')), 'P4 stock prompt owes a held part "held quantity" in place of stock and presale')

  // A return names its category by ID; the ID with the name, or the name
  // alone, names it too; another ID does not.
  r = await runTask('T2', { categoryText: c => `${c} (${cats.categories[c]})` })
  check((((r1(r) || {}).selection || [])[0] || {}).part === 'part1', 'category given as ID with its name: read as the ID')
  r = await runTask('T2', { categoryText: c => cats.categories[c] })
  check((((r1(r) || {}).selection || [])[0] || {}).part === 'part1', 'category given by its name alone: read as the ID')
  r = await runTask('T2', { categoryText: c => c === 'R1' ? 'R1 and R2' : c })
  check(r.result.missing.some(m => m.error === 'returned for category R1 and R2'), 'category text naming another ID too: not this category')
  // Every agent runs at the recorded effort; none is set without one.
  r = await runTask('T2', { runInfo: { effort: 'high' } })
  check(r.efforts.length > 0 && r.efforts.every(e => e === 'high'), 'T2: every agent at the recorded effort')
  r = await runTask('T2')
  check(r.efforts.every(e => e === undefined), 'T2: no effort set without one recorded')
  // The adjudicator runs on the oversight model, every other role on the
  // agent model; neither set, no model is given.
  r = await runTask('T2', { refute: ['P4-stock-R1:part1'], runInfo: { effort: 'high', agent_model: 'sonnet', oversight_model: 'opus' } })
  check(r.models.some(([l]) => l.startsWith('adjudicator')) && r.models.every(([l, m]) => m === (/^(adjudicator|P2-)/.test(l) ? 'opus' : 'sonnet')), 'T2: P2 and the adjudicator on the oversight model, every other role on the agent model')
  r = await runTask('T2', { refute: ['P4-stock-R1:part1'] })
  check(r.models.every(([, m]) => m === undefined), 'T2: no model set without one recorded')

  // Both verifiers confirm: the first-ranked part is kept.
  r = await runTask('T2')
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
  // P0 may add a note after the manifest's time.
  r = await runTask('T2', { p0: { snapshot: '2026-09-14T09:56:01+00:00 (manifest.json "created", equal to 2026-09-14T09:56:01Z)' } })
  check(!r.result.summary.stopped, 'snapshot with a note after its time: not stopped')
  r = await runTask('T2', { p0: { snapshot: 'created 2026-09-14T09:56:01Z' } })
  check(r.result.summary.stopped === true, 'snapshot that does not start with a time: stopped')
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
  // A stock check failing on the owner's excepted parts alone passes; the
  // exception is stated on the pages.
  const exc = [{ part: 'ADS1235IRHBR', reason: 'Digi-Key stock 0; kept (owner, 2026-09-29)' }]
  const excRow = [{ function: 'load-cell bridge ADC', part: 'ADS1235IRHBR', parts_line: 3, group_page: 'hardware/docs/GroupC.md' }]
  r = await runTask('T6', { failCheck: 'jlc_stock', failOutput: '[FAIL] ADS1235IRHBR (second vendor): stock 0, gate 50\n1 problem(s)', stockExceptions: exc, marked: [{ index: 0, file: 'hardware/docs/Parts.md', line: 3 }], partRows: excRow })
  check(!r.result.summary.stopped, 'T6: a stock check failing on an excepted part alone finishes it')
  r = await runTask('T6', { failCheck: 'jlc_stock', failOutput: '[FAIL] ADS1235IRHBR (second vendor): stock 0\n[FAIL] TCA9548APWR: stock 3', stockExceptions: exc, marked: [{ index: 0, file: 'hardware/docs/Parts.md', line: 3 }] })
  check(r.result.summary.stopped === true, 'T6: a stock check failing on another part too stops it')
  r = await runTask('T6', { failCheck: 'jlc_stock', failOutput: '[FAIL] ADS1235IRHBR (second vendor): stock 0', stockExceptions: exc })
  check(r.result.summary.stopped === true && /item 0 not stated/.test(r.result.summary.reasons[0]), 'T6: an exception not stated on the pages stops it')
  check(r.result.stock_exceptions.length === 1, 'T6: the exceptions are carried into the result')
  const atParts = [{ index: 0, file: 'hardware/docs/Parts.md', line: 3 }]
  r = await runTask('T6', { failCheck: 'jlc_stock', failOutput: '[FAIL] ADS1235IRHBR: stock 100, gate 50', stockExceptions: exc, marked: atParts })
  check(r.result.summary.stopped === true, 'T6: an excepted part whose stock is not below its gate is no shortfall and stops it')
  r = await runTask('T6', { failCheck: 'jlc_stock', failOutput: '[FAIL] ADS1235IRHBR: presale 20', stockExceptions: exc, marked: atParts })
  check(r.result.summary.stopped === true, 'T6: a presale reading above zero is no shortfall and stops it')
  r = await runTask('T6', { failCheck: 'jlc_stock', failOutput: '[FAIL] ADS1235IRHBR: lookup failed; stock 0, gate 50', stockExceptions: exc, marked: atParts })
  check(r.result.summary.stopped === true, 'T6: a line in another wording than the shortfall format is no shortfall and stops it')
  r = await runTask('T6', { failCheck: 'jlc_stock', failOutput: '[FAIL] ADS1235IRHBR: presale -99\n1 problem(s)', stockExceptions: exc, marked: atParts, partRows: excRow })
  check(!r.result.summary.stopped, 'T6: a presale reading below zero is a shortfall of an excepted part')
  r = await runTask('T6', { failCheck: 'jlc_stock', failOutput: '[FAIL] ADS1235IRHBR: stock 0, gate 50\nTraceback (most recent call last):\n  KeyError', stockExceptions: exc, marked: atParts, partRows: excRow })
  check(r.result.summary.stopped === true, 'T6: an excepted shortfall beside a crash, with no problem count, stops it')
  r = await runTask('T6', { passOutput: 'ran once; its one [FAIL] line is the owner\'s exception\n[FAIL] ADS1235IRHBR (second vendor): stock 0, gate 50\n1 problem(s)', stockExceptions: exc, marked: atParts, partRows: excRow })
  check(!r.result.summary.stopped, 'T6: prose that mentions [FAIL] is not a problem line')
  r = await runTask('T6', { failCheck: 'jlc_stock', failOutput: '[FAIL] ADS1235IRHBR: stock 0, gate 50\n1 problem(s)', stockExceptions: exc, marked: [{ index: 0, file: 'hardware/docs/Parts.md', line: 9 }], partRows: excRow })
  check(r.result.summary.stopped === true && /item 0 not stated/.test(r.result.summary.reasons[0]), 'T6: an exception marked on another line of Parts.md than its row stops it')
  r = await runTask('T6', { passOutput: '[FAIL] ADS1235IRHBR: stock 0, gate 50\n[FAIL] TCA9548APWR: stock 3, gate 50', stockExceptions: exc, marked: atParts })
  check(r.result.summary.stopped === true, 'T6: a stock check the critic marks passed is still read for [FAIL] lines outside the exceptions')
  r = await runTask('T6', { failCheck: 'jlc_stock', failOutput: '[FAIL] ADS1235IRHBRX: stock 0, gate 50', stockExceptions: exc, marked: atParts })
  check(r.result.summary.stopped === true, 'T6: a failure of a part whose number only contains the excepted one stops it')
  r = await runTask('T6', { failCheck: 'jlc_stock', failOutput: '[FAIL] ADS1235IRHBR: no exact match for C1234', stockExceptions: exc, marked: atParts })
  check(r.result.summary.stopped === true, 'T6: a failed lookup of an excepted part is no shortfall and stops it')
  r = await runTask('T6', { failCheck: 'jlc_stock', failOutput: '[FAIL] ADS1235IRHBR: stock 0, gate 50', stockExceptions: exc, marked: [{ index: 0, file: 'hardware/docs/Power.md', line: 3 }] })
  check(r.result.summary.stopped === true && /item 0 not stated/.test(r.result.summary.reasons[0]), 'T6: an exception stated outside Parts.md stops it')
  r = await runTask('T6', { runInfo: { agent_model: 'sonnet', oversight_model: 'opus' } })
  check(r.models.length === 2 && r.models.every(([, m]) => m === 'opus'), 'T6: P7 and its critic on the oversight model')

  // A second-vendor route needs the second vendor's stock re-read.
  r = await runTask('T2', { noSecondVendor: true })
  check(r1(r).selection[0].part === null, 'second-vendor route: not verified without the second-vendor stock check')

  // An alternate route that names no part is a missing second source.
  r = await runTask('T2', { emptyAlt: true })
  check(r1(r).selection[0].second_source_missing === true, 'alternate route without a part: second source missing')
  // Three group pages must be three files.
  r = await runTask('T6', { samePages: true })
  check(r.result.summary.stopped === true, 'T6: one file for three group pages stops it')
  r = await runTask('T6', { fourthPage: true })
  check(r.result.summary.stopped === true && /the three group pages are not three files/.test(r.result.summary.reasons[0]), 'T6: a fourth group page stops it')
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
  // A reading time may carry a note, a second time or a range's end; every
  // date it gives is of the run.
  r = await runTask('T2', { readAt: '2026-09-28T10:00Z to 10:05Z (JLCPCB), 2026-09-28T10:06:00Z (Digi-Key)' })
  check(r1(r).selection[0].part === 'part1', 'reading time with notes and a range: verified')
  r = await runTask('T2', { readAt: '2026-09-28T10:00:00Z; stock copied from 2026-09-20' })
  check(r1(r).selection[0].part === null, 'reading time naming an earlier date: not verified')
  r = await runTask('T2', { readAt: '2026-09-281' })
  check(r1(r).selection[0].part === null, 'reading time with a digit after the date: not verified')
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
  // A refutation about the part itself holds in every function.
  r = await runTask('T2', { refute: ['P4-stock-R1:part1'], partScope: true })
  check(r1(r).part_refuted.includes('part1') && r.result.summary.part_refuted.R1.includes('part1'), 'refutation of the part itself: recorded for every function')
  r = await runTask('T2', { refute: ['P4-stock-R1:part1'] })
  check(r1(r).part_refuted.length === 0, 'refutation about the function: not recorded for the part')
  // A replacement kept in a Q4 function has its ranked class confirmed.
  r = await runTask('T4', { qAlt: true, p4parts: ['part3', 'altQ'], qOptions: { Q4: ['reference', 'external ADC'] }, qOption: 'external ADC', keptOption: 'reference', rankOptions: { part1: 'reference', part2: 'reference' }, refute: ['P4-stock-R10:part1'] })
  const r10c = r.result.summary.results.find(c => c.category === 'R10')
  check(r10c.selection[0].part === 'part2' && r10c.selection[0].q_options_missing.length === 0 && r.prompts.find(x => x.label === 'P4-datasheet-R10').prompt.includes('Q option: f1: part2: reference'), 'replacement kept in the Q4 function: its ranked class confirmed')
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
  // Shared-part stock is owed when a part is kept for two functions.
  const twoUses = { R1: { f1: { part: 'X' } }, R8: { f2: { part: 'X' } } }
  r = await runTask('T5', { selection: twoUses, p5Budgets: ['shared-part stock'], p5Conditional: ['shared-part stock'], p5Items: ['shared-part stock'], budgetValues: { 'shared-part stock': 'not applicable: no part is shared' } })
  check(r.result.summary.budgets_missing.includes('shared-part stock') && r.result.summary.budgets_missing.includes('shared-part stock: X'), 'T5: shared-part stock not applicable while a part is shared: missing')
  r = await runTask('T5', { selection: twoUses, p5Budgets: ['shared-part stock'], p5Conditional: ['shared-part stock'], p5Items: ['shared-part stock: X'] })
  check(r.result.summary.budgets_missing.length === 0, 'T5: the shared part budgeted at its summed placements: complete')
  r = await runTask('T5', { selection: { R1: { f1: { part: 'X' } } }, p5Budgets: ['shared-part stock'], p5Conditional: ['shared-part stock'], p5Items: ['shared-part stock'], budgetValues: { 'shared-part stock': 'not applicable: no part is shared' } })
  check(r.result.summary.budgets_missing.length === 0, 'T5: shared-part stock not applicable with no shared part: complete')
  // An alternate kept in two functions is shared too.
  r = await runTask('T5', { selection: { R1: { f1: { part: 'A', alternate: 'X' } }, R8: { f2: { part: 'B', alternate: 'X' } } }, p5Budgets: ['shared-part stock'], p5Conditional: ['shared-part stock'], p5Items: ['shared-part stock'], budgetValues: { 'shared-part stock': 'not applicable: no part is shared' } })
  check(r.result.summary.budgets_missing.includes('shared-part stock: X'), 'T5: an alternate kept in two functions owes shared-part stock')
  // A P5-P6 follow-up re-reads stock: JLCPCB unreachable stops it.
  r = await runTask('FU', { followup: { phases: 'P5-P6', round: 1, categories: [], items: [] }, hosts: [{ host: 'jlcpcb.com', client: 'jlcpcb-api', probe: 'C39843328', stop: 'stock-tasks', hold: [] }], down: ['jlcpcb.com'] })
  check(r.result.summary.stopped === true, 'P5-P6 follow-up: JLCPCB down stops it')
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
  // A lifecycle status written not read, an unlisted part or a Digi-Key
  // value the Lifecycle check does not grade, neither passes nor fails: the
  // part stays not verified, no adjudicator runs, the owner gets a notice;
  // also when the verifier marks it as disagreeing.
  for (const agrees of [true, false]) {
    r = await runTask('T2', { edit: { 'lifecycle status': { read: 'not read: Digi-Key ProductStatus Discontinued at Digi-Key', agrees } } })
    check(r1(r).selection[0].part === null && !r.calls.some(c => c.startsWith('adjudicator-R1')) && r.result.followUps.some(f => f.figure === 'lifecycle status' && f.notice && /Discontinued at Digi-Key/.test(f.reason)), `lifecycle status not read (agrees ${agrees}): not verified, not refuted, listed for the owner`)
  }
  // A failing grade written inside such a line still fails.
  r = await runTask('T2', { edit: { 'lifecycle status': { read: 'not read: maker page HTTP 404; Digi-Key ProductStatus Obsolete', agrees: false, passes: false } } })
  check(r1(r).selection[0].part !== 'part1' && r.calls.some(c => c.startsWith('adjudicator-R1')), 'lifecycle status not read with passes false: refuted')
  r = await runTask('T2', { edit: { 'lifecycle status': { read: 'Active (Digi-Key ProductStatus, maker page HTTP 404)' } } })
  check(r1(r).selection[0].part === 'part1' && !r.result.followUps.some(f => f.figure === 'lifecycle status'), 'lifecycle status read at Digi-Key: verified')
  // A held quantity named in the owed list's earlier wording, as FU-2C's
  // R11 stock verifier named it, is the same check, also beside failing
  // live stock; another name is not.
  const oldName = { 'held quantity': { figure: 'held quantity, or both stock and presale' } }
  r = await runTask('T2', { held: 500, heldChecks: true, edit: oldName })
  check(r1(r).selection[0].part === 'part1', 'held part with its held quantity in the earlier owed wording: verified')
  r = await runTask('T2', { held: 500, heldChecks: true, heldAndLive: true, edit: oldName })
  check(r1(r).selection[0].part === 'part1' && !r.calls.some(c => c.startsWith('adjudicator-R1')), 'held part in the earlier owed wording with failing live stock: verified on its held quantity')
  r = await runTask('T2', { held: 500, heldChecks: true, edit: { 'held quantity': { figure: 'held stock' } } })
  check(r1(r).selection[0].part === null, 'held part with its held quantity under another name: not verified')
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
  // P3 names the functions whose drop it overturned: a function that
  // dropped the part for another reason is not held open by it. Without
  // names, or with none under which P2 dropped the part, every function that
  // dropped it is.
  const overYinX = { p3overturned: true, twoDeciders: true, p2DropYAll: true, extraFn: true, rrHandleYin: true }
  r = await runTask('T2', { ...overYinX, p3overFns: ['fX'] })
  check(r1(r).selection.find(e => e.function === 'f1').part === 'part1', 'overturned exclusion of the function P3 names, handled there: the other function that dropped it is ranked')
  r = await runTask('T2', overYinX)
  check(r1(r).selection.find(e => e.function === 'f1').part === null, 'overturned exclusion with no function named: every function that dropped it stays open')
  r = await runTask('T2', { ...overYinX, p3overFns: ['F1 buck'] })
  check(r1(r).selection.find(e => e.function === 'f1').part === null, 'overturned exclusion under a function that did not drop it: every function that dropped it stays open')
  r = await runTask('T2', { p3overturned: true, twoDeciders: true, p2DropY: true, extraFn: true, rrHandleYin: true, p3overFns: ['fX'] })
  check(r1(r).selection.find(e => e.function === 'f1').part === null, 'overturned exclusion under a P2 function that did not drop it: the function that dropped it stays open')
  r = await runTask('T2', { ...overYinX, p3overFns: ['f1'] })
  check(r1(r).selection.find(e => e.function === 'f1').part === null, 'overturned exclusion of f1, handled only under fX: f1 stays open')
  r = await runTask('T2', { ...overYinX, p3overFns: ['f1', 'fX'] })
  check(r1(r).selection.find(e => e.function === 'f1').part === null, 'overturned exclusion of f1 and fX, handled only under fX: f1 stays open')
  r = await runTask('T2', { ...overYinX, p3overFns: ['F1 buck', 'fX'] })
  check(r1(r).selection.find(e => e.function === 'f1').part === 'part1', 'overturned exclusion of fX and a function P2 did not return, handled under fX: f1 is ranked')
  // A '#' packing suffix does not make another part: the re-rank's
  // LTC4020EUHF#TRPBF handles P3's LTC4020EUHF#PBF.
  const overAdi = { p3overturned: true, p3overPart: 'LTC4020EUHF#PBF (C2858365)', p2DropY: true, p2DropPart: 'LTC4020EUHF#PBF', p3dropShort: true }
  r = await runTask('T2', { ...overAdi, dropShortPart: 'LTC4020EUHF#TRPBF', dropShortLcsc: 'C462630' })
  check(r1(r).selection[0].part === 'part1', "overturned exclusion of a '#' variant the re-rank dropped: handled")
  r = await runTask('T2', { ...overAdi, dropShortPart: 'LTC4021EUHF#TRPBF', dropShortLcsc: 'C462631' })
  check(r1(r).selection[0].part === null, "overturned exclusion, another number before the '#': open")
  for (const [base, what] of [['A1B', 'fewer than 4 characters'], ['1234', 'no letter'], ['ABCD', 'no digit']]) {
    r = await runTask('T2', { ...overAdi, p3overPart: `${base}#1`, p2DropPart: `${base}#1`, dropShortPart: `${base}#2` })
    check(r1(r).selection[0].part === null, `a '#' after ${what} is part of the number: open`)
  }
  // An overturned drop no P2 function made is handled under another option,
  // and the re-rank's drop of that option is re-read.
  const overAdiNoOwner = { p3overturned: true, p3overPart: 'LTC4020EUHF#PBF (C2858365)', p3dropShort: true, dropShortPart: 'LTC4020EUHF#TRPBF', dropShortLcsc: 'C462630' }
  r = await runTask('T2', overAdiNoOwner)
  check(r1(r).selection[0].part === 'part1', "overturned exclusion no function made, another '#' option dropped: handled")
  r = await runTask('T2', { ...overAdiNoOwner, p2DropY: true, p2DropPart: 'LTC4020EUHF#PBF', omitFigure: ['P4-datasheet-R1', 're-rank drop: f1: LTC4020EUHF#TRPBF'] })
  check(r.result.followUps.some(f => f.figure === 're-rank drop: f1: LTC4020EUHF#TRPBF' && f.reason === 'figure not verified'), "drop of another '#' option of an overturned exclusion: re-read")
  // P2's drop of another option is P2's drop of the part: only the function
  // that dropped it stays open.
  r = await runTask('T2', { p3overturned: true, p3overPart: 'ABC123#PBF', twoDeciders: true, extraFn: true, p2DropY: true, p2DropPart: 'ABC123#TRPBF' })
  check(r.result.followUps.some(f => f.part === 'ABC123#PBF' && f.reason === 'P2 exclusion P3 overturned, neither qualified nor dropped by the re-rank' && f.functions.join() === 'f1'), "overturned exclusion of a '#' option P2 dropped as another option: only the function that dropped it stays open")
  // A P3 find is not handled by another ordering option: the find's option
  // may be the one with stock.
  r = await runTask('T2', { p3missed: true, p3missedPart: 'ABC123#TRPBF (C462630)', p3dropShort: true, dropShortPart: 'ABC123#PBF', dropShortLcsc: 'C2858365' })
  check(r1(r).selection[0].part === null, "P3 find of a '#' option, another option dropped: open")
  r = await runTask('T2', { p3missed: true, p3missedFn: 'F1 buck', p3missedPart: 'ABC123#TRPBF (C462630)', p3dropShort: true, dropShortPart: 'ABC123#PBF', dropShortLcsc: 'C2858365' })
  check(r1(r).selection[0].part === null, "P3 find of a '#' option under no function of P2's, another option dropped: open")
  // The re-rank is told to list every P3 part in its lists, not its report.
  r = await runTask('T2', { p3overturned: true })
  const rrPrompt = r.prompts.find(x => x.label === 'rerank-R1').prompt
  check(["Every entry of P3's missed and exclusions_not_holding is a P3 candidate", 'also where P3 says the drop stands for another reason', 'under each function that dropped it', "with its part text copied exactly from P3's entry, one entry for each entry of P3's, also where the entry names several parts", 'Qualify it in new_candidates, by its exact part number', "Every record in new_candidates that answers an entry of P3's gives that entry's text, copied exactly, in p3_part", 'or a drop no function of P2\'s made, under one of P2\'s', 'under each such function', 'A part discussed only in your report is neither qualified nor dropped'].every(t => rrPrompt.includes(t)), 're-rank prompt: every P3 entry listed')
  // A record the re-rank qualified under another number handles the P3
  // entry it names in p3_part; without p3_part it does not.
  const groupFind = { p3missed: true, p3missedPart: '50 uOhm ABC8536 and DEF-R00005 parts', newP3: ['ABC8536L1000JK60'] }
  r = await runTask('T2', { ...groupFind, newP3For: '50 uOhm ABC8536 and DEF-R00005 parts' })
  check(r1(r).selection[0].part === 'part1', 'P3 group find answered by a qualified record naming it in p3_part: handled')
  r = await runTask('T2', groupFind)
  check(r1(r).selection[0].part === null, 'P3 group find, qualified record without p3_part: open')
  r = await runTask('T2', { ...groupFind, newP3For: '50 uOhm ABC8536 parts' })
  check(r1(r).selection[0].part === null, 'P3 group find, p3_part naming another entry: open')
  // A record whose number follows "also" in P3's entry needs p3_part too.
  r = await runTask('T2', { p3missed: true, p3missedPart: 'ABC123 (C201541), also ABC124', newP3: ['ABC124'], newP3For: 'ABC123 (C201541), also ABC124' })
  check(r1(r).selection[0].part === 'part1', 'P3 entry naming a second part after "also", qualified with p3_part: handled')
  r = await runTask('T2', { p3missed: true, p3missedPart: 'ABC123 (C201541), also ABC124', newP3: ['ABC124'] })
  check(r1(r).selection[0].part === null, 'P3 entry naming a second part after "also", qualified without p3_part: open')
  // p3_part handles only the entry whose text it copies, not another entry
  // that shares a part number with it.
  r = await runTask('T2', { p3missed: true, p3missedPart: 'TMP1075DR (C2878381)', p3overturned: true, p3overPart: 'TMP1075DR, TMP275AIDR', p2DropY: true, p2DropPart: 'TMP1075DR, TMP275AIDR', newP3: ['TMP275AIDR'], newP3For: 'TMP1075DR, TMP275AIDR' })
  check(r1(r).selection[0].part === null && r.result.followUps.some(f => f.part === 'TMP1075DR (C2878381)' && f.reason === 'P3 candidate neither qualified nor dropped by the re-rank') && !r.result.followUps.some(f => f.part === 'TMP1075DR, TMP275AIDR'), 'p3_part of one entry: the entry handled, another entry sharing a number open')
  r = await runTask('T2', { p3missed: true, p3missedPart: '50 uOhm  ABC8536 and DEF-R00005 parts', newP3: ['ABC8536L1000JK60'], newP3For: '50 uohm ABC8536 and DEF-R00005 parts ' })
  check(r1(r).selection[0].part === 'part1', 'p3_part with other spacing and case: handled')
  // A drop of a group find copies P3's text; split into its parts, it does
  // not match the find.
  r = await runTask('T2', { p3missed: true, p3missedPart: '50 uOhm ABC8536 and DEF-R00005 parts', dropP3: ['50 uOhm ABC8536 and DEF-R00005 parts'] })
  check(r1(r).selection[0].part === 'part1', "P3 group find dropped under P3's text: handled")
  r = await runTask('T2', { p3missed: true, p3missedPart: '50 uOhm ABC8536 and DEF-R00005 parts', dropP3: ['ABC8536', 'DEF-R00005'] })
  check(r1(r).selection[0].part === null, 'P3 group find dropped part by part: open')
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
  // P3 names a find in its own words; the re-rank's part number handles it.
  r = await runTask('T2', { p3missed: true, p3dropShort: true, p3missedPart: 'partX (C1234)' })
  check(r1(r).selection[0].part === 'part1', 'P3 find with its LCSC number, dropped by part number: handled')
  r = await runTask('T2', { p3missed: true, p3dropShort: true, p3missedPart: 'partW (C99); partX (C1234), f1' })
  check(r1(r).selection[0].part === 'part1', 'P3 find of two parts with its function appended, one dropped: handled')
  r = await runTask('T2', { p3missed: true, p3dropShort: true, p3missedPart: 'partW,215 (C99)' })
  check(r1(r).selection[0].part === null, 'P3 find of another part number: unhandled')
  r = await runTask('T2', { p3missed: true, p3dropShort: true, p3missedPart: 'CAN transceiver (C99)', dropShortPart: 'partY (C99)' })
  check(r1(r).selection[0].part === 'part1', 'P3 find by LCSC number alone, dropped under another name: handled')
  r = await runTask('T2', { p3missed: true, p3dropShort: true, p3missedPart: 'CAN transceiver (C99)', dropShortPart: 'partY', dropShortLcsc: 'C99' })
  check(r1(r).selection[0].part === 'part1', 'P3 find by LCSC number, the drop naming it in its lcsc field: handled')
  r = await runTask('T2', { p3missed: true, p3dropShort: true, p3missedPart: 'partW (c98) AND partx (c99)', dropShortPart: 'PARTX' })
  check(r1(r).selection[0].part === 'part1', 'P3 find in another case, joined by AND: handled')
  // A find that names only a family ("STEM rows") is handled by a part of
  // that family the re-rank qualified or dropped as a P3 candidate; a part
  // P2 shortlisted, a find that also names a part, and a stem that is not a
  // whole word of 6 or more letters, digits and hyphens with a letter and a
  // digit followed by a family word, are not.
  const kept = { firstRecord: { part: 'ABC1234A' }, ranking: [{ rank: 1, part: 'ABC1234A', reason: 'r' }, { rank: 2, part: 'part2', reason: 'r' }, { rank: 3, part: 'part3', reason: 'r' }] }
  const missedOpen = res => res.result.followUps.some(f => f.reason === 'P3 candidate neither qualified nor dropped by the re-rank')
  for (const [find, opts, open, what] of [
    ['ABC1234 rows (and others of the same kind)', { dropP3: ['ABC1234XYZ-RE'] }, false, 'a part of it dropped as a P3 candidate'],
    ['ABC1234 rows', { newP3: ['ABC1234XYZ'] }, false, 'a part of it qualified as a new candidate'],
    ['ABC123 family', { dropP3: ['ABC123X'] }, false, 'a 6-character stem'],
    ['ABC-1234 series', { dropP3: ['ABC-1234X'] }, false, 'a stem with a hyphen, series'],
    ['ABC1234 devices', { dropP3: ['ABC1234X'] }, false, 'devices'],
    ['ABC1234 variants', { dropP3: ['ABC1234X'] }, false, 'variants'],
    ['ABC1234 parts', { dropP3: ['ABC1234X'] }, false, 'parts'],
    ['ABC1234 rows, and more', { dropP3: ['ABC1234X'] }, false, 'a family word with punctuation after it'],
    ['ABC1234 rows', { dropP3: ['ABC9999XYZ'] }, true, 'another family dropped'],
    ['ABC1234 for the gate', { dropP3: ['ABC1234X'] }, true, 'a part number no family word follows'],
    ['ABC1234 rows', { dropShort: ['ABC1234X'] }, true, 'only a shortlist part of it dropped'],
    ['ABC1234 rows', kept, true, 'only a family part P2 kept ranked'],
    ['ABC1234 rows', { ...kept, dropP3: ['ABC1234A'] }, true, 'a family part P2 shortlisted, listed as a P3 drop'],
    ['AB123 rows', { dropP3: ['AB12345'] }, true, 'a 5-character stem'],
    ['ABCDEFG parts', { dropP3: ['ABCDEFGH'] }, true, 'a stem without a digit'],
    ['1234567 parts', { dropP3: ['12345678'] }, true, 'a stem without a letter'],
    ['-ABC123 rows', { dropP3: ['-ABC1234'] }, true, 'a word starting with a hyphen'],
    ['note (see ABC1234 rows)', { dropP3: ['ABC1234XYZ'] }, true, 'a family named only inside parentheses'],
    ['AP2112K-3.3TRG1 variants', { dropP3: ['AP2112K-3.3TRG1-7'] }, true, 'a part number with a dot'],
    ['MCP2542FD-E/SN variants', { dropP3: ['MCP2542FD-E/SN-1'] }, true, 'a part number with a slash'],
    ['2N7002BK,215 devices', { dropP3: ['2N7002BK,215X'] }, true, 'a part number with a comma'],
    ['ADBMS6832MWCCSZ (C18166020); ADBMS6833 is the 16-channel sibling', { dropP3: ['ADBMS6833'] }, true, 'a context word'],
    ['ABC1234XY (C9), ABC1234 family', { dropP3: ['ABC1234ZZ'] }, true, 'a find that also names a part'],
    ['ADBMS6830 family: ADBMS6830MWCCSZ-RL C18168951', { dropP3: ['ADBMS6830B'] }, true, 'a find that also names a part with its LCSC number'],
    ['ABC1234 rows (LCSC C99)', { dropP3: ['ABC1234X'] }, true, 'a family with an LCSC number'],
  ]) {
    r = await runTask('T2', { p3missed: true, p3missedPart: find, ...opts })
    check(missedOpen(r) === open, `P3 family find, ${what}: ${open ? 'open' : 'handled'}`)
  }
  // The owner of a family exclusion handles it the same way.
  const famExcl = { p3overturned: true, p3overPart: 'ABC1234 rows', p2DropY: true, p2DropPart: 'AS5048B-HTSP-500, ABC1234 rows' }
  const overturnedOpen = res => res.result.followUps.some(f => f.reason === 'P2 exclusion P3 overturned, neither qualified nor dropped by the re-rank')
  r = await runTask('T2', { ...famExcl, dropP3: ['ABC1234XYZ'] })
  check(!overturnedOpen(r), 'P3 family exclusion, a part of it dropped as a P3 candidate: handled')
  r = await runTask('T2', { ...famExcl, ...kept })
  check(overturnedOpen(r), 'P3 family exclusion, only a family part P2 kept ranked: open')
  // The drop of a family find is re-read as any P3 drop is.
  r = await runTask('T2', { p3missed: true, p3missedPart: 'ABC1234 rows', dropP3: ['ABC1234Q'], p3dropShort: true, dropShortPart: 'ABC1234XYZ-RE', omitFigure: ['P4-datasheet-R1', 're-rank drop: f1: ABC1234XYZ-RE'] })
  check(r.result.followUps.some(f => f.figure === 're-rank drop: f1: ABC1234XYZ-RE' && f.reason === 'figure not verified'), 'P3 family find dropped from the shortlist: re-read')
  r = await runTask('T2', { p3missed: true, p3dropShort: true, p3missedPart: 'partW-REEL / partX' })
  check(r1(r).selection[0].part === 'part1', 'P3 find of two parts joined by a spaced slash: handled')
  r = await runTask('T2', { p3missed: true, p3dropShort: true, p3missedPart: 'partW/partX' })
  check(r1(r).selection[0].part === null, 'a slash without spaces is part of one part number: unhandled')
  // The drop of a find named in P3's own words is re-read as any P3 drop.
  r = await runTask('T2', { p3missed: true, p3dropShort: true, p3missedPart: 'partX (C1234)', omitFigure: ['P4-datasheet-R1', 're-rank drop: f1: partX'] })
  check(r.result.followUps.some(f => f.figure === 're-rank drop: f1: partX' && f.reason === 'figure not verified'), 'drop of an annotated P3 find: re-read')
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
  // A fixed input that P2's record fails is dropped as any part is, and the
  // datasheet verifier re-reads the drop; the function keeps no other part
  // and the owner is told.
  r = await runTask('T2', { fixedInputs: fixedPart1, failedReq: true })
  check(r1(r).selection[0].part === null && r.result.followUps.some(f => f.role === 'owner' && f.part === 'part1' && /fails P2's requirement: x0/.test(f.reason)) && !r.result.followUps.some(f => /not ranked first/.test(f.reason || '')) && r.prompts.find(x => x.label === 'P4-datasheet-R1').prompt.includes('failed requirement: f1: part1 (x0)'), 'fixed input failing a P2 requirement: function open, drop re-read, reported to the owner')
  // A passing variant of the fixed input left first after the drops keeps
  // the function, whether the failing variant ranked above or below it.
  const fixedPrefix = { R1: [{ function: 'f1', part: 'PART' }] }
  r = await runTask('T2', { fixedInputs: fixedPrefix, failedReqAt: 1 })
  check(r1(r).selection[0].part === 'part1' && !r.result.followUps.some(f => f.role === 'owner'), 'fixed input first and passing, a failing variant below it: kept')
  r = await runTask('T2', { fixedInputs: { R1: [{ function: 'f1', part: 'part3' }] }, firstRecord: { part: 'part3-RL7' }, failedReq: true, ranking: ['part3-RL7', 'part2', 'part3'].map((part, i) => ({ rank: i + 1, part, reason: 'r' })) })
  check(r1(r).selection[0].part === null && r.result.followUps.some(f => f.role === 'owner' && f.part === 'part2' && /not ranked first/.test(f.reason)) && !r.result.followUps.some(f => /fails P2's requirement/.test(f.reason || '')), 'failing variant first, another part above the passing input: reported as not ranked first')
  r = await runTask('T2', { fixedInputs: fixedPrefix, failedReq: true, p4parts: ['part2'] })
  check(r1(r).ledger.some(l => l.part === 'part2') && !r.result.followUps.some(f => f.role === 'owner'), 'fixed input\'s failing variant dropped, a passing variant first: kept for P4')
  r = await runTask('T2', { failedReq: true })
  check(r1(r).ledger.every(l => l.part !== 'part1') && r.result.followUps.some(f => f.role === 'P2' && /with a failed requirement/.test(f.reason || '')), 'a part that is no fixed input and fails a P2 requirement: dropped')
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
  // A P1 follow-up keeps the names earlier runs gave the functions.
  r = await runTask('FU', { followup: { phases: 'P1', round: 1, categories: ['R1'], items: [] }, inventory: { R1: ['CAN transceiver', 'flash'] } })
  check(['P1-R1', 'P1-critic-R1'].every(l => r.prompts.find(x => x.label === l).prompt.includes("Give a function the category's inventory (CAN transceiver; flash) or undefined/hardware/research/round1/selection.json already names that exact name.")), 'P1 follow-up: P1 and the critic keep the functions\' names')
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
  // The time may follow one word naming the source; a negation, two words or
  // a date outside the task are no reading.
  for (const [t, counts] of [['JLCPCB 2026-09-28T15:17:10Z; Digi-Key 2026-09-28T15:17:30Z', true], ['datasheet: 2026-09-28T10:00:00Z', true], ['Nordic 2026-09-28T10:00:00Z', true], ['ADXL316: 2026-09-28T10:00:00Z', true], ...['not', 'No', 'None', 'nothing', 'never', 'unknown', 'Unread:', 'Not-read', 'No-data', 'None.', 'unread.', 'nil', 'unavailable', 'Missing', 'pending', 'failed', 'NA', 'n.a', 'N.A.', 'n/a'].map(w => [`${w} 2026-09-28T10:00:00Z`, false]), ['JLCPCB page 2026-09-28T10:00:00Z', false], ['JLCPCB 2026-09-20T10:00:00Z', false], ['JLCPCB 2026-09-28T10:00:00Z; Digi-Key 2026-09-20T10:00:00Z', false]]) {
    r = await runTask('T2', { refute: ['P4-stock-R1:part1'], stands: false, rulingReadAt: t })
    check(counts ? r1(r).selection[0].part === 'part1' : r1(r).selection[0].part === null && r.result.followUps.some(f => f.reason === 'the ruling gives no evidence, source and time read'), `ruling read at "${t}": ${counts ? 'a ruling' : 'no ruling'}`)
  }
  // "none" is a reading of a notice or commitment that does not exist, and
  // no reading of a status or identity; "not found" is none.
  // A reading recorded without a gate may be not read, with its reason; it
  // is listed for the owner. Any other check not read shows nothing.
  r = await runTask('T2', { edit: { 'longevity commitment': { read: 'not read: www.analog.com refused the page (HTTP 403)' }, 'market introduction': { read: 'not read: the datasheet gives no revision date' } } })
  check(r1(r).selection[0].part === 'part1' && r.result.followUps.some(f => f.figure === 'longevity commitment' && /^recorded as not read/.test(f.reason)), 'longevity and market introduction not read, with a reason: verified, listed')
  r = await runTask('T2', { edit: { 'longevity commitment': { read: 'not read: the programme page refused the client', passes: false, agrees: false } } })
  check(r1(r).selection[0].part === 'part1', 'longevity commitment not read, with a reason, marked failing: recorded, not a refutation')
  // A datasheet requirement of the same name is a requirement: not read
  // shows nothing there.
  r = await runTask('T2', { fnReq: 'longevity commitment', edit: { 'longevity commitment': { read: 'not read: the datasheet does not say' } } })
  check(r1(r).selection[0].part === null, 'datasheet requirement named longevity commitment, not read: not verified')
  for (const read of ['not read: none', 'not read: N/A', 'not read: -', 'not read (unknown)']) {
    r = await runTask('T2', { edit: { 'longevity commitment': { read } } })
    check(r1(r).selection[0].part === null, `longevity commitment read as ${read}: not verified`)
  }
  // A refuted verdict whose only failing check is such a reading is none.
  r = await runTask('T2', { refute: ['P4-stock-R1:part1'], refutationText: '', edit: { 'longevity commitment': { read: 'not read: the programme page refused the client', passes: false } } })
  check(r1(r).selection[0].part === 'part1' && !r.calls.some(c => c.startsWith('adjudicator-R1')), 'refutation stating nothing, resting only on an unread non-gate reading: none, no adjudicator')
  r = await runTask('T2', { refute: ['P4-stock-R1:part1'], edit: { 'longevity commitment': { read: 'not read: the programme page refused the client', passes: false } } })
  check(r.calls.some(c => c.startsWith('adjudicator-R1')), 'refutation stating a reason beside an unread non-gate reading: adjudicated')
  r = await runTask('T2', { refute: ['P4-stock-R1:part1'], edit: { 'longevity commitment': { read: 'not read: the programme page refused the client', passes: false }, 'lifecycle status': { passes: false } } })
  check(r.calls.some(c => c.startsWith('adjudicator-R1')), 'refutation with another failing check besides an unread reading: adjudicated')
  r = await runTask('T2', { edit: { 'longevity commitment': { read: 'not read' } } })
  check(r1(r).selection[0].part === null, 'longevity commitment not read without a reason: not verified')
  r = await runTask('T2', { edit: { 'lead time': { read: 'not read: API timed out' } } })
  check(r1(r).selection[0].part === null, 'lead time not read: not verified')
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
  r = await runTask('T2', { held: 20, addStock: ['held quantity'], edit: { 'held quantity': { figure: 'held quantity, or both stock and presale', passes: false } } })
  check(r1(r).selection[0].part === 'part1' && !r.calls.some(c => c.startsWith('adjudicator-R1')), 'failing held quantity in the earlier owed wording, passing live stock: verified')
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
  // A budget or combination comes from the last P5/P6 check; another figure,
  // such as the status line, may cite an earlier check's files.
  r = await runTask('T6', { p56Runs: ['T5', 'FU-b'], lastP56: 'FU-b', returnFile: 'hardware/research/round1/T5/003-P5.json', figureKind: 'budget' })
  check(r.result.summary.stopped === true && r.prompts.find(x => x.label === 'P7-critic').prompt.includes('against the last P5/P6 check, FU-b, only') && r.prompts.find(x => x.label === 'P7-critic').prompt.includes('and its kind: budget or combination'), 'T6: a budget checked against a superseded P5/P6 check stops it')
  r = await runTask('T6', { p56Runs: ['T5', 'FU-b'], lastP56: 'FU-b', returnFile: 'hardware/research/round1/selection.json', figureKind: 'budget' })
  check(r.result.summary.stopped === true, 'T6: a budget checked against selection.json stops it')
  r = await runTask('T6', { p56Runs: ['T5', 'FU-b'], lastP56: 'FU-b', returnFile: 'hardware/research/round1/T5/task.json' })
  check(!r.result.summary.stopped, 'T6: a figure other than a budget or combination checked against an earlier P5/P6 check finishes it')
  r = await runTask('T6', { p56Runs: ['T5', 'FU-b'], lastP56: 'FU-b', returnFile: 'hardware/research/round1/FU-b/003-P5.json', figureKind: 'budget', decisions: { Q4: 'external ADC (owner, 2026-10-01)' } })
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
  check(r.result.summary.stopped === true && /no Parts.md row or group A page for altA/.test(r.result.summary.reasons[0]), 'T6: a selected part without its Parts.md row stops it')
  r = await runTask('T6', { ...selT6, partRows: rows, jlcReview: [{ index: 0, holds: false, line: 0, reason: 'reads the first result' }] })
  check(r.result.summary.stopped === true && /tools\/jlc_stock.py not confirmed/.test(r.result.summary.reasons[0]), 'T6: a stock tool that does not do its Outputs row stops it')
  r = await runTask('T6', { ...selT6, partRows: rows, jlcReview: [{ index: 0, holds: true, line: 12, reason: 'r' }] })
  check(!r.result.summary.stopped, 'T6: every part in Parts.md and the stock tool confirmed finish it')
  // A part not verified, open and accepted as such, is owed no row.
  const qAlts = [{ part: 'part3', status: 'refuted', alternate: 'altQ', alternate_status: '' }, { part: 'part4', status: 'verified', alternate: 'altR', alternate_status: 'not verified' }, { part: 'part5', status: 'verified', alternate: 'altS', alternate_status: 'verified' }]
  const selQ = { ...selT6, selection: { R10: { f1: { part: 'part1', alternate: 'altA', alternate_unverified: ['altA'], q_alternatives: qAlts } } }, jlcReview: [{ index: 0, holds: true, line: 12, reason: 'r' }] }
  r = await runTask('T6', { ...selQ, partRows: ['part1', 'part4', 'part5', 'altS'].map(part => ({ function: 'f1', part, parts_line: 4, group_page: 'hardware/docs/GroupC.md' })) })
  check(!r.result.summary.stopped, `T6: parts not verified owed no Parts.md row: ${(r.result.summary.reasons || []).join()}`)
  // Each part is named on the page of its category's group.
  r = await runTask('T6', { ...selQ, partRows: ['part1', 'part4', 'part5', 'altS'].map(part => ({ function: 'f1', part, parts_line: 4, group_page: 'hardware/docs/GroupA.md' })) })
  check(r.result.summary.stopped === true && /group C page for part1/.test(r.result.summary.reasons[0]), 'T6: an R10 part named only on the group A page stops it')
  // A stock-tool verdict holds with a line of the tool that does it.
  r = await runTask('T6', { ...selT6, partRows: rows, jlcReview: [{ index: 0, holds: true, line: 0, reason: 'r' }] })
  check(r.result.summary.stopped === true && /jlc_stock.py not confirmed/.test(r.result.summary.reasons[0]), 'T6: a stock-tool verdict without its line stops it')
  // A candidate P2 shortlisted that is also another's alternate keeps its place.
  r = await runTask('T2', { altName: 'part2', ranked: [1] })
  check(r.result.followUps.some(f => f.part === 'part2' && f.reason === 'candidate neither ranked nor dropped'), 'P2 candidate named as an alternate and not ranked: kept in P2 order, listed')
  // Every figure P7 wrote needs a check of its own.
  r = await runTask('T6', { p7Figures: [{ file: 'hardware/docs/Parts.md', line: 1, figure: 'stock' }, { file: 'hardware/docs/Parts.md', line: 2, figure: '42 mA' }] })
  check(r.result.summary.stopped === true && /figures P7 wrote without a check/.test(r.result.summary.reasons[0]), 'T6: a figure P7 wrote without a check stops it')
  // A figure check names a line of its page; P7 names the line of each figure.
  r = await runTask('T6', { checkLine: 0 })
  check(r.result.summary.stopped === true && /no figure checked on|checked no figure/.test(r.result.summary.reasons[0]), 'T6: a figure check at line 0 checks nothing')
  r = await runTask('T6', { p7Figures: [{ file: 'hardware/docs/Parts.md', line: 0, figure: 'stock' }] })
  check(r.result.summary.stopped === true && /figures P7 wrote without their line/.test(r.result.summary.reasons[0]), 'T6: a figure P7 wrote without its line stops it')
  // A check whose text the critic corrected stands for P7's figure it names.
  r = await runTask('T6', { p7Figures: [{ file: 'hardware/docs/Parts.md', line: 1, figure: 'VGS(th) 3 V' }] })
  check(r.result.summary.stopped === true && /figures P7 wrote without a check/.test(r.result.summary.reasons[0]), 'T6: a P7 figure whose text the critic changed, without p7_figure, is unchecked')
  r = await runTask('T6', { p7Figures: [{ file: 'hardware/docs/Parts.md', line: 1, figure: 'stock' }], p7FigureAlias: 'stock (corrected)' })
  check(!/figures P7 wrote without a check/.test((r.result.summary.reasons || [''])[0]), 'T6: a corrected check naming P7\'s text in p7_figure covers it')
  // A figure P7 wrote on two lines needs checks on two lines.
  r = await runTask('T6', { p7Figures: [{ file: 'hardware/docs/Parts.md', line: 1, figure: 'stock' }, { file: 'hardware/docs/Parts.md', line: 3, figure: 'stock' }] })
  check(r.result.summary.stopped === true && /figures P7 wrote without a check/.test(r.result.summary.reasons[0]), 'T6: one check for a figure P7 wrote on two lines stops it')
  // A restarted P5 return of an earlier check is superseded too.
  r = await runTask('T6', { p56Runs: ['T5', 'FU-b'], lastP56: 'FU-b', returnFile: 'hardware/research/round1/T5/004-P5-restart.json', figureKind: 'other' })
  check(r.result.summary.stopped === true, 'T6: a figure citing an earlier restarted P5 return stops it')
  // A figure citing an earlier check's P5 return is superseded whatever its kind.
  r = await runTask('T6', { p56Runs: ['T5', 'FU-b'], lastP56: 'FU-b', returnFile: 'hardware/research/round1/T5/003-P5.json', figureKind: 'other' })
  check(r.result.summary.stopped === true, 'T6: a figure labelled other citing an earlier P5 return stops it')
  r = await runTask('T6', { ...selQ, partRows: rows.slice(0, 1) })
  check(r.result.summary.stopped === true && ['part4', 'part5', 'altS'].every(p => r.result.summary.reasons[0].includes(`for ${p} (`)) && !['part3', 'altQ', 'altR', 'altA'].some(p => r.result.summary.reasons[0].includes(`for ${p} (`)), 'T6: each verified part without its Parts.md row stops it')

  // Follow-up plans.
  for (const [phases, n, want] of [['P1', 2, 6], ['P2-P4', 3, 16], ['P5-P6', 0, 5]]) {
    const { result } = await runTask('FU', { followup: { phases, round: 1, categories: ['R1', 'R2', 'R3'].slice(0, n), items: [] } })
    check(result.planned === want, `FU ${phases}: planned ${result.planned}, expected ${want}`)
  }

  // Round 2: every prompt names the round, the records of both rounds, the
  // selection.json prepare gives and the round's own section of the page;
  // the follow-up plans count as in round 1.
  const R2 = { researchRound: 2, runsDir: 'hardware/research/round2', runsDirs: ['hardware/research/round1', 'hardware/research/round2'], branch: 'research/round2' }
  const bothRounds = 'undefined/hardware/research/round1/<run>/ and undefined/hardware/research/round2/<run>/'
  for (const [phases, n, want] of [['P1', 2, 6], ['P2-P4', 3, 16], ['P5-P6', 0, 5]]) {
    const { result, prompts } = await runTask('FU', { ...R2, selectionFile: 'hardware/research/round1/selection.json', followup: { phases, round: 1, categories: ['R1', 'R2', 'R3'].slice(0, n), items: [] } })
    check(result.planned === want, `round 2 FU ${phases}: planned ${result.planned}, expected ${want}`)
    const off = prompts.filter(x => !(x.prompt.includes('in task FU of round 2 of the rcbench') && x.prompt.includes(`Returns of earlier tasks: ${bothRounds}.`) && x.prompt.includes('is in undefined/hardware/research/round1/selection.json;') && x.prompt.includes('the section "Round 2" of that page; where it and a category row or a phase row differ, that section holds')))
    check(prompts.length && !off.length, `round 2 FU ${phases}: ${off.map(x => x.label).join(', ')} miss the round, its records, its selection or its section`)
    if (phases === 'P5-P6') {
      check(prompts.find(x => x.label === 'P5').prompt.includes('Read every return under undefined/hardware/research/round1/ and undefined/hardware/research/round2/ (T2, T3, T4'), 'round 2 P5: both rounds\' returns')
      check(prompts.find(x => x.label === 'P6').prompt.includes('or, as the section "Round 2" sets out, "stage 2 of round 2"; every figure in undefined/hardware/research/round1/ and undefined/hardware/research/round2/ has'), 'round 2 P6: the stage 2 mark and both rounds\' figures')
    }
  }
  // From round 2 each budget of a chip's resources is owed for the main and
  // the measurement coprocessor: one combined row does not do.
  const P56 = { ...R2, followup: { phases: 'P5-P6', round: 1, categories: [], items: [] }, p5Budgets: ['GPIO', 'DMA channels', 'current per rail'], p5Chips: { chips: cats.p5_chips.chips, budgets: cats.p5_chips.budgets } }
  r = await runTask('FU', { ...P56, p5Items: ['GPIO', 'DMA channels', 'current per rail'] })
  check(r.result.summary.budgets_missing.join() === 'GPIO: main,GPIO: measurement,DMA channels: main,DMA channels: measurement'
    && ['P5', 'P5-critic'].every(l => r.prompts.find(x => x.label === l).prompt.includes('GPIO, once for each chip as GPIO: main and GPIO: measurement; DMA channels, once for each chip as DMA channels: main and DMA channels: measurement; current per rail')),
    `round 2 P5: combined chip budgets are missing for each chip: ${r.result.summary.budgets_missing}`)
  r = await runTask('FU', { ...P56, p5Items: ['GPIO: main', 'DMA channels: main', 'DMA channels: measurement: PPM', 'current per rail'] })
  check(r.result.summary.budgets_missing.join() === 'GPIO: measurement', `round 2 P5: a chip without its budget is missing: ${r.result.summary.budgets_missing}`)
  r = await runTask('FU', { ...P56, p5Items: ['GPIO: main', 'GPIO: measurement', 'DMA channels: main', 'DMA channels: measurement', 'current per rail'] })
  check(r.result.summary.budgets_missing.length === 0, `round 2 P5: each chip budgeted: ${r.result.summary.budgets_missing}`)
  r = await runTask('FU', { ...P56, p5Chips: {}, p5Items: ['GPIO', 'DMA channels', 'current per rail'] })
  check(r.result.summary.budgets_missing.length === 0 && !r.prompts.find(x => x.label === 'P5').prompt.includes('once for each chip'), 'without p5_chips a budget is owed once')
  r = await runTask('FU', { ...R2, selectionFile: 'hardware/research/round2/selection.json', followup: { phases: 'P2-P4', round: 1, categories: ['R5'], items: [] } })
  const p2r2 = r.prompts.find(x => x.label === 'P2-R5').prompt
  check(p2r2.includes('The parts earlier tasks selected are in undefined/hardware/research/round2/selection.json;') && p2r2.includes('Name each function exactly as undefined/hardware/research/round2/selection.json names it for R5.'), 'round 2 P2: the selection.json prepare gives')
  // Round 2's T6 updates the group pages round 1 wrote and must write
  // IOBoard.md, Research.md and STATUS.md; the other outputs only where
  // they change, so an unwritten README.md does not stop it.
  const PAGES = { A: 'hardware/docs/Control.md', B: 'hardware/docs/Supply.md', C: 'hardware/docs/Sensing.md' }
  const MAY = ['hardware/docs/Parts.md', 'hardware/docs/Power.md', 'hardware/README.md', 'tools/jlc_stock.py']
  const T6R2 = { ...R2, selectionFile: 'hardware/research/round2/selection.json', groupPages: PAGES, t6Outputs: ['hardware/docs/IOBoard.md', 'hardware/docs/Research.md', 'hardware/STATUS.md'], t6MayWrite: MAY }
  r = await runTask('T6', T6R2)
  check(!r.result.summary.stopped, `round 2 T6: the given group pages and the three outputs finish it: ${(r.result.summary.reasons || []).join()}`)
  const p7r2 = r.prompts.find(x => x.label === 'P7').prompt
  const p7cr2 = r.prompts.find(x => x.label === 'P7-critic').prompt
  check(p7r2.includes('merged with research/round2 before this task') && p7r2.includes(`group_pages: the pages ${JSON.stringify(PAGES)}, which round 1 wrote; update them.`) && !p7r2.includes('three new files')
    && p7r2.includes(`Write each of ${MAY.join(', ')} only where the returns of round 2 change its content, and list it in files only then.`) && !p7r2.includes('Write tools/jlc_stock.py to read')
    && p7r2.includes('Write them from the returns under undefined/hardware/research/round1/ and undefined/hardware/research/round2/ only, the parts in hardware/research/round2/selection.json,'), 'round 2 T6: P7 updates the group pages and writes the other outputs only where they change')
  check(p7cr2.includes('against the returns under undefined/hardware/research/round1/ and undefined/hardware/research/round2/: a budget') && p7cr2.includes('checked against hardware/research/round2/selection.json counts')
    && p7cr2.includes('in the files P7 listed only: a file P7 did not list stays as it is') && p7cr2.includes('Check at least one figure on each of these pages: hardware/docs/IOBoard.md, hardware/docs/Research.md, hardware/docs/Control.md, hardware/docs/Supply.md, hardware/docs/Sensing.md;'), 'round 2 T6: the critic checks both rounds\' returns on the pages P7 writes')
  r = await runTask('T6', { ...T6R2, p7Pages: { ...PAGES, A: 'hardware/docs/GroupA.md' } })
  check(r.result.summary.stopped === true && /group page hardware\/docs\/GroupA.md is not group A's page/.test(r.result.summary.reasons[0]), 'round 2 T6: a group page other than round 1\'s stops it')
  // An output written only where it changes needs a figure checked once
  // P7 lists it in files.
  r = await runTask('T6', { ...T6R2, p7Extra: ['hardware/docs/Parts.md'] })
  check(!r.result.summary.stopped && r.prompts.find(x => x.label === 'P7-critic').prompt.includes('Check at least one figure on each of these pages: hardware/docs/IOBoard.md, hardware/docs/Research.md, hardware/docs/Parts.md, hardware/docs/Control.md,'), `round 2 T6: Parts.md listed in files is among the pages to check: ${(r.result.summary.reasons || []).join()}`)
  const r2Figs = ['IOBoard', 'Research', 'Control', 'Supply', 'Sensing'].map(n => ({ file: `hardware/docs/${n}.md`, line: 1, figure: 'stock' }))
  r = await runTask('T6', { ...T6R2, p7Extra: ['hardware/docs/Parts.md'], noFigureOn: 'hardware/docs/Parts.md', p7Figures: r2Figs })
  check(r.result.summary.stopped === true && /no figure checked on hardware\/docs\/Parts.md/.test(r.result.summary.reasons[0]), 'round 2 T6: Parts.md listed in files with no figure checked stops it')
  r = await runTask('T6', { ...T6R2, noFigureOn: 'hardware/docs/Parts.md', p7Figures: r2Figs })
  check(!r.result.summary.stopped, `round 2 T6: Parts.md not listed owes no figure check: ${(r.result.summary.reasons || []).join()}`)
  r = await runTask('T6', { ...T6R2, unwritten: 'hardware/STATUS.md' })
  check(r.result.summary.stopped === true && /not written: hardware\/STATUS.md/.test(r.result.summary.reasons[0]), 'round 2 T6: STATUS.md unwritten stops it')
  // A budget comes from round 2's last P5/P6 check, under round 2's
  // directory; one from round 1's T5, or from a run of that name under
  // another round's directory, is superseded. A figure from a directory no
  // round records in counts for no page.
  r = await runTask('T6', { ...T6R2, p56Runs: ['T5', 'FU-2P56'], lastP56: 'FU-2P56', returnFile: 'hardware/research/round2/FU-2P56/003-P5.json', figureKind: 'budget' })
  check(!r.result.summary.stopped, `round 2 T6: a budget from round 2's last P5/P6 check finishes it: ${(r.result.summary.reasons || []).join()}`)
  r = await runTask('T6', { ...T6R2, p56Runs: ['T5', 'FU-2P56'], lastP56: 'FU-2P56', returnFile: 'hardware/research/round1/T5/003-P5.json', figureKind: 'budget' })
  check(r.result.summary.stopped === true && /not checked against the last P5\/P6 check, FU-2P56/.test(r.result.summary.reasons[0]), 'round 2 T6: a budget from round 1\'s T5 stops it')
  r = await runTask('T6', { ...T6R2, p56Runs: ['T5', 'FU-2P56'], lastP56: 'FU-2P56', returnFile: 'hardware/research/round1/FU-2P56/003-P5.json', figureKind: 'other' })
  check(r.result.summary.stopped === true && /not checked against the last P5\/P6 check/.test(r.result.summary.reasons[0]), 'round 2 T6: a P5 return under another round\'s directory stops it')
  r = await runTask('T6', { ...T6R2, returnFile: 'hardware/research/round3/T2/012-P4-stock-R1.json' })
  check(r.result.summary.stopped === true && /the critic checked no figure/.test(r.result.summary.reasons[0]), 'round 2 T6: a figure from no round\'s directory counts for no page')
  r = await runTask('T6', { returnFile: 'hardware/research/round2/T2/012-P4-stock-R1.json' })
  check(r.result.summary.stopped === true && /the critic checked no figure/.test(r.result.summary.reasons[0]), 'round 1 T6: a figure from round 2\'s directory counts for no page')
  // A stock exception's mark sits on its Parts.md row, an output round 2
  // writes only where it changes.
  const excR2 = { selection: { R1: { f1: { part: 'part1', alternate: '', q_alternatives: [] } } }, stockExceptions: [{ part: 'part1', reason: 'kept by the owner' }], partRows: [{ function: 'f1', part: 'part1', parts_line: 4, group_page: 'hardware/docs/Control.md' }] }
  r = await runTask('T6', { ...T6R2, ...excR2, marked: [{ index: 0, file: 'hardware/docs/Parts.md', line: 4 }] })
  check(!r.result.summary.stopped, `round 2 T6: a stock exception marked on its Parts.md row finishes it: ${(r.result.summary.reasons || []).join()}`)
  r = await runTask('T6', { ...T6R2, ...excR2, marked: [] })
  check(r.result.summary.stopped === true && /item 0 not stated on the pages/.test(r.result.summary.reasons[0]), 'round 2 T6: a stock exception not marked stops it')

  console.log(failures ? `${failures} checks failed` : 'dry run: every check passed')
  process.exit(failures ? 1 : 0)
}

main()
