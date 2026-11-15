#pragma once

#include <stddef.h>
#include <string.h>

namespace ofe_json {
// Keep UTF-8 bytes intact and preserve the existing control-byte replacement.
template <typename Append>
bool appendEscaped(const char* value, Append append) {
  if (!value) return true;
  const char* run = value;
  for (const char* p = value; *p; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c >= 0x20 && c != '"' && c != '\\') continue;
    if (p != run && !append(run, size_t(p - run))) return false;
    char escaped[2] = {'\\', char(c)};
    size_t length = 2;
    if (c == '\n') escaped[1] = 'n';
    else if (c == '\r') escaped[1] = 'r';
    else if (c == '\t') escaped[1] = 't';
    else if (c != '"' && c != '\\') { escaped[0] = ' '; length = 1; }
    if (!append(escaped, length)) return false;
    run = p + 1;
  }
  return !*run || append(run, strlen(run));
}
}  // namespace ofe_json
