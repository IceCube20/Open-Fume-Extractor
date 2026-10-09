#pragma once
#include "Arduino.h"
#include <map>
#include <vector>
#include <cstring>

class Preferences {
public:
  enum class Kind { U8, U16, Text, Blob };
  struct Entry { Kind kind; uint16_t number = 0; String text; std::vector<uint8_t> blob; };
  inline static std::map<std::string, std::map<std::string, Entry>> db;
  inline static size_t writes = 0;
  inline static size_t reject_write = 0;
  inline static size_t drop_write = 0;
  inline static bool fail_begin = false;
  inline static bool fail_reads = false;
private:
  std::string ns_;
  bool active_ = false;
  bool readonly_ = false;
  const Entry* entry(const char* key) const {
    if (!active_ || fail_reads) return nullptr;
    const auto ns = db.find(ns_);
    if (ns == db.end()) return nullptr;
    const auto it = ns->second.find(key);
    return it == ns->second.end() ? nullptr : &it->second;
  }
  size_t put(const char* key, const Entry& value, size_t result) {
    if (!active_ || readonly_) return 0;
    ++writes;
    if (writes == reject_write) return 0;
    if (writes != drop_write) db[ns_].insert_or_assign(key, value);
    return result;
  }
public:
  static void reset() { db.clear(); writes = reject_write = drop_write = 0; fail_begin = fail_reads = false; }
  bool begin(const char* ns, bool readonly = false) {
    if (active_ || fail_begin) return false;
    ns_ = ns; active_ = true; readonly_ = readonly; return true;
  }
  void end() { active_ = false; }
  bool active() const { return active_; }
  bool isKey(const char* key) const { return entry(key) != nullptr; }
  bool remove(const char* key) { return active_ && !readonly_ && db[ns_].erase(key) != 0; }
  uint8_t getUChar(const char* key, uint8_t fallback = 0) const {
    const auto* e = entry(key); return e && e->kind == Kind::U8 ? (uint8_t)e->number : fallback;
  }
  uint16_t getUShort(const char* key, uint16_t fallback = 0) const {
    const auto* e = entry(key); return e && e->kind == Kind::U16 ? e->number : fallback;
  }
  bool getBool(const char* key, bool fallback = false) const { return getUChar(key, fallback ? 1 : 0) != 0; }
  String getString(const char* key, const String& fallback = String("")) const {
    const auto* e = entry(key); return e && e->kind == Kind::Text ? e->text : fallback;
  }
  size_t getBytesLength(const char* key) const {
    const auto* e = entry(key); return e && e->kind == Kind::Blob ? e->blob.size() : 0;
  }
  size_t getBytes(const char* key, void* data, size_t size) const {
    const auto* e = entry(key);
    if (!e || e->kind != Kind::Blob || e->blob.size() > size) return 0;
    memcpy(data, e->blob.data(), e->blob.size()); return e->blob.size();
  }
  size_t putUChar(const char* key, uint8_t n) { Entry e{Kind::U8}; e.number = n; return put(key, e, 1); }
  size_t putUShort(const char* key, uint16_t n) { Entry e{Kind::U16}; e.number = n; return put(key, e, 2); }
  size_t putBool(const char* key, bool n) { return putUChar(key, n ? 1 : 0); }
  size_t putString(const char* key, const String& s) { Entry e{Kind::Text}; e.text = s; return put(key, e, s.length()); }
  size_t putBytes(const char* key, const void* data, size_t size) {
    Entry e{Kind::Blob}; const auto* b = (const uint8_t*)data; e.blob.assign(b, b + size); return put(key, e, size);
  }
};
