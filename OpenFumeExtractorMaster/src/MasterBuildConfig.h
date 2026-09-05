#pragma once

#if __has_include("MasterPrivateConfig.h")
#include "MasterPrivateConfig.h"
#endif

#ifndef OFE_STATUS_LED_ENABLE
#define OFE_STATUS_LED_ENABLE 1
#endif

#ifndef OFE_STATUS_LED_PIN
#define OFE_STATUS_LED_PIN 4
#endif

#ifndef RS485_RX_PIN
#define RS485_RX_PIN 17
#endif

#ifndef RS485_TX_PIN
#define RS485_TX_PIN 16
#endif

#ifndef RS485_BAUD
#define RS485_BAUD 250000 // 230400 Standart
#endif

#ifndef MODULE_FW_CHUNK_SIZE
#define MODULE_FW_CHUNK_SIZE 188 // normal modules use the largest valid FW_CHUNK payload
#endif

#ifndef MODULE_FW_DISPLAY_CHUNK_SIZE
#define MODULE_FW_DISPLAY_CHUNK_SIZE 188 // display OTA test step; reduce to 96 B if the display times out
#endif

#ifndef MASTER_FW_WEB_CHUNK_SIZE
// Browser -> Master OTA request block. WebServer still streams each multipart
// request through its small HTTP upload buffer into Update.write(); this value
// mainly controls HTTP round-trip frequency, not a 32 KiB master-side queue.
#define MASTER_FW_WEB_CHUNK_SIZE 32768
#endif

#ifndef MODULE_FW_PUMP_FRAMES_PER_LOOP
// Pump more than one acknowledged RS485 OTA frame per loop for better module
// update speed. Display updates stay conservative because the display has the
// heaviest local UI/flash workload.
#define MODULE_FW_PUMP_FRAMES_PER_LOOP 2
#endif

#ifndef MODULE_FW_DISPLAY_PUMP_FRAMES_PER_LOOP
#define MODULE_FW_DISPLAY_PUMP_FRAMES_PER_LOOP 1
#endif

#ifndef MODULE_FW_WIFI_DISPLAY_PUMP_FRAMES_PER_LOOP
// Authenticated WiFi has no RS485 line-turnaround delay. Send a short burst
// per loop while retaining the conservative single-frame path for wired displays.
#define MODULE_FW_WIFI_DISPLAY_PUMP_FRAMES_PER_LOOP 4
#endif

#ifndef MODULE_FW_PUMP_BUDGET_MS
#define MODULE_FW_PUMP_BUDGET_MS 18
#endif

#ifndef MODULE_FW_WIFI_PUMP_BUDGET_MS
#define MODULE_FW_WIFI_PUMP_BUDGET_MS 48
#endif

#ifndef MODULE_FW_HTTP_CHUNK_SIZE
// Browser -> Master block size for RS485 module OTA. The master still splits
// this into peripheral-bus FW_CHUNK frames internally, but larger HTTP blocks
// avoid the heavy per-request overhead of one HTTP request per RS485 frame.
#define MODULE_FW_HTTP_CHUNK_SIZE 8192
#endif


#ifndef MODULE_FW_QUEUE_PREFILL_SIZE
// Do not start draining the RS485 OTA queue until one complete browser block
// is buffered. This removes the begin->first-chunk startup bubble and gives
// the producer enough headroom to absorb an occasional slower HTTP roundtrip.
#define MODULE_FW_QUEUE_PREFILL_SIZE MODULE_FW_HTTP_CHUNK_SIZE
#endif

#ifndef MODULE_FW_SERVER_YIELD_MS
// Optional cooperative pause after each RS485 OTA frame. Keep 0 for original
// module-update speed; the web task is no longer pinned to the WiFi core.
#define MODULE_FW_SERVER_YIELD_MS 0
#endif

#ifndef MODULE_FW_QUEUE_BLOCKS
// Number of browser chunks that may be queued ahead while RS485 keeps pumping.
#define MODULE_FW_QUEUE_BLOCKS 3
#endif

#ifndef MODULE_FW_QUEUE_SIZE
// Async module OTA queue between the HTTP task and the RS485 loop task.
// A deeper queue avoids idle RS485 gaps while the browser opens the next request.
#define MODULE_FW_QUEUE_SIZE (MODULE_FW_HTTP_CHUNK_SIZE * MODULE_FW_QUEUE_BLOCKS + MODULE_FW_CHUNK_SIZE)
#endif

