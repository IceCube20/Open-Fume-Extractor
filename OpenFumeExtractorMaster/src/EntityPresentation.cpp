#include "EntityPresentation.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

namespace ofe_entity {
namespace {
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool keyChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}
char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
Text token(Text& remaining) {
  remaining = trim(remaining);
  size_t n = 0;
  while (n < remaining.size && !space(remaining.data[n])) ++n;
  Text result{remaining.data, n};
  if (n) remaining.data += n;
  remaining.size -= n;
  return result;
}
bool same(Text a, Text b) {
  a = trim(a); b = trim(b);
  if (a.size != b.size) return false;
  for (size_t i = 0; i < a.size; ++i) if (lower(a.data[i]) != lower(b.data[i])) return false;
  return true;
}
bool number(Text value, int64_t& out, bool protocolPrefix = false) {
  value = trim(value);
  if (protocolPrefix && value.size > 1 &&
      ((value.data[0] >= 'A' && value.data[0] <= 'Z') ||
       (value.data[0] >= 'a' && value.data[0] <= 'z'))) {
    ++value.data; --value.size;
  }
  bool negative = false;
  if (value.size && (value.data[0] == '+' || value.data[0] == '-')) {
    negative = value.data[0] == '-'; ++value.data; --value.size;
  }
  if (!value.size) return false;
  uint64_t magnitude = 0;
  const uint64_t limit = negative ? (uint64_t)INT64_MAX + 1U : (uint64_t)INT64_MAX;
  for (size_t i = 0; i < value.size; ++i) {
    const char c = value.data[i];
    if (c < '0' || c > '9') return false;
    const unsigned digit = (unsigned)(c - '0');
    if (magnitude > (limit - digit) / 10U) return false;
    magnitude = magnitude * 10U + digit;
  }
  out = negative ? (magnitude == (uint64_t)INT64_MAX + 1U ? INT64_MIN : -(int64_t)magnitude)
                 : (int64_t)magnitude;
  return true;
}
bool minutes(const Definition& def, int64_t value, uint32_t& out) {
  Text base = metadata(def.line, "time_base");
  if (base.empty()) base = metadata(def.line, "tb");
  uint64_t v = value > 0 ? (uint64_t)value : 0;
  uint32_t multiplier = 1;
  if (equal(base, "s") || equal(base, "sec") || equal(base, "seconds")) v = (v + 30U) / 60U;
  else if (equal(base, "h") || equal(base, "hour") || equal(base, "hours")) multiplier = 60;
  else if (equal(base, "d") || equal(base, "day") || equal(base, "days")) multiplier = 1440;
  else if (!(equal(base, "m") || equal(base, "min") || equal(base, "minutes"))) return false;
  out = v > UINT32_MAX / multiplier ? UINT32_MAX : (uint32_t)v * multiplier;
  return true;
}
bool durationEnabled(const Definition& def) {
  Text format = metadata(def.line, "time_display");
  if (format.empty()) format = metadata(def.line, "tf");
  return equal(format, "dhm");
}
bool formattedMinutes(Text raw, uint32_t& out) {
  uint64_t total = 0;
  unsigned fields = 0;
  while (!(raw = trim(raw)).empty()) {
    size_t n = 0;
    while (n < raw.size && raw.data[n] >= '0' && raw.data[n] <= '9') ++n;
    int64_t value = 0;
    if (!n || n == raw.size || !number({raw.data, n}, value)) return false;
    const char unit = lower(raw.data[n]);
    const unsigned bit = unit == 'd' ? 4 : unit == 'h' ? 2 : unit == 'm' ? 1 : 0;
    if (!bit || (fields & bit)) return false;
    fields |= bit;
    const uint32_t factor = bit == 4 ? 1440 : bit == 2 ? 60 : 1;
    if ((uint64_t)value > UINT32_MAX / factor) return false;
    total += (uint64_t)value * factor;
    if (total > UINT32_MAX) return false;
    raw.data += n + 1; raw.size -= n + 1;
  }
  out = (uint32_t)total;
  return fields != 0;
}
// Reject binary/control payloads, but preserve valid UTF-8 profile text.
bool printable(Text value) {
  for (size_t i = 0; i < value.size;) {
    const uint8_t c = (uint8_t)value.data[i++];
    if (c < 0x20 || c == 0x7F) return false;
    if (c < 0x80) continue;
    const unsigned extra = c >= 0xC2 && c <= 0xDF ? 1 : c >= 0xE0 && c <= 0xEF ? 2 : c >= 0xF0 && c <= 0xF4 ? 3 : 0;
    if (!extra || i + extra > value.size) return false;
    const uint8_t first = (uint8_t)value.data[i];
    if ((c == 0xE0 && first < 0xA0) || (c == 0xED && first > 0x9F) ||
        (c == 0xF0 && first < 0x90) || (c == 0xF4 && first > 0x8F)) return false;
    for (unsigned j = 0; j < extra; ++j) if (((uint8_t)value.data[i++] & 0xC0) != 0x80) return false;
  }
  return true;
}
} // namespace

