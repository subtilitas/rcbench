function partKeys(p, functionNames) {
  const rec = p && typeof p === 'object' ? p : { part: p }
  const fn = new Set(functionNames.map(n => String(n).toUpperCase().replace(/\s+/g, '')))
  const text = String(rec.part || '').toUpperCase()
  const own = String(rec.lcsc || '').trim().toUpperCase()
  const lcsc = [...(text.match(/\bC\d+\b/g) || []), ...(/^C\d+$/.test(own) ? [own] : [])].map(c => `lcsc:${c}`)
  const mpn = text.replace(/\([^)]*\)/g, ' ').split(/;|,\s+|\s+\/\s+|\s+AND\s+/)
    .map(x => x.replace(/\s+/g, '')).filter(x => x && !fn.has(x)).map(x => `mpn:${x}`)
  return new Set([...mpn, ...lcsc])
}
function samePart(a, b, functionNames) {
  if (typeof a === 'string' && a === b) return true
  const kb = partKeys(b, functionNames)
  return [...partKeys(a, functionNames)].some(k => kb.has(k))
}
const names=['accelerometer','optical index','magnetic pickup','phase-wire clip','encoder input','encoder decode','rotation from ESC','rotation inputs','BENCH rpm source','servo measured position']
const horn='a magnetic angle sensor at the horn'
for (const c of ['AS5600-ASOT (C499458), AS5600L-ASOM (C2153666)','TLE5012BE1000 (C123083), TLI5012BE1000 (C190955)','A1335LLETR-T (C2655192)','RP2354B'])
  console.log(c,'=>',samePart({part:c},horn,names), [...partKeys(horn,names)], [...partKeys(c,names)].slice(0,3))
console.log(samePart('TLV7011DBVR, TLV7031DBVR, TLV1811DBVR','TLV7011DBVR',names))
console.log(samePart({part:'VCND2045X02, VCND2045SLX02, VCND2040X02, VCND2040SLX02'},'VCND2045X02, VCND2045SLX02, VCND2040X02, VCND2040SLX02',names))
