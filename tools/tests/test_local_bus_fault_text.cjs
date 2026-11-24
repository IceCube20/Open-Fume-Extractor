'use strict';
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync(path.join(__dirname,'../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebStatus.inc.h'),'utf8');
const code=source.match(/^function faultText\([^\n]+/m)[0];
for(const lang of ['de','en']) {
 const ctx=vm.createContext({uiLang:lang,hx:n=>'0x'+n.toString(16)});vm.runInContext(code,ctx);
 for(const type of [7,8]) {
  for(const [bit,de,en] of [[0x800,'Antwort-Timeouts','response timeouts'],[0x1000,'Prüfsummenfehler','checksum errors'],[0x2000,'Modbus-Exceptions','Modbus exceptions']]) {
   const result=ctx.faultText(bit,type);assert(result.includes(lang==='de'?de:en));
   assert(!result.includes(lang==='de'?'Drehzahl':'speed'));
  }
  const combined=ctx.faultText(0x3801,type);assert(combined.includes(lang==='de'?'Lokaler':'Local'));
  assert(combined.includes('Modbus'));assert(combined.includes('0x3801'));
 }
 assert(!ctx.faultText(0x800,5).includes('Modbus'));
}
console.log('PASS: production MCU browser fault labels: DE/EN, bridge-only diagnostic bits and combined faults');
