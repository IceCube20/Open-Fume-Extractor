const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
const assert=require('node:assert/strict');
const script=fs.readFileSync(path.join(__dirname,'../ota-mcu-url-recovery.js'),'utf8');
(async()=>{
  for(const ok of [true,false]){
    const calls=[];
    const window={fetch:async function(url,options){
      assert.equal(this,window);
      calls.push({url,options});
      return {ok,status:ok?200:403,text:async()=>ok?'MCU update aborted':'bad csrf token'};
    }};
    const original=window.fetch;
    const run=vm.runInNewContext(script,{window,console:{log(){}}});
    if(!ok){
      await assert.rejects(run,/HTTP 403/);
      assert.equal(window.fetch,original,'Restore prior fetch on rejected abort');
      continue;
    }
    await run;
    assert.equal(calls.length,1,'Installing workaround never sends an END/commit');
    assert.equal(calls[0].url,'/update/master/abort');
    const options={method:'POST',headers:{'X-CSRF-Token':'unchanged'},credentials:'same-origin'};
    await window.fetch('/update/MCU/end',options);
    assert.equal(calls.at(-1).url,'/update/master/end');
    assert.equal(calls.at(-1).options,options,'Preserve authentication and CSRF behavior');
    await window.fetch('/update/MCU/abort?test=1',options);
    assert.equal(calls.at(-1).url,'/update/master/abort?test=1');
    for(const url of ['/state','/update/module/end','/update/MCU/other','https://example.test/update/MCU/end']){
      await window.fetch(url,options);assert.equal(calls.at(-1).url,url);
    }
    const request={url:'/update/MCU/end'};
    await window.fetch(request,options);assert.equal(calls.at(-1).url,request);
  }
  console.log('PASS: temporary OTA route recovery preserves fetch/auth/CSRF, aborts only, leaves other requests alone and restores on failure');
})().catch(error=>{console.error(error);process.exitCode=1;});
