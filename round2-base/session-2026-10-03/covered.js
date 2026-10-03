const A={date:'2026-10-02'}
const LEADING_TIME = /^\d{4}-\d{2}-\d{2}(?:[T ]\d{2}:\d{2}(?::\d{2}(?:\.\d+)?)?(?:Z|[+-]\d{2}:?\d{2})?)?(?=$|[\s,;()]|-\d{2}:\d{2})/
function calendarDay(day) {
  const ms = Date.parse(day)
  // A date the calendar has: 2026-02-31 is not one.
  return Number.isFinite(ms) && new Date(ms).toISOString().slice(0, 10) === day ? ms : NaN
}
function leadingTime(text) {
  const token = (String(text || '').trim().match(LEADING_TIME) || [])[0]
  if (!token || !Number.isFinite(calendarDay(token.slice(0, 10)))) return NaN
  let t = token.replace(' ', 'T')
  if (/T\d{2}:\d{2}/.test(t) && !/(Z|[+-]\d{2}:?\d{2})$/.test(t)) t += 'Z'
  return Date.parse(t)
}
function isTime(text) {
  return Number.isFinite(leadingTime(text))
}

// A reading of this task: taken on or after the day the session prepared it,
// not copied from an earlier return or the parts database, and no later than
// the next day, for a run that passes midnight. Every date the text gives
// counts. The script has no clock.
function readInRun(text) {
  const start = Date.parse(A.date)
  const inRun = ms => ms >= start && ms < start + 2 * 864e5
  return inRun(leadingTime(text)) && (String(text).match(/\d{4}-\d{2}-\d{2}/g) || []).every(day => inRun(calendarDay(day)))
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
function altOf(c) {
  return c && c.second_source_route === 'alternate' && c.second_source_part && c.second_source_part !== c.part ? c.second_source_part : ''
}

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

// Readings the lifecycle table records without a gate: S5 records a
// longevity commitment and does not require one, and market introduction is
// recorded and flagged under 12 months. A reading of either that the
// verifier could not take, written "not read: REASON", is recorded as that
// and listed for the owner, as an unread manufacturer status is; it neither
// passes nor fails.
const RECORDED_ONLY = new Set(['longevity commitment', 'market introduction'])
// The reason is text of its own: "not read: none", "not read: N/A" or
// "not read: -" gives none.
function notReadWithReason(text) {
  const t = String(text || '').trim()
  const m = /^not read\s*[:;,(-]\s*/i.exec(t)
  const reason = m ? t.slice(m[0].length).replace(/[\s.)]+$/, '') : ''
  return !!reason && !readsNone(reason) && !/^(unknown|not known|not stated)$/i.test(reason)
}

// A lifecycle reading of the stock verifier that is recorded as not read.
// A datasheet check of the same name is a requirement, not this reading.
const recordedUnread = (c, verifier) => verifier === 'stock' && RECORDED_ONLY.has(c.figure) && notReadWithReason(c.read)

// A check written as not read, as none where a value exists to be read, or
// without its source or a reading time of this task, shows nothing.
function shown(c, verifier) {
  const read = recordedUnread(c, verifier) ? true
    : !(MAY_READ_NONE.has(c.figure) ? unread(c.read) : readsNone(c.read))
  return read && !readsNone(c.source) && readInRun(c.read_at)
}

function covered(v, cand) {
  // A part with no record cannot show its gate evidence, and a datasheet
  // confirmation with nothing to check shows none.
  if (!cand) return false
  const req = requiredChecks(v.verifier, cand, v.kind)
  if (v.verifier === 'datasheet' && !req.length) return false
  const have = new Set((v.checks || []).filter(c => shown(c, v.verifier)).map(c => c.figure))
  // Rule 6: a part the owner holds passes rule 4 on the held quantity, in
  // place of the live stock and presale.
  const held = v.verifier === 'stock' && onBoard(cand) && Number(cand.held) > 0 && have.has('held quantity')
  return req.filter(n => !(held && (n === 'stock' || n === 'presale'))).every(n => have.has(n))
}

// A part's status after a later ledger entry: a standing refutation is
function onBoard(c) {
  return !!(c && /^C\d+$/.test(c.lcsc || ''))
}
const fs=require('fs')
const D='/home/claude/rcbench-research/round2/results/hardware/research/round2/FU-2C/'
const p2=JSON.parse(fs.readFileSync(D+'005-P2-R11.json')).data
const st=JSON.parse(fs.readFileSync(D+'016-P4-stock-R11.json')).data.parts
const ds=JSON.parse(fs.readFileSync(D+'019-P4-datasheet-R11.json')).data.parts
const cand=(fn,part)=>{const f=p2.functions.find(x=>x.function===fn);return f.shortlist.find(c=>c.part===part)}
for (const [kind,parts] of [['stock',st],['datasheet',ds]]) for (const p of parts) {
  const c=cand(p.function,p.part)
  const v={verifier:kind,kind:p.kind,checks:p.checks}
  const req=requiredChecks(kind,c,p.kind)
  const have=new Set(p.checks.filter(x=>shown(x,kind)).map(x=>x.figure))
  console.log(kind.padEnd(9),p.function.padEnd(18),p.part.padEnd(18),'covered=',covered(v,c),'missing=',JSON.stringify(req.filter(n=>!have.has(n))))
}
