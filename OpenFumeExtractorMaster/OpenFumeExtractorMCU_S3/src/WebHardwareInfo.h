#pragma once
#include "MasterHardwareInfo.h"
#include <stdio.h>

static String hardware_memory(uint32_t bytes) {
  if(!bytes)return "-";
  char text[32];snprintf(text,sizeof(text),"%.1f MiB",double(bytes)/1048576.0);return String(text);
}
static String hardware_version(uint32_t version) {
  if(!version)return "-";
  char text[24];snprintf(text,sizeof(text),"%u.%u.%u",unsigned(version>>16)&255,unsigned(version>>8)&255,unsigned(version)&255);return String(text);
}
static void web_append_hardware_info(String& html,const MasterHardwareInfo& info) {
  html+=F("<section class='panel' id='master_hardware'><h2>");html+=web_text("Hardware / Chip","Hardware / chip");
  html+=F("</h2><dl style='display:grid;grid-template-columns:repeat(auto-fit,minmax(min(180px,100%),1fr));gap:12px 20px;margin:0'>");
  auto row=[&](const char* de,const char* en,const String& value){html+=F("<div style='min-width:0;overflow-wrap:anywhere'><dt class='muted'>");html+=web_text(de,en);html+=F("</dt><dd style='margin:4px 0 0;font-weight:600'>");html+=html_escape(value);html+=F("</dd></div>");};
  row("Prozessor","Processor",info.model);
  row("Architektur","Architecture",info.p4?"RISC-V":"Xtensa LX7");
  char revision[16];snprintf(revision,sizeof(revision),"%u.%u",unsigned(info.revision/100),unsigned(info.revision%100));
  row("Siliziumrevision","Silicon revision",revision);
  row("CPU-Kerne","CPU cores",String(info.cores));
  row("CPU-Takt","CPU clock",String(info.cpu_mhz)+" MHz");
  row("Flash (erkannt)","Flash (detected)",hardware_memory(info.flash_bytes));
  row("Flash-Modus","Flash mode",info.flash_mode);
  row("Flash-Takt","Flash clock",info.flash_hz?String(info.flash_hz/1000000U)+" MHz":String("-"));
  row("PSRAM (SDK)","PSRAM (SDK)",hardware_memory(info.psram_bytes));
  row("PSRAM-Takt","PSRAM clock",info.psram_bytes&&info.psram_mhz?String(info.psram_mhz)+" MHz":String("-"));
  row("Arduino-ESP32","Arduino-ESP32",info.arduino);
  row("ESP-IDF","ESP-IDF",info.idf);
  if(info.p4){row("Funk-Koprozessor","Radio coprocessor","ESP32-C6 (SDIO)");row("C6-Firmware","C6 firmware",hardware_version(info.c6_current));row("ESP-Hosted im MCU","ESP-Hosted in MCU",hardware_version(info.c6_host));}
  html+=F("</dl></section>");
}
