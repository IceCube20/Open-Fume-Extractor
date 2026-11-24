#include "../../experiments/C6_PoE_2131_Trial/TrialPolicy.h"
#include <cassert>
#include <cstring>
#include <vector>
#include <cstdio>

struct IO {
  bool verified = true, correct_board = true, ethernet = true, known = true, marked = true;
  bool begin_ok = true, end_ok = true, activate_ok = true;
  uint32_t current = c6_trial::BASELINE.version, time = 0, step = 1;
  unsigned fail_write = 0, lose_lan = 0, lan_checks = 0, writes = 0, begins = 0, ends = 0, activations = 0;
  uint32_t written = 0;
  bool verify(const c6_trial::Image&, const uint8_t*) { return verified; }
  bool board() { return correct_board; }
  bool lan() { return ethernet && (!lose_lan || ++lan_checks < lose_lan); }
  bool version(uint32_t& v) { v = current; return known; }
  bool mark(uint32_t) { return marked; }
  uint32_t now() { return time; }
  bool begin() { ++begins; return begin_ok; }
  bool write(const uint8_t*, uint32_t count) { ++writes; written += count; return writes != fail_write; }
  bool end() { ++ends; return end_ok; }
  bool activate() { ++activations; return activate_ok; }
  void yield() { time += step; }
  void progress(uint32_t done, uint32_t total) { assert(done <= total && done == written); }
};

int main() {
  std::vector<uint8_t> image(c6_trial::TEST.size);
  auto run = [&](IO& io, const c6_trial::Image& target = c6_trial::TEST) {
    return c6_trial::transfer(io, target, image.data());
  };
  IO good; assert(!strcmp(run(good), "OK"));
  assert(good.written == c6_trial::TEST.size && good.ends == 1 && good.activations == 1);
  IO restore; restore.current = c6_trial::TEST.version;
  assert(!strcmp(run(restore, c6_trial::BASELINE), "OK"));
  assert(restore.written == c6_trial::BASELINE.size && restore.activations == 1);
  for (unsigned mode = 0; mode < 7; ++mode) {
    IO io;
    if (mode == 0) io.verified = false;
    if (mode == 1) io.correct_board = false;
    if (mode == 2) io.ethernet = false;
    if (mode == 3) io.known = false;
    if (mode == 4) io.current = 0x020D02;
    if (mode == 5) io.current = c6_trial::TEST.version;
    if (mode == 6) io.marked = false;
    assert(strcmp(run(io), "OK") && !io.begins && !io.activations);
  }
  auto fake = c6_trial::TEST; IO unknown;
  assert(!strcmp(run(unknown, fake), "UNKNOWN_TARGET") && !unknown.begins);
  IO no_image; assert(!strcmp(c6_trial::transfer(no_image, c6_trial::TEST, nullptr), "IMAGE_NOT_VERIFIED"));
  for (unsigned mode = 0; mode < 6; ++mode) {
    IO io;
    if (mode == 0) io.begin_ok = false;
    if (mode == 1) io.fail_write = 2;
    if (mode == 2) io.lose_lan = 5;
    if (mode == 3) io.step = 180000;
    if (mode == 4) io.end_ok = false;
    if (mode == 5) io.activate_ok = false;
    assert(strcmp(run(io), "OK") && io.begins == 1);
    if (mode < 5) assert(!io.activations);
    if (mode < 4) assert(!io.ends);
  }
  IO wrap; wrap.time = 0xFFFFFF00U;
  assert(!strcmp(run(wrap), "OK") && wrap.activations == 1);
  puts("PASS: isolated C6 trial policy: two pinned targets, explicit restoration, host/board/hash/known-version gates, failures never activate, timeout and timer wrap");
}
