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
  for (const char* name : {"CON", "nul.txt", "COM1.bin", "lpt9.txt", " name.txt", "name.txt "}) assert(!safeName(name));
  char longName[MAX_NAME + 1];
  memset(longName, 'a', sizeof(longName)); longName[MAX_NAME] = 0;
  assert(!safeName(longName));
  assert(visibleFile("not-firmware.exe"));
  assert(visibleFile("photo (1).jpg"));
  assert(safeName("L\xc3\xb6tstation Daten"));
  assert(safeName("\xf0\x9f\x93\x81 Ordner"));
  for (const char* name : {"\xc0\xaf.bin", "\xed\xa0\x80.txt", "\xf4\x90\x80\x80.txt", "\xc3", "\xe2\x82"}) assert(!safeName(name));
  assert(!visibleFile("partial.bin.tmp"));
  assert(!visibleFile("partial.bin.TMP"));
  char joined[MAX_PATH];
  assert(joinPath(joined, sizeof(joined), "OFE/Backups", "backup (1).json"));
  assert(!strcmp(joined, "OFE/Backups/backup (1).json"));
  assert(pathDepth(joined) == 3 && pathDepth("") == 0);
  assert(!joinPath(joined, sizeof(joined), "OFE", "../escape"));
  assert(!joinPath(joined, 5, "OFE", "name.txt"));
  assert(range(MAX_FILE_BYTES, MAX_FILE_BYTES-4096, 4096));
  assert(range(1, 0, 1));
  assert(!range(0, 0, 0));
  assert(!range(MAX_FILE_BYTES+1, 0, 1));
  assert(!range(4096, 4096, 1));
  assert(!range(4096, std::numeric_limits<size_t>::max(), 2));
  assert(!range(4096, 1, std::numeric_limits<size_t>::max()));
  static_assert(MAX_FILE_BYTES == INT32_MAX, "VFS signed-offset limit, not a RAM cap");
  static_assert(TRANSFER_CHUNK_BYTES == 8192, "Fixed streaming chunk budget");
  assert(range(7 * 1024 * 1024 + 1, 7 * 1024 * 1024, 1));
  assert(range(MAX_FILE_BYTES, MAX_FILE_BYTES - 1, 1));
  puts("PASS: actual USB filename, extension, streaming VFS size and overflow policies");
}
