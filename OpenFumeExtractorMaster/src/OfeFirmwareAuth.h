#pragma once

#include <Arduino.h>
#include <mbedtls/sha256.h>
#include <sodium.h>

#include "OfeFirmwareAuthKey.h"

class OfeFirmwareAuthVerifier {
public:
  OfeFirmwareAuthVerifier() {
    mbedtls_sha256_init(&sha_);
    reset();
  }

  ~OfeFirmwareAuthVerifier() {
    mbedtls_sha256_free(&sha_);
  }

  void reset() {
    mbedtls_sha256_free(&sha_);
    mbedtls_sha256_init(&sha_);
    active_ = false;
    finished_ = false;
    expected_size_ = 0;
    received_size_ = 0;
    canonical_[0] = 0;
    error_[0] = 0;
    memset(expected_digest_, 0, sizeof(expected_digest_));
    memset(signature_, 0, sizeof(signature_));
  }

  bool begin(const String& trailer, uint32_t expected_size, const char* expected_target) {
    reset();
    if (!expected_target || !expected_target[0]) return fail("Missing expected target");
    if (trailer.length() < 220 || trailer.length() > 420) return fail("Missing OFE Ed25519 trailer");

    String target, version, size_text, digest_hex, key_id, signature_hex;
    if (!field(trailer, "OFE_FW_AUTH:v1;target=", ";version=", target) ||
        !field(trailer, ";version=", ";size=", version) ||
        !field(trailer, ";size=", ";sha256=", size_text) ||
        !field(trailer, ";sha256=", ";keyid=", digest_hex) ||
        !field(trailer, ";keyid=", ";sig=", key_id) ||
        !field(trailer, ";sig=", ";", signature_hex, true)) {
      return fail("Malformed OFE Ed25519 trailer");
    }
    if (target != expected_target) return fail("Signed firmware target mismatch");
    if (!valid_token(target, 31) || !valid_version(version, 31)) return fail("Invalid signed metadata");
    if (key_id != OFE_FW_AUTH_KEY_ID) return fail("Unknown OFE signing key");

    char* end = nullptr;
    const unsigned long parsed_size = strtoul(size_text.c_str(), &end, 10);
    if (!end || *end || parsed_size == 0 || parsed_size > UINT32_MAX) return fail("Invalid signed image size");
    if ((uint32_t)parsed_size != expected_size) return fail("Signed image size mismatch");
    if (!decode_hex(digest_hex, expected_digest_, sizeof(expected_digest_)) ||
        !decode_hex(signature_hex, signature_, sizeof(signature_))) {
      return fail("Invalid OFE signature encoding");
    }

    const int n = snprintf(canonical_, sizeof(canonical_),
      "OFE_FW_AUTH:v1;target=%s;version=%s;size=%lu;sha256=%s;keyid=%s;",
      target.c_str(), version.c_str(), (unsigned long)parsed_size,
      digest_hex.c_str(), key_id.c_str());
    if (n <= 0 || (size_t)n >= sizeof(canonical_)) return fail("Signed metadata too long");
    if (mbedtls_sha256_starts(&sha_, 0) != 0) return fail("SHA-256 init failed");
    if (sodium_init() < 0) return fail("Ed25519 init failed");

    expected_size_ = (uint32_t)parsed_size;
    active_ = true;
    return true;
  }

  bool update(const uint8_t* data, size_t len) {
    if (!active_ || finished_) return fail("Firmware auth not active");
    if ((!data && len) || len > expected_size_ - received_size_) return fail("Signed image exceeds size");
    if (len && mbedtls_sha256_update(&sha_, data, len) != 0) return fail("SHA-256 update failed");
    received_size_ += (uint32_t)len;
    return true;
  }

  bool finish() {
    if (!active_ || finished_) return fail("Firmware auth not active");
    finished_ = true;
    if (received_size_ != expected_size_) return fail("Signed image size incomplete");

    uint8_t actual_digest[32];
    if (mbedtls_sha256_finish(&sha_, actual_digest) != 0) return fail("SHA-256 finish failed");
    if (memcmp(actual_digest, expected_digest_, sizeof(actual_digest)) != 0) return fail("SHA-256 mismatch");
    if (crypto_sign_ed25519_verify_detached(
          signature_, reinterpret_cast<const unsigned char*>(canonical_),
          (unsigned long long)strlen(canonical_), OFE_FW_AUTH_PUBLIC_KEY) != 0) {
      return fail("Ed25519 signature invalid");
    }
    return true;
  }

  const char* error() const { return error_[0] ? error_ : "Firmware authentication failed"; }
  uint32_t received() const { return received_size_; }

private:
  static bool field(const String& input, const char* start_token, const char* end_token,
                    String& out, bool require_end = false) {
    const int start_at = input.indexOf(start_token);
    if (start_at < 0) return false;
    const int value_at = start_at + (int)strlen(start_token);
    const int end_at = input.indexOf(end_token, value_at);
    if (end_at < value_at) return false;
    if (require_end && end_at + (int)strlen(end_token) != (int)input.length()) return false;
    out = input.substring(value_at, end_at);
    return out.length() > 0;
  }

  static bool valid_token(const String& value, size_t max_len) {
    if (!value.length() || value.length() > max_len) return false;
    for (size_t i = 0; i < value.length(); ++i) {
      const char c = value[i];
      if (!(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '_') return false;
    }
    return true;
  }

  static bool valid_version(const String& value, size_t max_len) {
    if (!value.length() || value.length() > max_len) return false;
    for (size_t i = 0; i < value.length(); ++i) {
      const char c = value[i];
      if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
          !(c >= '0' && c <= '9') && c != '.' && c != '-' && c != '_') return false;
    }
    return true;
  }

  static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  static bool decode_hex(const String& text, uint8_t* out, size_t out_len) {
    if (!out || text.length() != out_len * 2U) return false;
    for (size_t i = 0; i < out_len; ++i) {
      const int hi = hex_value(text[i * 2U]);
      const int lo = hex_value(text[i * 2U + 1U]);
      if (hi < 0 || lo < 0) return false;
      out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
  }

  bool fail(const char* message) {
    strlcpy(error_, message ? message : "Firmware authentication failed", sizeof(error_));
    return false;
  }

  mbedtls_sha256_context sha_;
  bool active_ = false;
  bool finished_ = false;
  uint32_t expected_size_ = 0;
  uint32_t received_size_ = 0;
  uint8_t expected_digest_[32] = {0};
  uint8_t signature_[64] = {0};
  char canonical_[256] = {0};
  char error_[96] = {0};
};
