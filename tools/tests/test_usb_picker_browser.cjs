const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{chromium}=require('playwright');
const src=path.resolve(__dirname,'../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
const ui=fs.readFileSync(path.join(src,'WebUsbStorageUi.h'),'utf8').match(/R"USBJS\(([\s\S]*?)\)USBJS"/)[1];
const css=fs.readFileSync(path.join(src,'WebShell.inc.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1]+fs.readFileSync(path.join(src,'WebFormStyles.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1];
const diagCss=fs.readFileSync(path.join(src,'WebDiagnostics.inc.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1];
const out=path.join(__dirname,'build-checks/usb-picker');fs.mkdirSync(out,{recursive:true});
(async()=>{
 const browser=await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});
 try{for(const [width,lang] of [[320,'de'],[390,'en'],[1440,'de']]){
  const de=lang==='de',page=await browser.newPage({viewport:{width,height:900}}),errors=[],posts=[];
  const state={drive:0,drive_instance:101,detected:true,mounted:true,generation:10,phase:'idle',token:0,
   drives:[{index:0,instance:101,connected:true,selected:true,mounted:true,volume_name:'WERKSTATT'}],
   directories:[{index:0,path:''},{index:1,path:'Backups'}],
   files:[{index:0,name:'firmware.bin',path:'firmware.bin',size:4},{index:1,name:'backup.json',path:'backup.json',size:4},{index:2,name:'device.ofeprofile',path:'device.ofeprofile',size:4}]};
  let selectedFile=null,readGate=null,releaseRead=null,write=null;
  page.on('pageerror',e=>errors.push(e.message));page.on('dialog',d=>d.accept());
  await page.route('http://ofe.test/**',async route=>{
   const req=route.request(),u=new URL(req.url()),p=new URLSearchParams(req.postData()||''),reply=j=>route.fulfill({contentType:'application/json',body:JSON.stringify(j)});
   if(req.method()==='POST')posts.push({path:u.pathname,params:Object.fromEntries(p)});
   if(u.pathname==='/usb/status')return reply(state);
   if(['/usb/command','/usb/write/begin'].includes(u.pathname)){
    assert.equal(Number(p.get('generation')),state.generation);assert.equal(Number(p.get('drive')),state.drive);assert.equal(Number(p.get('drive_instance')),state.drive_instance);
   }
   if(u.pathname==='/usb/command'){assert.equal(p.get('action'),'read');selectedFile=state.files.find(f=>f.index===Number(p.get('index')));state.token=71;state.phase='ready';return reply({ok:true});}
   if(u.pathname==='/usb/file'){if(readGate)await readGate;assert(selectedFile);state.phase='idle';state.token=0;return route.fulfill({body:'test'});}
   if(u.pathname==='/usb/release'){state.phase='idle';state.token=0;return reply({ok:true});}
   if(u.pathname==='/usb/write/begin'){write=Object.fromEntries(p);state.phase='receiving';state.token=72;return reply({token:72});}
   if(u.pathname==='/usb/write/chunk')return reply({ok:true});
   if(u.pathname==='/usb/write/commit'){state.phase='idle';state.token=0;state.generation++;return reply({ok:true});}
   if(u.pathname==='/config/export')return reply({product:'Open Fume Extractor',schema:1,master:{}});
   if(u.pathname==='/diagnostics/state')return reply({diagnostics:true});
   return route.fulfill({contentType:'text/html',body:'<html></html>'});
  });
  await page.goto('http://ofe.test/update');
  await page.setContent(`<style>:root{--ofe-font-ui:Arial,sans-serif}${css}${diagCss}</style><header><nav class="nav"><a href="/">Status</a></nav><select id="lang_sel"><option value="${lang}">${lang}</option></select></header><main><section class="panel"><form id="masterForm"><input id="master_fw_file" type="file" accept=".bin"><button type="submit">Install MCU</button></form></section><section class="panel"><input id="module_fw_file" type="file" accept=".bin"><input id="c6_file" type="file" accept=".bin"></section><section class="panel"><a class="btn" href="/config/export">${de?'Backup herunterladen':'Download backup'}</a><input id="backup_file" type="file" accept=".json"></section><div id="modules"></div></main>`);
  await page.evaluate(()=>{window.changed={};for(const input of document.querySelectorAll('input[type=file]'))input.onchange=()=>window.changed[input.id]=input.files[0].name;document.getElementById('masterForm').onsubmit=e=>{e.preventDefault();window.installed=true;};});
  await page.addScriptTag({content:ui});
  const browse=page.locator('#master_fw_file');
  const browseColor=()=>browse.evaluate(e=>getComputedStyle(e,'::file-selector-button').backgroundColor);
  await page.mouse.move(0,0);const normalBrowseColor=await browseColor();
  await browse.hover({position:{x:20,y:20}});await page.waitForTimeout(200);
  assert.notEqual(await browseColor(),normalBrowseColor,'Browse button must lighten on hover');
  await browse.evaluate(e=>e.disabled=true);await page.waitForTimeout(200);
  assert.equal(await browseColor(),normalBrowseColor,'Disabled browse button must not highlight');
  await browse.evaluate(e=>e.disabled=false);await page.mouse.move(0,0);
  for(const group of await page.locator('.ofe-file-actions').all()){
   const input=await group.locator('input').boundingBox(),button=await group.locator('button').boundingBox();
   assert.equal(input.height,44);assert.equal(button.height,44);
   assert.equal(input.x,button.x);assert.equal(button.y-input.y-input.height,12);
   assert.equal(input.width,(await group.boundingBox()).width);
   if(width<=600)assert.equal(input.width,button.width);
   else assert(button.width<=input.width);
  }
  const backupGroup=page.locator('a[href="/config/export"]').locator('..');
  const exportBoxes=await backupGroup.locator('a,button').evaluateAll(es=>es.map(e=>({top:e.getBoundingClientRect().top,height:e.getBoundingClientRect().height})));
  assert.equal(exportBoxes[0].height,exportBoxes[1].height);
  if(exportBoxes[0].top!==exportBoxes[1].top)assert.equal(exportBoxes[1].top-exportBoxes[0].top-exportBoxes[0].height,12);
  await page.screenshot({path:path.join(out,`actions-${width}-${lang}.png`),fullPage:true});
  assert.equal(await page.locator('#usb-picker[open]').count(),0);
  assert.equal(await page.locator('#usb-picker #usb-storage.panel').count(),0);
  assert.equal(await page.locator('main #usb-storage').count(),0);
  assert.equal(await page.locator('header a[href="/usb"]').count(),1);
  const picker=page.locator('#usb-picker'),confirm=picker.locator('.usb-picker-confirm'),cancel=picker.locator('.usb-picker-cancel');
  const open=async id=>{await page.locator('#'+id+' + .ofe-usb-action').click();await picker.waitFor({state:'visible'});await page.waitForFunction(()=>document.querySelector('#usb-file .usb-row'));};
  await open('master_fw_file');
  assert.equal(await picker.locator('.usb-row').count(),1);assert(await confirm.isDisabled());
  await picker.locator('.usb-row').click();assert(!(await confirm.isDisabled()));
  await page.screenshot({path:path.join(out,`picker-${width}-${lang}.png`),fullPage:true});
  assert(await picker.evaluate(e=>e.getBoundingClientRect().left>=0&&e.getBoundingClientRect().right<=innerWidth&&e.scrollWidth<=e.clientWidth+1));
  await confirm.click();await picker.waitFor({state:'hidden'});
  assert.equal(await page.evaluate(()=>window.changed.master_fw_file),'firmware.bin');
  assert.equal(await page.evaluate(()=>!!window.installed),false);assert(!posts.some(p=>p.path.startsWith('/update')));
  for(const id of ['module_fw_file','c6_file','backup_file']){await open(id);await picker.locator('.usb-row').click();await confirm.click();await picker.waitFor({state:'hidden'});assert.equal(await page.evaluate(id=>window.changed[id],id),id==='backup_file'?'backup.json':'firmware.bin');}
  await page.evaluate(()=>{document.getElementById('modules').innerHTML='<input id="uni_file_80" type="file" accept=".ofeprofile,.txt,.json"><button onclick="universalExport(80)">Export</button>';window.universalExport=(addr,usb)=>window.exported=[addr,usb];document.getElementById('uni_file_80').onchange=()=>window.changed.profile=true;});
  await page.waitForSelector('#uni_file_80 + .ofe-usb-action');
  await open('uni_file_80');await picker.locator('.usb-row').filter({hasText:'device.ofeprofile'}).click();await confirm.click();await picker.waitFor({state:'hidden'});assert(await page.evaluate(()=>window.changed.profile));
  await page.locator('button[onclick="universalExport(80)"] + .ofe-usb-action').click();assert.deepEqual(await page.evaluate(()=>window.exported),[80,true]);
  // Cancelling is inert, restores focus, and resolves the caller.
  const before=posts.length;
  await page.evaluate(()=>{window.cancelResult='pending';void window.ofeUsbPick({accept:'.bin'}).then(f=>window.cancelResult=f);});
  await picker.waitFor({state:'visible'});await page.keyboard.press('Escape');await picker.waitFor({state:'hidden'});
  await page.waitForFunction(()=>window.cancelResult===null);assert.equal(posts.length,before);
  // The save dialog chooses a folder and filename; it is not an inline browser.
  await page.evaluate(()=>{window.saved=null;void window.ofeUsbSave(new Blob(['report']),'report.json').then(v=>window.saved=v);});
  await picker.waitFor({state:'visible'});await picker.locator('.usb-directory[data-directory="Backups"]').click();
  await picker.locator('#usb-picker-name').fill('renamed.json');await confirm.click();await picker.waitFor({state:'hidden'});
  await page.waitForFunction(()=>window.saved===true);assert.equal(write.name,'renamed.json');assert.equal(write.directory,'1');
  // Do not retarget a pending selection after the stick is replaced during a read.
  await open('master_fw_file');await picker.locator('.usb-row').click();
  readGate=new Promise(r=>releaseRead=r);const reading=page.waitForRequest(r=>new URL(r.url()).pathname==='/usb/file');await confirm.click();await reading;
  await page.keyboard.press('Escape');assert(await picker.isVisible());state.drive_instance++;state.generation++;releaseRead();readGate=null;
  await page.waitForFunction(()=>document.querySelector('.usb-picker-error').textContent.length>0);await cancel.click();
  assert.equal(await page.evaluate(()=>window.changed.master_fw_file),'firmware.bin');
  state.drives[0].instance=state.drive_instance;
  // All contextual exporters open the same destination picker.
  await page.evaluate(({exports,quality})=>{
   history.replaceState({},'', '/diagnostics');
   document.getElementById('modules').innerHTML=exports+quality;
   document.getElementById('rq_export').disabled=false;
   window.diagDownload=(fmt,usb)=>usb&&window.ofeUsbSave(new Blob(['trace']),'trace.'+fmt);
   window.ofeRs485QualityExport=usb=>usb&&window.ofeUsbSave(new Blob(['quality']),'quality.json');
  },{exports:fs.readFileSync(path.join(__dirname,`build-checks/master-p4-native/diagnostic-exports-${lang}.html`),'utf8'),quality:fs.readFileSync(path.join(__dirname,`build-checks/master-p4-native/quality-${lang}.html`),'utf8')});
  await page.waitForSelector('[data-usb-diagnostics]');
  const formatGroups=page.locator('.diag-export-actions:not(.diag-quality-export)>.diag-export-group');
  assert.equal(await formatGroups.count(),3);
  for(const group of await formatGroups.all()){
   const boxes=await group.locator('button,a').evaluateAll(es=>es.map(e=>{const r=e.getBoundingClientRect();return {top:r.top,height:r.height,left:r.left,right:r.right};}));
   assert.equal(boxes.length,2);assert.equal(boxes[0].height,40);assert.equal(boxes[1].height,40);assert.equal(boxes[0].top,boxes[1].top);
  }
  await page.screenshot({path:path.join(out,`exports-${width}-${lang}.png`),fullPage:true});
  await page.locator('#rq_export').evaluate(e=>e.disabled=true);await page.waitForFunction(()=>document.querySelector('#rq_export + .ofe-usb-action').disabled);
  await page.locator('#rq_export').evaluate(e=>e.disabled=false);await page.waitForFunction(()=>!document.querySelector('#rq_export + .ofe-usb-action').disabled);
  for(const [selector,name] of [['button[onclick=\'diagDownload("csv")\'] + .ofe-usb-action','trace.csv'],['button[onclick=\'diagDownload("json")\'] + .ofe-usb-action','trace.json'],['#rq_export + .ofe-usb-action','quality.json'],['[data-usb-diagnostics]','']]){
   await page.locator(selector).click();await picker.waitFor({state:'visible'});
   if(name)assert.equal(await picker.locator('#usb-picker-name').inputValue(),name);
   else assert((await picker.locator('#usb-picker-name').inputValue()).startsWith('open-fume-extractor-diagnostics-'));
   await cancel.click();await picker.waitFor({state:'hidden'});
  }
  await page.locator('a[href="/config/export"] + .ofe-usb-action').click();await picker.waitFor({state:'visible'});await cancel.click();
  const writesBefore=posts.filter(p=>p.path==='/usb/write/begin').length;
  await page.evaluate(()=>{void window.ofeUsbSave(new Blob(['x']),'stale.json',{drive:0,drive_instance:1,generation:1});});
  await picker.waitFor({state:'visible'});await page.waitForFunction(()=>document.querySelector('.usb-picker-error').textContent.length>0);
  assert(await confirm.isDisabled());await cancel.click();assert.equal(posts.filter(p=>p.path==='/usb/write/begin').length,writesBefore);
  await page.goto('http://ofe.test/usb');
  await page.setContent(`<style>:root{--ofe-font-ui:Arial,sans-serif}${css}</style><header><nav class="nav"></nav><select id="lang_sel"><option value="${lang}">${lang}</option></select></header><main><section class="panel" id="usb-storage" data-usb-purpose="browser"></section></main>`);
  await page.addScriptTag({content:ui});await page.waitForSelector('#usb-file .usb-row');
  assert.equal(await page.locator('#usb-picker').count(),0);assert.equal(await page.locator('header a[href="/usb"].active').count(),1);
  assert.equal(await page.locator('#usb-storage').evaluate(e=>getComputedStyle(e).borderTopWidth),'1px');
  assert(await page.getByRole('button',{name:de?'Datei hochladen':'Upload file',exact:true}).isVisible());
  await page.screenshot({path:path.join(out,`browser-${width}-${lang}.png`),fullPage:true});
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
  assert.deepEqual(errors,[]);
  await page.close();
 }
 console.log('PASS: standalone USB navigation and modal read/save, contextual MCU/module/C6/backup/profile staging, cancellation, stale-stick guard, no auto-install; 320/390/1440 DE/EN');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
