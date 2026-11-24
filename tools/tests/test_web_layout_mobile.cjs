'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),vm=require('node:vm');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'../..'),src=path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
const read=file=>fs.readFileSync(path.join(src,file),'utf8');
const shell=read('WebShell.inc.h'),status=read('WebStatus.inc.h'),logic=read('WebLogic.inc.h'),responsive=read('WebLogicResponsive.h');
const css=file=>read(file).match(/<style>([\s\S]*?)<\/style>/)[1];
const shellCss=JSON.parse(shell.match(/html \+= F\(("<\/title><style>[^\n]+?") OFE_WEB_TYPOGRAPHY_STYLE/)[1]).match(/<style>([\s\S]*?)<\/style>/)[1];
const statusCss=status.match(/<style>([\s\S]*?)<\/style>/)[1];
const header=status.match(/<body>(<header>[\s\S]*?<\/header>)/)[1]
 .replace(/\)HTML" OFE_TRANSPORT_HEADER_HTML R"HTML\(/,read('WebTransportIcons.h').match(/R"ICON\(([\s\S]*?)\)ICON"/)[1])
 .replace('/logo.png','data:image/png;base64,'+fs.readFileSync(path.join(root,'assets/IceCube20_96.png')).toString('base64'));
let editor=logic.slice(logic.indexOf('html += F(R"HTML(',logic.indexOf('static void web_handle_logic()'))+'html += F(R"HTML('.length);
editor=editor.slice(0,editor.indexOf(')HTML");'));
const responsiveCss=responsive.match(/R"LOGICCSS\(([\s\S]*?)\)LOGICCSS"/)[1],responsiveJs=responsive.match(/R"LOGICUI\(([\s\S]*?)\)LOGICUI"/)[1];
const connectionJs=read('WebLogicConnections.h').match(/R"LOGICWIRE\(([\s\S]*?)\)LOGICWIRE"/)[1];
new vm.Script(responsiveJs);
new vm.Script(connectionJs);
for(const match of editor.matchAll(/<script>([\s\S]*?)<\/script>/g))new vm.Script(match[1]);
const automation=read('WebAutomationSignals.h').match(/R"HTML\(([\s\S]*?)\)HTML"/)[1];
const fixture={schema:1,name:'Werkbank',enabled:false,nodes:[
 {id:'i1',type:'input',name:'JBC Work',x:100,y:80,signal:'system:marker:1',value:false},
 {id:'i2',type:'input',name:'Freigabe',x:100,y:280,signal:'system:marker:2',value:true},
 {id:'a1',type:'and',name:'UND',x:400,y:100},
 {id:'o1',type:'output',name:'Dauerlauf',x:700,y:100,signal:'system:eco_inhibit'}],
 links:[{from:'i1',to:'a1',port:0},{from:'a1',to:'o1',port:0}]};
const out=path.join(__dirname,'build-checks/web-layout');fs.mkdirSync(out,{recursive:true});

(async()=>{
 const browser=await chromium.launch({headless:true,channel:process.env.OFE_TEST_BROWSER_CHANNEL||(process.platform==='win32'?'msedge':undefined)});
 let cases=0;
 try{
  for(const [width,height] of [[320,568],[390,844],[844,390],[1024,768],[1366,768],[1920,1080],[2560,1440]])for(const lang of ['de','en']){
   const context=await browser.newContext({viewport:{width,height},hasTouch:width<=1024,colorScheme:lang==='de'?'light':'dark'});
   const page=await context.newPage(),errors=[];let stored=structuredClone(fixture),saved=null;
   page.on('pageerror',e=>errors.push(e.message));
   await page.route('http://ofe.test/**',async route=>{
    const url=new URL(route.request().url()),p=url.pathname;
    if(p==='/logic/responsive.css')return route.fulfill({contentType:'text/css; charset=utf-8',body:responsiveCss});
    if(p==='/logic/responsive.js')return route.fulfill({contentType:'application/javascript; charset=utf-8',body:responsiveJs});
    if(p==='/logic/connections.js')return route.fulfill({contentType:'application/javascript; charset=utf-8',body:connectionJs});
    let data={};
    if(p==='/state')data={modules:[],system_inputs_mask:0,automation_markers:0};
    if(p==='/logic/list')data={active:0,items:[{slot:0,used:true,name:stored.name}]};
    if(p==='/logic/json')data=stored;
    if(p==='/logic/json/save'){saved=JSON.parse(route.request().postData());stored=structuredClone(saved);return route.fulfill({body:'OK'})}
    if(p==='/descriptors')data={modules:[]};
    if(p==='/logic/runtime')data={nodes:[],active:false};
    if(p==='/')return route.fulfill({contentType:'text/html; charset=utf-8',body:`<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1"><style>:root{--ofe-font-ui:Arial,Helvetica,sans-serif}body{font-family:var(--ofe-font-ui)}${shellCss}${css('WebHeaderStyles.h')}${css('WebFormStyles.h')}</style>${read('WebTheme.h').match(/R"THEME\(([\s\S]*?)\)THEME"/)[1]}${header}<main>${automation}${editor}</main><footer>Open Fume Extractor</footer>`});
    return route.fulfill({contentType:'application/json',body:JSON.stringify(data)});
   });
   await page.goto('http://ofe.test/');
   await page.waitForFunction(()=>graph.nodes.length===4);
   await page.evaluate(lang=>{document.getElementById('lang_sel').value=lang;applyLogicLang()},lang);
   await page.waitForFunction(()=>document.querySelector('#logic_zoom_value').textContent!=='');
   const label=`${width}x${height} ${lang}`;
   const bounds=async()=>page.evaluate(()=>({overflow:document.documentElement.scrollWidth>innerWidth+1,main:document.querySelector('main').getBoundingClientRect().width,
    panels:[...document.querySelectorAll('[data-logic-pane]')].filter(e=>getComputedStyle(e).display!=='none').map(e=>e.dataset.logicPane)}));
   let b=await bounds();assert.equal(b.overflow,false,label+' body overflow');assert.equal(b.main,width,label+' main not full width');
   assert.deepEqual(b.panels,width<=1200?['plan']:['blocks','plan','properties'],label+' visible panes');
   assert.deepEqual(errors,[],label+' initial JS errors');
   if(width<=1200){
    await page.click('[data-logic-tab=properties]');await page.selectOption('#logic_node_picker','a1');
    assert.equal(await page.inputValue('#node_name'),'UND');
    await page.selectOption('[data-logic-input="0"]','i2');
    assert.equal(await page.evaluate(()=>graph.links.filter(l=>l.to==='a1'&&l.port===0).length),2,label+' must preserve multiple sources');
    await page.locator('.logic-connection-row').first().locator('button').last().click();
    assert.equal(await page.evaluate(()=>graph.links.filter(l=>l.to==='a1'&&l.port===0).length),1);
    await page.click('[data-logic-tab=blocks]');await page.click(".logic-palette button[onclick=\"addNode('ton')\"]");
    assert.equal(await page.evaluate(()=>document.querySelector('.logic-top').dataset.pane),'plan');
    await page.click('[data-logic-tab=properties]');
    await page.fill('#node_name','Timer');await page.selectOption('#node_delay_unit','1000');await page.fill('#node_delay','5');
    assert.equal(await page.evaluate(()=>graph.nodes.find(n=>n.id===sel).delay),5000);
    await page.click('#logic_mode_switch');assert.equal(await page.getAttribute('#logic_mode_switch','aria-checked'),'true');
    await page.click('#logic_mode_switch');
    assert.equal((await bounds()).overflow,false,label+' properties overflow');
    if(width===390&&lang==='de'){await page.evaluate(()=>scrollTo(0,0));await page.screenshot({path:path.join(out,'mobile-properties.png'),fullPage:true})}
    await page.click('[data-logic-tab=plan]');
   }
   await page.evaluate(()=>{logicGridEnabled=false;logicSetZoom(.5);const w=document.getElementById('logic_workspace');w.scrollLeft=0;w.scrollTop=0});
   const node=page.locator('[data-id=i1] h3');await node.scrollIntoViewIfNeeded();const box=await node.boundingBox();
   const before=await page.evaluate(()=>({x:graph.nodes[0].x,y:graph.nodes[0].y}));
   await page.mouse.move(box.x+box.width/2,box.y+box.height/2);await page.mouse.down();
   await page.mouse.move(box.x+box.width/2+30,box.y+box.height/2+20,{steps:5});await page.mouse.up();
   const after=await page.evaluate(()=>({x:graph.nodes[0].x,y:graph.nodes[0].y}));
   if(Math.abs(after.x-before.x-60)>=1||Math.abs(after.y-before.y-40)>=1){await page.screenshot({path:path.join(out,'drag-failure.png'),fullPage:true});console.log({label,before,after,box,scroll:await page.evaluate(()=>({y:scrollY,header:document.querySelector('header').getBoundingClientRect().height,tabs:document.querySelector('.logic-mobile-tabs').getBoundingClientRect().toJSON()}))})}
   assert(Math.abs(after.x-before.x-60)<1,label+' zoomed drag x');assert(Math.abs(after.y-before.y-40)<1,label+' zoomed drag y');
   if(width===390){
    await node.scrollIntoViewIfNeeded();const touchBox=await node.boundingBox(),cdp=await context.newCDPSession(page);
    const point={x:touchBox.x+touchBox.width/2,y:touchBox.y+touchBox.height/2};
    await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[point]});
    await cdp.send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[{x:point.x+15,y:point.y+10}]});
    await cdp.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});
    const touched=await page.evaluate(()=>({x:graph.nodes[0].x,y:graph.nodes[0].y}));
    assert(Math.abs(touched.x-after.x-30)<1,label+' touch drag x');assert(Math.abs(touched.y-after.y-20)<1,label+' touch drag y');
    Object.assign(after,touched);await cdp.detach();
   }
   await page.locator('[data-logic-tool=plus]').click();assert.equal(await page.locator('#logic_zoom_value').textContent(),'60%');
   await page.locator('[data-logic-tool=fit]').click();
   await page.locator('.logic-file-toolbar .logic-command').last().click();
   await page.waitForFunction(()=>document.getElementById('logic_msg').textContent==='Geladen'||document.getElementById('logic_msg').textContent==='Loaded');
   assert(saved,label+' save missing');assert.equal(saved.nodes[0].x,after.x);assert.equal(saved.nodes[0].y,after.y);
   assert.equal(saved.enabled,false,label+' enabling must remain explicit');
   assert(await page.locator('#logic_msg').isVisible(),label+' save feedback hidden');
   b=await bounds();assert.equal(b.overflow,false,label+' final overflow');assert.deepEqual(errors,[],label+' JS errors');
   if(lang==='de'&&[320,390,1366,1920].includes(width)){await page.evaluate(()=>scrollTo(0,0));await page.screenshot({path:path.join(out,width+'.png'),fullPage:true})}
   await context.close();cases++;
  }
  // The status stream and shell pages have separate base styles.
  for(const base of [shellCss,statusCss])for(const width of [320,390,1366,1920,2560]){
   const page=await browser.newPage({viewport:{width,height:900}});
   await page.setContent(`<style>${base}${css('WebHeaderStyles.h')}${css('WebFormStyles.h')}</style><main><section class="panel"><h2>Netzwerk/System</h2><div class="grid"><label>Hostname<input value="open-fume-extractor"></label><label>IP<input value="192.168.1.247"></label></div></section></main>`);
   const size=await page.evaluate(()=>{const b=document.querySelector('main').getBoundingClientRect();return {width:b.width,left:b.left,right:b.right,overflow:document.documentElement.scrollWidth>innerWidth+1}});
   assert.equal(size.width,Math.min(width,1600));assert.equal(size.left,(width-size.width)/2);assert.equal(size.right,width-size.left);assert.equal(size.overflow,false);
   if(width===2560)await page.screenshot({path:path.join(out,base===shellCss?'bounded-shell.png':'bounded-status.png'),fullPage:true});
   await page.close();cases++;
  }
  for(const width of [320,390,1366,1920,2560]){
   const page=await browser.newPage({viewport:{width,height:900}});
   const markup=status.slice(status.indexOf('<main>')+6,status.indexOf('</main><script>'))
    .replace(/\)HTML";\s*static const char html2\[\] PROGMEM = R"HTML\(/,'')
    .replace(/\)HTML" MASTER_MCU_MODEL R"HTML\(/g,'MCU-P4');
   await page.setContent(`<meta name="viewport" content="width=device-width,initial-scale=1"><style>:root{--ofe-font-ui:Arial,Helvetica,sans-serif}${statusCss}${css('WebHeaderStyles.h')}${css('WebFormStyles.h')}</style><main>${markup}</main>`);
   const layout=await page.evaluate(()=>{
    const main=document.querySelector('main'),s=getComputedStyle(main),rect=e=>{const r=e.getBoundingClientRect();return {left:r.left,width:r.width}};
    return {main:rect(main),padding:parseFloat(s.paddingLeft),overflow:document.documentElement.scrollWidth>innerWidth+1,
     modules:[...main.querySelectorAll(':scope>.status-modules')].map(rect),sections:[...main.children].map(rect)};
   });
   assert.equal(layout.main.width,width);assert.equal(layout.overflow,false);assert.equal(layout.modules.length,3);
   const full=width-2*layout.padding;
   for(const module of layout.modules){assert.equal(module.width,full);assert.equal(module.left,layout.padding)}
   for(const section of layout.sections){assert.equal(section.width,full);assert.equal(section.left,layout.padding)}
   if(width===1920)await page.screenshot({path:path.join(out,'full-width-modules.png'),fullPage:true});
   await page.close();cases++;
  }
  console.log(`PASS: ${cases} DE/EN mobile, landscape, laptop and wide-desktop layouts; uniform full-width status, bounded shell, block editing, multiple source links, zoomed drag, simulation and save`);
  console.log('Screenshots: '+out);
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exit(1)});
