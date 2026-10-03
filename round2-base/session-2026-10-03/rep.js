const fs=require('fs')
const R='/home/claude/github/rcbench/tools/research/round1.js'
const src=fs.readFileSync(R,'utf8')
function grab(startRe, endMarker){const i=src.search(startRe);const j=src.indexOf(endMarker,i);return src.slice(i,j)}
const A=JSON.parse(fs.readFileSync('/home/claude/rcbench-research/round2/args-FU-2P56.json'))
const code=[
 src.slice(src.indexOf('const SOURCE_WORD'), src.indexOf('function readInRun')),
 src.slice(src.indexOf('const LEADING_TIME'), src.indexOf('// A reading of this task: taken on or after')),
 src.slice(src.indexOf('function unread('), src.indexOf('// A refutation that says there is none')),
 src.slice(src.indexOf('function notFound('), src.indexOf('// P0\'s reading of one host'))
].join('\n')
eval(code.replace(/^const /gm,'var ').replace(/^function /gm,'function '))
const dir='/home/claude/rcbench-research/round2/results/hardware/research/round2/FU-2P56/'
const p5=JSON.parse(fs.readFileSync(dir+'002-P5.json')).data
const cr=JSON.parse(fs.readFileSync(dir+'003-P5-critic.json')).data
const vs=new Map(cr.budget_verdicts.map(v=>[v.index,v]))
const budgets=p5.budgets.map((it,i)=>({...it,upheld:vs.has(i)?!!vs.get(i).holds:undefined}))
const optionsOf = n => ((A.q_options || {})[(/^(Q\d+) alternatives$/.exec(n) || [])[1]] || []).map(c => `${n}: ${c}`)
const chipsOf = n => ((A.p5_chips || {}).budgets || []).includes(n) ? ((A.p5_chips || {}).chips || []).map(c => `${n}: ${c}`) : []
const conditional=n=>(A.p5_conditional||[]).some(c=>(n===c||n.startsWith(c+': ')))
const counts = x => x.upheld === true && !readsNone(x.source) && !!isTime(x.read_at) && !notFound(x.value)
  && (!/^(not applicable|n\/a|does not apply)\b/i.test(String(x.value).trim()) || conditional(String(x.item || '')))
const why=x=>{const r=[];if(x.upheld!==true)r.push('upheld='+x.upheld);if(readsNone(x.source))r.push('source none');if(!isTime(x.read_at))r.push('read_at not leading time');if(notFound(x.value))r.push('value notFound');return r}
const unmet=(list,n,ok)=>{const rows=list.filter(x=>String(x.item||'')===n||String(x.item||'').startsWith(n+': '));return !rows.length||!rows.every(ok)}
const owed=(A.p5_budgets).flatMap(n=>[n,...optionsOf(n),...chipsOf(n)])
for(const n of owed){ if(unmet(budgets,n,counts)){const rows=budgets.filter(x=>x.item===n||x.item.startsWith(n+': '));console.log('MISSING',JSON.stringify(n),'rows:',rows.length?rows.map(r=>r.item+' ['+why(r).join(',')+']').join(' ; '):'NO ROW')}}
console.log('--- over'); budgets.filter(x=>x.upheld===true&&x.within!==true).forEach(x=>console.log(x.item))
console.log('--- rejected'); budgets.filter(x=>x.upheld===false).forEach(x=>console.log(x.item))
console.log('--- unsourced'); budgets.filter(x=>readsNone(x.source)||!isTime(x.read_at)).forEach(x=>console.log(x.item,'|',why(x).join(','),'|',JSON.stringify(x.read_at).slice(0,70)))
console.log('=== combos/assumptions')
const cv=new Map(cr.combination_verdicts.map(v=>[v.index,v])), av=new Map(cr.assumption_verdicts.map(v=>[v.index,v]))
p5.combinations.forEach((c,i)=>{const v=cv.get(i);console.log('C',i,JSON.stringify(c.name||c.item||Object.keys(c)).slice(0,80),'fits',c.fits,'holds',v&&v.holds,'| read_at',JSON.stringify(c.read_at).slice(0,50),'| src none',readsNone(c.source),'isTime',isTime(c.read_at),'|',v&&v.reason.slice(0,250))})
p5.assumptions.forEach((c,i)=>{const v=av.get(i);console.log('A',i,c.item,'holds',v&&v.holds,'| read_at',JSON.stringify(c.read_at).slice(0,50),'| srcNone',readsNone(c.source),'isTime',isTime(c.read_at),'notFound',notFound(c.value))})
const uc=p5.combinations.filter(x=>readsNone(x.source)||!isTime(x.read_at)).length, ub=budgets.filter(x=>readsNone(x.source)||!isTime(x.read_at)).length, ua=p5.assumptions.filter(x=>readsNone(x.source)||!isTime(x.read_at)||notFound(x.value)).length
console.log('unsourced c,b,a',uc,ub,ua,uc+ub+ua)
console.log('budget rows failing counts:',budgets.filter(x=>!counts(x)&&!(x.item==='absent IOVDD')).length)
const sm=budgets.filter(x=>!isTime(x.read_at)&&isTime(sourceTime(x.read_at))).length
console.log('budget rows rescued by sourceTime:',sm,'of',budgets.filter(x=>!isTime(x.read_at)).length)
console.log('--- not rescued by sourceTime')
budgets.filter(x=>!isTime(x.read_at)&&!isTime(sourceTime(x.read_at))).forEach(x=>console.log(x.item,'|',x.read_at.slice(0,60)))
console.log('--- first-ISO-anywhere')
console.log(budgets.filter(x=>!isTime(x.read_at)&&!/\d{4}-\d{2}-\d{2}/.test(x.read_at)).length)
