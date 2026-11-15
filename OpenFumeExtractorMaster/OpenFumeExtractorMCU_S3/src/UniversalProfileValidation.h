#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string_view>

namespace ofe_profile {

inline std::string_view trimProfileValue(std::string_view value) {
  while (!value.empty() && static_cast<unsigned char>(value.front()) <= 32) value.remove_prefix(1);
  while (!value.empty() && static_cast<unsigned char>(value.back()) <= 32) value.remove_suffix(1);
  return value;
}

// Match the module's first exact key= lookup, including CRLF and whitespace.
inline std::string_view profileValue(std::string_view text, std::string_view key) {
  while (!text.empty()) {
    const size_t end = text.find('\n');
    const auto line = trimProfileValue(text.substr(0, end));
    if (line.size() > key.size() && line.substr(0, key.size()) == key && line[key.size()] == '=') {
      return trimProfileValue(line.substr(key.size() + 1));
    }
    if (end == std::string_view::npos) break;
    text.remove_prefix(end + 1);
  }
  return {};
}

inline bool profileValueEqual(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    char c = a[i];
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (c != b[i]) return false;
  }
  return true;
}

enum class AccessError {
  None, InvalidAccess, MissingMatch, MissingCommand, InvalidType,
  InvalidFunction, InvalidReadFunction, MissingReadFunction, MissingWriteFunction,
  InvalidRegister, InvalidSlave
};
struct AccessValidation {
  AccessError error = AccessError::None;
  uint8_t entity = 0;
};

// Preflight only the RS232 access contract; Modbus uses registers, not match/set.
inline AccessValidation validateRs232Access(std::string_view text) {
  for (uint8_t i = 1; i <= 32; ++i) {
    auto get = [&](const char* field) {
      char key[32];
      snprintf(key, sizeof(key), "entity.%u.%s", static_cast<unsigned>(i), field);
      return profileValue(text, key);
    };
    const auto type = get("type"), poll = get("poll"), match = get("match");
    const bool has_command = !get("set").empty() || !get("set_on").empty() || !get("set_off").empty();
    if (type.empty() && get("name").empty() && poll.empty() && match.empty() &&
        !has_command && get("options").empty()) continue;
    auto access = get("access");
    if (access.empty()) access = get("mode");
    if (access.empty()) {
      if (profileValueEqual(type, "sensor") || profileValueEqual(type, "binary_sensor") || profileValueEqual(type, "binary")) access = "ro";
      else if (profileValueEqual(type, "button")) access = "wo";
      else if (has_command) access = (!poll.empty() || !match.empty()) ? "rw" : "wo";
      else access = "ro";
    }
    const bool readable = profileValueEqual(access, "ro") || profileValueEqual(access, "rw");
    const bool writable = profileValueEqual(access, "wo") || profileValueEqual(access, "rw");
    if (!readable && !writable) return {AccessError::InvalidAccess, i};
    if (readable && match.empty()) return {AccessError::MissingMatch, i};
    if (writable && !has_command) return {AccessError::MissingCommand, i};
  }
  return {};
}

inline uint8_t modbusFunction(std::string_view name) {
  if (profileValueEqual(name, "read_coil") || profileValueEqual(name, "read_coils") || profileValueEqual(name, "coil") || profileValueEqual(name, "0x01")) return 0x01;
  if (profileValueEqual(name, "read_discrete") || profileValueEqual(name, "read_discrete_input") || profileValueEqual(name, "discrete") || profileValueEqual(name, "0x02")) return 0x02;
  if (profileValueEqual(name, "read_holding") || profileValueEqual(name, "read_holding_register") || profileValueEqual(name, "holding") || profileValueEqual(name, "0x03")) return 0x03;
  if (profileValueEqual(name, "read_input") || profileValueEqual(name, "read_input_register") || profileValueEqual(name, "input") || profileValueEqual(name, "0x04")) return 0x04;
  if (profileValueEqual(name, "write_coil") || profileValueEqual(name, "write_single_coil") || profileValueEqual(name, "0x05")) return 0x05;
  if (profileValueEqual(name, "write_holding") || profileValueEqual(name, "write_register") || profileValueEqual(name, "write_single_register") || profileValueEqual(name, "0x06")) return 0x06;
  return 0;
}

