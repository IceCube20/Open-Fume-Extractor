const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{chromium}=require('playwright');
const src=path.resolve(__dirname,'../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
const ui=fs.readFileSync(path.join(src,'WebUsbStorageUi.h'),'utf8').match(/R"USBJS\(([\s\S]*?)\)USBJS"/)[1];
const css=fs.readFileSync(path.join(src,'WebShell.inc.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1]+fs.readFileSync(path.join(src,'WebFormStyles.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1];
const out=path.join(__dirname,'build-checks/usb-drives');fs.mkdirSync(out,{recursive:true});
const NONE=4294967295;
(async()=>{const browser=await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});try{
for(const [count,width,lang] of [[2,320,'en'],[4,1440,'en'],[4,320,'de'],[2,1440,'de']]){
 const de=lang==='de',page=await browser.newPage({viewport:{width,height:1000}}),errors=[],posts=[],anchors=[];
 let pending=null,switchFault='',readFault=false,operation=0,write=null,exportGate=null,finishExport=null,fileGate=null,finishFile=null;
 const state={drive:0,drive_instance:101,drive_limit:4,detected:true,mounted:true,phase:'idle',generation:10,token:0,transfer_limit:2147483647,buffered_read:false,
  drives:Array.from({length:count},(_,index)=>({index,instance:101+index,label:'Identical USB model',volume_name:'Identical volume name',connected:true,selected:index===0,mounted:index===0,capacity_bytes:16*1024**3})),
  directories:[{index:0,path:''},{index:1,path:'Shared'},{index:2,path:'Shared/Sub'}],files:[
   {index:0,name:'same.bin',path:'same.bin',directory:0,size:4},{index:1,name:'same.bin',path:'Shared/same.bin',directory:1,size:4}
  ],completed_operation:0,result:''};
 function activate(index,bump=true){
  state.drive=index;state.drive_instance=index===NONE?0:state.drives[index].instance;state.mounted=index!==NONE;state.phase='idle';state.token=0;
  for(const d of state.drives){d.selected=d.index===index;d.mounted=d.selected;}if(bump)state.generation++;
 }
 page.on('pageerror',e=>errors.push(e.message));page.on('dialog',d=>d.accept());
 await page.exposeFunction('recordAnchor',url=>anchors.push(url));
 await page.route('http://ofe.test/**',async route=>{
  const req=route.request(),u=new URL(req.url()),p=new URLSearchParams(req.postData()||''),json=j=>route.fulfill({contentType:'application/json',body:JSON.stringify(j)});
  if(req.method()==='POST')posts.push({path:u.pathname,params:Object.fromEntries(p),query:Object.fromEntries(u.searchParams)});
  if(u.pathname==='/usb/status'){
   if(pending&&!--pending.ticks){
    const target=pending.index;pending=null;
    if(readFault){readFault=false;activate(target,false);state.phase='ready';state.token=999;}
    else if(switchFault==='reuse'){state.drives[target].instance++;activate(NONE);switchFault='';}
    else if(switchFault==='wrong'){activate(target===0?1:0);switchFault='';}
    else activate(target);
   }
   return json(state);
  }
  if(u.pathname==='/usb/drive'){
   assert.deepEqual([...p.keys()].sort(),['generation','index','instance']);assert.equal(Number(p.get('generation')),state.generation);
   const d=state.drives.find(d=>d.connected&&d.index===Number(p.get('index'))&&d.instance===Number(p.get('instance')));assert(d);
   state.phase='queued';pending={index:d.index,ticks:3};return json({ok:true});
  }
  if(['/usb/command','/usb/manage','/usb/write/begin'].includes(u.pathname)){
   assert.equal(Number(p.get('drive')),state.drive);assert.equal(Number(p.get('drive_instance')),state.drive_instance);assert.equal(Number(p.get('generation')),state.generation);assert(state.mounted);
  }
  if(u.pathname==='/usb/command'){
   if(p.get('action')==='eject'){state.mounted=false;state.drives[state.drive].mounted=false;state.generation++;}
   if(p.get('action')==='refresh')state.generation++;
   if(p.get('action')==='read'){
    if(readFault){state.phase='queued';pending={index:state.drive===0?1:0,ticks:2};}
    else {state.phase='ready';state.token=71;}
   }
   return json({ok:true});
  }
  if(u.pathname==='/usb/manage'){
   assert.equal(p.get('action'),'mkdir');state.directories.push({index:state.directories.length,path:p.get('name')});
   state.completed_operation=++operation;state.result='OK';state.generation++;return json({ok:true,operation});
  }
  if(u.pathname==='/usb/write/begin'){
   assert.equal(Number(p.get('directory')),0);write={name:p.get('name'),size:Number(p.get('size'))};state.phase='receiving';state.token=72;return json({ok:true,token:72});
  }
  if(u.pathname==='/usb/write/chunk'){assert.equal(u.searchParams.get('token'),'72');assert.equal(p.has('drive'),false);return json({ok:true});}
  if(u.pathname==='/usb/write/commit'){assert.deepEqual([...p.keys()],['token']);assert.equal(p.get('token'),'72');state.files.push({index:state.files.length,path:write.name,...write});state.phase='idle';state.token=0;state.generation++;return json({ok:true});}
  if(u.pathname==='/usb/release'){assert.deepEqual([...p.keys()],['token']);return json({ok:true});}
  if(u.pathname==='/usb/file'){if(fileGate)await fileGate;state.phase='idle';state.token=0;return route.fulfill({contentType:'application/octet-stream',body:'test'});}
  if(u.pathname==='/diagnostics/state'){if(exportGate)await exportGate;return json({diagnostics:true});}
  return route.fulfill({contentType:'text/html',body:'<html></html>'});
 });
 async function mount(purpose='update'){
  await page.goto('http://ofe.test/');await page.setContent(`<style>:root{--ofe-font-ui:Arial,sans-serif}${css}</style><select id="lang_sel"><option value="${lang}">${lang}</option></select><main><section class="panel" id="usb-storage" data-usb-purpose="${purpose}"></section><input id="master_fw_file" type="file"><input id="module_fw_file" type="file"></main>`);
  await page.evaluate(()=>{HTMLAnchorElement.prototype.click=function(){void window.recordAnchor(this.getAttribute('href'));};document.getElementById('master_fw_file').onchange=()=>window.firmwareSelected=true;});
  await page.addScriptTag({content:ui});await page.waitForSelector('#usb-drive option[value]:not([value=""])',{state:'attached'});
 }
 await mount();
 const picker=page.locator('#usb-drive'),dialog=page.getByRole('dialog'),button=name=>page.getByRole('button',{name,exact:true});
 const rootName=page.locator('.usb-directory[data-directory=""] .usb-path');
 const driveOption=index=>picker.locator(`option[value='${JSON.stringify([index,state.drives[index].instance])}']`);
 const row=path=>page.locator('.usb-row').filter({has:page.locator('.usb-path')}).locator(`xpath=self::*[@title="${path}"]`);
 const saveButton=()=>dialog.getByRole('button',{name:de?'Speichern':'Save',exact:true});
 const cancel=()=>dialog.getByRole('button',{name:de?'Abbrechen':'Cancel',exact:true}).click();
 const mutations=()=>posts.filter(p=>['/usb/manage','/usb/write/begin'].includes(p.path)).length;
 const switches=()=>posts.filter(p=>p.path==='/usb/drive').length;
 async function sync(){const response=page.waitForResponse('http://ofe.test/usb/status');await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));await response;await page.waitForTimeout(40);}
 async function settled(){await page.waitForFunction(()=>document.querySelector('.usb-progress').hidden&&!document.querySelector('#usb-drive').disabled);}
 async function choose(index){await picker.selectOption(JSON.stringify([index,state.drives[index].instance]));await settled();assert.equal(state.drive,index);assert(state.mounted);}
 async function startExport(name='export.txt'){await page.evaluate(name=>{window.exportResult=null;void window.ofeUsbSave(new Blob(['export']),name).then(r=>window.exportResult=r);},name);await dialog.waitFor();}
 assert.equal(switches(),0);assert.equal(await picker.locator('option:not([value=""])').count(),count);
 const optionNames=await picker.locator('option:not([value=""])').allTextContents();assert.equal(new Set(optionNames).size,count);assert(optionNames.every(s=>s.includes('16 GiB')));
 for(const d of state.drives){
  assert((await driveOption(d.index).textContent()).startsWith('Identical volume name \u00b7 '+(de?'Anschluss ':'Slot ')+(d.index+1)));
  assert.equal(await driveOption(d.index).getAttribute('value'),JSON.stringify([d.index,d.instance]));
 }
 assert.equal(await rootName.textContent(),'Identical volume name');
 assert(await picker.evaluate(e=>e.getBoundingClientRect().bottom<document.getElementById('usb-file').getBoundingClientRect().top));
 const limitWarning=limit=>de?'Zus\u00e4tzlicher USB-Stick nicht eingebunden: Laufwerkslimit '+limit+' erreicht.':'Additional USB drive not mounted: drive limit '+limit+' reached.';
 assert(!(await page.locator('#usb-count').textContent()).includes(limitWarning(4)));
 state.drive_limit_reached=true;await sync();assert((await page.locator('#usb-count').textContent()).endsWith(limitWarning(4)));assert((await page.locator('#usb-count').textContent()).startsWith('2 / 2'));
 state.drive_limit=3;await sync();assert((await page.locator('#usb-count').textContent()).endsWith(limitWarning(3)));
 delete state.drive_limit;await sync();assert((await page.locator('#usb-count').textContent()).endsWith(limitWarning(4)));
 state.drive_limit=4;state.drive_limit_reached=false;await sync();assert(!(await page.locator('#usb-count').textContent()).includes(limitWarning(4)));
 await page.locator('.usb-directory[data-directory="Shared"]').click();await row('Shared/same.bin').click();
 assert.equal(await page.locator('#usb-selected').textContent(),'Shared/same.bin');
 // Label metadata is display-only, including labels learned for inactive drives.
 const labelContext=[state.drive,state.drive_instance,state.generation],labelFiles=JSON.stringify([state.files,state.directories]);
 const selectedValue=await picker.inputValue(),beforeLabels=switches();
 async function checkLabel(name){
  assert.equal(await rootName.textContent(),name);
  assert.equal(await driveOption(0).textContent(),name+' \u00b7 '+(de?'Anschluss 1':'Slot 1')+' \u00b7 16 GiB');
  assert.equal(await picker.inputValue(),selectedValue);
  assert.equal(await page.locator('#usb-selected').textContent(),'Shared/same.bin');
  assert.equal(await row('Shared/same.bin').getAttribute('aria-pressed'),'true');
  assert.equal(await page.locator('.usb-directory[data-directory="Shared"]').getAttribute('aria-expanded'),'true');
  assert.deepEqual([state.drive,state.drive_instance,state.generation],labelContext);
  assert.equal(JSON.stringify([state.files,state.directories]),labelFiles);assert.equal(switches(),beforeLabels);
  assert.equal(await driveOption(0).evaluate(e=>e.childElementCount),0);assert.equal(await rootName.evaluate(e=>e.childElementCount),0);
  assert.equal(await page.locator('#usb-storage label').filter({hasText:'injected-label'}).count(),0);
  assert.equal(await page.locator('#usb-storage img, #usb-storage script').count(),0);
  assert.equal(await page.evaluate(()=>!!window.labelInjected),false);
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
 }
 for(const name of [' Workshop backups ','B\u00fcro Gr\u00f6\u00dfe \u00c4\u00d6\u00dc \u00e4\u00f6\u00fc \u00df',"O'Brien's USB",'A & B <label>injected-label</label>','<img src=x onerror="window.labelInjected=true">']){
  state.drives[0].volume_name=name;await sync();await checkLabel(name);
 }
 state.drives[0].volume_name='';state.drives[0].label="Legacy B\u00fcro's & <label>injected-label</label>";await sync();await checkLabel(state.drives[0].label);
 delete state.drives[0].volume_name;await sync();await checkLabel(state.drives[0].label);
 state.drives[0].label='';await sync();await checkLabel('USB 1');
 delete state.drives[0].label;await sync();await checkLabel('USB 1');
 state.drives[0].label='Identical USB model';state.drives[0].volume_name='Identical volume name';await sync();await checkLabel('Identical volume name');
 await row('Shared/same.bin').focus();
 const inactiveName="Inactive B\u00fcro's & <label>injected-label</label>";
 state.drives[1].volume_name=inactiveName;await sync();await checkLabel('Identical volume name');
 assert.equal(await driveOption(1).textContent(),inactiveName+' \u00b7 '+(de?'Anschluss 2':'Slot 2')+' \u00b7 16 GiB'+(de?' \u00b7 nicht eingebunden':' \u00b7 not mounted'));
 assert(await row('Shared/same.bin').evaluate(e=>e===document.activeElement));
 state.drives[1].volume_name='Identical volume name';await sync();
 // Inactive removal changes only the registry, preserving tree state and focus.
 state.drives[1].connected=false;await sync();assert.equal(await picker.locator('option:not([value=""])').count(),count-1);
 assert.equal(await page.locator('#usb-selected').textContent(),'Shared/same.bin');assert.equal(await page.locator('.usb-directory[data-directory="Shared"]').getAttribute('aria-expanded'),'true');
 assert(await row('Shared/same.bin').evaluate(e=>e===document.activeElement));assert.equal(switches(),0);
 state.drives[1].connected=true;state.drives[1].instance++;await sync();
 if(count===4){for(const index of [2,3,0]){await choose(index);assert.equal(state.drives.filter(d=>d.connected).length,4);assert.equal(state.drives.filter(d=>d.mounted).length,1);}}
 await choose(1);assert.equal(await page.locator('#usb-selected').textContent(),de?'Ordner: /':'Folder: /');
 assert.equal(await rootName.textContent(),'Identical volume name');assert.equal(await picker.inputValue(),JSON.stringify([1,state.drives[1].instance]));
 assert.equal(await page.locator('.usb-directory[data-directory="Shared"]').getAttribute('aria-expanded'),'false');
 await row('same.bin').click();assert.equal(await page.locator('#usb-selected').textContent(),'same.bin');
 // Identity resets do not depend on a different path, size, generation, or slot.
 state.drives[1].instance++;activate(1,false);await sync();assert.equal(await page.locator('#usb-selected').textContent(),de?'Ordner: /':'Folder: /');
 await button(de?'Auswerfen':'Eject').click();await settled();assert.equal(state.drive,1);assert.equal(state.drive_instance,state.drives[1].instance);assert(!state.mounted);assert(state.drives[1].connected);
 state.drive_limit_reached=true;await sync();assert.equal(await page.locator('#usb-count').textContent(),limitWarning(4));
 assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));await page.locator('#usb-storage').screenshot({path:path.join(out,`limit-unmounted-${count}-${lang}-${width}.png`)});
 state.drive_limit_reached=false;await sync();assert.equal(await page.locator('#usb-count').textContent(),'');
 assert(await picker.isVisible());assert(!(await picker.isDisabled()));assert.equal(await picker.inputValue(),'');await choose(1);
 state.drives[1].connected=false;activate(NONE);state.phase='error';state.message='USB drive disconnected; select another drive';const beforeRemoval=switches();await sync();assert.equal(state.drive,NONE);assert.equal(switches(),beforeRemoval);
 assert(await picker.isVisible());assert(!(await picker.isDisabled()));assert.equal(await page.locator('.usb-row').count(),0);await choose(0);
 state.drives[1].connected=true;state.drives[1].instance++;await sync();
 // A stale picker option cannot target a new connection reusing that slot.
 const oldOption=JSON.stringify([1,state.drives[1].instance]),beforeStale=switches();state.drives[1].instance++;
 await picker.selectOption(oldOption);await settled();assert.equal(switches(),beforeStale);assert.equal(state.drive,0);
 for(const fault of ['reuse','wrong']){
  switchFault=fault;await picker.selectOption(JSON.stringify([1,state.drives[1].instance]));await page.waitForFunction(()=>!document.querySelector('.usb-progress').hidden);
  assert(await picker.isDisabled());await settled();assert.equal(await page.locator('#usb-status').getAttribute('data-error'),'true');
  assert.notEqual(state.drive,1);await choose(0);
 }
 // Poll-observed disk changes invalidate an open export even at equal generation.
 await startExport();assert(await picker.isDisabled());const beforeExport=mutations();activate(1,false);await sync();await saveButton().click();assert(await dialog.isVisible());assert.equal(mutations(),beforeExport);
 assert((await dialog.locator('.usb-dialog-error').textContent()).includes(de?'USB-Laufwerk':'USB drive'));await cancel();await page.waitForFunction(()=>window.exportResult===false);
 // The pre-POST status check catches an unpolled instance change on the same slot.
  await startExport('stale-instance.txt');state.drives[1].instance++;activate(1,false);await saveButton().click();
  await page.waitForFunction(()=>window.exportResult===false||document.querySelector('.usb-dialog-error')?.textContent.length>0);
  if(await dialog.isVisible()){assert((await dialog.locator('.usb-dialog-error').textContent()).includes(de?'USB-Laufwerk':'USB drive'));await cancel();}
  await page.waitForFunction(()=>window.exportResult===false);assert.equal(mutations(),beforeExport);
 await settled();await row('same.bin').click();await button(de?'Verschieben / Umbenennen':'Move / Rename').click();await dialog.locator('#usb-new-name').fill('renamed.bin');
 activate(0,false);await dialog.getByRole('button',{name:de?'Verschieben / Umbenennen':'Move / Rename',exact:true}).click();await settled();assert.equal(mutations(),beforeExport);
 await button(de?'Datei hochladen':'Upload file').click();await dialog.locator('#usb-upload').setInputFiles({name:'stale-upload.txt',mimeType:'text/plain',buffer:Buffer.from('test')});
 state.drives[0].instance++;activate(0,false);await dialog.getByRole('button',{name:de?'Hochladen':'Upload',exact:true}).click();await settled();assert.equal(mutations(),beforeExport);
 if(await dialog.isVisible()){assert((await dialog.locator('.usb-dialog-error').textContent()).includes(de?'USB-Laufwerk':'USB drive'));await cancel();}
 await button(de?'Neuer Ordner':'New folder').click();await dialog.locator('#usb-new-name').fill('stale-folder');activate(1,false);await sync();
 await dialog.getByRole('button',{name:de?'Anlegen':'Create',exact:true}).click();assert(await dialog.isVisible());assert.equal(mutations(),beforeExport);await cancel();
 activate(0);await sync();
 // All mutation entry points send current drive, instance, and generation.
 await button(de?'Aktualisieren':'Refresh').click();await settled();await button(de?'Neuer Ordner':'New folder').click();await dialog.locator('#usb-new-name').fill('Created');await dialog.getByRole('button',{name:de?'Anlegen':'Create',exact:true}).click();await settled();
 await page.locator('.usb-directory[data-directory=""]').click();await startExport('saved.txt');await saveButton().click();await page.waitForFunction(()=>window.exportResult===true);await settled();
 assert.equal(write.name,'saved.txt');assert(posts.some(p=>p.path==='/usb/manage'));assert(posts.some(p=>p.path==='/usb/write/begin'));
 for(const phase of ['queued','reading','writing','receiving','ready']){state.phase=phase;await sync();assert(await picker.isDisabled());}
 state.phase='idle';await sync();await settled();
 // A native token locks the picker until the transfer ends; token routes stay unchanged.
 await row('same.bin').click();await button(de?'Herunterladen':'Download').click();await page.waitForFunction(()=>document.querySelector('.usb-progress').hidden);
 assert(await picker.isDisabled());assert.equal(anchors.at(-1),'/usb/file?token=71');assert.equal(await page.evaluate(()=>window.ofeUsbSave(new Blob(['busy']),'busy.txt')),false);
 state.token=0;state.phase='idle';await sync();await settled();
 // A queued read must not consume a different client's Ready token.
 const beforeRead=anchors.length;readFault=true;await button(de?'Herunterladen':'Download').click();await page.waitForFunction(()=>document.querySelector('.usb-progress').hidden&&document.querySelector('#usb-status').dataset.error==='true');
 assert.equal(anchors.length,beforeRead);assert.equal(state.token,999);state.phase='idle';state.token=0;await sync();await settled();
 // A consumer handoff also remains tied to its source while fetching the file.
 await row('same.bin').click();fileGate=new Promise(resolve=>finishFile=resolve);const fileRequest=page.waitForRequest('**/usb/file?token=71');await button(de?'Firmware ausw\u00e4hlen':'Select firmware').click();await fileRequest;
 activate(state.drive===0?1:0,false);finishFile();fileGate=null;await settled();assert.equal(await page.evaluate(()=>!!window.firmwareSelected),false);
 assert.equal(await page.locator('#usb-status').getAttribute('data-error'),'true');
 await page.locator('#usb-storage').screenshot({path:path.join(out,`drives-${count}-${lang}-${width}.png`)});
 assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
 // Delayed producer fetches may not retarget an export to the newly selected disk.
 await mount('trace');exportGate=new Promise(resolve=>finishExport=resolve);
 await page.evaluate(()=>{const save=window.ofeUsbSave;window.ofeUsbSave=async(...args)=>window.handoffResult=await save(...args);});
 const exportRequest=page.waitForRequest('**/diagnostics/state');await button(de?'Diagnose auf USB':'Diagnostics to USB').click();await exportRequest;
 activate(state.drive===0?1:0,false);await sync();const beforeHandoff=mutations();finishExport();exportGate=null;await page.waitForFunction(()=>window.handoffResult===false);
 assert(!(await dialog.isVisible()));assert.equal(mutations(),beforeHandoff);assert.deepEqual(errors,[]);
 await page.close();
}
console.log('PASS: 2/4 USB drives, UTF-8 volume labels and safe literal text, legacy/unnamed fallback, distinct slots/instances, metadata-only selection preservation and root refresh, identical paths, selected-only eject/remount, active/inactive removal, no fallback, stale picker/dialog/export/consumer guards, async switch/read identity checks, mutation drive guards and token locks; DE/EN mobile/desktop');
}finally{await browser.close();}})().catch(e=>{console.error(e);process.exitCode=1;});
