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

// opts: refute ['label:part'], refuteFigure ['label'], stands, omit ['label'],
// nulls {label: count}, throws [label], critic 'none' | 'all', p0 {...}, hosts
async function runTask(task, opts = {}) {
  const calls = []
  const nulls = new Map(Object.entries(opts.nulls || {}))
  async function agent(prompt, o) {
    const base = o.label.replace(/:restart$/, '')
    calls.push(o.label)
    if ((opts.throws || []).includes(base)) throw new Error('mock throw')
    const left = nulls.get(base)
    if (left) { nulls.set(base, left - 1); return null }
    const role = roleOf(o.schema)
    const data = fake(o.schema)
    const cand = rank => ({ ...fake(schemas.P2.properties.functions.items.properties.shortlist.items), rank, part: `part${rank}`,
      second_source_route: rank === 2 && opts.replacementAlt ? 'alternate' : 'second-vendor', second_source_part: rank === 2 && opts.replacementAlt ? 'altB' : '' })
    if (role === 'P0') {
      Object.assign(data, { stop: false, held: [], checkout_head: 'deadbeef', snapshot: '2026-09-14T09:56:01+00:00',
        jlcparts: { path: 'db', sha256: JL.sha256, sha256_ok: true, rows: JL.rows, missing_lcsc: [] },
        monostable: { commit: 'c', path: 'p', fetched: true },
        hosts: (opts.hosts || []).map(h => ({ host: h.host, url: '', client: '', http_status: 200, bytes: 1, status_marker: opts.markerless ? '' : 'Status - Active', reachable: !(opts.down || []).includes(h.host), note: '' })) }, opts.p0 || {})
    }
    if (role === 'P2') {
      data.functions = [{ ...data.functions[0], function: 'f1', shortlist: [1, 2, 3].map(cand) }]
      if (opts.replacementAlt) data.functions[0].shortlist.push({ ...cand(9), part: 'altB', rank: 9 })
      if (opts.fnReq) data.functions[0].requirements = [...data.functions[0].requirements, { name: opts.fnReq, value: 'v', source: 's' }]
    }
    if (role === 'rerank') {
      data.functions = [{ function: 'f1', ranking: (opts.ranked || [1, 2, 3]).map(rank => ({ rank, part: `part${rank}`, reason: 'r' })),
        new_candidates: opts.p3dropped ? [{ ...cand(7), part: 'partN' }] : [], dropped_from_p3: opts.p3dropped ? [{ part: 'partN', maker: 'm', reason: 'r' }] : [],
        dropped_from_shortlist: [], verify: (opts.verify || []).map(part => ({ part, kind: 'alternate' })) }]
    }
    if (role === 'P1' && opts.assumption) data.values = [{ where: 'IOBoard.md:1', quantity: 'ripple', value: '10 mV', marking: 'assumption', source: 's', refutation_tried: 'r', question: -1 }]
    if (role === 'P1' && opts.twoAssumptions) {
      data.values = [1, 2].map(n => ({ where: `IOBoard.md:${n}`, quantity: 'voltage', value: `${n} V`, marking: 'assumption', source: 's', refutation_tried: 'r', question: n === 1 ? 0 : -1 }))
      data.questions = [{ function: 'rail', question: 'State the voltage of rail 1', why: 'w', blocks: 'p2', decision: 'none', for_where: '' }]
    }
    if (role === 'P3' && opts.p3missed) data.missed = [{ function: 'f1', part: 'partX', maker: 'm', why: 'w' }]
    if (role === 'P3' && opts.p3overturned) data.exclusions_not_holding = [{ part: 'partY', reason_given: 'r', why_it_fails: 'w' }]
    if (role === 'P7-critic' || role === 'P7') {
      for (const k of Object.keys(data.checks)) data.checks[k] = { passed: !(opts.failCheck === k && role === 'P7-critic'), output: 'o' }
      const outs = [...T6OUT, 'hardware/docs/GroupA.md', 'hardware/docs/Power.md', 'hardware/docs/GroupC.md'].filter(f => !(role === 'P7' && f === opts.unwritten))
      if (role === 'P7') { data.files = outs; data.group_pages = { A: 'hardware/docs/GroupA.md', B: 'hardware/docs/Power.md', C: 'hardware/docs/GroupC.md' } }
      else data.reviewed = outs
    }
    if (role === 'P1-recheck') {
      const added = JSON.parse(/confirm or reject it[^\n]*\n([\s\S]*)$/.exec(prompt).pop().split('\n').pop())
      data.verdicts = added.map(q => ({ category: q.category, index: q.index, verdict: 'confirmed', evidence: 'e' }))
    }
    if (role === 'P1-critic') {
      if (opts.critic === 'none') data.question_verdicts = []
      else data.question_verdicts = (opts.twoAssumptions ? [0] : [0, 1]).map(index => ({ index, verdict: 'confirmed', reason: 'r' }))
      data.added = []
      data.marking_verdicts = opts.markings === 'none' ? [] : (opts.assumption
        ? [{ where: 'IOBoard.md:1', quantity: 'ripple', verdict: 'holds', correct_marking: 'unchanged', reason: 'r' }]
        : opts.twoAssumptions ? [1, 2].map(n => ({ where: `IOBoard.md:${n}`, quantity: 'voltage', verdict: 'holds', correct_marking: 'unchanged', reason: 'r' }))
        : [0, 1].map(i => ({ where: `x${i}`, quantity: `x${i}`, verdict: 'holds', correct_marking: 'unchanged', reason: 'r' })))
    }
    if (role === 'P4') {
      const only = /replaces a refuted one; list only it: (\{.*\})/.exec(prompt)
      const part = only ? JSON.parse(only[1]).part : 'part1'
      const kind = o.label.includes('stock') ? 'stock' : 'datasheet'
      const refute = (opts.refute || []).includes(`${base}:${part}`)
      data.verifier = kind
      // The checks a correct verifier returns, by the names round1.js requires.
      const bundle2 = JSON.parse(prompt.slice(prompt.lastIndexOf('\n') + 1))
      const f1 = bundle2.functions[0]
      const cand = pt => [...f1.shortlist, ...(f1.alternateRecords || [])].find(c => c.part === pt) || { requirements: [] }
      const checksFor = (pt, pk) => (opts.emptyChecks || []).includes(base) ? []
        : (kind === 'stock' ? ['stock', 'presale', 'lifecycle status', ...(cand(pt).second_source_route === 'second-vendor' && !opts.noSecondVendor ? ['second-vendor stock'] : [])]
          : [...cand(pt).requirements.map(r => r.name).filter(n => n !== opts.skipReq), ...(pk === 'alternate' && !opts.noCompat ? ['pin-for-pin match', 'functional match'] : [])])
          .map((figure, k) => ({ figure, stated: 's', read: 'r', source: 'src', agrees: !((opts.disagree || []).includes(base) && k === 0) }))
      data.parts = (opts.omit || []).includes(base) ? [] : [{ function: 'f1', part, kind: 'first', verdict: refute ? 'refuted' : 'confirmed', checks: checksFor(part), refutation: refute ? 'mock' : '' }]
      const oc = only ? JSON.parse(only[1]) : null
      if (oc && oc.second_source_route === 'alternate' && !opts.dropReplacementAlt) data.parts.push({ function: 'f1', part: oc.second_source_part, kind: 'alternate', verdict: 'confirmed', checks: checksFor(oc.second_source_part, 'alternate'), refutation: '' })
      const names = only ? [] : JSON.parse(/"figures_to_check":(\[[^\]]*\])/.exec(prompt)[1])
      data.figures = kind === 'datasheet' ? names.filter(n => !(opts.omitFigure && base === opts.omitFigure[0] && n === opts.omitFigure[1]))
        .map((figure, k) => ({ figure, verdict: (opts.refuteFigure || []).includes(base) && k === 0 ? 'refuted' : 'confirmed', evidence: 'e' })) : []
      if (opts.verify && !(opts.omit || []).includes(base) && !opts.dropVerify && !only) data.parts.push(...opts.verify.map(pt => ({ function: 'f1', part: pt, kind: opts.reportFirst ? 'first' : 'alternate', verdict: (opts.refute || []).includes(`${base}:${pt}`) ? 'refuted' : 'confirmed', checks: checksFor(pt, opts.reportFirst ? 'first' : 'alternate'), refutation: '' })))
    }
    if (role === 'adjudicator') data.stands = opts.stands !== false
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
    commit: 'deadbeef', date: '2026-09-27', paths: {}, hosts: opts.hosts || [], jlcparts: JL,
    followup: opts.followup, first_v: 5, t6_outputs: T6OUT }
  const fn = new Function('args', 'agent', 'parallel', 'pipeline', 'phase', 'log',
    `return (async () => {${src}})()`)
  const result = await fn(args, agent, parallel, pipeline, () => {}, () => {})
  return { result, calls }
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
  r = await runTask('T2', { omitFigure: ['P4-datasheet-R1', 'P2 report: x1'] })
  check(r.result.followUps.some(f => f.figure === 'P2 report: x1' && f.reason === 'figure not verified'), 'figure without a verdict: listed')

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
  // A reachable maker page without its lifecycle status is listed.
  r = await runTask('T2', { hosts: [{ host: 'www.nxp.com', marker: 'Status', hold: ['R2'] }], p0: {} , markerless: true })
  check(r.result.followUps.some(f => f.host === 'www.nxp.com' && f.reason === 'lifecycle status not read from the page'), 'marker absent: listed')

  // A part to verify with no shortlist record is still verified.
  // An alternate with no record cannot show its gate evidence.
  r = await runTask('T2', { verify: ['partZ'] })
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
  r = await runTask('T2', { verify: ['part2'], reportFirst: true })
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
