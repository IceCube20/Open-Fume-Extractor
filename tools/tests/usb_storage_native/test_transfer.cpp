#include "UsbStoragePolicy.h"
#include "JsonTextEscape.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <functional>
#include <limits>
#include <map>
#include <new>
#include <string>
#include <sys/types.h>
#include <vector>

using namespace ofe_usb;
#include "msc_types.h"
#include "manager_state.h"
State fixture;
State* state = &fixture;
bool mutex = true, workerContext = false, pausePump = false, failAllocation = false;
std::atomic<bool> present{true}, ready{true}, gone{false};
#include "drive_globals.h"
unsigned guardDepth = 0, clockValue = 0, randomValue = 40, refreshCalls = 0;
size_t allocated = 0, peakAllocated = 0, largestAllocation = 0;
std::map<void*, size_t> allocations;
struct Guard { Guard() { assert(!guardDepth); ++guardDepth; } ~Guard() { --guardDepth; } };
struct String : std::string {
  using std::string::string;
  using std::string::operator=;
  using std::string::operator+=;
  String& operator+=(unsigned value) { append(std::to_string(value)); return *this; }
  String& operator+=(uint16_t value) { append(std::to_string(value)); return *this; }
  bool concat(const char* data, size_t count) { append(data, count); return true; }
  void toCharArray(char* out, size_t count) const { snprintf(out, count, "%s", c_str()); }
};
size_t strlcpy(char* out, const char* in, size_t count) {
  if (count) snprintf(out, count, "%s", in);
  return strlen(in);
}
unsigned millis() { return clockValue; }
unsigned esp_random() { randomValue += 2; return randomValue; }
constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
void* heap_caps_malloc(size_t bytes, unsigned caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (failAllocation) return nullptr;
  assert(bytes <= 2 * TRANSFER_CHUNK_BYTES);
  void* data = malloc(bytes); assert(data);
  allocations[data] = bytes; allocated += bytes;
  largestAllocation = std::max(largestAllocation, bytes);
  peakAllocated = std::max(peakAllocated, allocated);
  assert(allocated <= 3 * TRANSFER_CHUNK_BYTES);
  return data;
}
void heap_caps_free(void* data) {
  if (!data) return;
  assert(allocations.count(data)); allocated -= allocations.at(data);
  allocations.erase(data); free(data);
}
void workerStep();
void pump() { assert(!guardDepth && !workerContext); workerContext = true; workerStep(); workerContext = false; }
void vTaskDelay(unsigned ticks) { assert(!guardDepth); clockValue += ticks; if (!workerContext && !pausePump) pump(); }

