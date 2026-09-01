#include "Rs485PeripheralBus.h"

#if defined(ESP32)
#include <Esp.h>
#endif

namespace jbc_rs485 {

uint16_t crc16_modbus(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; ++b) {
      if (crc & 1) crc = (crc >> 1) ^ 0xA001;
      else crc >>= 1;
    }
  }
  return crc;
}

uint64_t esp_uid64() {
#if defined(ESP32)
  return ESP.getEfuseMac();
#else
  return 0;
#endif
}

void put_u16_le(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)(v >> 8);
}

void put_u32_le(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

void put_u64_le(uint8_t* p, uint64_t v) {
  for (uint8_t i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i));
}

uint16_t get_u16_le(const uint8_t* p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

uint32_t get_u32_le(const uint8_t* p) {
  return (uint32_t)p[0] |
         ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

uint64_t get_u64_le(const uint8_t* p) {
  uint64_t v = 0;
  for (uint8_t i = 0; i < 8; ++i) v |= ((uint64_t)p[i] << (8 * i));
  return v;
}

void Parser::reset() {
  len_ = 0;
  in_frame_ = false;
  escaped_ = false;
}

void Parser::resetStats() {
  stats_ = BusStats();
}

bool Parser::input(uint8_t byte, Frame& out) {
  if (byte == SOF) {
    if (in_frame_ && len_ > 0) {
      const bool ok = decodeBuffer(out);
      len_ = 0;
      escaped_ = false;
      in_frame_ = true;
      return ok;
    }
    in_frame_ = true;
    len_ = 0;
    escaped_ = false;
    return false;
  }

  if (!in_frame_) return false;

  if (escaped_) {
    if (byte == ESC_SOF) byte = SOF;
    else if (byte == ESC_ESC) byte = ESC;
    else {
      stats_.escape_errors++;
      reset();
      return false;
    }
    escaped_ = false;
  } else if (byte == ESC) {
    escaped_ = true;
    return false;
  }

  if (len_ >= sizeof(buf_)) {
    stats_.overflow_errors++;
    reset();
    return false;
  }
  buf_[len_++] = byte;
  return false;
}

bool Parser::decodeBuffer(Frame& out) {
  if (len_ < 8) {
    stats_.short_frames++;
    return false;
  }
  if (buf_[0] != PROTOCOL_VERSION) {
    stats_.bad_version++;
    return false;
  }

  const uint8_t payload_len = buf_[5];
  const size_t expected = (size_t)6 + payload_len + 2;
  if (payload_len > MAX_PAYLOAD || len_ != expected) {
    stats_.bad_length++;
    return false;
  }

  const uint16_t rx_crc = get_u16_le(buf_ + 6 + payload_len);
  const uint16_t calc_crc = crc16_modbus(buf_, 6 + payload_len);
  if (rx_crc != calc_crc) {
    stats_.crc_errors++;
    return false;
  }

  stats_.rx_frames++;
  out.dst = buf_[1];
  out.src = buf_[2];
  out.seq = buf_[3];
  out.cmd = buf_[4];
  out.len = payload_len;
  if (payload_len) memcpy(out.payload, buf_ + 6, payload_len);
  return true;
}

void Link::writeEscaped(uint8_t byte) {
  if (byte == SOF) {
    stream_.write(ESC);
    stream_.write(ESC_SOF);
  } else if (byte == ESC) {
    stream_.write(ESC);
    stream_.write(ESC_ESC);
  } else {
    stream_.write(byte);
  }
}

void Link::send(const Frame& frame) {
  if (frame.len > MAX_PAYLOAD) { parser_.countOverflow(); return; }
  if (send_route_ && send_route_(context_, frame)) { tx_network_ = true; return; }
  sendPhysical(frame);
}

void Link::sendPhysical(const Frame& frame) {
  tx_network_ = false;
  if (frame.len > MAX_PAYLOAD) {
    parser_.countOverflow();
    return;
  }
  uint8_t raw[MAX_PAYLOAD + 8];
  size_t n = 0;
  raw[n++] = PROTOCOL_VERSION;
  raw[n++] = frame.dst;
  raw[n++] = frame.src;
  raw[n++] = frame.seq;
  raw[n++] = frame.cmd;
  raw[n++] = frame.len;
  if (frame.len) {
    memcpy(raw + n, frame.payload, frame.len);
    n += frame.len;
  }
  const uint16_t crc = crc16_modbus(raw, n);
  raw[n++] = (uint8_t)(crc & 0xFF);
  raw[n++] = (uint8_t)(crc >> 8);

  stream_.write(SOF);
  for (size_t i = 0; i < n; ++i) writeEscaped(raw[i]);
  stream_.write(SOF);
  stream_.flush();
  parser_.countTxFrame();
  if (activity_cb_) activity_cb_();
}

bool Link::poll(Frame& out) {
  // Keep callers cooperative. A noisy/bursty bus must not monopolize the CPU
  // until the UART FIFO is empty; parser state is preserved across calls.
  uint16_t budget = 256;
  while (budget-- && stream_.available()) {
    if (parser_.input((uint8_t)stream_.read(), out)) {
      rx_network_ = false;
      if (serial_filter_ && !serial_filter_(context_, out)) continue;
      if (activity_cb_) activity_cb_();
      return true;
    }
  }
  if (poll_route_ && poll_route_(context_, out)) { rx_network_ = true; return true; }
  return false;
}

} // namespace jbc_rs485
