"""Compile actual decoder, scheduler and service-handler excerpts on the host."""
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "Module/JbcUsbModule/JbcUsbModule.ino"
OUT = ROOT / "tools/tests/build-checks/jbc-usb-read-regressions"


def definition(source, name):
    match = re.search(r"^static (?:bool|void) " + name + r"\([^\n]*\) \{", source, re.M)
    if not match:
        raise ValueError(name)
    depth = 0
    for index in range(source.index("{", match.start()), len(source)):
        depth += (source[index] == "{") - (source[index] == "}")
        if depth == 0:
            return source[match.start():index + 1]
    raise ValueError(name)


def main():
    source = SOURCE.read_text(encoding="utf-8")
    OUT.mkdir(parents=True, exist_ok=True)
    constants = "\n".join(re.findall(r"^static const uint8_t JBC_CMD_\w+ = [^;]+;", source, re.M))
    fixture = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iostream>
using std::max;
using std::min;
using JbcProtocol=int;
enum {JBC_PROTO_UNKNOWN=0,JBC_PROTO_01=1,JBC_PROTO_02=2};
enum {JBC_STATION_SOLD=1,JBC_STATION_HA=2,JBC_STATION_CL=3};
enum {JBC_LINK_ACTIVE=1,JBC_CONFIG_WRITE_IDLE=0,JBC_NAME_WRITE_IDLE=0,JBC_UID_PROVISION_IDLE=0,JBC_UID_PROVISION_DONE=1};
enum {STATUS_OK=0,STATUS_BAD_LEN=1,STATUS_NOT_SUPPORTED=2,STATUS_BUSY=3,STATUS_BAD_VALUE=4};
enum {JBC_USB_CONFIG_STATION_NAME=1,JBC_USB_CONFIG_SERVICE_READ=5};
constexpr uint8_t JBC_MAX_PORTS=4,JBC_DECODE_LEN_UNKNOWN=255,JBC_DECODE_LEN_OPEN=254;
struct JbcFrame {int frame_protocol=JBC_PROTO_02;uint8_t command=0,len=0,data[255]={};};
struct Frame {uint8_t len=0,payload[192]={};};
struct JbcPortState {
 bool valid=true;uint8_t tool=0,cl_flags=0;bool cl_motors_on=false,cl_door_open=false;
 uint32_t cl_counter_plug_min=0,cl_counter_cleaning_continuous_min=0,cl_counter_cleaning_detection_min=0,cl_counter_work_cycles=0,cl_counter_door_open_cycles=0;
 uint32_t cl_partial_plug_min=0,cl_partial_cleaning_continuous_min=0,cl_partial_cleaning_detection_min=0,cl_partial_work_cycles=0,cl_partial_door_open_cycles=0;
};
JbcPortState jbc_ports[4];
int jbc_station_kind=JBC_STATION_CL,jbc_frame_protocol=JBC_PROTO_02,jbc_command_protocol=JBC_PROTO_02;
char jbc_model[16]="DDE";
bool jbc_cl_control_mode=false,jbc_cl_control_mode_valid=false,fw_update_active=false;
bool jbc_config_write_queued=false,jbc_config_write_inflight=false,jbc_station_name_write_queued=false,jbc_station_name_write_inflight=false;
int jbc_config_write_state=0,jbc_station_name_write_state=0,jbc_uid_provision_state=JBC_UID_PROVISION_DONE,jbc_link_state=JBC_LINK_ACTIVE;
char jbc_station_name_write_value[17]={};
uint8_t jbc_service_read_pending=0;
uint32_t jbc_service_read_queued_ms=0,jbc_service_read_last_ms=0,clock_ms=100;
uint32_t millis() {return clock_ms;}
bool uid_provisioning=false,levels_supported=false,transport_available=true;
uint8_t next_ha_detail_port=0,next_ha_detail_stage=0,jbc_host_addr=0x19,jbc_station_addr=0x10;
uint32_t next_ha_detail_poll_ms=0,next_ha_tool_refresh_fast_until_ms=0;
int sent[256]={},status=-1,errors=0;
uint8_t poll_port_limit() {return 1;}
bool ha_supports_temp_levels() {return levels_supported;}
bool ha_supports_partial_counters() {return true;}
uint8_t next_fid() {return 1;}
bool jbc_send_frame(int,uint8_t,uint8_t,uint8_t,uint8_t cmd,const uint8_t*,uint8_t,uint8_t) {
 if(!transport_available)return false;++sent[cmd];return true;
}
void jbc_note_decode_error(const JbcFrame&,int=0,int=0) {++errors;}
int pending_port_for_frame(const JbcFrame&) {return 0;}
void mark_fast_changed() {}
void jbc_initial_low_mark_success(const JbcFrame&,int) {}
uint32_t jbc_u32_from_le(const uint8_t* p) {return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
bool jbc_send_sold_station_read(uint8_t cmd) {if(!transport_available)return false;++sent[cmd];return true;}
void rs485_status_response(const Frame&,int value) {status=value;}
bool jbc_config_prepare(const uint8_t*,uint8_t) {return false;}
uint8_t jbc_station_name_write_command() {return 0xB2;}
bool jbc_station_name_valid(const char*,uint8_t) {return true;}
'''
    fixture += constants + "\n"
    for name in ("is_cl_read_frame", "decode_cl_read", "jbc_ha_detail_requires_tool", "jbc_send_ha_detail",
                 "sold_supports_excellence_289", "sold_supports_ground_type", "jbc_sold_service_read_supported",
                 "poll_jbc_service_read", "rs485_jbc_usb_config"):
        fixture += definition(source, name) + "\n"
    start = re.search(r"    if \(!uid_provisioning && jbc_station_kind == JBC_STATION_HA && jbc_frame_protocol == JBC_PROTO_02 &&\n        \(int32_t\)\(now - next_ha_detail_poll_ms\) >= 0\) \{", source).start()
    end = source.index("    if (!uid_provisioning && (int32_t)(now - next_station_error_poll_ms)", start)
    fixture += "void step(uint32_t now) {\n" + source[start:end] + "\n}\n"
    fixture += r'''
int main() {
 struct Vector {const char* text;bool control;};
 for(const auto& v: {Vector{"M",false},Vector{"C",true},Vector{":M",false},Vector{":C",true},Vector{"CLM:M",false},Vector{" Cx ",false},Vector{" : c ",true},Vector{"abc:C:extra",true},Vector{"abc::C",false},Vector{"C:",false}}) {
  JbcFrame f;f.command=0xE0;f.len=uint8_t(std::strlen(v.text));std::memcpy(f.data,v.text,f.len);
  decode_cl_read(f);assert(jbc_cl_control_mode==v.control);assert(!errors);
 }
 jbc_station_kind=JBC_STATION_HA;
 for(bool tool:{false,true})for(bool levels:{false,true}) {
  std::fill(std::begin(sent),std::end(sent),0);next_ha_detail_stage=0;next_ha_detail_poll_ms=0;
  jbc_ports[0].tool=tool?1:0;levels_supported=levels;
  for(uint32_t now=100;now<60100;now+=200)step(now);
  assert(sent[JBC_CMD_PROFILE_MODE_HA]>0);assert(sent[JBC_CMD_TOOL_STATUS_HA]>0);
  assert(bool(sent[JBC_CMD_ADJUST_TEMP_HA])==tool);
  assert(bool(sent[JBC_CMD_LEVELS_HA])==(tool&&levels));
 }
 next_ha_detail_stage=0;next_ha_detail_poll_ms=0;transport_available=false;jbc_ports[0].tool=1;
 step(100);assert(next_ha_detail_stage==0);transport_available=true;step(300);assert(next_ha_detail_stage==1);
 jbc_station_kind=JBC_STATION_SOLD;
 for(int cmd=0;cmd<256;++cmd) {
  const bool allowed=cmd==0x60||cmd==0xAA||cmd==0xA0||cmd==0xA6||cmd==0xE3;
  assert(jbc_sold_service_read_supported(uint8_t(cmd))==allowed);
 }
 std::strcpy(jbc_model,"ALE");assert(jbc_sold_service_read_supported(0xBE));assert(!jbc_sold_service_read_supported(0xA0));
 std::strcpy(jbc_model,"CFE");assert(jbc_sold_service_read_supported(0xBA));assert(!jbc_sold_service_read_supported(0xA0));
 std::strcpy(jbc_model,"DDE");jbc_frame_protocol=JBC_PROTO_01;assert(!jbc_sold_service_read_supported(0x60));jbc_frame_protocol=JBC_PROTO_02;
 Frame req;req.len=2;req.payload[0]=JBC_USB_CONFIG_SERVICE_READ;req.payload[1]=0xA0;
 for(auto* busy:{&fw_update_active,&jbc_config_write_queued,&jbc_config_write_inflight,&jbc_station_name_write_queued,&jbc_station_name_write_inflight}) {
  *busy=true;rs485_jbc_usb_config(req);assert(status==STATUS_BUSY&&!jbc_service_read_pending);*busy=false;
 }
 jbc_uid_provision_state=2;rs485_jbc_usb_config(req);assert(status==STATUS_BUSY);jbc_uid_provision_state=JBC_UID_PROVISION_DONE;
 req.len=1;rs485_jbc_usb_config(req);assert(status==STATUS_BAD_LEN);req.len=2;
 req.payload[1]=0x87;rs485_jbc_usb_config(req);assert(status==STATUS_NOT_SUPPORTED);req.payload[1]=0xA0;
 rs485_jbc_usb_config(req);assert(status==STATUS_OK&&jbc_service_read_pending==0xA0);
 int before=sent[0xA0];rs485_jbc_usb_config(req);assert(status==STATUS_BUSY);
 assert(poll_jbc_service_read());assert(!jbc_service_read_pending&&sent[0xA0]==before+1);assert(!poll_jbc_service_read());
 rs485_jbc_usb_config(req);assert(status==STATUS_BUSY);clock_ms+=2000;rs485_jbc_usb_config(req);assert(status==STATUS_OK);
 transport_available=false;assert(poll_jbc_service_read()&&jbc_service_read_pending);
 clock_ms+=3001;assert(!poll_jbc_service_read()&&!jbc_service_read_pending);transport_available=true;
 rs485_jbc_usb_config(req);assert(status==STATUS_OK);fw_update_active=true;assert(!poll_jbc_service_read()&&!jbc_service_read_pending);
 std::cout<<"PASS: actual CL decoder, HA no-tool/busy detail cycle and allowlisted single-shot service reads; length/model/OTA/UID/write/rate/expiry guards\n";
}
'''
    cpp = OUT / "test.cpp"
    cpp.write_text(fixture, encoding="utf-8")
    vc = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207")
    sdk = Path("C:/Program Files (x86)/Windows Kits/10")
    version = "10.0.26100.0"
    env = dict(os.environ)
    env["INCLUDE"] = ";".join(str(p) for p in (vc/"include",sdk/f"Include/{version}/ucrt",sdk/f"Include/{version}/shared",sdk/f"Include/{version}/um"))
    env["LIB"] = ";".join(str(p) for p in (vc/"lib/x64",sdk/f"Lib/{version}/ucrt/x64",sdk/f"Lib/{version}/um/x64"))
    exe = OUT / "test.exe"
    subprocess.run([str(vc/"bin/Hostx64/x64/cl.exe"),"/nologo","/EHsc","/std:c++17","/O2",str(cpp),"/Fo"+str(OUT)+"/","/Fe"+str(exe)],env=env,cwd=OUT,check=True)
    subprocess.run([str(exe)],check=True)


if __name__ == "__main__":
    main()
