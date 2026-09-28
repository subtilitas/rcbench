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
      for (const k of schema.required || []) o[k] = fake(schema.properties[k], i)
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
// nulls {label: count}, throws [label], critic 'none' | 'all', p0 {...}, hosts
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
      if (opts.reportPerPart) data.report = [{ figure: 'frames held: MCP2518FD', value: '2', source: 's' }]
      if (opts.strictReq) data.functions[0].shortlist[0].requirements = [{ name: 'x0', required: '>= 99 V', datasheet: '70 V', pass: false, source: 's' }, { name: 'x1', required: 'x1', datasheet: 'd', pass: true, source: 's' }]
      if (opts.reportParts) data.report = opts.reportParts.map(figure => ({ figure, value: 'not applicable: transceiver', source: 's' }))
      if (opts.p2DropY) data.functions[0].dropped = [{ part: 'partY', maker: 'm', reason: 'r' }]
      if (opts.foundNotRead) data.found_values = [{ question_id: 'V9', value: 'not read: HTTP 403', source: 's', read_at: 't' }]
      if (opts.onBoardAltOfOff) { Object.assign(data.functions[0].shortlist[0], { lcsc: 'none', second_source_route: 'alternate', second_source_part: 'altOn' }); data.functions[0].shortlist.push({ ...cand(8), part: 'altOn', lcsc: 'C2' }) }
      if (opts.weakReq) { data.functions[0].requirements = [{ name: 'x0', value: '>= 67.2 V', source: 's' }]; data.functions[0].shortlist.forEach(c => { c.requirements = [{ name: 'x0', required: '>= 40 V', datasheet: '45 V', pass: true, source: 's' }] }) }
      if (opts.offBoardAlt) { Object.assign(data.functions[0].shortlist[0], { lcsc: 'C1', second_source_route: 'alternate', second_source_part: 'altOff' }); data.functions[0].shortlist.push({ ...cand(8), part: 'altOff', lcsc: 'none' }) }
      if (opts.held !== undefined) data.functions[0].shortlist.forEach(c => { c.held = opts.held; c.lcsc = 'C9' })
      if (opts.altName) Object.assign(data.functions[0].shortlist[0], { second_source_route: 'alternate', second_source_part: opts.altName })
      if (opts.altFewer) {
        Object.assign(data.functions[0].shortlist[0], { placements: 20, second_source_route: 'alternate', second_source_part: 'altF' })
        data.functions[0].shortlist.push({ ...cand(9), part: 'altF', rank: 9, placements: 5 })
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
      data.functions = [{ function: 'f1', decision: opts.qDecision || ({ R10: 'Q4', R12: 'Q8' }[rcat] || 'none'), ranking: (opts.ranking || (opts.ranked || [1, 2, 3]).map(rank => ({ rank, part: `part${rank}`, reason: 'r' }))),
        new_candidates: opts.p3dropped ? [{ ...cand(7), part: 'partN' }] : [], dropped_from_p3: opts.p3dropped ? [{ part: 'partN', maker: 'm', reason: 'r' }] : [],
        dropped_from_shortlist: opts.rankDropped ? [{ part: 'part1', maker: 'm', reason: 'r' }] : [], verify: opts.twoQShare ? [{ part: 'part2', kind: 'q-alternative' }, { part: 'part3', kind: 'q-alternative' }] : opts.qAlt || opts.sharedAlt ? [{ part: 'part3', kind: 'q-alternative' }] : (opts.verify || []).map(part => ({ part, kind: 'alternate' })) }]
      if (opts.extraFn) data.functions.push({ ...data.functions[0], function: 'fX', ...(opts.rrHandleYin ? { dropped_from_shortlist: [{ part: 'partY', maker: 'm', reason: 'r' }] } : {}) })
      if (opts.p3dropShort) data.functions[0].dropped_from_shortlist = [{ part: 'partX', maker: 'm', reason: 'fails vmax' }]
      if (opts.rrDup) data.functions.push({ ...data.functions[0], ranking: [], dropped_from_shortlist: [{ part: 'part1', maker: 'm', reason: 'fails vmax at 85 C' }] })
      if (opts.noRerankFn) data.functions = []
      if (opts.emptyRanking) data.functions[0].ranking = []
      if (opts.qAltBack) data.functions[0].verify = [{ part: 'part3', kind: 'q-alternative' }]
      if (opts.qSelf) data.functions[0].verify = [{ part: 'part1', kind: 'q-alternative' }]
    }
    if (role === 'P5' && opts.p5Empty) Object.assign(data, { combinations: [], budgets: [] })
    if (role === 'P5' && opts.p5Items) data.budgets = opts.p5Items.map(item => ({ item, value: 'v', source: 's' }))
    if (role === 'P5-critic') for (const k of ['combination_verdicts', 'budget_verdicts']) data[k] = data[k].map((v, index) => ({ ...v, index, holds: !(opts.rejectBudget && k === 'budget_verdicts' && index === 0) }))
    if (role === 'P1' && opts.qCategory) data.questions = data.questions.map(q => ({ ...q, category: 'R99', source: 'mock' }))
    if (role === 'P1' && opts.decisionNone) data.questions = data.questions.map(q => ({ ...q, blocks: 'decision-only', decision: 'none' }))
    if (role === 'P1' && opts.contradict) data.values = data.values.map(v => ({ ...v, question: -1 }))
    if (role === 'P1' && opts.assumption) data.values = [{ where: 'IOBoard.md:1', quantity: 'ripple', value: '10 mV', marking: 'assumption', source: 's', refutation_tried: 'r', question: -1 }]
    if (role === 'P1' && (opts.twoAssumptions || opts.sharedQ)) {
      data.values = [1, 2].map(n => ({ where: `IOBoard.md:${n}`, quantity: 'voltage', value: `${n} V`, marking: 'assumption', source: 's', refutation_tried: 'r', question: n === 1 || opts.sharedQ ? 0 : -1 }))
      data.questions = [{ function: 'rail', question: 'State the voltage of rail 1', why: 'w', blocks: 'p2', decision: 'none', for_where: '' }]
    }
    if (role === 'P3') Object.assign(data, { missed: [], exclusions_not_holding: [], missed_functions: opts.missedFn ? [{ function: 'f2', why: 'the row names it' }] : [] })
    if (role === 'P3' && opts.p3missed) data.missed = [{ function: 'f1', part: 'partX', maker: 'm', why: 'w' }]
    if (role === 'P3' && opts.p3overturned) data.exclusions_not_holding = [{ part: 'partY', reason_given: 'r', why_it_fails: 'w' }]
    if (role === 'P7-critic' || role === 'P7') {
      for (const k of Object.keys(data.checks)) data.checks[k] = { passed: !(opts.failCheck === k && role === 'P7-critic'), output: 'o' }
      const outs = [...T6OUT, 'hardware/docs/GroupA.md', 'hardware/docs/GroupB.md', 'hardware/docs/GroupC.md'].filter(f => !(role === 'P7' && f === opts.unwritten))
      if (opts.pageOutside) outs.push('tools/research/README.md')
      if (role === 'P7') { data.files = outs; data.group_pages = opts.pageOutside ? { A: 'hardware/docs/GroupA.md', B: 'tools/research/README.md', C: 'hardware/docs/GroupC.md' } : opts.samePages ? { A: 'hardware/docs/Power.md', B: 'hardware/docs/Power.md', C: 'hardware/docs/Power.md' } : (opts.pagePower ? { A: 'hardware/docs/GroupA.md', B: 'hardware/docs/Power.md', C: 'hardware/docs/GroupC.md' } : { A: 'hardware/docs/GroupA.md', B: 'hardware/docs/GroupB.md', C: 'hardware/docs/GroupC.md' }) }
      else {
        data.reviewed = outs
        const figFiles = opts.oneFigure ? ['hardware/docs/Parts.md'] : outs.filter(f => f.startsWith('hardware/docs/'))
        data.figure_checks = opts.noFigures ? [] : figFiles.map(file => ({ file, line: 1, figure: 'stock', return_file: 'r', agrees: !opts.criticDisagrees }))
        data.sentence_issues = opts.sentenceIssue ? [{ file: 'f', line: 1, issue: 'i' }] : []
      }
    }
    if (role === 'P1-recheck' && opts.recheckRejects) {
      const added = JSON.parse(/confirm or reject it[^\n]*\n([\s\S]*)$/.exec(prompt).pop().split('\n').pop())
      data.verdicts = added.map(q => ({ category: q.category, index: q.index, verdict: 'rejected', evidence: 'e' }))
      return data
    }
    if (role === 'P1-recheck') {
      const added = JSON.parse(/confirm or reject it[^\n]*\n([\s\S]*)$/.exec(prompt).pop().split('\n').pop())
      data.verdicts = added.map(q => ({ category: q.category, index: q.index, verdict: 'confirmed', evidence: 'e' }))
    }
    if (role === 'P1-critic') {
      if (opts.critic === 'none') data.question_verdicts = []
      else data.question_verdicts = (opts.twoAssumptions ? [0] : opts.dupVerdicts ? [0, 0, 1] : [0, 1]).map(index => ({ index, verdict: 'confirmed', reason: 'r' }))
      data.added = []
      data.marking_verdicts = opts.markings === 'none' ? [] : (opts.assumption
        ? [{ where: 'IOBoard.md:1', quantity: 'ripple', verdict: 'holds', correct_marking: 'unchanged', reason: 'r' }]
        : opts.twoAssumptions ? [1, 2].map(n => ({ where: `IOBoard.md:${n}`, quantity: 'voltage', verdict: 'holds', correct_marking: 'unchanged', reason: 'r' }))
        : opts.contradict ? [{ where: 'x0', quantity: 'x0', verdict: 'wrong', correct_marking: 'unchanged', reason: 'r' }, { where: 'x1', quantity: 'x1', verdict: 'holds', correct_marking: 'unchanged', reason: 'r' }]
        : [0, 1].map(i => ({ where: `x${i}`, quantity: `x${i}`, verdict: 'holds', correct_marking: 'unchanged', reason: 'r' })))
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
        : (kind === 'stock' ? [...(opts.heldChecks ? ['held quantity', ...(opts.heldAndLive ? ['stock'] : [])] : ['stock', 'presale']), 'lifecycle status', 'end-of-life notices', ...(opts.noPlacementsCheck ? [] : ['placements']), ...(cand(pt).second_source_route === 'second-vendor' && !opts.noSecondVendor ? ['second-vendor stock'] : [])]
          : [...cand(pt).requirements.map(r => r.name).filter(n => n !== opts.skipReq), ...(opts.noMakerCheck ? [] : ['manufacturer allowlist']), ...(pk === 'alternate' && !opts.noCompat ? ['pin-for-pin match', 'functional match'] : [])])
          .map((figure, k) => ({ figure, stated: 's', read: opts.readNone && kind === 'datasheet' ? 'not read: API timed out' : 'r', source: opts.noSource && kind === 'stock' ? '' : 'src', read_at: opts.undated && kind === 'stock' ? '' : '2026-09-28T10:00:00Z', agrees: !((opts.disagree || []).includes(base) && k === 0), passes: !((opts.failPass || []).includes(base) && k === 0) }))
          .map(c => opts.heldAndLive && c.figure === 'stock' ? { ...c, passes: false } : c)
      data.parts = (opts.omit || []).includes(base) || (opts.omitPart || []).includes(`${base}:${part}`) ? [] : [{ function: 'f1', part, kind: 'first', verdict: refute ? 'refuted' : 'confirmed', checks: checksFor(part), refutation: refute ? 'mock' : '' }]
      if ((opts.dupRow || []).includes(base)) data.parts.push({ ...data.parts[0], verdict: 'refuted', refutation: 'second row' })
      const oc = only ? JSON.parse(only[1]) : null
      if (oc && oc.second_source_route === 'alternate' && !opts.dropReplacementAlt) data.parts.push({ function: 'f1', part: oc.second_source_part, kind: 'alternate', verdict: 'confirmed', checks: checksFor(oc.second_source_part, 'alternate'), refutation: '' })
      const names = only ? [] : JSON.parse(/"figures_to_check":(\[[^\]]*\])/.exec(prompt)[1])
      data.figures = kind === 'datasheet' ? names.filter(n => !(opts.omitFigure && base === opts.omitFigure[0] && n === opts.omitFigure[1]))
        .map((figure, k) => ({ figure, verdict: (opts.refuteFigure || []).includes(base) && k === 0 ? 'refuted' : 'confirmed', evidence: opts.unreadFigure && k === 0 ? 'not read' : 'e', source: opts.unsourcedFigure && k === 0 ? '' : 'datasheet table 5', read_at: '2026-09-28T10:00:00Z' })) : []
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
  const args = { task, cap: cats.cap, categories: cats.categories, tasks: cats.tasks, schemas,
    commit: 'deadbeef', date: '2026-09-27', paths: {}, hosts: opts.hosts || [], clients: CLIENTS, jlcparts: JL, p5_budgets: opts.p5Budgets || [],
    followup: opts.followup, first_v: 5, t6_outputs: T6OUT, for_research: opts.forResearch || [], required_reports: opts.requiredReports || {}, per_part_reports: opts.perPartReports || {} }
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
  // A Q alternative whose alternate is the kept part: the kept part needs the alternate's checks.
  r = await runTask('T4', { qAltBack: true, p4parts: ['part3'] })
  check(r.result.summary.results.find(c => c.category === 'R10').ledger.some(l => l.part === 'part1' && l.status === 'not verified'), 'kept part that is a Q alternative\'s alternate: compatibility checks required')
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
  // A malformed LCSC number drops the candidate.
  r = await runTask('T2', { lcsc: 'C123oops' })
  check(r1(r).selection[0].part === null && r.result.followUps.some(f => /LCSC number "C123oops"; dropped/.test(f.reason)), 'malformed LCSC number: dropped')
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
  check(r1(r).selection[0].part === null && r.result.followUps.some(f => f.reason === 'the ruling gives no evidence read'), 'ruling without evidence read: no ruling')
  // An overturned exclusion is handled by the function that dropped it.
  r = await runTask('T2', { p3overturned: true, p2DropY: true, extraFn: true, rrHandleYin: 'fX' })
  check(r1(r).selection.find(e => e.function === 'f1').part === null, 'overturned exclusion handled by another function: owner stays open')
  // A P3 find the re-rank did not handle keeps its function open.
  r = await runTask('T2', { p3missed: true })
  check(r1(r).selection[0].part === null, 'unhandled P3 find: function open')
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

  // P0's own stop and held fields do not decide the outcome.
  r = await runTask('T2', { p0: { stop: true, stop_reasons: ['mock'], held: [{ category: 'R1', host: 'h', reason: 'mock' }] } })
  check(!r.result.summary.stopped && r.calls.includes('P2-R1'), 'P0: its own stop and held are not applied')

  // Follow-up plans.
  for (const [phases, n, want] of [['P1', 2, 6], ['P2-P4', 3, 16], ['P5-P6', 0, 5]]) {
    const { result } = await runTask('FU', { followup: { phases, round: 1, categories: ['R1', 'R2', 'R3'].slice(0, n), items: [] } })
    check(result.planned === want, `FU ${phases}: planned ${result.planned}, expected ${want}`)
  }

  console.log(failures ? `${failures} checks failed` : 'dry run: every check passed')
  process.exit(failures ? 1 : 0)
}

main()
