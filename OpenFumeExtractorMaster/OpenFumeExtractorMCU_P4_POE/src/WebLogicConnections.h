#pragma once

static const char OFE_WEB_LOGIC_CONNECTION_SCRIPT[] PROGMEM = R"LOGICWIRE(
let logicGeometry=new Map(),logicRouteCache={key:null,routes:[],junctions:[]};
let logicWireGesture=null,logicWireTarget=null,logicReconnect=null,logicWireScrollFrame=0;
const LOGIC_GRID=18;
let logicGridEnabled=true,logicAvoid=null,logicAvoidState=null,logicRouterStatus='loading',logicSegmentGesture=null;
function logicSnap(value){return Math.round(value/LOGIC_GRID)*LOGIC_GRID}
function logicNodePosition(point,offset,ev){
 const x=point.x-offset.dx,y=point.y-offset.dy,snap=logicGridEnabled&&!ev.altKey;
 return {x:Math.max(0,Math.min(2600,snap?logicSnap(x):x)),y:Math.max(0,Math.min(1580,snap?logicSnap(y):y))};
}
function normaliseLogicLink(link){
 const result={from:link.from,to:link.to,port:Number(link.port||0)},r=link.route;
 const pair=p=>Array.isArray(p)&&p.length===2&&p.every(Number.isFinite)&&p[0]>=0&&p[0]<=2800&&p[1]>=0&&p[1]<=1700;
 if(r&&Array.isArray(r.p)&&r.p.length>=2&&r.p.length<=16&&r.p.every(pair)&&pair(r.s)&&pair(r.t)&&
  r.p.every((p,i)=>!i||p[0]===r.p[i-1][0]||p[1]===r.p[i-1][1])&&
  r.p[0].every((v,i)=>v===r.s[i])&&r.p.at(-1).every((v,i)=>v===r.t[i]))result.route={p:r.p.map(p=>p.slice()),s:r.s.slice(),t:r.t.slice()};
 return result;
}
function logicManualPoints(link,start,end){
 const r=normaliseLogicLink(link).route;if(!r)return null;
 const p=r.p.map(([x,y])=>({x,y}));
 if(p.length===2)return simplifyPts([start,{x:start.x+36,y:start.y},{x:start.x+36,y:end.y},end]);
 p[0]={...start};p[p.length-1]={...end};p[1].y=start.y;p[p.length-2].y=end.y;
 return simplifyPts(p);
}
function disposeLogicAvoid(){
 if(!logicAvoidState)return;
 const state=logicAvoidState;logicAvoidState=null;logicAvoid.destroy(state.router);
 // Native router ownership frees the children; remove only their JS wrappers.
 for(const [object,type] of state.owned)delete logicAvoid.getCache(type)[logicAvoid.getPointer(object)];
 for(const c of state.connectors.values())delete logicAvoid.getCache(logicAvoid.ConnRef)[logicAvoid.getPointer(c)];
}
function logicAvoidRoutes(){
 if(!logicAvoid)return null;
 const A=logicAvoid,manuals=new Map();
 for(const link of graph.links){const a=graph.nodes.find(n=>n.id===link.from),b=graph.nodes.find(n=>n.id===link.to);if(a&&b){const p=logicManualPoints(link,portPoint(a,true),portPoint(b,false,Number(link.port||0)));if(p)manuals.set(logicRouteKey(link),p)}}
 const signature=JSON.stringify([graph.nodes.map(n=>{const g=logicGeometry.get(n.id);return[n.id,n.type,g.width,g.height,g.top,inputPorts(n.type,n).length]}),[...manuals]]);
 if(logicAvoidState&&logicAvoidState.signature!==signature)disposeLogicAvoid();
 if(!logicAvoidState){
  const router=new A.Router(A.OrthogonalRouting),owned=[],shapes=new Map();
  router.setRoutingParameter(A.shapeBufferDistance,24);router.setRoutingParameter(A.idealNudgingDistance,LOGIC_GRID);
  router.setRoutingParameter(A.segmentPenalty,30);router.setRoutingParameter(A.crossingPenalty,80);
  router.setRoutingParameter(A.fixedSharedPathPenalty,180);
  router.setRoutingOption(A.nudgeOrthogonalSegmentsConnectedToShapes,false);
  router.setRoutingOption(A.nudgeOrthogonalTouchingColinearSegments,true);
  router.setRoutingOption(A.nudgeSharedPathsWithCommonEndPoint,false);
  logicAvoidState={router,owned,shapes,connectors:new Map(),signature};
  for(const n of graph.nodes){
   const g=logicGeometry.get(n.id),a=new A.Point(Number(n.x),Number(n.y)),b=new A.Point(Number(n.x)+g.width,Number(n.y)+g.height),box=new A.Rectangle(a,b),shape=new A.ShapeRef(router,box);
   A.destroy(box);A.destroy(a);A.destroy(b);owned.push([shape,A.ShapeRef]);
   const pin=(id,x,y,direction)=>{const p=new A.ShapeConnectionPin(shape,id,x,y/g.height,true,0,direction);p.setExclusive(false);owned.push([p,A.ShapeConnectionPin])};
   if(n.type!=='output')pin(1,1,g.top+NODE_OUT_Y,A.ConnDirRight);
   inputPorts(n.type,n).forEach((p,i)=>pin(10+i,0,g.top+inputPortY(n.type,i,n),A.ConnDirLeft));
   shapes.set(n.id,{shape,x:Number(n.x),y:Number(n.y)});
  }
 }
 const state=logicAvoidState,recipes=[];
 for(const link of graph.links){
  const key=logicRouteKey(link),manual=manuals.get(key);
  // Split locked wires into fixed straight segments so global routing sees them too.
  if(manual){for(let i=1;i<manual.length;i++)recipes.push({key:key+'#'+i,link,points:[manual[i-1],manual[i]]})}
  else recipes.push({key,link});
 }
 const keys=new Set(recipes.map(r=>r.key));
 for(const [key,c] of state.connectors)if(!keys.has(key)){state.router.deleteConnector(c);delete A.getCache(A.ConnRef)[A.getPointer(c)];state.connectors.delete(key)}
 for(const n of graph.nodes){const s=state.shapes.get(n.id),x=Number(n.x),y=Number(n.y);if(s.x!==x||s.y!==y){state.router.moveShape(s.shape,x-s.x,y-s.y);s.x=x;s.y=y}}
 for(const {key,link,points} of recipes.sort((a,b)=>a.key.localeCompare(b.key))){
  if(state.connectors.has(key))continue;
  const a=state.shapes.get(link.from),b=state.shapes.get(link.to);if(!a||!b)continue;
  const source=graph.nodes.find(n=>n.id===link.from),target=graph.nodes.find(n=>n.id===link.to),port=Number(link.port||0);
  if(source.type==='output'||!Number.isInteger(port)||port<0||port>=inputPorts(target.type,target).length)continue;
  const sp=points&&new A.Point(points[0].x,points[0].y),tp=points&&new A.Point(points[1].x,points[1].y);
  const s=points?new A.ConnEnd(sp):new A.ConnEnd(a.shape,1),t=points?new A.ConnEnd(tp):new A.ConnEnd(b.shape,10+port),c=new A.ConnRef(state.router,s,t);
  A.destroy(s);A.destroy(t);if(points){A.destroy(sp);A.destroy(tp)}c.setRoutingType(A.ConnType_Orthogonal);state.connectors.set(key,c);
 }
 state.router.processTransaction();
 return new Map([...state.connectors].map(([key,conn])=>{const line=conn.displayRoute(),polygon=A.wrapPointer(A.getPointer(line),A.PolygonInterface),points=[];for(let i=0;i<line.size();i++){const p=polygon.at(i);points.push({x:p.x,y:p.y});delete A.getCache(A.Point)[A.getPointer(p)]}delete A.getCache(A.PolygonInterface)[A.getPointer(polygon)];delete A.getCache(line.constructor)[A.getPointer(line)];return[key,simplifyPts(points)]}));
}
async function setupLogicRouter(){
 try{const {AvoidLib}=await import('/logic/router.mjs');await AvoidLib.load('/logic/libavoid.wasm');while(drag||logicSegmentGesture||logicWireGesture)await new Promise(resolve=>setTimeout(resolve,50));logicAvoid=AvoidLib.getInstance();logicRouterStatus='ready';clearLogicRouteHistory();drawLinks()}
 catch(e){logicRouterStatus='fallback';console.warn('OFE routing engine unavailable; using fallback',e)}
 renderLogicRoutingControls();
}
function rerouteLogicConnections(){clearLogicRouteHistory();drawLinks();msg(t('Automatische Leitungen neu geordnet','Automatic wires rerouted'))}
function unlockLogicConnection(){const links=graph.links.filter((l,i)=>logicSelectedLinks.has(logicRouteKey(l))||i===selLink);if(!links.length)return;for(const link of links)delete link.route;clearLogicRouteHistory();drawLinks();msg(t('Leitungsweg freigegeben','Wire route unlocked'))}
function renderLogicRoutingControls(){
 for(const button of document.querySelectorAll('[data-logic-route-tool]')){
  const mode=button.dataset.logicRouteTool,text=mode==='grid'?t('Am Raster ausrichten','Snap to grid'):mode==='route'?t('Leitungen neu ordnen','Reroute wires'):t('Leitungsweg freigeben','Unlock wire route');
  button.title=text;button.setAttribute('aria-label',text);
  if(mode==='grid')button.setAttribute('aria-pressed',String(logicGridEnabled));
  if(mode==='unlock')button.disabled=!graph.links.some((l,i)=>l.route&&(logicSelectedLinks.has(logicRouteKey(l))||i===selLink));
 }
 const status=document.getElementById('logic_router_status');if(status){status.hidden=logicRouterStatus!=='fallback';status.textContent=t('Routing eingeschr\u00e4nkt','Routing limited');status.title=t('Routing-Bibliothek nicht geladen; Seite erneut laden.','Routing library unavailable; reload the page.')}
}
function setupLogicRoutingTools(){
 const tools=document.querySelector('.logic-tools');
 const paths={grid:'<rect x="3" y="3" width="18" height="18" rx="2"/><path d="M9 3v18M15 3v18M3 9h18M3 15h18"/>',route:'<circle cx="6" cy="18" r="3"/><circle cx="18" cy="6" r="3"/><path d="M6 15V6a3 3 0 0 1 3-3h1m8 6v9a3 3 0 0 1-3 3h-1"/>',unlock:'<rect x="3" y="11" width="18" height="11" rx="2"/><path d="M7 11V7a5 5 0 0 1 9.9-1"/>'};
 for(const mode of ['grid','route','unlock']){const b=document.createElement('button');b.type='button';b.className='secondary';b.dataset.logicRouteTool=mode;b.innerHTML='<svg class="logic-tool-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">'+paths[mode]+'</svg>';b.onclick=()=>{if(mode==='grid'){logicGridEnabled=!logicGridEnabled;renderLogicRoutingControls()}else if(mode==='route')rerouteLogicConnections();else unlockLogicConnection()};tools.append(b)}
 const status=document.createElement('span');status.id='logic_router_status';status.className='logic-router-status';status.setAttribute('role','status');status.hidden=true;tools.append(status);renderLogicRoutingControls();
}
function refreshLogicGeometry(){
 const elements=new Map([...document.querySelectorAll('#logic_canvas>.logic-node')].map(e=>[e.dataset.id,e]));
 logicGeometry=new Map(graph.nodes.map(n=>{const el=elements.get(n.id);return[n.id,{el,width:el?el.offsetWidth:NODE_W,height:el?el.offsetHeight:118,top:el?el.clientTop:1}]}));
}
let logicSelectedNodes=new Set(),logicSelectedLinks=new Set(),logicSelectionGraph=null,logicSelectionBox=null,logicSelectionMode=false;
function syncLogicSelection(){
 if(logicSelectionGraph!==graph){cancelLogicSelection();logicSelectionGraph=graph;logicSelectedNodes.clear();logicSelectedLinks.clear();sel=null;selLink=null}
 const ids=new Set(graph.nodes.map(n=>n.id));logicSelectedNodes=new Set([...logicSelectedNodes].filter(id=>ids.has(id)));
 const keys=new Set(graph.links.map(logicRouteKey));logicSelectedLinks=new Set([...logicSelectedLinks].filter(key=>keys.has(key)));
 if(selLink!==null){const link=graph.links[selLink];if(link&&!logicSelectedLinks.has(logicRouteKey(link)))logicSelectedLinks=new Set([logicRouteKey(link)]);if(!logicSelectedNodes.size)sel=null}
 else if(sel&&ids.has(sel)&&!logicSelectedNodes.has(sel)){logicSelectedNodes=new Set([sel]);logicSelectedLinks.clear()}
 else if(!sel)logicSelectedNodes.clear();
}
function logicSelectionIds(){return graph.nodes.filter(n=>logicSelectedNodes.has(n.id)).map(n=>n.id)}
function setLogicSelection(ids,primary=null,links=[]){
 logicSelectedNodes=new Set(ids);logicSelectedLinks=new Set(links);sel=primary&&logicSelectedNodes.has(primary)?primary:[...logicSelectedNodes].at(-1)||null;selLink=sel?null:graph.links.map(l=>logicSelectedLinks.has(logicRouteKey(l))).lastIndexOf(true);if(selLink<0)selLink=null;
 markSelected();renderInspector();drawLinks();
}
function renderLogicSelectionControls(){
 const button=document.querySelector('[data-logic-selection-tool=select]'),clear=document.querySelector('[data-logic-selection-tool=clear]'),count=document.getElementById('logic_selection_count');
 if(!button)return;button.title=t('Mehrfachauswahl','Multiple selection');button.setAttribute('aria-label',button.title);button.setAttribute('aria-pressed',String(logicSelectionMode));
 clear.title=t('Auswahl aufheben','Clear selection');clear.setAttribute('aria-label',clear.title);clear.disabled=!logicSelectedNodes.size&&!logicSelectedLinks.size;
 count.textContent=[logicSelectedNodes.size>1?logicSelectedNodes.size+' '+t('Bausteine','blocks'):'',logicSelectedLinks.size>1?logicSelectedLinks.size+' '+t('Verbindungen','wires'):''].filter(Boolean).join(' · ');
 document.getElementById('logic_workspace').classList.toggle('is-selecting',logicSelectionMode);
}
function setupLogicSelection(){
 const tools=document.querySelector('.logic-tools');
 for(const mode of ['select','clear']){
  const b=document.createElement('button');b.type='button';b.className='secondary';b.dataset.logicSelectionTool=mode;
  b.innerHTML=logicToolSvg(mode==='select'?'fit':'close');b.onclick=()=>{cancelLogicSelection();clearPending();if(mode==='select')logicSelectionMode=!logicSelectionMode;else setLogicSelection([]);renderLogicSelectionControls()};tools.append(b);
 }
 const count=document.createElement('span');count.id='logic_selection_count';count.className='logic-selection-count';count.setAttribute('role','status');count.setAttribute('aria-live','polite');tools.append(count);
 const w=document.getElementById('logic_workspace');w.tabIndex=0;
 w.addEventListener('pointerdown',beginLogicSelectionBox,true);
 w.addEventListener('lostpointercapture',ev=>{if(drag?.pointerId===ev.pointerId||logicSelectionBox?.pointerId===ev.pointerId)cancelLogicSelection()});
 window.addEventListener('keydown',ev=>{
  if(ev.defaultPrevented)return;
  if(ev.target.closest('input,select,textarea,[contenteditable=true]'))return;
  if(ev.key==='Escape'){ev.preventDefault();if(drag||logicSelectionBox)cancelLogicSelection();else if(!linkFrom&&!lineDrag&&!logicSegmentGesture)setLogicSelection([]);return}
  if((ev.ctrlKey||ev.metaKey)&&ev.key.toLowerCase()==='a'){ev.preventDefault();setLogicSelection(graph.nodes.map(n=>n.id),null,graph.links.map(logicRouteKey))}
  if((ev.key==='Delete'||ev.key==='Backspace')&&(logicSelectedNodes.size||logicSelectedLinks.size)){ev.preventDefault();deleteSelected()}
 });
 window.addEventListener('blur',cancelLogicSelection);renderLogicSelectionControls();
}
function beginLogicNodeDrag(ev,n){
 if(!ev.isPrimary||ev.button!==0||ev.target.closest('.logic-port,button,select,input')||logicWireGesture||logicSegmentGesture||logicSelectionBox)return;
 if(drag)return;ev.preventDefault();ev.stopPropagation();syncLogicSelection();
 if(ev.ctrlKey||ev.metaKey||ev.shiftKey||logicSelectionMode){
  const ids=new Set(logicSelectedNodes);if(ids.has(n.id))ids.delete(n.id);else ids.add(n.id);setLogicSelection([...ids],n.id,[...logicSelectedLinks]);return;
 }
 if(!logicSelectedNodes.has(n.id))setLogicSelection([n.id],n.id);else{sel=n.id;selLink=null;markSelected();renderInspector();drawLinks()}
 const point=screenToCanvas(ev),nodes=graph.nodes.filter(node=>logicSelectedNodes.has(node.id)).map(node=>({node,x:Number(node.x),y:Number(node.y)})),routes=graph.links.filter(l=>l.route&&logicSelectedNodes.has(l.from)&&logicSelectedNodes.has(l.to)).map(link=>({link,route:structuredClone(link.route)}));
 drag={id:n.id,pointerId:ev.pointerId,graph,point,dx:point.x-n.x,dy:point.y-n.y,nodes,routes,cache:logicRouteCache,startX:ev.clientX,startY:ev.clientY,moved:false};
 document.getElementById('logic_workspace').setPointerCapture(ev.pointerId);
}
function moveLogicNodeGroup(ev){
 const g=drag;if(!g||g.pointerId!==ev.pointerId)return;
 if(!g.moved&&Math.hypot(ev.clientX-g.startX,ev.clientY-g.startY)<.1)return;g.moved=true;
 const p=screenToCanvas(ev);let dx=p.x-g.point.x,dy=p.y-g.point.y;
 if(logicGridEnabled&&!ev.altKey){dx=logicSnap(dx);dy=logicSnap(dy)}
 let left=-Math.min(...g.nodes.map(n=>n.x)),right=2600-Math.max(...g.nodes.map(n=>n.x)),top=-Math.min(...g.nodes.map(n=>n.y)),bottom=1580-Math.max(...g.nodes.map(n=>n.y));
 for(const {route} of g.routes)for(const [x,y] of route.p){left=Math.max(left,-x);right=Math.min(right,2800-x);top=Math.max(top,-y);bottom=Math.min(bottom,1700-y)}
 dx=Math.max(left,Math.min(right,dx));dy=Math.max(top,Math.min(bottom,dy));
 for(const item of g.nodes){item.node.x=item.x+dx;item.node.y=item.y+dy;const el=logicGeometry.get(item.node.id)?.el;if(el){el.style.left=item.node.x+'px';el.style.top=item.node.y+'px'}}
 for(const {link,route} of g.routes)link.route={p:route.p.map(([x,y])=>[x+dx,y+dy]),s:[route.s[0]+dx,route.s[1]+dy],t:[route.t[0]+dx,route.t[1]+dy]};
 drawLinks();
}
function beginLogicSelectionBox(ev){
 if(drag||logicSelectionBox){if(ev.pointerId!==(drag||logicSelectionBox).pointerId){ev.preventDefault();ev.stopPropagation()}return}
 if(!ev.isPrimary||ev.button!==0||linkFrom||logicSegmentGesture||logicWireGesture)return;
 if(ev.pointerType==='touch'&&!logicSelectionMode)return;
 if(!['logic_canvas','logic_svg','logic_canvas_scale','logic_workspace'].includes(ev.target.id)||!logicWireBounds(ev))return;
 ev.preventDefault();ev.stopPropagation();syncLogicSelection();
 const p=screenToCanvas(ev),start={x:Math.max(0,Math.min(2800,p.x)),y:Math.max(0,Math.min(1700,p.y))},element=document.createElement('div');element.className='logic-selection-box';document.getElementById('logic_canvas').append(element);
 logicSelectionBox={pointerId:ev.pointerId,graph,start,element,before:[...logicSelectedNodes],beforeLinks:[...logicSelectedLinks],primary:sel,add:ev.ctrlKey||ev.metaKey||ev.shiftKey,startX:ev.clientX,startY:ev.clientY,moved:false};
 document.getElementById('logic_workspace').setPointerCapture(ev.pointerId);
}
function moveLogicSelectionBox(ev){
 const g=logicSelectionBox;if(!g||g.pointerId!==ev.pointerId)return;
 if(!g.moved&&Math.hypot(ev.clientX-g.startX,ev.clientY-g.startY)<3)return;g.moved=true;
 const p=screenToCanvas(ev),left=Math.max(0,Math.min(g.start.x,p.x)),top=Math.max(0,Math.min(g.start.y,p.y)),right=Math.min(2800,Math.max(g.start.x,p.x)),bottom=Math.min(1700,Math.max(g.start.y,p.y));
 Object.assign(g.element.style,{left:left+'px',top:top+'px',width:Math.max(0,right-left)+'px',height:Math.max(0,bottom-top)+'px'});
 refreshLogicGeometry();const ids=g.add?new Set(g.before):new Set();
 for(const n of graph.nodes){const r=logicGeometry.get(n.id);if(Number(n.x)<right&&Number(n.x)+r.width>left&&Number(n.y)<bottom&&Number(n.y)+r.height>top)ids.add(n.id)}
 const links=g.add?new Set(g.beforeLinks):new Set();
 for(const route of getLogicRoutes().routes){if(!route)continue;for(let i=1;i<route.points.length;i++){const a=route.points[i-1],b=route.points[i];if(Math.max(a.x,b.x)>=left&&Math.min(a.x,b.x)<=right&&Math.max(a.y,b.y)>=top&&Math.min(a.y,b.y)<=bottom){links.add(JSON.stringify([route.fromId,route.toId,route.port]));break}}}
 setLogicSelection([...ids],null,[...links]);
}
function finishLogicSelection(ev){
 const g=drag||logicSelectionBox;if(!g||g.pointerId!==ev.pointerId)return;
 if(logicSelectionBox){const box=logicSelectionBox;logicSelectionBox=null;box.element.remove();if(!box.moved)setLogicSelection(box.add?box.before:[],box.primary,box.add?box.beforeLinks:[])}
 drag=null;const w=document.getElementById('logic_workspace');if(w.hasPointerCapture(ev.pointerId))w.releasePointerCapture(ev.pointerId);renderInspector();
}
function cancelLogicSelection(){
 const g=drag||logicSelectionBox;if(!g)return;drag=null;logicSelectionBox=null;
 if(g.element)g.element.remove();
 if(g.graph===graph){
  if(g.nodes){for(const item of g.nodes){item.node.x=item.x;item.node.y=item.y;const el=document.querySelector('#logic_canvas>.logic-node[data-id="'+CSS.escape(item.node.id)+'"]');if(el){el.style.left=item.x+'px';el.style.top=item.y+'px'}}for(const item of g.routes)item.link.route=structuredClone(item.route);logicRouteCache=g.cache}
  else{logicSelectedNodes=new Set(g.before);logicSelectedLinks=new Set(g.beforeLinks);sel=g.primary;selLink=sel?null:graph.links.map(l=>logicSelectedLinks.has(logicRouteKey(l))).lastIndexOf(true);if(selLink<0)selLink=null}
 }
 const w=document.getElementById('logic_workspace');if(w.hasPointerCapture(g.pointerId))w.releasePointerCapture(g.pointerId);markSelected();renderInspector();drawLinks();
}
function deleteLogicSelection(){
 const ids=new Set(logicSelectionIds());graph.nodes=graph.nodes.filter(n=>!ids.has(n.id));graph.links=graph.links.filter(l=>!ids.has(l.from)&&!ids.has(l.to)&&!logicSelectedLinks.has(logicRouteKey(l)));logicSelectedNodes.clear();logicSelectedLinks.clear();sel=null;selLink=null;render();
}
function duplicateLogicSelection(){
 const ids=new Set(logicSelectionIds()),nodes=graph.nodes.filter(n=>ids.has(n.id)),links=graph.links.filter(l=>ids.has(l.from)&&ids.has(l.to));
 if(!nodes.length||!ensureLogicCapacity(graph.nodes.length+nodes.length,graph.links.length+links.length))return;
 const points=links.flatMap(l=>normaliseLogicLink(l).route?.p||[]);
 const dx=Math.max(0,Math.min(36,2600-Math.max(...nodes.map(n=>Number(n.x))),...points.map(p=>2800-p[0]))),dy=Math.max(0,Math.min(36,1580-Math.max(...nodes.map(n=>Number(n.y))),...points.map(p=>1700-p[1]))),map=new Map(),copies=[];
 for(const n of nodes){const copy=structuredClone(n);copy.id=nid();map.set(n.id,copy.id);copy.x=Number(n.x)+dx;copy.y=Number(n.y)+dy;copy.name=(n.name||nodeTypeLabel(n))+' '+t('Kopie','copy');copies.push(copy)}
 const copiedLinks=links.map(l=>{const copy=structuredClone(l);copy.from=map.get(l.from);copy.to=map.get(l.to);if(copy.route){copy.route.p=copy.route.p.map(([x,y])=>[x+dx,y+dy]);copy.route.s=[copy.route.s[0]+dx,copy.route.s[1]+dy];copy.route.t=[copy.route.t[0]+dx,copy.route.t[1]+dy]}return normaliseLogicLink(copy)});
 graph.nodes.push(...copies);graph.links.push(...copiedLinks);logicSelectedNodes=new Set(copies.map(n=>n.id));logicSelectedLinks.clear();sel=copies.at(-1).id;selLink=null;render();
}
function logicRouteJunctions(routes){
 const points=new Map();
 const on=(p,a,b)=>Math.abs(a.x-b.x)<.1?Math.abs(p.x-a.x)<.1&&p.y>=Math.min(a.y,b.y)&&p.y<=Math.max(a.y,b.y):Math.abs(p.y-a.y)<.1&&p.x>=Math.min(a.x,b.x)&&p.x<=Math.max(a.x,b.x);
 for(let i=0;i<routes.length;i++)for(let j=0;j<i;j++){
  const a=routes[i],b=routes[j];if(!a||!b||!(a.fromId===b.fromId||(a.toId===b.toId&&a.port===b.port)))continue;
  for(const [one,two] of [[a,b],[b,a]])for(const p of one.points.slice(1,-1))for(let k=1;k<two.points.length;k++){
   if(!on(p,two.points[k-1],two.points[k]))continue;
   const key=p.x+','+p.y,item=points.get(key)||{x:p.x,y:p.y,sources:new Set()};item.sources.add(a.fromId);item.sources.add(b.fromId);points.set(key,item);
  }
 }
 return [...points.values()].filter(p=>{
  const directions=new Set();for(const route of routes){if(!route||!p.sources.has(route.fromId))continue;for(let i=1;i<route.points.length;i++){
   const a=route.points[i-1],b=route.points[i];if(!on(p,a,b))continue;
   if(a.x<p.x||b.x<p.x)directions.add('L');if(a.x>p.x||b.x>p.x)directions.add('R');if(a.y<p.y||b.y<p.y)directions.add('U');if(a.y>p.y||b.y>p.y)directions.add('D');
  }}return directions.size>=3;
 });
}
function clearLogicRouteHistory(){disposeLogicAvoid();logicRouteCache={key:null,routes:[],junctions:[]}}
function logicRouteKey(link){return JSON.stringify([link.from,link.to,Number(link.port||0)])}
function logicFanInRoute(link,node,start,end){
 if(inputPorts(node.type,node).length<2)return null;
 const incoming=graph.links.filter(l=>l.to===node.id).slice().sort((a,b)=>Number(a.port||0)-Number(b.port||0)||String(a.from).localeCompare(String(b.from)));
 if(incoming.length<2)return null;
 const index=incoming.findIndex(l=>logicRouteKey(l)===logicRouteKey(link)),lane=end.x-36-Math.max(0,incoming.length-1-index)*14;
 return simplifyPts([start,{x:lane,y:start.y},{x:lane,y:end.y},end]);
}
function stretchLogicRoute(anchor,start,end,fanIn){
 const old=anchor.points;
 if(old.length===2){
  if(Math.abs(start.y-end.y)<.1)return simplifyPts([start,end]);
  if(fanIn)return fanIn;
  const lane=end.x-36;return simplifyPts([start,{x:lane,y:start.y},{x:lane,y:end.y},end]);
 }
 const shift=(value,a,b,nextA,nextB)=>{
  const fraction=Math.abs(b-a)<.1?.5:Math.max(0,Math.min(1,(value-a)/(b-a)));
  return value+(nextA-a)*(1-fraction)+(nextB-b)*fraction;
 };
 const points=old.map(p=>({x:shift(p.x,anchor.start.x,anchor.end.x,start.x,end.x),y:shift(p.y,anchor.start.y,anchor.end.y,start.y,end.y)})),last=points.length-1;
 points[0]={...start};points[last]={...end};points[1].y=start.y;points[last-1].y=end.y;
 // Preserve the terminal lanes, not a fresh shortest-path choice on every pixel.
 if(points.length===4){const x=old[1].x+end.x-anchor.end.x;points[1].x=points[2].x=x}
 else{
  points[1].x=points[2].x=old[1].x+start.x-anchor.start.x;
  points[last-1].x=points[last-2].x=old[last-1].x+end.x-anchor.end.x;
 }
 return simplifyPts(points);
}
function logicRouteOverlap(points,fromId,toId,port,routed){
 for(const route of routed)for(let i=1;i<points.length;i++)for(let j=1;j<route.points.length;j++){
  const a=segInfo(points[i-1],points[i]),b=segInfo(route.points[j-1],route.points[j]);
  const overlap=a.h&&b.h&&Math.abs(a.a.y-b.a.y)<1?overlap1d(a.a.x,a.b.x,b.a.x,b.b.x):a.v&&b.v&&Math.abs(a.a.x-b.a.x)<1?overlap1d(a.a.y,a.b.y,b.a.y,b.b.y):0;
  if(overlap<=6)continue;
  if(route.fromId===fromId&&i===1&&j===1)continue;
  if(route.toId===toId&&route.port===port&&i===points.length-1&&j===route.points.length-1)continue;
  return true;
 }
 return false;
}
function getFallbackLogicRoutes(){
 refreshLogicGeometry();
 const key=JSON.stringify([graph.nodes.map(n=>{const g=logicGeometry.get(n.id);return[n.id,n.x,n.y,n.type,latchMode(n),g.width,g.height,g.top]}),graph.links]);
 if(key===logicRouteCache.key)return logicRouteCache;
 const history=new Map(logicRouteCache.routes.filter(Boolean).map(r=>[JSON.stringify([r.fromId,r.toId,r.port]),r])),routes=new Array(graph.links.length),routed=[];
 const jobs=graph.links.map((link,index)=>{
  const a=graph.nodes.find(n=>n.id===link.from),b=graph.nodes.find(n=>n.id===link.to),previous=history.get(logicRouteKey(link)),port=Number(link.port||0),start=a&&portPoint(a,true),end=b&&portPoint(b,false,port);
  const fixed=previous&&start&&end&&previous.start&&previous.end&&Math.hypot(start.x-previous.start.x,start.y-previous.start.y,end.x-previous.end.x,end.y-previous.end.y)<.1;
  return{link,index,a,b,port,start,end,previous,priority:fixed?0:previous?1:2};
 }).sort((a,b)=>a.priority-b.priority||String(a.link.to).localeCompare(String(b.link.to))||a.port-b.port||String(a.link.from).localeCompare(String(b.link.from))||a.index-b.index);
 for(const job of jobs){
  const {index,a,b,port,start,end,previous,link}=job;if(!a||!b)continue;
  const fanIn=logicFanInRoute(link,b,start,end);let points=null,anchor=null;
  if(previous&&!previous.blocked){
   const old=previous.anchor||previous,candidate=stretchLogicRoute(old,start,end,fanIn),distance=Math.abs(start.x-end.x)+Math.abs(start.y-end.y);
   if(pathClear(candidate,a.id,b.id)&&!logicRouteOverlap(candidate,a.id,b.id,port,routed)&&basePathScore(candidate)<=distance*1.75+240){points=candidate;anchor=old}
  }
  if(!points){
   const preferred=fanIn&&pathClear(fanIn,a.id,b.id)?fanIn:null;
   if(preferred&&routeInteractionPenalty(preferred,routed,a.id,b.id)===0)points=preferred;
   else{
    points=routePoints(start,end,routed.length,a.id,b.id,routed);
    if(preferred&&(points.blocked||scorePath(preferred,routed,a.id,b.id)<=scorePath(points,routed,a.id,b.id)))points=preferred;
   }
  }
  if(!anchor)anchor={points:points.map(p=>({...p})),start:{...start},end:{...end}};
  const route={points,fromId:a.id,toId:b.id,port,start:{...start},end:{...end},anchor,blocked:!!points.blocked};routes[index]=route;routed.push(route);
 }
 logicRouteCache={key,routes,junctions:logicRouteJunctions(routes)};return logicRouteCache;
}
function getLogicRoutes(){
 refreshLogicGeometry();
 const key=JSON.stringify([graph.nodes.map(n=>{const g=logicGeometry.get(n.id);return[n.id,n.x,n.y,n.type,latchMode(n),g.width,g.height,g.top]}),graph.links,logicSegmentGesture?.preview]);
 if(key===logicRouteCache.key)return logicRouteCache;
 const previous=new Map(logicRouteCache.routes.filter(Boolean).map(r=>[JSON.stringify([r.fromId,r.toId,r.port]),r]));
 const jobs=graph.links.map(link=>{const a=graph.nodes.find(n=>n.id===link.from),b=graph.nodes.find(n=>n.id===link.to);return{link,a,b,start:a&&portPoint(a,true),end:b&&portPoint(b,false,Number(link.port||0))}});
 let routes=null;
 if(logicAvoid){
  try{
   // Retain the topology during a drag; reroute the graph only if it stops fitting.
   const stable=jobs.map(({link,a,b,start,end})=>{
    if(!a||!b)return null;const old=previous.get(logicRouteKey(link));if(!old)return null;
    const manual=logicManualPoints(link,start,end),points=manual||stretchLogicRoute(old.anchor||old,start,end,null);
    return{...old,points,start,end,blocked:!pathClear(points,a.id,b.id),manual:!!manual};
   });
   const valid=previous.size===graph.links.length&&stable.every(r=>r&&(r.manual||(!r.blocked&&basePathScore(r.points)<=(Math.abs(r.start.x-r.end.x)+Math.abs(r.start.y-r.end.y))*1.75+240)))&&stable.every((r,i)=>r.manual||!logicRouteOverlap(r.points,r.fromId,r.toId,r.port,stable.slice(0,i)));
   if(valid)routes=stable;
   else{
    const planned=logicAvoidRoutes();
    routes=jobs.map(({link,a,b,start,end},index)=>{
     if(!a||!b)return null;const manual=logicManualPoints(link,start,end),p=planned.get(logicRouteKey(link));
     let points=manual||p;if(!points||(!manual&&!pathClear(points,a.id,b.id)))points=routePoints(start,end,index,a.id,b.id,[]);
     return{points,fromId:a.id,toId:b.id,port:Number(link.port||0),start,end,manual:!!manual,blocked:!!points.blocked||!pathClear(points,a.id,b.id),anchor:{points:points.map(p=>({...p})),start:{...start},end:{...end}}};
    });
   }
  }catch(e){console.warn('OFE routing failed; falling back',e);disposeLogicAvoid();logicAvoid=null;logicRouterStatus='fallback'}
 }
 if(!routes)routes=getFallbackLogicRoutes().routes;
 routes=routes.map((r,index)=>{
  if(!r)return r;const link=graph.links[index],preview=logicSegmentGesture?.link===link&&logicSegmentGesture.preview,manual=logicManualPoints(link,r.start,r.end);
  if(!preview&&!manual)return r;const points=preview||manual;return{...r,points,manual:true,blocked:!pathClear(points,r.fromId,r.toId)};
 });
 logicRouteCache={key,routes,junctions:logicRouteJunctions(routes)};return logicRouteCache;
}
function logicEditableSegments(route){
 let points=route.points.map(p=>({...p}));
 if(points.length===2){const a=points[0],b=points[1];if(b.x-a.x<96)return[];points=[a,{x:a.x+36,y:a.y},{x:a.x+36,y:a.y},{x:b.x-36,y:b.y},{x:b.x-36,y:b.y},b];return[{points,index:3}]}
 return points.slice(2,-1).map((p,i)=>({points,index:i+2})).filter(({index})=>Math.hypot(points[index].x-points[index-1].x,points[index].y-points[index-1].y)>12);
}
function drawLogicSegmentHandles(cache){
 document.querySelectorAll('#logic_canvas>.logic-segment-handle').forEach(e=>e.remove());
 if(logicSelectedLinks.size>1)return;
 const route=cache.routes[selLink],link=graph.links[selLink];if(!route||!link)return;
 const canvas=document.getElementById('logic_canvas');
 for(const item of logicEditableSegments(route)){
  const a=item.points[item.index-1],b=item.points[item.index],vertical=a.x===b.x,handle=document.createElement('span'),dot=document.createElement('span');
  handle.className='logic-segment-handle '+(vertical?'vertical':'horizontal');handle.title=t('Leitungssegment verschieben','Move wire segment');
  Object.assign(handle.style,{left:(a.x+b.x)/2+'px',top:(a.y+b.y)/2+'px',width:32/logicZoom+'px',height:32/logicZoom+'px'});
  dot.className='logic-segment-dot';dot.style.width=dot.style.height=12/logicZoom+'px';handle.append(dot);
  handle.onpointerdown=ev=>beginLogicSegment(ev,link,Number(selLink),item);canvas.append(handle);
 }
}
function beginLogicSegment(ev,link,index,item){
 if(!ev.isPrimary||ev.button!==0||logicSegmentGesture)return;
 ev.preventDefault();ev.stopPropagation();resetLogicWire();
 logicSegmentGesture={id:ev.pointerId,link,index,segment:item.index,points:item.points.map(p=>({...p})),preview:null,x:ev.clientX,y:ev.clientY};
 document.getElementById('logic_workspace').setPointerCapture(ev.pointerId);
}
function moveLogicSegment(ev){
 const g=logicSegmentGesture;if(!g||g.id!==ev.pointerId)return;
 if(!g.preview&&Math.hypot(ev.clientX-g.x,ev.clientY-g.y)<4)return;
 const p=g.points.map(p=>({...p})),a=p[g.segment-1],b=p[g.segment],axis=a.x===b.x?'x':'y',delta=(axis==='x'?ev.clientX-g.x:ev.clientY-g.y)/logicZoom,value=a[axis]+delta;
 a[axis]=b[axis]=logicGridEnabled&&!ev.altKey?logicSnap(value):Math.round(value);g.preview=simplifyPts(p);logicRouteCache.key=null;drawLinks();
}
function endLogicSegment(ev){
 const g=logicSegmentGesture;if(!g||g.id!==ev.pointerId)return;
 if(g.preview){
  const route=getLogicRoutes().routes[g.index],p=g.preview;
  const candidate={...g.link,route:{p:p.map(q=>[q.x,q.y]),s:[route.start.x,route.start.y],t:[route.end.x,route.end.y]}},clean=normaliseLogicLink(candidate);
  if(!route.blocked&&clean.route&&!logicRouteOverlap(p,route.fromId,route.toId,route.port,getLogicRoutes().routes.filter((r,i)=>r&&i!==g.index)))g.link.route=clean.route;
  else msg(t('Leitungsweg blockiert; vorheriger Weg bleibt erhalten.','Wire route blocked; previous route retained.'));
 }
 logicSegmentGesture=null;logicRouteCache.key=null;
 const w=document.getElementById('logic_workspace');if(w.hasPointerCapture(g.id))w.releasePointerCapture(g.id);drawLinks();
}
function logicWireBounds(ev){
 const w=document.getElementById('logic_workspace'),r=w.getBoundingClientRect(),hit=document.elementFromPoint(ev.clientX,ev.clientY);
 if(ev.clientX<r.left+w.clientLeft||ev.clientX>r.left+w.clientLeft+w.clientWidth||ev.clientY<r.top+w.clientTop||ev.clientY>r.top+w.clientTop+w.clientHeight||!hit||!w.contains(hit))return null;
 return r;
}
function logicPortCandidate(ev,compatible){
 if(!logicWireBounds(ev))return null;
 refreshLogicGeometry();
 const workspace=document.getElementById('logic_workspace'),bounds=workspace.getBoundingClientRect(),canvas=document.getElementById('logic_canvas').getBoundingClientRect(),touch=ev.pointerType==='touch',radius=compatible?(touch?32:22):(touch?22:14);
 let best=null,previous=null,invalid=false;
 for(const node of graph.nodes){
  if(compatible&&!linkFrom)return null;
  const g=logicGeometry.get(node.id);if(!g.el)continue;
  const pins=[...g.el.querySelectorAll('.logic-port')];
  for(const pin of pins){
   const out=pin.classList.contains('logic-out'),port=Number(pin.dataset.port||0);
   const point=portPoint(node,out,port),x=canvas.left+point.x*logicZoom,y=canvas.top+point.y*logicZoom;
   if(x<bounds.left+workspace.clientLeft||x>bounds.left+workspace.clientLeft+workspace.clientWidth||y<bounds.top+workspace.clientTop||y>bounds.top+workspace.clientTop+workspace.clientHeight)continue;
   const distance=Math.hypot(x-ev.clientX,y-ev.clientY);
   if(distance>radius)continue;
   const cover=document.elementFromPoint(x,y),owner=cover&&cover.closest('.logic-node');
   if(!cover||!workspace.contains(cover)||(owner&&owner.dataset.id!==node.id))continue;
   if(compatible&&(node.id===linkFrom.id||out===linkFrom.out)){if(distance<=Math.max(3,6*logicZoom))invalid=true;continue}
   const candidate={node,out,port,point,pin,distance};if(!best||distance<best.distance)best=candidate;
   if(compatible&&logicWireTarget&&node.id===logicWireTarget.node.id&&out===logicWireTarget.out&&port===logicWireTarget.port)previous=candidate;
  }
 }
 if(invalid)return null;
 if(compatible&&best&&previous&&previous.distance<=best.distance+2)best=previous;
 return best;
}
function setLogicWireTarget(target){
 if(logicWireTarget&&(!target||logicWireTarget.pin!==target.pin)){logicWireTarget.pin.classList.remove('hot');const el=logicGeometry.get(logicWireTarget.node.id)?.el;if(el)el.classList.remove('wire-target')}
 logicWireTarget=target;
 if(target){target.pin.classList.add('hot');logicGeometry.get(target.node.id)?.el?.classList.add('wire-target')}
}
function resetLogicWire(){
 const segment=logicSegmentGesture;logicSegmentGesture=null;if(segment)logicRouteCache.key=null;
 const gesture=logicWireGesture;logicWireGesture=null;
 if(logicWireScrollFrame)cancelAnimationFrame(logicWireScrollFrame);logicWireScrollFrame=0;
 document.querySelectorAll('.logic-port.hot,.logic-port.wire-source,.logic-node.wire-target').forEach(e=>e.classList.remove('hot','wire-source','wire-target'));
 const w=document.getElementById('logic_workspace');w.classList.remove('is-wiring');
 if(gesture&&w.hasPointerCapture(gesture.id))w.releasePointerCapture(gesture.id);
 if(segment&&w.hasPointerCapture(segment.id))w.releasePointerCapture(segment.id);
 logicWireTarget=null;logicReconnect=null;linkFrom=null;linkDrag=null;lineDrag=null;
}
function markLogicWireSource(){
 refreshLogicGeometry();if(!linkFrom)return;
 const el=logicGeometry.get(linkFrom.id)?.el,pin=el&&el.querySelector(linkFrom.out?'.logic-out':'.logic-in[data-port="'+linkFrom.port+'"]');
 if(pin)pin.classList.add('wire-source');document.getElementById('logic_workspace').classList.add('is-wiring');
}
function drawLogicWirePreview(){
 const svg=document.getElementById('logic_svg');let path=svg.querySelector('.logic-line.pending');
 if(!linkFrom||!linkDrag){if(path)path.remove();return}
 const node=graph.nodes.find(n=>n.id===linkFrom.id);if(!node)return;
 if(!path){path=document.createElementNS('http://www.w3.org/2000/svg','path');svg.append(path)}
 let p1=portPoint(node,linkFrom.out,linkFrom.port),p2=linkDrag;if(!linkFrom.out)[p1,p2]=[p2,p1];
 path.setAttribute('d',previewPath(p1,p2));path.setAttribute('class','logic-line pending'+(logicWireTarget?' snapped':''));
}
function updateLogicWire(ev){
 if(!linkFrom)return;
 const target=logicPortCandidate(ev,true);setLogicWireTarget(target);linkDrag=target?target.point:screenToCanvas(ev);drawLogicWirePreview();
}
function scrollLogicWire(){
 logicWireScrollFrame=0;const g=logicWireGesture;if(!g||!g.moved||!linkFrom)return;
 const w=document.getElementById('logic_workspace'),r=w.getBoundingClientRect(),speed=(p,min,max)=>p<min+32?-Math.min(10,(min+32-p)/3):p>max-32?Math.min(10,(p-max+32)/3):0;
 if(g.x>=r.left&&g.x<=r.right&&g.y>=r.top&&g.y<=r.bottom){
  const x=w.scrollLeft,y=w.scrollTop;w.scrollLeft+=speed(g.x,r.left,r.right);w.scrollTop+=speed(g.y,r.top,r.bottom);
  if(x!==w.scrollLeft||y!==w.scrollTop)updateLogicWire({clientX:g.x,clientY:g.y,pointerType:g.type});
 }
 logicWireScrollFrame=requestAnimationFrame(scrollLogicWire);
}
function beginLogicWire(ev){
 if(drag||logicSelectionBox){if(ev.pointerId!==(drag||logicSelectionBox).pointerId){ev.preventDefault();ev.stopPropagation()}return}
 if(logicSegmentGesture){if(ev.pointerId!==logicSegmentGesture.id){ev.preventDefault();ev.stopPropagation()}return}
 if(ev.target.closest('.logic-segment-handle'))return;
 if(logicWireGesture){if(ev.pointerId!==logicWireGesture.id){ev.preventDefault();ev.stopPropagation()}return}
 if(!ev.isPrimary||ev.button!==0)return;
 if(ev.target.closest('button,input,select,textarea'))return;
 const source=(linkFrom&&logicPortCandidate(ev,true))||logicPortCandidate(ev,false);if(!source){if(linkFrom)clearPending();return}
 ev.preventDefault();ev.stopPropagation();
 let armed=!!linkFrom;
 if(linkFrom&&source.node.id===linkFrom.id){clearPending();return}
 if(!linkFrom||source.out===linkFrom.out){resetLogicWire();startLink(source.node.id,source.out,source.port);armed=false}
 logicWireGesture={id:ev.pointerId,type:ev.pointerType,startX:ev.clientX,startY:ev.clientY,x:ev.clientX,y:ev.clientY,moved:false,armed};
 document.getElementById('logic_workspace').setPointerCapture(ev.pointerId);updateLogicWire(ev);
}
function startLogicReconnect(ev,link,index,vals){
 if(!ev.isPrimary||ev.button!==0)return;
 if(ev.ctrlKey||ev.metaKey||ev.shiftKey||logicSelectionMode){ev.preventDefault();ev.stopPropagation();const keys=new Set(logicSelectedLinks),key=logicRouteKey(link);if(keys.has(key))keys.delete(key);else keys.add(key);setLogicSelection([...logicSelectedNodes],sel,[...keys]);return}
 if(logicSelectedLinks.size>1&&logicSelectedLinks.has(logicRouteKey(link))){ev.preventDefault();ev.stopPropagation();return}
 ev.preventDefault();ev.stopPropagation();resetLogicWire();logicSelectedNodes.clear();sel=null;selLink=index;markSelected();
 lineDrag={link,clientX:ev.clientX,clientY:ev.clientY};
 logicWireGesture={id:ev.pointerId,type:ev.pointerType,startX:ev.clientX,startY:ev.clientY,x:ev.clientX,y:ev.clientY,moved:false,armed:false};
 document.getElementById('logic_workspace').setPointerCapture(ev.pointerId);renderInspector();drawLinks(vals);
}
function moveLogicWire(ev){
 if(drag){moveLogicNodeGroup(ev);return}
 if(logicSelectionBox){moveLogicSelectionBox(ev);return}
 if(logicSegmentGesture){moveLogicSegment(ev);return}
 const g=logicWireGesture;if(g&&ev.pointerId!==g.id)return;
 if(!g&&!linkFrom)return;
 if(g){g.x=ev.clientX;g.y=ev.clientY;if(Math.hypot(g.x-g.startX,g.y-g.startY)>(g.type==='touch'?8:4))g.moved=true}
 if(lineDrag&&g&&g.moved){const old=lineDrag.link;lineDrag=null;startLink(old.from,true);logicReconnect=old;drawLinks()}
 if(linkFrom){updateLogicWire(ev);if(g&&g.moved&&!logicWireScrollFrame)logicWireScrollFrame=requestAnimationFrame(scrollLogicWire)}
}
function endLogicWire(ev){
 if(drag||logicSelectionBox){finishLogicSelection(ev);return}
 if(logicSegmentGesture){endLogicSegment(ev);return}
 const g=logicWireGesture;if(g&&ev.pointerId!==g.id)return;
 if(!g)return;
 if(linkFrom){
  if(!g.moved&&!g.armed&&!logicReconnect){
   logicWireGesture=null;const w=document.getElementById('logic_workspace');if(w.hasPointerCapture(g.id))w.releasePointerCapture(g.id);
   linkDrag=null;setLogicWireTarget(null);drawLogicWirePreview();return;
  }
  const target=logicPortCandidate(ev,true);
  if(target){finishLink(target.node,target.out,target.port);return}
 }
 clearPending();
}
function setupLogicWireInteraction(){
 const w=document.getElementById('logic_workspace');w.addEventListener('pointerdown',beginLogicWire,true);
 w.addEventListener('lostpointercapture',ev=>{if((logicWireGesture&&logicWireGesture.id===ev.pointerId)||(logicSegmentGesture&&logicSegmentGesture.id===ev.pointerId))clearPending()});
 window.addEventListener('pointerdown',ev=>{if(linkFrom&&!logicWireGesture&&!w.contains(ev.target))clearPending()});
 window.addEventListener('keydown',ev=>{if(ev.key==='Escape'&&(linkFrom||lineDrag||logicSegmentGesture)){ev.preventDefault();clearPending()}});
 window.addEventListener('blur',()=>{if(linkFrom||lineDrag||logicSegmentGesture)clearPending()});
 setupLogicRoutingTools();setupLogicSelection();setupLogicRouter();
}
)LOGICWIRE";
