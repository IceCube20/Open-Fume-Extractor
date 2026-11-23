#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

namespace ofe_usb {
// Current ESP VFS uses signed 32-bit file offsets; this is not a RAM-buffer cap.
constexpr size_t MAX_FILE_BYTES = INT32_MAX;
constexpr size_t TRANSFER_CHUNK_BYTES = 8192;
constexpr unsigned MAX_DRIVES = 4;
constexpr unsigned NO_DRIVE = UINT32_MAX;
constexpr size_t MAX_FILES = 128;
constexpr size_t MAX_DIRECTORIES = 64;
constexpr size_t MAX_DEPTH = 6;
constexpr size_t MAX_PATH = 384;
constexpr size_t MAX_SCAN_ENTRIES = 2048;
constexpr uint32_t SCAN_TIMEOUT_MS = 10000;
constexpr unsigned DEFAULT_DIRECTORY = UINT32_MAX;
constexpr size_t MAX_NAME = 96;
constexpr uint32_t BUFFER_LEASE_MS = 120000;

inline bool safeName(const char* name) {
  if (!name || !*name || strlen(name) >= MAX_NAME || name[0] == '.' || name[0] == ' ' || strstr(name, "..")) return false;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(name); *p; ++p) {
    if (*p < 32 || *p == 127 || strchr("/\\:\"*?<>|", *p)) return false;
    if (*p < 128) continue;
    unsigned continuation = 0, low = 0x80, high = 0xBF;
    if (*p >= 0xC2 && *p <= 0xDF) continuation = 1;
    else if (*p >= 0xE0 && *p <= 0xEF) { continuation = 2; if (*p == 0xE0) low = 0xA0; if (*p == 0xED) high = 0x9F; }
    else if (*p >= 0xF0 && *p <= 0xF4) { continuation = 3; if (*p == 0xF0) low = 0x90; if (*p == 0xF4) high = 0x8F; }
    else return false;
    for (unsigned i = 1; i <= continuation; ++i) if (p[i] < (i == 1 ? low : 0x80) || p[i] > (i == 1 ? high : 0xBF)) return false;
    p += continuation;
  }
  const size_t n = strlen(name);
  const size_t stem = strcspn(name, ".");
  if (stem == 3 || stem == 4) {
    const char a = char(name[0] | 32), b = char(name[1] | 32), c = char(name[2] | 32);
    if (stem == 3 && ((a == 'c' && b == 'o' && c == 'n') || (a == 'p' && b == 'r' && c == 'n') ||
        (a == 'a' && b == 'u' && c == 'x') || (a == 'n' && b == 'u' && c == 'l'))) return false;
    if (stem == 4 && name[3] >= '1' && name[3] <= '9' &&
        ((a == 'c' && b == 'o' && c == 'm') || (a == 'l' && b == 'p' && c == 't'))) return false;
  }
  return name[n - 1] != '.' && name[n - 1] != ' ';
}

inline bool joinPath(char* out, size_t capacity, const char* directory, const char* name) {
  if (!out || !capacity || !directory || !safeName(name)) return false;
  const int length = snprintf(out, capacity, "%s%s%s", directory, *directory ? "/" : "", name);
  return length > 0 && size_t(length) < capacity;
}

inline size_t pathDepth(const char* path) {
  if (!path || !*path) return 0;
  size_t depth = 1;
  for (; *path; ++path) if (*path == '/') ++depth;
  return depth;
}

inline bool range(size_t total, size_t offset, size_t length) {
  return total && total <= MAX_FILE_BYTES && offset <= total && length <= total - offset;
}

inline bool visibleFile(const char* name) {
  if (!safeName(name)) return false;
  const char* ext = strrchr(name, '.');
  return !ext || strlen(ext) != 4 || (ext[1] | 32) != 't' || (ext[2] | 32) != 'm' || (ext[3] | 32) != 'p';
}
}  // namespace ofe_usb
