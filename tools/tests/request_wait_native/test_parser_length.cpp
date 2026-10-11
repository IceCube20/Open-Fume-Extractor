#include <cassert>
#include <cstdio>
#include <vector>
#include <random>
#include "Rs485PeripheralBus.h"

using namespace jbc_rs485;

class MemoryStream : public Stream {
public:
  std::vector<uint8_t> bytes;
  size_t pos = 0;
  unsigned byte_calls = 0, bulk_calls = 0, flush_calls = 0;
  size_t write(uint8_t byte) override { ++byte_calls; bytes.push_back(byte); return 1; }
  size_t write(const uint8_t* data, size_t size) override {
    ++bulk_calls;
    bytes.insert(bytes.end(), data, data + size);
    return size;
  }
  int available() override { return (int)(bytes.size() - pos); }
  int read() override { return pos < bytes.size() ? bytes[pos++] : -1; }
  void flush() override { ++flush_calls; }
};

struct RoutingContext { unsigned filters = 0; bool network_pending = true; };
static bool dropPhysical(void* context, const Frame&) {
  ++static_cast<RoutingContext*>(context)->filters;
  return false;
}
static bool pollNetwork(void* context, Frame& out) {
  auto& state = *static_cast<RoutingContext*>(context);
  if (!state.network_pending) return false;
  state.network_pending = false;
  out.dst = ADDR_MASTER;
  out.src = 0x40;
  out.seq = 17;
  out.cmd = 0xD0;
  out.len = 0;
  return true;
}

static std::vector<uint8_t> frame(uint8_t declared, size_t payload_size) {
  std::vector<uint8_t> raw = {PROTOCOL_VERSION, ADDR_MASTER, 0x20, 0x7E, 0xB4, declared};
  raw.insert(raw.end(), payload_size, 0x7D);
  const uint16_t crc = crc16_modbus(raw.data(), raw.size());
  raw.push_back((uint8_t)crc);
  raw.push_back((uint8_t)(crc >> 8));
  return raw;
}

static bool feed(Parser& parser, const std::vector<uint8_t>& raw, Frame& out) {
  bool accepted = false;
  parser.input(SOF, out);
  for (uint8_t byte : raw) {
    if (byte == SOF || byte == ESC) {
      parser.input(ESC, out);
      byte = byte == SOF ? ESC_SOF : ESC_ESC;
    }
    accepted |= parser.input(byte, out);
  }
  return parser.input(SOF, out) || accepted;
}

static std::vector<uint8_t> referenceEncoding(const Frame& input) {
  std::vector<uint8_t> raw = {1, input.dst, input.src, input.seq, input.cmd, input.len};
  raw.insert(raw.end(), input.payload, input.payload + input.len);
  const uint16_t crc = crc16_modbus(raw.data(), raw.size());
  raw.push_back((uint8_t)crc);
  raw.push_back((uint8_t)(crc >> 8));
  std::vector<uint8_t> wire = {SOF};
  for (uint8_t byte : raw) {
    if (byte == SOF || byte == ESC) {
      wire.push_back(ESC);
      wire.push_back(byte == SOF ? ESC_SOF : ESC_ESC);
    } else wire.push_back(byte);
  }
  wire.push_back(SOF);
  return wire;
}

static bool acceptNetwork(void*, const Frame&) { return true; }

struct GuardContext {
  MemoryStream* stream;
  Link* link;
  unsigned calls = 0;
  unsigned expected_writes = 0;
  bool nested_network = false;
};

static void beforePhysicalSend(void* context) {
  auto& guard = *static_cast<GuardContext*>(context);
  assert(guard.stream->bulk_calls == guard.expected_writes);
  ++guard.calls;
  if (guard.nested_network) {
    Frame frame;
    guard.link->send(frame);
    assert(guard.link->lastTxWasNetwork());
  }
}

static bool rejectNetwork(void*, const Frame&) { return false; }

