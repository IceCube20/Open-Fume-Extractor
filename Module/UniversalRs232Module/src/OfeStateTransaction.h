#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>

namespace ofe_profile {
struct StateRegion { void* data; size_t size; };

template<class T> StateRegion region(T& value) {
  static_assert(std::is_trivially_copyable<T>::value, "Snapshot state must not own dynamic memory");
  return {&value, sizeof(value)};
}

// Parsing uses the existing globals, but every early return restores the live
// profile. The large entity snapshot is allocated on the heap, never the stack.
class StateTransaction {
 public:
  StateTransaction(const StateRegion* regions, size_t count,
                   void* (*allocate)(size_t) = malloc)
      : regions_(regions), count_(count) {
    size_t total = 0;
    for (size_t i = 0; i < count; ++i) {
      if (regions[i].size > SIZE_MAX - total) return;
      total += regions[i].size;
    }
    snapshot_ = static_cast<uint8_t*>(allocate(total));
    if (!snapshot_) return;
    size_t offset = 0;
    for (size_t i = 0; i < count; ++i) {
      memcpy(snapshot_ + offset, regions[i].data, regions[i].size);
      offset += regions[i].size;
    }
  }
  ~StateTransaction() {
    if (snapshot_ && !committed_) {
      size_t offset = 0;
      for (size_t i = 0; i < count_; ++i) {
        memcpy(regions_[i].data, snapshot_ + offset, regions_[i].size);
        offset += regions_[i].size;
      }
    }
    free(snapshot_);
  }
  StateTransaction(const StateTransaction&) = delete;
  StateTransaction& operator=(const StateTransaction&) = delete;
  bool valid() const { return snapshot_ != nullptr; }
  void commit() { committed_ = true; }
 private:
  const StateRegion* regions_;
  size_t count_;
  uint8_t* snapshot_ = nullptr;
  bool committed_ = false;
};
}

