#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "../../../Module/ModbusRtuModule/src/OfeStateTransaction.h"

constexpr unsigned PROFILE_ENTITY_MAX = 32;
struct ProfileEntity {
  uint8_t id, slave;
  uint16_t reg;
  bool used, readable;
  uint8_t read_func;
  uint32_t last_poll_ms = 0, poll_ms = 100, last_update_ms = 0;
  uint16_t value = 0;
};
static ProfileEntity profile_entities[32];
static uint8_t profile_entity_count = 2, modbus_slave = 1;
static uint8_t pending_modbus_entity_id, pending_modbus_func, pending_modbus_slave;
static uint16_t pending_modbus_reg, pending_modbus_value;
static uint32_t pending_modbus_ms, now_ms = 1000;
static uint32_t local_baud = 9600;
struct ModbusQueuedWrite {uint8_t entity_id, function; uint16_t value;};
static ModbusQueuedWrite modbus_writes[32];
static uint8_t modbus_write_count, profile_readback_entity_id, profile_poll_cursor, rx_packet_len;
static bool modbus_recovering;
static uint32_t modbus_recovery_ms, rx_packet_last_ms, last_profile_poll_ms, profile_poll_pause_until_ms;
static unsigned sends;
static uint8_t sent_frame[8];
static uint32_t millis() {return now_ms;}
static uint32_t local_rx_idle_gap_ms() {return 4;}
static bool append_active_checksum(uint8_t*, uint8_t& len, size_t) {len = 8; return true;}
static void local_send_raw(const uint8_t* data, uint8_t) {++sends; memcpy(sent_frame,data,8);}
static ProfileEntity* profile_find_entity(uint8_t id) {
  for (auto& entity : profile_entities) if (entity.used && entity.id == id) return &entity;
  return nullptr;
}
static bool profile_entity_readable(const ProfileEntity& e) {return e.readable;}
static uint8_t entity_read_func_code(const ProfileEntity& e) {return e.read_func;}
static void profile_store_read_value(ProfileEntity& e, uint16_t raw) {e.value=raw;}
#include "generated_refactor.inc.h"
struct Frame {uint16_t len; uint8_t payload[256];};
static uint32_t detail_last_rx_ms;
static uint32_t get_u32_le(const uint8_t* data) {
  return uint32_t(data[0]) | uint32_t(data[1])<<8 | uint32_t(data[2])<<16 | uint32_t(data[3])<<24;
}
static uint16_t get_u16_le(const uint8_t* data) {return uint16_t(data[0]) | uint16_t(data[1])<<8;}
#include "generated_detail_prefix.inc.h"

namespace security {
#define WEB_AUTH_ENABLE 1
constexpr int BASIC_AUTH=0;
static const char* web_auth_user="test";
static const char* web_auth_password="fixture";
static bool captive_active, web_password_change_required, origin_ok=true, csrf_ok=true;
static void web_security_headers() {}
static bool web_post_origin_ok() {return origin_ok;}
static bool web_csrf_ok() {return csrf_ok;}
static bool web_password_change_blocked() {return false;}
struct Web {
  bool credentials=false;
  int challenges=0;
  bool authenticate(const char*,const char*) {return credentials;}
  void requestAuthentication(int,const char*) {++challenges;}
  void send(int,const char*,const char*) {}
} web;
#include "generated_security.inc.h"
}

namespace mqtt {
using String=std::string;
static String mqtt_session_topic_root="old/base/";
static String mqtt_topic_path(const char*) {return "new/base/";}
struct Registry {
  struct Record {uint8_t addr;} records[2]={{0x20},{0x40}};
  uint8_t count() const {return 2;}
  const Record& at(uint8_t i) const {return records[i];}
} registry;
struct Client {
  bool online=true;
  std::vector<std::pair<String,String>> messages;
  bool connected() const {return online;}
  bool publish(const char* topic,const char* payload,bool retained) {
    assert(retained);messages.emplace_back(topic,payload);return true;
  }
  void loop() {}
} mqtt_client;
static bool mqtt_publish_measured(const char* topic,const char* payload,bool retained) {
  return mqtt_client.publish(topic,payload,retained);
}
static void mqtt_client_loop_measured() {mqtt_client.loop();}
#include "generated_mqtt_offline.inc.h"
}

struct FakePowerPrefs {
  size_t result=0;
  unsigned calls=0;
  size_t putUShort(const char*,uint16_t) {++calls;return result;}
};
namespace fan {
static uint16_t manual_output_power=400;
static bool manual_output_power_dirty=true;
static uint32_t manual_output_power_save_due_ms=1000;
static FakePowerPrefs prefs;
#include "generated_FanIoModule_power.inc.h"
}
namespace fan_pro {
static uint16_t manual_output_power=400;
static bool manual_output_power_dirty=true;
static uint32_t manual_output_power_save_due_ms=1000;
static FakePowerPrefs prefs;
#include "generated_FanIoProModule_power.inc.h"
}

