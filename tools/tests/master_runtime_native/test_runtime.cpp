#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

constexpr uint8_t LOGIC_RT_MAX_NODES = 32, LOGIC_RT_MAX_OUTPUTS = 32;
constexpr uint32_t LOGIC_RT_INTERVAL_MS = 50;
struct LogicCompiledDefinition;
struct LogicRtInputs;
struct LogicRtState;
static LogicRtInputs reference_node_inputs(const LogicCompiledDefinition&, uint8_t);
static void reference_advance_clock(LogicRtState&, uint32_t, uint32_t, uint32_t);
static std::array<bool, 32> input_values;
static uint32_t input_calls = 0;
static bool logic_signal_active(const char* signal) {
  ++input_calls;
  return input_values[static_cast<uint8_t>(signal[0])];
}

using TickType_t = uint32_t;
using SemaphoreHandle_t = void*;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
constexpr int pdTRUE = 1;
static bool lock_available = true;
static SemaphoreHandle_t logic_rt_mutex = reinterpret_cast<void*>(1);
static int xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return lock_available; }
static void xSemaphoreGive(SemaphoreHandle_t) {}
static uint32_t pdMS_TO_TICKS(uint32_t value) { return value; }
static uint32_t now_ms = 0, logic_rt_last_tick_ms = 0, logic_rt_last_run_ms = 0;
static bool logic_rt_cache_ready = true;
static uint32_t millis() { return now_ms; }
static uint32_t micros() { return now_ms * 1000; }

enum { MODULE_JBC_BUS = 1, MODULE_FAN_IO, MODULE_FAN_IO_PRO,
       MODULE_WELLER_ZERO_SMOG, MODULE_DISPLAY, MODULE_UNIVERSAL_RS232,
       MODULE_MODBUS_RTU, MODULE_JBC_USB };
constexpr uint32_t CAP_JBC_ACTIVITY = 1;
constexpr uint8_t FAST_FLAG_CONNECTED = 1;
struct UniversalEntityState { uint8_t id = 0, len = 0, data[32] = {}; };
struct ModuleRecord {
  bool online = true;
  uint8_t type = MODULE_JBC_BUS, addr = 0, jbc_link_flags = 0;
  uint32_t caps = CAP_JBC_ACTIVITY;
  uint16_t weller_uart_age_sec = UINT16_MAX;
  bool universal_entities_valid = false;
  uint8_t universal_entity_count = 0;
  UniversalEntityState universal_entities[4];
};
struct Registry {
  std::vector<ModuleRecord> records;
  uint8_t count() const { return static_cast<uint8_t>(records.size()); }
  ModuleRecord& at(uint8_t i) { return records.at(i); }
};
struct MasterScheduler {
  Registry registry_;
  bool module_fw_active_ = false;
  uint8_t module_fw_target_ = 0, active_jbc_addr_ = 0x10;
  uint8_t next_jbc_poll_index_ = 0, next_disconnected_jbc_poll_index_ = 0;
  uint32_t last_default_jbc_probe_ms_ = 0;
  static constexpr uint32_t JBC_DISCONNECTED_FAST_POLL_PER_MODULE_MS = 500;
  static constexpr uint32_t JBC_FAST_POLL_PER_MODULE_MS = 75;
  static constexpr uint32_t JBC_DEFAULT_PROBE_INTERVAL_MS = 500;
  std::vector<uint8_t> polls;
  bool fastPollJbc(uint8_t addr) { polls.push_back(addr); return true; }
  bool pollNextJbc();
};

#include "generated_runtime.inc.h"

// Previous implementation: four complete link scans for each node/pass.
static LogicRtInputs reference_node_inputs(const LogicCompiledDefinition& def, uint8_t node) {
  LogicRtInputs result;
  for (uint8_t i = 0; i < def.link_count; ++i) {
    const auto& link = def.links[i];
    if (link.to != node || link.from >= def.node_count) continue;
    ++result.total;
    if (def.nodes[link.from].value) ++result.on;
  }
  for (uint8_t port = 0; port < 3; ++port) {
    for (uint8_t i = 0; i < def.link_count; ++i) {
      const auto& link = def.links[i];
      if (link.to == node && link.port == port && link.from < def.node_count && def.nodes[link.from].value) {
        result.ports |= uint8_t(1U << port);
        break;
      }
    }
  }
  return result;
}
static void reference_advance_clock(LogicRtState& state, uint32_t dt, uint32_t on, uint32_t off) {
  const uint32_t sum = state.timer_ms + dt;
  state.timer_ms = sum < state.timer_ms || sum > 0x7FFFFFFFUL ? 0x7FFFFFFFUL : sum;
  for (uint8_t i = 0; i < 8; ++i) {
    const uint32_t phase = state.mem ? on : off;
    if (state.timer_ms < phase) break;
    state.timer_ms -= phase;
    state.mem = !state.mem;
  }
}

