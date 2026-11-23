'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),{chromium}=require('playwright');
const root=path.resolve(__dirname,'../..'),src=path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
const source=fs.readFileSync(path.join(src,'WebStatus.inc.h'),'utf8');
const css=source.match(/<style>([\s\S]*?)<\/style>/)[1]+fs.readFileSync(path.join(src,'WebFormStyles.h'),'utf8').match(/<style>([\s\S]*?)<\/style>/)[1];
const section=(start,end)=>source.slice(source.indexOf(start),source.indexOf(end,source.indexOf(start)));
const line=name=>source.split(/\r?\n/).find(row=>row.startsWith(`function ${name}(`));
const script=section('function universalProtocolBuilderDirty()',"document.addEventListener('keydown'")+'\n'+
  section('function modbusBuilderModalDirty()',"document.addEventListener('keydown'")+'\n'+
  line('fanIoEditorOpen')+'\n'+line('fanIoEditorClose');
const out=path.join(__dirname,'build-checks/editor-modal-mobile');fs.mkdirSync(out,{recursive:true});
const kinds=[['rs232',80,'upb_modal_overlay','.mb-modal-close','.mb-modal-scroll','universalProtocolBuilderOpen'],
  ['modbus',96,'mb_modal_overlay','.mb-modal-close','.mb-modal-scroll','modbusBuilderOpen'],
  ['fanio',32,'fanio_editor_modal','.fanio-editor-close','.fanio-editor-scroll','fanIoEditorOpen'],
  ['fanio-pro',33,'fanio_editor_modal','.fanio-editor-close','.fanio-editor-scroll','fanIoEditorOpen']];
(async()=>{
 const browser=await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});
 try{
  for(const lang of ['de','en'])for(const size of [{width:320,height:568},{width:390,height:660},{width:844,height:390},{width:390,height:300},{width:1440,height:900}])for(const [kind,addr,id,close,scroll,open] of kinds){
   const page=await browser.newPage({viewport:size,isMobile:size.width<1000,hasTouch:true}),errors=[];
   page.on('pageerror',error=>errors.push(error.message));
   await page.setContent(`<meta name="viewport" content="width=device-width,initial-scale=1"><style>:root{--ofe-font-ui:Arial,sans-serif}${css}</style><main style="height:1800px">OFE</main>`);
   await page.addScriptTag({content:`var uiLang=${JSON.stringify(lang)},fanIoEditorDraft=null;
    const escHtml=v=>String(v).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('"','&quot;');
    const escAttr=escHtml,hx=a=>'0x'+Number(a).toString(16),amn=m=>m.label,t=k=>k;
    window.lastState={modules:[{addr:80,type:7,label:'UniversalRS232WerkbankNord'},{addr:96,type:8,label:'ModbusRegisterWerkbankNord'},{addr:32,type:2,label:'FanIOModulWerkbankNord'},{addr:33,type:3,label:'FanIOProModulWerkbankNord'}]};
    const longForm=()=>'<label>Alias<input value="Werkbank"></label><div style="height:2000px">I/O</div>';
    function modbusBuilderEditorHtml(){return '<div class="modbus-builder">'+longForm()+'</div>'}
    function modbusBuilderSync(){}function modbusBuilderOutputRefresh(){}
    async function universalProtocolBuilderLoadModule(){}function universalProtocolBuilderOutputRefresh(){}
    function fanIoEditorState(m){return {title:m.label,addr:m.addr}}
    function fanIoEditorRender(){document.getElementById('fanio_editor_scroll').innerHTML=longForm()}
    ${script}`});
   await page.evaluate(async({open,addr})=>{window.scrollTo(0,600);await window[open](addr)},{open,addr});
   if(kind==='modbus')assert(!/gro\u00dfer Editor|large editor/.test(await page.locator('#'+id+' .mb-modal-title-copy small').textContent()));
   const button=page.locator('#'+id+' '+close),body=page.locator('#'+id+' '+scroll);
   assert(await body.evaluate(e=>e.clientWidth>=200&&e.clientHeight>32),'editor content must retain usable space');
   const reachable=async()=>{
    const r=await button.boundingBox();assert(r,kind);
    assert(r.x>=0&&r.y>=0&&r.x+r.width<=size.width+1&&r.y+r.height<=size.height+1,`${kind}/${lang}/${JSON.stringify(size)} close outside viewport: ${JSON.stringify(r)}`);
    assert(r.width>=44&&r.height>=44,'44px touch target');
    assert(await button.evaluate(e=>{const r=e.getBoundingClientRect();return e.contains(document.elementFromPoint(r.x+r.width/2,r.y+r.height/2))}),'close must receive touches');
   };
   await reachable();const initial=await button.boundingBox();
   await body.evaluate(e=>{e.scrollTop=e.scrollHeight});await reachable();
   assert(await body.evaluate(e=>e.scrollTop>0),'editor content must scroll independently');
   assert(Math.abs((await button.boundingBox()).y-initial.y)<1,'close stays in the header while scrolling');
   await body.evaluate(e=>{e.scrollTop=0});
   if(size.width===390&&size.height===660&&lang==='de')await page.screenshot({path:path.join(out,kind+'.png'),fullPage:false});
   if(kind==='rs232'||kind==='modbus'){
    await page.locator('#'+id+' input').first().evaluate(e=>e.dataset.dirty='1');
    page.once('dialog',d=>d.dismiss());await button.tap();assert.equal(await page.locator('#'+id).count(),1,'dirty cancel keeps editor');
    page.once('dialog',d=>d.accept());
   }
   await button.tap();assert.equal(await page.locator('#'+id).count(),0,'close tap removes actual modal');
   assert.equal(await page.evaluate(()=>document.body.style.overflow),'');
   assert(!await page.locator('body').evaluate(e=>e.classList.contains('mb-modal-open')));
   assert.deepEqual(errors,[]);await page.close();
  }
  console.log('PASS: actual RS232/Modbus/FanIO/Pro modal markup and close handlers; long aliases, portrait/landscape/short viewport/desktop, 44px tap, scrolled content, dirty confirmation; DE/EN');
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
