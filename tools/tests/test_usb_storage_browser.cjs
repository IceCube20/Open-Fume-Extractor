const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{chromium}=require('playwright');
const root=path.resolve(__dirname,'../..'),src=path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
const ui=fs.readFileSync(path.join(src,'WebUsbStorageUi.h'),'utf8').match(/R"USBJS\(([\s\S]*?)\)USBJS"/)[1];
const icons=fs.readFileSync(path.join(src,'WebTransportIcons.h'),'utf8');
const header=icons.match(/R"ICON\(([\s\S]*?)\)ICON"/)[1],shared=icons.match(/R"ICONS\(([\s\S]*?)\)ICONS"/)[1];
const css=fs.readFileSync(path.join(src,'WebShell.inc.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1]+fs.readFileSync(path.join(src,'WebFormStyles.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1];
const out=path.join(__dirname,'build-checks/usb-storage-ui');fs.mkdirSync(out,{recursive:true});
(async()=>{const browser=await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});try{
 for(const lang of ['de','en'])for(const width of [320,390,1440])for(const purpose of ['config','update','trace']){
  const page=await browser.newPage({viewport:{width,height:1000}}),errors=[],calls=[];let writeDelay=0,writeFailure=false;
  const state={developer_mode:false,detected:true,mounted:true,phase:'idle',message:'Ready',generation:7,token:0,transfer_limit:2147483647,buffered_read:false,files:[
   {index:0,name:purpose==='config'?'backup.json':'firmware.bin',size:4,folder:false},
   {index:1,name:'open-fume-extractor-JBC-FAE-0X10-2026-11-15-long-filename.csv',size:12290,folder:true},
   {index:2,name:'Weller_Zero_Smog_Shield_Pro_v6.ofeprofile',size:3420,folder:true},
   {index:3,name:'diagnostics.json',size:2048,folder:false},
   {index:4,name:'open-fume-extractor-backup-2026-11-15.json',size:8080,folder:true}
  ]};
  Object.assign(state,{drive:0,drive_instance:101,drive_limit:4,drives:[{index:0,instance:101,label:'USB 1',connected:true,selected:true,mounted:true,capacity_bytes:16000000000}]});
  state.diagnostics={host_ready:true,observer_ready:true,msc_ready:true,root_connected:true,root_enabled:true,root_powered:true,enumerated:2,allocated:2,configured:2,host_error:0,observer_error:0,msc_error:0,event_error:0,devices:[{address:1,vendor:0x1A86,product:0x8091,class:9,hub:true,storage:false,bot:false},{address:2,vendor:0x0781,product:0x5581,class:0,hub:false,storage:true,bot:true}]};
  page.on('pageerror',e=>errors.push(e.message));
  await page.route('http://ofe.test/**',async route=>{
   const u=new URL(route.request().url()),body=route.request().postData()||'';calls.push(u.pathname);
   const reply=j=>route.fulfill({contentType:'application/json',body:JSON.stringify(j)});
   if(['/usb/command','/usb/manage','/usb/write/begin'].includes(u.pathname)){const p=new URLSearchParams(body);assert.equal(Number(p.get('drive')),state.drive);assert.equal(Number(p.get('drive_instance')),state.drive_instance);assert.equal(Number(p.get('generation')),state.generation);}
   if(u.pathname==='/display-link/ble')return reply({active:false,network_transport:'ethernet',usb_detected:state.detected,usb_mounted:state.mounted});
   if(u.pathname==='/usb/status')return reply(state);
   if(u.pathname==='/usb/command'){
    const p=new URLSearchParams(body);if(p.get('action')==='read'){assert.equal(p.get('generation'),String(state.generation));assert(state.files.some(f=>f.index===Number(p.get('index'))));state.phase='ready';state.token=45;}
    if(p.get('action')==='eject'){state.detected=false;state.mounted=false;}
    return reply({ok:true});
   }
   if(u.pathname==='/usb/file'){state.phase='idle';state.token=0;return route.fulfill({contentType:'application/octet-stream',body:'test'});}
   if(u.pathname==='/usb/release'){if(!state.token)return route.fulfill({status:409,body:'Session already closed'});state.phase='idle';state.token=0;return reply({ok:true});}
   if(u.pathname==='/usb/write/begin'){state.phase='receiving';state.token=46;return reply({ok:true,token:46});}
   if(u.pathname==='/usb/write/chunk'){if(writeDelay)await new Promise(r=>setTimeout(r,writeDelay));if(writeFailure)return route.fulfill({status:409,body:'USB write failed'});return reply({ok:true});}
   if(u.pathname==='/usb/write/commit'){state.phase='idle';state.token=0;return reply({ok:true});}
   if(u.pathname==='/config/export')return route.fulfill({contentType:'application/json',body:'{"product":"Open Fume Extractor","schema":1,"master":{}}'});
   return route.fulfill({contentType:'text/html',body:'<html></html>'});
  });
  await page.goto('http://ofe.test/');
  await page.setContent(`<style>:root{--ofe-font-ui:Arial,sans-serif}${css}</style><header><div class="brand"><div>Open Fume Extractor</div>${header}</div><select id="lang_sel" style="width:100px"><option value="${lang}">${lang}</option></select></header><main><input id="master_fw_file" type="file"><input id="module_fw_file" type="file"><button id="backup_import_btn"></button><section class="panel" id="usb-storage" data-usb-purpose="${purpose}"></section></main>`);
  await page.evaluate(()=>{window.autoInstall=0;window.loadedBackup=0;window.loadBackupFile=async()=>{window.loadedBackup++};window.diagDownload=(fmt,usb)=>window.ofeUsbSave(new Blob(['trace']),`open-fume-extractor-JBC-FAE-0X10.${fmt}`);});
  await page.addScriptTag({content:ui});
  await page.addScriptTag({content:shared.match(/<script>([\s\S]*?)<\/script>/)[1]});
  await page.addStyleTag({content:shared.match(/<style>([\s\S]*?)<\/style>/)[1]});
  await page.waitForFunction(()=>document.querySelector('#usb-file .usb-row'));
  await page.getByRole('button',{name:lang==='de'?'Alle Ordner aufklappen':'Expand all folders',exact:true}).click();
  assert(await page.locator('#usb-host-status').isHidden());
  assert(await page.locator('#usb-host-devices').isHidden());
  assert.equal(await page.locator('#usb-host-status').textContent(),'');
  assert.equal(await page.locator('#usb-host-devices li').count(),0);
  const download=page.getByRole('button',{name:lang==='de'?'Herunterladen':'Download',exact:true});
  assert(await download.isDisabled());
  assert.equal(await page.locator('#usb-file .usb-row').count(),5);
  const selectedName=state.files[0].name;
  await page.locator('.usb-row').filter({hasText:selectedName}).click();
  assert(!(await download.isDisabled()));
  // Polls must not replace focused rows or move a selection to a reused index.
  await page.locator('.usb-row[aria-pressed=true]').focus();
  await page.evaluate(()=>{window.savedUsbRow=document.activeElement;document.dispatchEvent(new Event('visibilitychange'));});
  await page.waitForTimeout(100);
  assert(await page.evaluate(()=>document.activeElement===window.savedUsbRow));
  assert.equal(await page.locator('.usb-row[aria-pressed=true] .usb-path').textContent(),selectedName);
  await page.locator('#usb-search').fill('not-present');
  assert.equal(await page.locator('.usb-row').count(),0);
  assert((await page.locator('.usb-empty').textContent()).includes(lang==='de'?'Keine passenden':'No matching'));
  await page.getByRole('button',{name:lang==='de'?'Suche leeren':'Clear search',exact:true}).click();
  assert.equal(await page.locator('.usb-row').count(),5);
  await page.locator('#usb-kind').selectOption('profile');
  assert.equal(await page.locator('.usb-row').count(),1);
  await page.locator('.usb-row').click();
  if(purpose==='update')assert(await page.getByRole('button',{name:lang==='de'?'Firmware auswählen':'Select firmware',exact:true}).isHidden());
  if(purpose==='config')assert(await page.getByRole('button',{name:lang==='de'?'Backup auswählen':'Select backup',exact:true}).isHidden());
  await page.locator('#usb-kind').selectOption('all');
  await page.locator('#usb-sort').selectOption('size-desc');
  assert((await page.locator('.usb-row').first().textContent()).includes('.csv'));
  await page.locator('#usb-sort').selectOption('name');
  await page.locator('.usb-row').filter({hasText:selectedName}).click();
  state.developer_mode=true;
  await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));
  await page.waitForFunction(()=>!document.querySelector('#usb-host-status').hidden);
  assert((await page.locator('#usb-host-status').textContent()).includes(lang==='de'?'USB-Objekte: 2':'USB objects: 2'));
  assert((await page.locator('#usb-host-status').textContent()).includes(lang==='de'?'Konfiguriert: 2':'Configured: 2'));
  assert.equal(await page.locator('#usb-host-devices li').count(),2);
  assert((await page.locator('#usb-host-devices li').first().textContent()).includes('1A86:8091 · Hub'));
  assert((await page.locator('#usb-host-devices li').last().textContent()).includes('SCSI/BOT'));
  await page.waitForFunction(()=>document.querySelector('.transport-usb:not([hidden])'));
  assert(await page.locator('.transport-usb').evaluate(e=>e.classList.contains('is-on')));
  const selectLabel=purpose==='update'?(lang==='de'?'Firmware auswählen':'Select firmware'):(lang==='de'?'Backup auswählen':'Select backup');
  if(purpose==='update'){
   await page.getByRole('button',{name:selectLabel,exact:true}).click();
   await page.waitForFunction(()=>document.getElementById('master_fw_file').files.length===1);
   assert.equal(await page.locator('#master_fw_file').evaluate(e=>e.files[0].name),'firmware.bin');
   assert(!calls.some(c=>c.includes('/update/master/')));
  }else if(purpose==='config'){
   await page.getByRole('button',{name:selectLabel,exact:true}).click();await page.waitForFunction(()=>window.loadedBackup===1);
   assert(!calls.includes('/config/import'));
   page.once('dialog',d=>d.accept());await page.getByRole('button',{name:lang==='de'?'Backup auf USB':'Backup to USB',exact:true}).click();
   await page.getByRole('dialog').getByRole('button',{name:lang==='de'?'Speichern':'Save',exact:true}).click();
   await page.waitForFunction(()=>!document.querySelector('.usb-tools button').disabled);
   assert(calls.includes('/usb/write/commit'));
  }else{
   await page.getByRole('button',{name:'CSV > USB',exact:true}).click();await page.getByRole('dialog').getByRole('button',{name:lang==='de'?'Speichern':'Save',exact:true}).click();await page.waitForFunction(()=>!document.querySelector('.usb-tools button').disabled);
   assert(calls.includes('/usb/write/commit'));
  }
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`${lang}/${width}/${purpose} overflow`);
  assert(await page.evaluate(()=>[...document.querySelectorAll('#usb-storage button,#usb-storage input,#usb-storage select')].filter(e=>e.offsetWidth).every(e=>e.getBoundingClientRect().right<=innerWidth)),`${lang}/${width}/${purpose} control overflow`);
  await page.evaluate(()=>window.scrollTo(0,0));
  await page.screenshot({path:path.join(out,`${purpose}-${lang}-${width}.png`),fullPage:true});
  if(purpose==='update'&&lang==='de')await page.locator('#usb-storage').screenshot({path:path.join(out,`manager-${width}.png`)});
  if(lang==='de'&&width===390){
   const original=state.files[0].name;state.files[0].name='replacement.bin';state.generation++;
   await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));
   await page.waitForFunction(()=>!document.querySelector('.usb-row[aria-pressed=true]'));
   assert(await download.isDisabled());
   state.files[0].name=original;await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));
   await page.waitForFunction(name=>document.querySelector('#usb-file').textContent.includes(name),original);
   await page.locator('.usb-row').filter({hasText:original}).click();
   state.phase='writing';await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));
   await page.waitForFunction(()=>document.querySelector('.usb-tools button').disabled);
   assert(await download.isDisabled());state.phase='idle';await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));
   await page.waitForFunction(()=>!document.querySelector('.usb-tools button').disabled);
   writeDelay=120;
   await page.evaluate(()=>{void window.ofeUsbSave(new Blob([new Uint8Array(20000)]),'progress.log');});
   await page.getByRole('dialog').getByRole('button',{name:'Speichern',exact:true}).click();
   await page.waitForFunction(()=>!document.querySelector('.usb-progress').hidden);
   assert(await page.getByRole('button',{name:'Auswerfen',exact:true}).isDisabled());
   await page.waitForFunction(()=>Number(document.querySelector('[role=progressbar]').getAttribute('aria-valuenow'))>0);
   const value=Number(await page.locator('[role=progressbar]').getAttribute('aria-valuenow'));assert(value<=100);
   await page.waitForFunction(()=>document.querySelector('.usb-progress').hidden);
   await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));
   await page.waitForTimeout(50);assert((await page.locator('#usb-status').textContent()).includes('progress.log'));
   const releases=calls.filter(c=>c==='/usb/release').length;writeFailure=true;
   await page.evaluate(()=>{window.failedExport=null;void window.ofeUsbSave(new Blob(['fail']),'failed.log').then(r=>window.failedExport=r)});
   await page.getByRole('dialog').getByRole('button',{name:'Speichern',exact:true}).click();await page.waitForFunction(()=>window.failedExport===false);
   assert.equal(calls.filter(c=>c==='/usb/release').length,releases+1);
   assert.equal(await page.locator('#usb-status').getAttribute('data-error'),'true');
   assert(await page.locator('.usb-progress').isHidden());writeFailure=false;writeDelay=0;
   state.files=[];state.truncated=true;await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));
   await page.waitForFunction(()=>document.querySelector('.usb-empty')?.textContent==='Keine Dateien');
   assert((await page.locator('#usb-count').textContent()).includes('Begrenzung'));
   await page.getByRole('button',{name:'Auswerfen',exact:true}).click();
   await page.waitForFunction(()=>document.querySelector('#usb-status').textContent.includes('kann entfernt'));
   assert(await download.isDisabled());
  }
  state.mounted=false;await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));
  await page.waitForFunction(()=>document.querySelector('.transport-usb')&&!document.querySelector('.transport-usb').classList.contains('is-on'));
  state.detected=false;await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));
  await page.waitForFunction(()=>document.querySelector('.transport-usb').hidden);
  state.diagnostics.devices=state.diagnostics.devices.slice(0,1);state.diagnostics.enumerated=1;state.diagnostics.allocated=1;state.diagnostics.configured=1;
  await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));
  await page.waitForFunction(()=>document.querySelectorAll('#usb-host-devices li').length===1);
  assert((await page.locator('#usb-host-devices li').textContent()).includes('Hub'));
  assert((await page.locator('#usb-status').textContent()).includes(lang==='de'&&width===390?'USB-Stick kann entfernt werden':lang==='de'?'Kein USB-Stick':'No USB stick'));
  state.diagnostics.devices=[];state.diagnostics.enumerated=1;state.diagnostics.allocated=1;state.diagnostics.configured=0;
  await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));
  await page.waitForFunction(()=>document.querySelectorAll('#usb-host-devices li').length===0);
  assert((await page.locator('#usb-host-status').textContent()).includes(lang==='de'?'USB-Objekte: 1':'USB objects: 1'));
  assert((await page.locator('#usb-host-status').textContent()).includes(lang==='de'?'Konfiguriert: 0':'Configured: 0'));
  state.diagnostics.enumerated=0;state.diagnostics.allocated=0;state.diagnostics.root_connected=false;
  await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));
  await page.waitForFunction(text=>document.querySelector('#usb-host-status').textContent.includes(text),lang==='de'?'Root-Link: nein':'Root link: no');
  assert((await page.locator('#usb-host-status').textContent()).includes(lang==='de'?'Root-Link: nein':'Root link: no'));
  state.developer_mode=false;
  await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));
  await page.waitForFunction(()=>document.querySelector('#usb-host-status').hidden);
  assert(await page.locator('#usb-host-devices').isHidden());
  assert.equal(await page.locator('#usb-host-status').textContent(),'');
  assert.equal(await page.locator('#usb-host-devices li').count(),0);
  assert.deepEqual(errors,[]);await page.close();
 }
 console.log('PASS: USB file manager search/filter/sort, contextual actions, stable selection/focus, diagnostics gating, disconnects and firmware/backup/trace staging; DE/EN at 320/390/1440');
}finally{await browser.close()}})().catch(e=>{console.error(e);process.exitCode=1});
