'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'../..'),src=path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'),vendor=path.join(root,'assets/vendor/libavoid');
const read=file=>fs.readFileSync(path.join(src,file),'utf8'),logic=read('WebLogic.inc.h'),assets=read('WebLogicResponsive.h');
let editor=logic.slice(logic.indexOf('html += F(R"HTML(',logic.indexOf('static void web_handle_logic()'))+'html += F(R"HTML('.length);editor=editor.slice(0,editor.indexOf(')HTML");'));
const css=assets.match(/R"LOGICCSS\(([\s\S]*?)\)LOGICCSS"/)[1],js=assets.match(/R"LOGICUI\(([\s\S]*?)\)LOGICUI"/)[1],wires=read('WebLogicConnections.h').match(/R"LOGICWIRE\(([\s\S]*?)\)LOGICWIRE"/)[1];
const shell=JSON.parse(read('WebShell.inc.h').match(/html \+= F\(("<\/title><style>[^\n]+?") OFE_WEB_TYPOGRAPHY_STYLE/)[1]).match(/<style>([\s\S]*?)<\/style>/)[1];
const automation=read('WebAutomationSignals.h').match(/R"HTML\(([\s\S]*?)\)HTML"/)[1];
const out=path.join(__dirname,'build-checks/logic-selection');fs.mkdirSync(out,{recursive:true});
const settle=page=>page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
const positions=page=>page.evaluate(()=>graph.nodes.map(n=>({id:n.id,x:n.x,y:n.y})));
const selected=page=>page.evaluate(()=>logicSelectionIds().sort());
const center=async(page,id)=>page.locator(`[data-id=${id}] h3`).evaluate(e=>{const r=e.getBoundingClientRect();return{x:r.x+r.width/2,y:r.y+r.height/2}});
const point=(page,x,y)=>page.evaluate(({x,y})=>{const r=document.getElementById('logic_canvas').getBoundingClientRect();return{x:r.x+x*logicZoom,y:r.y+y*logicZoom}},{x,y});
async function mouseDrag(page,from,to){await page.mouse.move(from.x,from.y);await page.mouse.down();await page.mouse.move(to.x,to.y,{steps:8});await page.mouse.up()}

(async()=>{
 const browser=await chromium.launch({headless:true,channel:'msedge'});
 try{
  for(const width of [430,1920]){
   const page=await browser.newPage({viewport:{width,height:1400},hasTouch:true}),errors=[];let stored={schema:1,name:'Selection',enabled:false,nodes:[],links:[]};
   page.on('pageerror',e=>errors.push(e.message));
   await page.route('http://ofe.test/**',async route=>{
    const p=new URL(route.request().url()).pathname,scripts={'/logic/responsive.js':js,'/logic/connections.js':wires,'/logic/router.mjs':fs.readFileSync(path.join(vendor,'index.js'),'utf8')};
    if(scripts[p])return route.fulfill({contentType:'application/javascript',body:scripts[p]});
    if(p==='/logic/libavoid.wasm')return route.fulfill({contentType:'application/wasm',body:fs.readFileSync(path.join(vendor,'libavoid.wasm'))});
    if(p==='/logic/responsive.css')return route.fulfill({contentType:'text/css',body:css});
    if(p==='/')return route.fulfill({contentType:'text/html; charset=utf-8',body:`<meta name="viewport" content="width=device-width,initial-scale=1"><style>:root{--ofe-font-ui:Arial,sans-serif}${shell}</style><select id="lang_sel"><option>de</option></select><main>${automation}${editor}</main>`});
    if(p==='/logic/json/save'){stored=JSON.parse(route.request().postData());return route.fulfill({body:'OK'})}
    const data=p==='/logic/list'?{active:0,max_nodes:64,max_links:128,items:[{slot:0,used:true,name:'Selection'}]}:p==='/logic/json'?stored:{modules:[]};
    return route.fulfill({contentType:'application/json',body:JSON.stringify(data)});
   });
   await page.goto('http://ofe.test/');await page.waitForFunction(()=>logicRouterStatus==='ready'&&document.getElementById('logic_msg').textContent==='Geladen');
   const reset=async zoom=>{
    await page.evaluate(zoom=>{
     const n=(id,type,x,y)=>({...nodeDefaults(type),id,type,x,y,name:id,signal:type==='input'?'manual':''});
     graph={schema:1,name:'Selection',enabled:false,nodes:[n('i','input',60,100),n('g','and',420,220),n('o','output',820,400),n('other','and',1000,700)],links:[{from:'i',to:'g',port:0},{from:'g',to:'o',port:0}]};
     logicSelectionMode=false;logicGridEnabled=false;clearLogicRouteHistory();render();logicSetZoom(zoom);showLogicPane('plan');const w=document.getElementById('logic_workspace');w.scrollLeft=0;w.scrollTop=0;
     const r=getLogicRoutes().routes[0];graph.links[0].route={p:r.points.map(p=>[p.x,p.y]),s:[r.start.x,r.start.y],t:[r.end.x,r.end.y]};drawLinks();
    },zoom);await settle(page);
   };
   for(const zoom of [.5,1,1.5]){
    if(width===430&&zoom> .5)continue;
    await reset(zoom);await page.locator('[data-id=i] h3').click();await page.locator('[data-id=g] h3').click({modifiers:['Control']});
    assert.deepEqual(await selected(page),['g','i']);assert.equal(await page.locator('.logic-selected').count(),2);assert.equal(await page.locator('#node_name').isDisabled(),true);
    const before=await positions(page),route=await page.evaluate(()=>structuredClone(graph.links[0].route)),p=await center(page,'g');
    await mouseDrag(page,p,{x:p.x+36*zoom,y:p.y+54*zoom});
    const after=await positions(page);
    after.forEach((n,i)=>{const moving=['i','g'].includes(n.id);assert.equal(n.x,before[i].x+(moving?36:0));assert.equal(n.y,before[i].y+(moving?54:0))});
    assert.deepEqual(await page.evaluate(()=>graph.links[0].route.p),route.p.map(([x,y])=>[x+36,y+54]),'Internal manual wire translates with the whole group');
    const snapshot=await positions(page),routeSnapshot=await page.evaluate(()=>structuredClone(graph.links[0].route)),start=await center(page,'g');
    await page.mouse.move(start.x,start.y);await page.mouse.down();await page.mouse.move(start.x+20,start.y+20,{steps:5});await page.keyboard.press('Escape');await page.mouse.up();
    assert.deepEqual(await positions(page),snapshot);assert.deepEqual(await page.evaluate(()=>graph.links[0].route),routeSnapshot);
    await page.evaluate(()=>{logicGridEnabled=true;render()});const gridStart=await center(page,'g');
    await mouseDrag(page,gridStart,{x:gridStart.x+42*zoom,y:gridStart.y+37*zoom});
    const snapped=await positions(page);assert.equal(snapped[0].x-snapshot[0].x,36);assert.equal(snapped[1].x-snapshot[1].x,36);assert.equal(snapped[1].y-snapped[0].y,snapshot[1].y-snapshot[0].y);
    await page.evaluate(()=>saveGraph());assert.deepEqual(stored.nodes.map(n=>({id:n.id,x:n.x,y:n.y})),snapped);assert(!JSON.stringify(stored).includes('logicSelectedNodes'));
   }
   await reset(.5);
   const from=await point(page,20,20),to=await point(page,590,420);await mouseDrag(page,from,to);
   assert.deepEqual(await selected(page),['g','i'],'Marquee selects the two enclosed nodes');
   await page.locator('[data-id=g] h3').click({modifiers:['Control']});assert.deepEqual(await selected(page),['i']);await page.locator('[data-id=g] h3').click({modifiers:['Shift']});assert.deepEqual(await selected(page),['g','i']);
   const cancelBox=await point(page,10,10),cancelEnd=await point(page,1100,900);
   await page.mouse.move(cancelBox.x,cancelBox.y);await page.mouse.down();await page.mouse.move(cancelEnd.x,cancelEnd.y,{steps:5});await page.keyboard.press('Escape');await page.mouse.up();assert.deepEqual(await selected(page),['g','i'],'Escape restores the selection before the marquee');
   const borderStart=await center(page,'g'),borderBefore=await positions(page);
   await page.mouse.move(borderStart.x,borderStart.y);await page.mouse.down();await page.evaluate(()=>moveLogicNodeGroup({pointerId:drag.pointerId,clientX:drag.startX-1000*logicZoom,clientY:drag.startY}));
   const borderAfter=await positions(page);assert.equal(borderAfter[0].x,0);assert.equal(borderAfter[1].x-borderAfter[0].x,borderBefore[1].x-borderBefore[0].x);await page.keyboard.press('Escape');await page.mouse.up();assert.deepEqual(await positions(page),borderBefore);
   await page.evaluate(()=>render());assert.deepEqual(await selected(page),['g','i'],'Selection survives redraw');
   if(width===1920)await page.screenshot({path:path.join(out,'desktop-group.png'),fullPage:true});
   await page.evaluate(()=>duplicateSelected());assert.equal(await page.evaluate(()=>graph.nodes.length),6);assert.equal(await page.evaluate(()=>graph.links.length),3);assert.equal((await selected(page)).length,2);
   await page.evaluate(()=>deleteSelected());assert.equal(await page.evaluate(()=>graph.nodes.length),4);assert.equal(await page.evaluate(()=>graph.links.length),2);
   await page.evaluate(()=>setLogicSelection(['i']));await page.locator('[data-id=i] h3').click();if(width<=1200)await page.click('[data-logic-tab=properties]');await page.locator('#node_name').focus();await page.keyboard.press('Control+a');assert.deepEqual(await selected(page),['i'],'Text selection must not select diagram blocks');
   if(width<=1200)await page.click('[data-logic-tab=plan]');await page.locator('#logic_workspace').focus();await page.keyboard.press('Control+a');assert.equal((await selected(page)).length,4);await page.keyboard.press('Escape');assert.equal((await selected(page)).length,0);
   await reset(.5);
   await page.locator('#logic_workspace').scrollIntoViewIfNeeded();
   if(width===430)await page.locator('[data-logic-selection-tool=select]').click();
   const wirePoint=async index=>page.locator('.logic-line:not(.pending)').nth(index).evaluate(e=>{
     const total=e.getTotalLength();
     for(let t=.03;t<.98;t+=.03){const p=e.getPointAtLength(total*t),r=document.getElementById('logic_canvas').getBoundingClientRect(),q={x:r.x+p.x*logicZoom,y:r.y+p.y*logicZoom},hit=document.elementFromPoint(q.x,q.y);if(hit===e||hit&&hit.classList.contains('logic-line-halo'))return {x:q.x,y:q.y}}
     const p=e.getPointAtLength(total/2),r=document.getElementById('logic_canvas').getBoundingClientRect(),x=r.x+p.x*logicZoom,y=r.y+p.y*logicZoom;throw new Error('No exposed wire: '+JSON.stringify({p,x,y,z:logicZoom,b:e.getBoundingClientRect().toJSON(),hit:document.elementsFromPoint(x,y).map(n=>n.tagName+'.'+n.classList.value)}));
   });
   await page.evaluate(()=>setLogicSelection([]));
   if(width===430){
    const touch=await page.context().newCDPSession(page);
    for(const i of [0,1]){const p=await wirePoint(i);await touch.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[p]});await touch.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]})}
    await touch.detach();
   }else{
    await page.keyboard.down('Control');let wp=await wirePoint(0);await page.mouse.click(wp.x,wp.y);wp=await wirePoint(1);await page.mouse.click(wp.x,wp.y);await page.keyboard.up('Control');
   }
   assert.equal(await page.evaluate(()=>logicSelectedLinks.size),2,'Ctrl-click selects two wires');
   assert.equal(await page.locator('.logic-line.selected').count(),2);
   assert.equal(await page.evaluate(()=>!!linkFrom||!!lineDrag),false,'Multi-selection must not reconnect wires');
   await page.evaluate(()=>unlockLogicConnection());assert(!await page.evaluate(()=>graph.links.some(l=>l.route)),'Bulk unlock');
   await page.locator('#logic_workspace').focus();await page.keyboard.press('Delete');assert.equal(await page.evaluate(()=>graph.links.length),0);assert.equal(await page.evaluate(()=>graph.nodes.length),4,'Wire deletion preserves nodes');
   if(width===430){
    await reset(.5);await page.locator('[data-logic-selection-tool=select]').click();const cdp=await page.context().newCDPSession(page);
    const tap=async p=>{await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[p]});await cdp.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]})};
    await tap(await center(page,'i'));await tap(await center(page,'g'));assert.deepEqual(await selected(page),['g','i']);
    await page.locator('[data-logic-selection-tool=select]').click();const before=await positions(page),p=await center(page,'g');
    await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[p]});await cdp.send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[{x:p.x+18,y:p.y+27}]});await cdp.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});
    const after=await positions(page);assert.equal(after[0].x-before[0].x,36);assert.equal(after[1].y-before[1].y,54);
    const cancelStart=await center(page,'g');await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[cancelStart]});await cdp.send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[{x:cancelStart.x+20,y:cancelStart.y+20}]});await cdp.send('Input.dispatchTouchEvent',{type:'touchCancel',touchPoints:[]});assert.deepEqual(await positions(page),after,'Touch cancellation restores all group positions');
    await cdp.detach();await page.screenshot({path:path.join(out,'mobile-group.png'),fullPage:true});
   }
   assert.equal(await page.locator('.logic-selection-box').count(),0);assert.deepEqual(errors,[]);await page.close();console.log(`PASS: ${width}px multi-selection, marquee, Ctrl-click, zoomed group moves, internal manual wires, grid, Escape, duplicate/delete, save and touch`);
  }
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
