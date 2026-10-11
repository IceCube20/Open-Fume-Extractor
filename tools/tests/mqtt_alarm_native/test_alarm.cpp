#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

#define F(value) value
class String : public std::string {
public:
  using std::string::string;
  String() = default;
  String(const std::string& value) : std::string(value) {}
  String substring(unsigned int begin, unsigned int end) const {
    return substr(begin, end - begin);
  }
};

#include "generated_alarm.inc.h"

int main() {
  assert(mqtt_alarm_text_json("") == "OK");
  assert(mqtt_alarm_text_json("OK") == "OK");
  assert(mqtt_alarm_text_json("0x20: Fan offline; 0x30: Weller disconnected") ==
         "0x20: Fan offline; 0x30: Weller disconnected");
  assert(mqtt_alarm_text_json("\"Fan\" \\ alarm\nnext\r\t") == "\\\"Fan\\\" \\\\ alarm\\nnext\\r\\t");
  assert(mqtt_alarm_text_json(std::string(400, 'A')) == std::string(255, 'A'));
  for (const std::string character : {"\xC3\xA4", "\xE2\x82\xAC", "\xF0\x9F\x8C\xB1"}) {
    const std::string prefix(254, 'A');
    assert(mqtt_alarm_text_json(prefix + character + "tail") == prefix + character);
    std::string text;
    for (int i = 0; i < 255; ++i) text += character;
    assert(mqtt_alarm_text_json(text + "tail") == text);
  }
  const std::string mixed = std::string(254, 'A') + '"';
  assert(mqtt_alarm_text_json(mixed + "tail") == std::string(254, 'A') + "\\\"");
  std::puts("PASS: production MQTT alarm text/JSON escaping, empty fallback, 255-character limit and 2/3/4-byte UTF-8 boundaries");
}
