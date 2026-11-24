const fs=require('node:fs'),path=require('node:path'),http=require('node:http'),{once}=require('node:events'),assert=require('node:assert/strict'),{chromium}=require('playwright');
const src=path.resolve(__dirname,'../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
const ui=fs.readFileSync(path.join(src,'WebUsbStorageUi.h'),'utf8').match(/R"USBJS\(([\s\S]*?)\)USBJS"/)[1];
const backupParser=fs.readFileSync(path.join(src,'WebConfig.inc.h'),'utf8').match(/function setBackupStatus[\s\S]*?(?=async function submitBackupImport)/)[0];
const c6Ui=fs.readFileSync(path.join(src,'HostedC6WebUi.h'),'utf8').replace(/\)C6"\r?\n#define OFE_C6_UI_TAIL R"C6\(/g,'').match(/<script>([\s\S]*?)<\/script>/)[1];
const largeSize=8*1024*1024+37,maxSize=2147483647;
const state={detected:true,mounted:true,phase:'idle',message:'Ready',generation:1,token:0,transfer_limit:maxSize,buffered_read:false,directories:[{index:0,path:''}],files:[
 {index:0,name:'large.dat',path:'large.dat',size:largeSize},
 {index:1,name:'firmware.bin',path:'firmware.bin',size:largeSize},
 {index:2,name:'backup.json',path:'backup.json',size:largeSize},
 {index:3,name:'small.bin',path:'small.bin',size:4}
]};
Object.assign(state,{drive:0,drive_instance:101,drive_limit:4,drives:[{index:0,instance:101,label:'USB 1',connected:true,selected:true,mounted:true,capacity_bytes:16000000000}]});
const calls=[],serverErrors=[];let selectedFile,write=null,downloadGate=null,nativeActive=false,finishDownload,chunkGate=null,ackChunk,rejectBegin=false,invalidBackup=false,c6Lan=true;
const server=http.createServer((req,res)=>{void (async()=>{
 const u=new URL(req.url,'http://localhost');calls.push(u.pathname);
 const json=j=>{res.setHeader('Content-Type','application/json');res.end(JSON.stringify(j));};
 const fail=text=>{res.statusCode=409;res.end(text);};
 if(u.pathname==='/usb/status'){
  // The device HTTP task cannot serve status until its native attachment finishes.
  if(nativeActive&&downloadGate)await downloadGate;return json(state);
 }
 if(u.pathname==='/usb/file'){
  assert.equal(u.searchParams.get('token'),'71');assert(selectedFile);
  const native=!!downloadGate;nativeActive=native;
  res.setHeader('Content-Type','application/octet-stream');res.setHeader('Content-Disposition',`attachment; filename="${selectedFile.name}"`);res.setHeader('Content-Length',selectedFile.size);
  const prefix=selectedFile.index===2?Buffer.from(invalidBackup?'invalid':'{"product":"Open Fume Extractor","schema":1,"master":{}}'):Buffer.alloc(0);
  let offset=0;while(offset<selectedFile.size){const chunk=Buffer.alloc(Math.min(8192,selectedFile.size-offset),selectedFile.index===2?32:17);if(offset===0)prefix.copy(chunk);if(!res.write(chunk))await once(res,'drain');offset+=chunk.length;if(native&&offset===8192&&downloadGate)await downloadGate;}
  res.end();state.phase='idle';state.token=0;nativeActive=false;return;
 }
 const parts=[];for await(const part of req)parts.push(part);const body=Buffer.concat(parts),p=new URLSearchParams(body.toString());
 if(['/usb/command','/usb/manage','/usb/write/begin'].includes(u.pathname)){assert.equal(Number(p.get('drive')),state.drive);assert.equal(Number(p.get('drive_instance')),state.drive_instance);assert.equal(Number(p.get('generation')),state.generation);}
 if(u.pathname==='/usb/command'){
  assert.equal(p.get('action'),'read');assert.equal(Number(p.get('generation')),state.generation);selectedFile=state.files.find(f=>f.index===Number(p.get('index')));assert(selectedFile);state.phase='ready';state.token=71;return json({ok:true});
 }
 if(u.pathname==='/usb/release')return fail('Session already auto-closed');
 if(u.pathname==='/usb/write/begin'){
  if(rejectBegin){assert.equal(Number(p.get('size')),maxSize);return fail('boundary accepted');}
  assert.equal(Number(p.get('directory')),0);assert.equal(Number(p.get('generation')),state.generation);
  write={name:p.get('name'),size:Number(p.get('size')),received:0,chunks:0};state.phase='receiving';state.token=72;return json({ok:true,token:72});
 }
 if(u.pathname==='/usb/write/chunk'){
  assert.equal(u.searchParams.get('token'),'72');assert.equal(Number(u.searchParams.get('offset')),write.received);
  const data=await new Request(u,{method:'POST',headers:req.headers,body}).formData(),chunk=data.get('chunk');
  assert(chunk.size>0&&chunk.size<=8192);assert.equal(chunk.size,Math.min(8192,write.size-write.received));
  if(chunkGate)await chunkGate;write.received+=chunk.size;write.chunks++;return json({ok:true});
 }
 if(u.pathname==='/usb/write/commit'){
  assert.equal(write.received,write.size);assert.equal(write.chunks,Math.ceil(write.size/8192));state.phase='idle';state.token=0;state.generation++;return json({ok:true});
 }
 if(u.pathname==='/update/c6/status')return json({phase:0,lan:c6Lan,host:0x020700,expected:0x020700,current:0x020600,error:'OK',received:0,written:0,verification:0});
 if(u.pathname.startsWith('/update/'))throw Error('Unexpected install/staging request: '+u.pathname);
 res.setHeader('Content-Type','text/html');res.end('<html></html>');
 })().catch(e=>{serverErrors.push(e);res.statusCode=500;res.end(String(e));});});
(async()=>{
 server.listen(0,'127.0.0.1');await once(server,'listening');const base=`http://127.0.0.1:${server.address().port}`;
 const browser=await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});
 try{
  const page=await browser.newPage({acceptDownloads:true}),errors=[];page.on('pageerror',e=>errors.push(e.message));
  async function mount(purpose,extra=''){
   await page.goto(base);await page.setContent(`<select id="lang_sel"><option value="en">en</option></select><div id="usb-storage" data-usb-purpose="${purpose}"></div>${extra}`);await page.addScriptTag({content:ui});await page.waitForSelector('.usb-row');
  }
  const button=name=>page.getByRole('button',{name,exact:true}),row=name=>page.locator('.usb-row').filter({has:page.locator('.usb-path',{hasText:name})});
  await mount('update','<input id="master_fw_file" type="file"><input id="module_fw_file" type="file">');
  await page.evaluate(()=>{
   window.fullFileObjects=0;window.fileFetches=0;const fetch=window.fetch;
   window.fetch=(url,options)=>{if(String(url).startsWith('/usb/file')){window.fileFetches++;throw Error('Native download must not fetch a blob');}return fetch(url,options);};
   window.File=new Proxy(File,{construct(){window.fullFileObjects++;throw Error('Native download must not construct a File');}});
   URL.createObjectURL=()=>{window.fullFileObjects++;throw Error('Native download must not create an object URL');};
  });
  downloadGate=new Promise(resolve=>finishDownload=resolve);await row('large.dat').click();
  const downloadEvent=page.waitForEvent('download');await button('Download').click();const download=await downloadEvent;
  assert.equal(download.url(),base+'/usb/file?token=71');assert.equal(download.suggestedFilename(),'large.dat');
  await page.waitForFunction(()=>document.getElementById('usb-status').textContent==='Download started: large.dat');
  assert(await page.locator('.usb-progress').isHidden());assert(await button('Download').isDisabled());assert(await button('Upload file').isDisabled());
  assert.equal(await page.evaluate(()=>window.ofeUsbSave(new Blob(['busy']),'busy.txt')),false);
  await page.clock.install();await page.clock.fastForward(121000);
  assert(await button('Download').isDisabled());assert.equal(state.token,71);assert(!calls.includes('/usb/release'));
  assert.deepEqual(await page.evaluate(()=>[window.fileFetches,window.fullFileObjects]),[0,0]);
  finishDownload();downloadGate=null;assert.equal(await download.failure(),null);assert.equal(fs.statSync(await download.path()).size,largeSize);
  await page.clock.resume();await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));await page.waitForFunction(()=>!document.querySelector('.usb-actions button').disabled);
  assert.equal(state.phase,'idle');assert(!calls.includes('/usb/release'));
  // A lost attachment request leaves Ready until the worker lease expires.
  await page.evaluate(()=>{HTMLAnchorElement.prototype.click=function(){};});
  for(const recovery of ['lease','disconnect','error']){
   await row('large.dat').click();await button('Download').click();await page.waitForFunction(()=>document.querySelector('.usb-progress').hidden);
   assert.equal(state.phase,'ready');assert(await button('Download').isDisabled());
   if(recovery==='disconnect'){
    state.mounted=false;await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));await page.waitForFunction(()=>document.querySelector('#usb-connection').textContent==='Detected, not mounted');state.mounted=true;
   }
   state.phase=recovery==='error'?'error':'idle';state.token=0;state.message='Attachment could not start';
   await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));await page.waitForFunction(()=>!document.querySelector('.usb-global button').disabled);
   if(recovery==='error'){assert.equal(await page.locator('#usb-status').textContent(),'Attachment could not start');assert.equal(await page.locator('#usb-status').getAttribute('data-error'),'true');}
   state.phase='idle';state.token=0;
  }
  assert(!calls.includes('/usb/release'));
  // Reload to remove the native-download memory guards before consumer-only reads.
  await mount('update','<input id="master_fw_file" type="file"><input id="module_fw_file" type="file">');
  await page.evaluate(()=>{window.firmwareChanges=[];for(const id of ['master_fw_file','module_fw_file'])document.getElementById(id).addEventListener('change',e=>window.firmwareChanges.push([id,e.target.files[0].size]));});
  await row('firmware.bin').click();await button('Select firmware').click();await page.waitForFunction(()=>window.firmwareChanges.length===1);
  await page.waitForFunction(()=>!document.querySelector('.usb-actions button').disabled);await page.locator('#usb-target').selectOption('module');await button('Select firmware').click();await page.waitForFunction(()=>window.firmwareChanges.length===2);
  assert.deepEqual(await page.evaluate(()=>window.firmwareChanges),[['master_fw_file',largeSize],['module_fw_file',largeSize]]);
  await page.waitForFunction(()=>!document.querySelector('.usb-actions button').disabled);
  // A delayed durable chunk acknowledgement must block the next request and progress.
  delete state.transfer_limit;chunkGate=new Promise(resolve=>ackChunk=resolve);
  await page.evaluate(n=>{window.exportResult=null;void window.ofeUsbSave(new Blob([new Uint8Array(n)]),'large-export.dat').then(r=>window.exportResult=r);},largeSize);
  const dialog=page.getByRole('dialog'),firstChunk=page.waitForRequest('**/usb/write/chunk?*');await dialog.getByRole('button',{name:'Save',exact:true}).click();
  await firstChunk;assert.equal(write.received,0);assert.equal(write.chunks,0);
  assert.equal(await page.getByRole('progressbar').getAttribute('aria-valuenow'),'0');ackChunk();chunkGate=null;
  await page.waitForFunction(()=>window.exportResult===true,null,{timeout:60000});assert.equal(write.received,largeSize);
  state.transfer_limit=maxSize;
  await button('Upload file').click();await dialog.locator('#usb-upload').setInputFiles({name:'large-upload.dat',mimeType:'application/octet-stream',buffer:Buffer.alloc(largeSize)});await dialog.getByRole('button',{name:'Upload',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('#usb-status').textContent==='Saved to USB: large-upload.dat',null,{timeout:60000});assert.equal(write.received,largeSize);
  // Check the signed VFS boundary without allocating a multi-gigabyte Blob.
  await page.evaluate(n=>{window.exportResult=null;void window.ofeUsbSave({size:n},'over-limit.dat').then(r=>window.exportResult=r);},maxSize+1);
  await dialog.getByRole('button',{name:'Save',exact:true}).click();assert((await dialog.locator('.usb-dialog-error').textContent()).includes('filesystem maximum'));await dialog.getByRole('button',{name:'Cancel',exact:true}).click();await page.waitForFunction(()=>window.exportResult===false);
  rejectBegin=true;await page.evaluate(n=>{window.exportResult=null;void window.ofeUsbSave({size:n},'at-limit.dat').then(r=>window.exportResult=r);},maxSize);await dialog.getByRole('button',{name:'Save',exact:true}).click();await page.waitForFunction(()=>window.exportResult===false);assert((await page.locator('#usb-status').textContent()).includes('boundary accepted'));rejectBegin=false;
  await mount('config','<input id="backup_json"><div id="backup_status"></div><button id="backup_import_btn" disabled></button>');await page.addScriptTag({content:backupParser});
  await row('backup.json').click();await button('Select backup').click();await page.waitForFunction(()=>!document.getElementById('backup_import_btn').disabled);assert.equal(await page.locator('#backup_json').inputValue().then(s=>s.length),largeSize);
  await page.waitForFunction(()=>!document.querySelector('.usb-actions button').disabled);invalidBackup=true;await button('Select backup').click();await page.waitForFunction(()=>document.getElementById('backup_status').textContent.includes('Backup ungültig'));assert(await page.locator('#backup_import_btn').isDisabled());invalidBackup=false;
  const c6Fields=['current','state','bar','catalog_state'].map(id=>`<div id="c6_${id}"></div>`).join('')+['download','cancel','install','catalog_check'].map(id=>`<button id="c6_${id}"></button>`).join('')+'<input id="c6_file" type="file"><input id="c6_recovery" type="checkbox"><input id="c6_legacy" type="checkbox">';
  await mount('update',c6Fields);await page.evaluate(()=>{window.OFE_C6={size:4,expected:0x020700,de:false,csrf:'test',catalog:''};window.OFE_C6_CATALOG={load:async()=>{throw Error('offline')}};});await page.addScriptTag({content:c6Ui});
  await page.waitForFunction(()=>!document.getElementById('c6_file').disabled);await row('firmware.bin').click();await page.locator('#usb-target').selectOption('c6');await button('Select firmware').click();await page.waitForFunction(()=>document.getElementById('c6_state').textContent==='Incorrect file size');assert(!calls.includes('/update/c6/begin'));
  await page.waitForFunction(()=>!document.querySelector('.usb-actions button').disabled);c6Lan=false;await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));await page.waitForFunction(()=>document.getElementById('c6_file').disabled);await page.locator('#usb-target').dispatchEvent('change');assert(await button('Select firmware').isDisabled());assert(await page.locator('#c6_install').isDisabled());
  assert.deepEqual(errors,[]);assert.deepEqual(serverErrors,[]);await page.close();
  console.log('PASS: native streamed >6 MiB download without browser full-file objects or early release, blocked status past 120s and lost-start recovery, >6 MiB upload/export in durable 8192-byte chunks, dynamic/filesystem limits, large firmware/backup validation handoffs and C6 guards');
 }finally{finishDownload?.();ackChunk?.();await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(()=>{server.closeAllConnections();server.close();});
