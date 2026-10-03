const fs = require('fs')
const src = fs.readFileSync(process.argv[2], 'utf8')
const start = src.indexOf('const LEADING_TIME'), end = src.indexOf('const p5Time')
const A = { date: '2026-10-03' }
eval(src.slice(start, src.indexOf('\n', end)).replace(/^const /gm, 'var '))
const d = JSON.parse(fs.readFileSync(process.argv[3], 'utf8')); const p = d.data || d
let before = 0, after = 0; const left = []
for (const k of ['combinations', 'budgets', 'assumptions']) for (const x of p[k] || []) {
  if (!isTime(x.read_at)) before++
  if (!p5Time(x.read_at)) { after++; left.push(`${k}: ${x.item || ''} | ${x.read_at.slice(0, 90)}`) }
}
console.log(`rows without a reading time: ${before} by isTime, ${after} by p5Time`); left.forEach(l => console.log('  ' + l))
