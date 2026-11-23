const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const root=path.resolve(__dirname,'../..'),src=path.join(root,'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src');
const header=fs.readFileSync(path.join(src,'HostedC6Firmware.h'),'utf8');
const boardBranches=header.match(/#if defined\(OFE_MASTER_P4_WAVESHARE_POE\)\r?\n([\s\S]*?)#else\r?\n([\s\S]*?)#endif/);
const constant=name=>(name==='BOARD'&&boardBranches?boardBranches[2]:header).match(new RegExp(`char ${name}\\[\\]="([^"]+)"`))[1];
const cfg=Object.freeze({expected:parseInt(header.match(/VERSION=0x([\da-f]+)/i)[1],16),size:Number(header.match(/IMAGE_SIZE=(\d+)/)[1]),board:constant('BOARD'),arduino:constant('ARDUINO_VERSION_TEXT'),idf:constant('IDF_VERSION_TEXT'),sha256:constant('SHA256_TEXT'),url:constant('URL')});
const catalog=JSON.parse(fs.readFileSync(path.join(root,'updates/hosted-c6.json'),'utf8'));
const clone=value=>JSON.parse(JSON.stringify(value));
let response,calls=[];
const context={window:{},TextDecoder,AbortSignal,fetch:async(url,options)=>{calls.push({url,options});if(response instanceof Error)throw response;return response;}};
vm.createContext(context);
vm.runInContext(fs.readFileSync(path.join(src,'HostedC6CatalogUi.h'),'utf8').match(/<script>([\s\S]*?)<\/script>/)[1],context);
const {inspect,load}=context.window.OFE_C6_CATALOG;
const check=data=>inspect(data,cfg,cfg.expected);
assert.equal(check(catalog).kind,'approved');
assert.equal(check(catalog).version,cfg.expected);
assert.equal(check(catalog).newerSdk,false);
assert.equal(inspect(catalog,cfg,0).kind,'none');
if(boardBranches){
 const poeBoard=boardBranches[1].match(/char BOARD\[\]="([^"]+)"/)[1];assert.notEqual(poeBoard,cfg.board);
 const poeCfg={...cfg,board:poeBoard};
 assert.equal(inspect(catalog,poeCfg,cfg.expected).kind,'approved');
 assert.equal(inspect(catalog,poeCfg,0).kind,'none');
 const withoutPoe=clone(catalog);withoutPoe.releases=withoutPoe.releases.filter(r=>r.board!==poeBoard);
 assert.equal(inspect(withoutPoe,poeCfg,cfg.expected).kind,'none');
 const withdrawn=clone(catalog);withdrawn.releases.find(r=>r.board===poeBoard).status='withdrawn';
 assert.equal(inspect(withdrawn,poeCfg,cfg.expected).kind,'none');
 assert.equal(check(withdrawn).kind,'approved');
 const changed=clone(catalog);changed.releases.find(r=>r.board===poeBoard).coprocessor.sha256='0'.repeat(64);
 assert.equal(inspect(changed,poeCfg,cfg.expected).kind,'master_update');
 assert.equal(inspect(catalog,{...poeCfg,arduino:'3.3.11'},cfg.expected).kind,'none');
}
const entry=catalog.releases[0];
assert.equal(entry.coprocessor.version,constant('VERSION_TEXT'));
const bytes=header.split('SHA256[32]=')[1].split(';')[0].match(/0x[\da-f]{2}/gi).map(x=>parseInt(x,16));
assert.equal(Buffer.from(bytes).toString('hex'),cfg.sha256);
for(const mutate of [
 data=>{data.releases[0].coprocessor.sha256='0'.repeat(64)},
 data=>{data.releases[0].coprocessor.size++},
]){const data=clone(catalog);mutate(data);assert.equal(check(data).kind,'master_update');}
for(const mutate of [
 data=>{data.releases[0].board='another-board'},
 data=>{data.releases[0].host.arduino_esp32='3.3.11'},
 data=>{data.releases[0].host.esp_idf='5.5.4'},
 data=>{data.releases[0].status='withdrawn'},
]){const data=clone(catalog);mutate(data);assert.equal(check(data).kind,'none');}
for(const mutate of [
 data=>{data.schema=2},data=>{data.releases=null},data=>{data.releases=Array(33).fill(entry)},
 data=>{data.releases.push(clone(entry))},
 data=>{const other=clone(entry);other.id='duplicate-combination';data.releases.push(other)},
 data=>{data.releases[0].status='beta'},
 data=>{data.releases[0].transport='uart'},
 data=>{data.releases[0].coprocessor.chip='esp32s3'},
 data=>{data.releases[0].coprocessor.size='1226976'},
 data=>{data.releases[0].coprocessor.size=2097153},
 data=>{data.releases[0].coprocessor.sha256='not-a-hash'},
 data=>{data.releases[0].coprocessor.url='https://example.invalid/payload.bin'},
 data=>{data.releases[0].host.esp_hosted='2.12.14'},
 data=>{data.releases[0].host.esp_idf='5.999.0'},
 data=>{data.releases[0].host.arduino_esp32='3.3'},
]){const data=clone(catalog);mutate(data);assert.throws(()=>check(data),/CATALOG_INVALID/);}
const future=clone(entry);future.id='future-test-only';future.host.esp_hosted='2.13.0';future.host.arduino_esp32='3.4.0';future.host.esp_idf='5.6.0';future.coprocessor.version='2.13.0';future.coprocessor.url='https://espressif.github.io/arduino-esp32/hosted/esp32c6-v2.13.0.bin';
const futureCatalog=clone(catalog);futureCatalog.releases.push(future);
assert.equal(check(futureCatalog).kind,'approved');assert.equal(check(futureCatalog).newerSdk,true);
assert.equal(check({schema:1,releases:[future]}).kind,'none');assert.equal(check({schema:1,releases:[future]}).newerSdk,true);
assert.equal(check({schema:1,releases:[]}).kind,'none');
assert.equal(cfg.url,constant('URL'));assert.equal(cfg.sha256,constant('SHA256_TEXT'));
(async()=>{
 const url=constant('CATALOG_URL');assert.equal(new URL(url).hostname,'raw.githubusercontent.com');
 response=new Response(JSON.stringify(catalog));assert.equal((await load(url)).schema,1);
 assert.equal(calls[0].options.credentials,'omit');assert.equal(calls[0].options.referrerPolicy,'no-referrer');assert.equal(calls[0].options.cache,'no-store');assert(!calls[0].options.headers);
 response=new Response('missing',{status:404});await assert.rejects(()=>load(url),/HTTP 404/);
 response=new Response('x'.repeat(32769));await assert.rejects(()=>load(url),/CATALOG_INVALID/);
 response=new Response('{broken');await assert.rejects(()=>load(url));
 response=new Error('offline');await assert.rejects(()=>load(url),/offline/);
 console.log('PASS: actual C6 catalog selector, SDK/board/image pins, withdrawals, future SDKs, malformed/oversized/offline replies; no firmware configuration mutation');
})().catch(error=>{console.error(error);process.exitCode=1});
