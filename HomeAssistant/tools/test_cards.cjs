/* Real OFE bundle with minimal HA host stubs; not a full Home Assistant test. */
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const {chromium} = require('playwright');
const root = path.join(__dirname, '..');
const component = path.join(root, 'custom_components/open_fume_extractor');
const settle = page => page.waitForFunction(() => document.querySelector('ofe-overview-card')?._panels.length === 8);

function fixture(lang) {
  const states = {};
  function entity(uid, type, key, state, name, group = 'status', extra = {}, domain = 'sensor') {
    const entity_id = `${domain}.${uid}_${key}`;
    states[entity_id] = {entity_id, state: String(state), attributes: {
      ofe_master_uid: 'master', ofe_uid: uid, ofe_active: true, ofe_key: key,
      ofe_device_type: type, ofe_device_name: uid === 'master' ? 'Open Fume Extractor Master' :
        `0x${{usb:'11',fae:'10',fan:'20',weller:'30',display:'40',universal:'50',modbus:'60'}[uid]} ${
          {usb:'JBC USB CLMU',fae:'JBC FAE Bus DDE',fan:'Fan/IO Pro Testbank mit langem Namen',weller:'Weller Zero Smog',
            display:'Display P4 800x480',universal:'Universal RS232 Bridge',modbus:'Modbus RTU Bridge'}[uid]}`,
      ofe_device_model: {0:'Open Fume Extractor',1:'JBC FAE Bus',9:'JBC USB',3:'Fan/IO Pro',5:'Weller Zero Smog',
        6:'Display P4 800x480',7:'Universal RS232 Bridge',8:'Modbus RTU Bridge'}[type],
      ofe_transport: uid === 'display' || uid === 'master' ? 'wifi' : 'rs485',
      ofe_name: name, friendly_name: name, ofe_group: group, icon:'mdi:gauge', ...extra,
    }};
  }
  const label = (de,en) => lang === 'de' ? de : en;
  const definitions = [['master',0],['fae',1],['usb',9],['fan',3],['weller',5],['display',6],['universal',7],['modbus',8]];
  for (const [uid,type] of definitions) {
    entity(uid,type,'online',uid === 'weller' ? 'off' : 'on',label('Verbindung','Connection'),'diagnostics',{},'binary_sensor');
    entity(uid,type,'eco','on',label('Energiesparen','Power saving'),'status',{icon:'mdi:sprout'},'binary_sensor');
    for (const channel of ['ofe','evt']) entity(uid,type,'led_'+channel,'on',channel.toUpperCase(),'diagnostics',
      {ofe_led_enabled:true,ofe_led_effect:channel === 'ofe' ? 'breath' : 'blink',ofe_led_color:'#39c779',ofe_led_period_ms:2400});
    entity(uid,type,'cpu',18,'CPU','diagnostics',{unit_of_measurement:'%',icon:'mdi:chip'});
    entity(uid,type,'firmware',uid === 'master' ? '2.0.87RC' : '1.0.13RC','Firmware','diagnostics');
    entity(uid,type,'power','off',label('Ausgang','Output'),'controls',{icon:'mdi:fan'},'switch');
    entity(uid,type,'setpoint',42,label('Leistung','Power'),'controls',{unit_of_measurement:'%',min:0,max:100},'number');
    entity(uid,type,'mode',label('Automatisch','Automatic'),label('Modus','Mode'),'controls',
      {options:[label('Automatisch','Automatic'),label('Manuell','Manual')]},'select');
  }
  entity('master',0,'output_power',42,label('Leistung','Power'),'status',{unit_of_measurement:'%',icon:'mdi:fan'});
  entity('master',0,'feedback_rpm',2460,label('Drehzahl','Fan speed'),'status',{unit_of_measurement:'rpm',icon:'mdi:fan'});
  entity('master',0,'afterrun',2,label('Nachlauf','After-run'),'status',{unit_of_measurement:'s',icon:'mdi:timer-outline'});
  entity('master',0,'alarm_count',2,label('Alarme','Alarms'),'status',
    {icon:'mdi:bell-alert-outline',alarms:['JBC JTSE: Station getrennt','Weller Zero Smog: Filter bald wechseln']});
  for (const [uid,type] of [['fae',1],['usb',9]]) {
    entity(uid,type,'station_type','CLM_UR3_01',label('Station','Station'),'status',{icon:'mdi:soldering-iron'});
    entity(uid,type,'jbc_usb_port_count',2,'Ports');
    entity(uid,type,'jbc_work_mask','on','Work','status',{icon:'mdi:soldering-iron'},'binary_sensor');
    entity(uid,type,'jbc_stand_mask','off','Stand','status',{icon:'mdi:car-brake-parking'},'binary_sensor');
  }
  for (const [uid,type] of [['fan',3],['weller',5],['universal',7],['modbus',8]]) {
    entity(uid,type,'main_power',40,label('Leistung','Power'),'status',{unit_of_measurement:'%',icon:'mdi:fan'});
    entity(uid,type,'rpm',uid === 'weller' ? 'unavailable' : 2480,label('Drehzahl','Fan speed'),'status',{unit_of_measurement:'rpm',icon:'mdi:fan'});
    entity(uid,type,'filter_remaining','9d 10h 2m',label('Filter-Restlaufzeit','Filter remaining'),'filter',{icon:'mdi:timer-sand'});
    entity(uid,type,'filter_runtime','1d 2h 18m',label('Filterbetriebszeit','Filter runtime'),'filter',{icon:'mdi:air-filter'});
  }
  for (const [key,value,name] of [['brightness',80,label('Helligkeit','Brightness')],['display_language',lang === 'de' ? 'Deutsch' : 'English',label('Sprache','Language')],
    ['display_theme','Dark',label('Farbschema','Theme')],['display_screensaver_min',10,label('Bildschirmschoner','Screensaver')]]) {
    entity('display',6,key,value,name,'status',{icon:'mdi:monitor'});
  }
  return states;
}

