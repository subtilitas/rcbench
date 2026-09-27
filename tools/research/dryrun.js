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

const JL = { sha256: 'sha', rows: 7, lcsc: [], manifest: 'm', manifest_created: 't' }

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
    const cand = rank => ({ ...fake(schemas.P2.properties.functions.items.properties.shortlist.items), rank, part: `part${rank}` })
    if (role === 'P0') {
      Object.assign(data, { stop: false, held: [], checkout_head: 'deadbeef',
        jlcparts: { path: 'db', sha256: JL.sha256, sha256_ok: true, rows: JL.rows, missing_lcsc: [] },
        monostable: { commit: 'c', path: 'p', fetched: true },
        hosts: (opts.hosts || []).map(h => ({ host: h.host, url: '', client: '', http_status: 200, bytes: 1, status_marker: '', reachable: !(opts.down || []).includes(h.host), note: '' })) }, opts.p0 || {})
    }
    if (role === 'P2') data.functions = [{ ...data.functions[0], function: 'f1', shortlist: [1, 2, 3].map(cand) }]
    if (role === 'rerank') data.functions = [{ function: 'f1', ranking: [1, 2, 3].map(rank => ({ rank, part: `part${rank}`, reason: 'r' })), new_candidates: [], dropped_from_p3: [] }]
    if (role === 'P1-critic') {
      if (opts.critic === 'none') data.question_verdicts = []
      else data.question_verdicts = [0, 1].map(index => ({ index, verdict: 'confirmed', reason: 'r' }))
      data.added = []
    }
    if (role === 'P4') {
      const only = /replaces a refuted one; list only it: (\{.*\})/.exec(prompt)
      const part = only ? JSON.parse(only[1]).part : 'part1'
      const kind = o.label.includes('stock') ? 'stock' : 'datasheet'
      const refute = (opts.refute || []).includes(`${base}:${part}`)
      data.verifier = kind
      data.parts = (opts.omit || []).includes(base) ? [] : [{ function: 'f1', part, kind: 'first', verdict: refute ? 'refuted' : 'confirmed', checks: [], refutation: refute ? 'mock' : '' }]
      data.figures = (opts.refuteFigure || []).includes(base) && !only ? [{ figure: 'frames held', verdict: 'refuted', evidence: 'e' }] : []
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
    followup: opts.followup, first_v: 5 }
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

  // A refuted figure is adjudicated.
  r = await runTask('T2', { refuteFigure: ['P4-datasheet-R2'] })
  check(r.calls.some(c => c.startsWith('adjudicator-R2')), 'refuted figure: adjudicated')
  check(r.result.followUps.some(f => f.figure === 'frames held'), 'refuted figure that stands: listed')

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

  // P0: the script applies the stop and hold rules itself.
  r = await runTask('T2', { p0: { jlcparts: { path: 'db', sha256: 'other', sha256_ok: true, rows: 7, missing_lcsc: [] } } })
  check(r.result.summary.stopped === true, 'P0: a wrong database SHA-256 stops the task')
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

  // Follow-up plans.
  for (const [phases, n, want] of [['P1', 2, 6], ['P2-P4', 3, 16], ['P5-P6', 0, 5]]) {
    const { result } = await runTask('FU', { followup: { phases, round: 1, categories: ['R1', 'R2', 'R3'].slice(0, n), items: [] } })
    check(result.planned === want, `FU ${phases}: planned ${result.planned}, expected ${want}`)
  }

  console.log(failures ? `${failures} checks failed` : 'dry run: every check passed')
  process.exit(failures ? 1 : 0)
}

main()
