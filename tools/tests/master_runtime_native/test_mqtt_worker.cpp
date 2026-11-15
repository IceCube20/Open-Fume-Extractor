#include "../../../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/MqttPublishWorker.h"
#include <iostream>
#include <map>
#include <string>

struct Delivery { std::string topic, payload; bool retained; int core; };
static std::vector<Delivery> deliveries;
static std::atomic<unsigned> active{0};
static const char* expected_topic = nullptr;
static const char* expected_payload = nullptr;
static bool publish(const char* topic, const char* payload, bool retained) {
  assert(active.fetch_add(1) == 0); // No concurrent access to the shared client.
  if (expected_topic) assert(topic == expected_topic && payload == expected_payload);
  std::this_thread::sleep_for(std::chrono::microseconds(150));
  deliveries.push_back({topic ? topic : "<null>", payload ? payload : "<null>", retained,
                        current_task ? current_task->core : -1});
  const bool success = !payload || std::string(payload) != "fail";
  --active;
  return success;
}
static void test_direct_and_failed_start() {
  MqttPublishWorker worker(publish);
  assert(worker.publish("before-start", "ok", false));
  assert(deliveries.back().core == -1);
  fail_semaphore_create = true;
  assert(!worker.start()); assert(!worker.taskHandle());
  assert(!worker.publish("allocation-fallback", "fail", true));
  fail_semaphore_create = false; fail_task_create = true;
  assert(!worker.start()); assert(!worker.taskHandle());
  assert(worker.publish("task-fallback", "", true));
  assert(deliveries.back().retained && deliveries.back().payload.empty());
  fail_task_create = false;
}
static void test_handoff_and_parallel_callers() {
  MqttPublishWorker worker(publish);
  assert(worker.start()); assert(worker.taskHandle()); assert(worker.start());
  assert(fake_tasks.size() == 1);
  std::string topic = "temporary/topic", payload(16384, 'x');
  expected_topic = topic.c_str(); expected_payload = payload.c_str();
  interrupted_binary_takes = 1;
  assert(worker.publish(topic.c_str(), payload.c_str(), true));
  expected_topic = expected_payload = nullptr;
  assert(deliveries.back().core == 0 && deliveries.back().payload == payload);
  payload.clear(); topic.clear(); // Handoff finished; no worker still borrows these buffers.
  const auto before = deliveries.size();
  assert(!worker.publish("failure", "fail", false));
  assert(deliveries.size() == before + 1); // No hidden retries or duplicate messages.
  assert(worker.publish("tombstone", "", true));
  assert(deliveries.back().retained && deliveries.back().payload.empty());
  const auto concurrent_start = deliveries.size();
  std::vector<std::thread> callers;
  for (unsigned owner = 0; owner < 4; ++owner) callers.emplace_back([&, owner] {
    for (unsigned index = 0; index < 50; ++index) {
      const auto name = std::to_string(owner) + "/" + std::to_string(index);
      assert(worker.publish(name.c_str(), name.c_str(), (index % 2) != 0));
    }
  });
  for (auto& caller : callers) caller.join();
  stop_fake_tasks();
  assert(deliveries.size() == concurrent_start + 200);
  std::map<unsigned, unsigned> next;
  for (size_t i = concurrent_start; i < deliveries.size(); ++i) {
    const auto& message = deliveries[i];
    assert(message.core == 0 && message.topic == message.payload);
    const auto split = message.topic.find('/');
    const unsigned owner = std::stoul(message.topic.substr(0, split));
    const unsigned index = std::stoul(message.topic.substr(split + 1));
    assert(index == next[owner]++ && message.retained == ((index % 2) != 0));
  }
  assert(worker_yields >= 203);
}
int main() {
  test_direct_and_failed_start();
  test_handoff_and_parallel_callers();
  std::cout << "PASS: actual MQTT publish worker, 200 concurrent handoffs, exact payload/retained/order/result, failure fallback, borrowed lifetime, interrupted wait, per-message idle yield\n";
}
