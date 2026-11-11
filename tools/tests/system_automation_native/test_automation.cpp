#include <cassert>
#include <cstdio>
#include <cstring>
#include "../../../OpenFumeExtractorMaster/src/OfeAutomationState.h"
#include "../../../Module/DisplayModule_800x480/src/OfeDisplayBacklight.h"

int main() {
  OfeAutomationRequests routing, logic;
  routing.eco_inhibit = true;
  logic.eco_inhibit = true;
  routing.eco_inhibit = false;
  assert(routing.eco_inhibit || logic.eco_inhibit);
  for (unsigned marker = 1; marker <= 8; ++marker) {
    routing.setMarker(uint8_t(marker), true);
    logic.setMarker(uint8_t(marker), true);
  }
  assert((routing.markers | logic.markers) == 255);
  routing.setMarker(1, false);
  assert((routing.markers | logic.markers) == 255);
  logic.setMarker(1, false);
  assert((routing.markers | logic.markers) == 254);
  routing.setMarker(0, true); routing.setMarker(9, true);
  assert(routing.markers == 254);
  routing.holdDisplay(0, true);
  logic.holdDisplay(0x41, true);
  assert(routing.holdsDisplay(0x40) && routing.holdsDisplay(0x6F));
  routing.holdDisplay(0, false);
  assert(!routing.holdsDisplay(0x40));
  assert(routing.holdsDisplay(0x41) || logic.holdsDisplay(0x41));
  for (unsigned addr = 0; addr <= 255; ++addr) {
    const bool valid = addr == 0 || (addr >= 16 && addr <= 111);
    assert(OfeAutomationRequests::validDisplayAddress(addr) == valid);
    OfeAutomationRequests request;
    request.holdDisplay(uint8_t(addr), true);
    assert(request.holdsDisplay(uint8_t(addr)) == valid);
  }

  OfeDisplayBacklight display;
  assert(display.configure(2, 1));
  display.tick(0, false, false);
  display.tick(60000, false, false);
  assert(display.isOff());
  uint8_t frame[14] = {0xA3, 1, 1, 0, 0, 0xA5, 0, 0xA6};
  display.applyAutomationFrame(frame, sizeof(frame), 60001);
  assert(display.automationHeld(60001));
  assert(!display.takeAutomationWake()); // Initial sequence is only a baseline.
  display.tick(60001, false, false);
  assert(!display.isOff());
  frame[3] = 1;
  display.applyAutomationFrame(frame, sizeof(frame), 60002);
  assert(display.takeAutomationWake());
  display.applyAutomationFrame(frame, sizeof(frame), 60003);
  assert(!display.takeAutomationWake()); // Duplicate status frames never replay an event.
  assert(display.automationHeld(65002));
  assert(!display.automationHeld(65003)); // Link loss expires a hold.
  frame[2] = 0;
  display.applyAutomationFrame(frame, sizeof(frame), 65004);
  assert(!display.automationHeld(65004));
  frame[3] = 0; // Master restart clears the sequence; it is not a wake event.
  display.applyAutomationFrame(frame, sizeof(frame), 65005);
  assert(!display.takeAutomationWake());
  for (uint16_t len = 0; len < 14; ++len) display.applyAutomationFrame(frame, len, 70000);
  display.applyAutomationFrame(nullptr, 255, 70000);
  assert(!display.automationHeld(70000));
  frame[2] = 1;
  display.applyAutomationFrame(frame, sizeof(frame), 0xFFFFFF00U);
  assert(display.automationHeld(0x00000100U));
  assert(!display.automationHeld(0x00002000U));
  frame[1] = 2;
  display.applyAutomationFrame(frame, sizeof(frame), 80000);
  assert(!display.automationHeld(80000)); // Unsupported version releases the lease.
  std::puts("PASS: runtime ownership, eight markers, display wake deduplication, hold leases, legacy/truncated frames and millis rollover");
}
