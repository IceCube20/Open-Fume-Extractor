// Arduino's .ino preprocessor emits generated function prototypes before the
// sketch body. Forward-declare every local protocol type used in function
// signatures before any includes so those generated prototypes are valid.
// Use the concrete underlying type here because uint8_t is provided by
// Arduino.h only below; on ESP32 uint8_t is unsigned char.
enum JbcProtocol : unsigned char;
enum JbcStationKind : unsigned char;
enum JbcLinkState : unsigned char;
enum JbcUidProvisionState : unsigned char;
enum RxState : unsigned char;
enum UsbSerialOpenResult : unsigned char;
struct JbcFrame;
struct JbcModelInfo;

#include <Arduino.h>
#include <ctype.h>
#include <Preferences.h>
#include <Update.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/stream_buffer.h>
#include <freertos/semphr.h>
#include <usb/usb_host.h>
#include <usb/usb_types_ch9.h>

#ifndef OFE_STATUS_LED_ENABLE
#define OFE_STATUS_LED_ENABLE 1
#endif
#ifndef OFE_STATUS_LED_PIN
#define OFE_STATUS_LED_PIN 4
#endif

#include "src/Rs485PeripheralBus.h"
#ifndef OFE_STATUS_LED_MASTER_TIMEOUT_MS
#define OFE_STATUS_LED_MASTER_TIMEOUT_MS 8000UL
#endif
#include "src/OfeStatusLed.h"

using namespace jbc_rs485;

// -----------------------------------------------------------------------------
// OpenFume bus / hardware
// -----------------------------------------------------------------------------
#ifndef RS485_RX_PIN
#define RS485_RX_PIN 17
#endif
#ifndef RS485_TX_PIN
#define RS485_TX_PIN 16
#endif
#ifndef RS485_BAUD
#define RS485_BAUD 250000
#endif

// ESP32-S3 native USB-OTG port must be wired as USB host and must supply 5 V VBUS.
// The USB D+/D- pins are the chip's native USB pins; no UART pins are used here.

static const uint16_t HW_VERSION = 0x0100;
#ifndef OFE_STR_HELPER
#define OFE_STR_HELPER(x) #x
#define OFE_STR(x) OFE_STR_HELPER(x)
#endif
#define OFE_MODULE_FW_MAJOR 1
#define OFE_MODULE_FW_MINOR 1
#define OFE_MODULE_FW_PATCH 76
#define OFE_MODULE_FW_SUFFIX "beta"
#define OFE_MODULE_FW_VERSION OFE_STR(OFE_MODULE_FW_MAJOR) "." OFE_STR(OFE_MODULE_FW_MINOR) "." OFE_STR(OFE_MODULE_FW_PATCH) OFE_MODULE_FW_SUFFIX

static const uint8_t FW_MAJOR = OFE_MODULE_FW_MAJOR;
static const uint8_t FW_MINOR = OFE_MODULE_FW_MINOR;
static const uint8_t FW_PATCH = OFE_MODULE_FW_PATCH;
static const char FW_SUFFIX[] = OFE_MODULE_FW_SUFFIX;
extern "C" const volatile char OFE_MODULE_FW_SIGNATURE[] __attribute__((used)) =
  "OFE_FW_SIG:v1;target=JBC_USB;version=" OFE_MODULE_FW_VERSION ";";
static void ofe_keep_module_fw_signature() __attribute__((noinline));
static void ofe_keep_module_fw_signature() {
  const volatile char* p = OFE_MODULE_FW_SIGNATURE;
  volatile size_t n = 0;
  while (p[n] != '\0') ++n;
  (void)n;
}

static const uint8_t DEFAULT_MODULE_ADDR = 0x11; // shares JBC family range with FAE bridge (0x10..0x1F)
static const uint32_t MODULE_CAPS =
  CAP_JBC_USB | CAP_FW_UPDATE | CAP_FAULT_REPORT | CAP_LOCAL_TRACE | CAP_LOCAL_PROTOCOL;

static HardwareSerial RS485(1);
static Link bus(RS485);
static Preferences prefs;
static OfeStatusLed ofe_status_leds;
static uint8_t module_addr = DEFAULT_MODULE_ADDR;
static char module_label[24] = {0};
static uint32_t last_master_ms = 0;

// System telemetry. Keep the same CPU/loop measurement semantics as the other
// OpenFume peripheral modules so the Master card reports real values instead of
// placeholder zeroes. CPU load is system-wide (both ESP32-S3 cores); loop max
// measures only work done in Arduino loop(), excluding the final delay(1).
static uint32_t loop_window_ms = 0;
static uint32_t loop_max_us = 0;
static uint8_t cpu_load_pct = 0;
static uint16_t loop_max_ms = 0;
static TaskStatus_t cpu_task_stats[48];
static configRUN_TIME_COUNTER_TYPE cpu_prev_total = 0;
static configRUN_TIME_COUNTER_TYPE cpu_prev_idle = 0;
static bool cpu_runtime_valid = false;

static void sample_cpu_load() {
  configRUN_TIME_COUNTER_TYPE total_runtime = 0;
  const UBaseType_t task_count = uxTaskGetSystemState(
    cpu_task_stats, sizeof(cpu_task_stats) / sizeof(cpu_task_stats[0]), &total_runtime);
  if (!task_count) return;

  configRUN_TIME_COUNTER_TYPE idle_runtime = 0;
  for (UBaseType_t i = 0; i < task_count; ++i) {
    const char* name = cpu_task_stats[i].pcTaskName;
    if (name && strncmp(name, "IDLE", 4) == 0) idle_runtime += cpu_task_stats[i].ulRunTimeCounter;
  }

  if (cpu_runtime_valid) {
    const configRUN_TIME_COUNTER_TYPE elapsed = total_runtime - cpu_prev_total;
    const uint64_t capacity = (uint64_t)elapsed * configNUMBER_OF_CORES;
    uint64_t idle_delta = (configRUN_TIME_COUNTER_TYPE)(idle_runtime - cpu_prev_idle);
    if (idle_delta > capacity) idle_delta = capacity;
    if (capacity) {
      cpu_load_pct = (uint8_t)(((capacity - idle_delta) * 100ULL + capacity / 2ULL) / capacity);
      if (cpu_load_pct > 100) cpu_load_pct = 100;
    }
  }
  cpu_prev_total = total_runtime;
  cpu_prev_idle = idle_runtime;
  cpu_runtime_valid = true;
}

static void record_loop_time(uint32_t busy_us) {
  if (busy_us > loop_max_us) loop_max_us = busy_us;
  const uint32_t now = millis();
  if ((uint32_t)(now - loop_window_ms) >= 1000UL) {
    uint32_t max_ms = (loop_max_us + 999UL) / 1000UL;
    if (max_ms > 65535UL) max_ms = 65535UL;
    loop_max_ms = (uint16_t)max_ms;
    sample_cpu_load();
    loop_window_ms = now;
    loop_max_us = 0;
  }
}

static uint64_t module_uid() {
  return 0x1000000000000000ULL | (esp_uid64() & 0x0FFFFFFFFFFFFFFFULL);
}
static bool valid_module_addr(uint8_t addr) {
  return addr >= ADDR_JBC_MIN && addr <= ADDR_JBC_MAX;
}

// -----------------------------------------------------------------------------
// Firmware update over OFE RS485
// -----------------------------------------------------------------------------
static bool fw_update_active = false;
static uint32_t fw_update_offset = 0;
static uint32_t fw_update_last_ms = 0;
static const uint32_t FW_UPDATE_TIMEOUT_MS = 30000UL;
#ifndef FW_UPDATE_WRITE_BUFFER_SIZE
#define FW_UPDATE_WRITE_BUFFER_SIZE 1024
#endif
static uint8_t fw_update_write_buffer[FW_UPDATE_WRITE_BUFFER_SIZE];
static size_t fw_update_write_len = 0;

static void fw_update_buffer_reset() { fw_update_write_len = 0; }
static bool fw_update_buffer_flush() {
  if (!fw_update_write_len) return true;
  const size_t n = fw_update_write_len;
  if (Update.write(fw_update_write_buffer, n) != n) return false;
  fw_update_write_len = 0;
  return true;
}
static bool fw_update_buffer_append(const uint8_t* data, size_t len) {
  size_t pos = 0;
  while (pos < len) {
    size_t free_len = FW_UPDATE_WRITE_BUFFER_SIZE - fw_update_write_len;
    if (!free_len) {
      if (!fw_update_buffer_flush()) return false;
      continue;
    }
    const size_t n = min(free_len, len - pos);
    memcpy(fw_update_write_buffer + fw_update_write_len, data + pos, n);
    fw_update_write_len += n;
    pos += n;
  }
  return true;
}
static void fw_update_abort_local() {
  if (fw_update_active) Update.abort();
  fw_update_active = false;
  fw_update_offset = 0;
  fw_update_buffer_reset();
}
static void fw_update_touch() { fw_update_last_ms = millis(); }
static void fw_update_check_timeout() {
  if (fw_update_active && (uint32_t)(millis() - fw_update_last_ms) > FW_UPDATE_TIMEOUT_MS) fw_update_abort_local();
}

// -----------------------------------------------------------------------------
// Local trace (USB/JBC byte traffic)
// -----------------------------------------------------------------------------
static const uint8_t LOCAL_TRACE_CAPACITY = 128;
static const uint8_t LOCAL_TRACE_PREVIEW = 48;
struct LocalTraceEvent {
  uint32_t ms = 0;
  uint8_t dir = 0;     // 1 RX, 2 TX
  uint8_t meta1 = 0;   // JBC command where known
  uint8_t meta2 = 0;   // status / parser marker
  uint8_t len = 0;
  uint8_t data[LOCAL_TRACE_PREVIEW];
};
static LocalTraceEvent local_trace[LOCAL_TRACE_CAPACITY];
static uint8_t local_trace_head = 0;
static uint8_t local_trace_count = 0;
static uint16_t local_trace_dropped = 0;
static bool local_trace_enabled = false;

static void local_trace_clear() {
  local_trace_head = 0;
  local_trace_count = 0;
  local_trace_dropped = 0;
}
static void local_trace_log(uint8_t dir, uint8_t meta1, uint8_t meta2, const uint8_t* data, size_t len) {
  if (!local_trace_enabled) return;
  LocalTraceEvent& ev = local_trace[local_trace_head];
  ev.ms = millis();
  ev.dir = dir;
  ev.meta1 = meta1;
  ev.meta2 = meta2;
  ev.len = (uint8_t)min(len, (size_t)LOCAL_TRACE_PREVIEW);
  if (ev.len && data) memcpy(ev.data, data, ev.len);
  local_trace_head = (uint8_t)((local_trace_head + 1) % LOCAL_TRACE_CAPACITY);
  if (local_trace_count < LOCAL_TRACE_CAPACITY) ++local_trace_count;
  else if (local_trace_dropped < 0xFFFF) ++local_trace_dropped;
}

// Bus-Diagnose uses one canonical inner-frame representation for every JBC
// transport: SRC, DST, FID (0 for P01), CMD, declared LEN, payload.  The USB
// wire framing (DLE/STX, stuffing, BCC, DLE/ETX) is intentionally not copied
// into the normal frame event; parser/BCC failures are logged separately with
// meta2 0xEE/0xEF.  meta2=1/2 identifies P01/P02 to the Master/Web decoder.
static void local_trace_log_jbc(uint8_t dir, uint8_t protocol, uint8_t source,
                                uint8_t target, uint8_t fid, uint8_t command,
                                const uint8_t* data, uint8_t len) {
  uint8_t inner[LOCAL_TRACE_PREVIEW];
  uint8_t o = 0;
  inner[o++] = source & 0x7F;
  inner[o++] = target & 0x7F;
  inner[o++] = protocol == 2 ? fid : 0;
  inner[o++] = command;
  inner[o++] = len;
  const uint8_t room = (uint8_t)(LOCAL_TRACE_PREVIEW - o);
  const uint8_t n = min(len, room);
  if (n && data) memcpy(inner + o, data, n);
  o += n;
  local_trace_log(dir, command, protocol, inner, o);
}

// -----------------------------------------------------------------------------
// CP210x USB host transport - self contained on the ESP-IDF USB Host API
// -----------------------------------------------------------------------------
static const uint16_t SILABS_VID = 0x10C4;
static const uint16_t CP210X_PID = 0xEA60;
static const uint16_t CP2105_PID = 0xEA70;
static const uint16_t CP2108_PID = 0xEA71;
static const uint32_t JBC_USB_BAUD = 500000;

static const uint8_t CP210X_REQ_OUT = USB_BM_REQUEST_TYPE_DIR_OUT |
  USB_BM_REQUEST_TYPE_TYPE_VENDOR | USB_BM_REQUEST_TYPE_RECIP_INTERFACE;
static const uint8_t CP210X_REQ_IN = USB_BM_REQUEST_TYPE_DIR_IN |
  USB_BM_REQUEST_TYPE_TYPE_VENDOR | USB_BM_REQUEST_TYPE_RECIP_INTERFACE;
static const uint8_t CP210X_IFC_ENABLE = 0x00;
static const uint8_t CP210X_SET_LINE_CTL = 0x03;
static const uint8_t CP210X_GET_LINE_CTL = 0x04;
static const uint8_t CP210X_SET_BREAK = 0x05;
static const uint8_t CP210X_SET_MHS = 0x07;
static const uint8_t CP210X_GET_MDMSTS = 0x08;
static const uint8_t CP210X_SET_XON = 0x09;
static const uint8_t CP210X_GET_COMM_STATUS = 0x10;
static const uint8_t CP210X_PURGE = 0x12;
static const uint8_t CP210X_SET_FLOW = 0x13;
static const uint8_t CP210X_GET_BAUDRATE = 0x1D;
static const uint8_t CP210X_SET_BAUDRATE = 0x1E;
static const uint16_t CP210X_CONTROL_DTR = 0x0001;
static const uint16_t CP210X_CONTROL_RTS = 0x0002;
static const uint16_t CP210X_CONTROL_WRITE_DTR = 0x0100;
static const uint16_t CP210X_CONTROL_WRITE_RTS = 0x0200;
static const uint32_t CP210X_SERIAL_DTR_ACTIVE = 0x00000001UL;
static const uint32_t CP210X_SERIAL_RTS_ACTIVE = 0x00000040UL;

struct Cp210xFlowCtl {
  uint32_t control_handshake;
  uint32_t flow_replace;
  uint32_t xon_limit;
  uint32_t xoff_limit;
} __attribute__((packed));
static_assert(sizeof(Cp210xFlowCtl) == 16, "CP210x flow block must be 16 bytes");

enum UsbSerialBackend : uint8_t {
  USB_SERIAL_BACKEND_NONE = 0,
  USB_SERIAL_BACKEND_CP210X = 1,
  // Reserved backend IDs keep the JBC protocol layer independent from the USB
  // bridge vendor. These drivers are intentionally not enabled until tested on
  // real JBC hardware using the corresponding bridge.
  USB_SERIAL_BACKEND_CDC_ACM = 2,
  USB_SERIAL_BACKEND_FTDI = 3,
  USB_SERIAL_BACKEND_CH34X = 4,
};

static usb_host_client_handle_t usb_client = nullptr;
static UsbSerialBackend usb_serial_backend = USB_SERIAL_BACKEND_NONE;
static usb_device_handle_t cp_dev = nullptr;
static usb_transfer_t* cp_rx_transfer = nullptr;
static uint8_t cp_intf = 0;
static uint8_t cp_alt = 0;
static uint8_t cp_ep_in = 0;
static uint8_t cp_ep_out = 0;
static uint16_t cp_ep_in_mps = 64;
static uint16_t cp_ep_out_mps = 64;
static bool cp_interface_claimed = false;
static volatile bool usb_host_ready = false;
static volatile bool cp_transport_ready = false;
static volatile bool cp_device_gone = false;
static volatile uint16_t cp_vid = 0;
static volatile uint16_t cp_pid = 0;
static uint32_t usb_connect_count = 0;
static uint32_t usb_disconnect_count = 0;
static uint32_t usb_rx_bytes = 0;
static uint32_t usb_tx_bytes = 0;
static uint32_t usb_errors = 0;
// CP210x UART-side diagnostics. These are read back from the bridge itself, so
// they tell us whether USB bulk writes really leave the UART and whether the
// bridge is held by flow-control or reports serial errors.
static uint32_t cp_diag_baud = 0;
static uint16_t cp_diag_line_ctl = 0;
static uint8_t cp_diag_mdmsts = 0;
static uint32_t cp_diag_comm_errors = 0;
static uint32_t cp_diag_hold_reasons = 0;
static uint32_t cp_diag_in_queue = 0;
static uint32_t cp_diag_out_queue = 0;
static bool cp_diag_valid = false;
static uint32_t cp_diag_next_ms = 0;

// USB enumeration/open retry state. USB_HOST_CLIENT_EVENT_NEW_DEV is edge-like:
// if opening/claiming/configuring the bridge fails once, ESP-IDF does not emit a
// second NEW_DEV event while the plug remains inserted. Keep the address around
// and retry locally so a transient early-enumeration/control-transfer failure does
// not force the user to unplug/replug the CP210x.
static uint8_t cp_open_retry_addr = 0;
static uint8_t cp_open_retry_attempt = 0;
static bool cp_open_retry_supported = false;
static uint32_t cp_open_retry_next_ms = 0;
static uint32_t cp_open_retry_deadline_ms = 0;
// Keep the newest NEW_DEV address even while the previous CP210x handle is still
// being torn down. Very fast unplug/replug can deliver NEW_DEV before DEV_GONE
// has been processed by poll_usb_transport(); without this pending slot the new
// device can sit behind a stale retry candidate until the retry window expires.
static uint8_t cp_pending_new_addr = 0;
static uint32_t cp_pending_new_seen_ms = 0;
static const uint32_t CP210X_PENDING_NEW_TTL_MS = 5000UL;
static const uint32_t CP210X_OPEN_SETTLE_MS = 100UL;
static const uint32_t CP210X_OPEN_ENUM_WINDOW_MS = 8000UL;
static const uint32_t CP210X_OPEN_SUPPORTED_WINDOW_MS = 30000UL;

static QueueHandle_t usb_new_dev_queue = nullptr;
static StreamBufferHandle_t usb_rx_stream = nullptr;
static SemaphoreHandle_t usb_ctrl_sem = nullptr;
static SemaphoreHandle_t usb_tx_sem = nullptr;
static volatile usb_transfer_status_t usb_sync_status = USB_TRANSFER_STATUS_ERROR;

static bool usb_enum_filter_cb(const usb_device_desc_t* dev_desc, uint8_t* configuration_value) {
  (void)dev_desc;
  if (configuration_value) *configuration_value = 1;
  return true;
}

static bool cp210x_pid_supported(uint16_t pid) {
  return pid == CP210X_PID || pid == CP2105_PID || pid == CP2108_PID;
}

static void usb_sync_transfer_cb(usb_transfer_t* transfer) {
  usb_sync_status = transfer->status;
  SemaphoreHandle_t sem = (SemaphoreHandle_t)transfer->context;
  if (sem) xSemaphoreGive(sem);
}

static bool usb_control_out(uint8_t request, uint16_t value, const void* data, uint16_t len) {
  if (!cp_dev || !usb_client) return false;
  usb_transfer_t* t = nullptr;
  if (usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + len, 0, &t) != ESP_OK || !t) return false;
  usb_setup_packet_t* setup = reinterpret_cast<usb_setup_packet_t*>(t->data_buffer);
  setup->bmRequestType = CP210X_REQ_OUT;
  setup->bRequest = request;
  setup->wValue = value;
  setup->wIndex = cp_intf;
  setup->wLength = len;
  if (len && data) memcpy(t->data_buffer + USB_SETUP_PACKET_SIZE, data, len);
  while (xSemaphoreTake(usb_ctrl_sem, 0) == pdTRUE) {}
  usb_sync_status = USB_TRANSFER_STATUS_ERROR;
  t->device_handle = cp_dev;
  t->bEndpointAddress = 0;
  t->callback = usb_sync_transfer_cb;
  t->context = usb_ctrl_sem;
  t->num_bytes = USB_SETUP_PACKET_SIZE + len;
  t->timeout_ms = 1000;
  esp_err_t err = usb_host_transfer_submit_control(usb_client, t);
  bool ok = false;
  if (err == ESP_OK && xSemaphoreTake(usb_ctrl_sem, pdMS_TO_TICKS(1200)) == pdTRUE) {
    ok = usb_sync_status == USB_TRANSFER_STATUS_COMPLETED;
  }
  usb_host_transfer_free(t);
  if (!ok) ++usb_errors;
  return ok;
}

static bool usb_control_in(uint8_t request, void* data, uint16_t len) {
  if (!cp_dev || !usb_client || !data || !len) return false;
  usb_transfer_t* t = nullptr;
  if (usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + len, 0, &t) != ESP_OK || !t) return false;
  usb_setup_packet_t* setup = reinterpret_cast<usb_setup_packet_t*>(t->data_buffer);
  setup->bmRequestType = CP210X_REQ_IN;
  setup->bRequest = request;
  setup->wValue = 0;
  setup->wIndex = cp_intf;
  setup->wLength = len;
  while (xSemaphoreTake(usb_ctrl_sem, 0) == pdTRUE) {}
  usb_sync_status = USB_TRANSFER_STATUS_ERROR;
  t->device_handle = cp_dev;
  t->bEndpointAddress = 0;
  t->callback = usb_sync_transfer_cb;
  t->context = usb_ctrl_sem;
  t->num_bytes = USB_SETUP_PACKET_SIZE + len;
  t->timeout_ms = 1000;
  esp_err_t err = usb_host_transfer_submit_control(usb_client, t);
  bool ok = false;
  if (err == ESP_OK && xSemaphoreTake(usb_ctrl_sem, pdMS_TO_TICKS(1200)) == pdTRUE) {
    ok = usb_sync_status == USB_TRANSFER_STATUS_COMPLETED;
  }
  if (ok) memcpy(data, t->data_buffer + USB_SETUP_PACKET_SIZE, len);
  usb_host_transfer_free(t);
  if (!ok) ++usb_errors;
  return ok;
}

static uint32_t cp_get_u32_le(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void refresh_cp210x_diag(bool full = false) {
  if (!cp_transport_ready || !cp_dev) { cp_diag_valid = false; return; }
  bool ok = true;
  uint8_t comm[19] = {0};
  if (usb_control_in(CP210X_GET_COMM_STATUS, comm, sizeof(comm))) {
    cp_diag_comm_errors = cp_get_u32_le(comm + 0);
    cp_diag_hold_reasons = cp_get_u32_le(comm + 4);
    cp_diag_in_queue = cp_get_u32_le(comm + 8);
    cp_diag_out_queue = cp_get_u32_le(comm + 12);
  } else ok = false;
  if (full || !cp_diag_baud) {
    uint8_t baud[4] = {0};
    uint8_t line[2] = {0};
    uint8_t mdm = 0;
    if (usb_control_in(CP210X_GET_BAUDRATE, baud, sizeof(baud))) cp_diag_baud = cp_get_u32_le(baud); else ok = false;
    if (usb_control_in(CP210X_GET_LINE_CTL, line, sizeof(line))) cp_diag_line_ctl = (uint16_t)line[0] | ((uint16_t)line[1] << 8); else ok = false;
    if (usb_control_in(CP210X_GET_MDMSTS, &mdm, 1)) cp_diag_mdmsts = mdm; else ok = false;
  } else {
    uint8_t mdm = 0;
    if (usb_control_in(CP210X_GET_MDMSTS, &mdm, 1)) cp_diag_mdmsts = mdm; else ok = false;
  }
  cp_diag_valid = ok;
}

static void cp_rx_cb(usb_transfer_t* transfer) {
  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
    if (transfer->actual_num_bytes > 0 && usb_rx_stream) {
      xStreamBufferSend(usb_rx_stream, transfer->data_buffer, transfer->actual_num_bytes, 0);
      usb_rx_bytes += (uint32_t)transfer->actual_num_bytes;
    }
  } else if (transfer->status == USB_TRANSFER_STATUS_NO_DEVICE) {
    cp_device_gone = true;
  } else if (transfer->status != USB_TRANSFER_STATUS_CANCELED) {
    ++usb_errors;
  }
  if (cp_transport_ready && !cp_device_gone && transfer == cp_rx_transfer) {
    transfer->num_bytes = transfer->data_buffer_size;
    if (usb_host_transfer_submit(transfer) != ESP_OK) {
      ++usb_errors;
      cp_device_gone = true;
    }
  }
}

static bool cp210x_bulk_write(const uint8_t* data, size_t len, uint32_t timeout_ms = 250) {
  if (!cp_transport_ready || !cp_dev || !cp_ep_out || !len) return false;
  usb_transfer_t* t = nullptr;
  if (usb_host_transfer_alloc(len, 0, &t) != ESP_OK || !t) return false;
  memcpy(t->data_buffer, data, len);
  while (xSemaphoreTake(usb_tx_sem, 0) == pdTRUE) {}
  usb_sync_status = USB_TRANSFER_STATUS_ERROR;
  t->device_handle = cp_dev;
  t->bEndpointAddress = cp_ep_out;
  t->callback = usb_sync_transfer_cb;
  t->context = usb_tx_sem;
  t->num_bytes = (int)len;
  t->timeout_ms = timeout_ms;
  esp_err_t err = usb_host_transfer_submit(t);
  bool ok = false;
  if (err == ESP_OK && xSemaphoreTake(usb_tx_sem, pdMS_TO_TICKS(timeout_ms + 100)) == pdTRUE) {
    ok = usb_sync_status == USB_TRANSFER_STATUS_COMPLETED;
  }
  usb_host_transfer_free(t);
  if (ok) usb_tx_bytes += (uint32_t)len;
  else ++usb_errors;
  return ok;
}

static bool find_bulk_interface(const usb_config_desc_t* cfg) {
  if (!cfg) return false;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(cfg);
  const uint8_t* end = p + cfg->wTotalLength;
  uint8_t cur_intf = 0xFF;
  uint8_t cur_alt = 0;
  uint8_t in_ep = 0, out_ep = 0;
  uint16_t in_mps = 0, out_mps = 0;

  while (p + 2 <= end) {
    const usb_standard_desc_t* std_desc = reinterpret_cast<const usb_standard_desc_t*>(p);
    if (std_desc->bLength < 2 || p + std_desc->bLength > end) break;
    if (std_desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
      if (cur_intf != 0xFF && in_ep && out_ep) {
        cp_intf = cur_intf; cp_alt = cur_alt;
        cp_ep_in = in_ep; cp_ep_out = out_ep;
        cp_ep_in_mps = in_mps; cp_ep_out_mps = out_mps;
        return true;
      }
      const usb_intf_desc_t* intf = reinterpret_cast<const usb_intf_desc_t*>(p);
      cur_intf = intf->bInterfaceNumber;
      cur_alt = intf->bAlternateSetting;
      in_ep = out_ep = 0;
      in_mps = out_mps = 0;
    } else if (std_desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT && cur_intf != 0xFF) {
      const usb_ep_desc_t* ep = reinterpret_cast<const usb_ep_desc_t*>(p);
      if ((ep->bmAttributes & 0x03) == USB_TRANSFER_TYPE_BULK) {
        if (ep->bEndpointAddress & 0x80) { in_ep = ep->bEndpointAddress; in_mps = ep->wMaxPacketSize; }
        else { out_ep = ep->bEndpointAddress; out_mps = ep->wMaxPacketSize; }
      }
    }
    p += std_desc->bLength;
  }
  if (cur_intf != 0xFF && in_ep && out_ep) {
    cp_intf = cur_intf; cp_alt = cur_alt;
    cp_ep_in = in_ep; cp_ep_out = out_ep;
    cp_ep_in_mps = in_mps; cp_ep_out_mps = out_mps;
    return true;
  }
  return false;
}

static void close_cp210x(bool count_disconnect = true) {
  cp_transport_ready = false;
  if (cp_rx_transfer) {
    usb_host_endpoint_halt(cp_dev, cp_ep_in);
    usb_host_endpoint_flush(cp_dev, cp_ep_in);
    delay(2);
    usb_host_transfer_free(cp_rx_transfer);
    cp_rx_transfer = nullptr;
  }
  if (cp_dev && usb_client && cp_interface_claimed) usb_host_interface_release(usb_client, cp_dev, cp_intf);
  if (cp_dev && usb_client) usb_host_device_close(usb_client, cp_dev);
  cp_dev = nullptr;
  usb_serial_backend = USB_SERIAL_BACKEND_NONE;
  cp_interface_claimed = false;
  cp_ep_in = cp_ep_out = 0;
  cp_vid = cp_pid = 0;
  cp_diag_valid = false; cp_diag_baud = 0; cp_diag_line_ctl = 0; cp_diag_mdmsts = 0;
  cp_diag_comm_errors = cp_diag_hold_reasons = cp_diag_in_queue = cp_diag_out_queue = 0;
  cp_device_gone = false;
  if (usb_rx_stream) xStreamBufferReset(usb_rx_stream);
  if (count_disconnect) ++usb_disconnect_count;
}

enum UsbSerialOpenResult : uint8_t {
  USB_SERIAL_OPEN_OK = 0,
  USB_SERIAL_OPEN_RETRY = 1,
  USB_SERIAL_OPEN_RETRY_SUPPORTED = 2,
  USB_SERIAL_OPEN_IGNORE = 3,
};

static UsbSerialOpenResult open_cp210x(uint8_t dev_addr) {
  if (!usb_client || cp_dev) return USB_SERIAL_OPEN_RETRY;
  usb_device_handle_t dev = nullptr;
  if (usb_host_device_open(usb_client, dev_addr, &dev) != ESP_OK || !dev) return USB_SERIAL_OPEN_RETRY;
  const usb_device_desc_t* desc = nullptr;
  if (usb_host_get_device_descriptor(dev, &desc) != ESP_OK || !desc) {
    usb_host_device_close(usb_client, dev);
    return USB_SERIAL_OPEN_RETRY;
  }
  if (desc->idVendor != SILABS_VID || !cp210x_pid_supported(desc->idProduct)) {
    usb_host_device_close(usb_client, dev);
    return USB_SERIAL_OPEN_IGNORE;
  }
  cp_dev = dev;
  usb_serial_backend = USB_SERIAL_BACKEND_CP210X;
  cp_vid = desc->idVendor;
  cp_pid = desc->idProduct;

  const usb_config_desc_t* cfg = nullptr;
  if (usb_host_get_active_config_descriptor(cp_dev, &cfg) != ESP_OK || !find_bulk_interface(cfg)) {
    ++usb_errors;
    close_cp210x(false);
    return USB_SERIAL_OPEN_RETRY_SUPPORTED;
  }
  if (usb_host_interface_claim(usb_client, cp_dev, cp_intf, cp_alt) != ESP_OK) {
    ++usb_errors;
    close_cp210x(false);
    return USB_SERIAL_OPEN_RETRY_SUPPORTED;
  }
  cp_interface_claimed = true;

  // CP210x serial configuration used by JBC Connect: 500000 baud, 8 data bits,
  // even parity, one stop bit, no flow control. Keep the UART disabled while the
  // bridge is configured. The bulk-IN transfer is armed before the final enable,
  // so an immediate P01/P02 discovery byte cannot be lost during USB setup.
  (void)usb_control_out(CP210X_IFC_ENABLE, 0, nullptr, 0);

  // IMPORTANT for CP2102N (VID 10C4 / PID EA60): PURGE clears the flow
  // configuration. Purge while disabled and BEFORE applying the final UART/flow
  // settings, otherwise bulk OUT can complete while the UART remains held.
  (void)usb_control_out(CP210X_PURGE, 0x000F, nullptr, 0);

  uint32_t baud_le = JBC_USB_BAUD;
  if (!usb_control_out(CP210X_SET_BAUDRATE, 0, &baud_le, sizeof(baud_le))) { close_cp210x(false); return USB_SERIAL_OPEN_RETRY_SUPPORTED; }
  const uint16_t line_ctl_8e1 = 0x0820; // CP210x: 8 data, even parity, 1 stop
  if (!usb_control_out(CP210X_SET_LINE_CTL, line_ctl_8e1, nullptr, 0)) { close_cp210x(false); return USB_SERIAL_OPEN_RETRY_SUPPORTED; }

  // Handshake.None means no CTS/DSR/DCD/XON/XOFF gating, but the CP210x
  // reference drivers still drive DTR and RTS ACTIVE in this mode.  A completely
  // zero SET_FLOW block makes both outputs INACTIVE and can leave equipment that
  // uses either modem line as an enable/wakeup signal silent even though USB bulk
  // OUT transfers complete successfully.
  Cp210xFlowCtl flow_none = {};
  flow_none.control_handshake = CP210X_SERIAL_DTR_ACTIVE;
  flow_none.flow_replace = CP210X_SERIAL_RTS_ACTIVE;
  if (!usb_control_out(CP210X_SET_FLOW, 0, &flow_none, sizeof(flow_none))) { close_cp210x(false); return USB_SERIAL_OPEN_RETRY_SUPPORTED; }

  // Make sure no stale BREAK or software-XOFF state can hold UART transmission.
  (void)usb_control_out(CP210X_SET_BREAK, 0, nullptr, 0);
  (void)usb_control_out(CP210X_SET_XON, 0, nullptr, 0);

  // Also assert both modem outputs explicitly.  SET_MHS uses state bits 0/1 and
  // write masks 8/9; 0x0303 therefore means DTR=1, RTS=1 and write both.
  (void)usb_control_out(CP210X_SET_MHS,
                        (uint16_t)(CP210X_CONTROL_DTR | CP210X_CONTROL_RTS |
                                   CP210X_CONTROL_WRITE_DTR | CP210X_CONTROL_WRITE_RTS),
                        nullptr, 0);

  size_t rx_size = max((size_t)64, (size_t)cp_ep_in_mps * 4U);
  if (usb_host_transfer_alloc(rx_size, 0, &cp_rx_transfer) != ESP_OK || !cp_rx_transfer) {
    ++usb_errors;
    close_cp210x(false);
    return USB_SERIAL_OPEN_RETRY_SUPPORTED;
  }
  cp_rx_transfer->device_handle = cp_dev;
  cp_rx_transfer->bEndpointAddress = cp_ep_in;
  cp_rx_transfer->callback = cp_rx_cb;
  cp_rx_transfer->context = nullptr;
  cp_rx_transfer->num_bytes = cp_rx_transfer->data_buffer_size;
  cp_rx_transfer->timeout_ms = 0;
  cp_transport_ready = true;
  if (usb_host_transfer_submit(cp_rx_transfer) != ESP_OK) {
    ++usb_errors;
    close_cp210x(false);
    return USB_SERIAL_OPEN_RETRY_SUPPORTED;
  }
  // Enable LAST, after RX is armed. This mirrors the discovery reopen path and
  // avoids losing the station's first bytes if it speaks immediately on COM open.
  if (!usb_control_out(CP210X_IFC_ENABLE, 1, nullptr, 0)) {
    close_cp210x(false);
    return USB_SERIAL_OPEN_RETRY_SUPPORTED;
  }
  refresh_cp210x_diag(true);
  cp_diag_next_ms = millis() + 500UL;
  ++usb_connect_count;
  return USB_SERIAL_OPEN_OK;
}
// JBC_Connect's normal USB discovery does not keep the COM port permanently open
// while blasting probes. It repeatedly opens the port, waits briefly for either
// a P01 NAK or a station-initiated P02 handshake, closes, and tries again.
// Recreate that behavior without tearing down the USB device: keep the bulk-IN
// transfer armed, cycle the CP210x UART interface, and apply the exact JBC serial
// settings before enabling the UART again. This is particularly important because
// a station may emit its discovery bytes only when the virtual COM port is opened.
static bool cp210x_reopen_uart_for_discovery(bool modem_outputs_active = false) {
  if (!cp_dev || !cp_transport_ready) return false;

  // Reproduce a real serial-port close/open closely enough for stations that use
  // CP210x modem outputs as an enable/wakeup indication.  This matters on a warm
  // OFE reboot: VBUS can stay present, so the bridge may retain DTR/RTS=ACTIVE
  // from the previous run.  Merely writing ACTIVE again has no edge for the CLMU.
  // Force both outputs inactive while the UART is disabled first; a later discovery
  // pass may explicitly raise them again and thus recreates the unplug/replug edge.
  (void)usb_control_out(CP210X_IFC_ENABLE, 0, nullptr, 0);
  (void)usb_control_out(CP210X_SET_MHS,
                        (uint16_t)(CP210X_CONTROL_WRITE_DTR | CP210X_CONTROL_WRITE_RTS),
                        nullptr, 0);
  delay(8);
  (void)usb_control_out(CP210X_PURGE, 0x000F, nullptr, 0);
  // JBCSerialPortHelper.OpenPort() discards both serial buffers immediately
  // after opening.  PURGE clears the CP210x queues; reset the already-armed
  // ESP-side stream at the same boundary so bytes from the previous attempt
  // cannot survive the virtual COM close/open cycle.
  if (usb_rx_stream) xStreamBufferReset(usb_rx_stream);

  uint32_t baud_le = JBC_USB_BAUD;
  if (!usb_control_out(CP210X_SET_BAUDRATE, 0, &baud_le, sizeof(baud_le))) return false;
  const uint16_t line_ctl_8e1 = 0x0820;
  if (!usb_control_out(CP210X_SET_LINE_CTL, line_ctl_8e1, nullptr, 0)) return false;

  Cp210xFlowCtl flow_none = {};
  if (modem_outputs_active) {
    // This is the same no-handshake/outputs-active state used by open_cp210x().
    // It provides a deliberate low->high wake edge for CLM/CLMU while preserving
    // the passive (outputs-low) JBC_Connect-style probes on the surrounding passes.
    flow_none.control_handshake = CP210X_SERIAL_DTR_ACTIVE;
    flow_none.flow_replace = CP210X_SERIAL_RTS_ACTIVE;
  }
  if (!usb_control_out(CP210X_SET_FLOW, 0, &flow_none, sizeof(flow_none))) return false;
  (void)usb_control_out(CP210X_SET_BREAK, 0, nullptr, 0);
  (void)usb_control_out(CP210X_SET_XON, 0, nullptr, 0);

  uint16_t mhs = (uint16_t)(CP210X_CONTROL_WRITE_DTR | CP210X_CONTROL_WRITE_RTS);
  if (modem_outputs_active) mhs |= (uint16_t)(CP210X_CONTROL_DTR | CP210X_CONTROL_RTS);
  (void)usb_control_out(CP210X_SET_MHS, mhs, nullptr, 0);

  // Enable LAST. The bulk-IN transfer is already armed, so discovery bytes emitted
  // immediately by the station cannot be lost during post-open configuration.
  const bool ok = usb_control_out(CP210X_IFC_ENABLE, 1, nullptr, 0);
  if (ok) {
    refresh_cp210x_diag(true);
    cp_diag_next_ms = millis() + 150UL;
  } else {
    ++usb_errors;
  }
  return ok;
}

// JBC P01/P02 uses this vendor-neutral transport surface. Adding another USB
// serial bridge later only requires a backend implementation here; the JBC frame
// encoder/decoder, scheduler and discovery state machine stay unchanged.
static bool usb_serial_ready() {
  switch (usb_serial_backend) {
    case USB_SERIAL_BACKEND_CP210X: return cp_transport_ready;
    default: return false;
  }
}

static bool usb_serial_write(const uint8_t* data, size_t len, uint32_t timeout_ms = 250) {
  switch (usb_serial_backend) {
    case USB_SERIAL_BACKEND_CP210X: return cp210x_bulk_write(data, len, timeout_ms);
    default: return false;
  }
}

static bool usb_serial_reopen_for_discovery(bool modem_outputs_active = false) {
  switch (usb_serial_backend) {
    case USB_SERIAL_BACKEND_CP210X: return cp210x_reopen_uart_for_discovery(modem_outputs_active);
    default: return false;
  }
}

static UsbSerialOpenResult open_usb_serial_candidate(uint8_t dev_addr) {
  // Current production JBC hardware uses Silicon Labs. Future backends plug into
  // this dispatcher without touching the JBC protocol engine. open_cp210x() first
  // verifies VID/PID, so unsupported USB devices are safely ignored.
  return open_cp210x(dev_addr);
}

static void cp210x_clear_open_retry() {
  cp_open_retry_addr = 0;
  cp_open_retry_attempt = 0;
  cp_open_retry_supported = false;
  cp_open_retry_next_ms = 0;
  cp_open_retry_deadline_ms = 0;
}

static void cp210x_begin_open_retry(uint8_t addr, uint32_t now) {
  cp_open_retry_addr = addr;
  cp_open_retry_attempt = 0;
  cp_open_retry_supported = false;
  cp_open_retry_next_ms = now + CP210X_OPEN_SETTLE_MS;
  cp_open_retry_deadline_ms = now + CP210X_OPEN_ENUM_WINDOW_MS;
}

static uint32_t cp210x_open_retry_backoff_ms(uint8_t attempt) {
  if (attempt < 2U) return 100UL;
  if (attempt < 5U) return 250UL;
  if (attempt < 9U) return 500UL;
  return 1000UL;
}

static void cp210x_schedule_open_retry(uint32_t now, bool supported_seen) {
  if (supported_seen && !cp_open_retry_supported) {
    cp_open_retry_supported = true;
    cp_open_retry_deadline_ms = now + CP210X_OPEN_SUPPORTED_WINDOW_MS;
  }
  if (cp_open_retry_attempt < 0xFFU) ++cp_open_retry_attempt;
  cp_open_retry_next_ms = now + cp210x_open_retry_backoff_ms(cp_open_retry_attempt);
}

static void usb_client_event_cb(const usb_host_client_event_msg_t* msg, void*) {
  if (!msg) return;
  if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    const uint8_t addr = msg->new_dev.address;
    if (usb_new_dev_queue) xQueueOverwrite(usb_new_dev_queue, &addr);
  } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    if (cp_dev && msg->dev_gone.dev_hdl == cp_dev) cp_device_gone = true;
  }
}

static void usb_lib_task(void*) {
  for (;;) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(pdMS_TO_TICKS(20), &flags);
  }
}
static void usb_client_task(void*) {
  for (;;) {
    if (usb_client) usb_host_client_handle_events(usb_client, pdMS_TO_TICKS(20));
    else vTaskDelay(pdMS_TO_TICKS(20));
  }
}

static bool start_usb_host() {
  usb_new_dev_queue = xQueueCreate(1, sizeof(uint8_t));
  usb_rx_stream = xStreamBufferCreate(4096, 1);
  usb_ctrl_sem = xSemaphoreCreateBinary();
  usb_tx_sem = xSemaphoreCreateBinary();
  if (!usb_new_dev_queue || !usb_rx_stream || !usb_ctrl_sem || !usb_tx_sem) return false;

  usb_host_config_t host_cfg = {};
  host_cfg.skip_phy_setup = false;
  host_cfg.root_port_unpowered = false;
  host_cfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
  host_cfg.enum_filter_cb = usb_enum_filter_cb;
  if (usb_host_install(&host_cfg) != ESP_OK) return false;

  usb_host_client_config_t client_cfg = {};
  client_cfg.is_synchronous = false;
  client_cfg.max_num_event_msg = 8;
  client_cfg.async.client_event_callback = usb_client_event_cb;
  client_cfg.async.callback_arg = nullptr;
  if (usb_host_client_register(&client_cfg, &usb_client) != ESP_OK) return false;

  xTaskCreatePinnedToCore(usb_lib_task, "jbcUsbLib", 4096, nullptr, 3, nullptr, 0);
  xTaskCreatePinnedToCore(usb_client_task, "jbcUsbClient", 4096, nullptr, 4, nullptr, 0);
  usb_host_ready = true;
  return true;
}

static void poll_usb_transport() {
  if (!usb_host_ready) return;
  const uint32_t now = millis();

  // Drain ALL queued insert notifications on every pass, even while an older
  // CP210x handle is still open. On a very fast unplug/replug cycle NEW_DEV for
  // the replacement can arrive before DEV_GONE for the old handle. Remember the
  // newest address so the teardown below cannot lose that replacement event.
  if (usb_new_dev_queue) {
    uint8_t addr = 0;
    while (xQueueReceive(usb_new_dev_queue, &addr, 0) == pdTRUE) {
      if (addr) {
        cp_pending_new_addr = addr;
        cp_pending_new_seen_ms = now;
      }
    }
  }

  if (cp_device_gone && cp_dev) {
    close_cp210x();
    cp210x_clear_open_retry();
    // Intentionally keep cp_pending_new_addr: it may already describe the
    // replacement device from a NEW_DEV that raced ahead of DEV_GONE.
  }

  // Latest hotplug wins. If no bridge is open, a newly observed address replaces
  // any stale retry candidate immediately instead of waiting up to the old 8/30 s
  // retry deadline. This is the key path for rapid unplug/replug recovery.
  if (!cp_dev && cp_pending_new_addr) {
    cp210x_begin_open_retry(cp_pending_new_addr, now);
    cp_pending_new_addr = 0;
    cp_pending_new_seen_ms = 0;
  } else if (cp_dev && cp_pending_new_addr && cp_pending_new_seen_ms &&
             (uint32_t)(now - cp_pending_new_seen_ms) > CP210X_PENDING_NEW_TTL_MS) {
    // A second USB device can legitimately be inserted while the current CP210x
    // remains healthy. OFE supports one JBC serial bridge, so do not keep such a
    // pending address forever. A real rapid replug reaches DEV_GONE well before
    // this grace period and therefore retains the candidate.
    cp_pending_new_addr = 0;
    cp_pending_new_seen_ms = 0;
  }

  // NEW_DEV is only a trigger. Once a candidate address is seen, keep retrying
  // that same physical device locally until it opens, is proven unsupported, or
  // the bounded retry window expires. A newer NEW_DEV preempts this candidate via
  // cp_pending_new_addr above.
  if (!cp_dev && cp_open_retry_addr &&
      (int32_t)(now - cp_open_retry_next_ms) >= 0) {
    const UsbSerialOpenResult result = open_usb_serial_candidate(cp_open_retry_addr);
    if (result == USB_SERIAL_OPEN_OK) {
      cp210x_clear_open_retry();
    } else if (result == USB_SERIAL_OPEN_IGNORE) {
      cp210x_clear_open_retry();
    } else {
      const bool supported_seen = result == USB_SERIAL_OPEN_RETRY_SUPPORTED;
      cp210x_schedule_open_retry(now, supported_seen);
      if ((int32_t)(now - cp_open_retry_deadline_ms) >= 0) {
        cp210x_clear_open_retry();
      }
    }
  }

  if (cp_transport_ready && (int32_t)(now - cp_diag_next_ms) >= 0) {
    refresh_cp210x_diag(false);
    cp_diag_next_ms = now + 500UL;
  }
}

// -----------------------------------------------------------------------------
// JBC USB protocol core - Protocol 01 + Protocol 02
//
// JBC_Connect distinguishes the wire/frame protocol from the command protocol.
// Most stations report the same generation for both, but keeping them separate
// also covers transitional devices that use P02 framing with P01 commands.
//
// P01 wire frame: DLE STX, SRC DST CMD LEN DATA BCC, DLE ETX
// P02 wire frame: DLE STX, SRC DST FID CMD LEN DATA BCC, DLE ETX
// BCC is XOR such that XOR(STX..ETX including BCC) == 0.
// Protocol 01 discovery begins with raw NAK/SYN/ACK/ACK/address/ACK bytes.
// -----------------------------------------------------------------------------
static const uint8_t JBC_DLE = 0x10;
static const uint8_t JBC_STX = 0x02;
static const uint8_t JBC_ETX = 0x03;
static const uint8_t JBC_ACK = 0x06;
static const uint8_t JBC_NAK = 0x15;
static const uint8_t JBC_SYN = 0x16;
static const uint8_t JBC_CMD_HS = 0x00;
static const uint8_t JBC_CMD_FIRMWARE = 0x21;
// Original JBC_Connect.dll Device-ID commands. P02 uses 0x1E/0x1F for
// read/write. Protocol 01 SOLD uses the legacy 0xB9/0xBA pair.
static const uint8_t JBC_CMD_DEVICE_UID_READ_P02 = 0x1E;
static const uint8_t JBC_CMD_DEVICE_UID_WRITE_P02 = 0x1F;
static const uint8_t JBC_CMD_DEVICE_UID_READ_P01 = 0xB9;
static const uint8_t JBC_CMD_DEVICE_UID_WRITE_P01 = 0xBA;
// JBC_Connect temporarily switches the station to CONTROL before creating a
// missing UUID, then returns it to MONITOR. On USB/P02 this is M_W_CONNECTSTATUS
// 0xE1 with the ASCII payload ":C" / ":M". P01 SOLD uses command 0x1F and a
// single ASCII 'C' / 'M' byte.
static const uint8_t JBC_CMD_CONNECT_WRITE_P02_USB = 0xE1;
static const uint8_t JBC_CMD_CONNECT_WRITE_P01 = 0x1F;
static const uint8_t JBC_CMD_INFO_PORT = 0x30;
// CLM/CLMU cleaner commands from SendFrame02_CL / ReceiveFrame02_CL. All
// read requests are station-wide Protocol-02 messages with an empty payload.
static const uint8_t JBC_CMD_CL_MOTORS_STATE = 0x32;
static const uint8_t JBC_CMD_CL_DOORS_STATE = 0x38;
static const uint8_t JBC_CMD_CL_COUNTERS = 0xC0;
static const uint8_t JBC_CMD_CL_COUNTERS_PARTIAL = 0xC2;
static const uint8_t JBC_CMD_CL_CONNECT_STATUS = 0xE0;
// User-configurable station name (Settings.Name) uses a station-family-specific
// read command in the original JBC_Connect DLL: SOLD/HA/PH = 0xB1, SF/FE =
// 0x5B, CL = 0x54. Protocol 01 SOLD also uses the legacy 0xB1 command.
static const uint8_t JBC_CMD_DEVICE_NAME_STD = 0xB1;
static const uint8_t JBC_CMD_DEVICE_NAME_SF_FE = 0x5B;
static const uint8_t JBC_CMD_DEVICE_NAME_CL = 0x54;
// JBCStationsData.dll: SOLD P02 ReadDelayTime = 0x5A. Station error is
// 0xAE for SOLD/HA/PH (and SOLD P01), while SF/FE use 0x59.
static const uint8_t JBC_CMD_DELAY_TIME_P02_SOLD = 0x5A;
// Original SOLD tool settings. ReadSleepDelay/ReadHiberDelay are 0x40/0x44
// in both P01 and P02; the request payload is {port, internalTool}.
static const uint8_t JBC_CMD_SLEEP_DELAY_SOLD = 0x40;
static const uint8_t JBC_CMD_HIBER_DELAY_SOLD = 0x44;
// SOLD/DDE status/settings from the original JBC_Connect DLL.
static const uint8_t JBC_CMD_LEVELS_SOLD = 0x33;
static const uint8_t JBC_CMD_SLEEP_TEMP_SOLD = 0x42;
static const uint8_t JBC_CMD_ADJUST_TEMP_SOLD = 0x46;
// SOLD cartridge/service data from JBC_Connect.dll. 0x48 is the cartridge
// record; 0x52/0x53/0x54 return A/B tip temperature, current and power.
static const uint8_t JBC_CMD_CARTRIDGE_SOLD = 0x48;
static const uint8_t JBC_CMD_SELECT_TEMP_SOLD = 0x50;
static const uint8_t JBC_CMD_TIP_TEMP_SOLD = 0x52;
static const uint8_t JBC_CMD_CURRENT_SOLD = 0x53;
static const uint8_t JBC_CMD_POWER_PERTHOUSAND_SOLD = 0x54;
// M_R_TOOLLASTSTATE in both SendFrame02_k20_SOLD and SendFrame02_k26_SOLD.
// Reply: {raw ToolStatus byte, port}; bit5=QSTLock, bit6=ActiveCleaning.
static const uint8_t JBC_CMD_TOOL_TYPE_SOLD = 0x55;
static const uint8_t JBC_CMD_TOOL_LAST_ERROR_SOLD = 0x56;
static const uint8_t JBC_CMD_TOOL_STATUS_SOLD = 0x57;
static const uint8_t JBC_CMD_MOS_TEMP_P01_SOLD = 0x58;
static const uint8_t JBC_CMD_MOS_TEMP_P02_SOLD = 0x59;
static const uint8_t JBC_CMD_ALARM_MAX_SOLD = 0x83;
static const uint8_t JBC_CMD_ALARM_MIN_SOLD = 0x85;
// 0x87 is M_R_ALARMTEMPSET_NCLEAR: read AND clear. Never poll it.
static const uint8_t JBC_CMD_ALARM_TRIGGER_NCLEAR_SOLD = 0x87;
// SOLD QST settings from JBC_Connect.dll. Protocol 02 uses 0x9C/0x9E,
// Protocol 01 uses the legacy 0xD0/0xD2 commands. ReadLockPort is separate
// (P02 0x88 / P01 0xD4) and represents EnabledPort, not QSTLock. In the
// P02 InfoPort status byte bit5 is likewise the inverted EnabledPort state.
static const uint8_t JBC_CMD_LOCK_PORT_P02 = 0x88;
static const uint8_t JBC_CMD_QST_ACTIVATE_P02 = 0x9C;
static const uint8_t JBC_CMD_QST_STATUS_P02 = 0x9E;
static const uint8_t JBC_CMD_QST_ACTIVATE_P01 = 0xD0;
static const uint8_t JBC_CMD_QST_STATUS_P01 = 0xD2;
static const uint8_t JBC_CMD_LOCK_PORT_P01 = 0xD4;
// HA/JT/JTSE read-only settings/status, matching SendFrame02_HA in JBC_Connect.dll.
static const uint8_t JBC_CMD_PROFILE_MODE_HA = 0x33;
static const uint8_t JBC_CMD_HEATER_STATUS_HA = 0x35;
static const uint8_t JBC_CMD_SUCTION_STATUS_HA = 0x37;
static const uint8_t JBC_CMD_EXTERNAL_TC_MODE_HA = 0x39;
static const uint8_t JBC_CMD_LEVELS_HA = 0x40;
static const uint8_t JBC_CMD_ADJUST_TEMP_HA = 0x42;
static const uint8_t JBC_CMD_TIME_TO_STOP_HA = 0x44;
static const uint8_t JBC_CMD_START_MODE_HA = 0x46;
static const uint8_t JBC_CMD_SELECT_TEMP_HA = 0x50;
static const uint8_t JBC_CMD_SELECT_FLOW_HA = 0x59;
static const uint8_t JBC_CMD_SELECT_EXT_TEMP_HA = 0x5B;
static const uint8_t JBC_CMD_AIR_TEMP_HA = 0x52;
static const uint8_t JBC_CMD_POWER_HA = 0x54;
static const uint8_t JBC_CMD_CONNECT_TOOL_HA = 0x55;
static const uint8_t JBC_CMD_TOOL_ERROR_HA = 0x56;
static const uint8_t JBC_CMD_TOOL_STATUS_HA = 0x57;
static const uint8_t JBC_CMD_ACTUAL_EXT_TEMP_HA = 0x5F;
static const uint8_t JBC_CMD_AIR_FLOW_HA = 0x5D;
static const uint8_t JBC_CMD_REMOTE_MODE_HA = 0x60;
static const uint8_t JBC_CMD_SELECTED_PROFILE_HA = 0x9A;
static const uint8_t JBC_CMD_TEMP_UNIT_HA = 0xA0;
static const uint8_t JBC_CMD_MAXMIN_TEMP_HA = 0xA2;
static const uint8_t JBC_CMD_MAXMIN_FLOW_HA = 0xA4;
static const uint8_t JBC_CMD_MAXMIN_EXT_TEMP_HA = 0xA6;
static const uint8_t JBC_CMD_PIN_ENABLED_HA = 0xA8;
static const uint8_t JBC_CMD_PIN_HA = 0xAC;
static const uint8_t JBC_CMD_BEEP_HA = 0xB3;
static const uint8_t JBC_CMD_COUNTER_PLUG_HA = 0xC0;
static const uint8_t JBC_CMD_COUNTER_WORK_HA = 0xC2;
static const uint8_t JBC_CMD_COUNTER_WORK_CYCLES_HA = 0xC4;
static const uint8_t JBC_CMD_COUNTER_SUCTION_CYCLES_HA = 0xC6;
static const uint8_t JBC_CMD_COUNTER_PLUG_PARTIAL_HA = 0xD0;
static const uint8_t JBC_CMD_COUNTER_WORK_PARTIAL_HA = 0xD2;
static const uint8_t JBC_CMD_COUNTER_WORK_CYCLES_PARTIAL_HA = 0xD4;
static const uint8_t JBC_CMD_COUNTER_SUCTION_CYCLES_PARTIAL_HA = 0xD6;
static const uint8_t JBC_CMD_ROBOT_CONFIG_HA = 0xF0;
static const uint8_t JBC_CMD_ROBOT_STATUS_HA = 0xF2;
// PH/PHBE/PHNE/PHSE/PHXL read-only commands from SendFrame02_PH. These are
// the exact reads used by the original JBC_Connect UpdateData_PH loop. The
// destructive 0xB0 ReadResetParam command is intentionally NOT defined/polled.
static const uint8_t JBC_CMD_WORK_MODE_PH = 0x33;
static const uint8_t JBC_CMD_HEATER_STATUS_PH = 0x35;
static const uint8_t JBC_CMD_EXTERNAL_TC_MODE_PH = 0x39;
static const uint8_t JBC_CMD_TIME_TO_STOP_PH = 0x44;
static const uint8_t JBC_CMD_SELECT_TEMP_PH = 0x50;
static const uint8_t JBC_CMD_SELECT_POWER_PH = 0x52;
static const uint8_t JBC_CMD_TC_WARNING_PH = 0x58;
static const uint8_t JBC_CMD_ACTIVE_ZONES_PH = 0x5B;
static const uint8_t JBC_CMD_EXTERNAL_AIR_TEMP_PH = 0x5F;
static const uint8_t JBC_CMD_REMOTE_MODE_PH = 0x60;
static const uint8_t JBC_CMD_PROFILE_PH = 0x90;
static const uint8_t JBC_CMD_PROFILE_SETTINGS_PH = 0x92;
static const uint8_t JBC_CMD_PROFILE_TEACH_PH = 0x94;
static const uint8_t JBC_CMD_MAXMIN_POWER_PH = 0xA2;
static const uint8_t JBC_CMD_MAXMIN_TEMP_PH = 0xA6;
static const uint8_t JBC_CMD_PIN_ENABLED_PH = 0xA8;
static const uint8_t JBC_CMD_PIN_PH = 0xAC;
static const uint8_t JBC_CMD_BEEP_PH = 0xB3;
static const uint8_t JBC_CMD_COUNTER_PLUG_PH = 0xC0;
static const uint8_t JBC_CMD_COUNTER_WORK_PH = 0xC2;
static const uint8_t JBC_CMD_COUNTER_WORK_CYCLES_PH = 0xC7;
static const uint8_t JBC_CMD_COUNTER_PLUG_PARTIAL_PH = 0xD0;
static const uint8_t JBC_CMD_COUNTER_WORK_PARTIAL_PH = 0xD2;
static const uint8_t JBC_CMD_COUNTER_WORK_CYCLES_PARTIAL_PH = 0xD7;
static const uint8_t JBC_CMD_CONNECT_STATUS_PH = 0xE0;
static const uint8_t JBC_CMD_ROBOT_CONFIG_PH = 0xF0;
static const uint8_t JBC_CMD_ROBOT_STATUS_PH = 0xF2;
// FE/F1/F2/F2W/F4W read-only commands from SendFrame02_FE. This includes
// the cyclic UpdateData_FE set plus the remaining non-destructive public Read...
// API. ReadResetFilter(0x42) and ReadResetParam(0x50) are actions and stay out.
static const uint8_t JBC_CMD_FLOW_FE = 0x32;
static const uint8_t JBC_CMD_SPEED_FE = 0x33;
static const uint8_t JBC_CMD_SELECT_FLOW_FE = 0x34;
static const uint8_t JBC_CMD_STAND_INTAKES_FE = 0x36;
static const uint8_t JBC_CMD_INTAKE_ACTIVATION_FE = 0x38;
static const uint8_t JBC_CMD_SUCTION_DELAY_FE = 0x3A;
static const uint8_t JBC_CMD_TIME_TO_STOP_SUCTION_FE = 0x3C;
static const uint8_t JBC_CMD_ACTIVATION_PEDAL_FE = 0x3D;
static const uint8_t JBC_CMD_PEDAL_MODE_FE = 0x3F;
static const uint8_t JBC_CMD_FILTER_STATUS_FE = 0x41;
static const uint8_t JBC_CMD_CONNECTED_PEDAL_FE = 0x44;
static const uint8_t JBC_CMD_PIN_FE = 0x51;
static const uint8_t JBC_CMD_BEEP_FE = 0x55;
static const uint8_t JBC_CMD_CONTINUOUS_SUCTION_FE = 0x57;
static const uint8_t JBC_CMD_COUNTERS_FE = 0xC0;
static const uint8_t JBC_CMD_COUNTERS_PARTIAL_FE = 0xC2;
static const uint8_t JBC_CMD_CONNECT_STATUS_FE = 0xE0;
static const uint8_t JBC_CMD_ROBOT_CONFIG_FE = 0xF0;
static const uint8_t JBC_CMD_ROBOT_STATUS_FE = 0xF2;
// SF/Solder Feeder read-only commands from SendFrame02_SF. These mirror the
// exact non-destructive reads used by UpdateData_SF. ReadResetParam (0x50) is
// destructive and BackwardMode (0x3D) is not part of UpdateData_SF, so neither
// is cyclically polled here. DispenserMode uses the existing InfoPort 0x30 path.
static const uint8_t JBC_CMD_PROGRAM_SF = 0x32;
static const uint8_t JBC_CMD_PROGRAM_LIST_SF = 0x34;
static const uint8_t JBC_CMD_SPEED_SF = 0x36;
static const uint8_t JBC_CMD_LENGTH_SF = 0x38;
static const uint8_t JBC_CMD_FEEDING_SF = 0x3C;
static const uint8_t JBC_CMD_PIN_SF = 0x51;
static const uint8_t JBC_CMD_BEEP_SF = 0x55;
static const uint8_t JBC_CMD_LENGTH_UNIT_SF = 0x57;
static const uint8_t JBC_CMD_TOOL_ENABLED_SF = 0x5D;
static const uint8_t JBC_CMD_PIN_ENABLED_SF = 0x5F;
static const uint8_t JBC_CMD_COUNTERS_SF = 0xC0;
static const uint8_t JBC_CMD_COUNTERS_PARTIAL_SF = 0xC2;
static const uint8_t JBC_CMD_CONNECT_STATUS_SF = 0xE0;
static const uint8_t JBC_CMD_ROBOT_CONFIG_SF = 0xF0;
static const uint8_t JBC_CMD_ROBOT_STATUS_SF = 0xF2;
// Global operating counters (k20 protocol). K26-capable stations use 0xC0 as
// one grouped counter reply; see sold_k26_protocol().
static const uint8_t JBC_CMD_COUNTER_PLUG = 0xC0;
static const uint8_t JBC_CMD_COUNTER_WORK = 0xC2;
static const uint8_t JBC_CMD_COUNTER_SLEEP = 0xC4;
static const uint8_t JBC_CMD_COUNTER_HIBER = 0xC6;
static const uint8_t JBC_CMD_COUNTER_IDLE = 0xC8;
static const uint8_t JBC_CMD_COUNTER_SLEEP_CYCLES = 0xCA;
static const uint8_t JBC_CMD_COUNTER_DESOLD_CYCLES = 0xCC;
// P02/k20 partial counters. K26-capable SOLD stations use 0xC2 as the grouped
// partial-counter command instead (same command byte as k20 Work counter).
static const uint8_t JBC_CMD_COUNTER_PLUG_PARTIAL_SOLD = 0xD0;
static const uint8_t JBC_CMD_COUNTER_WORK_PARTIAL_SOLD = 0xD2;
static const uint8_t JBC_CMD_COUNTER_SLEEP_PARTIAL_SOLD = 0xD4;
static const uint8_t JBC_CMD_COUNTER_HIBER_PARTIAL_SOLD = 0xD6;
static const uint8_t JBC_CMD_COUNTER_IDLE_PARTIAL_SOLD = 0xD8;
static const uint8_t JBC_CMD_COUNTER_SLEEP_CYCLES_PARTIAL_SOLD = 0xDA;
static const uint8_t JBC_CMD_COUNTER_DESOLD_CYCLES_PARTIAL_SOLD = 0xDC;
// Remaining SOLD settings/diagnostics mirrored from UpdateData_SOLD and the
// non-destructive public JBC_Connect read API. Protocol-specific aliases are
// kept explicit where command numbers differ.
static const uint8_t JBC_CMD_FIX_TEMP_P01_SOLD = 0x31;
static const uint8_t JBC_CMD_LEVEL1_P01_SOLD = 0x35;
static const uint8_t JBC_CMD_LEVEL2_P01_SOLD = 0x37;
static const uint8_t JBC_CMD_LEVEL3_P01_SOLD = 0x39;
// P01 public service reads from SendFrame01_SOLD. ReadDelayTime returns
// {countdownLE16, FutureMode}; ReadStatusRemoteMode returns the separate
// extractor/desolder/sleep status byte for one port.
static const uint8_t JBC_CMD_DELAY_TIME_P01_SOLD = 0x59;
static const uint8_t JBC_CMD_REMOTE_MODE_SOLD = 0x60;
static const uint8_t JBC_CMD_STATUS_REMOTE_P01_SOLD = 0x62;
static const uint8_t JBC_CMD_TEMP_UNIT_P01_SOLD = 0xA0;
static const uint8_t JBC_CMD_N2_MODE_P01_SOLD = 0xA6;
static const uint8_t JBC_CMD_HELP_TEXT_P01_SOLD = 0xA8;
static const uint8_t JBC_CMD_POWER_LIMIT_SOLD = 0xAA;
static const uint8_t JBC_CMD_BEEP_P01_SOLD = 0xB3;
static const uint8_t JBC_CMD_PIN_ENABLED_P01_SOLD = 0xBD;
static const uint8_t JBC_CMD_TYPE_GROUND_P02_SOLD = 0xBA;
static const uint8_t JBC_CMD_STATION_INTERFACE_P02_SOLD = 0xBE;
static const uint8_t JBC_CMD_ETHERNET_P02_SOLD = 0xE7;
static const uint8_t JBC_CMD_ASSISTANT_WARNING_SOLD = 0x8D;
static const uint8_t JBC_CMD_SOLDERING_RESULT_SOLD = 0x8C;
static const uint8_t JBC_CMD_INTERFACE_CONFIG_SOLD = 0xA0;
static const uint8_t JBC_CMD_AUTOCLEAN_SOLD = 0xA6;
static const uint8_t JBC_CMD_STATION_DATETIME_SOLD = 0xBB;
static const uint8_t JBC_CMD_FRONTAL_CONNECTION_SOLD = 0xE3;
static const uint8_t JBC_CMD_NACK = 0x15;
static const uint8_t JBC_CMD_ALE_FEEDER_INFO_SOLD = 0x70;
static const uint8_t JBC_CMD_ALE_FEEDER_PROGRAM_SOLD = 0x72;
static const uint8_t JBC_CMD_COUNTER_PLUG_PARTIAL_P01_SOLD = 0xF0;
static const uint8_t JBC_CMD_COUNTER_WORK_PARTIAL_P01_SOLD = 0xF2;
static const uint8_t JBC_CMD_COUNTER_SLEEP_PARTIAL_P01_SOLD = 0xF4;
static const uint8_t JBC_CMD_COUNTER_HIBER_PARTIAL_P01_SOLD = 0xF6;
static const uint8_t JBC_CMD_COUNTER_IDLE_PARTIAL_P01_SOLD = 0xF8;
static const uint8_t JBC_CMD_COUNTER_SLEEP_CYCLES_PARTIAL_P01_SOLD = 0xFA;
static const uint8_t JBC_CMD_COUNTER_DESOLD_CYCLES_PARTIAL_P01_SOLD = 0xFC;
// Remaining P02 SOLD settings/diagnostics mirrored from UpdateData_SOLD.
static const uint8_t JBC_CMD_PROFILE_MODE_SOLD = 0x35;
static const uint8_t JBC_CMD_ASSISTANT_CONFIG_SOLD = 0x8A;
static const uint8_t JBC_CMD_SELECTED_PROFILE_SOLD = 0x9A;
static const uint8_t JBC_CMD_MAX_TEMP_SOLD = 0xA2;
static const uint8_t JBC_CMD_MIN_TEMP_SOLD = 0xA4;
static const uint8_t JBC_CMD_PIN_ENABLED_SOLD = 0xA8;
static const uint8_t JBC_CMD_PIN_SOLD = 0xAC;
static const uint8_t JBC_CMD_ROBOT_CONFIG_SOLD = 0xF0;
static const uint8_t JBC_CMD_ROBOT_STATUS_SOLD = 0xF2;
static const uint8_t JBC_CMD_PERIPHERAL_COUNT_SOLD = 0xF9;
static const uint8_t JBC_CMD_PERIPHERAL_CONFIG_SOLD = 0xFA;
static const uint8_t JBC_CMD_PERIPHERAL_STATUS_SOLD = 0xFC;
static const uint8_t JBC_CMD_STATION_ERROR_STD = 0xAE;
static const uint8_t JBC_CMD_TRAFO_TEMP_SOLD = 0xAF;
static const uint8_t JBC_CMD_TEMP_ERROR_TRAFO_SOLD = 0xB7;
static const uint8_t JBC_CMD_TEMP_ERROR_MOS_SOLD = 0xB8;
static const uint8_t JBC_CMD_CONNECT_READ_P01 = 0x1E;
static const uint8_t JBC_CMD_CONNECT_READ_P02_USB = 0xE0;
static const uint8_t JBC_CMD_STATION_ERROR_SF_FE = 0x59;
static const uint8_t JBC_CMD_CONTI_READ = 0x80;
static const uint8_t JBC_CMD_CONTI_WRITE = 0x81;
static const uint8_t JBC_CMD_CONTI_INFO = 0x82;
static const uint8_t JBC_CONTI_SPEED_OFF = 0;
static const uint8_t JBC_CONTI_SPEED_10MS = 1;  // JBCStationsData SpeedContinuousMode.T_10mS
static const uint8_t JBC_CONTI_SPEED_100MS = 4; // JBCStationsData SpeedContinuousMode.T_100mS

static const uint8_t JBC_INITIAL_SOURCE = 0x19;
static const uint8_t JBC_INITIAL_TARGET = 0x10;
static const uint8_t JBC_HS_FID = 0xFD;
static const uint8_t JBC_FW_FID = 0xED;
static const uint8_t JBC_MAX_RUNTIME_FID = 239; // Station_Com.MAX_FID
// JBC_Connect EncodeFrame uses one byte for LENGTH and therefore accepts the
// complete 0..255-byte frame payload range for both Protocol 01 and 02.
static const size_t JBC_MAX_FRAME_DATA = 255U;
static const size_t JBC_MAX_LOGICAL_FRAME = 8U + JBC_MAX_FRAME_DATA; // P02 is the larger layout
static const uint8_t JBC_MAX_PORTS = 4;

enum JbcProtocol : uint8_t {
  JBC_PROTO_UNKNOWN = 0,
  JBC_PROTO_01 = 1,
  JBC_PROTO_02 = 2,
};

enum JbcStationKind : uint8_t {
  JBC_STATION_UNKNOWN = 0,
  JBC_STATION_SOLD = 1,
  JBC_STATION_HA = 2,
  JBC_STATION_SF = 3,
  JBC_STATION_FE = 4,
  JBC_STATION_PH = 5,
  JBC_STATION_CL = 6,
};

enum JbcLinkState : uint8_t {
  JBC_LINK_USB_DOWN = 0,
  JBC_LINK_DETECT = 1,
  JBC_LINK_P01_WAIT_ACK = 2,
  JBC_LINK_P01_WAIT_ADDR = 3,
  JBC_LINK_WAIT_FW = 4,
  JBC_LINK_ACTIVE = 5,
};

struct JbcFrame {
  JbcProtocol frame_protocol = JBC_PROTO_UNKNOWN;
  bool response = false;
  uint8_t source = 0;
  uint8_t target = 0;
  uint8_t fid = 0;
  uint8_t command = 0;
  uint8_t len = 0;
  uint8_t data[JBC_MAX_FRAME_DATA];
};

// Arduino's .ino preprocessor can otherwise emit prototypes before these local
// protocol types are declared (same class of issue as BinaryFrameMode). Keep an
// explicit declaration for every function whose signature contains a local JBC
// type so the Arduino auto-prototyper has nothing to invent above these enums.
// JbcModelInfo is defined later next to the model table; forward-declare it here
// so model_info() can also have an explicit prototype.
struct JbcModelInfo;
static const char* jbc_protocol_name(JbcProtocol p);
static const char* jbc_station_kind_name(JbcStationKind kind);
static const JbcModelInfo* model_info(const char* model);
static JbcStationKind model_station_kind(const char* model);
static void set_jbc_link_state(JbcLinkState state);
static bool jbc_send_frame(JbcProtocol frame_protocol, uint8_t source, uint8_t target,
                           uint8_t fid, uint8_t command, const uint8_t* data, uint8_t len,
                           uint8_t pending_port = 0xFF, bool response = false,
                           bool wait_response = true);
static uint8_t pending_port_for_frame(const JbcFrame& f);
static void decode_info_port(const JbcFrame& f);
static void decode_delay_time(const JbcFrame& f);
static void decode_sold_delay_setting(const JbcFrame& f);
static bool is_sold_detail_frame(const JbcFrame& f);
static void decode_sold_detail(const JbcFrame& f);
static bool jbc_send_sold_mos_temp(uint8_t port);
static bool is_sold_mos_temp_frame(const JbcFrame& f);
static void decode_sold_mos_temp(const JbcFrame& f);
static bool jbc_send_sold_station_read(uint8_t command);
static bool jbc_send_sold_selected_profile(uint8_t port);
static bool jbc_send_sold_peripheral_read(uint8_t command, uint8_t id);
static bool is_sold_station_read_frame(const JbcFrame& f);
static void decode_sold_station_read(const JbcFrame& f);
static bool jbc_send_sold_diag(uint8_t port, uint8_t command);
static bool is_sold_diag_frame(const JbcFrame& f);
static void decode_sold_diag(const JbcFrame& f);
static bool sold_k26_protocol();
static uint8_t poll_port_limit();
static bool sold_supports_cartridges();
static bool sold_supports_qst();
static bool sold_supports_partial_counters();
static bool sold_supports_robot();
static bool sold_supports_peripherals();
static bool sold_supports_profiles();
static bool sold_supports_assistant();
static bool sold_supports_excellence_289();
static bool sold_supports_ground_type();
static bool sold_supports_ethernet();
static bool sold_supports_p01_partial_counters();
static bool sold_supports_ale_feeder();
static bool jbc_send_sold_ale_feeder_read(uint8_t port, int8_t program);
static bool is_sold_ale_feeder_frame(const JbcFrame& f);
static void decode_sold_ale_feeder(const JbcFrame& f);
static bool jbc_send_sold_p01_port_read(uint8_t port, uint8_t command, bool with_tool);
static bool jbc_send_sold_p01_counter_read(uint8_t command);
static bool is_sold_p01_extra_frame(const JbcFrame& f);
static void decode_sold_p01_extra(const JbcFrame& f);
static bool jbc_send_sold_qst(uint8_t command);
static bool is_sold_qst_frame(const JbcFrame& f);
static void decode_sold_qst(const JbcFrame& f);
static bool jbc_send_sold_lock_port(uint8_t port);
static bool is_sold_lock_port_frame(const JbcFrame& f);
static void decode_sold_lock_port(const JbcFrame& f);
static bool jbc_send_conti_read();
static bool jbc_send_conti_write(uint8_t speed, uint8_t ports);
static bool jbc_send_cl_read(uint8_t command);
static bool is_cl_read_frame(const JbcFrame& f);
static void decode_cl_read(const JbcFrame& f);
static void decode_conti_read(const JbcFrame& f);
static void decode_conti_info(const JbcFrame& f);
static bool ha_supports_temp_levels();
static bool jbc_send_ha_connect_status();
static bool is_ha_connect_status_frame(const JbcFrame& f);
static void decode_ha_connect_status(const JbcFrame& f);
static bool jbc_send_ha_detail(uint8_t port, uint8_t command);
static bool is_ha_station_diag_frame(const JbcFrame& f);
static void decode_ha_station_diag(const JbcFrame& f);
static bool is_ha_detail_frame(const JbcFrame& f);
static void decode_ha_detail(const JbcFrame& f);
static bool jbc_send_ph_read(uint8_t command, uint8_t context = 0xFF);
static bool is_ph_read_frame(const JbcFrame& f);
static void decode_ph_read(const JbcFrame& f);
static bool jbc_send_fe_read(uint8_t command, uint8_t port = 0xFF, uint8_t intake = 0xFF);
static bool is_fe_read_frame(const JbcFrame& f);
static void decode_fe_read(const JbcFrame& f);
static bool jbc_send_sf_read(uint8_t command, uint8_t context = 0xFF);
static bool is_sf_read_frame(const JbcFrame& f);
static void decode_sf_read(const JbcFrame& f);
static bool is_station_error_frame(const JbcFrame& f);
static void decode_station_error(const JbcFrame& f);
static bool is_device_uid_frame(const JbcFrame& f);
static uint8_t jbc_station_name_read_command();
static uint8_t jbc_station_name_write_command();
static bool jbc_send_station_name();
static bool poll_jbc_station_name_write();
static bool poll_jbc_config_write();
static bool is_station_name_frame(const JbcFrame& f);
static void decode_station_name(const JbcFrame& f);
static void jbc_clear_in_progress(JbcProtocol p);
static bool jbc_device_uid_is_valid(const uint8_t* data, uint8_t len);
static void decode_device_uid(const JbcFrame& f);
static void poll_jbc_uid_provisioning();
static void handle_jbc_frame(const JbcFrame& f);
static bool frame_matches_protocol(JbcProtocol p);
static void restart_jbc_discovery(bool mark_change, bool cycle_uart);

static JbcLinkState jbc_link_state = JBC_LINK_USB_DOWN;
static JbcProtocol jbc_frame_protocol = JBC_PROTO_UNKNOWN;
static JbcProtocol jbc_command_protocol = JBC_PROTO_UNKNOWN;
static JbcStationKind jbc_station_kind = JBC_STATION_UNKNOWN;
static uint8_t jbc_host_addr = JBC_INITIAL_SOURCE;
static uint8_t jbc_station_addr = JBC_INITIAL_TARGET;
static uint8_t jbc_next_fid = 0;
static uint32_t jbc_state_since_ms = 0;
static uint32_t last_jbc_frame_ms = 0;
static uint32_t last_jbc_tx_ms = 0;
static uint32_t jbc_rx_frames = 0;
static uint32_t jbc_tx_frames = 0;
static uint32_t jbc_bcc_errors = 0;
// Keep transport framing, command/payload decode validation and handshake
// validation separate, matching the layers used by the original JBC DLL.
static uint32_t jbc_frame_errors = 0;      // DLE/STX/ETX, logical length, parser resync/overflow
static uint32_t jbc_decode_errors = 0;     // valid transport frame, unexpected command payload/shape
// Decode diagnostics are kept separately from transport framing, mirroring the
// original JBC DLL layers.  Track the offending command so a station/firmware
// specific payload variant can be identified without enabling the raw trace.
static const uint8_t JBC_DECODE_LEN_UNKNOWN = 0xFF;
static const uint8_t JBC_DECODE_LEN_OPEN = 0xFE;
static uint32_t jbc_decode_cmd_errors[256] = {0};
static uint8_t jbc_decode_last_cmd = 0;
static uint8_t jbc_decode_last_got_len = 0;
static uint8_t jbc_decode_last_expected_min = JBC_DECODE_LEN_UNKNOWN;
static uint8_t jbc_decode_last_expected_max = JBC_DECODE_LEN_UNKNOWN;
static void jbc_note_decode_error(const JbcFrame& f,
                                  uint8_t expected_min = JBC_DECODE_LEN_UNKNOWN,
                                  uint8_t expected_max = JBC_DECODE_LEN_UNKNOWN) {
  ++jbc_decode_errors;
  if (jbc_decode_cmd_errors[f.command] != 0xFFFFFFFFUL) ++jbc_decode_cmd_errors[f.command];
  jbc_decode_last_cmd = f.command;
  jbc_decode_last_got_len = f.len;
  jbc_decode_last_expected_min = expected_min;
  jbc_decode_last_expected_max = expected_max;
}
static uint32_t jbc_handshake_errors = 0;  // discovery/ACK/NAK validation failures
static uint32_t jbc_handshake_count = 0;
static uint32_t jbc_nack_times_ms[4] = {0};
static uint8_t jbc_nack_times_count = 0;
static char jbc_protocol_text[12] = "-";
static char jbc_model_raw[32] = "-";
static char jbc_model[24] = "-";
static char jbc_model_type[16] = "-";
static uint16_t jbc_model_version = 0;
static char jbc_sw_version[24] = "-";
static char jbc_hw_version[24] = "-";
// SOLD/P02 UpdateData also probes firmware target 0x7F and target 0x00. Target
// 0 may expose an IMX secondary micro whose software version becomes the public
// station software version in the original DLL. P02 UpdateMicros repeats this
// pair in the Moderate (~15 s) tier.
static uint32_t next_sold_micro_version_poll_ms = 0;
static uint32_t next_device_versions_poll_ms = 0;
static uint8_t next_sold_micro_version_stage = 0; // 0=0x7F, 1=0x00, 2=done
// JBC Settings.Name, separate from the model string (DDE/JTSE/etc.). The DLL
// validates names to max 16 characters, so keep exactly that plus terminator.
static char jbc_station_name[17] = {0};
static uint32_t next_station_name_poll_ms = 0;
static char jbc_station_name_write_value[17] = {0};
enum JbcStationNameWriteState : uint8_t {
  JBC_NAME_WRITE_IDLE = 0,
  JBC_NAME_WRITE_ENTER_CONTROL,
  JBC_NAME_WRITE_WAIT_CONTROL,
  JBC_NAME_WRITE_SEND_NAME,
  JBC_NAME_WRITE_WAIT_NAME,
  JBC_NAME_WRITE_LEAVE_CONTROL,
  JBC_NAME_WRITE_WAIT_MONITOR,
  JBC_NAME_WRITE_VERIFY_READ,
  JBC_NAME_WRITE_WAIT_VERIFY,
};
static bool jbc_station_name_write_queued = false;
static JbcStationNameWriteState jbc_station_name_write_state = JBC_NAME_WRITE_IDLE;
static uint32_t jbc_station_name_write_due_ms = 0;
static bool jbc_station_name_write_inflight = false;
static JbcProtocol jbc_station_name_write_protocol = JBC_PROTO_UNKNOWN;
static uint8_t jbc_station_name_write_command_inflight = 0;
static uint8_t jbc_station_name_write_fid = 0;
static uint32_t jbc_station_name_write_sent_ms = 0;
enum JbcConfigWriteState : uint8_t {
  JBC_CONFIG_WRITE_IDLE = 0,
  JBC_CONFIG_WRITE_ENTER_CONTROL,
  JBC_CONFIG_WRITE_WAIT_CONTROL,
  JBC_CONFIG_WRITE_SEND_VALUE,
  JBC_CONFIG_WRITE_WAIT_VALUE,
  JBC_CONFIG_WRITE_LEAVE_CONTROL,
  JBC_CONFIG_WRITE_WAIT_MONITOR,
  JBC_CONFIG_WRITE_VERIFY_READ,
  JBC_CONFIG_WRITE_WAIT_VERIFY,
};
struct JbcConfigWriteTransaction {
  uint8_t action = 0;
  uint8_t port = 0;
  uint8_t command_count = 0;
  uint8_t command_index = 0;
  uint8_t command[4] = {0};
  uint8_t len[4] = {0};
  uint8_t data[4][25] = {{0}};
  uint8_t verify_command = 0;
  uint8_t verify_len = 0;
  uint8_t verify_data[2] = {0};
};
static JbcConfigWriteTransaction jbc_config_write;
static bool jbc_config_write_queued = false;
static JbcConfigWriteState jbc_config_write_state = JBC_CONFIG_WRITE_IDLE;
static uint32_t jbc_config_write_due_ms = 0;
static bool jbc_config_write_inflight = false;
static JbcProtocol jbc_config_write_protocol = JBC_PROTO_UNKNOWN;
static uint8_t jbc_config_write_command_inflight = 0;
static uint8_t jbc_config_write_fid = 0;
static uint32_t jbc_config_write_sent_ms = 0;
// Raw station UUID / Device-ID payload returned by JBC ReadDeviceUID.
static uint8_t jbc_device_uid[32] = {0};
static uint8_t jbc_device_uid_len = 0;
static uint8_t jbc_device_uid_attempts = 0;
static uint32_t next_uid_poll_ms = 0;
// Some stations (JTSE/CAP v1 confirmed on hardware) expose the serial link and
// answer the JBC handshake before their volatile Device-ID storage is ready.
// Keep reading until a valid ID is visible, but never infer "missing ID" from
// silence. Writes remain gated by an actual invalid/empty ReadDeviceUID reply.
static const uint32_t JBC_UID_INITIAL_READ_DELAY_MS = 500UL;
static const uint32_t JBC_UID_WRITE_GRACE_MS = 3000UL;
static const uint32_t JBC_UID_RETRY_READ_MS = 1000UL;
static const uint32_t JBC_UID_REPROVISION_DELAY_MS = 1500UL;
static uint32_t jbc_uid_write_not_before_ms = 0;

// JBC_Connect Station.TrySetUUID(): only create/write a new station UUID after
// an actual ReadDeviceUID reply was received and that reply is invalid. Silence
// or a timeout is NEVER interpreted as "no ID", so a communication problem can
// never overwrite an existing station identity.
enum JbcUidProvisionState : uint8_t {
  JBC_UID_PROVISION_IDLE = 0,
  JBC_UID_PROVISION_ENTER_CONTROL,
  JBC_UID_PROVISION_WRITE,
  JBC_UID_PROVISION_VERIFY_READ,
  JBC_UID_PROVISION_WAIT_VERIFY,
  JBC_UID_PROVISION_LEAVE_CONTROL,
  JBC_UID_PROVISION_DONE,
  JBC_UID_PROVISION_FAILED,
};
static JbcUidProvisionState jbc_uid_provision_state = JBC_UID_PROVISION_IDLE;
static uint32_t jbc_uid_provision_due_ms = 0;
static uint8_t jbc_uid_provision_verify_tries = 0;
static uint8_t jbc_uid_generated[32] = {0};
static uint8_t jbc_uid_generated_len = 0;
static bool jbc_uid_auto_created = false;

static uint8_t jbc_port_count = 0;
static bool jbc_port_count_from_model = false;
static uint8_t jbc_highest_seen_port = 0;
static uint16_t jbc_station_error = 0xFFFF;
// SOLD station diagnostics mirrored from JBC_Connect. valid bits:
// bit0 Trafo temperature, bit1 USB connect/control mode,
// bit2 Trafo overtemperature trigger, bit3 MOS overtemperature trigger.
static uint8_t jbc_sold_station_diag_flags = 0;
static uint16_t jbc_sold_trafo_temp = 0;
static uint16_t jbc_sold_trafo_error_temp = 0;
static uint16_t jbc_sold_mos_error_temp = 0;
static bool jbc_sold_control_mode = false;
// P02 SOLD station completion data (0.1.33+). Flags:
// bit0 PIN-enabled valid, bit1 PIN enabled, bit2 PIN-read valid, bit3 PIN configured,
// bit4 min temp, bit5 max temp, bit6 robot config, bit7 robot status valid,
// bit8 robot status ON, bit9 peripheral count valid. The 4-byte PIN is carried
// to the Master but the Master only exposes it in developer mode.
static uint16_t jbc_sold_extra_station_flags = 0;
static uint16_t jbc_sold_min_temp = 0;
static uint16_t jbc_sold_max_temp = 0;
static char jbc_sold_pin[5] = {0};
static uint8_t jbc_sold_robot_config[7] = {0};
static uint8_t jbc_sold_peripheral_count = 0;
struct SoldPeripheralState {
  uint8_t flags = 0; // bit0 config valid, bit1 status valid, bit2 active
  uint8_t version = 0;
  char hash_mcu_uid[5] = {0}; // DLL CPeripheralData.Hash_MCU_UID, 4 chars
  char datetime[15] = {0}; // DLL CPeripheralData.DateTime, 14 chars
  uint8_t type = 0; // 1 PD, 2 MS, 3 MN, 4 FS, 5 MV
  uint8_t port = 0xFF;
  uint8_t function = 0; // 1 Sleep, 2 Extractor, 3 Modul
  uint8_t activation = 0; // 1 Pressed, 2 Pulled
  uint8_t delay = 0;
  uint8_t pd_status = 0; // 1 CC, 2 OC, 3 OK
};
static SoldPeripheralState jbc_sold_peripherals[4];
// Comprehensive safe READ-only station data (0.1.34+). File/profile block
// transfer (0x90..0x97) and side-effecting reads (0x87, 0xB0) are excluded.
static uint32_t jbc_sold_readonly_flags = 0;
static bool jbc_sold_remote_mode = false;
static uint8_t jbc_sold_temp_unit = 0;
static bool jbc_sold_n2_mode = false;
static bool jbc_sold_help_text = false;
static uint16_t jbc_sold_power_limit = 0;
static bool jbc_sold_beep = false;
static uint8_t jbc_sold_interface[7] = {0};
static uint16_t jbc_sold_graph_temp_max = 0, jbc_sold_graph_temp_min = 0, jbc_sold_graph_temp_range = 0;
static uint16_t jbc_sold_graph_power_max = 0, jbc_sold_graph_power_min = 0;
static bool jbc_sold_autoclean = false;
static uint16_t jbc_sold_autoclean_temp = 0, jbc_sold_autoclean_seconds = 0;
static uint8_t jbc_sold_ground_type = 0;
static uint8_t jbc_sold_station_interface[4] = {0};
static uint8_t jbc_sold_datetime[7] = {0};
static uint8_t jbc_sold_ethernet[23] = {0};
static char jbc_sold_frontal[21] = {0};
static bool jbc_ha_control_mode_valid = false;
static bool jbc_ha_control_mode = false;
// CLM/CLMU station-wide state. JBC_Connect exposes ConnectStatus separately
// from the one cleaner port. The read-only polling timers mirror UpdateData_CL:
// port status ~1 s, connection mode ~15 s, counters ~60 s.
static bool jbc_cl_control_mode_valid = false;
static bool jbc_cl_control_mode = false;
static uint32_t next_cl_status_poll_ms = 0;
static bool next_cl_status_doors = false;
static uint32_t next_cl_connect_poll_ms = 0;
static uint32_t next_cl_counter_poll_ms = 0;
static bool next_cl_counter_partial = false;
static bool next_cl_counter_fast = false;
// HA station diagnostics mirrored from UpdateData_HA / ReceiveFrame02_HA.
// flags: bit0 remote mode, bit1 temp unit, bit2 temp limits, bit3 flow limits,
// bit4 external-TC limits, bit5 selected profile, bit6 robot config, bit7 robot status.
static uint16_t jbc_ha_station_diag_flags = 0;
static bool jbc_ha_remote_mode = false;
static uint8_t jbc_ha_temp_unit = 0;
static uint16_t jbc_ha_max_temp = 0;
static uint16_t jbc_ha_min_temp = 0;
static uint16_t jbc_ha_max_flow = 0;
static uint16_t jbc_ha_min_flow = 0;
static uint16_t jbc_ha_max_ext_temp = 0;
static uint16_t jbc_ha_min_ext_temp = 0;
static char jbc_ha_selected_profile[13] = {0};
static uint8_t jbc_ha_robot_config[7] = {0};
static uint8_t jbc_ha_robot_status = 0;
// Remaining UpdateData_HA station parameters. Flags: bit0 PINEnabled valid,
// bit1 enabled, bit2 PIN valid, bit3 PIN configured, bit4 Beep valid, bit5 Beep ON.
static uint8_t jbc_ha_security_flags = 0;
static char jbc_ha_pin[5] = {0};
static bool jbc_ha_beep = false;
// PH/Preheater station-wide read-only state from UpdateData_PH. ph_station_flags:
// bit0 max/min power, bit1 max/min temp, bit2 PINEnabled valid, bit3 PIN enabled,
// bit4 PIN valid, bit5 PIN configured, bit6 Beep valid, bit7 Beep ON,
// bit8 ConnectStatus valid, bit9 CONTROL, bit10 Robot config, bit11 Robot status,
// bit12 Robot ON, bit13 Profile, bit14 ProfileSettings, bit15 ProfileTeach,
// bit16 RemoteMode valid, bit17 RemoteMode ON.
static uint32_t jbc_ph_station_flags = 0;
static int16_t jbc_ph_max_power = 0, jbc_ph_min_power = 0;
static uint16_t jbc_ph_max_temp = 0, jbc_ph_min_temp = 0;
static char jbc_ph_pin[5] = {0};
static bool jbc_ph_beep = false;
static bool jbc_ph_remote_mode = false;
static uint8_t jbc_ph_robot_config[7] = {0};
static uint8_t jbc_ph_profile_points_setting = 0;
static uint8_t jbc_ph_profile_consignment = 0;
static uint8_t jbc_ph_profile_tc_regulation = 0;
static int16_t jbc_ph_profile_teach_interval = 0;
struct PhTcState {
  uint8_t flags = 0; // bit0 actual temp, bit1 warning, bit2 mode, bit3 selected temp
  uint16_t actual_temp = 0;
  uint8_t warning = 0;
  uint8_t mode = 0;
  uint16_t selected_temp = 0;
};
static PhTcState jbc_ph_tc[4];
static const uint8_t JBC_PH_PROFILE_MAX_POINTS = 47;
static const uint8_t JBC_PH_TEACH_MAX_POINTS = 94;
static uint8_t jbc_ph_profile_count = 0;
static int16_t jbc_ph_profile_time[JBC_PH_PROFILE_MAX_POINTS] = {0};
static int16_t jbc_ph_profile_value[JBC_PH_PROFILE_MAX_POINTS] = {0};
static uint8_t jbc_ph_teach_count = 0;
static int16_t jbc_ph_teach_value[JBC_PH_TEACH_MAX_POINTS] = {0};
// FE/Fume Extractor station-wide read-only UpdateData_FE state.
// flags: bit0 ContinuousSuction valid, bit1 ON, bit2 ConnectStatus valid,
// bit3 CONTROL, bit4 Robot configuration valid, bit5 Robot status valid,
// bit6 Robot status ON. Full public-read service values use the separate
// jbc_fe_service_flags record so the established E8 layout stays compatible.
static uint16_t jbc_fe_station_flags = 0;
static uint16_t jbc_fe_service_flags = 0; // flow,speed,selected-flow,filter,PIN,configured,beep-valid,beep-on
static uint16_t jbc_fe_flow_x_mil = 0;
static uint16_t jbc_fe_speed_rpm = 0;
static uint16_t jbc_fe_selected_flow_x_mil = 0;
static uint16_t jbc_fe_filter_status = 0;
static char jbc_fe_pin[5] = {0};
static bool jbc_fe_beep = false;
static uint8_t jbc_fe_robot_config[7] = {0};
// SF/Solder Feeder station-wide UpdateData_SF state. station_flags:
// bit0 PIN valid, bit1 PIN configured, bit2 PINEnabled valid, bit3 enabled,
// bit4 Beep valid, bit5 Beep ON, bit6 LengthUnit valid, bit7 ConnectStatus
// valid, bit8 CONTROL, bit9 Robot config valid, bit10 Robot status valid,
// bit11 Robot ON, bit12 ProgramList valid.
static const uint8_t JBC_SF_PROGRAM_COUNT = 35;
static uint16_t jbc_sf_station_flags = 0;
static char jbc_sf_pin[5] = {0};
static uint8_t jbc_sf_length_unit = 0;
static uint8_t jbc_sf_robot_config[7] = {0};
static uint8_t jbc_sf_program_list[JBC_SF_PROGRAM_COUNT] = {0};
struct SfProgramState {
  uint8_t flags = 0; // bit0 valid, bit1 enabled
  char name[9] = {0};
  uint16_t length[3] = {0,0,0};
  uint16_t speed[3] = {0,0,0};
};
static SfProgramState jbc_sf_programs[JBC_SF_PROGRAM_COUNT];
// Station-level QST settings. valid bit0 = QSTActivate, bit1 = QSTStatus.
// state uses the same bits and stores the returned OnOff value.
static uint8_t jbc_qst_valid_flags = 0;
static uint8_t jbc_qst_state_flags = 0;
// JBC M_R_CONTIMODE / M_I_CONTIMODE describe the station telemetry stream.
// This is intentionally NOT the OFE FAST_FLAG_CONTINUOUS suction mode.
static bool jbc_continuous_valid = false;
static uint8_t jbc_continuous_speed = 0;
static uint8_t jbc_continuous_ports = 0;
static uint32_t jbc_continuous_frames = 0;

static const uint16_t JBC_SOLD_DETAIL_TOOL_STATUS_VALID = 0x0400;
static const uint16_t JBC_SOLD_DETAIL_ENABLED_PORT_VALID = 0x0800;
static const uint16_t JBC_SOLD_DETAIL_ENABLED_PORT_ON = 0x1000;

struct JbcPortState {
  bool valid = false;
  uint8_t tool = 0;
  uint8_t error = 0;
  uint16_t temp = 0;
  uint16_t power_permille = 0;
  bool extractor = false;
  bool hibernation = false;
  bool sleep = false;
  bool stand = false;
  bool desolder = false;
  bool heater = false;
  bool cooling = false;
  bool suction = false;
  uint16_t time_to_sleep_hibern = 0;
  // HA/JT/JTSE: live ToolStatus.TimeToStop from M_INF_PORT bytes 10..11.
  // Kept separate from time_to_sleep_hibern, which carries HA Flow_x_Mil.
  uint16_t time_to_stop = 0;
  uint8_t future_mode = 0;
  // CLM/CLMU read-only extension. cl_flags: bit0 MotorsState valid, bit1
  // DoorsState valid, bit2 global grouped counters valid, bit3 partial grouped
  // counters valid. CleanerMode itself is future_mode and is valid with port.valid.
  uint8_t cl_flags = 0;
  bool cl_motors_on = false;
  bool cl_door_open = false;
  uint32_t cl_counter_plug_min = 0;
  uint32_t cl_counter_cleaning_continuous_min = 0;
  uint32_t cl_counter_cleaning_detection_min = 0;
  uint32_t cl_counter_work_cycles = 0;
  uint32_t cl_counter_door_open_cycles = 0;
  uint32_t cl_partial_plug_min = 0;
  uint32_t cl_partial_cleaning_continuous_min = 0;
  uint32_t cl_partial_cleaning_detection_min = 0;
  uint32_t cl_partial_work_cycles = 0;
  uint32_t cl_partial_door_open_cycles = 0;
  // Complete station-specific status byte from the original DLL. This keeps
  // requested/cooling/pedal/QST/etc. flags separate from FutureMode.
  uint8_t detail_flags = 0;
  // Configured SOLD delays from the original DLL settings API. Values are
  // minutes (ToolTimeSleep / ToolTimeHibernation enum values).
  uint8_t sleep_delay_min = 0;
  uint8_t hiber_delay_min = 0;
  // bit0 sleep valid, bit1 sleep enabled, bit2 hiber valid, bit3 hiber enabled
  uint8_t delay_config_flags = 0;
  // SOLD/DDE read-only details from the same commands used by JBC_Connect.
  // bit0 selected temp, bit1 sleep temp, bit2 adjust, bit3 time counters,
  // bit4 temperature levels, bit5 Sleep/Desolder cycle counters,
  // bit6 cartridge record, bit7 cartridge current, bit8 cartridge power,
  // bit9 A/B tip temperature, bit10 ToolLastStatus(0x57) valid,
  // bit11 EnabledPort valid, bit12 EnabledPort ON. Bits 10..12 were added in
  // module 0.1.24 without changing the RS485/telemetry record size.
  uint16_t detail_value_flags = 0;
  uint16_t selected_temp = 0;
  uint16_t sleep_temp = 0;
  int16_t adjust_temp = 0;
  uint8_t cartridge_on = 0;
  int16_t cartridge_jbc_code = 0;
  int16_t cartridge_adjust_300 = 0;
  int16_t cartridge_adjust_400 = 0;
  uint8_t cartridge_group = 0;
  uint8_t cartridge_family = 0;
  int16_t tip_temp_a = 0;
  int16_t tip_temp_b = 0;
  int16_t cartridge_ma_a = 0;
  int16_t cartridge_ma_b = 0;
  int16_t cartridge_power_permille_a = 0;
  int16_t cartridge_power_permille_b = 0;
  uint32_t counter_plug_min = 0;
  uint32_t counter_work_min = 0;
  uint32_t counter_sleep_min = 0;
  uint32_t counter_hiber_min = 0;
  uint32_t counter_idle_min = 0;
  uint32_t counter_sleep_cycles = 0;
  uint32_t counter_desold_cycles = 0;
  // SOLD DLL diagnostic reads (0.1.26+). sold_diag_flags:
  // bit0 MOS temperature, bit1 tool type, bit2 tool last error,
  // bit3 max-temperature alarm config, bit4 min-temperature alarm config.
  // Command 0x87 is intentionally excluded because it reads AND clears the alarm latch.
  uint8_t sold_diag_flags = 0;
  // SOLD live-source merge bookkeeping (0.1.27+). M_INF_PORT remains the
  // authoritative detail/status source; M_I_CONTIMODE supplies the faster
  // overlapping temperature/power/live-state values. Bit5 of the P02 continuous
  // status byte is deliberately kept raw because JBC_Connect interprets it
  // inconsistently outside ALE; real QSTLock still comes from 0x57.
  bool sold_conti_valid = false;
  uint8_t sold_conti_status_raw = 0;
  bool sold_soldering = false;
  bool sold_calibrating = false;
  uint32_t sold_conti_last_ms = 0;
  uint32_t sold_info_last_ms = 0;
  uint16_t sold_mos_temp = 0;
  uint8_t sold_tool_type = 0;
  uint8_t sold_tool_last_error = 0;
  int16_t sold_alarm_max_temp = 0;
  int16_t sold_alarm_max_delay_tenth_sec = 0;
  int16_t sold_alarm_min_temp = 0;
  int16_t sold_alarm_min_delay_tenth_sec = 0;
  // P02 SOLD completion data (0.1.33+). sold_extra_flags:
  // bit0 partial time counters, bit1 partial cycle counters, bit2 selected profile,
  // bit3 profile mode, bit4 assistant configuration.
  uint16_t sold_extra_flags = 0;
  uint32_t sold_partial_plug_min = 0;
  uint32_t sold_partial_work_min = 0;
  uint32_t sold_partial_sleep_min = 0;
  uint32_t sold_partial_hiber_min = 0;
  uint32_t sold_partial_idle_min = 0;
  uint32_t sold_partial_sleep_cycles = 0;
  uint32_t sold_partial_desold_cycles = 0;
  uint8_t sold_profile_mode = 0;
  char sold_selected_profile[13] = {0};
  uint8_t sold_assistant_on = 0;
  int16_t sold_assistant_warning = 0;
  int16_t sold_assistant_error = 0;
  // Safe read-only extras not carried by the legacy SOLD records.
  uint16_t sold_readonly_port_flags = 0; // fixed temp, warning code, solder result, direct P01 service values
  uint16_t sold_fixed_temp = 0;
  uint8_t sold_fixed_temp_on = 0;
  uint8_t sold_assistant_warning_code = 0;
  int16_t sold_result_similarity = 0;
  int16_t sold_result_tenths = 0;
  int16_t sold_result_energy = 0;
  uint16_t sold_direct_power_permille = 0;
  // ALE-only Tin Feeder read-only service data (0x70/0x72 + live 0x30). sold_feeder_flags:
  // bit0 config valid, bits1..5 programs 0..4 valid, bit6 live motor status valid.
  uint16_t sold_feeder_flags = 0;
  uint8_t sold_feeder_working_mode = 0;
  uint8_t sold_feeder_selected_program = 0;
  uint16_t sold_feeder_delivery_length = 0;
  uint16_t sold_feeder_delivery_speed = 0;
  uint8_t sold_feeder_tin_diameter = 0;
  uint8_t sold_feeder_remove_length = 0;
  uint8_t sold_feeder_speed_length_readonly = 0;
  uint16_t sold_feeder_selectable_programs = 0;
  uint8_t sold_feeder_clogging_detection = 0;
  uint8_t sold_feeder_motor_on = 0;
  // 0 = ADD_TIN, 1 = REMOVE_TIN (ReceiveFrame02_SOLD maps 0x30 status bit1).
  uint8_t sold_feeder_motor_direction = 0;
  uint16_t sold_feeder_program_length[5][3] = {{0}};
  uint16_t sold_feeder_program_speed[5][3] = {{0}};
  // Unique grouped k26 counter extensions. sold_special_counter_flags:
  // bit0 ALE global, bit1 ALE partial, bit2 CDE global, bit3 CDE partial.
  uint16_t sold_special_counter_flags = 0;
  uint32_t sold_tin_deliver_cycles = 0;
  uint32_t sold_tin_length = 0;
  uint32_t sold_partial_tin_deliver_cycles = 0;
  uint32_t sold_partial_tin_length = 0;
  uint32_t sold_cde_sold_number = 0;
  uint32_t sold_cde_energy_delivered = 0;
  uint32_t sold_cde_sold_total = 0;
  uint32_t sold_cde_sold_per_min = 0;
  uint32_t sold_cde_sold_ok = 0;
  uint32_t sold_cde_partial_sold_number = 0;
  uint32_t sold_cde_partial_energy_delivered = 0;
  uint32_t sold_cde_partial_sold_total = 0;
  uint32_t sold_cde_partial_sold_per_min = 0;
  uint32_t sold_cde_partial_sold_ok = 0;
  // HA/JT/JTSE detail data. ha_value_flags bits:
  // 0 protection TC, 1 selected temp, 2 selected flow, 3 selected ext temp,
  // 4 actual ext temp, 5 adjust, 6 configured TimeToStop, 7 external TC mode,
  // 8 start mode, 9 profile mode, 10 levels, 11 counters. Bits 12/13 are
  // added only while serializing D7: station ConnectStatus valid / CONTROL.
  uint16_t ha_value_flags = 0;
  // HA live-source merge bookkeeping (0.1.28+). M_INF_PORT remains the rich
  // reference/fallback source while M_I_CONTIMODE supplies high-rate temp,
  // flow, power, TimeToStop and the raw ToolStatus_HA byte. The two external
  // thermocouples from the continuous packet are retained internally and are
  // deliberately not aliased to ProtectionTC or ReadExternalAirTemp.
  bool ha_conti_valid = false;
  uint8_t ha_conti_status_raw = 0;
  uint16_t ha_conti_ext_tc1 = 0;
  uint16_t ha_conti_ext_tc2 = 0;
  uint32_t ha_conti_last_ms = 0;
  uint32_t ha_info_last_ms = 0;
  uint16_t protection_temp = 0;
  uint16_t selected_flow_permille = 0;
  uint16_t selected_ext_temp = 0;
  uint16_t actual_ext_temp = 0;
  int16_t ha_adjust_temp = 0;
  uint16_t configured_time_to_stop = 0;
  uint8_t external_tc_mode = 0;
  uint8_t start_mode = 0;
  uint8_t profile_mode = 0;
  uint8_t levels_on = 0;
  uint8_t selected_level = 0;
  uint8_t level_on[3] = {0,0,0};
  uint16_t level_temp[3] = {0,0,0};
  uint16_t level_flow_permille[3] = {0,0,0};
  uint16_t level_ext_temp[3] = {0,0,0};
  uint32_t ha_counter_plug_min = 0;
  uint32_t ha_counter_work_min = 0;
  uint32_t ha_counter_work_cycles = 0;
  uint32_t ha_counter_suction_cycles = 0;
  // HA DLL/service diagnostics (0.1.29+). ha_diag_flags bits:
  // 0 direct air temp, 1 direct power, 2 direct flow, 3 connected tool,
  // 4 last tool error, 5 raw tool status, 6 partial counters complete,
  // 7 direct heater state, 8 direct suction state.
  uint16_t ha_diag_flags = 0;
  uint16_t ha_diag_air_temp = 0;
  uint16_t ha_diag_power_permille = 0;
  uint16_t ha_diag_flow_permille = 0;
  uint8_t ha_diag_tool = 0;
  uint8_t ha_diag_error = 0;
  uint8_t ha_diag_status = 0;
  uint8_t ha_diag_heater_state = 0;
  uint8_t ha_diag_suction_state = 0;
  uint32_t ha_partial_plug_min = 0;
  uint32_t ha_partial_work_min = 0;
  uint32_t ha_partial_work_cycles = 0;
  uint32_t ha_partial_suction_cycles = 0;
  // PH/Preheater read-only settings/counters. ph_flags bits: bit0 WorkMode,
  // bit1 direct HeaterStatus, bit2 configured TimeToStop, bit3 selected Power,
  // bit4 ActiveZones, bits5..7 global plug/work/work-cycles, bits8..10 partial.
  uint16_t ph_flags = 0;
  uint8_t ph_work_mode = 0;
  uint8_t ph_heater_status = 0;
  uint32_t ph_configured_time_to_stop = 0;
  uint16_t ph_selected_power = 0;
  uint8_t ph_active_zones = 0;
  uint32_t ph_counter_plug_min = 0;
  uint32_t ph_counter_work_min_power = 0;
  uint32_t ph_counter_work_min_temp = 0;
  uint32_t ph_counter_work_min_profile = 0;
  uint32_t ph_counter_work_cycles_power = 0;
  uint32_t ph_counter_work_cycles_temp = 0;
  uint32_t ph_counter_work_cycles_profile = 0;
  uint32_t ph_partial_plug_min = 0;
  uint32_t ph_partial_work_min_power = 0;
  uint32_t ph_partial_work_min_temp = 0;
  uint32_t ph_partial_work_min_profile = 0;
  uint32_t ph_partial_work_cycles_power = 0;
  uint32_t ph_partial_work_cycles_temp = 0;
  uint32_t ph_partial_work_cycles_profile = 0;
  // FE/Fume Extractor complete UpdateData_FE read-only state. fe_flags:
  // bit0 WORK intake valid, bit1 WORK intake ON, bit2 STAND intake valid,
  // bit3 STAND intake ON, bit4 WORK TimeToStop valid, bit5 STAND TimeToStop
  // valid, bit6 pedal action valid, bit7 pedal mode valid, bit8 global
  // counters valid, bit9 partial counters valid.
  uint16_t fe_flags = 0;
  uint16_t fe_time_to_stop_work = 0;
  uint16_t fe_time_to_stop_stand = 0;
  uint8_t fe_pedal_action = 0; // 0 HOLD_DOWN, 1 PULSE
  uint8_t fe_pedal_mode = 0;
  // Remaining safe public FE getters (not part of UpdateData_FE). fe_service_flags:
  // bit0 StandIntakes, bit1 Work SuctionDelay, bit2 Stand SuctionDelay,
  // bit3 PedalConnected valid, bit4 PedalConnected ON.
  uint16_t fe_service_flags = 0;
  uint8_t fe_stand_intakes = 0;
  uint16_t fe_suction_delay_work = 0;
  uint16_t fe_suction_delay_stand = 0;
  uint8_t fe_pedal_connected = 0;
  uint32_t fe_counter_plug_min = 0;
  uint32_t fe_counter_idle_min = 0;
  uint32_t fe_counter_work_intake_min = 0;
  uint32_t fe_counter_stand_intake_min = 0;
  uint32_t fe_counter_work_cycles = 0;
  uint32_t fe_partial_plug_min = 0;
  uint32_t fe_partial_idle_min = 0;
  uint32_t fe_partial_work_intake_min = 0;
  uint32_t fe_partial_stand_intake_min = 0;
  uint32_t fe_partial_work_cycles = 0;
  // SF/Solder Feeder complete UpdateData_SF port state. sf_flags: bit0 Speed,
  // bit1 Length, bit2 Feeding, bit3 ToolEnabled valid, bit4 ToolEnabled ON,
  // bit5 global counters, bit6 partial counters. DispenserMode/SelectedProgram
  // remain in future_mode/time_to_sleep_hibern from the 0x30 InfoPort read.
  uint16_t sf_flags = 0;
  uint16_t sf_speed_tenth_mm_s = 0;
  uint16_t sf_length_tenth_mm = 0;
  uint8_t sf_feeding_state = 0;
  uint16_t sf_feeding_value_raw = 0;
  uint8_t sf_feeding_selected_program = 0;
  uint8_t sf_current_program_step = 0;
  uint64_t sf_counter_tin_length = 0;
  uint32_t sf_counter_plug_min = 0;
  uint32_t sf_counter_work_min = 0;
  uint32_t sf_counter_idle_min = 0;
  uint32_t sf_counter_work_cycles = 0;
  uint64_t sf_partial_tin_length = 0;
  uint32_t sf_partial_plug_min = 0;
  uint32_t sf_partial_work_min = 0;
  uint32_t sf_partial_idle_min = 0;
  uint32_t sf_partial_work_cycles = 0;
  uint32_t last_ms = 0;
};
static JbcPortState jbc_ports[JBC_MAX_PORTS];

struct PendingRequest {
  uint8_t command = 0;
  uint8_t port = 0xFF;
  uint32_t sent_ms = 0;
};
static PendingRequest pending_by_fid[256];
static PendingRequest p01_pending;

// JBC_Connect QueueMessages keeps one message in progress and retries it up to
// four times after the initial transmission when the normal 500 ms response
// timeout expires. Keep one compact replay copy because OFE single-flight means
// only one response-waiting request can exist at a time, independent of station
// family. This makes startup and steady-state retry behavior uniform for SOLD,
// HA, PH, FE, SF and CL without allocating a second request queue.
struct JbcRetryRequest {
  bool valid = false;
  JbcProtocol frame_protocol = JBC_PROTO_UNKNOWN;
  uint8_t source = 0;
  uint8_t target = 0;
  uint8_t fid = 0;
  uint8_t command = 0;
  uint8_t len = 0;
  uint8_t port = 0xFF;
  uint8_t retries_remaining = 0;
  uint8_t data[JBC_MAX_FRAME_DATA] = {0};
};
static JbcRetryRequest jbc_retry_request;
static const uint8_t JBC_MESSAGE_RETRY_COUNT = 4; // initial send + 4 retries = DLL total of 5 transmissions

// Initial Low/60 s tier completion tracker.  The embedded scheduler advances a
// round-robin stage as soon as its request is accepted by the transport, while a
// cold-booting station may still fail that request after all QueueMessages-style
// retries.  Track valid responses separately so the one-time startup sweep only
// finishes after every expected Low-tier read has actually decoded successfully.
// A 64-bit mask is sufficient for the largest family: SOLD/P02 k20 = 4 ports *
// 14 global/partial counter reads = 56 logical requests.
struct JbcInitialLowTracker {
  bool tracking = false;
  uint64_t expected = 0;
  uint64_t done = 0;
  uint8_t completed_passes = 0;
};
static JbcInitialLowTracker jbc_initial_low;
// A cold JBC station can either ignore Low-tier reads or briefly answer them
// with boot-time placeholder values (notably zeroed counters).  During the
// initial snapshot, sweep the complete 60 s tier three times before settling
// to the normal DLL cadence.  This is startup-only and does not raise steady
// state bus load.
static const uint8_t JBC_INITIAL_LOW_REQUIRED_PASSES = 3;
static const uint32_t JBC_INITIAL_LOW_FAST_SPACING_MS = 90UL;
static bool jbc_initial_station_name_pending = false; // FE/CL use a 60 s name fallback
static void jbc_initial_low_begin();
static void jbc_initial_low_mark_success(const JbcFrame& f, uint8_t port);
static bool jbc_initial_low_stage_done(uint8_t port, uint8_t stage);
static bool jbc_initial_low_complete();
static bool jbc_initial_low_finish_or_verify();
static bool jbc_initial_low_opportunistic_request(uint8_t command, uint8_t port);
static void jbc_initial_low_finish();
static bool jbc_write_frame_wire(JbcProtocol frame_protocol, uint8_t source, uint8_t target,
                                 uint8_t fid, uint8_t command, const uint8_t* data, uint8_t len,
                                 bool response);
static bool jbc_retry_timed_out_request(JbcProtocol frame_protocol);
static void jbc_clear_retry_for_frame(const JbcFrame& f);

// JBC_Connect Station_Com routes every response-waiting station message through
// one QueueMessages instance. QueueMessages owns exactly one _messageInProgress
// and uses a 500 ms timeout for normal responses, independent of station class.
// Mirror that transport invariant for the whole local JBC bus: SOLD, HOT_AIR,
// CL, PH, FE and SF must never receive bursts of unrelated response-waiting
// requests. Unsolicited telemetry (for example M_I_CONTIMODE/0x82) is RX-only
// and therefore does not occupy this slot. Response frames generated by OFE
// (for example the P02 handshake ACK) bypass the gate as they do in Station_Com.
//
// OFE's existing poll scheduler remains the producer: when the single slot is
// busy a caller returns false and retries on its normal short backoff. This
// reproduces the important on-wire serialization without introducing a second
// heap-backed queue into the module.
static const uint32_t JBC_SINGLE_FLIGHT_TIMEOUT_MS = 500UL;

static bool jbc_single_flight_ready(JbcProtocol frame_protocol, uint8_t target,
                                    bool response, bool wait_response) {
  // Station_Com sends response frames, explicit waitResponse=false messages and
  // low-nibble 0x0F broadcasts directly instead of placing them in QueueMessages.
  if (response || !wait_response || ((target & 0x0FU) == 0x0FU) ||
      jbc_link_state != JBC_LINK_ACTIVE) return true;
  const uint32_t now = millis();

  if (frame_protocol == JBC_PROTO_02) {
    for (uint16_t i = 0; i < 256; ++i) {
      PendingRequest& pr = pending_by_fid[i];
      if (!pr.command) continue;
      if ((uint32_t)(now - pr.sent_ms) >= JBC_SINGLE_FLIGHT_TIMEOUT_MS) {
        // QueueMessages in the original DLL retries the same in-progress message
        // before allowing the queue to advance. Re-send with the same FID so the
        // eventual response still matches this transaction.
        if (jbc_retry_request.valid && jbc_retry_request.frame_protocol == JBC_PROTO_02 &&
            jbc_retry_request.fid == (uint8_t)i && jbc_retry_timed_out_request(JBC_PROTO_02)) return false;
        // A logical QueueMessages timeout must not be mapped to a physical
        // CP210x/JBC rediscovery in OFE. Some station/service targets can ignore
        // individual safe reads while the active P01/P02 transport remains valid.
        // After the DLL-like five transmissions, fail only this request and let
        // the scheduler continue with the already established station session.
        pr = PendingRequest();
        if (jbc_retry_request.valid && jbc_retry_request.frame_protocol == JBC_PROTO_02 &&
            jbc_retry_request.fid == (uint8_t)i) jbc_retry_request = JbcRetryRequest();
        continue;
      }
      return false;
    }
    return true;
  }

  if (frame_protocol == JBC_PROTO_01) {
    if (!p01_pending.command) return true;
    if ((uint32_t)(now - p01_pending.sent_ms) >= JBC_SINGLE_FLIGHT_TIMEOUT_MS) {
      if (jbc_retry_request.valid && jbc_retry_request.frame_protocol == JBC_PROTO_01 &&
          jbc_retry_timed_out_request(JBC_PROTO_01)) return false;
      p01_pending = PendingRequest();
      if (jbc_retry_request.valid && jbc_retry_request.frame_protocol == JBC_PROTO_01)
        jbc_retry_request = JbcRetryRequest();
      // Same rule as P02: exhaust this logical request, but keep the valid
      // physical JBC session alive.
      return true;
    }
    return false;
  }

  return true;
}

// JBC_Connect queues requests and throttles its slower update tiers when the
// queue grows. OFE talks directly to the station, so low-priority snapshot
// reads use this lightweight equivalent. Stale transport entries are cleared
// here as well; a lost/NACKed request must not make the queue look busy forever.
static uint8_t p02_recent_pending_count(uint32_t max_age_ms) {
  const uint32_t now = millis();
  uint8_t count = 0;
  for (uint16_t i = 0; i < 256; ++i) {
    PendingRequest& pr = pending_by_fid[i];
    if (!pr.command) continue;
    if ((uint32_t)(now - pr.sent_ms) > max_age_ms) {
      pr = PendingRequest();
      continue;
    }
    if (count < 255) ++count;
  }
  return count;
}

static uint8_t work_mask = 0;
static uint8_t stand_mask = 0;
static uint8_t fast_flags = 0;
static uint16_t event_seq = 1;
// JBC SOLD/HA InfoPort change bits mirror the original DLL callbacks:
// bit0 selected temperature, bit1 station parameters, bits2..5 tool parameters
// for ports 0..3, bit7 counters. Slow groups are pulled forward on demand.
static uint8_t jbc_change_refresh_flags = 0;
static uint8_t jbc_change_tool_port_mask = 0;
static bool jbc_scheduler_prime_pending = false;

// CMD_GET_STATE must fit in one OFE RS485 frame (MAX_PAYLOAD = 192). Each text
// field is length-prefixed; the limits below leave room for all four 12-byte
// port records even when every string is at its maximum length. 0.1.13 adds
// three bytes per port for the configured Sleep/Hibernation delay. SW/HW text
// limits are reduced from 23 to 17 bytes; real JBC versions are much shorter.
static const uint8_t STATE_PROTOCOL_TEXT_MAX = 2;
static const uint8_t STATE_MODEL_RAW_MAX = 31;
static const uint8_t STATE_MODEL_MAX = 23;
static const uint8_t STATE_MODEL_TYPE_MAX = 15;
static const uint8_t STATE_SW_MAX = 17;
static const uint8_t STATE_HW_MAX = 17;
static const size_t JBC_STATE_FIXED_BYTES = 19;
static const size_t JBC_STATE_PORT_BYTES = 15;
static_assert(JBC_STATE_FIXED_BYTES + 6 + STATE_PROTOCOL_TEXT_MAX + STATE_MODEL_RAW_MAX +
              STATE_MODEL_MAX + STATE_MODEL_TYPE_MAX + STATE_SW_MAX + STATE_HW_MAX +
              JBC_MAX_PORTS * JBC_STATE_PORT_BYTES + 2 <= MAX_PAYLOAD,
              "JBC USB state payload exceeds OFE MAX_PAYLOAD");

static const char* jbc_protocol_name(JbcProtocol p) {
  switch (p) {
    case JBC_PROTO_01: return "01";
    case JBC_PROTO_02: return "02";
    default: return "-";
  }
}

static const char* jbc_station_kind_name(JbcStationKind kind) {
  switch (kind) {
    case JBC_STATION_SOLD: return "SOLD";
    case JBC_STATION_HA: return "HA";
    case JBC_STATION_SF: return "SF";
    case JBC_STATION_FE: return "FE";
    case JBC_STATION_PH: return "PH";
    case JBC_STATION_CL: return "CL";
    default: return "UNKNOWN";
  }
}

static void mark_fast_changed() {
  ++event_seq;
  if (!event_seq) ++event_seq;
  fast_flags |= FAST_FLAG_EVENT_PENDING | FAST_FLAG_STATE_CHANGED;
}

static void clear_jbc_runtime(bool mark_change) {
  const bool had = work_mask || stand_mask || (fast_flags & FAST_FLAG_CONNECTED);
  work_mask = 0;
  stand_mask = 0;
  fast_flags &= (uint8_t)~(FAST_FLAG_CONNECTED | FAST_FLAG_CONTINUOUS);
  jbc_change_refresh_flags = 0;
  jbc_change_tool_port_mask = 0;
  jbc_scheduler_prime_pending = false;
  memset(jbc_ports, 0, sizeof(jbc_ports));
  memset(pending_by_fid, 0, sizeof(pending_by_fid));
  p01_pending = PendingRequest();
  jbc_retry_request = JbcRetryRequest();
  jbc_initial_low = JbcInitialLowTracker();
  jbc_initial_station_name_pending = false;
  jbc_frame_protocol = JBC_PROTO_UNKNOWN;
  jbc_command_protocol = JBC_PROTO_UNKNOWN;
  jbc_station_kind = JBC_STATION_UNKNOWN;
  jbc_host_addr = JBC_INITIAL_SOURCE;
  jbc_station_addr = JBC_INITIAL_TARGET;
  jbc_next_fid = 0;
  memset(jbc_nack_times_ms, 0, sizeof(jbc_nack_times_ms));
  jbc_nack_times_count = 0;
  strcpy(jbc_protocol_text, "-");
  strcpy(jbc_model_raw, "-");
  strcpy(jbc_model, "-");
  strcpy(jbc_model_type, "-");
  jbc_model_version = 0;
  strcpy(jbc_sw_version, "-");
  strcpy(jbc_hw_version, "-");
  next_sold_micro_version_poll_ms = 0;
  next_device_versions_poll_ms = 0;
  next_sold_micro_version_stage = 0;
  jbc_station_name[0] = 0;
  next_station_name_poll_ms = 0;
  jbc_station_name_write_value[0] = 0;
  jbc_station_name_write_queued = false;
  jbc_station_name_write_state = JBC_NAME_WRITE_IDLE;
  jbc_station_name_write_due_ms = 0;
  jbc_station_name_write_inflight = false;
  jbc_station_name_write_protocol = JBC_PROTO_UNKNOWN;
  jbc_station_name_write_command_inflight = 0;
  jbc_station_name_write_fid = 0;
  jbc_station_name_write_sent_ms = 0;
  jbc_config_write = JbcConfigWriteTransaction();
  jbc_config_write_queued = false;
  jbc_config_write_state = JBC_CONFIG_WRITE_IDLE;
  jbc_config_write_due_ms = 0;
  jbc_config_write_inflight = false;
  jbc_config_write_protocol = JBC_PROTO_UNKNOWN;
  jbc_config_write_command_inflight = 0;
  jbc_config_write_fid = 0;
  jbc_config_write_sent_ms = 0;
  memset(jbc_device_uid, 0, sizeof(jbc_device_uid));
  jbc_device_uid_len = 0;
  jbc_device_uid_attempts = 0;
  next_uid_poll_ms = 0;
  jbc_uid_write_not_before_ms = 0;
  jbc_uid_provision_state = JBC_UID_PROVISION_IDLE;
  jbc_uid_provision_due_ms = 0;
  jbc_uid_provision_verify_tries = 0;
  memset(jbc_uid_generated, 0, sizeof(jbc_uid_generated));
  jbc_uid_generated_len = 0;
  jbc_uid_auto_created = false;
  jbc_port_count = 0;
  jbc_port_count_from_model = false;
  jbc_highest_seen_port = 0;
  jbc_station_error = 0xFFFF;
  jbc_sold_station_diag_flags = 0;
  jbc_sold_trafo_temp = 0;
  jbc_sold_trafo_error_temp = 0;
  jbc_sold_mos_error_temp = 0;
  jbc_sold_control_mode = false;
  jbc_sold_extra_station_flags = 0;
  jbc_sold_min_temp = jbc_sold_max_temp = 0;
  memset(jbc_sold_pin, 0, sizeof(jbc_sold_pin));
  memset(jbc_sold_robot_config, 0, sizeof(jbc_sold_robot_config));
  jbc_sold_peripheral_count = 0;
  memset(jbc_sold_peripherals, 0, sizeof(jbc_sold_peripherals));
  for (uint8_t i = 0; i < 4; ++i) jbc_sold_peripherals[i].port = 0xFF;
  jbc_sold_readonly_flags = 0;
  jbc_sold_remote_mode = false;
  jbc_sold_temp_unit = 0;
  jbc_sold_n2_mode = jbc_sold_help_text = false;
  jbc_sold_power_limit = 0;
  jbc_sold_beep = false;
  memset(jbc_sold_interface, 0, sizeof(jbc_sold_interface));
  jbc_sold_graph_temp_max = jbc_sold_graph_temp_min = jbc_sold_graph_temp_range = 0;
  jbc_sold_graph_power_max = jbc_sold_graph_power_min = 0;
  jbc_sold_autoclean = false; jbc_sold_autoclean_temp = jbc_sold_autoclean_seconds = 0;
  jbc_sold_ground_type = 0;
  memset(jbc_sold_station_interface, 0, sizeof(jbc_sold_station_interface));
  memset(jbc_sold_datetime, 0, sizeof(jbc_sold_datetime));
  memset(jbc_sold_ethernet, 0, sizeof(jbc_sold_ethernet));
  memset(jbc_sold_frontal, 0, sizeof(jbc_sold_frontal));
  jbc_ha_control_mode_valid = false;
  jbc_ha_control_mode = false;
  jbc_cl_control_mode_valid = false;
  jbc_cl_control_mode = false;
  next_cl_status_poll_ms = 0;
  next_cl_status_doors = false;
  next_cl_connect_poll_ms = 0;
  next_cl_counter_poll_ms = 0;
  next_cl_counter_partial = false;
  next_cl_counter_fast = false;
  jbc_ha_station_diag_flags = 0;
  jbc_ha_remote_mode = false;
  jbc_ha_temp_unit = 0;
  jbc_ha_max_temp = jbc_ha_min_temp = 0;
  jbc_ha_max_flow = jbc_ha_min_flow = 0;
  jbc_ha_max_ext_temp = jbc_ha_min_ext_temp = 0;
  memset(jbc_ha_selected_profile, 0, sizeof(jbc_ha_selected_profile));
  memset(jbc_ha_robot_config, 0, sizeof(jbc_ha_robot_config));
  jbc_ha_robot_status = 0;
  jbc_ha_security_flags = 0;
  memset(jbc_ha_pin, 0, sizeof(jbc_ha_pin));
  jbc_ha_beep = false;
  jbc_ph_station_flags = 0;
  jbc_ph_max_power = jbc_ph_min_power = 0;
  jbc_ph_max_temp = jbc_ph_min_temp = 0;
  memset(jbc_ph_pin, 0, sizeof(jbc_ph_pin));
  jbc_ph_beep = false;
  jbc_ph_remote_mode = false;
  memset(jbc_ph_robot_config, 0, sizeof(jbc_ph_robot_config));
  jbc_ph_profile_points_setting = 0;
  jbc_ph_profile_consignment = 0;
  jbc_ph_profile_tc_regulation = 0;
  jbc_ph_profile_teach_interval = 0;
  memset(jbc_ph_tc, 0, sizeof(jbc_ph_tc));
  jbc_ph_profile_count = 0;
  memset(jbc_ph_profile_time, 0, sizeof(jbc_ph_profile_time));
  memset(jbc_ph_profile_value, 0, sizeof(jbc_ph_profile_value));
  jbc_ph_teach_count = 0;
  memset(jbc_ph_teach_value, 0, sizeof(jbc_ph_teach_value));
  jbc_fe_station_flags = 0;
  jbc_fe_service_flags = 0;
  jbc_fe_flow_x_mil = jbc_fe_speed_rpm = jbc_fe_selected_flow_x_mil = jbc_fe_filter_status = 0;
  memset(jbc_fe_pin, 0, sizeof(jbc_fe_pin));
  jbc_fe_beep = false;
  memset(jbc_fe_robot_config, 0, sizeof(jbc_fe_robot_config));
  jbc_sf_station_flags = 0;
  memset(jbc_sf_pin, 0, sizeof(jbc_sf_pin));
  jbc_sf_length_unit = 0;
  memset(jbc_sf_robot_config, 0, sizeof(jbc_sf_robot_config));
  memset(jbc_sf_program_list, 0, sizeof(jbc_sf_program_list));
  memset(jbc_sf_programs, 0, sizeof(jbc_sf_programs));
  jbc_qst_valid_flags = 0;
  jbc_qst_state_flags = 0;
  jbc_continuous_valid = false;
  jbc_continuous_speed = 0;
  jbc_continuous_ports = 0;
  jbc_continuous_frames = 0;
  last_jbc_frame_ms = 0;
  last_jbc_tx_ms = 0;
  if (mark_change && had) mark_fast_changed();
}

static void set_jbc_link_state(JbcLinkState state) {
  if (jbc_link_state == state) return;
  jbc_link_state = state;
  jbc_state_since_ms = millis();
}

static uint8_t next_fid() {
  // Station_Com uses runtime FIDs 1..239 inclusive, then wraps to 1. Discovery
  // uses fixed FD/ED values separately and does not reserve ED from runtime.
  if (jbc_next_fid >= JBC_MAX_RUNTIME_FID) jbc_next_fid = 0;
  return ++jbc_next_fid;
}

static bool jbc_send_raw_byte(uint8_t b) {
  const bool ok = usb_serial_write(&b, 1, 250);
  if (ok) {
    ++jbc_tx_frames;
    last_jbc_tx_ms = millis();
    local_trace_log(2, b, 0xF0, &b, 1);
  }
  return ok;
}

static bool jbc_write_frame_wire(JbcProtocol frame_protocol, uint8_t source, uint8_t target,
                                 uint8_t fid, uint8_t command, const uint8_t* data, uint8_t len,
                                 bool response) {
  if (!usb_serial_ready() || frame_protocol == JBC_PROTO_UNKNOWN) return false;
  uint8_t logical[JBC_MAX_LOGICAL_FRAME];
  size_t n = 0;
  logical[n++] = JBC_STX;
  // EncodeFrame.Encode() toggles bit 7 with XOR only for response messages and
  // otherwise preserves source/target bytes exactly as supplied.
  uint8_t source_device = source;
  if (response) source_device ^= 0x80U;
  logical[n++] = source_device;
  logical[n++] = target;
  if (frame_protocol == JBC_PROTO_02) logical[n++] = fid;
  logical[n++] = command;
  logical[n++] = len;
  for (uint8_t i = 0; i < len; ++i) logical[n++] = data[i];

  uint8_t bcc = JBC_ETX;
  for (size_t i = 0; i < n; ++i) bcc ^= logical[i];
  logical[n++] = bcc;
  logical[n++] = JBC_ETX;

  uint8_t wire[2 + JBC_MAX_LOGICAL_FRAME * 2 + 2];
  size_t w = 0;
  wire[w++] = JBC_DLE;
  wire[w++] = JBC_STX;
  for (size_t i = 1; i + 1 < n; ++i) {
    wire[w++] = logical[i];
    if (logical[i] == JBC_DLE) wire[w++] = JBC_DLE;
  }
  wire[w++] = JBC_DLE;
  wire[w++] = JBC_ETX;

  const bool ok = usb_serial_write(wire, w, 250);
  if (ok) {
    ++jbc_tx_frames;
    last_jbc_tx_ms = millis();
    local_trace_log_jbc(2, (uint8_t)frame_protocol, source, target, fid, command, data, len);
  }
  return ok;
}

static bool jbc_retry_timed_out_request(JbcProtocol frame_protocol) {
  if (!jbc_retry_request.valid || jbc_retry_request.frame_protocol != frame_protocol ||
      !jbc_retry_request.retries_remaining || !usb_serial_ready()) return false;

  JbcRetryRequest& rr = jbc_retry_request;
  const bool ok = jbc_write_frame_wire(rr.frame_protocol, rr.source, rr.target, rr.fid,
                                       rr.command, rr.data, rr.len, false);
  if (!ok) return false;
  --rr.retries_remaining;
  const uint32_t now = millis();
  if (rr.frame_protocol == JBC_PROTO_02) {
    PendingRequest& pr = pending_by_fid[rr.fid];
    pr.command = rr.command; pr.port = rr.port; pr.sent_ms = now;
  } else {
    p01_pending.command = rr.command; p01_pending.port = rr.port; p01_pending.sent_ms = now;
  }
  return true;
}

static void jbc_clear_retry_for_frame(const JbcFrame& f) {
  if (!jbc_retry_request.valid) return;
  // Station_Com has dedicated NACK semantics (including command matching and
  // the BCC-error retry exception). Handle every NACK in handle_jbc_frame().
  if (f.command == JBC_CMD_NACK) return;
  if (f.frame_protocol == JBC_PROTO_02) {
    if (f.response && jbc_retry_request.frame_protocol == JBC_PROTO_02 &&
        jbc_retry_request.fid == f.fid) jbc_retry_request = JbcRetryRequest();
  } else if (f.frame_protocol == JBC_PROTO_01 && f.response &&
             jbc_retry_request.frame_protocol == JBC_PROTO_01) {
    jbc_retry_request = JbcRetryRequest();
  }
}

static bool jbc_send_frame(JbcProtocol frame_protocol, uint8_t source, uint8_t target,
                           uint8_t fid, uint8_t command, const uint8_t* data, uint8_t len,
                           uint8_t pending_port, bool response, bool wait_response) {
  if (!usb_serial_ready() || frame_protocol == JBC_PROTO_UNKNOWN) return false;
  if (!jbc_single_flight_ready(frame_protocol, target, response, wait_response)) return false;
  const bool ok = jbc_write_frame_wire(frame_protocol, source, target, fid, command, data, len, response);
  if (ok && !response && wait_response && ((target & 0x0FU) != 0x0FU)) {
    const uint32_t now = millis();
    if (frame_protocol == JBC_PROTO_02) {
      pending_by_fid[fid].command = command;
      pending_by_fid[fid].port = pending_port;
      pending_by_fid[fid].sent_ms = now;
    } else {
      p01_pending.command = command;
      p01_pending.port = pending_port;
      p01_pending.sent_ms = now;
    }
    jbc_retry_request = JbcRetryRequest();
    jbc_retry_request.valid = true;
    jbc_retry_request.frame_protocol = frame_protocol;
    jbc_retry_request.source = source;
    jbc_retry_request.target = target;
    jbc_retry_request.fid = fid;
    jbc_retry_request.command = command;
    jbc_retry_request.len = len;
    jbc_retry_request.port = pending_port;
    // Initial 60 s catch-up is intentionally opportunistic: one transmission
    // per logical read, then rotate to the next item.  The completion tracker
    // revisits only missing values and performs verification sweeps, so burning
    // 2.5 s (4 retries) on every not-yet-ready counter would only delay boot.
    jbc_retry_request.retries_remaining =
        jbc_initial_low_opportunistic_request(command, pending_port) ? 0U : JBC_MESSAGE_RETRY_COUNT;
    if (len && data) memcpy(jbc_retry_request.data, data, len);
  }
  return ok;
}

static bool jbc_send_handshake_p02() {
  const uint8_t data[1] = {JBC_DLE};
  return jbc_send_frame(JBC_PROTO_02, JBC_INITIAL_SOURCE, JBC_INITIAL_TARGET,
                        JBC_HS_FID, JBC_CMD_HS, data, 1);
}

static bool jbc_send_handshake_ack_p02() {
  const uint8_t data[1] = {JBC_ACK};
  return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr,
                        JBC_HS_FID, JBC_CMD_HS, data, 1, 0xFF, true);
}

static bool jbc_send_firmware() {
  if (jbc_frame_protocol == JBC_PROTO_01) {
    return jbc_send_frame(JBC_PROTO_01, jbc_host_addr, jbc_station_addr,
                          0, JBC_CMD_FIRMWARE, nullptr, 0);
  }
  if (jbc_frame_protocol == JBC_PROTO_02) {
    return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr,
                          JBC_FW_FID, JBC_CMD_FIRMWARE, nullptr, 0);
  }
  return false;
}

static bool jbc_send_sold_micro_version(uint8_t target) {
  if (jbc_link_state != JBC_LINK_ACTIVE || jbc_station_kind != JBC_STATION_SOLD ||
      jbc_frame_protocol != JBC_PROTO_02) return false;
  // UpdateData_SOLD sends 0x21 to 0x7F as a broadcast (Station_Com bypasses
  // QueueMessages for low-nibble 0x0F targets) and then to target 0 with
  // waitResponse=false. Keep both probes outside the response-waiting slot.
  const bool wait_response = target != 0;
  return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, target, next_fid(),
                        JBC_CMD_FIRMWARE, nullptr, 0, 0xFF, false, wait_response);
}

static uint8_t jbc_device_uid_read_command() {
  const JbcProtocol command_protocol = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
  if (command_protocol == JBC_PROTO_01) return JBC_CMD_DEVICE_UID_READ_P01;
  if (command_protocol == JBC_PROTO_02) return JBC_CMD_DEVICE_UID_READ_P02;
  return 0;
}

static uint8_t jbc_device_uid_write_command() {
  const JbcProtocol command_protocol = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
  if (command_protocol == JBC_PROTO_01) return JBC_CMD_DEVICE_UID_WRITE_P01;
  if (command_protocol == JBC_PROTO_02) return JBC_CMD_DEVICE_UID_WRITE_P02;
  return 0;
}

static bool jbc_send_command_for_uid(uint8_t command, const uint8_t* data, uint8_t len) {
  if (!command || jbc_frame_protocol == JBC_PROTO_UNKNOWN) return false;
  if (jbc_frame_protocol == JBC_PROTO_01) {
    // Provisioning pauses normal port polling. Clear the previous P01 transaction
    // slot explicitly because P01 has no FID and the UUID sequence is serialized.
    p01_pending = PendingRequest();
    return jbc_send_frame(JBC_PROTO_01, jbc_host_addr, jbc_station_addr,
                          0, command, data, len);
  }
  const uint8_t fid = next_fid();
  return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr,
                        fid, command, data, len);
}

static bool jbc_send_device_uid() {
  const uint8_t command = jbc_device_uid_read_command();
  if (!command || jbc_frame_protocol == JBC_PROTO_UNKNOWN) return false;
  bool ok = false;
  if (jbc_frame_protocol == JBC_PROTO_01) {
    ok = jbc_send_frame(JBC_PROTO_01, jbc_host_addr, jbc_station_addr,
                        0, command, nullptr, 0);
  } else {
    const uint8_t fid = next_fid();
    ok = jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr,
                        fid, command, nullptr, 0);
  }
  if (ok && jbc_device_uid_attempts != 0xFF) ++jbc_device_uid_attempts;
  return ok;
}

static uint8_t jbc_station_name_read_command() {
  if (jbc_frame_protocol == JBC_PROTO_01)
    return jbc_station_kind == JBC_STATION_SOLD ? JBC_CMD_DEVICE_NAME_STD : 0;
  if (jbc_frame_protocol != JBC_PROTO_02) return 0;
  switch (jbc_station_kind) {
    case JBC_STATION_SOLD:
    case JBC_STATION_HA:
    case JBC_STATION_PH: return JBC_CMD_DEVICE_NAME_STD;
    case JBC_STATION_SF:
    case JBC_STATION_FE: return JBC_CMD_DEVICE_NAME_SF_FE;
    case JBC_STATION_CL: return JBC_CMD_DEVICE_NAME_CL;
    default: return 0;
  }
}

static uint8_t jbc_station_name_write_command() {
  if (jbc_frame_protocol == JBC_PROTO_01)
    return jbc_station_kind == JBC_STATION_SOLD ? 0xB2 : 0;
  if (jbc_frame_protocol != JBC_PROTO_02) return 0;
  switch (jbc_station_kind) {
    case JBC_STATION_SOLD:
    case JBC_STATION_HA:
    case JBC_STATION_PH: return 0xB2;
    case JBC_STATION_SF:
    case JBC_STATION_FE: return 0x5C;
    case JBC_STATION_CL: return 0x55;
    default: return 0;
  }
}

static bool jbc_station_name_valid(const char* value, uint8_t len) {
  static const char allowed[] = " 0123456789QWERTYUIOPASDFGHJKLMNBVCXZ'!?$%&@-=,.;()[]";
  if (!value || len > 16U) return false;
  for (uint8_t i = 0; i < len; ++i) {
    const unsigned char raw = (unsigned char)value[i];
    if (raw == 0U || raw >= 0x80U || !strchr(allowed, toupper(raw))) return false;
  }
  return true;
}

static void jbc_station_name_write_finish() {
  jbc_station_name_write_value[0] = 0;
  jbc_station_name_write_queued = false;
  jbc_station_name_write_state = JBC_NAME_WRITE_IDLE;
  jbc_station_name_write_due_ms = 0;
  jbc_station_name_write_inflight = false;
  jbc_station_name_write_protocol = JBC_PROTO_UNKNOWN;
  jbc_station_name_write_command_inflight = 0;
  jbc_station_name_write_fid = 0;
  jbc_station_name_write_sent_ms = 0;
}

static bool jbc_station_name_send_tracked(uint8_t command, const uint8_t* data, uint8_t len) {
  if (!command || jbc_frame_protocol == JBC_PROTO_UNKNOWN) return false;
  uint8_t fid = 0;
  bool ok = false;
  if (jbc_frame_protocol == JBC_PROTO_01) {
    ok = jbc_send_frame(JBC_PROTO_01, jbc_host_addr, jbc_station_addr, 0,
                        command, data, len);
  } else {
    fid = next_fid();
    ok = jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr, fid,
                        command, data, len);
  }
  if (!ok) return false;
  jbc_station_name_write_inflight = true;
  jbc_station_name_write_protocol = jbc_frame_protocol;
  jbc_station_name_write_command_inflight = command;
  jbc_station_name_write_fid = fid;
  jbc_station_name_write_sent_ms = millis();
  return true;
}

static bool jbc_station_name_send_control_mode(bool control) {
  const JbcProtocol command_protocol = jbc_command_protocol == JBC_PROTO_UNKNOWN
                                        ? jbc_frame_protocol : jbc_command_protocol;
  if (command_protocol == JBC_PROTO_01) {
    const uint8_t value = control ? (uint8_t)'C' : (uint8_t)'M';
    return jbc_station_name_send_tracked(JBC_CMD_CONNECT_WRITE_P01, &value, 1);
  }
  if (command_protocol == JBC_PROTO_02) {
    const uint8_t value[2] = {(uint8_t)':', control ? (uint8_t)'C' : (uint8_t)'M'};
    return jbc_station_name_send_tracked(JBC_CMD_CONNECT_WRITE_P02_USB, value, 2);
  }
  return false;
}

static bool poll_jbc_station_name_write() {
  const uint32_t now = millis();

  if (jbc_station_name_write_inflight) {
    if ((uint32_t)(now - jbc_station_name_write_sent_ms) <= 3000UL) return true;
    jbc_clear_in_progress(jbc_station_name_write_protocol);
    jbc_station_name_write_inflight = false;
    // A failed CONTROL or name transaction must still be followed by :M. If
    // the :M response itself is lost, end the operation after the attempt so a
    // broken station cannot monopolize the local JBC scheduler indefinitely.
    if (jbc_station_name_write_state == JBC_NAME_WRITE_WAIT_MONITOR) {
      jbc_station_name_write_finish();
      next_station_name_poll_ms = now + 250UL;
      return false;
    }
    jbc_station_name_write_state = JBC_NAME_WRITE_LEAVE_CONTROL;
    jbc_station_name_write_due_ms = now + 20UL;
    return true;
  }

  if (jbc_station_name_write_state == JBC_NAME_WRITE_WAIT_VERIFY) {
    if ((int32_t)(now - jbc_station_name_write_due_ms) < 0) return true;
    jbc_clear_in_progress(jbc_frame_protocol);
    jbc_station_name_write_finish();
    next_station_name_poll_ms = now + 250UL;
    return false;
  }

  if (jbc_station_name_write_queued && jbc_station_name_write_state == JBC_NAME_WRITE_IDLE) {
    jbc_station_name_write_queued = false;
    jbc_station_name_write_state = JBC_NAME_WRITE_ENTER_CONTROL;
    jbc_station_name_write_due_ms = now;
  }
  if (jbc_station_name_write_state == JBC_NAME_WRITE_IDLE) return false;

  if (jbc_link_state != JBC_LINK_ACTIVE || !jbc_station_name_write_command()) {
    jbc_station_name_write_finish();
    return false;
  }
  if ((int32_t)(now - jbc_station_name_write_due_ms) < 0) return true;

  switch (jbc_station_name_write_state) {
    case JBC_NAME_WRITE_ENTER_CONTROL:
      if (jbc_station_name_send_control_mode(true)) {
        jbc_station_name_write_state = JBC_NAME_WRITE_WAIT_CONTROL;
      } else jbc_station_name_write_due_ms = now + 100UL;
      break;

    case JBC_NAME_WRITE_SEND_NAME: {
      const uint8_t command = jbc_station_name_write_command();
      const uint8_t len = (uint8_t)strlen(jbc_station_name_write_value);
      if (jbc_station_name_send_tracked(command,
            (const uint8_t*)jbc_station_name_write_value, len)) {
        jbc_station_name_write_state = JBC_NAME_WRITE_WAIT_NAME;
      } else jbc_station_name_write_due_ms = now + 100UL;
      break;
    }

    case JBC_NAME_WRITE_LEAVE_CONTROL:
      if (jbc_station_name_send_control_mode(false)) {
        jbc_station_name_write_state = JBC_NAME_WRITE_WAIT_MONITOR;
      } else jbc_station_name_write_due_ms = now + 100UL;
      break;

    case JBC_NAME_WRITE_VERIFY_READ:
      if (jbc_send_station_name()) {
        jbc_station_name_write_state = JBC_NAME_WRITE_WAIT_VERIFY;
        jbc_station_name_write_due_ms = now + 1500UL;
      } else jbc_station_name_write_due_ms = now + 100UL;
      break;

    default:
      break;
  }
  return true;
}

static bool jbc_send_station_name() {
  const uint8_t command = jbc_station_name_read_command();
  if (!command || jbc_link_state != JBC_LINK_ACTIVE) return false;
  if (jbc_frame_protocol == JBC_PROTO_01) {
    return jbc_send_frame(JBC_PROTO_01, jbc_host_addr, jbc_station_addr, 0, command, nullptr, 0);
  }
  return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr,
                        next_fid(), command, nullptr, 0);
}

static bool is_station_name_frame(const JbcFrame& f) {
  const uint8_t command = jbc_station_name_read_command();
  return command && f.command == command;
}

static void decode_station_name(const JbcFrame& f) {
  // Release the matching request slot. P02 still keys the slot by FID so
  // response context remains exact; the global transport gate permits only one
  // response-waiting request at a time.
  if (f.frame_protocol == JBC_PROTO_02) pending_by_fid[f.fid] = PendingRequest();
  else p01_pending = PendingRequest();
  jbc_initial_station_name_pending = false;
  const bool dll_name_15s = jbc_station_kind == JBC_STATION_SOLD || jbc_station_kind == JBC_STATION_HA ||
                            jbc_station_kind == JBC_STATION_PH || jbc_station_kind == JBC_STATION_SF;
  next_station_name_poll_ms = millis() + (dll_name_15s ? 15000UL : 60000UL);

  char next_name[17] = {0};
  const uint8_t n = f.len > 16U ? (uint8_t)16 : f.len;
  if (n) memcpy(next_name, f.data, n);
  // The DLL decodes UTF-8 directly. JBC station names are constrained to a
  // short ASCII-compatible set; trim only padding NUL/space bytes at the end.
  int end = (int)n;
  while (end > 0 && (next_name[end - 1] == '\0' || next_name[end - 1] == ' ')) --end;
  next_name[end] = 0;
  if (strcmp(jbc_station_name, next_name) != 0) {
    strncpy(jbc_station_name, next_name, sizeof(jbc_station_name) - 1);
    jbc_station_name[sizeof(jbc_station_name) - 1] = 0;
    mark_fast_changed();
  }
  if (jbc_station_name_write_state == JBC_NAME_WRITE_WAIT_VERIFY) {
    jbc_station_name_write_finish();
  }
}

static void jbc_config_write_finish() {
  jbc_config_write = JbcConfigWriteTransaction();
  jbc_config_write_queued = false;
  jbc_config_write_state = JBC_CONFIG_WRITE_IDLE;
  jbc_config_write_due_ms = 0;
  jbc_config_write_inflight = false;
  jbc_config_write_protocol = JBC_PROTO_UNKNOWN;
  jbc_config_write_command_inflight = 0;
  jbc_config_write_fid = 0;
  jbc_config_write_sent_ms = 0;
}

static bool jbc_config_send_tracked(uint8_t command, const uint8_t* data, uint8_t len) {
  if (!command || jbc_frame_protocol == JBC_PROTO_UNKNOWN) return false;
  uint8_t fid = 0;
  bool ok = false;
  if (jbc_frame_protocol == JBC_PROTO_01) {
    ok = jbc_send_frame(JBC_PROTO_01, jbc_host_addr, jbc_station_addr, 0,
                        command, data, len, jbc_config_write.port);
  } else {
    fid = next_fid();
    ok = jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr, fid,
                        command, data, len, jbc_config_write.port);
  }
  if (!ok) return false;
  jbc_config_write_inflight = true;
  jbc_config_write_protocol = jbc_frame_protocol;
  jbc_config_write_command_inflight = command;
  jbc_config_write_fid = fid;
  jbc_config_write_sent_ms = millis();
  return true;
}

static bool jbc_config_send_control_mode(bool control) {
  const JbcProtocol protocol = jbc_command_protocol == JBC_PROTO_UNKNOWN
                                 ? jbc_frame_protocol : jbc_command_protocol;
  if (protocol == JBC_PROTO_01) {
    const uint8_t value = control ? (uint8_t)'C' : (uint8_t)'M';
    return jbc_config_send_tracked(JBC_CMD_CONNECT_WRITE_P01, &value, 1);
  }
  if (protocol == JBC_PROTO_02) {
    const uint8_t value[2] = {(uint8_t)':', control ? (uint8_t)'C' : (uint8_t)'M'};
    return jbc_config_send_tracked(JBC_CMD_CONNECT_WRITE_P02_USB, value, 2);
  }
  return false;
}

static bool jbc_config_send_verify() {
  if (!jbc_config_write.verify_command) return false;
  const uint8_t fid = jbc_frame_protocol == JBC_PROTO_02 ? next_fid() : 0;
  return jbc_send_frame(jbc_frame_protocol, jbc_host_addr, jbc_station_addr, fid,
                        jbc_config_write.verify_command,
                        jbc_config_write.verify_len ? jbc_config_write.verify_data : nullptr,
                        jbc_config_write.verify_len, jbc_config_write.port);
}

static bool jbc_config_temp_in_range(uint16_t value) {
  uint16_t minimum = 0, maximum = 0;
  if (jbc_station_kind == JBC_STATION_SOLD) {
    minimum = jbc_sold_min_temp; maximum = jbc_sold_max_temp;
  } else if (jbc_station_kind == JBC_STATION_HA) {
    minimum = jbc_ha_min_temp; maximum = jbc_ha_max_temp;
  } else if (jbc_station_kind == JBC_STATION_PH) {
    minimum = jbc_ph_min_temp; maximum = jbc_ph_max_temp;
  }
  if (minimum && value < minimum) return false;
  if (maximum && value > maximum) return false;
  return value != 0xFFFFU;
}

static bool jbc_config_prepare(const uint8_t* payload, uint8_t len) {
  if (!payload || !len || jbc_link_state != JBC_LINK_ACTIVE) return false;
  JbcConfigWriteTransaction next;
  next.action = payload[0];

  if (next.action == JBC_USB_CONFIG_SELECTED_TEMP) {
    if (len != 4U) return false;
    next.port = payload[1];
    const uint16_t value = get_u16_le(payload + 2);
    if (!jbc_config_temp_in_range(value)) return false;
    if (jbc_station_kind == JBC_STATION_PH) {
      if (jbc_frame_protocol != JBC_PROTO_02 || next.port >= 4U) return false;
    } else if ((jbc_station_kind != JBC_STATION_SOLD && jbc_station_kind != JBC_STATION_HA) ||
               next.port >= poll_port_limit()) return false;
    next.command_count = 1;
    next.command[0] = 0x51;
    next.len[0] = 3;
    if (jbc_frame_protocol == JBC_PROTO_01) {
      next.data[0][0] = next.port;
      put_u16_le(next.data[0] + 1, value);
    } else {
      put_u16_le(next.data[0], value);
      next.data[0][2] = next.port;
    }
    next.verify_command = 0x50;
    next.verify_len = 1;
    next.verify_data[0] = next.port;
  } else if (next.action == JBC_USB_CONFIG_SELECTED_FLOW) {
    if (len != 4U || jbc_frame_protocol != JBC_PROTO_02) return false;
    next.port = payload[1];
    const uint16_t value = get_u16_le(payload + 2);
    uint16_t minimum = 0, maximum = 1000;
    if (jbc_station_kind == JBC_STATION_HA) {
      if (next.port >= poll_port_limit()) return false;
      minimum = jbc_ha_min_flow ? jbc_ha_min_flow : 100U;
      if (jbc_ha_max_flow) maximum = jbc_ha_max_flow;
      next.command[0] = 0x5A;
      next.len[0] = 3;
      put_u16_le(next.data[0], value);
      next.data[0][2] = next.port;
      next.verify_command = JBC_CMD_SELECT_FLOW_HA;
      next.verify_len = 1;
      next.verify_data[0] = next.port;
    } else if (jbc_station_kind == JBC_STATION_FE) {
      minimum = 200U;
      if (value < minimum || value > maximum) return false;
      next.command[0] = 0x35;
      next.len[0] = 2;
      // CStation_FE maps the public 200..1000 flow range to the station's
      // native 450..580 value before sending WriteSelectFlow(0x35).
      const uint16_t raw_value = (uint16_t)(450U +
        ((uint32_t)(value - minimum) * 130U) / (1000U - minimum));
      put_u16_le(next.data[0], raw_value);
      next.verify_command = JBC_CMD_SELECT_FLOW_FE;
    } else return false;
    if (value < minimum || value > maximum) return false;
    next.command_count = 1;
  } else if (next.action == JBC_USB_CONFIG_LEVELS) {
    if (len != 23U || (jbc_station_kind != JBC_STATION_SOLD &&
                       jbc_station_kind != JBC_STATION_HA)) return false;
    // CFeaturesDataInitializer disables TempLevels for JTSE/CAP v1+.
    // Keep the transport guard in sync with the web UI so unsupported
    // stations can never receive a level-write frame.
    if (jbc_station_kind == JBC_STATION_HA && !ha_supports_temp_levels()) return false;
    next.port = payload[1];
    if (next.port >= poll_port_limit()) return false;
    const uint8_t enabled = payload[2] ? 1U : 0U;
    const uint8_t selected = payload[3];
    const uint8_t mask = payload[4] & 0x07U;
    if (selected > 2U) return false;
    uint16_t temp[3], flow[3], ext[3];
    for (uint8_t i = 0; i < 3; ++i) temp[i] = get_u16_le(payload + 5 + i * 2U);
    for (uint8_t i = 0; i < 3; ++i) flow[i] = get_u16_le(payload + 11 + i * 2U);
    for (uint8_t i = 0; i < 3; ++i) ext[i] = get_u16_le(payload + 17 + i * 2U);
    for (uint8_t i = 0; i < 3; ++i) {
      if ((mask & (1U << i)) && !jbc_config_temp_in_range(temp[i])) return false;
    }
    const uint8_t tool = jbc_ports[next.port].tool;
    if (jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_01) {
      next.command_count = 4;
      next.command[0] = 0x34;
      next.len[0] = 3;
      next.data[0][0] = next.port; next.data[0][1] = tool;
      next.data[0][2] = enabled ? selected : 0xFFU;
      for (uint8_t i = 0; i < 3; ++i) {
        next.command[i + 1] = (uint8_t)(0x36U + i * 2U);
        next.len[i + 1] = 4;
        next.data[i + 1][0] = next.port; next.data[i + 1][1] = tool;
        put_u16_le(next.data[i + 1] + 2,
                   (mask & (1U << i)) ? temp[i] : 0xFFFFU);
      }
      next.verify_command = JBC_CMD_LEVELS_SOLD;
      next.verify_len = 2;
      next.verify_data[0] = next.port; next.verify_data[1] = tool;
    } else if (jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_02) {
      next.command_count = 1;
      next.command[0] = 0x34;
      next.len[0] = 13;
      next.data[0][0] = enabled;
      // Protocol 02 mirrors JBC_Connect TempLevelsData: disabling the level
      // feature changes only LevelsOnOff. Keep the selected level and all
      // configured temperatures so they are still available when re-enabled.
      next.data[0][1] = selected;
      for (uint8_t i = 0; i < 3; ++i) {
        const uint8_t o = (uint8_t)(2U + i * 3U);
        next.data[0][o] = (mask & (1U << i)) ? 1U : 0U;
        put_u16_le(next.data[0] + o + 1, temp[i]);
      }
      next.data[0][11] = next.port; next.data[0][12] = tool;
      next.verify_command = JBC_CMD_LEVELS_SOLD;
      next.verify_len = 2;
      next.verify_data[0] = next.port; next.verify_data[1] = tool;
    } else if (jbc_station_kind == JBC_STATION_HA && jbc_frame_protocol == JBC_PROTO_02) {
      const uint16_t min_flow = jbc_ha_min_flow ? jbc_ha_min_flow : 100U;
      const uint16_t max_flow = jbc_ha_max_flow ? jbc_ha_max_flow : 1000U;
      for (uint8_t i = 0; i < 3; ++i) {
        if ((mask & (1U << i)) && (flow[i] < min_flow || flow[i] > max_flow)) return false;
      }
      next.command_count = 1;
      next.command[0] = 0x41;
      next.len[0] = 25;
      next.data[0][0] = enabled;
      next.data[0][1] = selected;
      for (uint8_t i = 0; i < 3; ++i) {
        const uint8_t o = (uint8_t)(2U + i * 7U);
        next.data[0][o] = (mask & (1U << i)) ? 1U : 0U;
        put_u16_le(next.data[0] + o + 1, temp[i]);
        put_u16_le(next.data[0] + o + 3, flow[i]);
        put_u16_le(next.data[0] + o + 5, ext[i]);
      }
      next.data[0][23] = next.port; next.data[0][24] = tool;
      next.verify_command = JBC_CMD_LEVELS_HA;
      next.verify_len = 2;
      next.verify_data[0] = next.port; next.verify_data[1] = tool;
    } else return false;
  } else return false;

  jbc_config_write = next;
  jbc_config_write_queued = true;
  return true;
}

static bool poll_jbc_config_write() {
  const uint32_t now = millis();
  if (jbc_config_write_inflight) {
    if ((uint32_t)(now - jbc_config_write_sent_ms) <= 3000UL) return true;
    jbc_clear_in_progress(jbc_config_write_protocol);
    jbc_config_write_inflight = false;
    if (jbc_config_write_state == JBC_CONFIG_WRITE_WAIT_MONITOR) {
      jbc_config_write_finish();
      return false;
    }
    jbc_config_write_state = JBC_CONFIG_WRITE_LEAVE_CONTROL;
    jbc_config_write_due_ms = now + 20UL;
    return true;
  }
  if (jbc_config_write_state == JBC_CONFIG_WRITE_WAIT_VERIFY) {
    if ((int32_t)(now - jbc_config_write_due_ms) < 0) return true;
    jbc_clear_in_progress(jbc_frame_protocol);
    jbc_config_write_finish();
    return false;
  }
  if (jbc_config_write_queued && jbc_config_write_state == JBC_CONFIG_WRITE_IDLE) {
    jbc_config_write_queued = false;
    jbc_config_write_state = JBC_CONFIG_WRITE_ENTER_CONTROL;
    jbc_config_write_due_ms = now;
  }
  if (jbc_config_write_state == JBC_CONFIG_WRITE_IDLE) return false;
  if (jbc_link_state != JBC_LINK_ACTIVE) {
    jbc_config_write_finish();
    return false;
  }
  if ((int32_t)(now - jbc_config_write_due_ms) < 0) return true;
  switch (jbc_config_write_state) {
    case JBC_CONFIG_WRITE_ENTER_CONTROL:
      if (jbc_config_send_control_mode(true))
        jbc_config_write_state = JBC_CONFIG_WRITE_WAIT_CONTROL;
      else jbc_config_write_due_ms = now + 100UL;
      break;
    case JBC_CONFIG_WRITE_SEND_VALUE: {
      const uint8_t i = jbc_config_write.command_index;
      if (i >= jbc_config_write.command_count) {
        jbc_config_write_state = JBC_CONFIG_WRITE_LEAVE_CONTROL;
        break;
      }
      if (jbc_config_send_tracked(jbc_config_write.command[i],
                                  jbc_config_write.data[i], jbc_config_write.len[i]))
        jbc_config_write_state = JBC_CONFIG_WRITE_WAIT_VALUE;
      else jbc_config_write_due_ms = now + 100UL;
      break;
    }
    case JBC_CONFIG_WRITE_LEAVE_CONTROL:
      if (jbc_config_send_control_mode(false))
        jbc_config_write_state = JBC_CONFIG_WRITE_WAIT_MONITOR;
      else jbc_config_write_due_ms = now + 100UL;
      break;
    case JBC_CONFIG_WRITE_VERIFY_READ:
      if (jbc_config_send_verify()) {
        jbc_config_write_state = JBC_CONFIG_WRITE_WAIT_VERIFY;
        jbc_config_write_due_ms = now + 1500UL;
      } else jbc_config_write_due_ms = now + 100UL;
      break;
    default:
      break;
  }
  return true;
}

static bool is_device_uid_frame(const JbcFrame& f) {
  const JbcProtocol command_protocol = jbc_command_protocol == JBC_PROTO_UNKNOWN ? f.frame_protocol : jbc_command_protocol;
  return (command_protocol == JBC_PROTO_01 && f.command == JBC_CMD_DEVICE_UID_READ_P01) ||
         (command_protocol == JBC_PROTO_02 && f.command == JBC_CMD_DEVICE_UID_READ_P02);
}

// Mirrors Station.IsValidUUID() from JBC_Connect: non-empty and more than three
// distinct characters. Applying it to the station bytes also accepts the DLL's
// GUIDS (32 ASCII), GUIDB (16 binary), MAC (20 ASCII) and CUSTOM forms while
// rejecting empty/zero-filled placeholder IDs.
static bool jbc_device_uid_is_valid(const uint8_t* data, uint8_t len) {
  if (!data || !len) return false;
  uint8_t distinct[4] = {0};
  uint8_t distinct_count = 0;
  for (uint8_t i = 0; i < len; ++i) {
    bool seen = false;
    for (uint8_t j = 0; j < distinct_count; ++j) {
      if (distinct[j] == data[i]) { seen = true; break; }
    }
    if (!seen) {
      if (distinct_count < 4) distinct[distinct_count] = data[i];
      ++distinct_count;
      if (distinct_count > 3) return true;
    }
  }
  return false;
}

static void jbc_generate_uid_p02() {
  // JBC_Connect ClsStationUID.NewGUIDS(): Guid.NewGuid().ToString().Replace("-", "")
  // -> 32 lowercase ASCII hex chars. Generate an RFC-4122 v4 value and store the
  // same compact text representation in the station.
  uint8_t raw[16];
  for (uint8_t i = 0; i < sizeof(raw); i += 4) {
    const uint32_t r = esp_random();
    raw[i + 0] = (uint8_t)r;
    raw[i + 1] = (uint8_t)(r >> 8);
    raw[i + 2] = (uint8_t)(r >> 16);
    raw[i + 3] = (uint8_t)(r >> 24);
  }
  raw[6] = (uint8_t)((raw[6] & 0x0F) | 0x40); // version 4
  raw[8] = (uint8_t)((raw[8] & 0x3F) | 0x80); // RFC-4122 variant
  static const char hex[] = "0123456789abcdef";
  for (uint8_t i = 0; i < sizeof(raw); ++i) {
    jbc_uid_generated[i * 2] = (uint8_t)hex[(raw[i] >> 4) & 0x0F];
    jbc_uid_generated[i * 2 + 1] = (uint8_t)hex[raw[i] & 0x0F];
  }
  jbc_uid_generated_len = 32;
}

static void jbc_generate_uid_p01() {
  // JBC_Connect ClsStationUID.NewMAC(iSequence): 12-char MAC + an 8-digit
  // sequence. We use the ESP32-S3's unique 48-bit eFuse/MAC identity and a
  // non-zero random sequence, preserving the original 20-byte MAC-form layout.
  const uint64_t mac = esp_uid64() & 0x0000FFFFFFFFFFFFULL;
  char mac_text[13];
  snprintf(mac_text, sizeof(mac_text), "%012llX", (unsigned long long)mac);
  uint32_t seq = (esp_random() % 99999999UL) + 1UL;
  char seq_text[9];
  snprintf(seq_text, sizeof(seq_text), "%08lu", (unsigned long)seq);
  memcpy(jbc_uid_generated, mac_text, 12);
  memcpy(jbc_uid_generated + 12, seq_text, 8);
  jbc_uid_generated_len = 20;
}

static bool jbc_prepare_generated_uid() {
  // Keep one candidate for the whole logical station session. If a station is
  // still booting and rejects the first write, retries must verify/write the
  // same identity instead of generating a different UUID on every pass.
  if (jbc_device_uid_is_valid(jbc_uid_generated, jbc_uid_generated_len)) return true;
  memset(jbc_uid_generated, 0, sizeof(jbc_uid_generated));
  jbc_uid_generated_len = 0;
  const JbcProtocol command_protocol = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
  if (command_protocol == JBC_PROTO_02) {
    jbc_generate_uid_p02();
    return true;
  }
  // The original DLL only supports Protocol 01 Device-ID writes for SOLD.
  if (command_protocol == JBC_PROTO_01 && jbc_station_kind == JBC_STATION_SOLD) {
    jbc_generate_uid_p01();
    return true;
  }
  return false;
}

static bool jbc_send_uid_control_mode(bool control) {
  const JbcProtocol command_protocol = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
  if (command_protocol == JBC_PROTO_01) {
    const uint8_t value = control ? (uint8_t)'C' : (uint8_t)'M';
    return jbc_send_command_for_uid(JBC_CMD_CONNECT_WRITE_P01, &value, 1);
  }
  if (command_protocol == JBC_PROTO_02) {
    const uint8_t value[2] = {(uint8_t)':', control ? (uint8_t)'C' : (uint8_t)'M'};
    return jbc_send_command_for_uid(JBC_CMD_CONNECT_WRITE_P02_USB, value, 2);
  }
  return false;
}

static void jbc_begin_uid_provisioning() {
  if (jbc_uid_provision_state != JBC_UID_PROVISION_IDLE || !jbc_prepare_generated_uid()) return;
  jbc_uid_auto_created = false;
  jbc_uid_provision_verify_tries = 0;
  jbc_uid_provision_state = JBC_UID_PROVISION_ENTER_CONTROL;
  jbc_uid_provision_due_ms = millis() + 20UL;
}

static void decode_device_uid(const JbcFrame& f) {
  const uint8_t n = min(f.len, (uint8_t)sizeof(jbc_device_uid));
  memset(jbc_device_uid, 0, sizeof(jbc_device_uid));
  if (n) memcpy(jbc_device_uid, f.data, n);
  jbc_device_uid_len = n;
  const bool valid = jbc_device_uid_is_valid(jbc_device_uid, jbc_device_uid_len);

  if (f.frame_protocol == JBC_PROTO_02) pending_by_fid[f.fid] = PendingRequest();
  else p01_pending = PendingRequest();

  if (valid) {
    if (jbc_uid_provision_state == JBC_UID_PROVISION_WAIT_VERIFY) {
      const bool matches = jbc_device_uid_len == jbc_uid_generated_len &&
                           !memcmp(jbc_device_uid, jbc_uid_generated, jbc_device_uid_len);
      jbc_uid_auto_created = matches;
      jbc_uid_provision_state = JBC_UID_PROVISION_LEAVE_CONTROL;
      jbc_uid_provision_due_ms = millis() + 20UL;
    } else if (jbc_uid_provision_state == JBC_UID_PROVISION_IDLE) {
      jbc_uid_provision_state = JBC_UID_PROVISION_DONE;
      jbc_uid_provision_due_ms = 0;
    }
  } else if (jbc_uid_provision_state == JBC_UID_PROVISION_IDLE) {
    // Important safety rule: only an actual invalid reply enters this path.
    // During the station's power-on grace period keep reading, but do not write
    // yet: JTSE can answer before its volatile Device-ID store is ready.
    const uint32_t now = millis();
    if ((int32_t)(now - jbc_uid_write_not_before_ms) >= 0) {
      jbc_begin_uid_provisioning();
    } else {
      const uint32_t retry = now + JBC_UID_RETRY_READ_MS;
      next_uid_poll_ms = (int32_t)(retry - jbc_uid_write_not_before_ms) > 0
                           ? jbc_uid_write_not_before_ms : retry;
    }
  }
  mark_fast_changed();
}

static void poll_jbc_uid_provisioning() {
  if (jbc_uid_provision_state == JBC_UID_PROVISION_IDLE ||
      jbc_uid_provision_state == JBC_UID_PROVISION_DONE) return;
  const uint32_t now = millis();
  if ((int32_t)(now - jbc_uid_provision_due_ms) < 0) return;

  switch (jbc_uid_provision_state) {
    case JBC_UID_PROVISION_ENTER_CONTROL:
      if (jbc_send_uid_control_mode(true)) {
        jbc_uid_provision_state = JBC_UID_PROVISION_WRITE;
        jbc_uid_provision_due_ms = now + 180UL;
      } else jbc_uid_provision_due_ms = now + 250UL;
      break;

    case JBC_UID_PROVISION_WRITE: {
      const uint8_t command = jbc_device_uid_write_command();
      if (command && jbc_uid_generated_len &&
          jbc_send_command_for_uid(command, jbc_uid_generated, jbc_uid_generated_len)) {
        jbc_uid_provision_state = JBC_UID_PROVISION_VERIFY_READ;
        jbc_uid_provision_due_ms = now + 220UL;
      } else jbc_uid_provision_due_ms = now + 250UL;
      break;
    }

    case JBC_UID_PROVISION_VERIFY_READ: {
      const uint8_t command = jbc_device_uid_read_command();
      if (command && jbc_send_command_for_uid(command, nullptr, 0)) {
        ++jbc_uid_provision_verify_tries;
        jbc_uid_provision_state = JBC_UID_PROVISION_WAIT_VERIFY;
        jbc_uid_provision_due_ms = now + 1200UL;
      } else jbc_uid_provision_due_ms = now + 250UL;
      break;
    }

    case JBC_UID_PROVISION_WAIT_VERIFY:
      if (jbc_uid_provision_verify_tries < 3) {
        jbc_uid_provision_state = JBC_UID_PROVISION_VERIFY_READ;
        jbc_uid_provision_due_ms = now;
      } else {
        jbc_uid_provision_state = JBC_UID_PROVISION_LEAVE_CONTROL;
        jbc_uid_provision_due_ms = now;
      }
      break;

    case JBC_UID_PROVISION_LEAVE_CONTROL:
      // Always attempt to return to MONITOR, even when verification failed. A
      // failed write/read-back is not terminal: after a short cooldown perform
      // a fresh ReadDeviceUID. Only another actual invalid reply may start the
      // next write attempt. This lets slow-starting volatile-ID stations recover
      // without ever turning a timeout into permission to overwrite an ID.
      if (jbc_send_uid_control_mode(false)) {
        if (jbc_device_uid_is_valid(jbc_device_uid, jbc_device_uid_len)) {
          jbc_uid_provision_state = JBC_UID_PROVISION_DONE;
          jbc_uid_provision_due_ms = 0;
        } else {
          jbc_uid_provision_state = JBC_UID_PROVISION_FAILED;
          jbc_uid_provision_due_ms = now + JBC_UID_REPROVISION_DELAY_MS;
        }
        mark_fast_changed();
      } else jbc_uid_provision_due_ms = now + 250UL;
      break;

    case JBC_UID_PROVISION_FAILED:
      // Retry indefinitely while the JBC session remains alive. The next step
      // is always a read; a write is still impossible without an invalid reply.
      jbc_uid_provision_state = JBC_UID_PROVISION_IDLE;
      jbc_uid_provision_due_ms = 0;
      next_uid_poll_ms = now;
      break;

    default:
      break;
  }
}

static bool jbc_send_info_port(uint8_t port) {
  const uint8_t data[1] = {port};
  if (jbc_frame_protocol == JBC_PROTO_01) {
    // Protocol 01 has no FID, so only one request may be outstanding. The
    // common QueueMessages-style 500 ms retry layer owns timeout/recovery.
    return jbc_send_frame(JBC_PROTO_01, jbc_host_addr, jbc_station_addr,
                          0, JBC_CMD_INFO_PORT, data, 1, port);
  }
  if (jbc_frame_protocol == JBC_PROTO_02) {
    const uint8_t fid = next_fid();
    if (jbc_station_kind == JBC_STATION_CL) {
      // SendFrame02_CL.ReadCleanerMode() uses new byte[0] and maps to port 0.
      return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr,
                            fid, JBC_CMD_INFO_PORT, nullptr, 0, 0);
    }
    if (jbc_station_kind == JBC_STATION_FE) {
      // SendFrame02_FE.ReadSuctionLevel() uses new byte[0]. The result is
      // station-wide and ReceiveFrame02_FE applies it to every FE port.
      return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr,
                            fid, JBC_CMD_INFO_PORT, nullptr, 0, 0xFF);
    }
    if (jbc_station_kind == JBC_STATION_SF) {
      // SendFrame02_SF.ReadDispenserMode() also uses new byte[0]; SF has one
      // port and ReceiveFrame02_SF maps the response to port 0.
      return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr,
                            fid, JBC_CMD_INFO_PORT, nullptr, 0, 0);
    }
    return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr,
                          fid, JBC_CMD_INFO_PORT, data, 1, port);
  }
  return false;
}

static bool jbc_send_delay_time(uint8_t port) {
  const JbcProtocol cp = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
  if (jbc_station_kind != JBC_STATION_SOLD || jbc_frame_protocol != JBC_PROTO_02 || cp != JBC_PROTO_02) return false;
  const uint8_t data[1] = {port};
  const uint8_t fid = next_fid();
  return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr,
                        fid, JBC_CMD_DELAY_TIME_P02_SOLD, data, 1, port);
}

static bool jbc_send_sold_delay_setting(uint8_t port, uint8_t command) {
  if (jbc_station_kind != JBC_STATION_SOLD || port >= JBC_MAX_PORTS || !jbc_ports[port].valid || !jbc_ports[port].tool) return false;
  if (command != JBC_CMD_SLEEP_DELAY_SOLD && command != JBC_CMD_HIBER_DELAY_SOLD) return false;
  const uint8_t data[2] = {port, jbc_ports[port].tool};
  const JbcProtocol cp = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
  if (jbc_frame_protocol == JBC_PROTO_01 && cp == JBC_PROTO_01) {
    return jbc_send_frame(JBC_PROTO_01, jbc_host_addr, jbc_station_addr, 0, command, data, 2, port);
  }
  if (jbc_frame_protocol == JBC_PROTO_02 && cp == JBC_PROTO_02) {
    const uint8_t fid = next_fid();
    return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr, fid, command, data, 2, port);
  }
  return false;
}

static bool sold_k26_protocol() {
  if (jbc_station_kind != JBC_STATION_SOLD || jbc_frame_protocol != JBC_PROTO_02) return false;
  if (!strcmp(jbc_model, "ALE")) return true;
  if ((!strcmp(jbc_model, "CDE") || !strcmp(jbc_model, "CFE")) && jbc_model_version >= 7) return true;
  if (!strcmp(jbc_model, "DDE") && !strcmp(jbc_model_type, "CAP26") && jbc_model_version >= 7) return true;
  return false;
}

static bool sold_supports_cartridges() {
  // CFeaturesDataInitializer: only DME/TCH and PSE expose Features.Cartridges.
  if (jbc_station_kind != JBC_STATION_SOLD) return false;
  if (!strcmp(jbc_model, "DME") && !strcmp(jbc_model_type, "TCH")) return true;
  if (!strcmp(jbc_model, "PSE")) return true;
  return false;
}

static bool sold_supports_qst() {
  // CFeaturesDataInitializer.SetModel() has different feature tables for
  // command Protocol 01 and 02. Do not infer P02 QST support from a legacy P01
  // model rule (or vice versa).
  if (jbc_station_kind != JBC_STATION_SOLD) return false;
  const JbcProtocol cp = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
  if (cp == JBC_PROTO_01) {
    if (!strcmp(jbc_model, "CA")) return jbc_model_version >= 1;
    if (!strcmp(jbc_model, "CDCF") || !strcmp(jbc_model, "CDN") || !strcmp(jbc_model, "CP") ||
        !strcmp(jbc_model, "CSCV") || !strcmp(jbc_model, "DD") || !strcmp(jbc_model, "DI") ||
        !strcmp(jbc_model, "DM") || !strcmp(jbc_model, "HD") || !strcmp(jbc_model, "NA"))
      return jbc_model_version >= 1;
    return false;
  }
  if (cp == JBC_PROTO_02) {
    if (!strcmp(jbc_model, "CDE") || !strcmp(jbc_model, "CFE")) return true;
    if (!strcmp(jbc_model, "HDE")) return jbc_model_version >= 3;
    if (!strcmp(jbc_model, "DDE")) {
      if (!strcmp(jbc_model_type, "CAP")) return jbc_model_version >= 3;
      if (!strcmp(jbc_model_type, "CAP26")) return jbc_model_version >= 5;
      return false;
    }
    if (!strcmp(jbc_model, "DME")) return !strcmp(jbc_model_type, "TCH") && jbc_model_version >= 4;
    if (!strcmp(jbc_model, "NAE")) return !strcmp(jbc_model_type, "CAP") && jbc_model_version >= 3;
    if (!strcmp(jbc_model, "PSE")) return jbc_model_version >= 4;
    return false;
  }
  return false;
}

static bool sold_supports_p01_partial_counters() {
  if (jbc_station_kind != JBC_STATION_SOLD || jbc_frame_protocol != JBC_PROTO_01 || jbc_model_version < 2) return false;
  return !strcmp(jbc_model, "CDN") || !strcmp(jbc_model, "NA") || !strcmp(jbc_model, "DD") ||
         !strcmp(jbc_model, "DI") || !strcmp(jbc_model, "DM") || !strcmp(jbc_model, "CP") ||
         !strcmp(jbc_model, "HD") || !strcmp(jbc_model, "CSCV") || !strcmp(jbc_model, "CD/CF") ||
         !strcmp(jbc_model, "CD_CF") || !strcmp(jbc_model, "CDCF");
}

static bool sold_supports_partial_counters() {
  if (jbc_station_kind != JBC_STATION_SOLD) return false;
  if (jbc_frame_protocol == JBC_PROTO_02) return true; // protocol-02 default
  return sold_supports_p01_partial_counters();
}

static bool sold_supports_robot() {
  // Protocol-02 defaults Robot=true; SM explicitly disables it.
  return jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_02 && strcmp(jbc_model, "SM") != 0;
}

static bool sold_supports_peripherals() {
  if (jbc_station_kind != JBC_STATION_SOLD || jbc_frame_protocol != JBC_PROTO_02) return false;
  if (!strcmp(jbc_model, "DDE")) return !strcmp(jbc_model_type, "CAP") || !strcmp(jbc_model_type, "CAP26");
  if (!strcmp(jbc_model, "HDE") || !strcmp(jbc_model, "ALE") || !strcmp(jbc_model, "PSE")) return true;
  return !strcmp(jbc_model, "DME") && !strcmp(jbc_model_type, "TCH");
}

static bool sold_supports_profiles() {
  return jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_02 &&
         !strcmp(jbc_model, "DDE") && !strcmp(jbc_model_type, "CAP26") && jbc_model_version >= 6;
}

static bool sold_supports_assistant() {
  if (jbc_station_kind != JBC_STATION_SOLD || jbc_frame_protocol != JBC_PROTO_02) return false;
  if (!strcmp(jbc_model, "ALE")) return true;
  if ((!strcmp(jbc_model, "CDE") || !strcmp(jbc_model, "CFE")) && jbc_model_version >= 7) return true;
  return !strcmp(jbc_model, "DDE") && !strcmp(jbc_model_type, "CAP26") && jbc_model_version >= 7;
}

static bool sold_supports_excellence_289() {
  if (jbc_station_kind != JBC_STATION_SOLD || jbc_frame_protocol != JBC_PROTO_02) return false;
  if (!strcmp(jbc_model, "CDE") || !strcmp(jbc_model, "HDE") || !strcmp(jbc_model, "DDE") ||
      !strcmp(jbc_model, "DME") || !strcmp(jbc_model, "NAE") || !strcmp(jbc_model, "PSE")) return true;
  return false;
}

static bool sold_supports_ground_type() {
  if (jbc_station_kind != JBC_STATION_SOLD || jbc_frame_protocol != JBC_PROTO_02) return false;
  return !strcmp(jbc_model, "CPE") || !strcmp(jbc_model, "CDE") || !strcmp(jbc_model, "CFE") ||
         !strcmp(jbc_model, "CSVE");
}

static bool sold_supports_ethernet() {
  if (jbc_station_kind != JBC_STATION_SOLD || jbc_frame_protocol != JBC_PROTO_02) return false;
  return (!strcmp(jbc_model, "DME") && !strcmp(jbc_model_type, "TCH")) || !strcmp(jbc_model, "PSE");
}

static bool sold_supports_ale_feeder() {
  // CStation_SOLD exposes GetPortToolFeederInfo/GetToolStatusTinFeederProg only for ALE.
  return jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_02 &&
         !strcmp(jbc_model, "ALE");
}

static bool jbc_send_sold_qst(uint8_t command) {
  if (!sold_supports_qst()) return false;
  const JbcProtocol cp = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
  if (jbc_frame_protocol == JBC_PROTO_02 && cp == JBC_PROTO_02) {
    if (command != JBC_CMD_QST_ACTIVATE_P02 && command != JBC_CMD_QST_STATUS_P02) return false;
    const uint8_t fid = next_fid();
    return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr, fid, command, nullptr, 0, 0xFF);
  }
  if (jbc_frame_protocol == JBC_PROTO_01 && cp == JBC_PROTO_01) {
    if (command != JBC_CMD_QST_ACTIVATE_P01 && command != JBC_CMD_QST_STATUS_P01) return false;
    return jbc_send_frame(JBC_PROTO_01, jbc_host_addr, jbc_station_addr, 0, command, nullptr, 0, 0xFF);
  }
  return false;
}

static bool is_sold_qst_frame(const JbcFrame& f) {
  if (jbc_station_kind != JBC_STATION_SOLD) return false;
  // Command bytes 0xD0/0xD2 are overloaded by JBC: on Protocol 01 they
  // are QST Activate/Status, while on Protocol 02 they are the k20
  // partial Plug/Work counters (5-byte replies). Disambiguate by the
  // protocol carried by the received frame so valid P02 counter replies
  // are not misclassified as 1-byte P01 QST responses.
  if (f.frame_protocol == JBC_PROTO_02)
    return f.command == JBC_CMD_QST_ACTIVATE_P02 || f.command == JBC_CMD_QST_STATUS_P02;
  if (f.frame_protocol == JBC_PROTO_01)
    return f.command == JBC_CMD_QST_ACTIVATE_P01 || f.command == JBC_CMD_QST_STATUS_P01;
  return false;
}

static bool jbc_send_sold_lock_port(uint8_t port) {
  if (!sold_supports_qst() || port >= JBC_MAX_PORTS) return false;
  const JbcProtocol cp = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
  const uint8_t data[1] = {port};
  if (jbc_frame_protocol == JBC_PROTO_02 && cp == JBC_PROTO_02) {
    return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr, next_fid(),
                          JBC_CMD_LOCK_PORT_P02, data, 1, port);
  }
  if (jbc_frame_protocol == JBC_PROTO_01 && cp == JBC_PROTO_01) {
    return jbc_send_frame(JBC_PROTO_01, jbc_host_addr, jbc_station_addr, 0,
                          JBC_CMD_LOCK_PORT_P01, data, 1, port);
  }
  return false;
}

static bool is_sold_lock_port_frame(const JbcFrame& f) {
  if (jbc_station_kind != JBC_STATION_SOLD) return false;
  return (f.frame_protocol == JBC_PROTO_02 && f.command == JBC_CMD_LOCK_PORT_P02) ||
         (f.frame_protocol == JBC_PROTO_01 && f.command == JBC_CMD_LOCK_PORT_P01);
}

static void decode_sold_lock_port(const JbcFrame& f) {
  const uint8_t port = pending_port_for_frame(f);
  if (port >= JBC_MAX_PORTS || f.len != 2) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }

  bool enabled = false;
  if (f.frame_protocol == JBC_PROTO_02) {
    // ReceiveFrame02_SOLD case 0x88: raw 0 means EnabledPort=ON, non-zero OFF.
    enabled = f.data[0] == 0;
  } else {
    // ReceiveFrame01_SOLD case 0xD4 ignores the response byte and derives the
    // value from the station-level QSTActivate/QSTStatus settings. Mirror that
    // odd legacy behavior only after both settings are known.
    if ((jbc_qst_valid_flags & 0x03U) != 0x03U) return;
    enabled = (jbc_qst_state_flags & 0x03U) != 0x03U;
  }

  JbcPortState& ps = jbc_ports[port];
  const uint16_t old = ps.detail_value_flags;
  ps.detail_value_flags |= JBC_SOLD_DETAIL_ENABLED_PORT_VALID;
  if (enabled) ps.detail_value_flags |= JBC_SOLD_DETAIL_ENABLED_PORT_ON;
  else ps.detail_value_flags &= (uint16_t)~JBC_SOLD_DETAIL_ENABLED_PORT_ON;
  if (old != ps.detail_value_flags) mark_fast_changed();
}

static bool jbc_send_conti_read() {
  if ((jbc_station_kind != JBC_STATION_SOLD && jbc_station_kind != JBC_STATION_HA &&
       jbc_station_kind != JBC_STATION_PH && jbc_station_kind != JBC_STATION_SF) ||
      jbc_link_state != JBC_LINK_ACTIVE) return false;
  const JbcProtocol cp = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
  if (jbc_frame_protocol == JBC_PROTO_02 && cp == JBC_PROTO_02)
    return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr, next_fid(),
                          JBC_CMD_CONTI_READ, nullptr, 0, 0xFF);
  if (jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_01 && cp == JBC_PROTO_01) {
    return jbc_send_frame(JBC_PROTO_01, jbc_host_addr, jbc_station_addr, 0,
                          JBC_CMD_CONTI_READ, nullptr, 0, 0xFF);
  }
  return false;
}

static bool jbc_send_conti_write(uint8_t speed, uint8_t ports) {
  if ((jbc_station_kind != JBC_STATION_SOLD && jbc_station_kind != JBC_STATION_HA) ||
      jbc_link_state != JBC_LINK_ACTIVE) return false;
  const JbcProtocol cp = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
  uint8_t data[3] = {speed, ports, 1};
  uint8_t len = 2;
  // JBC_Connect uses WriteContiModeExtended only when starting ALE on P02.
  if (jbc_frame_protocol == JBC_PROTO_02 && cp == JBC_PROTO_02 &&
      speed != JBC_CONTI_SPEED_OFF && !strcmp(jbc_model, "ALE")) len = 3;
  if (jbc_frame_protocol == JBC_PROTO_02 && cp == JBC_PROTO_02)
    return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr, next_fid(),
                          JBC_CMD_CONTI_WRITE, data, len, 0xFF);
  if (jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_01 && cp == JBC_PROTO_01) {
    return jbc_send_frame(JBC_PROTO_01, jbc_host_addr, jbc_station_addr, 0,
                          JBC_CMD_CONTI_WRITE, data, 2, 0xFF);
  }
  return false;
}

static void decode_conti_read(const JbcFrame& f) {
  // HA/P02 requires exactly the original 2-byte reply. SOLD P01 also requires
  // 2 bytes, while newer SOLD P02 firmware may return a 3-byte variant.
  // Byte0=speed, byte1=enabled ports.
  const bool strict_p02_two = jbc_station_kind == JBC_STATION_HA ||
    jbc_station_kind == JBC_STATION_PH || jbc_station_kind == JBC_STATION_SF;
  const bool length_ok = strict_p02_two
    ? (f.frame_protocol == JBC_PROTO_02 && f.len == 2)
    : (f.frame_protocol == JBC_PROTO_01 ? f.len == 2 : (f.len == 2 || f.len == 3));
  if (!length_ok) { jbc_note_decode_error(f); return; }
  if (f.frame_protocol == JBC_PROTO_02) pending_by_fid[f.fid] = PendingRequest();
  else p01_pending = PendingRequest();
  jbc_continuous_speed = f.data[0];
  jbc_continuous_ports = f.data[1];
  jbc_continuous_valid = true;
  if (jbc_station_kind == JBC_STATION_SOLD && jbc_continuous_speed == JBC_CONTI_SPEED_OFF) {
    // Once OFF is confirmed, do not let the old 750 ms Conti freshness window
    // mask fresh M_INF_PORT data. The A/B test is immediately InfoPort-only.
    for (uint8_t port = 0; port < JBC_MAX_PORTS; ++port) {
      jbc_ports[port].sold_conti_valid = false;
      jbc_ports[port].sold_conti_last_ms = 0;
    }
  }
}

static void decode_conti_info(const JbcFrame& f) {
  // M_I_CONTIMODE (0x82) is JBC's high-rate telemetry stream, not a request
  // for continuous fume extraction. Never map it to FAST_FLAG_CONTINUOUS.
  ++jbc_continuous_frames;
  // OFF is authoritative. Late/in-flight 0x82 frames after StopContinuousMode
  // must not refresh SOLD live state during the A/B stability test.
  if (!jbc_continuous_valid || jbc_continuous_speed == JBC_CONTI_SPEED_OFF) return;

  if (jbc_station_kind == JBC_STATION_HA) {
    // ReceiveFrame02_HA M_I_CONTIMODE: sequence + one 14-byte block for each
    // enabled port. Keep M_INF_PORT running in parallel; continuous mode is
    // only the high-rate source for values present in both frames.
    if (f.frame_protocol != JBC_PROTO_02) return;
    uint8_t count = 0;
    for (uint8_t p = 0; p < JBC_MAX_PORTS; ++p) if (jbc_continuous_ports & (1U << p)) ++count;
    if (!count || f.len < (uint8_t)(1U + count * 14U)) return;

    const uint32_t now = millis();
    bool changed = false;
    uint8_t slot = 0;
    for (uint8_t port = 0; port < JBC_MAX_PORTS; ++port) {
      if (!(jbc_continuous_ports & (1U << port))) continue;
      const uint8_t base = (uint8_t)(1U + slot * 14U);
      ++slot;
      JbcPortState& ps = jbc_ports[port];
      const uint16_t temp = get_u16_le(f.data + base);
      const uint16_t flow = get_u16_le(f.data + base + 2);
      const uint16_t power = get_u16_le(f.data + base + 4);
      uint16_t ext1 = get_u16_le(f.data + base + 6);
      uint16_t ext2 = get_u16_le(f.data + base + 8);
      if (ext1 == 0xFFFFU) ext1 = 0;
      if (ext2 == 0xFFFFU) ext2 = 0;
      const uint16_t time_to_stop = get_u16_le(f.data + base + 10);
      const uint8_t status = f.data[base + 12];
      if (!ps.valid) { ps.valid = true; changed = true; }
      if (ps.temp != temp) { ps.temp = temp; changed = true; }
      if (ps.time_to_sleep_hibern != flow) { ps.time_to_sleep_hibern = flow; changed = true; }
      if (ps.power_permille != power) { ps.power_permille = power; changed = true; }
      if (ps.time_to_stop != time_to_stop) { ps.time_to_stop = time_to_stop; changed = true; }
      const bool heater = (status & 0x01U) != 0;
      const bool cooling = (status & 0x04U) != 0;
      const bool suction = (status & 0x08U) != 0;
      const bool stand = (status & 0x80U) != 0;
      if (ps.heater != heater) { ps.heater = heater; changed = true; }
      if (ps.cooling != cooling) { ps.cooling = cooling; changed = true; }
      if (ps.suction != suction) { ps.suction = suction; changed = true; }
      if (ps.stand != stand) { ps.stand = stand; changed = true; }
      if (ps.detail_flags != status) { ps.detail_flags = status; changed = true; }
      if (ps.ha_conti_ext_tc1 != ext1) { ps.ha_conti_ext_tc1 = ext1; changed = true; }
      if (ps.ha_conti_ext_tc2 != ext2) { ps.ha_conti_ext_tc2 = ext2; changed = true; }
      ps.ha_conti_valid = true;
      ps.ha_conti_status_raw = status;
      ps.ha_conti_last_ms = now;
      ps.last_ms = now;
      const uint8_t seen_count = (uint8_t)(port + 1);
      if (seen_count > jbc_highest_seen_port) jbc_highest_seen_port = seen_count;
      if (!jbc_port_count_from_model && seen_count > jbc_port_count) jbc_port_count = seen_count;
    }
    if (changed) mark_fast_changed();
    recompute_work_masks();
    return;
  }

  if (jbc_station_kind != JBC_STATION_SOLD) return;

  // ALE uses a special extended continuous packet in the original DLL. Keep
  // its existing M_INF_PORT path authoritative until we have real ALE hardware.
  if (!strcmp(jbc_model, "ALE")) return;

  const uint8_t block = f.frame_protocol == JBC_PROTO_01 ? 9 : 10;
  uint8_t count = 0;
  for (uint8_t p = 0; p < JBC_MAX_PORTS; ++p) if (jbc_continuous_ports & (1U << p)) ++count;
  if (!count || f.len != (uint8_t)(1U + count * block)) return;

  const uint32_t now = millis();
  bool changed = false;
  uint8_t slot = 0;
  for (uint8_t port = 0; port < JBC_MAX_PORTS; ++port) {
    if (!(jbc_continuous_ports & (1U << port))) continue;
    const uint8_t base = (uint8_t)(1U + slot * block);
    ++slot;
    JbcPortState& ps = jbc_ports[port];

    const uint16_t temp_a = get_u16_le(f.data + base);
    const uint16_t temp_b = get_u16_le(f.data + base + 2);
    const uint16_t power_a = get_u16_le(f.data + base + 4);
    const uint16_t power_b = get_u16_le(f.data + base + 6);
    // GenericStationTools: PA=3, HT=4. JBC_Connect averages the two heater
    // channels only for these dual-heater tools; all others use channel A.
    const bool dual = ps.tool == 3 || ps.tool == 4;
    const uint16_t temp = dual ? (uint16_t)(((uint32_t)temp_a + temp_b) / 2U) : temp_a;
    const uint16_t power = dual ? (uint16_t)(((uint32_t)power_a + power_b) / 2U) : power_a;
    const uint8_t status = f.data[base + 8];

    if (!ps.valid) { ps.valid = true; changed = true; }
    if (ps.temp != temp) { ps.temp = temp; changed = true; }
    if (ps.power_permille != power) { ps.power_permille = power; changed = true; }

    if (f.frame_protocol == JBC_PROTO_02) {
      const bool stand = (status & 0x01U) != 0;
      const bool sleep = (status & 0x02U) != 0;
      const bool hiber = (status & 0x04U) != 0;
      const bool extractor = (status & 0x08U) != 0;
      const bool desolder = (status & 0x10U) != 0;
      const bool soldering = (status & 0x40U) != 0;
      const bool calibrating = (status & 0x80U) != 0;
      // Do not alias bit5 to QSTLock or Stand. The normal DLL continuous parser
      // treats it inconsistently; M_INF_PORT + 0x57 are our authoritative
      // EnabledPort/QSTLock sources.
      if (ps.stand != stand) { ps.stand = stand; changed = true; }
      if (ps.sleep != sleep) { ps.sleep = sleep; changed = true; }
      if (ps.hibernation != hiber) { ps.hibernation = hiber; changed = true; }
      if (ps.extractor != extractor) { ps.extractor = extractor; changed = true; }
      if (ps.desolder != desolder) { ps.desolder = desolder; changed = true; }
      if (ps.sold_soldering != soldering) { ps.sold_soldering = soldering; changed = true; }
      if (ps.sold_calibrating != calibrating) { ps.sold_calibrating = calibrating; changed = true; }
    } else {
      // P01 continuous status overlaps M_INF_PORT: Sleep/Hibernation/Extractor/
      // Desolder. There is no reliable Stand bit in this 9-byte layout.
      const bool sleep = (status & 0x01U) != 0;
      const bool hiber = (status & 0x02U) != 0;
      const bool extractor = (status & 0x04U) != 0;
      const bool desolder = (status & 0x08U) != 0;
      if (ps.sleep != sleep) { ps.sleep = sleep; changed = true; }
      if (ps.hibernation != hiber) { ps.hibernation = hiber; changed = true; }
      if (ps.extractor != extractor) { ps.extractor = extractor; changed = true; }
      if (ps.desolder != desolder) { ps.desolder = desolder; changed = true; }
    }

    ps.sold_conti_valid = true;
    ps.sold_conti_status_raw = status;
    ps.sold_conti_last_ms = now;
    ps.last_ms = now;
    const uint8_t seen_count = (uint8_t)(port + 1);
    if (seen_count > jbc_highest_seen_port) jbc_highest_seen_port = seen_count;
    if (!jbc_port_count_from_model && seen_count > jbc_port_count) jbc_port_count = seen_count;
  }
  if (changed) mark_fast_changed();
  recompute_work_masks();
}

static void decode_sold_qst(const JbcFrame& f) {
  if (f.len != 1) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; }
  const bool on = f.data[0] == 1;
  uint8_t valid_bit = 0, state_bit = 0;
  if (f.command == JBC_CMD_QST_ACTIVATE_P02 || f.command == JBC_CMD_QST_ACTIVATE_P01) { valid_bit = 0x01; state_bit = 0x01; }
  else if (f.command == JBC_CMD_QST_STATUS_P02 || f.command == JBC_CMD_QST_STATUS_P01) { valid_bit = 0x02; state_bit = 0x02; }
  else return;
  bool changed = false;
  if (!(jbc_qst_valid_flags & valid_bit)) { jbc_qst_valid_flags |= valid_bit; changed = true; }
  const uint8_t old_state = jbc_qst_state_flags;
  if (on) jbc_qst_state_flags |= state_bit; else jbc_qst_state_flags &= (uint8_t)~state_bit;
  if (old_state != jbc_qst_state_flags) changed = true;
  if (changed) mark_fast_changed();
}

static bool jbc_send_sold_p01_port_read(uint8_t port, uint8_t command, bool with_tool) {
  if (jbc_station_kind != JBC_STATION_SOLD || jbc_frame_protocol != JBC_PROTO_01 ||
      port >= JBC_MAX_PORTS || !jbc_ports[port].valid) return false;
  if (with_tool && !jbc_ports[port].tool) return false;
  uint8_t data[2] = {port, jbc_ports[port].tool};
  return jbc_send_frame(JBC_PROTO_01, jbc_host_addr, jbc_station_addr, 0, command,
                        data, with_tool ? 2 : 1, port);
}

static bool jbc_send_sold_p01_counter_read(uint8_t command) {
  if (jbc_station_kind != JBC_STATION_SOLD || jbc_frame_protocol != JBC_PROTO_01) return false;
  const bool global = command == JBC_CMD_COUNTER_PLUG || command == JBC_CMD_COUNTER_WORK ||
                      command == JBC_CMD_COUNTER_SLEEP || command == JBC_CMD_COUNTER_HIBER ||
                      command == JBC_CMD_COUNTER_IDLE || command == JBC_CMD_COUNTER_SLEEP_CYCLES ||
                      command == JBC_CMD_COUNTER_DESOLD_CYCLES;
  const bool partial = command == JBC_CMD_COUNTER_PLUG_PARTIAL_P01_SOLD || command == JBC_CMD_COUNTER_WORK_PARTIAL_P01_SOLD ||
                       command == JBC_CMD_COUNTER_SLEEP_PARTIAL_P01_SOLD || command == JBC_CMD_COUNTER_HIBER_PARTIAL_P01_SOLD ||
                       command == JBC_CMD_COUNTER_IDLE_PARTIAL_P01_SOLD || command == JBC_CMD_COUNTER_SLEEP_CYCLES_PARTIAL_P01_SOLD ||
                       command == JBC_CMD_COUNTER_DESOLD_CYCLES_PARTIAL_P01_SOLD;
  if (!global && !partial) return false;
  if (partial && !sold_supports_p01_partial_counters()) return false;
  return jbc_send_frame(JBC_PROTO_01, jbc_host_addr, jbc_station_addr, 0, command, nullptr, 0, 0xFF);
}

static bool is_sold_p01_extra_frame(const JbcFrame& f) {
  if (jbc_station_kind != JBC_STATION_SOLD || f.frame_protocol != JBC_PROTO_01) return false;
  switch (f.command) {
    case JBC_CMD_FIX_TEMP_P01_SOLD: case JBC_CMD_LEVELS_SOLD:
    case JBC_CMD_LEVEL1_P01_SOLD: case JBC_CMD_LEVEL2_P01_SOLD: case JBC_CMD_LEVEL3_P01_SOLD:
    case JBC_CMD_SLEEP_TEMP_SOLD: case JBC_CMD_ADJUST_TEMP_SOLD: case JBC_CMD_SELECT_TEMP_SOLD:
    case JBC_CMD_TIP_TEMP_SOLD: case JBC_CMD_POWER_PERTHOUSAND_SOLD:
    case JBC_CMD_DELAY_TIME_P01_SOLD: case JBC_CMD_STATUS_REMOTE_P01_SOLD:
    case JBC_CMD_TOOL_TYPE_SOLD: case JBC_CMD_TOOL_LAST_ERROR_SOLD: case JBC_CMD_TOOL_STATUS_SOLD:
    case JBC_CMD_COUNTER_PLUG: case JBC_CMD_COUNTER_WORK: case JBC_CMD_COUNTER_SLEEP:
    case JBC_CMD_COUNTER_HIBER: case JBC_CMD_COUNTER_IDLE: case JBC_CMD_COUNTER_SLEEP_CYCLES:
    case JBC_CMD_COUNTER_DESOLD_CYCLES:
    case JBC_CMD_COUNTER_PLUG_PARTIAL_P01_SOLD: case JBC_CMD_COUNTER_WORK_PARTIAL_P01_SOLD:
    case JBC_CMD_COUNTER_SLEEP_PARTIAL_P01_SOLD: case JBC_CMD_COUNTER_HIBER_PARTIAL_P01_SOLD:
    case JBC_CMD_COUNTER_IDLE_PARTIAL_P01_SOLD: case JBC_CMD_COUNTER_SLEEP_CYCLES_PARTIAL_P01_SOLD:
    case JBC_CMD_COUNTER_DESOLD_CYCLES_PARTIAL_P01_SOLD: return true;
    default: return false;
  }
}

static void decode_sold_p01_extra(const JbcFrame& f) {
  const uint8_t pending_port = p01_pending.port;
  p01_pending = PendingRequest();
  bool changed = false;
  const bool is_counter = (f.command >= JBC_CMD_COUNTER_PLUG && f.command <= JBC_CMD_COUNTER_DESOLD_CYCLES) ||
                          (f.command >= JBC_CMD_COUNTER_PLUG_PARTIAL_P01_SOLD && f.command <= JBC_CMD_COUNTER_DESOLD_CYCLES_PARTIAL_P01_SOLD);
  if (is_counter) {
    const uint8_t limit = poll_port_limit();
    if (f.len < (uint8_t)(limit * 4U)) { jbc_note_decode_error(f); return; }
    for (uint8_t port = 0; port < limit && port < JBC_MAX_PORTS; ++port) {
      JbcPortState& ps = jbc_ports[port];
      const uint32_t v = jbc_u32_from_le(f.data + port * 4U);
      uint32_t* dst = nullptr;
      if (f.command == JBC_CMD_COUNTER_PLUG) dst=&ps.counter_plug_min;
      else if (f.command == JBC_CMD_COUNTER_WORK) dst=&ps.counter_work_min;
      else if (f.command == JBC_CMD_COUNTER_SLEEP) dst=&ps.counter_sleep_min;
      else if (f.command == JBC_CMD_COUNTER_HIBER) dst=&ps.counter_hiber_min;
      else if (f.command == JBC_CMD_COUNTER_IDLE) dst=&ps.counter_idle_min;
      else if (f.command == JBC_CMD_COUNTER_SLEEP_CYCLES) dst=&ps.counter_sleep_cycles;
      else if (f.command == JBC_CMD_COUNTER_DESOLD_CYCLES) dst=&ps.counter_desold_cycles;
      else if (f.command == JBC_CMD_COUNTER_PLUG_PARTIAL_P01_SOLD) dst=&ps.sold_partial_plug_min;
      else if (f.command == JBC_CMD_COUNTER_WORK_PARTIAL_P01_SOLD) dst=&ps.sold_partial_work_min;
      else if (f.command == JBC_CMD_COUNTER_SLEEP_PARTIAL_P01_SOLD) dst=&ps.sold_partial_sleep_min;
      else if (f.command == JBC_CMD_COUNTER_HIBER_PARTIAL_P01_SOLD) dst=&ps.sold_partial_hiber_min;
      else if (f.command == JBC_CMD_COUNTER_IDLE_PARTIAL_P01_SOLD) dst=&ps.sold_partial_idle_min;
      else if (f.command == JBC_CMD_COUNTER_SLEEP_CYCLES_PARTIAL_P01_SOLD) dst=&ps.sold_partial_sleep_cycles;
      else if (f.command == JBC_CMD_COUNTER_DESOLD_CYCLES_PARTIAL_P01_SOLD) dst=&ps.sold_partial_desold_cycles;
      if (dst && *dst != v) { *dst=v; changed=true; }
      if (f.command == JBC_CMD_COUNTER_IDLE && !(ps.detail_value_flags & 0x08U)) { ps.detail_value_flags|=0x08U; changed=true; }
      if (f.command == JBC_CMD_COUNTER_DESOLD_CYCLES && !(ps.detail_value_flags & 0x20U)) { ps.detail_value_flags|=0x20U; changed=true; }
      if (f.command == JBC_CMD_COUNTER_IDLE_PARTIAL_P01_SOLD && !(ps.sold_extra_flags & 0x0001U)) { ps.sold_extra_flags|=0x0001U; changed=true; }
      if (f.command == JBC_CMD_COUNTER_DESOLD_CYCLES_PARTIAL_P01_SOLD && !(ps.sold_extra_flags & 0x0002U)) { ps.sold_extra_flags|=0x0002U; changed=true; }
    }
    jbc_initial_low_mark_success(f, 0xFF);
    if (changed) mark_fast_changed();
    return;
  }
  if (pending_port >= JBC_MAX_PORTS) return;
  JbcPortState& ps=jbc_ports[pending_port];
  if (f.command == JBC_CMD_FIX_TEMP_P01_SOLD) {
    if (f.len != 2) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
    const uint16_t v=get_u16_le(f.data); const uint8_t on=v!=0xFFFFU;
    if(ps.sold_fixed_temp!=v){ps.sold_fixed_temp=v;changed=true;} if(ps.sold_fixed_temp_on!=on){ps.sold_fixed_temp_on=on;changed=true;}
    if(!(ps.sold_readonly_port_flags&0x0001U)){ps.sold_readonly_port_flags|=0x0001U;changed=true;}
  } else if (f.command == JBC_CMD_LEVELS_SOLD) {
    if (f.len != 1) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; }
    if(ps.selected_level!=f.data[0]){ps.selected_level=f.data[0];changed=true;} if(!(ps.detail_value_flags&0x10U)){ps.detail_value_flags|=0x10U;changed=true;}
  } else if (f.command == JBC_CMD_LEVEL1_P01_SOLD || f.command == JBC_CMD_LEVEL2_P01_SOLD || f.command == JBC_CMD_LEVEL3_P01_SOLD) {
    if (f.len != 2) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; } const uint8_t ix=f.command==JBC_CMD_LEVEL1_P01_SOLD?0:(f.command==JBC_CMD_LEVEL2_P01_SOLD?1:2); const uint16_t v=get_u16_le(f.data);
    if(ps.level_temp[ix]!=v){ps.level_temp[ix]=v;changed=true;} const uint8_t on=v!=0xFFFFU; if(ps.level_on[ix]!=on){ps.level_on[ix]=on;changed=true;}
    uint8_t any=ps.level_on[0]||ps.level_on[1]||ps.level_on[2]; if(ps.levels_on!=any){ps.levels_on=any;changed=true;} if(!(ps.detail_value_flags&0x10U)){ps.detail_value_flags|=0x10U;changed=true;}
  } else if (f.command == JBC_CMD_SLEEP_TEMP_SOLD) {
    if(f.len!=2){jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN);return;} const uint16_t v=get_u16_le(f.data); if(ps.sleep_temp!=v){ps.sleep_temp=v;changed=true;} if(!(ps.detail_value_flags&0x02U)){ps.detail_value_flags|=0x02U;changed=true;}
  } else if (f.command == JBC_CMD_ADJUST_TEMP_SOLD) {
    if(f.len!=2){jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN);return;} const int16_t v=(int16_t)get_u16_le(f.data); if(ps.adjust_temp!=v){ps.adjust_temp=v;changed=true;} if(!(ps.detail_value_flags&0x04U)){ps.detail_value_flags|=0x04U;changed=true;}
  } else if (f.command == JBC_CMD_SELECT_TEMP_SOLD) {
    if(f.len!=2){jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN);return;} const uint16_t v=get_u16_le(f.data); if(ps.selected_temp!=v){ps.selected_temp=v;changed=true;} if(!(ps.detail_value_flags&0x01U)){ps.detail_value_flags|=0x01U;changed=true;}
  } else if (f.command == JBC_CMD_TIP_TEMP_SOLD) {
    if(f.len!=4){jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN);return;} const int16_t a=(int16_t)get_u16_le(f.data),b=(int16_t)get_u16_le(f.data+2); if(ps.tip_temp_a!=a){ps.tip_temp_a=a;changed=true;}if(ps.tip_temp_b!=b){ps.tip_temp_b=b;changed=true;}if(!(ps.detail_value_flags&0x0200U)){ps.detail_value_flags|=0x0200U;changed=true;}
  } else if (f.command == JBC_CMD_POWER_PERTHOUSAND_SOLD) {
    if(f.len!=4){jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN);return;} const uint16_t v=get_u16_le(f.data); if(ps.sold_direct_power_permille!=v){ps.sold_direct_power_permille=v;changed=true;}if(!(ps.sold_readonly_port_flags&0x0008U)){ps.sold_readonly_port_flags|=0x0008U;changed=true;}
  } else if (f.command == JBC_CMD_DELAY_TIME_P01_SOLD) {
    // ReceiveFrame01_SOLD case 0x59: {TimeToSleepHibern:u16, FutureMode:u8}.
    // Mirror the receiver's derived Stand/Sleep semantics exactly.
    if(f.len!=3){jbc_note_decode_error(f, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN);return;} const uint16_t countdown=get_u16_le(f.data); const uint8_t future=f.data[2];
    if(ps.time_to_sleep_hibern!=countdown){ps.time_to_sleep_hibern=countdown;changed=true;}
    if(ps.future_mode!=future){ps.future_mode=future;changed=true;}
    bool new_sleep=ps.sleep; const bool new_stand=(countdown>0U && future==(uint8_t)'S');
    if(new_stand)new_sleep=false;
    if(ps.sleep!=new_sleep){ps.sleep=new_sleep;changed=true;}
    if(ps.stand!=new_stand){ps.stand=new_stand;changed=true;}
  } else if (f.command == JBC_CMD_STATUS_REMOTE_P01_SOLD) {
    if(f.len!=1){jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN);return;} const uint8_t st=f.data[0];
    const bool ex=(st&0x04U)!=0,de=(st&0x08U)!=0,sl=(st&0x01U)!=0;
    if(ps.extractor!=ex){ps.extractor=ex;changed=true;} if(ps.desolder!=de){ps.desolder=de;changed=true;} if(ps.sleep!=sl){ps.sleep=sl;changed=true;}
  } else if (f.command == JBC_CMD_TOOL_TYPE_SOLD) {
    if(f.len!=1){jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN);return;} if(ps.sold_tool_type!=f.data[0]){ps.sold_tool_type=f.data[0];changed=true;}if(!(ps.sold_diag_flags&0x02U)){ps.sold_diag_flags|=0x02U;changed=true;}
  } else if (f.command == JBC_CMD_TOOL_LAST_ERROR_SOLD) {
    if(f.len!=1){jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN);return;} if(ps.sold_tool_last_error!=f.data[0]){ps.sold_tool_last_error=f.data[0];changed=true;}if(!(ps.sold_diag_flags&0x04U)){ps.sold_diag_flags|=0x04U;changed=true;}
  } else if (f.command == JBC_CMD_TOOL_STATUS_SOLD) {
    if(f.len!=1){jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN);return;} const uint8_t st=f.data[0];
    const bool ex=(st&4U)!=0,hib=(st&2U)!=0,sl=(st&1U)!=0,de=(st&8U)!=0; if(ps.extractor!=ex){ps.extractor=ex;changed=true;}if(ps.hibernation!=hib){ps.hibernation=hib;changed=true;}if(ps.sleep!=sl){ps.sleep=sl;changed=true;}if(ps.desolder!=de){ps.desolder=de;changed=true;}if(!(ps.detail_value_flags&JBC_SOLD_DETAIL_TOOL_STATUS_VALID)){ps.detail_value_flags|=JBC_SOLD_DETAIL_TOOL_STATUS_VALID;changed=true;}
  }
  if(changed){mark_fast_changed();recompute_work_masks();}
}

static bool jbc_send_sold_detail(uint8_t port, uint8_t command) {
  if (jbc_station_kind != JBC_STATION_SOLD || port >= JBC_MAX_PORTS || !jbc_ports[port].valid) return false;
  const JbcProtocol cp = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
  if (cp != JBC_PROTO_02 || jbc_frame_protocol != JBC_PROTO_02) return false; // current tested path
  uint8_t data[2] = {port, jbc_ports[port].tool};
  uint8_t len = 0;
  if (command == JBC_CMD_SELECT_TEMP_SOLD || command == JBC_CMD_TOOL_STATUS_SOLD ||
      command == JBC_CMD_COUNTER_PLUG || command == JBC_CMD_COUNTER_WORK ||
      command == JBC_CMD_COUNTER_SLEEP || command == JBC_CMD_COUNTER_HIBER ||
      command == JBC_CMD_COUNTER_IDLE || command == JBC_CMD_COUNTER_SLEEP_CYCLES ||
      command == JBC_CMD_COUNTER_DESOLD_CYCLES ||
      command == JBC_CMD_COUNTER_PLUG_PARTIAL_SOLD || command == JBC_CMD_COUNTER_WORK_PARTIAL_SOLD ||
      command == JBC_CMD_COUNTER_SLEEP_PARTIAL_SOLD || command == JBC_CMD_COUNTER_HIBER_PARTIAL_SOLD ||
      command == JBC_CMD_COUNTER_IDLE_PARTIAL_SOLD || command == JBC_CMD_COUNTER_SLEEP_CYCLES_PARTIAL_SOLD ||
      command == JBC_CMD_COUNTER_DESOLD_CYCLES_PARTIAL_SOLD || command == JBC_CMD_TIP_TEMP_SOLD ||
      command == JBC_CMD_CURRENT_SOLD || command == JBC_CMD_POWER_PERTHOUSAND_SOLD ||
      command == JBC_CMD_ASSISTANT_WARNING_SOLD || command == JBC_CMD_SOLDERING_RESULT_SOLD) {
    if ((command == JBC_CMD_TIP_TEMP_SOLD || command == JBC_CMD_CURRENT_SOLD ||
         command == JBC_CMD_POWER_PERTHOUSAND_SOLD) && !jbc_ports[port].tool) return false;
    len = 1;
  } else if (command == JBC_CMD_LEVELS_SOLD || command == JBC_CMD_SLEEP_TEMP_SOLD ||
             command == JBC_CMD_ADJUST_TEMP_SOLD || command == JBC_CMD_CARTRIDGE_SOLD ||
             command == JBC_CMD_PROFILE_MODE_SOLD || command == JBC_CMD_ASSISTANT_CONFIG_SOLD) {
    if (!jbc_ports[port].tool) return false;
    len = 2;
  } else return false;
  const uint8_t fid = next_fid();
  return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr, fid, command, data, len, port);
}

static bool jbc_send_sold_selected_profile(uint8_t port) {
  if (!sold_supports_profiles() || port >= JBC_MAX_PORTS || !jbc_ports[port].valid) return false;
  const uint8_t data[4] = {(uint8_t)'j', (uint8_t)'p', (uint8_t)'f', port};
  return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr, next_fid(),
                        JBC_CMD_SELECTED_PROFILE_SOLD, data, sizeof(data), port);
}

static bool jbc_send_sold_ale_feeder_read(uint8_t port, int8_t program) {
  if (!sold_supports_ale_feeder() || port >= JBC_MAX_PORTS || !jbc_ports[port].valid) return false;
  if (program < 0) {
    const uint8_t data[1] = {port};
    return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr, next_fid(),
                          JBC_CMD_ALE_FEEDER_INFO_SOLD, data, sizeof(data), port);
  }
  if (program > 4) return false;
  const uint8_t data[2] = {port, (uint8_t)program};
  return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr, next_fid(),
                        JBC_CMD_ALE_FEEDER_PROGRAM_SOLD, data, sizeof(data), port);
}

static bool is_sold_ale_feeder_frame(const JbcFrame& f) {
  return sold_supports_ale_feeder() && f.frame_protocol == JBC_PROTO_02 &&
         (f.command == JBC_CMD_ALE_FEEDER_INFO_SOLD || f.command == JBC_CMD_ALE_FEEDER_PROGRAM_SOLD);
}

static void decode_sold_ale_feeder(const JbcFrame& f) {
  uint8_t port = pending_port_for_frame(f);
  if (f.command == JBC_CMD_ALE_FEEDER_INFO_SOLD) {
    // ReceiveFrame02_SOLD command 0x70 carries no port byte and the original
    // DLL stores it in port 0. Preserve request context where possible; ALE is
    // currently a one-port station in the JBC model table.
    if (f.len != 12) { jbc_note_decode_error(f, f.len != 12 ? (uint8_t)12 : JBC_DECODE_LEN_UNKNOWN, f.len != 12 ? (uint8_t)12 : JBC_DECODE_LEN_UNKNOWN); return; }
    if (port >= JBC_MAX_PORTS) port = 0;
    JbcPortState& ps = jbc_ports[port];
    ps.sold_feeder_working_mode = f.data[0];
    ps.sold_feeder_selected_program = f.data[1];
    ps.sold_feeder_delivery_length = get_u16_le(f.data + 2);
    ps.sold_feeder_delivery_speed = get_u16_le(f.data + 4);
    ps.sold_feeder_tin_diameter = f.data[6];
    ps.sold_feeder_remove_length = f.data[7];
    ps.sold_feeder_speed_length_readonly = f.data[8] ? 1 : 0;
    ps.sold_feeder_selectable_programs = get_u16_le(f.data + 9);
    ps.sold_feeder_clogging_detection = f.data[11] ? 1 : 0;
    ps.sold_feeder_flags |= 0x0001U;
    mark_fast_changed();
    return;
  }
  if (f.command == JBC_CMD_ALE_FEEDER_PROGRAM_SOLD) {
    if (f.len != 14) { jbc_note_decode_error(f, f.len != 14 ? (uint8_t)14 : JBC_DECODE_LEN_UNKNOWN, f.len != 14 ? (uint8_t)14 : JBC_DECODE_LEN_UNKNOWN); return; }
    const uint8_t response_port = f.data[12];
    const uint8_t program = f.data[13];
    if (response_port < JBC_MAX_PORTS) port = response_port;
    if (port >= JBC_MAX_PORTS || program > 4) return;
    JbcPortState& ps = jbc_ports[port];
    for (uint8_t step = 0; step < 3; ++step) {
      ps.sold_feeder_program_length[program][step] = get_u16_le(f.data + (size_t)step * 2U);
      ps.sold_feeder_program_speed[program][step] = get_u16_le(f.data + 6U + (size_t)step * 2U);
    }
    ps.sold_feeder_flags |= (uint16_t)(1U << (program + 1U));
    mark_fast_changed();
  }
}

static bool ha_supports_temp_levels() {
  // CFeaturesDataInitializer: protocol-02 defaults TempLevels on, but JTSE CAP v1+
  // explicitly disables them. Keep that exact exception and avoid unsupported polls.
  if (jbc_station_kind != JBC_STATION_HA) return false;
  if (!strcmp(jbc_model, "JTSE") && !strcmp(jbc_model_type, "CAP") && jbc_model_version >= 1) return false;
  return true;
}

static bool ha_supports_partial_counters() {
  // Protocol-02 defaults PartialCounters=true; JTSE CAP v1 does not disable it.
  return jbc_station_kind == JBC_STATION_HA && jbc_frame_protocol == JBC_PROTO_02;
}

static bool ha_supports_robot() {
  // Protocol-02 defaults Robot=true; JTSE CAP v1 does not disable it.
  return jbc_station_kind == JBC_STATION_HA && jbc_frame_protocol == JBC_PROTO_02;
}

static bool ha_supports_profiles() {
  // CFeaturesDataInitializer explicitly enables Profiles for JTSE CAP v1+.
  return jbc_station_kind == JBC_STATION_HA && !strcmp(jbc_model, "JTSE") &&
         !strcmp(jbc_model_type, "CAP") && jbc_model_version >= 1;
}

// SOLD status/diagnostic reads that are present in the original JBC_Connect
// update/API paths but were not mirrored by older OFE module revisions.
static bool jbc_send_sold_mos_temp(uint8_t port) {
  if (jbc_station_kind != JBC_STATION_SOLD || port >= JBC_MAX_PORTS || !jbc_ports[port].valid) return false;
  const JbcProtocol cp = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
  const uint8_t data[1] = {port};
  if (jbc_frame_protocol == JBC_PROTO_01 && cp == JBC_PROTO_01) {
    return jbc_send_frame(JBC_PROTO_01, jbc_host_addr, jbc_station_addr, 0,
                          JBC_CMD_MOS_TEMP_P01_SOLD, data, 1, port);
  }
  if (jbc_frame_protocol == JBC_PROTO_02 && cp == JBC_PROTO_02) {
    return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr, next_fid(),
                          JBC_CMD_MOS_TEMP_P02_SOLD, data, 1, port);
  }
  return false;
}

static bool is_sold_mos_temp_frame(const JbcFrame& f) {
  if (jbc_station_kind != JBC_STATION_SOLD) return false;
  return (f.frame_protocol == JBC_PROTO_01 && f.command == JBC_CMD_MOS_TEMP_P01_SOLD) ||
         (f.frame_protocol == JBC_PROTO_02 && f.command == JBC_CMD_MOS_TEMP_P02_SOLD);
}

static void decode_sold_mos_temp(const JbcFrame& f) {
  uint8_t port = pending_port_for_frame(f);
  if (f.frame_protocol == JBC_PROTO_01) {
    if (f.len != 2) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
  } else if (f.frame_protocol == JBC_PROTO_02) {
    // ReceiveFrame02_SOLD requires 3 bytes. The request context is the DLL's
    // authoritative port; byte2 is also a port on current stations.
    if (f.len != 3) { jbc_note_decode_error(f, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN); return; }
    if (f.data[2] < JBC_MAX_PORTS) port = f.data[2];
  } else return;
  if (port >= JBC_MAX_PORTS) return;
  JbcPortState& ps = jbc_ports[port];
  const uint16_t v = get_u16_le(f.data);
  bool changed = false;
  if (ps.sold_mos_temp != v) { ps.sold_mos_temp = v; changed = true; }
  if (!(ps.sold_diag_flags & 0x01U)) { ps.sold_diag_flags |= 0x01U; changed = true; }
  if (changed) mark_fast_changed();
}

static bool jbc_send_sold_station_read(uint8_t command) {
  if (jbc_station_kind != JBC_STATION_SOLD) return false;
  const JbcProtocol cp = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
  uint8_t actual = command;
  if (command == JBC_CMD_CONNECT_READ_P02_USB && cp == JBC_PROTO_01) actual = JBC_CMD_CONNECT_READ_P01;
  if (jbc_frame_protocol == JBC_PROTO_01 && cp == JBC_PROTO_01) {
    // Protocol-01 uses A8 for HelpText and BD for PINEnabled.  Do not
    // remap numeric A8 here: HELP_TEXT_P01 and PIN_ENABLED_P02 deliberately
    // share that byte value.  P01 callers must request the explicit BD command.
    switch (actual) {
      case JBC_CMD_CONNECT_READ_P01:
      case JBC_CMD_REMOTE_MODE_SOLD:
      case JBC_CMD_TEMP_UNIT_P01_SOLD:
      case JBC_CMD_MAX_TEMP_SOLD:
      case JBC_CMD_MIN_TEMP_SOLD:
      case JBC_CMD_N2_MODE_P01_SOLD:
      case JBC_CMD_HELP_TEXT_P01_SOLD:
      case JBC_CMD_POWER_LIMIT_SOLD:
      case JBC_CMD_PIN_SOLD:
      case JBC_CMD_TRAFO_TEMP_SOLD:
      case JBC_CMD_BEEP_P01_SOLD:
      case JBC_CMD_TEMP_ERROR_TRAFO_SOLD:
      case JBC_CMD_TEMP_ERROR_MOS_SOLD:
      case JBC_CMD_PIN_ENABLED_P01_SOLD: break;
      default: return false;
    }
    return jbc_send_frame(JBC_PROTO_01, jbc_host_addr, jbc_station_addr, 0, actual, nullptr, 0, 0xFF);
  }
  if (jbc_frame_protocol == JBC_PROTO_02 && cp == JBC_PROTO_02) {
    if (actual == JBC_CMD_CONNECT_READ_P01) actual = JBC_CMD_CONNECT_READ_P02_USB;
    switch (actual) {
      case JBC_CMD_CONNECT_READ_P02_USB:
      case JBC_CMD_REMOTE_MODE_SOLD:
      case JBC_CMD_MAX_TEMP_SOLD:
      case JBC_CMD_MIN_TEMP_SOLD:
      case JBC_CMD_PIN_ENABLED_SOLD:
      case JBC_CMD_POWER_LIMIT_SOLD:
      case JBC_CMD_PIN_SOLD:
      case JBC_CMD_TRAFO_TEMP_SOLD:
      case JBC_CMD_TEMP_ERROR_TRAFO_SOLD:
      case JBC_CMD_TEMP_ERROR_MOS_SOLD:
      case JBC_CMD_ROBOT_CONFIG_SOLD:
      case JBC_CMD_ROBOT_STATUS_SOLD:
      case JBC_CMD_PERIPHERAL_COUNT_SOLD: break;
      case JBC_CMD_INTERFACE_CONFIG_SOLD:
      case JBC_CMD_AUTOCLEAN_SOLD:
      case JBC_CMD_STATION_DATETIME_SOLD:
      case JBC_CMD_FRONTAL_CONNECTION_SOLD:
        if (!sold_supports_excellence_289()) return false;
        break;
      case JBC_CMD_TYPE_GROUND_P02_SOLD:
        if (!sold_supports_ground_type()) return false;
        break;
      case JBC_CMD_STATION_INTERFACE_P02_SOLD:
        if (strcmp(jbc_model, "ALE") != 0) return false;
        break;
      case JBC_CMD_ETHERNET_P02_SOLD:
        if (!sold_supports_ethernet()) return false;
        break;
      default: return false;
    }
    return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr, next_fid(), actual, nullptr, 0, 0xFF);
  }
  return false;
}

static bool jbc_send_sold_peripheral_read(uint8_t command, uint8_t id) {
  if (!sold_supports_peripherals() || id >= min(jbc_sold_peripheral_count, (uint8_t)4)) return false;
  if (command != JBC_CMD_PERIPHERAL_CONFIG_SOLD && command != JBC_CMD_PERIPHERAL_STATUS_SOLD) return false;
  const uint8_t data[1] = {id};
  return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr, next_fid(), command, data, 1, id);
}

static bool is_sold_station_read_frame(const JbcFrame& f) {
  if (jbc_station_kind != JBC_STATION_SOLD) return false;
  if (f.frame_protocol == JBC_PROTO_01) {
    switch (f.command) {
      case JBC_CMD_CONNECT_READ_P01:
      case JBC_CMD_REMOTE_MODE_SOLD:
      case JBC_CMD_TEMP_UNIT_P01_SOLD:
      case JBC_CMD_MAX_TEMP_SOLD:
      case JBC_CMD_MIN_TEMP_SOLD:
      case JBC_CMD_N2_MODE_P01_SOLD:
      case JBC_CMD_HELP_TEXT_P01_SOLD:
      case JBC_CMD_POWER_LIMIT_SOLD:
      case JBC_CMD_PIN_SOLD:
      case JBC_CMD_TRAFO_TEMP_SOLD:
      case JBC_CMD_BEEP_P01_SOLD:
      case JBC_CMD_TEMP_ERROR_TRAFO_SOLD:
      case JBC_CMD_TEMP_ERROR_MOS_SOLD:
      case JBC_CMD_PIN_ENABLED_P01_SOLD: return true;
      default: return false;
    }
  }
  if (f.frame_protocol != JBC_PROTO_02) return false;
  switch (f.command) {
    case JBC_CMD_CONNECT_READ_P02_USB:
    case JBC_CMD_REMOTE_MODE_SOLD:
    case JBC_CMD_MAX_TEMP_SOLD:
    case JBC_CMD_MIN_TEMP_SOLD:
    case JBC_CMD_PIN_ENABLED_SOLD:
    case JBC_CMD_POWER_LIMIT_SOLD:
    case JBC_CMD_PIN_SOLD:
    case JBC_CMD_TRAFO_TEMP_SOLD:
    case JBC_CMD_TEMP_ERROR_TRAFO_SOLD:
    case JBC_CMD_TEMP_ERROR_MOS_SOLD:
    case JBC_CMD_ROBOT_CONFIG_SOLD:
    case JBC_CMD_ROBOT_STATUS_SOLD:
    case JBC_CMD_PERIPHERAL_COUNT_SOLD:
    case JBC_CMD_PERIPHERAL_CONFIG_SOLD:
    case JBC_CMD_PERIPHERAL_STATUS_SOLD:
    case JBC_CMD_INTERFACE_CONFIG_SOLD:
    case JBC_CMD_AUTOCLEAN_SOLD:
    case JBC_CMD_STATION_DATETIME_SOLD:
    case JBC_CMD_FRONTAL_CONNECTION_SOLD:
    case JBC_CMD_TYPE_GROUND_P02_SOLD:
    case JBC_CMD_STATION_INTERFACE_P02_SOLD:
    case JBC_CMD_ETHERNET_P02_SOLD: return true;
    default: return false;
  }
}

static bool sold_datetime_valid(const uint8_t* data, uint8_t len) {
  if (!data || len != 7) return false;
  const uint16_t year = get_u16_le(data);
  const uint8_t month=data[2], day=data[3], hour=data[4], minute=data[5], second=data[6];
  // ReceiveFrame02_k20/k26_SOLD constructs a System.DateTime and silently
  // ignores invalid values. Mirror that observable behavior instead of
  // publishing station sentinels such as FF00-00-00 00:00:02.
  if (year < 1 || year > 9999 || month < 1 || month > 12 || day < 1 || hour > 23 || minute > 59 || second > 59) return false;
  static const uint8_t mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  uint8_t maxday = mdays[month-1];
  const bool leap = ((year % 4U)==0U && (year % 100U)!=0U) || (year % 400U)==0U;
  if (month == 2 && leap) maxday = 29;
  return day <= maxday;
}

static void decode_sold_station_read(const JbcFrame& f) {
  const uint8_t pending_id = pending_port_for_frame(f);
  if (f.frame_protocol == JBC_PROTO_02) pending_by_fid[f.fid] = PendingRequest();
  else p01_pending = PendingRequest();
  bool changed = false;
  auto set_extra_flag = [&](uint16_t bit) { if (!(jbc_sold_extra_station_flags & bit)) { jbc_sold_extra_station_flags |= bit; changed = true; } };
  auto set_ro_flag = [&](uint32_t bit) { if (!(jbc_sold_readonly_flags & bit)) { jbc_sold_readonly_flags |= bit; changed = true; } };
  auto set_ro_bool = [&](uint32_t valid, uint32_t onbit, bool on) {
    const uint32_t old=jbc_sold_readonly_flags; jbc_sold_readonly_flags|=valid;
    if(on) jbc_sold_readonly_flags|=onbit; else jbc_sold_readonly_flags&=~onbit;
    if(old!=jbc_sold_readonly_flags) changed=true;
  };
  const bool p01 = f.frame_protocol == JBC_PROTO_01;

  if (f.command == JBC_CMD_TRAFO_TEMP_SOLD || f.command == JBC_CMD_TEMP_ERROR_TRAFO_SOLD || f.command == JBC_CMD_TEMP_ERROR_MOS_SOLD) {
    if (f.len != 2) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
    const uint16_t v=get_u16_le(f.data);
    if(f.command==JBC_CMD_TRAFO_TEMP_SOLD){if(jbc_sold_trafo_temp!=v){jbc_sold_trafo_temp=v;changed=true;}if(!(jbc_sold_station_diag_flags&0x01U)){jbc_sold_station_diag_flags|=0x01U;changed=true;}}
    else if(f.command==JBC_CMD_TEMP_ERROR_TRAFO_SOLD){if(jbc_sold_trafo_error_temp!=v){jbc_sold_trafo_error_temp=v;changed=true;}if(!(jbc_sold_station_diag_flags&0x04U)){jbc_sold_station_diag_flags|=0x04U;changed=true;}}
    else {if(jbc_sold_mos_error_temp!=v){jbc_sold_mos_error_temp=v;changed=true;}if(!(jbc_sold_station_diag_flags&0x08U)){jbc_sold_station_diag_flags|=0x08U;changed=true;}}
  } else if (f.command == JBC_CMD_MAX_TEMP_SOLD || f.command == JBC_CMD_MIN_TEMP_SOLD) {
    if(f.len!=2){jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN);return;} const uint16_t v=get_u16_le(f.data);
    if(f.command==JBC_CMD_MAX_TEMP_SOLD){if(jbc_sold_max_temp!=v){jbc_sold_max_temp=v;changed=true;}set_extra_flag(0x0020U);}else{if(jbc_sold_min_temp!=v){jbc_sold_min_temp=v;changed=true;}set_extra_flag(0x0010U);}
  } else if ((p01 && f.command==JBC_CMD_PIN_ENABLED_P01_SOLD) || (!p01 && f.command==JBC_CMD_PIN_ENABLED_SOLD)) {
    if(f.len!=1){jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN);return;} const bool on=f.data[0]!=0; const uint16_t old=jbc_sold_extra_station_flags;jbc_sold_extra_station_flags|=0x0001U;if(on)jbc_sold_extra_station_flags|=0x0002U;else jbc_sold_extra_station_flags&=(uint16_t)~0x0002U;if(old!=jbc_sold_extra_station_flags)changed=true;
  } else if (f.command == JBC_CMD_PIN_SOLD) {
    if(f.len!=4){jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN);return;} bool configured=false; char pin[5]={0};
    for(uint8_t i=0;i<4;++i){const uint8_t c=f.data[i];pin[i]=(c>=0x20&&c<=0x7E)?(char)c:0;if(c!=0&&c!=0xFF&&c!=(uint8_t)' ')configured=true;}
    if(memcmp(jbc_sold_pin,pin,sizeof(jbc_sold_pin))!=0){memcpy(jbc_sold_pin,pin,sizeof(jbc_sold_pin));changed=true;} const uint16_t old=jbc_sold_extra_station_flags;jbc_sold_extra_station_flags|=0x0004U;if(configured)jbc_sold_extra_station_flags|=0x0008U;else jbc_sold_extra_station_flags&=(uint16_t)~0x0008U;if(old!=jbc_sold_extra_station_flags)changed=true;
  } else if (f.command == JBC_CMD_REMOTE_MODE_SOLD) {
    if(f.len!=1){jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN);return;} const bool on=f.data[0]!=0;if(jbc_sold_remote_mode!=on){jbc_sold_remote_mode=on;changed=true;}set_ro_bool(0x00000001UL,0x00000002UL,on);
  } else if (p01 && f.command == JBC_CMD_TEMP_UNIT_P01_SOLD) {
    if(f.len!=1){jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN);return;}if(jbc_sold_temp_unit!=f.data[0]){jbc_sold_temp_unit=f.data[0];changed=true;}set_ro_flag(0x00000004UL);
  } else if (p01 && f.command == JBC_CMD_N2_MODE_P01_SOLD) {
    if(f.len!=1){jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN);return;}const bool on=f.data[0]!=0;if(jbc_sold_n2_mode!=on){jbc_sold_n2_mode=on;changed=true;}set_ro_bool(0x00000008UL,0x00000010UL,on);
  } else if (p01 && f.command == JBC_CMD_HELP_TEXT_P01_SOLD) {
    if(f.len!=1){jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN);return;}const bool on=f.data[0]!=0;if(jbc_sold_help_text!=on){jbc_sold_help_text=on;changed=true;}set_ro_bool(0x00000020UL,0x00000040UL,on);
  } else if (f.command == JBC_CMD_POWER_LIMIT_SOLD) {
    if(f.len!=2){jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN);return;}const uint16_t v=get_u16_le(f.data);if(jbc_sold_power_limit!=v){jbc_sold_power_limit=v;changed=true;}set_ro_flag(0x00000080UL);
  } else if (p01 && f.command == JBC_CMD_BEEP_P01_SOLD) {
    if(f.len!=1){jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN);return;}const bool on=f.data[0]!=0;if(jbc_sold_beep!=on){jbc_sold_beep=on;changed=true;}set_ro_bool(0x00000100UL,0x00000200UL,on);
  } else if (!p01 && f.command == JBC_CMD_INTERFACE_CONFIG_SOLD) {
    if(f.len!=30){jbc_note_decode_error(f, f.len != 30 ? (uint8_t)30 : JBC_DECODE_LEN_UNKNOWN, f.len != 30 ? (uint8_t)30 : JBC_DECODE_LEN_UNKNOWN);return;}if(memcmp(jbc_sold_interface,f.data,7)!=0){memcpy(jbc_sold_interface,f.data,7);changed=true;}
    const uint16_t vals[5]={get_u16_le(f.data+7),get_u16_le(f.data+9),get_u16_le(f.data+11),get_u16_le(f.data+13),get_u16_le(f.data+15)};
    if(jbc_sold_graph_temp_max!=vals[0]){jbc_sold_graph_temp_max=vals[0];changed=true;}if(jbc_sold_graph_temp_min!=vals[1]){jbc_sold_graph_temp_min=vals[1];changed=true;}if(jbc_sold_graph_temp_range!=vals[2]){jbc_sold_graph_temp_range=vals[2];changed=true;}if(jbc_sold_graph_power_max!=vals[3]){jbc_sold_graph_power_max=vals[3];changed=true;}if(jbc_sold_graph_power_min!=vals[4]){jbc_sold_graph_power_min=vals[4];changed=true;}
    const bool beep=f.data[2]!=0;if(jbc_sold_beep!=beep){jbc_sold_beep=beep;changed=true;}set_ro_bool(0x00000100UL,0x00000200UL,beep);set_ro_flag(0x00000400UL);
  } else if (!p01 && f.command == JBC_CMD_AUTOCLEAN_SOLD) {
    if(f.len!=5){jbc_note_decode_error(f, f.len != 5 ? (uint8_t)5 : JBC_DECODE_LEN_UNKNOWN, f.len != 5 ? (uint8_t)5 : JBC_DECODE_LEN_UNKNOWN);return;}const bool on=f.data[0]!=0;const uint16_t t=get_u16_le(f.data+1),sec=get_u16_le(f.data+3);if(jbc_sold_autoclean!=on){jbc_sold_autoclean=on;changed=true;}if(jbc_sold_autoclean_temp!=t){jbc_sold_autoclean_temp=t;changed=true;}if(jbc_sold_autoclean_seconds!=sec){jbc_sold_autoclean_seconds=sec;changed=true;}set_ro_flag(0x00000800UL);
  } else if (!p01 && f.command == JBC_CMD_TYPE_GROUND_P02_SOLD) {
    if(f.len!=1){jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN);return;}if(jbc_sold_ground_type!=f.data[0]){jbc_sold_ground_type=f.data[0];changed=true;}set_ro_flag(0x00001000UL);
  } else if (!p01 && f.command == JBC_CMD_STATION_DATETIME_SOLD) {
    if(f.len!=7){jbc_note_decode_error(f, f.len != 7 ? (uint8_t)7 : JBC_DECODE_LEN_UNKNOWN, f.len != 7 ? (uint8_t)7 : JBC_DECODE_LEN_UNKNOWN);return;}
    if(!sold_datetime_valid(f.data,f.len)) return; // DLL ignores invalid DateTime replies.
    if(memcmp(jbc_sold_datetime,f.data,7)!=0){memcpy(jbc_sold_datetime,f.data,7);changed=true;}set_ro_flag(0x00002000UL);
  } else if (!p01 && f.command == JBC_CMD_FRONTAL_CONNECTION_SOLD) {
    if(f.len==0){jbc_note_decode_error(f, 1, JBC_DECODE_LEN_OPEN);return;}char tmp[21]={0};const uint8_t n=min(f.len,(uint8_t)20);for(uint8_t i=0;i<n;++i){const uint8_t c=f.data[i];tmp[i]=(c>=0x20&&c<=0x7E)?(char)c:'?';}if(memcmp(jbc_sold_frontal,tmp,sizeof(tmp))!=0){memcpy(jbc_sold_frontal,tmp,sizeof(tmp));changed=true;}set_ro_flag(0x00004000UL);
  } else if (!p01 && f.command == JBC_CMD_ETHERNET_P02_SOLD) {
    if(f.len!=23){jbc_note_decode_error(f, f.len != 23 ? (uint8_t)23 : JBC_DECODE_LEN_UNKNOWN, f.len != 23 ? (uint8_t)23 : JBC_DECODE_LEN_UNKNOWN);return;}if(memcmp(jbc_sold_ethernet,f.data,23)!=0){memcpy(jbc_sold_ethernet,f.data,23);changed=true;}set_ro_flag(0x00008000UL);
  } else if (!p01 && f.command == JBC_CMD_STATION_INTERFACE_P02_SOLD) {
    if(f.len!=4){jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN);return;}if(memcmp(jbc_sold_station_interface,f.data,4)!=0){memcpy(jbc_sold_station_interface,f.data,4);changed=true;}set_ro_flag(0x00010000UL);
  } else if (f.command == JBC_CMD_ROBOT_CONFIG_SOLD) {
    if(f.len!=7){jbc_note_decode_error(f, f.len != 7 ? (uint8_t)7 : JBC_DECODE_LEN_UNKNOWN, f.len != 7 ? (uint8_t)7 : JBC_DECODE_LEN_UNKNOWN);return;}if(memcmp(jbc_sold_robot_config,f.data,7)!=0){memcpy(jbc_sold_robot_config,f.data,7);changed=true;}set_extra_flag(0x0040U);
  } else if (f.command == JBC_CMD_ROBOT_STATUS_SOLD) {
    if(f.len!=1){jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN);return;}const bool on=f.data[0]=='C'||f.data[0]=='c';const uint16_t old=jbc_sold_extra_station_flags;jbc_sold_extra_station_flags|=0x0080U;if(on)jbc_sold_extra_station_flags|=0x0100U;else jbc_sold_extra_station_flags&=(uint16_t)~0x0100U;if(old!=jbc_sold_extra_station_flags)changed=true;
  } else if (f.command == JBC_CMD_PERIPHERAL_COUNT_SOLD) {
    if(f.len!=1){jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN);return;}if(jbc_sold_peripheral_count!=f.data[0]){jbc_sold_peripheral_count=f.data[0];changed=true;}set_extra_flag(0x0200U);
  } else if (f.command == JBC_CMD_PERIPHERAL_CONFIG_SOLD) {
    if(f.len!=31){jbc_note_decode_error(f, f.len != 31 ? (uint8_t)31 : JBC_DECODE_LEN_UNKNOWN, f.len != 31 ? (uint8_t)31 : JBC_DECODE_LEN_UNKNOWN);return;}uint8_t id=f.data[30];if(id>=4&&pending_id<4)id=pending_id;if(id>=4)return;SoldPeripheralState& ps=jbc_sold_peripherals[id];SoldPeripheralState next=ps;next.flags|=0x01U;auto digit=[](uint8_t c)->int{return c>='0'&&c<='9'?(int)(c-'0'):-1;};auto hex_ascii=[](uint8_t c)->bool{return (c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f');};bool fae_device_id=true;for(uint8_t bi=0;bi<24;++bi)if(!hex_ascii(f.data[bi])){fae_device_id=false;break;}if(fae_device_id)for(uint8_t bi=24;bi<30;++bi)if(f.data[bi]!=' '){fae_device_id=false;break;}if(fae_device_id){next.version=0;next.hash_mcu_uid[0]=0;next.datetime[0]=0;next.type=6;next.port=0xFF;next.function=0;next.activation=0;next.delay=0;}else{int d0=digit(f.data[0]),d1=digit(f.data[1]);next.version=(uint8_t)((d0>=0&&d1>=0)?d0*10+d1:0);memcpy(next.hash_mcu_uid,f.data+2,4);next.hash_mcu_uid[4]=0;memcpy(next.datetime,f.data+6,14);next.datetime[14]=0;if(f.data[20]=='P'&&f.data[21]=='D')next.type=1;else if(f.data[20]=='M'&&f.data[21]=='S')next.type=2;else if(f.data[20]=='M'&&f.data[21]=='N')next.type=3;else if(f.data[20]=='F'&&f.data[21]=='S')next.type=4;else if(f.data[20]=='M'&&f.data[21]=='V')next.type=5;else next.type=0;if(f.data[22]=='0'&&f.data[23]>='0'&&f.data[23]<='3')next.port=(uint8_t)(f.data[23]-'0');else next.port=0xFF;if(f.data[24]=='S'&&f.data[25]=='L')next.function=1;else if(f.data[24]=='E'&&f.data[25]=='X')next.function=2;else if(f.data[24]=='M'&&f.data[25]=='O')next.function=3;else next.function=0;if(f.data[26]=='P'&&f.data[27]=='S')next.activation=1;else if(f.data[26]=='P'&&f.data[27]=='L')next.activation=2;else next.activation=0;int da=digit(f.data[28]),db=digit(f.data[29]);next.delay=(uint8_t)((da>=0&&db>=0)?da*10+db:0);}if(memcmp(&ps,&next,sizeof(ps))!=0){ps=next;changed=true;}
  } else if (f.command == JBC_CMD_PERIPHERAL_STATUS_SOLD) {
    if(f.len!=3){jbc_note_decode_error(f, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN);return;}uint8_t id=f.data[2];if(id>=4&&pending_id<4)id=pending_id;if(id>=4)return;SoldPeripheralState& ps=jbc_sold_peripherals[id];uint8_t flags=(uint8_t)(ps.flags|0x02U);if(f.data[0])flags|=0x04U;else flags&=(uint8_t)~0x04U;uint8_t pd=f.data[1]=='C'?1:(f.data[1]=='O'?2:(f.data[1]=='K'?3:0));if(ps.flags!=flags){ps.flags=flags;changed=true;}if(ps.pd_status!=pd){ps.pd_status=pd;changed=true;}
  } else {
    if(f.len==0){jbc_note_decode_error(f, 1, JBC_DECODE_LEN_OPEN);return;}bool control=false;if(p01){control=f.data[0]=='C'||f.data[0]=='c'||f.data[0]==1;}else{uint8_t start=0;for(uint8_t i=0;i<f.len;++i)if(f.data[i]==':'){start=(uint8_t)(i+1);break;}char mode=0;for(uint8_t i=start;i<f.len;++i)if(f.data[i]!=' '&&f.data[i]!='\t'&&f.data[i]!='\r'&&f.data[i]!='\n'){mode=(char)f.data[i];break;}control=mode=='C'||mode=='c';}if(jbc_sold_control_mode!=control){jbc_sold_control_mode=control;changed=true;}if(!(jbc_sold_station_diag_flags&0x02U)){jbc_sold_station_diag_flags|=0x02U;changed=true;}
  }
  if(changed) mark_fast_changed();
}

static bool jbc_send_sold_diag(uint8_t port, uint8_t command) {
  if (jbc_station_kind != JBC_STATION_SOLD || jbc_frame_protocol != JBC_PROTO_02 ||
      port >= JBC_MAX_PORTS || !jbc_ports[port].valid) return false;
  if (command != JBC_CMD_TOOL_TYPE_SOLD && command != JBC_CMD_TOOL_LAST_ERROR_SOLD &&
      command != JBC_CMD_ALARM_MAX_SOLD && command != JBC_CMD_ALARM_MIN_SOLD) return false;
  const uint8_t data[1] = {port};
  return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr, next_fid(), command, data, 1, port);
}

static bool is_sold_diag_frame(const JbcFrame& f) {
  if (jbc_station_kind != JBC_STATION_SOLD || f.frame_protocol != JBC_PROTO_02) return false;
  return f.command == JBC_CMD_TOOL_TYPE_SOLD || f.command == JBC_CMD_TOOL_LAST_ERROR_SOLD ||
         f.command == JBC_CMD_ALARM_MAX_SOLD || f.command == JBC_CMD_ALARM_MIN_SOLD;
}

static void decode_sold_diag(const JbcFrame& f) {
  uint8_t port = pending_port_for_frame(f);
  if (f.command == JBC_CMD_TOOL_TYPE_SOLD || f.command == JBC_CMD_TOOL_LAST_ERROR_SOLD) {
    if (f.len != 2) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
    if (f.data[1] < JBC_MAX_PORTS) port = f.data[1];
  } else {
    if (f.len != 5) { jbc_note_decode_error(f, f.len != 5 ? (uint8_t)5 : JBC_DECODE_LEN_UNKNOWN, f.len != 5 ? (uint8_t)5 : JBC_DECODE_LEN_UNKNOWN); return; }
    if (f.data[4] < JBC_MAX_PORTS) port = f.data[4];
  }
  if (port >= JBC_MAX_PORTS) return;
  JbcPortState& ps = jbc_ports[port];
  bool changed = false;
  if (f.command == JBC_CMD_TOOL_TYPE_SOLD) {
    if (ps.sold_tool_type != f.data[0]) { ps.sold_tool_type = f.data[0]; changed = true; }
    if (!(ps.sold_diag_flags & 0x02U)) { ps.sold_diag_flags |= 0x02U; changed = true; }
  } else if (f.command == JBC_CMD_TOOL_LAST_ERROR_SOLD) {
    if (ps.sold_tool_last_error != f.data[0]) { ps.sold_tool_last_error = f.data[0]; changed = true; }
    if (!(ps.sold_diag_flags & 0x04U)) { ps.sold_diag_flags |= 0x04U; changed = true; }
  } else if (f.command == JBC_CMD_ALARM_MAX_SOLD) {
    const int16_t temp = (int16_t)get_u16_le(f.data);
    const int16_t delay = (int16_t)get_u16_le(f.data + 2);
    if (ps.sold_alarm_max_temp != temp) { ps.sold_alarm_max_temp = temp; changed = true; }
    if (ps.sold_alarm_max_delay_tenth_sec != delay) { ps.sold_alarm_max_delay_tenth_sec = delay; changed = true; }
    if (!(ps.sold_diag_flags & 0x08U)) { ps.sold_diag_flags |= 0x08U; changed = true; }
  } else {
    const int16_t temp = (int16_t)get_u16_le(f.data);
    const int16_t delay = (int16_t)get_u16_le(f.data + 2);
    if (ps.sold_alarm_min_temp != temp) { ps.sold_alarm_min_temp = temp; changed = true; }
    if (ps.sold_alarm_min_delay_tenth_sec != delay) { ps.sold_alarm_min_delay_tenth_sec = delay; changed = true; }
    if (!(ps.sold_diag_flags & 0x10U)) { ps.sold_diag_flags |= 0x10U; changed = true; }
  }
  if (changed) mark_fast_changed();
}

static bool jbc_send_ha_connect_status() {
  if (jbc_station_kind != JBC_STATION_HA || jbc_link_state != JBC_LINK_ACTIVE ||
      jbc_frame_protocol != JBC_PROTO_02) return false;
  const JbcProtocol cp = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
  if (cp != JBC_PROTO_02) return false;
  return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr, next_fid(),
                        JBC_CMD_CONNECT_READ_P02_USB, nullptr, 0, 0xFF);
}

static bool is_ha_connect_status_frame(const JbcFrame& f) {
  return jbc_station_kind == JBC_STATION_HA && f.frame_protocol == JBC_PROTO_02 &&
         f.command == JBC_CMD_CONNECT_READ_P02_USB;
}

static void decode_ha_connect_status(const JbcFrame& f) {
  pending_by_fid[f.fid] = PendingRequest();
  if (!f.len) { jbc_note_decode_error(f, 1, JBC_DECODE_LEN_OPEN); return; }
  uint8_t start = 0;
  for (uint8_t i = 0; i < f.len; ++i) if (f.data[i] == (uint8_t)':') { start = (uint8_t)(i + 1); break; }
  char mode = 0;
  for (uint8_t i = start; i < f.len; ++i) {
    if (f.data[i] != (uint8_t)' ' && f.data[i] != (uint8_t)'\t' &&
        f.data[i] != (uint8_t)'\r' && f.data[i] != (uint8_t)'\n') { mode = (char)f.data[i]; break; }
  }
  const bool control = mode == 'C' || mode == 'c';
  bool changed = false;
  if (!jbc_ha_control_mode_valid) { jbc_ha_control_mode_valid = true; changed = true; }
  if (jbc_ha_control_mode != control) { jbc_ha_control_mode = control; changed = true; }
  if (changed) mark_fast_changed();
}

static bool jbc_send_ha_station_diag(uint8_t command) {
  if (jbc_station_kind != JBC_STATION_HA || jbc_link_state != JBC_LINK_ACTIVE || jbc_frame_protocol != JBC_PROTO_02) return false;
  const JbcProtocol cp=jbc_command_protocol==JBC_PROTO_UNKNOWN?jbc_frame_protocol:jbc_command_protocol;if(cp!=JBC_PROTO_02)return false;
  switch(command){
    case JBC_CMD_REMOTE_MODE_HA: case JBC_CMD_TEMP_UNIT_HA: case JBC_CMD_MAXMIN_TEMP_HA: case JBC_CMD_MAXMIN_FLOW_HA: case JBC_CMD_MAXMIN_EXT_TEMP_HA:
    case JBC_CMD_PIN_ENABLED_HA: case JBC_CMD_PIN_HA: case JBC_CMD_BEEP_HA: break;
    case JBC_CMD_SELECTED_PROFILE_HA: if(!ha_supports_profiles())return false;break;
    case JBC_CMD_ROBOT_CONFIG_HA: case JBC_CMD_ROBOT_STATUS_HA: if(!ha_supports_robot())return false;break;
    default:return false;
  }
  return jbc_send_frame(JBC_PROTO_02,jbc_host_addr,jbc_station_addr,next_fid(),command,nullptr,0,0xFF);
}

static bool is_ha_station_diag_frame(const JbcFrame& f) {
  if(jbc_station_kind!=JBC_STATION_HA||f.frame_protocol!=JBC_PROTO_02)return false;
  switch(f.command){case JBC_CMD_REMOTE_MODE_HA:case JBC_CMD_SELECTED_PROFILE_HA:case JBC_CMD_TEMP_UNIT_HA:case JBC_CMD_MAXMIN_TEMP_HA:case JBC_CMD_MAXMIN_FLOW_HA:case JBC_CMD_MAXMIN_EXT_TEMP_HA:case JBC_CMD_PIN_ENABLED_HA:case JBC_CMD_PIN_HA:case JBC_CMD_BEEP_HA:case JBC_CMD_ROBOT_CONFIG_HA:case JBC_CMD_ROBOT_STATUS_HA:return true;default:return false;}
}

static void decode_ha_station_diag(const JbcFrame& f) {
  pending_by_fid[f.fid] = PendingRequest();
  bool changed = false;
  auto set_station_flag = [&](uint16_t bit) {
    if (!(jbc_ha_station_diag_flags & bit)) { jbc_ha_station_diag_flags |= bit; changed = true; }
  };
  switch (f.command) {
    case JBC_CMD_REMOTE_MODE_HA:
      if (f.len != 1) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; }
      if (jbc_ha_remote_mode != (f.data[0] != 0)) { jbc_ha_remote_mode = f.data[0] != 0; changed = true; }
      set_station_flag(0x0001); break;
    case JBC_CMD_SELECTED_PROFILE_HA: {
      // ReceiveFrame02_HA removes the final port byte before decoding the profile name.
      if (f.len < 1) { jbc_note_decode_error(f, 1, JBC_DECODE_LEN_OPEN); return; }
      const uint8_t raw_n = (uint8_t)(f.len - 1U);
      const uint8_t n = min(raw_n, (uint8_t)12);
      char next[13] = {0};
      for (uint8_t i = 0; i < n && f.data[i] != 0; ++i) next[i] = (char)f.data[i];
      if (strncmp(jbc_ha_selected_profile, next, sizeof(jbc_ha_selected_profile)) != 0) {
        memcpy(jbc_ha_selected_profile, next, sizeof(jbc_ha_selected_profile)); changed = true;
      }
      set_station_flag(0x0020); break;
    }
    case JBC_CMD_TEMP_UNIT_HA:
      if (f.len != 1) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; }
      if (jbc_ha_temp_unit != f.data[0]) { jbc_ha_temp_unit = f.data[0]; changed = true; }
      set_station_flag(0x0002); break;
    case JBC_CMD_MAXMIN_TEMP_HA:
      if (f.len != 4) { jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN); return; }
      { const uint16_t mx=get_u16_le(f.data), mn=get_u16_le(f.data+2);
        if (jbc_ha_max_temp!=mx) {jbc_ha_max_temp=mx;changed=true;} if (jbc_ha_min_temp!=mn){jbc_ha_min_temp=mn;changed=true;} }
      set_station_flag(0x0004); break;
    case JBC_CMD_MAXMIN_FLOW_HA:
      if (f.len != 4) { jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN); return; }
      { const uint16_t mx=get_u16_le(f.data), mn=get_u16_le(f.data+2);
        if (jbc_ha_max_flow!=mx) {jbc_ha_max_flow=mx;changed=true;} if (jbc_ha_min_flow!=mn){jbc_ha_min_flow=mn;changed=true;} }
      set_station_flag(0x0008); break;
    case JBC_CMD_MAXMIN_EXT_TEMP_HA:
      if (f.len != 4) { jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN); return; }
      { const uint16_t mx=get_u16_le(f.data), mn=get_u16_le(f.data+2);
        if (jbc_ha_max_ext_temp!=mx) {jbc_ha_max_ext_temp=mx;changed=true;} if (jbc_ha_min_ext_temp!=mn){jbc_ha_min_ext_temp=mn;changed=true;} }
      set_station_flag(0x0010); break;
    case JBC_CMD_PIN_ENABLED_HA: {
      if (f.len != 1) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; } const bool on=f.data[0]!=0; const uint8_t old=jbc_ha_security_flags; jbc_ha_security_flags|=0x01U; if(on)jbc_ha_security_flags|=0x02U;else jbc_ha_security_flags&=(uint8_t)~0x02U; if(old!=jbc_ha_security_flags)changed=true; break;
    }
    case JBC_CMD_PIN_HA: {
      if (f.len != 4) { jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN); return; } char pin[5]={0}; bool configured=false; for(uint8_t i=0;i<4;++i){const uint8_t c=f.data[i];pin[i]=(c>=0x20&&c<=0x7E)?(char)c:0;if(c!=0&&c!=0xFF&&c!=(uint8_t)' ')configured=true;} if(memcmp(jbc_ha_pin,pin,sizeof(jbc_ha_pin))!=0){memcpy(jbc_ha_pin,pin,sizeof(jbc_ha_pin));changed=true;} const uint8_t old=jbc_ha_security_flags;jbc_ha_security_flags|=0x04U;if(configured)jbc_ha_security_flags|=0x08U;else jbc_ha_security_flags&=(uint8_t)~0x08U;if(old!=jbc_ha_security_flags)changed=true; break;
    }
    case JBC_CMD_BEEP_HA: {
      if (f.len != 1) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; } const bool on=f.data[0]!=0;if(jbc_ha_beep!=on){jbc_ha_beep=on;changed=true;}const uint8_t old=jbc_ha_security_flags;jbc_ha_security_flags|=0x10U;if(on)jbc_ha_security_flags|=0x20U;else jbc_ha_security_flags&=(uint8_t)~0x20U;if(old!=jbc_ha_security_flags)changed=true;break;
    }
    case JBC_CMD_ROBOT_CONFIG_HA:
      if (f.len != 7) { jbc_note_decode_error(f, f.len != 7 ? (uint8_t)7 : JBC_DECODE_LEN_UNKNOWN, f.len != 7 ? (uint8_t)7 : JBC_DECODE_LEN_UNKNOWN); return; }
      if (memcmp(jbc_ha_robot_config, f.data, 7) != 0) { memcpy(jbc_ha_robot_config, f.data, 7); changed = true; }
      set_station_flag(0x0040); break;
    case JBC_CMD_ROBOT_STATUS_HA:
      if (f.len != 1) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; }
      { const uint8_t on = (f.data[0]=='C'||f.data[0]=='c') ? 1 : 0;
        if (jbc_ha_robot_status != on) { jbc_ha_robot_status = on; changed = true; } }
      set_station_flag(0x0080); break;
    default: return;
  }
  if (changed) mark_fast_changed();
}

static bool jbc_send_ha_detail(uint8_t port, uint8_t command) {
  if (jbc_station_kind != JBC_STATION_HA || port >= JBC_MAX_PORTS || !jbc_ports[port].valid) return false;
  const JbcProtocol cp = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
  if (cp != JBC_PROTO_02 || jbc_frame_protocol != JBC_PROTO_02) return false;
  uint8_t data[2] = {port, jbc_ports[port].tool};
  uint8_t len = 1;
  switch (command) {
    case JBC_CMD_PROFILE_MODE_HA:
    case JBC_CMD_SELECT_TEMP_HA:
    case JBC_CMD_SELECT_FLOW_HA:
    case JBC_CMD_SELECT_EXT_TEMP_HA:
    case JBC_CMD_ACTUAL_EXT_TEMP_HA:
    case JBC_CMD_HEATER_STATUS_HA:
    case JBC_CMD_SUCTION_STATUS_HA:
    case JBC_CMD_AIR_TEMP_HA:
    case JBC_CMD_POWER_HA:
    case JBC_CMD_CONNECT_TOOL_HA:
    case JBC_CMD_TOOL_ERROR_HA:
    case JBC_CMD_TOOL_STATUS_HA:
    case JBC_CMD_AIR_FLOW_HA:
    case JBC_CMD_COUNTER_PLUG_HA:
    case JBC_CMD_COUNTER_WORK_HA:
    case JBC_CMD_COUNTER_WORK_CYCLES_HA:
    case JBC_CMD_COUNTER_SUCTION_CYCLES_HA:
      len = 1; break;
    case JBC_CMD_COUNTER_PLUG_PARTIAL_HA:
    case JBC_CMD_COUNTER_WORK_PARTIAL_HA:
    case JBC_CMD_COUNTER_WORK_CYCLES_PARTIAL_HA:
    case JBC_CMD_COUNTER_SUCTION_CYCLES_PARTIAL_HA:
      if (!ha_supports_partial_counters()) return false;
      len = 1; break;
    case JBC_CMD_EXTERNAL_TC_MODE_HA:
    case JBC_CMD_LEVELS_HA:
    case JBC_CMD_ADJUST_TEMP_HA:
    case JBC_CMD_TIME_TO_STOP_HA:
    case JBC_CMD_START_MODE_HA:
      if (!jbc_ports[port].tool) return false;
      if (command == JBC_CMD_LEVELS_HA && !ha_supports_temp_levels()) return false;
      len = 2; break;
    default: return false;
  }
  return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr, next_fid(), command, data, len, port);
}

static bool is_ha_detail_frame(const JbcFrame& f) {
  if (jbc_station_kind != JBC_STATION_HA || f.frame_protocol != JBC_PROTO_02) return false;
  switch (f.command) {
    case JBC_CMD_PROFILE_MODE_HA:
    case JBC_CMD_EXTERNAL_TC_MODE_HA:
    case JBC_CMD_LEVELS_HA:
    case JBC_CMD_ADJUST_TEMP_HA:
    case JBC_CMD_TIME_TO_STOP_HA:
    case JBC_CMD_START_MODE_HA:
    case JBC_CMD_SELECT_TEMP_HA:
    case JBC_CMD_SELECT_FLOW_HA:
    case JBC_CMD_SELECT_EXT_TEMP_HA:
    case JBC_CMD_ACTUAL_EXT_TEMP_HA:
    case JBC_CMD_HEATER_STATUS_HA:
    case JBC_CMD_SUCTION_STATUS_HA:
    case JBC_CMD_AIR_TEMP_HA:
    case JBC_CMD_POWER_HA:
    case JBC_CMD_CONNECT_TOOL_HA:
    case JBC_CMD_TOOL_ERROR_HA:
    case JBC_CMD_TOOL_STATUS_HA:
    case JBC_CMD_AIR_FLOW_HA:
    case JBC_CMD_COUNTER_PLUG_HA:
    case JBC_CMD_COUNTER_WORK_HA:
    case JBC_CMD_COUNTER_WORK_CYCLES_HA:
    case JBC_CMD_COUNTER_SUCTION_CYCLES_HA:
    case JBC_CMD_COUNTER_PLUG_PARTIAL_HA:
    case JBC_CMD_COUNTER_WORK_PARTIAL_HA:
    case JBC_CMD_COUNTER_WORK_CYCLES_PARTIAL_HA:
    case JBC_CMD_COUNTER_SUCTION_CYCLES_PARTIAL_HA: return true;
    default: return false;
  }
}

static void decode_ha_detail(const JbcFrame& f) {
  const uint8_t port = pending_port_for_frame(f);
  if (port >= JBC_MAX_PORTS) return;
  JbcPortState& ps = jbc_ports[port];
  bool changed = false;
  auto set_flag = [&](uint16_t bit) { if (!(ps.ha_value_flags & bit)) { ps.ha_value_flags |= bit; changed = true; } };
  switch (f.command) {
    case JBC_CMD_PROFILE_MODE_HA:
      if (f.len != 2) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
      if (ps.profile_mode != (f.data[0] ? 1 : 0)) { ps.profile_mode = f.data[0] ? 1 : 0; changed = true; }
      set_flag(0x0200); break;
    case JBC_CMD_EXTERNAL_TC_MODE_HA:
      if (f.len != 3) { jbc_note_decode_error(f, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN); return; }
      if (ps.external_tc_mode != f.data[0]) { ps.external_tc_mode = f.data[0]; changed = true; }
      set_flag(0x0080); break;
    case JBC_CMD_LEVELS_HA:
      if (f.len != 25) { jbc_note_decode_error(f, f.len != 25 ? (uint8_t)25 : JBC_DECODE_LEN_UNKNOWN, f.len != 25 ? (uint8_t)25 : JBC_DECODE_LEN_UNKNOWN); return; }
      if (ps.levels_on != f.data[0]) { ps.levels_on = f.data[0]; changed = true; }
      if (ps.selected_level != f.data[1]) { ps.selected_level = f.data[1]; changed = true; }
      for (uint8_t i = 0; i < 3; ++i) {
        const uint8_t b = i == 0 ? 2 : (i == 1 ? 9 : 16);
        const uint8_t to = i == 0 ? 3 : (i == 1 ? 10 : 17);
        const uint8_t fo = i == 0 ? 5 : (i == 1 ? 12 : 19);
        const uint8_t eo = i == 0 ? 7 : (i == 1 ? 14 : 21);
        const uint8_t on = f.data[b]; const uint16_t tv = get_u16_le(f.data + to);
        const uint16_t fv = get_u16_le(f.data + fo); const uint16_t ev = get_u16_le(f.data + eo);
        if (ps.level_on[i] != on) { ps.level_on[i] = on; changed = true; }
        if (ps.level_temp[i] != tv) { ps.level_temp[i] = tv; changed = true; }
        if (ps.level_flow_permille[i] != fv) { ps.level_flow_permille[i] = fv; changed = true; }
        if (ps.level_ext_temp[i] != ev) { ps.level_ext_temp[i] = ev; changed = true; }
      }
      set_flag(0x0400); break;
    case JBC_CMD_ADJUST_TEMP_HA:
      if (f.len != 4) { jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN); return; }
      { const int16_t v = (int16_t)get_u16_le(f.data); if (ps.ha_adjust_temp != v) { ps.ha_adjust_temp = v; changed = true; } }
      set_flag(0x0020); break;
    case JBC_CMD_TIME_TO_STOP_HA:
      if (f.len != 4) { jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN); return; }
      { const uint16_t v = get_u16_le(f.data); if (ps.configured_time_to_stop != v) { ps.configured_time_to_stop = v; changed = true; } }
      set_flag(0x0040); break;
    case JBC_CMD_START_MODE_HA:
      if (f.len != 3) { jbc_note_decode_error(f, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN); return; }
      if (ps.start_mode != f.data[0]) { ps.start_mode = f.data[0]; changed = true; }
      set_flag(0x0100); break;
    case JBC_CMD_SELECT_TEMP_HA:
      if (f.len != 3) { jbc_note_decode_error(f, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN); return; }
      { const uint16_t v = get_u16_le(f.data); if (ps.selected_temp != v) { ps.selected_temp = v; changed = true; } }
      set_flag(0x0002); break;
    case JBC_CMD_SELECT_FLOW_HA:
      if (f.len != 3) { jbc_note_decode_error(f, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN); return; }
      { const uint16_t v = get_u16_le(f.data); if (ps.selected_flow_permille != v) { ps.selected_flow_permille = v; changed = true; } }
      set_flag(0x0004); break;
    case JBC_CMD_SELECT_EXT_TEMP_HA:
      if (f.len != 3) { jbc_note_decode_error(f, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN); return; }
      { const uint16_t v = get_u16_le(f.data); if (ps.selected_ext_temp != v) { ps.selected_ext_temp = v; changed = true; } }
      set_flag(0x0008); break;
    case JBC_CMD_ACTUAL_EXT_TEMP_HA:
      if (f.len != 3) { jbc_note_decode_error(f, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN); return; }
      { uint16_t v = get_u16_le(f.data); if (v == 0xFFFFU) v = 0; if (ps.actual_ext_temp != v) { ps.actual_ext_temp = v; changed = true; } }
      set_flag(0x0010); break;
    case JBC_CMD_HEATER_STATUS_HA:
      if (f.len != 2) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
      if (ps.ha_diag_heater_state != f.data[0]) { ps.ha_diag_heater_state = f.data[0]; changed = true; }
      if (!(ps.ha_diag_flags & 0x0080U)) { ps.ha_diag_flags |= 0x0080U; changed = true; }
      break;
    case JBC_CMD_SUCTION_STATUS_HA:
      if (f.len != 2) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
      if (ps.ha_diag_suction_state != f.data[0]) { ps.ha_diag_suction_state = f.data[0]; changed = true; }
      if (!(ps.ha_diag_flags & 0x0100U)) { ps.ha_diag_flags |= 0x0100U; changed = true; }
      break;
    case JBC_CMD_AIR_TEMP_HA:
      if (f.len != 3) { jbc_note_decode_error(f, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN); return; }
      { const uint16_t v = get_u16_le(f.data); if (ps.ha_diag_air_temp != v) { ps.ha_diag_air_temp = v; changed = true; } }
      if (!(ps.ha_diag_flags & 0x0001U)) { ps.ha_diag_flags |= 0x0001U; changed = true; }
      break;
    case JBC_CMD_POWER_HA:
      if (f.len != 3) { jbc_note_decode_error(f, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN); return; }
      { const uint16_t v = get_u16_le(f.data); if (ps.ha_diag_power_permille != v) { ps.ha_diag_power_permille = v; changed = true; } }
      if (!(ps.ha_diag_flags & 0x0002U)) { ps.ha_diag_flags |= 0x0002U; changed = true; }
      break;
    case JBC_CMD_AIR_FLOW_HA:
      if (f.len != 3) { jbc_note_decode_error(f, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN); return; }
      { const uint16_t v = get_u16_le(f.data); if (ps.ha_diag_flow_permille != v) { ps.ha_diag_flow_permille = v; changed = true; } }
      if (!(ps.ha_diag_flags & 0x0004U)) { ps.ha_diag_flags |= 0x0004U; changed = true; }
      break;
    case JBC_CMD_CONNECT_TOOL_HA:
      if (f.len != 2) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
      if (ps.ha_diag_tool != f.data[0]) { ps.ha_diag_tool = f.data[0]; changed = true; }
      if (!(ps.ha_diag_flags & 0x0008U)) { ps.ha_diag_flags |= 0x0008U; changed = true; }
      break;
    case JBC_CMD_TOOL_ERROR_HA:
      if (f.len != 2) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
      if (ps.ha_diag_error != f.data[0]) { ps.ha_diag_error = f.data[0]; changed = true; }
      if (!(ps.ha_diag_flags & 0x0010U)) { ps.ha_diag_flags |= 0x0010U; changed = true; }
      break;
    case JBC_CMD_TOOL_STATUS_HA:
      if (f.len != 2) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
      if (ps.ha_diag_status != f.data[0]) { ps.ha_diag_status = f.data[0]; changed = true; }
      if (!(ps.ha_diag_flags & 0x0020U)) { ps.ha_diag_flags |= 0x0020U; changed = true; }
      break;
    case JBC_CMD_COUNTER_PLUG_HA:
    case JBC_CMD_COUNTER_WORK_HA:
    case JBC_CMD_COUNTER_WORK_CYCLES_HA:
    case JBC_CMD_COUNTER_SUCTION_CYCLES_HA:
      if (f.len != 5) { jbc_note_decode_error(f, f.len != 5 ? (uint8_t)5 : JBC_DECODE_LEN_UNKNOWN, f.len != 5 ? (uint8_t)5 : JBC_DECODE_LEN_UNKNOWN); return; }
      { const uint32_t v = get_u32_le(f.data); uint32_t* dst = nullptr;
        if (f.command == JBC_CMD_COUNTER_PLUG_HA) dst = &ps.ha_counter_plug_min;
        else if (f.command == JBC_CMD_COUNTER_WORK_HA) dst = &ps.ha_counter_work_min;
        else if (f.command == JBC_CMD_COUNTER_WORK_CYCLES_HA) dst = &ps.ha_counter_work_cycles;
        else dst = &ps.ha_counter_suction_cycles;
        if (*dst != v) { *dst = v; changed = true; }
        if (f.command == JBC_CMD_COUNTER_SUCTION_CYCLES_HA) set_flag(0x0800);
      }
      break;
    case JBC_CMD_COUNTER_PLUG_PARTIAL_HA:
    case JBC_CMD_COUNTER_WORK_PARTIAL_HA:
    case JBC_CMD_COUNTER_WORK_CYCLES_PARTIAL_HA:
    case JBC_CMD_COUNTER_SUCTION_CYCLES_PARTIAL_HA:
      if (f.len != 5) { jbc_note_decode_error(f, f.len != 5 ? (uint8_t)5 : JBC_DECODE_LEN_UNKNOWN, f.len != 5 ? (uint8_t)5 : JBC_DECODE_LEN_UNKNOWN); return; }
      { const uint32_t v=get_u32_le(f.data); uint32_t* dst=nullptr;
        if (f.command==JBC_CMD_COUNTER_PLUG_PARTIAL_HA) dst=&ps.ha_partial_plug_min;
        else if (f.command==JBC_CMD_COUNTER_WORK_PARTIAL_HA) dst=&ps.ha_partial_work_min;
        else if (f.command==JBC_CMD_COUNTER_WORK_CYCLES_PARTIAL_HA) dst=&ps.ha_partial_work_cycles;
        else dst=&ps.ha_partial_suction_cycles;
        if (*dst!=v){*dst=v;changed=true;}
        if (f.command==JBC_CMD_COUNTER_SUCTION_CYCLES_PARTIAL_HA && !(ps.ha_diag_flags&0x0040U)){ps.ha_diag_flags|=0x0040U;changed=true;}
      }
      break;
    default: return;
  }
  jbc_initial_low_mark_success(f, port);
  if (f.frame_protocol == JBC_PROTO_02) pending_by_fid[f.fid] = PendingRequest();
  if (changed) mark_fast_changed();
}

static bool jbc_send_ph_read(uint8_t command, uint8_t context) {
  if (jbc_station_kind != JBC_STATION_PH || jbc_link_state != JBC_LINK_ACTIVE ||
      jbc_frame_protocol != JBC_PROTO_02) return false;
  const JbcProtocol cp = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
  if (cp != JBC_PROTO_02) return false;
  bool with_context = false;
  switch (command) {
    case JBC_CMD_WORK_MODE_PH:
    case JBC_CMD_HEATER_STATUS_PH:
    case JBC_CMD_EXTERNAL_TC_MODE_PH:
    case JBC_CMD_TIME_TO_STOP_PH:
    case JBC_CMD_SELECT_TEMP_PH:
    case JBC_CMD_SELECT_POWER_PH:
    case JBC_CMD_TC_WARNING_PH:
    case JBC_CMD_ACTIVE_ZONES_PH:
    case JBC_CMD_EXTERNAL_AIR_TEMP_PH:
    case JBC_CMD_COUNTER_PLUG_PH:
    case JBC_CMD_COUNTER_WORK_PH:
    case JBC_CMD_COUNTER_WORK_CYCLES_PH:
    case JBC_CMD_COUNTER_PLUG_PARTIAL_PH:
    case JBC_CMD_COUNTER_WORK_PARTIAL_PH:
    case JBC_CMD_COUNTER_WORK_CYCLES_PARTIAL_PH:
      with_context = true;
      break;
    case JBC_CMD_PROFILE_PH:
    case JBC_CMD_PROFILE_SETTINGS_PH:
    case JBC_CMD_PROFILE_TEACH_PH:
    case JBC_CMD_MAXMIN_POWER_PH:
    case JBC_CMD_MAXMIN_TEMP_PH:
    case JBC_CMD_PIN_ENABLED_PH:
    case JBC_CMD_PIN_PH:
    case JBC_CMD_BEEP_PH:
    case JBC_CMD_REMOTE_MODE_PH:
    case JBC_CMD_CONNECT_STATUS_PH:
    case JBC_CMD_ROBOT_CONFIG_PH:
    case JBC_CMD_ROBOT_STATUS_PH:
      break;
    default:
      return false;
  }
  if (with_context && context >= 4) return false;
  const uint8_t data[1] = {context};
  return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr, next_fid(), command,
                        with_context ? data : nullptr, with_context ? 1 : 0,
                        with_context ? context : 0xFF);
}

static bool is_ph_read_frame(const JbcFrame& f) {
  if (jbc_station_kind != JBC_STATION_PH || f.frame_protocol != JBC_PROTO_02) return false;
  switch (f.command) {
    case JBC_CMD_WORK_MODE_PH:
    case JBC_CMD_HEATER_STATUS_PH:
    case JBC_CMD_EXTERNAL_TC_MODE_PH:
    case JBC_CMD_TIME_TO_STOP_PH:
    case JBC_CMD_SELECT_TEMP_PH:
    case JBC_CMD_SELECT_POWER_PH:
    case JBC_CMD_TC_WARNING_PH:
    case JBC_CMD_ACTIVE_ZONES_PH:
    case JBC_CMD_EXTERNAL_AIR_TEMP_PH:
    case JBC_CMD_PROFILE_PH:
    case JBC_CMD_PROFILE_SETTINGS_PH:
    case JBC_CMD_PROFILE_TEACH_PH:
    case JBC_CMD_MAXMIN_POWER_PH:
    case JBC_CMD_MAXMIN_TEMP_PH:
    case JBC_CMD_PIN_ENABLED_PH:
    case JBC_CMD_PIN_PH:
    case JBC_CMD_BEEP_PH:
    case JBC_CMD_REMOTE_MODE_PH:
    case JBC_CMD_COUNTER_PLUG_PH:
    case JBC_CMD_COUNTER_WORK_PH:
    case JBC_CMD_COUNTER_WORK_CYCLES_PH:
    case JBC_CMD_COUNTER_PLUG_PARTIAL_PH:
    case JBC_CMD_COUNTER_WORK_PARTIAL_PH:
    case JBC_CMD_COUNTER_WORK_CYCLES_PARTIAL_PH:
    case JBC_CMD_CONNECT_STATUS_PH:
    case JBC_CMD_ROBOT_CONFIG_PH:
    case JBC_CMD_ROBOT_STATUS_PH:
      return true;
    default:
      return false;
  }
}

static void decode_ph_read(const JbcFrame& f) {
  const uint8_t context = pending_port_for_frame(f);
  bool changed = false;
  auto station_flag = [&](uint32_t bit) {
    if (!(jbc_ph_station_flags & bit)) { jbc_ph_station_flags |= bit; changed = true; }
  };
  auto port_flag = [&](JbcPortState& ps, uint16_t bit) {
    if (!(ps.ph_flags & bit)) { ps.ph_flags |= bit; changed = true; }
  };
  switch (f.command) {
    case JBC_CMD_WORK_MODE_PH: {
      if (f.len != 2 || context >= JBC_MAX_PORTS) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
      JbcPortState& ps = jbc_ports[context];
      if (ps.ph_work_mode != f.data[0]) { ps.ph_work_mode = f.data[0]; changed = true; }
      port_flag(ps, 0x0001U);
      break;
    }
    case JBC_CMD_HEATER_STATUS_PH: {
      if (f.len != 2 || context >= JBC_MAX_PORTS) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
      JbcPortState& ps = jbc_ports[context];
      if (ps.ph_heater_status != f.data[0]) { ps.ph_heater_status = f.data[0]; changed = true; }
      const bool on = f.data[0] != 0;
      if (ps.heater != on) { ps.heater = on; changed = true; }
      port_flag(ps, 0x0002U);
      break;
    }
    case JBC_CMD_EXTERNAL_TC_MODE_PH: {
      if (f.len != 2 || context >= 4) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
      PhTcState& tc = jbc_ph_tc[context];
      if (tc.mode != f.data[0]) { tc.mode = f.data[0]; changed = true; }
      if (!(tc.flags & 0x04U)) { tc.flags |= 0x04U; changed = true; }
      break;
    }
    case JBC_CMD_TIME_TO_STOP_PH: {
      if (f.len != 5 || context >= JBC_MAX_PORTS) { jbc_note_decode_error(f, f.len != 5 ? (uint8_t)5 : JBC_DECODE_LEN_UNKNOWN, f.len != 5 ? (uint8_t)5 : JBC_DECODE_LEN_UNKNOWN); return; }
      JbcPortState& ps = jbc_ports[context];
      const uint32_t v = get_u32_le(f.data);
      if (ps.ph_configured_time_to_stop != v) { ps.ph_configured_time_to_stop = v; changed = true; }
      port_flag(ps, 0x0004U);
      break;
    }
    case JBC_CMD_SELECT_TEMP_PH: {
      if (f.len != 3) { jbc_note_decode_error(f, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN); return; }
      const uint8_t channel = f.data[2] < 4 ? f.data[2] : context;
      if (channel >= 4) return;
      PhTcState& tc = jbc_ph_tc[channel];
      const uint16_t v = get_u16_le(f.data);
      if (tc.selected_temp != v) { tc.selected_temp = v; changed = true; }
      if (!(tc.flags & 0x08U)) { tc.flags |= 0x08U; changed = true; }
      break;
    }
    case JBC_CMD_SELECT_POWER_PH: {
      if (f.len != 3 || context >= JBC_MAX_PORTS) { jbc_note_decode_error(f, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN); return; }
      JbcPortState& ps = jbc_ports[context];
      const uint16_t v = get_u16_le(f.data);
      if (ps.ph_selected_power != v) { ps.ph_selected_power = v; changed = true; }
      port_flag(ps, 0x0008U);
      break;
    }
    case JBC_CMD_TC_WARNING_PH: {
      if (f.len != 1 || context >= 4) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; }
      PhTcState& tc = jbc_ph_tc[context];
      if (tc.warning != f.data[0]) { tc.warning = f.data[0]; changed = true; }
      if (!(tc.flags & 0x02U)) { tc.flags |= 0x02U; changed = true; }
      break;
    }
    case JBC_CMD_ACTIVE_ZONES_PH: {
      if (f.len != 2 || context >= JBC_MAX_PORTS) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
      JbcPortState& ps = jbc_ports[context];
      if (ps.ph_active_zones != f.data[0]) { ps.ph_active_zones = f.data[0]; changed = true; }
      port_flag(ps, 0x0010U);
      break;
    }
    case JBC_CMD_EXTERNAL_AIR_TEMP_PH: {
      if (f.len != 2 || context >= 4) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
      uint16_t v = get_u16_le(f.data); if (v == 0xFFFFU) v = 0;
      PhTcState& tc = jbc_ph_tc[context];
      if (tc.actual_temp != v) { tc.actual_temp = v; changed = true; }
      if (!(tc.flags & 0x01U)) { tc.flags |= 0x01U; changed = true; }
      // The decompiled DLL assigns this read to a local variable instead of the
      // backing TC field; OFE retains the non-destructive value intentionally.
      break;
    }
    case JBC_CMD_PROFILE_PH: {
      if (f.len < 1) { jbc_note_decode_error(f, 1, JBC_DECODE_LEN_OPEN); return; }
      const uint8_t count = f.data[0];
      if (count > JBC_PH_PROFILE_MAX_POINTS || f.len != (uint8_t)(1U + (uint16_t)count * 4U)) { jbc_note_decode_error(f); return; }
      if (jbc_ph_profile_count != count) { jbc_ph_profile_count = count; changed = true; }
      for (uint8_t i = 0; i < count; ++i) {
        const int16_t tm = (int16_t)get_u16_le(f.data + 1 + i * 4);
        const int16_t val = (int16_t)get_u16_le(f.data + 3 + i * 4);
        if (jbc_ph_profile_time[i] != tm) { jbc_ph_profile_time[i] = tm; changed = true; }
        if (jbc_ph_profile_value[i] != val) { jbc_ph_profile_value[i] = val; changed = true; }
      }
      station_flag(0x00002000UL);
      break;
    }
    case JBC_CMD_PROFILE_SETTINGS_PH:
      if (f.len != 3) { jbc_note_decode_error(f, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN); return; }
      if (jbc_ph_profile_points_setting != f.data[0]) { jbc_ph_profile_points_setting = f.data[0]; changed = true; }
      if (jbc_ph_profile_consignment != f.data[1]) { jbc_ph_profile_consignment = f.data[1]; changed = true; }
      if (jbc_ph_profile_tc_regulation != f.data[2]) { jbc_ph_profile_tc_regulation = f.data[2]; changed = true; }
      station_flag(0x00004000UL);
      break;
    case JBC_CMD_PROFILE_TEACH_PH: {
      if (f.len < 4) { jbc_note_decode_error(f); return; }
      const int16_t interval = (int16_t)get_u16_le(f.data);
      const uint16_t count16 = get_u16_le(f.data + 2);
      if (count16 > JBC_PH_TEACH_MAX_POINTS || f.len != (uint8_t)(4U + count16 * 2U)) { jbc_note_decode_error(f); return; }
      const uint8_t count = (uint8_t)count16;
      if (jbc_ph_profile_teach_interval != interval) { jbc_ph_profile_teach_interval = interval; changed = true; }
      if (jbc_ph_teach_count != count) { jbc_ph_teach_count = count; changed = true; }
      for (uint8_t i = 0; i < count; ++i) {
        const int16_t val = (int16_t)get_u16_le(f.data + 4 + i * 2);
        if (jbc_ph_teach_value[i] != val) { jbc_ph_teach_value[i] = val; changed = true; }
      }
      station_flag(0x00008000UL);
      break;
    }
    case JBC_CMD_MAXMIN_POWER_PH:
      if (f.len != 4) { jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN); return; }
      { const int16_t mx=(int16_t)get_u16_le(f.data), mn=(int16_t)get_u16_le(f.data+2);
        if(jbc_ph_max_power!=mx){jbc_ph_max_power=mx;changed=true;} if(jbc_ph_min_power!=mn){jbc_ph_min_power=mn;changed=true;} }
      station_flag(0x00000001UL); break;
    case JBC_CMD_MAXMIN_TEMP_PH:
      if (f.len != 4) { jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN); return; }
      { const uint16_t mx=get_u16_le(f.data), mn=get_u16_le(f.data+2);
        if(jbc_ph_max_temp!=mx){jbc_ph_max_temp=mx;changed=true;} if(jbc_ph_min_temp!=mn){jbc_ph_min_temp=mn;changed=true;} }
      station_flag(0x00000002UL); break;
    case JBC_CMD_PIN_ENABLED_PH:
      if (f.len != 1) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; }
      { const bool on=f.data[0]!=0; const bool old=(jbc_ph_station_flags&0x8UL)!=0;
        if(old!=on){if(on)jbc_ph_station_flags|=0x8UL;else jbc_ph_station_flags&=~0x8UL;changed=true;} }
      station_flag(0x00000004UL); break;
    case JBC_CMD_PIN_PH:
      if (f.len != 4) { jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN); return; }
      { char pin[5]={0}; memcpy(pin,f.data,4); if(memcmp(jbc_ph_pin,pin,5)!=0){memcpy(jbc_ph_pin,pin,5);changed=true;}
        bool configured=false;for(uint8_t i=0;i<4;++i)if(f.data[i]!=0&&f.data[i]!=' '){configured=true;break;}
        const bool old=(jbc_ph_station_flags&0x20UL)!=0;if(old!=configured){if(configured)jbc_ph_station_flags|=0x20UL;else jbc_ph_station_flags&=~0x20UL;changed=true;} }
      station_flag(0x00000010UL); break;
    case JBC_CMD_BEEP_PH:
      if (f.len != 1) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; }
      { const bool on=f.data[0]!=0;if(jbc_ph_beep!=on){jbc_ph_beep=on;changed=true;} const bool old=(jbc_ph_station_flags&0x80UL)!=0;
        if(old!=on){if(on)jbc_ph_station_flags|=0x80UL;else jbc_ph_station_flags&=~0x80UL;changed=true;} }
      station_flag(0x00000040UL); break;
    case JBC_CMD_REMOTE_MODE_PH:
      if (f.len != 1) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; }
      { const bool on=f.data[0]!=0; if(jbc_ph_remote_mode!=on){jbc_ph_remote_mode=on;changed=true;}
        const bool old=(jbc_ph_station_flags&0x00020000UL)!=0;
        if(old!=on){if(on)jbc_ph_station_flags|=0x00020000UL;else jbc_ph_station_flags&=~0x00020000UL;changed=true;} }
      station_flag(0x00010000UL); break;
    case JBC_CMD_CONNECT_STATUS_PH: {
      if (!f.len) { jbc_note_decode_error(f, 1, JBC_DECODE_LEN_OPEN); return; }
      uint8_t start=0;for(uint8_t i=0;i<f.len;++i)if(f.data[i]==(uint8_t)':'){start=(uint8_t)(i+1);break;}
      char mode=0;for(uint8_t i=start;i<f.len;++i){const uint8_t c=f.data[i];if(c!=' '&&c!='\t'&&c!='\r'&&c!='\n'){mode=(char)c;break;}}
      const bool control=mode=='C'||mode=='c';const bool old=(jbc_ph_station_flags&0x200UL)!=0;
      if(old!=control){if(control)jbc_ph_station_flags|=0x200UL;else jbc_ph_station_flags&=~0x200UL;changed=true;}
      station_flag(0x00000100UL); break;
    }
    case JBC_CMD_ROBOT_CONFIG_PH:
      if (f.len != 7) { jbc_note_decode_error(f, f.len != 7 ? (uint8_t)7 : JBC_DECODE_LEN_UNKNOWN, f.len != 7 ? (uint8_t)7 : JBC_DECODE_LEN_UNKNOWN); return; }
      if(memcmp(jbc_ph_robot_config,f.data,7)!=0){memcpy(jbc_ph_robot_config,f.data,7);changed=true;}
      station_flag(0x00000400UL); break;
    case JBC_CMD_ROBOT_STATUS_PH:
      if (f.len != 1) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; }
      {const bool on=f.data[0]=='C'||f.data[0]=='c';const bool old=(jbc_ph_station_flags&0x1000UL)!=0;
       if(old!=on){if(on)jbc_ph_station_flags|=0x1000UL;else jbc_ph_station_flags&=~0x1000UL;changed=true;}}
      station_flag(0x00000800UL); break;
    case JBC_CMD_COUNTER_PLUG_PH:
    case JBC_CMD_COUNTER_PLUG_PARTIAL_PH: {
      if (f.len != 4 || context >= JBC_MAX_PORTS) { jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN); return; }
      JbcPortState& ps=jbc_ports[context];const uint32_t v=get_u32_le(f.data);
      uint32_t& dst=f.command==JBC_CMD_COUNTER_PLUG_PH?ps.ph_counter_plug_min:ps.ph_partial_plug_min;
      if(dst!=v){dst=v;changed=true;} port_flag(ps,f.command==JBC_CMD_COUNTER_PLUG_PH?0x0020U:0x0100U); break;
    }
    case JBC_CMD_COUNTER_WORK_PH:
    case JBC_CMD_COUNTER_WORK_PARTIAL_PH: {
      if (f.len != 12 || context >= JBC_MAX_PORTS) { jbc_note_decode_error(f, f.len != 12 ? (uint8_t)12 : JBC_DECODE_LEN_UNKNOWN, f.len != 12 ? (uint8_t)12 : JBC_DECODE_LEN_UNKNOWN); return; }
      JbcPortState& ps=jbc_ports[context];const uint32_t a=get_u32_le(f.data),b=get_u32_le(f.data+4),c=get_u32_le(f.data+8);
      uint32_t *da,*db,*dc;uint16_t flag;
      if(f.command==JBC_CMD_COUNTER_WORK_PH){da=&ps.ph_counter_work_min_power;db=&ps.ph_counter_work_min_temp;dc=&ps.ph_counter_work_min_profile;flag=0x0040U;}
      else{da=&ps.ph_partial_work_min_power;db=&ps.ph_partial_work_min_temp;dc=&ps.ph_partial_work_min_profile;flag=0x0200U;}
      if(*da!=a){*da=a;changed=true;}if(*db!=b){*db=b;changed=true;}if(*dc!=c){*dc=c;changed=true;}port_flag(ps,flag);break;
    }
    case JBC_CMD_COUNTER_WORK_CYCLES_PH:
    case JBC_CMD_COUNTER_WORK_CYCLES_PARTIAL_PH: {
      if (f.len != 12 || context >= JBC_MAX_PORTS) { jbc_note_decode_error(f, f.len != 12 ? (uint8_t)12 : JBC_DECODE_LEN_UNKNOWN, f.len != 12 ? (uint8_t)12 : JBC_DECODE_LEN_UNKNOWN); return; }
      JbcPortState& ps=jbc_ports[context];const uint32_t a=get_u32_le(f.data),b=get_u32_le(f.data+4),c=get_u32_le(f.data+8);
      uint32_t *da,*db,*dc;uint16_t flag;
      if(f.command==JBC_CMD_COUNTER_WORK_CYCLES_PH){da=&ps.ph_counter_work_cycles_power;db=&ps.ph_counter_work_cycles_temp;dc=&ps.ph_counter_work_cycles_profile;flag=0x0080U;}
      else{da=&ps.ph_partial_work_cycles_power;db=&ps.ph_partial_work_cycles_temp;dc=&ps.ph_partial_work_cycles_profile;flag=0x0400U;}
      if(*da!=a){*da=a;changed=true;}if(*db!=b){*db=b;changed=true;}if(*dc!=c){*dc=c;changed=true;}port_flag(ps,flag);break;
    }
    default:
      return;
  }
  jbc_initial_low_mark_success(f, context);
  if (changed) { mark_fast_changed(); recompute_work_masks(); }
}

static uint8_t jbc_station_error_command() {
  const JbcProtocol cp = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
  if (cp == JBC_PROTO_01) return jbc_station_kind == JBC_STATION_SOLD ? JBC_CMD_STATION_ERROR_STD : 0;
  if (cp != JBC_PROTO_02) return 0;
  switch (jbc_station_kind) {
    case JBC_STATION_SOLD:
    case JBC_STATION_HA:
    case JBC_STATION_PH: return JBC_CMD_STATION_ERROR_STD;
    case JBC_STATION_SF:
    case JBC_STATION_FE: return JBC_CMD_STATION_ERROR_SF_FE;
    default: return 0; // CL has no periodic station-error read in the original DLL updater.
  }
}

static bool jbc_send_station_error() {
  const uint8_t command = jbc_station_error_command();
  if (!command || jbc_frame_protocol == JBC_PROTO_UNKNOWN) return false;
  if (jbc_frame_protocol == JBC_PROTO_01) {
    return jbc_send_frame(JBC_PROTO_01, jbc_host_addr, jbc_station_addr,
                          0, command, nullptr, 0);
  }
  const uint8_t fid = next_fid();
  return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr,
                        fid, command, nullptr, 0);
}

// Shared DLE decoder. The logical frame is classified as P01/P02 from the
// length field while protocol detection is still in progress.
enum RxState : uint8_t { RX_WAIT_DLE, RX_WAIT_STX, RX_FRAME, RX_AFTER_DLE };
static RxState rx_state = RX_WAIT_DLE;
static uint8_t rx_logical[JBC_MAX_LOGICAL_FRAME];
static size_t rx_logical_len = 0;

static void normalize_model_name(const char* model, char* out, size_t out_len) {
  if (!out || !out_len) return;
  size_t n = 0;
  if (model) {
    for (const char* p = model; *p && n + 1 < out_len; ++p) {
      if (*p == '/' || *p == ' ' || *p == '-') continue;
      out[n++] = (char)toupper((unsigned char)*p);
    }
  }
  out[n] = 0;
}

struct JbcModelInfo { const char* name; uint8_t ports; JbcStationKind kind; };
static const JbcModelInfo JBC_MODEL_TABLE[] = {
  {"CA",1,JBC_STATION_SOLD},{"CDCF",1,JBC_STATION_SOLD},{"CDN",1,JBC_STATION_SOLD},
  {"CP",1,JBC_STATION_SOLD},{"CSCV",1,JBC_STATION_SOLD},{"CDE",1,JBC_STATION_SOLD},
  {"CFE",1,JBC_STATION_SOLD},{"CAE",1,JBC_STATION_SOLD},{"CPE",1,JBC_STATION_SOLD},{"CSVE",1,JBC_STATION_SOLD},
  {"DD",2,JBC_STATION_SOLD},{"DDE",2,JBC_STATION_SOLD},{"DDR",2,JBC_STATION_SOLD},
  {"DI",1,JBC_STATION_SOLD},{"DM",4,JBC_STATION_SOLD},{"DME",4,JBC_STATION_SOLD},
  {"HD",1,JBC_STATION_SOLD},{"HDE",1,JBC_STATION_SOLD},{"HDR",1,JBC_STATION_SOLD},
  {"LC",1,JBC_STATION_SOLD},{"NA",2,JBC_STATION_SOLD},{"NAE",2,JBC_STATION_SOLD},
  {"PSE",4,JBC_STATION_SOLD},{"SM",1,JBC_STATION_SOLD},{"WS",1,JBC_STATION_SOLD},
  {"ALE",1,JBC_STATION_SOLD},
  {"JT",1,JBC_STATION_HA},{"JTSE",1,JBC_STATION_HA},
  {"SF",1,JBC_STATION_SF},
  {"F1",1,JBC_STATION_FE},{"F2W",2,JBC_STATION_FE},{"F2",2,JBC_STATION_FE},{"F4W",4,JBC_STATION_FE},
  {"PH",1,JBC_STATION_PH},{"PHBE",1,JBC_STATION_PH},{"PHNE",1,JBC_STATION_PH},{"PHSE",1,JBC_STATION_PH},{"PHXL",1,JBC_STATION_PH},
  {"CLM",1,JBC_STATION_CL},
  {"CLMU",1,JBC_STATION_CL},
};

static const JbcModelInfo* model_info(const char* model) {
  char m[24];
  normalize_model_name(model, m, sizeof(m));
  for (const JbcModelInfo& e : JBC_MODEL_TABLE) if (!strcmp(m, e.name)) return &e;
  return nullptr;
}

static uint8_t model_port_count(const char* model) {
  const JbcModelInfo* info = model_info(model);
  return info ? info->ports : 0;
}

static JbcStationKind model_station_kind(const char* model) {
  const JbcModelInfo* info = model_info(model);
  return info ? info->kind : JBC_STATION_UNKNOWN;
}

static void parse_model_string(const char* raw) {
  if (!raw || !*raw) return;
  strncpy(jbc_model_raw, raw, sizeof(jbc_model_raw) - 1);
  jbc_model_raw[sizeof(jbc_model_raw) - 1] = 0;
  char tmp[sizeof(jbc_model_raw)];
  strncpy(tmp, raw, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = 0;
  char* save = nullptr;
  char* base = strtok_r(tmp, "_", &save);
  char* type = strtok_r(nullptr, "_", &save);
  char* ver = strtok_r(nullptr, "_", &save);
  if (base) {
    while (*base == ' ') ++base;
    if (strstr(base, "CDB")) base = (char*)"CD/CF";
    strncpy(jbc_model, base, sizeof(jbc_model) - 1); jbc_model[sizeof(jbc_model) - 1] = 0;
  }
  if (type && *type) {
    while (*type == ' ') ++type;
    strncpy(jbc_model_type, type, sizeof(jbc_model_type) - 1); jbc_model_type[sizeof(jbc_model_type) - 1] = 0;
  } else strcpy(jbc_model_type, "-");
  jbc_model_version = ver ? (uint16_t)atoi(ver) : 0;
  jbc_station_kind = model_station_kind(jbc_model);
  const uint8_t known = model_port_count(jbc_model);
  if (known) {
    jbc_port_count = known;
    jbc_port_count_from_model = true;
  } else {
    jbc_port_count = 0;
    jbc_port_count_from_model = false;
  }
}

static void parse_firmware_string(const uint8_t* data, uint8_t len) {
  char tmp[160];
  const size_t n = min((size_t)len, sizeof(tmp) - 1);
  memcpy(tmp, data, n); tmp[n] = 0;
  char* parts[8] = {nullptr};
  uint8_t count = 0;
  char* save = nullptr;
  for (char* tok = strtok_r(tmp, ":", &save); tok && count < 8; tok = strtok_r(nullptr, ":", &save)) {
    while (*tok == ' ') ++tok;
    char* end = tok + strlen(tok);
    while (end > tok && end[-1] == ' ') *--end = 0;
    parts[count++] = tok;
  }
  if (count > 0) {
    strncpy(jbc_protocol_text, parts[0], sizeof(jbc_protocol_text) - 1);
    jbc_protocol_text[sizeof(jbc_protocol_text) - 1] = 0;
    if (!strcmp(parts[0], "01") || !strcmp(parts[0], "1")) jbc_command_protocol = JBC_PROTO_01;
    else if (!strcmp(parts[0], "02") || !strcmp(parts[0], "2")) jbc_command_protocol = JBC_PROTO_02;
  }
  const char* model_token = count > 1 ? parts[1] : nullptr;
  // JBC discovery treats a fifth non-"B" field as an alternate model string.
  if (count == 5 && parts[4] && strcmp(parts[4], "B") != 0 && *parts[4]) model_token = parts[4];
  if (model_token) parse_model_string(model_token);
  if (count > 2) { strncpy(jbc_sw_version, parts[2], sizeof(jbc_sw_version)-1); jbc_sw_version[sizeof(jbc_sw_version)-1]=0; }
  if (count > 3) { strncpy(jbc_hw_version, parts[3], sizeof(jbc_hw_version)-1); jbc_hw_version[sizeof(jbc_hw_version)-1]=0; }
  if (jbc_command_protocol == JBC_PROTO_UNKNOWN) jbc_command_protocol = jbc_frame_protocol;
}

static void parse_secondary_firmware_string(const uint8_t* data, uint8_t len) {
  char tmp[160];
  const size_t n=min((size_t)len,sizeof(tmp)-1); memcpy(tmp,data,n); tmp[n]=0;
  char* parts[6]={nullptr}; uint8_t count=0; char* save=nullptr;
  for(char* tok=strtok_r(tmp,":",&save);tok&&count<6;tok=strtok_r(nullptr,":",&save)){while(*tok==' ')++tok;char* end=tok+strlen(tok);while(end>tok&&end[-1]==' ')*--end=0;parts[count++]=tok;}
  if(count<3||!parts[1]||!parts[2]) return;
  char model[32];strncpy(model,parts[1],sizeof(model)-1);model[sizeof(model)-1]=0;char* us=strchr(model,'_');if(us)*us=0;
  if(strcmp(model,"IMX")!=0) return;
  if(strcmp(jbc_sw_version,parts[2])!=0){strncpy(jbc_sw_version,parts[2],sizeof(jbc_sw_version)-1);jbc_sw_version[sizeof(jbc_sw_version)-1]=0;mark_fast_changed();}
}

static void recompute_work_masks() {
  uint8_t new_work = 0;
  uint8_t new_stand = 0;
  const uint32_t now = millis();
  for (uint8_t i = 0; i < JBC_MAX_PORTS; ++i) {
    JbcPortState& p = jbc_ports[i];
    if (!p.valid || (uint32_t)(now - p.last_ms) > 2500UL) continue;

    bool working = false;
    bool standish = false;
    switch (jbc_station_kind) {
      case JBC_STATION_SOLD:
        // Original JBC Connect FEAutoWorking rule: a soldering port works only
        // with a connected tool and while Stand/Sleep/Hibernation/Extractor are all OFF.
        standish = p.stand || p.sleep || p.hibernation || p.extractor;
        working = p.tool != 0 && !standish;
        break;
      case JBC_STATION_HA:
        // Hot-air stations (JT/JTSE): JBC Connect keys extraction exclusively
        // from HeaterStatus, not merely from tool presence or Stand.
        standish = p.stand;
        working = p.tool != 0 && p.heater;
        break;
      case JBC_STATION_PH:
        // Preheaters have no soldering-tool ID. Their original auto-working
        // criterion is simply HeaterStatus == ON.
        working = p.heater;
        break;
      case JBC_STATION_SF:
      case JBC_STATION_FE:
      case JBC_STATION_CL:
      case JBC_STATION_UNKNOWN:
      default:
        // The original FEAutoWorking implementation does not use these station
        // classes as automatic fume-extractor work sources.
        break;
    }

    if (standish) new_stand |= (uint8_t)(1U << i);
    if (working) new_work |= (uint8_t)(1U << i);
  }
  if (new_work != work_mask || new_stand != stand_mask) {
    work_mask = new_work;
    stand_mask = new_stand;
    mark_fast_changed();
  }
}


static bool jbc_send_fe_read(uint8_t command, uint8_t port, uint8_t intake) {
  if (jbc_station_kind != JBC_STATION_FE || jbc_link_state != JBC_LINK_ACTIVE ||
      jbc_frame_protocol != JBC_PROTO_02) return false;
  const JbcProtocol cp = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
  if (cp != JBC_PROTO_02) return false;

  uint8_t data[2] = {0, 0};
  uint8_t len = 0;
  uint8_t context = 0xFF;
  switch (command) {
    case JBC_CMD_INTAKE_ACTIVATION_FE:
    case JBC_CMD_SUCTION_DELAY_FE:
    case JBC_CMD_TIME_TO_STOP_SUCTION_FE:
      if (port >= JBC_MAX_PORTS || intake > 1) return false;
      data[0] = port; data[1] = intake; len = 2;
      context = (uint8_t)((port & 0x0FU) | ((intake & 1U) << 4));
      break;
    case JBC_CMD_STAND_INTAKES_FE:
    case JBC_CMD_ACTIVATION_PEDAL_FE:
    case JBC_CMD_PEDAL_MODE_FE:
    case JBC_CMD_CONNECTED_PEDAL_FE:
      if (port >= JBC_MAX_PORTS) return false;
      data[0] = port; len = 1; context = port;
      break;
    case JBC_CMD_FLOW_FE:
    case JBC_CMD_SPEED_FE:
    case JBC_CMD_SELECT_FLOW_FE:
    case JBC_CMD_FILTER_STATUS_FE:
    case JBC_CMD_PIN_FE:
    case JBC_CMD_BEEP_FE:
    case JBC_CMD_CONTINUOUS_SUCTION_FE:
    case JBC_CMD_COUNTERS_FE:
    case JBC_CMD_COUNTERS_PARTIAL_FE:
    case JBC_CMD_CONNECT_STATUS_FE:
    case JBC_CMD_ROBOT_CONFIG_FE:
    case JBC_CMD_ROBOT_STATUS_FE:
      break;
    default:
      return false;
  }
  return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr, next_fid(),
                        command, len ? data : nullptr, len, context);
}

static bool is_fe_read_frame(const JbcFrame& f) {
  if (jbc_station_kind != JBC_STATION_FE || f.frame_protocol != JBC_PROTO_02) return false;
  switch (f.command) {
    case JBC_CMD_FLOW_FE:
    case JBC_CMD_SPEED_FE:
    case JBC_CMD_SELECT_FLOW_FE:
    case JBC_CMD_STAND_INTAKES_FE:
    case JBC_CMD_INTAKE_ACTIVATION_FE:
    case JBC_CMD_SUCTION_DELAY_FE:
    case JBC_CMD_TIME_TO_STOP_SUCTION_FE:
    case JBC_CMD_ACTIVATION_PEDAL_FE:
    case JBC_CMD_PEDAL_MODE_FE:
    case JBC_CMD_FILTER_STATUS_FE:
    case JBC_CMD_CONNECTED_PEDAL_FE:
    case JBC_CMD_PIN_FE:
    case JBC_CMD_BEEP_FE:
    case JBC_CMD_CONTINUOUS_SUCTION_FE:
    case JBC_CMD_COUNTERS_FE:
    case JBC_CMD_COUNTERS_PARTIAL_FE:
    case JBC_CMD_CONNECT_STATUS_FE:
    case JBC_CMD_ROBOT_CONFIG_FE:
    case JBC_CMD_ROBOT_STATUS_FE:
      return true;
    default:
      return false;
  }
}

static void decode_fe_read(const JbcFrame& f) {
  const uint8_t context = pending_port_for_frame(f);
  bool changed = false;
  auto station_valid = [&](uint16_t bit) {
    if (!(jbc_fe_station_flags & bit)) { jbc_fe_station_flags |= bit; changed = true; }
  };
  auto port_valid = [&](JbcPortState& ps, uint16_t bit) {
    if (!(ps.fe_flags & bit)) { ps.fe_flags |= bit; changed = true; }
  };
  switch (f.command) {
    case JBC_CMD_FLOW_FE:
    case JBC_CMD_SPEED_FE:
    case JBC_CMD_SELECT_FLOW_FE:
    case JBC_CMD_FILTER_STATUS_FE: {
      if (f.len != 2) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
      const uint16_t v = get_u16_le(f.data); uint16_t* dst = nullptr; uint16_t bit = 0;
      if (f.command == JBC_CMD_FLOW_FE) { dst=&jbc_fe_flow_x_mil; bit=0x0001U; }
      else if (f.command == JBC_CMD_SPEED_FE) { dst=&jbc_fe_speed_rpm; bit=0x0002U; }
      else if (f.command == JBC_CMD_SELECT_FLOW_FE) { dst=&jbc_fe_selected_flow_x_mil; bit=0x0004U; }
      else { dst=&jbc_fe_filter_status; bit=0x0008U; }
      if (*dst != v) { *dst = v; changed = true; }
      if (!(jbc_fe_service_flags & bit)) { jbc_fe_service_flags |= bit; changed = true; }
      break;
    }
    case JBC_CMD_STAND_INTAKES_FE: {
      if (f.len != 2 || context >= JBC_MAX_PORTS) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
      JbcPortState& ps=jbc_ports[context];
      if(ps.fe_stand_intakes!=f.data[0]){ps.fe_stand_intakes=f.data[0];changed=true;}
      if(!(ps.fe_service_flags&0x0001U)){ps.fe_service_flags|=0x0001U;changed=true;}
      break;
    }
    case JBC_CMD_SUCTION_DELAY_FE: {
      if (f.len != 4) { jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN); return; }
      const uint8_t port=context&0x0FU, intake=(context>>4)&1U; if(port>=JBC_MAX_PORTS){jbc_note_decode_error(f);return;}
      JbcPortState& ps=jbc_ports[port]; const uint16_t v=get_u16_le(f.data);
      uint16_t& dst=intake?ps.fe_suction_delay_stand:ps.fe_suction_delay_work; const uint16_t bit=intake?0x0004U:0x0002U;
      if(dst!=v){dst=v;changed=true;} if(!(ps.fe_service_flags&bit)){ps.fe_service_flags|=bit;changed=true;}
      break;
    }
    case JBC_CMD_CONNECTED_PEDAL_FE: {
      if (f.len != 2 || context >= JBC_MAX_PORTS) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
      JbcPortState& ps=jbc_ports[context]; const uint8_t on=f.data[0]!=0?1:0;
      if(ps.fe_pedal_connected!=on){ps.fe_pedal_connected=on;changed=true;}
      const uint16_t old=ps.fe_service_flags; ps.fe_service_flags|=0x0008U; if(on)ps.fe_service_flags|=0x0010U;else ps.fe_service_flags&=(uint16_t)~0x0010U;
      if(old!=ps.fe_service_flags) changed=true;
      break;
    }
    case JBC_CMD_PIN_FE: {
      if (f.len != 4) { jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN); return; } char pin[5]={0}; memcpy(pin,f.data,4);
      if(memcmp(jbc_fe_pin,pin,5)!=0){memcpy(jbc_fe_pin,pin,5);changed=true;}
      bool configured=false;for(uint8_t i=0;i<4;++i)if(f.data[i]!=0&&f.data[i]!=0xFF&&f.data[i]!=(uint8_t)' '){configured=true;break;}
      const uint16_t old=jbc_fe_service_flags; jbc_fe_service_flags|=0x0010U; if(configured)jbc_fe_service_flags|=0x0020U;else jbc_fe_service_flags&=(uint16_t)~0x0020U;
      if(old!=jbc_fe_service_flags) changed=true; break;
    }
    case JBC_CMD_BEEP_FE: {
      if (f.len != 1) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; } const bool on=f.data[0]!=0; if(jbc_fe_beep!=on){jbc_fe_beep=on;changed=true;}
      const uint16_t old=jbc_fe_service_flags; jbc_fe_service_flags|=0x0040U; if(on)jbc_fe_service_flags|=0x0080U;else jbc_fe_service_flags&=(uint16_t)~0x0080U;
      if(old!=jbc_fe_service_flags) changed=true; break;
    }
    case JBC_CMD_INTAKE_ACTIVATION_FE: {
      if (f.len != 3) { jbc_note_decode_error(f, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN); return; }
      const uint8_t port = context & 0x0FU, intake = (context >> 4) & 1U;
      if (port >= JBC_MAX_PORTS) return;
      JbcPortState& ps = jbc_ports[port];
      const bool on = f.data[0] != 0;
      const uint16_t valid = intake ? 0x0004U : 0x0001U;
      const uint16_t state = intake ? 0x0008U : 0x0002U;
      const bool old = (ps.fe_flags & state) != 0;
      if (old != on) {
        if (on) ps.fe_flags |= state; else ps.fe_flags &= (uint16_t)~state;
        changed = true;
      }
      port_valid(ps, valid);
      break;
    }
    case JBC_CMD_TIME_TO_STOP_SUCTION_FE: {
      if (f.len != 4) { jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN); return; }
      const uint8_t port = context & 0x0FU, intake = (context >> 4) & 1U;
      if (port >= JBC_MAX_PORTS) return;
      JbcPortState& ps = jbc_ports[port];
      const uint16_t v = get_u16_le(f.data);
      uint16_t& dst = intake ? ps.fe_time_to_stop_stand : ps.fe_time_to_stop_work;
      if (dst != v) { dst = v; changed = true; }
      port_valid(ps, intake ? 0x0020U : 0x0010U);
      break;
    }
    case JBC_CMD_ACTIVATION_PEDAL_FE: {
      if (f.len != 2 || context >= JBC_MAX_PORTS) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
      if (f.data[0] > 1) return; // ReceiveFrame02_FE accepts only HOLD_DOWN/PULSE.
      JbcPortState& ps = jbc_ports[context];
      if (ps.fe_pedal_action != f.data[0]) { ps.fe_pedal_action = f.data[0]; changed = true; }
      port_valid(ps, 0x0040U);
      break;
    }
    case JBC_CMD_PEDAL_MODE_FE: {
      if (f.len != 2 || context >= JBC_MAX_PORTS) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
      JbcPortState& ps = jbc_ports[context];
      if (ps.fe_pedal_mode != f.data[0]) { ps.fe_pedal_mode = f.data[0]; changed = true; }
      port_valid(ps, 0x0080U);
      break;
    }
    case JBC_CMD_CONTINUOUS_SUCTION_FE: {
      if (f.len != 1) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; }
      const bool on = f.data[0] != 0;
      const bool old = (jbc_fe_station_flags & 0x0002U) != 0;
      if (old != on) {
        if (on) jbc_fe_station_flags |= 0x0002U; else jbc_fe_station_flags &= (uint16_t)~0x0002U;
        changed = true;
      }
      station_valid(0x0001U);
      break;
    }
    case JBC_CMD_COUNTERS_FE:
    case JBC_CMD_COUNTERS_PARTIAL_FE: {
      uint8_t count = jbc_port_count;
      if (count < 1 || count > JBC_MAX_PORTS) count = JBC_MAX_PORTS;
      if (f.len != (uint8_t)(20U * count)) { jbc_note_decode_error(f); return; }
      const bool partial = f.command == JBC_CMD_COUNTERS_PARTIAL_FE;
      for (uint8_t port = 0; port < count; ++port) {
        JbcPortState& ps = jbc_ports[port];
        const uint32_t plug = get_u32_le(f.data + port * 4U + count * 4U * 0U);
        const uint32_t idle = get_u32_le(f.data + port * 4U + count * 4U * 1U);
        const uint32_t work = get_u32_le(f.data + port * 4U + count * 4U * 2U);
        const uint32_t stand = get_u32_le(f.data + port * 4U + count * 4U * 3U);
        const uint32_t cycles = get_u32_le(f.data + port * 4U + count * 4U * 4U);
        uint32_t *d0,*d1,*d2,*d3,*d4;
        if (partial) {
          d0=&ps.fe_partial_plug_min; d1=&ps.fe_partial_idle_min; d2=&ps.fe_partial_work_intake_min;
          d3=&ps.fe_partial_stand_intake_min; d4=&ps.fe_partial_work_cycles;
        } else {
          d0=&ps.fe_counter_plug_min; d1=&ps.fe_counter_idle_min; d2=&ps.fe_counter_work_intake_min;
          d3=&ps.fe_counter_stand_intake_min; d4=&ps.fe_counter_work_cycles;
        }
        if(*d0!=plug){*d0=plug;changed=true;} if(*d1!=idle){*d1=idle;changed=true;}
        if(*d2!=work){*d2=work;changed=true;} if(*d3!=stand){*d3=stand;changed=true;}
        if(*d4!=cycles){*d4=cycles;changed=true;}
        port_valid(ps, partial ? 0x0200U : 0x0100U);
      }
      break;
    }
    case JBC_CMD_CONNECT_STATUS_FE: {
      if (!f.len) { jbc_note_decode_error(f, 1, JBC_DECODE_LEN_OPEN); return; }
      uint8_t start=0;
      for(uint8_t i=0;i<f.len;++i) if(f.data[i]==(uint8_t)':'){start=(uint8_t)(i+1);break;}
      char mode=0;
      for(uint8_t i=start;i<f.len;++i){uint8_t c=f.data[i];if(c!=' '&&c!='\t'&&c!='\r'&&c!='\n'){mode=(char)c;break;}}
      const bool control = mode=='C' || mode=='c';
      const bool old = (jbc_fe_station_flags & 0x0008U) != 0;
      if(old!=control){
        if(control)jbc_fe_station_flags|=0x0008U;else jbc_fe_station_flags&=(uint16_t)~0x0008U;
        changed=true;
      }
      station_valid(0x0004U);
      break;
    }
    case JBC_CMD_ROBOT_CONFIG_FE:
      if (f.len != 7) { jbc_note_decode_error(f, f.len != 7 ? (uint8_t)7 : JBC_DECODE_LEN_UNKNOWN, f.len != 7 ? (uint8_t)7 : JBC_DECODE_LEN_UNKNOWN); return; }
      if (memcmp(jbc_fe_robot_config, f.data, 7) != 0) { memcpy(jbc_fe_robot_config, f.data, 7); changed = true; }
      station_valid(0x0010U);
      break;
    case JBC_CMD_ROBOT_STATUS_FE: {
      if (f.len != 1) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; }
      const bool on = f.data[0]=='C' || f.data[0]=='c';
      const bool old = (jbc_fe_station_flags & 0x0040U) != 0;
      if(old!=on){
        if(on)jbc_fe_station_flags|=0x0040U;else jbc_fe_station_flags&=(uint16_t)~0x0040U;
        changed=true;
      }
      station_valid(0x0020U);
      break;
    }
    default:
      return;
  }
  jbc_initial_low_mark_success(f, context);
  if (f.frame_protocol == JBC_PROTO_02) pending_by_fid[f.fid] = PendingRequest();
  if (changed) mark_fast_changed();
}

static bool jbc_send_sf_read(uint8_t command, uint8_t context) {
  if (jbc_station_kind != JBC_STATION_SF || jbc_link_state != JBC_LINK_ACTIVE ||
      jbc_frame_protocol != JBC_PROTO_02) return false;
  const JbcProtocol cp = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
  if (cp != JBC_PROTO_02) return false;

  uint8_t data[1] = {0};
  uint8_t len = 0;
  uint8_t pending_context = context;
  switch (command) {
    case JBC_CMD_PROGRAM_SF:
      if (context < 1 || context > JBC_SF_PROGRAM_COUNT) return false;
      data[0] = context; len = 1;
      break;
    case JBC_CMD_PROGRAM_LIST_SF:
    case JBC_CMD_INFO_PORT:
    case JBC_CMD_SPEED_SF:
    case JBC_CMD_LENGTH_SF:
    case JBC_CMD_FEEDING_SF:
    case JBC_CMD_PIN_SF:
    case JBC_CMD_BEEP_SF:
    case JBC_CMD_LENGTH_UNIT_SF:
    case JBC_CMD_TOOL_ENABLED_SF:
    case JBC_CMD_PIN_ENABLED_SF:
    case JBC_CMD_COUNTERS_SF:
    case JBC_CMD_COUNTERS_PARTIAL_SF:
    case JBC_CMD_CONNECT_STATUS_SF:
    case JBC_CMD_ROBOT_CONFIG_SF:
    case JBC_CMD_ROBOT_STATUS_SF:
      if (pending_context == 0xFF) pending_context = 0;
      break;
    default:
      return false;
  }
  return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr, next_fid(),
                        command, len ? data : nullptr, len, pending_context);
}

static bool is_sf_read_frame(const JbcFrame& f) {
  if (jbc_station_kind != JBC_STATION_SF || f.frame_protocol != JBC_PROTO_02) return false;
  switch (f.command) {
    case JBC_CMD_PROGRAM_SF:
    case JBC_CMD_PROGRAM_LIST_SF:
    case JBC_CMD_SPEED_SF:
    case JBC_CMD_LENGTH_SF:
    case JBC_CMD_FEEDING_SF:
    case JBC_CMD_PIN_SF:
    case JBC_CMD_BEEP_SF:
    case JBC_CMD_LENGTH_UNIT_SF:
    case JBC_CMD_TOOL_ENABLED_SF:
    case JBC_CMD_PIN_ENABLED_SF:
    case JBC_CMD_COUNTERS_SF:
    case JBC_CMD_COUNTERS_PARTIAL_SF:
    case JBC_CMD_CONNECT_STATUS_SF:
    case JBC_CMD_ROBOT_CONFIG_SF:
    case JBC_CMD_ROBOT_STATUS_SF:
      return true;
    default:
      return false;
  }
}

static void decode_sf_read(const JbcFrame& f) {
  const uint8_t context = pending_port_for_frame(f);
  JbcPortState& ps = jbc_ports[0]; // SF has one port in the JBC model table/DLL.
  bool changed = false;
  auto station_valid = [&](uint16_t bit) {
    if (!(jbc_sf_station_flags & bit)) { jbc_sf_station_flags |= bit; changed = true; }
  };
  auto port_valid = [&](uint16_t bit) {
    if (!(ps.sf_flags & bit)) { ps.sf_flags |= bit; changed = true; }
  };
  switch (f.command) {
    case JBC_CMD_PROGRAM_SF: {
      if (f.len != 21 || f.data[0] < 1 || f.data[0] > JBC_SF_PROGRAM_COUNT) { jbc_note_decode_error(f, f.len != 21 ? (uint8_t)21 : JBC_DECODE_LEN_UNKNOWN, f.len != 21 ? (uint8_t)21 : JBC_DECODE_LEN_UNKNOWN); return; }
      const uint8_t idx = (uint8_t)(f.data[0] - 1U);
      // The response carries its own program number; context is only used to
      // match/release the transaction and may be ignored if a station replies out of order.
      (void)context;
      SfProgramState next;
      next.flags = 0x01U;
      memcpy(next.name, f.data + 1, 8); next.name[8] = 0;
      for (int i=7; i>=0 && (next.name[i] == ' ' || next.name[i] == '\0' || next.name[i] == '\r' || next.name[i] == '\n' || next.name[i] == '\t'); --i) next.name[i] = 0;
      next.length[0] = get_u16_le(f.data + 9);  next.speed[0] = get_u16_le(f.data + 11);
      next.length[1] = get_u16_le(f.data + 13); next.speed[1] = get_u16_le(f.data + 15);
      next.length[2] = get_u16_le(f.data + 17); next.speed[2] = get_u16_le(f.data + 19);
      const bool empty = !strcmp(next.name, "--------") && !next.length[0] && !next.speed[0] &&
                         !next.length[1] && !next.speed[1] && !next.length[2] && !next.speed[2];
      if (!empty) next.flags |= 0x02U;
      if (memcmp(&jbc_sf_programs[idx], &next, sizeof(next)) != 0) { jbc_sf_programs[idx] = next; changed = true; }
      break;
    }
    case JBC_CMD_PROGRAM_LIST_SF:
      if (f.len != JBC_SF_PROGRAM_COUNT) { jbc_note_decode_error(f); return; }
      if (memcmp(jbc_sf_program_list, f.data, JBC_SF_PROGRAM_COUNT) != 0) { memcpy(jbc_sf_program_list, f.data, JBC_SF_PROGRAM_COUNT); changed = true; }
      station_valid(0x1000U);
      break;
    case JBC_CMD_SPEED_SF: {
      if (f.len != 2) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
      const uint16_t v = get_u16_le(f.data);
      if (ps.sf_speed_tenth_mm_s != v) { ps.sf_speed_tenth_mm_s = v; changed = true; }
      port_valid(0x0001U);
      break;
    }
    case JBC_CMD_LENGTH_SF: {
      if (f.len != 2) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
      const uint16_t v = get_u16_le(f.data);
      if (ps.sf_length_tenth_mm != v) { ps.sf_length_tenth_mm = v; changed = true; }
      port_valid(0x0002U);
      break;
    }
    case JBC_CMD_FEEDING_SF: {
      if (f.len != 5) { jbc_note_decode_error(f, f.len != 5 ? (uint8_t)5 : JBC_DECODE_LEN_UNKNOWN, f.len != 5 ? (uint8_t)5 : JBC_DECODE_LEN_UNKNOWN); return; }
      const uint8_t state = f.data[0];
      const uint16_t raw = get_u16_le(f.data + 1);
      const uint8_t selected = f.data[3];
      // Mirror ReceiveFrame02_SF exactly, including its CurrentProgramStep=data[0].
      if (ps.sf_feeding_state != state) { ps.sf_feeding_state = state; changed = true; }
      if (ps.sf_feeding_value_raw != raw) { ps.sf_feeding_value_raw = raw; changed = true; }
      if (ps.sf_feeding_selected_program != selected) { ps.sf_feeding_selected_program = selected; changed = true; }
      if (ps.sf_current_program_step != f.data[0]) { ps.sf_current_program_step = f.data[0]; changed = true; }
      port_valid(0x0004U);
      break;
    }
    case JBC_CMD_PIN_SF: {
      if (f.len != 4) { jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN); return; }
      char pin[5] = {0}; bool configured = false;
      for (uint8_t i=0; i<4; ++i) { const uint8_t c=f.data[i]; pin[i]=(c>=0x20&&c<=0x7E)?(char)c:0; if(c!=0&&c!=0xFF&&c!=(uint8_t)' ') configured=true; }
      if (memcmp(jbc_sf_pin, pin, sizeof(jbc_sf_pin)) != 0) { memcpy(jbc_sf_pin, pin, sizeof(jbc_sf_pin)); changed = true; }
      const bool old_cfg = (jbc_sf_station_flags & 0x0002U) != 0;
      if (old_cfg != configured) { if (configured) jbc_sf_station_flags |= 0x0002U; else jbc_sf_station_flags &= (uint16_t)~0x0002U; changed=true; }
      station_valid(0x0001U);
      break;
    }
    case JBC_CMD_PIN_ENABLED_SF: {
      if (f.len != 1) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; }
      const bool on=f.data[0]!=0, old=(jbc_sf_station_flags&0x0008U)!=0;
      if(old!=on){if(on)jbc_sf_station_flags|=0x0008U;else jbc_sf_station_flags&=(uint16_t)~0x0008U;changed=true;}
      station_valid(0x0004U);
      break;
    }
    case JBC_CMD_BEEP_SF: {
      if (f.len != 1) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; }
      const bool on=f.data[0]!=0, old=(jbc_sf_station_flags&0x0020U)!=0;
      if(old!=on){if(on)jbc_sf_station_flags|=0x0020U;else jbc_sf_station_flags&=(uint16_t)~0x0020U;changed=true;}
      station_valid(0x0010U);
      break;
    }
    case JBC_CMD_LENGTH_UNIT_SF:
      if (f.len != 1) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; }
      if (jbc_sf_length_unit != f.data[0]) { jbc_sf_length_unit=f.data[0]; changed=true; }
      station_valid(0x0040U);
      break;
    case JBC_CMD_TOOL_ENABLED_SF: {
      if (f.len != 1) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; }
      const bool on=f.data[0]!=0, old=(ps.sf_flags&0x0010U)!=0;
      if(old!=on){if(on)ps.sf_flags|=0x0010U;else ps.sf_flags&=(uint16_t)~0x0010U;changed=true;}
      port_valid(0x0008U);
      break;
    }
    case JBC_CMD_COUNTERS_SF:
    case JBC_CMD_COUNTERS_PARTIAL_SF: {
      if (f.len != 20) { jbc_note_decode_error(f, f.len != 20 ? (uint8_t)20 : JBC_DECODE_LEN_UNKNOWN, f.len != 20 ? (uint8_t)20 : JBC_DECODE_LEN_UNKNOWN); return; }
      const bool partial = f.command == JBC_CMD_COUNTERS_PARTIAL_SF;
      const uint64_t tin = get_u64_le(f.data);
      const uint32_t plug = (uint32_t)((int32_t)get_u32_le(f.data + 8) * 60);
      const uint32_t work = (uint32_t)((int32_t)get_u32_le(f.data + 12) * 60);
      const uint32_t cycles = get_u32_le(f.data + 16);
      const uint32_t idle = plug - work;
      if (partial) {
        if(ps.sf_partial_tin_length!=tin){ps.sf_partial_tin_length=tin;changed=true;} if(ps.sf_partial_plug_min!=plug){ps.sf_partial_plug_min=plug;changed=true;}
        if(ps.sf_partial_work_min!=work){ps.sf_partial_work_min=work;changed=true;} if(ps.sf_partial_idle_min!=idle){ps.sf_partial_idle_min=idle;changed=true;}
        if(ps.sf_partial_work_cycles!=cycles){ps.sf_partial_work_cycles=cycles;changed=true;} port_valid(0x0040U);
      } else {
        if(ps.sf_counter_tin_length!=tin){ps.sf_counter_tin_length=tin;changed=true;} if(ps.sf_counter_plug_min!=plug){ps.sf_counter_plug_min=plug;changed=true;}
        if(ps.sf_counter_work_min!=work){ps.sf_counter_work_min=work;changed=true;} if(ps.sf_counter_idle_min!=idle){ps.sf_counter_idle_min=idle;changed=true;}
        if(ps.sf_counter_work_cycles!=cycles){ps.sf_counter_work_cycles=cycles;changed=true;} port_valid(0x0020U);
      }
      break;
    }
    case JBC_CMD_CONNECT_STATUS_SF: {
      if (!f.len) { jbc_note_decode_error(f, 1, JBC_DECODE_LEN_OPEN); return; }
      uint8_t start=0; for(uint8_t i=0;i<f.len;++i) if(f.data[i]==(uint8_t)':'){start=(uint8_t)(i+1);break;}
      char mode=0; for(uint8_t i=start;i<f.len;++i){const uint8_t c=f.data[i];if(c!=' '&&c!='\t'&&c!='\r'&&c!='\n'){mode=(char)c;break;}}
      const bool control=mode=='C'||mode=='c', old=(jbc_sf_station_flags&0x0100U)!=0;
      if(old!=control){if(control)jbc_sf_station_flags|=0x0100U;else jbc_sf_station_flags&=(uint16_t)~0x0100U;changed=true;}
      station_valid(0x0080U);
      break;
    }
    case JBC_CMD_ROBOT_CONFIG_SF:
      if (f.len != 7) { jbc_note_decode_error(f, f.len != 7 ? (uint8_t)7 : JBC_DECODE_LEN_UNKNOWN, f.len != 7 ? (uint8_t)7 : JBC_DECODE_LEN_UNKNOWN); return; }
      if (memcmp(jbc_sf_robot_config, f.data, 7) != 0) { memcpy(jbc_sf_robot_config, f.data, 7); changed=true; }
      station_valid(0x0200U);
      break;
    case JBC_CMD_ROBOT_STATUS_SF: {
      if (f.len != 1) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; }
      const bool on=f.data[0]=='C'||f.data[0]=='c', old=(jbc_sf_station_flags&0x0800U)!=0;
      if(old!=on){if(on)jbc_sf_station_flags|=0x0800U;else jbc_sf_station_flags&=(uint16_t)~0x0800U;changed=true;}
      station_valid(0x0400U);
      break;
    }
    default:
      return;
  }
  jbc_initial_low_mark_success(f, context);
  if (changed) mark_fast_changed();
}

static uint8_t pending_port_for_frame(const JbcFrame& f) {
  if (f.frame_protocol == JBC_PROTO_02) {
    PendingRequest& pending = pending_by_fid[f.fid];
    const uint8_t p = pending.port;
    pending = PendingRequest();
    return p;
  }
  const uint8_t p = p01_pending.port;
  p01_pending = PendingRequest();
  return p;
}

static void decode_info_port(const JbcFrame& f) {
  uint8_t port = pending_port_for_frame(f);
  const JbcProtocol command_protocol = jbc_command_protocol == JBC_PROTO_UNKNOWN ? f.frame_protocol : jbc_command_protocol;

  if (jbc_station_kind == JBC_STATION_FE) {
    // ReceiveFrame02_FE command 0x30 is station-wide: one suction level is
    // written into every PortData entry by the original DLL.
    if (command_protocol != JBC_PROTO_02 || f.len != 1) return;
    uint8_t count = jbc_port_count;
    if (count < 1 || count > JBC_MAX_PORTS) count = JBC_MAX_PORTS;
    bool changed = false;
    for (uint8_t i = 0; i < count; ++i) {
      JbcPortState& ps = jbc_ports[i];
      if (!ps.valid || ps.future_mode != f.data[0]) changed = true;
      ps.valid = true;
      ps.future_mode = f.data[0];
      ps.last_ms = millis();
    }
    if (jbc_highest_seen_port + 1U < count) jbc_highest_seen_port = (uint8_t)(count - 1U);
    if (changed) mark_fast_changed();
    return;
  }

  if (port >= JBC_MAX_PORTS && !(jbc_station_kind == JBC_STATION_SOLD && command_protocol == JBC_PROTO_02 && f.len == 15)) return;

  JbcPortState next;
  next.valid = true;
  next.last_ms = millis();

  switch (jbc_station_kind) {
    case JBC_STATION_SOLD: {
      if (command_protocol == JBC_PROTO_02) {
        if (f.len == 15) port = f.data[14]; // ALE form in the original DLL.
        if (port >= JBC_MAX_PORTS || (f.len != 12 && f.len != 14 && f.len != 15)) return;
        next.tool = f.data[0];
        next.error = f.data[1];
        next.temp = get_u16_le(f.data + 2);
        next.power_permille = get_u16_le(f.data + 6);
        const uint8_t status = f.data[10];
        if (f.len == 15 && !strcmp(jbc_model, "ALE")) {
          // ReceiveFrame02_SOLD ALE M_INF_PORT: byte11 bit0 motor ON, bit1
          // direction (0 ADD_TIN, 1 REMOVE_TIN). Keep it separate from tool status.
          next.sold_feeder_flags |= 0x0040U;
          next.sold_feeder_motor_on = (f.data[11] & 0x01U) ? 1 : 0;
          next.sold_feeder_motor_direction = (f.data[11] & 0x02U) ? 1 : 0;
        }
        next.stand = (status & 0x01) != 0;
        next.sleep = (status & 0x02) != 0;
        next.hibernation = (status & 0x04) != 0;
        next.extractor = (status & 0x08) != 0;
        next.desolder = (status & 0x10) != 0;
        // ReceiveFrame02_SOLD M_INF_PORT does NOT expose the same byte as
        // M_R_TOOLLASTSTATE: bits0..4 are live operational flags, while bit5
        // is EnabledPort inverted when Features.QST is present. Keep the true
        // 0x57 ToolLastStatus byte separately in detail_flags.
        next.time_to_sleep_hibern = jbc_ports[port].time_to_sleep_hibern;
        next.future_mode = jbc_ports[port].future_mode;
        if (jbc_ports[port].tool == next.tool) {
          next.sleep_delay_min = jbc_ports[port].sleep_delay_min;
          next.hiber_delay_min = jbc_ports[port].hiber_delay_min;
          next.delay_config_flags = jbc_ports[port].delay_config_flags;
          next.detail_flags = jbc_ports[port].detail_flags;
          next.detail_value_flags = jbc_ports[port].detail_value_flags;
          next.selected_temp = jbc_ports[port].selected_temp;
          next.sleep_temp = jbc_ports[port].sleep_temp;
          next.adjust_temp = jbc_ports[port].adjust_temp;
          next.cartridge_on = jbc_ports[port].cartridge_on;
          next.cartridge_jbc_code = jbc_ports[port].cartridge_jbc_code;
          next.cartridge_adjust_300 = jbc_ports[port].cartridge_adjust_300;
          next.cartridge_adjust_400 = jbc_ports[port].cartridge_adjust_400;
          next.cartridge_group = jbc_ports[port].cartridge_group;
          next.cartridge_family = jbc_ports[port].cartridge_family;
          next.tip_temp_a = jbc_ports[port].tip_temp_a;
          next.tip_temp_b = jbc_ports[port].tip_temp_b;
          next.cartridge_ma_a = jbc_ports[port].cartridge_ma_a;
          next.cartridge_ma_b = jbc_ports[port].cartridge_ma_b;
          next.cartridge_power_permille_a = jbc_ports[port].cartridge_power_permille_a;
          next.cartridge_power_permille_b = jbc_ports[port].cartridge_power_permille_b;
          next.counter_plug_min = jbc_ports[port].counter_plug_min;
          next.counter_work_min = jbc_ports[port].counter_work_min;
          next.counter_sleep_min = jbc_ports[port].counter_sleep_min;
          next.counter_hiber_min = jbc_ports[port].counter_hiber_min;
          next.counter_idle_min = jbc_ports[port].counter_idle_min;
          next.counter_sleep_cycles = jbc_ports[port].counter_sleep_cycles;
          next.counter_desold_cycles = jbc_ports[port].counter_desold_cycles;
          next.sold_diag_flags = jbc_ports[port].sold_diag_flags;
          next.sold_mos_temp = jbc_ports[port].sold_mos_temp;
          next.sold_tool_type = jbc_ports[port].sold_tool_type;
          next.sold_tool_last_error = jbc_ports[port].sold_tool_last_error;
          next.sold_alarm_max_temp = jbc_ports[port].sold_alarm_max_temp;
          next.sold_alarm_max_delay_tenth_sec = jbc_ports[port].sold_alarm_max_delay_tenth_sec;
          next.sold_alarm_min_temp = jbc_ports[port].sold_alarm_min_temp;
          next.sold_alarm_min_delay_tenth_sec = jbc_ports[port].sold_alarm_min_delay_tenth_sec;
          next.sold_extra_flags = jbc_ports[port].sold_extra_flags;
          next.sold_partial_plug_min = jbc_ports[port].sold_partial_plug_min;
          next.sold_partial_work_min = jbc_ports[port].sold_partial_work_min;
          next.sold_partial_sleep_min = jbc_ports[port].sold_partial_sleep_min;
          next.sold_partial_hiber_min = jbc_ports[port].sold_partial_hiber_min;
          next.sold_partial_idle_min = jbc_ports[port].sold_partial_idle_min;
          next.sold_partial_sleep_cycles = jbc_ports[port].sold_partial_sleep_cycles;
          next.sold_partial_desold_cycles = jbc_ports[port].sold_partial_desold_cycles;
          next.sold_profile_mode = jbc_ports[port].sold_profile_mode;
          memcpy(next.sold_selected_profile, jbc_ports[port].sold_selected_profile, sizeof(next.sold_selected_profile));
          next.sold_assistant_on = jbc_ports[port].sold_assistant_on;
          next.sold_assistant_warning = jbc_ports[port].sold_assistant_warning;
          next.sold_assistant_error = jbc_ports[port].sold_assistant_error;
          next.sold_readonly_port_flags = jbc_ports[port].sold_readonly_port_flags;
          next.sold_fixed_temp = jbc_ports[port].sold_fixed_temp;
          next.sold_fixed_temp_on = jbc_ports[port].sold_fixed_temp_on;
          next.sold_assistant_warning_code = jbc_ports[port].sold_assistant_warning_code;
          next.sold_result_similarity = jbc_ports[port].sold_result_similarity;
          next.sold_result_tenths = jbc_ports[port].sold_result_tenths;
          next.sold_result_energy = jbc_ports[port].sold_result_energy;
          next.sold_direct_power_permille = jbc_ports[port].sold_direct_power_permille;
          next.sold_feeder_flags = jbc_ports[port].sold_feeder_flags;
          next.sold_feeder_working_mode = jbc_ports[port].sold_feeder_working_mode;
          next.sold_feeder_selected_program = jbc_ports[port].sold_feeder_selected_program;
          next.sold_feeder_delivery_length = jbc_ports[port].sold_feeder_delivery_length;
          next.sold_feeder_delivery_speed = jbc_ports[port].sold_feeder_delivery_speed;
          next.sold_feeder_tin_diameter = jbc_ports[port].sold_feeder_tin_diameter;
          next.sold_feeder_remove_length = jbc_ports[port].sold_feeder_remove_length;
          next.sold_feeder_speed_length_readonly = jbc_ports[port].sold_feeder_speed_length_readonly;
          next.sold_feeder_selectable_programs = jbc_ports[port].sold_feeder_selectable_programs;
          next.sold_feeder_clogging_detection = jbc_ports[port].sold_feeder_clogging_detection;
          next.sold_feeder_motor_on = jbc_ports[port].sold_feeder_motor_on;
          next.sold_feeder_motor_direction = jbc_ports[port].sold_feeder_motor_direction;
          next.sold_special_counter_flags = jbc_ports[port].sold_special_counter_flags;
          next.sold_tin_deliver_cycles = jbc_ports[port].sold_tin_deliver_cycles;
          next.sold_tin_length = jbc_ports[port].sold_tin_length;
          next.sold_partial_tin_deliver_cycles = jbc_ports[port].sold_partial_tin_deliver_cycles;
          next.sold_partial_tin_length = jbc_ports[port].sold_partial_tin_length;
          next.sold_cde_sold_number = jbc_ports[port].sold_cde_sold_number;
          next.sold_cde_energy_delivered = jbc_ports[port].sold_cde_energy_delivered;
          next.sold_cde_sold_total = jbc_ports[port].sold_cde_sold_total;
          next.sold_cde_sold_per_min = jbc_ports[port].sold_cde_sold_per_min;
          next.sold_cde_sold_ok = jbc_ports[port].sold_cde_sold_ok;
          next.sold_cde_partial_sold_number = jbc_ports[port].sold_cde_partial_sold_number;
          next.sold_cde_partial_energy_delivered = jbc_ports[port].sold_cde_partial_energy_delivered;
          next.sold_cde_partial_sold_total = jbc_ports[port].sold_cde_partial_sold_total;
          next.sold_cde_partial_sold_per_min = jbc_ports[port].sold_cde_partial_sold_per_min;
          next.sold_cde_partial_sold_ok = jbc_ports[port].sold_cde_partial_sold_ok;
          memcpy(next.sold_feeder_program_length, jbc_ports[port].sold_feeder_program_length, sizeof(next.sold_feeder_program_length));
          memcpy(next.sold_feeder_program_speed, jbc_ports[port].sold_feeder_program_speed, sizeof(next.sold_feeder_program_speed));
          next.levels_on = jbc_ports[port].levels_on;
          next.selected_level = jbc_ports[port].selected_level;
          for (uint8_t lv = 0; lv < 3; ++lv) {
            next.level_on[lv] = jbc_ports[port].level_on[lv];
            next.level_temp[lv] = jbc_ports[port].level_temp[lv];
          }
        }
        if (sold_supports_qst()) {
          next.detail_value_flags |= JBC_SOLD_DETAIL_ENABLED_PORT_VALID;
          // DLL ReceiveFrame02_SOLD: bit5=0 -> EnabledPort ON, bit5=1 -> OFF.
          if ((status & 0x20U) == 0) next.detail_value_flags |= JBC_SOLD_DETAIL_ENABLED_PORT_ON;
          else next.detail_value_flags &= (uint16_t)~JBC_SOLD_DETAIL_ENABLED_PORT_ON;
        }
        // ReceiveFrame02_SOLD uses byte 11 as ChangesStatusInformation
        // (byte 12 on ALE). Mirror its event callbacks instead of waiting for
        // the next slow periodic tier.
        const uint8_t change_index = (f.len == 15 && !strcmp(jbc_model, "ALE")) ? 12U : 11U;
        const uint8_t change_flags = f.data[change_index];
        jbc_change_refresh_flags |= (uint8_t)(change_flags & 0x83U);
        jbc_change_tool_port_mask |= (uint8_t)((change_flags >> 2) & 0x0FU);
      } else if (command_protocol == JBC_PROTO_01) {
        if (port >= JBC_MAX_PORTS || (f.len != 12 && f.len != 15)) return;
        next.tool = f.data[0];
        next.error = f.data[1];
        next.temp = get_u16_le(f.data + 2);
        next.power_permille = get_u16_le(f.data + 6);
        const uint8_t status = f.data[10];
        // Original P01 layout: bit0 Sleep, bit1 Hibernation, bit2 Extractor,
        // bit3 Desolder. Stand is synthesized from the extended future-mode data.
        next.sleep = (status & 0x01) != 0;
        next.hibernation = (status & 0x02) != 0;
        next.extractor = (status & 0x04) != 0;
        next.desolder = (status & 0x08) != 0;
        next.detail_flags = status;
        if (jbc_ports[port].tool == next.tool) {
          next.sleep_delay_min = jbc_ports[port].sleep_delay_min;
          next.hiber_delay_min = jbc_ports[port].hiber_delay_min;
          next.delay_config_flags = jbc_ports[port].delay_config_flags;
          next.detail_value_flags = jbc_ports[port].detail_value_flags;
          next.selected_temp = jbc_ports[port].selected_temp;
          next.sleep_temp = jbc_ports[port].sleep_temp;
          next.adjust_temp = jbc_ports[port].adjust_temp;
          next.cartridge_on = jbc_ports[port].cartridge_on;
          next.cartridge_jbc_code = jbc_ports[port].cartridge_jbc_code;
          next.cartridge_adjust_300 = jbc_ports[port].cartridge_adjust_300;
          next.cartridge_adjust_400 = jbc_ports[port].cartridge_adjust_400;
          next.cartridge_group = jbc_ports[port].cartridge_group;
          next.cartridge_family = jbc_ports[port].cartridge_family;
          next.tip_temp_a = jbc_ports[port].tip_temp_a;
          next.tip_temp_b = jbc_ports[port].tip_temp_b;
          next.cartridge_ma_a = jbc_ports[port].cartridge_ma_a;
          next.cartridge_ma_b = jbc_ports[port].cartridge_ma_b;
          next.cartridge_power_permille_a = jbc_ports[port].cartridge_power_permille_a;
          next.cartridge_power_permille_b = jbc_ports[port].cartridge_power_permille_b;
          next.counter_plug_min = jbc_ports[port].counter_plug_min;
          next.counter_work_min = jbc_ports[port].counter_work_min;
          next.counter_sleep_min = jbc_ports[port].counter_sleep_min;
          next.counter_hiber_min = jbc_ports[port].counter_hiber_min;
          next.counter_idle_min = jbc_ports[port].counter_idle_min;
          next.counter_sleep_cycles = jbc_ports[port].counter_sleep_cycles;
          next.counter_desold_cycles = jbc_ports[port].counter_desold_cycles;
          next.sold_diag_flags = jbc_ports[port].sold_diag_flags;
          next.sold_mos_temp = jbc_ports[port].sold_mos_temp;
          next.sold_tool_type = jbc_ports[port].sold_tool_type;
          next.sold_tool_last_error = jbc_ports[port].sold_tool_last_error;
          next.sold_alarm_max_temp = jbc_ports[port].sold_alarm_max_temp;
          next.sold_alarm_max_delay_tenth_sec = jbc_ports[port].sold_alarm_max_delay_tenth_sec;
          next.sold_alarm_min_temp = jbc_ports[port].sold_alarm_min_temp;
          next.sold_alarm_min_delay_tenth_sec = jbc_ports[port].sold_alarm_min_delay_tenth_sec;
          next.sold_extra_flags = jbc_ports[port].sold_extra_flags;
          next.sold_partial_plug_min = jbc_ports[port].sold_partial_plug_min;
          next.sold_partial_work_min = jbc_ports[port].sold_partial_work_min;
          next.sold_partial_sleep_min = jbc_ports[port].sold_partial_sleep_min;
          next.sold_partial_hiber_min = jbc_ports[port].sold_partial_hiber_min;
          next.sold_partial_idle_min = jbc_ports[port].sold_partial_idle_min;
          next.sold_partial_sleep_cycles = jbc_ports[port].sold_partial_sleep_cycles;
          next.sold_partial_desold_cycles = jbc_ports[port].sold_partial_desold_cycles;
          next.sold_profile_mode = jbc_ports[port].sold_profile_mode;
          memcpy(next.sold_selected_profile, jbc_ports[port].sold_selected_profile, sizeof(next.sold_selected_profile));
          next.sold_assistant_on = jbc_ports[port].sold_assistant_on;
          next.sold_assistant_warning = jbc_ports[port].sold_assistant_warning;
          next.sold_assistant_error = jbc_ports[port].sold_assistant_error;
          next.sold_readonly_port_flags = jbc_ports[port].sold_readonly_port_flags;
          next.sold_fixed_temp = jbc_ports[port].sold_fixed_temp;
          next.sold_fixed_temp_on = jbc_ports[port].sold_fixed_temp_on;
          next.sold_assistant_warning_code = jbc_ports[port].sold_assistant_warning_code;
          next.sold_result_similarity = jbc_ports[port].sold_result_similarity;
          next.sold_result_tenths = jbc_ports[port].sold_result_tenths;
          next.sold_result_energy = jbc_ports[port].sold_result_energy;
          next.sold_direct_power_permille = jbc_ports[port].sold_direct_power_permille;
          next.sold_feeder_flags = jbc_ports[port].sold_feeder_flags;
          next.sold_feeder_working_mode = jbc_ports[port].sold_feeder_working_mode;
          next.sold_feeder_selected_program = jbc_ports[port].sold_feeder_selected_program;
          next.sold_feeder_delivery_length = jbc_ports[port].sold_feeder_delivery_length;
          next.sold_feeder_delivery_speed = jbc_ports[port].sold_feeder_delivery_speed;
          next.sold_feeder_tin_diameter = jbc_ports[port].sold_feeder_tin_diameter;
          next.sold_feeder_remove_length = jbc_ports[port].sold_feeder_remove_length;
          next.sold_feeder_speed_length_readonly = jbc_ports[port].sold_feeder_speed_length_readonly;
          next.sold_feeder_selectable_programs = jbc_ports[port].sold_feeder_selectable_programs;
          next.sold_feeder_clogging_detection = jbc_ports[port].sold_feeder_clogging_detection;
          next.sold_feeder_motor_on = jbc_ports[port].sold_feeder_motor_on;
          next.sold_feeder_motor_direction = jbc_ports[port].sold_feeder_motor_direction;
          next.sold_special_counter_flags = jbc_ports[port].sold_special_counter_flags;
          next.sold_tin_deliver_cycles = jbc_ports[port].sold_tin_deliver_cycles;
          next.sold_tin_length = jbc_ports[port].sold_tin_length;
          next.sold_partial_tin_deliver_cycles = jbc_ports[port].sold_partial_tin_deliver_cycles;
          next.sold_partial_tin_length = jbc_ports[port].sold_partial_tin_length;
          next.sold_cde_sold_number = jbc_ports[port].sold_cde_sold_number;
          next.sold_cde_energy_delivered = jbc_ports[port].sold_cde_energy_delivered;
          next.sold_cde_sold_total = jbc_ports[port].sold_cde_sold_total;
          next.sold_cde_sold_per_min = jbc_ports[port].sold_cde_sold_per_min;
          next.sold_cde_sold_ok = jbc_ports[port].sold_cde_sold_ok;
          next.sold_cde_partial_sold_number = jbc_ports[port].sold_cde_partial_sold_number;
          next.sold_cde_partial_energy_delivered = jbc_ports[port].sold_cde_partial_energy_delivered;
          next.sold_cde_partial_sold_total = jbc_ports[port].sold_cde_partial_sold_total;
          next.sold_cde_partial_sold_per_min = jbc_ports[port].sold_cde_partial_sold_per_min;
          next.sold_cde_partial_sold_ok = jbc_ports[port].sold_cde_partial_sold_ok;
          memcpy(next.sold_feeder_program_length, jbc_ports[port].sold_feeder_program_length, sizeof(next.sold_feeder_program_length));
          memcpy(next.sold_feeder_program_speed, jbc_ports[port].sold_feeder_program_speed, sizeof(next.sold_feeder_program_speed));
          next.levels_on = jbc_ports[port].levels_on;
          next.selected_level = jbc_ports[port].selected_level;
          for (uint8_t lv = 0; lv < 3; ++lv) {
            next.level_on[lv] = jbc_ports[port].level_on[lv];
            next.level_temp[lv] = jbc_ports[port].level_temp[lv];
          }
        }
        if (f.len == 15) {
          next.time_to_sleep_hibern = get_u16_le(f.data + 12);
          next.future_mode = f.data[14];
          // ReceiveFrame01_SOLD: an active countdown specifically to Sleep is
          // represented as Stand until Sleep is actually entered.
          if (next.time_to_sleep_hibern > 0 && next.future_mode == (uint8_t)'S') {
            next.sleep = false;
            next.stand = true;
          }
        }
        const uint8_t change_flags = f.data[11];
        jbc_change_refresh_flags |= (uint8_t)(change_flags & 0x83U);
        jbc_change_tool_port_mask |= (uint8_t)((change_flags >> 2) & 0x0FU);
      } else return;
      break;
    }

    case JBC_STATION_HA: {
      // ReceiveFrame02_HA::M_INF_PORT: 14 or 16 bytes.
      if (command_protocol != JBC_PROTO_02 || (f.len != 14 && f.len != 16) || port >= JBC_MAX_PORTS) return;
      next.tool = f.data[0];
      next.error = f.data[1];
      next.temp = get_u16_le(f.data + 2);
      next.protection_temp = get_u16_le(f.data + 4);
      next.ha_value_flags |= 0x0001;
      next.power_permille = get_u16_le(f.data + 6);
      // ReceiveFrame02_HA maps bytes 8..9 to Flow_x_Mil and bytes 10..11
      // to ToolStatus.TimeToStop. Keep both: the legacy auxiliary u16 remains
      // Flow_x_Mil while the 0.1.14 station-specific suffix transports TimeToStop.
      next.time_to_sleep_hibern = get_u16_le(f.data + 8);
      next.time_to_stop = get_u16_le(f.data + 10);
      const uint8_t status = f.data[12];
      next.heater = (status & 0x01) != 0;
      next.cooling = (status & 0x04) != 0;
      next.suction = (status & 0x08) != 0;
      next.stand = (status & 0x80) != 0;
      // Raw ToolStatus_HA byte: HEATER, HEATER_REQUESTED, COOLING, SUCTION,
      // SUCTION_REQUESTED, PEDAL_CONNECTED, PEDAL_PRESSED, STAND.
      next.detail_flags = status;
      // Preserve slower HA settings/counters across frequent M_INF_PORT frames.
      // Without this, 0x50/0x59 setpoints were erased again about 200 ms later.
      const JbcPortState& prev = jbc_ports[port];
      if (prev.tool == next.tool) {
        next.ha_value_flags |= prev.ha_value_flags;
        next.selected_temp = prev.selected_temp;
        next.selected_flow_permille = prev.selected_flow_permille;
        next.selected_ext_temp = prev.selected_ext_temp;
        next.actual_ext_temp = prev.actual_ext_temp;
        next.ha_adjust_temp = prev.ha_adjust_temp;
        next.configured_time_to_stop = prev.configured_time_to_stop;
        next.external_tc_mode = prev.external_tc_mode;
        next.start_mode = prev.start_mode;
        next.profile_mode = prev.profile_mode;
        next.levels_on = prev.levels_on;
        next.selected_level = prev.selected_level;
        for (uint8_t lv = 0; lv < 3; ++lv) {
          next.level_on[lv] = prev.level_on[lv];
          next.level_temp[lv] = prev.level_temp[lv];
          next.level_flow_permille[lv] = prev.level_flow_permille[lv];
          next.level_ext_temp[lv] = prev.level_ext_temp[lv];
        }
        next.ha_counter_plug_min = prev.ha_counter_plug_min;
        next.ha_counter_work_min = prev.ha_counter_work_min;
        next.ha_counter_work_cycles = prev.ha_counter_work_cycles;
        next.ha_counter_suction_cycles = prev.ha_counter_suction_cycles;
        next.ha_diag_flags = prev.ha_diag_flags;
        next.ha_diag_air_temp = prev.ha_diag_air_temp;
        next.ha_diag_power_permille = prev.ha_diag_power_permille;
        next.ha_diag_flow_permille = prev.ha_diag_flow_permille;
        next.ha_diag_tool = prev.ha_diag_tool;
        next.ha_diag_error = prev.ha_diag_error;
        next.ha_diag_status = prev.ha_diag_status;
        next.ha_diag_heater_state = prev.ha_diag_heater_state;
        next.ha_diag_suction_state = prev.ha_diag_suction_state;
        next.ha_partial_plug_min = prev.ha_partial_plug_min;
        next.ha_partial_work_min = prev.ha_partial_work_min;
        next.ha_partial_work_cycles = prev.ha_partial_work_cycles;
        next.ha_partial_suction_cycles = prev.ha_partial_suction_cycles;
        next.ha_conti_valid = prev.ha_conti_valid;
        next.ha_conti_status_raw = prev.ha_conti_status_raw;
        next.ha_conti_ext_tc1 = prev.ha_conti_ext_tc1;
        next.ha_conti_ext_tc2 = prev.ha_conti_ext_tc2;
        next.ha_conti_last_ms = prev.ha_conti_last_ms;
      }
      if (f.len == 16) {
        const uint8_t change_flags = f.data[15];
        jbc_change_refresh_flags |= (uint8_t)(change_flags & 0x83U);
        jbc_change_tool_port_mask |= (uint8_t)((change_flags >> 2) & 0x0FU);
      }
      break;
    }

    case JBC_STATION_PH: {
      // ReceiveFrame02_PH::M_INF_PORT: 17 bytes. Preserve all four external-TC
      // temperatures and the complete PH status byte exactly as JBC_Connect does.
      if (command_protocol != JBC_PROTO_02 || f.len != 17 || port >= JBC_MAX_PORTS) return;
      next.error = f.data[0];
      for (uint8_t tc = 0; tc < 4; ++tc) {
        uint16_t v = get_u16_le(f.data + 1 + tc * 2);
        if (v == 0xFFFFU) v = 0;
        jbc_ph_tc[tc].actual_temp = v;
        jbc_ph_tc[tc].flags |= 0x01U;
      }
      next.temp = jbc_ph_tc[0].actual_temp; // legacy compact field = TC1
      next.power_permille = get_u16_le(f.data + 9);
      const uint32_t time_to_stop = get_u32_le(f.data + 11);
      next.time_to_sleep_hibern = (uint16_t)min(time_to_stop, 65535UL);
      next.time_to_stop = (uint16_t)min(time_to_stop, 65535UL);
      const uint8_t status = f.data[15];
      next.heater = (status & 0x01) != 0;
      next.cooling = (status & 0x04) != 0; // legacy compact alias = Zone A
      next.suction = (status & 0x08) != 0; // legacy compact alias = Internal fan
      next.detail_flags = status; // heater, Zone B/A, fan, pedal connected/pressed
      // Slow UpdateData_PH settings/counters must survive the frequent InfoPort
      // refresh just like the existing SOLD/HA extensions do.
      const JbcPortState& prev = jbc_ports[port];
      next.ph_flags = prev.ph_flags;
      next.ph_work_mode = prev.ph_work_mode;
      next.ph_heater_status = prev.ph_heater_status;
      next.ph_configured_time_to_stop = prev.ph_configured_time_to_stop;
      next.ph_selected_power = prev.ph_selected_power;
      next.ph_active_zones = prev.ph_active_zones;
      next.ph_counter_plug_min = prev.ph_counter_plug_min;
      next.ph_counter_work_min_power = prev.ph_counter_work_min_power;
      next.ph_counter_work_min_temp = prev.ph_counter_work_min_temp;
      next.ph_counter_work_min_profile = prev.ph_counter_work_min_profile;
      next.ph_counter_work_cycles_power = prev.ph_counter_work_cycles_power;
      next.ph_counter_work_cycles_temp = prev.ph_counter_work_cycles_temp;
      next.ph_counter_work_cycles_profile = prev.ph_counter_work_cycles_profile;
      next.ph_partial_plug_min = prev.ph_partial_plug_min;
      next.ph_partial_work_min_power = prev.ph_partial_work_min_power;
      next.ph_partial_work_min_temp = prev.ph_partial_work_min_temp;
      next.ph_partial_work_min_profile = prev.ph_partial_work_min_profile;
      next.ph_partial_work_cycles_power = prev.ph_partial_work_cycles_power;
      next.ph_partial_work_cycles_temp = prev.ph_partial_work_cycles_temp;
      next.ph_partial_work_cycles_profile = prev.ph_partial_work_cycles_profile;
      break;
    }

    case JBC_STATION_SF: {
      // ReceiveFrame02_SF::ReadDispenserMode (0x30): dispenser mode + selected
      // program. Preserve the slower UpdateData_SF detail/counter reads while
      // refreshing this fast path.
      if (command_protocol != JBC_PROTO_02 || f.len != 2 || port >= JBC_MAX_PORTS) return;
      const JbcPortState& prev = jbc_ports[port];
      next.future_mode = f.data[0];            // DispenserMode raw
      next.time_to_sleep_hibern = f.data[1];  // selected program raw (meaningful in PROGRAM mode)
      next.sf_flags = prev.sf_flags;
      next.sf_speed_tenth_mm_s = prev.sf_speed_tenth_mm_s;
      next.sf_length_tenth_mm = prev.sf_length_tenth_mm;
      next.sf_feeding_state = prev.sf_feeding_state;
      next.sf_feeding_value_raw = prev.sf_feeding_value_raw;
      next.sf_feeding_selected_program = prev.sf_feeding_selected_program;
      next.sf_current_program_step = prev.sf_current_program_step;
      next.sf_counter_tin_length = prev.sf_counter_tin_length;
      next.sf_counter_plug_min = prev.sf_counter_plug_min;
      next.sf_counter_work_min = prev.sf_counter_work_min;
      next.sf_counter_idle_min = prev.sf_counter_idle_min;
      next.sf_counter_work_cycles = prev.sf_counter_work_cycles;
      next.sf_partial_tin_length = prev.sf_partial_tin_length;
      next.sf_partial_plug_min = prev.sf_partial_plug_min;
      next.sf_partial_work_min = prev.sf_partial_work_min;
      next.sf_partial_idle_min = prev.sf_partial_idle_min;
      next.sf_partial_work_cycles = prev.sf_partial_work_cycles;
      break;
    }

    case JBC_STATION_FE:
      // Handled station-wide above before constructing a per-port snapshot.
      return;

    case JBC_STATION_CL: {
      // ReceiveFrame02_CL::M_INF_PORT: one-byte cleaner mode. ReadCleanerMode
      // itself is station-wide/no-payload, but JBC_Connect maps it to port 0.
      if (command_protocol != JBC_PROTO_02 || f.len != 1 || port >= JBC_MAX_PORTS) return;
      next.future_mode = f.data[0]; // CleanerMode raw
      const JbcPortState& prev = jbc_ports[port];
      next.cl_flags = prev.cl_flags;
      next.cl_motors_on = prev.cl_motors_on; next.cl_door_open = prev.cl_door_open;
      next.cl_counter_plug_min = prev.cl_counter_plug_min;
      next.cl_counter_cleaning_continuous_min = prev.cl_counter_cleaning_continuous_min;
      next.cl_counter_cleaning_detection_min = prev.cl_counter_cleaning_detection_min;
      next.cl_counter_work_cycles = prev.cl_counter_work_cycles;
      next.cl_counter_door_open_cycles = prev.cl_counter_door_open_cycles;
      next.cl_partial_plug_min = prev.cl_partial_plug_min;
      next.cl_partial_cleaning_continuous_min = prev.cl_partial_cleaning_continuous_min;
      next.cl_partial_cleaning_detection_min = prev.cl_partial_cleaning_detection_min;
      next.cl_partial_work_cycles = prev.cl_partial_work_cycles;
      next.cl_partial_door_open_cycles = prev.cl_partial_door_open_cycles;
      break;
    }

    case JBC_STATION_UNKNOWN:
    default: {
      // Preserve unknown/new models rather than rejecting them. A valid reply is
      // enough to confirm the port, but UNKNOWN never asserts WORK automatically.
      if (port >= JBC_MAX_PORTS) return;
      if (f.len >= 2) { next.tool = f.data[0]; next.error = f.data[1]; }
      if (f.len >= 4) next.temp = get_u16_le(f.data + 2);
      if (f.len >= 8) next.power_permille = get_u16_le(f.data + 6);
      break;
    }
  }

  if (jbc_station_kind == JBC_STATION_SOLD) {
    const JbcPortState& prev = jbc_ports[port];
    next.sold_info_last_ms = next.last_ms;
    if (prev.tool == next.tool && prev.sold_conti_valid) {
      next.sold_conti_valid = true;
      next.sold_conti_status_raw = prev.sold_conti_status_raw;
      next.sold_soldering = prev.sold_soldering;
      next.sold_calibrating = prev.sold_calibrating;
      next.sold_conti_last_ms = prev.sold_conti_last_ms;
      // ContiMode is the faster source for fields that exist in both frames.
      // M_INF_PORT still runs and refreshes tool/error/EnabledPort/change flags
      // and becomes the fallback automatically when the stream goes stale.
      const bool conti_fresh = (uint32_t)(next.last_ms - prev.sold_conti_last_ms) <= 750UL;
      if (conti_fresh) {
        next.temp = prev.temp;
        next.power_permille = prev.power_permille;
        next.sleep = prev.sleep;
        next.hibernation = prev.hibernation;
        next.extractor = prev.extractor;
        next.desolder = prev.desolder;
        if (command_protocol == JBC_PROTO_02) next.stand = prev.stand;
      }
    }
  }

  if (jbc_station_kind == JBC_STATION_HA) {
    const JbcPortState& prev = jbc_ports[port];
    next.ha_info_last_ms = next.last_ms;
    if (prev.tool == next.tool && prev.ha_conti_valid) {
      next.ha_conti_valid = true;
      next.ha_conti_status_raw = prev.ha_conti_status_raw;
      next.ha_conti_ext_tc1 = prev.ha_conti_ext_tc1;
      next.ha_conti_ext_tc2 = prev.ha_conti_ext_tc2;
      next.ha_conti_last_ms = prev.ha_conti_last_ms;
      const bool ha_conti_fresh = (uint32_t)(next.last_ms - prev.ha_conti_last_ms) <= 750UL;
      if (ha_conti_fresh) {
        next.temp = prev.temp;
        next.time_to_sleep_hibern = prev.time_to_sleep_hibern; // Flow_x_Mil
        next.power_permille = prev.power_permille;
        next.time_to_stop = prev.time_to_stop;
        next.heater = prev.heater;
        next.cooling = prev.cooling;
        next.suction = prev.suction;
        next.stand = prev.stand;
        next.detail_flags = prev.detail_flags;
      }
    }
  }

  jbc_ports[port] = next;
  const uint8_t seen_count = (uint8_t)(port + 1);
  if (seen_count > jbc_highest_seen_port) jbc_highest_seen_port = seen_count;
  if (!jbc_port_count_from_model && seen_count > jbc_port_count) jbc_port_count = seen_count;
  recompute_work_masks();
}

static void decode_delay_time(const JbcFrame& f) {
  if (jbc_station_kind != JBC_STATION_SOLD || f.frame_protocol != JBC_PROTO_02 ||
      f.command != JBC_CMD_DELAY_TIME_P02_SOLD || f.len != 4) return;
  const uint8_t port = pending_by_fid[f.fid].port;
  pending_by_fid[f.fid] = PendingRequest();
  if (port >= JBC_MAX_PORTS) return;

  uint16_t countdown = get_u16_le(f.data);
  uint8_t future = f.data[2];
  if (countdown == 0xFFFFU) countdown = 0;

  // Keep the station's raw M_R_DELAYTIME countdown. JBC Connect's DLL
  // intentionally clears this value once STAND/SLEEP/HIBERNATION is set,
  // but OFE also exposes the live station countdown so the configured
  // delay and the remaining time can be shown together. A zero value is
  // still treated as no future transition.
  if (!countdown) future = (uint8_t)'N';

  if (jbc_ports[port].time_to_sleep_hibern != countdown || jbc_ports[port].future_mode != future) {
    jbc_ports[port].time_to_sleep_hibern = countdown;
    jbc_ports[port].future_mode = future;
    mark_fast_changed();
  }
}

static void decode_sold_delay_setting(const JbcFrame& f) {
  if (jbc_station_kind != JBC_STATION_SOLD ||
      (f.command != JBC_CMD_SLEEP_DELAY_SOLD && f.command != JBC_CMD_HIBER_DELAY_SOLD)) return;
  const uint8_t port = pending_port_for_frame(f);
  if (port >= JBC_MAX_PORTS) return;

  bool enabled = false;
  uint16_t minutes = 0;
  if (f.frame_protocol == JBC_PROTO_02) {
    // ReceiveFrame02_SOLD: 4-byte reply; data[0]=enum minutes, data[1]=OnOff.
    if (f.len != 4) { jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN); return; }
    enabled = f.data[0] != 0xFF && f.data[1] == 1;
    minutes = f.data[0] == 0xFF ? 0 : f.data[0];
  } else if (f.frame_protocol == JBC_PROTO_01) {
    // ReceiveFrame01_SOLD: 16-bit ToolTime* enum; 0xFFFF means disabled.
    if (f.len != 2) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
    const uint16_t raw = get_u16_le(f.data);
    enabled = raw != 0xFFFFU;
    minutes = enabled ? raw : 0;
  } else return;

  JbcPortState& p = jbc_ports[port];
  uint8_t flags = p.delay_config_flags;
  const uint8_t mins = (minutes > 255U) ? (uint8_t)255 : (uint8_t)minutes;
  bool changed = false;
  if (f.command == JBC_CMD_SLEEP_DELAY_SOLD) {
    if (p.sleep_delay_min != mins) { p.sleep_delay_min = mins; changed = true; }
    flags |= 0x01;
    if (enabled) flags |= 0x02; else flags &= (uint8_t)~0x02;
  } else {
    if (p.hiber_delay_min != mins) { p.hiber_delay_min = mins; changed = true; }
    flags |= 0x04;
    if (enabled) flags |= 0x08; else flags &= (uint8_t)~0x08;
  }
  if (p.delay_config_flags != flags) { p.delay_config_flags = flags; changed = true; }
  if (changed) mark_fast_changed();
}

static bool is_sold_detail_frame(const JbcFrame& f) {
  if (jbc_station_kind != JBC_STATION_SOLD) return false;
  switch (f.command) {
    case JBC_CMD_LEVELS_SOLD:
    case JBC_CMD_SLEEP_TEMP_SOLD:
    case JBC_CMD_ADJUST_TEMP_SOLD:
    case JBC_CMD_CARTRIDGE_SOLD:
    case JBC_CMD_SELECT_TEMP_SOLD:
    case JBC_CMD_TIP_TEMP_SOLD:
    case JBC_CMD_CURRENT_SOLD:
    case JBC_CMD_POWER_PERTHOUSAND_SOLD:
    case JBC_CMD_COUNTER_PLUG:
    case JBC_CMD_COUNTER_WORK:
    case JBC_CMD_COUNTER_SLEEP:
    case JBC_CMD_COUNTER_HIBER:
    case JBC_CMD_COUNTER_IDLE:
    case JBC_CMD_COUNTER_SLEEP_CYCLES:
    case JBC_CMD_COUNTER_DESOLD_CYCLES:
    case JBC_CMD_COUNTER_PLUG_PARTIAL_SOLD:
    case JBC_CMD_COUNTER_WORK_PARTIAL_SOLD:
    case JBC_CMD_COUNTER_SLEEP_PARTIAL_SOLD:
    case JBC_CMD_COUNTER_HIBER_PARTIAL_SOLD:
    case JBC_CMD_COUNTER_IDLE_PARTIAL_SOLD:
    case JBC_CMD_COUNTER_SLEEP_CYCLES_PARTIAL_SOLD:
    case JBC_CMD_COUNTER_DESOLD_CYCLES_PARTIAL_SOLD:
    case JBC_CMD_PROFILE_MODE_SOLD:
    case JBC_CMD_ASSISTANT_CONFIG_SOLD:
    case JBC_CMD_ASSISTANT_WARNING_SOLD:
    case JBC_CMD_SOLDERING_RESULT_SOLD:
    case JBC_CMD_SELECTED_PROFILE_SOLD: return f.frame_protocol == JBC_PROTO_02;
    case JBC_CMD_TOOL_STATUS_SOLD: return f.frame_protocol == JBC_PROTO_02;
    default: return false;
  }
}

static uint32_t jbc_u32_from_le(const uint8_t* p) { return get_u32_le(p); }

static void decode_sold_detail(const JbcFrame& f) {
  uint8_t port = pending_port_for_frame(f);
  // Cartridge/service replies carry the port in the payload too. Follow the
  // original DLL and prefer that embedded port when it is sane.
  if (f.command == JBC_CMD_CARTRIDGE_SOLD) {
    if (f.len != 11) { jbc_note_decode_error(f, f.len != 11 ? (uint8_t)11 : JBC_DECODE_LEN_UNKNOWN, f.len != 11 ? (uint8_t)11 : JBC_DECODE_LEN_UNKNOWN); return; }
    // DLL ReceiveFrame02_SOLD / ReceiveFrame02_k20_SOLD: bytes 0..8 are
    // cartridge data, byte 9 is port and byte 10 is the internal tool id.
    if (f.data[9] < JBC_MAX_PORTS) port = f.data[9];
  } else if (f.command == JBC_CMD_TIP_TEMP_SOLD || f.command == JBC_CMD_CURRENT_SOLD ||
             f.command == JBC_CMD_POWER_PERTHOUSAND_SOLD) {
    if (f.len != 5) { jbc_note_decode_error(f, f.len != 5 ? (uint8_t)5 : JBC_DECODE_LEN_UNKNOWN, f.len != 5 ? (uint8_t)5 : JBC_DECODE_LEN_UNKNOWN); return; }
    if (f.data[4] < JBC_MAX_PORTS) port = f.data[4];
  } else if (f.command == JBC_CMD_TOOL_STATUS_SOLD || f.command == JBC_CMD_ASSISTANT_WARNING_SOLD) {
    if (f.len != 2) { jbc_note_decode_error(f, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN, f.len != 2 ? (uint8_t)2 : JBC_DECODE_LEN_UNKNOWN); return; }
    if (f.data[1] < JBC_MAX_PORTS) port = f.data[1];
  } else if (f.command == JBC_CMD_SOLDERING_RESULT_SOLD) {
    if (f.len != 7) { jbc_note_decode_error(f, f.len != 7 ? (uint8_t)7 : JBC_DECODE_LEN_UNKNOWN, f.len != 7 ? (uint8_t)7 : JBC_DECODE_LEN_UNKNOWN); return; }
    if (f.data[6] < JBC_MAX_PORTS) port = f.data[6];
  } else if (f.command == JBC_CMD_SELECTED_PROFILE_SOLD) {
    if (f.len < 1 || f.len > 13) { jbc_note_decode_error(f, 1, 13); return; }
    if (f.data[f.len - 1] < JBC_MAX_PORTS) port = f.data[f.len - 1];
  }
  if (port >= JBC_MAX_PORTS) return;
  JbcPortState& ps = jbc_ports[port];
  bool changed = false;
  bool work_state_changed = false;
  if (f.command == JBC_CMD_TOOL_STATUS_SOLD) {
    const uint8_t raw = f.data[0];
    if (ps.detail_flags != raw) { ps.detail_flags = raw; changed = true; }
    const bool stand = (raw & 0x01U) != 0;
    const bool sleep = (raw & 0x02U) != 0;
    const bool hiber = (raw & 0x04U) != 0;
    const bool extractor = (raw & 0x08U) != 0;
    const bool desolder = (raw & 0x10U) != 0;
    if (ps.stand != stand) { ps.stand = stand; work_state_changed = true; changed = true; }
    if (ps.sleep != sleep) { ps.sleep = sleep; work_state_changed = true; changed = true; }
    if (ps.hibernation != hiber) { ps.hibernation = hiber; work_state_changed = true; changed = true; }
    if (ps.extractor != extractor) { ps.extractor = extractor; work_state_changed = true; changed = true; }
    if (ps.desolder != desolder) { ps.desolder = desolder; work_state_changed = true; changed = true; }
    if (!(ps.detail_value_flags & JBC_SOLD_DETAIL_TOOL_STATUS_VALID)) {
      ps.detail_value_flags |= JBC_SOLD_DETAIL_TOOL_STATUS_VALID; changed = true;
    }
  } else if (f.command == JBC_CMD_CARTRIDGE_SOLD) {
    const uint8_t on = f.data[0];
    const int16_t code = (int16_t)get_u16_le(f.data + 1);
    const int16_t adj300 = (int16_t)get_u16_le(f.data + 3);
    const int16_t adj400 = (int16_t)get_u16_le(f.data + 5);
    const uint8_t group = f.data[7], family = f.data[8];
    if (ps.cartridge_on != on) { ps.cartridge_on = on; changed = true; }
    if (ps.cartridge_jbc_code != code) { ps.cartridge_jbc_code = code; changed = true; }
    if (ps.cartridge_adjust_300 != adj300) { ps.cartridge_adjust_300 = adj300; changed = true; }
    if (ps.cartridge_adjust_400 != adj400) { ps.cartridge_adjust_400 = adj400; changed = true; }
    if (ps.cartridge_group != group) { ps.cartridge_group = group; changed = true; }
    if (ps.cartridge_family != family) { ps.cartridge_family = family; changed = true; }
    if (!(ps.detail_value_flags & 0x0040)) { ps.detail_value_flags |= 0x0040; changed = true; }
  } else if (f.command == JBC_CMD_TIP_TEMP_SOLD) {
    const int16_t a = (int16_t)get_u16_le(f.data);
    const int16_t b = (int16_t)get_u16_le(f.data + 2);
    if (ps.tip_temp_a != a) { ps.tip_temp_a = a; changed = true; }
    if (ps.tip_temp_b != b) { ps.tip_temp_b = b; changed = true; }
    if (!(ps.detail_value_flags & 0x0200)) { ps.detail_value_flags |= 0x0200; changed = true; }
  } else if (f.command == JBC_CMD_CURRENT_SOLD) {
    const int16_t a = (int16_t)get_u16_le(f.data);
    const int16_t b = (int16_t)get_u16_le(f.data + 2);
    if (ps.cartridge_ma_a != a) { ps.cartridge_ma_a = a; changed = true; }
    if (ps.cartridge_ma_b != b) { ps.cartridge_ma_b = b; changed = true; }
    if (!(ps.detail_value_flags & 0x0080)) { ps.detail_value_flags |= 0x0080; changed = true; }
  } else if (f.command == JBC_CMD_POWER_PERTHOUSAND_SOLD) {
    const int16_t a = (int16_t)get_u16_le(f.data);
    const int16_t b = (int16_t)get_u16_le(f.data + 2);
    if (ps.cartridge_power_permille_a != a) { ps.cartridge_power_permille_a = a; changed = true; }
    if (ps.cartridge_power_permille_b != b) { ps.cartridge_power_permille_b = b; changed = true; }
    if (!(ps.detail_value_flags & 0x0100)) { ps.detail_value_flags |= 0x0100; changed = true; }
  } else if (f.command == JBC_CMD_SELECT_TEMP_SOLD) {
    if (f.len != 3) { jbc_note_decode_error(f, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN); return; }
    const uint16_t v = get_u16_le(f.data);
    if (ps.selected_temp != v) { ps.selected_temp = v; changed = true; }
    if (!(ps.detail_value_flags & 0x01)) { ps.detail_value_flags |= 0x01; changed = true; }
  } else if (f.command == JBC_CMD_LEVELS_SOLD) {
    // ReceiveFrame02_SOLD case 0x33: 13 bytes. Bytes 11..12 are reserved
    // by the DLL; the three level temperatures are UTI values.
    if (f.len != 13) { jbc_note_decode_error(f, f.len != 13 ? (uint8_t)13 : JBC_DECODE_LEN_UNKNOWN, f.len != 13 ? (uint8_t)13 : JBC_DECODE_LEN_UNKNOWN); return; }
    const uint8_t levels_on = f.data[0];
    const uint8_t selected_level = f.data[1]; // 0,1,2 or 0xFF (NO_LEVELS)
    const uint8_t on0 = f.data[2], on1 = f.data[5], on2 = f.data[8];
    const uint16_t t0 = get_u16_le(f.data + 3);
    const uint16_t t1 = get_u16_le(f.data + 6);
    const uint16_t t2 = get_u16_le(f.data + 9);
    if (ps.levels_on != levels_on) { ps.levels_on = levels_on; changed = true; }
    if (ps.selected_level != selected_level) { ps.selected_level = selected_level; changed = true; }
    if (ps.level_on[0] != on0) { ps.level_on[0] = on0; changed = true; }
    if (ps.level_on[1] != on1) { ps.level_on[1] = on1; changed = true; }
    if (ps.level_on[2] != on2) { ps.level_on[2] = on2; changed = true; }
    if (ps.level_temp[0] != t0) { ps.level_temp[0] = t0; changed = true; }
    if (ps.level_temp[1] != t1) { ps.level_temp[1] = t1; changed = true; }
    if (ps.level_temp[2] != t2) { ps.level_temp[2] = t2; changed = true; }
    if (!(ps.detail_value_flags & 0x10)) { ps.detail_value_flags |= 0x10; changed = true; }
  } else if (f.command == JBC_CMD_SLEEP_TEMP_SOLD) {
    if (f.len != 4) { jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN); return; }
    const uint16_t v = get_u16_le(f.data);
    if (ps.sleep_temp != v) { ps.sleep_temp = v; changed = true; }
    if (!(ps.detail_value_flags & 0x02)) { ps.detail_value_flags |= 0x02; changed = true; }
  } else if (f.command == JBC_CMD_ADJUST_TEMP_SOLD) {
    if (f.len != 4) { jbc_note_decode_error(f, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN, f.len != 4 ? (uint8_t)4 : JBC_DECODE_LEN_UNKNOWN); return; }
    const int16_t v = (int16_t)get_u16_le(f.data);
    if (ps.adjust_temp != v) { ps.adjust_temp = v; changed = true; }
    if (!(ps.detail_value_flags & 0x04)) { ps.detail_value_flags |= 0x04; changed = true; }
  } else if (f.command == JBC_CMD_PROFILE_MODE_SOLD) {
    if (f.len != 3) { jbc_note_decode_error(f, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN, f.len != 3 ? (uint8_t)3 : JBC_DECODE_LEN_UNKNOWN); return; }
    const uint8_t v = f.data[0] ? 1 : 0;
    if (ps.sold_profile_mode != v) { ps.sold_profile_mode = v; changed = true; }
    if (!(ps.sold_extra_flags & 0x0008U)) { ps.sold_extra_flags |= 0x0008U; changed = true; }
  } else if (f.command == JBC_CMD_ASSISTANT_CONFIG_SOLD) {
    if (f.len != 7) { jbc_note_decode_error(f, f.len != 7 ? (uint8_t)7 : JBC_DECODE_LEN_UNKNOWN, f.len != 7 ? (uint8_t)7 : JBC_DECODE_LEN_UNKNOWN); return; }
    const uint8_t on = (f.data[0] == 1 || f.data[0] == (uint8_t)'1') ? 1 : 0;
    const int16_t warn=(int16_t)get_u16_le(f.data+1), err=(int16_t)get_u16_le(f.data+3);
    if(ps.sold_assistant_on!=on){ps.sold_assistant_on=on;changed=true;}
    if(ps.sold_assistant_warning!=warn){ps.sold_assistant_warning=warn;changed=true;}
    if(ps.sold_assistant_error!=err){ps.sold_assistant_error=err;changed=true;}
    if (!(ps.sold_extra_flags & 0x0010U)) { ps.sold_extra_flags |= 0x0010U; changed = true; }
  } else if (f.command == JBC_CMD_ASSISTANT_WARNING_SOLD) {
    const uint8_t v=f.data[0];
    if(ps.sold_assistant_warning_code!=v){ps.sold_assistant_warning_code=v;changed=true;}
    if(!(ps.sold_readonly_port_flags&0x0002U)){ps.sold_readonly_port_flags|=0x0002U;changed=true;}
  } else if (f.command == JBC_CMD_SOLDERING_RESULT_SOLD) {
    const int16_t sim=(int16_t)get_u16_le(f.data),tenths=(int16_t)get_u16_le(f.data+2),energy=(int16_t)get_u16_le(f.data+4);
    if(ps.sold_result_similarity!=sim){ps.sold_result_similarity=sim;changed=true;}
    if(ps.sold_result_tenths!=tenths){ps.sold_result_tenths=tenths;changed=true;}
    if(ps.sold_result_energy!=energy){ps.sold_result_energy=energy;changed=true;}
    if(!(ps.sold_readonly_port_flags&0x0004U)){ps.sold_readonly_port_flags|=0x0004U;changed=true;}
  } else if (f.command == JBC_CMD_SELECTED_PROFILE_SOLD) {
    uint8_t n=(uint8_t)min((int)f.len-1,12); char tmp[13]={0};
    if(n) memcpy(tmp,f.data,n); while(n && (tmp[n-1]==0 || tmp[n-1]==' ')) tmp[--n]=0;
    if(strcmp(ps.sold_selected_profile,tmp)!=0){strncpy(ps.sold_selected_profile,tmp,12);ps.sold_selected_profile[12]=0;changed=true;}
    if (!(ps.sold_extra_flags & 0x0004U)) { ps.sold_extra_flags |= 0x0004U; changed = true; }
  } else if (f.command == JBC_CMD_COUNTER_WORK && sold_k26_protocol() && (f.len == 33 || f.len == 37 || f.len == 49)) {
    const uint32_t vals[5] = {jbc_u32_from_le(f.data), jbc_u32_from_le(f.data + 4), jbc_u32_from_le(f.data + 8),
                              jbc_u32_from_le(f.data + 12), jbc_u32_from_le(f.data + 16)};
    if(ps.sold_partial_plug_min!=vals[0]){ps.sold_partial_plug_min=vals[0];changed=true;}
    if(ps.sold_partial_work_min!=vals[1]){ps.sold_partial_work_min=vals[1];changed=true;}
    if(ps.sold_partial_sleep_min!=vals[2]){ps.sold_partial_sleep_min=vals[2];changed=true;}
    if(ps.sold_partial_hiber_min!=vals[3]){ps.sold_partial_hiber_min=vals[3];changed=true;}
    if(ps.sold_partial_idle_min!=vals[4]){ps.sold_partial_idle_min=vals[4];changed=true;}
    const uint32_t sc=jbc_u32_from_le(f.data+20); if(ps.sold_partial_sleep_cycles!=sc){ps.sold_partial_sleep_cycles=sc;changed=true;}
    if(strcmp(jbc_model,"ALE")!=0){const uint32_t dc=jbc_u32_from_le(f.data+24);if(ps.sold_partial_desold_cycles!=dc){ps.sold_partial_desold_cycles=dc;changed=true;}if(!(ps.sold_extra_flags&0x0002U)){ps.sold_extra_flags|=0x0002U;changed=true;}}
    if (!strcmp(jbc_model,"ALE") && f.len==33) {
      const uint32_t cyc=jbc_u32_from_le(f.data+24), len=jbc_u32_from_le(f.data+28);
      if(ps.sold_partial_tin_deliver_cycles!=cyc){ps.sold_partial_tin_deliver_cycles=cyc;changed=true;}
      if(ps.sold_partial_tin_length!=len){ps.sold_partial_tin_length=len;changed=true;}
      if(!(ps.sold_special_counter_flags&0x0002U)){ps.sold_special_counter_flags|=0x0002U;changed=true;}
    } else if (!strcmp(jbc_model,"CDE") && f.len==49) {
      const uint32_t v0=jbc_u32_from_le(f.data+28),v1=jbc_u32_from_le(f.data+32),v2=jbc_u32_from_le(f.data+36),v3=jbc_u32_from_le(f.data+40),v4=jbc_u32_from_le(f.data+44);
      if(ps.sold_cde_partial_sold_number!=v0){ps.sold_cde_partial_sold_number=v0;changed=true;}
      if(ps.sold_cde_partial_energy_delivered!=v1){ps.sold_cde_partial_energy_delivered=v1;changed=true;}
      if(ps.sold_cde_partial_sold_total!=v2){ps.sold_cde_partial_sold_total=v2;changed=true;}
      if(ps.sold_cde_partial_sold_per_min!=v3){ps.sold_cde_partial_sold_per_min=v3;changed=true;}
      if(ps.sold_cde_partial_sold_ok!=v4){ps.sold_cde_partial_sold_ok=v4;changed=true;}
      if(!(ps.sold_special_counter_flags&0x0008U)){ps.sold_special_counter_flags|=0x0008U;changed=true;}
    }
    if (!(ps.sold_extra_flags & 0x0001U)) { ps.sold_extra_flags |= 0x0001U; changed = true; }
  } else if (f.command == JBC_CMD_COUNTER_PLUG && sold_k26_protocol() && (f.len == 33 || f.len == 37 || f.len == 49)) {
    const uint32_t vals[5] = {jbc_u32_from_le(f.data), jbc_u32_from_le(f.data + 4), jbc_u32_from_le(f.data + 8),
                              jbc_u32_from_le(f.data + 12), jbc_u32_from_le(f.data + 16)};
    if (ps.counter_plug_min != vals[0]) { ps.counter_plug_min = vals[0]; changed = true; }
    if (ps.counter_work_min != vals[1]) { ps.counter_work_min = vals[1]; changed = true; }
    if (ps.counter_sleep_min != vals[2]) { ps.counter_sleep_min = vals[2]; changed = true; }
    if (ps.counter_hiber_min != vals[3]) { ps.counter_hiber_min = vals[3]; changed = true; }
    if (ps.counter_idle_min != vals[4]) { ps.counter_idle_min = vals[4]; changed = true; }
    const uint32_t sleep_cycles = jbc_u32_from_le(f.data + 20);
    if (ps.counter_sleep_cycles != sleep_cycles) { ps.counter_sleep_cycles = sleep_cycles; changed = true; }
    // ALE uses offset 24 for tin-delivery cycles. Other SOLD models use it
    // for Desolder cycles, matching ReceiveFrame02_k26_SOLD.
    if (strcmp(jbc_model, "ALE") != 0) {
      const uint32_t desold_cycles = jbc_u32_from_le(f.data + 24);
      if (ps.counter_desold_cycles != desold_cycles) { ps.counter_desold_cycles = desold_cycles; changed = true; }
      if (!(ps.detail_value_flags & 0x20)) { ps.detail_value_flags |= 0x20; changed = true; }
    }
    if (!strcmp(jbc_model,"ALE") && f.len==33) {
      const uint32_t cyc=jbc_u32_from_le(f.data+24), len=jbc_u32_from_le(f.data+28);
      if(ps.sold_tin_deliver_cycles!=cyc){ps.sold_tin_deliver_cycles=cyc;changed=true;}
      if(ps.sold_tin_length!=len){ps.sold_tin_length=len;changed=true;}
      if(!(ps.sold_special_counter_flags&0x0001U)){ps.sold_special_counter_flags|=0x0001U;changed=true;}
    } else if (!strcmp(jbc_model,"CDE") && f.len==49) {
      const uint32_t v0=jbc_u32_from_le(f.data+28),v1=jbc_u32_from_le(f.data+32),v2=jbc_u32_from_le(f.data+36),v3=jbc_u32_from_le(f.data+40),v4=jbc_u32_from_le(f.data+44);
      if(ps.sold_cde_sold_number!=v0){ps.sold_cde_sold_number=v0;changed=true;}
      if(ps.sold_cde_energy_delivered!=v1){ps.sold_cde_energy_delivered=v1;changed=true;}
      if(ps.sold_cde_sold_total!=v2){ps.sold_cde_sold_total=v2;changed=true;}
      if(ps.sold_cde_sold_per_min!=v3){ps.sold_cde_sold_per_min=v3;changed=true;}
      if(ps.sold_cde_sold_ok!=v4){ps.sold_cde_sold_ok=v4;changed=true;}
      if(!(ps.sold_special_counter_flags&0x0004U)){ps.sold_special_counter_flags|=0x0004U;changed=true;}
    }
    if (!(ps.detail_value_flags & 0x08)) { ps.detail_value_flags |= 0x08; changed = true; }
  } else {
    if (f.len != 5) { jbc_note_decode_error(f, f.len != 5 ? (uint8_t)5 : JBC_DECODE_LEN_UNKNOWN, f.len != 5 ? (uint8_t)5 : JBC_DECODE_LEN_UNKNOWN); return; }
    const uint32_t v = jbc_u32_from_le(f.data);
    uint32_t* dst = nullptr;
    if (f.command == JBC_CMD_COUNTER_PLUG) dst = &ps.counter_plug_min;
    else if (f.command == JBC_CMD_COUNTER_WORK) dst = &ps.counter_work_min;
    else if (f.command == JBC_CMD_COUNTER_SLEEP) dst = &ps.counter_sleep_min;
    else if (f.command == JBC_CMD_COUNTER_HIBER) dst = &ps.counter_hiber_min;
    else if (f.command == JBC_CMD_COUNTER_IDLE) dst = &ps.counter_idle_min;
    else if (f.command == JBC_CMD_COUNTER_SLEEP_CYCLES) dst = &ps.counter_sleep_cycles;
    else if (f.command == JBC_CMD_COUNTER_DESOLD_CYCLES) dst = &ps.counter_desold_cycles;
    else if (f.command == JBC_CMD_COUNTER_PLUG_PARTIAL_SOLD) dst = &ps.sold_partial_plug_min;
    else if (f.command == JBC_CMD_COUNTER_WORK_PARTIAL_SOLD) dst = &ps.sold_partial_work_min;
    else if (f.command == JBC_CMD_COUNTER_SLEEP_PARTIAL_SOLD) dst = &ps.sold_partial_sleep_min;
    else if (f.command == JBC_CMD_COUNTER_HIBER_PARTIAL_SOLD) dst = &ps.sold_partial_hiber_min;
    else if (f.command == JBC_CMD_COUNTER_IDLE_PARTIAL_SOLD) dst = &ps.sold_partial_idle_min;
    else if (f.command == JBC_CMD_COUNTER_SLEEP_CYCLES_PARTIAL_SOLD) dst = &ps.sold_partial_sleep_cycles;
    else if (f.command == JBC_CMD_COUNTER_DESOLD_CYCLES_PARTIAL_SOLD) dst = &ps.sold_partial_desold_cycles;
    if (!dst) return;
    if (*dst != v) { *dst = v; changed = true; }
    // Mark each logical group only after its final k20 reply.
    if (f.command == JBC_CMD_COUNTER_IDLE && !(ps.detail_value_flags & 0x08)) {
      ps.detail_value_flags |= 0x08; changed = true;
    }
    if (f.command == JBC_CMD_COUNTER_DESOLD_CYCLES && !(ps.detail_value_flags & 0x20)) {
      ps.detail_value_flags |= 0x20; changed = true;
    }
    if (f.command == JBC_CMD_COUNTER_IDLE_PARTIAL_SOLD && !(ps.sold_extra_flags & 0x0001U)) {
      ps.sold_extra_flags |= 0x0001U; changed = true;
    }
    if (f.command == JBC_CMD_COUNTER_DESOLD_CYCLES_PARTIAL_SOLD && !(ps.sold_extra_flags & 0x0002U)) {
      ps.sold_extra_flags |= 0x0002U; changed = true;
    }
  }
  jbc_initial_low_mark_success(f, port);
  if (work_state_changed) recompute_work_masks();
  if (changed) mark_fast_changed();
}

static bool jbc_send_cl_read(uint8_t command) {
  if (jbc_station_kind != JBC_STATION_CL || jbc_frame_protocol != JBC_PROTO_02) return false;
  if (command != JBC_CMD_CL_MOTORS_STATE && command != JBC_CMD_CL_DOORS_STATE &&
      command != JBC_CMD_CL_COUNTERS && command != JBC_CMD_CL_COUNTERS_PARTIAL &&
      command != JBC_CMD_CL_CONNECT_STATUS) return false;
  const uint8_t fid = next_fid();
  return jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr,
                        fid, command, nullptr, 0, 0);
}

static bool is_cl_read_frame(const JbcFrame& f) {
  if (jbc_station_kind != JBC_STATION_CL || f.frame_protocol != JBC_PROTO_02) return false;
  return f.command == JBC_CMD_CL_MOTORS_STATE || f.command == JBC_CMD_CL_DOORS_STATE ||
         f.command == JBC_CMD_CL_COUNTERS || f.command == JBC_CMD_CL_COUNTERS_PARTIAL ||
         f.command == JBC_CMD_CL_CONNECT_STATUS;
}

static void decode_cl_read(const JbcFrame& f) {
  if (!is_cl_read_frame(f)) return;
  (void)pending_port_for_frame(f); // clear the P02 FID transaction
  JbcPortState& ps = jbc_ports[0];
  bool changed = false;

  if (f.command == JBC_CMD_CL_MOTORS_STATE || f.command == JBC_CMD_CL_DOORS_STATE) {
    if (f.len != 1) { jbc_note_decode_error(f, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN, f.len != 1 ? (uint8_t)1 : JBC_DECODE_LEN_UNKNOWN); return; }
    if (f.command == JBC_CMD_CL_MOTORS_STATE) {
      const bool value = f.data[0] != 0;
      if (!(ps.cl_flags & 0x01U) || ps.cl_motors_on != value) changed = true;
      ps.cl_motors_on = value; ps.cl_flags |= 0x01U;
    } else {
      const bool value = f.data[0] != 0;
      if (!(ps.cl_flags & 0x02U) || ps.cl_door_open != value) changed = true;
      ps.cl_door_open = value; ps.cl_flags |= 0x02U;
    }
  } else if (f.command == JBC_CMD_CL_COUNTERS || f.command == JBC_CMD_CL_COUNTERS_PARTIAL) {
    if (f.len != 20) { jbc_note_decode_error(f, f.len != 20 ? (uint8_t)20 : JBC_DECODE_LEN_UNKNOWN, f.len != 20 ? (uint8_t)20 : JBC_DECODE_LEN_UNKNOWN); return; }
    // ReceiveFrame02_CL uses BitConverter.ToInt32(...)/60 for all five fields,
    // including the two fields named *Cycles. Preserve that exact DLL behavior.
    uint32_t v[5];
    for (uint8_t i = 0; i < 5; ++i) v[i] = jbc_u32_from_le(f.data + 4U * i) / 60UL;
    if (f.command == JBC_CMD_CL_COUNTERS) {
      uint32_t* dst[5] = {&ps.cl_counter_plug_min, &ps.cl_counter_cleaning_continuous_min,
                          &ps.cl_counter_cleaning_detection_min, &ps.cl_counter_work_cycles,
                          &ps.cl_counter_door_open_cycles};
      for (uint8_t i=0;i<5;++i) if (*dst[i] != v[i]) { *dst[i]=v[i]; changed=true; }
      if (!(ps.cl_flags & 0x04U)) { ps.cl_flags |= 0x04U; changed=true; }
    } else {
      uint32_t* dst[5] = {&ps.cl_partial_plug_min, &ps.cl_partial_cleaning_continuous_min,
                          &ps.cl_partial_cleaning_detection_min, &ps.cl_partial_work_cycles,
                          &ps.cl_partial_door_open_cycles};
      for (uint8_t i=0;i<5;++i) if (*dst[i] != v[i]) { *dst[i]=v[i]; changed=true; }
      if (!(ps.cl_flags & 0x08U)) { ps.cl_flags |= 0x08U; changed=true; }
    }
  } else if (f.command == JBC_CMD_CL_CONNECT_STATUS) {
    if (!f.len) { jbc_note_decode_error(f, 1, JBC_DECODE_LEN_OPEN); return; }
    bool control = false;
    // DLL: UTF8 string, take text after ':' if present, trim, CONTROL iff "C".
    for (uint8_t i=0;i<f.len;++i) {
      const uint8_t c=f.data[i];
      if (c=='C' || c=='c') { control=true; break; }
    }
    if (!jbc_cl_control_mode_valid || jbc_cl_control_mode != control) changed = true;
    jbc_cl_control_mode = control; jbc_cl_control_mode_valid = true;
  }
  jbc_initial_low_mark_success(f, 0);
  if (changed) mark_fast_changed();
}

static bool is_station_error_frame(const JbcFrame& f) {
  const uint8_t command = jbc_station_error_command();
  return command && f.command == command;
}

static void decode_station_error(const JbcFrame& f) {
  uint16_t value = 0;
  bool valid = false;
  if (jbc_station_kind == JBC_STATION_FE) {
    if (f.len == 2) { value = get_u16_le(f.data); valid = true; }
  } else if (f.len == 1) {
    value = f.data[0]; valid = true;
  }
  if (f.frame_protocol == JBC_PROTO_02) pending_by_fid[f.fid] = PendingRequest();
  else p01_pending = PendingRequest();
  if (!valid) { jbc_note_decode_error(f); return; }
  if (jbc_station_error != value) {
    jbc_station_error = value;
    mark_fast_changed();
  }
}

static bool jbc_note_nack_and_check_excessive() {
  // Station_Com keeps all NACK timestamps from the last second and treats more
  // than three as a connection error.
  const uint32_t now = millis();
  uint8_t kept = 0;
  for (uint8_t i = 0; i < jbc_nack_times_count; ++i) {
    if ((uint32_t)(now - jbc_nack_times_ms[i]) <= 1000UL && kept < 4)
      jbc_nack_times_ms[kept++] = jbc_nack_times_ms[i];
  }
  if (kept < 4) jbc_nack_times_ms[kept++] = now;
  else {
    for (uint8_t i = 1; i < 4; ++i) jbc_nack_times_ms[i - 1] = jbc_nack_times_ms[i];
    jbc_nack_times_ms[3] = now;
    kept = 4;
  }
  jbc_nack_times_count = kept;
  return kept > 3;
}

static uint8_t jbc_in_progress_command(JbcProtocol p) {
  if (p == JBC_PROTO_01) return p01_pending.command;
  if (p == JBC_PROTO_02) {
    for (uint16_t i = 0; i < 256; ++i) if (pending_by_fid[i].command) return pending_by_fid[i].command;
  }
  return 0;
}

static void jbc_clear_in_progress(JbcProtocol p) {
  if (p == JBC_PROTO_01) p01_pending = PendingRequest();
  else if (p == JBC_PROTO_02) {
    for (uint16_t i = 0; i < 256; ++i) if (pending_by_fid[i].command) pending_by_fid[i] = PendingRequest();
  }
  if (jbc_retry_request.valid && jbc_retry_request.frame_protocol == p)
    jbc_retry_request = JbcRetryRequest();
}

static void handle_jbc_frame(const JbcFrame& f) {
  last_jbc_frame_ms = millis();
  ++jbc_rx_frames;
  jbc_clear_retry_for_frame(f);

  // During USB discovery, do not lock the wire protocol merely because some
  // syntactically valid frame happened to decode. JBC_Connect only commits to
  // P02 after the real M_HS/FID=FD discovery handshake, and to P01 after the
  // raw NAK -> SYN -> ACK sequence. This prevents random warm-start bytes from
  // turning a CL/CLMU into a false P01/P02 session.
  if (jbc_link_state != JBC_LINK_DETECT && jbc_frame_protocol == JBC_PROTO_UNKNOWN)
    jbc_frame_protocol = f.frame_protocol;

  if (f.frame_protocol == JBC_PROTO_02 && f.command == JBC_CMD_HS &&
      f.fid == JBC_HS_FID && (f.source == 0x00 || f.source == 0x10)) {
    // A P02 station can be power-cycled while the USB-powered CP210x remains
    // enumerated. In that case a new JBC handshake may arrive before the normal
    // 4.5 s silence timeout has cleared the previous logical station session.
    // Treat every new P02 handshake as a fresh station session so transient
    // station state (especially Device-ID provisioning DONE/FAILED) cannot leak
    // across a station reboot. A valid persistent Device-ID is only re-read; the
    // provisioning path still writes exclusively after an actual invalid reply.
    const bool had_session = (fast_flags & FAST_FLAG_CONNECTED) ||
                             jbc_link_state == JBC_LINK_ACTIVE ||
                             jbc_link_state == JBC_LINK_WAIT_FW;
    clear_jbc_runtime(had_session);
    last_jbc_frame_ms = millis();
    jbc_frame_protocol = JBC_PROTO_02;
    jbc_station_addr = f.source;
    jbc_host_addr = f.target;
    ++jbc_handshake_count;
    // Active handshake: station replies with ACK. Passive handshake: station
    // initiates M_HS and JBC_Connect answers with a response frame carrying ACK.
    if (!(f.len >= 1 && f.data[0] == JBC_ACK)) (void)jbc_send_handshake_ack_p02();
    set_jbc_link_state(JBC_LINK_WAIT_FW);
    (void)jbc_send_firmware();
    return;
  }

  if (f.command == JBC_CMD_FIRMWARE) {
    // QueueMessages.Response() releases the in-progress firmware request as soon
    // as the reply arrives. Do this for the initial discovery reply as well as
    // later SOLD micro-version probes so the initial scheduler is never blocked
    // by a stale firmware PendingRequest for another 500 ms.
    if (f.frame_protocol == JBC_PROTO_02) pending_by_fid[f.fid] = PendingRequest();
    else p01_pending = PendingRequest();
    if (jbc_link_state == JBC_LINK_ACTIVE) {
      // Moderate-tier UpdateMicros() replies are ordinary data refreshes, not a
      // new discovery. Source 0 on SOLD/P02 is the IMX secondary micro; a reply
      // from the station address refreshes the main protocol/model/SW/HW data.
      if (jbc_station_kind == JBC_STATION_SOLD && f.frame_protocol == JBC_PROTO_02 && f.source == 0) {
        parse_secondary_firmware_string(f.data, f.len);
      } else if (f.source == jbc_station_addr) {
        char prev_protocol[sizeof(jbc_protocol_text)]; strncpy(prev_protocol,jbc_protocol_text,sizeof(prev_protocol));
        char prev_model[sizeof(jbc_model_raw)]; strncpy(prev_model,jbc_model_raw,sizeof(prev_model));
        char prev_sw[sizeof(jbc_sw_version)]; strncpy(prev_sw,jbc_sw_version,sizeof(prev_sw));
        char prev_hw[sizeof(jbc_hw_version)]; strncpy(prev_hw,jbc_hw_version,sizeof(prev_hw));
        parse_firmware_string(f.data, f.len);
        if (strncmp(prev_protocol,jbc_protocol_text,sizeof(prev_protocol)) ||
            strncmp(prev_model,jbc_model_raw,sizeof(prev_model)) ||
            strncmp(prev_sw,jbc_sw_version,sizeof(prev_sw)) ||
            strncmp(prev_hw,jbc_hw_version,sizeof(prev_hw))) mark_fast_changed();
      }
      return;
    }
    parse_firmware_string(f.data, f.len);
    fast_flags |= FAST_FLAG_CONNECTED;
    set_jbc_link_state(JBC_LINK_ACTIVE);
    const uint32_t uid_now = millis();
    jbc_scheduler_prime_pending = true;
    // Do not probe/write the volatile ID immediately after firmware discovery.
    // JTSE can complete the JBC handshake while its Device-ID storage is still
    // starting. Begin with reads only, and allow writes after the grace period.
    jbc_uid_write_not_before_ms = uid_now + JBC_UID_WRITE_GRACE_MS;
    next_uid_poll_ms = uid_now + JBC_UID_INITIAL_READ_DELAY_MS;
    next_station_name_poll_ms = uid_now + 250UL;
    next_sold_micro_version_stage = 0;
    next_sold_micro_version_poll_ms = uid_now + 5000UL;
    mark_fast_changed();
    return;
  }

  if (jbc_link_state != JBC_LINK_ACTIVE) return;

  if (jbc_config_write_inflight && f.response &&
      f.frame_protocol == jbc_config_write_protocol &&
      f.command == jbc_config_write_command_inflight &&
      (f.frame_protocol == JBC_PROTO_01 || f.fid == jbc_config_write_fid)) {
    if (f.frame_protocol == JBC_PROTO_02) pending_by_fid[f.fid] = PendingRequest();
    else p01_pending = PendingRequest();
    jbc_config_write_inflight = false;
    const uint32_t now = millis();
    switch (jbc_config_write_state) {
      case JBC_CONFIG_WRITE_WAIT_CONTROL:
        jbc_config_write_state = JBC_CONFIG_WRITE_SEND_VALUE;
        break;
      case JBC_CONFIG_WRITE_WAIT_VALUE:
        ++jbc_config_write.command_index;
        jbc_config_write_state = jbc_config_write.command_index < jbc_config_write.command_count
                                   ? JBC_CONFIG_WRITE_SEND_VALUE
                                   : JBC_CONFIG_WRITE_LEAVE_CONTROL;
        break;
      case JBC_CONFIG_WRITE_WAIT_MONITOR:
        jbc_config_write_state = JBC_CONFIG_WRITE_VERIFY_READ;
        break;
      default:
        jbc_config_write_state = JBC_CONFIG_WRITE_LEAVE_CONTROL;
        break;
    }
    jbc_config_write_due_ms = now + 80UL;
    return;
  }

  if (jbc_config_write_state == JBC_CONFIG_WRITE_WAIT_VERIFY && f.response &&
      f.command == jbc_config_write.verify_command) {
    jbc_config_write_finish();
  }

  if (jbc_station_name_write_inflight && f.response &&
      f.frame_protocol == jbc_station_name_write_protocol &&
      f.command == jbc_station_name_write_command_inflight &&
      (f.frame_protocol == JBC_PROTO_01 || f.fid == jbc_station_name_write_fid)) {
    if (f.frame_protocol == JBC_PROTO_02) pending_by_fid[f.fid] = PendingRequest();
    else p01_pending = PendingRequest();
    jbc_station_name_write_inflight = false;
    const uint32_t now = millis();
    switch (jbc_station_name_write_state) {
      case JBC_NAME_WRITE_WAIT_CONTROL:
        jbc_station_name_write_state = JBC_NAME_WRITE_SEND_NAME;
        break;
      case JBC_NAME_WRITE_WAIT_NAME:
        jbc_station_name_write_state = JBC_NAME_WRITE_LEAVE_CONTROL;
        break;
      case JBC_NAME_WRITE_WAIT_MONITOR:
        jbc_station_name_write_state = JBC_NAME_WRITE_VERIFY_READ;
        break;
      default:
        jbc_station_name_write_state = JBC_NAME_WRITE_LEAVE_CONTROL;
        break;
    }
    jbc_station_name_write_due_ms = now + 80UL;
    return;
  }

  if (f.command == JBC_CMD_NACK) {
    if (jbc_config_write_inflight) {
      jbc_clear_in_progress(f.frame_protocol);
      jbc_config_write_inflight = false;
      const uint32_t now = millis();
      if (jbc_config_write_state == JBC_CONFIG_WRITE_WAIT_MONITOR) {
        jbc_config_write_finish();
      } else {
        jbc_config_write_state = JBC_CONFIG_WRITE_LEAVE_CONTROL;
        jbc_config_write_due_ms = now + 100UL;
      }
      return;
    }
    if (jbc_station_name_write_inflight) {
      jbc_clear_in_progress(f.frame_protocol);
      jbc_station_name_write_inflight = false;
      const uint32_t now = millis();
      if (jbc_station_name_write_state == JBC_NAME_WRITE_WAIT_MONITOR) {
        jbc_station_name_write_finish();
        next_station_name_poll_ms = now + 250UL;
      } else {
        jbc_station_name_write_state = JBC_NAME_WRITE_LEAVE_CONTROL;
        jbc_station_name_write_due_ms = now + 100UL;
      }
      return;
    }
    // Station_Com reports a logical connection error after >3 NACKs/s. OFE's
    // single-flight transport has no separate PC-side Station_Com connection to
    // tear down, so do NOT turn that logical error into a physical USB/JBC
    // rediscovery. Drop the in-progress request and keep the active frame session.
    if (jbc_note_nack_and_check_excessive()) {
      jbc_clear_in_progress(f.frame_protocol);
      memset(jbc_nack_times_ms, 0, sizeof(jbc_nack_times_ms));
      jbc_nack_times_count = 0;
      return;
    }

    // QueueMessages NACK semantics are intentionally shape-specific:
    //   len 0                 -> close the current request
    //   len 5, error != 1     -> close only if data[1] matches its command
    //   error 1 (BCC) / other -> keep it in progress for timeout+retry
    bool close_request = false;
    if (f.len == 0) close_request = true;
    else if (f.len == 5 && f.data[0] != 1 && jbc_in_progress_command(f.frame_protocol) == f.data[1])
      close_request = true;
    if (close_request) jbc_clear_in_progress(f.frame_protocol);
    return;
  }

  if (is_device_uid_frame(f)) {
    decode_device_uid(f);
    return;
  }

  if (is_station_name_frame(f)) {
    decode_station_name(f);
    return;
  }

  if (jbc_station_kind == JBC_STATION_SOLD &&
      (f.command == JBC_CMD_SLEEP_DELAY_SOLD || f.command == JBC_CMD_HIBER_DELAY_SOLD)) {
    decode_sold_delay_setting(f);
    return;
  }

  if (jbc_station_kind == JBC_STATION_SOLD && f.frame_protocol == JBC_PROTO_02 &&
      f.command == JBC_CMD_DELAY_TIME_P02_SOLD) {
    decode_delay_time(f);
    return;
  }

  if (is_cl_read_frame(f)) {
    decode_cl_read(f);
    return;
  }

  if (is_sold_p01_extra_frame(f)) {
    decode_sold_p01_extra(f);
    return;
  }

  if (is_sold_qst_frame(f)) {
    decode_sold_qst(f);
    return;
  }

  if (is_sold_lock_port_frame(f)) {
    decode_sold_lock_port(f);
    return;
  }

  if ((jbc_station_kind == JBC_STATION_SOLD || jbc_station_kind == JBC_STATION_HA ||
       jbc_station_kind == JBC_STATION_PH || jbc_station_kind == JBC_STATION_SF) &&
      f.command == JBC_CMD_CONTI_READ) {
    decode_conti_read(f);
    return;
  }

  if (is_sold_mos_temp_frame(f)) {
    decode_sold_mos_temp(f);
    return;
  }

  if (is_sold_ale_feeder_frame(f)) {
    decode_sold_ale_feeder(f);
    return;
  }

  if (is_sold_station_read_frame(f)) {
    decode_sold_station_read(f);
    return;
  }

  if (is_sold_diag_frame(f)) {
    decode_sold_diag(f);
    return;
  }

  if (is_sold_detail_frame(f)) {
    decode_sold_detail(f);
    return;
  }

  if (is_ha_connect_status_frame(f)) {
    decode_ha_connect_status(f);
    return;
  }

  if (is_ha_station_diag_frame(f)) {
    decode_ha_station_diag(f);
    return;
  }

  if (is_ha_detail_frame(f)) {
    decode_ha_detail(f);
    return;
  }

  if (is_ph_read_frame(f)) {
    decode_ph_read(f);
    return;
  }

  if (is_fe_read_frame(f)) {
    decode_fe_read(f);
    return;
  }

  if (is_sf_read_frame(f)) {
    decode_sf_read(f);
    return;
  }

  if (is_station_error_frame(f)) {
    decode_station_error(f);
    return;
  }

  if (f.command == JBC_CMD_INFO_PORT) {
    decode_info_port(f);
    return;
  }

  if ((jbc_station_kind == JBC_STATION_SOLD || jbc_station_kind == JBC_STATION_HA) &&
      f.command == JBC_CMD_CONTI_WRITE) {
    // WriteContiMode is followed by ReadContiMode; clear only the transport
    // pending marker here. The 0x80 reply remains authoritative for speed/mask.
    if (f.frame_protocol == JBC_PROTO_02) pending_by_fid[f.fid] = PendingRequest();
    else p01_pending = PendingRequest();
    return;
  }

  if ((jbc_station_kind == JBC_STATION_SOLD || jbc_station_kind == JBC_STATION_HA) &&
      f.command == JBC_CMD_CONTI_INFO) {
    decode_conti_info(f);
    return;
  }
}

static bool frame_matches_protocol(JbcProtocol p) {
  if (rx_logical_len < 7 || rx_logical[0] != JBC_STX || rx_logical[rx_logical_len - 1] != JBC_ETX) return false;
  if (p == JBC_PROTO_01) return rx_logical_len >= 7 && (size_t)rx_logical[4] + 7U == rx_logical_len;
  if (p == JBC_PROTO_02) return rx_logical_len >= 8 && (size_t)rx_logical[5] + 8U == rx_logical_len;
  return false;
}

static void finish_rx_frame() {
  if (rx_logical_len < 7 || rx_logical[0] != JBC_STX || rx_logical[rx_logical_len - 1] != JBC_ETX) {
    ++jbc_frame_errors;
    local_trace_log(1, 0, 0xEF, rx_logical, rx_logical_len);
    return;
  }
  uint8_t x = 0;
  for (size_t i = 0; i < rx_logical_len; ++i) x ^= rx_logical[i];
  if (x != 0) { ++jbc_bcc_errors; local_trace_log(1, 0, 0xEE, rx_logical, rx_logical_len); return; }

  JbcProtocol p = jbc_frame_protocol;
  if (p == JBC_PROTO_UNKNOWN) {
    const bool p01 = frame_matches_protocol(JBC_PROTO_01);
    const bool p02 = frame_matches_protocol(JBC_PROTO_02);
    if (p02 && !p01) p = JBC_PROTO_02;
    else if (p01 && !p02) p = JBC_PROTO_01;
    else if (p02 && rx_logical_len > 5 && rx_logical[3] == JBC_HS_FID) p = JBC_PROTO_02;
    else { ++jbc_frame_errors; local_trace_log(1, 0, 0xEF, rx_logical, rx_logical_len); return; }
  }
  if (!frame_matches_protocol(p)) {
    ++jbc_frame_errors;
    local_trace_log(1, 0, 0xEF, rx_logical, rx_logical_len);
    return;
  }

  JbcFrame f;
  f.frame_protocol = p;
  const uint8_t source_raw = rx_logical[1];
  f.response = (source_raw & 0x80) != 0;
  f.source = source_raw & 0x7F;
  f.target = rx_logical[2] & 0x7F;
  if (p == JBC_PROTO_02) {
    f.fid = rx_logical[3]; f.command = rx_logical[4]; f.len = rx_logical[5];
    if (f.len) memcpy(f.data, rx_logical + 6, f.len);
  } else {
    f.fid = 0; f.command = rx_logical[3]; f.len = rx_logical[4];
    if (f.len) memcpy(f.data, rx_logical + 5, f.len);
  }
  local_trace_log_jbc(1, (uint8_t)p, f.source, f.target, f.fid, f.command, f.data, f.len);
  handle_jbc_frame(f);
}

static void feed_jbc_framed_byte(uint8_t b) {
  switch (rx_state) {
    case RX_WAIT_DLE:
      if (b == JBC_DLE) rx_state = RX_WAIT_STX;
      break;
    case RX_WAIT_STX:
      if (b == JBC_STX) { rx_logical_len = 0; rx_logical[rx_logical_len++] = JBC_STX; rx_state = RX_FRAME; }
      else if (b != JBC_DLE) rx_state = RX_WAIT_DLE;
      break;
    case RX_FRAME:
      if (b == JBC_DLE) rx_state = RX_AFTER_DLE;
      else if (rx_logical_len < sizeof(rx_logical)) rx_logical[rx_logical_len++] = b;
      else { ++jbc_frame_errors; rx_state = RX_WAIT_DLE; }
      break;
    case RX_AFTER_DLE:
      if (b == JBC_DLE) {
        if (rx_logical_len < sizeof(rx_logical)) rx_logical[rx_logical_len++] = JBC_DLE;
        rx_state = RX_FRAME;
      } else if (b == JBC_ETX) {
        if (rx_logical_len < sizeof(rx_logical)) rx_logical[rx_logical_len++] = JBC_ETX;
        finish_rx_frame(); rx_state = RX_WAIT_DLE;
      } else if (b == JBC_STX) {
        rx_logical_len = 0; rx_logical[rx_logical_len++] = JBC_STX; rx_state = RX_FRAME;
      } else { ++jbc_frame_errors; rx_state = RX_WAIT_DLE; }
      break;
  }
}

static uint32_t next_handshake_ms = 0;
static uint32_t detect_started_ms = 0;
static uint8_t detect_reopen_count = 0;
static bool detect_p01_nak_pending = false;
static uint32_t detect_p01_nak_since_ms = 0;

static void reset_jbc_rx_decoder() {
  rx_state = RX_WAIT_DLE;
  rx_logical_len = 0;
  detect_p01_nak_pending = false;
  detect_p01_nak_since_ms = 0;
}

// Mirror JBC_Connect's Close/Dispose -> Open -> DiscardIn/Out behavior at the
// CP210x UART layer.  The USB device itself stays enumerated on ESP32-S3, but
// every failed discovery attempt gets a fresh UART session, empty host RX queue
// and a fresh P01/P02 decoder.  Keep the low/high/low modem-output compatibility
// cycle from 0.1.41; unlike the old code it can no longer carry parser state or
// stale bytes from one pass into the next one.
static void restart_jbc_discovery(bool mark_change, bool cycle_uart) {
  clear_jbc_runtime(mark_change);
  reset_jbc_rx_decoder();
  if (usb_rx_stream) xStreamBufferReset(usb_rx_stream);
  set_jbc_link_state(JBC_LINK_DETECT);

  const uint32_t now = millis();
  detect_started_ms = now;
  if (cycle_uart && usb_serial_ready()) {
    const bool wake_modem_outputs = (detect_reopen_count % 3U) == 1U;
    (void)usb_serial_reopen_for_discovery(wake_modem_outputs);
    detect_reopen_count = (uint8_t)((detect_reopen_count + 1U) % 3U);
  }
  next_handshake_ms = millis() + 250UL;
}

static bool all_bytes_are(const uint8_t* data, size_t len, uint8_t value) {
  if (!data || !len) return false;
  for (size_t i = 0; i < len; ++i) if (data[i] != value) return false;
  return true;
}

static void feed_jbc_framed_block(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) feed_jbc_framed_byte(data[i]);
}

static void poll_jbc_rx() {
  if (!usb_rx_stream) return;
  uint8_t buf[128];
  size_t n;
  while ((n = xStreamBufferReceive(usb_rx_stream, buf, sizeof(buf), 0)) > 0) {
    if (jbc_link_state == JBC_LINK_DETECT) {
      // JBC_Connect WaitNAKorHS is block based, not byte based:
      //   * a non-DLE block is P01 only when EVERY received byte is NAK (0x15)
      //   * a DLE block must decode as a real P02 frame; malformed data closes
      //     the serial connection and starts discovery again.
      // This is the key guard against a warm CLMU byte stream accidentally
      // becoming the old OFE "P01: ACK" false-positive session.
      if (rx_state == RX_WAIT_DLE && buf[0] != JBC_DLE) {
        // Keep raw P01 discovery bytes visible in Bus Diagnose without changing
        // the DLL-faithful block validation below.
        local_trace_log(1, n ? buf[0] : 0, 0xF0, buf, n);
        if (!all_bytes_are(buf, n, JBC_NAK)) {
          ++jbc_handshake_errors;
          restart_jbc_discovery(false, true);
          return;
        }
        // JBCSerialPort's receive worker runs on a 10 ms cadence, so bytes that
        // arrive together are normally validated as one block. OFE polls much
        // faster; hold an all-NAK candidate for the same small window so a lone
        // NAK inside a noisy warm-start stream cannot be accepted before the
        // following garbage bytes have arrived.
        if (!detect_p01_nak_pending) {
          detect_p01_nak_pending = true;
          detect_p01_nak_since_ms = millis();
        }
        continue;
      }

      if (detect_p01_nak_pending) {
        // A DLE/framed byte arriving in the same 10 ms discovery window makes
        // the candidate mixed data, exactly the case the DLL rejects.
        ++jbc_handshake_errors;
        restart_jbc_discovery(false, true);
        return;
      }

      const uint32_t frame_errors_before = jbc_frame_errors;
      const uint32_t bcc_errors_before = jbc_bcc_errors;
      const uint32_t frames_before = jbc_rx_frames;
      feed_jbc_framed_block(buf, n);
      if (jbc_link_state != JBC_LINK_DETECT) continue; // valid P02 HS won

      // A complete malformed frame, or any complete non-handshake frame during
      // discovery, is equivalent to JBC_Connect failing Decode/HS validation:
      // close this attempt and create a fresh serial/parser session.
      if (jbc_frame_errors != frame_errors_before || jbc_bcc_errors != bcc_errors_before ||
          jbc_rx_frames != frames_before) {
        restart_jbc_discovery(false, true);
        return;
      }
      continue; // partial DLE frame: keep collecting until complete or timeout
    }

    if (jbc_link_state == JBC_LINK_P01_WAIT_ACK) {
      local_trace_log(1, n ? buf[0] : 0, 0xF0, buf, n);
      // DLL WaitACK rejects any byte other than ACK/NAK.  It only advances when
      // the final received handshake byte is ACK; garbage immediately restarts.
      uint8_t last = 0;
      for (size_t i = 0; i < n; ++i) {
        if (buf[i] != JBC_ACK && buf[i] != JBC_NAK) {
          ++jbc_handshake_errors;
          restart_jbc_discovery(false, true);
          return;
        }
        last = buf[i];
      }
      if (last == JBC_ACK) {
        last_jbc_frame_ms = millis();
        (void)jbc_send_raw_byte(JBC_ACK);
        set_jbc_link_state(JBC_LINK_P01_WAIT_ADDR);
      }
      continue;
    }

    if (jbc_link_state == JBC_LINK_P01_WAIT_ADDR) {
      if (n) local_trace_log(1, buf[0], 0xF0, buf, 1);
      // JBC_Connect reads exactly one raw address byte here.  If more bytes are
      // already queued, they belong to the following framed firmware response.
      last_jbc_frame_ms = millis();
      jbc_host_addr = buf[0];
      jbc_station_addr = 0;
      (void)jbc_send_raw_byte(JBC_ACK);
      ++jbc_handshake_count;
      set_jbc_link_state(JBC_LINK_WAIT_FW);
      (void)jbc_send_firmware();
      if (n > 1) feed_jbc_framed_block(buf + 1, n - 1);
      continue;
    }

    feed_jbc_framed_block(buf, n);
  }
}

static uint8_t next_poll_port = 0;
// SOLD/P02 UpdateData reads InfoPort and DelayTime as a serialized pair.
// Single-flight means they cannot be emitted back-to-back in one scheduler pass.
static bool next_sold_port_poll_delay_phase = false;
static uint32_t next_port_poll_ms = 0;
static uint32_t next_station_error_poll_ms = 0;
static uint32_t next_sold_mos_poll_ms = 0;
static uint8_t next_sold_mos_port = 0;
static uint32_t next_sold_station_status_poll_ms = 0;
static uint32_t next_sold_robot_status_poll_ms = 0;
static uint32_t next_sold_selected_profile_poll_ms = 0;
static uint8_t next_sold_selected_profile_port = 0;
static uint32_t next_sold_connect_poll_ms = 0;
static uint32_t next_sold_station_param_poll_ms = 0;
static uint8_t next_sold_station_param_stage = 0;
static bool next_sold_station_param_fast = false;
static uint32_t next_sold_temp_error_poll_ms = 0;
static bool next_sold_temp_error_mos = false;
static uint32_t next_sold_diag_poll_ms = 0;
static uint8_t next_sold_diag_port = 0;
static uint8_t next_sold_diag_stage = 0;
static uint32_t next_sold_selected_temp_poll_ms = 0;
static uint8_t next_sold_selected_temp_port = 0;
static bool next_sold_selected_temp_fast = false;
static uint32_t next_sold_delay_setting_poll_ms = 0;
static uint8_t next_sold_delay_setting_port = 0;
static bool next_sold_delay_setting_hiber = false;
static uint32_t next_sold_detail_poll_ms = 0;
static uint8_t next_sold_detail_port = 0;
static uint8_t next_sold_detail_stage = 0;
static uint32_t next_sold_counter_poll_ms = 0;
static uint8_t next_sold_counter_port = 0;
static uint8_t next_sold_counter_stage = 0;
static bool next_sold_counter_fast = false;
static uint32_t next_sold_tool_refresh_fast_until_ms = 0;
static uint32_t next_sold_p01_detail_poll_ms = 0;
static uint8_t next_sold_p01_detail_port = 0;
static uint8_t next_sold_p01_detail_stage = 0;
static uint32_t next_sold_p01_counter_poll_ms = 0;
static uint8_t next_sold_p01_counter_stage = 0;
static bool next_sold_p01_counter_fast = false;
static uint32_t next_sold_tool_status_poll_ms = 0;
static uint8_t next_sold_tool_status_port = 0;
static uint32_t next_sold_qst_poll_ms = 0;
static bool next_sold_qst_status = false;
static uint32_t next_sold_lock_poll_ms = 0;
static uint8_t next_sold_lock_port = 0;
static uint32_t next_conti_read_poll_ms = 0;
static uint8_t next_sold_telemetry_port = 0;
static uint8_t next_sold_telemetry_stage = 0; // D7,D8,DA,DB,D3(peripheral metadata),DD,DC,DF,D5(ALE),D4(ALE/CDE extended counters)
static uint32_t next_sold_extra_station_poll_ms = 0;
static uint8_t next_sold_extra_station_stage = 0;
static uint32_t next_sold_peripheral_status_poll_ms = 0;
static uint8_t next_sold_peripheral_status_id = 0;
static uint32_t next_sold_peripheral_config_poll_ms = 0;
static uint8_t next_sold_peripheral_config_stage = 0; // 0=count, 1..N=config ID 0..N-1
static uint32_t next_sold_ale_feeder_poll_ms = 0;
static uint8_t next_sold_ale_feeder_stage = 0; // 0=config, 1..5=programs 0..4
static uint32_t next_ha_selected_poll_ms = 0;
static uint8_t next_ha_selected_port = 0;
static uint8_t next_ha_selected_stage = 0;
static bool next_ha_selected_fast = false;
static uint32_t next_ha_ext_temp_poll_ms = 0;
static uint8_t next_ha_ext_temp_port = 0;
static uint32_t next_ha_counter_poll_ms = 0;
static uint8_t next_ha_counter_port = 0;
static uint8_t next_ha_counter_stage = 0;
static bool next_ha_counter_fast = false;
static uint32_t next_ha_tool_refresh_fast_until_ms = 0;
static uint32_t next_ha_detail_poll_ms = 0;
static uint32_t next_ha_station_status_poll_ms = 0;
static uint32_t next_ha_station_diag_poll_ms = 0;
static uint8_t next_ha_station_diag_stage = 0;
static bool next_ha_station_diag_fast = false;
static uint8_t next_ha_detail_port = 0;
static uint8_t next_ha_detail_stage = 0;
static uint8_t next_ha_telemetry_stage = 0; // D7, D9, DE
// PH UpdateData scheduler: TC temperatures ~1 s, warnings ~5 s, station/tool
// parameters ~15 s and counters ~60 s, matching the original DLL tiers while
// spreading individual USB transactions to keep the CP210x request queue small.
static uint32_t next_ph_tc_poll_ms = 0;
static uint8_t next_ph_tc_channel = 0;
static uint32_t next_ph_warning_poll_ms = 0;
static uint8_t next_ph_warning_channel = 0;
static uint32_t next_ph_station_poll_ms = 0;
static uint8_t next_ph_station_stage = 0;
static bool next_ph_station_fast = false;
static uint32_t next_ph_port_detail_poll_ms = 0;
static uint8_t next_ph_port_detail_stage = 0;
static bool next_ph_port_detail_fast = false;
static uint32_t next_ph_counter_poll_ms = 0;
static uint8_t next_ph_counter_stage = 0;
static bool next_ph_counter_fast = false;
static uint8_t next_ph_telemetry_stage = 0; // E4 station, E5 port, E6 profile, E7 teach
static uint8_t next_ph_profile_tx_index = 0;
static uint8_t next_ph_teach_tx_index = 0;
// FE UpdateData scheduler. Six per-port reads are spread across the ~1 s
// UpdatePortInfo tier; station status/parameters and counters use the DLL tiers.
static uint32_t next_fe_port_detail_poll_ms = 0;
static uint8_t next_fe_port_detail_port = 0;
static uint8_t next_fe_port_detail_stage = 0;
static uint32_t next_fe_continuous_poll_ms = 0;
static uint32_t next_fe_robot_status_poll_ms = 0;
static uint32_t next_fe_station_param_poll_ms = 0;
static uint8_t next_fe_station_param_stage = 0;
static bool next_fe_station_param_fast = false;
static uint32_t next_fe_counter_poll_ms = 0;
static bool next_fe_counter_partial = false;
static bool next_fe_counter_fast = false;
static uint8_t next_fe_telemetry_stage = 0; // E8 station, E9 one port
static uint8_t next_fe_telemetry_port = 0;
// SF UpdateData scheduler. ReadFeeding follows the fast port tier, station
// status every ~5 s, settings/programs/tool parameters are spread over the
// ~15 s tier, and global/partial counters refresh approximately once/minute.
static uint32_t next_sf_feeding_poll_ms = 0;
static uint32_t next_sf_robot_status_poll_ms = 0;
static uint32_t next_sf_station_param_poll_ms = 0;
static uint8_t next_sf_station_param_stage = 0;
static uint8_t next_sf_program_poll_index = 0;
static bool next_sf_station_param_fast = false;
static uint32_t next_sf_tool_param_poll_ms = 0;
static uint8_t next_sf_tool_param_stage = 0;
static bool next_sf_tool_param_fast = false;
static uint32_t next_sf_counter_poll_ms = 0;
static bool next_sf_counter_partial = false;
static bool next_sf_counter_fast = false;
static uint8_t next_sf_telemetry_stage = 0; // EA station, EB port, EC programs
static uint8_t next_sf_program_tx_index = 0;
static uint8_t poll_port_limit() {
  if (jbc_port_count >= 1 && jbc_port_count <= JBC_MAX_PORTS) return jbc_port_count;
  return JBC_MAX_PORTS; // unknown/new model: safely probe all four possible ports
}

static int8_t jbc_initial_low_stage_for_command(uint8_t command) {
  if (jbc_station_kind == JBC_STATION_SOLD) {
    if (jbc_frame_protocol == JBC_PROTO_01) {
      static const uint8_t cmds[] = {
        JBC_CMD_COUNTER_PLUG,JBC_CMD_COUNTER_WORK,JBC_CMD_COUNTER_SLEEP,JBC_CMD_COUNTER_HIBER,
        JBC_CMD_COUNTER_IDLE,JBC_CMD_COUNTER_SLEEP_CYCLES,JBC_CMD_COUNTER_DESOLD_CYCLES,
        JBC_CMD_COUNTER_PLUG_PARTIAL_P01_SOLD,JBC_CMD_COUNTER_WORK_PARTIAL_P01_SOLD,
        JBC_CMD_COUNTER_SLEEP_PARTIAL_P01_SOLD,JBC_CMD_COUNTER_HIBER_PARTIAL_P01_SOLD,
        JBC_CMD_COUNTER_IDLE_PARTIAL_P01_SOLD,JBC_CMD_COUNTER_SLEEP_CYCLES_PARTIAL_P01_SOLD,
        JBC_CMD_COUNTER_DESOLD_CYCLES_PARTIAL_P01_SOLD};
      for (uint8_t i=0;i<(uint8_t)sizeof(cmds);++i) if (cmds[i] == command) return (int8_t)i;
    } else if (jbc_frame_protocol == JBC_PROTO_02) {
      if (sold_k26_protocol()) {
        if (command == JBC_CMD_COUNTER_PLUG) return 0;
        if (command == JBC_CMD_COUNTER_WORK) return 1;
      } else {
        static const uint8_t cmds[] = {
          JBC_CMD_COUNTER_PLUG,JBC_CMD_COUNTER_WORK,JBC_CMD_COUNTER_SLEEP,JBC_CMD_COUNTER_HIBER,
          JBC_CMD_COUNTER_IDLE,JBC_CMD_COUNTER_SLEEP_CYCLES,JBC_CMD_COUNTER_DESOLD_CYCLES,
          JBC_CMD_COUNTER_PLUG_PARTIAL_SOLD,JBC_CMD_COUNTER_WORK_PARTIAL_SOLD,
          JBC_CMD_COUNTER_SLEEP_PARTIAL_SOLD,JBC_CMD_COUNTER_HIBER_PARTIAL_SOLD,
          JBC_CMD_COUNTER_IDLE_PARTIAL_SOLD,JBC_CMD_COUNTER_SLEEP_CYCLES_PARTIAL_SOLD,
          JBC_CMD_COUNTER_DESOLD_CYCLES_PARTIAL_SOLD};
        for (uint8_t i=0;i<(uint8_t)sizeof(cmds);++i) if (cmds[i] == command) return (int8_t)i;
      }
    }
  } else if (jbc_station_kind == JBC_STATION_HA) {
    static const uint8_t cmds[] = {JBC_CMD_COUNTER_PLUG_HA,JBC_CMD_COUNTER_WORK_HA,
      JBC_CMD_COUNTER_WORK_CYCLES_HA,JBC_CMD_COUNTER_SUCTION_CYCLES_HA,
      JBC_CMD_COUNTER_PLUG_PARTIAL_HA,JBC_CMD_COUNTER_WORK_PARTIAL_HA,
      JBC_CMD_COUNTER_WORK_CYCLES_PARTIAL_HA,JBC_CMD_COUNTER_SUCTION_CYCLES_PARTIAL_HA};
    for (uint8_t i=0;i<(uint8_t)sizeof(cmds);++i) if (cmds[i] == command) return (int8_t)i;
  } else if (jbc_station_kind == JBC_STATION_PH) {
    static const uint8_t cmds[] = {JBC_CMD_COUNTER_PLUG_PH,JBC_CMD_COUNTER_WORK_PH,
      JBC_CMD_COUNTER_WORK_CYCLES_PH,JBC_CMD_COUNTER_PLUG_PARTIAL_PH,
      JBC_CMD_COUNTER_WORK_PARTIAL_PH,JBC_CMD_COUNTER_WORK_CYCLES_PARTIAL_PH};
    for (uint8_t i=0;i<(uint8_t)sizeof(cmds);++i) if (cmds[i] == command) return (int8_t)i;
  } else if (jbc_station_kind == JBC_STATION_FE) {
    if (command == JBC_CMD_COUNTERS_FE) return 0;
    if (command == JBC_CMD_COUNTERS_PARTIAL_FE) return 1;
  } else if (jbc_station_kind == JBC_STATION_SF) {
    if (command == JBC_CMD_COUNTERS_SF) return 0;
    if (command == JBC_CMD_COUNTERS_PARTIAL_SF) return 1;
  } else if (jbc_station_kind == JBC_STATION_CL) {
    if (command == JBC_CMD_CL_COUNTERS) return 0;
    if (command == JBC_CMD_CL_COUNTERS_PARTIAL) return 1;
  }
  return -1;
}

static int8_t jbc_initial_low_bit_for_stage(uint8_t port, uint8_t stage) {
  uint8_t stages = 0;
  bool per_port = false;
  if (jbc_station_kind == JBC_STATION_SOLD) {
    if (jbc_frame_protocol == JBC_PROTO_01) { stages = 14; per_port = false; }
    else if (jbc_frame_protocol == JBC_PROTO_02) { stages = sold_k26_protocol() ? 2U : 14U; per_port = true; }
  } else if (jbc_station_kind == JBC_STATION_HA) { stages = 8; per_port = true; }
  else if (jbc_station_kind == JBC_STATION_PH) { stages = 6; per_port = false; }
  else if (jbc_station_kind == JBC_STATION_FE || jbc_station_kind == JBC_STATION_SF ||
           jbc_station_kind == JBC_STATION_CL) { stages = 2; per_port = false; }
  if (!stages || stage >= stages) return -1;
  const uint16_t bit = per_port ? (uint16_t)port * stages + stage : stage;
  return bit < 64U ? (int8_t)bit : -1;
}

static void jbc_initial_low_begin() {
  jbc_initial_low = JbcInitialLowTracker();
  const uint8_t limit = max((uint8_t)1, poll_port_limit());
  if (jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_01) {
    const uint8_t stages = sold_supports_p01_partial_counters() ? 14U : 7U;
    for (uint8_t st=0; st<stages; ++st) jbc_initial_low.expected |= (1ULL << st);
  } else if (jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_02) {
    const uint8_t stages = sold_k26_protocol() ? 2U : 14U;
    for (uint8_t port=0; port<limit; ++port) {
      for (uint8_t st=0; st<stages; ++st) {
        const bool partial = sold_k26_protocol() ? (st == 1U) : (st >= 7U);
        if (partial && !sold_supports_partial_counters()) continue;
        const int8_t bit = jbc_initial_low_bit_for_stage(port, st);
        if (bit >= 0) jbc_initial_low.expected |= (1ULL << bit);
      }
    }
  } else if (jbc_station_kind == JBC_STATION_HA && jbc_frame_protocol == JBC_PROTO_02) {
    for (uint8_t port=0; port<limit; ++port) for (uint8_t st=0; st<8U; ++st) {
      if (st >= 4U && !ha_supports_partial_counters()) continue;
      const int8_t bit = jbc_initial_low_bit_for_stage(port, st);
      if (bit >= 0) jbc_initial_low.expected |= (1ULL << bit);
    }
  } else if (jbc_station_kind == JBC_STATION_PH && jbc_frame_protocol == JBC_PROTO_02) {
    for (uint8_t st=0; st<6U; ++st) jbc_initial_low.expected |= (1ULL << st);
  } else if ((jbc_station_kind == JBC_STATION_FE || jbc_station_kind == JBC_STATION_SF ||
              jbc_station_kind == JBC_STATION_CL) && jbc_frame_protocol == JBC_PROTO_02) {
    jbc_initial_low.expected = 0x3ULL;
  }
  jbc_initial_low.tracking = jbc_initial_low.expected != 0;
  // DeviceName is 15 s for SOLD/HA/PH/SF. FE/CL use the safe 60 s fallback,
  // so cold-start completion must also keep their initial name read alive.
  jbc_initial_station_name_pending = jbc_station_kind == JBC_STATION_FE || jbc_station_kind == JBC_STATION_CL;
}

static bool jbc_initial_low_stage_done(uint8_t port, uint8_t stage) {
  if (!jbc_initial_low.tracking) return false;
  const int8_t bit = jbc_initial_low_bit_for_stage(port, stage);
  if (bit < 0) return true;
  const uint64_t mask = 1ULL << bit;
  // Stages excluded by feature gating are considered skipped during the one-time
  // completion pass; expected stages are skipped only after a valid response.
  return !(jbc_initial_low.expected & mask) || (jbc_initial_low.done & mask);
}

static void jbc_initial_low_mark_success(const JbcFrame& f, uint8_t port) {
  if (!jbc_initial_low.tracking || !f.response) return;
  const int8_t stage = jbc_initial_low_stage_for_command(f.command);
  if (stage < 0) return;
  const uint8_t mapped_port = (jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_02) ||
                              jbc_station_kind == JBC_STATION_HA ? port : 0;
  const int8_t bit = jbc_initial_low_bit_for_stage(mapped_port, (uint8_t)stage);
  if (bit < 0) return;
  const uint64_t mask = 1ULL << bit;
  if (jbc_initial_low.expected & mask) jbc_initial_low.done |= mask;
}

static bool jbc_initial_low_complete() {
  return jbc_initial_low.tracking && jbc_initial_low.expected &&
         (jbc_initial_low.done & jbc_initial_low.expected) == jbc_initial_low.expected;
}

static bool jbc_initial_low_finish_or_verify() {
  if (!jbc_initial_low_complete()) return false;
  ++jbc_initial_low.completed_passes;
  if (jbc_initial_low.completed_passes >= JBC_INITIAL_LOW_REQUIRED_PASSES) {
    jbc_initial_low_finish();
    return true;
  }
  // A cold station may have returned syntactically valid boot placeholders.
  // Re-read the complete Low tier on the next fast round.
  jbc_initial_low.done = 0;
  return false;
}

static bool jbc_initial_low_opportunistic_request(uint8_t command, uint8_t port) {
  // FE/CL station name is their only additional 60 s fallback read.  A missing
  // name should rotate quickly too; one valid reply is enough for a string.
  if (jbc_initial_station_name_pending && command == jbc_station_name_read_command()) return true;
  if (!jbc_initial_low.tracking) return false;
  const int8_t stage = jbc_initial_low_stage_for_command(command);
  if (stage < 0) return false;
  uint8_t mapped_port = 0;
  if ((jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_02) ||
      jbc_station_kind == JBC_STATION_HA) {
    if (port >= JBC_MAX_PORTS) return false;
    mapped_port = port;
  }
  const int8_t bit = jbc_initial_low_bit_for_stage(mapped_port, (uint8_t)stage);
  if (bit < 0) return false;
  const uint64_t mask = 1ULL << bit;
  return (jbc_initial_low.expected & mask) && !(jbc_initial_low.done & mask);
}

static void jbc_initial_low_finish() {
  jbc_initial_low.tracking = false;
}

static void poll_jbc_protocol() {
  if (!usb_serial_ready()) {
    if (jbc_link_state != JBC_LINK_USB_DOWN) { clear_jbc_runtime(true); set_jbc_link_state(JBC_LINK_USB_DOWN); }
    return;
  }
  if (jbc_link_state == JBC_LINK_USB_DOWN) {
    clear_jbc_runtime(false);
    reset_jbc_rx_decoder();
    if (usb_rx_stream) xStreamBufferReset(usb_rx_stream);
    set_jbc_link_state(JBC_LINK_DETECT);
    detect_started_ms = millis();
    detect_reopen_count = 0;
    next_handshake_ms = detect_started_ms + 250UL; // JBC_Connect waits ~200 ms after opening
  }

  const uint32_t now = millis();
  if (jbc_link_state == JBC_LINK_DETECT) {
    if (detect_p01_nak_pending && (uint32_t)(now - detect_p01_nak_since_ms) >= 10UL) {
      detect_p01_nak_pending = false;
      last_jbc_frame_ms = now;
      jbc_frame_protocol = JBC_PROTO_01;
      jbc_command_protocol = JBC_PROTO_UNKNOWN;
      (void)jbc_send_raw_byte(JBC_SYN);
      set_jbc_link_state(JBC_LINK_P01_WAIT_ACK);
      return;
    }
    // Normal JBC_Connect discovery is passive: open COM, wait about 200 ms for
    // P01 NAK or station-initiated P02 HS, then Close/Dispose and try again.
    // Do not inject an active P02 handshake here: the original normal USB search
    // does not do that, and it can mask a bad parser state after a warm reboot.
    if ((int32_t)(now - next_handshake_ms) >= 0) {
      restart_jbc_discovery(false, true);
    }
    return;
  }
  if (jbc_link_state == JBC_LINK_P01_WAIT_ACK || jbc_link_state == JBC_LINK_P01_WAIT_ADDR) {
    if ((uint32_t)(now - jbc_state_since_ms) > 500UL) restart_jbc_discovery(false, true);
    return;
  }
  if (jbc_link_state == JBC_LINK_WAIT_FW) {
    // JBC_Connect treats firmware discovery as part of the same short serial
    // search attempt. Keep a little extra margin for embedded scheduling, but
    // never remain stuck for seconds in a half-discovered protocol session.
    if ((uint32_t)(now - jbc_state_since_ms) > 1200UL) restart_jbc_discovery(true, true);
    return;
  }
  if (jbc_link_state == JBC_LINK_ACTIVE) {
    if (last_jbc_frame_ms && (uint32_t)(now - last_jbc_frame_ms) > 4500UL) {
      restart_jbc_discovery(true, true); return;
    }
    // Read the real JBC station Device-ID/UUID. If (and only if) the station
    // actually replies with an invalid/empty ID, mirror JBC_Connect.TrySetUUID():
    // CONTROL -> create/write ID -> read-back verify -> MONITOR.
    poll_jbc_uid_provisioning();
    const bool uid_provisioning = jbc_uid_provision_state != JBC_UID_PROVISION_IDLE &&
                                  jbc_uid_provision_state != JBC_UID_PROVISION_DONE;
    const bool uid_valid = jbc_device_uid_is_valid(jbc_device_uid, jbc_device_uid_len);
    if (!uid_provisioning && poll_jbc_config_write()) return;
    if (!uid_provisioning && poll_jbc_station_name_write()) return;
    // UpdateDataProcess() starts with all tier counters at zero, so the original
    // DLL enqueues one complete initial snapshot before settling into 1/2/5/15/60 s.
    // Prime every supported station family here. The original DLL enqueues all
    // initial tiers immediately; OFE distributes them through single-flight and
    // retries an individual request before allowing the scheduler to move on.
    if (!uid_provisioning && jbc_scheduler_prime_pending) {
      jbc_scheduler_prime_pending = false;
      jbc_initial_low_begin();
      next_poll_port = 0; next_port_poll_ms = now; next_sold_port_poll_delay_phase = false;
      next_station_error_poll_ms = now;
      next_device_versions_poll_ms = now;
      if (jbc_station_kind == JBC_STATION_SOLD) {
        next_sold_selected_temp_port = 0; next_sold_selected_temp_fast = true; next_sold_selected_temp_poll_ms = now;
        next_sold_station_param_stage = 0; next_sold_station_param_fast = true; next_sold_station_param_poll_ms = now;
        next_sold_delay_setting_port = 0; next_sold_delay_setting_hiber = false; next_sold_delay_setting_poll_ms = now;
        next_sold_detail_port = 0; next_sold_detail_stage = 0; next_sold_detail_poll_ms = now;
        next_sold_p01_detail_port = 0; next_sold_p01_detail_stage = 0; next_sold_p01_detail_poll_ms = now;
        next_sold_tool_refresh_fast_until_ms = now + 5000UL;
        next_sold_p01_counter_stage = 0; next_sold_p01_counter_fast = jbc_frame_protocol == JBC_PROTO_01; next_sold_p01_counter_poll_ms = now;
        next_sold_counter_port = 0; next_sold_counter_stage = 0; next_sold_counter_fast = jbc_frame_protocol == JBC_PROTO_02; next_sold_counter_poll_ms = now;
        next_sold_mos_port = 0; next_sold_mos_poll_ms = now; next_sold_station_status_poll_ms = now;
        next_sold_robot_status_poll_ms = now; next_sold_selected_profile_port = 0; next_sold_selected_profile_poll_ms = now;
        next_sold_connect_poll_ms = now;
        next_sold_qst_poll_ms = now; next_sold_lock_port = 0; next_sold_lock_poll_ms = now;
        next_sold_peripheral_status_id = 0; next_sold_peripheral_status_poll_ms = now;
        next_sold_peripheral_config_stage = 0; next_sold_peripheral_config_poll_ms = now;
        if (jbc_frame_protocol == JBC_PROTO_02) { next_sold_micro_version_stage = 0; next_sold_micro_version_poll_ms = now; }
      } else if (jbc_station_kind == JBC_STATION_HA) {
        next_ha_selected_port = 0; next_ha_selected_stage = 0; next_ha_selected_fast = true; next_ha_selected_poll_ms = now;
        next_ha_ext_temp_port = 0; next_ha_ext_temp_poll_ms = now;
        next_ha_station_diag_stage = 0; next_ha_station_diag_fast = true; next_ha_station_diag_poll_ms = now; next_ha_station_status_poll_ms = now;
        next_ha_detail_port = 0; next_ha_detail_stage = 0; next_ha_detail_poll_ms = now; next_ha_tool_refresh_fast_until_ms = now + 5000UL;
        next_ha_counter_port = 0; next_ha_counter_stage = 0; next_ha_counter_fast = true; next_ha_counter_poll_ms = now;
      } else if (jbc_station_kind == JBC_STATION_PH) {
        next_ph_tc_channel = 0; next_ph_tc_poll_ms = now; next_ph_warning_channel = 0; next_ph_warning_poll_ms = now;
        next_ph_station_stage = 0; next_ph_station_fast = true; next_ph_station_poll_ms = now;
        next_ph_port_detail_stage = 0; next_ph_port_detail_fast = true; next_ph_port_detail_poll_ms = now;
        next_ph_counter_stage = 0; next_ph_counter_fast = true; next_ph_counter_poll_ms = now;
        next_conti_read_poll_ms = now;
      } else if (jbc_station_kind == JBC_STATION_FE) {
        next_fe_port_detail_port = 0; next_fe_port_detail_stage = 0; next_fe_port_detail_poll_ms = now;
        next_fe_continuous_poll_ms = now; next_fe_robot_status_poll_ms = now;
        next_fe_station_param_stage = 0; next_fe_station_param_fast = true; next_fe_station_param_poll_ms = now;
        next_fe_counter_partial = false; next_fe_counter_fast = true; next_fe_counter_poll_ms = now;
      } else if (jbc_station_kind == JBC_STATION_SF) {
        next_sf_feeding_poll_ms = now; next_sf_robot_status_poll_ms = now;
        next_sf_station_param_stage = 0; next_sf_program_poll_index = 0; next_sf_station_param_fast = true; next_sf_station_param_poll_ms = now;
        next_sf_tool_param_stage = 0; next_sf_tool_param_fast = true; next_sf_tool_param_poll_ms = now;
        next_sf_counter_partial = false; next_sf_counter_fast = true; next_sf_counter_poll_ms = now;
        next_conti_read_poll_ms = now;
      } else if (jbc_station_kind == JBC_STATION_CL) {
        next_cl_status_doors = false; next_cl_status_poll_ms = now; next_cl_connect_poll_ms = now;
        next_cl_counter_partial = false; next_cl_counter_fast = true; next_cl_counter_poll_ms = now;
      }
    }
    // Mirror JBC_Connect's Changed_* callbacks from SOLD/HA InfoPort change bits.
    // The periodic tiers remain the fallback; these flags only pull the affected
    // group forward so local station edits appear immediately.
    if (!uid_provisioning && (jbc_station_kind == JBC_STATION_SOLD || jbc_station_kind == JBC_STATION_HA)) {
      const uint8_t refresh_flags = jbc_change_refresh_flags;
      const uint8_t tool_mask = jbc_change_tool_port_mask;
      if (refresh_flags || tool_mask) {
        jbc_change_refresh_flags = 0;
        jbc_change_tool_port_mask = 0;
        if (jbc_station_kind == JBC_STATION_SOLD) {
          if ((refresh_flags & 0x01U) && !next_sold_selected_temp_fast) {
            next_sold_selected_temp_port = 0; next_sold_selected_temp_fast = true; next_sold_selected_temp_poll_ms = now;
          }
          if ((refresh_flags & 0x02U) && !next_sold_station_param_fast) {
            next_sold_station_param_stage = 0; next_sold_station_param_fast = true; next_sold_station_param_poll_ms = now;
            next_sold_connect_poll_ms = now; next_station_name_poll_ms = now;
          }
          if (refresh_flags & 0x80U) {
            if (jbc_frame_protocol == JBC_PROTO_01) {
              if (!next_sold_p01_counter_fast) { next_sold_p01_counter_stage = 0; next_sold_p01_counter_fast = true; next_sold_p01_counter_poll_ms = now; }
            } else if (!next_sold_counter_fast) {
              next_sold_counter_port = 0; next_sold_counter_stage = 0; next_sold_counter_fast = true; next_sold_counter_poll_ms = now;
            }
          }
          if (tool_mask) {
            const bool already_fast = (int32_t)(next_sold_tool_refresh_fast_until_ms - now) > 0;
            if (!already_fast) {
              uint8_t first = 0; while (first < JBC_MAX_PORTS && !(tool_mask & (1U << first))) ++first;
              if (first < JBC_MAX_PORTS) {
                next_sold_delay_setting_port = first; next_sold_delay_setting_hiber = false; next_sold_delay_setting_poll_ms = now;
                next_sold_detail_port = first; next_sold_detail_stage = 0; next_sold_detail_poll_ms = now;
                next_sold_p01_detail_port = first; next_sold_p01_detail_stage = 0; next_sold_p01_detail_poll_ms = now;
              }
            }
            next_sold_tool_refresh_fast_until_ms = now + 4000UL;
          }
        } else {
          if ((refresh_flags & 0x01U) && !next_ha_selected_fast) {
            next_ha_selected_port = 0; next_ha_selected_stage = 0; next_ha_selected_fast = true; next_ha_selected_poll_ms = now;
          }
          if ((refresh_flags & 0x02U) && !next_ha_station_diag_fast) {
            next_ha_station_diag_stage = 0; next_ha_station_diag_fast = true; next_ha_station_diag_poll_ms = now;
            next_ha_station_status_poll_ms = now; next_station_name_poll_ms = now;
          }
          if ((refresh_flags & 0x80U) && !next_ha_counter_fast) {
            next_ha_counter_port = 0; next_ha_counter_stage = 0; next_ha_counter_fast = true; next_ha_counter_poll_ms = now;
          }
          if (tool_mask) {
            const bool already_fast = (int32_t)(next_ha_tool_refresh_fast_until_ms - now) > 0;
            if (!already_fast) {
              uint8_t first = 0; while (first < JBC_MAX_PORTS && !(tool_mask & (1U << first))) ++first;
              if (first < JBC_MAX_PORTS) { next_ha_detail_port = first; next_ha_detail_stage = 0; next_ha_detail_poll_ms = now; }
            }
            next_ha_tool_refresh_fast_until_ms = now + 4000UL;
          }
        }
      }
    }
    if (!uid_provisioning && !uid_valid && (int32_t)(now - next_uid_poll_ms) >= 0) {
      const bool uid_read_sent = jbc_send_device_uid();
      next_uid_poll_ms = now + (uid_read_sent ? JBC_UID_RETRY_READ_MS : 250UL);
    }
    if (!uid_provisioning && (int32_t)(now - next_station_name_poll_ms) >= 0) {
      const bool name_ok = jbc_send_station_name();
      // Settings.Name changes very rarely. Re-read periodically so changes made
      // on the station appear without reconnecting; retry quickly if P01 was busy.
      const bool dll_name_15s = jbc_station_kind == JBC_STATION_SOLD || jbc_station_kind == JBC_STATION_HA ||
                                jbc_station_kind == JBC_STATION_PH || jbc_station_kind == JBC_STATION_SF;
      const uint32_t normal_name_interval = dll_name_15s ? 15000UL : 60000UL;
      next_station_name_poll_ms = now + (name_ok ? (jbc_initial_station_name_pending ? 250UL : normal_name_interval) : 100UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_sold_micro_version_poll_ms) >= 0) {
      // DLL UpdateMicros() for SOLD/P02 runs in the 15 s Moderate tier and
      // probes target 0x7F followed by target 0. Keep the pair serialized and
      // repeat it every 15 s; P01 intentionally remains initial-only.
      const uint8_t target = next_sold_micro_version_stage == 0 ? 0x7FU : 0x00U;
      const bool ok = jbc_send_sold_micro_version(target);
      if (ok) {
        if (next_sold_micro_version_stage == 0) {
          next_sold_micro_version_stage = 1;
          next_sold_micro_version_poll_ms = now + 600UL;
        } else {
          next_sold_micro_version_stage = 0;
          next_sold_micro_version_poll_ms = now + 15000UL;
        }
      } else next_sold_micro_version_poll_ms = now + 200UL;
    }
    if (!uid_provisioning && jbc_frame_protocol == JBC_PROTO_02 &&
        jbc_station_kind != JBC_STATION_SOLD && jbc_station_kind != JBC_STATION_UNKNOWN &&
        (int32_t)(now - next_device_versions_poll_ms) >= 0) {
      // HA/PH/FE/SF/CL UpdateMicros() is part of the same 15 s Moderate tier.
      const bool ok = jbc_send_frame(JBC_PROTO_02, jbc_host_addr, jbc_station_addr,
                                     next_fid(), JBC_CMD_FIRMWARE, nullptr, 0);
      next_device_versions_poll_ms = now + (ok ? 15000UL : 250UL);
    }
    if (!uid_provisioning && (int32_t)(now - next_port_poll_ms) >= 0) {
      const uint8_t limit = poll_port_limit();
      if (next_poll_port >= limit) next_poll_port = 0;
      const uint8_t polled_port = next_poll_port;
      const JbcProtocol cp = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
      const bool sold_p02 = jbc_station_kind == JBC_STATION_SOLD &&
                            jbc_frame_protocol == JBC_PROTO_02 && cp == JBC_PROTO_02;

      if (sold_p02) {
        // DLL UpdateData_SOLD reads InfoPort and then DelayTime for the same port.
        // Since 0.1.50 all response-waiting JBC requests are single-flight, so
        // emitting both here back-to-back would make DelayTime lose the gate on
        // every pass. Sequence the pair across scheduler passes instead:
        //   InfoPort -> response/timeout -> DelayTime -> response/timeout -> next port.
        bool ok = false;
        if (!next_sold_port_poll_delay_phase) {
          ok = jbc_send_info_port(polled_port);
          if (ok) next_sold_port_poll_delay_phase = true;
        } else {
          ok = jbc_send_delay_time(polled_port);
          if (ok) {
            next_sold_port_poll_delay_phase = false;
            next_poll_port = (uint8_t)((next_poll_port + 1) % max((uint8_t)1, limit));
          }
        }
        // Retry quickly while the preceding single-flight request is still open.
        // After the DelayTime request was accepted, keep the established port
        // cadence before starting the next InfoPort/DelayTime pair.
        const uint32_t sold_port_spacing = max((uint32_t)125UL, (uint32_t)(1000UL / max((uint8_t)1, limit)));
        next_port_poll_ms = now + (ok && !next_sold_port_poll_delay_phase ? sold_port_spacing : 10UL);
      } else {
        next_sold_port_poll_delay_phase = false;
        // SF UpdatePortInfo() contains only ReadFeeding(). DispenserMode (0x30)
        // belongs to UpdateAllToolParam() and therefore the 15 s Moderate tier.
        if (jbc_station_kind == JBC_STATION_SF) {
          next_poll_port = 0;
          next_port_poll_ms = now + 15000UL;
        } else {
          const bool ok = jbc_send_info_port(polled_port);
          if (jbc_station_kind == JBC_STATION_FE) {
          // FE ReadSuctionLevel(0x30) has no port payload and is station-wide.
          next_poll_port = 0;
          next_port_poll_ms = now + (ok ? 1000UL : 80UL);
        } else if (ok) {
          next_poll_port = (uint8_t)((next_poll_port + 1) % max((uint8_t)1, limit));
          if (jbc_station_kind == JBC_STATION_CL || jbc_station_kind == JBC_STATION_PH) next_port_poll_ms = now + 1000UL;
          else if (jbc_station_kind == JBC_STATION_SOLD || jbc_station_kind == JBC_STATION_HA)
            next_port_poll_ms = now + max((uint32_t)125UL, (uint32_t)(1000UL / max((uint8_t)1, limit)));
          else next_port_poll_ms = now + (limit >= 4 ? 125UL : 200UL);
          } else {
            // A different single-flight request owns the slot. Do not skip this
            // port; retry soon and only advance once InfoPort was actually sent.
            next_port_poll_ms = now + 20UL;
          }
        }
      }
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_CL && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_cl_status_poll_ms) >= 0) {
      const uint8_t cmd = next_cl_status_doors ? JBC_CMD_CL_DOORS_STATE : JBC_CMD_CL_MOTORS_STATE;
      const bool ok = jbc_send_cl_read(cmd);
      if (ok) next_cl_status_doors = !next_cl_status_doors;
      next_cl_status_poll_ms = now + (ok ? 500UL : 150UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_CL && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_cl_connect_poll_ms) >= 0) {
      const bool ok = jbc_send_cl_read(JBC_CMD_CL_CONNECT_STATUS);
      next_cl_connect_poll_ms = now + (ok ? 15000UL : 250UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_CL && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_cl_counter_poll_ms) >= 0) {
      const uint8_t stage = next_cl_counter_partial ? 1U : 0U;
      const uint8_t cmd = next_cl_counter_partial ? JBC_CMD_CL_COUNTERS_PARTIAL : JBC_CMD_CL_COUNTERS;
      const bool initial_done = jbc_initial_low_stage_done(0, stage);
      const bool ok = initial_done || jbc_send_cl_read(cmd);
      if (ok) {
        if (next_cl_counter_fast) {
          if (!next_cl_counter_partial) {
            next_cl_counter_partial = true;
            next_cl_counter_poll_ms = now + (initial_done ? 10UL : JBC_INITIAL_LOW_FAST_SPACING_MS);
          } else {
            next_cl_counter_partial = false;
            if (jbc_initial_low.tracking) {
              if (jbc_initial_low_finish_or_verify()) { next_cl_counter_fast = false; next_cl_counter_poll_ms = now + 60000UL; }
              else next_cl_counter_poll_ms = now + JBC_INITIAL_LOW_FAST_SPACING_MS;
            } else { next_cl_counter_fast = false; next_cl_counter_poll_ms = now + 60000UL; }
          }
        } else {
          next_cl_counter_partial = !next_cl_counter_partial;
          next_cl_counter_poll_ms = now + (next_cl_counter_partial ? 500UL : 59500UL);
        }
      } else next_cl_counter_poll_ms = now + JBC_INITIAL_LOW_FAST_SPACING_MS;
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_PH && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_ph_tc_poll_ms) >= 0) {
      // UpdateData_PH.UpdatePortInfo(): ReadExternalAirTemp for TC0..TC3 once per second.
      const bool ok = jbc_send_ph_read(JBC_CMD_EXTERNAL_AIR_TEMP_PH, next_ph_tc_channel);
      if (ok) next_ph_tc_channel = (uint8_t)((next_ph_tc_channel + 1U) & 3U);
      next_ph_tc_poll_ms = now + (ok ? 250UL : 100UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_PH && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_ph_warning_poll_ms) >= 0) {
      // UpdateStationStatus(): four TC warning reads per ~5 s tier.
      const bool ok = jbc_send_ph_read(JBC_CMD_TC_WARNING_PH, next_ph_warning_channel);
      if (ok) next_ph_warning_channel = (uint8_t)((next_ph_warning_channel + 1U) & 3U);
      next_ph_warning_poll_ms = now + (ok ? 1250UL : 150UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_PH && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_ph_station_poll_ms) >= 0) {
      // UpdateStationParam(), excluding DeviceName which is handled by the common
      // name poll above. 19 remaining reads are spread across ~15 seconds.
      static const uint8_t cmds[] = {
        JBC_CMD_PIN_PH, JBC_CMD_MAXMIN_POWER_PH, JBC_CMD_MAXMIN_TEMP_PH,
        JBC_CMD_PIN_ENABLED_PH, JBC_CMD_BEEP_PH, JBC_CMD_REMOTE_MODE_PH, JBC_CMD_CONNECT_STATUS_PH,
        JBC_CMD_EXTERNAL_TC_MODE_PH, JBC_CMD_EXTERNAL_TC_MODE_PH, JBC_CMD_EXTERNAL_TC_MODE_PH, JBC_CMD_EXTERNAL_TC_MODE_PH,
        JBC_CMD_SELECT_TEMP_PH, JBC_CMD_SELECT_TEMP_PH, JBC_CMD_SELECT_TEMP_PH, JBC_CMD_SELECT_TEMP_PH,
        JBC_CMD_ROBOT_CONFIG_PH, JBC_CMD_ROBOT_STATUS_PH,
        JBC_CMD_PROFILE_PH, JBC_CMD_PROFILE_SETTINGS_PH, JBC_CMD_PROFILE_TEACH_PH
      };
      static const uint8_t ctx[] = {
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0,1,2,3, 0,1,2,3, 0xFF,0xFF, 0xFF,0xFF,0xFF
      };
      const uint8_t stages = (uint8_t)(sizeof(cmds) / sizeof(cmds[0]));
      if (next_ph_station_stage >= stages) next_ph_station_stage = 0;
      const bool ok = jbc_send_ph_read(cmds[next_ph_station_stage], ctx[next_ph_station_stage]);
      if (ok) {
        next_ph_station_stage = (uint8_t)((next_ph_station_stage + 1U) % stages);
        if (!next_ph_station_stage && next_ph_station_fast) next_ph_station_fast = false;
      }
      next_ph_station_poll_ms = now + (ok ? (next_ph_station_fast ? 140UL : 780UL) : 120UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_PH && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_ph_port_detail_poll_ms) >= 0) {
      // UpdateAllToolParam(): one preheater port, five reads per 15-second tier.
      static const uint8_t cmds[] = {JBC_CMD_WORK_MODE_PH, JBC_CMD_HEATER_STATUS_PH,
        JBC_CMD_TIME_TO_STOP_PH, JBC_CMD_SELECT_POWER_PH, JBC_CMD_ACTIVE_ZONES_PH};
      if (next_ph_port_detail_stage >= (uint8_t)(sizeof(cmds)/sizeof(cmds[0]))) next_ph_port_detail_stage = 0;
      const uint8_t stages = (uint8_t)(sizeof(cmds)/sizeof(cmds[0]));
      const bool ok = jbc_send_ph_read(cmds[next_ph_port_detail_stage], 0);
      if (ok) {
        next_ph_port_detail_stage = (uint8_t)((next_ph_port_detail_stage + 1U) % stages);
        if (!next_ph_port_detail_stage && next_ph_port_detail_fast) next_ph_port_detail_fast = false;
      }
      next_ph_port_detail_poll_ms = now + (ok ? (next_ph_port_detail_fast ? 140UL : 3000UL) : 150UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_PH && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_ph_counter_poll_ms) >= 0) {
      // UpdateCounters(): three global + three partial reads every ~60 seconds.
      static const uint8_t cmds[] = {JBC_CMD_COUNTER_PLUG_PH, JBC_CMD_COUNTER_WORK_PH,
        JBC_CMD_COUNTER_WORK_CYCLES_PH, JBC_CMD_COUNTER_PLUG_PARTIAL_PH,
        JBC_CMD_COUNTER_WORK_PARTIAL_PH, JBC_CMD_COUNTER_WORK_CYCLES_PARTIAL_PH};
      if (next_ph_counter_stage >= (uint8_t)(sizeof(cmds)/sizeof(cmds[0]))) next_ph_counter_stage = 0;
      const uint8_t stages = (uint8_t)(sizeof(cmds)/sizeof(cmds[0]));
      const bool initial_done = jbc_initial_low_stage_done(0, next_ph_counter_stage);
      const bool ok = initial_done || jbc_send_ph_read(cmds[next_ph_counter_stage], 0);
      if (ok) {
        next_ph_counter_stage = (uint8_t)((next_ph_counter_stage + 1U) % stages);
        if (!next_ph_counter_stage && next_ph_counter_fast) {
          if (jbc_initial_low.tracking) {
            if (jbc_initial_low_finish_or_verify()) { next_ph_counter_fast = false; }
          } else next_ph_counter_fast = false;
        }
      }
      next_ph_counter_poll_ms = now + (ok ? (next_ph_counter_fast ? (initial_done ? 10UL : JBC_INITIAL_LOW_FAST_SPACING_MS) : 10000UL) : 180UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_FE && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_fe_port_detail_poll_ms) >= 0) {
      // UpdateData_FE.UpdatePortInfo() plus the remaining safe per-port public
      // getters: StandIntakes, SuctionDelay WORK/STAND and ConnectedPedal.
      const uint8_t limit = poll_port_limit();
      if (next_fe_port_detail_port >= limit) next_fe_port_detail_port = 0;
      bool ok = false;
      switch (next_fe_port_detail_stage) {
        case 0: ok = jbc_send_fe_read(JBC_CMD_INTAKE_ACTIVATION_FE, next_fe_port_detail_port, 0); break;
        case 1: ok = jbc_send_fe_read(JBC_CMD_INTAKE_ACTIVATION_FE, next_fe_port_detail_port, 1); break;
        case 2: ok = jbc_send_fe_read(JBC_CMD_TIME_TO_STOP_SUCTION_FE, next_fe_port_detail_port, 0); break;
        case 3: ok = jbc_send_fe_read(JBC_CMD_TIME_TO_STOP_SUCTION_FE, next_fe_port_detail_port, 1); break;
        case 4: ok = jbc_send_fe_read(JBC_CMD_ACTIVATION_PEDAL_FE, next_fe_port_detail_port); break;
        case 5: ok = jbc_send_fe_read(JBC_CMD_PEDAL_MODE_FE, next_fe_port_detail_port); break;
        case 6: ok = jbc_send_fe_read(JBC_CMD_STAND_INTAKES_FE, next_fe_port_detail_port); break;
        case 7: ok = jbc_send_fe_read(JBC_CMD_SUCTION_DELAY_FE, next_fe_port_detail_port, 0); break;
        case 8: ok = jbc_send_fe_read(JBC_CMD_SUCTION_DELAY_FE, next_fe_port_detail_port, 1); break;
        default: ok = jbc_send_fe_read(JBC_CMD_CONNECTED_PEDAL_FE, next_fe_port_detail_port); break;
      }
      if (ok) {
        ++next_fe_port_detail_stage;
        if (next_fe_port_detail_stage >= 10) {
          next_fe_port_detail_stage = 0;
          next_fe_port_detail_port = (uint8_t)((next_fe_port_detail_port + 1U) % max((uint8_t)1, limit));
        }
      }
      uint8_t denom = (uint8_t)(limit * 10U);
      if (denom < 1U) denom = 1U;
      uint32_t spacing = 1000UL / denom;
      if (spacing < 40UL) spacing = 40UL;
      next_fe_port_detail_poll_ms = now + (ok ? spacing : 100UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_FE && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_fe_continuous_poll_ms) >= 0) {
      const bool ok = jbc_send_fe_read(JBC_CMD_CONTINUOUS_SUCTION_FE);
      next_fe_continuous_poll_ms = now + (ok ? 1000UL : 150UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_FE && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_fe_robot_status_poll_ms) >= 0) {
      // UpdateStationStatus() tier; station error remains in the common poll.
      const bool ok = jbc_send_fe_read(JBC_CMD_ROBOT_STATUS_FE);
      next_fe_robot_status_poll_ms = now + (ok ? 5000UL : 250UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_FE && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_fe_station_param_poll_ms) >= 0) {
      static const uint8_t cmds[] = {JBC_CMD_CONNECT_STATUS_FE,JBC_CMD_ROBOT_CONFIG_FE,
        JBC_CMD_FLOW_FE,JBC_CMD_SPEED_FE,JBC_CMD_SELECT_FLOW_FE,JBC_CMD_FILTER_STATUS_FE,JBC_CMD_PIN_FE,JBC_CMD_BEEP_FE};
      const uint8_t stages=(uint8_t)(sizeof(cmds)/sizeof(cmds[0])); if(next_fe_station_param_stage>=stages)next_fe_station_param_stage=0;
      const bool ok = jbc_send_fe_read(cmds[next_fe_station_param_stage]);
      if (ok) {
        next_fe_station_param_stage=(uint8_t)((next_fe_station_param_stage+1U)%stages);
        if (!next_fe_station_param_stage && next_fe_station_param_fast) next_fe_station_param_fast = false;
      }
      next_fe_station_param_poll_ms = now + (ok ? (next_fe_station_param_fast ? 140UL : 1900UL) : 150UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_FE && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_fe_counter_poll_ms) >= 0) {
      const uint8_t stage = next_fe_counter_partial ? 1U : 0U;
      const bool initial_done = jbc_initial_low_stage_done(0, stage);
      const bool ok = initial_done || jbc_send_fe_read(next_fe_counter_partial ? JBC_CMD_COUNTERS_PARTIAL_FE : JBC_CMD_COUNTERS_FE);
      if (ok) {
        if (next_fe_counter_fast) {
          if (!next_fe_counter_partial) { next_fe_counter_partial = true; next_fe_counter_poll_ms = now + (initial_done ? 10UL : JBC_INITIAL_LOW_FAST_SPACING_MS); }
          else {
            next_fe_counter_partial = false;
            if (jbc_initial_low.tracking) {
              if (jbc_initial_low_finish_or_verify()) { next_fe_counter_fast = false; next_fe_counter_poll_ms = now + 60000UL; }
              else next_fe_counter_poll_ms = now + JBC_INITIAL_LOW_FAST_SPACING_MS;
            } else { next_fe_counter_fast = false; next_fe_counter_poll_ms = now + 60000UL; }
          }
        } else {
          next_fe_counter_partial = !next_fe_counter_partial;
          next_fe_counter_poll_ms = now + (next_fe_counter_partial ? 500UL : 59500UL);
        }
      } else next_fe_counter_poll_ms = now + JBC_INITIAL_LOW_FAST_SPACING_MS;
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SF && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_sf_feeding_poll_ms) >= 0) {
      const bool ok = jbc_send_sf_read(JBC_CMD_FEEDING_SF);
      next_sf_feeding_poll_ms = now + (ok ? 1000UL : 150UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SF && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_sf_robot_status_poll_ms) >= 0) {
      // UpdateStationStatus(): station error is handled by the common 0x59 poll.
      const bool ok = jbc_send_sf_read(JBC_CMD_ROBOT_STATUS_SF);
      next_sf_robot_status_poll_ms = now + (ok ? 5000UL : 250UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SF && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_sf_station_param_poll_ms) >= 0) {
      // UpdateStationParam(): PIN, PINEnabled, Beep, LengthUnit, ConnectStatus,
      // all 35 programs, ProgramList and RobotConfiguration. DeviceName uses
      // the common 15 s station-name poll above. One request per 500 ms keeps
      // the bridge responsive while still completing a full settings sweep.
      bool ok = false;
      if (next_sf_station_param_stage < 5) {
        static const uint8_t cmds[] = {JBC_CMD_PIN_SF, JBC_CMD_PIN_ENABLED_SF, JBC_CMD_BEEP_SF,
          JBC_CMD_LENGTH_UNIT_SF, JBC_CMD_CONNECT_STATUS_SF};
        ok = jbc_send_sf_read(cmds[next_sf_station_param_stage]);
        if (ok) ++next_sf_station_param_stage;
      } else if (next_sf_station_param_stage == 5) {
        const uint8_t program = (uint8_t)(next_sf_program_poll_index + 1U);
        ok = jbc_send_sf_read(JBC_CMD_PROGRAM_SF, program);
        if (ok) {
          ++next_sf_program_poll_index;
          if (next_sf_program_poll_index >= JBC_SF_PROGRAM_COUNT) { next_sf_program_poll_index = 0; ++next_sf_station_param_stage; }
        }
      } else if (next_sf_station_param_stage == 6) {
        ok = jbc_send_sf_read(JBC_CMD_PROGRAM_LIST_SF); if (ok) ++next_sf_station_param_stage;
      } else {
        ok = jbc_send_sf_read(JBC_CMD_ROBOT_CONFIG_SF);
        if (ok) {
          next_sf_station_param_stage = 0; next_sf_program_poll_index = 0;
          if (next_sf_station_param_fast) next_sf_station_param_fast = false;
        }
      }
      next_sf_station_param_poll_ms = now + (ok ? (next_sf_station_param_fast ? 120UL : 500UL) : 120UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SF && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_sf_tool_param_poll_ms) >= 0) {
      // DLL UpdateAllToolParam(): ToolEnabled, DispenserMode, Speed, Length all
      // belong to the 15 s Moderate tier. Spread the four reads across it.
      static const uint8_t cmds[] = {JBC_CMD_TOOL_ENABLED_SF, JBC_CMD_INFO_PORT,
                                     JBC_CMD_SPEED_SF, JBC_CMD_LENGTH_SF};
      if (next_sf_tool_param_stage >= 4) next_sf_tool_param_stage = 0;
      const bool ok = jbc_send_sf_read(cmds[next_sf_tool_param_stage]);
      if (ok) {
        next_sf_tool_param_stage = (uint8_t)((next_sf_tool_param_stage + 1U) % 4U);
        if (!next_sf_tool_param_stage && next_sf_tool_param_fast) next_sf_tool_param_fast = false;
      }
      next_sf_tool_param_poll_ms = now + (ok ? (next_sf_tool_param_fast ? 150UL : 3750UL) : 150UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SF && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_sf_counter_poll_ms) >= 0) {
      const uint8_t stage = next_sf_counter_partial ? 1U : 0U;
      const bool initial_done = jbc_initial_low_stage_done(0, stage);
      const bool ok = initial_done || jbc_send_sf_read(next_sf_counter_partial ? JBC_CMD_COUNTERS_PARTIAL_SF : JBC_CMD_COUNTERS_SF);
      if (ok) {
        if (next_sf_counter_fast) {
          if (!next_sf_counter_partial) { next_sf_counter_partial = true; next_sf_counter_poll_ms = now + (initial_done ? 10UL : JBC_INITIAL_LOW_FAST_SPACING_MS); }
          else {
            next_sf_counter_partial = false;
            if (jbc_initial_low.tracking) {
              if (jbc_initial_low_finish_or_verify()) { next_sf_counter_fast = false; next_sf_counter_poll_ms = now + 60000UL; }
              else next_sf_counter_poll_ms = now + JBC_INITIAL_LOW_FAST_SPACING_MS;
            } else { next_sf_counter_fast = false; next_sf_counter_poll_ms = now + 60000UL; }
          }
        } else {
          next_sf_counter_partial = !next_sf_counter_partial;
          next_sf_counter_poll_ms = now + (next_sf_counter_partial ? 500UL : 59500UL);
        }
      } else next_sf_counter_poll_ms = now + JBC_INITIAL_LOW_FAST_SPACING_MS;
    }
    // DLL VeryHigh tier: selected temperature for every SOLD port every ~2 s.
    // Keep it independent from the 15 s tool-parameter tier.
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SOLD &&
        (int32_t)(now - next_sold_selected_temp_poll_ms) >= 0) {
      const uint8_t limit = poll_port_limit();
      if (next_sold_selected_temp_port >= limit) next_sold_selected_temp_port = 0;
      const uint8_t p = next_sold_selected_temp_port;
      bool ok = false;
      if (p < JBC_MAX_PORTS && jbc_ports[p].valid) {
        if (jbc_frame_protocol == JBC_PROTO_01) ok = jbc_send_sold_p01_port_read(p, JBC_CMD_SELECT_TEMP_SOLD, false);
        else if (jbc_frame_protocol == JBC_PROTO_02) ok = jbc_send_sold_detail(p, JBC_CMD_SELECT_TEMP_SOLD);
      }
      if (ok || p >= JBC_MAX_PORTS || !jbc_ports[p].valid) {
        next_sold_selected_temp_port = (uint8_t)((p + 1U) % max((uint8_t)1, limit));
        if (next_sold_selected_temp_fast && next_sold_selected_temp_port == 0) next_sold_selected_temp_fast = false;
      }
      const uint32_t spacing = next_sold_selected_temp_fast ? 120UL :
        max((uint32_t)250UL, (uint32_t)(2000UL / max((uint8_t)1, limit)));
      next_sold_selected_temp_poll_ms = now + (ok ? spacing : 100UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SOLD &&
        (int32_t)(now - next_sold_delay_setting_poll_ms) >= 0) {
      const uint8_t limit = poll_port_limit();
      if (next_sold_delay_setting_port >= limit) next_sold_delay_setting_port = 0;
      const uint8_t p = next_sold_delay_setting_port;
      bool ok = false;
      if (p < JBC_MAX_PORTS && jbc_ports[p].valid && jbc_ports[p].tool) {
        ok = jbc_send_sold_delay_setting(p, next_sold_delay_setting_hiber ? JBC_CMD_HIBER_DELAY_SOLD : JBC_CMD_SLEEP_DELAY_SOLD);
      }
      if (ok) {
        if (next_sold_delay_setting_hiber) {
          next_sold_delay_setting_hiber = false;
          next_sold_delay_setting_port = (uint8_t)((p + 1) % max((uint8_t)1, limit));
        } else next_sold_delay_setting_hiber = true;
      } else if (!jbc_ports[p].valid || !jbc_ports[p].tool) {
        next_sold_delay_setting_hiber = false;
        next_sold_delay_setting_port = (uint8_t)((p + 1) % max((uint8_t)1, limit));
      }
      const bool fast_refresh = (int32_t)(next_sold_tool_refresh_fast_until_ms - now) > 0;
      const uint32_t spacing = fast_refresh ? 150UL :
        max((uint32_t)750UL, (uint32_t)(15000UL / (2UL * max((uint8_t)1, limit))));
      next_sold_delay_setting_poll_ms = now + (ok ? spacing : 80UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_sold_tool_status_poll_ms) >= 0) {
      const uint8_t limit = poll_port_limit();
      if (next_sold_tool_status_port >= limit) next_sold_tool_status_port = 0;
      const uint8_t p = next_sold_tool_status_port;
      bool ok = false;
      if (p < JBC_MAX_PORTS && jbc_ports[p].valid)
        ok = jbc_send_sold_detail(p, JBC_CMD_TOOL_STATUS_SOLD);
      next_sold_tool_status_port = (uint8_t)((p + 1) % max((uint8_t)1, limit));
      // The DLL exposes ReadToolLastStatus explicitly. Poll it separately from
      // the slow settings/counter round-robin so QSTLock/ActiveCleaning stay live.
      const uint32_t spacing = max((uint32_t)750UL, (uint32_t)(5000UL / max((uint8_t)1, limit)));
      next_sold_tool_status_poll_ms = now + (ok ? spacing : 150UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_sold_detail_poll_ms) >= 0) {
      const uint8_t limit = poll_port_limit();
      if (next_sold_detail_port >= limit) next_sold_detail_port = 0;
      const uint8_t p = next_sold_detail_port;
      bool ok = false;
      if (p < JBC_MAX_PORTS && jbc_ports[p].valid) {
        // Service/cartridge diagnostics are deliberately part of this slow
        // ~15 s round-robin, never the fast InfoPort path.
        // The low-level JBC_Connect k20 and k26 frame classes both implement
        // ReadTipTemperature (0x52) with A/B values. Probe it read-only here; an
        // unsupported station simply does not set the validity flag. Current is
        // available on both generations and power-per-thousand on k20.
        static const uint8_t k20_commands[] = {JBC_CMD_LEVELS_SOLD, JBC_CMD_SLEEP_TEMP_SOLD, JBC_CMD_ADJUST_TEMP_SOLD,
          JBC_CMD_CARTRIDGE_SOLD, JBC_CMD_TIP_TEMP_SOLD, JBC_CMD_CURRENT_SOLD, JBC_CMD_POWER_PERTHOUSAND_SOLD};
        static const uint8_t k26_commands[] = {JBC_CMD_LEVELS_SOLD, JBC_CMD_SLEEP_TEMP_SOLD, JBC_CMD_ADJUST_TEMP_SOLD,
          JBC_CMD_TIP_TEMP_SOLD, JBC_CMD_CURRENT_SOLD, JBC_CMD_POWER_PERTHOUSAND_SOLD};
        const bool k26 = sold_k26_protocol();
        const uint8_t stages = k26
          ? (uint8_t)(sizeof(k26_commands) / sizeof(k26_commands[0]))
          : (uint8_t)(sizeof(k20_commands) / sizeof(k20_commands[0]));
        if (next_sold_detail_stage >= stages) next_sold_detail_stage = 0;
        const uint8_t cmd = k26 ? k26_commands[next_sold_detail_stage] : k20_commands[next_sold_detail_stage];
        // Do not probe the 0x48 cartridge configuration record on stations for
        // which the original DLL says Features.Cartridges=false (notably DDE).
        // The separate 0x52..0x54 service diagnostics remain useful/read-only.
        if (cmd == JBC_CMD_CARTRIDGE_SOLD && !sold_supports_cartridges()) {
          ok = true; // feature-gated skip; advance without sending
        } else {
          ok = jbc_send_sold_detail(p, cmd);
        }
        if (ok) {
          ++next_sold_detail_stage;
          if (next_sold_detail_stage >= stages) {
            next_sold_detail_stage = 0;
            next_sold_detail_port = (uint8_t)((p + 1) % max((uint8_t)1, limit));
          }
        }
      }
      if (!ok && (!jbc_ports[p].valid || (next_sold_detail_stage <= 2 && !jbc_ports[p].tool))) {
        next_sold_detail_stage = 0;
        next_sold_detail_port = (uint8_t)((p + 1) % max((uint8_t)1, limit));
      }
      // Tool/service details use the DLL's ~15 s moderate tier. Change bits
      // temporarily accelerate the cycle, but normal InfoPort/DelayTime stays first.
      const bool fast_refresh = (int32_t)(next_sold_tool_refresh_fast_until_ms - now) > 0;
      const bool k26_now = sold_k26_protocol();
      const uint8_t stages_now = k26_now ? 6U : 7U;
      const uint32_t spacing = fast_refresh ? 150UL :
        max((uint32_t)500UL, (uint32_t)(15000UL / ((uint32_t)stages_now * max((uint8_t)1, limit))));
      next_sold_detail_poll_ms = now + (ok ? spacing : 100UL);
    }
    // DLL Low tier: SOLD global/partial counters refresh approximately once per
    // minute. k20 uses seven commands per group; k26 uses grouped C0/C2 replies.
    // The central QueueMessages-style single-flight retry now guarantees that a
    // timed-out individual counter read is retried before this stage advances to
    // another on-wire request, uniformly with HA/PH/FE/SF/CL.
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_sold_counter_poll_ms) >= 0) {
      static const uint8_t k20_counter_cmds[] = {
        JBC_CMD_COUNTER_PLUG, JBC_CMD_COUNTER_WORK, JBC_CMD_COUNTER_SLEEP, JBC_CMD_COUNTER_HIBER,
        JBC_CMD_COUNTER_IDLE, JBC_CMD_COUNTER_SLEEP_CYCLES, JBC_CMD_COUNTER_DESOLD_CYCLES,
        JBC_CMD_COUNTER_PLUG_PARTIAL_SOLD, JBC_CMD_COUNTER_WORK_PARTIAL_SOLD, JBC_CMD_COUNTER_SLEEP_PARTIAL_SOLD,
        JBC_CMD_COUNTER_HIBER_PARTIAL_SOLD, JBC_CMD_COUNTER_IDLE_PARTIAL_SOLD,
        JBC_CMD_COUNTER_SLEEP_CYCLES_PARTIAL_SOLD, JBC_CMD_COUNTER_DESOLD_CYCLES_PARTIAL_SOLD
      };
      static const uint8_t k26_counter_cmds[] = {JBC_CMD_COUNTER_PLUG, JBC_CMD_COUNTER_WORK};
      const uint8_t limit = poll_port_limit();
      const bool k26 = sold_k26_protocol();
      const uint8_t stages = k26 ? 2U : (uint8_t)(sizeof(k20_counter_cmds) / sizeof(k20_counter_cmds[0]));
      if (next_sold_counter_port >= limit) next_sold_counter_port = 0;
      if (next_sold_counter_stage >= stages) next_sold_counter_stage = 0;
      const uint8_t p = next_sold_counter_port;
      const uint8_t stage = next_sold_counter_stage;
      const uint8_t cmd = k26 ? k26_counter_cmds[stage] : k20_counter_cmds[stage];
      const bool partial = k26 ? (stage == 1U) : (stage >= 7U);
      const bool initial_done = jbc_initial_low_stage_done(p, stage);
      bool ok = initial_done;
      if (!ok && p < JBC_MAX_PORTS && jbc_ports[p].valid) {
        if (partial && !sold_supports_partial_counters()) ok = true;
        else ok = jbc_send_sold_detail(p, cmd);
      }
      bool completed_fast_cycle = false;
      if (ok) {
        ++next_sold_counter_stage;
        if (next_sold_counter_stage >= stages) {
          next_sold_counter_stage = 0;
          next_sold_counter_port = (uint8_t)((p + 1U) % max((uint8_t)1, limit));
          if (next_sold_counter_fast && next_sold_counter_port == 0) {
            if (jbc_initial_low.tracking) {
              if (jbc_initial_low_finish_or_verify()) { next_sold_counter_fast = false; completed_fast_cycle = true; }
            } else { next_sold_counter_fast = false; completed_fast_cycle = true; }
          }
        }
      } else if (p >= JBC_MAX_PORTS || !jbc_ports[p].valid) {
        // During the cold-start completion pass, an InfoPort may not have made
        // this port valid yet. Keep the same logical port/stage pending instead
        // of skipping the entire port until the next 60 s cycle.
        if (!jbc_initial_low.tracking) {
          next_sold_counter_stage = 0;
          next_sold_counter_port = (uint8_t)((p + 1U) % max((uint8_t)1, limit));
        }
      }
      const uint32_t regular_spacing = max((uint32_t)500UL,
        (uint32_t)(60000UL / ((uint32_t)stages * max((uint8_t)1, limit))));
      next_sold_counter_poll_ms = now + (ok ? (completed_fast_cycle ? 60000UL :
        (next_sold_counter_fast ? (initial_done ? 10UL : JBC_INITIAL_LOW_FAST_SPACING_MS) : regular_spacing)) : 120UL);
    }

    // Protocol-01 UpdateData_SOLD completion plus safe observable service reads.
    // P01 has a single outstanding request slot, so every helper yields when any
    // higher-priority request owns it. SendFrame01_SOLD also exposes ReadCurrent
    // (0x53), but ReceiveFrame01_SOLD has no command-83 decoder at all; do not
    // invent a value the original DLL itself never makes observable.
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_01 &&
        (int32_t)(now - next_sold_p01_detail_poll_ms) >= 0) {
      static const uint8_t cmds[] = {JBC_CMD_FIX_TEMP_P01_SOLD,JBC_CMD_LEVELS_SOLD,JBC_CMD_LEVEL1_P01_SOLD,JBC_CMD_LEVEL2_P01_SOLD,JBC_CMD_LEVEL3_P01_SOLD,
        JBC_CMD_SLEEP_TEMP_SOLD,JBC_CMD_ADJUST_TEMP_SOLD,JBC_CMD_TIP_TEMP_SOLD,JBC_CMD_POWER_PERTHOUSAND_SOLD,
        JBC_CMD_DELAY_TIME_P01_SOLD,JBC_CMD_STATUS_REMOTE_P01_SOLD,JBC_CMD_TOOL_TYPE_SOLD,JBC_CMD_TOOL_LAST_ERROR_SOLD,JBC_CMD_TOOL_STATUS_SOLD};
      const uint8_t limit=poll_port_limit(); if(next_sold_p01_detail_port>=limit)next_sold_p01_detail_port=0;
      if(next_sold_p01_detail_stage>=sizeof(cmds))next_sold_p01_detail_stage=0; const uint8_t p=next_sold_p01_detail_port,cmd=cmds[next_sold_p01_detail_stage];
      const bool with_tool = cmd==JBC_CMD_FIX_TEMP_P01_SOLD || cmd==JBC_CMD_LEVELS_SOLD || cmd==JBC_CMD_LEVEL1_P01_SOLD || cmd==JBC_CMD_LEVEL2_P01_SOLD || cmd==JBC_CMD_LEVEL3_P01_SOLD || cmd==JBC_CMD_SLEEP_TEMP_SOLD || cmd==JBC_CMD_ADJUST_TEMP_SOLD;
      bool ok=false; if(p<JBC_MAX_PORTS&&jbc_ports[p].valid) ok=jbc_send_sold_p01_port_read(p,cmd,with_tool);
      if(ok){++next_sold_p01_detail_stage;if(next_sold_p01_detail_stage>=sizeof(cmds)){next_sold_p01_detail_stage=0;next_sold_p01_detail_port=(uint8_t)((p+1)%max((uint8_t)1,limit));}}
      else if(p>=JBC_MAX_PORTS||!jbc_ports[p].valid||(with_tool&&!jbc_ports[p].tool)){next_sold_p01_detail_stage=0;next_sold_p01_detail_port=(uint8_t)((p+1)%max((uint8_t)1,limit));}
      const bool fast_refresh=(int32_t)(next_sold_tool_refresh_fast_until_ms-now)>0;
      const uint32_t spacing=fast_refresh?150UL:max((uint32_t)500UL,(uint32_t)(15000UL/((uint32_t)sizeof(cmds)*max((uint8_t)1,limit))));
      next_sold_p01_detail_poll_ms=now+(ok?spacing:100UL);
    }
    // P01 global/partial counters are station-wide responses containing four
    // bytes per port. Partial commands are feature-gated exactly like the DLL.
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_01 &&
        (int32_t)(now - next_sold_p01_counter_poll_ms) >= 0) {
      static const uint8_t cmds[] = {JBC_CMD_COUNTER_PLUG,JBC_CMD_COUNTER_WORK,JBC_CMD_COUNTER_SLEEP,JBC_CMD_COUNTER_HIBER,JBC_CMD_COUNTER_IDLE,JBC_CMD_COUNTER_SLEEP_CYCLES,JBC_CMD_COUNTER_DESOLD_CYCLES,
        JBC_CMD_COUNTER_PLUG_PARTIAL_P01_SOLD,JBC_CMD_COUNTER_WORK_PARTIAL_P01_SOLD,JBC_CMD_COUNTER_SLEEP_PARTIAL_P01_SOLD,JBC_CMD_COUNTER_HIBER_PARTIAL_P01_SOLD,JBC_CMD_COUNTER_IDLE_PARTIAL_P01_SOLD,JBC_CMD_COUNTER_SLEEP_CYCLES_PARTIAL_P01_SOLD,JBC_CMD_COUNTER_DESOLD_CYCLES_PARTIAL_P01_SOLD};
      if(next_sold_p01_counter_stage>=sizeof(cmds))next_sold_p01_counter_stage=0;const uint8_t stage=next_sold_p01_counter_stage,cmd=cmds[stage];
      const bool partial=cmd>=JBC_CMD_COUNTER_PLUG_PARTIAL_P01_SOLD;
      const bool initial_done=jbc_initial_low_stage_done(0,stage);
      bool ok=initial_done;if(!ok){if(partial&&!sold_supports_p01_partial_counters())ok=true;else ok=jbc_send_sold_p01_counter_read(cmd);}
      bool completed_fast_cycle=false;
      if(ok){++next_sold_p01_counter_stage;if(next_sold_p01_counter_stage>=sizeof(cmds)){next_sold_p01_counter_stage=0;if(next_sold_p01_counter_fast){if(jbc_initial_low.tracking){if(jbc_initial_low_finish_or_verify()){next_sold_p01_counter_fast=false;completed_fast_cycle=true;}}else{next_sold_p01_counter_fast=false;completed_fast_cycle=true;}}}}
      const uint32_t regular_spacing=max((uint32_t)1000UL,(uint32_t)(60000UL/(uint32_t)sizeof(cmds)));
      next_sold_p01_counter_poll_ms=now+(ok?(completed_fast_cycle?60000UL:(next_sold_p01_counter_fast?(initial_done?10UL:JBC_INITIAL_LOW_FAST_SPACING_MS):regular_spacing)):120UL);
    }
    // Profiles/Assistant are model-gated exactly like CFeaturesDataInitializer.
    // Keep them out of the base detail cycle so older DDE models are not probed.
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_02 &&
        (sold_supports_profiles() || sold_supports_assistant()) && (int32_t)(now - next_sold_extra_station_poll_ms) >= 0) {
      const uint8_t limit=poll_port_limit(); const uint8_t p=(uint8_t)(next_sold_extra_station_stage % max((uint8_t)1,limit));
      const uint8_t sub=(uint8_t)(next_sold_extra_station_stage / max((uint8_t)1,limit)); bool ok=true;
      if (p < JBC_MAX_PORTS && jbc_ports[p].valid) {
        // SelectedFile belongs to the DLL High/5 s port-status tier and is
        // scheduled separately below. ProfileMode/Assistant are Moderate/15 s.
        if (sub==0 && sold_supports_profiles() && jbc_ports[p].tool) ok=jbc_send_sold_detail(p,JBC_CMD_PROFILE_MODE_SOLD);
        else if (sub==1 && sold_supports_assistant() && jbc_ports[p].tool) ok=jbc_send_sold_detail(p,JBC_CMD_ASSISTANT_CONFIG_SOLD);
        else if (sub==2 && sold_supports_assistant()) ok=jbc_send_sold_detail(p,JBC_CMD_ASSISTANT_WARNING_SOLD);
        else if (sub==3 && sold_supports_assistant()) ok=jbc_send_sold_detail(p,JBC_CMD_SOLDERING_RESULT_SOLD);
      }
      if(ok){++next_sold_extra_station_stage;if(next_sold_extra_station_stage >= (uint8_t)(4*max((uint8_t)1,limit))) next_sold_extra_station_stage=0;}
      const uint32_t extra_spacing=max((uint32_t)750UL,(uint32_t)(15000UL/(4UL*max((uint8_t)1,limit))));
      next_sold_extra_station_poll_ms=now+(ok?extra_spacing:150UL);
    }
    // Station parameters used by UpdateData_SOLD. PIN is read exactly like the DLL;
    // it is transported to the Master, which exposes it only in developer mode.
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_sold_extra_station_poll_ms) >= 0 && !sold_supports_profiles() && !sold_supports_assistant()) {
      next_sold_extra_station_poll_ms=now+150UL; // feature poll owns this timer only when enabled
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SOLD && (int32_t)(now - next_sold_station_param_poll_ms) >= 0) {
      bool ok=true; bool wrapped=false; uint8_t cmd=0; uint8_t stages_total=1;
      if(jbc_frame_protocol==JBC_PROTO_01){
        // Protocol-01 UpdateStationParam is itself the DLL's normal 15 s tier.
        static const uint8_t p01_cmds[]={JBC_CMD_REMOTE_MODE_SOLD,JBC_CMD_TEMP_UNIT_P01_SOLD,JBC_CMD_MAX_TEMP_SOLD,JBC_CMD_MIN_TEMP_SOLD,JBC_CMD_N2_MODE_P01_SOLD,JBC_CMD_HELP_TEXT_P01_SOLD,JBC_CMD_POWER_LIMIT_SOLD,JBC_CMD_PIN_SOLD,JBC_CMD_BEEP_P01_SOLD,JBC_CMD_PIN_ENABLED_P01_SOLD};
        stages_total=(uint8_t)sizeof(p01_cmds);
        if(next_sold_station_param_stage>=stages_total)next_sold_station_param_stage=0;cmd=p01_cmds[next_sold_station_param_stage];ok=jbc_send_sold_station_read(cmd);
        if(ok){++next_sold_station_param_stage;if(next_sold_station_param_stage>=stages_total){next_sold_station_param_stage=0;wrapped=true;}}
      } else if(jbc_frame_protocol==JBC_PROTO_02){
        // Keep the recurring P02 tier faithful to UpdateData_SOLD::UpdateStationParam.
        // Public service Get... APIs are intentionally excluded from cyclic traffic.
        static const uint8_t p02_cmds[]={JBC_CMD_PIN_ENABLED_SOLD,JBC_CMD_PIN_SOLD,JBC_CMD_MAX_TEMP_SOLD,JBC_CMD_MIN_TEMP_SOLD,JBC_CMD_ROBOT_CONFIG_SOLD,JBC_CMD_ETHERNET_P02_SOLD};
        stages_total=(uint8_t)sizeof(p02_cmds);
        if(next_sold_station_param_stage>=stages_total)next_sold_station_param_stage=0;cmd=p02_cmds[next_sold_station_param_stage];
        const bool robot=cmd==JBC_CMD_ROBOT_CONFIG_SOLD,ether=cmd==JBC_CMD_ETHERNET_P02_SOLD;
        if((robot&&!sold_supports_robot())||(ether&&!sold_supports_ethernet())) ok=true; else ok=jbc_send_sold_station_read(cmd);
        if(ok){++next_sold_station_param_stage;if(next_sold_station_param_stage>=stages_total){next_sold_station_param_stage=0;wrapped=true;}}
      }
      if(wrapped&&next_sold_station_param_fast)next_sold_station_param_fast=false;
      const uint32_t spacing=next_sold_station_param_fast?150UL:max((uint32_t)750UL,(uint32_t)(15000UL/max((uint8_t)1,stages_total)));
      next_sold_station_param_poll_ms=now+(ok?spacing:150UL);
    }

    // Public SOLD/P02 service reads (RemoteMode/PowerLimit/InterfaceConfig/
    // AutoClean/DateTime/FrontalConnection/GroundType/StationInterface) are
    // intentionally not polled in the background. They are outside
    // UpdateData_SOLD and a real DDE trace showed 0xBB ReadStationDateTime as
    // the last request immediately before the station stopped responding.
    // Keep their decoders for future on-demand diagnostics, but never inject
    // them into the normal cyclic traffic.
    // JBC DLL fidelity: UpdateData calls ReadPeripheralStatus for every peripheral
    // every 2 s, regardless of whether its 0xFA configuration maps to a known
    // type/port. Configuration (including count) is refreshed every 5 s.
    // Spread the DLL bursts over the interval to keep the embedded request queue
    // shallow while preserving the same per-ID refresh cadence.
    if (!uid_provisioning && sold_supports_peripherals()) {
      const uint8_t count=min(jbc_sold_peripheral_count,(uint8_t)4);

      if (count && (int32_t)(now - next_sold_peripheral_status_poll_ms) >= 0) {
        if(next_sold_peripheral_status_id>=count) next_sold_peripheral_status_id=0;
        const uint8_t id=next_sold_peripheral_status_id;
        const bool ok=(p02_recent_pending_count(2500UL)<8U)&&jbc_send_sold_peripheral_read(JBC_CMD_PERIPHERAL_STATUS_SOLD,id);
        if(ok) next_sold_peripheral_status_id=(uint8_t)((id+1U)%count);
        const uint32_t per_id_ms=max((uint32_t)500UL,(uint32_t)(2000UL/max((uint8_t)1,count)));
        next_sold_peripheral_status_poll_ms=now+(ok?per_id_ms:250UL);
      }

      if ((int32_t)(now - next_sold_peripheral_config_poll_ms) >= 0) {
        bool ok=false;
        if(next_sold_peripheral_config_stage==0){
          ok=(p02_recent_pending_count(2500UL)<8U)&&jbc_send_sold_station_read(JBC_CMD_PERIPHERAL_COUNT_SOLD);
          if(ok){
            if(count){ next_sold_peripheral_config_stage=1; next_sold_peripheral_config_poll_ms=now+1000UL; }
            else next_sold_peripheral_config_poll_ms=now+5000UL;
          }
        } else {
          const uint8_t id=(uint8_t)(next_sold_peripheral_config_stage-1U);
          if(id>=count){
            next_sold_peripheral_config_stage=0;
            next_sold_peripheral_config_poll_ms=now+500UL;
            ok=true;
          } else {
            ok=(p02_recent_pending_count(2500UL)<8U)&&jbc_send_sold_peripheral_read(JBC_CMD_PERIPHERAL_CONFIG_SOLD,id);
            if(ok){
              if((uint8_t)(id+1U)>=count){
                next_sold_peripheral_config_stage=0;
                const uint32_t tail_ms=max((uint32_t)500UL,(uint32_t)(5000UL-(uint32_t)count*1000UL));
                next_sold_peripheral_config_poll_ms=now+tail_ms;
              } else {
                ++next_sold_peripheral_config_stage;
                next_sold_peripheral_config_poll_ms=now+1000UL;
              }
            }
          }
        }
        if (!ok && (int32_t)(now - next_sold_peripheral_config_poll_ms) >= 0) next_sold_peripheral_config_poll_ms = now + 250UL;
      }
    }
    // ALE exposes two additional safe read APIs: Tin Feeder configuration
    // (0x70) and programs 0..4 (0x72). Poll only on ALE and keep the cadence
    // slow because these are configuration/service data, not live control.
    if (!uid_provisioning && sold_supports_ale_feeder() &&
        (int32_t)(now - next_sold_ale_feeder_poll_ms) >= 0) {
      const uint8_t limit = poll_port_limit();
      const uint8_t p = 0; // ReceiveFrame02_SOLD stores 0x70 explicitly on port 0.
      bool ok = false;
      if (p < limit && jbc_ports[p].valid) {
        if (next_sold_ale_feeder_stage == 0) ok = jbc_send_sold_ale_feeder_read(p, -1);
        else ok = jbc_send_sold_ale_feeder_read(p, (int8_t)(next_sold_ale_feeder_stage - 1U));
      }
      if (ok) { ++next_sold_ale_feeder_stage; if (next_sold_ale_feeder_stage >= 6U) next_sold_ale_feeder_stage = 0; }
      next_sold_ale_feeder_poll_ms = now + (ok ? 2500UL : 200UL);
    }
    // Original UpdateData_SOLD::UpdatePortStatus reads MOS temperature for
    // every port independently of InfoPort. P01 has no FID, so the helper
    // politely retries whenever another transaction owns the single slot.
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SOLD &&
        (int32_t)(now - next_sold_mos_poll_ms) >= 0) {
      const uint8_t limit = poll_port_limit();
      if (next_sold_mos_port >= limit) next_sold_mos_port = 0;
      const uint8_t p = next_sold_mos_port;
      const bool ok = jbc_send_sold_mos_temp(p);
      if (ok || !jbc_ports[p].valid) next_sold_mos_port = (uint8_t)((p + 1) % max((uint8_t)1, limit));
      next_sold_mos_poll_ms = now + (ok ? max((uint32_t)750UL, (uint32_t)(5000UL / max((uint8_t)1, limit))) : 100UL);
    }
    // DLL High tier: transformer temperature every ~5 s. ConnectStatus belongs
    // to the 15 s station-parameter tier and has its own timer below.
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SOLD &&
        (int32_t)(now - next_sold_station_status_poll_ms) >= 0) {
      const bool ok = jbc_send_sold_station_read(JBC_CMD_TRAFO_TEMP_SOLD);
      next_sold_station_status_poll_ms = now + (ok ? 5000UL : 100UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_02 &&
        sold_supports_robot() && (int32_t)(now - next_sold_robot_status_poll_ms) >= 0) {
      // UpdateStationStatus(): RobotStatus is High tier (~5 s); RobotConfig
      // remains in the Moderate/15 s station-parameter group.
      const bool ok = jbc_send_sold_station_read(JBC_CMD_ROBOT_STATUS_SOLD);
      next_sold_robot_status_poll_ms = now + (ok ? 5000UL : 150UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_02 &&
        sold_supports_profiles() && (int32_t)(now - next_sold_selected_profile_poll_ms) >= 0) {
      // UpdatePortStatus(): SelectedFile is read for every port in the High
      // tier, independently of whether a tool is currently connected.
      const uint8_t limit = max((uint8_t)1, poll_port_limit());
      if (next_sold_selected_profile_port >= limit) next_sold_selected_profile_port = 0;
      const uint8_t p = next_sold_selected_profile_port;
      const bool ok = p < JBC_MAX_PORTS && jbc_ports[p].valid && jbc_send_sold_selected_profile(p);
      if (ok || p >= JBC_MAX_PORTS || !jbc_ports[p].valid)
        next_sold_selected_profile_port = (uint8_t)((p + 1U) % limit);
      const uint32_t spacing = max((uint32_t)500UL, (uint32_t)(5000UL / limit));
      next_sold_selected_profile_poll_ms = now + (ok ? spacing : 150UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SOLD &&
        (int32_t)(now - next_sold_connect_poll_ms) >= 0) {
      const bool ok = jbc_send_sold_station_read(JBC_CMD_CONNECT_READ_P02_USB);
      next_sold_connect_poll_ms = now + (ok ? 15000UL : 150UL);
    }
    // Temperature error-trigger thresholds are read-only. P01's original
    // UpdateStationParam includes them; P02 exposes the same reads through the
    // public API. Keep them deliberately slow.
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SOLD &&
        (int32_t)(now - next_sold_temp_error_poll_ms) >= 0) {
      const uint8_t cmd = next_sold_temp_error_mos ? JBC_CMD_TEMP_ERROR_MOS_SOLD : JBC_CMD_TEMP_ERROR_TRAFO_SOLD;
      const bool ok = jbc_send_sold_station_read(cmd);
      if (ok) next_sold_temp_error_mos = !next_sold_temp_error_mos;
      next_sold_temp_error_poll_ms = now + (ok ? 7500UL : 150UL);
    }
    // k20/k26 public read-only diagnostics. These are not part of the DLL's
    // high-frequency UpdateData loop, so keep them very slow. Crucially 0x87
    // (read-and-clear alarm latch) is NOT in this table.
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_sold_diag_poll_ms) >= 0) {
      static const uint8_t cmds[] = {JBC_CMD_TOOL_TYPE_SOLD, JBC_CMD_TOOL_LAST_ERROR_SOLD,
                                     JBC_CMD_ALARM_MAX_SOLD, JBC_CMD_ALARM_MIN_SOLD};
      const uint8_t limit = poll_port_limit();
      if (next_sold_diag_port >= limit) next_sold_diag_port = 0;
      if (next_sold_diag_stage >= sizeof(cmds)) next_sold_diag_stage = 0;
      const uint8_t p = next_sold_diag_port;
      bool ok = false;
      if (p < JBC_MAX_PORTS && jbc_ports[p].valid)
        ok = jbc_send_sold_diag(p, cmds[next_sold_diag_stage]);
      if (ok) {
        ++next_sold_diag_stage;
        if (next_sold_diag_stage >= sizeof(cmds)) {
          next_sold_diag_stage = 0;
          next_sold_diag_port = (uint8_t)((p + 1) % max((uint8_t)1, limit));
        }
      } else if (!jbc_ports[p].valid) {
        next_sold_diag_stage = 0;
        next_sold_diag_port = (uint8_t)((p + 1) % max((uint8_t)1, limit));
      }
      next_sold_diag_poll_ms = now + (ok ? max((uint32_t)750UL, (uint32_t)(15000UL / (4UL * max((uint8_t)1, limit)))) : 250UL);
    }
    if (!uid_provisioning && sold_supports_qst() && (int32_t)(now - next_sold_qst_poll_ms) >= 0) {
      const JbcProtocol cp = jbc_command_protocol == JBC_PROTO_UNKNOWN ? jbc_frame_protocol : jbc_command_protocol;
      const uint8_t cmd = cp == JBC_PROTO_01
        ? (next_sold_qst_status ? JBC_CMD_QST_STATUS_P01 : JBC_CMD_QST_ACTIVATE_P01)
        : (next_sold_qst_status ? JBC_CMD_QST_STATUS_P02 : JBC_CMD_QST_ACTIVATE_P02);
      if (jbc_send_sold_qst(cmd)) next_sold_qst_status = !next_sold_qst_status;
      next_sold_qst_poll_ms = now + 2500UL; // each station value about every 5 s
    }
    if (!uid_provisioning && sold_supports_qst() && (int32_t)(now - next_sold_lock_poll_ms) >= 0) {
      const uint8_t limit = poll_port_limit();
      if (next_sold_lock_port >= limit) next_sold_lock_port = 0;
      const uint8_t p = next_sold_lock_port;
      const bool ok = jbc_send_sold_lock_port(p);
      next_sold_lock_port = (uint8_t)((p + 1) % max((uint8_t)1, limit));
      // UpdateData_SOLD polls ReadLockPort as normal port status whenever QST
      // is supported. Keep it slower than InfoPort to avoid needless USB load.
      next_sold_lock_poll_ms = now + (ok ? max((uint32_t)750UL, (uint32_t)(5000UL / max((uint8_t)1, limit))) : 250UL);
    }
    if (!uid_provisioning && (jbc_station_kind == JBC_STATION_SOLD || jbc_station_kind == JBC_STATION_HA) &&
        (int32_t)(now - next_conti_read_poll_ms) >= 0) {
      // Original JBC_Connect behavior: SOLD and HOT_AIR use the station's
      // continuous telemetry stream. Start T_10mS only when 0x80 reports OFF;
      // an already-active station-selected rate is left untouched. The DDE
      // stability fix is request serialization, not disabling ContiMode.
      bool ok = false;
      const bool stream_off = jbc_continuous_valid &&
        jbc_continuous_speed == JBC_CONTI_SPEED_OFF;
      if (stream_off) {
        const uint8_t limit = poll_port_limit();
        const uint8_t ports = limit >= 4 ? 0x0F : (uint8_t)((1U << limit) - 1U);
        ok = jbc_send_conti_write(JBC_CONTI_SPEED_10MS, ports);
        if (ok) {
          // Wait for the 0x80 read-back before accepting 0x82 as authoritative.
          jbc_continuous_valid = false;
          next_conti_read_poll_ms = now + 250UL;
        } else next_conti_read_poll_ms = now + 500UL;
      } else {
        const bool had_config = jbc_continuous_valid;
        ok = jbc_send_conti_read();
        next_conti_read_poll_ms = now + (ok ? (had_config ? 5000UL : 500UL) : 500UL);
      }
    }
    if (!uid_provisioning && (jbc_station_kind == JBC_STATION_PH || jbc_station_kind == JBC_STATION_SF) &&
        jbc_frame_protocol == JBC_PROTO_02 && (int32_t)(now - next_conti_read_poll_ms) >= 0) {
      // PH/SF expose ReadContiMode(0x80) too, but unlike SOLD/HA we only read it:
      // never start/change the station stream from this service-completeness path.
      const bool ok=jbc_send_conti_read();
      next_conti_read_poll_ms=now+(ok?5000UL:500UL);
    }
    // Original UpdateData_HA tiers: selected values every 2 s, external air
    // temperature every 1 s, station/tool parameters every 15 s and counters
    // every 60 s. We spread each group across the interval instead of bursting
    // the whole group into the CP210x queue at the tick boundary.
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_HA && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_ha_selected_poll_ms) >= 0) {
      static const uint8_t cmds[] = {JBC_CMD_SELECT_TEMP_HA, JBC_CMD_SELECT_FLOW_HA, JBC_CMD_SELECT_EXT_TEMP_HA};
      const uint8_t limit = max((uint8_t)1, poll_port_limit());
      if (next_ha_selected_port >= limit) next_ha_selected_port = 0;
      if (next_ha_selected_stage >= sizeof(cmds)) next_ha_selected_stage = 0;
      const uint8_t p = next_ha_selected_port;
      const bool ok = p < JBC_MAX_PORTS && jbc_ports[p].valid && jbc_send_ha_detail(p, cmds[next_ha_selected_stage]);
      if (ok) {
        ++next_ha_selected_stage;
        if (next_ha_selected_stage >= sizeof(cmds)) {
          next_ha_selected_stage = 0;
          next_ha_selected_port = (uint8_t)((p + 1U) % limit);
          if (next_ha_selected_port == 0) next_ha_selected_fast = false;
        }
      } else if (p >= JBC_MAX_PORTS || !jbc_ports[p].valid) {
        next_ha_selected_stage = 0;
        next_ha_selected_port = (uint8_t)((p + 1U) % limit);
      }
      const uint32_t spacing = next_ha_selected_fast ? 120UL :
          max((uint32_t)180UL, (uint32_t)(2000UL / (3UL * limit)));
      next_ha_selected_poll_ms = now + (ok ? spacing : 100UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_HA && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_ha_ext_temp_poll_ms) >= 0) {
      const uint8_t limit = max((uint8_t)1, poll_port_limit());
      if (next_ha_ext_temp_port >= limit) next_ha_ext_temp_port = 0;
      const uint8_t p = next_ha_ext_temp_port;
      const bool ok = p < JBC_MAX_PORTS && jbc_ports[p].valid && jbc_send_ha_detail(p, JBC_CMD_ACTUAL_EXT_TEMP_HA);
      next_ha_ext_temp_port = (uint8_t)((p + 1U) % limit);
      const uint32_t spacing = max((uint32_t)180UL, (uint32_t)(1000UL / limit));
      next_ha_ext_temp_poll_ms = now + (ok ? spacing : 100UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_HA && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_ha_counter_poll_ms) >= 0) {
      static const uint8_t cmds[] = {JBC_CMD_COUNTER_PLUG_HA, JBC_CMD_COUNTER_WORK_HA,
        JBC_CMD_COUNTER_WORK_CYCLES_HA, JBC_CMD_COUNTER_SUCTION_CYCLES_HA,
        JBC_CMD_COUNTER_PLUG_PARTIAL_HA, JBC_CMD_COUNTER_WORK_PARTIAL_HA,
        JBC_CMD_COUNTER_WORK_CYCLES_PARTIAL_HA, JBC_CMD_COUNTER_SUCTION_CYCLES_PARTIAL_HA};
      const uint8_t limit = max((uint8_t)1, poll_port_limit());
      if (next_ha_counter_port >= limit) next_ha_counter_port = 0;
      if (next_ha_counter_stage >= sizeof(cmds)) next_ha_counter_stage = 0;
      const uint8_t p = next_ha_counter_port;
      const uint8_t stage = next_ha_counter_stage;
      const bool feature_skip = stage >= 4U && !ha_supports_partial_counters();
      const bool initial_done = jbc_initial_low_stage_done(p, stage);
      bool ok = initial_done || feature_skip;
      if (!ok && p < JBC_MAX_PORTS && jbc_ports[p].valid) ok = jbc_send_ha_detail(p, cmds[stage]);
      if (ok) {
        ++next_ha_counter_stage;
        if (next_ha_counter_stage >= sizeof(cmds)) {
          next_ha_counter_stage = 0;
          next_ha_counter_port = (uint8_t)((p + 1U) % limit);
          if (next_ha_counter_port == 0 && next_ha_counter_fast) {
            if (jbc_initial_low.tracking) { if (jbc_initial_low_finish_or_verify()) { next_ha_counter_fast = false; } }
            else next_ha_counter_fast = false;
          }
        }
      } else if (p >= JBC_MAX_PORTS || !jbc_ports[p].valid) {
        if (!jbc_initial_low.tracking) {
          next_ha_counter_stage = 0;
          next_ha_counter_port = (uint8_t)((p + 1U) % limit);
        }
      }
      const uint32_t spacing = next_ha_counter_fast ? (initial_done ? 10UL : JBC_INITIAL_LOW_FAST_SPACING_MS) :
          max((uint32_t)500UL, (uint32_t)(60000UL / (8UL * limit)));
      next_ha_counter_poll_ms = now + (ok ? spacing : 120UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_HA && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_ha_station_status_poll_ms) >= 0) {
      const bool ok = jbc_send_ha_connect_status();
      next_ha_station_status_poll_ms = now + (ok ? 15000UL : 250UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_HA && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_ha_station_diag_poll_ms) >= 0) {
      static const uint8_t cmds[] = {JBC_CMD_REMOTE_MODE_HA, JBC_CMD_TEMP_UNIT_HA,
        JBC_CMD_MAXMIN_TEMP_HA, JBC_CMD_MAXMIN_FLOW_HA, JBC_CMD_MAXMIN_EXT_TEMP_HA,
        JBC_CMD_PIN_ENABLED_HA, JBC_CMD_PIN_HA, JBC_CMD_BEEP_HA,
        JBC_CMD_SELECTED_PROFILE_HA, JBC_CMD_ROBOT_CONFIG_HA, JBC_CMD_ROBOT_STATUS_HA};
      if (next_ha_station_diag_stage >= sizeof(cmds)) next_ha_station_diag_stage = 0;
      const bool ok = jbc_send_ha_station_diag(cmds[next_ha_station_diag_stage]);
      if (ok) {
        ++next_ha_station_diag_stage;
        if (next_ha_station_diag_stage >= sizeof(cmds)) { next_ha_station_diag_stage = 0; next_ha_station_diag_fast = false; }
      }
      const uint32_t spacing = next_ha_station_diag_fast ? 150UL :
          max((uint32_t)500UL, (uint32_t)(15000UL / (uint32_t)sizeof(cmds)));
      next_ha_station_diag_poll_ms = now + (ok ? spacing : 180UL);
    }
    if (!uid_provisioning && jbc_station_kind == JBC_STATION_HA && jbc_frame_protocol == JBC_PROTO_02 &&
        (int32_t)(now - next_ha_detail_poll_ms) >= 0) {
      const uint8_t limit = max((uint8_t)1, poll_port_limit());
      if (next_ha_detail_port >= limit) next_ha_detail_port = 0;
      const uint8_t p = next_ha_detail_port;
      bool ok = false;
      static const uint8_t cmds_with_levels[] = {
        JBC_CMD_ADJUST_TEMP_HA, JBC_CMD_TIME_TO_STOP_HA, JBC_CMD_EXTERNAL_TC_MODE_HA, JBC_CMD_START_MODE_HA,
        JBC_CMD_PROFILE_MODE_HA, JBC_CMD_LEVELS_HA, JBC_CMD_HEATER_STATUS_HA, JBC_CMD_SUCTION_STATUS_HA,
        JBC_CMD_AIR_TEMP_HA, JBC_CMD_POWER_HA, JBC_CMD_AIR_FLOW_HA, JBC_CMD_CONNECT_TOOL_HA,
        JBC_CMD_TOOL_ERROR_HA, JBC_CMD_TOOL_STATUS_HA
      };
      static const uint8_t cmds_no_levels[] = {
        JBC_CMD_ADJUST_TEMP_HA, JBC_CMD_TIME_TO_STOP_HA, JBC_CMD_EXTERNAL_TC_MODE_HA, JBC_CMD_START_MODE_HA,
        JBC_CMD_PROFILE_MODE_HA, JBC_CMD_HEATER_STATUS_HA, JBC_CMD_SUCTION_STATUS_HA,
        JBC_CMD_AIR_TEMP_HA, JBC_CMD_POWER_HA, JBC_CMD_AIR_FLOW_HA, JBC_CMD_CONNECT_TOOL_HA,
        JBC_CMD_TOOL_ERROR_HA, JBC_CMD_TOOL_STATUS_HA
      };
      const bool levels = ha_supports_temp_levels();
      const uint8_t stages = levels ? (uint8_t)(sizeof(cmds_with_levels) / sizeof(cmds_with_levels[0]))
                                    : (uint8_t)(sizeof(cmds_no_levels) / sizeof(cmds_no_levels[0]));
      if (next_ha_detail_stage >= stages) next_ha_detail_stage = 0;
      if (p < JBC_MAX_PORTS && jbc_ports[p].valid) {
        const uint8_t cmd = levels ? cmds_with_levels[next_ha_detail_stage] : cmds_no_levels[next_ha_detail_stage];
        ok = jbc_send_ha_detail(p, cmd);
        if (ok) {
          ++next_ha_detail_stage;
          if (next_ha_detail_stage >= stages) {
            next_ha_detail_stage = 0;
            next_ha_detail_port = (uint8_t)((p + 1U) % limit);
          }
        }
      }
      if (!ok && (p >= JBC_MAX_PORTS || !jbc_ports[p].valid || !jbc_ports[p].tool)) {
        next_ha_detail_stage = 0;
        next_ha_detail_port = (uint8_t)((p + 1U) % limit);
      }
      const bool fast_refresh = (int32_t)(next_ha_tool_refresh_fast_until_ms - now) > 0;
      const uint32_t spacing = fast_refresh ? 150UL :
          max((uint32_t)400UL, (uint32_t)(15000UL / ((uint32_t)stages * limit)));
      next_ha_detail_poll_ms = now + (ok ? spacing : 100UL);
    }
    if (!uid_provisioning && (int32_t)(now - next_station_error_poll_ms) >= 0) {
      // P01 has one transaction slot only; retry shortly if InfoPort currently
      // owns it instead of postponing the error read for another full period.
      next_station_error_poll_ms = now + (jbc_send_station_error() ? 5000UL : 50UL);
    }
    recompute_work_masks();
  }
}

// -----------------------------------------------------------------------------
// OFE RS485 protocol
// -----------------------------------------------------------------------------
static bool discover_response_pending = false;
static uint8_t discover_response_dst = ADDR_MASTER;
static uint8_t discover_response_seq = 0;
static uint32_t discover_response_due_ms = 0;
static uint8_t join_announce_left = 0;
static uint32_t next_join_announce_ms = 0;

static void rs485_status_response(const Frame& req, Status status) {
  Frame resp;
  resp.dst = req.src; resp.src = module_addr; resp.seq = req.seq; resp.cmd = req.cmd | 0x80;
  resp.len = 1; resp.payload[0] = status;
  bus.send(resp);
}

static void copy_label_from_payload(const Frame& req) {
  uint8_t n = min(req.len, (uint8_t)(sizeof(module_label) - 1));
  for (uint8_t i = 0; i < n; ++i) {
    char c = (char)req.payload[i];
    module_label[i] = ((uint8_t)c < 0x20 || c == '"' || c == '\\' || c == '<' || c == '>') ? ' ' : c;
  }
  module_label[n] = 0;
  while (n > 0 && module_label[n - 1] == ' ') module_label[--n] = 0;
}
static void rs485_set_label(const Frame& req) {
  copy_label_from_payload(req);
  bool ok = true;
  if (module_label[0]) ok = prefs.putString("label", module_label) > 0;
  else prefs.remove("label");
  rs485_status_response(req, ok ? STATUS_OK : STATUS_BUSY);
}

static void rs485_info(const Frame& req) {
  Frame resp;
  resp.dst = req.src; resp.src = module_addr; resp.seq = req.seq; resp.cmd = CMD_INFO | 0x80;
  size_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = MODULE_JBC_USB;
  resp.payload[o++] = PROTOCOL_VERSION;
  put_u16_le(resp.payload + o, HW_VERSION); o += 2;
  resp.payload[o++] = FW_MAJOR; resp.payload[o++] = FW_MINOR; resp.payload[o++] = FW_PATCH;
  put_u64_le(resp.payload + o, module_uid()); o += 8;
  resp.payload[o++] = module_addr;
  resp.payload[o++] = 2; // interface/protocol generation
  uint8_t suffix_len = (uint8_t)min(strlen(FW_SUFFIX), (size_t)7);
  resp.payload[o++] = suffix_len;
  for (uint8_t i = 0; i < suffix_len && o < MAX_PAYLOAD; ++i) resp.payload[o++] = (uint8_t)FW_SUFFIX[i];
  const char* shown = module_label[0] ? module_label : "JBC USB";
  while (*shown && o < MAX_PAYLOAD) resp.payload[o++] = (uint8_t)*shown++;
  resp.len = (uint8_t)o;
  bus.send(resp);
}
static void rs485_caps(const Frame& req) {
  Frame resp;
  resp.dst = req.src; resp.src = module_addr; resp.seq = req.seq; resp.cmd = CMD_GET_CAPS | 0x80;
  resp.len = 5; resp.payload[0] = STATUS_OK; put_u32_le(resp.payload + 1, MODULE_CAPS);
  bus.send(resp);
}
static void rs485_fast_poll(const Frame& req) {
  Frame resp;
  resp.dst = req.src; resp.src = module_addr; resp.seq = req.seq; resp.cmd = CMD_FAST_POLL | 0x80;
  resp.len = 6; resp.payload[0] = STATUS_OK;
  put_u16_le(resp.payload + 1, event_seq);
  resp.payload[3] = work_mask; resp.payload[4] = stand_mask; resp.payload[5] = fast_flags;
  bus.send(resp);
  fast_flags &= (uint8_t)~FAST_FLAG_STATE_CHANGED;
}
static void rs485_get_state(const Frame& req) {
  Frame resp;
  resp.dst = req.src; resp.src = module_addr; resp.seq = req.seq; resp.cmd = CMD_GET_STATE | 0x80;
  uint8_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = fast_flags;
  resp.payload[o++] = (uint8_t)jbc_link_state;
  resp.payload[o++] = (uint8_t)jbc_frame_protocol;
  resp.payload[o++] = (uint8_t)jbc_command_protocol;
  resp.payload[o++] = jbc_host_addr;
  resp.payload[o++] = jbc_station_addr;
  resp.payload[o++] = jbc_port_count;
  resp.payload[o++] = work_mask;
  resp.payload[o++] = stand_mask;
  put_u16_le(resp.payload + o, event_seq); o += 2;
  put_u16_le(resp.payload + o, cp_vid); o += 2;
  put_u16_le(resp.payload + o, cp_pid); o += 2;
  put_u16_le(resp.payload + o, jbc_model_version); o += 2;
  resp.payload[o++] = jbc_port_count_from_model ? 1 : 0;

  auto append_text = [&](const char* text, uint8_t max_len) {
    const size_t room = MAX_PAYLOAD - o;
    if (!room) return;
    const uint8_t n = (uint8_t)min(min(strlen(text ? text : ""), (size_t)max_len), room - 1);
    resp.payload[o++] = n;
    if (n) { memcpy(resp.payload + o, text, n); o += n; }
  };
  append_text(jbc_protocol_text, STATE_PROTOCOL_TEXT_MAX);
  append_text(jbc_model_raw, STATE_MODEL_RAW_MAX);
  append_text(jbc_model, STATE_MODEL_MAX);
  append_text(jbc_model_type, STATE_MODEL_TYPE_MAX);
  append_text(jbc_sw_version, STATE_SW_MAX);
  append_text(jbc_hw_version, STATE_HW_MAX);

  for (uint8_t i = 0; i < JBC_MAX_PORTS && o + JBC_STATE_PORT_BYTES <= MAX_PAYLOAD; ++i) {
    const JbcPortState& p = jbc_ports[i];
    resp.payload[o++] = p.valid ? 1 : 0;
    resp.payload[o++] = p.tool;
    resp.payload[o++] = p.error;
    resp.payload[o++] = (p.stand ? 1 : 0) | (p.sleep ? 2 : 0) | (p.hibernation ? 4 : 0) |
                        (p.extractor ? 8 : 0) | (p.desolder ? 16 : 0) |
                        (p.heater ? 32 : 0) | (p.cooling ? 64 : 0) | (p.suction ? 128 : 0);
    put_u16_le(resp.payload + o, p.temp); o += 2;
    put_u16_le(resp.payload + o, p.power_permille); o += 2;
    put_u16_le(resp.payload + o, p.time_to_sleep_hibern); o += 2;
    resp.payload[o++] = p.future_mode;
    resp.payload[o++] = p.detail_flags;
    // The final three bytes are station-specific. SOLD keeps the configured
    // Sleep/Hibernation delays introduced in 0.1.13. HA uses the same slots for
    // the live 16-bit TimeToStop from ReceiveFrame02_HA::M_INF_PORT. Bit7 marks
    // this representation so a new Master can distinguish it unambiguously.
    if (jbc_station_kind == JBC_STATION_HA) {
      put_u16_le(resp.payload + o, p.time_to_stop); o += 2;
      resp.payload[o++] = 0x80;
    } else {
      resp.payload[o++] = p.sleep_delay_min;
      resp.payload[o++] = p.hiber_delay_min;
      resp.payload[o++] = p.delay_config_flags;
    }
  }
  // Optional suffix (0.1.12+). Old Masters safely ignore it; new Masters still
  // parse the entire legacy prefix unchanged.
  if (o + 2 <= MAX_PAYLOAD) { put_u16_le(resp.payload + o, jbc_station_error); o += 2; }
  resp.len = o;
  bus.send(resp);
}
static void put_u24_sat(uint8_t* p, uint32_t v) {
  if (v > 0xFFFFFFUL) v = 0xFFFFFFUL;
  p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF); p[2] = (uint8_t)((v >> 16) & 0xFF);
}

static void rs485_get_telemetry(const Frame& req) {
  Frame resp;
  resp.dst = req.src; resp.src = module_addr; resp.seq = req.seq; resp.cmd = CMD_GET_TELEMETRY | 0x80;
  uint8_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = MODULE_JBC_USB;
  put_u32_le(resp.payload + o, ESP.getFreeHeap()); o += 4;
  put_u32_le(resp.payload + o, ESP.getMinFreeHeap()); o += 4;
  put_u32_le(resp.payload + o, monotonic_uptime_seconds()); o += 4;
  put_u32_le(resp.payload + o, usb_rx_bytes); o += 4;
  put_u32_le(resp.payload + o, usb_tx_bytes); o += 4;
  put_u32_le(resp.payload + o, jbc_rx_frames); o += 4;
  put_u32_le(resp.payload + o, jbc_tx_frames); o += 4;
  put_u16_le(resp.payload + o, (uint16_t)min(usb_errors, 65535UL)); o += 2;
  put_u16_le(resp.payload + o, (uint16_t)min(jbc_bcc_errors, 65535UL)); o += 2;
  put_u16_le(resp.payload + o, (uint16_t)min(jbc_frame_errors, 65535UL)); o += 2;
  resp.payload[o++] = (uint8_t)jbc_link_state;
  resp.payload[o++] = (uint8_t)ofe_status_leds.busEvent();
  resp.payload[o++] = (uint8_t)ofe_status_leds.moduleEvent();
  // CP210x diagnostics suffix (0.1.5+). Legacy fields/LED positions above remain unchanged.
  put_u32_le(resp.payload + o, cp_diag_baud); o += 4;
  put_u16_le(resp.payload + o, cp_diag_line_ctl); o += 2;
  resp.payload[o++] = cp_diag_mdmsts;
  put_u32_le(resp.payload + o, cp_diag_comm_errors); o += 4;
  put_u32_le(resp.payload + o, cp_diag_hold_reasons); o += 4;
  put_u32_le(resp.payload + o, cp_diag_in_queue); o += 4;
  put_u32_le(resp.payload + o, cp_diag_out_queue); o += 4;
  resp.payload[o++] = cp_diag_valid ? 1 : 0;
  // 0.1.9+: append JBC station Device-ID/UUID to telemetry. CMD_GET_STATE is
  // already close to MAX_PAYLOAD because it carries four port records.
  const uint8_t uid_n = min(jbc_device_uid_len, (uint8_t)sizeof(jbc_device_uid));
  resp.payload[o++] = uid_n;
  if (uid_n) { memcpy(resp.payload + o, jbc_device_uid, uid_n); o += uid_n; }
  // 0.1.10+: append standard OFE CPU/loop telemetry after the variable-length
  // station UID. Older Masters safely stop before this suffix.
  resp.payload[o++] = cpu_load_pct;
  put_u16_le(resp.payload + o, loop_max_ms); o += 2;
  // 0.1.19+: richer SOLD detail extension. The 192-byte RS485 payload cannot
  // carry all four ports at once, so one indexed port record is emitted per
  // telemetry request and the Master accumulates records. 0.1.20 / extension
  // v4 appends cartridge/service diagnostics; 0.1.23 adds a backward-compatible QST suffix.
  if (jbc_station_kind == JBC_STATION_SOLD) {
    const uint8_t limit = poll_port_limit();
    if (next_sold_telemetry_port >= limit) next_sold_telemetry_port = 0;
    const uint8_t i = next_sold_telemetry_port;
    if (i < JBC_MAX_PORTS) {
      const JbcPortState& ps = jbc_ports[i];
      if (next_sold_telemetry_stage == 0) {
        const size_t need=65U;
        if((size_t)o+need<=MAX_PAYLOAD){
          resp.payload[o++]=0xD7;resp.payload[o++]=4;resp.payload[o++]=1;resp.payload[o++]=i;
          put_u16_le(resp.payload+o,ps.detail_value_flags);o+=2;put_u16_le(resp.payload+o,ps.selected_temp);o+=2;put_u16_le(resp.payload+o,ps.sleep_temp);o+=2;put_u16_le(resp.payload+o,(uint16_t)ps.adjust_temp);o+=2;
          put_u24_sat(resp.payload+o,ps.counter_plug_min);o+=3;put_u24_sat(resp.payload+o,ps.counter_work_min);o+=3;put_u24_sat(resp.payload+o,ps.counter_sleep_min);o+=3;put_u24_sat(resp.payload+o,ps.counter_hiber_min);o+=3;put_u24_sat(resp.payload+o,ps.counter_idle_min);o+=3;put_u24_sat(resp.payload+o,ps.counter_sleep_cycles);o+=3;put_u24_sat(resp.payload+o,ps.counter_desold_cycles);o+=3;
          resp.payload[o++]=ps.levels_on;resp.payload[o++]=ps.selected_level;uint8_t lom=0;for(uint8_t lv=0;lv<3;++lv)if(ps.level_on[lv])lom|=(uint8_t)(1U<<lv);resp.payload[o++]=lom;for(uint8_t lv=0;lv<3;++lv){put_u16_le(resp.payload+o,ps.level_temp[lv]);o+=2;}
          resp.payload[o++]=ps.cartridge_on;put_u16_le(resp.payload+o,(uint16_t)ps.cartridge_jbc_code);o+=2;put_u16_le(resp.payload+o,(uint16_t)ps.cartridge_adjust_300);o+=2;put_u16_le(resp.payload+o,(uint16_t)ps.cartridge_adjust_400);o+=2;resp.payload[o++]=ps.cartridge_group;resp.payload[o++]=ps.cartridge_family;
          put_u16_le(resp.payload+o,(uint16_t)ps.tip_temp_a);o+=2;put_u16_le(resp.payload+o,(uint16_t)ps.tip_temp_b);o+=2;put_u16_le(resp.payload+o,(uint16_t)ps.cartridge_ma_a);o+=2;put_u16_le(resp.payload+o,(uint16_t)ps.cartridge_ma_b);o+=2;put_u16_le(resp.payload+o,(uint16_t)ps.cartridge_power_permille_a);o+=2;put_u16_le(resp.payload+o,(uint16_t)ps.cartridge_power_permille_b);o+=2;resp.payload[o++]=jbc_qst_valid_flags;resp.payload[o++]=jbc_qst_state_flags;
          next_sold_telemetry_stage=1;
        }
      } else if (next_sold_telemetry_stage == 1) {
        if((size_t)o+24U<=MAX_PAYLOAD){resp.payload[o++]=0xD8;resp.payload[o++]=1;resp.payload[o++]=i;resp.payload[o++]=ps.sold_diag_flags;put_u16_le(resp.payload+o,ps.sold_mos_temp);o+=2;resp.payload[o++]=ps.sold_tool_type;resp.payload[o++]=ps.sold_tool_last_error;put_u16_le(resp.payload+o,(uint16_t)ps.sold_alarm_max_temp);o+=2;put_u16_le(resp.payload+o,(uint16_t)ps.sold_alarm_max_delay_tenth_sec);o+=2;put_u16_le(resp.payload+o,(uint16_t)ps.sold_alarm_min_temp);o+=2;put_u16_le(resp.payload+o,(uint16_t)ps.sold_alarm_min_delay_tenth_sec);o+=2;resp.payload[o++]=jbc_sold_station_diag_flags;put_u16_le(resp.payload+o,jbc_sold_trafo_temp);o+=2;put_u16_le(resp.payload+o,jbc_sold_trafo_error_temp);o+=2;put_u16_le(resp.payload+o,jbc_sold_mos_error_temp);o+=2;resp.payload[o++]=jbc_sold_control_mode?1:0;next_sold_telemetry_stage=2;}
      } else if (next_sold_telemetry_stage == 2) {
        const uint8_t pl=min((uint8_t)strlen(ps.sold_selected_profile),(uint8_t)12); const size_t need=33U+pl;
        if((size_t)o+need<=MAX_PAYLOAD){resp.payload[o++]=0xDA;resp.payload[o++]=1;resp.payload[o++]=i;put_u16_le(resp.payload+o,ps.sold_extra_flags);o+=2;
          put_u24_sat(resp.payload+o,ps.sold_partial_plug_min);o+=3;put_u24_sat(resp.payload+o,ps.sold_partial_work_min);o+=3;put_u24_sat(resp.payload+o,ps.sold_partial_sleep_min);o+=3;put_u24_sat(resp.payload+o,ps.sold_partial_hiber_min);o+=3;put_u24_sat(resp.payload+o,ps.sold_partial_idle_min);o+=3;put_u24_sat(resp.payload+o,ps.sold_partial_sleep_cycles);o+=3;put_u24_sat(resp.payload+o,ps.sold_partial_desold_cycles);o+=3;resp.payload[o++]=ps.sold_profile_mode;resp.payload[o++]=ps.sold_assistant_on;put_u16_le(resp.payload+o,(uint16_t)ps.sold_assistant_warning);o+=2;put_u16_le(resp.payload+o,(uint16_t)ps.sold_assistant_error);o+=2;resp.payload[o++]=pl;if(pl){memcpy(resp.payload+o,ps.sold_selected_profile,pl);o+=pl;}next_sold_telemetry_stage=3;}
      } else if (next_sold_telemetry_stage == 3) {
        const uint8_t pc=min(jbc_sold_peripheral_count,(uint8_t)4); const size_t need=21U+(size_t)pc*8U;
        if((size_t)o+need<=MAX_PAYLOAD){resp.payload[o++]=0xDB;resp.payload[o++]=1;put_u16_le(resp.payload+o,jbc_sold_extra_station_flags);o+=2;put_u16_le(resp.payload+o,jbc_sold_min_temp);o+=2;put_u16_le(resp.payload+o,jbc_sold_max_temp);o+=2;memcpy(resp.payload+o,jbc_sold_pin,4);o+=4;memcpy(resp.payload+o,jbc_sold_robot_config,7);o+=7;resp.payload[o++]=jbc_sold_peripheral_count;resp.payload[o++]=pc;for(uint8_t id=0;id<pc;++id){const SoldPeripheralState& sp=jbc_sold_peripherals[id];resp.payload[o++]=sp.flags;resp.payload[o++]=sp.version;resp.payload[o++]=sp.type;resp.payload[o++]=sp.port;resp.payload[o++]=sp.function;resp.payload[o++]=sp.activation;resp.payload[o++]=sp.delay;resp.payload[o++]=sp.pd_status;}next_sold_telemetry_stage=4;}
      } else if (next_sold_telemetry_stage == 4) {
        // D3/v1 carries the two CPeripheralData fields that DB/v1 did not have:
        // Hash_MCU_UID (4 chars) + DateTime (14 chars) for each peripheral.
        const uint8_t pc=min(jbc_sold_peripheral_count,(uint8_t)4); const size_t need=3U+(size_t)pc*18U;
        if((size_t)o+need<=MAX_PAYLOAD){resp.payload[o++]=0xD3;resp.payload[o++]=1;resp.payload[o++]=pc;for(uint8_t id=0;id<pc;++id){const SoldPeripheralState& sp=jbc_sold_peripherals[id];memcpy(resp.payload+o,sp.hash_mcu_uid,4);o+=4;memcpy(resp.payload+o,sp.datetime,14);o+=14;}next_sold_telemetry_stage=6;}
      } else if (next_sold_telemetry_stage == 6) {
        const size_t need=17U;
        if((size_t)o+need<=MAX_PAYLOAD){resp.payload[o++]=0xDD;resp.payload[o++]=1;resp.payload[o++]=i;put_u16_le(resp.payload+o,ps.sold_readonly_port_flags);o+=2;put_u16_le(resp.payload+o,ps.sold_fixed_temp);o+=2;resp.payload[o++]=ps.sold_fixed_temp_on;resp.payload[o++]=ps.sold_assistant_warning_code;put_u16_le(resp.payload+o,(uint16_t)ps.sold_result_similarity);o+=2;put_u16_le(resp.payload+o,(uint16_t)ps.sold_result_tenths);o+=2;put_u16_le(resp.payload+o,(uint16_t)ps.sold_result_energy);o+=2;put_u16_le(resp.payload+o,ps.sold_direct_power_permille);o+=2;next_sold_telemetry_stage=7;}
      } else if (next_sold_telemetry_stage == 7) {
        const size_t need=40U;
        if((size_t)o+need<=MAX_PAYLOAD){resp.payload[o++]=0xDC;resp.payload[o++]=1;put_u32_le(resp.payload+o,jbc_sold_readonly_flags);o+=4;resp.payload[o++]=jbc_sold_remote_mode?1:0;resp.payload[o++]=jbc_sold_temp_unit;resp.payload[o++]=jbc_sold_n2_mode?1:0;resp.payload[o++]=jbc_sold_help_text?1:0;put_u16_le(resp.payload+o,jbc_sold_power_limit);o+=2;resp.payload[o++]=jbc_sold_beep?1:0;memcpy(resp.payload+o,jbc_sold_interface,7);o+=7;put_u16_le(resp.payload+o,jbc_sold_graph_temp_max);o+=2;put_u16_le(resp.payload+o,jbc_sold_graph_temp_min);o+=2;put_u16_le(resp.payload+o,jbc_sold_graph_temp_range);o+=2;put_u16_le(resp.payload+o,jbc_sold_graph_power_max);o+=2;put_u16_le(resp.payload+o,jbc_sold_graph_power_min);o+=2;resp.payload[o++]=jbc_sold_autoclean?1:0;put_u16_le(resp.payload+o,jbc_sold_autoclean_temp);o+=2;put_u16_le(resp.payload+o,jbc_sold_autoclean_seconds);o+=2;resp.payload[o++]=jbc_sold_ground_type;memcpy(resp.payload+o,jbc_sold_station_interface,4);o+=4;next_sold_telemetry_stage=8;}
      } else if (next_sold_telemetry_stage == 8) {
        const uint8_t fn=min((uint8_t)strlen(jbc_sold_frontal),(uint8_t)20);const size_t need=35U+fn;
        if((size_t)o+need<=MAX_PAYLOAD){resp.payload[o++]=0xDF;resp.payload[o++]=1;put_u16_le(resp.payload+o,(uint16_t)((jbc_sold_readonly_flags>>13)&0x0FU));o+=2;memcpy(resp.payload+o,jbc_sold_datetime,7);o+=7;memcpy(resp.payload+o,jbc_sold_ethernet,23);o+=23;resp.payload[o++]=fn;if(fn){memcpy(resp.payload+o,jbc_sold_frontal,fn);o+=fn;}if(sold_supports_ale_feeder())next_sold_telemetry_stage=9;else if(!strcmp(jbc_model,"CDE")&&sold_k26_protocol())next_sold_telemetry_stage=10;else{next_sold_telemetry_stage=0;next_sold_telemetry_port=(uint8_t)((i+1)%max((uint8_t)1,limit));}}
      } else if (next_sold_telemetry_stage == 9) {
        // D5/v1 is ALE-only Tin Feeder configuration + programs 0..4 + live motor state.
        const size_t need=79U;
        if((size_t)o+need<=MAX_PAYLOAD){resp.payload[o++]=0xD5;resp.payload[o++]=1;resp.payload[o++]=i;put_u16_le(resp.payload+o,ps.sold_feeder_flags);o+=2;resp.payload[o++]=ps.sold_feeder_working_mode;resp.payload[o++]=ps.sold_feeder_selected_program;put_u16_le(resp.payload+o,ps.sold_feeder_delivery_length);o+=2;put_u16_le(resp.payload+o,ps.sold_feeder_delivery_speed);o+=2;resp.payload[o++]=ps.sold_feeder_tin_diameter;resp.payload[o++]=ps.sold_feeder_remove_length;resp.payload[o++]=ps.sold_feeder_speed_length_readonly;put_u16_le(resp.payload+o,ps.sold_feeder_selectable_programs);o+=2;resp.payload[o++]=ps.sold_feeder_clogging_detection;resp.payload[o++]=ps.sold_feeder_motor_on;resp.payload[o++]=ps.sold_feeder_motor_direction;for(uint8_t pg=0;pg<5;++pg)for(uint8_t st=0;st<3;++st){put_u16_le(resp.payload+o,ps.sold_feeder_program_length[pg][st]);o+=2;}for(uint8_t pg=0;pg<5;++pg)for(uint8_t st=0;st<3;++st){put_u16_le(resp.payload+o,ps.sold_feeder_program_speed[pg][st]);o+=2;}next_sold_telemetry_stage=10;}
      } else {
        // D4/v1 carries unique grouped k26 counter extensions for ALE/CDE.
        const size_t need=61U;
        if((size_t)o+need<=MAX_PAYLOAD){resp.payload[o++]=0xD4;resp.payload[o++]=1;resp.payload[o++]=i;put_u16_le(resp.payload+o,ps.sold_special_counter_flags);o+=2;put_u32_le(resp.payload+o,ps.sold_tin_deliver_cycles);o+=4;put_u32_le(resp.payload+o,ps.sold_tin_length);o+=4;put_u32_le(resp.payload+o,ps.sold_partial_tin_deliver_cycles);o+=4;put_u32_le(resp.payload+o,ps.sold_partial_tin_length);o+=4;put_u32_le(resp.payload+o,ps.sold_cde_sold_number);o+=4;put_u32_le(resp.payload+o,ps.sold_cde_energy_delivered);o+=4;put_u32_le(resp.payload+o,ps.sold_cde_sold_total);o+=4;put_u32_le(resp.payload+o,ps.sold_cde_sold_per_min);o+=4;put_u32_le(resp.payload+o,ps.sold_cde_sold_ok);o+=4;put_u32_le(resp.payload+o,ps.sold_cde_partial_sold_number);o+=4;put_u32_le(resp.payload+o,ps.sold_cde_partial_energy_delivered);o+=4;put_u32_le(resp.payload+o,ps.sold_cde_partial_sold_total);o+=4;put_u32_le(resp.payload+o,ps.sold_cde_partial_sold_per_min);o+=4;put_u32_le(resp.payload+o,ps.sold_cde_partial_sold_ok);o+=4;next_sold_telemetry_stage=0;next_sold_telemetry_port=(uint8_t)((i+1)%max((uint8_t)1,limit));}
      }
    }
  }
  // 0.1.40+: CLM/CLMU read-only extension. D7/v5 carries the one cleaner
  // port plus station ConnectStatus. Five grouped global and five grouped
  // partial counters are uint32 values after the exact JBC DLL /60 conversion.
  if (jbc_station_kind == JBC_STATION_CL) {
    const JbcPortState& ps = jbc_ports[0];
    const size_t cl_need = 48U;
    if ((size_t)o + cl_need <= MAX_PAYLOAD) {
      resp.payload[o++] = 0xD7; resp.payload[o++] = 5; resp.payload[o++] = 1;
      resp.payload[o++] = 0; // port index
      uint16_t flags = ps.cl_flags;
      if (jbc_cl_control_mode_valid) { flags |= 0x0010U; if (jbc_cl_control_mode) flags |= 0x0020U; }
      put_u16_le(resp.payload + o, flags); o += 2;
      resp.payload[o++] = ps.cl_motors_on ? 1 : 0;
      resp.payload[o++] = ps.cl_door_open ? 1 : 0;
      put_u32_le(resp.payload+o, ps.cl_counter_plug_min); o+=4;
      put_u32_le(resp.payload+o, ps.cl_counter_cleaning_continuous_min); o+=4;
      put_u32_le(resp.payload+o, ps.cl_counter_cleaning_detection_min); o+=4;
      put_u32_le(resp.payload+o, ps.cl_counter_work_cycles); o+=4;
      put_u32_le(resp.payload+o, ps.cl_counter_door_open_cycles); o+=4;
      put_u32_le(resp.payload+o, ps.cl_partial_plug_min); o+=4;
      put_u32_le(resp.payload+o, ps.cl_partial_cleaning_continuous_min); o+=4;
      put_u32_le(resp.payload+o, ps.cl_partial_cleaning_detection_min); o+=4;
      put_u32_le(resp.payload+o, ps.cl_partial_work_cycles); o+=4;
      put_u32_le(resp.payload+o, ps.cl_partial_door_open_cycles); o+=4;
    }
  }
  // 0.1.18+: HA/JT/JTSE detail extension. 0.1.29 alternates the
  // legacy D7/v2 settings/counter record with D9/v1 JBC diagnostics so D7
  // remains byte-for-byte compatible with older Masters.
  if (jbc_station_kind == JBC_STATION_HA) {
    const JbcPortState& ps = jbc_ports[0];
    if (next_ha_telemetry_stage == 0) {
      const size_t ha_need = 3 + 54;
      if ((size_t)o + ha_need <= MAX_PAYLOAD) {
        resp.payload[o++] = 0xD7;
        resp.payload[o++] = 2;
        resp.payload[o++] = 1;
        uint16_t ha_flags = ps.ha_value_flags;
        if (jbc_ha_control_mode_valid) { ha_flags |= 0x1000U; if (jbc_ha_control_mode) ha_flags |= 0x2000U; }
        put_u16_le(resp.payload + o, ha_flags); o += 2;
        put_u16_le(resp.payload + o, ps.protection_temp); o += 2;
        put_u16_le(resp.payload + o, ps.selected_temp); o += 2;
        put_u16_le(resp.payload + o, ps.selected_flow_permille); o += 2;
        put_u16_le(resp.payload + o, ps.selected_ext_temp); o += 2;
        put_u16_le(resp.payload + o, ps.actual_ext_temp); o += 2;
        put_u16_le(resp.payload + o, (uint16_t)ps.ha_adjust_temp); o += 2;
        put_u16_le(resp.payload + o, ps.configured_time_to_stop); o += 2;
        resp.payload[o++] = ps.external_tc_mode;
        resp.payload[o++] = ps.start_mode;
        resp.payload[o++] = ps.profile_mode;
        resp.payload[o++] = ps.levels_on;
        resp.payload[o++] = ps.selected_level;
        for (uint8_t i = 0; i < 3; ++i) {
          resp.payload[o++] = ps.level_on[i];
          put_u16_le(resp.payload + o, ps.level_temp[i]); o += 2;
          put_u16_le(resp.payload + o, ps.level_flow_permille[i]); o += 2;
          put_u16_le(resp.payload + o, ps.level_ext_temp[i]); o += 2;
        }
        put_u24_sat(resp.payload + o, ps.ha_counter_plug_min); o += 3;
        put_u24_sat(resp.payload + o, ps.ha_counter_work_min); o += 3;
        put_u24_sat(resp.payload + o, ps.ha_counter_work_cycles); o += 3;
        put_u24_sat(resp.payload + o, ps.ha_counter_suction_cycles); o += 3;
        next_ha_telemetry_stage = 1;
      }
    } else if (next_ha_telemetry_stage == 1) {
      const uint8_t profile_len_raw = (uint8_t)strlen(jbc_ha_selected_profile);
      const uint8_t profile_len = min(profile_len_raw, (uint8_t)12);
      const size_t ha_diag_need = 53U + profile_len;
      if ((size_t)o + ha_diag_need <= MAX_PAYLOAD) {
        resp.payload[o++] = 0xD9;
        resp.payload[o++] = 1;
        resp.payload[o++] = 0; // port index
        put_u16_le(resp.payload + o, ps.ha_diag_flags); o += 2;
        put_u16_le(resp.payload + o, ps.ha_diag_air_temp); o += 2;
        put_u16_le(resp.payload + o, ps.ha_diag_power_permille); o += 2;
        put_u16_le(resp.payload + o, ps.ha_diag_flow_permille); o += 2;
        resp.payload[o++] = ps.ha_diag_tool;
        resp.payload[o++] = ps.ha_diag_error;
        resp.payload[o++] = ps.ha_diag_status;
        resp.payload[o++] = ps.ha_diag_heater_state;
        resp.payload[o++] = ps.ha_diag_suction_state;
        put_u24_sat(resp.payload + o, ps.ha_partial_plug_min); o += 3;
        put_u24_sat(resp.payload + o, ps.ha_partial_work_min); o += 3;
        put_u24_sat(resp.payload + o, ps.ha_partial_work_cycles); o += 3;
        put_u24_sat(resp.payload + o, ps.ha_partial_suction_cycles); o += 3;
        put_u16_le(resp.payload + o, jbc_ha_station_diag_flags); o += 2;
        resp.payload[o++] = jbc_ha_remote_mode ? 1 : 0;
        resp.payload[o++] = jbc_ha_temp_unit;
        put_u16_le(resp.payload + o, jbc_ha_max_temp); o += 2;
        put_u16_le(resp.payload + o, jbc_ha_min_temp); o += 2;
        put_u16_le(resp.payload + o, jbc_ha_max_flow); o += 2;
        put_u16_le(resp.payload + o, jbc_ha_min_flow); o += 2;
        put_u16_le(resp.payload + o, jbc_ha_max_ext_temp); o += 2;
        put_u16_le(resp.payload + o, jbc_ha_min_ext_temp); o += 2;
        memcpy(resp.payload + o, jbc_ha_robot_config, 7); o += 7;
        resp.payload[o++] = jbc_ha_robot_status;
        resp.payload[o++] = profile_len;
        if (profile_len) { memcpy(resp.payload + o, jbc_ha_selected_profile, profile_len); o += profile_len; }
        next_ha_telemetry_stage = 2;
      }
    } else {
      const size_t ha_sec_need=8U;
      if((size_t)o+ha_sec_need<=MAX_PAYLOAD){resp.payload[o++]=0xDE;resp.payload[o++]=1;resp.payload[o++]=jbc_ha_security_flags;memcpy(resp.payload+o,jbc_ha_pin,4);o+=4;resp.payload[o++]=jbc_ha_beep?1:0;next_ha_telemetry_stage=0;}
    }
  }
  // 0.1.43+: PH/Preheater complete UpdateData_PH read-only telemetry. The
  // records alternate because a full profile + teach trace can exceed one 192-byte
  // RS485 payload. E4/E5 carry scalar state, E6/E7 stream indexed profile chunks.
  if (jbc_station_kind == JBC_STATION_PH) {
    const JbcPortState& ps = jbc_ports[0];
    if (next_ph_telemetry_stage == 0) {
      const size_t need = 59U;
      if ((size_t)o + need <= MAX_PAYLOAD) {
        resp.payload[o++] = 0xE4; resp.payload[o++] = 1;
        put_u32_le(resp.payload + o, jbc_ph_station_flags); o += 4;
        put_u16_le(resp.payload + o, (uint16_t)jbc_ph_max_power); o += 2;
        put_u16_le(resp.payload + o, (uint16_t)jbc_ph_min_power); o += 2;
        put_u16_le(resp.payload + o, jbc_ph_max_temp); o += 2;
        put_u16_le(resp.payload + o, jbc_ph_min_temp); o += 2;
        memcpy(resp.payload + o, jbc_ph_pin, 4); o += 4;
        resp.payload[o++] = jbc_ph_beep ? 1 : 0;
        memcpy(resp.payload + o, jbc_ph_robot_config, 7); o += 7;
        resp.payload[o++] = jbc_ph_profile_points_setting;
        resp.payload[o++] = jbc_ph_profile_consignment;
        resp.payload[o++] = jbc_ph_profile_tc_regulation;
        put_u16_le(resp.payload + o, (uint16_t)jbc_ph_profile_teach_interval); o += 2;
        for (uint8_t tc = 0; tc < 4; ++tc) {
          resp.payload[o++] = jbc_ph_tc[tc].flags;
          put_u16_le(resp.payload + o, jbc_ph_tc[tc].actual_temp); o += 2;
          resp.payload[o++] = jbc_ph_tc[tc].warning;
          resp.payload[o++] = jbc_ph_tc[tc].mode;
          put_u16_le(resp.payload + o, jbc_ph_tc[tc].selected_temp); o += 2;
        }
        next_ph_profile_tx_index = 0;
        next_ph_teach_tx_index = 0;
        next_ph_telemetry_stage = 1;
      }
    } else if (next_ph_telemetry_stage == 1) {
      const size_t need = 70U;
      if ((size_t)o + need <= MAX_PAYLOAD) {
        resp.payload[o++] = 0xE5; resp.payload[o++] = 1; resp.payload[o++] = 0;
        put_u16_le(resp.payload + o, ps.ph_flags); o += 2;
        resp.payload[o++] = ps.ph_work_mode;
        resp.payload[o++] = ps.ph_heater_status;
        put_u32_le(resp.payload + o, ps.ph_configured_time_to_stop); o += 4;
        put_u16_le(resp.payload + o, ps.ph_selected_power); o += 2;
        resp.payload[o++] = ps.ph_active_zones;
        put_u32_le(resp.payload + o, ps.ph_counter_plug_min); o += 4;
        put_u32_le(resp.payload + o, ps.ph_counter_work_min_power); o += 4;
        put_u32_le(resp.payload + o, ps.ph_counter_work_min_temp); o += 4;
        put_u32_le(resp.payload + o, ps.ph_counter_work_min_profile); o += 4;
        put_u32_le(resp.payload + o, ps.ph_counter_work_cycles_power); o += 4;
        put_u32_le(resp.payload + o, ps.ph_counter_work_cycles_temp); o += 4;
        put_u32_le(resp.payload + o, ps.ph_counter_work_cycles_profile); o += 4;
        put_u32_le(resp.payload + o, ps.ph_partial_plug_min); o += 4;
        put_u32_le(resp.payload + o, ps.ph_partial_work_min_power); o += 4;
        put_u32_le(resp.payload + o, ps.ph_partial_work_min_temp); o += 4;
        put_u32_le(resp.payload + o, ps.ph_partial_work_min_profile); o += 4;
        put_u32_le(resp.payload + o, ps.ph_partial_work_cycles_power); o += 4;
        put_u32_le(resp.payload + o, ps.ph_partial_work_cycles_temp); o += 4;
        put_u32_le(resp.payload + o, ps.ph_partial_work_cycles_profile); o += 4;
        next_ph_telemetry_stage = 2;
      }
    } else if (next_ph_telemetry_stage == 2) {
      const uint8_t total = jbc_ph_profile_count;
      const size_t room = MAX_PAYLOAD > (size_t)o + 5U ? MAX_PAYLOAD - (size_t)o - 5U : 0U;
      const uint8_t max_points = (uint8_t)min((size_t)22U, room / 4U);
      const uint8_t remaining = next_ph_profile_tx_index < total ? (uint8_t)(total - next_ph_profile_tx_index) : 0;
      const uint8_t count = min(remaining, max_points);
      if ((size_t)o + 5U + (size_t)count * 4U <= MAX_PAYLOAD) {
        resp.payload[o++] = 0xE6; resp.payload[o++] = 1;
        resp.payload[o++] = total; resp.payload[o++] = next_ph_profile_tx_index; resp.payload[o++] = count;
        for (uint8_t i = 0; i < count; ++i) {
          const uint8_t idx = (uint8_t)(next_ph_profile_tx_index + i);
          put_u16_le(resp.payload + o, (uint16_t)jbc_ph_profile_time[idx]); o += 2;
          put_u16_le(resp.payload + o, (uint16_t)jbc_ph_profile_value[idx]); o += 2;
        }
        next_ph_profile_tx_index = (uint8_t)(next_ph_profile_tx_index + count);
        if (next_ph_profile_tx_index >= total || count == 0) next_ph_telemetry_stage = 3;
      }
    } else {
      const uint8_t total = jbc_ph_teach_count;
      const size_t room = MAX_PAYLOAD > (size_t)o + 5U ? MAX_PAYLOAD - (size_t)o - 5U : 0U;
      const uint8_t max_points = (uint8_t)min((size_t)44U, room / 2U);
      const uint8_t remaining = next_ph_teach_tx_index < total ? (uint8_t)(total - next_ph_teach_tx_index) : 0;
      const uint8_t count = min(remaining, max_points);
      if ((size_t)o + 5U + (size_t)count * 2U <= MAX_PAYLOAD) {
        resp.payload[o++] = 0xE7; resp.payload[o++] = 1;
        resp.payload[o++] = total; resp.payload[o++] = next_ph_teach_tx_index; resp.payload[o++] = count;
        for (uint8_t i = 0; i < count; ++i) {
          const uint8_t idx = (uint8_t)(next_ph_teach_tx_index + i);
          put_u16_le(resp.payload + o, (uint16_t)jbc_ph_teach_value[idx]); o += 2;
        }
        next_ph_teach_tx_index = (uint8_t)(next_ph_teach_tx_index + count);
        if (next_ph_teach_tx_index >= total || count == 0) next_ph_telemetry_stage = 0;
      }
    }
  }
  // 0.1.44+: FE/Fume Extractor complete UpdateData_FE read-only telemetry.
  // E8 is station-wide; E9 carries one port per response to stay comfortably
  // below the 192-byte OFE RS485 payload limit on F4W.
  if (jbc_station_kind == JBC_STATION_FE) {
    if (next_fe_telemetry_stage == 0) {
      const size_t need = 11U;
      if ((size_t)o + need <= MAX_PAYLOAD) {
        resp.payload[o++] = 0xE8; resp.payload[o++] = 1;
        put_u16_le(resp.payload + o, jbc_fe_station_flags); o += 2;
        memcpy(resp.payload + o, jbc_fe_robot_config, 7); o += 7;
        next_fe_telemetry_stage = 1;
      }
    } else {
      uint8_t count = jbc_port_count;
      if (count < 1 || count > JBC_MAX_PORTS) count = JBC_MAX_PORTS;
      if (next_fe_telemetry_port >= count) next_fe_telemetry_port = 0;
      const JbcPortState& ps = jbc_ports[next_fe_telemetry_port];
      const size_t need = 51U;
      if ((size_t)o + need <= MAX_PAYLOAD) {
        resp.payload[o++] = 0xE9; resp.payload[o++] = 1;
        resp.payload[o++] = next_fe_telemetry_port;
        put_u16_le(resp.payload + o, ps.fe_flags); o += 2;
        put_u16_le(resp.payload + o, ps.fe_time_to_stop_work); o += 2;
        put_u16_le(resp.payload + o, ps.fe_time_to_stop_stand); o += 2;
        resp.payload[o++] = ps.fe_pedal_action;
        resp.payload[o++] = ps.fe_pedal_mode;
        put_u32_le(resp.payload + o, ps.fe_counter_plug_min); o += 4;
        put_u32_le(resp.payload + o, ps.fe_counter_idle_min); o += 4;
        put_u32_le(resp.payload + o, ps.fe_counter_work_intake_min); o += 4;
        put_u32_le(resp.payload + o, ps.fe_counter_stand_intake_min); o += 4;
        put_u32_le(resp.payload + o, ps.fe_counter_work_cycles); o += 4;
        put_u32_le(resp.payload + o, ps.fe_partial_plug_min); o += 4;
        put_u32_le(resp.payload + o, ps.fe_partial_idle_min); o += 4;
        put_u32_le(resp.payload + o, ps.fe_partial_work_intake_min); o += 4;
        put_u32_le(resp.payload + o, ps.fe_partial_stand_intake_min); o += 4;
        put_u32_le(resp.payload + o, ps.fe_partial_work_cycles); o += 4;
        next_fe_telemetry_port = (uint8_t)((next_fe_telemetry_port + 1U) % max((uint8_t)1, count));
        next_fe_telemetry_stage = 0;
      }
    }
  }
  // 0.1.45+: SF/Solder Feeder complete UpdateData_SF read-only telemetry.
  // EA = station settings + 35-byte concatenation list, EB = the single port
  // including global/partial counters, EC = chunks of the 35 program records.
  if (jbc_station_kind == JBC_STATION_SF) {
    if (next_sf_telemetry_stage == 0) {
      const size_t need = 51U;
      if ((size_t)o + need <= MAX_PAYLOAD) {
        resp.payload[o++] = 0xEA; resp.payload[o++] = 1;
        put_u16_le(resp.payload + o, jbc_sf_station_flags); o += 2;
        memcpy(resp.payload + o, jbc_sf_pin, 4); o += 4;
        resp.payload[o++] = jbc_sf_length_unit;
        memcpy(resp.payload + o, jbc_sf_robot_config, 7); o += 7;
        memcpy(resp.payload + o, jbc_sf_program_list, JBC_SF_PROGRAM_COUNT); o += JBC_SF_PROGRAM_COUNT;
        next_sf_telemetry_stage = 1;
      }
    } else if (next_sf_telemetry_stage == 1) {
      const JbcPortState& ps = jbc_ports[0];
      const size_t need = 62U;
      if ((size_t)o + need <= MAX_PAYLOAD) {
        resp.payload[o++] = 0xEB; resp.payload[o++] = 1; resp.payload[o++] = 0;
        put_u16_le(resp.payload + o, ps.sf_flags); o += 2;
        put_u16_le(resp.payload + o, ps.sf_speed_tenth_mm_s); o += 2;
        put_u16_le(resp.payload + o, ps.sf_length_tenth_mm); o += 2;
        resp.payload[o++] = ps.sf_feeding_state;
        put_u16_le(resp.payload + o, ps.sf_feeding_value_raw); o += 2;
        resp.payload[o++] = ps.sf_feeding_selected_program;
        resp.payload[o++] = ps.sf_current_program_step;
        put_u64_le(resp.payload + o, ps.sf_counter_tin_length); o += 8;
        put_u32_le(resp.payload + o, ps.sf_counter_plug_min); o += 4;
        put_u32_le(resp.payload + o, ps.sf_counter_work_min); o += 4;
        put_u32_le(resp.payload + o, ps.sf_counter_idle_min); o += 4;
        put_u32_le(resp.payload + o, ps.sf_counter_work_cycles); o += 4;
        put_u64_le(resp.payload + o, ps.sf_partial_tin_length); o += 8;
        put_u32_le(resp.payload + o, ps.sf_partial_plug_min); o += 4;
        put_u32_le(resp.payload + o, ps.sf_partial_work_min); o += 4;
        put_u32_le(resp.payload + o, ps.sf_partial_idle_min); o += 4;
        put_u32_le(resp.payload + o, ps.sf_partial_work_cycles); o += 4;
        next_sf_telemetry_stage = 2;
        next_sf_program_tx_index = 0;
      }
    } else {
      const uint8_t total = JBC_SF_PROGRAM_COUNT;
      const size_t room = MAX_PAYLOAD > (size_t)o + 5U ? MAX_PAYLOAD - (size_t)o - 5U : 0U;
      const uint8_t max_programs = (uint8_t)min((size_t)8U, room / 21U);
      const uint8_t remaining = next_sf_program_tx_index < total ? (uint8_t)(total - next_sf_program_tx_index) : 0;
      const uint8_t count = min(remaining, max_programs);
      if ((size_t)o + 5U + (size_t)count * 21U <= MAX_PAYLOAD) {
        resp.payload[o++] = 0xEC; resp.payload[o++] = 1;
        resp.payload[o++] = total; resp.payload[o++] = next_sf_program_tx_index; resp.payload[o++] = count;
        for (uint8_t i=0; i<count; ++i) {
          const SfProgramState& pg = jbc_sf_programs[next_sf_program_tx_index + i];
          resp.payload[o++] = pg.flags;
          char name8[8] = {' ',' ',' ',' ',' ',' ',' ',' '};
          const size_t n = min((size_t)8U, strlen(pg.name)); if (n) memcpy(name8, pg.name, n);
          memcpy(resp.payload + o, name8, 8); o += 8;
          for (uint8_t st=0; st<3; ++st) { put_u16_le(resp.payload + o, pg.length[st]); o+=2; put_u16_le(resp.payload + o, pg.speed[st]); o+=2; }
        }
        next_sf_program_tx_index = (uint8_t)(next_sf_program_tx_index + count);
        if (next_sf_program_tx_index >= total || count == 0) { next_sf_program_tx_index = 0; next_sf_telemetry_stage = 0; }
      }
    }
  }
  // 1.1.53+: safe public Read... completion suffix. ReadFile*/bootloader and
  // destructive reset/read-and-clear commands are deliberately excluded.
  // ED/v1: family 1=FE (one port + station service values), 2=PH, 3=SF.
  if (jbc_station_kind == JBC_STATION_FE) {
    uint8_t count=jbc_port_count; if(count<1||count>JBC_MAX_PORTS)count=JBC_MAX_PORTS;
    const uint8_t pi=next_fe_telemetry_port<count?next_fe_telemetry_port:0; const JbcPortState& ps=jbc_ports[pi];
    const size_t need=27U; if((size_t)o+need<=MAX_PAYLOAD){
      resp.payload[o++]=0xED;resp.payload[o++]=1;resp.payload[o++]=1;resp.payload[o++]=pi;
      put_u16_le(resp.payload+o,jbc_fe_service_flags);o+=2; put_u16_le(resp.payload+o,jbc_fe_flow_x_mil);o+=2;
      put_u16_le(resp.payload+o,jbc_fe_speed_rpm);o+=2; put_u16_le(resp.payload+o,jbc_fe_selected_flow_x_mil);o+=2;
      put_u16_le(resp.payload+o,jbc_fe_filter_status);o+=2; memcpy(resp.payload+o,jbc_fe_pin,4);o+=4;resp.payload[o++]=jbc_fe_beep?1:0;
      put_u16_le(resp.payload+o,ps.fe_service_flags);o+=2;resp.payload[o++]=ps.fe_stand_intakes;
      put_u16_le(resp.payload+o,ps.fe_suction_delay_work);o+=2;put_u16_le(resp.payload+o,ps.fe_suction_delay_stand);o+=2;resp.payload[o++]=ps.fe_pedal_connected;
    }
  } else if (jbc_station_kind == JBC_STATION_PH) {
    const size_t need=8U;if((size_t)o+need<=MAX_PAYLOAD){resp.payload[o++]=0xED;resp.payload[o++]=1;resp.payload[o++]=2;
      resp.payload[o++]=(jbc_ph_station_flags&0x00010000UL)?1:0;resp.payload[o++]=jbc_ph_remote_mode?1:0;
      resp.payload[o++]=jbc_continuous_valid?1:0;resp.payload[o++]=jbc_continuous_speed;resp.payload[o++]=jbc_continuous_ports;}
  } else if (jbc_station_kind == JBC_STATION_SF) {
    const size_t need=6U;if((size_t)o+need<=MAX_PAYLOAD){resp.payload[o++]=0xED;resp.payload[o++]=1;resp.payload[o++]=3;
      resp.payload[o++]=jbc_continuous_valid?1:0;resp.payload[o++]=jbc_continuous_speed;resp.payload[o++]=jbc_continuous_ports;}
  }
  // 0.1.21+: common station Settings.Name suffix. Keep it after the existing
  // D7 station-specific extension so older Masters still parse D7 unchanged.
  // Marker D6, version 1, length, UTF-8/ASCII bytes (max 16 per JBC DLL).
  const uint8_t station_name_len = (uint8_t)strlen(jbc_station_name);
  const uint8_t station_name_n = station_name_len > 16U ? (uint8_t)16 : station_name_len;
  if ((size_t)o + 3U + station_name_n <= MAX_PAYLOAD) {
    resp.payload[o++] = 0xD6;
    resp.payload[o++] = 1;
    resp.payload[o++] = station_name_n;
    if (station_name_n) { memcpy(resp.payload + o, jbc_station_name, station_name_n); o += station_name_n; }
  }
  // 1.1.55+: DLL-aligned JBC diagnostics. Preserve the legacy fixed-prefix
  // counters above, then append a tagged extension so older Masters can ignore it.
  // EE/v1 = transport frame errors, payload/decode errors, handshake errors.
  if ((size_t)o + 8U <= MAX_PAYLOAD) {
    resp.payload[o++] = 0xEE;
    resp.payload[o++] = 1;
    put_u16_le(resp.payload + o, (uint16_t)min(jbc_frame_errors, 65535UL)); o += 2;
    put_u16_le(resp.payload + o, (uint16_t)min(jbc_decode_errors, 65535UL)); o += 2;
    put_u16_le(resp.payload + o, (uint16_t)min(jbc_handshake_errors, 65535UL)); o += 2;
  }
  // 1.1.56+: EF/v1 identifies where payload/decode mismatches originate.
  // Keep the payload compact: last mismatch plus the three most frequent CMDs.
  if ((size_t)o + 16U <= MAX_PAYLOAD) {
    uint8_t top_cmd[3] = {0,0,0};
    uint32_t top_count[3] = {0,0,0};
    for (uint16_t cmd = 0; cmd < 256U; ++cmd) {
      const uint32_t count = jbc_decode_cmd_errors[cmd];
      if (!count) continue;
      for (uint8_t rank = 0; rank < 3; ++rank) {
        if (count > top_count[rank]) {
          for (uint8_t move = 2; move > rank; --move) { top_count[move] = top_count[move-1]; top_cmd[move] = top_cmd[move-1]; }
          top_count[rank] = count; top_cmd[rank] = (uint8_t)cmd;
          break;
        }
      }
    }
    resp.payload[o++] = 0xEF;
    resp.payload[o++] = 1;
    resp.payload[o++] = jbc_decode_last_cmd;
    resp.payload[o++] = jbc_decode_last_got_len;
    resp.payload[o++] = jbc_decode_last_expected_min;
    resp.payload[o++] = jbc_decode_last_expected_max;
    resp.payload[o++] = 3;
    for (uint8_t rank = 0; rank < 3; ++rank) {
      resp.payload[o++] = top_cmd[rank];
      put_u16_le(resp.payload + o, (uint16_t)min(top_count[rank], 65535UL)); o += 2;
    }
  }
  resp.len = o;
  bus.send(resp);
}

static void rs485_trace_control(const Frame& req) {
  if (req.len < 1) { rs485_status_response(req, STATUS_BAD_LEN); return; }
  // Shared local-trace contract: bit0=enable, bit1=clear.  The Master starts a
  // local session with 0x03 (enable+clear), so treating the byte as an enum
  // made JBC USB the odd module out and caused Bus Diagnose start to fail.
  const uint8_t flags = req.payload[0];
  local_trace_enabled = (flags & 0x01U) != 0;
  if (flags & 0x02U) local_trace_clear();
  rs485_status_response(req, STATUS_OK);
}
static void rs485_trace_read(const Frame& req) {
  Frame resp;
  resp.dst = req.src; resp.src = module_addr; resp.seq = req.seq; resp.cmd = CMD_TRACE_READ | 0x80;
  uint8_t o = 0;
  resp.payload[o++] = STATUS_OK;
  resp.payload[o++] = local_trace_enabled ? 1 : 0;
  put_u16_le(resp.payload + o, local_trace_dropped); o += 2;
  const uint8_t count_pos = o++;
  uint8_t sent = 0;
  while (local_trace_count && o + 6U <= MAX_PAYLOAD) {
    const uint8_t pos = (uint8_t)((local_trace_head + LOCAL_TRACE_CAPACITY - local_trace_count) % LOCAL_TRACE_CAPACITY);
    const LocalTraceEvent& ev = local_trace[pos];
    const uint32_t age = millis() - ev.ms;
    const uint16_t age_ms = age > 0xFFFFUL ? 0xFFFFU : (uint16_t)age;
    const uint8_t n = min(ev.len, (uint8_t)LOCAL_TRACE_PREVIEW);
    if ((size_t)o + 6U + n > MAX_PAYLOAD) break;
    put_u16_le(resp.payload + o, age_ms); o += 2;
    resp.payload[o++] = ev.dir;
    resp.payload[o++] = ev.meta1;
    resp.payload[o++] = ev.meta2;
    resp.payload[o++] = n;
    if (n) memcpy(resp.payload + o, ev.data, n);
    o += n;
    --local_trace_count;
    ++sent;
  }
  resp.payload[count_pos] = sent;
  resp.len = o;
  // dropped is a delta counter for the Master, not a boot-total.  Reset only
  // after it has been included in a successful TRACE_READ response payload.
  local_trace_dropped = 0;
  bus.send(resp);
}

static void rs485_fw_begin(const Frame& req) {
  if (req.len < 4) { rs485_status_response(req, STATUS_BAD_LEN); return; }
  const uint32_t size = get_u32_le(req.payload);
  if (!Update.begin(size ? size : UPDATE_SIZE_UNKNOWN)) { fw_update_active = false; rs485_status_response(req, STATUS_BUSY); return; }
  fw_update_active = true; fw_update_offset = 0; fw_update_buffer_reset(); fw_update_touch();
  rs485_status_response(req, STATUS_OK);
}
static void rs485_fw_chunk(const Frame& req) {
  if (!fw_update_active) { rs485_status_response(req, STATUS_BUSY); return; }
  if (req.len < 5) { fw_update_abort_local(); rs485_status_response(req, STATUS_BAD_LEN); return; }
  const uint32_t offset = get_u32_le(req.payload);
  const uint8_t n = req.len - 4;
  if (offset != fw_update_offset) {
    if (offset < fw_update_offset && offset + n <= fw_update_offset) { fw_update_touch(); rs485_status_response(req, STATUS_OK); return; }
    fw_update_abort_local(); rs485_status_response(req, STATUS_BAD_VALUE); return;
  }
  if (!fw_update_buffer_append(req.payload + 4, n)) { fw_update_abort_local(); rs485_status_response(req, STATUS_BUSY); return; }
  fw_update_offset += n; fw_update_touch(); rs485_status_response(req, STATUS_OK);
}
static void rs485_fw_end(const Frame& req) {
  if (!fw_update_active) { rs485_status_response(req, STATUS_BUSY); return; }
  const bool ok = fw_update_buffer_flush() && Update.end(true);
  fw_update_active = false; rs485_status_response(req, ok ? STATUS_OK : STATUS_BUSY);
  if (ok) { delay(300); ESP.restart(); }
}
static void rs485_fw_status(const Frame& req) {
  Frame resp;
  resp.dst=req.src; resp.src=module_addr; resp.seq=req.seq; resp.cmd=CMD_FW_STATUS|0x80; resp.len=6;
  resp.payload[0]=STATUS_OK; resp.payload[1]=fw_update_active?1:0; put_u32_le(resp.payload+2,fw_update_offset); bus.send(resp);
}

static uint32_t discover_delay_ms(const Frame& req) {
  const uint64_t uid = module_uid();
  const uint8_t round = req.len ? req.payload[0] : req.seq;
  uint32_t mix = (uint32_t)uid ^ (uint32_t)(uid >> 32) ^ 0x9E3779B9UL ^ ((uint32_t)round * 0x85EBCA6BUL);
  mix ^= mix >> 16;
  return 5UL + (mix & 0x3FUL) * 6UL;
}
static void send_discover_response(uint8_t dst, uint8_t seq) {
  Frame resp;
  resp.dst=dst; resp.src=module_addr; resp.seq=seq; resp.cmd=CMD_DISCOVER_MODULES|0x80;
  size_t o=0; resp.payload[o++]=STATUS_OK; resp.payload[o++]=MODULE_JBC_USB;
  put_u64_le(resp.payload+o,module_uid()); o+=8; resp.payload[o++]=module_addr;
  resp.payload[o++]=FW_MAJOR; resp.payload[o++]=FW_MINOR; resp.payload[o++]=FW_PATCH;
  put_u32_le(resp.payload+o,MODULE_CAPS); o+=4; resp.len=(uint8_t)o; bus.send(resp);
}
static void rs485_discover(const Frame& req) {
  if (fw_update_active) return;
  discover_response_dst=req.src; discover_response_seq=req.seq;
  discover_response_due_ms=millis()+discover_delay_ms(req); discover_response_pending=true;
}
static void poll_pending_discover_response() {
  if (!discover_response_pending || (int32_t)(millis()-discover_response_due_ms)<0) return;
  discover_response_pending=false; send_discover_response(discover_response_dst,discover_response_seq);
}
static uint32_t join_delay_ms(uint8_t round) {
  uint32_t mix=(uint32_t)module_uid()^(uint32_t)(module_uid()>>32)^((uint32_t)round*0x9E3779B9UL); mix^=mix>>16;
  return 300UL+(mix%900UL);
}
static void poll_join_announce() {
  if (!join_announce_left || (int32_t)(millis()-next_join_announce_ms)<0) return;
  send_discover_response(ADDR_MASTER,0); --join_announce_left; next_join_announce_ms=millis()+join_delay_ms(join_announce_left);
}

static void rs485_set_address_uid(const Frame& req) {
  if (req.len < 9) return;
  if (get_u64_le(req.payload) != module_uid()) return;
  const uint8_t next = req.payload[8];
  if (!valid_module_addr(next)) { rs485_status_response(req, STATUS_BAD_VALUE); return; }
  prefs.putUChar("addr", next); rs485_status_response(req, STATUS_OK); delay(20); module_addr=next;
}

static void rs485_jbc_usb_config(const Frame& req) {
  if (req.len < 1U) { rs485_status_response(req, STATUS_BAD_LEN); return; }
  if (req.payload[0] != JBC_USB_CONFIG_STATION_NAME) {
    if (jbc_link_state != JBC_LINK_ACTIVE) {
      rs485_status_response(req, STATUS_BUSY);
      return;
    }
    if (jbc_config_write_queued || jbc_config_write_inflight ||
        jbc_config_write_state != JBC_CONFIG_WRITE_IDLE ||
        jbc_station_name_write_queued || jbc_station_name_write_inflight ||
        jbc_station_name_write_state != JBC_NAME_WRITE_IDLE) {
      rs485_status_response(req, STATUS_BUSY);
      return;
    }
    if (!jbc_config_prepare(req.payload, req.len)) {
      rs485_status_response(req, STATUS_BAD_VALUE);
      return;
    }
    rs485_status_response(req, STATUS_OK);
    return;
  }
  const uint8_t len = req.len - 1U;
  if (len > 16U) { rs485_status_response(req, STATUS_BAD_LEN); return; }
  if (jbc_link_state != JBC_LINK_ACTIVE) {
    rs485_status_response(req, STATUS_BUSY);
    return;
  }
  if (!jbc_station_name_write_command()) {
    rs485_status_response(req, STATUS_NOT_SUPPORTED);
    return;
  }
  if (!jbc_station_name_valid((const char*)req.payload + 1, len)) {
    rs485_status_response(req, STATUS_BAD_VALUE);
    return;
  }
  if (jbc_station_name_write_queued || jbc_station_name_write_inflight ||
      jbc_station_name_write_state != JBC_NAME_WRITE_IDLE ||
      jbc_config_write_queued || jbc_config_write_inflight ||
      jbc_config_write_state != JBC_CONFIG_WRITE_IDLE) {
    rs485_status_response(req, STATUS_BUSY);
    return;
  }
  if (len) memcpy(jbc_station_name_write_value, req.payload + 1, len);
  jbc_station_name_write_value[len] = 0;
  jbc_station_name_write_queued = true;
  rs485_status_response(req, STATUS_OK);
}

static uint8_t status_led_sync_brightness(uint8_t pct) {
  pct = constrain(pct, (uint8_t)10, (uint8_t)100);
  return (uint8_t)((uint16_t)pct * 255U / 100U);
}
static void handle_led_sync(const Frame& req) {
  if (req.src == ADDR_MASTER) last_master_ms = millis();
  if (req.len >= 4) ofe_status_leds.syncClock(get_u32_le(req.payload));
  if (req.len >= 6) ofe_status_leds.setBrightness(req.payload[4] ? status_led_sync_brightness(req.payload[5]) : 0);
}

static void handle_rs485(const Frame& req) {
  if (req.dst != module_addr && req.dst != ADDR_BROADCAST) return;
  if (req.dst == ADDR_BROADCAST && req.cmd == CMD_LED_SYNC) { handle_led_sync(req); return; }
  if (req.src == ADDR_MASTER) last_master_ms = millis();
  if (req.dst == ADDR_BROADCAST) {
    if (req.cmd == CMD_DISCOVER_MODULES) rs485_discover(req);
    else if (req.cmd == CMD_SET_ADDRESS_UID) rs485_set_address_uid(req);
    return;
  }
  switch (req.cmd) {
    case CMD_PING: rs485_status_response(req, STATUS_OK); break;
    case CMD_INFO: rs485_info(req); break;
    case CMD_GET_CAPS: rs485_caps(req); break;
    case CMD_FAST_POLL: rs485_fast_poll(req); break;
    case CMD_GET_STATE: rs485_get_state(req); break;
    case CMD_GET_TELEMETRY: rs485_get_telemetry(req); break;
    case CMD_SET_LABEL: rs485_set_label(req); break;
    case CMD_JBC_USB_CONFIG: rs485_jbc_usb_config(req); break;
    case CMD_TRACE_CONTROL: rs485_trace_control(req); break;
    case CMD_TRACE_READ: rs485_trace_read(req); break;
    case CMD_FW_BEGIN: rs485_fw_begin(req); break;
    case CMD_FW_CHUNK: rs485_fw_chunk(req); break;
    case CMD_FW_END: rs485_fw_end(req); break;
    case CMD_FW_ABORT: fw_update_abort_local(); rs485_status_response(req, STATUS_OK); break;
    case CMD_FW_STATUS: rs485_fw_status(req); break;
    case CMD_FW_REBOOT: rs485_status_response(req, STATUS_OK); delay(100); ESP.restart(); break;
    case CMD_SET_ADDRESS:
      if (req.len != 1 || !valid_module_addr(req.payload[0])) rs485_status_response(req, STATUS_BAD_VALUE);
      else { const uint8_t next=req.payload[0]; prefs.putUChar("addr",next); rs485_status_response(req,STATUS_OK); delay(20); module_addr=next; }
      break;
    default: rs485_status_response(req, STATUS_UNKNOWN_CMD); break;
  }
}
static void poll_rs485() {
  Frame req; uint8_t frames=0;
  while (frames<8 && bus.poll(req)) { handle_rs485(req); ++frames; }
  if (frames>=8) yield();
}

// -----------------------------------------------------------------------------
// Arduino entry points
// -----------------------------------------------------------------------------
void setup() {
  ofe_keep_module_fw_signature();
  ofe_status_leds.begin();
  bus.setActivityCallback([](){ ofe_status_leds.pulseBusActivity(); });
  Serial.begin(115200);
  delay(250);

#if !CONFIG_IDF_TARGET_ESP32S3
  Serial.println("ERROR: JbcUsbModule requires ESP32-S3 USB-OTG host hardware.");
#endif

  RS485.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  prefs.begin("jbc-usb", false);
  module_addr = prefs.getUChar("addr", DEFAULT_MODULE_ADDR);
  if (!valid_module_addr(module_addr)) { module_addr=DEFAULT_MODULE_ADDR; prefs.putUChar("addr",module_addr); }
  prefs.getString("label", module_label, sizeof(module_label));

  Serial.println("JBC USB / CP210x OFE module");
  Serial.print("OFE addr=0x"); Serial.println(module_addr, HEX);
  Serial.println("JBC USB: CP210x 500000 8E1, automatic P01/P02 + safe missing Device-ID provisioning");
  if (!start_usb_host()) Serial.println("USB host init FAILED");
  else Serial.println("USB host ready; waiting for CP210x");

  join_announce_left = 8;
  next_join_announce_ms = millis() + join_delay_ms(join_announce_left);
}

void loop() {
  const uint32_t loop_start_us = micros();
  const uint32_t now = millis();
  const bool bus_online = last_master_ms && (uint32_t)(now-last_master_ms) <= OFE_STATUS_LED_MASTER_TIMEOUT_MS;
  const bool jbc_online = (fast_flags & FAST_FLAG_CONNECTED) && jbc_link_state == JBC_LINK_ACTIVE;
  ofe_status_leds.setBusOnline(bus_online);
  ofe_status_leds.setFirmwareUpdate(fw_update_active);
  ofe_status_leds.setModuleEvent(work_mask ? OFE_LED_EVENT_WORK_ACTIVE : (jbc_online ? OFE_LED_EVENT_DEVICE_ONLINE : OFE_LED_EVENT_WARNING));
  ofe_status_leds.tick();

  // OFE RS485 is the real-time control path. Service it before potentially
  // blocking USB/CP210x work and once again afterwards so a request that arrived
  // during USB handling does not have to wait for the next loop iteration.
  poll_rs485();


  poll_usb_transport();
  poll_jbc_rx();
  poll_jbc_protocol();
  poll_rs485();
  poll_pending_discover_response();
  poll_join_announce();
  fw_update_check_timeout();
  record_loop_time((uint32_t)(micros() - loop_start_us));
  // Exclude idle time from loop_max_ms, consistent with the other modules.
  delay(1);
}