inline bool profileUnsigned(std::string_view value, uint32_t maximum, bool register_address, uint32_t& out) {
  if (!value.empty() && value.front() == '+') value.remove_prefix(1);
  if (value.empty()) return false;
  unsigned base = 10;
  if (register_address && value.size() > 1 && value.front() == '0') {
    if (value[1] == 'x' || value[1] == 'X') { base = 16; value.remove_prefix(2); }
    else base = 8;  // The module uses strtoul(..., 0) for register addresses.
  }
  if (value.empty()) return false;
  uint32_t number = 0;
  for (const char c : value) {
    const unsigned digit = c >= '0' && c <= '9' ? c - '0'
      : c >= 'a' && c <= 'f' ? c - 'a' + 10
      : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 255;
    if (digit >= base || digit > maximum || number > (maximum - digit) / base) return false;
    number = number * base + digit;
  }
  out = number;
  return true;
}

inline AccessValidation validateModbusProfile(std::string_view text) {
  auto slave = profileValue(text, "slave");
  if (slave.empty()) slave = profileValue(text, "slave_id");
  uint32_t number = 0;
  if (!slave.empty() && (!profileUnsigned(slave, 247, false, number) || number == 0)) {
    return {AccessError::InvalidSlave, 0};
  }
  for (uint8_t i = 1; i <= 32; ++i) {
    auto get = [&](const char* field) {
      char key[32];
      snprintf(key, sizeof(key), "entity.%u.%s", static_cast<unsigned>(i), field);
      return profileValue(text, key);
    };
    auto type = get("type"), access = get("access");
    const auto func = get("func"), read_func = get("read_func");
    auto reg = get("reg");
    if (reg.empty()) reg = get("register");
    if (type.empty() && get("name").empty() && func.empty() && read_func.empty() &&
        reg.empty() && get("options").empty()) continue;
    uint8_t fc = modbusFunction(func), rfc = modbusFunction(read_func);
    if (type.empty()) type = (fc == 1 || fc == 2 || fc == 5) ? "switch"
      : !get("options").empty() ? "select" : "number";
    if (profileValueEqual(type, "binary")) type = "binary_sensor";
    if (!profileValueEqual(type, "sensor") && !profileValueEqual(type, "number") &&
        !profileValueEqual(type, "switch") && !profileValueEqual(type, "binary_sensor") &&
        !profileValueEqual(type, "text") && !profileValueEqual(type, "select") &&
        !profileValueEqual(type, "button")) return {AccessError::InvalidType, i};
    if (access.empty()) access = get("mode");
    if (access.empty()) {
      const bool readable = (fc >= 1 && fc <= 4) || (rfc >= 1 && rfc <= 4);
      const bool writable = fc == 5 || fc == 6;
      access = readable && writable ? "rw"
        : writable || profileValueEqual(type, "button") ? "wo" : "ro";
    }
    const bool readable = profileValueEqual(access, "ro") || profileValueEqual(access, "rw");
    const bool writable = profileValueEqual(access, "wo") || profileValueEqual(access, "rw");
    if (!readable && !writable) return {AccessError::InvalidAccess, i};
    const bool bit = profileValueEqual(type, "switch") || profileValueEqual(type, "binary_sensor");
    if (func.empty()) fc = writable ? (bit ? 5 : 6) : (bit ? 1 : 3);
    if (!fc) return {AccessError::InvalidFunction, i};
    if (!read_func.empty() && !rfc) return {AccessError::InvalidReadFunction, i};
    // Preserve the module's RW defaults and explicit read_func precedence.
    if (readable && read_func.empty() && (fc == 5 || fc == 6)) rfc = fc == 5 ? 1 : 3;
    const uint8_t read_code = rfc ? rfc : fc;
    if (readable && !(read_code >= 1 && read_code <= 4)) return {AccessError::MissingReadFunction, i};
    if (writable && fc != 5 && fc != 6) return {AccessError::MissingWriteFunction, i};
    if (!profileUnsigned(reg, 65535, true, number)) return {AccessError::InvalidRegister, i};
    const auto entity_slave = get("slave");
    if (!entity_slave.empty() && (!profileUnsigned(entity_slave, 247, false, number) || number == 0)) {
      return {AccessError::InvalidSlave, i};
    }
  }
  return {};
}

}  // namespace ofe_profile
