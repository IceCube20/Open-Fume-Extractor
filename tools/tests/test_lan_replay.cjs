const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync(path.join(__dirname,'../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/WebStatus.inc.h'),'utf8');
let now=1000,frames=0;const nodes={};
function node(id){if(!nodes[id]){const classes=new Set();nodes[id]={style:{},setAttribute(){},getBoundingClientRect:()=>({left:0,top:0}),classList:{toggle(k,on){if(on)classes.add(k);else classes.delete(k)},remove:k=>classes.delete(k),contains:k=>classes.has(k)}};}return nodes[id];}
const context={document:{getElementById:node},window:{devicePixelRatio:1},uiLang:'de',performance:{now:()=>now},requestAnimationFrame:()=>++frames,cancelAnimationFrame(){},set(){}};
vm.createContext(context);
vm.runInContext(['lanActivityAt','lanAlignLeds','lanAnimate','lanReceive','lanSet'].map(name=>source.match(new RegExp('^function '+name+'\\(.*$','m'))[0]).join('\n'),context);
const active=()=>node('lan_activity').classList.contains('is-pulse');
const lan={ethernet_link:true,ethernet_activity_available:true,ethernet_activity_history:'0000000800000000'};
context.lanReceive(lan,1000,1000);assert(active());const frame=context.lanReceive.frame;
now=1010;for(let n=0;n<9;n++)context.lanSet({...lan,ethernet_activity_history:'0000000000000000'});
assert.equal(context.lanReceive.at,1000);assert.equal(context.lanReceive.frame,frame);assert(active());
now=1100;context.lanAnimate();assert(!active());
now=1200;context.lanReceive({...lan,ethernet_activity_history:'0000000100000000'},1100,1200);context.lanAnimate();assert(active());
now=1250;context.lanAnimate();assert(!active());
for(let n=0;n<20;n++){now+=200;context.lanReceive({...lan,ethernet_activity_history:'ffffffffffffffff'},now,now);context.lanAnimate();assert(active());}
assert.equal(context.lanReceive.samples.length,8);
now+=4001;context.lanAnimate();assert(!active());assert.equal(context.lanReceive.frame,0);
context.lanReceive({...lan,ethernet_activity_history:'ffffffffffffffff'},now,now);assert(active());
context.lanReceive({...lan,ethernet_activity_available:false},now,now);assert(!active());assert.equal(context.lanReceive.samples.length,0);
context.lanReceive({...lan,ethernet_activity_history:'ffffffffffffffff'},now,now);context.lanSet({ethernet_link:false});assert(!active());
assert(!context.lanActivityAt('bogus',100));assert(!context.lanActivityAt('ffffffffffffffff',1280));assert(!context.lanActivityAt('ffffffffffffffff',-1));
console.log('PASS: actual LAN replay under nine-module status refreshes, RTT, burst/continuous traffic, bounded history, stale/malformed/unavailable/disconnected data');
