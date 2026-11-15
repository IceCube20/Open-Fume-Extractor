#pragma once
#include <stddef.h>
#include <string.h>

namespace ofe_module_label {
inline bool equal_folded(const char* a, const char* b) {
  if (!a || !b) return a == b;
  for (; *a && *b; ++a, ++b) {
    const auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
    if (lower(*a) != lower(*b)) return false;
  }
  return *a == *b;
}

inline void from_info(const char* name, const char* default_name, char* label, size_t capacity) {
  if (!label || !capacity) return;
  label[0] = 0;
  if (!name) return;
  while (*name && static_cast<unsigned char>(*name) <= ' ') ++name;
  size_t length = 0;
  while (*name && length + 1 < capacity) {
    const unsigned char c = static_cast<unsigned char>(*name++);
    label[length++] = c < 0x20 || c == '"' || c == '\\' || c == '<' || c == '>' ? ' ' : char(c);
  }
  while (length && label[length - 1] == ' ') --length;
  label[length] = 0;
  if (equal_folded(label, default_name)) label[0] = 0;
}
}  // namespace ofe_module_label