struct Fixture {
  std::array<LogicRtNode, 32> nodes;
  std::array<LogicRtLink, 64> links;
  LogicCompiledDefinition def;
  void bind() { def.nodes = nodes.data(); def.links = links.data(); }
  explicit Fixture(std::mt19937& rng) {
    def.used = def.enabled = true;
    def.node_count = 32;
    def.link_count = 64;
    bind();
    for (uint8_t i = 0; i < 32; ++i) {
      nodes[i].type = uint8_t(1 + rng() % 11);
      if (i < 4) nodes[i].type = LOGIC_NODE_INPUT;
      if (i >= 28) nodes[i].type = LOGIC_NODE_OUTPUT;
      nodes[i].signal[0] = char(i);
      nodes[i].signal[1] = 0;
      nodes[i].delay_ms = 75 + rng() % 2000;
      nodes[i].delay2_ms = 75 + rng() % 2000;
      nodes[i].reset_dominant = rng() % 2;
    }
    for (auto& link : links) {
      link.from = uint8_t(rng() % 32);
      link.to = uint8_t(rng() % 32);
      link.port = uint8_t(rng() % 4);
    }
    logic_index_links(def);
  }
  Fixture(const Fixture& other) : nodes(other.nodes), links(other.links), def(other.def) { bind(); }
};

static void test_logic_equivalence() {
  std::mt19937 rng(20261004);
  for (int graph = 0; graph < 250; ++graph) {
    Fixture optimized(rng), reference(optimized);
    for (int tick = 0; tick < 200; ++tick) {
      for (auto& value : input_values) value = rng() % 2;
      const uint32_t dt = rng() % 501;
      LogicRtOutput actual[32] = {}, expected[32] = {};
      uint8_t actual_count = 0, expected_count = 0;
      input_calls = 0;
      logic_eval_definition(optimized.def, dt, actual, actual_count);
      uint32_t inputs = 0;
      for (const auto& n : optimized.nodes) inputs += n.type == LOGIC_NODE_INPUT;
      assert(input_calls == inputs);
      input_calls = 0;
      reference_eval_definition(reference.def, dt, expected, expected_count);
      assert(input_calls == inputs * 8);
      assert(actual_count == expected_count);
      for (uint8_t i = 0; i < actual_count; ++i) {
        assert(std::strcmp(actual[i].signal, expected[i].signal) == 0);
        assert(actual[i].active == expected[i].active);
      }
      for (uint8_t i = 0; i < 32; ++i) {
        const auto& a = optimized.nodes[i];
        const auto& b = reference.nodes[i];
        assert(a.value == b.value && a.state.mem == b.state.mem && a.state.last == b.state.last);
        assert(a.state.timer_ms == b.state.timer_ms);
      }
    }
  }
  std::puts("PASS: 50,000 differential logic ticks (32 nodes/64 links, feedback, all block types)");
}

static void test_clock() {
  std::mt19937 rng(42);
  for (int i = 0; i < 100000; ++i) {
    const uint32_t on = 1 + rng() % 2000, off = 1 + rng() % 2000;
    LogicRtState state;
    state.mem = rng() % 2;
    state.timer_ms = rng() % (state.mem ? on : off);
    const uint32_t dt = rng();
    const uint64_t position = (uint64_t)(state.mem ? 0 : on) + state.timer_ms + dt;
    const uint64_t expected = position % ((uint64_t)on + off);
    logic_advance_clock(state, dt, on, off);
    assert(state.mem == (expected < on));
    assert(state.timer_ms == (expected < on ? expected : expected - on));
  }
  LogicRtState state;
  state.mem = true;
  logic_advance_clock(state, 500, UINT32_MAX, UINT32_MAX);
  assert(state.mem && state.timer_ms == 500);
  std::puts("PASS: clock phase/overflow, 100,000 cases including short/asymmetric durations");
}