#ifndef DISPLAY_STATUS_SLOT_MS
// One display is serviced per slot. With two displays 200 ms means roughly
// 2.5 DISPLAY_STATUS updates/s per display instead of 1 Hz at the old 500 ms.
#define DISPLAY_STATUS_SLOT_MS 200UL
#endif

#ifndef MASTER_COMMAND_QUEUE_LENGTH
// External Web/MQTT control requests. The queue stores pointers to synchronous
// request objects; no dynamic allocation is used.
#define MASTER_COMMAND_QUEUE_LENGTH 16
#endif

#ifndef MASTER_EXTMEM_MALLOC_THRESHOLD
// With PSRAM available, ordinary allocations >= this size prefer external RAM.
// Small control/Scheduler/RS485 allocations remain internal.
#define MASTER_EXTMEM_MALLOC_THRESHOLD 2048UL
#endif

#ifndef MASTER_STATE_JSON_RESERVE_PSRAM
// /state is a large, short-lived web payload. The Master target has 8 MB PSRAM
// and supports up to 16 modules, so reserve the worst-case response up front in
// external RAM instead of repeatedly growing/reallocating the Arduino String.
#define MASTER_STATE_JSON_RESERVE_PSRAM (256UL * 1024UL)
#endif

#ifndef MASTER_STATE_JSON_DESC_RESERVE_PSRAM
// /state?desc=1 can additionally carry cached Universal/Modbus descriptors.
#define MASTER_STATE_JSON_DESC_RESERVE_PSRAM (384UL * 1024UL)
#endif

#ifndef MASTER_STATE_JSON_RESERVE_INTERNAL
// Safe fallback for builds/boards without usable PSRAM.
#define MASTER_STATE_JSON_RESERVE_INTERNAL 36000UL
#endif

#ifndef MASTER_STATE_JSON_DESC_RESERVE_INTERNAL
#define MASTER_STATE_JSON_DESC_RESERVE_INTERNAL 42000UL
#endif

#ifndef MASTER_COMMAND_QUEUE_SEND_MS
// Only the enqueue operation is bounded. Once accepted, the caller sleeps until
// the main loop has executed the request, preserving the old synchronous result.
#define MASTER_COMMAND_QUEUE_SEND_MS 250UL
#endif

#define OFE_STR_HELPER(x) #x
#define OFE_STR(x) OFE_STR_HELPER(x)

#ifndef WEB_ENABLE
#define WEB_ENABLE 1
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef MASTER_AP_SSID
#define MASTER_AP_SSID "OpenFumeExtractor"
#endif

#ifndef MASTER_DEFAULT_PASSWORD
#define MASTER_DEFAULT_PASSWORD "extractor123"
#endif

#ifndef MASTER_AP_PASSWORD
#define MASTER_AP_PASSWORD MASTER_DEFAULT_PASSWORD
#endif

#ifndef WEB_AUTH_ENABLE
#define WEB_AUTH_ENABLE 1
#endif

#ifndef WEB_AUTH_USER
#define WEB_AUTH_USER "admin"
#endif

#ifndef WEB_AUTH_PASSWORD
#define WEB_AUTH_PASSWORD MASTER_AP_PASSWORD
#endif

#ifndef OFE_DEVELOPER_MODE_ENABLE
#define OFE_DEVELOPER_MODE_ENABLE 0
#endif

#ifndef OFE_DEVELOPER_PASSWORD_SHA256
#define OFE_DEVELOPER_PASSWORD_SHA256 ""
#endif

#define MASTER_FW_MAJOR 1
#define MASTER_FW_MINOR 9
#define MASTER_FW_PATCH 29
#define MASTER_FW_SUFFIX "beta"
#define MASTER_FW_NAME "Open Fume Extractor"
#define MASTER_FW_VERSION OFE_STR(MASTER_FW_MAJOR) "." OFE_STR(MASTER_FW_MINOR) "." OFE_STR(MASTER_FW_PATCH) MASTER_FW_SUFFIX

extern "C" inline const volatile char OFE_MASTER_FW_SIGNATURE[] __attribute__((used)) = "OFE_FW_SIG:v1;target=MASTER;version=" MASTER_FW_VERSION ";";
static void ofe_keep_master_fw_signature() __attribute__((noinline));
static void ofe_keep_master_fw_signature() {
  const volatile char* p = OFE_MASTER_FW_SIGNATURE;
  volatile size_t n = 0;
  while (p[n] != '\0') ++n;
  (void)n;
}
