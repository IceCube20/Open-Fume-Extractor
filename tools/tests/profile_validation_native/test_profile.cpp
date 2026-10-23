#include "UniversalProfileValidation.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include "module_functions.h"

using namespace ofe_profile;

static std::string read_file(const char* path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  return std::string((std::istreambuf_iterator<char>(input)), {});
}

static void modbus_tests(const char* io, const char* transforms) {
  assert(validateModbusProfile(read_file(io)).error == AccessError::None);
  assert(validateModbusProfile(read_file(transforms)).error == AccessError::None);
  assert(validateModbusProfile("").error == AccessError::None);
  assert(validateModbusProfile("profile=Metadata only").error == AccessError::None);
  for (const char* alias : module_function_aliases) {
    const uint8_t code = modbus_func_code(alias);
    assert(code && modbusFunction(alias) == code);
    std::string upper = alias;
    for (char& c : upper) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    assert(modbusFunction(upper) == code);
    const std::string profile = "entity.1.reg=0\nentity.1.func=" + std::string(alias);
    assert(validateModbusProfile(profile).error == AccessError::None);
    const std::string rw = profile + "\nentity.1.access=rw";
    assert(validateModbusProfile(rw).error == (modbus_func_is_write(code) ? AccessError::None : AccessError::MissingWriteFunction));
    const std::string ro = profile + "\nentity.1.access=ro";
    assert(validateModbusProfile(ro).error == AccessError::None); // Write functions infer their matching read function.
    const std::string wo = profile + "\nentity.1.access=wo";
    assert(validateModbusProfile(wo).error == (modbus_func_is_read(code) ? AccessError::MissingWriteFunction : AccessError::None));
  }
  for (const char* type : {"sensor", "number", "switch", "binary_sensor", "binary", "text", "select", "button"}) {
    const std::string profile = "entity.1.reg=0\nentity.1.type=" + std::string(type);
    for (const char* access : {"ro", "rw", "wo"}) {
      assert(validateModbusProfile(profile + "\nentity.1.access=" + access).error == AccessError::None);
    }
  }
  for (const char* reg : {"0", "65535", "0x0000", "0xFFFF", "0XfFfF", "010", "+42"}) {
    assert(validateModbusProfile("entity.1.reg=" + std::string(reg)).error == AccessError::None);
  }
  for (const char* reg : {"", "-1", "65536", "0x10000", "999999999999999999999", "12x", "0x", "0xFG", "1.5", "08"}) {
    assert(validateModbusProfile("entity.1.type=sensor\nentity.1.reg=" + std::string(reg)).error == AccessError::InvalidRegister);
  }
  for (const char* slave : {"1", "247", "+42", "001"}) {
    assert(validateModbusProfile("slave=" + std::string(slave)).error == AccessError::None);
    assert(validateModbusProfile("entity.1.reg=0\nentity.1.slave=" + std::string(slave)).error == AccessError::None);
  }
  for (const char* slave : {"0", "248", "-1", "1x", "0x01", "999999999999999999999"}) {
    const auto global = validateModbusProfile("slave_id=" + std::string(slave));
    assert(global.error == AccessError::InvalidSlave && global.entity == 0);
    const auto entity = validateModbusProfile("entity.32.reg=65535\nentity.32.slave=" + std::string(slave));
    assert(entity.error == AccessError::InvalidSlave && entity.entity == 32);
  }
  assert(validateModbusProfile("entity.1.reg=0\nentity.1.type=other").error == AccessError::InvalidType);
  assert(validateModbusProfile("entity.1.reg=0\nentity.1.access=other").error == AccessError::InvalidAccess);
  assert(validateModbusProfile("entity.1.reg=0\nentity.1.func=write_multiple_registers").error == AccessError::InvalidFunction);
  assert(validateModbusProfile("entity.1.reg=0\nentity.1.read_func=unknown").error == AccessError::InvalidReadFunction);
  assert(validateModbusProfile("entity.1.reg=0\nentity.1.access=rw\nentity.1.func=write_holding\nentity.1.read_func=write_coil").error == AccessError::MissingReadFunction);
  assert(validateModbusProfile("entity.1.reg=0\nentity.1.access=wo\nentity.1.func=write_holding\nentity.1.read_func=write_coil").error == AccessError::None);
  assert(validateModbusProfile("slave=\nslave_id=1\nentity.1.type=SWITCH\nentity.1.register=0x10\nentity.1.mode=RW").error == AccessError::None);
  assert(validateModbusProfile("#entity.1.type=other\r\n entity.32.register=0xFF \r\n entity.32.mode=RO \r\n entity.32.read_func=READ_INPUT \r\n").error == AccessError::None);
  assert(validateModbusProfile("entity.1.reg=0\nentity.1.reg=bad").error == AccessError::None);
  assert(validateModbusProfile("entity.1.reg=bad\nentity.1.reg=0").error == AccessError::InvalidRegister);
  const std::string bounded = "entity.1.type=sensor\nentity.1.reg=0";
  assert(validateModbusProfile(std::string_view(bounded.data(), 20)).error == AccessError::InvalidRegister);
  std::cout << "PASS: both Modbus fixtures; all actual module function aliases; access/default combinations; register/slave bounds; sparse slots and malformed input\n";
}