static void test_lock_timing() {
  logic_rt_mutex = nullptr;
  assert(!logic_rt_lock());
  logic_rt_mutex = reinterpret_cast<void*>(1);
  logic_rt_last_tick_ms = 100;
  logic_rt_last_run_ms = 100;
  now_ms = 150;
  lock_available = false;
  logic_runtime_tick();
  assert(logic_rt_last_tick_ms == 100);
  now_ms = 200;
  lock_available = true;
  logic_runtime_tick();
  assert(logic_rt_last_tick_ms == 200);
  logic_rt_last_tick_ms = logic_rt_last_run_ms = UINT32_MAX - 24;
  now_ms = 25;
  logic_runtime_tick();
  assert(logic_rt_last_tick_ms == 25);
  std::puts("PASS: fail-closed mutex, contended timer accounting, millis wraparound");
}

static void test_probe_fairness() {
  MasterScheduler s;
  for (uint8_t i = 0; i < 4; ++i) {
    ModuleRecord m;
    m.addr = 0x10 + i;
    s.registry_.records.push_back(m);
  }
  s.registry_.records[1].online = false;
  s.module_fw_active_ = true;
  s.module_fw_target_ = 0x12;
  now_ms = 0;
  for (int i = 0; i < 100; ++i) { now_ms += 1000; assert(s.pollNextJbc()); }
  assert(s.polls.size() == 100);
  for (size_t i = 0; i < s.polls.size(); ++i) assert(s.polls[i] == (i % 2 ? 0x13 : 0x10));
  s.registry_.records[3].jbc_link_flags = FAST_FLAG_CONNECTED;
  s.polls.clear();
  for (int i = 0; i < 100; ++i) { now_ms += 75; s.pollNextJbc(); }
  bool connected_polled = false, disconnected_polled = false;
  for (auto addr : s.polls) { connected_polled |= addr == 0x13; disconnected_polled |= addr == 0x10; }
  assert(connected_polled && disconnected_polled);
  s.last_default_jbc_probe_ms_ = UINT32_MAX - 250;
  now_ms = 300;
  assert(s.pollNextJbc());
  std::puts("PASS: fair absent-station probes with jitter, offline/OTA exclusion, hot-plug and wraparound");
}

static void test_device_diagnostics() {
  ModuleRecord m;
  for (auto type : {MODULE_JBC_BUS, MODULE_JBC_USB}) {
    m.type = uint8_t(type);
    assert(diagnostics_device_link_state(m) == 0);
    m.jbc_link_flags = FAST_FLAG_CONNECTED;
    assert(diagnostics_device_link_state(m) == 1);
    m.jbc_link_flags = 0;
  }
  m.type = MODULE_WELLER_ZERO_SMOG;
  for (auto age : {0, 10, 11, 65534, 65535}) {
    m.weller_uart_age_sec = uint16_t(age);
    assert(diagnostics_device_link_state(m) == (age <= 10));
  }
  for (auto type : {MODULE_UNIVERSAL_RS232, MODULE_MODBUS_RTU}) {
    m.type = uint8_t(type);
    m.universal_entities_valid = false;
    assert(diagnostics_device_link_state(m) == -1);
    m.universal_entities_valid = true;
    m.universal_entity_count = 1;
    auto& e = m.universal_entities[0];
    e.id = 1;
    e.len = 1;
    for (auto value : {0, 1, int('0'), int('1')}) {
      e.data[0] = uint8_t(value);
      assert(diagnostics_device_link_state(m) == (value != 0 && value != '0'));
    }
    e.id = 20;
    assert(diagnostics_device_link_state(m) == -1);
  }
  m.type = MODULE_DISPLAY;
  assert(diagnostics_device_link_state(m) == 1);
  m.online = false;
  assert(diagnostics_device_link_state(m) == 0);
  std::puts("PASS: transport/device status separated for JBC, Weller, Universal/Modbus and display");
}

static double benchmark(Fixture& fixture, bool reference) {
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 20000; ++i) {
    LogicRtOutput outputs[32] = {};
    uint8_t count = 0;
    if (reference) reference_eval_definition(fixture.def, 50, outputs, count);
    else logic_eval_definition(fixture.def, 50, outputs, count);
  }
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

int main() {
  test_logic_equivalence();
  test_clock();
  test_lock_timing();
  test_probe_fairness();
  test_device_diagnostics();
  std::mt19937 rng(7);
  Fixture optimized(rng), reference(optimized);
  const double old_ms = benchmark(reference, true), new_ms = benchmark(optimized, false);
  std::printf("HOST benchmark (not ESP32 CPU): old %.1f ms, indexed %.1f ms, %.2fx speedup\n", old_ms, new_ms, old_ms / new_ms);
}
