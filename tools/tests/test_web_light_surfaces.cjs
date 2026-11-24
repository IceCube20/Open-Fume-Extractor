'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {chromium}=require('playwright'),{src,sheets,inventory}=require('./audit_web_colors.cjs');
const theme=fs.readFileSync(path.join(src,'WebTheme.h'),'utf8').match(/R"THEME\(([\s\S]*?)\)THEME"/)[1];
const out=path.resolve(__dirname,'build-checks/light-surfaces');fs.mkdirSync(out,{recursive:true});
const pages=['WebStatus.inc.h','WebDiagnostics.inc.h','WebLogic.inc.h','WebUsbStorageUi.h','WebConfig.inc.h','WebShell.inc.h'];
const exception=selector=>/::|:hover|:active|button|\.usb-dot$|\.uni-primary-action$|\.btn$|\.nav \.active$|\.switch|\.lan-|\.logic-port(?:\W|$)|\.jbu-toggle|(?:^|[\s.>-])(?:diag-dot|logic-live-dot|diag-legend-dot)|\.diag-filter-chip\.active/.test(selector);
(async()=>{
 const browser=await chromium.launch({headless:true,channel:'msedge'});let count=0;
 try{
  for(const name of pages){
   const page=await browser.newPage({viewport:{width:1440,height:1000}});
   const rows=(await inventory(page,name,true)).filter(r=>r.selector).flatMap(r=>r.selector.split(',').map(selector=>({...r,selector:selector.trim()}))).filter(r=>(/^rgb\(/.test(r.bg)||!r.bg&&/^rgb\(/.test(r.color)&&!r.selector.includes(':'))&&!exception(r.selector)&&!r.selector.includes('led-word'));
   await page.setContent('<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1"><style>'+sheets('WebShell.inc.h').join('')+'</style>'+theme.replace('<style>','<style id="theme-sheet">')+'<style>'+sheets(name).join('')+'</style><main id="samples"></main>');
   const result=await page.evaluate(async rows=>{
    const root=document.getElementById('samples'),cases=[],unsupported=[];
    for(const row of rows){
     let host=document.createElement('section'),parent=host;
     for(const part of row.selector.split(/\s*>\s*|\s+/)){
      const match=part.replace(':focus','').match(/^([a-z][\w-]*)?(#[\w-]+)?((?:\.[\w-]+)*)(\[[^\]]+\])?$/i);
      if(!match){parent=null;break}
      const el=document.createElement(match[1]||(part.includes('inline-alias')?'input':'div'));if(match[2])el.id=match[2].slice(1);el.className=match[3].split('.').filter(Boolean).join(' ');
      if(match[4]){const a=match[4].slice(1,-1).split('=');el.setAttribute(a[0],(a[1]||'').replace(/^"|"$/g,''))}
      parent.append(el);parent=el;
     }
     if(!parent||!parent.matches(row.selector.replace(':focus',''))){unsupported.push(row.selector);continue}
     parent.append(document.createTextNode(row.selector));root.append(host);if(row.selector==='header')document.body.append(parent);if(row.selector.endsWith(':focus'))parent.focus();cases.push({el:parent,row});
    }
    const values=()=>cases.map(({el,row})=>{const s=getComputedStyle(el);let p=el,effectiveBg=s.backgroundColor;while(effectiveBg==='rgba(0, 0, 0, 0)'&&p.parentElement){p=p.parentElement;effectiveBg=getComputedStyle(p).backgroundColor}return {selector:row.selector,bg:s.backgroundColor,effectiveBg,color:s.color,image:s.backgroundImage}});
    document.documentElement.dataset.ofeTheme='dark';const sheet=document.getElementById('theme-sheet').sheet;sheet.disabled=true;await new Promise(r=>setTimeout(r,200));const baseline=values();sheet.disabled=false;await new Promise(r=>setTimeout(r,200));const dark=values();
    document.documentElement.dataset.ofeTheme='light';await new Promise(r=>setTimeout(r,200));const light=values();
    return {baseline,dark,light,unsupported};
   },rows);
   const failures=[];
   for(let i=0;i<result.light.length;i++){
    const row=result.light[i],original=result.dark[i],rgb=row.bg.match(/[\d.]+/g).map(Number);
    const lum=rgb.slice(0,3).map(n=>n/255).map(n=>n<=.04045?n/12.92:((n+.055)/1.055)**2.4).reduce((s,n,i)=>s+n*[.2126,.7152,.0722][i],0);
    assert(lum>.5||rgb[3]===0,name+' retains dark surface '+JSON.stringify(row));
    assert.equal(original.bg,result.baseline[i].bg,name+' dark-mode regression '+row.selector);
    const luma=value=>value.match(/[\d.]+/g).slice(0,3).map(Number).map(n=>n/255).map(n=>n<=.04045?n/12.92:((n+.055)/1.055)**2.4).reduce((s,n,i)=>s+n*[.2126,.7152,.0722][i],0);
    if(row.effectiveBg!=='rgba(0, 0, 0, 0)'){const fg=luma(row.color),bg=luma(row.effectiveBg),contrast=(Math.max(fg,bg)+.05)/(Math.min(fg,bg)+.05);if(contrast<4.5)failures.push(row.selector+' ('+contrast.toFixed(2)+', '+row.color+')')}
    count++;
   }
   assert.deepEqual(failures,[],name+' low-contrast text');
   assert.deepEqual(result.unsupported,[],name+' untested selectors');
   if(name==='WebStatus.inc.h'||name==='WebDiagnostics.inc.h'){
    await page.addStyleTag({content:'#samples{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px}#samples>section{min-width:0;border:1px solid var(--line);padding:12px;overflow-wrap:anywhere}#samples>section>div{min-height:28px}body{font-family:Arial,sans-serif}#samples button{min-width:0}'});await page.screenshot({path:path.join(out,name+'.png'),fullPage:true});
   }
   await page.close();
  }
  console.log('PASS: '+count+' actual CSS surface/state selectors are light; original dark backgrounds preserved, including page styles loaded after the theme');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
