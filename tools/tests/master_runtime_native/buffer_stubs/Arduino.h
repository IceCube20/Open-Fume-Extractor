#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
struct __FlashStringHelper;
using PGM_P = const char*;
#define strlen_P strlen
#define memcpy_P memcpy
class String {
 public:
  inline static unsigned constructions=0;
  inline static unsigned concat_calls=0,reserve_calls=0;
  inline static bool fail_reserve=false,fail_concat=false;
  String() { ++constructions; }
  explicit String(const char* value) : value_(value ? value : "") { ++constructions; }
  bool reserve(size_t length) { ++reserve_calls; if(fail_reserve)return false;value_.reserve(length);return true; }
  bool concat(const char* text,size_t length) { ++concat_calls; if(fail_concat)return false;value_.append(text,length);return true; }
  const char* c_str() const { return value_.c_str(); }
  size_t length() const { return value_.size(); }
 private:
  std::string value_;
};
