#include "OfeRgbOtaProbe.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
  OfeRgbOtaProbe probe(16000000);
  unsigned calls=0;
  uint32_t last=0;
  auto apply = [&](uint32_t hz) { ++calls; last=hz; return int32_t(0); };
  assert(!probe.enabled() && probe.requestedHz()==16000000);
  assert(!probe.service(0, true, apply).attempted && calls==0);
  assert(probe.select("15") && probe.selectedHz()==15000000);
  assert(!probe.service(1, false, apply).attempted && calls==0);
  assert(probe.service(2, true, apply).attempted && last==15000000 && calls==1);
  assert(probe.requestedHz()==15000000);
  assert(!probe.service(3, true, apply).attempted && calls==1);
  assert(probe.service(4, false, apply).attempted && last==16000000);
  assert(probe.service(5, true, apply).attempted && last==15000000);
  assert(probe.select("14"));
  assert(probe.service(6, true, apply).attempted && last==14000000);
  for (const char* bad : {"6", "low", "8", "10", "12", "16", "24", "14.0", "14x", " 14", "", "-1"}) {
    assert(!probe.select(bad) && probe.selectedHz()==14000000);
  }
  assert(!probe.select(nullptr) && probe.selectedHz()==14000000);
  assert(probe.select("normal"));
  assert(probe.service(7, true, apply).attempted && last==16000000);

  assert(probe.select("15"));
  auto fail = [&](uint32_t hz) { ++calls; last=hz; return int32_t(42); };
  assert(probe.service(100, true, fail).error==42);
  assert(probe.requestedHz()==16000000);
  assert(!probe.service(1099, true, fail).attempted);
  assert(probe.service(1100, true, apply).attempted);
  assert(probe.requestedHz()==15000000);
  assert(probe.service(1101, false, fail).error==42);
  assert(probe.requestedHz()==15000000);
  assert(!probe.service(2100, false, apply).attempted);
  assert(probe.service(2101, false, apply).attempted);
  assert(probe.requestedHz()==16000000);

  OfeRgbOtaProbe wrap(16000000);
  assert(wrap.select("13"));
  assert(wrap.service(UINT32_MAX-99, true, fail).attempted);
  assert(!wrap.service(899, true, apply).attempted);
  assert(wrap.service(900, true, apply).attempted);
  assert(wrap.requestedHz()==13000000);
  assert(wrap.service(901, false, apply).attempted);
  OfeRgbOtaProbe reboot(16000000);
  assert(!reboot.enabled() && reboot.requestedHz()==16000000);
  assert(!reboot.service(0, true, apply).attempted);
  OfeRgbOtaProbe slower_panel(14000000);
  assert(!slower_panel.select("15") && !slower_panel.select("14"));
  assert(slower_panel.select("13"));
  assert(slower_panel.select("normal") && !slower_panel.enabled());
  assert(sizeof(probe)<=48);
  std::puts("PASS: bounded 15/14/13 MHz OTA probe, live switching, low-clock rejection, no default writes, restore, errors, retries, rollover and reboot reset");
}
