#pragma once

// Both editors use the same catalogue and the same master-computed live masks.
static const char OFE_WEB_AUTOMATION_SCRIPT[] PROGMEM = R"HTML(<script>
function ofeAutomationSignals(d,lang,moduleName){
  const de=lang==='de', tr=(a,b)=>de?a:b, modules=d.modules||[], inputs=[], outputs=[];
  const system=tr('Systemzust\u00e4nde','System states'), filter=tr('Filterzust\u00e4nde','Filter states');
  const add=(bit,addr,name,group=system)=>inputs.push({id:`state:system:${bit}:${addr}`,name,group,source_type:4,source_addr:addr,source_bit:bit});
  add(11,0,tr('Energiesparmodus aktiv','Power saving active'));
  add(12,0,tr('Alarm vorhanden','Alarm present'));
  add(13,0,tr('Warnung vorhanden','Warning present'));
  add(14,0,tr('Fehler vorhanden','Error present'));
  const filterNames=[[15,tr('Filter bald wechseln','Filter change soon')],[16,tr('Filter wechseln','Replace filter')],[17,tr('Filterdaten g\u00fcltig','Filter data valid')]];
  for(const [bit,label] of filterNames)add(bit,0,label+tr(' - mindestens ein Modul',' - any module'),filter);
  add(19,0,tr('Ger\u00e4t verbunden - mindestens ein Modul','Device connected - any module'));
  add(20,0,tr('JBC Stand aktiv - alle JBC-Module','JBC Stand active - all JBC modules'));
  for(const m of modules){
    const addr=Number(m.addr), name=moduleName(m);
    add(18,addr,name+tr(' - Modul online',' - module online'));
    if([1,9,5,7,8].includes(Number(m.type)))add(19,addr,name+tr(' - Ger\u00e4t verbunden',' - device connected'));
    if(Number(m.caps||0)&16777217)add(20,addr,name+' - JBC Stand');
    if([3,5].includes(Number(m.type)))for(const [bit,label] of filterNames)add(bit,addr,name+' - '+label,filter);
  }
  const group=tr('Systemaktionen','System actions');
  outputs.push({id:'system:eco_inhibit',name:tr('ECO sperren','Inhibit power saving'),group,target_type:7,target_addr:0,target_bit:1});
  const displays=[{addr:0,name:tr('Alle unterst\u00fctzten Displays','All supported displays')},...modules.filter(m=>Number(m.caps||0)&536870912).map(m=>({addr:Number(m.addr),name:moduleName(m)}))];
  for(const display of displays){
    outputs.push({id:`system:display:wake:${display.addr}`,name:display.name+tr(' - aufwecken (Impuls)',' - wake (pulse)'),group,target_type:7,target_addr:display.addr,target_bit:2});
    outputs.push({id:`system:display:hold:${display.addr}`,name:display.name+tr(' - Beleuchtung eingeschaltet halten',' - keep backlight on'),group,target_type:7,target_addr:display.addr,target_bit:3});
  }
  for(let bit=1;bit<=8;bit++){
    const item={id:`system:marker:${bit}`,name:tr('Merker ','Marker ')+bit,group:tr('Virtuelle Merker','Virtual markers')};
    inputs.push({...item,source_type:5,source_addr:0,source_bit:bit});
    outputs.push({...item,target_type:8,target_addr:0,target_bit:bit});
  }
  return {inputs,outputs};
}
function ofeAutomationInputInfo(d,signal){
  let match=String(signal||'').match(/^system:marker:([1-8])$/);
  if(match)return {known:Number.isFinite(d.automation_markers),on:!!(Number(d.automation_markers||0)&(1<<(Number(match[1])-1))),online:true};
  match=String(signal||'').match(/^state:system:(\d+):(\d+)$/);
  if(!match)return null;
  const bit=Number(match[1]),addr=Number(match[2]),row=addr?(d.modules||[]).find(m=>Number(m.addr)===addr):d;
  const mask=Number(row&&row.system_inputs_mask||0),valid=bit<15||bit>16||!!(mask&(1<<17))||!!(mask&(1<<bit));
  return {known:bit>=11&&bit<=20&&!!row&&Number.isFinite(row.system_inputs_mask)&&valid,on:!!(mask&(1<<bit)),online:!!row};
}
function ofeSignalModuleName(m){return String(m.label||m.display_name||m.name||m.type_name||('0x'+Number(m.addr).toString(16).toUpperCase()))}
function ofeSignalModuleLabel(m){return '0x'+Number(m.addr).toString(16).toUpperCase().padStart(2,'0')+' '+ofeSignalModuleName(m)}
function ofeSignalNormalizeDef(value){
  const e={...value,meta:{...(value.meta||{})}};
  e.type=String(e.type||'').toLowerCase();
  const role=String(e.meta.role||'').toLowerCase().replace(/[ -]/g,'_');
  if(role)e.meta.role=role;
  const access=String(e.meta.access||e.meta.mode||'').toLowerCase();
  let mode=String(e.mode||'').toLowerCase();
  if(access.includes('w')&&!mode.includes('w'))mode=access;
  if(['main_output_power','output_power'].includes(role)){if(!e.type||e.type==='sensor')e.type='number';if(!mode.includes('w'))mode='rw'}
  if(['main_output_enable','output_enable','output'].includes(role)){if(!e.type||e.type==='binary_sensor')e.type='switch';if(!mode.includes('w'))mode='rw'}
  e.mode=mode||access||'ro';
  return e;
}
function ofeSignalDefinitions(m,lang){
  const definitions=new Map(),de=lang==='de';let profileOrd=0;
  for(let line of String(m.universal_descriptor||'').split(/\r?\n/)){
    const joined=line.trim().match(/^line_end\s*=\s*(\d+\s+\S+\s+\S+\s+\S+.*)$/);if(joined)line=joined[1];
    const match=line.trim().match(/^p?(\d+)\s+(\S+)\s+(\S+)\s+(\S+)(?:\s+([\s\S]*))?$/i);
    if(!match)continue;
    const e={id:Number(match[1]),type:match[2],key:match[3],mode:match[4],meta:{}};
    const meta=/(\w+)=([\s\S]*?)(?=\s+\w+=|$)/g;
    for(const item of String(match[5]||'').matchAll(meta))e.meta[item[1].toLowerCase()]=item[2].trim();
    if((String(e.meta.source||'').toLowerCase()==='profile'||e.id>=20)&&!e.meta.profile_index)e.meta.profile_index=e.meta.idx||String(++profileOrd);
    e.label=(de?e.meta.de||e.meta.en:e.meta.en||e.meta.de)||e.key;
    definitions.set(e.id,ofeSignalNormalizeDef(e));
  }
  for(const value of Array.isArray(m.universal_entity_defs)?m.universal_entity_defs:[]){
    const previous=definitions.get(Number(value.id)),e={...previous,...value,meta:{...(previous&&previous.meta||{}),...(value.meta||{})}};
    if(previous){if(!value.mode||value.mode==='ro')e.mode=previous.mode;if(!value.type)e.type=previous.type;if(!value.key)e.key=previous.key}
    e.label=value.label||(de?e.meta.de||e.meta.en:e.meta.en||e.meta.de)||e.key||('Entity '+e.id);
    definitions.set(Number(e.id),ofeSignalNormalizeDef(e));
  }
  return [...definitions.values()].sort((a,b)=>Number(a.meta.profile_index||a.meta.idx||a.id)-Number(b.meta.profile_index||b.meta.idx||b.id));
}
function ofeRoutingSignals(d,lang){
  const tr=(a,b)=>lang==='de'?a:b,mods=[...(d.modules||[])].sort((a,b)=>Number(a.addr)-Number(b.addr));
  const definitions=new Map(mods.map(m=>[Number(m.addr),ofeSignalDefinitions(m,lang)]));
  const defs=m=>definitions.get(Number(m.addr))||[],entityName=e=>String(e.label||e.key||('Entity '+e.id));
  const isFan=m=>[2,3].includes(Number(m.type));
  const header=(m,key)=>{const match=String(m.universal_descriptor||'').match(new RegExp('(?:^|\\n)'+key+'=([^\\r\\n]*)','i'));return match?match[1].trim():''};
  const fanConfigured=m=>header(m,'fan_enabled')!==''?Number(header(m,'fan_enabled'))>0:!!(Number(m.caps||0)&36);
  const readable=e=>Number(e.id)>=20&&['binary_sensor','switch'].includes(e.type)&&e.mode.includes('r');
  const writable=e=>Number(e.id)>=20&&['switch','button'].includes(e.type)&&e.mode.includes('w');
  const outputRole=e=>['main_output_power','output_power','main_output_enable','output_enable','output'].includes(e.meta.role);
  const provides=m=>(!isFan(m)||fanConfigured(m))&&!!((Number(m.caps||0)&36)||((Number(m.caps||0)&65536)&&defs(m).some(outputRole)));
  const inputs=[],outputs=[];
  const addInput=(id,name,group,type,addr,bit,mainInput=false)=>inputs.push({id,name,group,source_type:type,source_addr:addr,source_bit:bit,mainInput});
  const addOutput=(id,name,group,type,addr,bit)=>outputs.push({id,name,group,target_type:type,target_addr:addr,target_bit:bit});
  const system=tr('Systemzust\u00e4nde \u00b7 Absaugung','System states \u00b7 Extractor');
  for(const [bit,id,name] of [[1,'state:extractor:active',tr('Absaugung aktiv','Extractor active')],[2,'state:extractor:continuous',tr('Dauerlauf aktiv','Continuous active')],[3,'state:extractor:afterrun',tr('Nachlauf aktiv','Afterrun active')]])addInput(id,name,system,4,0,bit);
  const levels=[tr('Hoch','High'),tr('Mittel','Medium'),tr('Niedrig','Low'),tr('Benutzer','Custom')];
  levels.forEach((name,index)=>addInput('state:extractor:level:'+index,tr('Absaugstufe ','Suction level ')+name+tr(' gew\u00e4hlt',' selected'),system,4,0,index+4));
  const selected=tr('Systemzust\u00e4nde \u00b7 Hauptausgang gew\u00e4hlt','System states \u00b7 Main output selected'),active=tr('Systemzust\u00e4nde \u00b7 Hauptausgang aktiv','System states \u00b7 Main output active');
  addInput('state:output:selected:auto',tr('Automatisch','Automatic'),selected,4,0,8);
  for(const m of mods)if(provides(m))addInput('state:output:selected:'+m.addr,ofeSignalModuleLabel(m),selected,4,Number(m.addr),9);
  for(const m of mods)if(provides(m))addInput('state:output:active:'+m.addr,ofeSignalModuleLabel(m),active,4,Number(m.addr),10);
  if(d.jbc_inputs_count){
    const group=tr('Alle JBC-Module','All JBC modules');
    addInput('jbc:0',group+' ('+d.jbc_inputs_count+')',group,1,0,0,true);
    for(const m of mods)if(m.online&&(Number(m.caps||0)&16777217))addInput('jbc:'+m.addr,ofeSignalModuleLabel(m)+' - JBC',group,1,Number(m.addr),0,true);
  }
  addOutput('main_output',tr('Absaugung Hauptausgang','Extractor output'),'',1,0,0);
  const controls=tr('Absaugung steuern','Control extractor');
  addOutput('extractor:continuous',tr('Dauerlauf','Continuous'),controls,5,0,0);
  const actions=[['level_next',tr('N\u00e4chste Absaugstufe','Cycle suction level')],['level_previous',tr('Vorherige Absaugstufe','Previous suction level')],['level_high',tr('Absaugstufe Hoch','Suction level High')],['level_medium',tr('Absaugstufe Mittel','Suction level Medium')],['level_low',tr('Absaugstufe Niedrig','Suction level Low')],['level_custom',tr('Absaugstufe Benutzer','Suction level Custom')],['power_plus_1',tr('Benutzerleistung +1 %','Custom power +1%')],['power_minus_1',tr('Benutzerleistung -1 %','Custom power -1%')],['power_plus_10',tr('Benutzerleistung +10 %','Custom power +10%')],['power_minus_10',tr('Benutzerleistung -10 %','Custom power -10%')]];
  actions.forEach(([id,name],index)=>addOutput('extractor:action:'+id,name,controls,4,0,index+1));
  const choose=tr('Hauptausgang w\u00e4hlen','Select main output');
  addOutput('extractor:select_output:0',tr('Automatisch','Automatic'),choose,6,0,0);
  for(const m of mods)if(m.online&&provides(m))addOutput('extractor:select_output:'+m.addr,ofeSignalModuleLabel(m),choose,6,Number(m.addr),0);
  for(const m of mods){
    if(!m.online)continue;
    const addr=Number(m.addr),caps=Number(m.caps||0),group=ofeSignalModuleLabel(m),entries=defs(m);
    const alias=(output,bit,e)=>String((bit===0?m[output?'io_out1_alias':'io_in1_alias']:bit===1?m[output?'io_out2_alias':'io_in2_alias']:'')||(e&&entityName(e))||((output?'OUT':'IN')+(bit+1)));
    if(isFan(m)){
      for(const e of entries.filter(e=>readable(e)&&Number(e.id)<28)){
        const bit=Number(e.id)-20;
        addInput(bit<2?'io:'+addr+':'+bit:'io:'+addr+':in:'+bit,group+' - '+alias(false,bit,e),group,3,addr,Number(e.id),['main_input','input'].includes(e.meta.role));
      }
      const main=entries.find(e=>Number(e.id)===60&&e.type==='switch'&&e.mode.includes('w'));
      if(main&&fanConfigured(m))addOutput('io:'+addr+':main',group+' - '+String(m.io_main_alias||'Relais / Fan'),group,3,addr,60);
      for(const e of entries.filter(e=>Number(e.id)>=40&&Number(e.id)<48&&e.type==='switch'&&e.mode.includes('w'))){
        const bit=Number(e.id)-40;
        addOutput(bit<2?'io:'+addr+':'+(bit+2):'io:'+addr+':out:'+bit,group+' - '+alias(true,bit,e),group,3,addr,Number(e.id));
      }
      continue;
    }
    if((caps&2048)&&!(caps&65536))for(let bit=0;bit<2;bit++)addInput('io:'+addr+':'+bit,group+' - '+alias(false,bit),group,2,addr,bit,true);
    if((caps&16384)&&!(caps&65536))for(let bit=0;bit<2;bit++){
      const label=Number(m.type)===5?(bit?tr('Licht','Light'):tr('L\u00fcfter','Fan')):alias(true,bit);
      const id=Number(m.type)===5?'weller:'+addr+':'+(bit?'light':'fan'):'io:'+addr+':out:'+bit;
      addOutput(id,group+' - '+label,group,2,addr,bit);
    }
    if(caps&65536){
      for(const e of entries.filter(readable))addInput('uni:'+addr+':entity:'+e.id,group+' - '+entityName(e),group,3,addr,Number(e.id),['main_input','input'].includes(e.meta.role));
      for(const e of entries.filter(writable))addOutput('uni:'+addr+':'+e.type+':'+e.id,group+' - '+entityName(e),group,3,addr,Number(e.id));
    }
  }
  const automation=ofeAutomationSignals({...d,modules:mods},lang,ofeSignalModuleLabel);
  inputs.push(...automation.inputs);outputs.push(...automation.outputs);
  return {inputs,outputs};
}
function ofeSignalEscape(value){return String(value??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function ofeRoutingOptions(items,side,current){
  const groups=new Map();for(const item of items){const group=groups.get(item.group||'')||[];group.push(item);groups.set(item.group||'',group)}
  let html='';for(const [group,entries] of groups){if(group)html+='<optgroup label="'+ofeSignalEscape(group)+'">';for(const item of entries){const value=side==='source'?[item.source_type,item.source_addr,item.source_bit]:[item.target_type,item.target_addr,item.target_bit];html+='<option value="'+value.join(',')+'"'+(value.join(',')===current?' selected':'')+'>'+ofeSignalEscape(item.name)+'</option>'}if(group)html+='</optgroup>'}return html;
}
</script>)HTML";