async function host(page, lang, dark, icons) {
  await page.route('**/open_fume_extractor/logo.png', route => route.fulfill({path:path.join(component,'brand/logo.png'),contentType:'image/png'}));
  await page.setContent(`<base href="http://ofe.test/"><style>
    :root{--primary-text-color:${dark?'#edf0f4':'#20252c'};--secondary-text-color:${dark?'#a4acb7':'#606c79'};
      --ha-card-background:${dark?'#1c1f23':'#fff'};--card-background-color:var(--ha-card-background);
      --secondary-background-color:${dark?'#292d33':'#eff2f5'};--divider-color:${dark?'#353a42':'#e0e5ea'};
      --ha-card-border-color:var(--divider-color);--primary-color:#2586c6;--success-color:${dark?'#65d59b':'#19734a'};
      --error-color:${dark?'#f28a84':'#b92f35'};--paper-font-body1_-_font-family:Arial,sans-serif}
    *{box-sizing:border-box}body{margin:0;padding:20px;background:${dark?'#101214':'#f5f6f8'};font:14px Arial,sans-serif;color:var(--primary-text-color)}
    #single{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(360px,100%),1fr));gap:16px;margin-bottom:24px}
    #overview{max-width:1440px;margin:auto}@media(max-width:400px){body{padding:12px}}
  </style><div id="single"></div><div id="overview"></div>`);
  await page.evaluate(({icons}) => {
    class Icon extends HTMLElement {
      static get observedAttributes(){return ['icon'];}
      constructor(){super();this.attachShadow({mode:'open'});}
      attributeChangedCallback(){
        const key='mdi'+(this.getAttribute('icon')||'mdi:gauge').split(':').pop().split('-').map(s=>s[0].toUpperCase()+s.slice(1)).join('');
        this.shadowRoot.innerHTML=`<style>:host{display:inline-flex;width:var(--mdc-icon-size,24px);height:var(--mdc-icon-size,24px)}svg{width:100%;height:100%;fill:currentColor}</style><svg viewBox="0 0 24 24"><path d="${icons[key]||icons.mdiGauge}"></path></svg>`;
      }
    }
    customElements.define('ha-icon',Icon);
    class NativeEntities extends HTMLElement {
      constructor(){super();this.attachShadow({mode:'open'});}
      setConfig(config){
        this._config=config; this.shadowRoot.innerHTML=`<style>:host{display:block}ha-card{display:block}#states{padding:12px 16px}.section{color:var(--secondary-text-color);font-size:12px;margin:14px 0 4px;padding-top:10px;border-top:1px solid var(--divider-color)}.row{display:flex;align-items:center;gap:10px;min-height:48px}.name{flex:1;min-width:0;overflow-wrap:anywhere}input,select{font:inherit;color:inherit;max-width:140px;min-width:0;min-height:36px;background:var(--secondary-background-color);border:1px solid var(--divider-color);border-radius:4px;padding:6px}input[type=number]{width:70px}output{max-width:48%;overflow-wrap:anywhere;text-align:right;font-variant-numeric:tabular-nums}ha-icon{--mdc-icon-size:20px;color:var(--secondary-text-color);flex-shrink:0}</style><ha-card><div id="states"></div></ha-card>`;
        const body=this.shadowRoot.querySelector('#states');
        for(const item of config.entities){
          const row=document.createElement('div');
          if(item.type==='section'){row.className='section';row.textContent=item.label;body.append(row);continue;}
          row.className='row';row.dataset.entity=item.entity;
          const icon=document.createElement('ha-icon');icon.setAttribute('icon','mdi:gauge');
          const name=document.createElement('span');name.className='name';name.textContent=item.name;
          const domain=item.entity.split('.')[0], control=document.createElement(domain==='select'?'select':['number','switch'].includes(domain)?'input':'output');
          if(domain==='number')control.type='number';if(domain==='switch')control.type='checkbox';
          control.setAttribute('aria-label',item.name);row.append(icon,name,control);body.append(row);
        }
      }
      set hass(hass){
        this._hass=hass;
        for(const row of this.shadowRoot.querySelectorAll('.row')){
          const state=hass.states[row.dataset.entity];if(!state)continue;
          row.querySelector('ha-icon').setAttribute('icon',state.attributes.icon||'mdi:gauge');
          const control=row.lastElementChild;if(this.shadowRoot.activeElement===control)continue;
          if(control.localName==='select'){control.replaceChildren(...(state.attributes.options||[]).map(value=>{const option=document.createElement('option');option.value=value;option.textContent=value;return option;}));control.value=state.state;}
          else if(control.type==='checkbox')control.checked=state.state==='on';
          else if(control.localName==='input')control.value=state.state;
          else control.textContent=state.state+(state.attributes.unit_of_measurement?' '+state.attributes.unit_of_measurement:'');
        }
      }
    }
    customElements.define('test-entities-card',NativeEntities);
    window.loadCardHelpers=async()=>({createCardElement(config){const card=document.createElement('test-entities-card');card.setConfig(config);return card;}});
  },{icons});
  await page.addScriptTag({path:path.join(component,'frontend/ofe-cards.js')});
  await page.evaluate(({states,lang}) => {
    window.hass={states,locale:{language:lang},formatEntityState(state){return ['on','off'].includes(state.state)?({de:{on:'Ein',off:'Aus'},en:{on:'On',off:'Off'}}[lang][state.state]):state.state+(state.attributes.unit_of_measurement?' '+state.attributes.unit_of_measurement:'');}};
    for(const [type,extra] of [['master',{}],['module',{module_uid:'fan'}],['alarms',{entity:'sensor.master_alarm_count'}]]){
      const card=document.createElement(`ofe-${type}-card`);card.setConfig({type:`custom:ofe-${type}-card`,...extra});card.hass=window.hass;document.querySelector('#single').append(card);
    }
    const overview=document.createElement('ofe-overview-card');overview.setConfig({type:'custom:ofe-overview-card'});overview.hass=window.hass;document.querySelector('#overview').append(overview);
  },{states:fixture(lang),lang});
  await settle(page);
}

