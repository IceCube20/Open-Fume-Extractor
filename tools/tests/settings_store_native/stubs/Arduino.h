#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>
#include <type_traits>

class String {
  std::string value_;
public:
  String() = default;
  String(const char* s) : value_(s ? s : "") {}
  String(const std::string& s) : value_(s) {}
  template<class T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  String(T n) : value_(std::to_string(n)) {}
  const char* c_str() const { return value_.c_str(); }
  size_t length() const { return value_.size(); }
  friend bool operator==(const String& a, const String& b) { return a.value_ == b.value_; }
  friend String operator+(const String& a, const String& b) { return String(a.value_ + b.value_); }
};
