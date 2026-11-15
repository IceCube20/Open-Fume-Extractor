#include "UsbHostDiagnostics.h"
#include <cassert>
#include <cstdio>

int main() {
  using namespace ofe_usb;
  UsbHostDiagnostics report;
  assert(!report.host_ready && !report.root_connected);
  assert(!report.attach({}));
  UsbObservedDevice hub;
  hub.address = 1; hub.device_class = 9; hub.hub = true; hub.vendor = 0x1A86; hub.product = 0x8091;
  assert(report.attach(hub));
  assert(report.attach(hub));
  assert(report.attachments == 1);
  UsbObservedDevice stick;
  stick.address = 2; stick.storage = true; stick.bot = true;
  assert(report.attach(stick));
  assert(report.devices[0].hub && !report.devices[0].storage);
  assert(report.devices[1].storage && report.devices[1].bot);
  for (uint8_t address = 3; address <= UsbHostDiagnostics::CAPACITY; ++address) {
    UsbObservedDevice other; other.address = address;
    assert(report.attach(other));
  }
  UsbObservedDevice extra; extra.address = 9;
  assert(!report.attach(extra)); assert(report.overflow == 1);
  report.remove(0); report.remove(100); assert(report.removals == 0);
  report.remove(2); assert(report.removals == 1);
  assert(report.attach(extra));
  assert(report.devices[1].address == 9 && !report.devices[1].storage);
  report.remove(1); assert(report.removals == 2 && !report.devices[0].address);
  assert(report.attach(hub)); assert(report.devices[0].hub);
  puts("PASS: actual USB observer table: no-device, hub vs SCSI/BOT, duplicates, bounded storage, overflow, removal and reconnection");
}
