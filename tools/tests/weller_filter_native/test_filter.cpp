#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>

static uint32_t now_ms = 100;
static uint32_t millis() { return now_ms; }
static uint32_t last_weller_rx_ms = 0, last_filter_status_ms = 0, fan_on_since_ms = 0;
static uint8_t filter_status_code = 0, reported_speed_percent = 0, work_light_state = 0;
static uint16_t fault_mask = 0, fan_rpm = 0, filter_runtime_minutes = 0, programmed_filter_minutes = 0;
static uint16_t weller_version = 0, io_output_mask = 0;
static bool output_enabled = false;
static const uint16_t WELLER_FAULT_BUS = 1, WELLER_FAULT_SPEED_FEEDBACK = 0x100;
static const uint32_t WELLER_RPM_GRACE_MS = 8000;
static const uint8_t MODULE_WELLER_ZERO_SMOG = 5, STATUS_OK = 0, CMD_GET_TELEMETRY = 0x20, module_addr = 0x30;
struct Frame { uint8_t dst = 0, src = 0, seq = 0, cmd = 0, len = 0; uint8_t payload[192] = {}; };
struct Bus { Frame last; void send(const Frame& frame) { last = frame; } };
static Bus bus;
struct Leds { uint8_t busEvent() { return 0; } uint8_t moduleEvent() { return 0; } };
static Leds ofe_status_leds;
static void put_u16_le(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint16_t u16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static void append_system_telemetry(uint8_t* p, uint8_t& o) { memset(p + o, 0, 15); o += 15; }

#include "generated_filter.inc.h"

static void check(bool value, const char* message) {
  if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

int main() {
  Frame request; request.src = 1; request.seq = 7;
  handle_telemetry(request);
  check(bus.last.payload[3] == 0 && (fault_mask & WELLER_FAULT_BUS), "no Weller at boot: unknown, not warning");
  publish_weller_frame('G', 6); publish_weller_frame('F', 960); publish_weller_frame('L', 10);
  handle_telemetry(request);
  check(bus.last.len == 32 && bus.last.seq == request.seq && bus.last.payload[1] == MODULE_WELLER_ZERO_SMOG,
        "telemetry layout and identity unchanged");
  check(bus.last.payload[3] == 10 && (fault_mask & 6) == 2, "genuine fresh warning is retained");
  check(u16(bus.last.payload + 4) == 60 && u16(bus.last.payload + 6) == 9600, "runtime and lifetime unchanged");
  now_ms += 5001;
  // First telemetry poll after an OFE master reboot, Weller itself still off.
  handle_telemetry(request);
  check(bus.last.payload[3] == 10 && (fault_mask & 6) == 0 && (fault_mask & WELLER_FAULT_BUS),
        "powered-off Weller keeps its last reading without resurrecting active faults on master reboot");
  check(u16(bus.last.payload + 4) == 60 && u16(bus.last.payload + 6) == 9600, "expiry does not reset real counters");
  publish_weller_frame('S', 60);
  handle_telemetry(request);
  check(bus.last.payload[3] == 10 && (fault_mask & 6) == 0 && !(fault_mask & WELLER_FAULT_BUS), "other fresh replies preserve cached text but cannot revalidate filter faults");
  publish_weller_frame('L', 1);
  handle_telemetry(request);
  check(bus.last.payload[3] == 1 && (fault_mask & 6) == 0, "fresh good L reply restores very good immediately");
  publish_weller_frame('L', 100);
  handle_telemetry(request);
  check(bus.last.payload[3] == 100 && (fault_mask & 6) == 4, "genuine critical filter warning retained");
  now_ms += WELLER_FILTER_MAX_AGE_MS + 1;
  publish_weller_frame('S', 60); // UART alive, but no new filter reply.
  handle_telemetry(request);
  check(bus.last.payload[3] == 100 && (fault_mask & 6) == 0 && !(fault_mask & WELLER_FAULT_BUS),
        "filter display survives expiry independently of live fault flags");
  publish_weller_frame('L', 300);
  handle_telemetry(request);
  check(bus.last.payload[3] == 100 && (fault_mask & 6) == 0, "unsupported filter replies do not erase the last recognized reading");
  fault_mask |= 0x8000;
  publish_weller_frame('L', 10);
  now_ms += 5001;
  handle_telemetry(request);
  check((fault_mask & 0x8000) && !(fault_mask & 6), "expiration preserves unrelated faults");
  now_ms = UINT32_MAX - 1000;
  publish_weller_frame('L', 10);
  now_ms += 2000;
  handle_telemetry(request);
  check(bus.last.payload[3] == 10, "freshness works across millis overflow");
  now_ms += 5001;
  handle_telemetry(request);
  check(bus.last.payload[3] == 10 && !(fault_mask & 6), "cached display and fault expiration work across millis overflow");
  publish_weller_frame('L', 1);
  handle_telemetry(request);
  check(bus.last.payload[3] == 1, "reconnect restores current filter state");
  now_ms += 5001;
  handle_telemetry(request);
  check(bus.last.payload[3] == 1 && (fault_mask & WELLER_FAULT_BUS), "very good remains visible after disconnect as well");
  std::cout << "PASS: production Weller last-known filter display, fault expiry and telemetry; master restart/offline/reconnect/wraparound\n";
}
