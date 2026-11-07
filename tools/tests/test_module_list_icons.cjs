const fs=require('node:fs');
const path=require('node:path');
const assert=require('node:assert/strict');
const {chromium}=require('playwright');
const src=fs.readFileSync(path.join(__dirname,'../../OpenFumeExtractorMaster/src/WebStatus.inc.h'),'utf8');
const css=src.match(/<style>([\s\S]*?)<\/style>/)[1];
const helpers=['moduleSerialGlyph','moduleSwitchGlyph','moduleFanGlyph','moduleMonitorGlyph','jbcUsbPortGlyph','escHtml']
  .map(name=>src.split('\n').find(line=>line.startsWith('function '+name+'('))).join('\n');
const script=helpers+'\n'+src.slice(src.indexOf('function moduleListType('),src.indexOf('function amn('));
assert(src.includes('<td>${moduleListType(m)}</td>'),'Real module table uses the tested icon renderer');
const modules=[
  {type:1,type_name:'JBC FAE Bus',glyph:'moduleSerialGlyph',color:'rgb(57, 199, 121)'},
  {type:9,type_name:'JBC USB',glyph:'jbcUsbPortGlyph',color:'rgb(57, 199, 121)'},
  {type:2,type_name:'Fan/IO',glyph:'moduleSwitchGlyph',color:'rgb(74, 144, 217)'},
  {type:3,type_name:'Fan/IO Pro',glyph:'moduleSwitchGlyph',color:'rgb(74, 144, 217)'},
  {type:5,type_name:'Weller Zero Smog Bus',glyph:'moduleFanGlyph',color:'rgb(85, 185, 202)'},
  {type:6,type_name:'Display P4 800x480',glyph:'moduleMonitorGlyph',color:'rgb(181, 140, 255)'},
  {type:7,type_name:'Universal RS232 Bridge',glyph:'moduleSerialGlyph',color:'rgb(241, 184, 75)'},
  {type:8,type_name:'Modbus RTU Bridge',glyph:'moduleSerialGlyph',color:'rgb(226, 139, 82)'}
];
(async()=>{
  const browser=await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});
  try{
    const page=await browser.newPage({viewport:{width:1440,height:900}});
    await page.setContent('<style>'+css+'</style><main><div class="table-wrap"><table><thead><tr><th>Addr</th><th>Role</th><th>Type</th><th>Name</th></tr></thead><tbody id="mt"></tbody></table></div></main>');
    await page.evaluate(code=>(0,eval)(code),script);
    await page.evaluate(modules=>document.getElementById('mt').innerHTML=modules.map((m,i)=>'<tr><td>0x'+(16+i).toString(16)+'</td><td>-</td><td>'+moduleListType(m)+'</td><td>'+m.type_name+'</td></tr>').join(''),modules);
    for(let i=0;i<modules.length;i++){
      const row=page.locator('#mt tr').nth(i),m=modules[i];
      assert.equal(await row.locator('.module-list-type>span:last-child').textContent(),m.type_name);
      const actual=await row.locator('.module-list-icon').evaluate(e=>({color:getComputedStyle(e).color,w:e.getBoundingClientRect().width,svg:e.innerHTML}));
      assert.equal(actual.color,m.color);assert.equal(actual.w,28);
      const expected=await page.evaluate(name=>{const box=document.createElement('div');box.innerHTML=window[name]();return box.innerHTML;},m.glyph);
      assert.equal(actual.svg,expected,'Reuse the exact card SVG, not a separate icon design');
      assert.equal(await row.locator('.module-list-icon svg').count(),1);
    }
    const escaped=await page.evaluate(()=>moduleListType({type:6,type_name:'<img src=x onerror=alert(1)>'}));
    assert(!escaped.includes('<img'),'Type names stay escaped');
    const output=path.join(__dirname,'build-checks/module-list-icons');fs.mkdirSync(output,{recursive:true});
    await page.screenshot({path:path.join(output,'desktop.png'),fullPage:true});
    await page.setViewportSize({width:390,height:900});
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'Wide table stays inside its scroll container');
    await page.screenshot({path:path.join(output,'mobile.png'),fullPage:true});
    console.log('PASS: eight module types reuse card SVGs/colors in the status table, fixed icon dimensions, escaped names and mobile scrolling');
  }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
