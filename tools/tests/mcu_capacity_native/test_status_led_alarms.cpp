#include "ModuleAlarmPolicy.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace jbc_rs485;

ModuleRegistry registry;
struct Scheduler {
  uint8_t output=0x20, preferred=0;
  uint16_t jbc_error=0;
  bool input=true, eco=false, ota=false;
  bool mainInputSourceAvailable() const {return input;}
  uint8_t outputAddr() const {return output;}
  uint16_t systemJbcError() const {return jbc_error;}
  bool powerSaveActive() const {return eco;}
  bool firmwareUpdateActive() const {return ota;}
  ofe_module_alarm::State moduleAlarmState(const ModuleRecord& rec) const {
    return ofe_module_alarm::evaluate(rec,output,preferred);
  }
} scheduler;
struct Extractor {
  bool continuous_=false,output_=false;
  uint32_t afterrun=0;
  uint32_t afterrunLeftMs() const {return afterrun;}
  bool continuous() const {return continuous_;}
  bool outputEnabled() const {return output_;}
} extractor;
enum {OFE_LED_EVENT_OFF,OFE_LED_EVENT_CRITICAL,OFE_LED_EVENT_AFTER_RUN,
      OFE_LED_EVENT_CONTINUOUS,OFE_LED_EVENT_EXTRACTOR_ON,OFE_LED_EVENT_WARNING};
struct Leds {
  int event=-1;
  bool eco=false,bus=false,ota=false;
  void setEcoMode(bool on,uint8_t) {eco=on;}
  void setBusOnline(bool on) {bus=on;}
  void setFirmwareUpdate(bool on) {ota=on;}
  void setModuleEvent(int value) {event=value;}
} ofe_status_leds;
#define WEB_ENABLE 0
#include "MasterStatusLed.inc.h"

int main() {
  assert(registry.begin());
  auto check=[](int event){update_status_leds();assert(ofe_status_leds.event==event);};
  for(uint8_t type:{MODULE_JBC_BUS,MODULE_JBC_USB,MODULE_WELLER_ZERO_SMOG,MODULE_UNIVERSAL_RS232,MODULE_MODBUS_RTU}) {
    registry.clear();scheduler=Scheduler{};extractor=Extractor{};
    auto& rec=*registry.upsert(0x30);rec.online=true;rec.type=type;
    rec.caps=type==MODULE_JBC_BUS?CAP_JBC_BUS:type==MODULE_JBC_USB?CAP_JBC_USB:type==MODULE_WELLER_ZERO_SMOG?CAP_WELLER_INTERFACE:0;
    rec.weller_uart_age_sec=0xFFFF;
    if(type==MODULE_UNIVERSAL_RS232 || type==MODULE_MODBUS_RTU)rec.output_fault_mask=0x0801;
    check(OFE_LED_EVENT_WARNING);
    rec.connection_warnings_muted=true;check(OFE_LED_EVENT_OFF);
    scheduler.output=rec.addr;check(OFE_LED_EVENT_CRITICAL);
    scheduler.output=0x20;scheduler.preferred=rec.addr;check(OFE_LED_EVENT_CRITICAL);
    scheduler.preferred=0;rec.online=false;check(OFE_LED_EVENT_CRITICAL);
    rec.online=true;check(OFE_LED_EVENT_OFF);
    scheduler.eco=true;check(OFE_LED_EVENT_OFF);assert(ofe_status_leds.eco);
    extractor.afterrun=1;check(OFE_LED_EVENT_AFTER_RUN);
    extractor.afterrun=0;extractor.continuous_=true;check(OFE_LED_EVENT_CONTINUOUS);
    extractor.continuous_=false;extractor.output_=true;check(OFE_LED_EVENT_EXTRACTOR_ON);
  }
  registry.clear();scheduler=Scheduler{};extractor=Extractor{};
  scheduler.input=false;check(OFE_LED_EVENT_WARNING);scheduler.input=true;
  scheduler.output=0;check(OFE_LED_EVENT_WARNING);scheduler.output=0x20;
  scheduler.jbc_error=4;check(OFE_LED_EVENT_OFF); // No JBC consumer, no global JBC alarm.
  auto& rec=*registry.upsert(0x10);rec.type=MODULE_JBC_BUS;rec.online=true;
  rec.caps=CAP_JBC_BUS;rec.jbc_link_flags=FAST_FLAG_CONNECTED;rec.station_addr=0x18;
  check(OFE_LED_EVENT_CRITICAL);scheduler.jbc_error=2;check(OFE_LED_EVENT_WARNING);
  scheduler.jbc_error=0;check(OFE_LED_EVENT_OFF);
  registry.clear();auto& output=*registry.upsert(0x20);
  output.online=true;output.type=MODULE_FAN_IO_PRO;output.output_fault_mask=2;
  check(OFE_LED_EVENT_WARNING);output.output_fault_mask=4;check(OFE_LED_EVENT_CRITICAL);
  output.output_fault_mask=0;check(OFE_LED_EVENT_OFF);
  scheduler.ota=true;check(OFE_LED_EVENT_OFF);assert(ofe_status_leds.ota);
  puts("PASS: actual MCU EVT policy: muted/required links, five module types, bus loss, recovery, input/output selection, filter/hardware/JBC errors, operation and ECO");
}
