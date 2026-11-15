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
  String& operator+=(const String& other) { value_+=other.value_;return *this; }
  String& operator+=(char other) { value_+=other;return *this; }
  template<class T,typename std::enable_if<std::is_integral<T>::value && !std::is_same<T,char>::value,int>::type=0>
  String& operator+=(T other) {value_+=std::to_string(other);return *this;}
  friend bool operator==(const String& a, const String& b) { return a.value_ == b.value_; }
  friend String operator+(const String& a, const String& b) { return String(a.value_ + b.value_); }
};

class IPAddress {
  uint8_t bytes_[4]={};
public:
  IPAddress()=default;
  IPAddress(uint8_t a,uint8_t b,uint8_t c,uint8_t d):bytes_{a,b,c,d} {}
  explicit IPAddress(uint32_t value) { for(unsigned i=0;i<4;++i)bytes_[i]=uint8_t(value>>(8*i)); }
  uint8_t operator[](size_t i) const { return bytes_[i]; }
  uint8_t& operator[](size_t i) { return bytes_[i]; }
  operator uint32_t() const { return uint32_t(bytes_[0])|uint32_t(bytes_[1])<<8|uint32_t(bytes_[2])<<16|uint32_t(bytes_[3])<<24; }
  bool operator==(const IPAddress& other) const { return uint32_t(*this)==uint32_t(other); }
  bool operator!=(const IPAddress& other) const { return !(*this==other); }
  String toString() const {
    char text[16];std::snprintf(text,sizeof(text),"%u.%u.%u.%u",bytes_[0],bytes_[1],bytes_[2],bytes_[3]);return String(text);
  }
};
