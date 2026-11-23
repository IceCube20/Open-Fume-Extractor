#pragma once

#include "OfePsramTextBuffer.h"

// HTTP-task owned. Keys contain exact, length-prefixed fields, not hashes or
// timestamps. Only immutable descriptions/options belong in these fragments.
class HaMetadataKey {
 public:
  explicit HaMetadataKey(const OfePsramTextBuffer& expected) : expected_(&expected) {}
  explicit HaMetadataKey(OfePsramTextBuffer* capture) : capture_(capture) {}

  template <typename T>
  void scalar(T value) {
    static_assert(std::is_arithmetic<T>::value || std::is_enum<T>::value, "scalar fields only");
    bytes(reinterpret_cast<const char*>(&value), sizeof(value));
  }

  void text(const char* value) {
    const size_t length = value ? strlen(value) : 0;
    scalar(length);
    bytes(value, length);
  }

  bool matches() const { return equal_ && expected_ && offset_ == expected_->length(); }

 private:
  const OfePsramTextBuffer* expected_ = nullptr;
  OfePsramTextBuffer* capture_ = nullptr;
  size_t offset_ = 0;
  bool equal_ = true;

  void bytes(const char* value, size_t length) {
    if (capture_) { capture_->appendBytes(value, length); return; }
    if (!equal_) return;
    if (offset_ > expected_->length() || length > expected_->length() - offset_ ||
        (length && memcmp(expected_->c_str() + offset_, value, length))) {
      equal_ = false;
      return;
    }
    offset_ += length;
  }
};

class HaMetadataCache {
 public:
  template <typename KeyWriter, typename Builder>
  void appendTo(OfePsramTextBuffer& destination, KeyWriter write_key, Builder build) {
    if (valid_) {
      HaMetadataKey check(key_);
      write_key(check);
      if (check.matches()) { destination += value_.c_str(); return; }
    }
    valid_ = false;
    key_.clear();
    value_.clear();
    // Never consume internal RAM for this optional optimization.
    if (key_.reservePsram(256) && value_.reservePsram(256)) {
      HaMetadataKey capture(&key_);
      write_key(capture);
      if (key_.ok()) {
        build(value_);
        HaMetadataKey check(key_);
        write_key(check);
        if (value_.ok() && check.matches()) {
          valid_ = true;
          destination += value_.c_str();
          return;
        }
      }
    }
    // A failed allocation or a concurrent configuration change must never
    // resurrect a previous module's fragment. Use the ordinary fresh builder.
    key_ = OfePsramTextBuffer();
    value_ = OfePsramTextBuffer();
    build(destination);
  }

 private:
  OfePsramTextBuffer key_;
  OfePsramTextBuffer value_;
  bool valid_ = false;
};
