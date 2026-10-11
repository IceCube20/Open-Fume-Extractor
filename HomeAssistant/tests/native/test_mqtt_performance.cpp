#include <cassert>
#include <iostream>
#include "../../../OpenFumeExtractorMaster/src/MqttPerformance.h"

int main() {
  MqttPerfWindow window;
  window.record(MqttPerfPhase::Build, 999);
  assert(window.snapshot(0).phases[0].calls == 0);
  window.tick(100, true);
  window.record(MqttPerfPhase::Build, 200);
  window.record(MqttPerfPhase::Build, 400);
  window.record(MqttPerfPhase::Publish, 1000, 1024, true);
  window.record(MqttPerfPhase::Publish, 3000, 999, false);
  window.record(MqttPerfPhase::ClientLoop, 120);
  window.record(MqttPerfPhase::Count, 1000);
  auto snapshot = window.snapshot(1100);
  assert(snapshot.window_ms == 1000);
  assert(snapshot.phases[0].calls == 2 && snapshot.phases[0].averageUs() == 300);
  assert(snapshot.phases[0].max_us == 400 && snapshot.phases[0].total_us == 600);
  assert(snapshot.phases[1].averageUs() == 2000 && snapshot.phases[2].calls == 1);
  assert(snapshot.payload_bytes == 1024 && snapshot.publish_failed == 1);
  window.tick(5100, true);
  snapshot = window.snapshot(5200);
  assert(snapshot.window_ms == 5000 && snapshot.phases[0].calls == 2);
  window.record(MqttPerfPhase::Build, 900);
  assert(window.snapshot(5300).phases[0].calls == 2);
  window.tick(10100, true);
  assert(window.snapshot(10100).phases[0].averageUs() == 900);
  window.tick(15100, true);
  assert(window.snapshot(15100).phases[0].averageUs() == 0);
  window.tick(15101, false);
  window.record(MqttPerfPhase::Build, 1000);
  assert(window.snapshot(20000).window_ms == 0);
  window.tick(20000, true);
  assert(window.snapshot(20000).phases[0].calls == 0);
  window.reset(UINT32_MAX - 99, true);
  window.record(MqttPerfPhase::Build, 42);
  assert(window.snapshot(0).window_ms == 100);
  window.tick(4900, true);
  assert(window.snapshot(4900).window_ms == 5000);
  MqttPerfMetric metric;
  metric.add(UINT32_MAX);
  metric.add(UINT32_MAX);
  assert(metric.total_us == uint64_t(UINT32_MAX) * 2 && metric.averageUs() == UINT32_MAX);
  std::cout << "MQTT performance: phase isolation, rolling windows, disable/reset, rollover and 64-bit sums passed\n";
}