int main(int argc, char** argv) {
  assert(argc == 4);
  modbus_tests(argv[2], argv[3]);
  std::string fixture = read_file(argv[1]);
  assert(validateRs232Access(fixture).error == AccessError::None);
  const std::string corrected = "entity.7.access=wo";
  const auto pos = fixture.find(corrected);
  assert(pos != std::string::npos);
  fixture.replace(pos, corrected.size(), "entity.7.access=rw");
  const auto failure = validateRs232Access(fixture);
  assert(failure.error == AccessError::MissingMatch && failure.entity == 7);

  assert(validateRs232Access("").error == AccessError::None);
  assert(validateRs232Access("profile=Metadata only\n").error == AccessError::None);
  assert(validateRs232Access("entity.1.type=switch\nentity.1.set_on=N\nentity.1.set_off=M").error == AccessError::None);
  assert(validateRs232Access("entity.1.type=sensor\nentity.1.match=S###").error == AccessError::None);
  assert(validateRs232Access("entity.1.type=button\nentity.1.set=r001").error == AccessError::None);
  assert(validateRs232Access("entity.1.type=text\nentity.1.mode=RO\nentity.1.match=EVENT{value}").error == AccessError::None);
  assert(validateRs232Access("entity.1.type=number\nentity.1.access=rw\nentity.1.match=S###\nentity.1.set=d{value:03}").error == AccessError::None);
  assert(validateRs232Access("entity.1.type=sensor").error == AccessError::MissingMatch);
  assert(validateRs232Access("entity.1.type=button").error == AccessError::MissingCommand);
  assert(validateRs232Access("entity.1.type=number\nentity.1.access=rw\nentity.1.match=S###").error == AccessError::MissingCommand);
  assert(validateRs232Access("entity.1.type=switch\nentity.1.access=other\nentity.1.set=N").error == AccessError::InvalidAccess);
  assert(validateRs232Access("entity.32.type=switch\nentity.32.access=rw\nentity.32.set=N").entity == 32);
  assert(validateRs232Access("entity.1.type=switch\nentity.1.set=N\nentity.1.mode=wo\nentity.1.access=ro").error == AccessError::MissingMatch);
  assert(validateRs232Access("entity.1.type=switch\nentity.1.access=wo\nentity.1.access=rw\nentity.1.set=N").error == AccessError::None);
  assert(validateRs232Access(";entity.1.access=rw\r\n#entity.1.type=sensor\r\n entity.1.type=switch \r\n entity.1.access=WO \r\n entity.1.set=N \r\n").error == AccessError::None);
  assert(validateRs232Access("entity.1.type=switch\nentity.1.access=rw\nentity.1.set=N\nentity.10.match=N###").entity == 1);
  // Only the bounded view is inspected, even when additional rules follow it.
  const std::string joined = "entity.1.type=sensor\nentity.1.match=S###";
  assert(validateRs232Access(std::string_view(joined.data(), 20)).error == AccessError::MissingMatch);
  std::cout << "PASS: corrected Weller v6; original rejected at entity 7; access inference, read/write rules, CRLF, aliases, sparse slots and bounded input\n";
}