// The simulated disk is outside the production PSRAM budget. Every VFS call
// asserts worker ownership, including close and cleanup after HTTP failures.
struct Node { bool directory = false; std::vector<uint8_t> bytes; };
struct Handle { std::string path; size_t offset = 0; bool writable = false; };
std::map<std::string, Node> disk;
std::map<int, Handle> descriptors;
int nextDescriptor = 3;
size_t maxIo = 0, ioLimit = TRANSFER_CHUNK_BYTES, writeCalls = 0, readCalls = 0;
size_t failWriteAfter = std::numeric_limits<size_t>::max(), failReadAfter = std::numeric_limits<size_t>::max();
unsigned closeCalls = 0, renameCalls = 0, unlinkCalls = 0;
bool failSync = false, failRename = false, failClose = false, failStat = false, disconnectOnWrite = false;
void disconnectCurrent();
void fsContext() { assert(workerContext && !guardDepth); }
void pathCheck(const char* path) {
  fsContext(); const std::string text(path);
  assert(text == "/usb" || text.rfind("/usb/", 0) == 0);
  assert(text.find("/../") == std::string::npos && text.find('\\') == std::string::npos);
}
struct SimStat { int st_mode = 0; int64_t st_size = 0; };
int SimStat(const char* path, struct SimStat* info) {
  pathCheck(path);
  if (failStat) { errno = EIO; return -1; }
  auto node = disk.find(path);
  if (node == disk.end()) { errno = ENOENT; return -1; }
  info->st_mode = node->second.directory ? 2 : 1;
  info->st_size = int64_t(node->second.bytes.size()); return 0;
}
int simOpen(const char* path, int flags, unsigned = 0) {
  pathCheck(path); auto node = disk.find(path);
  if ((flags & O_CREAT) && (flags & O_EXCL) && node != disk.end()) { errno = EEXIST; return -1; }
  if (node == disk.end()) {
    if (!(flags & O_CREAT)) { errno = ENOENT; return -1; }
    const std::string name(path), parent = name.substr(0, name.find_last_of('/'));
    if (!disk.count(parent) || !disk.at(parent).directory) { errno = ENOENT; return -1; }
    node = disk.emplace(path, Node{}).first;
  }
  if (node->second.directory) { errno = EISDIR; return -1; }
  const int descriptor = nextDescriptor++;
  descriptors[descriptor] = {path, 0, (flags & (O_RDWR | O_WRONLY)) != 0};
  assert(descriptors.size() == 1); return descriptor;
}
int simFstat(int descriptor, struct SimStat* info) {
  fsContext(); assert(descriptors.count(descriptor));
  return SimStat(descriptors.at(descriptor).path.c_str(), info);
}
int simClose(int descriptor) {
  fsContext(); assert(descriptors.erase(descriptor) == 1); ++closeCalls;
  return failClose ? -1 : 0;
}
int simMkdir(const char* path, unsigned) {
  pathCheck(path); if (disk.count(path)) { errno = EEXIST; return -1; }
  disk[path].directory = true; return 0;
}
int simUnlink(const char* path) {
  pathCheck(path); ++unlinkCalls;
  if (!disk.erase(path)) { errno = ENOENT; return -1; }
  return 0;
}
int simRename(const char* from, const char* to) {
  pathCheck(from); pathCheck(to); ++renameCalls;
  assert(!disk.count(to));
  if (failRename) { errno = EIO; return -1; }
  assert(disk.count(from)); disk[to] = std::move(disk.at(from)); disk.erase(from); return 0;
}
int simSync(int descriptor) { fsContext(); assert(descriptors.count(descriptor)); return failSync ? -1 : 0; }
off_t simSeek(int descriptor, off_t offset, int origin) {
  fsContext(); assert(descriptors.count(descriptor) && origin == SEEK_SET && offset >= 0);
  descriptors.at(descriptor).offset = size_t(offset); return offset;
}
using ssize_t = intptr_t;
ssize_t simWrite(int descriptor, const void* data, size_t count) {
  fsContext(); assert(descriptors.count(descriptor)); auto& handle = descriptors.at(descriptor);
  assert(handle.writable && count <= TRANSFER_CHUNK_BYTES); ++writeCalls; maxIo = std::max(maxIo, count);
  if (handle.offset >= failWriteAfter) { errno = EIO; return -1; }
  count = std::min({count, ioLimit, failWriteAfter - handle.offset});
  auto& bytes = disk.at(handle.path).bytes; bytes.resize(std::max(bytes.size(), handle.offset + count));
  memcpy(bytes.data() + handle.offset, data, count); handle.offset += count;
  if (disconnectOnWrite) disconnectCurrent();
  return ssize_t(count);
}
ssize_t simRead(int descriptor, void* data, size_t count) {
  fsContext(); assert(descriptors.count(descriptor)); auto& handle = descriptors.at(descriptor);
  assert(count <= TRANSFER_CHUNK_BYTES); ++readCalls; maxIo = std::max(maxIo, count);
  if (handle.offset >= failReadAfter) { errno = EIO; return -1; }
  const auto& bytes = disk.at(handle.path).bytes;
  count = std::min({count, ioLimit, bytes.size() - std::min(bytes.size(), handle.offset), failReadAfter - handle.offset});
  if (count) memcpy(data, bytes.data() + handle.offset, count);
  handle.offset += count; return ssize_t(count);
}

uint32_t receiveBegin(const char*, size_t, uint32_t, unsigned = DEFAULT_DIRECTORY);
bool release(uint32_t);
struct ClientState {
  const std::vector<uint8_t>* expected = nullptr;
  size_t received = 0, writes = 0, failAfter = std::numeric_limits<size_t>::max();
  bool connected = true, stopped = false, checkSendingLease = false;
};
struct TestClient {
  ClientState* state;
  bool connected() const { assert(!guardDepth && !workerContext); return state->connected; }
  size_t write(const uint8_t* bytes, size_t count) {
    assert(!guardDepth && !workerContext && count <= TRANSFER_CHUNK_BYTES);
    assert(state->expected && state->received + count <= state->expected->size());
    assert(!memcmp(bytes, state->expected->data() + state->received, count));
    if (state->checkSendingLease) {
      assert(::state->sending && !release(::state->token));
      clockValue += BUFFER_LEASE_MS + 1; pump(); assert(::state->buffer);
    }
    ++state->writes;
    const size_t sent = std::min(count, state->failAfter - std::min(state->failAfter, state->received));
    state->received += sent; return sent;
  }
  void stop() { assert(!guardDepth && !workerContext); state->connected = false; state->stopped = true; }
};
struct WebServer {
  ClientState response;
  std::map<std::string, std::string> headers;
  size_t length = 0; unsigned status = 0;
  void sendHeader(const std::string& key, const std::string& value) { headers[key] = value; }
  void setContentLength(size_t total) { length = total; }
  void send(unsigned code, const char* type, const char* body) {
    assert(!guardDepth && !workerContext && !strcmp(type, "application/octet-stream") && !*body); status = code;
  }
  TestClient client() { return {&response}; }
};

