'use strict';
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync(path.join(__dirname,'../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/HostedC6UpstreamUi.h'),'utf8').match(/<script>([\s\S]*?)<\/script>/)[1];
const commit='a'.repeat(40),blob='b'.repeat(40);
const entry=(version='2.13.1')=>({name:`esp32c6-v${version}.bin`,path:`hosted/esp32c6-v${version}.bin`,type:'file',size:1244160,sha:blob,download_url:'https://untrusted.invalid/ignored'});
async function run(list,ref={object:{sha:commit,type:'commit'}},fail=false){
 const calls=[],window={};
 vm.runInNewContext(source,{window,AbortSignal,fetch:async(url,options)=>{calls.push({url,options});return {ok:!fail,status:fail?403:200,text:async()=>JSON.stringify(url.includes('/git/ref/')?ref:list)}}});
 const result=await window.OFE_C6_UPSTREAM.load();return {result,calls};
}
(async()=>{
 const {result,calls}=await run([entry('2.12.13'),entry('2.13.1'),entry('2.0.13')]);
 assert.deepEqual(Array.from(result,r=>r.expected),[0x020d01,0x020c0d]);
 assert.equal(result[0].url,`https://raw.githubusercontent.com/espressif/arduino-esp32/${commit}/hosted/esp32c6-v2.13.1.bin`);
 assert.equal(calls[1].url,`https://api.github.com/repos/espressif/arduino-esp32/contents/hosted?ref=${commit}`);
 for(const {options} of calls){assert.equal(options.credentials,'omit');assert.equal(options.redirect,'error');assert(!options.headers.Authorization)}
 for(const change of [{sha:'x'},{size:2097153},{path:'../../evil'},{type:'dir'},{name:'esp32c6-v2.999.1.bin'},{name:'esp32c6-v02.13.1.bin'}])await assert.rejects(run([{...entry(),...change}]));
 await assert.rejects(run([entry(),entry()]));await assert.rejects(run([]));await assert.rejects(run([entry()],{object:{sha:'main',type:'commit'}}));await assert.rejects(run([entry()],undefined,true));
 console.log('PASS: upstream C6 discovery pins immutable official URLs; older targets, sort, duplicate/size/version/path/hash checks, rate-limit failure and no credentials');
})().catch(e=>{console.error(e);process.exitCode=1});
