const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const {spawnSync} = require('node:child_process');
const {chromium} = require('playwright');

function parseCsv(text) {
  const result = spawnSync(process.env.OFE_TEST_PYTHON || 'python', ['-X','utf8','-c',
    'import csv,io,json,sys; print(json.dumps(list(csv.reader(io.StringIO(sys.stdin.read().lstrip(chr(65279))), delimiter=";"))))'],
    {input:text,encoding:'utf8'});
  assert.equal(result.status,0,result.stderr);
  return JSON.parse(result.stdout);
}

const source = fs.readFileSync(path.join(__dirname, '../../OpenFumeExtractorMaster/src/WebDiagnostics.inc.h'), 'utf8');
const script = source.split('html += R"JS(')[1].split(')JS";')[0].split('setInterval(loadDiag,500);')[0];
const event = (seq, addr, dir, data, status = 255) => ({seq, addr, dir, data, status,
  ms: 1000 + seq * 10, latency: 0, frame_seq: 255, cmd: 'GET_IO', len: data.length / 2, text: 'raw firmware text'});
const fae = [event(1, 16, 'UART_RX', '0012173400', 23), event(2, 16, 'UART_TX', '12001734029001', 23)];
const usb = [event(3, 17, 'UART_TX', '0100173000', 2), event(4, 17, 'UART_RX', '000117300100', 2)];
const weller = [event(5, 48, 'UART_TX', '53'), event(6, 48, 'UART_RX', '53303330E6')];
function withCrc(bytes) {
  let crc = 65535;
  for (const b of bytes) {crc ^= b; for (let i = 0; i < 8; i++) crc = crc & 1 ? (crc >> 1) ^ 0xA001 : crc >> 1;}
  return [...bytes, crc & 255, crc >> 8].map(b => b.toString(16).padStart(2, '0')).join('');
}
const modbus = [event(7, 96, 'UART_TX', withCrc([1, 3, 0, 0, 0, 1])),
  event(8, 96, 'UART_RX', withCrc([1, 3, 2, 0, 42]))];
const universal = [event(9, 80, 'UART_RX', '53303330')];
const modules = [{addr:16,type:1}, {addr:17,type:9,jbc_local:{family:'FE'}}, {addr:48},
  {addr:80,local_trace_defs:[{id:1,key:'speed',label:'Speed',match_hex:'53232323',type:'sensor',unit:'%'},
    {id:2,key:'scaled',label:'Scaled',match_hex:'53232323',type:'sensor',scale:2}]},
  {addr:96,local_slave:1,local_trace_defs:[{id:1,key:'rpm',label:'RPM',reg:0,mode:'ro',read_func:'holding',type:'uint16'}]}];

