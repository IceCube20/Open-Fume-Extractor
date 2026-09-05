#pragma once

// Master-wide singleton objects and Web/MQTT/update state.
static HardwareSerial RS485(1);
static Link rs485_link(RS485);
static ModuleRegistry registry;
static ExtractorLogic extractor;
static MasterScheduler scheduler(rs485_link, registry, extractor);
static OfeStatusLed ofe_status_leds;
static TaskHandle_t master_loop_task_handle = nullptr;
#if WEB_ENABLE
static WebServer web(80);
static DNSServer dns;
static Preferences net_prefs;
static TaskHandle_t web_service_task_handle = nullptr;

static bool master_extmem_malloc_enabled = false;
static uint32_t master_extmem_malloc_threshold = MASTER_EXTMEM_MALLOC_THRESHOLD;
static uint32_t master_psram_total_at_boot = 0;
static uint32_t master_psram_free_after_policy = 0;

// `master_prefs` is shared by the Arduino loop, HTTP task and MQTT task.
// Preferences stores an NVS handle inside the object, so overlapping
// begin()/end() calls on one instance must be serialized.
class SerializedPreferences : public Preferences {
public:
  bool begin(const char* name, bool readOnly = false, const char* partition_label = nullptr) {
    if (!mutex_) {
      mutex_ = xSemaphoreCreateRecursiveMutexStatic(&mutex_storage_);
      if (!mutex_) return false;
    }

    const uint32_t wait_start_us = micros();
    if (xSemaphoreTakeRecursive(mutex_, 0) != pdTRUE) {
      ++contention_count_;
      if (xSemaphoreTakeRecursive(mutex_, portMAX_DELAY) != pdTRUE) return false;
    }
    const uint32_t waited_us = (uint32_t)(micros() - wait_start_us);
    if (waited_us > max_wait_us_) max_wait_us_ = waited_us;

    owner_ = xTaskGetCurrentTaskHandle();
    ++lock_depth_;
    ++begin_count_;

    const bool ok = Preferences::begin(name, readOnly, partition_label);
    if (!ok) {
      --lock_depth_;
      if (!lock_depth_) owner_ = nullptr;
      xSemaphoreGiveRecursive(mutex_);
    }
    return ok;
  }

  void end() {
    Preferences::end();
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    if (mutex_ && owner_ == current && lock_depth_) {
      --lock_depth_;
      if (!lock_depth_) owner_ = nullptr;
      xSemaphoreGiveRecursive(mutex_);
    }
  }

  uint32_t contentionCount() const { return contention_count_; }
  uint32_t maxWaitUs() const { return max_wait_us_; }
  uint32_t beginCount() const { return begin_count_; }

private:
  SemaphoreHandle_t mutex_ = nullptr;
  StaticSemaphore_t mutex_storage_;
  TaskHandle_t owner_ = nullptr;
  uint16_t lock_depth_ = 0;
  volatile uint32_t contention_count_ = 0;
  volatile uint32_t max_wait_us_ = 0;
  volatile uint32_t begin_count_ = 0;
};

static SerializedPreferences master_prefs;
static bool logic_fs_ready = false;
static bool captive_active = false;
static char wifi_ssid[33] = WIFI_SSID;
static char wifi_password[65] = WIFI_PASSWORD;
static char web_auth_user[33] = WEB_AUTH_USER;
static char web_auth_password[65] = WEB_AUTH_PASSWORD;
static char web_lang[3] = "de";
static char master_hostname[32] = "open-fume-extractor";
static char master_ap_ssid[33] = "OpenFume";
static char master_bootstrap_password[24] = {0};
static char master_ap_password[24] = {0};
static bool web_password_change_required = false;
static char master_device_id[40] = "open-fume-extractor";
static bool status_led_enabled = true;
static uint8_t status_led_brightness_pct = 20;
static bool wifi_static_enabled = false;
static IPAddress wifi_static_ip(0, 0, 0, 0);
static IPAddress wifi_static_gateway(0, 0, 0, 0);
static IPAddress wifi_static_subnet(255, 255, 255, 0);
static IPAddress wifi_static_dns1(0, 0, 0, 0);
static IPAddress wifi_static_dns2(0, 0, 0, 0);
static bool mqtt_enabled = false;
static bool mqtt_tls_enabled = false;
static bool mqtt_ha_discovery = true;
static char mqtt_host[65] = {0};
static uint16_t mqtt_port = 1883;
static char mqtt_user[65] = {0};
static char mqtt_password[65] = {0};
static char mqtt_base_topic[65] = "open-fume-extractor";
static char mqtt_discovery_prefix[33] = "homeassistant";
static String mqtt_ca_cert;
static bool mqtt_tls_verify_enabled = false;
static WiFiClient mqtt_plain_client;
static WiFiClientSecure mqtt_tls_client;
static PubSubClient mqtt_client;
static StaticSemaphore_t mqtt_config_mutex_storage;
static SemaphoreHandle_t mqtt_config_mutex = nullptr;
static volatile bool mqtt_reconfigure_requested = false;

