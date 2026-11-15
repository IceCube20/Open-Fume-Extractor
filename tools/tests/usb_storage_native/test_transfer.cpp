#include "UsbStoragePolicy.h"
#include <cassert>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <string>

struct String : std::string {
  using std::string::string;
  void toCharArray(char* out, size_t n) const { snprintf(out, n, "%s", c_str()); }
};
enum class Phase { Idle, Queued, Reading, Ready, Receiving, Writing, Error };
struct State {
  Phase phase = Phase::Idle;
  uint32_t generation = 7, token = 0, touched = 0;
  size_t count = 1, selected = 0, size = 0, received = 0;
  char operation[12]{}, filename[ofe_usb::MAX_NAME]{};
  uint8_t* buffer = nullptr;
};
State fixture;
State* state = &fixture;
bool mutex = true, failAllocation = false;
std::atomic<bool> ready{true};
struct Guard {};
constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
void* heap_caps_malloc(size_t bytes, unsigned caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  return failAllocation ? nullptr : malloc(bytes);
}
unsigned millis() { return 100; }
unsigned esp_random() { return 42; }
size_t strlcpy(char* out, const char* text, size_t n) { if (n) snprintf(out, n, "%s", text); return strlen(text); }
void freeBuffer() { free(state->buffer); state->buffer = nullptr; state->token = 0; state->size = state->received = 0; }
bool busy() { return state->phase == Phase::Queued || state->phase == Phase::Reading ||
  state->phase == Phase::Writing || state->phase == Phase::Receiving || state->buffer; }
using namespace ofe_usb;
#include "transfer.h"

int main() {
  assert(!command("read", 6, 0));
  assert(!command("read", 7, 1));
  assert(!command("format", 7, 0));
  assert(command("read", 7, 0));
  assert(!command("eject", 0, 0));
  state->phase = Phase::Idle;
  assert(receiveBegin("../bad.bin", 10) == 0);
  assert(receiveBegin("test.bin", MAX_FILE_BYTES+1) == 0);
  failAllocation = true;
  assert(receiveBegin("test.bin", 4) == 0);
  failAllocation = false;
  uint32_t token = receiveBegin("test.bin", 4);
  assert(token && !command("refresh", 0, 0));
  assert(!receiveCommit(token));
  const uint8_t data[]{1, 2, 3, 4}, different[]{9, 9};
  assert(!receiveChunk(token+1, 0, data, 2));
  assert(!receiveChunk(token, 1, data, 2));
  assert(!receiveChunk(token, 0, data, 5));
  assert(receiveChunk(token, 0, data, 2));
  assert(receiveChunk(token, 0, data, 2));
  assert(!receiveChunk(token, 0, different, 2));
  assert(state->received == 2);
  assert(!receiveCommit(token));
  assert(receiveChunk(token, 2, data+2, 2));
  assert(receiveCommit(token));
  assert(!release(token));
  state->phase = Phase::Writing;
  assert(!release(token));
  state->phase = Phase::Error;
  assert(release(token));
  assert(!state->buffer && !state->token);
  ready = false;
  assert(!command("read", 7, 0));
  assert(!receiveBegin("test.bin", 4));
  ready = true;
  token = receiveBegin("test.bin", 4);
  assert(receiveChunk(token, 0, data, 4));
  ready = false;
  assert(!receiveCommit(token));
  assert(release(token));
  state->buffer = static_cast<uint8_t*>(malloc(4)); state->token = 9; state->phase = Phase::Ready;
  assert(!release(8)); assert(release(9));
  puts("PASS: actual USB transfer functions: stale selection, PSRAM failure, token/offset/bounds, duplicate identity, incomplete commit, active-write ownership, disconnect and cleanup");
}
