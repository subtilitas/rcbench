const path = require('path')
process.chdir(process.argv[2])
const src = require('fs').readFileSync('tools/research/dryrun.js', 'utf8')
// run only our case by patching the main: find runTask definition and call it
const m = src.replace(/\nmain\(\)[\s\S]*$/, '\n')