(async () => {
  const browser = await chromium.launch({headless:true,
    executablePath:process.env.OFE_BROWSER_EXECUTABLE});
  try {
    for (const de of [true, false]) {
      const page = await browser.newPage();
      await page.setContent('<select id="diag_view"><option value="local">Local</option></select><input id="diag_addr" value="0x10">');
      await page.evaluate(code => (0,eval)(code), `const UI_DE=${de};\n${script}\n` + `
        window.setTrace=(data,filtered)=>{lastDiag=data;diagFilteredEvents=filtered||data.events;};
        window.exportTrace=()=>diagBuildExport();
        window.makeCsv=exported=>diagCsvExport(exported);
        window.setExportLayout=layout=>{diagLocalLayout=layout;diagTraceLayout=layout;};
        window.decodeEvent=e=>diagExportEvent(e);
        window.decodeFields=html=>diagExportFields(html);
        URL.createObjectURL=blob=>{window.exportedBlob=blob;return 'blob:test';};
        URL.revokeObjectURL=()=>{};
        HTMLAnchorElement.prototype.click=function(){window.exportFilename=this.download;};
        window.downloadTrace=async fmt=>{diagDownload(fmt);return {name:window.exportFilename,text:await window.exportedBlob.text(),prefix:Array.from(new Uint8Array(await window.exportedBlob.arrayBuffer()).slice(0,3))};};
      `);
      const events = [...fae, ...usb, ...weller, ...modbus, ...universal];
      const data = {view:'local',target_addr:0,modules,events};
      await page.evaluate(data => window.setTrace(data), data);
      const exported = await page.evaluate(() => window.exportTrace());
      assert.equal(exported.format_version, 2);
      assert.equal(exported.language, de ? 'de' : 'en');
      assert.equal(exported.events.length, events.length);
      assert.equal(exported.stats.events, undefined, 'Do not duplicate undecoded events in stats');
      assert.equal(exported.transactions.length, 5, 'Do not pair the same FID across modules');
      const f = exported.events.find(e => e.seq === 2);
      assert.equal(f.command_name, 'M_R_SELECTFLOW', 'Decode local CTRL, not the OFE GET_IO alias');
      assert.equal(f.role, 'response', 'FAE bridge TX is a response');
      assert.match(f.decoded_semantic, /400 raw.*40 %/);
      assert.equal(f.jbc.src, 18);
      assert.equal(f.jbc.length_valid, true);
      assert.equal(exported.events.find(e => e.seq === 3).role, 'request', 'USB TX is a request');
      assert(exported.events.find(e => e.seq === 4).decoded_fields.length);
      const w = exported.events.find(e => e.seq === 6);
      assert.match(w.decoded_semantic, /30 %/);
      assert.equal(w.weller.checksum_valid, true);
      const m = exported.events.find(e => e.seq === 8);
      assert.equal(m.modbus.crc_valid, true);
      assert.equal(m.modbus.entities[0].raw, 42);
      assert.match(m.decoded_semantic, /RPM.*42/);
      const universalResult = exported.events.find(e => e.seq === 9);
      assert.equal(universalResult.universal.entities.length, 2);
      assert.match(universalResult.decoded_semantic, /Speed:.*30 %.*Scaled:.*60/);
      for (const input of events) {
        const result = exported.events.find(e => e.seq === input.seq);
        assert.equal(result.data, input.data);
        assert.equal(result.text, input.text);
        assert.doesNotMatch(result.decoded_semantic, /<span|&gt;|&amp;/);
      }
      const pair = exported.transactions.find(t => t.addr === 16);
      assert.equal(pair.request_seq, 1);
      assert.equal(pair.response_seq, 2);
      assert.match(pair.decoded_semantic, /40 %/);
      const json = await page.evaluate(() => window.downloadTrace('json'));
      assert.equal(JSON.parse(json.text).transactions.length, 5);
      assert.equal(exported.layout,'transactions');
      const pairedDownload=await page.evaluate(()=>window.downloadTrace('csv'));
      const pairedCsv=parseCsv(pairedDownload.text);
      assert.equal(pairedCsv.length,6,'One row per transaction, not per raw frame');
      assert.equal(pairedCsv[0][5],de?'Ergebnis':'Result');
      assert.equal(pairedCsv[1][4],'M_R_SELECTFLOW');
      assert.match(pairedCsv[1][5],/40 %/);
      assert.equal(pairedCsv[1][6],de?'Anfrage + Antwort':'Request + response');
      assert.deepEqual(pairedCsv[1].slice(7,11),['5','7','1','2']);
      assert(pairedCsv.every(row=>row.length===pairedCsv[0].length));
      // Browser buffers are newest first; export must be oldest first with a
      // deterministic sequence tie-breaker when multiple frames arrive together.
      await page.evaluate(data=>window.setTrace({...data,events:data.events.slice().reverse()}),data);
      assert.equal((await page.evaluate(()=>window.downloadTrace('csv'))).text,pairedDownload.text);
      await page.evaluate(()=>window.setExportLayout('raw'));
      const csv = await page.evaluate(() => window.downloadTrace('csv'));
      assert.deepEqual(csv.prefix,[239,187,191], 'Excel UTF-8 BOM');
      const table = parseCsv(csv.text), head = table[0];
      assert.equal(head[0],'event_no');
      assert(head.every(Boolean), 'Every column has a heading');
      assert.equal(new Set(head).size,head.length,'Unique headings');
      assert(table.every(row=>row.length===head.length),'Same number of cells on every row');
      const record = Object.fromEntries(head.map((label,i)=>[label,table[2][i]]));
      assert.equal(record.command_hex,'0x34');
      assert.equal(record.source_addr,'0x12');
      assert.equal(record.destination_addr,'0x00');
      assert.equal(record.frame_id,'0x17');
      assert.equal(record.frame_age_ms,'0');
      assert.equal(record.declared_payload_len,'2','Use the exported declared_len, not the decoder-only declared property');
      assert.equal(record['field:'+(de?'Flow-Auswahl':'Flow select')],'400 raw · 40 %');
      assert.equal(record.raw_hex,'0x12 0x00 0x17 0x34 0x02 0x90 0x01');
      assert.equal(record.payload_hex,'0x90 0x01');
      const specialCsv=await page.evaluate(()=>window.makeCsv({view:'local',events:[{
        seq:0,ms:0,addr:16,dir:'UART_RX',protocol:'RAW',len:1,latency:0,data:'00',
        decoded_semantic:'Text mit Ü; "Zitat"\nzweite Zeile',
        decoded_fields:[{label:'A;B',value:'Wert; "Zitat"\nzweite Zeile'}]
      }]}));
      const special=parseCsv(specialCsv), specialRow=Object.fromEntries(special[0].map((key,i)=>[key,special[1][i]]));
      assert.equal(special.length,2,'Quoted newlines must not create extra records');
      assert.equal(specialRow['field:A;B'],'Wert; "Zitat"\nzweite Zeile');
      assert.equal(specialRow.raw_hex,'0x00','A single zero byte must remain hexadecimal text');
      assert.equal(specialRow.trace_seq,'0');
      assert(!head.includes('age_or_latency_ms'));
      assert.match(csv.text, /M_R_SELECTFLOW/);
      assert.match(csv.text, /40 %/);
      assert.match(csv.text, /;0;/, 'Keep zero-valued timing fields');
      await page.evaluate(({data,filtered}) => window.setTrace(data,filtered), {data,filtered:[fae[1]]});
      const filtered = await page.evaluate(() => window.exportTrace());
      assert.equal(filtered.events.length, 1);
      assert.equal(filtered.scope.available_events, events.length);
      assert.equal(filtered.scope.exported_events, 1);
      assert.equal(filtered.transactions[0].request_seq, null);
      assert.equal(filtered.transactions[0].unpaired, true);
      await page.evaluate(()=>window.setExportLayout('transactions'));
      const filteredCsv=parseCsv((await page.evaluate(()=>window.downloadTrace('csv'))).text);
      assert.equal(filteredCsv.length,2);
      assert.equal(filteredCsv[1][6],de?'Nur Antwort':'Response only');
      assert.equal(filteredCsv[1][7],'','Do not invent a request or timeout when filtered out');
      const fields = await page.evaluate(() => window.decodeFields('<span class="jbc-value"><span class="jbc-chip-label">A &amp; B</span><span class="jbc-chip-value">&lt;x&gt;; "y"</span></span>'));
      assert.deepEqual(fields, [{label:'A & B',value:'<x>; "y"'}]);
      const malformed = await page.evaluate(e => window.decodeEvent(e), event(20,17,'UART_RX','AA',0xEF));
      assert.equal(malformed.decoded_semantic, 'raw firmware text');
      await page.evaluate(({modules,e})=>window.setTrace({view:'local',modules,events:[e]}),
        {modules,e:event(20,17,'UART_RX','AA',0xEF)});
      const malformedCsv=parseCsv((await page.evaluate(()=>window.downloadTrace('csv'))).text);
      assert.equal(malformedCsv.length,2,'Malformed frames must not be dropped by the transaction export');
      assert.equal(malformedCsv[1][13],'0xAA');
      assert.equal(malformedCsv[1][6],de?'Einzelframe':'Single frame');
      const truncated = await page.evaluate(e => window.decodeEvent(e), event(21,16,'UART_TX','12001734049001'));
      assert.equal(truncated.jbc.truncated, true);
      assert.equal(truncated.jbc.length_valid, false);
      const badWeller = await page.evaluate(e => window.decodeEvent(e), {...weller[1],data:'5330333000'});
      assert.equal(badWeller.weller.checksum_valid, false);
      const badModbus = await page.evaluate(e => window.decodeEvent(e), {...modbus[1],status:0xEE});
      assert.equal(badModbus.modbus.crc_valid, false);
      // FAE identity commands are distinct, including on the USB FE family.
      await page.evaluate(modules=>window.setTrace({view:'local',modules,events:[]}),modules);
      const identityFrame=(addr,dir,ctrl,payload)=>event(30,addr,dir,
        [0x12,0x00,0x10,ctrl,payload.length,...payload].map(b=>b.toString(16).padStart(2,'0')).join(''),2);
      const firmware=Array.from(Buffer.from('02:F2:8886791:0023324:F1_DIS_02','ascii'));
      const deviceId=Array.from(Buffer.from('849158E467A340159646170D6B1595EF','ascii'));
      for(const addr of [16,17]){
        const response=addr===16?'UART_TX':'UART_RX',request=addr===16?'UART_RX':'UART_TX';
        const fw=await page.evaluate(e=>window.decodeEvent(e),identityFrame(addr,response,0x21,firmware));
        assert.match(fw.decoded_semantic,/8886791.*0023324.*F1_DIS_02/);
        assert(fw.decoded_fields.some(f=>f.label===(de?'Modell':'Model')&&f.value==='F1'));
        assert(fw.decoded_fields.some(f=>f.label==='ModelVersion'&&f.value==='2'));
        const hw=await page.evaluate(e=>window.decodeEvent(e),identityFrame(addr,response,0x1C,[0,16,32,48,64,80,96,112]));
        assert.match(hw.payload_format,/8-byte hardware ID/);
        assert.match(hw.decoded_semantic,/00 10 20 30 40 50 60 70/);
        // Even eight printable bytes remain a binary hardware ID.
        const printable=await page.evaluate(e=>window.decodeEvent(e),identityFrame(addr,response,0x1C,[65,66,67,68,69,70,71,72]));
        assert.match(printable.decoded_semantic,/41 42 43 44 45 46 47 48/);
        const id=await page.evaluate(e=>window.decodeEvent(e),identityFrame(addr,response,0x1E,deviceId));
        assert.match(id.decoded_semantic,/849158E467A340159646170D6B1595EF/);
        const write=await page.evaluate(e=>window.decodeEvent(e),identityFrame(addr,request,0x1F,deviceId));
        assert.match(write.decoded_semantic,/Device ID.*849158/);
        const ack=await page.evaluate(e=>window.decodeEvent(e),identityFrame(addr,response,0x1F,[6]));
        assert.match(ack.decoded_semantic,/ACK=0x06/);
        assert.doesNotMatch(ack.decoded_semantic,/Device ID=/);
      }
      // OFE events must not accidentally enter a local protocol decoder.
      await page.evaluate(data => window.setTrace(data), {view:'ofe',events:[{...fae[0],dir:'TX',text:'OFE command'}]});
      const ofe = await page.evaluate(() => window.exportTrace());
      assert.equal(ofe.events[0].decoded_semantic, 'OFE command');
      assert.equal(ofe.events[0].jbc, undefined);
      assert.equal(ofe.transactions.length, 0);
      const ofeCsv = parseCsv((await page.evaluate(()=>window.downloadTrace('csv'))).text);
      assert.equal(ofeCsv[0][0],'event_no');
      assert(ofeCsv.every(row=>row.length===ofeCsv[0].length));
      // Optional replay of a user capture without checking private data into Git.
      for (const capturePath of process.argv.slice(2)) {
        let capture;
        if(capturePath.endsWith('.csv')){
          const table=parseCsv(fs.readFileSync(capturePath,'utf8')),head=table.shift();
          const rows=table.map(row=>Object.fromEntries(head.map((key,i)=>[key,row[i]])));
          const cleaner=rows.some(row=>['CleanerMode','DoorsState'].includes(row.command_name));
          const captureModules=Array.from(new Set(rows.map(row=>Number(row.module_addr||16))))
            .map(addr=>({addr,type:cleaner?9:1,jbc_local:{family:cleaner?'CL':'FE'}}));
          capture={view:'local',stats:{modules:captureModules},events:rows.map(row=>({
            seq:Number(row.trace_seq??row['#']),ms:Number(row.master_received_ms??row.ms),
            addr:Number(row.module_addr||16),dir:'UART_'+(row.direction||row.dir),
            len:Number(row.frame_len_bytes??row.len),latency:Number(row.frame_age_ms??row.age_or_latency_ms),
            data:(row.raw_hex||row.raw||'').replace(/0x/g,'').replace(/\s/g,''),
            text:row.decoded,status:cleaner?2:255,frame_seq:255,cmd:row.command_name
          }))};
        }else capture = JSON.parse(fs.readFileSync(capturePath, 'utf8'));
        await page.evaluate(()=>window.setExportLayout('transactions'));
        await page.evaluate(c => window.setTrace({...c.stats,view:c.view,events:c.events}), capture);
        const replay = await page.evaluate(() => window.exportTrace());
        assert.equal(replay.events.length, capture.events.length);
        assert(replay.events.some(e => e.jbc && e.decoded_fields.length));
        assert(replay.transactions.some(t => t.kind === 'pair'));
        const csvReplay=parseCsv((await page.evaluate(()=>window.downloadTrace('csv'))).text);
        assert.equal(csvReplay.length,replay.transactions.length+1);
        assert(csvReplay.every(row=>row.length===csvReplay[0].length),'All capture columns have matching headers');
        const covered=replay.transactions.flatMap(t=>t.event_seqs);
        assert.equal(covered.length,capture.events.length,'Every raw frame is represented exactly once');
        assert.equal(new Set(covered).size,capture.events.length);
        const times=csvReplay.slice(1).map(row=>Number(row[1]));
        assert(times.every((time,i)=>i===0||time>=times[i-1]),'Chronological transaction rows');
        await page.evaluate(()=>window.setExportLayout('raw'));
        const rawReplay=parseCsv((await page.evaluate(()=>window.downloadTrace('csv'))).text);
        assert.equal(rawReplay.length,capture.events.length+1);
        console.log(`PASS: ${de?'DE':'EN'} capture replay, ${replay.events.length} events / ${replay.transactions.length} transactions`);
      }
      const namedModules=[
        {addr:16,type:1,expected:'jbc-fae'},{addr:17,type:9,expected:'jbc-usb'},
        {addr:32,type:2,expected:'fan-io'},{addr:33,type:3,expected:'fan-io-pro'},
        {addr:48,type:5,expected:'weller'},{addr:64,type:6,type_name:'Display P4 800x480',expected:'display-p4-800x480'},
        {addr:65,type:6,type_name:'Display ST7796 320x480',expected:'display-st7796-320x480'},
        {addr:80,type:7,expected:'universal-rs232'},{addr:96,type:8,expected:'modbus-rtu'},
        {addr:112,type:99,expected:de?'modul':'module'}
      ];
      for(const module of namedModules){
        // A changed selector must not mislabel the already captured data.
        await page.evaluate(module=>{
          window.setTrace({view:'local',target_addr:module.addr,modules:[{...module,name:'../../untrusted alias'}],events:[]});
          document.getElementById('diag_view').innerHTML='<option value="rs485">OFE</option><option value="local">Local</option>';
          document.getElementById('diag_view').value='rs485';
          document.getElementById('diag_addr').value='0x7F';
        },module);
        for(const fmt of ['csv','json']){
          const named=await page.evaluate(fmt=>window.downloadTrace(fmt),fmt);
          const prefix='open-fume-extractor-'+(de?'lokal':'local')+'-'+module.expected+'-0x'+module.addr.toString(16).toUpperCase()+'-';
          assert(named.name.startsWith(prefix),named.name);
          assert(named.name.endsWith('.'+fmt));
          assert(!/[\\/:*?"<>|\s]/.test(named.name),'Safe filename without path/OS-reserved characters');
          assert.match(named.name.slice(prefix.length),/^\d{4}-\d{2}-\d{2}T\d{2}-\d{2}-\d{2}-\d{3}Z\.(csv|json)$/);
        }
      }
      for(const view of ['local','rs485']){
        await page.evaluate(view=>window.setTrace({view,target_addr:0,modules:[],events:[]}),view);
        const all=await page.evaluate(()=>window.downloadTrace('csv'));
        assert(all.name.startsWith('open-fume-extractor-'+(view==='local'?(de?'lokal':'local'):'ofe-bus')+'-'+(de?'alle-module':'all-modules')+'-'));
      }
      await page.evaluate(()=>window.setTrace({view:'rs485',target_addr:16,modules:[{addr:16,type:1}],events:[]}));
      assert((await page.evaluate(()=>window.downloadTrace('json'))).name.startsWith('open-fume-extractor-ofe-bus-jbc-fae-0x10-'));
      await page.close();
    }
    console.log('PASS: DE/EN JSON and CSV downloads, FAE/USB roles, Weller, Universal, Modbus descriptors, filters, raw preservation and malformed frames');
  } finally {await browser.close();}
})().catch(error => {console.error(error);process.exitCode=1;});
