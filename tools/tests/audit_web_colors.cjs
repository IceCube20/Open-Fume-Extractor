'use strict';
const fs=require('node:fs'),path=require('node:path'),{chromium}=require('playwright');
const src=path.resolve(__dirname,'../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
function sheets(name){
 const text=fs.readFileSync(path.join(src,name),'utf8');
 if(name==='WebShell.inc.h')return [JSON.parse(text.match(/html \+= F\(("<\/title><style>[^\n]+?") OFE_WEB_TYPOGRAPHY_STYLE/)[1]).match(/<style>([\s\S]*?)<\/style>/)[1]];
 return [...text.matchAll(/<style>([\s\S]*?)<\/style>/g)].map(m=>m[1]).concat(name==='WebLogicResponsive.h'?[text.match(/R"LOGICCSS\(([\s\S]*?)\)LOGICCSS"/)[1]]:[]);
}
async function inventory(page,name,includeText=false){
 return page.evaluate(({css,includeText})=>{
  const results=[];
  for(const text of css){const sheet=new CSSStyleSheet();sheet.replaceSync(text);
   const visit=rules=>{for(const r of rules){if(r.cssRules)visit(r.cssRules);if(!r.style)continue;const bg=r.style.background||r.style.backgroundColor;if((includeText&&r.style.color&&!r.style.color.includes('var('))||(bg&&!bg.includes('var(')&&!['none','transparent','inherit'].includes(bg)))results.push({selector:r.selectorText,bg,color:r.style.color,border:r.style.borderColor});}};
   visit(sheet.cssRules);
  }return results;
 },{css:sheets(name),includeText});
}
if(require.main===module)(async()=>{const browser=await chromium.launch({headless:true,channel:'msedge'});try{const page=await browser.newPage();for(const name of fs.readdirSync(src).filter(n=>/^Web.*\.h$/.test(n)&&!['WebTheme.h','WebLogo.h','WebLogicRouterAssets.h'].includes(n))){const rows=await inventory(page,name,process.argv.includes('--text'));if(rows.length)console.log(name+'\n'+rows.map(r=>JSON.stringify(r)).join('\n'));}}finally{await browser.close()}})().catch(e=>{console.error(e);process.exitCode=1});
module.exports={sheets,inventory,src};
