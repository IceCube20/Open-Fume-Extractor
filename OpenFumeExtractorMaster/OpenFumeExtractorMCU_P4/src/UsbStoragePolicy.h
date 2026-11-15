#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ofe_usb {
constexpr size_t MAX_FILE_BYTES = 6 * 1024 * 1024;
constexpr size_t MAX_FILES = 32;
constexpr size_t MAX_NAME = 96;
constexpr uint32_t BUFFER_LEASE_MS = 120000;

inline bool safeName(const char* name) {
  if (!name || !*name || strlen(name) >= MAX_NAME || name[0] == '.' || strstr(name, "..")) return false;
  for (const char* p = name; *p; ++p)
    if (!( (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
           (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.')) return false;
  const size_t n = strlen(name);
  return name[n - 1] != '.';
}

inline bool range(size_t total, size_t offset, size_t length) {
  return total && total <= MAX_FILE_BYTES && offset <= total && length <= total - offset;
}

inline bool visibleFile(const char* name) {
  if (!safeName(name)) return false;
  const char* ext = strrchr(name, '.');
  if (!ext) return false;
  const char* allowed[] = {".bin", ".json", ".csv", ".log", ".txt", ".ndjson", ".ofeprofile"};
  for (const char* candidate : allowed) {
    const char* a = ext;
    const char* b = candidate;
    while (*a && *b && char(*a >= 'A' && *a <= 'Z' ? *a + 32 : *a) == *b) { ++a; ++b; }
    if (!*a && !*b) return true;
  }
  return false;
}
}  // namespace ofe_usb
