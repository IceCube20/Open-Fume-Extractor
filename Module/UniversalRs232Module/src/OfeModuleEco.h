#pragma once

#include "OfeStatusLed.h"
#include "Rs485PeripheralBus.h"

inline bool ofe_handle_power_save_command(
    const jbc_rs485::Frame& req,
    jbc_rs485::Link& bus,
    uint8_t module_addr,
    OfeStatusLed& leds,
    bool supports_light_sleep,
    bool& eco_mode,
    bool& light_sleep_armed) {
  using namespace jbc_rs485;
  if (req.cmd != CMD_POWER_SAVE) return false;

  Frame resp;
  resp.dst = req.src;
  resp.src = module_addr;
  resp.seq = req.seq;
  resp.cmd = CMD_POWER_SAVE | 0x80;
  if (req.len < 2) {
    resp.len = 1;
    resp.payload[0] = STATUS_BAD_LEN;
    bus.send(resp);
    return true;
  }

  eco_mode = req.payload[0] != 0;
  const uint8_t led_percent = constrain(req.payload[1], (uint8_t)1, (uint8_t)20);
  light_sleep_armed = eco_mode && supports_light_sleep && req.len >= 3 && req.payload[2] != 0;
  leds.setEcoMode(eco_mode, (uint8_t)((uint16_t)led_percent * 255U / 100U));
  resp.len = 3;
  resp.payload[0] = STATUS_OK;
  resp.payload[1] = eco_mode ? 1 : 0;
  resp.payload[2] = light_sleep_armed ? 1 : 0;
  bus.send(resp);
  return true;
}
