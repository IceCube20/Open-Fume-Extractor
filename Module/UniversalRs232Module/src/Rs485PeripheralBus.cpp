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
  last_length_error_ = LengthErrorSnapshot();
  last_version_error_ = VersionErrorSnapshot();
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
    last_version_error_.actual_length = (uint16_t)len_;
    memcpy(last_version_error_.header, buf_, sizeof(last_version_error_.header));
    return false;
  }

  const uint8_t payload_len = buf_[5];
  const size_t expected = (size_t)6 + payload_len + 2;
  if (payload_len > MAX_PAYLOAD || len_ != expected) {
    stats_.bad_length++;
    last_length_error_ = LengthErrorSnapshot();
    last_length_error_.actual_length = (uint16_t)len_;
    last_length_error_.expected_length = (uint16_t)expected;
    memcpy(last_length_error_.header, buf_, sizeof(last_length_error_.header));
    // Inspect possible joined contents for diagnostics only; never recover them.
    if (payload_len <= MAX_PAYLOAD && expected <= len_) {
      last_length_error_.first_crc_checked = true;
      last_length_error_.first_crc_valid =
          crc16_modbus(buf_, expected - 2) == get_u16_le(buf_ + expected - 2);
      const size_t remaining = len_ - expected;
      last_length_error_.remaining_length = (uint16_t)remaining;
      if (remaining >= sizeof(last_length_error_.second_header)) {
        last_length_error_.second_header_present = true;
        memcpy(last_length_error_.second_header, buf_ + expected,
               sizeof(last_length_error_.second_header));
        const uint8_t second_payload = buf_[expected + 5];
        const size_t second_expected = (size_t)second_payload + 8;
        last_length_error_.second_expected_length = (uint16_t)second_expected;
        if (second_payload <= MAX_PAYLOAD && remaining == second_expected) {
          last_length_error_.second_crc_checked = true;
          last_length_error_.second_crc_valid =
              crc16_modbus(buf_ + expected, remaining - 2) ==
              get_u16_le(buf_ + len_ - 2);
        }
      }
    }
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
  // Guard only the physical path, including a failed network-route fallback.
  if (physical_send_guard_) physical_send_guard_(physical_guard_context_);
  tx_network_ = false;
  uint8_t raw[2 * (MAX_PAYLOAD + 8) + 2];
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

  // Expand backwards in place so escaping needs only one bounded stack buffer.
  size_t begin = sizeof(raw);
  raw[--begin] = SOF;
  while (n) {
    const uint8_t byte = raw[--n];
    if (byte == SOF || byte == ESC) {
      raw[--begin] = byte == SOF ? ESC_SOF : ESC_ESC;
      raw[--begin] = ESC;
    } else {
      raw[--begin] = byte;
    }
  }
  raw[--begin] = SOF;
  stream_.write(raw + begin, sizeof(raw) - begin);
  stream_.flush();
  parser_.countTxFrame();
  if (activity_cb_) activity_cb_();
}

void Link::sendWakePreamble(uint8_t bytes) {
  if (physical_send_guard_) physical_send_guard_(physical_guard_context_);
  tx_network_ = false;
  if (bytes < 4) bytes = 4;
  while (bytes--) stream_.write((uint8_t)0x55);
  stream_.flush();
  if (activity_cb_) activity_cb_();
}

bool Link::poll(Frame& out) {
  // Keep callers cooperative. A noisy/bursty bus must not monopolize the CPU
  // until the UART FIFO is empty; parser state is preserved across calls.
  uint16_t budget = 256;
  while (budget-- && stream_.available()) {
    if (parser_.input((uint8_t)stream_.read(), out)) {
      rx_network_ = false;
      const bool filtered = serial_filter_ && !serial_filter_(context_, out);
      last_physical_frame_ = {out.dst, out.src, out.seq, out.cmd, out.len, filtered};
      if (filtered) continue;
      if (activity_cb_) activity_cb_();
      return true;
    }
  }
  if (poll_route_ && poll_route_(context_, out)) { rx_network_ = true; return true; }
  return false;
}

} // namespace jbc_rs485
