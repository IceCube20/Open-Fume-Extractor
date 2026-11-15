#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>

class String {
  std::string value_;
 public:
  String() = default;
  String(const char* value) : value_(value ? value : "") {}
  explicit String(std::string value) : value_(std::move(value)) {}
  const char* c_str() const { return value_.c_str(); }
  size_t length() const { return value_.length(); }
  bool reserve(size_t length) { value_.reserve(length); return true; }
  bool startsWith(const String& prefix) const { return value_.compare(0, prefix.length(), prefix.value_) == 0; }
  String substring(size_t start) const { return String(value_.substr(start)); }
  void trim() {
    const auto begin = value_.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) { value_.clear(); return; }
    value_ = value_.substr(begin, value_.find_last_not_of(" \t\r\n") - begin + 1);
  }
  bool endsWith(const char* suffix) const {
    const size_t n = strlen(suffix);
    return n <= value_.size() && value_.compare(value_.size() - n, n, suffix) == 0;
  }
  void remove(size_t index) { value_.erase(index); }
  void toCharArray(char* out, size_t length) const {
    if (length) std::snprintf(out, length, "%s", value_.c_str());
  }
  String& operator+=(const String& other) { value_ += other.value_; return *this; }
  String& operator+=(char other) { value_ += other; return *this; }
  template<class T, typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, char>::value, int>::type = 0>
  String& operator+=(T other) { value_ += std::to_string(other); return *this; }
  friend bool operator==(const String& a, const String& b) { return a.value_ == b.value_; }
  friend String operator+(const String& a, const String& b) { return String(a.value_ + b.value_); }
};
