#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ofe_entity {

// Views borrow descriptor storage. Do not retain them across a descriptor update.
struct Text {
  const char* data = nullptr;
  size_t size = 0;
  bool empty() const { return size == 0; }
};

struct Definition {
  uint8_t id = 0;
  Text line;
  Text type;
  Text key;
  Text mode;
};

Text text(const char* value);
Text trim(Text value);
bool equal(Text value, const char* expected);
size_t copy(Text value, char* out, size_t capacity);
bool parse(Text line, Definition& out);
bool find(const char* descriptor, uint8_t id, Definition& out);
Text metadata(Text line, const char* key);
Text label(const Definition& def, bool german);
Text access(const Definition& def);
Text listItem(Text list, size_t index);
enum class Encoding : uint8_t { Auto, Unsigned32, Signed32 };
Encoding valueEncoding(const Definition& def);
size_t rawText(const uint8_t* data, size_t size, char* out, size_t capacity, Encoding encoding = Encoding::Auto);
bool boolValue(Text raw);
bool selectIndex(const Definition& def, Text raw, uint8_t& index);
Text selectValue(const Definition& def, uint8_t index);
size_t durationText(uint32_t minutes, char* out, size_t capacity);
size_t displayText(const Definition& def, Text raw, char* out, size_t capacity);

} // namespace ofe_entity
