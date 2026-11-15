#pragma once

#include <Arduino.h>

// Observe the existing transport; never bypass PubSubClient, retry or split writes.
template <typename Base, bool (*Enabled)(), void (*Record)(uint32_t, size_t, size_t)>
class OfeMqttTransportClient : public Base {
public:
  size_t write(const uint8_t* data, size_t size) override {
    if (!Enabled()) return Base::write(data, size);
    const uint32_t start = micros();
    const size_t sent = Base::write(data, size);
    Record(uint32_t(micros() - start), size, sent);
    return sent;
  }
  size_t write(uint8_t data) override { return write(&data, 1); }
};
