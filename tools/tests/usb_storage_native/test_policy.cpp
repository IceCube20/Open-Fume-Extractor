#include "UsbStoragePolicy.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <initializer_list>

int main() {
  using namespace ofe_usb;
  for (const char* name : {"firmware.bin", "BACKUP.JSON", "open-fume-extractor-JBC-FAE-0X10.csv", "trace.ndjson", "Weller_v6.ofeprofile"}) {
    assert(safeName(name)); assert(visibleFile(name));
  }
  for (const char* name : {"", ".hidden.bin", "../firmware.bin", "a/firmware.bin", "a\\firmware.bin", "a:firmware.bin", "a\n.bin", "firmware.bin.", "..", "a..bin"}) assert(!safeName(name));
  char longName[MAX_NAME + 1];
  memset(longName, 'a', sizeof(longName)); longName[MAX_NAME] = 0;
  assert(!safeName(longName));
  assert(!visibleFile("not-firmware.exe"));
  assert(!visibleFile("partial.bin.tmp"));
  assert(range(MAX_FILE_BYTES, MAX_FILE_BYTES-4096, 4096));
  assert(range(1, 0, 1));
  assert(!range(0, 0, 0));
  assert(!range(MAX_FILE_BYTES+1, 0, 1));
  assert(!range(4096, 4096, 1));
  assert(!range(4096, std::numeric_limits<size_t>::max(), 2));
  assert(!range(4096, 1, std::numeric_limits<size_t>::max()));
  puts("PASS: actual USB filename, extension, PSRAM size and overflow policies");
}
