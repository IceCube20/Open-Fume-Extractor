#pragma once
namespace ofe_wifi {
enum { AUTO,WIRED,WIRELESS };
struct Config { int mode=AUTO,from_master=0; char ssid[33]{},password[65]{},host[64]{}; uint8_t key[16]{}; };
static bool nonzero(const uint8_t* p,size_t n) { while(n--) if(*p++) return true; return false; }
static void hex(const uint8_t*,size_t,char* out) { strcpy(out,"0123456789ABCDEF0123456789ABCDEF"); }
static bool unhex(const char* s,uint8_t*,size_t) { return strlen(s)==32; }
}
struct Wifi {
  struct View {
    ofe_wifi::Config config;
    char networks[544]{},ip[16]{};
    int rssi=-45; uint32_t revision=0,scan_revision=0,free_internal=64000,largest_internal=32000;
    int result=0; bool wifi=true,active=true,scanning=false;
  } data;
  int saves=0,forgets=0,scans=0;
  View view() { return data; }
  bool wireless() { return data.active; }
  uint64_t uid() { return 0x4000123456789ABCULL; }
  bool save(const ofe_wifi::Config&) { ++saves; return true; }
  bool forgetPairing() { ++forgets; return true; }
  void scan() { ++scans; }
} display_wifi;
namespace ofe_ble { enum { READY,CONFIRM,SAVING,DONE,LOW_MEMORY,FAILED,CANCELLED,EXPIRED }; }
struct Ble {
  struct View { bool active=false; int phase=ofe_ble::READY; uint32_t token=5,pin=123456; } data;
  bool active() { return data.active; }
  View view() { return data; }
  bool start() { data.active=true; return true; }
  void cancel() { data.active=false; }
  bool approve(uint32_t token,uint32_t pin,bool) { assert(token==5 && pin==123456); return true; }
} display_ble;
static bool master_link_online() { return true; }
static bool fw_update_active=false;
static struct {
  bool update_active=false,jbc_present=true,fan_present=true,weller_present=true,clock_valid=true,jbc_connected=true,output_enabled=false;
  uint8_t jbc_work_mask=0,jbc_stand_mask=0,output_addr=0x20;
  uint8_t master_alarm_critical_mask=0;
  unsigned select_flow=550,delay_work_s=12,afterrun_power=300,output_power=550,fan_rpm=1250;
  unsigned clock_hour=23,clock_minute=59,clock_day=31,clock_month=12,clock_year=2026;
} status;