(async()=>{
  const mdiPath=process.env.OFE_MDI_MODULE;
  if(!mdiPath)throw new Error('Set OFE_MDI_MODULE to @mdi/js mdi.js (ES module) for visual tests.');
  const mdi=await import('data:text/javascript;base64,'+fs.readFileSync(path.resolve(mdiPath)).toString('base64'));
  const names=['Gauge','Fan','TimerOutline','TimerSand','BellAlertOutline','CheckCircleOutline','HelpCircleOutline','Sprout','SerialPort','UsbPort','ElectricSwitch','Monitor','Chip','LanConnect','LanDisconnect','Wifi','SolderingIron','CarBrakeParking','AirFilter'];
  const icons=Object.fromEntries(names.map(name=>['mdi'+name,mdi['mdi'+name]]));
  const browser=await chromium.launch({headless:true,executablePath:process.env.OFE_BROWSER_EXECUTABLE});
  const output=path.join(root,'tests/artifacts/cards');fs.mkdirSync(output,{recursive:true});
  try{
    for(const lang of ['de','en'])for(const dark of [true,false])for(const width of [320,390,760,1440]){
      const page=await browser.newPage({viewport:{width,height:1000},reducedMotion:'reduce'});
      const errors=[];page.on('pageerror',error=>errors.push(error.message));
      await host(page,lang,dark,icons);
      assert.deepEqual(errors,[]);
      const geometry=await page.evaluate(()=>{
        const issues=[];
        function scan(root){for(const el of root.querySelectorAll('*')){
          if(el.shadowRoot)scan(el.shadowRoot);
          if(el.matches('.device,.device-title,.model,.indicator,.metric,.metric-label,.metric-value,.led-pair,.heading,.alarms,h2,.alarm-list li')){
            const r=el.getBoundingClientRect();if(r.width&& (r.left < -1||r.right>innerWidth+1||el.scrollWidth>el.clientWidth+1))issues.push([el.className||el.localName,r.left,r.right,el.scrollWidth,el.clientWidth]);
          }
        }}scan(document);return {issues,width:document.documentElement.scrollWidth,viewport:innerWidth};
      });
      assert.deepEqual(geometry.issues,[],JSON.stringify({lang,dark,width,geometry}));
      assert(geometry.width<=geometry.viewport+1);
      await page.screenshot({path:path.join(output,`${lang}-${dark?'dark':'light'}-${width}.png`),fullPage:true});
      if(lang==='de'&&dark&&[390,1440].includes(width))await page.screenshot({path:path.join(output,`preview-${width}.png`)});
      assert.equal(await page.locator('ofe-module-card').evaluate(el=>el._panels[0].root.style.getPropertyValue('--ofe-accent')),'#4a90d9');
      await page.evaluate(()=>{
        window.moreInfo=[];document.addEventListener('hass-more-info',event=>window.moreInfo.push(event.detail.entityId));
        const card=document.querySelector('ofe-master-card');window.beforeCard=card._children[0];
        window.input=window.beforeCard.shadowRoot.querySelector('input[type=number]');window.input.focus();window.input.value='73';
        window.hass.states['sensor.master_output_power'].state='65';card.hass=window.hass;
      });
      assert(await page.evaluate(()=>document.querySelector('ofe-master-card')._children[0]===window.beforeCard&&window.beforeCard.shadowRoot.activeElement===window.input&&window.input.value==='73'));
      assert.equal(await page.locator('ofe-master-card .metric-value').first().textContent(),'65 %');
      await page.locator('ofe-master-card .metric').first().click();
      await page.locator('ofe-master-card .led-word').first().click();
      assert.deepEqual(await page.evaluate(()=>window.moreInfo),['sensor.master_output_power','sensor.master_led_ofe']);
      assert.equal(await page.locator('ofe-overview-card .device').last().locator('.led-word').first().getAttribute('data-live'),'false');
      await page.evaluate(()=>{
        const alarm=document.querySelector('ofe-alarms-card');window.hass.states['sensor.master_alarm_count'].attributes.alarms=[];alarm.hass=window.hass;
      });
      assert.equal(await page.locator('ofe-alarms-card ha-card').getAttribute('data-state'),'clear');
      assert.equal(await page.locator('ofe-alarms-card h2 ha-icon').getAttribute('icon'),'mdi:check-circle-outline');
      await page.evaluate(()=>{
        const alarm=document.querySelector('ofe-alarms-card');window.hass.states['sensor.master_alarm_count'].state='unavailable';alarm.hass=window.hass;
      });
      assert.equal(await page.locator('ofe-alarms-card ha-card').getAttribute('data-state'),'unknown');
      if(lang==='de'&&dark&&width===390){
        await page.evaluate(()=>{
          window.hass.states['sensor.master_alarm_count'].state='1';
          window.hass.states['sensor.master_alarm_count'].attributes.alarms=['<img src=x onerror="alert(1)">'];
          document.querySelector('ofe-alarms-card').hass=window.hass;
          document.querySelector('ofe-module-card').setConfig({type:'custom:ofe-module-card',module_uid:'fan',show_summary:false,show_diagnostics:true});
        });
        await page.waitForFunction(()=>document.querySelector('ofe-module-card')._panels[0].metrics.hidden);
        assert.equal(await page.locator('ofe-alarms-card li img').count(),0);
        assert.equal(await page.locator('ofe-alarms-card li').textContent(),'<img src=x onerror="alert(1)">');
        const config=await page.locator('ofe-module-card').evaluate(el=>el._children[0]._config);
        assert(config.entities.some(item=>item.entity==='sensor.fan_cpu'));
        assert(config.entities.some(item=>item.entity==='number.fan_setpoint'));
      }
      await page.close();
    }
    console.log('PASS: actual OFE card bundle in DE/EN, light/dark, 320-1440px, live values, stable native-control focus and alarm states.');
    console.log('Screenshots: '+output);
  }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