static void testPhysicalSendGuard() {
  MemoryStream stream;
  Link link(stream);
  GuardContext guard{&stream, &link};
  RoutingContext route;
  link.setPhysicalSendGuard(&guard, beforePhysicalSend);
  link.setTransportHooks(&route, acceptNetwork, pollNetwork, dropPhysical);
  Frame input;
  input.len = 0;
  link.send(input);
  assert(guard.calls == 0 && stream.bulk_calls == 0 && link.lastTxWasNetwork());
  Frame received;
  assert(link.poll(received) && received.src == 0x40);
  assert(!route.network_pending);

  guard.nested_network = true;
  link.sendPhysical(input);
  assert(guard.calls == 1 && stream.bulk_calls == 1 && !link.lastTxWasNetwork());
  guard.nested_network = false;
  guard.expected_writes = 1;
  link.setTransportHooks(&route, rejectNetwork, nullptr, nullptr);
  link.send(input);
  assert(guard.calls == 2 && stream.bulk_calls == 2 && !link.lastTxWasNetwork());
  guard.expected_writes = 2;
  link.sendWakePreamble(4);
  assert(guard.calls == 3 && stream.byte_calls == 4);

  input.len = MAX_PAYLOAD + 1;
  link.send(input);
  link.sendPhysical(input);
  assert(guard.calls == 3 && stream.bulk_calls == 2);
  assert(link.stats().overflow_errors == 2);
  link.setPhysicalSendGuard(nullptr, nullptr);
  input.len = 0;
  link.send(input);
  assert(guard.calls == 3 && stream.bulk_calls == 3);
}

static void testEncoder() {
  std::mt19937 random(29);
  bool escaped_crc_seen = false;
  for (unsigned trial = 0; trial < 10000; ++trial) {
    Frame input;
    input.dst = (uint8_t)random();
    input.src = (uint8_t)random();
    input.seq = (uint8_t)random();
    input.cmd = (uint8_t)random();
    input.len = (uint8_t)(trial % (MAX_PAYLOAD + 1));
    for (unsigned i = 0; i < input.len; ++i)
      input.payload[i] = trial < 193 ? (i % 2 ? ESC : SOF) : (uint8_t)random();
    MemoryStream stream;
    Link link(stream);
    link.sendPhysical(input);
    const auto expected = referenceEncoding(input);
    assert(stream.bytes == expected);
    assert(stream.byte_calls == 0 && stream.bulk_calls == 1 && stream.flush_calls == 1);
    assert(link.stats().tx_frames == 1 && !link.lastTxWasNetwork());
    Parser parser;
    Frame output;
    unsigned accepted = 0;
    for (uint8_t byte : stream.bytes) accepted += parser.input(byte, output) ? 1 : 0;
    assert(accepted == 1);
    assert(output.dst == input.dst && output.src == input.src);
    assert(output.seq == input.seq && output.cmd == input.cmd && output.len == input.len);
    assert(memcmp(output.payload, input.payload, input.len) == 0);
    escaped_crc_seen |= expected[expected.size() - 3] == ESC ||
                        (expected.size() >= 5 && expected[expected.size() - 5] == ESC);
  }
  assert(escaped_crc_seen);
  MemoryStream stream;
  Link link(stream);
  Frame input;
  input.len = MAX_PAYLOAD;
  memset(input.payload, ESC, input.len);
  input.dst = SOF; input.src = ESC; input.seq = SOF; input.cmd = ESC;
  link.sendPhysical(input);
  link.sendPhysical(input);
  assert(stream.bulk_calls == 2 && stream.byte_calls == 0);
  Parser parser;
  Frame output;
  unsigned accepted = 0;
  for (uint8_t byte : stream.bytes) accepted += parser.input(byte, output) ? 1 : 0;
  assert(accepted == 2 && parser.stats().bad_length == 0);
  const auto saved = stream.bytes;
  link.setTransportHooks(nullptr, acceptNetwork, nullptr, nullptr);
  link.send(input);
  assert(link.lastTxWasNetwork() && stream.bytes == saved && stream.bulk_calls == 2);
  input.len = MAX_PAYLOAD + 1;
  link.send(input);
  link.sendPhysical(input);
  assert(stream.bytes == saved && link.stats().overflow_errors == 2);
}

