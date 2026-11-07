#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace ofe_fae_identity {
static const char FIRMWARE[] = "02:F2:8886791:0023324:F1_DIS_02";
static_assert(sizeof(FIRMWARE) - 1 == 31, "FAE firmware payload excludes its terminator");
static const size_t ID_MAX = 32;
static const size_t LEGACY_ID_MAX = 64;

inline bool validWritableId(const uint8_t* data, size_t len) {
  if (!data || !len || len > ID_MAX) return false;
  for (size_t i = 0; i < len; ++i) {
    if (data[i] < 0x20 || data[i] > 0x7E) return false;
  }
  return true;
}

inline void hardwareId(uint64_t uid, uint8_t out[8]) {
  for (size_t i = 0; i < 8; ++i) out[i] = uint8_t(uid >> (56 - 8 * i));
}

// The hardware prefix prevents different boards sharing the old fixed default.
// This identifier is not a security token; randomness is only used at first boot.
inline void initialId(uint64_t uid, uint32_t random_a, uint32_t random_b, uint8_t out[ID_MAX]) {
  char text[ID_MAX + 1];
  snprintf(text, sizeof(text), "%016llX%08lX%08lX",
           (unsigned long long)uid, (unsigned long)random_a, (unsigned long)random_b);
  memcpy(out, text, ID_MAX);
}

template<class Store>
bool load(Store& store, uint8_t (&data)[LEGACY_ID_MAX], uint8_t& len,
          uint64_t uid, uint32_t random_a, uint32_t random_b) {
  const size_t saved = store.getBytesLength("devid");
  len = 0;
  if (saved) {
    // Never replace an existing ID merely because a read failed or an old
    // build accepted a longer payload.
    if (saved > sizeof(data) || store.getBytes("devid", data, sizeof(data)) != saved) return false;
    len = uint8_t(saved);
    return true;
  }
  initialId(uid, random_a, random_b, data);
  len = ID_MAX;
  return store.putBytes("devid", data, len) == len;
}

template<class Store>
bool save(Store& store, uint8_t (&current)[LEGACY_ID_MAX], uint8_t& current_len,
          const uint8_t* data, size_t len) {
  if (!validWritableId(data, len) || store.putBytes("devid", data, len) != len) return false;
  memcpy(current, data, len);
  current_len = uint8_t(len);
  return true;
}
} // namespace ofe_fae_identity
