#include "EntityPresentation.h"

#include <assert.h>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

using namespace ofe_entity;

static std::string own(Text value) { return value.size ? std::string(value.data, value.size) : ""; }
static Definition definition(const char* line) {
  Definition def;
  assert(parse(text(line), def));
  return def;
}
static std::string shown(const char* line, const char* value) {
  char out[128];
  displayText(definition(line), text(value), out, sizeof(out));
  return out;
}
static void test_metadata() {
  const char* line = " 20\tselect\tfilter_time\trw en=Programmed filter lifetime de=Filterwechselintervall unit=min access=RO options=8 h|16 h values=48|96 future_metadata=kept\r";
  Definition def = definition(line);
  assert(def.id == 20 && equal(def.type, "select"));
  assert(own(label(def, false)) == "Programmed filter lifetime");
  assert(own(label(def, true)) == "Filterwechselintervall");
  assert(equal(access(def), "ro"));
  assert(own(metadata(def.line, "values")) == "48|96");
  assert(metadata(def.line, "value").empty());
  assert(own(label(definition("20 text fallback_key ro"), true)) == "fallback_key");
  assert(own(label(definition("20 text key ro de=Nur Deutsch"), false)) == "Nur Deutsch");
  assert(own(label(definition("20 text key ro en=English only"), true)) == "English only");
  assert(equal(access(definition("20 text key ro access=broken mode=wo")), "wo"));
  assert(equal(access(definition("20 text key rw access=broken")), "rw"));
  for (const char* bad : {"256 text key ro", "99999999999999999999999 text key ro", "-1 text key ro", "20text key ro", "20 text", "schema=1"}) assert(!parse(text(bad), def));
  const std::string descriptor = "schema=1\n20 number power rw en=Power\n21 text state ro en=State\n";
  assert(find(descriptor.c_str(), 21, def));
  assert(own(label(def, false)) == "State");
  assert(!find(descriptor.c_str(), 22, def));
  assert(def.line.empty());
  // Bounded, non-NUL-terminated view must not borrow metadata from the next line.
  const std::string joined = "20 text key ro en=First\n21 text key ro en=Second";
  assert(parse({joined.data(), joined.find('\n')}, def));
  assert(own(label(def, false)) == "First");
}
static void test_payloads() {
  char out[64];
  const uint8_t bytes[] = {0xD0, 0x07};
  rawText(bytes, sizeof(bytes), out, sizeof(out));
  assert(std::string(out) == "2000");
  const uint8_t large[] = {0xFF, 0xFF, 0xFF, 0xFF};
  rawText(large, sizeof(large), out, sizeof(out));
  assert(std::string(out) == "4294967295");
  assert(valueEncoding(definition("2 sensor counter ro")) == Encoding::Unsigned32);
  assert(valueEncoding(definition("20 sensor temperature ro")) == Encoding::Signed32);
  assert(valueEncoding(definition("20 number value rw")) == Encoding::Signed32);
  assert(valueEncoding(definition("20 text state ro")) == Encoding::Auto);
  const uint8_t negative[] = {0x30, 0xF8, 0xFF, 0xFF};
  rawText(negative, sizeof(negative), out, sizeof(out), Encoding::Signed32);
  assert(std::string(out) == "-2000");
  rawText(large, sizeof(large), out, sizeof(out), Encoding::Signed32);
  assert(std::string(out) == "-1");
  const uint8_t looksLikeText[] = {'1', '2', '3', '4'};
  rawText(looksLikeText, 4, out, sizeof(out), Encoding::Unsigned32);
  assert(std::string(out) == "875770417");
  rawText(looksLikeText, 4, out, sizeof(out), Encoding::Auto);
  assert(std::string(out) == "1234");
  const std::string umlauts = "L\xC3\xBC" "fter bereit";
  rawText((const uint8_t*)umlauts.data(), umlauts.size(), out, sizeof(out));
  assert(std::string(out) == umlauts);
  for (size_t cap = 1; cap <= umlauts.size() + 1; ++cap) {
    char bounded[64];
    copy(text(umlauts.c_str()), bounded, cap);
    assert(strlen(bounded) < cap);
    if (cap == 3) assert(std::string(bounded) == "L");
  }
  const uint8_t badUtf8[] = {0xC0, 0x80};
  rawText(badUtf8, sizeof(badUtf8), out, sizeof(out));
  assert(std::string(out) == "32960");
  const uint8_t on = 1;
  rawText(&on, 1, out, sizeof(out));
  assert(boolValue(text(out)));
  for (const char* raw : {"ON", "on", "True", "1", "100", "-2", " 42 "}) assert(boolValue(text(raw)));
  for (const char* raw : {"OFF", "off", "false", "0", "00", "", "pending"}) assert(!boolValue(text(raw)));
  rawText(nullptr, 0, out, sizeof(out)); assert(!out[0]);
  assert(rawText(&on, 1, nullptr, 0) == 0);
}
static void test_values() {
  const char* duration = "24 text runtime ro scale=10 tb=m tf=dhm";
  assert(shown(duration, "1d 16h 50m") == "1d 16h 50m");
  assert(shown(duration, "2450") == "1d 16h 50m");
  assert(shown("24 sensor runtime ro tb=minutes tf=dhm", "10") == "10m");
  assert(shown("24 text runtime ro tb=seconds tf=dhm", "90") == "2m");
  assert(shown("24 text runtime ro tb=hours tf=dhm", "40") == "1d 16h");
  assert(shown("24 text runtime ro tb=days tf=dhm", "3") == "3d");
  assert(shown("24 text runtime ro tb=m tf=dhm", "-1") == "0m");
  assert(shown("24 text runtime ro tb=m tf=dhm", "9223372036854775808") == "9223372036854775808");
  assert(shown("24 text runtime ro tb=days tf=dhm", "9223372036854775807") == "2982616d 4h 15m");
  assert(shown("24 text runtime ro tb=none tf=dhm", "10") == "10");
  assert(shown("20 sensor rpm ro scale=10", "1230") == "1230");
  assert(shown("25 text filter ro map=1=Good|2=Replace soon", "Replace soon") == "Replace soon");
  assert(shown("25 text filter ro map=1=Good|2=Replace soon", "2") == "2");
  assert(shown("21 switch light rw value_on=100 value_off=0", "off") == "0");
  assert(shown("21 switch light rw value_on=100 value_off=0", "100") == "1");
  const char* select = "26 select programmed_filter rw en=Programmed filter lifetime scale=10 tb=m tf=dhm options=8 h|16 h|1 day|6 days 16 h values=48|96|144|960";
  for (const char* value : {"960", "F960", "9600", "6d 16h 0m", "6 days 16 h"}) {
    uint8_t index = 0;
    assert(selectIndex(definition(select), text(value), index) && index == 3);
    assert(own(selectValue(definition(select), index)) == "960");
    assert(shown(select, value) == "6 days 16 h");
  }
  assert(shown(select, "unknown") == "unknown");
  assert(shown(select, "1d 0h 0m") == "1 day");
  assert(shown(select, "not960") == "not960");
  // Exact option values take precedence over scaled values (1 vs 10).
  assert(shown("20 select key rw scale=10 options=A|B values=1|10", "10") == "B");
  // Lists longer than 255 bytes must not wrap their cursor or loop forever.
  std::string options;
  for (unsigned i = 0; i < 32; ++i) { if (i) options += '|'; options += "Long option " + std::to_string(i); }
  const std::string longLine = "20 select key rw options=" + options;
  assert(own(listItem(metadata(definition(longLine.c_str()).line, "options"), 31)) == "Long option 31");
  assert(shown(longLine.c_str(), "Long option 31") == "Long option 31");
  assert(own(listItem(text("a ; b | c"), 2)) == "c");
  char out[40]; durationText(0, out, sizeof(out)); assert(std::string(out) == "0m");
}
static void benchmark() {
  std::string descriptor = "schema=1\n";
  for (unsigned i = 20; i < 84; ++i) descriptor += std::to_string(i) + " text value ro en=Filter runtime scale=10 tb=m tf=dhm\n";
  const auto begin = std::chrono::steady_clock::now();
  size_t bytes = 0;
  for (unsigned pass = 0; pass < 1000; ++pass) for (unsigned id = 20; id < 84; ++id) {
    Definition def; assert(find(descriptor.c_str(), (uint8_t)id, def));
    char out[64]; bytes += displayText(def, text("1d 16h 50m"), out, sizeof(out));
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
  std::cout << "64-entity presentation: " << ms / 1000 << " ms/snapshot (host), bytes=" << bytes << '\n';
}
static std::string quote(const std::string& value) {
  std::string result = "\"";
  for (char c : value) { if (c == '\\' || c == '"') result += '\\'; result += c; }
  return result + '"';
}
static void fixtures() {
  struct Case { const char* line; std::string raw; std::string expected; };
  const Case cases[] = {
    {"20 number power rw en=Power de=Leistung unit=% min=30 max=100 step=1 scale=10", std::string("\x37\x00\x00\x00", 4), "55 %"},
    {"21 switch light rw en=Work light de=Arbeitslicht value_on=100 value_off=0", "off", "OFF"},
    {"22 sensor rpm ro en=Speed de=Drehzahl unit=rpm scale=10", std::string("\xCE\x04\x00\x00", 4), "1230 rpm"},
    {"24 text runtime ro en=Filter operating time de=Filterbetriebszeit scale=10 tb=m tf=dhm", "1d 16h 50m", "1d 16h 50m"},
    {"25 text state ro de=Nur Deutsch map=1=Good|2=Replace soon", "Replace soon", "Replace soon"},
    {"26 select programmed_filter rw en=Filter replacement interval de=Filterwechselintervall scale=10 tb=m tf=dhm options=8 h|16 h|3 days|6 days 16 h values=48|96|432|960", "6d 16h 0m", "6 days 16 h"},
    {"27 text name ro en=Station name de=Stationsname", "L\xC3\xBC" "fter bereit", "L\xC3\xBC" "fter bereit"},
    {"28 sensor pressure ro en=Pressure de=Druck unit=Pa", std::string("\xD0\x07\x00\x00", 4), "2000 Pa"},
    {"29 sensor temperature ro en=Temperature de=Temperatur unit=C", std::string("\x30\xF8\xFF\xFF", 4), "-2000 C"},
    {"30 text unavailable ro en=Unknown state de=Unbekannter Status", "-", "-"},
  };
  bool first = true;
  std::cout << '[';
  for (const Case& c : cases) {
    const Definition def = definition(c.line);
    char raw[33], display[64];
    rawText((const uint8_t*)c.raw.data(), c.raw.size(), raw, sizeof(raw), valueEncoding(def));
    displayText(def, text(raw), display, sizeof(display));
    if (!first) std::cout << ','; first = false;
    std::cout << "{\"line\":" << quote(c.line) << ",\"expected\":" << quote(c.expected)
              << ",\"label_en\":" << quote(own(label(def, false)))
              << ",\"label_de\":" << quote(own(label(def, true)))
              << ",\"display_wire\":" << quote(display)
              << ",\"state\":{\"id\":" << (unsigned)def.id << ",\"ascii\":" << quote(raw);
    if (std::string(display) != raw) std::cout << ",\"display_text\":" << quote(display);
    if (c.raw.size() == 1) std::cout << ",\"value\":" << (boolValue(text(raw)) ? 1 : 0);
    if (equal(def.type, "switch")) std::cout << ",\"value_bool\":" << (boolValue(text(raw)) ? "true" : "false");
    if (equal(def.type, "select")) {
      uint8_t selected = 0;
      if (selectIndex(def, text(raw), selected)) std::cout << ",\"selected_value\":" << quote(own(selectValue(def, selected)));
    }
    std::cout << "}}";
  }
  std::cout << "]\n";
}
int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--fixtures") { fixtures(); return 0; }
  test_metadata(); test_payloads(); test_values(); benchmark();
  std::cout << "Entity presentation regressions passed\n";
}