static void testJoinedContents() {
  Parser parser;
  Frame output;
  output.src = 0x42;
  const auto first = frame(9, 9);
  auto second = first;
  second[3] = 0x9D;
  put_u16_le(second.data() + second.size() - 2,
             crc16_modbus(second.data(), second.size() - 2));
  for (bool identical : {false, true}) {
    auto joined = first;
    const auto& tail = identical ? first : second;
    joined.insert(joined.end(), tail.begin(), tail.end());
    assert(!feed(parser, joined, output));
    const auto& error = parser.lastLengthError();
    assert(error.actual_length == 34 && error.expected_length == 17);
    assert(error.first_crc_checked && error.first_crc_valid);
    assert(error.remaining_length == 17 && error.second_expected_length == 17);
    assert(error.second_header_present && error.second_crc_checked && error.second_crc_valid);
    assert(memcmp(error.second_header, tail.data(), 6) == 0);
    for (unsigned corrupt_offset : {6u, 23u}) {
      auto corrupt = joined;
      corrupt[corrupt_offset] ^= 1;
      assert(!feed(parser, corrupt, output));
      assert(parser.lastLengthError().first_crc_valid == (corrupt_offset != 6));
      assert(parser.lastLengthError().second_crc_valid == (corrupt_offset != 23));
    }
    joined.pop_back();
    assert(!feed(parser, joined, output));
    assert(parser.lastLengthError().first_crc_valid);
    assert(!parser.lastLengthError().second_crc_checked);
    assert(parser.lastLengthError().remaining_length == 16);
  }
  auto bad_version = first;
  second[0] = 2;
  put_u16_le(second.data() + second.size() - 2,
             crc16_modbus(second.data(), second.size() - 2));
  bad_version.insert(bad_version.end(), second.begin(), second.end());
  assert(!feed(parser, bad_version, output));
  assert(parser.lastLengthError().second_header[0] == 2);
  assert(parser.lastLengthError().second_crc_valid);
  auto short_tail = first;
  short_tail.push_back(1);
  assert(!feed(parser, short_tail, output));
  assert(parser.lastLengthError().first_crc_valid);
  assert(!parser.lastLengthError().second_header_present);
  assert(!feed(parser, frame(9, 7), output));
  assert(!parser.lastLengthError().first_crc_checked);
  assert(!parser.lastLengthError().second_header_present);
  assert(parser.lastLengthError().remaining_length == 0);
  assert(!feed(parser, frame(255, 7), output));
  assert(!parser.lastLengthError().first_crc_checked);
  assert(output.src == 0x42 && parser.stats().rx_frames == 0);
  assert(parser.stats().crc_errors == 0 && parser.stats().bad_version == 0);
  parser.resetStats();
  assert(!parser.lastLengthError().first_crc_checked);
  assert(!parser.lastLengthError().second_header_present);
}

