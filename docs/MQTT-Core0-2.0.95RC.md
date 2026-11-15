# Bounded core-0 publish experiment, 2.0.95RC

The 2.0.94 device sample showed core 0 at 24%, core 1 at 74%, with the MQTT
task using 17% of one core. Equal core percentages are not an objective by
themselves. This test moves only synchronous PubSubClient publishes to core 0.

## Ownership and limits

- MQTT state/discovery serialization, connects, callbacks and client loop stay
  on the original core-1 task. HTTP/OTA, bus scheduling and logic are unchanged.
- A low-priority worker on core 0 handles one publish at a time. It sleeps
  between messages, including discovery bursts, to allow IDLE0 to run.
- The caller retains the recursive MQTT configuration mutex while waiting.
  Client configuration, reconnects and restarts cannot overlap a publish.
  Offline publications during controlled restart now explicitly take that lock.
- Topic/payload pointers remain borrowed until completion. There is no
  asynchronous payload backlog, timeout that releases live buffers, additional
  publish retry, retained flag change or payload copy.
- A task-allocation failure uses the previous direct caller path. Set
  `MASTER_MQTT_PUBLISH_CORE0=0` at build time for the same comparison path.
- An additional 8192-byte internal task stack, TCB and static synchronization
  objects are required. This is a tradeoff, not a memory optimization.
  The stack is allocated only once MQTT is enabled, including enable-after-boot;
  after disabling MQTT, an already started worker remains blocked and reusable.
- Runtime diagnostics show `mqtt_send` / MQTT publish on core 0 separately.
  Publish/transport wall-time measurements still cover the actual client call,
  not time spent waiting for the worker. Poll/publish interval constants stay
  unchanged. No CPU or latency improvement is claimed before device testing.

## Verification

Native tests use the actual worker header with a threaded FreeRTOS test shim:
200 concurrent handoffs, per-caller order, exact topic/payload/retained/result,
failure propagation without duplicate sends, allocation-failure fallback,
interrupted completion waits, borrowed buffer lifetime and per-message yields.
The actual runtime sampler and DE/EN rendering tests include the fifth task
and its core-0 CPU label. Python regressions and ESP32 syntax checks are run.

This is not a hardware/TLS/OTA soak test or a linked firmware build. After
building/flashing only the master, compare both core percentages, the two MQTT
task loads, failed publishes, heap and bus latency against 2.0.94 under the
same workload. Exercise broker disconnect/reconnect, retained discovery,
Bluetooth pairing and master/module OTA before considering it release-ready.
