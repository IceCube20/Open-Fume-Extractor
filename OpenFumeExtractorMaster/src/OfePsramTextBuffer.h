#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>

// Growable text buffer for large, cold-path JSON/HTTP/MQTT payloads.
// Unlike Arduino String::reserve(), this class explicitly selects PSRAM via
// heap_caps_realloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT). It is intended
// for web/HA serialization only; real-time bus and scheduler data stay in DRAM.
class OfePsramTextBuffer {
 public:
  OfePsramTextBuffer() = default;
  ~OfePsramTextBuffer() { reset(); }

  OfePsramTextBuffer(const OfePsramTextBuffer&) = delete;
  OfePsramTextBuffer& operator=(const OfePsramTextBuffer&) = delete;

  OfePsramTextBuffer(OfePsramTextBuffer&& other) noexcept { moveFrom(other); }
  OfePsramTextBuffer& operator=(OfePsramTextBuffer&& other) noexcept {
    if (this != &other) {
      reset();
      moveFrom(other);
    }
    return *this;
  }

  bool reservePsram(size_t capacity) {
    return reserveWithCaps(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, true);
  }

  bool reserveInternal(size_t capacity) {
    return reserveWithCaps(capacity, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, false);
  }

  // Prefer a large PSRAM allocation. If external RAM is unavailable or the
  // allocation fails, keep a smaller but functional internal-DRAM fallback.
  bool reservePreferred(size_t psram_capacity, size_t internal_fallback_capacity) {
    if (reservePsram(psram_capacity)) return true;
    return reserveInternal(internal_fallback_capacity);
  }

  const char* c_str() const { return buffer_ ? buffer_ : ""; }
  char* data() { return buffer_; }
  size_t length() const { return length_; }
  size_t capacity() const { return capacity_; }
  bool empty() const { return length_ == 0; }
  bool ok() const { return ok_; }
  bool usesPsram() const { return uses_psram_; }

  void clear() {
    length_ = 0;
    if (buffer_) buffer_[0] = 0;
    ok_ = true;
  }

  OfePsramTextBuffer& operator+=(const String& value) {
    append(value.c_str(), value.length());
    return *this;
  }

  OfePsramTextBuffer& operator+=(const char* value) {
    if (value) append(value, strlen(value));
    return *this;
  }

  OfePsramTextBuffer& operator+=(const __FlashStringHelper* value) {
    if (!value) return *this;
    PGM_P p = reinterpret_cast<PGM_P>(value);
    const size_t n = strlen_P(p);
    if (!ensure(length_ + n)) return *this;
    memcpy_P(buffer_ + length_, p, n);
    length_ += n;
    buffer_[length_] = 0;
    return *this;
  }

  OfePsramTextBuffer& operator+=(char value) {
    append(&value, 1);
    return *this;
  }

  OfePsramTextBuffer& operator+=(signed char value) { return appendSigned((long long)value); }
  OfePsramTextBuffer& operator+=(unsigned char value) { return appendUnsigned((unsigned long long)value); }
  OfePsramTextBuffer& operator+=(short value) { return appendSigned((long long)value); }
  OfePsramTextBuffer& operator+=(unsigned short value) { return appendUnsigned((unsigned long long)value); }
  OfePsramTextBuffer& operator+=(int value) { return appendSigned((long long)value); }
  OfePsramTextBuffer& operator+=(unsigned int value) { return appendUnsigned((unsigned long long)value); }
  OfePsramTextBuffer& operator+=(long value) { return appendSigned((long long)value); }
  OfePsramTextBuffer& operator+=(unsigned long value) { return appendUnsigned((unsigned long long)value); }
  OfePsramTextBuffer& operator+=(long long value) { return appendSigned(value); }
  OfePsramTextBuffer& operator+=(unsigned long long value) { return appendUnsigned(value); }

  OfePsramTextBuffer& operator+=(float value) { return appendFloat((double)value, 2); }
  OfePsramTextBuffer& operator+=(double value) { return appendFloat(value, 2); }

 private:
  char* buffer_ = nullptr;
  size_t length_ = 0;
  size_t capacity_ = 0;
  uint32_t caps_ = 0;
  bool uses_psram_ = false;
  bool ok_ = true;

  void reset() {
    if (buffer_) free(buffer_);
    buffer_ = nullptr;
    length_ = 0;
    capacity_ = 0;
    caps_ = 0;
    uses_psram_ = false;
    ok_ = true;
  }

  void moveFrom(OfePsramTextBuffer& other) {
    buffer_ = other.buffer_;
    length_ = other.length_;
    capacity_ = other.capacity_;
    caps_ = other.caps_;
    uses_psram_ = other.uses_psram_;
    ok_ = other.ok_;
    other.buffer_ = nullptr;
    other.length_ = 0;
    other.capacity_ = 0;
    other.caps_ = 0;
    other.uses_psram_ = false;
    other.ok_ = true;
  }

  bool reserveWithCaps(size_t capacity, uint32_t caps, bool psram) {
    if (capacity <= capacity_ && buffer_ && caps_ == caps) return true;
    if (buffer_ && caps_ != caps) return false;
    if (capacity > SIZE_MAX - 1U) {
      ok_ = false;
      return false;
    }
    const size_t alloc_size = capacity + 1U;
    void* next = heap_caps_realloc(buffer_, alloc_size, caps);
    if (!next) return false;
    buffer_ = static_cast<char*>(next);
    capacity_ = capacity;
    caps_ = caps;
    uses_psram_ = psram;
    if (length_ > capacity_) length_ = capacity_;
    buffer_[length_] = 0;
    return true;
  }

  bool ensure(size_t required) {
    if (!ok_) return false;
    if (required <= capacity_) return true;
    size_t next_capacity = capacity_ ? capacity_ : 256U;
    while (next_capacity < required) {
      const size_t grown = next_capacity + (next_capacity >> 1) + 64U;
      if (grown <= next_capacity) {
        next_capacity = required;
        break;
      }
      next_capacity = grown;
    }
    // Avoid lots of tiny reallocations when appending long JSON documents.
    next_capacity = (next_capacity + 255U) & ~(size_t)255U;
    const uint32_t caps = caps_ ? caps_ : (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const bool psram = caps_ ? uses_psram_ : false;
    if (!reserveWithCaps(next_capacity, caps, psram)) {
      ok_ = false;
      return false;
    }
    return true;
  }

  bool append(const char* value, size_t n) {
    if (!value || n == 0) return ok_;
    if (n > SIZE_MAX - length_) {
      ok_ = false;
      return false;
    }
    if (!ensure(length_ + n)) return false;
    memcpy(buffer_ + length_, value, n);
    length_ += n;
    buffer_[length_] = 0;
    return true;
  }

  OfePsramTextBuffer& appendSigned(long long value) {
    char tmp[32];
    const int n = snprintf(tmp, sizeof(tmp), "%lld", value);
    if (n > 0) append(tmp, (size_t)n);
    return *this;
  }

  OfePsramTextBuffer& appendUnsigned(unsigned long long value) {
    char tmp[32];
    const int n = snprintf(tmp, sizeof(tmp), "%llu", value);
    if (n > 0) append(tmp, (size_t)n);
    return *this;
  }

  OfePsramTextBuffer& appendFloat(double value, unsigned int decimals) {
    char tmp[40];
    char fmt[12];
    snprintf(fmt, sizeof(fmt), "%%.%uf", decimals);
    const int n = snprintf(tmp, sizeof(tmp), fmt, value);
    if (n > 0) append(tmp, (size_t)n);
    return *this;
  }
};
