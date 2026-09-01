#pragma once

#include <Arduino.h>

namespace jbc_rs485 {

static const uint8_t SOF = 0x7E;
static const uint8_t ESC = 0x7D;
static const uint8_t ESC_SOF = 0x5E;
static const uint8_t ESC_ESC = 0x5D;

static const uint8_t PROTOCOL_VERSION = 0x01;
static const uint8_t ADDR_BROADCAST = 0x00;
static const uint8_t ADDR_MASTER = 0x01;
static const uint8_t ADDR_FACTORY = 0x70;
static const uint8_t ADDR_INVALID = 0xFF;

// Open Fume Extractor module address map. Keep these helpers in the shared
// bus header so Master, Display and all peripheral modules stay protocol-
// compatible when a new module family is added.
static const uint8_t ADDR_JBC_MIN = 0x10;
static const uint8_t ADDR_JBC_MAX = 0x1F;
static const uint8_t ADDR_FAN_IO_MIN = 0x20;
static const uint8_t ADDR_FAN_IO_MAX = 0x2F;
static const uint8_t ADDR_WELLER_MIN = 0x30;
static const uint8_t ADDR_WELLER_MAX = 0x3F;
static const uint8_t ADDR_DISPLAY_MIN = 0x40;
static const uint8_t ADDR_DISPLAY_MAX = 0x4F;
static const uint8_t ADDR_UNIVERSAL_MIN = 0x50;
static const uint8_t ADDR_UNIVERSAL_MAX = 0x5F;
static const uint8_t ADDR_MODBUS_MIN = 0x60;
static const uint8_t ADDR_MODBUS_MAX = 0x6F;

inline bool ofe_addr_in_range(uint8_t addr, uint8_t first, uint8_t last) {
  return addr >= first && addr <= last;
}
inline bool ofe_addr_is_jbc(uint8_t addr) { return ofe_addr_in_range(addr, ADDR_JBC_MIN, ADDR_JBC_MAX); }
inline bool ofe_addr_is_fan_io(uint8_t addr) { return ofe_addr_in_range(addr, ADDR_FAN_IO_MIN, ADDR_FAN_IO_MAX); }
inline bool ofe_addr_is_weller(uint8_t addr) { return ofe_addr_in_range(addr, ADDR_WELLER_MIN, ADDR_WELLER_MAX); }
inline bool ofe_addr_is_display(uint8_t addr) { return ofe_addr_in_range(addr, ADDR_DISPLAY_MIN, ADDR_DISPLAY_MAX); }
inline bool ofe_addr_is_universal(uint8_t addr) { return ofe_addr_in_range(addr, ADDR_UNIVERSAL_MIN, ADDR_UNIVERSAL_MAX); }
inline bool ofe_addr_is_modbus(uint8_t addr) { return ofe_addr_in_range(addr, ADDR_MODBUS_MIN, ADDR_MODBUS_MAX); }
inline bool ofe_addr_is_module(uint8_t addr) { return ofe_addr_in_range(addr, ADDR_JBC_MIN, ADDR_MODBUS_MAX); }

static const uint8_t MAX_PAYLOAD = 192;

// ModuleType numeric values are part of the RS485 wire protocol.
// Keep the values stable. Names mirror the actual OFE module projects.
// 0x04 is reserved for the historical/planned sensor type; no SensorModule
// currently exists in this repository.
enum ModuleType : uint8_t {
  MODULE_UNKNOWN = 0x00,
  MODULE_JBC_BUS = 0x01,
  MODULE_FAN_IO = 0x02,
  MODULE_FAN_IO_PRO = 0x03,
  MODULE_SENSOR_RESERVED = 0x04,
  MODULE_WELLER_ZERO_SMOG = 0x05,
  MODULE_DISPLAY = 0x06,
  MODULE_UNIVERSAL_RS232 = 0x07,
  MODULE_MODBUS_RTU = 0x08,
  MODULE_JBC_USB = 0x09,
};

enum Command : uint8_t {
  CMD_PING = 0x01,
  CMD_INFO = 0x02,
  CMD_GET_CAPS = 0x03,
  CMD_GET_STATUS = 0x04,
  CMD_GET_STATE = 0x05,
  CMD_SET_STATE = 0x06,
  CMD_GET_TELEMETRY = 0x07,
  CMD_FAST_POLL = 0x10,
  CMD_GET_EVENTS = 0x11,
  CMD_ACK_EVENTS = 0x12,
  CMD_LED_SYNC = 0x13,
  CMD_SET_ADDRESS = 0x20,
  CMD_SAVE_CONFIG = 0x21,
  CMD_FACTORY_RESET = 0x22,
  CMD_DISCOVER_MODULES = 0x23,
  CMD_SET_ADDRESS_UID = 0x24,
  CMD_SET_LABEL = 0x25,
  CMD_SET_ENABLE = 0x30,
  CMD_SET_POWER = 0x31,
  CMD_SET_TARGET_RPM = 0x32,
  CMD_SET_OUTPUT = 0x33,
  CMD_GET_IO = 0x34,
  CMD_SET_IO = 0x35,
  CMD_FILTER_CALIBRATION = 0x36,
  CMD_IO_LABEL = 0x37,
  CMD_JBC_USB_CONFIG = 0x38,
  CMD_FW_BEGIN = 0x40,
  CMD_FW_CHUNK = 0x41,
  CMD_FW_END = 0x42,
  CMD_FW_ABORT = 0x43,
  CMD_FW_STATUS = 0x44,
  CMD_FW_REBOOT = 0x45,
  CMD_DISPLAY_STATUS = 0x50,
  CMD_DISPLAY_EVENT = 0x51,
  CMD_DISPLAY_UPDATE = 0x52,
  CMD_DISPLAY_DETAIL_PAGE = 0x53,
  CMD_DISPLAY_ALARMS = 0x54,
  CMD_DISPLAY_MODULE_LIST = 0x55,
  CMD_DISPLAY_MODULE_DETAIL = 0x56,
  CMD_DISPLAY_CONFIG = 0x57,
  CMD_TRACE_CONTROL = 0x60,
  CMD_TRACE_READ = 0x61,
  CMD_DESCRIPTOR_GET = 0x70,
  CMD_ENTITY_GET = 0x71,
  CMD_ENTITY_SET = 0x72,
  CMD_ENTITY_EVENT = 0x73,
  CMD_FAULT_MAP_GET = 0x74,
  CMD_PROFILE_BEGIN = 0x75,
  CMD_PROFILE_CHUNK = 0x76,
  CMD_PROFILE_END = 0x77,
  CMD_PROFILE_GET = 0x78,
  CMD_ERROR = 0xFF,
};

enum JbcUsbConfigAction : uint8_t {
  JBC_USB_CONFIG_STATION_NAME = 0x01,
};


// Optional CMD_GET_IO request flags.
// A legacy zero-length request means "full" for backward compatibility.
// New masters may send a one-byte flags payload and omit static alias strings
// from high-rate live polls while still requesting them during scan/config.
enum IoQueryFlags : uint8_t {
  IO_QUERY_INCLUDE_ALIASES = 0x01,
};

enum Status : uint8_t {
  STATUS_OK = 0x00,
  STATUS_UNKNOWN_CMD = 0x01,
  STATUS_BAD_LEN = 0x02,
  STATUS_BAD_VALUE = 0x03,
  STATUS_BUSY = 0x04,
  STATUS_CRC_ERROR = 0x05,
  STATUS_NOT_SUPPORTED = 0x06,
};

enum Caps : uint32_t {
  CAP_JBC_BUS = 1UL << 0,
  CAP_JBC_DUAL_BUS = 1UL << 1,
  CAP_PWM_OUTPUT = 1UL << 2,
  CAP_TACHO_INPUT = 1UL << 3,
  CAP_CLOSED_LOOP_RPM = 1UL << 4,
  CAP_RELAY_OUTPUT = 1UL << 5,
  CAP_ANALOG_INPUT = 1UL << 6,
  CAP_PRESSURE_SENSOR = 1UL << 7,
  CAP_FILTER_SENSOR = 1UL << 8,
  CAP_WELLER_INTERFACE = 1UL << 9,
  CAP_DISPLAY = 1UL << 10,
  CAP_INPUT_KEYS = 1UL << 11,
  CAP_FW_UPDATE = 1UL << 12,
  CAP_FAULT_REPORT = 1UL << 13,
  CAP_DIGITAL_OUTPUT = 1UL << 14,
  CAP_LOCAL_TRACE = 1UL << 15,
  CAP_DESCRIPTOR = 1UL << 16,
  CAP_ENTITY_CONTROL = 1UL << 17,
  CAP_ENTITY_EVENTS = 1UL << 18,
  CAP_LOCAL_PROTOCOL = 1UL << 19,
  CAP_FAULT_MAP = 1UL << 20,
  CAP_MODBUS_RTU = 1UL << 21,
  CAP_DISPLAY_320X480 = 1UL << 22,
  CAP_DISPLAY_800X480 = 1UL << 23,
  CAP_JBC_USB = 1UL << 24,
  CAP_DISPLAY_HYBRID = 1UL << 25,
};

static const uint32_t CAP_JBC_ACTIVITY = CAP_JBC_BUS | CAP_JBC_USB;

enum FastPollFlags : uint8_t {
  FAST_FLAG_CONNECTED = 1U << 0,
  FAST_FLAG_CONTINUOUS = 1U << 1,
  FAST_FLAG_ERROR_PENDING = 1U << 2,
  FAST_FLAG_EVENT_PENDING = 1U << 3,
  FAST_FLAG_STATE_CHANGED = 1U << 4,
};

struct Frame {
  uint8_t dst = ADDR_INVALID;
  uint8_t src = ADDR_INVALID;
  uint8_t seq = 0;
  uint8_t cmd = 0;
  uint8_t len = 0;
  uint8_t payload[MAX_PAYLOAD];
};

struct FastPollState {
  uint16_t event_seq = 0;
  uint8_t work_mask = 0;
  uint8_t stand_mask = 0;
  uint8_t flags = 0;
};

struct BusStats {
  uint32_t rx_frames = 0;
  uint32_t tx_frames = 0;
  uint32_t crc_errors = 0;
  uint32_t bad_length = 0;
  uint32_t bad_version = 0;
  uint32_t escape_errors = 0;
  uint32_t overflow_errors = 0;
  uint32_t short_frames = 0;
};

uint16_t crc16_modbus(const uint8_t* data, size_t len);
uint64_t esp_uid64();

void put_u16_le(uint8_t* p, uint16_t v);
void put_u32_le(uint8_t* p, uint32_t v);
void put_u64_le(uint8_t* p, uint64_t v);
uint16_t get_u16_le(const uint8_t* p);
uint32_t get_u32_le(const uint8_t* p);
uint64_t get_u64_le(const uint8_t* p);

class Parser {
public:
  void reset();
  void resetStats();
  const BusStats& stats() const { return stats_; }
  void countTxFrame() { stats_.tx_frames++; }
  void countOverflow() { stats_.overflow_errors++; }
  bool input(uint8_t byte, Frame& out);

private:
  bool decodeBuffer(Frame& out);

