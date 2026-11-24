#include <cassert>
#include <cstring>
#include <cstdio>
#include "FanIoHardwareConfig.h"
using namespace ofe_fanio;
int main(){
  for(bool pro:{false,true}){
    HardwareConfig cfg;defaults(cfg,pro);assert(validate(cfg,pro));assert(cfg.leds.pin==PIN_UNUSED);
    HardwareConfigV3 old{};memcpy(&old,&cfg,offsetof(HardwareConfigV3,checksum));old.version=3;old.size=sizeof(old);
    copyName(old.inputs[0].name,"Workbench");old.checksum=checksumBytes(&old,offsetof(HardwareConfigV3,checksum));
    Preferences prefs;prefs.begin(pro?"pro":"fan",false);prefs.putBytes("hwcfg",&old,sizeof(old));
    HardwareConfig migrated;assert(load(prefs,migrated,pro));assert(!strcmp(migrated.inputs[0].name,"Workbench"));
    assert(migrated.leds.pin==PIN_UNUSED);assert(validate(migrated,pro));
    cfg=migrated;cfg.leds.pin=4;cfg.checksum=checksum(cfg);assert(!validate(cfg,pro));
    cfg.leds.pin=cfg.outputs[0].pin;cfg.checksum=checksum(cfg);assert(!validate(cfg,pro));
    uint8_t freePin=PIN_UNUSED;
    for(uint8_t pin=0;pin<=33;++pin){cfg.leds.pin=pin;cfg.checksum=checksum(cfg);if(validate(cfg,pro)){freePin=pin;break;}}
    assert(freePin!=PIN_UNUSED);
    auto& last=cfg.leds.channels[31];last.enabled=1;last.first=0;last.count=128;cfg.leds.count=128;cfg.checksum=checksum(cfg);
    assert(validate(cfg,pro));assert(ledEnabledMask(cfg.leds)==0x80000000UL);
    last.count=129;cfg.checksum=checksum(cfg);assert(!validate(cfg,pro));last.count=128;
    last.effect=1;last.period_ms=1000;assert(ledLevel(last,0)==255);assert(ledLevel(last,500)==0);
    last.effect=2;assert(ledLevel(last,0)==0);assert(ledLevel(last,500)==255);assert(ledLevel(last,1000)==0);
    last.period_ms=99;assert(!ledValidate(cfg.leds));last.period_ms=1000;
    cfg.checksum=checksum(cfg);assert(save(prefs,cfg,pro));assert(load(prefs,migrated,pro));assert(migrated.leds.channels[31].enabled);
    old.checksum^=1;prefs.putBytes("hwcfg",&old,sizeof(old));assert(!load(prefs,migrated,pro));assert(migrated.leds.pin==PIN_UNUSED);
  }
  puts("PASS: original hardware config, V3 migration, corrupt storage, pin reservations/collisions, LED31 mask, range/effects and persistence");
}
