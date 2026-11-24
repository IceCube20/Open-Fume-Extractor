'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'../..'),src=path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
const read=file=>fs.readFileSync(path.join(src,file),'utf8'),logic=read('WebLogic.inc.h'),assets=read('WebLogicResponsive.h');
let editor=logic.slice(logic.indexOf('html += F(R"HTML(',logic.indexOf('static void web_handle_logic()'))+'html += F(R"HTML('.length);editor=editor.slice(0,editor.indexOf(')HTML");'));
const css=assets.match(/R"LOGICCSS\(([\s\S]*?)\)LOGICCSS"/)[1],js=assets.match(/R"LOGICUI\(([\s\S]*?)\)LOGICUI"/)[1],wires=read('WebLogicConnections.h').match(/R"LOGICWIRE\(([\s\S]*?)\)LOGICWIRE"/)[1];
const shell=JSON.parse(read('WebShell.inc.h').match(/html \+= F\(("<\/title><style>[^\n]+?") OFE_WEB_TYPOGRAPHY_STYLE/)[1]).match(/<style>([\s\S]*?)<\/style>/)[1];
const automation=read('WebAutomationSignals.h').match(/R"HTML\(([\s\S]*?)\)HTML"/)[1];
const fixture={schema:1,name:'Wires',enabled:false,nodes:[{id:'i',type:'input',name:'Work',x:40,y:40,signal:'manual'},{id:'a',type:'and',name:'AND',x:340,y:40},{id:'b',type:'and',name:'AND B',x:340,y:350},{id:'o',type:'output',name:'Output',x:660,y:40,signal:'system:eco_inhibit'}],links:[]};
const out=path.join(__dirname,'build-checks/logic-wires');fs.mkdirSync(out,{recursive:true});
const settle=page=>page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
const port=async(page,id,out,index=0)=>page.locator(`[data-id="${id}"] ${out?'.logic-out':'.logic-in[data-port="'+index+'"]'}`).evaluate(e=>{const b=e.getBoundingClientRect();return{x:b.x+b.width/2,y:b.y+b.height/2}});

(async()=>{
 const browser=await chromium.launch({headless:true,channel:process.env.OFE_TEST_BROWSER_CHANNEL||(process.platform==='win32'?'msedge':undefined)});
 try{
  for(const width of [430,1920]){
   const context=await browser.newContext({viewport:{width,height:1100},hasTouch:true}),page=await context.newPage(),errors=[];
   page.on('pageerror',e=>errors.push(e.message));
   await page.route('http://ofe.test/**',async route=>{
    const p=new URL(route.request().url()).pathname;
    if(p==='/logic/responsive.css')return route.fulfill({contentType:'text/css',body:css});
    if(p==='/logic/responsive.js')return route.fulfill({contentType:'application/javascript',body:js});
    if(p==='/logic/connections.js')return route.fulfill({contentType:'application/javascript',body:wires});
    if(p==='/')return route.fulfill({contentType:'text/html; charset=utf-8',body:`<meta name="viewport" content="width=device-width,initial-scale=1"><style>:root{--ofe-font-ui:Arial,sans-serif}${shell}</style><select id="lang_sel"><option>de</option><option>en</option></select><main>${automation}${editor}</main>`});
    const data=p==='/logic/list'?{active:0,max_nodes:64,max_links:128,items:[{slot:0,used:true,name:'Wires'}]}:p==='/logic/json'?fixture:{modules:[]};
    return route.fulfill({contentType:'application/json',body:JSON.stringify(data)});
   });
   await page.goto('http://ofe.test/');await page.waitForFunction(()=>graph.nodes.length===4&&document.getElementById('logic_msg').textContent==='Geladen');
   const reset=async zoom=>{await page.evaluate(({fixture,zoom})=>{graph=structuredClone(fixture);sel=null;selLink=null;render();showLogicPane('plan');logicSetZoom(zoom);const w=document.getElementById('logic_workspace');w.scrollLeft=Math.max(0,340*zoom-280);w.scrollTop=0;scrollTo(0,0)},{fixture,zoom});await settle(page)};
   for(const zoom of [.5,1,1.5]){
    await reset(zoom);const from=await port(page,'i',true),target=await port(page,'a',false,1);
    const exact=await page.evaluate(()=>{const n=graph.nodes.find(n=>n.id==='a'),p=portPoint(n,false,1),r=document.getElementById('logic_canvas').getBoundingClientRect();return{x:r.left+p.x*logicZoom,y:r.top+p.y*logicZoom}});
    assert(Math.hypot(exact.x-target.x,exact.y-target.y)<.6,'Routes terminate at the visual port center');
    await page.mouse.move(from.x+12,from.y);await page.mouse.down();await page.mouse.move(target.x-16,target.y+1,{steps:10});
    assert.equal(await page.locator('.logic-port.hot').count(),1,`${width}/${zoom}: target highlighted`);
    assert.equal(await page.locator('[data-id=a] .logic-in[data-port="1"]').evaluate(e=>e.classList.contains('hot')),true,'Nearest adjacent AND input selected');
    assert.equal(await page.locator('.logic-line.pending.snapped').count(),1);
    assert.equal(await page.evaluate(()=>graph.links.length),0,'Preview never mutates the graph');
    if(width===1920&&zoom===1)await page.screenshot({path:path.join(out,'snap-preview.png'),fullPage:true});
    await page.mouse.up();assert.deepEqual(await page.evaluate(()=>graph.links),[{from:'i',to:'a',port:1}]);
    assert.equal(await page.locator('.logic-port.hot,.logic-line.pending').count(),0);
    await reset(zoom);const reverse=await port(page,'a',false,2),output=await port(page,'i',true);
    await page.mouse.move(reverse.x,reverse.y);await page.mouse.down();await page.mouse.move(output.x+15,output.y+1,{steps:8});await page.mouse.up();
    assert.deepEqual(await page.evaluate(()=>graph.links),[{from:'i',to:'a',port:2}],'Reverse input-to-output drag');
   }
   await reset(.1);const first=await port(page,'i',true),second=await port(page,'a',false,0);
   await page.mouse.click(first.x,first.y);assert.equal(await page.evaluate(()=>graph.links.length),0,'A tap only arms the source even when other pins are close');
   assert.equal(await page.evaluate(()=>!!linkFrom),true);await page.mouse.click(second.x,second.y);
   assert.deepEqual(await page.evaluate(()=>graph.links),[{from:'i',to:'a',port:0}],'Tap-to-tap connection');
   await reset(1);const tapSource=await port(page,'i',true),tapTarget=await port(page,'a',false,0);
   await page.mouse.click(tapSource.x,tapSource.y);await page.mouse.click(tapTarget.x-20,tapTarget.y);
   assert.deepEqual(await page.evaluate(()=>graph.links),[{from:'i',to:'a',port:0}],'Tap-to-tap uses the same generous destination catch');
   await reset(1);const from=await port(page,'i',true);await page.mouse.move(from.x,from.y);await page.mouse.down();await page.mouse.move(from.x+40,from.y+20);await page.keyboard.press('Escape');await page.mouse.up();
   assert.equal(await page.evaluate(()=>graph.links.length),0);assert.equal(await page.evaluate(()=>!!linkFrom||!!logicWireGesture),false);
   await reset(.5);const start=await port(page,'i',true),end=await port(page,'a',false,2),cdp=await context.newCDPSession(page);
   await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{x:start.x+20,y:start.y}]});
   for(let i=1;i<=8;i++)await cdp.send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[{x:start.x+(end.x-24-start.x)*i/8,y:start.y+(end.y+1-start.y)*i/8}]});
   assert.equal(await page.locator('.logic-port.hot').count(),1);await cdp.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});
   assert.deepEqual(await page.evaluate(()=>graph.links),[{from:'i',to:'a',port:2}],'Touch gets larger source and destination catch radii');
   await reset(.5);const cancelStart=await port(page,'i',true),cancelEnd=await port(page,'a',false,1);
   await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[cancelStart]});await cdp.send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[cancelEnd]});await cdp.send('Input.dispatchTouchEvent',{type:'touchCancel',touchPoints:[]});
   assert.equal(await page.evaluate(()=>graph.links.length),0);assert.equal(await page.evaluate(()=>!!linkFrom||!!logicWireGesture),false,'Pointer cancellation clears transient state');await cdp.detach();
   if(width===430){
    await reset(1);const source=await port(page,'i',true),edge=await page.locator('#logic_workspace').evaluate(w=>{const r=w.getBoundingClientRect();return{x:r.left+w.clientLeft+w.clientWidth-6,y:r.top+110}});
    await page.mouse.move(source.x,source.y);await page.mouse.down();await page.mouse.move(edge.x,edge.y,{steps:8});await page.waitForFunction(()=>document.getElementById('logic_workspace').scrollLeft>80);
    await page.keyboard.press('Escape');await page.mouse.up();assert.equal(await page.evaluate(()=>logicWireScrollFrame),0,'Edge scroll stops on cancellation');
   }
   if(width===1920){
    await reset(1);let source=await port(page,'i',true),wrong=await port(page,'a',true);
    await page.mouse.move(source.x,source.y);await page.mouse.down();await page.mouse.move(wrong.x,wrong.y,{steps:8});await page.mouse.up();assert.equal(await page.evaluate(()=>graph.links.length),0,'Output-to-output must not snap to a nearby input');
    await reset(1);source=await port(page,'a',true);wrong=await port(page,'a',false,2);
    await page.mouse.move(source.x,source.y);await page.mouse.down();await page.mouse.move(wrong.x,wrong.y,{steps:8});await page.mouse.up();assert.equal(await page.evaluate(()=>graph.links.length),0,'No self-connection');
    await reset(1);await page.evaluate(()=>{addLink('i','a',1);render()});
    const midpoint=await page.evaluate(()=>{const pts=getLogicRoutes().routes[0].points;let longest=null;for(let i=1;i<pts.length;i++){const a=pts[i-1],b=pts[i],length=Math.hypot(a.x-b.x,a.y-b.y);if(!longest||length>longest.length)longest={length,x:(a.x+b.x)/2,y:(a.y+b.y)/2}}const r=document.getElementById('logic_canvas').getBoundingClientRect();return{x:r.left+longest.x*logicZoom,y:r.top+longest.y*logicZoom}});
    await page.mouse.move(midpoint.x,midpoint.y);await page.mouse.down();await page.mouse.move(midpoint.x+40,midpoint.y+60,{steps:4});
    assert.deepEqual(await page.evaluate(()=>graph.links),[{from:'i',to:'a',port:1}],'Reconnecting retains the existing model until commit');
    await page.keyboard.press('Escape');await page.mouse.up();assert.deepEqual(await page.evaluate(()=>graph.links),[{from:'i',to:'a',port:1}],'Cancelled reconnection preserves the old wire');
    await page.mouse.move(midpoint.x,midpoint.y);await page.mouse.down();const b=await port(page,'b',false,3);await page.mouse.move(b.x-15,b.y,{steps:10});await page.mouse.up();
    assert.deepEqual(await page.evaluate(()=>graph.links),[{from:'i',to:'b',port:3}],'Reconnect replaces exactly one wire');
   }
   assert.deepEqual(errors,[],`${width}: JS errors`);console.log(`PASS: ${width}px mouse/touch snap at 50/100/150%, nearest pin, reverse drag, tap-to-tap, Escape and safe reconnection`);
   await context.close();
  }
  const page=await browser.newPage({viewport:{width:1920,height:1200}});
  await page.setContent(`<style>:root{--ofe-font-ui:Arial,sans-serif}${shell}${css}</style><select id="lang_sel"><option>de</option></select><main>${automation}${editor.replace(/<script[\s\S]*$/, '')}</main>`);
  const inline=editor.match(/<script>([\s\S]*?)<\/script>/)[1];
  await page.addScriptTag({content:inline});await page.addScriptTag({content:js});await page.addScriptTag({content:wires});await page.evaluate(()=>setupLogicResponsive());
  const routing=await page.evaluate(()=>{
   const n=(id,type,x,y)=>({...nodeDefaults(type),id,type,x,y,name:id});
   graph.nodes=[n('left','input',40,80),n('right','output',650,80)];graph.links=[{from:'left',to:'right',port:0}];render();
   const direct=getLogicRoutes();if(direct.routes[0].points.length!==2)throw Error('Straight links must stay straight');
   const cached=getLogicRoutes()===direct;drawLinks({left:true});if(getLogicRoutes()!==direct)throw Error('Live-state changes must not reroute the diagram');
   graph.nodes.push(n('obstacle','and',320,60));render();document.querySelector('[data-id=obstacle]').style.height='280px';drawLinks();
   const routed=getLogicRoutes(),detour=routed.routes[0];if(detour.blocked||!pathClear(detour.points,'left','right'))throw Error('Path crosses the actual tall block');
   const extent=Math.max(...detour.points.map(p=>p.y))-Math.min(...detour.points.map(p=>p.y));if(extent>160)throw Error('Needless tall detour: '+extent);
   graph.nodes=[n('i1','input',40,60),n('i2','input',40,320),n('o1','output',650,320),n('o2','output',650,60)];graph.links=[{from:'i1',to:'o1',port:0},{from:'i2',to:'o2',port:0}];render();
   const crossed=getLogicRoutes();for(const route of crossed.routes){if(route.blocked||!pathClear(route.points,route.fromId,route.toId))throw Error('Crossing fixture is blocked');for(let i=1;i<route.points.length;i++){const a=route.points[i-1],b=route.points[i];if(a.x!==b.x&&a.y!==b.y)throw Error('Non-orthogonal wire')}}
   const a=crossed.routes[0].points,b=crossed.routes[1].points;let overlap=0;for(let i=1;i<a.length;i++)for(let j=1;j<b.length;j++){
    const x=segInfo(a[i-1],a[i]),y=segInfo(b[j-1],b[j]);if(x.h&&y.h&&Math.abs(x.a.y-y.a.y)<.1)overlap+=overlap1d(x.a.x,x.b.x,y.a.x,y.b.x);if(x.v&&y.v&&Math.abs(x.a.x-y.a.x)<.1)overlap+=overlap1d(x.a.y,x.b.y,y.a.y,y.b.y);
   }
   if(overlap>6)throw Error('Unrelated wires overlap for '+overlap+' px');
   const length=route=>route.points.slice(1).reduce((sum,p,i)=>sum+Math.abs(p.x-route.points[i].x)+Math.abs(p.y-route.points[i].y),0);
   if(crossed.routes.some(r=>length(r)>1100))throw Error('Crossing avoidance introduces a needless loop');
   return{cached,obstacleExtent:extent,overlap,lengths:crossed.routes.map(length),halos:document.querySelectorAll('.logic-line-halo').length};
  });
  assert.equal(routing.cached,true);assert.equal(routing.halos,2);await page.screenshot({path:path.join(out,'routes.png'),fullPage:true});console.log('PASS: real orthogonal routing, actual block heights, no overlapping unrelated wires, short crossings and cached live redraws: '+JSON.stringify(routing));await page.close();
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
