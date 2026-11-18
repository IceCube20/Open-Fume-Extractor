const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{chromium}=require('playwright');
const root=path.resolve(__dirname,'../..'),src=path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
const ui=fs.readFileSync(path.join(src,'WebUsbStorageUi.h'),'utf8').match(/R"USBJS\(([\s\S]*?)\)USBJS"/)[1];
const css=fs.readFileSync(path.join(src,'WebShell.inc.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1]+fs.readFileSync(path.join(src,'WebFormStyles.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1];
const out=path.join(__dirname,'build-checks/usb-file-manager');fs.mkdirSync(out,{recursive:true});
(async()=>{const browser=await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});try{
for(const lang of ['de','en'])for(const width of [320,390,1440]){
 const de=lang==='de',page=await browser.newPage({viewport:{width,height:1000}}),errors=[],requests=[];let confirmAccept=true,operation=0,write=null,statusFailure=false;
 const state={detected:true,mounted:true,developer_mode:false,phase:'idle',message:'Ready',generation:10,token:0,completed_operation:0,result:'',transfer_limit:2147483647,buffered_read:false,directories:[{index:0,path:''},{index:1,path:'OFE'},{index:2,path:'OFE/Backups'},{index:3,path:'Firmware'},{index:4,path:'Firmware/Archiv'},{index:5,path:'Leer'}],files:[
  {index:0,name:'readme.txt',path:'readme.txt',directory:0,size:4},
  {index:1,name:'trace.csv',path:'OFE/trace.csv',directory:1,size:4},
  {index:2,name:'backup.json',path:'OFE/Backups/backup.json',directory:2,size:4},
  {index:3,name:'display.bin',path:'Firmware/display.bin',directory:3,size:4},
  {index:4,name:'old.bin',path:'Firmware/Archiv/old.bin',directory:4,size:4},
  {index:5,name:'large.dat',path:'Firmware/large.dat',directory:3,size:8000000}
 ]};
 Object.assign(state,{drive:0,drive_instance:101,drive_limit:4,drives:[{index:0,instance:101,label:'USB 1',connected:true,selected:true,mounted:true,capacity_bytes:16000000000}]});
 page.on('pageerror',e=>errors.push(e.message));page.on('dialog',d=>confirmAccept?d.accept():d.dismiss());
 const folder=index=>state.directories.find(d=>d.index===index),joined=(dir,name)=>(dir.path?dir.path+'/':'')+name;
 await page.route('http://ofe.test/**',async route=>{
  const req=route.request(),u=new URL(req.url()),p=new URLSearchParams(req.postData()||''),json=j=>route.fulfill({contentType:'application/json',body:JSON.stringify(j)});
  if(['/usb/command','/usb/manage','/usb/write/begin'].includes(u.pathname)){assert.equal(Number(p.get('drive')),state.drive);assert.equal(Number(p.get('drive_instance')),state.drive_instance);assert.equal(Number(p.get('generation')),state.generation);}
  if(u.pathname==='/usb/status')return statusFailure?route.fulfill({status:503,body:'USB unavailable'}):json(state);
  if(u.pathname==='/usb/manage'){
   const action=p.get('action'),index=Number(p.get('index')),directory=Number(p.get('directory')),name=p.get('name');requests.push({action,index,directory,name,generation:p.get('generation')});assert.equal(Number(p.get('generation')),state.generation);
   const dest=folder(directory),f=state.files.find(f=>f.index===index),d=folder(index);assert(dest);let result='OK';
   if(action==='mkdir'){const next=joined(dest,name);if(state.directories.some(d=>d.path===next))result='EXISTS';else state.directories.push({index:Math.max(...state.directories.map(d=>d.index))+1,path:next});}
   else if(action==='move'){assert(f);const next=joined(dest,name||f.name);if(state.files.some(other=>other.path===next))result='EXISTS';else Object.assign(f,{name:name||f.name,path:next,directory});}
   else if(action==='move_dir'){assert(d&&d.path);const old=d.path,next=joined(dest,name);assert(dest.path!==old&&!dest.path.startsWith(old+'/'));for(const other of state.directories)if(other.path===old||other.path.startsWith(old+'/'))other.path=next+other.path.slice(old.length);for(const f of state.files)if(f.path.startsWith(old+'/'))f.path=next+f.path.slice(old.length);}
   else if(action==='delete'){assert(f);state.files=state.files.filter(other=>other!==f);}
   else if(action==='rmdir'){assert(d&&d.path);if(state.files.some(f=>f.path.startsWith(d.path+'/'))||state.directories.some(other=>other.path.startsWith(d.path+'/')))result='NOT_EMPTY';else state.directories=state.directories.filter(other=>other!==d);}
   else throw Error('Unexpected mutation');
   if(result==='OK')state.generation++;state.result=result;state.phase=result==='OK'?'idle':'error';state.message=result;state.completed_operation=++operation;
   return json({ok:true,operation});
  }
  if(u.pathname==='/usb/write/begin'){assert.equal(Number(p.get('generation')),state.generation);assert(folder(Number(p.get('directory'))));write={name:p.get('name'),size:Number(p.get('size')),directory:Number(p.get('directory'))};state.phase='receiving';state.token=123;return json({ok:true,token:123});}
  if(u.pathname==='/usb/write/chunk'){assert.equal(u.searchParams.get('token'),'123');return json({ok:true});}
  if(u.pathname==='/usb/write/commit'){assert(write);const next=joined(folder(write.directory),write.name);assert(!state.files.some(f=>f.path===next));state.files.push({...write,path:next,index:Math.max(...state.files.map(f=>f.index))+1});state.generation++;state.phase='idle';state.token=0;return json({ok:true});}
  if(u.pathname==='/usb/release'){if(!state.token)return route.fulfill({status:409,body:'Session already closed'});state.phase='idle';state.token=0;return json({ok:true});}
  if(u.pathname==='/usb/command'){if(p.get('action')==='read'){state.phase='ready';state.token=12;}return json({ok:true});}
  if(u.pathname==='/usb/file'){state.phase='idle';state.token=0;return route.fulfill({contentType:'application/octet-stream',body:'test'});}
  return route.fulfill({contentType:'text/html',body:'<html></html>'});
 });
 await page.goto('http://ofe.test/');await page.setContent(`<style>:root{--ofe-font-ui:Arial,sans-serif}${css}</style><select id="lang_sel"><option value="${lang}">${lang}</option></select><main><section class="panel" id="usb-storage" data-usb-purpose="update"></section><input id="master_fw_file" type="file" hidden><input id="module_fw_file" type="file" hidden><input id="c6_file" type="file" hidden></main>`);
 await page.evaluate(()=>document.getElementById('c6_file').onchange=()=>window.c6Selected=document.getElementById('c6_file').files[0].name);
 await page.addScriptTag({content:ui});await page.waitForSelector('.usb-directory[data-directory="Firmware"]');
 const expand=()=>page.getByRole('button',{name:de?'Alle Ordner aufklappen':'Expand all folders',exact:true}).click();
 assert.equal(await page.locator('.usb-row').count(),1);
 await page.locator('.usb-directory[data-directory="Firmware"]').click();assert.equal(await page.locator('.usb-row').count(),3);
 await page.locator('.usb-directory[data-directory="Firmware/Archiv"]').click();assert.equal(await page.locator('.usb-row').count(),4);
 assert.equal(await page.locator('.usb-directory[data-directory="Firmware/Archiv"]').getAttribute('aria-expanded'),'true');
 await page.locator('.usb-directory[data-directory="Firmware"]').click();assert.equal(await page.locator('.usb-row').count(),1);
 assert(await page.locator('.usb-directory[data-directory="Firmware"]').evaluate(e=>document.activeElement===e));
 await expand();assert.equal(await page.locator('.usb-row').count(),6);
 const row=name=>page.locator('.usb-row').filter({has:page.locator('.usb-path',{hasText:name})});
 await row('display.bin').click();
 await page.getByRole('button',{name:de?'Verschieben / Umbenennen':'Move / Rename',exact:true}).click();
 const dialog=page.getByRole('dialog');await dialog.locator('#usb-new-name').fill('USB Snapshot.bin');await dialog.locator('#usb-destination').selectOption('2');
 await dialog.screenshot({path:path.join(out,`move-${lang}-${width}.png`)});
 await dialog.getByRole('button',{name:de?'Verschieben / Umbenennen':'Move / Rename',exact:true}).click();
 await page.waitForFunction(()=>document.querySelector('#usb-selected').textContent==='OFE/Backups/USB Snapshot.bin');
 assert(!state.files.some(f=>f.path==='Firmware/display.bin'));assert(state.files.some(f=>f.path==='OFE/Backups/USB Snapshot.bin'));
 await row('old.bin').click();await page.getByRole('button',{name:de?'Verschieben / Umbenennen':'Move / Rename',exact:true}).click();
 await dialog.locator('#usb-destination').selectOption('2');await dialog.locator('#usb-new-name').fill('USB Snapshot.bin');await dialog.getByRole('button',{name:de?'Verschieben / Umbenennen':'Move / Rename',exact:true}).click();
 await page.waitForFunction(text=>document.querySelector('#usb-status').textContent.includes(text),de?'Nichts wurde überschrieben':'Nothing was overwritten');assert(state.files.some(f=>f.path==='Firmware/Archiv/old.bin'));
 await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));await page.waitForTimeout(60);assert((await page.locator('#usb-status').textContent()).includes(de?'Nichts wurde überschrieben':'Nothing was overwritten'));
 await page.getByRole('button',{name:de?'Neuer Ordner':'New folder',exact:true}).click();await dialog.locator('#usb-destination').selectOption('0');await dialog.locator('#usb-new-name').fill('Daten 2026');await dialog.getByRole('button',{name:de?'Anlegen':'Create',exact:true}).click();
 await page.waitForSelector('.usb-directory[data-directory="Daten 2026"]');const created=state.directories.find(d=>d.path==='Daten 2026');assert(created);
 await page.getByRole('button',{name:de?'Datei hochladen':'Upload file',exact:true}).click();
 await dialog.locator('#usb-upload').setInputFiles({name:'Foto (1).png',mimeType:'image/png',buffer:Buffer.from('picture')});
 assert.equal(await dialog.locator('#usb-destination').inputValue(),String(created.index));
 await dialog.getByRole('button',{name:de?'Hochladen':'Upload',exact:true}).click();
 await page.waitForFunction(()=>document.querySelector('#usb-selected').textContent==='Daten 2026/Foto (1).png');assert(state.files.some(f=>f.path==='Daten 2026/Foto (1).png'&&f.size===7));
 async function moveFolder(path,name,dest){await page.locator(`.usb-directory[data-directory="${path}"]`).click();await page.getByRole('button',{name:de?'Verschieben / Umbenennen':'Move / Rename',exact:true}).click();assert(!(await dialog.locator('#usb-destination option').evaluateAll((options,source)=>options.some(o=>o.textContent==='/'+source||o.textContent.startsWith('/'+source+'/')),path)));await dialog.locator('#usb-new-name').fill(name);await dialog.locator('#usb-destination').selectOption(String(dest));await dialog.getByRole('button',{name:de?'Verschieben / Umbenennen':'Move / Rename',exact:true}).click();await page.waitForFunction(text=>document.querySelector('#usb-status').textContent.includes(text),de?'Verschoben:':'Moved:');}
 await moveFolder('Daten 2026','Renamed',3);assert(state.files.some(f=>f.path==='Firmware/Renamed/Foto (1).png'));await moveFolder('Firmware/Renamed','Daten 2026',0);assert(state.files.some(f=>f.path==='Daten 2026/Foto (1).png'));await row('Foto (1).png').click();
 state.transfer_limit=3;await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));await page.waitForTimeout(100);
 await page.getByRole('button',{name:de?'Datei hochladen':'Upload file',exact:true}).click();await dialog.locator('#usb-upload').setInputFiles({name:'too-large.dat',mimeType:'application/octet-stream',buffer:Buffer.alloc(4)});await dialog.getByRole('button',{name:de?'Hochladen':'Upload',exact:true}).click();
 assert(await dialog.isVisible());assert((await dialog.locator('.usb-dialog-error').textContent()).includes(de?'Dateisystemmaximum':'filesystem maximum'));await dialog.getByRole('button',{name:de?'Abbrechen':'Cancel',exact:true}).click();
 state.transfer_limit=2147483647;await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));await page.waitForTimeout(100);
 // A changed directory generation must never reuse stale option indexes.
 await page.getByRole('button',{name:de?'Neuer Ordner':'New folder',exact:true}).click();await dialog.locator('#usb-new-name').fill('stale');state.generation++;
 await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));await page.waitForTimeout(100);const before=requests.length;
 await dialog.getByRole('button',{name:de?'Anlegen':'Create',exact:true}).click();assert.equal(requests.length,before);assert((await dialog.locator('.usb-dialog-error').textContent()).includes(de?'Dateiliste':'file list'));await dialog.getByRole('button',{name:de?'Abbrechen':'Cancel',exact:true}).click();
 const del=page.getByRole('button',{name:de?'Löschen':'Delete',exact:true});confirmAccept=false;await del.click();await page.waitForTimeout(80);assert(state.files.some(f=>f.path==='Daten 2026/Foto (1).png'));
 confirmAccept=true;await del.click();await page.waitForFunction(text=>document.querySelector('#usb-status').textContent.includes(text),de?'Gelöscht':'Deleted');assert(!state.files.some(f=>f.path==='Daten 2026/Foto (1).png'));
 await page.locator('.usb-directory[data-directory="OFE"]').click();await del.click();await page.waitForFunction(text=>document.querySelector('#usb-status').textContent.includes(text),de?'nicht leer':'not empty');assert(state.directories.some(d=>d.path==='OFE'));
 await page.locator('.usb-directory[data-directory="Leer"]').click();await del.click();await page.waitForFunction(()=>!document.querySelector('.usb-directory[data-directory="Leer"]'));assert(!state.directories.some(d=>d.path==='Leer'));
 await expand();await row('large.dat').click();assert(!(await page.getByRole('button',{name:de?'Herunterladen':'Download',exact:true}).isDisabled()));assert(!(await del.isDisabled()));
 await page.locator('#usb-search').fill('USB Snapshot');await page.waitForSelector('.usb-row');assert.equal(await page.locator('.usb-row').count(),1);
 await page.getByRole('button',{name:de?'Suche leeren':'Clear search',exact:true}).click();
 assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));await page.evaluate(()=>scrollTo(0,0));await page.locator('#usb-storage').screenshot({path:path.join(out,`tree-${lang}-${width}.png`)});
 await row('USB Snapshot.bin').click();await page.locator('#usb-target').selectOption('c6');
 await page.evaluate(()=>{document.getElementById('c6_file').disabled=true;document.getElementById('usb-target').dispatchEvent(new Event('change'))});assert(await page.getByRole('button',{name:de?'Firmware auswählen':'Select firmware',exact:true}).isDisabled());
 await page.evaluate(()=>{document.getElementById('c6_file').disabled=false;document.getElementById('usb-target').dispatchEvent(new Event('change'))});
 await page.getByRole('button',{name:de?'Firmware auswählen':'Select firmware',exact:true}).click();await page.waitForFunction(()=>window.c6Selected==='USB Snapshot.bin');assert.equal(state.token,0);
 await page.evaluate(()=>{window.exportResult=null;void window.ofeUsbSave(new Blob(['log data']),'test-log.txt').then(r=>window.exportResult=r)});await dialog.waitFor();await dialog.locator('#usb-destination').selectOption('4');await dialog.screenshot({path:path.join(out,`export-${lang}-${width}.png`)});await dialog.getByRole('button',{name:de?'Speichern':'Save',exact:true}).click();await page.waitForFunction(()=>window.exportResult===true);assert(state.files.some(f=>f.path==='Firmware/Archiv/test-log.txt'&&f.size===8));
 await page.evaluate(()=>{window.exportResult=null;void window.ofeUsbSave(new Blob(['cancel']),'cancel.txt').then(r=>window.exportResult=r)});await dialog.waitFor();await page.keyboard.press('Escape');await page.waitForFunction(()=>window.exportResult===false);assert(!state.files.some(f=>f.name==='cancel.txt'));assert.deepEqual(errors,[]);
 statusFailure=true;await page.evaluate(()=>document.dispatchEvent(new Event('visibilitychange')));await page.waitForFunction(text=>document.querySelector('#usb-connection').textContent===text,de?'Nicht erreichbar':'Unavailable');assert(await del.isHidden()||await del.isDisabled());
 assert.deepEqual(errors,[]);await page.close();
}
console.log('PASS: USB folder tree, collapsing/search, move/rename/collision, chosen-directory upload, size/stale-dialog guards, confirmed file/empty-folder deletion and disconnects; DE/EN 320/390/1440');
}finally{await browser.close()}})().catch(e=>{console.error(e);process.exitCode=1});
