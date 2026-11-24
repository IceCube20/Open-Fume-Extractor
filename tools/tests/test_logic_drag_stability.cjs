'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'../..'),src=path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
const read=file=>fs.readFileSync(path.join(src,file),'utf8'),logic=read('WebLogic.inc.h'),assets=read('WebLogicResponsive.h');
let editor=logic.slice(logic.indexOf('html += F(R"HTML(',logic.indexOf('static void web_handle_logic()'))+'html += F(R"HTML('.length);editor=editor.slice(0,editor.indexOf(')HTML");'));
const css=assets.match(/R"LOGICCSS\(([\s\S]*?)\)LOGICCSS"/)[1],js=assets.match(/R"LOGICUI\(([\s\S]*?)\)LOGICUI"/)[1],wires=read('WebLogicConnections.h').match(/R"LOGICWIRE\(([\s\S]*?)\)LOGICWIRE"/)[1];
const shell=JSON.parse(read('WebShell.inc.h').match(/html \+= F\(("<\/title><style>[^\n]+?") OFE_WEB_TYPOGRAPHY_STYLE/)[1]).match(/<style>([\s\S]*?)<\/style>/)[1];
const out=path.join(__dirname,'build-checks/logic-drag-stability');fs.mkdirSync(out,{recursive:true});
const snapshot=page=>page.evaluate(()=>getLogicRoutes().routes.map(r=>({key:JSON.stringify([r.fromId,r.toId,r.port]),points:r.points.map(p=>({x:p.x,y:p.y})),blocked:r.blocked})));

(async()=>{
 const browser=await chromium.launch({headless:true,channel:process.env.OFE_TEST_BROWSER_CHANNEL||(process.platform==='win32'?'msedge':undefined)});
 try{
  const page=await browser.newPage({viewport:{width:1920,height:1400}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.setContent(`<style>:root{--ofe-font-ui:Arial,sans-serif}${shell}${css}</style><select id="lang_sel"><option>de</option></select><main>${read('WebAutomationSignals.h').match(/R"HTML\(([\s\S]*?)\)HTML"/)[1]}${editor.replace(/<script[\s\S]*$/, '')}</main>`);
  await page.addScriptTag({content:editor.match(/<script>([\s\S]*?)<\/script>/)[1]});await page.addScriptTag({content:js});await page.addScriptTag({content:wires});await page.evaluate(()=>{setupLogicResponsive();applyLogicLang();applyLogicCapacity({max_nodes:64,max_links:128})});
  for(const fixture of [{name:'ordered',xs:[40,40,40,40],ports:[0,1,2,3],x:600,y:320,steps:80,dx:.5,dy:1},{name:'reported-jump',xs:[244,247,246,201],ports:[0,2,1,3],x:650,y:300,steps:24,dx:2,dy:4}])for(const zoom of [.5,1,1.5]){
   await page.evaluate(({fixture,zoom})=>{
    const n=(id,type,x,y)=>({...nodeDefaults(type),id,type,x,y,name:id,signal:type==='input'?'manual':''});
    graph={schema:1,name:'Four-input AND',enabled:false,nodes:[...Array.from({length:4},(_,i)=>n('source'+i,'input',fixture.xs[i],80+i*180)),n('gate','and',fixture.x,fixture.y),n('other','input',1000,650),n('other-out','output',1600,650)],links:[...Array.from({length:4},(_,i)=>({from:'source'+i,to:'gate',port:fixture.ports[i]})),{from:'other',to:'other-out',port:0}]};
    logicGridEnabled=false;clearLogicRouteHistory();render();logicSetZoom(zoom);document.getElementById('logic_workspace').scrollLeft=0;document.getElementById('logic_workspace').scrollTop=0;
   },{fixture,zoom});
   const initial=await snapshot(page);assert(initial.every(r=>!r.blocked),'Initial four-input fixture must be routable');
   const box=await page.locator('[data-id=gate] h3').boundingBox(),start={x:box.x+box.width/2,y:box.y+box.height/2};
   await page.mouse.move(start.x,start.y);await page.mouse.down();
   let previous=initial,maxStep=0,topologyChanges=0;
   for(let step=1;step<=fixture.steps;step++){
    await page.mouse.move(start.x+step*fixture.dx*zoom,start.y+step*fixture.dy*zoom);
    const next=await snapshot(page);
    for(let i=0;i<next.length;i++){
     assert.equal(next[i].blocked,false,'Moving an unobstructed AND must not block a wire');
     if(next[i].points.length!==previous[i].points.length)topologyChanges++;
     else for(let p=0;p<next[i].points.length;p++)maxStep=Math.max(maxStep,Math.hypot(next[i].points[p].x-previous[i].points[p].x,next[i].points[p].y-previous[i].points[p].y));
    }
    previous=next;
   }
   await page.mouse.up();const released=await snapshot(page);await page.evaluate(()=>drawLinks({}));assert.deepEqual(await snapshot(page),released,'No route changes on mouse release/live refresh');
   console.log(JSON.stringify({fixture:fixture.name,zoom,maxStep,topologyChanges}));
   assert.equal(topologyChanges,0,'Four input wires must keep their topology during a small drag');assert(maxStep<=Math.hypot(fixture.dx,fixture.dy)+2,'Wire waypoints jumped '+maxStep+' model pixels in a small step');
   assert.deepEqual(released[4].points,initial[4].points,'Unrelated wire must stay fixed');
   const endBox=await page.locator('[data-id=gate] h3').boundingBox();await page.mouse.move(endBox.x+endBox.width/2,endBox.y+endBox.height/2);await page.mouse.down();
   for(let step=fixture.steps-1;step>=0;step--)await page.mouse.move(start.x+step*fixture.dx*zoom,start.y+step*fixture.dy*zoom);
   await page.mouse.up();assert.deepEqual(await snapshot(page),initial,'Back-and-forth dragging must not accumulate route drift');
   if(zoom===1)await page.screenshot({path:path.join(out,'four-input-and-'+fixture.name+'.png'),fullPage:true});
   await page.evaluate(()=>{graph.links.reverse();drawLinks()});
   const sorted=rows=>rows.slice().sort((a,b)=>a.key.localeCompare(b.key));assert.deepEqual(sorted(await snapshot(page)),sorted(initial),'Link storage order must not change established routes');
   await page.evaluate(()=>{const n=graph.nodes.find(n=>n.id==='gate');n.x=180;n.y=900;const el=document.querySelector('[data-id=gate]');el.style.left='180px';el.style.top='900px';drawLinks()});
   assert(await page.evaluate(()=>getLogicRoutes().routes.every(r=>!r.blocked&&pathClear(r.points,r.fromId,r.toId))),'A large move must replace invalid lanes with clear orthogonal routes');
  }
  assert.deepEqual(errors,[]);await browser.close();console.log('PASS: four-input AND drag stability, 50/100/150% zoom, fixed unrelated wires, release and return path');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
