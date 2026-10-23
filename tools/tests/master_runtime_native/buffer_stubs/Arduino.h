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
  explicit String(const char* value) : value_(value ? value : "") { ++constructions; }
  const char* c_str() const { return value_.c_str(); }
  size_t length() const { return value_.size(); }
 private:
  std::string value_;
};
