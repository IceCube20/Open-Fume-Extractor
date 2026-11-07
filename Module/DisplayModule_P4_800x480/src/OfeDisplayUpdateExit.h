#pragma once
#include <atomic>
#include <stdint.h>

// Transport tasks only post a request; timing and navigation belong to the GUI.
class OfeDisplayUpdateExit {
 public:
  enum class Action { None, ShowAborted, Home };
  static constexpr uint32_t HoldMs = 10000;
  void requestAbort() { pending_.store(true); }
  void cancel() { armed_ = false; }
  bool canReturn(bool updating) const { return armed_ && !updating; }
  Action poll(uint32_t now, bool updating, bool onResultScreen) {
    const bool requested = pending_.exchange(false);
    if (updating) { cancel(); return Action::None; }
    if (requested) { armed_ = true; shown_ms_ = now; return Action::ShowAborted; }
    if (!armed_) return Action::None;
    if (!onResultScreen) { cancel(); return Action::None; }
    if (uint32_t(now - shown_ms_) < HoldMs) return Action::None;
    cancel();
    return Action::Home;
  }
 private:
  std::atomic<bool> pending_{false};
  bool armed_ = false;
  uint32_t shown_ms_ = 0;
};
