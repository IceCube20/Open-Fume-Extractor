'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'../..'),src=path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src'),vendor=path.join(root,'assets/vendor/libavoid');
const read=file=>fs.readFileSync(path.join(src,file),'utf8'),logic=read('WebLogic.inc.h'),assets=read('WebLogicResponsive.h');
let editor=logic.slice(logic.indexOf('html += F(R"HTML(',logic.indexOf('static void web_handle_logic()'))+'html += F(R"HTML('.length);editor=editor.slice(0,editor.indexOf(')HTML");'));
const css=assets.match(/R"LOGICCSS\(([\s\S]*?)\)LOGICCSS"/)[1],js=assets.match(/R"LOGICUI\(([\s\S]*?)\)LOGICUI"/)[1],wires=read('WebLogicConnections.h').match(/R"LOGICWIRE\(([\s\S]*?)\)LOGICWIRE"/)[1];
const shell=JSON.parse(read('WebShell.inc.h').match(/html \+= F\(("<\/title><style>[^\n]+?") OFE_WEB_TYPOGRAPHY_STYLE/)[1]).match(/<style>([\s\S]*?)<\/style>/)[1];
const automation=read('WebAutomationSignals.h').match(/R"HTML\(([\s\S]*?)\)HTML"/)[1];
const out=path.join(__dirname,'build-checks/logic-router');fs.mkdirSync(out,{recursive:true});
const checkRoutes=page=>page.evaluate(()=>getLogicRoutes().routes.map(r=>{const start=portPoint(graph.nodes.find(n=>n.id===r.fromId),true),end=portPoint(graph.nodes.find(n=>n.id===r.toId),false,r.port);return {points:r.points,blocked:r.blocked,clear:pathClear(r.points,r.fromId,r.toId),manual:r.manual,exact:Math.hypot(r.points[0].x-start.x,r.points[0].y-start.y,r.points.at(-1).x-end.x,r.points.at(-1).y-end.y)<1}}));

(async()=>{
 const browser=await chromium.launch({headless:true,channel:'msedge'});
 try{
  for(const width of [430,1920]){
   const page=await browser.newPage({viewport:{width,height:1300},hasTouch:true}),errors=[],warnings=[];let stored={schema:1,name:'Router',enabled:false,nodes:[],links:[]};
   page.on('pageerror',e=>errors.push(e.message));page.on('console',m=>{if(m.type()==='warning'){warnings.push(m.text());console.log(m.text())}});
   await page.route('http://ofe.test/**',async route=>{
    const p=new URL(route.request().url()).pathname;
    const scripts={'/logic/responsive.js':js,'/logic/connections.js':wires,'/logic/router.mjs':fs.readFileSync(path.join(vendor,'index.js'),'utf8')};
    if(scripts[p])return route.fulfill({contentType:'application/javascript',body:scripts[p]});
    if(p==='/logic/libavoid.wasm')return route.fulfill({contentType:'application/wasm',body:fs.readFileSync(path.join(vendor,'libavoid.wasm'))});
    if(p==='/logic/responsive.css')return route.fulfill({contentType:'text/css',body:css});
    if(p==='/')return route.fulfill({contentType:'text/html; charset=utf-8',body:`<meta name="viewport" content="width=device-width,initial-scale=1"><style>:root{--ofe-font-ui:Arial,sans-serif}${shell}</style><select id="lang_sel"><option>de</option><option>en</option></select><main>${automation}${editor}</main>`});
    if(p==='/logic/json/save'){stored=JSON.parse(route.request().postData());return route.fulfill({body:'OK'})}
    const data=p==='/logic/list'?{active:0,max_nodes:64,max_links:128,items:[{slot:0,used:true,name:'Router'}]}:p==='/logic/json'?stored:{modules:[]};
    return route.fulfill({contentType:'application/json',body:JSON.stringify(data)});
   });
   await page.goto('http://ofe.test/');await page.waitForFunction(()=>logicRouterStatus==='ready'&&document.getElementById('logic_msg').textContent==='Geladen');
   await page.evaluate(()=>{
    const n=(id,type,x,y)=>({...nodeDefaults(type),id,type,x,y,name:id,signal:type==='input'?'manual':''});
    graph={schema:1,name:'Four inputs',enabled:false,nodes:[...Array.from({length:4},(_,i)=>n('input'+i,'input',36,72+i*180)),n('gate','and',648,306),n('out','output',1044,324)],links:[...Array.from({length:4},(_,i)=>({from:'input'+i,to:'gate',port:i})),{from:'gate',to:'out',port:0}]};
    clearLogicRouteHistory();render();logicSetZoom(logicMobile()?.5:1);logicGridEnabled=false;
    const w=document.getElementById('logic_workspace');w.scrollTop=0;w.scrollLeft=0;
   });
   let routes=await checkRoutes(page);assert(routes.every(r=>r.clear&&!r.blocked&&r.exact));
   assert.equal(await page.evaluate(()=>logicRouterStatus),'ready','Real WASM router must not fall back');
   const before=routes.map(r=>r.points);
   await page.evaluate(()=>logicFitGraph());await page.screenshot({path:path.join(out,`${width}-four-inputs.png`),fullPage:true});
   await page.evaluate(()=>{const n=graph.nodes.find(n=>n.id==='gate');n.x+=36;n.y+=36;render()});
   assert((await checkRoutes(page)).every(r=>r.clear&&!r.blocked));
   await page.evaluate(()=>{const n=graph.nodes.find(n=>n.id==='gate');n.x-=36;n.y-=36;render()});
   assert.deepEqual((await checkRoutes(page)).map(r=>r.points),before,'No route drift on return');
   await page.evaluate(()=>{sel=null;selLink=0;drawLinks();renderInspector()});
   if(width<=1200){
    await page.click('[data-logic-tab=properties]');await page.selectOption('#logic_node_picker','gate');await page.locator('[data-logic-edit-link]').first().click();
    assert.equal(await page.evaluate(()=>document.querySelector('.logic-top').dataset.pane),'plan');assert.equal(await page.evaluate(()=>selLink),0);
   }
   for(const zoom of [.5,1,1.5]){
    await page.evaluate(zoom=>{logicSetZoom(zoom);selLink=0;drawLinks();const w=document.getElementById('logic_workspace');const r=getLogicRoutes().routes[0];const p=logicEditableSegments(r).find(s=>s.points[s.index].x===s.points[s.index-1].x);w.scrollLeft=Math.max(0,p.points[p.index].x*zoom-w.clientWidth/2);w.scrollTop=0},zoom);
    await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
    const handle=page.locator('.logic-segment-handle.vertical').first();assert(await handle.count(),'Wire has editable vertical segment');
    const box=await handle.evaluate(e=>e.getBoundingClientRect().toJSON()),start={x:box.x+box.width/2,y:box.y+box.height/2};
    const cdp=width<=1200&&zoom===.5?await page.context().newCDPSession(page):null;
    if(cdp){await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[start]});assert(await page.evaluate(()=>!!logicSegmentGesture),'Touch must select the segment handle');await cdp.send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[{x:start.x+36*zoom,y:start.y}]});await page.waitForFunction(()=>!!logicSegmentGesture?.preview)}
    else{await page.mouse.move(start.x,start.y);await page.mouse.down();await page.mouse.move(start.x+36*zoom,start.y,{steps:6})}
    assert.equal(await page.evaluate(()=>!!graph.links[0].route),zoom!==.5,'Preview must not commit prematurely');
    if(cdp){await cdp.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});await cdp.detach()}else await page.mouse.up();
    assert.equal(await page.evaluate(()=>!!graph.links[0].route),true,'Manual route committed');
    const kept=await page.evaluate(()=>structuredClone(graph.links[0].route));
    await page.evaluate(()=>saveGraph());assert.deepEqual(await page.evaluate(()=>graph.links[0].route),kept,'Manual route survives save and reload');
    assert.deepEqual(stored.links[0].route,kept);
    await page.evaluate(()=>{selLink=0;drawLinks()});
    const cancelBox=await page.locator('.logic-segment-handle.vertical').first().evaluate(e=>e.getBoundingClientRect().toJSON());
    await page.mouse.move(cancelBox.x+cancelBox.width/2,cancelBox.y+cancelBox.height/2);await page.mouse.down();await page.mouse.move(cancelBox.x+cancelBox.width/2-20,cancelBox.y+cancelBox.height/2);await page.keyboard.press('Escape');await page.mouse.up();
    assert.deepEqual(await page.evaluate(()=>graph.links[0].route),kept,'Escape retains manual route');
    if(width===1920&&zoom===1){
     const box=await page.locator('.logic-segment-handle.vertical').first().evaluate(e=>e.getBoundingClientRect().toJSON());
     await page.mouse.move(box.x+box.width/2,box.y+box.height/2);await page.mouse.down();await page.mouse.move(box.x+box.width/2-500,box.y+box.height/2,{steps:5});await page.mouse.up();
     assert.deepEqual(await page.evaluate(()=>graph.links[0].route),kept,'Invalid route through a block is rejected');
    }
   }
   await page.evaluate(()=>{selLink=0;drawLinks()});assert.equal(await page.locator('[data-logic-route-tool=unlock]').isEnabled(),true);
   await page.locator('[data-logic-route-tool=unlock]').click();assert.equal(await page.evaluate(()=>!!graph.links[0].route),false);
   await page.evaluate(()=>{
    logicGridEnabled=true;const p=logicNodePosition({x:347,y:211},{dx:0,dy:0},{});if(p.x%18||p.y%18)throw Error('Grid snapping failed');
    const free=logicNodePosition({x:347,y:211},{dx:0,dy:0},{altKey:true});if(free.x!==347||free.y!==211)throw Error('Alt bypass failed');
    for(const p of [[[0,0],[10,10]],[[0,0],[2900,0]],[[NaN,0],[20,0]]]){const link=normaliseLogicLink({from:'a',to:'b',port:0,route:{p,s:p[0],t:p.at(-1)}});if(link.route||link.from!=='a'||link.to!=='b')throw Error('Invalid layout must be dropped without losing the logical link')}
    const n=(id,type,x,y)=>({...nodeDefaults(type),id,type,x,y,name:id});
    graph.nodes=[n('left','input',36,72),n('right','output',684,72),n('obstacle','and',342,54)];graph.links=[{from:'left',to:'right',port:0}];clearLogicRouteHistory();render();document.querySelector('[data-id=obstacle]').style.height='280px';clearLogicRouteHistory();drawLinks();
   });
   assert((await checkRoutes(page)).every(r=>r.clear&&!r.blocked),'Engine avoids actual tall obstacles');
   if(width===1920){
    const perf=await page.evaluate(()=>{
     graph.nodes=Array.from({length:64},(_,i)=>({...nodeDefaults('and'),id:'n'+i,x:36+(i%8)*270,y:36+Math.floor(i/8)*190,name:'AND '+i}));
     graph.links=Array.from({length:128},(_,i)=>({from:'n'+Math.floor(i/2),to:'n'+((Math.floor(i/2)+(i%2?8:1))%64),port:i%2}));
     clearLogicRouteHistory();const start=performance.now();render();const initial=performance.now()-start,heap=logicAvoid.HEAPU8.length;
     for(let i=0;i<15;i++)rerouteLogicConnections();
     return{initial,heap,afterHeap:logicAvoid.HEAPU8.length,routes:getLogicRoutes().routes.length,ready:logicRouterStatus,pointCache:Object.keys(logicAvoid.getCache(logicAvoid.Point)).length};
    });
    assert.equal(perf.ready,'ready');assert.equal(perf.routes,128);assert(perf.initial<2500,'Dense-plan routing must not freeze the browser');assert.equal(perf.afterHeap,perf.heap,'Repeated rerouting must reuse WASM memory');assert(perf.pointCache<300,'Borrowed points must not leak JS wrappers');console.log('Dense-plan routing: '+JSON.stringify(perf));
   }
   assert.equal(await page.evaluate(()=>logicRouterStatus),'ready');
   assert.deepEqual(errors,[]);assert.deepEqual(warnings,[]);
   await page.screenshot({path:path.join(out,`${width}-routing.png`),fullPage:true});await page.close();
   console.log(`PASS: ${width}px real libavoid WASM, four inputs, stable drag, manual segments 50/100/150%, save/reload, cancel, unlock, grid and tall obstacles`);
  }
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