#define stat SimStat
#define fstat simFstat
#define S_ISREG(mode) ((mode) == 1)
#define open simOpen
#define close simClose
#define mkdir simMkdir
#define unlink simUnlink
#define rename simRename
#define fsync simSync
#define lseek simSeek
#define read simRead
#define write simWrite
#include "transfer_helpers.h"
void refresh(bool finish = true) { fsContext(); ++refreshCalls; ++state->generation; if (finish) state->phase = Phase::Idle; }
void manageSelected(const char*) { assert(false); }
struct SerialStub { template<class... T> void printf(const char*, T...) {} } Serial;
#include "device_event.h"
#include "msc_fixture.h"
#include "transfer_unmount.h"
#include "drive_registry.h"
#include "drive_api.h"
#include "quoted.h"
namespace monitor { void appendJson(String& json) { json += ",\"diagnostics\":{}"; } }
#include "status_json.h"
#include "transfer_fs.h"
#include "transfer.h"
#undef write
#include "send_file.h"
#include "worker_step.h"
#undef stat
#undef fstat
#undef open
#undef close
#undef mkdir
#undef unlink
#undef rename
#undef fsync
#undef lseek
#undef read
#undef write

uint8_t pattern(size_t offset) { return uint8_t((offset * 37 + offset / 251) & 255); }
void clean() { assert(!allocated && allocations.empty() && descriptors.empty() && !state->token && !state->buffer); }
void reset(bool attached = true) {
  clean(); resetMsc(); fixture = State{};
  disk.clear(); disk["/usb"].directory = true;
  if (attached) { connect(1); pump(); assert(ready && device); }
  fixture.generation = 7;
  clockValue = refreshCalls = closeCalls = renameCalls = unlinkCalls = 0;
  peakAllocated = largestAllocation = maxIo = writeCalls = readCalls = 0;
  ioLimit = TRANSFER_CHUNK_BYTES; failWriteAfter = failReadAfter = std::numeric_limits<size_t>::max();
  failAllocation = failSync = failRename = failClose = failStat = disconnectOnWrite = pausePump = false;
}
void cancel(uint32_t token) { assert(release(token)); assert(state->buffer); pump(); clean(); }
void select(const char* name, size_t bytes) {
  state->count = 1; state->files[0].size = bytes; state->files[0].directory = 0;
  strlcpy(state->files[0].name, name, MAX_NAME);
}
uint32_t openRead(const char* name) {
  select(name, disk.at(std::string("/usb/") + name).bytes.size());
  assert(command("read", state->generation, 0)); assert(descriptors.empty()); pump();
  assert(state->phase == Phase::Ready && state->token); return state->token;
}
uint32_t upload(const char* name, size_t bytes) {
  const auto token = receiveBegin(name, bytes, state->generation, 0); assert(token);
  assert(state->phase == Phase::Queued && descriptors.empty() && state->size == bytes);
  assert(allocated == 2 * TRANSFER_CHUNK_BYTES); return token;
}
void fill(uint8_t* bytes, size_t offset, size_t count) { for (size_t i = 0; i < count; ++i) bytes[i] = pattern(offset + i); }
void writeAll(uint32_t token, size_t total) {
  uint8_t bytes[TRANSFER_CHUNK_BYTES];
  for (size_t offset = 0; offset < total;) {
    const size_t count = std::min(sizeof(bytes), total - offset); fill(bytes, offset, count);
    assert(receiveChunk(token, offset, bytes, count)); offset += count;
    assert(state->received == offset && allocated == 2 * TRANSFER_CHUNK_BYTES);
  }
}
void commit(uint32_t token) {
  assert(receiveCommit(token)); assert(!release(token));
  assert(state->phase == Phase::Queued && !strcmp(state->operation, "write"));
  assert(descriptors.size() == 1); // Commit is queued, not performed by the caller.
  pump(); clean();
}
void largeTransfer() {
  reset(); const size_t total = 7 * 1024 * 1024 + 137;
  auto token = upload("large.bin", total); ioLimit = 997;
  writeAll(token, total); assert(writeCalls > total / TRANSFER_CHUNK_BYTES);
  ioLimit = TRANSFER_CHUNK_BYTES;
  uint8_t replay[TRANSFER_CHUNK_BYTES]; fill(replay, 0, sizeof(replay));
  auto writes = writeCalls;
  assert(receiveChunk(token, 0, replay, sizeof(replay)) && state->received == total && writeCalls == writes);
  fill(replay, total - 137, 137); assert(receiveChunk(token, total - 137, replay, 137));
  assert(!receiveChunk(token, total - 137, replay, 138));
  commit(token); assert(state->phase == Phase::Idle && refreshCalls == 1 && renameCalls == 1);
  const auto& saved = disk.at("/usb/large.bin").bytes; assert(saved.size() == total);
  for (size_t i = 0; i < total; ++i) assert(saved[i] == pattern(i));
  token = openRead("large.bin"); assert(readCalls == 2); // Replays only: readSelected does not preload.
  WebServer server; server.response.expected = &saved; server.response.checkSendingLease = true;
  ioLimit = 673; assert(sendFile(server, token));
  assert(server.status == 200 && server.length == total && server.response.received == total && !server.response.stopped);
  assert(server.headers.at("Cache-Control") == "no-store");
  assert(server.headers.at("Content-Disposition").find("large.bin") != std::string::npos);
  assert(state->cancel && !state->sending && allocated == 2 * TRANSFER_CHUNK_BYTES);
  pump(); clean(); assert(peakAllocated == 3 * TRANSFER_CHUNK_BYTES && maxIo <= TRANSFER_CHUNK_BYTES);
}
void validationAndReplay() {
  reset(); select("missing.bin", 4);
  assert(!command("read", 6, 0) && !command("read", 7, 1) && !command("format", 7, 0));
  assert(!receiveBegin("../bad.bin", 4, state->generation) && !receiveBegin("bad.tmp", 4, state->generation));
  assert(!receiveBegin("safe.bin", MAX_FILE_BYTES + size_t(1), state->generation));
  assert(!receiveBegin("safe.bin", 4, 6, 0) && !receiveBegin("safe.bin", 4, 7, 1));
  failAllocation = true; assert(!receiveBegin("safe.bin", 4, state->generation)); failAllocation = false;
  const auto token = upload("small.bin", 4); uint8_t bytes[]{1,2,3,4}, wrong[]{9,9};
  assert(!command("refresh", 0, 0) && !receiveBegin("other.bin", 1, state->generation));
  assert(!receiveChunk(token + 2, 0, bytes, 2));
  assert(!receiveChunk(token, 1, bytes, 2) && !receiveChunk(token, 0, bytes, 5));
  assert(!receiveChunk(token, 0, nullptr, 2) && !receiveChunk(token, 0, bytes, 0));
  assert(!receiveChunk(token, 0, bytes, TRANSFER_CHUNK_BYTES + 1));
  assert(!receiveChunk(token, std::numeric_limits<size_t>::max(), bytes, 2));
  assert(!receiveCommit(token)); assert(receiveChunk(token, 0, bytes, 2));
  assert(state->received == 2 && disk.at("/usb/small.bin.tmp").bytes.size() == 2);
  assert(receiveChunk(token, 0, bytes, 2) && state->received == 2);
  assert(!receiveChunk(token, 0, bytes, 3) && !receiveCommit(token));
  assert(!receiveChunk(token, 0, wrong, 2) && state->phase == Phase::Error);
  cancel(token); assert(!disk.count("/usb/small.bin.tmp") && !disk.count("/usb/small.bin"));
  assert(!release(token));
  const auto next = upload("next.bin", 4); assert(next != token && !release(token));
  assert(!receiveChunk(token, 0, bytes, 2)); cancel(next);
  reset(); auto limit = upload("limit.bin", MAX_FILE_BYTES); assert(allocated == 16384); cancel(limit);
  reset(); std::string path(MAX_PATH - 2, 'a'); strlcpy(state->directories[0].path, path.c_str(), MAX_PATH);
  assert(!receiveBegin("long.bin", 1, 7, 0)); clean();
}
void ownershipAndEmpty() {
  reset(); auto token = upload("cancel.bin", 4); cancel(token); assert(!disk.count("/usb/cancel.bin.tmp"));
  reset(); token = upload("empty.bin", 0); commit(token); assert(disk.at("/usb/empty.bin").bytes.empty());
  token = openRead("empty.bin"); WebServer empty; empty.response.expected = &disk.at("/usb/empty.bin").bytes;
  assert(sendFile(empty, token) && empty.status == 200 && !empty.length && !empty.response.writes); pump(); clean();
  reset(); token = receiveBegin("default.bin", 0, state->generation); assert(token); commit(token);
  assert(disk.at("/usb/OFE").directory && disk.count("/usb/OFE/default.bin"));
  reset(); token = upload("cancel-chunk.bin", 4); pump();
  strlcpy(state->operation, "chunk", sizeof(state->operation)); state->phase = Phase::Queued;
  cancel(token); assert(!disk.count("/usb/cancel-chunk.bin.tmp"));
}
void noOverwriteAndFailures() {
  reset(); disk["/usb/same.bin"].bytes = {7,8,9}; auto token = upload("same.bin", 4); pump();
  assert(state->phase == Phase::Error && descriptors.empty()); cancel(token);
  assert((disk.at("/usb/same.bin").bytes == std::vector<uint8_t>{7,8,9}) && !renameCalls);
  for (bool finish : {false, true}) {
    reset(); disk["/usb/same.bin.tmp"].bytes = {7,8,9}; token = upload("same.bin", 4); pump();
    assert(state->phase == Phase::Receiving && descriptors.size() == 1);
    const std::string owned = state->temporary;
    assert(owned != "/usb/same.bin.tmp" && owned.rfind("/usb/same.bin.", 0) == 0);
    assert(owned.size() >= 4 && owned.substr(owned.size() - 4) == ".tmp");
    if (finish) { writeAll(token, 4); commit(token); assert(disk.at("/usb/same.bin").bytes.size() == 4); }
    else { cancel(token); assert(!disk.count("/usb/same.bin")); }
    assert(!disk.count(owned) && (disk.at("/usb/same.bin.tmp").bytes == std::vector<uint8_t>{7,8,9}));
  }
  reset(); disk["/usb/full.bin.tmp"].bytes = {9}; token = upload("full.bin", 4);
  for (unsigned i = 1; i <= 8; ++i) {
    char path[MAX_PATH + 24]; snprintf(path, sizeof(path), "/usb/full.bin.%08X.tmp", randomValue + 2 * i);
    disk[path].bytes = {9};
  }
  pump(); assert(state->phase == Phase::Error && descriptors.empty()); cancel(token);
  assert(disk.size() == 10 && !renameCalls && !unlinkCalls); // Root and all nine orphans survive.
  reset(); token = upload("race.bin", 4); writeAll(token, 4); disk["/usb/race.bin"].bytes = {9};
  commit(token); assert(state->phase == Phase::Error && !renameCalls && disk.at("/usb/race.bin").bytes[0] == 9);
  for (unsigned failure = 0; failure < 4; ++failure) {
    reset(); token = upload("failure.bin", 4); writeAll(token, 4);
    failSync = failure == 0; failRename = failure == 1; failClose = failure == 2; failStat = failure == 3;
    commit(token); assert(state->phase == Phase::Error && !disk.count("/usb/failure.bin") && !disk.count("/usb/failure.bin.tmp"));
  }
  reset(); token = upload("partial.bin", 4); failWriteAfter = 2; const uint8_t bytes[]{1,2,3,4};
  assert(!receiveChunk(token, 0, bytes, 4) && state->phase == Phase::Error && state->received == 0);
  assert(disk.at("/usb/partial.bin.tmp").bytes.size() == 2 && !receiveCommit(token)); cancel(token);
  assert(!disk.count("/usb/partial.bin.tmp"));
  reset(); token = upload("timeout.bin", 4); pausePump = true;
  assert(!receiveChunk(token, 0, bytes, 4) && clockValue >= 15000); pausePump = false; cancel(token);
}
void readsAndCleanup() {
  reset(); disk["/usb/read.bin"].bytes.resize(TRANSFER_CHUNK_BYTES + 5);
  auto& bytes = disk.at("/usb/read.bin").bytes; fill(bytes.data(), 0, bytes.size());
  select("read.bin", MAX_FILE_BYTES + size_t(1)); assert(command("read", 7, 0)); pump();
  assert(state->phase == Phase::Error && descriptors.empty()); clean(); state->phase = Phase::Idle;
  select("missing.bin", 4); assert(command("read", 7, 0)); pump();
  assert(state->phase == Phase::Error && descriptors.empty()); clean(); state->phase = Phase::Idle;
  disk["/usb/folder"].directory = true; select("folder", 0); assert(command("read", 7, 0)); pump();
  assert(state->phase == Phase::Error && descriptors.empty()); clean(); state->phase = Phase::Idle;
  select("read.bin", bytes.size() - 1); assert(command("read", 7, 0)); pump();
  assert(state->phase == Phase::Error && descriptors.empty()); clean();
  state->phase = Phase::Idle; failAllocation = true; select("read.bin", bytes.size());
  assert(command("read", 7, 0)); pump(); clean(); failAllocation = false; state->phase = Phase::Idle;
  auto token = openRead("read.bin"); WebServer bad; bad.response.expected = &bytes;
  assert(!sendFile(bad, token + 2) && !bad.status && allocated == 16384);
  failAllocation = true; assert(!sendFile(bad, token) && !bad.status); failAllocation = false;
  cancel(token);
  for (unsigned failure = 0; failure < 3; ++failure) {
    state->phase = Phase::Idle; token = openRead("read.bin"); WebServer server; server.response.expected = &bytes;
    if (failure == 0) server.response.failAfter = 11;
    if (failure == 1) server.response.connected = false;
    if (failure == 2) failReadAfter = 11;
    assert(sendFile(server, token) && server.response.stopped && server.response.received < bytes.size());
    pump(); clean(); failReadAfter = std::numeric_limits<size_t>::max();
  }
  state->phase = Phase::Idle; token = openRead("read.bin"); clockValue = state->touched + BUFFER_LEASE_MS;
  pump(); assert(state->token == token); ++clockValue; pump(); clean();
  reset(); token = upload("lease.bin", 4); pump(); clockValue = state->touched + BUFFER_LEASE_MS + 1;
  pump(); clean(); assert(!disk.count("/usb/lease.bin.tmp"));
  reset(); clockValue = UINT32_MAX - 10; token = upload("wrap.bin", 4); pump();
  clockValue += BUFFER_LEASE_MS + 1; pump(); clean();
  reset(); token = upload("gone.bin", 4); const uint8_t chunk[]{1,2,3,4}; disconnectOnWrite = true;
  assert(!receiveChunk(token, 0, chunk, 4)); pump(); clean(); assert(!disk.count("/usb/gone.bin"));
  assert(!receiveBegin("disconnected.bin", 1, state->generation) && !command("refresh", 0, 0));
  // Removal cannot unlink media that is no longer present, but must close its handle.
  assert(closeCalls == 1 && state->phase == Phase::Error);
  reset(); disk["/usb/read.bin"].bytes = {1}; token = openRead("read.bin");
  disconnectCurrent(); pump(); clean(); assert(!release(token));
}
void reconnectDuringCleanup() {
  reset(); const auto oldDevice = device;
  disconnectCurrent(); assert(gone && !present && !ready);
  connect(2); pump();
  assert(!ready && !present && selectedDrive == NO_DRIVE && !gone);
  assert(installed.count(oldDevice) == 0 && drives[0].address == 2);
  assert(selectDrive(state->generation, 0, drives[0].instance)); pump();
  assert(ready && present && device != oldDevice && device->address == 2); clean();
}
#ifndef USB_MULTI_DRIVE_TEST
int main() {
  static_assert(TRANSFER_CHUNK_BYTES == 8192 && MAX_FILE_BYTES == INT32_MAX, "VFS limit, not a whole-file RAM cap");
  largeTransfer(); validationAndReplay(); ownershipAndEmpty(); noOverwriteAndFailures(); readsAndCleanup(); reconnectDuringCleanup();
  clean(); assert(!guardDepth && !workerContext);
  resetMsc();
  puts("PASS: extracted production streaming APIs, worker dispatch, VFS IO and cleanup; >6 MiB identity, bounded PSRAM, replay, stale/token/path/bounds, failures, no overwrite, empty files, disconnect and leases");
}
#endif
