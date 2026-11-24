#pragma once

static const char OFE_WEB_LOGIC_RESPONSIVE_STYLE[] PROGMEM = R"LOGICCSS(
main{width:100%!important;max-width:none!important}
.logic-top{grid-template-columns:280px minmax(0,1fr) 300px}
.logic-panel{min-width:0}.logic-panel input,.logic-panel select{min-width:0;max-width:100%}
.logic-panel h2{font-size:18px;margin:0 0 14px}
.logic-file-toolbar{align-items:center}.logic-file-toolbar .logic-enable{margin:0}
.logic-workspace{min-height:420px;height:calc(100dvh - 270px);overscroll-behavior:contain;-webkit-overflow-scrolling:touch}
.logic-canvas-scale{position:relative;overflow:hidden;width:2800px;height:1700px}
.logic-canvas-scale>.logic-canvas{position:absolute;left:0;top:0;transform-origin:0 0}
.logic-mobile-tabs{display:none}
.logic-capacity{display:flex;align-items:center;flex-wrap:wrap;gap:8px 18px;margin:8px 0 12px;color:var(--muted);font-size:12px}
.logic-limit-warning{margin:0 0 12px;color:#f1bc5b;font-size:12px;overflow-wrap:anywhere}
.logic-capacity .at-limit{color:#f1bc5b}
.logic-port{touch-action:none}
.logic-port::before{content:"";position:absolute;left:50%;top:50%;width:clamp(16px,calc(28px / var(--logic-zoom,1)),56px);height:clamp(16px,calc(28px / var(--logic-zoom,1)),56px);transform:translate(-50%,-50%)}
@media(pointer:coarse){.logic-port::before{width:clamp(22px,calc(44px / var(--logic-zoom,1)),88px);height:clamp(22px,calc(44px / var(--logic-zoom,1)),88px)}}
.logic-port-name{transform:translateY(calc(-100% - 3px))}
.logic-port.hot{background:#59c77a;box-shadow:0 0 0 4px rgba(89,199,122,.25)}
.logic-port.wire-source{background:#f4c25b}
.logic-node.wire-target{border-color:#59c77a}
.logic-workspace.is-wiring{cursor:crosshair}
.logic-workspace.is-wiring .logic-port:hover:not(.hot):not(.wire-source){background:#4a90d9}
.logic-line.pending.snapped{stroke:#59c77a;stroke-dasharray:none}
.logic-line-halo{stroke:#11151a;stroke-width:8;fill:none;pointer-events:none;stroke-linecap:round;stroke-linejoin:round}
.logic-line-junction{fill:#526072;pointer-events:none}.logic-line-junction.is-on{fill:#59c77a}.logic-line-junction.live-flow-on{fill:#4a90d9}
.logic-line.route-blocked{stroke:#e68172;stroke-dasharray:5 5}
.logic-segment-handle{position:absolute;transform:translate(-50%,-50%);display:grid;place-items:center;z-index:3;pointer-events:all;touch-action:none}
.logic-segment-dot{display:block;background:#f4c25b;border:1px solid #11151a;border-radius:2px;pointer-events:none}
.logic-segment-handle.vertical{cursor:ew-resize}.logic-segment-handle.horizontal{cursor:ns-resize}
.logic-tools{display:flex;flex-wrap:wrap;gap:6px;align-items:center;margin-bottom:8px}
.logic-tools [aria-pressed=true]{color:#77dd96;border-color:#59c77a}
.logic-selection-count{min-width:0;font-size:12px;color:#f4c25b}
.logic-selection-box{position:absolute;z-index:4;pointer-events:none;border:1px solid #f4c25b;background:#f4c25b18}
.logic-workspace.is-selecting{touch-action:none;cursor:crosshair}
.logic-workspace.is-selecting .logic-svg{pointer-events:none}
.logic-workspace.is-selecting .logic-line{pointer-events:stroke}
.logic-workspace.is-selecting .logic-line-halo{pointer-events:stroke;stroke:transparent;stroke-width:calc(28px / var(--logic-zoom,1))}
.logic-tools button:disabled{opacity:.4;cursor:default}.logic-router-status{color:#e8bf6b;font-size:12px}
.logic-canvas{background-size:18px 18px}
.logic-tools button{width:40px;min-height:40px;height:40px;padding:8px;flex:none}
.logic-tools .logic-zoom-value{width:60px;font-size:12px}
.logic-tool-svg{width:20px;height:20px;display:block;flex:none}
.logic-command-icon{display:none}
.logic-node-picker{display:none}
.logic-connection-port{border:0;padding:0;margin:12px 0}
.logic-connection-row{display:flex;gap:8px;align-items:center;min-width:0;margin:6px 0}
.logic-connection-row>span{min-width:0;flex:1;overflow-wrap:anywhere;font-size:12px}
.logic-connection-row>button{width:40px;height:40px;min-height:40px;padding:8px;flex:none}
.logic-connections h3{font-size:14px;margin:18px 0 8px}
@media(max-width:1200px){
 html{scroll-padding-top:calc(var(--logic-header-height,0px) + 72px)}
 .logic-mobile-tabs{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:6px;margin-bottom:10px;position:sticky;top:var(--logic-header-height,0px);z-index:4;background:var(--bg);padding:6px 0}
 .logic-mobile-tabs button{min-width:0;padding:8px 4px;font-size:12px;min-height:44px}
 .logic-mobile-tabs [aria-selected=true]{background:#326da5;border-color:#417fb9;color:#fff}
 .logic-top{grid-template-columns:minmax(0,1fr);gap:0}
 .logic-top>[data-logic-pane]{display:none}
 .logic-top[data-pane=plan]>[data-logic-pane=plan],.logic-top[data-pane=blocks]>[data-logic-pane=blocks],.logic-top[data-pane=properties]>[data-logic-pane=properties]{display:block}
 .logic-panel{padding:10px}
 .logic-line{pointer-events:none}
 .logic-workspace{height:calc(100dvh - 370px);min-height:260px;max-height:720px}
 .logic-file-toolbar{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:6px}
 .logic-file-toolbar #logic_slot{grid-column:1/3;max-width:none!important;width:100%}
 .logic-file-toolbar #logic_name{grid-column:3/6;max-width:none!important;width:100%}
 .logic-file-toolbar .logic-enable{grid-column:1/-1;margin:0;padding:0 8px;min-height:40px}
 .logic-file-toolbar .logic-command{width:100%;min-width:0;min-height:40px;padding:8px}
 .logic-command-label{display:none}.logic-command-icon{display:block}
 .logic-inspector .logic-toolbar>*{flex:1;min-width:0;white-space:normal}
 .logic-node-picker{display:block}
 .sim-row{grid-template-columns:minmax(0,1fr) 110px auto}
 .sim-row>span{min-width:0;overflow-wrap:anywhere}
}
)LOGICCSS";

// Lucide tool icons (ISC), matching the existing local header icon license.
static const char OFE_WEB_LOGIC_RESPONSIVE_SCRIPT[] PROGMEM = R"LOGICUI(
let logicZoom=1;
let logicLimits={nodes:32,links:64,outputs:32};
function applyLogicCapacity(data){
 const nodes=Number(data.max_nodes),links=Number(data.max_links);
 const outputs=Number(data.max_outputs||32);
 if(Number.isInteger(nodes)&&nodes>0&&nodes<=127&&Number.isInteger(links)&&links>0&&links<255&&Number.isInteger(outputs)&&outputs>0&&outputs<255)logicLimits={nodes,links,outputs};
 renderLogicCapacity();
}
function logicCapacityMessage(nodes,links){
 if(nodes>logicLimits.nodes)return t('Maximal ','At most ')+logicLimits.nodes+t(' Bausteine pro Definition.',' blocks per definition.');
 if(links>logicLimits.links)return t('Maximal ','At most ')+logicLimits.links+t(' Verbindungen pro Definition.',' connections per definition.');
 if(new Set(graph.nodes.filter(n=>n.type==='output'&&n.signal).map(n=>n.signal)).size>logicLimits.outputs)return t('Maximal ','At most ')+logicLimits.outputs+t(' unterschiedliche Ausgangsziele pro Definition.',' distinct output targets per definition.');
 return '';
}
function renderLogicCapacity(){
 const nodes=document.getElementById('logic_capacity_nodes'),links=document.getElementById('logic_capacity_links'),warning=document.getElementById('logic_capacity_warning');
 if(!nodes)return;
 for(const [element,count,max,label] of [[nodes,graph.nodes.length,logicLimits.nodes,t('Bausteine','Blocks')],[links,graph.links.length,logicLimits.links,t('Verbindungen','Connections')]]){
  const text=label+': '+count+' / '+max;if(element.textContent!==text)element.textContent=text;element.classList.toggle('at-limit',count>=max);
 }
 const text=logicCapacityMessage(graph.nodes.length,graph.links.length)||(graph.nodes.length===logicLimits.nodes?t('Bausteinlimit erreicht.','Block limit reached.'):graph.links.length===logicLimits.links?t('Verbindungslimit erreicht.','Connection limit reached.'):'');
 warning.hidden=!text;if(warning.textContent!==text)warning.textContent=text;
}
function ensureLogicCapacity(nodes,links){
 const text=logicCapacityMessage(nodes,links);if(!text)return true;
 const warning=document.getElementById('logic_capacity_warning');warning.textContent=text;warning.hidden=false;msg(text);alert(text);return false;
}
function logicToolSvg(icon){
 const paths={filePlus:'<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8Z"/><path d="M14 2v6h6M12 12v6M9 15h6"/>',fileX:'<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8Z"/><path d="M14 2v6h6m-6 5-4 4m0-4 4 4"/>',plus:'<path d="M12 5v14M5 12h14"/>',minus:'<path d="M5 12h14"/>',fit:'<path d="M8 3H5a2 2 0 0 0-2 2v3m13-5h3a2 2 0 0 1 2 2v3M3 16v3a2 2 0 0 0 2 2h3m8 0h3a2 2 0 0 0 2-2v-3"/><rect x="8" y="8" width="8" height="8" rx="1"/>',save:'<path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h12l4 4v12a2 2 0 0 1-2 2Z"/><path d="M7 3v6h10V3M7 21v-8h10v8"/>',trash:'<path d="M3 6h18M9 6V4h6v2M5 6l1 14h12l1-14M10 10v6M14 10v6"/>',layout:'<rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/><path d="M14 6h4v4M6 14v4h4"/>',close:'<path d="m6 6 12 12M6 18 18 6"/>'};
 return '<svg class="logic-tool-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">'+(paths[icon]||paths.plus)+'</svg>';
}
function logicMobile(){return matchMedia('(max-width:1200px)').matches}
function showLogicPane(pane){
 if(pane!=='plan')cancelLogicSelection();
 if(pane!=='plan'&&(linkFrom||lineDrag||logicSegmentGesture))clearPending();
 const root=document.querySelector('.logic-top'),previous=root.dataset.pane;root.dataset.pane=pane;
 document.querySelectorAll('[data-logic-tab]').forEach(b=>{let on=b.dataset.logicTab===pane;b.setAttribute('aria-selected',String(on));b.tabIndex=on?0:-1});
 if(pane==='plan')requestAnimationFrame(()=>logicApplyZoom());
 if(logicMobile()&&previous&&previous!==pane)requestAnimationFrame(()=>root.scrollIntoView({block:'start'}));
}
function logicApplyZoom(){
 const canvas=document.getElementById('logic_canvas'),scale=document.getElementById('logic_canvas_scale');
 if(!scale)return;scale.style.width=(2800*logicZoom)+'px';scale.style.height=(1700*logicZoom)+'px';canvas.style.transform='scale('+logicZoom+')';canvas.style.setProperty('--logic-zoom',String(logicZoom));
 document.getElementById('logic_zoom_value').textContent=Math.round(logicZoom*100)+'%';
}
function logicSetZoom(value){
 cancelLogicSelection();
 if(linkFrom||lineDrag||logicSegmentGesture)clearPending();
 const w=document.getElementById('logic_workspace'),x=(w.scrollLeft+w.clientWidth/2)/logicZoom,y=(w.scrollTop+w.clientHeight/2)/logicZoom;
 logicZoom=Math.max(.1,Math.min(1.5,value));logicApplyZoom();w.scrollLeft=x*logicZoom-w.clientWidth/2;w.scrollTop=y*logicZoom-w.clientHeight/2;
}
function logicFitGraph(){
 cancelLogicSelection();
 if(linkFrom||lineDrag||logicSegmentGesture)clearPending();
 const w=document.getElementById('logic_workspace');if(!w.clientWidth||!graph.nodes.length)return;
 const left=Math.max(0,Math.min(...graph.nodes.map(n=>Number(n.x)))-50),top=Math.max(0,Math.min(...graph.nodes.map(n=>Number(n.y)))-40);
 const heights=new Map([...document.querySelectorAll('.logic-node')].map(e=>[e.dataset.id,e.offsetHeight]));
 const right=Math.max(...graph.nodes.map(n=>Number(n.x)+NODE_W))+50,bottom=Math.max(...graph.nodes.map(n=>Number(n.y)+(heights.get(n.id)||180)))+40;
 logicZoom=Math.max(.1,Math.min(1,(w.clientWidth-16)/(right-left),(w.clientHeight-16)/(bottom-top)));
 logicApplyZoom();w.scrollLeft=left*logicZoom;w.scrollTop=top*logicZoom;
}
function logicRevealNode(){
 if(!logicMobile())return;showLogicPane('plan');
 requestAnimationFrame(()=>{const n=graph.nodes.find(n=>n.id===sel),w=document.getElementById('logic_workspace');if(n){w.scrollLeft=(Number(n.x)+NODE_W/2)*logicZoom-w.clientWidth/2;w.scrollTop=(Number(n.y)+70)*logicZoom-w.clientHeight/2}});
}
function renderLogicConnections(n){
 const picker=document.getElementById('logic_node_picker');if(picker){picker.replaceChildren(new Option(t('Baustein waehlen','Select block'),''));graph.nodes.forEach(node=>picker.add(new Option(node.name||nodeTypeLabel(node),node.id)));picker.value=n?n.id:''}
 const target=document.getElementById('logic_connections');if(!target)return;
 target.replaceChildren();target.hidden=!n||!inputPorts(n.type,n).length;if(target.hidden)return;
 const title=document.createElement('h3');title.textContent=t('Verbindungen','Connections');target.append(title);
 inputPorts(n.type,n).forEach((port,index)=>{
  const group=document.createElement('div');group.className='logic-connection-port';
  const label=document.createElement('label');label.textContent=port;
  const select=document.createElement('select');select.dataset.logicInput=String(index);select.setAttribute('aria-label',t('Quelle fuer ','Source for ')+port);
  select.add(new Option(t('Quelle hinzufuegen','Add source'),''));
  for(const source of graph.nodes){
   if(source.id===n.id||source.type==='output'||graph.links.some(l=>l.from===source.id&&l.to===n.id&&Number(l.port||0)===index))continue;
   select.add(new Option(source.name||nodeTypeLabel(source),source.id));
  }
  select.onchange=()=>{if(select.value){addLink(select.value,n.id,index);render()}};
  label.append(select);group.append(label);
  graph.links.filter(l=>l.to===n.id&&Number(l.port||0)===index).forEach(link=>{
   const row=document.createElement('div');row.className='logic-connection-row';
   const text=document.createElement('span'),source=graph.nodes.find(s=>s.id===link.from);text.textContent=source?(source.name||nodeTypeLabel(source)):link.from;
   const edit=document.createElement('button');edit.type='button';edit.className='secondary';edit.dataset.logicEditLink='';edit.title=t('Leitungsweg bearbeiten','Edit wire route');edit.setAttribute('aria-label',edit.title);edit.innerHTML=logicToolSvg('layout');
   edit.onclick=()=>{sel=null;selLink=graph.links.indexOf(link);showLogicPane('plan');drawLinks();renderInspector();requestAnimationFrame(()=>{const route=getLogicRoutes().routes[selLink],item=route&&logicEditableSegments(route)[0];if(!item)return;const a=item.points[item.index-1],b=item.points[item.index],w=document.getElementById('logic_workspace');w.scrollLeft=Math.max(0,(a.x+b.x)/2*logicZoom-w.clientWidth/2);w.scrollTop=Math.max(0,(a.y+b.y)/2*logicZoom-w.clientHeight/2)})};
   const remove=document.createElement('button');remove.type='button';remove.className='secondary';remove.title=t('Verbindung loeschen','Delete connection');remove.setAttribute('aria-label',remove.title);remove.innerHTML=logicToolSvg('close');
   remove.onclick=()=>{graph.links=graph.links.filter(l=>l!==link);render()};row.append(text,edit,remove);group.append(row);
  });
  target.append(group);
 });
}
function syncLogicToolLabels(){
 document.querySelectorAll('.logic-command').forEach(b=>{let label=b.querySelector('.logic-command-label');if(label){b.title=label.textContent;b.setAttribute('aria-label',label.textContent)}});
 document.querySelectorAll('[data-logic-tool]').forEach(b=>{const names={plus:t('Vergroessern','Zoom in'),minus:t('Verkleinern','Zoom out'),fit:t('Plan einpassen','Fit diagram'),reset:t('Originalgroesse','Actual size')};b.title=names[b.dataset.logicTool];b.setAttribute('aria-label',b.title)});
}
function setupLogicResponsive(){
 const root=document.querySelector('.logic-top'),panes=[...root.children],names=['blocks','plan','properties'],labels=['blocks','plan','props'];
 Object.assign(L,{plan:['Plan','Diagram']});
 const tabs=document.createElement('div');tabs.className='logic-mobile-tabs';tabs.setAttribute('role','tablist');
 for(const name of ['plan','blocks','properties']){
  const i=names.indexOf(name),p=panes[i];p.dataset.logicPane=name;p.id='logic_pane_'+name;
  const b=document.createElement('button');b.className='secondary';b.type='button';b.dataset.logicTab=name;b.dataset.l=labels[i];b.setAttribute('role','tab');b.setAttribute('aria-controls',p.id);b.onclick=()=>showLogicPane(name);
  b.onkeydown=e=>{if(!['ArrowLeft','ArrowRight','Home','End'].includes(e.key))return;e.preventDefault();const all=[...tabs.children],at=all.indexOf(b),next=e.key==='Home'?0:e.key==='End'?2:(at+(e.key==='ArrowRight'?1:2))%3;all[next].click();all[next].focus()};
  tabs.append(b);
 }
 root.before(tabs);root.after(document.getElementById('logic_msg'));showLogicPane('plan');
 const capacity=document.createElement('div');capacity.className='logic-capacity';capacity.setAttribute('role','status');capacity.setAttribute('aria-live','polite');
 for(const id of ['logic_capacity_nodes','logic_capacity_links']){const count=document.createElement('span');count.id=id;capacity.append(count)}
 const warning=document.createElement('div');warning.id='logic_capacity_warning';warning.className='logic-limit-warning';warning.hidden=true;warning.setAttribute('role','alert');root.before(capacity,warning);
 const canvas=document.getElementById('logic_canvas'),scale=document.createElement('div');scale.className='logic-canvas-scale';scale.id='logic_canvas_scale';canvas.before(scale);scale.append(canvas);
 const toolbar=panes[1].querySelector('.logic-toolbar');toolbar.classList.add('logic-file-toolbar');
 const save=document.createElement('button');save.type='button';save.dataset.l='save';save.onclick=saveGraph;toolbar.append(save);
 for(const button of toolbar.querySelectorAll('button')){
  const label=document.createElement('span');label.className='logic-command-label';label.textContent=button.textContent;
  if(button.dataset.l){label.dataset.l=button.dataset.l;delete button.dataset.l}
  const action=button.getAttribute('onclick')||'saveGraph()',icon=action.includes('deleteLogicDef')?'fileX':action.includes('newLogicDef')?'filePlus':action.includes('delete')?'trash':action.includes('autoLayout')?'layout':action.includes('saveGraph')?'save':'plus';
  const symbol=document.createElement('span');symbol.className='logic-command-icon';symbol.innerHTML=logicToolSvg(icon);button.replaceChildren(symbol,label);button.classList.add('logic-command');
 }
 const tools=document.createElement('div');tools.className='logic-tools';
 for(const icon of ['minus','reset','plus','fit']){
  const b=document.createElement('button');b.type='button';b.className='secondary';b.dataset.logicTool=icon;
  if(icon==='reset'){b.id='logic_zoom_value';b.classList.add('logic-zoom-value');b.textContent='100%'}else b.innerHTML=logicToolSvg(icon);
  b.onclick=()=>icon==='fit'?logicFitGraph():logicSetZoom(icon==='reset'?1:logicZoom+(icon==='plus'?.1:-.1));tools.append(b);
 }
 document.getElementById('logic_workspace').before(tools);
 const connections=document.createElement('div');connections.id='logic_connections';connections.className='logic-connections';panes[2].querySelector('.logic-mode-row').before(connections);
 const pickerLabel=document.createElement('label');pickerLabel.className='logic-node-picker';const pickerTitle=document.createElement('span');pickerTitle.dataset.l='blocks';
 const picker=document.createElement('select');picker.id='logic_node_picker';picker.onchange=()=>{sel=picker.value||null;selLink=null;markSelected();renderInspector()};pickerLabel.append(pickerTitle,picker);panes[2].querySelector('h2').after(pickerLabel);
 const header=document.querySelector('body>header'),measure=()=>document.documentElement.style.setProperty('--logic-header-height',(header?header.getBoundingClientRect().height:0)+'px');
 if(header&&window.ResizeObserver)new ResizeObserver(measure).observe(header);measure();logicApplyZoom();setupLogicWireInteraction();
}
window.addEventListener('pointercancel',ev=>{if(drag?.pointerId===ev.pointerId||logicSelectionBox?.pointerId===ev.pointerId)cancelLogicSelection();if(!logicWireGesture||logicWireGesture.id===ev.pointerId)clearPending()});
)LOGICUI";