int main() {
  testPhysicalSendGuard();
  testEncoder();
  testJoinedContents();
  Parser parser;
  Frame out;
  out.src = 0x42;
  out.len = 0x17;
  assert(!feed(parser, frame(9, 7), out));
  assert(parser.stats().bad_length == 1);
  assert(parser.stats().rx_frames == 0);
  assert(out.src == 0x42 && out.len == 0x17);
  const LengthErrorSnapshot saved = parser.lastLengthError();
  assert(saved.actual_length == 15 && saved.expected_length == 17);
  const uint8_t expected_header[] = {1, 1, 0x20, 0x7E, 0xB4, 9};
  assert(memcmp(saved.header, expected_header, sizeof(expected_header)) == 0);

  // Valid, CRC-invalid and short frames must not overwrite a LEN observation.
  assert(feed(parser, frame(7, 7), out));
  assert(out.src == 0x20 && out.seq == 0x7E && out.len == 7);
  auto corrupt = frame(7, 7);
  corrupt.back() ^= 1;
  assert(!feed(parser, corrupt, out));
  assert(parser.stats().crc_errors == 1);
  assert(!feed(parser, {1, 1, 0x20, 3, 0xB4, 9, 0}, out));
  assert(parser.stats().short_frames == 1);
  auto wrong_version = frame(9, 7);
  wrong_version[0] = 2;
  assert(!feed(parser, wrong_version, out));
  assert(parser.stats().bad_version == 1);
  assert(parser.lastVersionError().actual_length == 15);
  assert(parser.lastVersionError().header[0] == 2);
  assert(parser.lastVersionError().header[2] == 0x20);
  const VersionErrorSnapshot saved_version = parser.lastVersionError();
  assert(!feed(parser, {3, 1, 0x20, 3, 0xB4, 9, 0}, out));
  assert(parser.stats().bad_version == 1);
  assert(parser.stats().short_frames == 2);
  assert(memcmp(saved.header, parser.lastLengthError().header, 6) == 0);
  assert(parser.lastLengthError().actual_length == saved.actual_length);

  // Deliberately invalid escapes and parser overflow still recover at SOF.
  parser.input(SOF, out);
  parser.input(ESC, out);
  parser.input(0, out);
  assert(parser.stats().escape_errors == 1);
  assert(!feed(parser, frame(MAX_PAYLOAD, MAX_PAYLOAD + 2), out));
  assert(parser.stats().overflow_errors == 1);
  assert(parser.lastLengthError().expected_length == saved.expected_length);
  assert(feed(parser, frame(MAX_PAYLOAD, MAX_PAYLOAD), out));
  assert(out.len == MAX_PAYLOAD);
  assert(parser.lastVersionError().header[0] == saved_version.header[0]);
  assert(parser.lastVersionError().actual_length == saved_version.actual_length);
  wrong_version = frame(7, 7);
  wrong_version[0] = SOF;
  assert(!feed(parser, wrong_version, out));
  assert(parser.lastVersionError().header[0] == SOF);
  assert(saved_version.header[0] == 2);

  // Extra bytes and impossible payload lengths are captured, not accepted.
  assert(!feed(parser, frame(7, 9), out));
  assert(parser.lastLengthError().actual_length == 17);
  assert(parser.lastLengthError().expected_length == 15);
  assert(!feed(parser, frame(255, 7), out));
  assert(parser.lastLengthError().expected_length == 263);
  assert(parser.lastLengthError().header[5] == 255);
  parser.reset();
  assert(parser.lastLengthError().expected_length == 263);
  parser.resetStats();
  assert(parser.stats().bad_length == 0);
  assert(parser.lastLengthError().actual_length == 0);
  assert(parser.lastLengthError().expected_length == 0);
  assert(parser.lastVersionError().actual_length == 0);
  for (uint8_t byte : parser.lastVersionError().header) assert(byte == 0);
  for (uint8_t byte : parser.lastLengthError().header) assert(byte == 0);
  assert(feed(parser, frame(0, 0), out));
  assert(parser.stats().rx_frames == 1);

  MemoryStream stream;
  Link link(stream);
  Frame sent;
  sent.dst = 0x20;
  sent.src = ADDR_MASTER;
  sent.seq = 0x7E;
  sent.cmd = CMD_SET_IO;
  sent.len = 0;
  link.sendPhysical(sent);
  RoutingContext routing;
  link.setTransportHooks(&routing, nullptr, pollNetwork, dropPhysical);
  assert(link.poll(out));
  assert(link.lastRxWasNetwork() && out.src == 0x40);
  assert(link.stats().rx_frames == 1 && routing.filters == 1);
  const PhysicalFrameSnapshot saved_physical = link.lastPhysicalFrame();
  assert(saved_physical.dst == 0x20 && saved_physical.src == ADDR_MASTER);
  assert(saved_physical.seq == 0x7E && saved_physical.cmd == CMD_SET_IO);
  assert(saved_physical.filtered && saved_physical.len == 0);
  assert(!link.poll(out));
  assert(link.lastPhysicalFrame().filtered);
  link.setTransportHooks(nullptr, nullptr, nullptr, nullptr);
  sent.src = 0x30;
  sent.dst = ADDR_MASTER;
  sent.seq = 33;
  link.sendPhysical(sent);
  assert(link.poll(out));
  assert(!link.lastRxWasNetwork() && out.src == 0x30);
  assert(!link.lastPhysicalFrame().filtered && link.lastPhysicalFrame().src == 0x30);
  assert(saved_physical.filtered && saved_physical.src == ADDR_MASTER);
  link.resetStats();
  assert(link.stats().rx_frames == 0);
  assert(link.lastPhysicalFrame().src == ADDR_INVALID);
  assert(!link.lastPhysicalFrame().filtered);

  std::puts("PASS: physical send guard/WLAN fallback, 10000 differential bulk encodings, strict joined-content rejection/CRC diagnostics, LEN/VERSION snapshots and WLAN separation");
}