  uint8_t buf_[MAX_PAYLOAD + 9];
  size_t len_ = 0;
  bool in_frame_ = false;
  bool escaped_ = false;
  BusStats stats_;
};

class Link {
public:
  using ActivityCallback = void (*)();
  explicit Link(Stream& stream) : stream_(stream) {}

  void send(const Frame& frame);
  void sendPhysical(const Frame& frame);
  bool lastTxWasNetwork() const { return tx_network_; }
  bool lastRxWasNetwork() const { return rx_network_; }
  using SendRoute = bool (*)(void*, const Frame&);
  using PollRoute = bool (*)(void*, Frame&);
  using SerialFilter = bool (*)(void*, const Frame&);
  void setTransportHooks(void* context, SendRoute send, PollRoute poll, SerialFilter serial) {
    context_ = context; send_route_ = send; poll_route_ = poll; serial_filter_ = serial;
  }
  bool poll(Frame& out);
  const BusStats& stats() const { return parser_.stats(); }
  void resetStats() { parser_.resetStats(); }

  void setActivityCallback(ActivityCallback cb) { activity_cb_ = cb; }

private:
  void writeEscaped(uint8_t byte);

  Stream& stream_;
  Parser parser_;
  ActivityCallback activity_cb_ = nullptr;
  void* context_ = nullptr;
  SendRoute send_route_ = nullptr;
  PollRoute poll_route_ = nullptr;
  SerialFilter serial_filter_ = nullptr;
  bool tx_network_ = false;
  bool rx_network_ = false;
};

} // namespace jbc_rs485