Text text(const char* value) { return {value, value ? strlen(value) : 0}; }
Text trim(Text value) {
  while (value.size && space(value.data[0])) { ++value.data; --value.size; }
  while (value.size && space(value.data[value.size - 1])) --value.size;
  return value;
}
bool equal(Text value, const char* expected) { return same(value, text(expected)); }
size_t copy(Text value, char* out, size_t capacity) {
  if (!out || !capacity) return 0;
  size_t n = value.size < capacity ? value.size : capacity - 1;
  // Never split a UTF-8 character at the display's fixed wire-text boundary.
  if (n < value.size) while (n && ((uint8_t)value.data[n] & 0xC0) == 0x80) --n;
  if (n) memmove(out, value.data, n);
  out[n] = 0;
  return n;
}
bool parse(Text line, Definition& out) {
  out = Definition{};
  line = trim(line);
  Text rest = line;
  const Text id = token(rest);
  int64_t numeric = 0;
  if (!number(id, numeric) || numeric < 0 || numeric > 255 || id.empty() || id.data[0] < '0' || id.data[0] > '9') return false;
  out.id = (uint8_t)numeric;
  out.line = line;
  out.type = token(rest); out.key = token(rest); out.mode = token(rest);
  return !out.type.empty() && !out.key.empty() && !out.mode.empty();
}
bool find(const char* descriptor, uint8_t id, Definition& out) {
  out = Definition{};
  for (const char* p = descriptor; p && *p;) {
    const char* next = strchr(p, '\n');
    const Text line{p, next ? (size_t)(next - p) : strlen(p)};
    if (parse(line, out) && out.id == id) return true;
    p = next ? next + 1 : nullptr;
  }
  out = Definition{};
  return false;
}
Text metadata(Text line, const char* key) {
  const size_t keySize = key ? strlen(key) : 0;
  if (!keySize) return {};
  for (size_t i = 0; i + keySize < line.size; ++i) {
    if (i && !space(line.data[i - 1])) continue;
    if (strncmp(line.data + i, key, keySize) || line.data[i + keySize] != '=') continue;
    const size_t start = i + keySize + 1;
    size_t end = start;
    for (; end < line.size; ++end) {
      if (!space(line.data[end])) continue;
      size_t k = end + 1;
      while (k < line.size && space(line.data[k])) ++k;
      const size_t begin = k;
      while (k < line.size && keyChar(line.data[k])) ++k;
      if (k > begin && k < line.size && line.data[k] == '=') break;
    }
    return trim({line.data + start, end - start});
  }
  return {};
}
Text label(const Definition& def, bool german) {
  Text value = metadata(def.line, german ? "de" : "en");
  if (value.empty()) value = metadata(def.line, german ? "en" : "de");
  return value.empty() ? def.key : value;
}
Text access(const Definition& def) {
  Text mode = metadata(def.line, "access");
  if (!(equal(mode, "ro") || equal(mode, "rw") || equal(mode, "wo"))) mode = metadata(def.line, "mode");
  if (!(equal(mode, "ro") || equal(mode, "rw") || equal(mode, "wo"))) mode = def.mode;
  return equal(mode, "ro") || equal(mode, "rw") || equal(mode, "wo") ? mode : Text{};
}
Text listItem(Text list, size_t index) {
  size_t start = 0, item = 0;
  for (size_t i = 0; i <= list.size; ++i) {
    if (i == list.size || list.data[i] == '|' || list.data[i] == ';') {
      if (item++ == index) return trim({list.data ? list.data + start : nullptr, i - start});
      start = i + 1;
    }
  }
  return {};
}
Encoding valueEncoding(const Definition& def) {
  // Profile builders publish int32_t numeric values; built-in counters are u32.
  if (!(equal(def.type, "number") || equal(def.type, "sensor"))) return Encoding::Auto;
  return def.id >= 20 ? Encoding::Signed32 : Encoding::Unsigned32;
}
size_t rawText(const uint8_t* data, size_t size, char* out, size_t capacity, Encoding encoding) {
  if (!out || !capacity) return 0;
  out[0] = 0;
  if (!data || !size) return 0;
  const Text raw{(const char*)data, size};
  if ((size != 4 || encoding == Encoding::Auto) && printable(raw)) return copy(trim(raw), out, capacity);
  uint32_t value = 0;
  for (size_t i = 0; i < size && i < 4; ++i) value |= (uint32_t)data[i] << (8U * i);
  char buffer[12];
  if (encoding == Encoding::Signed32 && size == 4) snprintf(buffer, sizeof(buffer), "%ld", (long)(int32_t)value);
  else snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)value);
  return copy(text(buffer), out, capacity);
}
bool boolValue(Text raw) {
  if (equal(raw, "on") || equal(raw, "true")) return true;
  int64_t value = 0;
  return number(raw, value) && value != 0;
}
Text selectValue(const Definition& def, uint8_t index) {
  Text value = listItem(metadata(def.line, "values"), index);
  return value.empty() ? listItem(metadata(def.line, "options"), index) : value;
}
bool selectIndex(const Definition& def, Text raw, uint8_t& index) {
  index = 0;
  const Text options = metadata(def.line, "options"), values = metadata(def.line, "values");
  // Exact matches take precedence over compatibility numeric/scale matching.
  for (unsigned i = 0; i < 32; ++i) {
    const Text option = listItem(options, i), value = listItem(values, i);
    if (option.empty() && value.empty()) break;
    if ((!option.empty() && same(raw, option)) || (!value.empty() && same(raw, value))) { index = (uint8_t)i; return true; }
  }
  int64_t rawNumber = 0, scale = 1;
  const bool numeric = number(raw, rawNumber, true);
  if (numeric) for (unsigned i = 0; i < 32; ++i) {
    const Text option = listItem(options, i), value = listItem(values, i);
    if (option.empty() && value.empty()) break;
    int64_t candidate = 0;
    if (number(value.empty() ? option : value, candidate) && rawNumber == candidate) {
      index = (uint8_t)i; return true;
    }
  }
  static const char* const scaleKeys[] = {"scale", "sc", "trace_scale"};
  for (const char* key : scaleKeys) {
    int64_t candidate = 0;
    if (number(metadata(def.line, key), candidate) && candidate > 1 && candidate <= 32767) { scale = candidate; break; }
  }
  uint32_t rawMinutes = 0;
  const bool duration = durationEnabled(def) && formattedMinutes(raw, rawMinutes);
  for (unsigned i = 0; i < 32; ++i) {
    const Text option = listItem(options, i), value = listItem(values, i);
    if (option.empty() && value.empty()) break;
    int64_t candidate = 0;
    if (!number(value.empty() ? option : value, candidate)) continue;
    const bool canScale = candidate <= INT64_MAX / scale && candidate >= INT64_MIN / scale;
    if (numeric && (rawNumber == candidate || (canScale && rawNumber == candidate * scale) ||
        (rawNumber <= INT64_MAX / scale && rawNumber >= INT64_MIN / scale && rawNumber * scale == candidate))) {
      index = (uint8_t)i; return true;
    }
    uint32_t candidateMinutes = 0;
    if (duration && canScale && minutes(def, candidate * scale, candidateMinutes) && candidateMinutes == rawMinutes) {
      index = (uint8_t)i; return true;
    }
  }
  return false;
}
size_t durationText(uint32_t minutes, char* out, size_t capacity) {
  char buffer[40];
  const uint32_t days = minutes / 1440U, hours = (minutes % 1440U) / 60U, mins = minutes % 60U;
  size_t n = 0;
  if (days) n += (size_t)snprintf(buffer + n, sizeof(buffer) - n, "%lud", (unsigned long)days);
  if (hours) n += (size_t)snprintf(buffer + n, sizeof(buffer) - n, "%s%luh", n ? " " : "", (unsigned long)hours);
  if (mins || !n) snprintf(buffer + n, sizeof(buffer) - n, "%s%lum", n ? " " : "", (unsigned long)mins);
  return copy(text(buffer), out, capacity);
}
size_t displayText(const Definition& def, Text raw, char* out, size_t capacity) {
  if (equal(def.type, "switch") || equal(def.type, "binary_sensor")) return copy(text(boolValue(raw) ? "1" : "0"), out, capacity);
  if (equal(def.type, "select")) {
    uint8_t index = 0;
    if (selectIndex(def, raw, index)) {
      Text option = listItem(metadata(def.line, "options"), index);
      if (!option.empty()) return copy(option, out, capacity);
    }
    // Select text without a matching option stays intact, including d/h/m text.
    return copy(raw, out, capacity);
  }
  // Numeric duration fallback only. Never reapply scale/map to module values.
  int64_t value = 0;
  uint32_t total = 0;
  if (durationEnabled(def) && number(raw, value) && minutes(def, value, total)) return durationText(total, out, capacity);
  return copy(raw, out, capacity);
}
} // namespace ofe_entity