class MqttConfigGuard {
public:
  MqttConfigGuard() {
    if (!mqtt_config_mutex) mqtt_config_mutex = xSemaphoreCreateRecursiveMutexStatic(&mqtt_config_mutex_storage);
    locked_ = mqtt_config_mutex && xSemaphoreTakeRecursive(mqtt_config_mutex, portMAX_DELAY) == pdTRUE;
  }
  ~MqttConfigGuard() {
    if (locked_) xSemaphoreGiveRecursive(mqtt_config_mutex);
  }
  bool locked() const { return locked_; }
private:
  bool locked_ = false;
};
static bool mqtt_client_tls_active = false;
static uint32_t mqtt_next_connect_ms = 0;
static uint32_t mqtt_last_publish_ms = 0;
static bool mqtt_was_connected = false;
static bool mqtt_discovery_published = false;
static bool mqtt_discovery_publish_failed = false;
static int mqtt_last_state = 0;
static String mqtt_discovery_signature;
static uint32_t mqtt_next_discovery_check_ms = 0;
static bool wifi_sta_pending = false;
static bool wifi_time_configured = false;
static uint32_t wifi_sta_start_ms = 0;
static uint32_t wifi_next_retry_ms = 0;
static bool master_update_ok = false;
static bool master_chunk_ok = false;
static bool master_update_active = false;
static volatile bool module_update_ok = false;
static volatile bool module_chunk_ok = false;
static uint32_t master_update_offset = 0;
static uint32_t master_update_size = 0;
static uint8_t master_update_progress = 0;
static uint32_t master_update_speed_bps = 0;
static uint32_t master_update_speed_sample_ms = 0;
static uint32_t master_update_speed_sample_offset = 0;
static volatile uint8_t module_update_addr = 0;
static volatile uint32_t module_update_offset = 0;
static volatile uint32_t module_update_size = 0;
static volatile uint8_t module_update_progress = 0;
static volatile uint32_t module_update_speed_bps = 0;
static volatile uint32_t module_update_speed_sample_ms = 0;
static volatile uint32_t module_update_speed_sample_offset = 0;
static volatile uint32_t module_update_queued_offset = 0;
static uint8_t module_update_queue[MODULE_FW_QUEUE_SIZE];
static uint8_t module_update_wifi_bulk[1024];
static volatile size_t module_update_queue_head = 0;
static volatile size_t module_update_queue_tail = 0;
static volatile size_t module_update_queue_count = 0;
static volatile size_t module_update_queue_low_water = 0;
static volatile uint32_t module_update_queue_empty_polls = 0;
static volatile uint32_t module_update_frames_sent = 0;
static volatile uint32_t module_update_http_chunks = 0;
static volatile uint32_t module_update_last_http_ms = 0;
static volatile uint32_t module_update_max_http_gap_ms = 0;
static volatile uint32_t module_update_last_ack_ms = 0;
static volatile uint32_t module_update_max_ack_ms = 0;
static volatile uint32_t module_update_starve_count = 0;
static volatile uint32_t module_update_starve_since_ms = 0;
static volatile uint32_t module_update_starve_max_ms = 0;
static volatile uint32_t module_update_last_pump_ms = 0;
static volatile uint32_t module_update_last_pump_gap_ms = 0;
static volatile uint32_t module_update_max_pump_gap_ms = 0;
static volatile bool module_update_stream_started = false;
static portMUX_TYPE module_update_queue_mux = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t module_update_io_mutex_storage;
static SemaphoreHandle_t module_update_io_mutex = nullptr;
static char update_status_msg[96] = {0};
static bool module_update_unsafe_fw_type = false;
static bool module_update_signature_checked = false; // image-header check
static bool master_update_unsafe_fw_type = false;
static OfeFirmwareAuthVerifier master_update_auth;
static OfeFirmwareAuthVerifier module_update_auth;

// Embedded OFE signature verification against the actual uploaded BIN bytes.
static bool master_update_payload_signature_verified = false;
static bool master_update_header_checked = false;
static uint32_t master_update_signature_probe_bytes = 0;
static uint8_t master_update_signature_overlap[96] = {0};
static uint8_t master_update_signature_overlap_len = 0;

static bool module_update_payload_signature_verified = false;
static uint32_t module_update_signature_probe_bytes = 0;
static uint8_t module_update_signature_overlap[96] = {0};
static uint8_t module_update_signature_overlap_len = 0;
static char module_update_expected_signature[72] = {0};

struct MqttUniversalEntityDef {
  bool valid = false;
  uint8_t id = 0;
  String type;
  String key;
  String mode;
  String label;
  String unit;
  String min_value;
  String max_value;
  String step;
  String options;
  String values;
  String role;
  String access;
};

struct MasterAlarmJson {
  uint8_t count = 0;
  String text;
  String strings_json = "[]";
  String items_json = "[]";
};
#endif