int main() {
  struct PowerTest {bool* dirty;uint32_t* due;FakePowerPrefs* prefs;void (*poll)();};
  for (auto state : {PowerTest{&fan::manual_output_power_dirty,&fan::manual_output_power_save_due_ms,&fan::prefs,fan::poll_output_power_save},
                     PowerTest{&fan_pro::manual_output_power_dirty,&fan_pro::manual_output_power_save_due_ms,&fan_pro::prefs,fan_pro::poll_output_power_save}}) {
    now_ms=1000;state.poll();
    assert(*state.dirty && *state.due==61000 && state.prefs->calls==1);
    now_ms=60000;state.poll();assert(state.prefs->calls==1);
    state.prefs->result=sizeof(uint16_t);now_ms=61000;state.poll();
    assert(!*state.dirty && state.prefs->calls==2);
  }
  mqtt::mqtt_publish_all_availability_offline();
  assert(mqtt::mqtt_client.messages.size()==5);
  for (const auto& message:mqtt::mqtt_client.messages) assert(message.first.find("old/base/")==0);
  assert(mqtt::mqtt_client.messages[0].second=="offline");
  assert(mqtt::mqtt_client.messages[2].second=="{\"addr\":32,\"online\":false}");
  mqtt::mqtt_client.online=false;mqtt::mqtt_publish_all_availability_offline();
  assert(mqtt::mqtt_client.messages.size()==5);
  for (auto parse : {parse_prefix_0,parse_prefix_1,parse_prefix_2}) {
    for (unsigned suffix=0;suffix<256;++suffix) {
      for (unsigned length=0;length<=192;++length) {
        Frame frame{};frame.len=uint16_t(length);frame.payload[0]=1;frame.payload[7]=uint8_t(suffix);
        DisplayModuleDetail detail{};
        assert(parse(frame,0,detail)==(length>=64+suffix));
      }
    }
  }
  using namespace security;
  captive_active=true;web_password_change_required=true;
  assert(web_require_config_auth());
  csrf_ok=false;assert(!web_require_config_auth());csrf_ok=true;
  origin_ok=false;assert(!web_require_config_auth());origin_ok=true;
  web_password_change_required=false;
  assert(!web_require_config_auth() && web.challenges==1);
  web.credentials=true;assert(web_require_config_auth());
  captive_active=false;web.credentials=false;assert(!web_require_config_auth());
  int values[] = {11,22};
  uint32_t baud = 9600;
  const ofe_profile::StateRegion regions[] = {ofe_profile::region(values),ofe_profile::region(baud)};
  {
    ofe_profile::StateTransaction tx(regions,2);
    assert(tx.valid());values[0]=99;baud=1200;
  }
  assert(values[0]==11 && baud==9600);
  {
    ofe_profile::StateTransaction tx(regions,2);
    values[0]=33;tx.commit();
  }
  assert(values[0]==33);
  {
    ofe_profile::StateTransaction tx(regions,2,[](size_t)->void*{return nullptr;});
    assert(!tx.valid());
  }
  assert(values[0]==33);
  profile_entities[0]={20,1,2,true,true,3};
  profile_entities[1]={21,2,4,true,true,3};
  auto& entity=profile_entities[0];
  assert(modbus_send_request(entity,3,0));
  assert(!modbus_send_request(profile_entities[1],3,0));
  uint8_t reply[]={2,3,2,0,55,0,0};
  assert(!process_profile_rx(reply,7));
  reply[0]=1;reply[1]=4;
  assert(!process_profile_rx(reply,7));
  reply[1]=3;reply[2]=4;
  assert(!process_profile_rx(reply,7));
  reply[2]=2;
  assert(process_profile_rx(reply,7));
  assert(entity.value==55 && !pending_modbus_entity_id);
  assert(!process_profile_rx(reply,7));
  assert(modbus_send_request(entity,6,77));
  uint8_t echo[]={1,6,0,2,0,78,0,0};
  assert(!process_profile_rx(echo,8));
  echo[5]=77;echo[3]=3;
  assert(!process_profile_rx(echo,8));
  echo[3]=2;
  assert(process_profile_rx(echo,8));
  assert(entity.value==55 && profile_readback_entity_id==20);
  assert(modbus_queue_write(20,6,100));
  assert(modbus_queue_write(21,6,100));
  assert(modbus_queue_write(20,6,0));
  assert(modbus_write_count==2);
  profile_poll_tick();
  assert(pending_modbus_entity_id==20 && sent_frame[5]==0);
  now_ms+=251;profile_poll_tick();
  assert(modbus_recovering && !pending_modbus_entity_id && modbus_write_count==1);
  assert(!process_profile_rx(reply,7));
  now_ms+=249;profile_poll_tick();assert(modbus_write_count==1);
  now_ms+=1;profile_poll_tick();
  assert(pending_modbus_entity_id==21 && sent_frame[5]==100);
  modbus_reset_exchange();
  assert(modbus_recovering && !pending_modbus_entity_id && !modbus_write_count);
  assert(modbus_queue_write(20,6,1,false));
  assert(modbus_queue_write(20,6,1,false));
  assert(modbus_write_count==2); // Button events are not coalesced.
  modbus_reset_exchange();
  // Unsigned elapsed-time guards remain correct across millis wrap.
  modbus_recovery_ms=0xFFFFFF00U;now_ms=10;profile_poll_tick();
  assert(!modbus_recovering);
  pending_modbus_entity_id=0;
  assert(modbus_response_timeout_ms()==250);
  local_baud=300;
  assert(modbus_response_timeout_ms()>=790);
  now_ms=0xFFFFFF00U;
  assert(modbus_send_request(entity,3,0));
  now_ms+=251;profile_poll_tick();
  assert(pending_modbus_entity_id==20 && !modbus_recovering);
  now_ms=pending_modbus_ms+modbus_response_timeout_ms()-1;profile_poll_tick();
  assert(pending_modbus_entity_id==20 && !modbus_recovering);
  assert(process_profile_rx(reply,7));
  assert(modbus_send_request(entity,3,0));
  now_ms=pending_modbus_ms+modbus_response_timeout_ms();profile_poll_tick();
  assert(!pending_modbus_entity_id && modbus_recovering);
  std::cout << "PASS: captive auth, three display packet boundaries, profile rollback, Modbus ownership/echo validation, queued OFF/buttons and rollover\n";
}
